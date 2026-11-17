/*
 * Bursztyn OS - biblioteka GUI dla aplikacji Ring 3.
 * Wrappery BWS, prywatna sterta procesu i podstawowe widgety.
 */

#include "bursztyn_gui.h"
#include "app_identity.h"
#include "ui_skala.h"
#include "wm_belka_hit.h"
#if defined(BSF_NATIVE)
#include "bursztyn_ui/czcionka.h"
#endif

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * 1. PRYWATNA STERTA APLIKACJI RING 3
 * ========================================================================= */

namespace {

uint64_t liczba_alokacji_sterty = 0;
uint64_t liczba_aktywnych_alokacji_sterty = 0;
uint64_t spakuj_dwa_i32(int a, int b);

#if defined(BSF_NATIVE)
char bsf_window_title[48]{};
uint32_t bsf_title_generation{}, bsf_title_scale{};
void bsf_update_window_title() {
  if (!bsf_window_title[0])
    return;
  struct FontMode {
    bool old;
    FontMode() : old(BSF::monospace()) { BSF::monospace(false); }
    ~FontMode() { BSF::monospace(old); }
  } mode;
  uint32_t generation = BSF::generation();
  if (bsf_title_generation == generation && bsf_title_scale == ui_skala_procent)
    return;
  if (!BSF::ready()) {
    (void)bws_wywolaj(93, 0, 0, 0, 0);
    bsf_title_generation = generation;
    bsf_title_scale = ui_skala_procent;
    return;
  }
  int px = ui_skaluj(BSF::ui_size(), ui_skala_procent),
      w = BSF::measure_text(bsf_window_title, px) + 8, h = BSF::height(px);
  if (w < 1 || h < 1)
    return;
  if (w > 512)
    w = 512;
  if (h > 128)
    h = 128;
  auto *alpha = new uint8_t[size_t(w) * h];
  if (!alpha)
    return;
  for (size_t i = 0; i < size_t(w) * h; ++i)
    static_cast<volatile uint8_t *>(alpha)[i] = 0;
  BursztynUI::CzcionkaUI::rysuj_bsf(
      bsf_window_title, 0, 0, px,
      [&](int x, int y, int gw, int gh, const uint8_t *a) {
        for (int yy = 0; yy < gh; ++yy)
          for (int xx = 0; xx < gw; ++xx)
            if (x + xx >= 0 && x + xx < w && y + yy >= 0 && y + yy < h) {
              auto &dst = alpha[(y + yy) * w + x + xx];
              unsigned src = a[yy * gw + xx];
              dst = uint8_t(src + unsigned(dst) * (255 - src) / 255);
            }
      });
  uint64_t dimensions = (uint64_t(uint32_t(w)) << 32) | uint32_t(h);
  if (bws_wywolaj(93, dimensions, reinterpret_cast<uint64_t>(alpha), 0, 0) ==
      1) {
    bsf_title_generation = generation;
    bsf_title_scale = ui_skala_procent;
  }
  delete[] alpha;
}
#endif

bool legacy_wlaczony = false;
bool legacy_ma_warstwe = false;
bool legacy_test_skali = false;
bool legacy_test_widzial_skale = false;
unsigned legacy_test_timer_do_wyjscia = 0;
uint32_t legacy_skala = 100;
GuiLegacyProstokat legacy_logiczne_okno{0, 0, 1, 1};
GuiLegacyProstokat legacy_fizyczne_okno{0, 0, 1, 1};
BwsCelDrop legacy_cele_drop[BWS_DROP_CELE_MAX] = {};

bool legacy_monitor_raw(GuiMonitorInfo* info) {
    return info && bws_wywolaj(BWS_GUI_MONITOR_INFO, 0,
        reinterpret_cast<uint64_t>(info), 0, 0) == 1ULL;
}

void legacy_odswiez_skale() {
    GuiMonitorInfo mon{};
    if (legacy_monitor_raw(&mon) && ui_skala_poprawna(mon.scale_x1000 / 10U))
        legacy_skala = mon.scale_x1000 / 10U;
    ui_skala_procent = legacy_skala;
}

GuiLegacyProstokat legacy_work_area() {
    GuiMonitorInfo mon{};
    if (legacy_monitor_raw(&mon) && mon.work_szer > 0 && mon.work_wys > 0)
        return {mon.work_x, mon.work_y, mon.work_szer, mon.work_wys};
    int w = 1024, h = 768;
    bws_wywolaj(23, reinterpret_cast<uint64_t>(&w),
                reinterpret_cast<uint64_t>(&h), 0, 0);
    const int tb = ui_skaluj(40, legacy_skala);
    return {0, 0, w, h > tb ? h - tb : h};
}

GuiLegacyProstokat legacy_przelicz_okno(GuiLegacyProstokat r) {
    return gui_legacy_geometria_fizyczna(r, legacy_work_area(), legacy_skala);
}

int legacy_x(int x) {
    return legacy_wlaczony && legacy_ma_warstwe
        ? legacy_fizyczne_okno.x + ui_skaluj(x - legacy_logiczne_okno.x, legacy_skala)
        : x;
}
int legacy_y(int y) {
    return legacy_wlaczony && legacy_ma_warstwe
        ? legacy_fizyczne_okno.y + ui_skaluj(y - legacy_logiczne_okno.y, legacy_skala)
        : y;
}
int legacy_w(int w) { return legacy_wlaczony ? ui_skaluj(w, legacy_skala) : w; }
int legacy_h(int h) { return legacy_wlaczony ? ui_skaluj(h, legacy_skala) : h; }
int legacy_odskaluj_x(int x) {
    return legacy_wlaczony && legacy_ma_warstwe
        ? legacy_logiczne_okno.x + ui_odskaluj(x - legacy_fizyczne_okno.x, legacy_skala)
        : x;
}
int legacy_odskaluj_y(int y) {
    return legacy_wlaczony && legacy_ma_warstwe
        ? legacy_logiczne_okno.y + ui_odskaluj(y - legacy_fizyczne_okno.y, legacy_skala)
        : y;
}

void legacy_przetworz_zdarzenie(bws_zdarzenie* e) {
    if (!legacy_wlaczony || !e) return;
    if (e->typ == BWS_ZDARZENIE_EKRAN) {
        legacy_odswiez_skale();
        ui_skala_procent = legacy_skala;
        if (legacy_test_skali) {
            if (legacy_skala != 100) legacy_test_widzial_skale = true;
            else if (legacy_test_widzial_skale && legacy_test_timer_do_wyjscia == 0)
                legacy_test_timer_do_wyjscia = 1;
        }
        if (legacy_ma_warstwe) {
            legacy_fizyczne_okno = legacy_przelicz_okno(legacy_logiczne_okno);
            (void)bws_wywolaj(BWS_GUI_GEOMETRIA,
                spakuj_dwa_i32(legacy_fizyczne_okno.x, legacy_fizyczne_okno.y),
                spakuj_dwa_i32(legacy_fizyczne_okno.szer, legacy_fizyczne_okno.wys),
                0, 0);
        }
    }
    switch (e->typ) {
        case BWS_ZDARZENIE_MYSZ_RUCH:
        case BWS_ZDARZENIE_MYSZ_DOWN:
        case BWS_ZDARZENIE_MYSZ_UP:
        case BWS_ZDARZENIE_MYSZ_PRAWY_DOWN:
        case BWS_ZDARZENIE_MYSZ_PRAWY_UP:
        case BWS_ZDARZENIE_DRAG_HOVER:
        case BWS_ZDARZENIE_DRAG_LEAVE:
        case BWS_ZDARZENIE_DRAG_DROP:
            e->x = legacy_odskaluj_x(e->x);
            e->y = legacy_odskaluj_y(e->y);
            e->dx = ui_odskaluj(e->dx, legacy_skala);
            e->dy = ui_odskaluj(e->dy, legacy_skala);
            break;
        default: break;
    }
}

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
    if(skala>=100&&skala<=400)return skala;
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

void gui_legacy_wlacz() {
    legacy_wlaczony = true;
    char argument[32] = {};
    if (pobierz_argument_startowy(argument, sizeof(argument))) {
        static constexpr char TEST_ARG[] = "--legacy-scale-test";
        unsigned i = 0;
        while (argument[i] && TEST_ARG[i] && argument[i] == TEST_ARG[i]) ++i;
        legacy_test_skali = argument[i] == '\0' && TEST_ARG[i] == '\0';
    }
    legacy_odswiez_skale();
}

bool gui_legacy_czy_wlaczony() { return legacy_wlaczony; }
uint32_t gui_legacy_skala_procent() { return legacy_skala; }

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
        ++liczba_alokacji_sterty;
        ++liczba_aktywnych_alokacji_sterty;
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

    ++liczba_alokacji_sterty;
    ++liczba_aktywnych_alokacji_sterty;
    return static_cast<void*>(nowy + 1);
}

uint64_t gui_liczba_alokacji_sterty() {
    return liczba_alokacji_sterty;
}

uint64_t gui_liczba_aktywnych_alokacji_sterty() {
    return liczba_aktywnych_alokacji_sterty;
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
    if (liczba_aktywnych_alokacji_sterty > 0)
        --liczba_aktywnych_alokacji_sterty;

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
        0, 0, 0) == 1ULL;
}

bool utworz_katalog_uzytkownika(const char* sciezka) {
    if (!sciezka) return false;
    return bws_wywolaj(
        46,
        reinterpret_cast<uint64_t>(sciezka),
        0, 0, 0) == 1ULL;
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
        0) == 1ULL;
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
        0) == 1ULL;
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
        0) == 1ULL;
}

bool uruchom_program_uzytkownika(const char* sciezka) {
    if (!sciezka) return false;
    return bws_wywolaj(
        10,
        reinterpret_cast<uint64_t>(sciezka),
        0, 0, 0) == 1ULL;
}

uint8_t pobierz_blad_uruchomienia() {
    return static_cast<uint8_t>(bws_wywolaj(100, 0, 0, 0, 0)); // BWS_OSTATNI_BLAD_URUCHOMIENIA
}

bool uruchom_program_z_argumentem_uzytkownika(const char* program,
                                               const char* argument) {
    if (!program || !argument) return false;
    return bws_wywolaj(
        10,
        reinterpret_cast<uint64_t>(program),
        reinterpret_cast<uint64_t>(argument),
        0, 0) == 1ULL;
}

uint32_t gui_okno_podrzedne_otworz(uint32_t rodzaj, const char* tytul, const char* tresc, uint32_t dlugosc) {
    if (!tytul || !tresc || dlugosc == 0) return 0;
    return static_cast<uint32_t>(bws_wywolaj(101, rodzaj, reinterpret_cast<uint64_t>(tresc), dlugosc,
                                              reinterpret_cast<uint64_t>(tytul)));
}

uint32_t gui_okno_podrzedne_tresc(char* bufor, uint32_t pojemnosc) {
    if (!bufor || pojemnosc == 0) return 0;
    return static_cast<uint32_t>(bws_wywolaj(102, reinterpret_cast<uint64_t>(bufor), pojemnosc, 0, 0));
}

bool pobierz_argument_startowy(char* bufor, uint32_t pojemnosc) {
    if (!bufor || pojemnosc == 0) return false;
    return bws_wywolaj(
        45,
        reinterpret_cast<uint64_t>(bufor),
        static_cast<uint64_t>(pojemnosc),
        0, 0) == 1ULL;
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
        0, 0) == 1ULL;
}

uint32_t bws_siec_pobierz_bajty_origin(bool https, uint8_t* ip, const char* domain, const char* path, char* buffer, uint32_t capacity, const char* origin) {
    if (!origin) return bws_siec_pobierz_bajty(https, ip, domain, path, buffer, capacity);
    if (!ip || !domain || !path || !buffer || capacity<2 || capacity>256*1024) return 0;
    BwsBuforSieci64Origin req{{reinterpret_cast<uint64_t>(buffer),capacity,BWS_HTTP64_COUNTED|BWS_HTTP64_ORIGIN},
                              reinterpret_cast<uint64_t>(origin)};
    uint64_t result=bws_wywolaj(https?BWS_HTTPS64:BWS_HTTP64,
        reinterpret_cast<uint64_t>(ip),reinterpret_cast<uint64_t>(domain),
        reinterpret_cast<uint64_t>(path),reinterpret_cast<uint64_t>(&req));
    return result>1 && result<=capacity ? uint32_t(result-1) : 0;
}

uint32_t bws_siec_pobierz_bajty(bool https, uint8_t* ip, const char* domain, const char* path, char* buffer, uint32_t capacity) {
    if (!ip || !domain || !path || !buffer || capacity<2 || capacity>256*1024) return 0;
    if (reinterpret_cast<uint64_t>(buffer)>UINT32_MAX) {
        BwsBuforSieci64 req{reinterpret_cast<uint64_t>(buffer),capacity,1};
        uint64_t result=bws_wywolaj(https?BWS_HTTPS64:BWS_HTTP64,
            reinterpret_cast<uint64_t>(ip),reinterpret_cast<uint64_t>(domain),
            reinterpret_cast<uint64_t>(path),reinterpret_cast<uint64_t>(&req));
        return result>1 && result<=capacity ? uint32_t(result-1) : 0;
    }
    uint64_t packed=(reinterpret_cast<uint64_t>(buffer)<<32)|uint64_t(capacity)|0x80000000ULL;
    uint64_t result=bws_wywolaj(https?30:29,reinterpret_cast<uint64_t>(ip),reinterpret_cast<uint64_t>(domain),reinterpret_cast<uint64_t>(path),packed);
    return result>1 && result<=capacity ? uint32_t(result-1) : 0;
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
    if (reinterpret_cast<uint64_t>(bufor)>UINT32_MAX) {
        BwsBuforSieci64 req{reinterpret_cast<uint64_t>(bufor),max_dlugosc,0};
        return bws_wywolaj(BWS_HTTP64,reinterpret_cast<uint64_t>(cel_ip),
            reinterpret_cast<uint64_t>(domena),reinterpret_cast<uint64_t>(sciezka),
            reinterpret_cast<uint64_t>(&req)) == 1;
    }
    const uint64_t bufor_i_rozmiar =
        (reinterpret_cast<uint64_t>(bufor) << 32) |
        static_cast<uint64_t>(max_dlugosc);

    return bws_wywolaj(
        29,
        reinterpret_cast<uint64_t>(cel_ip),
        reinterpret_cast<uint64_t>(domena),
        reinterpret_cast<uint64_t>(sciezka),
        bufor_i_rozmiar) == 1ULL;
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

    if (reinterpret_cast<uint64_t>(bufor)>UINT32_MAX) {
        BwsBuforSieci64 req{reinterpret_cast<uint64_t>(bufor),max_dlugosc,0};
        return bws_wywolaj(BWS_HTTPS64,reinterpret_cast<uint64_t>(cel_ip),
            reinterpret_cast<uint64_t>(domena),reinterpret_cast<uint64_t>(sciezka),
            reinterpret_cast<uint64_t>(&req)) == 1;
    }
    const uint64_t bufor_i_rozmiar =
        (reinterpret_cast<uint64_t>(bufor) << 32) |
        static_cast<uint64_t>(max_dlugosc);

    return bws_wywolaj(
        30,
        reinterpret_cast<uint64_t>(cel_ip),
        reinterpret_cast<uint64_t>(domena),
        reinterpret_cast<uint64_t>(sciezka),
        bufor_i_rozmiar) == 1ULL;
}

bool bws_tls_certyfikat_zaufany() {
    return bws_wywolaj(
        31, 0, 0, 0, 0) == 1ULL;
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

    if (legacy_wlaczony) {
        (void)gui_ustaw_tytul_okna(tytul);
        gui_rysuj_prostokat(x, y, w, h, 0x008A5A00);
        gui_rysuj_prostokat(x + 2, y + 28, w - 4, h - 30, 0x001A0B00);
    } else {
        bws_wywolaj(14, spakuj_dwa_i32(x, y), spakuj_dwa_i32(w, h),
                    reinterpret_cast<uint64_t>(tytul), 0);
    }
}

void gui_wypisz_tekst(int x,
                      int y,
                      const char* tekst) {
    if (!tekst)
        return;

#if defined(BSF_NATIVE)
    if(BSF::ready()){gui_wypisz_tekst_kolor(x,y,motyw_kolor(MOTYW_KOLOR_TEKST),tekst);return;}
#endif
    if (legacy_wlaczony) {
        gui_wypisz_tekst_kolor_skala(x, y, 0x00D1D5DB, 100, tekst);
        return;
    }

    bws_wywolaj(
        15,
        i32_do_u64(legacy_x(x)),
        i32_do_u64(legacy_y(y)),
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
        i32_do_u64(legacy_x(x)),
        i32_do_u64(legacy_y(y)),
        i32_do_u64(legacy_w(w)),
        i32_do_u64(legacy_h(h)));
}

void gui_odswiez() {
#if defined(BSF_NATIVE)
    bsf_update_window_title();
#endif
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
    *x = legacy_odskaluj_x(*x);
    *y = legacy_odskaluj_y(*y);
}

void gui_odswiez_pulpit() {
    bws_wywolaj(
        19, 0, 0, 0, 0);
}

bool gui_potwierdz_gotowosc_pulpitu() {
    return bws_wywolaj(BWS_PULPIT_GOTOWY, 0, 0, 0, 0) == 1ULL;
}

void gui_wypisz_tekst_kolor(int x,int y,uint32_t kolor,const char*tekst){
    gui_wypisz_tekst_kolor_skala(x,y,kolor,
        legacy_wlaczony ? 100 : static_cast<int>(ui_skala_procent),tekst);
}

void gui_wypisz_tekst_kolor_skala(int x,
                                  int y,
                                  uint32_t kolor,
                                  int skala,
                                  const char* tekst) {
    if (!tekst)
        return;

    uint32_t poprawna_skala = static_cast<uint32_t>(popraw_skale(skala));
    if (legacy_wlaczony) {
        const uint32_t lokalna = skala >= 100 ? poprawna_skala
                                              : 100U * poprawna_skala;
        poprawna_skala = static_cast<uint32_t>(ui_skaluj(
            static_cast<int>(lokalna), legacy_skala));
        if (poprawna_skala > 400U) poprawna_skala = 400U;
    }

#if defined(BSF_NATIVE)
    int pixel_size=ui_skaluj(BSF::ui_size(),poprawna_skala);
    if(pixel_size>=1&&pixel_size<=128&&BSF::ready()){
        int width=BSF::measure_text(tekst,pixel_size)+8,height=BSF::height(pixel_size)+4;
        if(width>0&&width<=2048&&height>0&&height<=128){
            auto* pixels=new uint32_t[size_t(width)*height];
            if(pixels){for(size_t i=0;i<size_t(width)*height;++i)static_cast<volatile uint32_t*>(pixels)[i]=0;
                BursztynUI::CzcionkaUI::rysuj_bsf(tekst,4,0,pixel_size,[&](int gx,int gy,int w,int h,const uint8_t* alpha){for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)if(gx+xx>=0&&gx+xx<width&&gy+yy>=0&&gy+yy<height){auto& dst=pixels[size_t(gy+yy)*width+gx+xx];unsigned a=alpha[yy*w+xx]*((kolor>>24)?(kolor>>24):255)/255,previous=dst>>24;a+=previous*(255-a)/255;dst=(a<<24)|(kolor&0xffffff);}});
                gui_blit_alpha(legacy_x(x)-4,legacy_y(y),width,height,pixels);delete[] pixels;return;
            }
        }
    }
    if(BursztynUI::CzcionkaUI::rysuj_bsf(tekst,legacy_x(x),legacy_y(y),pixel_size,
        [&](int gx,int gy,int w,int h,const uint8_t* alpha){uint32_t row[BSF::MaxBitmap];for(int yy=0;yy<h;++yy){for(int xx=0;xx<w;++xx)row[xx]=(uint32_t(alpha[yy*w+xx]*((kolor>>24)?(kolor>>24):255)/255)<<24)|(kolor&0xffffff);gui_blit_alpha(gx,gy+yy,w,1,row);}}))return;
#endif
    const uint64_t kolor_skala =
        (static_cast<uint64_t>(poprawna_skala) << 32) |
        static_cast<uint64_t>(kolor);

    bws_wywolaj(
        20,
        i32_do_u64(legacy_x(x)),
        i32_do_u64(legacy_y(y)),
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
        spakuj_dwa_i32(legacy_x(x), legacy_y(y)),
        spakuj_dwa_i32(legacy_w(w), legacy_h(h)),
        static_cast<uint64_t>(kolor),
        0);
}

bool gui_blit(int x, int y, int szer, int wys, const uint32_t* piksele) {
    if (!piksele || szer <= 0 || wys <= 0) return false;
    const uint64_t wynik = bws_wywolaj(
        57,
        spakuj_dwa_i32(legacy_x(x), legacy_y(y)),
        spakuj_dwa_i32(legacy_w(szer), legacy_h(wys)),
        reinterpret_cast<uint64_t>(piksele),
        0);
    return wynik == 1ULL;
}

bool gui_blit_alpha(int x,int y,int w,int h,const uint32_t* pixels){if(!pixels||w<1||h<1)return false;return bws_wywolaj(57,spakuj_dwa_i32(x,y),spakuj_dwa_i32(w,h),reinterpret_cast<uint64_t>(pixels),3)==1;}

bool gui_blit_popup(int x, int y, int szer, int wys, const uint32_t* piksele) {
    if (!piksele || szer <= 0 || wys <= 0) return false;
    const uint64_t wynik = bws_wywolaj(
        57,
        spakuj_dwa_i32(legacy_x(x), legacy_y(y)),
        spakuj_dwa_i32(legacy_w(szer), legacy_h(wys)),
        reinterpret_cast<uint64_t>(piksele),
        1);
    return wynik == 1ULL;
}

bool gui_ustaw_tytul_okna(const char* tytul) {
    if(!tytul)return false;
#if defined(BSF_NATIVE)
    size_t length=0;while(tytul[length]&&length<4096)++length;size_t at=0;
    while(at<length){uint32_t cp;size_t n=BSF::utf8(tytul+at,length-at,cp);if(at+n>=sizeof(bsf_window_title))break;for(size_t j=0;j<n;++j)bsf_window_title[at+j]=tytul[at+j];at+=n;}bsf_window_title[at]=0;bsf_title_generation=0;
    bool result=bws_wywolaj(58,reinterpret_cast<uint64_t>(bsf_window_title),0,0,0)==1;bsf_update_window_title();return result;
#else
    return bws_wywolaj(58,reinterpret_cast<uint64_t>(tytul),0,0,0)==1;
#endif
}

void gui_ustaw_przejecie_myszy(bool stan) {
    bws_wywolaj(
        22,
        stan ? 1ULL : 0ULL,
        0, 0, 0);
}

bool gui_pobierz_zdarzenie(bws_zdarzenie* zdarzenie) {
    if (!zdarzenie) return false;
    const bool ok = bws_wywolaj(37,
        reinterpret_cast<uint64_t>(zdarzenie), 0, 0, 0) == 1ULL;
    if (ok) {
        if (zdarzenie->typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) motyw_odswiez_lokalny();
        legacy_przetworz_zdarzenie(zdarzenie);
    }
    return ok;
}

bool gui_czekaj_na_zdarzenie(bws_zdarzenie* zdarzenie) {
    if (!zdarzenie) return false;
    uint64_t timeout = 0;
    if (legacy_test_timer_do_wyjscia) timeout = 10;
    const uint64_t res = bws_wywolaj(38,
        reinterpret_cast<uint64_t>(zdarzenie), timeout, 0, 0);
    if (legacy_test_timer_do_wyjscia && res == 2ULL) {
        gui_zakoncz_aplikacje();
    }
    const bool ok = res == 1ULL;
    if (ok) {
        if (zdarzenie->typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) motyw_odswiez_lokalny();
        legacy_przetworz_zdarzenie(zdarzenie);
    }
    return ok;
}

bool gui_czekaj_na_zdarzenie_z_timeoutem(bws_zdarzenie* zdarzenie, uint64_t timeout_ticki) {
    if (!zdarzenie) return false;
    if (timeout_ticki == 0) return gui_czekaj_na_zdarzenie(zdarzenie);
    const uint64_t to = legacy_test_timer_do_wyjscia ? 10 : timeout_ticki;
    const uint64_t wynik = bws_wywolaj(
        38, reinterpret_cast<uint64_t>(zdarzenie), to, 0, 0);
    if (legacy_test_timer_do_wyjscia && wynik == 2ULL) {
        gui_zakoncz_aplikacje();
    }
    const bool ok = wynik == 1ULL;
    if (ok) {
        if (zdarzenie->typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) motyw_odswiez_lokalny();
        legacy_przetworz_zdarzenie(zdarzenie);
    }
    return ok;
}

void gui_ustaw_capture_myszy(bool stan) {
    bws_wywolaj(39, stan ? 1ULL : 0ULL, 0, 0, 0);
}

void gui_ustaw_system_overlay(bool otwarty,
                              int x, int y, int szer, int wys) {
    bws_wywolaj(40, otwarty ? 1ULL : 0ULL,
                spakuj_dwa_i32(legacy_x(x), legacy_y(y)),
                spakuj_dwa_i32(legacy_w(szer), legacy_h(wys)), 0);
}

bool gui_minimalizuj_okno() {
    return bws_wywolaj(41, 0, 0, 0, 0) == 1ULL;
}

uint32_t gui_pobierz_okna(GuiOknoInfo* okna, uint32_t max) {
    if (!okna || max == 0) return 0;
    return static_cast<uint32_t>(bws_wywolaj(
        42, reinterpret_cast<uint64_t>(okna), max, 0, 0));
}

bool gui_aktywuj_okno(uint64_t window_id) {
    return window_id != 0 && bws_wywolaj(43, window_id, 0, 0, 0) == 1ULL;
}

uint32_t gui_pobierz_okna_programu(GuiOknoInfo* okna,uint32_t max,const char* path) {
    if(!okna||!max||!path)return 0;
    return static_cast<uint32_t>(bws_wywolaj(42,reinterpret_cast<uint64_t>(okna),max,reinterpret_cast<uint64_t>(path),0));
}

uint32_t gui_pobierz_okna_aplikacji(GuiOknoInfo* okna,uint32_t max,const char* path) {
    if(!okna||!max||!path)return 0;
    return static_cast<uint32_t>(bws_wywolaj(42,reinterpret_cast<uint64_t>(okna),max,reinterpret_cast<uint64_t>(path),
        BWS_OKNA_TOZSAMOSC_APLIKACJI));
}

bool usun_twor_uzytkownika(const char* sciezka) {
    return sciezka && bws_wywolaj(7, reinterpret_cast<uint64_t>(sciezka)) == 1ULL;
}

WynikPlikowy usun_drzewo_uzytkownika(const char* sciezka) {
    if(!sciezka)return WynikPlikowy::NieprawidlowaNazwa;
    return static_cast<WynikPlikowy>(bws_wywolaj(7,reinterpret_cast<uint64_t>(sciezka),BWS_USUN_DRZEWO));
}
bool gui_zastap_region(int x,int y,int szer,int wys,const uint32_t* piksele) {
    if(!piksele || szer<=0 || wys<=0)return false;
    return bws_wywolaj(57,spakuj_dwa_i32(legacy_x(x),legacy_y(y)),
        spakuj_dwa_i32(legacy_w(szer),legacy_h(wys)),reinterpret_cast<uint64_t>(piksele),2)==1;
}
bool gui_zastap_regiony(const BwsGuiRegion* regions,uint32_t count,uint32_t generation) {
    if(!regions || !count || count>BWS_GUI_BATCH_MAX)return false;
    const BwsGuiBatch batch{count,generation,reinterpret_cast<uint64_t>(regions)};
    return bws_wywolaj(57,reinterpret_cast<uint64_t>(&batch),sizeof(batch),0,4)==1;
}

bool zmien_nazwe_uzytkownika(const char* sciezka, const char* nowa_nazwa) {
    return sciezka && nowa_nazwa &&
        bws_wywolaj(8, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(nowa_nazwa)) == 1ULL;
}

bool pobierz_metadane_pliku(const char* sciezka,
                            BwsMetadanePliku* metadane) {
    return sciezka && metadane &&
        bws_wywolaj(47, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(metadane)) == 1ULL;
}

bool przenies_twor_uzytkownika(const char* sciezka,
                               const char* folder_docelowy) {
    return sciezka && folder_docelowy &&
        bws_wywolaj(48, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(folder_docelowy)) == 1ULL;
}

bool gui_rejestruj_cele_drop(const BwsCelDrop* cele, uint32_t liczba) {
    if (liczba > BWS_DROP_CELE_MAX || (liczba != 0 && !cele)) return false;
    if (legacy_wlaczony && cele) {
        for (uint32_t i = 0; i < liczba; ++i) {
            legacy_cele_drop[i] = cele[i];
            legacy_cele_drop[i].x = legacy_x(cele[i].x);
            legacy_cele_drop[i].y = legacy_y(cele[i].y);
            legacy_cele_drop[i].szer = legacy_w(cele[i].szer);
            legacy_cele_drop[i].wys = legacy_h(cele[i].wys);
        }
        cele = legacy_cele_drop;
    }
    return bws_wywolaj(49, reinterpret_cast<uint64_t>(cele), liczba) == 1ULL;
}

BwsWynikDrop gui_aktualizuj_drag(const char* sciezka,
                                 int x, int y, bool wykonaj_drop) {
    if (!sciezka) return BWS_DROP_BLAD;
    const uint64_t pozycja = spakuj_dwa_i32(legacy_x(x), legacy_y(y));
    const uint64_t wynik = bws_wywolaj(
        50, reinterpret_cast<uint64_t>(sciezka), pozycja,
        wykonaj_drop ? 1ULL : 0ULL);
    return wynik <= BWS_DROP_BLAD
        ? static_cast<BwsWynikDrop>(wynik) : BWS_DROP_BLAD;
}

bool ustaw_schowek_wielu_plikow(const char (*paths)[BWS_DROP_SCIEZKA_MAX],uint32_t count,BwsOperacjaSchowka operation) {
    return paths && count>0 && count<=BWS_SCHOWEK_MAX_PLIKOW &&
        bws_wywolaj(52,reinterpret_cast<uint64_t>(paths),operation,count,0)==1;
}
bool pobierz_element_schowka_plikow(BwsSchowekPlikow* out,uint32_t index,uint64_t generation) {
    return out && bws_wywolaj(53,reinterpret_cast<uint64_t>(out),index,generation,0)==1;
}
bool usun_prefiks_schowka_plikow(uint64_t generation,uint32_t count) {
    return count && bws_wywolaj(54,generation,count,0,0)==1;
}
bool ustaw_schowek_plikow(const char* sciezka,
                          BwsOperacjaSchowka operacja) {
    if (!sciezka || (operacja != BWS_SCHOWEK_COPY &&
                     operacja != BWS_SCHOWEK_CUT)) return false;
    return bws_wywolaj(52, reinterpret_cast<uint64_t>(sciezka),
                       static_cast<uint64_t>(operacja), 0, 0) == 1ULL;
}

bool pobierz_schowek_plikow(BwsSchowekPlikow* schowek) {
    return schowek && bws_wywolaj(
        53, reinterpret_cast<uint64_t>(schowek), 0, 0, 0) == 1ULL;
}

bool wyczysc_schowek_plikow(uint64_t oczekiwana_generacja) {
    return oczekiwana_generacja != 0 &&
        bws_wywolaj(54, oczekiwana_generacja, 0, 0, 0) == 1ULL;
}

bool kopiuj_twor_uzytkownika(const char* sciezka,
                             const char* folder_docelowy) {
    return sciezka && folder_docelowy &&
        bws_wywolaj(55, reinterpret_cast<uint64_t>(sciezka),
                    reinterpret_cast<uint64_t>(folder_docelowy), 0, 0) == 1ULL;
}

bool czytaj_fragment_pliku(const char* sciezka, uint64_t offset,
                           char* bufor, uint32_t dlugosc,
                           uint32_t* odczytano) {
    if (!sciezka || !bufor || dlugosc == 0) return false;
    if (odczytano) *odczytano = 0;

    constexpr uint32_t MAKS_CHUNK = 1024U * 1024U;
    uint32_t calkowicie_odczytano = 0;

    while (calkowicie_odczytano < dlugosc) {
        uint32_t do_odczytu = dlugosc - calkowicie_odczytano;
        if (do_odczytu > MAKS_CHUNK) do_odczytu = MAKS_CHUNK;

        const uint64_t res = bws_wywolaj(
            59,
            reinterpret_cast<uint64_t>(sciezka),
            offset + calkowicie_odczytano,
            reinterpret_cast<uint64_t>(bufor + calkowicie_odczytano),
            static_cast<uint64_t>(do_odczytu));

        if (res == 0 || res > static_cast<uint64_t>(UINT32_MAX) + 1ULL) {
            if (odczytano) *odczytano = calkowicie_odczytano;
            return false;
        }

        const uint32_t got = static_cast<uint32_t>(res - 1ULL);
        if (got > do_odczytu) {
            if (odczytano) *odczytano = calkowicie_odczytano;
            return false;
        }

        calkowicie_odczytano += got;
        if (got < do_odczytu) {
            break;
        }
    }

    if (odczytano) *odczytano = calkowicie_odczytano;
    return true;
}

uint64_t gui_czas_monotoniczny_ns() {
    return bws_wywolaj(60, 0, 0, 0, 0);
}

bool gui_ustaw_popup_aplikacji(bool otwarty,
                               int x, int y, int szer, int wys) {
    return bws_wywolaj(56, otwarty ? 1ULL : 0ULL,
                       spakuj_dwa_i32(legacy_x(x), legacy_y(y)),
                       spakuj_dwa_i32(legacy_w(szer), legacy_h(wys)), 0) == 1ULL;
}

namespace {
#if BURSZTYN_DIAG_GEOM_TRACE
static void trace_dopisz_str(char* buf, size_t cap, size_t* pos, const char* str) {
    if (!buf || !pos || !str || *pos >= cap) return;
    while (*str != '\0' && *pos + 1 < cap) {
        buf[(*pos)++] = *str++;
    }
    buf[*pos] = '\0';
}

static void trace_dopisz_i32(char* buf, size_t cap, size_t* pos, int val) {
    if (!buf || !pos || *pos >= cap) return;
    if (val < 0) {
        if (*pos + 1 < cap) buf[(*pos)++] = '-';
        val = -val;
    }
    char rev[16];
    size_t ile = 0;
    uint32_t uval = static_cast<uint32_t>(val);
    do {
        rev[ile++] = static_cast<char>('0' + (uval % 10));
        uval /= 10;
    } while (uval != 0 && ile < sizeof(rev));
    while (ile > 0 && *pos + 1 < cap) {
        buf[(*pos)++] = rev[--ile];
    }
    buf[*pos] = '\0';
}
#endif
}

static bool g_izolacja_geometrii_wlaczona = false;
static int g_widoczna_geometria_x = 0;
static int g_widoczna_geometria_y = 0;
static int g_widoczna_geometria_w = 0;
static int g_widoczna_geometria_h = 0;
static int g_izolowana_geometria_x = 0;
static int g_izolowana_geometria_y = 0;
static int g_izolowana_geometria_w = 0;
static int g_izolowana_geometria_h = 0;

void gui_ustaw_izolacje_geometrii(bool wlaczona) {
    g_izolacja_geometrii_wlaczona = wlaczona;
}

bool gui_czy_izolacja_geometrii_wlaczona() {
    return g_izolacja_geometrii_wlaczona;
}

void gui_pobierz_widoczna_geometrie(int* x, int* y, int* w, int* h) {
    if (x) *x = g_widoczna_geometria_x;
    if (y) *y = g_widoczna_geometria_y;
    if (w) *w = g_widoczna_geometria_w;
    if (h) *h = g_widoczna_geometria_h;
}

void gui_pobierz_izolowana_geometrie(int* x, int* y, int* w, int* h) {
    if (x) *x = g_izolowana_geometria_x;
    if (y) *y = g_izolowana_geometria_y;
    if (w) *w = g_izolowana_geometria_w;
    if (h) *h = g_izolowana_geometria_h;
}

void gui_log_geom_trace(const char* caller, int old_x, int old_y, int old_w, int old_h,
                        int req_x, int req_y, int req_w, int req_h,
                        bool isolation, bool reaches_kernel,
                        int vis_x, int vis_y, int vis_w, int vis_h) {
#if BURSZTYN_DIAG_GEOM_TRACE
    char buf[256];
    size_t pos = 0;
    buf[0] = '\0';
    trace_dopisz_str(buf, sizeof(buf), &pos, "[GEOM-TRACE] caller=\"");
    trace_dopisz_str(buf, sizeof(buf), &pos, caller ? caller : "unknown");
    trace_dopisz_str(buf, sizeof(buf), &pos, "\" old=(");
    trace_dopisz_i32(buf, sizeof(buf), &pos, old_x);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, old_y);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, old_w);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, old_h);
    trace_dopisz_str(buf, sizeof(buf), &pos, ") req=(");
    trace_dopisz_i32(buf, sizeof(buf), &pos, req_x);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, req_y);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, req_w);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, req_h);
    trace_dopisz_str(buf, sizeof(buf), &pos, ") isolation=");
    trace_dopisz_i32(buf, sizeof(buf), &pos, isolation ? 1 : 0);
    trace_dopisz_str(buf, sizeof(buf), &pos, " reaches_kernel=");
    trace_dopisz_i32(buf, sizeof(buf), &pos, reaches_kernel ? 1 : 0);
    trace_dopisz_str(buf, sizeof(buf), &pos, " visible=(");
    trace_dopisz_i32(buf, sizeof(buf), &pos, vis_x);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, vis_y);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, vis_w);
    trace_dopisz_str(buf, sizeof(buf), &pos, ",");
    trace_dopisz_i32(buf, sizeof(buf), &pos, vis_h);
    trace_dopisz_str(buf, sizeof(buf), &pos, ")\n");
    bws_wywolaj(1, reinterpret_cast<uint64_t>(buf), 0, 0, 0);
#else
    (void)caller;
    (void)old_x;
    (void)old_y;
    (void)old_w;
    (void)old_h;
    (void)req_x;
    (void)req_y;
    (void)req_w;
    (void)req_h;
    (void)isolation;
    (void)reaches_kernel;
    (void)vis_x;
    (void)vis_y;
    (void)vis_w;
    (void)vis_h;
#endif
}

bool gui_ustaw_geometrie_okna(int x, int y, int szer, int wys, int stan) {
#if BURSZTYN_DIAG_GEOM_TRACE
    const int old_x = g_widoczna_geometria_x;
    const int old_y = g_widoczna_geometria_y;
    const int old_w = g_widoczna_geometria_w;
    const int old_h = g_widoczna_geometria_h;
#endif
    const bool isolation = g_izolacja_geometrii_wlaczona;
    if (legacy_wlaczony) {
        legacy_logiczne_okno = {x, y, szer, wys};
        legacy_fizyczne_okno = legacy_przelicz_okno(legacy_logiczne_okno);
        x = legacy_fizyczne_okno.x; y = legacy_fizyczne_okno.y;
        szer = legacy_fizyczne_okno.szer; wys = legacy_fizyczne_okno.wys;
    }

    if (isolation) {
        bool valid = true;
        if (szer <= 0 || wys <= 0) {
            valid = false;
        } else {
            int sw = 1024, sh = 768;
            gui_pobierz_rozdzielczosc(&sw, &sh);
            if (sw <= 0) sw = 1024;
            if (sh <= 0) sh = 768;
            if (szer > sw || wys > sh) valid = false;
        }
        if (valid) {
            g_izolowana_geometria_x = x;
            g_izolowana_geometria_y = y;
            g_izolowana_geometria_w = szer;
            g_izolowana_geometria_h = wys;
        }
#if BURSZTYN_DIAG_GEOM_TRACE
        gui_log_geom_trace("gui_ustaw_geometrie_okna", old_x, old_y, old_w, old_h,
                           x, y, szer, wys, isolation, false,
                           g_widoczna_geometria_x, g_widoczna_geometria_y,
                           g_widoczna_geometria_w, g_widoczna_geometria_h);
#endif
        return valid;
    }

    if (szer <= 0 || wys <= 0) {
#if BURSZTYN_DIAG_GEOM_TRACE
        gui_log_geom_trace("gui_ustaw_geometrie_okna", old_x, old_y, old_w, old_h,
                           x, y, szer, wys, isolation, false,
                           g_widoczna_geometria_x, g_widoczna_geometria_y,
                           g_widoczna_geometria_w, g_widoczna_geometria_h);
#endif
        return false;
    }

    // BWS62 zachowuje stan okna w argumencie 3: 0=nieokreslony,
    // 1=normalny, 2=zmaksymalizowany. Sygnatura i numer ABI pozostaja bez zmian.
    const uint64_t res = bws_wywolaj(BWS_GUI_GEOMETRIA, spakuj_dwa_i32(x, y),
                                     spakuj_dwa_i32(szer, wys),
                                     static_cast<uint64_t>(static_cast<uint32_t>(stan)), 0);
    const bool ok = (res == 1ULL);
    if (ok) {
        g_widoczna_geometria_x = x;
        g_widoczna_geometria_y = y;
        g_widoczna_geometria_w = szer;
        g_widoczna_geometria_h = wys;
    }
#if BURSZTYN_DIAG_GEOM_TRACE
    gui_log_geom_trace("gui_ustaw_geometrie_okna", old_x, old_y, old_w, old_h,
                       x, y, szer, wys, isolation, true,
                       g_widoczna_geometria_x, g_widoczna_geometria_y,
                       g_widoczna_geometria_w, g_widoczna_geometria_h);
#endif
    return ok;
}

uint32_t gui_liczba_monitorow() {
    const uint64_t n = bws_wywolaj(BWS_GUI_MONITOR_INFO, 0, 0, 0, 0);
    return n == UINT64_MAX || n > UINT32_MAX ? 0U : static_cast<uint32_t>(n);
}

bool gui_pobierz_monitor(uint32_t indeks, GuiMonitorInfo* info) {
    if (!info) return false;
    const bool ok = bws_wywolaj(BWS_GUI_MONITOR_INFO, indeks,
        reinterpret_cast<uint64_t>(info), 0, 0) == 1ULL;
    if (ok && legacy_wlaczony) {
        const uint32_t p = ui_skala_poprawna(info->scale_x1000 / 10U)
            ? info->scale_x1000 / 10U : legacy_skala;
        info->x = ui_odskaluj(info->x, p);
        info->y = ui_odskaluj(info->y, p);
        info->szer = ui_odskaluj(info->szer, p);
        info->wys = ui_odskaluj(info->wys, p);
        info->work_x = ui_odskaluj(info->work_x, p);
        info->work_y = ui_odskaluj(info->work_y, p);
        info->work_szer = ui_odskaluj(info->work_szer, p);
        info->work_wys = ui_odskaluj(info->work_wys, p);
    }
    return ok;
}

bool gui_ustaw_obszar_roboczy(int x, int y, int szer, int wys) {
    if (szer <= 0 || wys <= 0) return false;
    return bws_wywolaj(BWS_GUI_USTAW_WORK_AREA, spakuj_dwa_i32(x, y),
                       spakuj_dwa_i32(szer, wys), 0, 0) == 1ULL;
}

uint64_t shm_create(uint64_t rozmiar, uint32_t flagi) {
    const uint64_t h = bws_wywolaj(BWS_SHM_CREATE, rozmiar, flagi, 0, 0);
    return h == UINT64_MAX ? 0 : h;
}

void* shm_map(uint64_t handle, uint32_t flagi) {
    if (!handle) return nullptr;
    const uint64_t a = bws_wywolaj(BWS_SHM_MAP, handle, flagi, 0, 0);
    return a == 0 || a == UINT64_MAX ? nullptr : reinterpret_cast<void*>(a);
}

bool shm_unmap(void* adres) {
    return adres && bws_wywolaj(BWS_SHM_UNMAP,
        reinterpret_cast<uint64_t>(adres), 0, 0, 0) == 1ULL;
}

bool shm_close(uint64_t handle) {
    return handle && bws_wywolaj(BWS_SHM_CLOSE, handle, 0, 0, 0) == 1ULL;
}

uint64_t ipc_create() {
    const uint64_t h = bws_wywolaj(BWS_IPC_CREATE, 0, 0, 0, 0);
    return h == UINT64_MAX ? 0 : h;
}

bool ipc_send(uint64_t endpoint, const BwsIpcWyslij* wiadomosc) {
    return endpoint && wiadomosc && bws_wywolaj(BWS_IPC_SEND, endpoint,
        reinterpret_cast<uint64_t>(wiadomosc), 0, 0) == 1ULL;
}

bool ipc_receive(uint64_t endpoint, BwsIpcOdbierz* wiadomosc) {
    return endpoint && wiadomosc && bws_wywolaj(BWS_IPC_RECEIVE, endpoint,
        reinterpret_cast<uint64_t>(wiadomosc), 0, 0) == 1ULL;
}

bool ipc_close(uint64_t endpoint) {
    return endpoint && bws_wywolaj(BWS_IPC_CLOSE, endpoint, 0, 0, 0) == 1ULL;
}

uint64_t file_watch(const char* sciezka, uint32_t maska) {
    if (!sciezka || !maska) return 0;
    const uint64_t h = bws_wywolaj(BWS_FILE_WATCH, BWS_FILE_WATCH_DODAJ,
        reinterpret_cast<uint64_t>(sciezka), maska, 0);
    return h == UINT64_MAX ? 0 : h;
}

bool file_unwatch(uint64_t handle) {
    return handle && bws_wywolaj(BWS_FILE_WATCH, BWS_FILE_WATCH_USUN,
                                 handle, 0, 0) == 1ULL;
}

bool clipboard_format(uint32_t indeks, char* mime, uint32_t pojemnosc) {
    return mime && pojemnosc && bws_wywolaj(BWS_CLIPBOARD_ENUM, indeks,
        reinterpret_cast<uint64_t>(mime), pojemnosc, 0) == 1ULL;
}

bool clipboard_size(const char* mime, uint64_t* rozmiar) {
    return mime && rozmiar && bws_wywolaj(BWS_CLIPBOARD_SIZE,
        reinterpret_cast<uint64_t>(mime), reinterpret_cast<uint64_t>(rozmiar),
        0, 0) == 1ULL;
}

bool clipboard_read(const char* mime, void* dane, uint64_t pojemnosc,
                    uint64_t* odczytano) {
    return mime && odczytano && (pojemnosc == 0 || dane) &&
        bws_wywolaj(BWS_CLIPBOARD_READ, reinterpret_cast<uint64_t>(mime),
            reinterpret_cast<uint64_t>(dane), pojemnosc,
            reinterpret_cast<uint64_t>(odczytano)) == 1ULL;
}

bool clipboard_set(const char* mime, const void* dane, uint64_t rozmiar) {
    return mime && (rozmiar == 0 || dane) &&
        bws_wywolaj(BWS_CLIPBOARD_SET, reinterpret_cast<uint64_t>(mime),
            reinterpret_cast<uint64_t>(dane), rozmiar, 0) == 1ULL;
}

static uint64_t pakuj_i32(int a, int b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}
bool gui_ustaw_pasek_zadan(bool otwarty, int x, int y, int szer, int wys) {
    return bws_wywolaj(BWS_GUI_SYSTEM_OVERLAY, otwarty ? 3 : 2, pakuj_i32(x, y), pakuj_i32(szer, wys), 0) == 1ULL;
}
bool gui_ustaw_nad_paskiem(uint64_t window_id, bool wlaczone) {
    return bws_wywolaj(BWS_GUI_SYSTEM_OVERLAY, wlaczone ? 5 : 4, window_id, 0, 0) == 1ULL;
}

bool gui_ustaw_kursor(uint32_t typ) {
    return bws_wywolaj(BWS_GUI_CURSOR_SHAPE, typ, 0, 0, 0) == 1ULL;
}

bool gui_ustaw_kursor_custom(const GuiCustomCursorInfo* info) {
    return info && bws_wywolaj(BWS_GUI_CURSOR_CUSTOM,
        reinterpret_cast<uint64_t>(info), 0, 0, 0) == 1ULL;
}

uint64_t socket_create(uint32_t typ) {
    const uint64_t h = bws_wywolaj(BWS_SOCKET_CREATE, typ, 0, 0, 0);
    return h == UINT64_MAX ? 0 : h;
}

bool socket_connect(uint64_t handle, const uint8_t ipv4[4], uint16_t port) {
    return handle && ipv4 && port && bws_wywolaj(BWS_SOCKET_CONNECT, handle,
        reinterpret_cast<uint64_t>(ipv4), port, 0) == 1ULL;
}

bool socket_send(uint64_t handle, const void* dane, uint32_t rozmiar,
                 BwsIoResult* wynik) {
    return handle && wynik && (rozmiar == 0 || dane) &&
        bws_wywolaj(BWS_SOCKET_SEND, handle, reinterpret_cast<uint64_t>(dane),
            rozmiar, reinterpret_cast<uint64_t>(wynik)) == 1ULL;
}

bool socket_recv(uint64_t handle, void* dane, uint32_t pojemnosc,
                 BwsIoResult* wynik) {
    return handle && dane && pojemnosc && wynik &&
        bws_wywolaj(BWS_SOCKET_RECV, handle, reinterpret_cast<uint64_t>(dane),
            pojemnosc, reinterpret_cast<uint64_t>(wynik)) == 1ULL;
}

bool socket_close(uint64_t handle) {
    return handle && bws_wywolaj(BWS_SOCKET_CLOSE, handle, 0, 0, 0) == 1ULL;
}

bool ustawienia_sciezka(const char* app_id, const char* klucz,
                        char* wynik, uint32_t pojemnosc) {
    if (!app_id || !klucz || !wynik || pojemnosc < 2) return false;
    const char* prefix = "/uzytkownicy/domyslny/Ustawienia/";
    uint32_t out = 0;
    for (uint32_t i = 0; prefix[i]; ++i) {
        if (out + 1U >= pojemnosc) return false;
        wynik[out++] = prefix[i];
    }
    auto segment = [&](const char* s) {
        if (!s || !s[0]) return false;
        for (uint32_t i = 0; s[i]; ++i) {
            const char c = s[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
                return false;
            if (out + 1U >= pojemnosc) return false;
            wynik[out++] = c;
        }
        return true;
    };
    if (!segment(app_id) || out + 1U >= pojemnosc) return false;
    wynik[out++] = '/';
    if (!segment(klucz) || out >= pojemnosc) return false;
    wynik[out] = '\0';
    return true;
}

/* =========================================================================
 * 4b. TRWALE USTAWIENIA PER APLIKACJA
 * ========================================================================= */

namespace {

/* Maksymalny rozmiar jednej wartosci. Ustawienie to krotki tekst, nie plik. */
constexpr uint32_t USTAWIENIA_MAKS_WARTOSC = 4096U;

/*
 * Katalog aplikacji jest tworzony jednorazowo. PSF nie ma "mkdir -p",
 * wiec tworzymy jawnie i ignorujemy blad "juz istnieje".
 */
bool zapewnij_katalog_aplikacji(const char* app_id) {
    char katalog[192];

    if (!ustawienia_sciezka(app_id, "x", katalog, sizeof(katalog))) {
        return false;
    }

    /* Odetnij ostatni segment ("/x") zostawiajac katalog aplikacji. */
    for (int i = static_cast<int>(sizeof(katalog)) - 1; i >= 0; --i) {
        if (katalog[i] == '/') {
            katalog[i] = '\0';
            break;
        }
    }

    if (katalog[0] == '\0') return false;

    /* Tworzymy kazdy poziom sciezki po kolei (/uzytkownicy/domyslny, .../Ustawienia, .../<app>):
     * system tworzy tylko /uzytkownicy i /uzytkownicy/Pulpit, wiec pojedynczy mkdir nie wystarczal.
     * utworz_katalog_uzytkownika zwraca false takze gdy katalog juz jest - to nie jest blad. */
    for (int i = 1; katalog[i] != '\0'; ++i) {
        if (katalog[i] != '/') continue;
        katalog[i] = '\0';
        (void)utworz_katalog_uzytkownika(katalog);
        katalog[i] = '/';
    }
    (void)utworz_katalog_uzytkownika(katalog);
    return true;
}

} // namespace

bool ustawienia_zapisz(const char* app_id, const char* klucz,
                       const char* wartosc) {
    if (!app_id || !klucz || !wartosc) return false;

    uint32_t dlugosc = 0;
    while (wartosc[dlugosc] != '\0') {
        if (dlugosc + 1U >= USTAWIENIA_MAKS_WARTOSC) return false;
        ++dlugosc;
    }

    if (!zapewnij_katalog_aplikacji(app_id)) return false;

    char sciezka[256];

    if (!ustawienia_sciezka(app_id, klucz, sciezka, sizeof(sciezka))) {
        return false;
    }

    /*
     * BWS 2 tworzy plik, jesli go nie ma; istniejący zostawia bez zmian.
     * Bez tego pierwszy zapis klucza zawsze by sie nie powiodl, bo
     * BWS 3 wymaga istniejacego pliku.
     */
    (void)bws_wywolaj(2, reinterpret_cast<uint64_t>(sciezka), 0, 0, 0);

    return zapisz_plik(sciezka, wartosc, dlugosc);
}

bool ustawienia_wczytaj(const char* app_id, const char* klucz,
                        char* wynik, uint32_t pojemnosc) {
    if (!app_id || !klucz || !wynik || pojemnosc < 2) return false;

    wynik[0] = '\0';

    char sciezka[256];

    if (!ustawienia_sciezka(app_id, klucz, sciezka, sizeof(sciezka))) {
        return false;
    }

    uint32_t rozmiar = 0;

    if (!pobierz_rozmiar_pliku(sciezka, &rozmiar)) return false;
    if (rozmiar == 0 || rozmiar >= pojemnosc || rozmiar >= USTAWIENIA_MAKS_WARTOSC) {
        return false;
    }

    uint32_t odczytano = 0;

    if (!czytaj_fragment_pliku(sciezka, 0, wynik, rozmiar, &odczytano)) {
        return false;
    }

    if (odczytano != rozmiar) return false;

    wynik[rozmiar] = '\0';
    return true;
}

bool ustawienia_usun(const char* app_id, const char* klucz) {
    if (!app_id || !klucz) return false;

    char sciezka[256];

    if (!ustawienia_sciezka(app_id, klucz, sciezka, sizeof(sciezka))) {
        return false;
    }

    return usun_twor_uzytkownika(sciezka);
}

/* =========================================================================
 * 5.1 IMPLEMENTACJA PODSYSTEMU MOTYWOW (Ring 3)
 * ========================================================================= */

static BursztynMotyw g_lokalny_motyw = g_domyslny_motyw_fallback;
static bool g_motyw_zainicjalizowany = false;

void motyw_odswiez_lokalny() {
#if defined(BSF_NATIVE)
    BSF::refresh();bsf_update_window_title();
#endif
    BursztynMotyw m{};
    if (bws_wywolaj(BWS_MOTYW_POBIERZ, reinterpret_cast<uint64_t>(&m)) == 1) {
        g_lokalny_motyw = m;
        g_motyw_zainicjalizowany = true;
    }
}

const BursztynMotyw* gui_pobierz_motyw() {
    if (!g_motyw_zainicjalizowany) {
        motyw_odswiez_lokalny();
    }
    return &g_lokalny_motyw;
}

uint32_t motyw_kolor(uint32_t rola) {
    const BursztynMotyw* m = gui_pobierz_motyw();
    switch (rola) {
        case MOTYW_KOLOR_PULPIT: return m->pulpit;
        case MOTYW_KOLOR_OKNOTLO: return m->oknotlo;
        case MOTYW_KOLOR_PANEL: return m->panel;
        case MOTYW_KOLOR_PODNIESIONY: return m->podniesiony;
        case MOTYW_KOLOR_RAMKA: return m->ramka;
        case MOTYW_KOLOR_TEKST: return m->tekst;
        case MOTYW_KOLOR_TEKST_DRUGI: return m->tekstdrugorzedny;
        case MOTYW_KOLOR_WYLACZONY: return m->wylaczony;
        case MOTYW_KOLOR_AKCENT: return m->akcent;
        case MOTYW_KOLOR_AKCENT_NAJECHANY: return m->akcentnajechany;
        case MOTYW_KOLOR_AKCENT_AKTYWNY: return m->akcentaktywny;
        case MOTYW_KOLOR_BLAD: return m->blad;
        case MOTYW_KOLOR_SUKCES: return m->sukces;
        case MOTYW_KOLOR_OSTRZEZENIE: return m->ostrzezenie;
        case MOTYW_KOLOR_INFORMACJA: return m->informacja;
        case MOTYW_KOLOR_POLETEKSTOWE: return m->poletekstowe;
        case MOTYW_KOLOR_ZAZNACZENIE: return m->zaznaczenie;
        case MOTYW_KOLOR_PRZYCISK: return m->przycisk;
        case MOTYW_KOLOR_PRZYCISK_NAJECHANY: return m->przycisknajechany;
        case MOTYW_KOLOR_TEKST_AKCENTU: return m->tekstakcentu;
        case MOTYW_KOLOR_PULPIT_ZAZNACZENIE: return motyw_pulpit_zaznaczenie(*m);
        case MOTYW_KOLOR_PULPIT_RAMKA: return motyw_pulpit_ramka(*m);
        case MOTYW_KOLOR_PULPIT_TEKST: return m->tekst;
        case MOTYW_KOLOR_PULPIT_TEKST_ZAZNACZONY: return motyw_pulpit_tekst_zaznaczony(*m);
        default: return m->tekst;
    }
}

uint16_t motyw_promien(uint32_t element) {
    const BursztynMotyw* m = gui_pobierz_motyw();
    switch (element) {
        case MOTYW_ROG_OKNO: return m->rogokna;
        case MOTYW_ROG_PANEL: return m->rogpanelu;
        case MOTYW_ROG_PRZYCISK: return m->rogprzycisku;
        case MOTYW_ROG_POLE: return m->rogpola;
        case MOTYW_ROG_MENU: return m->rogmenu;
        case MOTYW_ROG_PODPOWIEDZ: return m->rogpodpowiedzi;
        default: return 0;
    }
}

uint8_t motyw_grubosc_ramki() {
    return gui_pobierz_motyw()->gruboscramki;
}

uint8_t motyw_cien_okna() {
    return gui_pobierz_motyw()->cienokna;
}

static uint64_t g_lokalny_podglad_token = 0;

bool motyw_polecenie_ustaw(const char* id) {
    if (!id) return false;
    return bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_USTAW, reinterpret_cast<uint64_t>(id)) == 1;
}

bool motyw_polecenie_podglad(const char* id) {
    if (!id) return false;
    uint64_t res = bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_PODGLAD, reinterpret_cast<uint64_t>(id));
    if (res > 0) {
        g_lokalny_podglad_token = res;
        return true;
    }
    return false;
}

bool motyw_polecenie_zatwierdz() {
    uint64_t res = bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_ZATWIERDZ, g_lokalny_podglad_token);
    g_lokalny_podglad_token = 0;
    return res == 1;
}

bool motyw_polecenie_przywroc() {
    uint64_t res = bws_wywolaj(BWS_MOTYW_POLECENIE, MOTYW_CMD_PRZYWROC, g_lokalny_podglad_token);
    g_lokalny_podglad_token = 0;
    return res == 1;
}

void gui_rysuj_standardowa_belke(int x, int y, int szer,
                                 const char* tytul, bool zmaksymalizowane) {
    const uint32_t p = legacy_wlaczony ? 100U : ui_skala_procent;
    if (!tytul || szer < ui_skaluj(90,p)) return;
    // Normal window chrome is composed by the WM from central theme roles.
    (void)gui_ustaw_tytul_okna(tytul);
    if (!legacy_wlaczony) return;

    const uint32_t kolor_belki = motyw_kolor(MOTYW_KOLOR_PANEL);
    const uint32_t kolor_akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
    const uint32_t kolor_tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
    const uint32_t kolor_zamknij = motyw_kolor(MOTYW_KOLOR_BLAD);
    const uint32_t kolor_zamknij_tekst = 0x00FFFFFF;

    const uint32_t kolor_przycisk_tekst = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);

    const int belka_h = ui_skaluj(28, p);
    const int akcent_h = ui_skaluj(2, p);
    int r = zmaksymalizowane ? 0 : ui_skaluj(motyw_promien(MOTYW_ROG_OKNO), p);
    if (r > belka_h) r = belka_h;
    if (r > szer / 2) r = szer / 2;

    if (r > 0) {
        for (int dy = 0; dy < r; ++dy) {
            int inset = motyw_promien_inset(r, dy);
            if (inset > 0) {
                // 0x00000000 = przezroczysty dla kompozytora
                gui_rysuj_prostokat(x, y + dy, inset, 1, 0x00000000);
                gui_rysuj_prostokat(x + szer - inset, y + dy, inset, 1, 0x00000000);
            }
            int row_w = szer - 2 * inset;
            if (row_w > 0) {
                uint32_t c = (dy < akcent_h) ? kolor_akcent : kolor_belki;
                gui_rysuj_prostokat(x + inset, y + dy, row_w, 1, c);
            }
        }
        if (belka_h > r) {
            gui_rysuj_prostokat(x, y + r, szer, belka_h - r, kolor_belki);
        }
    } else {
        gui_rysuj_prostokat(x, y, szer, belka_h, kolor_belki);
        gui_rysuj_prostokat(x, y, szer, akcent_h, kolor_akcent);
    }

    gui_wypisz_tekst_kolor(x + ui_skaluj(8,p), y + ui_skaluj(7,p), kolor_tekst, tytul);
    // Legacy path only; modern chrome is drawn by the compositor (wm_dekoracje).
    RysujPrzycisk(x + szer - ui_skaluj(74,p), y + ui_skaluj(4,p), ui_skaluj(20,p), ui_skaluj(20,p),
                  kolor_akcent, kolor_przycisk_tekst, "-");
    RysujPrzycisk(x + szer - ui_skaluj(50,p), y + ui_skaluj(4,p), ui_skaluj(20,p), ui_skaluj(20,p),
                  kolor_akcent, kolor_przycisk_tekst,
                  zmaksymalizowane ? "v" : "^");
    RysujPrzycisk(x + szer - ui_skaluj(26,p), y + ui_skaluj(4,p), ui_skaluj(20,p), ui_skaluj(20,p),
                  kolor_zamknij, kolor_zamknij_tekst, "X");
}

gui_akcja_belki gui_hit_test_belki(int mx, int my,
                                   int x, int y, int szer) {
    const uint32_t p = legacy_wlaczony ? 100U : ui_skala_procent;
    return wm_hit_test_belki(mx,my,x,y,szer,p,ui_skaluj(motyw_promien(MOTYW_ROG_OKNO),p));
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
    if (legacy_wlaczony) {
        *w = ui_odskaluj(*w, legacy_skala);
        *h = ui_odskaluj(*h, legacy_skala);
    }
}

int gui_pobierz_szerokosc_znaku(uint32_t znak) {
#if defined(BSF_NATIVE)
    int a=BSF::measure_cp(znak,BSF::ui_size());if(a>=0)return a>0?a-1:0;
#endif
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
#if defined(BSF_NATIVE)
    int h=BSF::height(BSF::ui_size());if(h>0)return h;
#endif
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

    if (legacy_wlaczony) {
        legacy_logiczne_okno = {x, y, szer, wys};
        legacy_fizyczne_okno = legacy_przelicz_okno(legacy_logiczne_okno);
        x = legacy_fizyczne_okno.x; y = legacy_fizyczne_okno.y;
        szer = legacy_fizyczne_okno.szer; wys = legacy_fizyczne_okno.wys;
    }
    const uint64_t pozycja = spakuj_dwa_i32(x, y);

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
    legacy_ma_warstwe = legacy_wlaczony;

    if (wynik - 1ULL >
        static_cast<uint64_t>(INT32_MAX)) {
        return -1;
    }

    g_widoczna_geometria_x = x;
    g_widoczna_geometria_y = y;
    g_widoczna_geometria_w = szer;
    g_widoczna_geometria_h = wys;
#if BURSZTYN_DIAG_GEOM_TRACE
    gui_log_geom_trace("bws_utworz_warstwe", 0, 0, 0, 0,
                       x, y, szer, wys, g_izolacja_geometrii_wlaczona, true,
                       x, y, szer, wys);
#endif

    return static_cast<int>(
        wynik - 1ULL);
}

void bws_przesun_warstwe(int nowy_x,
                         int nowy_y) {
#if BURSZTYN_DIAG_GEOM_TRACE
    const int old_x = g_widoczna_geometria_x;
    const int old_y = g_widoczna_geometria_y;
#endif
    if (legacy_wlaczony && legacy_ma_warstwe) {
        legacy_logiczne_okno.x = nowy_x;
        legacy_logiczne_okno.y = nowy_y;
        legacy_fizyczne_okno = legacy_przelicz_okno(legacy_logiczne_okno);
        nowy_x = legacy_fizyczne_okno.x;
        nowy_y = legacy_fizyczne_okno.y;
    }
    bws_wywolaj(
        34,
        i32_do_u64(nowy_x),
        i32_do_u64(nowy_y),
        0, 0);
    g_widoczna_geometria_x = nowy_x;
    g_widoczna_geometria_y = nowy_y;
#if BURSZTYN_DIAG_GEOM_TRACE
    gui_log_geom_trace("bws_przesun_warstwe", old_x, old_y, g_widoczna_geometria_w, g_widoczna_geometria_h,
                       nowy_x, nowy_y, g_widoczna_geometria_w, g_widoczna_geometria_h,
                       g_izolacja_geometrii_wlaczona, true,
                       nowy_x, nowy_y, g_widoczna_geometria_w, g_widoczna_geometria_h);
#endif
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

    const uint32_t p = legacy_wlaczony ? 100U : ui_skala_procent;
    int r = ui_skaluj(motyw_promien(MOTYW_ROG_PRZYCISK), p);
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (r <= 1) {
        gui_rysuj_prostokat(x, y, w, h, kolor_bg);
    } else {
        for (int dy = 0; dy < r; ++dy) {
            int inset = motyw_promien_inset(r, dy);
            int row_w = w - 2 * inset;
            if (row_w > 0) {
                gui_rysuj_prostokat(x + inset, y + dy, row_w, 1, kolor_bg);
                gui_rysuj_prostokat(x + inset, y + h - 1 - dy, row_w, 1, kolor_bg);
            }
        }
        if (h - 2 * r > 0) {
            gui_rysuj_prostokat(x, y + r, w, h - 2 * r, kolor_bg);
        }
    }

    rysuj_tekst_wysrodkowany(x, y, w, h, 1, kolor_txt, tekst);
}

int oblicz_szerokosc_tekstu(const char* tekst,
                            int skala) {
    if (!tekst)
        return 0;

    const uint32_t procent = legacy_wlaczony
        ? (skala >= 100 ? uint32_t(popraw_skale(skala))
                        : 100U * uint32_t(popraw_skale(skala)))
        : (skala>=100?uint32_t(popraw_skale(skala)):ui_skala_procent*popraw_skale(skala));

#if defined(BSF_NATIVE)
    if(BSF::ready())return BSF::measure_text(tekst,ui_skaluj(BSF::ui_size(),procent));
#endif
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
            ui_skaluj(szerokosc_znaku + 1,procent);

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

    const uint32_t procent = legacy_wlaczony
        ? (skala >= 100 ? uint32_t(popraw_skale(skala))
                        : 100U * uint32_t(popraw_skale(skala)))
        : (skala>=100?uint32_t(popraw_skale(skala)):ui_skala_procent*popraw_skale(skala));

#if defined(BSF_NATIVE)
    // Labels constrained by a button/taskbar cell must retain UTF-8 boundaries.
    char fitted[512];
    size_t length = 0;
    while (tekst[length] && length < sizeof(fitted) - 1) ++length;
    size_t accepted = 0;
    for (size_t at = 0; at < length;) {
        uint32_t cp;
        const size_t consumed = BSF::utf8(tekst + at, length - at, cp);
        if (!consumed || at + consumed >= sizeof(fitted)) break;
        for (size_t j = 0; j < consumed; ++j) fitted[at + j] = tekst[at + j];
        fitted[at + consumed] = 0;
        if (oblicz_szerokosc_tekstu(fitted, procent) > w) break;
        accepted = at += consumed;
    }
    fitted[accepted] = 0;
    tekst = fitted;
#endif
    const int szer_tekstu =
        oblicz_szerokosc_tekstu(
            tekst,
            procent);

    const int wys_tekstu =
        ui_skaluj(gui_pobierz_wysokosc_fontu(),procent);

    const int tx =
        px + (w - szer_tekstu) / 2;

    const int ty =
        py + (h - wys_tekstu) / 2;

    gui_wypisz_tekst_kolor_skala(
        tx,
        ty,
        kolor,
        procent,
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
