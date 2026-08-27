/*
 * Bursztyn OS - biblioteka GUI dla aplikacji Ring 3.
 * Wrappery BWS, prywatna sterta procesu i podstawowe widgety.
 */

#include "bursztyn_gui.h"

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * 1. PRYWATNA STERTA APLIKACJI RING 3
 * ========================================================================= */

namespace {

constexpr unsigned long WYROWNANIE_STERTY = 16UL;
constexpr unsigned long ROZMIAR_STRONY = 4096UL;
constexpr unsigned long MINIMALNY_BLOK = 16UL;

struct alignas(16) BlokPamieci {
    unsigned long rozmiar;
    bool wolny;
    BlokPamieci* nastepny;
};

static_assert(alignof(BlokPamieci) == 16,
              "BlokPamieci musi miec wyrownanie 16 bajtow");
static_assert((sizeof(BlokPamieci) % 16) == 0,
              "BlokPamieci musi miec rozmiar wielokrotnosci 16 bajtow");

BlokPamieci* poczatek_sterty = nullptr;

bool dodaj_bez_przepelnienia(unsigned long a,
                             unsigned long b,
                             unsigned long* wynik) {
    if (!wynik) return false;
    if (a > ~0UL - b) return false;
    *wynik = a + b;
    return true;
}

bool wyrownaj_w_gore(unsigned long wartosc,
                     unsigned long wyrownanie,
                     unsigned long* wynik) {
    if (!wynik || wyrownanie == 0) return false;
    if ((wyrownanie & (wyrownanie - 1UL)) != 0) return false;

    const unsigned long maska = wyrownanie - 1UL;
    if (wartosc > ~0UL - maska) return false;

    *wynik = (wartosc + maska) & ~maska;
    return true;
}

bool bloki_sasiaduja(const BlokPamieci* lewy,
                     const BlokPamieci* prawy) {
    if (!lewy || !prawy) return false;

    const uintptr_t adres_lewego =
        reinterpret_cast<uintptr_t>(lewy);
    const uintptr_t adres_prawego =
        reinterpret_cast<uintptr_t>(prawy);

    if (adres_lewego > UINTPTR_MAX - sizeof(BlokPamieci))
        return false;

    const uintptr_t dane =
        adres_lewego + sizeof(BlokPamieci);

    if (lewy->rozmiar > UINTPTR_MAX - dane)
        return false;

    return dane + lewy->rozmiar == adres_prawego;
}

void polacz_z_nastepnym(BlokPamieci* blok) {
    if (!blok || !blok->wolny || !blok->nastepny)
        return;

    BlokPamieci* nastepny = blok->nastepny;

    if (!nastepny->wolny ||
        !bloki_sasiaduja(blok, nastepny)) {
        return;
    }

    unsigned long temp = 0;
    unsigned long nowy_rozmiar = 0;

    if (!dodaj_bez_przepelnienia(
            blok->rozmiar,
            static_cast<unsigned long>(sizeof(BlokPamieci)),
            &temp)) {
        return;
    }

    if (!dodaj_bez_przepelnienia(
            temp,
            nastepny->rozmiar,
            &nowy_rozmiar)) {
        return;
    }

    blok->rozmiar = nowy_rozmiar;
    blok->nastepny = nastepny->nastepny;
}

uint64_t spakuj_dwa_i32(int a, int b) {
    return (static_cast<uint64_t>(
                static_cast<uint32_t>(a)) << 32) |
           static_cast<uint64_t>(
                static_cast<uint32_t>(b));
}

uint64_t i32_do_u64(int wartosc) {
    return static_cast<uint64_t>(
        static_cast<int64_t>(wartosc));
}

int popraw_skale(int skala) {
    if (skala < 1) return 1;
    if (skala > 4) return 4;
    return skala;
}

size_t dekoduj_utf8_gui(const char* tekst, size_t i, uint32_t* znak) {
    if (!tekst || !znak || tekst[i] == '\0') return 0;
    const uint8_t b0 = static_cast<uint8_t>(tekst[i]);
    *znak = b0;
    if (b0 < 0xC2U || b0 > 0xF4U) return 1;
    const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
    if (b1 == 0 || (b1 & 0xC0U) != 0x80U) return 1;
    if (b0 <= 0xDFU) {
        *znak = (static_cast<uint32_t>(b0 & 0x1FU) << 6) |
                static_cast<uint32_t>(b1 & 0x3FU);
        return 2;
    }
    const uint8_t b2 = static_cast<uint8_t>(tekst[i + 2]);
    if (b2 == 0 || (b2 & 0xC0U) != 0x80U) return 1;
    if (b0 <= 0xEFU) {
        if ((b0 == 0xE0U && b1 < 0xA0U) ||
            (b0 == 0xEDU && b1 > 0x9FU)) return 1;
        *znak = (static_cast<uint32_t>(b0 & 0x0FU) << 12) |
                (static_cast<uint32_t>(b1 & 0x3FU) << 6) |
                static_cast<uint32_t>(b2 & 0x3FU);
        return 3;
    }
    const uint8_t b3 = static_cast<uint8_t>(tekst[i + 3]);
    if (b3 == 0 || (b3 & 0xC0U) != 0x80U ||
        (b0 == 0xF0U && b1 < 0x90U) ||
        (b0 == 0xF4U && b1 > 0x8FU)) return 1;
    *znak = (static_cast<uint32_t>(b0 & 0x07U) << 18) |
            (static_cast<uint32_t>(b1 & 0x3FU) << 12) |
            (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
            static_cast<uint32_t>(b3 & 0x3FU);
    return 4;
}

} // namespace

void* gui_malloc(unsigned long rozmiar) {
    if (rozmiar == 0)
        return nullptr;

    unsigned long potrzebne = 0;
    if (!wyrownaj_w_gore(
            rozmiar,
            WYROWNANIE_STERTY,
            &potrzebne)) {
        return nullptr;
    }

    BlokPamieci* ostatni = nullptr;

    for (BlokPamieci* blok = poczatek_sterty;
         blok != nullptr;
         blok = blok->nastepny) {

        ostatni = blok;

        if (!blok->wolny ||
            blok->rozmiar < potrzebne) {
            continue;
        }

        const unsigned long reszta =
            blok->rozmiar - potrzebne;

        if (reszta >=
            sizeof(BlokPamieci) + MINIMALNY_BLOK) {

            unsigned char* dane =
                reinterpret_cast<unsigned char*>(blok + 1);

            BlokPamieci* nowy =
                reinterpret_cast<BlokPamieci*>(
                    dane + potrzebne);

            nowy->rozmiar =
                reszta - sizeof(BlokPamieci);
            nowy->wolny = true;
            nowy->nastepny = blok->nastepny;

            blok->rozmiar = potrzebne;
            blok->nastepny = nowy;
        }

        blok->wolny = false;
        return static_cast<void*>(blok + 1);
    }

    unsigned long zamawiane = 0;
    if (!dodaj_bez_przepelnienia(
            potrzebne,
            static_cast<unsigned long>(sizeof(BlokPamieci)),
            &zamawiane)) {
        return nullptr;
    }

    unsigned long przydzielone = 0;
    if (!wyrownaj_w_gore(
            zamawiane,
            ROZMIAR_STRONY,
            &przydzielone)) {
        return nullptr;
    }

    void* surowa_pamiec =
        reinterpret_cast<void*>(
            bws_wywolaj(
                35,
                static_cast<uint64_t>(przydzielone),
                0, 0, 0));

    if (!surowa_pamiec)
        return nullptr;

    if ((reinterpret_cast<uintptr_t>(surowa_pamiec) &
         (WYROWNANIE_STERTY - 1UL)) != 0) {
        return nullptr;
    }

    BlokPamieci* nowy =
        static_cast<BlokPamieci*>(surowa_pamiec);

    nowy->rozmiar =
        przydzielone - sizeof(BlokPamieci);
    nowy->wolny = false;
    nowy->nastepny = nullptr;

    if (ostatni)
        ostatni->nastepny = nowy;
    else
        poczatek_sterty = nowy;

    const unsigned long reszta =
        nowy->rozmiar - potrzebne;

    if (reszta >=
        sizeof(BlokPamieci) + MINIMALNY_BLOK) {

        unsigned char* dane =
            reinterpret_cast<unsigned char*>(nowy + 1);

        BlokPamieci* wolna_reszta =
            reinterpret_cast<BlokPamieci*>(
                dane + potrzebne);

        wolna_reszta->rozmiar =
            reszta - sizeof(BlokPamieci);
        wolna_reszta->wolny = true;
        wolna_reszta->nastepny = nullptr;

        nowy->rozmiar = potrzebne;
        nowy->nastepny = wolna_reszta;
    }

    return static_cast<void*>(nowy + 1);
}

void gui_free(void* ptr) {
    if (!ptr || !poczatek_sterty)
        return;

    BlokPamieci* poprzedni = nullptr;
    BlokPamieci* blok = poczatek_sterty;

    while (blok &&
           static_cast<void*>(blok + 1) != ptr) {
        poprzedni = blok;
        blok = blok->nastepny;
    }

    if (!blok || blok->wolny)
        return;

    blok->wolny = true;

    polacz_z_nastepnym(blok);

    if (poprzedni && poprzedni->wolny)
        polacz_z_nastepnym(poprzedni);
}

void* operator new(unsigned long rozmiar) {
    if (rozmiar == 0) rozmiar = 1;
    return gui_malloc(rozmiar);
}

void* operator new[](unsigned long rozmiar) {
    if (rozmiar == 0) rozmiar = 1;
    return gui_malloc(rozmiar);
}

void operator delete(void* p) noexcept {
    gui_free(p);
}

void operator delete[](void* p) noexcept {
    gui_free(p);
}

void operator delete(void* p, unsigned long) noexcept {
    gui_free(p);
}

void operator delete[](void* p, unsigned long) noexcept {
    gui_free(p);
}

/* =========================================================================
 * 2. GLOWNE WYWOLANIE SYSTEMOWE BWS
 * ========================================================================= */

uint64_t bws_wywolaj(uint64_t nr_funkcji,
                     uint64_t arg1,
                     uint64_t arg2,
                     uint64_t arg3,
                     uint64_t arg4) {
    register uint64_t r8  asm("r8")  = nr_funkcji;
    register uint64_t r9  asm("r9")  = arg1;
    register uint64_t r10 asm("r10") = arg2;
    register uint64_t r12 asm("r12") = arg3;
    register uint64_t r13 asm("r13") = arg4;
    register uint64_t rax asm("rax");

    asm volatile(
        "syscall"
        : "=a"(rax)
        : "r"(r8),
          "r"(r9),
          "r"(r10),
          "r"(r12),
          "r"(r13)
        : "rcx", "r11", "memory", "cc"
    );

    return rax;
}

/* =========================================================================
 * 3. STANDARDOWE API SYSTEMOWE
 * ========================================================================= */

void wypisz(const char* tekst) {
    if (!tekst) return;

    bws_wywolaj(
        1,
        reinterpret_cast<uint64_t>(tekst),
        0, 0, 0);
}

bool utworz(const char* sciezka) {
    if (!sciezka) return false;

    return bws_wywolaj(
        2,
        reinterpret_cast<uint64_t>(sciezka),
        0, 0, 0) != 0;
}

bool utworz_katalog_uzytkownika(const char* sciezka) {
    if (!sciezka) return false;
    return bws_wywolaj(
        46,
        reinterpret_cast<uint64_t>(sciezka),
        0, 0, 0) != 0;
}

bool zapisz_plik(const char* sciezka,
                 const char* dane,
                 uint32_t dlugosc) {
    if (!sciezka) return false;
    if (dlugosc != 0 && !dane) return false;

    return bws_wywolaj(
        3,
        reinterpret_cast<uint64_t>(sciezka),
        reinterpret_cast<uint64_t>(dane),
        static_cast<uint64_t>(dlugosc),
        0) != 0;
}

char pobierz_znak() {
    return static_cast<char>(
        bws_wywolaj(4, 0, 0, 0, 0));
}

bool czytaj_plik(const char* sciezka,
                 char* bufor,
                 uint32_t maksymalna_dlugosc) {
    if (!sciezka) return false;
    if (maksymalna_dlugosc != 0 && !bufor) return false;

    return bws_wywolaj(
        5,
        reinterpret_cast<uint64_t>(sciezka),
        reinterpret_cast<uint64_t>(bufor),
        static_cast<uint64_t>(maksymalna_dlugosc),
        0) != 0;
}

bool wylistuj_katalog_uzytkownika(const char* sciezka,
                                  char* bufor,
                                  uint32_t maksymalna_dlugosc) {
    if (!sciezka || !bufor || maksymalna_dlugosc == 0) return false;
    return bws_wywolaj(
        6,
        reinterpret_cast<uint64_t>(sciezka),
        reinterpret_cast<uint64_t>(bufor),
        static_cast<uint64_t>(maksymalna_dlugosc),
        0) != 0;
}

bool uruchom_program_uzytkownika(const char* sciezka) {
    if (!sciezka) return false;
    return bws_wywolaj(
        10,
        reinterpret_cast<uint64_t>(sciezka),
        0, 0, 0) != 0;
}

bool uruchom_program_z_argumentem_uzytkownika(const char* program,
                                               const char* argument) {
    if (!program || !argument) return false;
    return bws_wywolaj(
        10,
        reinterpret_cast<uint64_t>(program),
        reinterpret_cast<uint64_t>(argument),
        0, 0) != 0;
}

bool pobierz_argument_startowy(char* bufor, uint32_t pojemnosc) {
    if (!bufor || pojemnosc == 0) return false;
    return bws_wywolaj(
        45,
        reinterpret_cast<uint64_t>(bufor),
        static_cast<uint64_t>(pojemnosc),
        0, 0) != 0;
}

namespace {

bool tekst_konczy_sie_ci(const char* tekst, const char* sufiks) {
    if (!tekst || !sufiks) return false;
    size_t lt = 0;
    size_t ls = 0;
    while (lt < 511U && tekst[lt] != '\0') ++lt;
    while (ls < 32U && sufiks[ls] != '\0') ++ls;
    if (lt == 511U || ls == 32U || ls > lt) return false;
    for (size_t i = 0; i < ls; ++i) {
        char a = tekst[lt - ls + i];
        char b = sufiks[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
        if (a != b) return false;
    }
    return true;
}

} // namespace

wynik_otwarcia_skojarzonego otworz_plik_skojarzony(const char* sciezka) {
    if (!sciezka || sciezka[0] != '/') return OTWORZ_PLIK_BLAD;
    if (tekst_konczy_sie_ci(sciezka, ".txt")) {
        return uruchom_program_z_argumentem_uzytkownika(
                   "/programy/notatnik.cebula/notatnik.bur", sciezka)
            ? OTWORZ_PLIK_URUCHOMIONO
            : OTWORZ_PLIK_BLAD;
    }
    if (tekst_konczy_sie_ci(sciezka, ".bmp")) {
        return uruchom_program_z_argumentem_uzytkownika(
                   "/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur", sciezka)
            ? OTWORZ_PLIK_URUCHOMIONO
            : OTWORZ_PLIK_BLAD;
    }
    return OTWORZ_PLIK_BRAK_SKOJARZENIA;
}

bool pobierz_rozmiar_pliku(const char* sciezka, uint32_t* rozmiar) {
    if (!sciezka || !rozmiar) return false;
    const uint64_t wynik = bws_wywolaj(
        44, reinterpret_cast<uint64_t>(sciezka), 0, 0, 0);
    if (wynik == 0 || wynik > static_cast<uint64_t>(UINT32_MAX) + 1ULL)
        return false;
    *rozmiar = static_cast<uint32_t>(wynik - 1ULL);
    return true;
}

void bws_dzwiek_test(uint32_t czestotliwosc,
                     uint32_t czas) {
    bws_wywolaj(
        27,
        static_cast<uint64_t>(czestotliwosc),
        static_cast<uint64_t>(czas),
        0, 0);
}

extern "C" {

bool bws_siec_dns(const char* domena,
                  uint8_t* wyjsciowy_ip) {
    if (!domena || !wyjsciowy_ip)
        return false;

    return bws_wywolaj(
        28,
        reinterpret_cast<uint64_t>(domena),
        reinterpret_cast<uint64_t>(wyjsciowy_ip),
        0, 0) != 0;
}

bool bws_siec_pobierz_http(uint8_t* cel_ip,
                           const char* domena,
                           const char* sciezka,
                           char* bufor,
                           uint32_t max_dlugosc) {
    if (!cel_ip || !domena || !sciezka)
        return false;

    if (max_dlugosc != 0 && !bufor)
        return false;

    /*
     * Zachowanie zgodnosci z aktualnym ABI BWS 29.
     * Arg4 zawiera wskaznik i rozmiar w formacie oczekiwanym
     * przez obecna implementacje jadra.
     */
    const uint64_t bufor_i_rozmiar =
        (reinterpret_cast<uint64_t>(bufor) << 32) |
        static_cast<uint64_t>(max_dlugosc);

    return bws_wywolaj(
        29,
        reinterpret_cast<uint64_t>(cel_ip),
        reinterpret_cast<uint64_t>(domena),
        reinterpret_cast<uint64_t>(sciezka),
        bufor_i_rozmiar) != 0;
}

bool bws_siec_pobierz_https(uint8_t* cel_ip,
                            const char* domena,
                            const char* sciezka,
                            char* bufor,
                            uint32_t max_dlugosc) {
    if (!cel_ip || !domena || !sciezka)
        return false;

    if (max_dlugosc != 0 && !bufor)
        return false;

    const uint64_t bufor_i_rozmiar =
        (reinterpret_cast<uint64_t>(bufor) << 32) |
        static_cast<uint64_t>(max_dlugosc);

    return bws_wywolaj(
        30,
        reinterpret_cast<uint64_t>(cel_ip),
        reinterpret_cast<uint64_t>(domena),
        reinterpret_cast<uint64_t>(sciezka),
        bufor_i_rozmiar) != 0;
}

bool bws_tls_certyfikat_zaufany() {
    return bws_wywolaj(
        31, 0, 0, 0, 0) != 0;
}

__attribute__((noreturn))
void bws_zakoncz_proces() {
    bws_wywolaj(
        32, 0, 0, 0, 0);

    while (true)
        asm volatile("pause");
}

__attribute__((noreturn))
void gui_zakoncz_aplikacje() {
    bws_zakoncz_proces();
}

} // extern "C"

/* =========================================================================
 * 4. API GRAFICZNE RING 3
 * ========================================================================= */

void gui_rysuj_okno(int x,
                    int y,
                    int w,
                    int h,
                    const char* tytul) {
    if (w <= 0 || h <= 0 || !tytul)
        return;

    bws_wywolaj(
        14,
        spakuj_dwa_i32(x, y),
        spakuj_dwa_i32(w, h),
        reinterpret_cast<uint64_t>(tytul),
        0);
}

void gui_wypisz_tekst(int x,
                      int y,
                      const char* tekst) {
    if (!tekst)
        return;

    bws_wywolaj(
        15,
        i32_do_u64(x),
        i32_do_u64(y),
        reinterpret_cast<uint64_t>(tekst),
        0);
}

void gui_wyczyscz_obszar(int x,
                         int y,
                         int w,
                         int h) {
    if (w <= 0 || h <= 0)
        return;

    bws_wywolaj(
        16,
        i32_do_u64(x),
        i32_do_u64(y),
        i32_do_u64(w),
        i32_do_u64(h));
}

void gui_odswiez() {
    bws_wywolaj(
        17, 0, 0, 0, 0);
}

void gui_pobierz_mysz(int* x,
                      int* y,
                      uint8_t* przyciski) {
    if (!x || !y || !przyciski)
        return;

    bws_wywolaj(
        18,
        reinterpret_cast<uint64_t>(x),
        reinterpret_cast<uint64_t>(y),
        reinterpret_cast<uint64_t>(przyciski),
        0);
}

void gui_odswiez_pulpit() {
    bws_wywolaj(
        19, 0, 0, 0, 0);
}

void gui_wypisz_tekst_kolor(int x,
                            int y,
                            uint32_t kolor,
                            const char* tekst) {
    if (!tekst)
        return;

    bws_wywolaj(
        20,
        i32_do_u64(x),
        i32_do_u64(y),
        static_cast<uint64_t>(kolor),
        reinterpret_cast<uint64_t>(tekst));
}

void gui_wypisz_tekst_kolor_skala(int x,
                                  int y,
                                  uint32_t kolor,
                                  int skala,
                                  const char* tekst) {
    if (!tekst)
        return;

    const uint32_t poprawna_skala =
        static_cast<uint32_t>(
            popraw_skale(skala));

    const uint64_t kolor_skala =
        (static_cast<uint64_t>(poprawna_skala) << 32) |
        static_cast<uint64_t>(kolor);

    bws_wywolaj(
        20,
        i32_do_u64(x),
        i32_do_u64(y),
        kolor_skala,
        reinterpret_cast<uint64_t>(tekst));
}

void gui_rysuj_prostokat(int x,
                         int y,
                         int w,
                         int h,
                         uint32_t kolor) {
    if (w <= 0 || h <= 0)
        return;

    bws_wywolaj(
        21,
        spakuj_dwa_i32(x, y),
        spakuj_dwa_i32(w, h),
        static_cast<uint64_t>(kolor),
        0);
}

bool gui_blit(int x, int y, int szer, int wys, const uint32_t* piksele) {
    if (!piksele || szer <= 0 || wys <= 0) return false;
    const uint64_t wynik = bws_wywolaj(
        57,
        spakuj_dwa_i32(x, y),
        spakuj_dwa_i32(szer, wys),
        reinterpret_cast<uint64_t>(piksele),
        0);
    return wynik == 1ULL;
}

bool gui_ustaw_tytul_okna(const char* tytul) {
    if (!tytul) return false;
    return bws_wywolaj(
        58,
        reinterpret_cast<uint64_t>(tytul),
        0, 0, 0) == 1ULL;
}

void gui_ustaw_przejecie_myszy(bool stan) {
    bws_wywolaj(
        22,
        stan ? 1ULL : 0ULL,
        0, 0, 0);
}

bool gui_pobierz_zdarzenie(bws_zdarzenie* zdarzenie) {
    if (!zdarzenie) return false;
    return bws_wywolaj(37,
        reinterpret_cast<uint64_t>(zdarzenie), 0, 0, 0) != 0;
}

bool gui_czekaj_na_zdarzenie(bws_zdarzenie* zdarzenie) {
    if (!zdarzenie) return false;
    for (;;) {
        if (bws_wywolaj(38,
                reinterpret_cast<uint64_t>(zdarzenie), 0, 0, 0) != 0)
            return true;
        /* Proces zostal oznaczony BLOCKED; timer przelaczy go z Ring 3. */
        asm volatile("pause" ::: "memory");
    }
}

void gui_ustaw_capture_myszy(bool stan) {
    bws_wywolaj(39, stan ? 1ULL : 0ULL, 0, 0, 0);
}

void gui_ustaw_system_overlay(bool otwarty,
                              int x, int y, int szer, int wys) {
    bws_wywolaj(40, otwarty ? 1ULL : 0ULL,
                spakuj_dwa_i32(x, y), spakuj_dwa_i32(szer, wys), 0);
}

bool gui_minimalizuj_okno() {
    return bws_wywolaj(41, 0, 0, 0, 0) != 0;
}

uint32_t gui_pobierz_okna(GuiOknoInfo* okna, uint32_t max) {
    if (!okna || max == 0) return 0;
    return static_cast<uint32_t>(bws_wywolaj(
        42, reinterpret_cast<uint64_t>(okna), max, 0, 0));
}

bool gui_aktywuj_okno(uint64_t window_id) {
    return window_id != 0 && bws_wywolaj(43, window_id, 0, 0, 0) != 0;
}

bool usun_twor_uzytkownika(const char* sciezka) {
    return sciezka && bws_wywolaj(7, reinterpret_cast<uint64_t>(sciezka)) != 0;
}

bool zmien_nazwe_uzytkownika(const char* sciezka, const char* nowa_nazwa) {
    return sciezka && nowa_nazwa &&
        bws_wywolaj(8, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(nowa_nazwa)) != 0;
}

bool pobierz_metadane_pliku(const char* sciezka,
                            BwsMetadanePliku* metadane) {
    return sciezka && metadane &&
        bws_wywolaj(47, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(metadane)) != 0;
}

bool przenies_twor_uzytkownika(const char* sciezka,
                               const char* folder_docelowy) {
    return sciezka && folder_docelowy &&
        bws_wywolaj(48, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(folder_docelowy)) != 0;
}

bool gui_rejestruj_cele_drop(const BwsCelDrop* cele, uint32_t liczba) {
    if (liczba > BWS_DROP_CELE_MAX || (liczba != 0 && !cele)) return false;
    return bws_wywolaj(49, reinterpret_cast<uint64_t>(cele), liczba) != 0;
}

BwsWynikDrop gui_aktualizuj_drag(const char* sciezka,
                                 int x, int y, bool wykonaj_drop) {
    if (!sciezka) return BWS_DROP_BLAD;
    const uint64_t pozycja = spakuj_dwa_i32(x, y);
    const uint64_t wynik = bws_wywolaj(
        50, reinterpret_cast<uint64_t>(sciezka), pozycja,
        wykonaj_drop ? 1ULL : 0ULL);
    return wynik <= BWS_DROP_BLAD
        ? static_cast<BwsWynikDrop>(wynik) : BWS_DROP_BLAD;
}

bool ustaw_schowek_plikow(const char* sciezka,
                          BwsOperacjaSchowka operacja) {
    if (!sciezka || (operacja != BWS_SCHOWEK_COPY &&
                     operacja != BWS_SCHOWEK_CUT)) return false;
    return bws_wywolaj(52, reinterpret_cast<uint64_t>(sciezka),
                       static_cast<uint64_t>(operacja), 0, 0) != 0;
}

bool pobierz_schowek_plikow(BwsSchowekPlikow* schowek) {
    return schowek && bws_wywolaj(
        53, reinterpret_cast<uint64_t>(schowek), 0, 0, 0) != 0;
}

bool wyczysc_schowek_plikow(uint64_t oczekiwana_generacja) {
    return oczekiwana_generacja != 0 &&
        bws_wywolaj(54, oczekiwana_generacja, 0, 0, 0) != 0;
}

bool kopiuj_twor_uzytkownika(const char* sciezka,
                             const char* folder_docelowy) {
    return sciezka && folder_docelowy &&
        bws_wywolaj(55, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(folder_docelowy), 0, 0) != 0;
}

bool czytaj_fragment_pliku(const char* sciezka, uint64_t offset,
                           char* bufor, uint32_t dlugosc,
                           uint32_t* odczytano) {
    if (!sciezka || !bufor || dlugosc==0) return false;
    if (odczytano) *odczytano = 0;
    uint64_t res = bws_wywolaj(59,
        reinterpret_cast<uint64_t>(sciezka),
        offset,
        reinterpret_cast<uint64_t>(bufor),
        (uint64_t)dlugosc);
    if (res==0 || res > (uint64_t)UINT32_MAX + 1ULL) return false;
    uint32_t got = (uint32_t)(res - 1ULL);
    if (got > dlugosc) return false;
    if (odczytano) *odczytano = got;
    return true;
}

bool gui_ustaw_popup_aplikacji(bool otwarty,
                               int x, int y, int szer, int wys) {
    return bws_wywolaj(56, otwarty ? 1ULL : 0ULL,
                       spakuj_dwa_i32(x, y),
                       spakuj_dwa_i32(szer, wys), 0) != 0;
}

void gui_rysuj_standardowa_belke(int x, int y, int szer,
                                 const char* tytul, bool zmaksymalizowane) {
    if (!tytul || szer < 90) return;
    gui_rysuj_prostokat(x, y, szer, 28, 0x00301500);
    gui_rysuj_prostokat(x, y, szer, 2, 0x00E58A00);
    gui_wypisz_tekst_kolor(x + 8, y + 7, 0x00FFFFFF, tytul);
    RysujPrzycisk(x + szer - 74, y + 4, 20, 20,
                  0x00E58A00, 0x001A0B00, "-");
    RysujPrzycisk(x + szer - 50, y + 4, 20, 20,
                  0x00E58A00, 0x001A0B00,
                  zmaksymalizowane ? "v" : "^");
    RysujPrzycisk(x + szer - 26, y + 4, 20, 20,
                  0x00AA0000, 0x00FFFFFF, "X");
}

gui_akcja_belki gui_hit_test_belki(int mx, int my,
                                   int x, int y, int szer) {
    if (szer < 90 || mx < x || mx >= x + szer || my < y || my >= y + 28)
        return GUI_BELKA_BRAK;
    if (mx >= x + szer - 26) return GUI_BELKA_ZAMKNIJ;
    if (mx >= x + szer - 50) return GUI_BELKA_MAKSYMALIZUJ;
    if (mx >= x + szer - 74) return GUI_BELKA_MINIMALIZUJ;
    return GUI_BELKA_DRAG;
}

void gui_pobierz_rozdzielczosc(int* w,
                               int* h) {
    if (!w || !h)
        return;

    bws_wywolaj(
        23,
        reinterpret_cast<uint64_t>(w),
        reinterpret_cast<uint64_t>(h),
        0, 0);
}

int gui_pobierz_szerokosc_znaku(uint32_t znak) {
    const uint64_t wynik =
        bws_wywolaj(
            24,
            static_cast<uint64_t>(znak),
            0, 0, 0);

    if (wynik > 64)
        return 8;

    return static_cast<int>(wynik);
}

int gui_pobierz_wysokosc_fontu() {
    const uint64_t wynik = bws_wywolaj(51, 0, 0, 0, 0);
    return wynik >= 1 && wynik <= 64 ? static_cast<int>(wynik) : 16;
}

int bws_utworz_warstwe(int x,
                       int y,
                       int szer,
                       int wys,
                       int z_order) {
    if (szer <= 0 || wys <= 0)
        return -1;

    const uint64_t pozycja =
        spakuj_dwa_i32(x, y);

    const uint64_t rozmiar =
        spakuj_dwa_i32(szer, wys);

    const uint64_t wynik =
        bws_wywolaj(
            33,
            pozycja,
            rozmiar,
            i32_do_u64(z_order),
            0);

    if (wynik == 0)
        return -1;

    if (wynik - 1ULL >
        static_cast<uint64_t>(INT32_MAX)) {
        return -1;
    }

    return static_cast<int>(
        wynik - 1ULL);
}

void bws_przesun_warstwe(int nowy_x,
                         int nowy_y) {
    bws_wywolaj(
        34,
        i32_do_u64(nowy_x),
        i32_do_u64(nowy_y),
        0, 0);
}

/* =========================================================================
 * 5. WIDGETY
 * ========================================================================= */

void RysujPrzycisk(int x,
                   int y,
                   int w,
                   int h,
                   uint32_t kolor_bg,
                   uint32_t kolor_txt,
                   const char* tekst) {
    if (w <= 0 || h <= 0 || !tekst)
        return;

    gui_rysuj_prostokat(
        x, y, w, h, kolor_bg);

    rysuj_tekst_wysrodkowany(x, y, w, h, 1, kolor_txt, tekst);
}

int oblicz_szerokosc_tekstu(const char* tekst,
                            int skala) {
    if (!tekst)
        return 0;

    const int poprawna_skala =
        popraw_skale(skala);

    int szerokosc = 0;
    size_t i = 0;

    while (tekst[i] != '\0') {
        uint32_t znak = 0;
        const size_t zuzyte = dekoduj_utf8_gui(tekst, i, &znak);

        int szerokosc_znaku =
            gui_pobierz_szerokosc_znaku(znak);

        if (szerokosc_znaku < 0 ||
            szerokosc_znaku > 64) {
            szerokosc_znaku = 8;
        }

        const int przyrost =
            (szerokosc_znaku + 1) *
            poprawna_skala;

        if (szerokosc >
            INT32_MAX - przyrost) {
            return INT32_MAX;
        }

        szerokosc += przyrost;
        i += zuzyte;
    }

    return szerokosc;
}

void rysuj_tekst_wysrodkowany(int px,
                              int py,
                              int w,
                              int h,
                              int skala,
                              uint32_t kolor,
                              const char* tekst) {
    if (!tekst || w <= 0 || h <= 0)
        return;

    const int poprawna_skala =
        popraw_skale(skala);

    const int szer_tekstu =
        oblicz_szerokosc_tekstu(
            tekst,
            poprawna_skala);

    const int wys_tekstu =
        gui_pobierz_wysokosc_fontu() * poprawna_skala;

    const int tx =
        px + (w - szer_tekstu) / 2;

    const int ty =
        py + (h - wys_tekstu) / 2;

    gui_wypisz_tekst_kolor_skala(
        tx,
        ty,
        kolor,
        poprawna_skala,
        tekst);
}

/* =========================================================================
 * 6. WSPOLNY DIALOG MODALNY
 * ========================================================================= */

namespace {

GuiModalDialog* modal_aktywny = nullptr;

constexpr int MODAL_FIELD_H = 26;
constexpr int MODAL_BTN_H = 26;
constexpr int MODAL_BTN_OK_W = 78;
constexpr int MODAL_BTN_ANULUJ_W = 84;

size_t modal_dlugosc(const char* tekst, size_t pojemnosc) {
    if (!tekst) return 0;
    for (size_t i = 0; i < pojemnosc; ++i)
        if (tekst[i] == '\0') return i;
    return pojemnosc;
}

bool modal_kopiuj(char* cel, size_t pojemnosc, const char* zrodlo) {
    if (!cel || pojemnosc == 0) return false;
    cel[0] = '\0';
    if (!zrodlo) return false;
    const size_t dlugosc = modal_dlugosc(zrodlo, pojemnosc);
    if (dlugosc >= pojemnosc) return false;
    for (size_t i = 0; i <= dlugosc; ++i) cel[i] = zrodlo[i];
    return true;
}

void modal_kopiuj_przytnij(char* cel, size_t pojemnosc, const char* zrodlo) {
    if (!cel || pojemnosc == 0) return;
    cel[0] = '\0';
    if (!zrodlo || pojemnosc == 1) return;
    size_t dlugosc = modal_dlugosc(zrodlo, pojemnosc - 1);
    if (dlugosc > pojemnosc - 1) dlugosc = pojemnosc - 1;
    for (size_t i = 0; i < dlugosc; ++i) cel[i] = zrodlo[i];
    cel[dlugosc] = '\0';
}

bool modal_kontynuacja(uint8_t wartosc) {
    return (wartosc & 0xC0U) == 0x80U;
}

int modal_bajty_znaku(uint8_t wartosc) {
    if (wartosc >= 0xC2U && wartosc <= 0xDFU) return 2;
    if (wartosc >= 0xE0U && wartosc <= 0xEFU) return 3;
    if (wartosc >= 0xF0U && wartosc <= 0xF4U) return 4;
    return 1;
}

size_t modal_nastepny(const char* tekst, size_t offset, size_t dlugosc) {
    if (!tekst || offset >= dlugosc) return dlugosc;
    const int ile = modal_bajty_znaku(static_cast<uint8_t>(tekst[offset]));
    if (offset + static_cast<size_t>(ile) > dlugosc) return offset + 1;
    for (int i = 1; i < ile; ++i)
        if (!modal_kontynuacja(static_cast<uint8_t>(tekst[offset + i])))
            return offset + 1;
    return offset + static_cast<size_t>(ile);
}

size_t modal_poprzedni(const char* tekst, size_t offset) {
    if (!tekst || offset == 0) return 0;
    size_t poprzedni = offset - 1;
    while (poprzedni > 0 &&
           modal_kontynuacja(static_cast<uint8_t>(tekst[poprzedni])))
        --poprzedni;
    return poprzedni;
}

void modal_przytnij_px(const char* tekst, char* cel, size_t pojemnosc,
                       int maks_pikseli) {
    if (!cel || pojemnosc < 4) return;
    cel[0] = '\0';
    if (!tekst || maks_pikseli <= 0) return;
    const size_t dlugosc = modal_dlugosc(tekst, pojemnosc - 1);
    if (dlugosc >= pojemnosc) return;
    size_t zaakceptowane = 0;
    while (zaakceptowane < dlugosc) {
        const size_t nastepny = modal_nastepny(tekst, zaakceptowane, dlugosc);
        if (nastepny + 4 > pojemnosc) break;
        char proba[192] = {};
        for (size_t i = 0; i < nastepny; ++i) proba[i] = tekst[i];
        if (oblicz_szerokosc_tekstu(proba, 1) > maks_pikseli) break;
        zaakceptowane = nastepny;
    }
    if (zaakceptowane + 4 > pojemnosc) return;
    for (size_t i = 0; i < zaakceptowane; ++i) cel[i] = tekst[i];
    if (zaakceptowane < dlugosc) {
        cel[zaakceptowane++] = '.';
        cel[zaakceptowane++] = '.';
        cel[zaakceptowane++] = '.';
    }
    cel[zaakceptowane] = '\0';
}

void modal_rysuj_ramke(const GuiModalProstokat& r) {
    gui_rysuj_prostokat(r.x, r.y, r.szer, r.wys, GUI_MODAL_TLO);
    gui_rysuj_prostokat(r.x, r.y, r.szer, 2, GUI_MODAL_RAMKA);
    gui_rysuj_prostokat(r.x, r.y + r.wys - 2, r.szer, 2,
                        GUI_MODAL_RAMKA_CIEMNA);
    gui_rysuj_prostokat(r.x, r.y, 2, r.wys, GUI_MODAL_RAMKA);
    gui_rysuj_prostokat(r.x + r.szer - 2, r.y, 2, r.wys,
                        GUI_MODAL_RAMKA_CIEMNA);
}

void modal_rysuj_pole(GuiModalDialog& d) {
    const GuiModalProstokat& p = d.pole;
    if (p.szer <= 0 || p.wys <= 0) return;
    gui_rysuj_prostokat(p.x, p.y, p.szer, p.wys, GUI_MODAL_POLE_AKTYWNE);
    gui_rysuj_prostokat(p.x, p.y, p.szer, 1, GUI_MODAL_RAMKA);
    gui_rysuj_prostokat(p.x, p.y + p.wys - 1, p.szer, 1, GUI_MODAL_RAMKA);
    gui_rysuj_prostokat(p.x, p.y, 1, p.wys, GUI_MODAL_RAMKA_CIEMNA);
    gui_rysuj_prostokat(p.x + p.szer - 1, p.y, 1, p.wys,
                        GUI_MODAL_RAMKA_CIEMNA);

    const int dostepne = p.szer - 12;
    if (dostepne <= 0) return;

    while (d.widoczne_od > d.karetka)
        d.widoczne_od =
            static_cast<uint32_t>(modal_poprzedni(d.tekst, d.widoczne_od));
    while (d.widoczne_od < d.karetka) {
        char przed[128] = {};
        size_t out = 0;
        for (size_t i = d.widoczne_od; i < d.karetka && out + 1 < sizeof(przed); ++i)
            przed[out++] = d.tekst[i];
        if (oblicz_szerokosc_tekstu(przed, 1) +
            oblicz_szerokosc_tekstu("|", 1) <= dostepne)
            break;
        d.widoczne_od = static_cast<uint32_t>(
            modal_nastepny(d.tekst, d.widoczne_od, d.dlugosc));
    }

    char pokazane[80] = {};
    size_t out = 0;
    for (size_t i = d.widoczne_od; i < d.dlugosc && out + 2 < sizeof(pokazane); ++i) {
        if (i == d.karetka) pokazane[out++] = '|';
        pokazane[out++] = d.tekst[i];
    }
    if (d.karetka == d.dlugosc && out + 1 < sizeof(pokazane))
        pokazane[out++] = '|';
    pokazane[out] = '\0';

    char przyciete[80] = {};
    modal_przytnij_px(pokazane, przyciete, sizeof(przyciete), dostepne);
    gui_wypisz_tekst_kolor(p.x + 6, p.y + 5, GUI_MODAL_TEKST, przyciete);
}

} // namespace

bool gui_modal_otworz(GuiModalDialog* dialog,
                      const char* tytul,
                      const char* tekst_poczatkowy,
                      const char* pytanie,
                      const GuiModalProstokat* obszar,
                      uint32_t limit_tekstu) {
    if (!dialog || !tytul || !obszar ||
        obszar->szer <= 0 || obszar->wys <= 0)
        return false;

    /* Jeden modal na proces: nie nakladamy dwoch popupow na ten sam surface.
       Ponowne otwarcie tego samego modala najpierw go zamyka, dzieki czemu
       backup kompozytora nigdy nie zapamieta juz narysowanego dialogu. */
    if (modal_aktywny && modal_aktywny != dialog)
        gui_modal_zamknij(modal_aktywny);
    if (dialog->otwarty)
        gui_modal_zamknij(dialog);

    GuiModalDialog czysty = {};
    /* Etykieta i kolor OK moga byc ustawione wczesniej przez
       gui_modal_ustaw_przycisk_ok; w przeciwnym razie stosujemy domyslne. */
    if (dialog->etykieta_ok[0])
        modal_kopiuj_przytnij(czysty.etykieta_ok, sizeof(czysty.etykieta_ok),
                              dialog->etykieta_ok);
    else
        modal_kopiuj(czysty.etykieta_ok, sizeof(czysty.etykieta_ok), "OK");
    czysty.kolor_ok = dialog->kolor_ok ? dialog->kolor_ok : GUI_MODAL_RAMKA;
    czysty.limit_tekstu =
        limit_tekstu == 0 || limit_tekstu > sizeof(czysty.tekst) - 1
            ? static_cast<uint32_t>(sizeof(czysty.tekst) - 1) : limit_tekstu;
    modal_kopiuj_przytnij(czysty.tytul, sizeof(czysty.tytul), tytul);
    czysty.potwierdzenie = tekst_poczatkowy == nullptr;
    if (!czysty.potwierdzenie) {
        if (!modal_kopiuj(czysty.tekst, sizeof(czysty.tekst), tekst_poczatkowy))
            return false;
        czysty.dlugosc = modal_dlugosc(czysty.tekst, sizeof(czysty.tekst));
        czysty.karetka = czysty.dlugosc;
    } else {
        modal_kopiuj_przytnij(czysty.pytanie, sizeof(czysty.pytanie), pytanie);
    }
    czysty.obszar = *obszar;

    int szerokosc = czysty.obszar.szer > 430 ? 430
        : (czysty.obszar.szer > 20 ? czysty.obszar.szer - 20 : czysty.obszar.szer);
    int wysokosc = czysty.potwierdzenie ? 150 : 176;
    if (wysokosc > czysty.obszar.wys - 16)
        wysokosc = czysty.obszar.wys > 16 ? czysty.obszar.wys - 16 : czysty.obszar.wys;
    if (szerokosc <= 0 || wysokosc <= 0) return false;

    czysty.ramka = {czysty.obszar.x + (czysty.obszar.szer - szerokosc) / 2,
                    czysty.obszar.y + (czysty.obszar.wys - wysokosc) / 2,
                    szerokosc, wysokosc};
    czysty.pole = {czysty.ramka.x + 18, czysty.ramka.y + 70,
                   czysty.ramka.szer > 36 ? czysty.ramka.szer - 36 : 0,
                   MODAL_FIELD_H};
    czysty.przycisk_ok = {czysty.ramka.x + czysty.ramka.szer - 190,
                          czysty.ramka.y + czysty.ramka.wys - MODAL_BTN_H - 12,
                          MODAL_BTN_OK_W, MODAL_BTN_H};
    czysty.przycisk_anuluj = {czysty.ramka.x + czysty.ramka.szer - 102,
                              czysty.ramka.y + czysty.ramka.wys - MODAL_BTN_H - 12,
                              MODAL_BTN_ANULUJ_W, MODAL_BTN_H};

    if (!gui_ustaw_popup_aplikacji(true, czysty.obszar.x, czysty.obszar.y,
                                   czysty.obszar.szer, czysty.obszar.wys))
        return false;

    *dialog = czysty;
    dialog->otwarty = true;
    modal_aktywny = dialog;
    gui_modal_rysuj(dialog);
    return true;
}

void gui_modal_ustaw_przycisk_ok(GuiModalDialog* dialog,
                                 const char* etykieta,
                                 uint32_t kolor) {
    if (!dialog) return;
    if (etykieta)
        modal_kopiuj(dialog->etykieta_ok, sizeof(dialog->etykieta_ok), etykieta);
    dialog->kolor_ok = kolor ? kolor : GUI_MODAL_RAMKA;
}

void gui_modal_zamknij(GuiModalDialog* dialog) {
    if (!dialog || !dialog->otwarty) return;
    (void)gui_ustaw_popup_aplikacji(false, 0, 0, 0, 0);
    *dialog = GuiModalDialog{};
    if (modal_aktywny == dialog) modal_aktywny = nullptr;
}

bool gui_modal_otwarty(const GuiModalDialog* dialog) {
    return dialog && dialog->otwarty;
}

const char* gui_modal_tekst(const GuiModalDialog* dialog) {
    return dialog ? dialog->tekst : "";
}

const GuiModalProstokat* gui_modal_pole(const GuiModalDialog* dialog) {
    return dialog ? &dialog->pole : nullptr;
}

const GuiModalProstokat* gui_modal_ramka(const GuiModalDialog* dialog) {
    return dialog ? &dialog->ramka : nullptr;
}

namespace {

bool modal_punkt(const GuiModalProstokat& r, int x, int y) {
    return x >= r.x && y >= r.y &&
           x < r.x + r.szer && y < r.y + r.wys;
}

} // namespace

gui_modal_wynik gui_modal_klik(GuiModalDialog* dialog, int mysz_x, int mysz_y) {
    if (!dialog || !dialog->otwarty) return GUI_MODAL_BRAK;
    if (modal_punkt(dialog->przycisk_ok, mysz_x, mysz_y)) return GUI_MODAL_OK;
    if (modal_punkt(dialog->przycisk_anuluj, mysz_x, mysz_y))
        return GUI_MODAL_ANULUJ;
    return GUI_MODAL_BRAK;
}

bool gui_modal_pole_trafione(const GuiModalDialog* dialog,
                             int mysz_x, int mysz_y) {
    return dialog && dialog->otwarty && !dialog->potwierdzenie &&
           modal_punkt(dialog->pole, mysz_x, mysz_y);
}

void gui_modal_karetka_za_mysza(GuiModalDialog* dialog, int mysz_x) {
    if (!dialog || !dialog->otwarty || dialog->potwierdzenie) return;
    const GuiModalProstokat& p = dialog->pole;
    int pozostalo = mysz_x - p.x - 6;
    size_t pozycja = dialog->widoczne_od;
    while (pozostalo > 0 && pozycja < dialog->dlugosc) {
        const size_t nastepny =
            modal_nastepny(dialog->tekst, pozycja, dialog->dlugosc);
        char glif[8] = {};
        for (size_t i = pozycja;
             i < nastepny && i - pozycja + 1 < sizeof(glif); ++i)
            glif[i - pozycja] = dialog->tekst[i];
        const int szerokosc = oblicz_szerokosc_tekstu(glif, 1);
        if (pozostalo < szerokosc / 2) break;
        pozostalo -= szerokosc;
        pozycja = nastepny;
    }
    dialog->karetka = static_cast<uint32_t>(pozycja);
}

gui_modal_wynik gui_modal_klawisz(GuiModalDialog* dialog, char znak) {
    if (!dialog || !dialog->otwarty) return GUI_MODAL_BRAK;
    if (znak == '\n' || znak == '\r') return GUI_MODAL_OK;
    if (znak == '\x1B') return GUI_MODAL_ANULUJ;
    if (dialog->potwierdzenie) return GUI_MODAL_BRAK;
    const uint8_t wartosc = static_cast<uint8_t>(znak);
    if (wartosc == '\b' || wartosc == 0x7FU) (void)gui_modal_usun_wstecz(dialog);
    else (void)gui_modal_dopisz(dialog, znak);
    return GUI_MODAL_BRAK;
}

bool gui_modal_dopisz(GuiModalDialog* dialog, char znak) {
    if (!dialog || !dialog->otwarty || dialog->potwierdzenie) return false;
    const uint8_t wartosc = static_cast<uint8_t>(znak);
    if (wartosc < 0x20U || wartosc == 0x7FU) return false;
    if (dialog->dlugosc >= dialog->limit_tekstu) return false;
    for (size_t i = dialog->dlugosc + 1; i > dialog->karetka; --i)
        dialog->tekst[i] = dialog->tekst[i - 1];
    dialog->tekst[dialog->karetka++] = znak;
    ++dialog->dlugosc;
    return true;
}

bool gui_modal_usun_wstecz(GuiModalDialog* dialog) {
    if (!dialog || !dialog->otwarty || dialog->potwierdzenie) return false;
    if (dialog->karetka == 0) return false;
    const size_t poprzedni = modal_poprzedni(dialog->tekst, dialog->karetka);
    const size_t usuniete = dialog->karetka - poprzedni;
    for (size_t i = poprzedni; i + usuniete <= dialog->dlugosc; ++i)
        dialog->tekst[i] = dialog->tekst[i + usuniete];
    dialog->dlugosc -= static_cast<uint32_t>(usuniete);
    dialog->karetka = static_cast<uint32_t>(poprzedni);
    if (dialog->widoczne_od > dialog->karetka) dialog->widoczne_od = dialog->karetka;
    return true;
}

bool gui_modal_usun_przed(GuiModalDialog* dialog) {
    if (!dialog || !dialog->otwarty || dialog->potwierdzenie) return false;
    if (dialog->karetka >= dialog->dlugosc) return false;
    const size_t nastepny =
        modal_nastepny(dialog->tekst, dialog->karetka, dialog->dlugosc);
    const size_t usuniete = nastepny - dialog->karetka;
    for (size_t i = dialog->karetka; i + usuniete <= dialog->dlugosc; ++i)
        dialog->tekst[i] = dialog->tekst[i + usuniete];
    dialog->dlugosc -= static_cast<uint32_t>(usuniete);
    return true;
}

bool gui_modal_przesun_karetke(GuiModalDialog* dialog, int kierunek) {
    if (!dialog || !dialog->otwarty || dialog->potwierdzenie) return false;
    if (kierunek < 0) {
        if (dialog->karetka == 0) return false;
        dialog->karetka = static_cast<uint32_t>(
            modal_poprzedni(dialog->tekst, dialog->karetka));
        if (dialog->widoczne_od > dialog->karetka)
            dialog->widoczne_od = dialog->karetka;
        return true;
    }
    if (kierunek > 0) {
        if (dialog->karetka >= dialog->dlugosc) return false;
        dialog->karetka = static_cast<uint32_t>(
            modal_nastepny(dialog->tekst, dialog->karetka, dialog->dlugosc));
        return true;
    }
    return false;
}

void gui_modal_rysuj(GuiModalDialog* dialog) {
    if (!dialog || !dialog->otwarty) return;

    modal_rysuj_ramke(dialog->ramka);

    char przyciete[160] = {};
    modal_przytnij_px(dialog->tytul, przyciete, sizeof(przyciete),
                      dialog->ramka.szer > 36 ? dialog->ramka.szer - 36 : 0);
    gui_wypisz_tekst_kolor(dialog->ramka.x + 18, dialog->ramka.y + 14,
                           GUI_MODAL_TEKST, przyciete);

    if (dialog->potwierdzenie) {
        modal_przytnij_px(dialog->pytanie, przyciete, sizeof(przyciete),
                          dialog->ramka.szer > 36 ? dialog->ramka.szer - 36 : 0);
        gui_wypisz_tekst_kolor(dialog->ramka.x + 18, dialog->ramka.y + 52,
                               GUI_MODAL_TEKST_DRUGI, przyciete);
    } else {
        modal_rysuj_pole(*dialog);
    }

    RysujPrzycisk(dialog->przycisk_ok.x, dialog->przycisk_ok.y,
                  dialog->przycisk_ok.szer, dialog->przycisk_ok.wys,
                  dialog->kolor_ok, GUI_MODAL_POLE_TLO, dialog->etykieta_ok);
    RysujPrzycisk(dialog->przycisk_anuluj.x, dialog->przycisk_anuluj.y,
                  dialog->przycisk_anuluj.szer, dialog->przycisk_anuluj.wys,
                  GUI_MODAL_RAMKA, GUI_MODAL_POLE_TLO, "Anuluj");
}
