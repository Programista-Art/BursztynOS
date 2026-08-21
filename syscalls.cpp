/*
 * Bursztyn OS - Bursztynowe Wywolania Systemowe (BWS)
 *
 * Punkt C++ obslugujacy wywolania Ring 3 przychodzace przez SYSCALL/LSTAR.
 *
 * Najwazniejsze zasady bezpieczenstwa:
 *
 *  1. Wskaznik Ring 3 nigdy nie jest bezposrednio przekazywany do kodu
 *     plikow/sieci/RTC bez walidacji albo kopii user<->kernel.
 *
 *  2. Uprawnienia PZB sa sprawdzane dla kazdej klasy operacji.
 *
 *  3. Program uruchamiany przez Ring 3 nie moze otrzymac bardziej
 *     zaufanego poziomu ani szerszych praw niz proces rodzic.
 *
 *  4. Operacje reset/shutdown wymagaja PRAWO_SYSTEM_CONFIG i poziomu
 *     co najmniej PZB_ZAUFANE.
 *
 *  5. Sterta Ring 3 jest ograniczona do prywatnego okna 256 MiB.
 *
 *  6. IA32_GS_BASE / IA32_KERNEL_GS_BASE sa ustawione zgodnie z klasyczna
 *     polityka SWAPGS:
 *
 *       Ring 0: GS_BASE        = kernelowy per-CPU / stos SYSCALL
 *               KERNEL_GS_BASE = baza uzytkownika (obecnie 0)
 *
 *       przed powrotem Ring 3: SWAPGS
 *
 *       Ring 3: GS_BASE        = 0
 *               KERNEL_GS_BASE = kernelowy GS
 *
 *       wejscie SYSCALL: SWAPGS -> kernelowy GS wraca do GS_BASE.
 *
 *     Jest to zgodne z poprawionym przerwania.S i ring3.S.
 *
 * UWAGA ABI:
 * BWS 29/30 zachowuje obecne tymczasowe kodowanie:
 *
 *     arg4[63:32] = 32-bitowy adres bufora Ring 3
 *     arg4[31:0]  = rozmiar
 *
 * To ogranicza bufor HTTP/HTTPS do dolnych 4 GiB VA. Pelna naprawa wymaga
 * zmiany ABI oraz bursztyn_gui.cpp/h jednoczesnie.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pzb.h"
#include "zegar-rtc.h"
#include "grafika.h"
#include "siec.h"
#include "scheduler.h"
#include "skladacz_obrazu.h"
#include "bws_zdarzenia.h"
#include "bws_pliki.h"
#include "pamiec.h"
#include "loader.h"
#include "heap.h"
#include "psf.h"
#include "acpi.h"

/* =========================================================================
 * PUBLICZNY STAN UZYWANY PRZEZ GUI / SYSCALL ASM
 * ========================================================================= */

volatile bool ekran_zajety = false;

/*
 * Brama SYSCALL korzysta z kernelowego GS, ktorego baza wskazuje na adres
 * tej zmiennej. Scheduler aktualizuje jej wartosc przy zmianie procesu.
 */
uint64_t bezpieczny_stos_jadra = 0;

/* =========================================================================
 * ZEWNĘTRZNE API JADRA
 * ========================================================================= */

extern "C" {

void wypisz_na_ekranie(
    const char* buf
);

char pobierz_znak_klawiatury();

uint32_t bws_siec_ping(
    uint8_t ip1,
    uint8_t ip2,
    uint8_t ip3,
    uint8_t ip4
);

void bws_gui_rysuj_okno(
    int x,
    int y,
    int szer,
    int wys,
    const char* tytul
);

void bws_gui_wypisz_tekst(
    int x,
    int y,
    const char* text
);

void bws_gui_wyczyscz_obszar(
    int x,
    int y,
    int szer,
    int wys
);

void bws_gui_odswiez();

void bws_gui_pobierz_mysz(
    int* x,
    int* y,
    uint8_t* przyciski
);

void bws_gui_odswiez_pulpit();

void bws_gui_wypisz_tekst_kolor(
    int x,
    int y,
    uint64_t kolor_skala,
    const char* text
);

void bws_gui_rysuj_prostokat(
    int x,
    int y,
    int w,
    int h,
    uint32_t kolor
);

void bws_gui_ustaw_przejecie_myszy(
    bool stan
);

void bws_gui_zwolnij_mysz_procesu(
    int pid
);

void bws_gui_pobierz_rozdzielczosc(
    int* szer,
    int* wys
);

int bws_gui_pobierz_szerokosc_znaku(
    uint32_t unicode
);

void bws_dzwiek_test(
    uint32_t czestotliwosc,
    uint32_t czas
);

void brama_wywolan_systemowych();

/*
 * stack_top jest etykieta z boot.S, nie zmienna przechowujaca adres.
 */
extern uint8_t stack_top[];

} // extern "C"

extern bool hda_test_ton(
    uint32_t czestotliwosc_hz,
    uint32_t czas_ms
);

extern uint32_t tcp_zapisano_bajtow;

/*
 * Warstwa bezpiecznego dostepu Ring 3 <-> Ring 0.
 * Implementacja znajduje sie w bezpieczenstwo.cpp.
 */
extern bool skopiuj_z_przestrzeni_uzytkownika(
    void* bufor_jadra,
    const void* ptr_uzytkownika,
    size_t rozmiar
);

extern bool skopiuj_do_przestrzeni_uzytkownika(
    void* ptr_uzytkownika,
    const void* bufor_jadra,
    size_t rozmiar
);

extern bool skopiuj_string_z_uzytkownika(
    char* bufor_jadra,
    const char* ptr_uzytkownika,
    size_t max_rozmiar
);

extern bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(
    const void* ptr,
    size_t rozmiar
);

extern bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(
    void* ptr,
    size_t rozmiar
);

/*
 * Hook VMM z poprawionego vmm.cpp.
 * Weak pozwala uruchomic build ze starszym VMM, ale BWS 35 wtedy nie moze
 * potwierdzic mapowania i zachowuje bardziej konserwatywna semantyke.
 */
extern "C" bool bws_vmm_sprawdz_zakres_uzytkownika(
    const void* adres,
    size_t rozmiar,
    bool zapis
) __attribute__((weak));

void wypisz_log(
    const char* tekst
);

/* =========================================================================
 * STALE
 * ========================================================================= */

namespace {

constexpr uint32_t MSR_EFER =
    0xC0000080U;

constexpr uint32_t MSR_STAR =
    0xC0000081U;

constexpr uint32_t MSR_LSTAR =
    0xC0000082U;

constexpr uint32_t MSR_FMASK =
    0xC0000084U;

constexpr uint32_t MSR_GS_BASE =
    0xC0000101U;

constexpr uint32_t MSR_KERNEL_GS_BASE =
    0xC0000102U;

constexpr uint64_t EFER_SCE =
    1ULL << 0;

/*
 * Selektory zgodne z GDT:
 *
 *   0x08 Kernel Code
 *   0x10 Kernel Data
 *   0x1B User Data
 *   0x23 User Code
 *
 * Dla SYSRET STAR[63:48] ma zawierac 0x13:
 *   SS = 0x13 + 8  = 0x1B
 *   CS = 0x13 + 16 = 0x23
 */
constexpr uint16_t STAR_KERNEL_CS =
    0x08U;

constexpr uint16_t STAR_USER_BASE =
    0x13U;

/*
 * Na wejściu SYSCALL kernel nie moze odziedziczyc niebezpiecznych flag:
 *
 *   TF  bit 8   - single-step
 *   IF  bit 9   - IRQ przed gotowym stosem
 *   DF  bit 10  - ABI C/C++ wymaga DF=0
 *   NT  bit 14  - nested task
 *   AC  bit 18  - alignment check
 */
constexpr uint64_t SYSCALL_FMASK =
    (1ULL << 8)  |
    (1ULL << 9)  |
    (1ULL << 10) |
    (1ULL << 14) |
    (1ULL << 18);

constexpr size_t MAX_TEKST_BWS =
    4096;

constexpr size_t MAX_SCIEZKA_PLIKU_BWS =
    512;

constexpr size_t MAX_SCIEZKA_PROGRAMU_BWS =
    PZB_DLUGOSC_SCIEZKI_PROCESU;

constexpr size_t MAX_NAZWA_BWS =
    PSF_MAX_NAZWA;

constexpr size_t MAX_DOMENA_BWS =
    256;

constexpr size_t MAX_SCIEZKA_HTTP_BWS =
    2048;

constexpr uint32_t MAX_LISTA_KATALOGU_BWS =
    64U * 1024U;

constexpr uint32_t MAX_HTTP_BWS =
    256U * 1024U;

constexpr uint32_t LEGACY_HTTP_BUF =
    64U * 1024U;

/*
 * Obecny BSP2 korzysta z 490 blokow bezposrednich po 4096 B.
 * Nie uzywamy makra PSF_MAKS_ROZMIAR_PLIKU_BEZPOSREDNI, aby zachowac
 * kompatybilnosc takze ze starszym psf.h.
 */
constexpr uint64_t MAX_PLIK_BSP2_BWS =
    static_cast<uint64_t>(
        PSF_MAX_BLOKOW_W_WEZLE
    ) *
    static_cast<uint64_t>(
        PSF_ROZMIAR_BLOKU
    );

constexpr uint64_t ROZMIAR_STRONY =
    4096ULL;

constexpr uint64_t MASKA_STRONY =
    ROZMIAR_STRONY - 1ULL;

constexpr uint64_t BAZA_STERTY_USER =
    0x0000000800000000ULL;

/*
 * 256 MiB prywatnej sterty na proces.
 *
 * To limit polityki BWS, nie limit architektury VMM.
 */
constexpr uint64_t MAKS_STERTA_USER =
    256ULL * 1024ULL * 1024ULL;

constexpr uint64_t KONIEC_STERTY_USER =
    BAZA_STERTY_USER +
    MAKS_STERTA_USER;

/*
 * Pojedynczy syscall nie moze poprosic o cala kwote procesu naraz.
 * Ogranicza to dlugosc petli z wylaczonym/preempcja zabroniona kontekstem
 * syscalls oraz blast radius blednego programu.
 */
constexpr uint64_t MAX_ALOKACJA_STERTY_NA_BWS =
    16ULL * 1024ULL * 1024ULL;

struct StanDropProcesu {
    BwsCelDrop cele[BWS_DROP_CELE_MAX];
    uint32_t liczba;
    uint64_t ostatnia_rejestracja;
    int ostatni_cel;
    int ostatni_pid_celu;
};

StanDropProcesu cele_drop[PZB_MAKS_PROCESOW] = {};
constexpr uint64_t DROP_WYGASA_PO_TICKACH = 1000ULL;

BwsSchowekPlikow schowek_plikow = {};
uint64_t schowek_generacja = 0;
bool schowek_blokada = false;

struct BlokadaSchowka {
    BlokadaSchowka() {
        while (__atomic_test_and_set(&schowek_blokada, __ATOMIC_ACQUIRE))
            asm volatile("pause" ::: "memory");
    }
    ~BlokadaSchowka() {
        __atomic_clear(&schowek_blokada, __ATOMIC_RELEASE);
    }
};

void powiadom_zmiane_systemu_plikow() {
    bws_zdarzenie zdarzenie{};
    zdarzenie.typ = BWS_ZDARZENIE_PLIKI_ZMIENIONE;
    zdarzenie.timestamp = scheduler_pobierz_tick();
    for (int pid = 1; pid < static_cast<int>(PZB_MAKS_PROCESOW); ++pid)
        if (scheduler_pid_uzytkownika(pid))
            (void)scheduler_dodaj_zdarzenie(pid, &zdarzenie);
}

/* =========================================================================
 * LOCK EKRANU
 * ========================================================================= */

void zablokuj_ekran() {
    while (__atomic_test_and_set(
               &ekran_zajety,
               __ATOMIC_ACQUIRE)) {

        asm volatile(
            "pause"
            :
            :
            : "memory"
        );
    }
}

void odblokuj_ekran() {
    __atomic_clear(
        &ekran_zajety,
        __ATOMIC_RELEASE
    );
}

class BlokadaEkranu {
public:
    BlokadaEkranu() {
        zablokuj_ekran();
    }

    ~BlokadaEkranu() {
        odblokuj_ekran();
    }

    BlokadaEkranu(
        const BlokadaEkranu&
    ) = delete;

    BlokadaEkranu& operator=(
        const BlokadaEkranu&
    ) = delete;
};

/* =========================================================================
 * MSR
 * ========================================================================= */

void zapisz_msr(
    uint32_t msr,
    uint64_t wartosc
) {
    const uint32_t dolny =
        static_cast<uint32_t>(
            wartosc &
            0xFFFFFFFFULL
        );

    const uint32_t gorny =
        static_cast<uint32_t>(
            wartosc >> 32
        );

    asm volatile(
        "wrmsr"
        :
        : "a"(dolny),
          "d"(gorny),
          "c"(msr)
        : "memory"
    );
}

uint64_t odczytaj_msr(
    uint32_t msr
) {
    uint32_t dolny = 0;
    uint32_t gorny = 0;

    asm volatile(
        "rdmsr"
        : "=a"(dolny),
          "=d"(gorny)
        : "c"(msr)
        : "memory"
    );

    return
        (static_cast<uint64_t>(
             gorny) << 32) |
        static_cast<uint64_t>(
            dolny
        );
}

/* =========================================================================
 * PROCES / PZB
 * ========================================================================= */

proces_t* pobierz_proces_wywolujacy() {
    if (!scheduler_pid_uzytkownika(
            aktualny_pid)) {

        return nullptr;
    }

    proces_t& proces =
        tablica_procesow[
            aktualny_pid];

    const int stan =
        __atomic_load_n(
            &proces.stan,
            __ATOMIC_ACQUIRE
        );

    if (stan == PROCES_PUSTY) {
        return nullptr;
    }

    if (proces.pid !=
        static_cast<uint64_t>(
            aktualny_pid)) {

        return nullptr;
    }

    if (!pzb_poziom_poprawny(
            proces.poziom_zaufania)) {

        return nullptr;
    }

    return &proces;
}

uint64_t znane_prawa(
    const proces_t& proces
) {
    /*
     * Nieznane bity sa ignorowane, a nie automatycznie honorowane.
     * Zachowuje to kompatybilnosc z obecnym managerem okien, ktory mogl
     * zostac jeszcze uruchomiony ze stara maska 0xFFFFFFFF.
     */
    return
        proces.uprawnienia &
        static_cast<uint64_t>(
            PRAWA_ZNANE
        );
}

bool proces_ma_prawo(
    const proces_t& proces,
    uint64_t wymagane
) {
    return
        (znane_prawa(proces) &
         wymagane) ==
        wymagane;
}

bool proces_ma_poziom_co_najmniej(
    const proces_t& proces,
    uint8_t maks_numer
) {
    return
        pzb_poziom_poprawny(
            proces.poziom_zaufania) &&
        pzb_poziom_poprawny(
            maks_numer) &&
        proces.poziom_zaufania <=
            maks_numer;
}

/* =========================================================================
 * PROSTE FUNKCJE PAMIECIOWE
 * ========================================================================= */

void wyzeruj(
    void* ptr,
    uint64_t rozmiar
) {
    if (!ptr) {
        return;
    }

    uint8_t* p =
        static_cast<uint8_t*>(
            ptr
        );

    for (uint64_t i = 0;
         i < rozmiar;
         ++i) {

        p[i] = 0;
    }
}

uint64_t min_u64(
    uint64_t a,
    uint64_t b
) {
    return
        a < b
            ? a
            : b;
}

size_t dlugosc_stringa_limit(
    const char* tekst,
    size_t limit
) {
    if (!tekst) {
        return 0;
    }

    for (size_t i = 0;
         i < limit;
         ++i) {

        if (tekst[i] == '\0') {
            return i;
        }
    }

    return limit;
}

/* =========================================================================
 * KOPIOWANIE ARGUMENTOW Z RING 3
 * ========================================================================= */

bool pobierz_string_user(
    uint64_t adres,
    char* cel,
    size_t pojemnosc
) {
    if (adres == 0 ||
        !cel ||
        pojemnosc == 0) {

        return false;
    }

    return
        skopiuj_string_z_uzytkownika(
            cel,
            reinterpret_cast<const char*>(
                adres
            ),
            pojemnosc
        );
}

bool pobierz_sciezke_pliku(
    uint64_t adres,
    char* cel
) {
    return
        pobierz_string_user(
            adres,
            cel,
            MAX_SCIEZKA_PLIKU_BWS
        );
}

bool pobierz_sciezke_programu(
    uint64_t adres,
    char* cel
) {
    return
        pobierz_string_user(
            adres,
            cel,
            MAX_SCIEZKA_PROGRAMU_BWS
        );
}

bool pobierz_nazwe(
    uint64_t adres,
    char* cel
) {
    return
        pobierz_string_user(
            adres,
            cel,
            MAX_NAZWA_BWS
        );
}

/* =========================================================================
 * OCHRONA SCIEZEK SYSTEMOWYCH
 * ========================================================================= */

bool segment_prefiksu(
    const char* sciezka,
    const char* prefiks
) {
    if (!sciezka ||
        !prefiks) {

        return false;
    }

    size_t i = 0;

    while (prefiks[i] != '\0') {
        if (sciezka[i] !=
            prefiks[i]) {

            return false;
        }

        ++i;
    }

    /*
     * "/system2" nie jest dzieckiem "/system".
     */
    return
        sciezka[i] == '\0' ||
        sciezka[i] == '/';
}

bool sciezka_chroniona(
    const char* sciezka
) {
    return
        segment_prefiksu(
            sciezka,
            "/system"
        ) ||
        segment_prefiksu(
            sciezka,
            "/jadro"
        );
}

bool proces_moze_modyfikowac_sciezke(
    const proces_t& proces,
    const char* sciezka
) {
    if (!proces_ma_prawo(
            proces,
            PRAWO_PLIKI_ZAPISZ)) {

        return false;
    }

    /*
     * Zwykle aplikacje i sandbox nie moga modyfikowac krytycznych drzew.
     */
    if (proces.poziom_zaufania >=
            PZB_UZYTKOWNIK &&
        sciezka_chroniona(
            sciezka)) {

        return false;
    }

    return true;
}

bool poziom_pliku_pozwala_modyfikowac(
    const proces_t& proces,
    const char* sciezka
) {
    psf_metadane meta{};
    if (!pobierz_metadane_tworu(sciezka, &meta)) return false;
    /* Stare BSP2 nie maja etykiety: zachowuja dotychczasowa polityke
     * prawa PRAWO_PLIKI_ZAPISZ i ochrony drzew systemowych. */
    return !meta.pzb_dostepny || proces.poziom_zaufania <= meta.poziom_pzb;
}

bool pobierz_rodzica_sciezki(
    const char* sciezka,
    char* rodzic,
    size_t pojemnosc
) {
    if (!sciezka || !rodzic || pojemnosc < 2 || sciezka[0] != '/')
        return false;
    const size_t dlugosc = dlugosc_stringa_limit(sciezka, pojemnosc);
    if (dlugosc == 0 || dlugosc >= pojemnosc) return false;
    size_t slash = dlugosc;
    while (slash > 0 && sciezka[slash] != '/') --slash;
    if (slash == 0) {
        rodzic[0] = '/';
        rodzic[1] = '\0';
        return true;
    }
    if (slash + 1 > pojemnosc) return false;
    for (size_t i = 0; i < slash; ++i) rodzic[i] = sciezka[i];
    rodzic[slash] = '\0';
    return true;
}

bool proces_moze_utworzyc_w_sciezce(
    const proces_t& proces,
    const char* sciezka
) {
    if (!proces_moze_modyfikowac_sciezke(proces, sciezka)) return false;
    char rodzic[MAX_SCIEZKA_PLIKU_BWS] = {};
    return pobierz_rodzica_sciezki(sciezka, rodzic, sizeof(rodzic)) &&
           poziom_pliku_pozwala_modyfikowac(proces, rodzic);
}

bool proces_moze_modyfikowac_obiekt(
    const proces_t& proces,
    const char* sciezka
) {
    if (!proces_moze_modyfikowac_sciezke(proces, sciezka) ||
        !poziom_pliku_pozwala_modyfikowac(proces, sciezka)) return false;
    if (sciezka[0] == '/' && sciezka[1] == '\0') return true;
    char rodzic[MAX_SCIEZKA_PLIKU_BWS] = {};
    return pobierz_rodzica_sciezki(sciezka, rodzic, sizeof(rodzic)) &&
           poziom_pliku_pozwala_modyfikowac(proces, rodzic);
}

/* =========================================================================
 * POMOCNICZE BUFOROWANIE KERNELA
 * ========================================================================= */

void* alokuj_bufor(
    uint64_t rozmiar,
    bool zeruj
) {
    if (rozmiar == 0) {
        return nullptr;
    }

    void* ptr =
        kmalloc(rozmiar);

    if (!ptr) {
        return nullptr;
    }

    if (zeruj) {
        wyzeruj(
            ptr,
            rozmiar
        );
    }

    return ptr;
}

class BuforKernelowy {
public:
    explicit BuforKernelowy(
        uint64_t rozmiar,
        bool zeruj = false
    )
        : ptr_(
              alokuj_bufor(
                  rozmiar,
                  zeruj
              )
          ),
          rozmiar_(rozmiar) {
    }

    ~BuforKernelowy() {
        if (ptr_) {
            kfree(ptr_);
        }
    }

    BuforKernelowy(
        const BuforKernelowy&
    ) = delete;

    BuforKernelowy& operator=(
        const BuforKernelowy&
    ) = delete;

    void* get() const {
        return ptr_;
    }

    char* jako_char() const {
        return static_cast<char*>(
            ptr_
        );
    }

    uint8_t* jako_u8() const {
        return static_cast<uint8_t*>(
            ptr_
        );
    }

    uint64_t rozmiar() const {
        return rozmiar_;
    }

    explicit operator bool() const {
        return
            ptr_ != nullptr;
    }

private:
    void* ptr_;
    uint64_t rozmiar_;
};

/* =========================================================================
 * FILE BWS
 * ========================================================================= */

uint64_t bws_wypisz(
    uint64_t arg1
) {
    char tekst[
        MAX_TEKST_BWS] = {};

    if (!pobierz_string_user(
            arg1,
            tekst,
            sizeof(tekst))) {

        return 0;
    }

    BlokadaEkranu blokada;

    wypisz_na_ekranie(
        tekst
    );

    return 1;
}

uint64_t bws_utworz_plik(
    proces_t& proces,
    uint64_t arg1
) {
    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka)) {

        return 0;
    }

    if (!proces_moze_utworzyc_w_sciezce(
            proces,
            sciezka)) {
        wypisz_log("[PZB] file_create=DENY");
        return 0;
    }
    wypisz_log("[PZB] file_create=ALLOW");

    const bool wynik = utworz_plik_z_pzb(sciezka, proces.poziom_zaufania);
    if (wynik) powiadom_zmiane_systemu_plikow();
    return wynik ? 1ULL : 0ULL;
}

uint64_t bws_zapisz_plik(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
) {
    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka)) {

        return 0;
    }

    if (!proces_moze_modyfikowac_obiekt(
            proces,
            sciezka)) {
        wypisz_log("[PZB] file_write=DENY");
        return 0;
    }
    wypisz_log("[PZB] file_write=ALLOW");

    if (arg3 >
        MAX_PLIK_BSP2_BWS) {

        return 0;
    }

    const uint32_t dlugosc =
        static_cast<uint32_t>(
            arg3
        );

    if (dlugosc == 0) {
        const bool wynik = zapisz_do_pliku(sciezka, nullptr, 0);
        if (wynik) powiadom_zmiane_systemu_plikow();
        return wynik ? 1ULL : 0ULL;
    }

    if (arg2 == 0) {
        return 0;
    }

    BuforKernelowy dane(
        dlugosc,
        false
    );

    if (!dane) {
        return 0;
    }

    if (!skopiuj_z_przestrzeni_uzytkownika(
            dane.get(),
            reinterpret_cast<const void*>(
                arg2
            ),
            dlugosc)) {

        return 0;
    }

    const bool wynik = zapisz_do_pliku(sciezka, dane.jako_char(), dlugosc);
    if (wynik) powiadom_zmiane_systemu_plikow();
    return wynik ? 1ULL : 0ULL;
}

uint64_t bws_czytaj_plik(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
) {
    if (!proces_ma_prawo(
            proces,
            PRAWO_PLIKI_CZYTAJ)) {

        return 0;
    }

    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka)) {

        return 0;
    }

    if (arg3 >
        MAX_PLIK_BSP2_BWS) {

        return 0;
    }

    const uint32_t max_dlugosc =
        static_cast<uint32_t>(
            arg3
        );

    if (max_dlugosc == 0) {
        return
            czytaj_z_pliku(
                sciezka,
                nullptr,
                0)
                ? 1ULL
                : 0ULL;
    }

    if (arg2 == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(
                arg2
            ),
            max_dlugosc)) {

        return 0;
    }

    const uint32_t rozmiar =
        rozmiar_pliku(
            sciezka
        );

    const uint32_t do_odczytu =
        static_cast<uint32_t>(
            min_u64(
                rozmiar,
                max_dlugosc
            )
        );

    /*
     * Dla pustego pliku nadal wywolujemy FS, bo rozmiar_pliku()==0 moze
     * oznaczac rowniez brak pliku.
     */
    if (do_odczytu == 0) {
        return
            czytaj_z_pliku(
                sciezka,
                nullptr,
                0)
                ? 1ULL
                : 0ULL;
    }

    BuforKernelowy bufor(
        do_odczytu,
        false
    );

    if (!bufor) {
        return 0;
    }

    if (!czytaj_z_pliku(
            sciezka,
            bufor.jako_char(),
            do_odczytu)) {

        return 0;
    }

    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(
                arg2
            ),
            bufor.get(),
            do_odczytu)) {

        return 0;
    }

    return 1;
}

uint64_t bws_wylistuj_katalog(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
) {
    if (!proces_ma_prawo(
            proces,
            PRAWO_PLIKI_CZYTAJ)) {

        return 0;
    }

    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka)) {

        return 0;
    }

    if (arg2 == 0 ||
        arg3 == 0 ||
        arg3 >
            MAX_LISTA_KATALOGU_BWS) {

        return 0;
    }

    const uint32_t max_dlugosc =
        static_cast<uint32_t>(
            arg3
        );

    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(
                arg2
            ),
            max_dlugosc)) {

        return 0;
    }

    BuforKernelowy bufor(
        max_dlugosc,
        true
    );

    if (!bufor) {
        return 0;
    }

    if (!wylistuj_katalog(
            sciezka,
            bufor.jako_char(),
            max_dlugosc)) {

        return 0;
    }

    const size_t dlugosc =
        dlugosc_stringa_limit(
            bufor.jako_char(),
            max_dlugosc
        );

    if (dlugosc >=
        max_dlugosc) {

        return 0;
    }

    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(
                arg2
            ),
            bufor.get(),
            dlugosc + 1U)) {

        return 0;
    }

    return 1;
}

uint64_t bws_usun_twor(
    proces_t& proces,
    uint64_t arg1
) {
    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka)) {

        return 0;
    }

    if (!proces_moze_modyfikowac_obiekt(
            proces,
            sciezka)) {

        return 0;
    }

    const bool wynik = usun_twor(sciezka);
    if (wynik) powiadom_zmiane_systemu_plikow();
    return wynik ? 1ULL : 0ULL;
}

uint64_t bws_zmien_nazwe(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2
) {
    char sciezka[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    char nowa_nazwa[
        MAX_NAZWA_BWS] = {};

    if (!pobierz_sciezke_pliku(
            arg1,
            sciezka) ||
        !pobierz_nazwe(
            arg2,
            nowa_nazwa)) {

        return 0;
    }

    if (!proces_moze_modyfikowac_obiekt(
            proces,
            sciezka)) {

        return 0;
    }

    const bool wynik = zmien_nazwe_tworu(sciezka, nowa_nazwa);
    if (wynik) powiadom_zmiane_systemu_plikow();
    return wynik ? 1ULL : 0ULL;
}

uint64_t bws_pobierz_metadane(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2
) {
    if (!proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ) || arg2 == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(arg2), sizeof(BwsMetadanePliku)))
        return 0;
    char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_sciezke_pliku(arg1, sciezka)) return 0;
    psf_metadane psf{};
    if (!pobierz_metadane_tworu(sciezka, &psf)) return 0;
    BwsMetadanePliku wynik{};
    wynik.wersja = BWS_METADANE_WERSJA;
    wynik.typ = psf.typ;
    wynik.rozmiar = psf.rozmiar;
    wynik.czas_utworzenia_rtc = psf.czas_utworzenia_rtc;
    wynik.poziom_pzb = psf.poziom_pzb;
    if (psf.typ == TYP_PLIK) wynik.flagi |= BWS_META_ROZMIAR_DOSTEPNY;
    if (psf.czas_dostepny) wynik.flagi |= BWS_META_CZAS_DOSTEPNY;
    if (psf.pzb_dostepny) wynik.flagi |= BWS_META_PZB_DOSTEPNY;
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(arg2), &wynik, sizeof(wynik)) ? 1ULL : 0ULL;
}

uint64_t bws_przenies(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2
) {
    char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
    char folder[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_sciezke_pliku(arg1, sciezka) ||
        !pobierz_sciezke_pliku(arg2, folder)) return 0;
    if (!proces_moze_modyfikowac_obiekt(proces, sciezka) ||
        !proces_moze_modyfikowac_sciezke(proces, folder) ||
        !poziom_pliku_pozwala_modyfikowac(proces, folder)) {
        wypisz_log("[PZB] file_move=DENY");
        return 0;
    }
    const bool wynik = przenies_twor(sciezka, folder);
    wypisz_log(wynik ? "[PZB] file_move=ALLOW" : "[PSF] file_move=ERROR");
    if (wynik) powiadom_zmiane_systemu_plikow();
    return wynik ? 1ULL : 0ULL;
}

bool punkt_w_celu_drop(int32_t x, int32_t y, const BwsCelDrop& cel) {
    if (cel.szer <= 0 || cel.wys <= 0) return false;
    const int64_t prawa = static_cast<int64_t>(cel.x) + cel.szer;
    const int64_t dol = static_cast<int64_t>(cel.y) + cel.wys;
    return static_cast<int64_t>(x) >= cel.x &&
           static_cast<int64_t>(y) >= cel.y &&
           static_cast<int64_t>(x) < prawa &&
           static_cast<int64_t>(y) < dol;
}

bool poprawny_folder_celu_drop(const char* folder) {
    if (!folder || folder[0] != '/') return false;
    const size_t n = dlugosc_stringa_limit(folder, BWS_DROP_SCIEZKA_MAX);
    if (n == 0 || n >= BWS_DROP_SCIEZKA_MAX) return false;
    return czy_katalog_istnieje(folder);
}

void wyslij_zdarzenie_drop(int pid, uint32_t typ, int32_t x, int32_t y,
                           uint32_t kod) {
    if (!scheduler_pid_uzytkownika(pid)) return;
    bws_zdarzenie zdarzenie{};
    zdarzenie.typ = typ;
    zdarzenie.x = x;
    zdarzenie.y = y;
    zdarzenie.kod = kod;
    zdarzenie.timestamp = scheduler_pobierz_tick();
    (void)scheduler_dodaj_zdarzenie(pid, &zdarzenie);
}

uint64_t bws_rejestruj_cele_drop(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2
) {
    if (!proces_ma_prawo(proces, PRAWO_GUI) ||
        !proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ) ||
        aktualny_pid <= 0 || aktualny_pid >= static_cast<int>(PZB_MAKS_PROCESOW) ||
        arg2 > BWS_DROP_CELE_MAX || (arg2 != 0 && arg1 == 0)) return 0;
    StanDropProcesu& stan = cele_drop[aktualny_pid];
    stan.liczba = 0;
    if (arg2 != 0) {
        const size_t bajty = static_cast<size_t>(arg2) * sizeof(BwsCelDrop);
        if (!czy_bezpieczny_zakres_uzytkownika_do_odczytu(
                reinterpret_cast<const void*>(arg1), bajty) ||
            !skopiuj_z_przestrzeni_uzytkownika(
                stan.cele, reinterpret_cast<const void*>(arg1), bajty))
            return 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(arg2); ++i) {
            BwsCelDrop& cel = stan.cele[i];
            if (cel.szer <= 0 || cel.wys <= 0 ||
                !poprawny_folder_celu_drop(cel.folder)) {
                stan.liczba = 0;
                return 0;
            }
        }
        stan.liczba = static_cast<uint32_t>(arg2);
    }
    stan.ostatnia_rejestracja = scheduler_pobierz_tick();
    return 1;
}

bool znajdz_cel_drop(int32_t x, int32_t y, int* pid_wyj, int* indeks_wyj) {
    int najlepszy_pid = -1;
    int najlepszy = -1;
    int64_t najlepsze_pole = INT64_MAX;
    const uint64_t teraz = scheduler_pobierz_tick();
    for (int pid = 1; pid < static_cast<int>(PZB_MAKS_PROCESOW); ++pid) {
        StanDropProcesu& stan = cele_drop[pid];
        if (stan.liczba == 0 || teraz < stan.ostatnia_rejestracja ||
            teraz - stan.ostatnia_rejestracja > DROP_WYGASA_PO_TICKACH)
            continue;
        for (uint32_t i = 0; i < stan.liczba; ++i) {
            if (!punkt_w_celu_drop(x, y, stan.cele[i])) continue;
            const int64_t pole = static_cast<int64_t>(stan.cele[i].szer) *
                                 static_cast<int64_t>(stan.cele[i].wys);
            if (pole < najlepsze_pole) {
                najlepsze_pole = pole;
                najlepszy_pid = pid;
                najlepszy = static_cast<int>(i);
            }
        }
    }
    if (pid_wyj) *pid_wyj = najlepszy_pid;
    if (indeks_wyj) *indeks_wyj = najlepszy;
    return najlepszy_pid > 0 && najlepszy >= 0;
}

uint64_t bws_aktualizuj_drag(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
) {
    if (!proces_ma_prawo(proces, PRAWO_GUI) ||
        aktualny_pid <= 0 || aktualny_pid >= static_cast<int>(PZB_MAKS_PROCESOW))
        return BWS_DROP_BLAD;
    if (arg3 != 0)
        skladacz_obrazu_ustaw_drag_overlay(aktualny_pid, false, 0, 0);
    char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_sciezke_pliku(arg1, sciezka)) return BWS_DROP_BLAD;
    const int32_t x = static_cast<int32_t>(arg2 >> 32U);
    const int32_t y = static_cast<int32_t>(arg2 & UINT64_C(0xFFFFFFFF));
    if (arg3 == 0)
        skladacz_obrazu_ustaw_drag_overlay(aktualny_pid, true, x, y);
    int pid_celu = -1;
    int indeks = -1;
    const bool znaleziony = znajdz_cel_drop(x, y, &pid_celu, &indeks);
    StanDropProcesu& zrodlo = cele_drop[aktualny_pid];
    if (zrodlo.ostatni_pid_celu != pid_celu || zrodlo.ostatni_cel != indeks) {
        if (zrodlo.ostatni_pid_celu > 0)
            wyslij_zdarzenie_drop(zrodlo.ostatni_pid_celu,
                                  BWS_ZDARZENIE_DRAG_LEAVE, x, y, 0);
        if (znaleziony)
            wyslij_zdarzenie_drop(pid_celu, BWS_ZDARZENIE_DRAG_HOVER,
                                  x, y, static_cast<uint32_t>(indeks));
        zrodlo.ostatni_pid_celu = pid_celu;
        zrodlo.ostatni_cel = indeks;
    }
    if (!znaleziony) return BWS_DROP_BRAK_CELU;
    if (arg3 == 0) return BWS_DROP_CEL_POPRAWNY;

    const BwsCelDrop& cel = cele_drop[pid_celu].cele[indeks];
    const bool przeniesiono =
        proces_moze_modyfikowac_obiekt(proces, sciezka) &&
        proces_moze_modyfikowac_sciezke(proces, cel.folder) &&
        poziom_pliku_pozwala_modyfikowac(proces, cel.folder) &&
        przenies_twor(sciezka, cel.folder);
    wyslij_zdarzenie_drop(pid_celu,
        przeniesiono ? BWS_ZDARZENIE_DRAG_DROP : BWS_ZDARZENIE_DRAG_LEAVE,
        x, y, static_cast<uint32_t>(indeks));
    zrodlo.ostatni_pid_celu = -1;
    zrodlo.ostatni_cel = -1;
    if (przeniesiono) powiadom_zmiane_systemu_plikow();
    return przeniesiono ? BWS_DROP_PRZENIESIONO : BWS_DROP_BLAD;
}

uint64_t bws_ustaw_schowek_plikow(proces_t& proces,
                                  uint64_t arg1, uint64_t arg2) {
    if (!proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ) ||
        (arg2 != BWS_SCHOWEK_COPY && arg2 != BWS_SCHOWEK_CUT)) return 0;
    char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_sciezke_pliku(arg1, sciezka)) return 0;
    psf_metadane meta{};
    if (!pobierz_metadane_tworu(sciezka, &meta)) return 0;
    if (arg2 == BWS_SCHOWEK_CUT &&
        !proces_moze_modyfikowac_obiekt(proces, sciezka)) {
        wypisz_log("[PZB] clipboard_cut=DENY");
        return 0;
    }

    BlokadaSchowka blokada;
    BwsSchowekPlikow nowy{};
    nowy.wersja = BWS_SCHOWEK_WERSJA;
    nowy.operacja = static_cast<uint8_t>(arg2);
    nowy.typ = meta.typ;
    ++schowek_generacja;
    if (schowek_generacja == 0) ++schowek_generacja;
    nowy.generacja = schowek_generacja;
    size_t i = 0;
    for (; i + 1U < sizeof(nowy.sciezka) && sciezka[i] != '\0'; ++i)
        nowy.sciezka[i] = sciezka[i];
    if (sciezka[i] != '\0') return 0;
    nowy.sciezka[i] = '\0';
    schowek_plikow = nowy;
    return 1;
}

uint64_t bws_pobierz_schowek_plikow(proces_t& proces, uint64_t arg1) {
    if (!proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ) || arg1 == 0 ||
        !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(arg1), sizeof(BwsSchowekPlikow))) return 0;
    BwsSchowekPlikow kopia{};
    {
        BlokadaSchowka blokada;
        kopia = schowek_plikow;
        if (kopia.wersja == 0) kopia.wersja = BWS_SCHOWEK_WERSJA;
    }
    return skopiuj_do_przestrzeni_uzytkownika(
        reinterpret_cast<void*>(arg1), &kopia, sizeof(kopia)) ? 1ULL : 0ULL;
}

uint64_t bws_wyczysc_schowek_plikow(proces_t& proces, uint64_t generacja) {
    if (!proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ) || generacja == 0)
        return 0;
    BlokadaSchowka blokada;
    if (schowek_plikow.operacja == BWS_SCHOWEK_PUSTY ||
        schowek_plikow.generacja != generacja) return 0;
    schowek_plikow = {};
    schowek_plikow.wersja = BWS_SCHOWEK_WERSJA;
    schowek_plikow.generacja = generacja;
    return 1;
}

uint64_t bws_kopiuj_twor(proces_t& proces, uint64_t arg1, uint64_t arg2) {
    if (!proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ)) return 0;
    char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
    char folder[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (!pobierz_sciezke_pliku(arg1, sciezka) ||
        !pobierz_sciezke_pliku(arg2, folder)) return 0;
    psf_metadane meta{};
    if (!pobierz_metadane_tworu(sciezka, &meta) ||
        !proces_moze_modyfikowac_sciezke(proces, folder) ||
        !poziom_pliku_pozwala_modyfikowac(proces, folder)) {
        wypisz_log("[PZB] file_copy=DENY");
        return 0;
    }
    const bool wynik = kopiuj_twor_z_pzb(
        sciezka, folder, proces.poziom_zaufania);
    wypisz_log(wynik ? "[PZB] file_copy=ALLOW" : "[PSF] file_copy=ERROR");
    if (wynik) powiadom_zmiane_systemu_plikow();
    return wynik ? 1ULL : 0ULL;
}

/* =========================================================================
 * RTC
 * ========================================================================= */

uint64_t bws_rtc(
    uint64_t arg1
) {
    if (arg1 == 0) {
        return 0;
    }

    constexpr size_t BUF_RTC =
        32;

    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(
                arg1
            ),
            BUF_RTC)) {

        return 0;
    }

    czas_rtc czas{};
    char tekst[BUF_RTC] = {};

    pobierz_czas_rtc(
        &czas
    );

    formatuj_czas_do_stringa(
        &czas,
        tekst
    );

    const size_t dlugosc =
        dlugosc_stringa_limit(
            tekst,
            sizeof(tekst)
        );

    if (dlugosc >=
        sizeof(tekst)) {

        return 0;
    }

    return
        skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(
                arg1
            ),
            tekst,
            dlugosc + 1U)
            ? 1ULL
            : 0ULL;
}

/* =========================================================================
 * URUCHAMIANIE PROGRAMU
 * ========================================================================= */

uint64_t bws_uruchom(
    proces_t& rodzic,
    uint64_t arg1,
    uint64_t arg2
) {
    if (!proces_ma_prawo(
            rodzic,
            PRAWO_URUCHOM_PROGRAM)) {

        return 0;
    }

    char sciezka[
        MAX_SCIEZKA_PROGRAMU_BWS] = {};

    if (!pobierz_sciezke_programu(
            arg1,
            sciezka)) {

        return 0;
    }

    char argument[MAX_SCIEZKA_PLIKU_BWS] = {};
    if (arg2 != 0 && !pobierz_sciezke_pliku(arg2, argument)) return 0;

    /*
     * Ring 3 nigdy nie tworzy procesu bardziej zaufanego niz:
     *
     *  - PZB_UZYTKOWNIK dla rodzica 0..4,
     *  - jego wlasny poziom dla sandboxu 5.
     */
    uint8_t poziom_dziecka =
        rodzic.poziom_zaufania;

    if (poziom_dziecka <
        PZB_UZYTKOWNIK) {

        poziom_dziecka =
            PZB_UZYTKOWNIK;
    }

    uint64_t prawa_dziecka =
        znane_prawa(
            rodzic
        );

    auto rowna_sciezka = [](const char* a, const char* b) {
        if (!a || !b) return false;
        size_t i = 0;
        while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) ++i;
        return a[i] == '\0' && b[i] == '\0';
    };

    /* BWS10 uruchamia plik_startowy bez parsera .cebula. Ograniczamy maske
       do praw deklarowanych przez zaufane, wbudowane paczki. */
    uint64_t wymagane = prawa_dziecka;
    if (rowna_sciezka(sciezka, "/programy/notatnik.cebula/notatnik.bur"))
        wymagane = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ;
    else if (rowna_sciezka(
                sciezka,
                "/programy/eksplorator.cebula/eksplorator-plikow.bur"))
        wymagane = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ |
                   PRAWO_URUCHOM_PROGRAM;
    else if (rowna_sciezka(sciezka,
                           "/programy/przegladarka.cebula/przegladarka.bur"))
        wymagane = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_SIEC;
    else if (rowna_sciezka(sciezka,
                           "/programy/kalkulator.cebula/kalkulator.bur"))
        wymagane = PRAWO_GUI;
    else if (rowna_sciezka(sciezka, "/shell.bur"))
        wymagane = PRAWO_GUI | PRAWO_PLIKI_CZYTAJ | PRAWO_PLIKI_ZAPISZ |
                   PRAWO_SIEC | PRAWO_URUCHOM_PROGRAM;
    prawa_dziecka &= wymagane;

    /*
     * Proces uruchamiany przez zwykle Ring 3 nie dziedziczy praw do
     * konfiguracji jadra, sterownikow ani debugowania.
     */
    if (poziom_dziecka >=
        PZB_UZYTKOWNIK) {

        prawa_dziecka &=
            ~static_cast<uint64_t>(
                PRAWA_UPRZYWILEJOWANE
            );
    }

    /*
     * Sciezka jest juz skopiowana do bufora Ring 0, wiec loader otrzymuje
     * z_syscalla=false. To usuwa podwojna dereferencje user pointera.
     */
    if (arg2 != 0) {
        const int istniejacy = loader_przekaz_argument_uruchomionemu(
            sciezka, argument);
        if (istniejacy > 0) {
            bws_zdarzenie zdarzenie{};
            zdarzenie.typ = BWS_ZDARZENIE_OTWORZ_PLIK;
            zdarzenie.timestamp = scheduler_pobierz_tick();
            return scheduler_dodaj_zdarzenie(istniejacy, &zdarzenie)
                ? 1ULL : 0ULL;
        }
        return bws_uruchom_program_z_pliku_z_argumentem(
                   sciezka, poziom_dziecka, prawa_dziecka, false, argument)
            ? 1ULL : 0ULL;
    }

    return bws_uruchom_program_z_pliku(
               sciezka, poziom_dziecka, prawa_dziecka, false)
        ? 1ULL : 0ULL;
}

/* =========================================================================
 * SIEC - DNS / HTTP
 * ========================================================================= */

bool pobierz_ip_user(
    uint64_t arg,
    uint8_t ip[4]
) {
    if (arg == 0 ||
        !ip) {

        return false;
    }

    return
        skopiuj_z_przestrzeni_uzytkownika(
            ip,
            reinterpret_cast<const void*>(
                arg
            ),
            4
        );
}

bool pobierz_domena_user(
    uint64_t arg,
    char* domena
) {
    return
        pobierz_string_user(
            arg,
            domena,
            MAX_DOMENA_BWS
        );
}

bool pobierz_http_sciezka_user(
    uint64_t arg,
    char* sciezka
) {
    return
        pobierz_string_user(
            arg,
            sciezka,
            MAX_SCIEZKA_HTTP_BWS
        );
}

uint64_t bws_dns(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2
) {
    if (!proces_ma_prawo(
            proces,
            PRAWO_SIEC)) {

        return 0;
    }

    if (arg1 == 0 ||
        arg2 == 0) {

        return 0;
    }

    char domena[
        MAX_DOMENA_BWS] = {};

    if (!pobierz_domena_user(
            arg1,
            domena)) {

        return 0;
    }

    uint8_t ip[4] = {};

    if (!kernel_siec_dns(
            domena,
            ip)) {

        return 0;
    }

    return
        skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(
                arg2
            ),
            ip,
            sizeof(ip))
            ? 1ULL
            : 0ULL;
}

bool dekoduj_bufor_sieci(
    uint64_t arg4,
    uint64_t* adres,
    uint32_t* max_dlugosc
) {
    if (!adres ||
        !max_dlugosc) {

        return false;
    }

    /*
     * Obecny wrapper Ring 3 koduje tylko dolne 32 bity adresu.
     */
    const uint64_t adres32 =
        (arg4 >> 32) &
        0xFFFFFFFFULL;

    const uint32_t rozmiar =
        static_cast<uint32_t>(
            arg4 &
            0xFFFFFFFFULL
        );

    if (adres32 == 0 ||
        rozmiar < 2 ||
        rozmiar >
            MAX_HTTP_BWS) {

        return false;
    }

    *adres = adres32;
    *max_dlugosc = rozmiar;

    return true;
}

uint64_t bws_http_common(
    proces_t& proces,
    bool https,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
) {
    if (!proces_ma_prawo(
            proces,
            PRAWO_SIEC)) {

        return 0;
    }

    uint8_t ip[4] = {};

    if (!pobierz_ip_user(
            arg1,
            ip)) {

        return 0;
    }

    char domena[
        MAX_DOMENA_BWS] = {};

    char sciezka[
        MAX_SCIEZKA_HTTP_BWS] = {};

    if (!pobierz_domena_user(
            arg2,
            domena) ||
        !pobierz_http_sciezka_user(
            arg3,
            sciezka)) {

        return 0;
    }

    uint64_t adres_user = 0;
    uint32_t max_dlugosc = 0;

    if (!dekoduj_bufor_sieci(
            arg4,
            &adres_user,
            &max_dlugosc)) {

        return 0;
    }

    if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
            reinterpret_cast<void*>(
                adres_user
            ),
            max_dlugosc)) {

        return 0;
    }

    BuforKernelowy bufor(
        max_dlugosc,
        true
    );

    if (!bufor) {
        return 0;
    }

    /*
     * Rezerwujemy jeden bajt na NUL, bo obecne publiczne API ma char* i
     * shell/przegladarka interpretuja wynik jako tekst.
     */
    const uint32_t limit_sieci =
        max_dlugosc - 1U;

    tcp_zapisano_bajtow = 0;

    bool sukces = false;

    if (https) {
        sukces =
            kernel_siec_pobierz_https(
                ip,
                domena,
                sciezka,
                bufor.jako_char(),
                limit_sieci
            );
    } else {
        sukces =
            kernel_siec_pobierz_http(
                ip,
                domena,
                sciezka,
                bufor.jako_char(),
                limit_sieci
            );
    }

    if (!sukces) {
        return 0;
    }

    uint32_t odebrano =
        tcp_zapisano_bajtow;

    if (odebrano >
        limit_sieci) {

        return 0;
    }

    /*
     * Gdy sterownik/protokol nie raportuje liczby bajtow, probujemy
     * bezpiecznie wyliczyc dlugosc tekstowa w ograniczonym buforze.
     */
    if (odebrano == 0) {
        const size_t tekst_len =
            dlugosc_stringa_limit(
                bufor.jako_char(),
                limit_sieci
            );

        if (tekst_len >=
            limit_sieci) {

            return 0;
        }

        odebrano =
            static_cast<uint32_t>(
                tekst_len
            );
    }

    bufor.jako_char()[
        odebrano] =
        '\0';

    if (!skopiuj_do_przestrzeni_uzytkownika(
            reinterpret_cast<void*>(
                adres_user
            ),
            bufor.get(),
            static_cast<size_t>(
                odebrano) + 1U)) {

        return 0;
    }

    return 1;
}

/* =========================================================================
 * LEGACY BWS 13 - HTTP -> PLIK
 * ========================================================================= */

uint64_t bws_legacy_http_do_pliku(
    proces_t& proces,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
) {
    if (!proces_ma_prawo(
            proces,
            PRAWO_SIEC) ||
        !proces_ma_prawo(
            proces,
            PRAWO_PLIKI_ZAPISZ)) {

        return 0;
    }

    uint8_t ip[4] = {};

    if (!pobierz_ip_user(
            arg1,
            ip)) {

        return 0;
    }

    char domena[
        MAX_DOMENA_BWS] = {};

    char sciezka_http[
        MAX_SCIEZKA_HTTP_BWS] = {};

    char sciezka_dyskowa[
        MAX_SCIEZKA_PLIKU_BWS] = {};

    if (!pobierz_domena_user(
            arg2,
            domena) ||
        !pobierz_http_sciezka_user(
            arg3,
            sciezka_http) ||
        !pobierz_sciezke_pliku(
            arg4,
            sciezka_dyskowa)) {

        return 0;
    }

    if (!proces_moze_modyfikowac_sciezke(
            proces,
            sciezka_dyskowa)) {

        return 0;
    }

    BuforKernelowy bufor(
        LEGACY_HTTP_BUF,
        true
    );

    if (!bufor) {
        return 0;
    }

    tcp_zapisano_bajtow = 0;

    if (!kernel_siec_pobierz_http(
            ip,
            domena,
            sciezka_http,
            bufor.jako_char(),
            LEGACY_HTTP_BUF)) {

        return 0;
    }

    const uint32_t odebrano =
        tcp_zapisano_bajtow;

    if (odebrano >
        LEGACY_HTTP_BUF ||
        static_cast<uint64_t>(
            odebrano) >
            MAX_PLIK_BSP2_BWS) {

        return 0;
    }

    /*
     * utworz_plik moze zwrocic false, jezeli plik juz istnieje.
     * zapisz_do_pliku nadpisuje istniejacy plik, wiec nie traktujemy tego
     * jako bledu.
     */
    (void)utworz_plik(
        sciezka_dyskowa
    );

    return
        zapisz_do_pliku(
            sciezka_dyskowa,
            bufor.jako_char(),
            odebrano)
            ? 1ULL
            : 0ULL;
}

/* =========================================================================
 * GUI
 * ========================================================================= */

bool waliduj_tekst_gui(
    uint64_t arg,
    size_t limit
) {
    if (arg == 0 ||
        limit < 2) {

        return false;
    }

    /*
     * Wlasciwa funkcja grafiki kopiuje string ponownie do lokalnego bufora.
     * Ten odczyt daje syscallowi wiarygodny kod wyniku dla zlego wskaznika.
     */
    constexpr size_t MAX_TEKST_GUI_SYSCALL =
        1024U;

    char tmp[MAX_TEKST_GUI_SYSCALL] = {};

    if (limit >
        sizeof(tmp)) {

        limit =
            sizeof(tmp);
    }

    return
        skopiuj_string_z_uzytkownika(
            tmp,
            reinterpret_cast<const char*>(
                arg
            ),
            limit
        );
}

int32_t gorne_i32(
    uint64_t wartosc
) {
    return
        static_cast<int32_t>(
            static_cast<uint32_t>(
                wartosc >> 32
            )
        );
}

int32_t dolne_i32(
    uint64_t wartosc
) {
    return
        static_cast<int32_t>(
            static_cast<uint32_t>(
                wartosc &
                0xFFFFFFFFULL
            )
        );
}

/* =========================================================================
 * RESET / SHUTDOWN
 * ========================================================================= */

bool operacja_systemowa_dozwolona(
    const proces_t& proces
) {
    return
        proces_ma_prawo(
            proces,
            PRAWO_SYSTEM_CONFIG) &&
        proces_ma_poziom_co_najmniej(
            proces,
            PZB_ZAUFANE
        );
}

uint64_t reset_ps2() {
    wypisz_log("[POWER] Restart systemu.");
    asm volatile("cli" ::: "memory");
    if (acpi_wykonaj_restart()) {
        for (;;) asm volatile("hlt" ::: "memory");
    }
    wypisz_log("[POWER] Restart przez fallback 8042.");
    bool gotowe = false;

    for (uint32_t proby = 0;
         proby < 1000000U;
         ++proby) {

        uint8_t stan = 0;

        asm volatile(
            "inb %1, %0"
            : "=a"(stan)
            : "Nd"(
                static_cast<uint16_t>(
                    0x64)
              )
            : "memory"
        );

        if ((stan & 0x02U) == 0) {
            gotowe = true;
            break;
        }

        asm volatile(
            "pause"
        );
    }

    if (!gotowe) {
        return 0;
    }

    asm volatile(
        "outb %0, %1"
        :
        : "a"(
              static_cast<uint8_t>(
                  0xFE)
          ),
          "Nd"(
              static_cast<uint16_t>(
                  0x64)
          )
        : "memory"
    );

    /*
     * Jezeli kontroler poprawnie zresetuje CPU, wykonanie tu nie wroci.
     */
    for (;;) {
        asm volatile(
            "cli\n\t"
            "hlt"
            :
            :
            : "memory", "cc"
        );
    }
}

[[noreturn]]
void wylacz_qemu() {
    wypisz_log("[POWER] Wylaczanie systemu.");
    asm volatile("cli" ::: "memory");
    if (acpi_wykonaj_shutdown()) {
        for (;;) asm volatile("hlt" ::: "memory");
    }

#ifndef BURSZTYN_DEVELOPER_QEMU_POWER_FALLBACK
#define BURSZTYN_DEVELOPER_QEMU_POWER_FALLBACK 0
#endif
#if BURSZTYN_DEVELOPER_QEMU_POWER_FALLBACK
    wypisz_log("[POWER] Shutdown QEMU fallback.");
    /*
     * QEMU/Bochs compatibility ports.
     */
    asm volatile(
        "outw %0, %1"
        :
        : "a"(
              static_cast<uint16_t>(
                  0x2000)
          ),
          "Nd"(
              static_cast<uint16_t>(
                  0x604)
          )
        : "memory"
    );
    asm volatile(
        "outw %0, %1"
        :
        : "a"(
              static_cast<uint16_t>(
                  0x2000)
          ),
          "Nd"(
              static_cast<uint16_t>(
                  0xB004)
          )
        : "memory"
    );

    asm volatile(
        "outw %0, %1"
        :
        : "a"(
              static_cast<uint16_t>(
                  0x3400)
          ),
          "Nd"(
              static_cast<uint16_t>(
                  0x4004)
          )
        : "memory"
    );
#else
    wypisz_log("[POWER-WARN] Brak wspieranego mechanizmu shutdown.");
#endif

    for (;;) {
        asm volatile(
            "cli\n\t"
            "hlt"
            :
            :
            : "memory", "cc"
        );
    }
}

/* =========================================================================
 * BWS 35 - STERTA RING 3
 * ========================================================================= */

bool dodawanie_bez_przepelnienia(
    uint64_t a,
    uint64_t b,
    uint64_t* wynik
) {
    if (!wynik ||
        a >
            UINT64_MAX - b) {

        return false;
    }

    *wynik =
        a + b;

    return true;
}

uint64_t bws_alokuj_sterte(
    proces_t& proces,
    uint64_t rozmiar
) {
    /*
     * Bez statusu z ZmapujStrone() tylko walker VMM pozwala potwierdzic,
     * ze strona naprawde zostala opublikowana jako USER|WRITE.
     * Starszy VMM bez hooka nie jest wystarczajaco bezpieczny dla BWS 35.
     */
    if (!bws_vmm_sprawdz_zakres_uzytkownika) {
        return 0;
    }

    if (rozmiar == 0 ||
        rozmiar >
            MAX_ALOKACJA_STERTY_NA_BWS) {

        return 0;
    }

    if (rozmiar >
        UINT64_MAX -
            MASKA_STRONY) {

        return 0;
    }

    const uint64_t zaokraglony =
        (rozmiar +
         MASKA_STRONY) &
        ~MASKA_STRONY;

    if (zaokraglony == 0) {
        return 0;
    }

    uint64_t poczatek =
        proces.granica_sterty;

    if (poczatek <
        BAZA_STERTY_USER) {

        /*
         * Nie naprawiamy przypadkowego uszkodzenia deskryptora po cichu.
         */
        return 0;
    }

    if ((poczatek &
         MASKA_STRONY) != 0) {

        return 0;
    }

    uint64_t koniec = 0;

    if (!dodawanie_bez_przepelnienia(
            poczatek,
            zaokraglony,
            &koniec)) {

        return 0;
    }

    if (koniec >
        KONIEC_STERTY_USER) {

        return 0;
    }

    const uint64_t liczba_stron =
        zaokraglony /
        ROZMIAR_STRONY;

    uint64_t poprawnie_zmapowane =
        0;

    for (uint64_t i = 0;
         i < liczba_stron;
         ++i) {

        uint64_t adres = 0;

        if (!dodawanie_bez_przepelnienia(
                poczatek,
                i * ROZMIAR_STRONY,
                &adres)) {

            break;
        }

        /*
         * Istniejace mapowanie w obszarze "wolnej" sterty oznacza
         * niespojnosc deskryptora procesu.
         */
        if (bws_vmm_sprawdz_zakres_uzytkownika &&
            bws_vmm_sprawdz_zakres_uzytkownika(
                reinterpret_cast<void*>(
                    adres
                ),
                ROZMIAR_STRONY,
                true)) {

            break;
        }

        void* ramka =
            ZaalokujRamke();

        if (!ramka) {
            break;
        }

        /*
         * PMM obecnie wydaje ramki ponizej 1 GiB, wiec fizyczny adres
         * jest bezposrednio dostepny przez identity map kernela.
         */
        wyzeruj(
            ramka,
            ROZMIAR_STRONY
        );

        ZmapujStrone(
            reinterpret_cast<void*>(
                adres
            ),
            ramka,
            VMM_FLAGA_OBECNA |
            VMM_FLAGA_ZAPIS |
            VMM_FLAGA_USER
        );

        if (bws_vmm_sprawdz_zakres_uzytkownika) {
            if (!bws_vmm_sprawdz_zakres_uzytkownika(
                    reinterpret_cast<void*>(
                        adres
                    ),
                    ROZMIAR_STRONY,
                    true)) {

                /*
                 * ZmapujStrone() nadal zwraca void i VMM nie ma publicznego
                 * unmap(). Nie zwalniamy ramki po probie mapowania, bo
                 * moglaby pozostac aktywnie zmapowana z blednymi flagami.
                 *
                 * Ten adres nie jest publikowany aplikacji i zostanie
                 * pominiety przez przesuniecie granicy ponizej.
                 */
                ++poprawnie_zmapowane;
                break;
            }
        }

        ++poprawnie_zmapowane;
    }

    if (poprawnie_zmapowane !=
        liczba_stron) {

        /*
         * Nie ma jeszcze atomowego map/unmap, dlatego nie mozemy wykonac
         * bezpiecznego rollbacku juz opublikowanych PTE.
         *
         * Przesuwamy granice za strony, ktore mogly zostac zmapowane, aby
         * kolejna alokacja nigdy ich nie wykorzystala drugi raz.
         */
        uint64_t nowa_granica =
            poczatek;

        const uint64_t zuzyto =
            poprawnie_zmapowane *
            ROZMIAR_STRONY;

        if (dodawanie_bez_przepelnienia(
                poczatek,
                zuzyto,
                &nowa_granica) &&
            nowa_granica <=
                KONIEC_STERTY_USER) {

            proces.granica_sterty =
                nowa_granica;

            aktywny_proces.granica_sterty =
                nowa_granica;
        }

        return 0;
    }

    proces.granica_sterty =
        koniec;

    aktywny_proces.granica_sterty =
        koniec;

    return poczatek;
}

/* =========================================================================
 * ZAMYKANIE PROCESU
 * ========================================================================= */

[[noreturn]]
void bws_zakoncz_biezacy_proces() {
    {
        BlokadaEkranu blokada;

        if (scheduler_pid_uzytkownika(
                aktualny_pid)) {

            if (aktualny_pid < static_cast<int>(PZB_MAKS_PROCESOW)) {
                StanDropProcesu& drop = cele_drop[aktualny_pid];
                if (drop.ostatni_pid_celu > 0)
                    wyslij_zdarzenie_drop(drop.ostatni_pid_celu,
                        BWS_ZDARZENIE_DRAG_LEAVE, 0, 0, 0);
                drop = {};
            }

            usun_warstwe(
                aktualny_pid
            );

            bws_gui_powiadom_lifecycle(
                BWS_ZDARZENIE_OKNO_ZAMKNIETE,
                aktualny_pid
            );

            bws_gui_usun_stan_procesu(aktualny_pid);

            bws_gui_zwolnij_mysz_procesu(
                aktualny_pid
            );
        }

        /* usun_warstwe() zachowuje stary rect i oznacza dokladnie ten
           obszar. Pelnoekranowy dirty przy kazdym SYS_EXIT byl zbedny. */
    }

    zakoncz_aktualny_proces();

    /*
     * Funkcja schedulera jest semantycznie noreturn, ale jej naglowek
     * zachowuje jeszcze zwykle void dla zgodnosci ABI.
     */
    for (;;) {
        asm volatile(
            "pause"
        );
    }
}

} // namespace

/* =========================================================================
 * INICJALIZACJA SYSCALL / MSR
 * ========================================================================= */

extern "C" void inicjalizuj_syscalls() {
    /*
     * Szczyt bootowego stosu jadra.
     * Scheduler pozniej podmienia wartosc przy kazdym context switch.
     */
    bezpieczny_stos_jadra =
        reinterpret_cast<uint64_t>(
            stack_top
        );

    /*
     * Kernelowy GS musi byc AKTYWNY podczas konfiguracji. Przy pierwszym
     * powrocie do Ring 3 poprawione przerwania.S wykona SWAPGS i przeniesie
     * ten adres do IA32_KERNEL_GS_BASE.
     */
    zapisz_msr(
        MSR_GS_BASE,
        reinterpret_cast<uint64_t>(
            &bezpieczny_stos_jadra
        )
    );

    zapisz_msr(
        MSR_KERNEL_GS_BASE,
        0
    );

    uint64_t efer =
        odczytaj_msr(
            MSR_EFER
        );

    efer |=
        EFER_SCE;

    zapisz_msr(
        MSR_EFER,
        efer
    );

    const uint64_t star =
        (static_cast<uint64_t>(
             STAR_USER_BASE) << 48) |
        (static_cast<uint64_t>(
             STAR_KERNEL_CS) << 32);

    zapisz_msr(
        MSR_STAR,
        star
    );

    zapisz_msr(
        MSR_LSTAR,
        reinterpret_cast<uint64_t>(
            &brama_wywolan_systemowych
        )
    );

    zapisz_msr(
        MSR_FMASK,
        SYSCALL_FMASK
    );
}

/* =========================================================================
 * GLOWNY DISPATCHER BWS
 * ========================================================================= */

extern "C" uint64_t obsluga_wywolan_systemowych(
    uint64_t nr_funkcji,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4
) {
    proces_t* proces_ptr =
        pobierz_proces_wywolujacy();

    /*
     * Ten dispatcher jest wejściem Ring 3. Brak poprawnego procesu Ring 3
     * oznacza uszkodzony kontekst lub nieprawidlowe wywolanie.
     */
    if (!proces_ptr) {
        return 0;
    }

    proces_t& proces =
        *proces_ptr;

    switch (nr_funkcji) {
        /* -------------------------------------------------------------
         * Terminal / pliki
         * ---------------------------------------------------------- */

        case 1:
            return
                bws_wypisz(
                    arg1
                );

        case 2:
            return
                bws_utworz_plik(
                    proces,
                    arg1
                );

        case 3:
            return
                bws_zapisz_plik(
                    proces,
                    arg1,
                    arg2,
                    arg3
                );

        case 4: {
            const char znak=scheduler_pobierz_klawisz_lub_zablokuj(aktualny_pid);
            return static_cast<uint64_t>(static_cast<uint8_t>(znak));
        }

        case 5:
            return
                bws_czytaj_plik(
                    proces,
                    arg1,
                    arg2,
                    arg3
                );

        case 6:
            return
                bws_wylistuj_katalog(
                    proces,
                    arg1,
                    arg2,
                    arg3
                );

        case 7:
            return
                bws_usun_twor(
                    proces,
                    arg1
                );

        case 8:
            return
                bws_zmien_nazwe(
                    proces,
                    arg1,
                    arg2
                );

        case 9:
            return
                bws_rtc(
                    arg1
                );

        case 10:
            return
                bws_uruchom(
                    proces,
                    arg1,
                    arg2
                );

        /* -------------------------------------------------------------
         * Siec - legacy
         * ---------------------------------------------------------- */

        case 11: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_SIEC)) {
                wypisz_log("[ICMP] result=NET_ERR_PERMISSION");
                return 0;
            }

            return bws_siec_ping(
                static_cast<uint8_t>(
                    arg1),
                static_cast<uint8_t>(
                    arg2),
                static_cast<uint8_t>(
                    arg3),
                static_cast<uint8_t>(
                    arg4)
            );
        }

        case 12:
            /*
             * Legacy alias DNS. Zachowany dla starych .bur.
             */
            return
                bws_dns(
                    proces,
                    arg1,
                    arg2
                );

        case 13:
            /*
             * Legacy HTTP -> plik.
             */
            return
                bws_legacy_http_do_pliku(
                    proces,
                    arg1,
                    arg2,
                    arg3,
                    arg4
                );

        /* -------------------------------------------------------------
         * GUI
         * ---------------------------------------------------------- */

        case 14: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI) ||
                !waliduj_tekst_gui(
                    arg3,
                    64)) {

                return 0;
            }

            const int x =
                gorne_i32(arg1);

            const int y =
                dolne_i32(arg1);

            const int w =
                gorne_i32(arg2);

            const int h =
                dolne_i32(arg2);

            BlokadaEkranu blokada;

            bws_gui_rysuj_okno(
                x,
                y,
                w,
                h,
                reinterpret_cast<const char*>(
                    arg3
                )
            );

            return 1;
        }

        case 15: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI) ||
                !waliduj_tekst_gui(
                    arg3,
                    1024)) {

                return 0;
            }

            BlokadaEkranu blokada;

            bws_gui_wypisz_tekst(
                static_cast<int32_t>(
                    arg1
                ),
                static_cast<int32_t>(
                    arg2
                ),
                reinterpret_cast<const char*>(
                    arg3
                )
            );

            return 1;
        }

        case 16: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            BlokadaEkranu blokada;

            bws_gui_wyczyscz_obszar(
                static_cast<int32_t>(
                    arg1
                ),
                static_cast<int32_t>(
                    arg2
                ),
                static_cast<int32_t>(
                    arg3
                ),
                static_cast<int32_t>(
                    arg4
                )
            );

            return 1;
        }

        case 17: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            BlokadaEkranu blokada;

            bws_gui_odswiez();

            return 1;
        }

        case 18: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI) ||
                arg1 == 0 ||
                arg2 == 0 ||
                arg3 == 0) {

                return 0;
            }

            /*
             * grafika.cpp wykonuje finalna walidacje copy_to_user.
             */
            if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(
                        arg1
                    ),
                    sizeof(int)) ||
                !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(
                        arg2
                    ),
                    sizeof(int)) ||
                !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(
                        arg3
                    ),
                    sizeof(uint8_t))) {

                return 0;
            }

            bws_gui_pobierz_mysz(
                reinterpret_cast<int*>(
                    arg1
                ),
                reinterpret_cast<int*>(
                    arg2
                ),
                reinterpret_cast<uint8_t*>(
                    arg3
                )
            );

            return 1;
        }

        case 19: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            BlokadaEkranu blokada;

            bws_gui_odswiez_pulpit();

            return 1;
        }

        case 20: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI) ||
                !waliduj_tekst_gui(
                    arg4,
                    1024)) {

                return 0;
            }

            BlokadaEkranu blokada;

            bws_gui_wypisz_tekst_kolor(
                static_cast<int32_t>(
                    arg1
                ),
                static_cast<int32_t>(
                    arg2
                ),
                arg3,
                reinterpret_cast<const char*>(
                    arg4
                )
            );

            return 1;
        }

        case 21: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            const int x =
                gorne_i32(arg1);

            const int y =
                dolne_i32(arg1);

            const int w =
                gorne_i32(arg2);

            const int h =
                dolne_i32(arg2);

            BlokadaEkranu blokada;

            bws_gui_rysuj_prostokat(
                x,
                y,
                w,
                h,
                static_cast<uint32_t>(
                    arg3
                )
            );

            return 1;
        }

        case 22: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            bws_gui_ustaw_przejecie_myszy(
                arg1 != 0
            );

            return 1;
        }

        case 23: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI) ||
                arg1 == 0 ||
                arg2 == 0) {

                return 0;
            }

            if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(
                        arg1
                    ),
                    sizeof(int)) ||
                !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(
                        arg2
                    ),
                    sizeof(int))) {

                return 0;
            }

            bws_gui_pobierz_rozdzielczosc(
                reinterpret_cast<int*>(
                    arg1
                ),
                reinterpret_cast<int*>(
                    arg2
                )
            );

            return 1;
        }

        case 24: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            const int wynik =
                bws_gui_pobierz_szerokosc_znaku(
                    static_cast<uint32_t>(
                        arg1
                    )
                );

            return
                wynik > 0
                    ? static_cast<uint64_t>(
                        wynik)
                    : 0ULL;
        }

        /* -------------------------------------------------------------
         * Operacje systemowe / audio
         * ---------------------------------------------------------- */

        case 25: {
            if (!operacja_systemowa_dozwolona(
                    proces)) {

                return 0;
            }

            return
                reset_ps2();
        }

        case 26: {
            if (!operacja_systemowa_dozwolona(
                    proces)) {

                return 0;
            }

            wylacz_qemu();
        }

        case 27: {
            /*
             * Brak osobnego PRAWO_DZWIEK w obecnym PZB.
             * Do czasu rozszerzenia maski wymagamy PRAWO_GUI jako prawa
             * aplikacyjnego do urzadzen multimedialnych.
             */
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            const uint32_t hz =
                static_cast<uint32_t>(
                    arg1
                );

            const uint32_t ms =
                static_cast<uint32_t>(
                    arg2
                );

            if (hz < 20U ||
                hz > 20000U ||
                ms == 0 ||
                ms > 10000U) {

                return 0;
            }

            return
                hda_test_ton(
                    hz,
                    ms)
                    ? 1ULL
                    : 0ULL;
        }

        /* -------------------------------------------------------------
         * Nowe API sieciowe
         * ---------------------------------------------------------- */

        case 28:
            return
                bws_dns(
                    proces,
                    arg1,
                    arg2
                );

        case 29:
            return
                bws_http_common(
                    proces,
                    false,
                    arg1,
                    arg2,
                    arg3,
                    arg4
                );

        case 30:
            return
                bws_http_common(
                    proces,
                    true,
                    arg1,
                    arg2,
                    arg3,
                    arg4
                );

        case 31: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_SIEC)) {

                return 0;
            }

            return
                kernel_tls_certyfikat_zaufany()
                    ? 1ULL
                    : 0ULL;
        }

        /* -------------------------------------------------------------
         * Proces / warstwy
         * ---------------------------------------------------------- */

        case 32:
            bws_zakoncz_biezacy_proces();

        case 33: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            const int x =
                gorne_i32(arg1);

            const int y =
                dolne_i32(arg1);

            const int szer =
                gorne_i32(arg2);

            const int wys =
                dolne_i32(arg2);

            const int z_order =
                static_cast<int32_t>(
                    arg3
                );

            if (szer <= 0 ||
                wys <= 0) {

                return 0;
            }

            BlokadaEkranu blokada;

            const int id =
                utworz_warstwe(
                    aktualny_pid,
                    x,
                    y,
                    szer,
                    wys,
                    z_order
                );

            if (id >= 0)
                bws_gui_aktywuj_warstwe(aktualny_pid);
            if (id >= 0)
                bws_gui_powiadom_lifecycle(BWS_ZDARZENIE_OKNO_UTWORZONE,
                                           aktualny_pid);

            return
                id >= 0
                    ? static_cast<uint64_t>(
                        id + 1)
                    : 0ULL;
        }

        case 34: {
            if (!proces_ma_prawo(
                    proces,
                    PRAWO_GUI)) {

                return 0;
            }

            BlokadaEkranu blokada;

            zaktualizuj_pozycje_warstwy(
                aktualny_pid,
                static_cast<int32_t>(
                    arg1
                ),
                static_cast<int32_t>(
                    arg2
                )
            );

            return 1;
        }

        case 35:
            return
                bws_alokuj_sterte(
                    proces,
                    arg1
                );

        case 36:
            return
                gui_czy_zamknieto_powloke()
                    ? 1ULL
                    : 0ULL;

        case 37:
        case 38: {
            if (!proces_ma_prawo(proces, PRAWO_GUI) || arg1 == 0 ||
                !czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(arg1), sizeof(bws_zdarzenie)))
                return 0;
            bws_zdarzenie zdarzenie{};
            if (!scheduler_pobierz_zdarzenie(aktualny_pid, &zdarzenie)) {
                if (nr_funkcji == 38)
                    scheduler_czekaj_na_zdarzenie(aktualny_pid);
                return 0;
            }
            return skopiuj_do_przestrzeni_uzytkownika(
                       reinterpret_cast<void*>(arg1), &zdarzenie,
                       sizeof(zdarzenie)) ? 1ULL : 0ULL;
        }

        case 39:
            if (!proces_ma_prawo(proces, PRAWO_GUI)) return 0;
            bws_gui_ustaw_capture(arg1 != 0);
            return 1;

        case 40:
            if (!operacja_systemowa_dozwolona(proces) ||
                !proces_ma_prawo(proces, PRAWO_GUI)) return 0;
            skladacz_obrazu_ustaw_overlay(
                aktualny_pid, arg1 != 0, gorne_i32(arg2), dolne_i32(arg2),
                gorne_i32(arg3), dolne_i32(arg3));
            return 1;

        case 41:
            if (!proces_ma_prawo(proces, PRAWO_GUI)) return 0;
            return bws_gui_minimalizuj_warstwe(aktualny_pid) ? 1ULL : 0ULL;

        case 42: {
            if (!operacja_systemowa_dozwolona(proces) || arg1 == 0 ||
                arg2 == 0 || arg2 > SKLADACZ_MAKS_WARSTW) return 0;
            const uint32_t max = static_cast<uint32_t>(arg2);
            const size_t bytes = static_cast<size_t>(max) * sizeof(GuiOknoInfo);
            if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(arg1), bytes)) return 0;
            GuiOknoInfo snapshot[SKLADACZ_MAKS_WARSTW] = {};
            const uint32_t count = skladacz_obrazu_snapshot_okien(
                snapshot, max, bws_gui_aktywny_pid());
            return skopiuj_do_przestrzeni_uzytkownika(
                       reinterpret_cast<void*>(arg1), snapshot,
                       static_cast<size_t>(count) * sizeof(GuiOknoInfo))
                       ? count : 0ULL;
        }

        case 43:
            if (!operacja_systemowa_dozwolona(proces) || arg1 == 0) return 0;
            return bws_gui_aktywuj_okno(arg1) ? 1ULL : 0ULL;

        case 44: {
            if (!proces_ma_prawo(proces, PRAWO_PLIKI_CZYTAJ)) return 0;
            char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
            if (!pobierz_sciezke_pliku(arg1, sciezka)) return 0;
            /* +1 zachowuje 0 jako blad i pozwala reprezentowac pusty plik. */
            return static_cast<uint64_t>(rozmiar_pliku(sciezka)) + 1ULL;
        }

        case 45: {
            if (arg1 == 0 || arg2 == 0 || arg2 > MAX_SCIEZKA_PLIKU_BWS)
                return 0;
            if (!czy_bezpieczny_zakres_uzytkownika_do_zapisu(
                    reinterpret_cast<void*>(arg1), static_cast<size_t>(arg2)))
                return 0;
            char argument[MAX_SCIEZKA_PLIKU_BWS] = {};
            if (!loader_pobierz_argument_startowy(
                    aktualny_pid, argument, sizeof(argument))) return 0;
            const size_t len = dlugosc_stringa_limit(argument, sizeof(argument));
            if (len >= sizeof(argument) || len + 1U > arg2) return 0;
            return skopiuj_do_przestrzeni_uzytkownika(
                       reinterpret_cast<void*>(arg1), argument, len + 1U)
                ? 1ULL : 0ULL;
        }

        case 46: {
            char sciezka[MAX_SCIEZKA_PLIKU_BWS] = {};
            if (!pobierz_sciezke_pliku(arg1, sciezka)) return 0;
            if (!proces_moze_utworzyc_w_sciezce(proces, sciezka)) {
                wypisz_log("[PZB] directory_create=DENY");
                return 0;
            }
            wypisz_log("[PZB] directory_create=ALLOW");
            const bool wynik = utworz_katalog_z_pzb(
                sciezka, proces.poziom_zaufania);
            if (wynik) powiadom_zmiane_systemu_plikow();
            return wynik ? 1ULL : 0ULL;
        }

        case 47:
            return bws_pobierz_metadane(proces, arg1, arg2);

        case 48:
            return bws_przenies(proces, arg1, arg2);

        case 49:
            return bws_rejestruj_cele_drop(proces, arg1, arg2);

        case 50:
            return bws_aktualizuj_drag(proces, arg1, arg2, arg3);

        case 51:
            /* extronic16B_unicode.h: jeden glif ma 16 wierszy bitmapy. */
            return proces_ma_prawo(proces, PRAWO_GUI) ? 16ULL : 0ULL;

        case 52:
            return bws_ustaw_schowek_plikow(proces, arg1, arg2);

        case 53:
            return bws_pobierz_schowek_plikow(proces, arg1);

        case 54:
            return bws_wyczysc_schowek_plikow(proces, arg1);

        case 55:
            return bws_kopiuj_twor(proces, arg1, arg2);

        case 56:
            if (!proces_ma_prawo(proces, PRAWO_GUI)) return 0;
            if (arg1 != 0 && bws_gui_aktywny_pid() != aktualny_pid) return 0;
            return skladacz_obrazu_ustaw_popup_aplikacji(
                aktualny_pid, arg1 != 0, gorne_i32(arg2), dolne_i32(arg2),
                gorne_i32(arg3), dolne_i32(arg3)) ? 1ULL : 0ULL;

        default:
            /*
             * Nie wypisujemy user-controlled numeru ani nie zalewamy logu.
             */
            return UINT64_MAX;
    }
}
