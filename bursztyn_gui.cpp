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

bool tekst_konczy_sie_gui(const char* tekst, const char* sufiks) {
    if (!tekst || !sufiks) return false;
    size_t lt = 0;
    size_t ls = 0;
    while (lt < 511U && tekst[lt] != '\0') ++lt;
    while (ls < 32U && sufiks[ls] != '\0') ++ls;
    if (lt == 511U || ls == 32U || ls > lt) return false;
    for (size_t i = 0; i < ls; ++i)
        if (tekst[lt - ls + i] != sufiks[i]) return false;
    return true;
}

} // namespace

wynik_otwarcia_skojarzonego otworz_plik_skojarzony(const char* sciezka) {
    if (!sciezka || sciezka[0] != '/') return OTWORZ_PLIK_BLAD;
    if (!tekst_konczy_sie_gui(sciezka, ".txt"))
        return OTWORZ_PLIK_BRAK_SKOJARZENIA;
    return uruchom_program_z_argumentem_uzytkownika(
               "/programy/notatnik.cebula/notatnik.bur", sciezka)
        ? OTWORZ_PLIK_URUCHOMIONO
        : OTWORZ_PLIK_BLAD;
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
