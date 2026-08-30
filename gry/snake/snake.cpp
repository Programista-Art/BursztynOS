/*
 * Snake dla Bursztyn OS (Ring 3)
 * - standardowy naglowek .bur (60 bajtow)
 * - standardowa belka okna Bursztyna
 * - sterowanie zdarzeniami, bez petli busy-wait opartej o RDTSC
 * - poprawne rosniecie weza
 * - jablko nigdy nie pojawia sie na wezu
 * - blokada zawracania o 180 stopni i wielu skretow w jednym kroku
 * - WASD + strzalki (sekwencje ANSI)
 */

#include "../../bursztyn_gui.h"
#include "../../bws_zdarzenia.h"

/* ========================================================================
 * Standardowy naglowek programu .bur
 * UWAGA: musi pozostac zgodny z linkerem/loaderem Ring 3.
 * sizeof(NaglowekBur) == 60.
 * ======================================================================== */
struct NaglowekBur {
    uint8_t  magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

static_assert(sizeof(NaglowekBur) == 60, "NaglowekBur musi miec dokladnie 60 bajtow");

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        4096, 32768, 0x1001000,
        36864, 131072, 0x1009000
    };
}

namespace {

/* ========================================================================
 * Parametry okna i planszy
 * ======================================================================== */
constexpr int ROZMIAR_KAFELKA = 20;
constexpr int PLANSZA_X = 20;
constexpr int PLANSZA_Y = 15;

/* Pole gry ma sciany na x=0/x=19 i y=0/y=14.
 * Rzeczywiste pole dostepne dla weza: 18 * 13 = 234 pola. */
constexpr int POLA_GRACZA_X = PLANSZA_X - 2;
constexpr int POLA_GRACZA_Y = PLANSZA_Y - 2;
constexpr int MAKS_DLUGOSC_WEZA = POLA_GRACZA_X * POLA_GRACZA_Y;

constexpr int WYSOKOSC_BELKI = 28;
constexpr int SZEROKOSC_OKNA = PLANSZA_X * ROZMIAR_KAFELKA;
constexpr int WYSOKOSC_PLANSZY = PLANSZA_Y * ROZMIAR_KAFELKA;
constexpr int WYSOKOSC_OKNA = WYSOKOSC_BELKI + WYSOKOSC_PLANSZY;

constexpr int OKNO_START_X = 150;
constexpr int OKNO_START_Y = 150;
constexpr int OKNO_Z_ORDER = 10;
constexpr int WYSOKOSC_PASKA_SYSTEMOWEGO = 40;

/*
 * Pozycja okna nie może być constexpr: BWS34 przesuwa warstwę w jądrze,
 * a aplikacja musi od tej chwili rysować pod nowymi współrzędnymi ekranowymi.
 */
static int okno_x = OKNO_START_X;
static int okno_y = OKNO_START_Y;
static int ekran_w = 1024;
static int ekran_h = 768;

static bool przeciaganie_okna = false;
static int chwyt_x = 0;
static int chwyt_y = 0;
static bool przerysuj_po_przeciaganiu = false;

/* Typowy krok Snake ~125 ms = 8 ruchow/s.
 * Gdy timestamp zdarzen jest w ns, korzystamy z tego czasu.
 * Dla starego ABI z malym licznikiem mamy awaryjny licznik TIMER. */
constexpr uint64_t KROK_NS = UINT64_C(125000000);
constexpr uint32_t TIMER_FALLBACK_DZIELNIK = 8;

constexpr uint32_t KOLOR_TLO = 0x00111111;
constexpr uint32_t KOLOR_RAMKA = 0x00E58A00;
constexpr uint32_t KOLOR_JABLKO = 0x00FF3333;
constexpr uint32_t KOLOR_GLOWA = 0x0055FF55;
constexpr uint32_t KOLOR_CIALO = 0x0022AA22;
constexpr uint32_t KOLOR_NAKLADKA = 0x00222222;
constexpr uint32_t KOLOR_BIALY = 0x00FFFFFF;
constexpr uint32_t KOLOR_SZARY = 0x00AAAAAA;

struct Punkt {
    int x;
    int y;
};

static Punkt waz[MAKS_DLUGOSC_WEZA] __attribute__((section(".data"))) = {};
static Punkt jablko __attribute__((section(".data"))) = {1, 1};

static int dlugosc_weza = 3;
static int kierunek_x = 1;
static int kierunek_y = 0;
static int oczekiwany_kierunek_x = 1;
static int oczekiwany_kierunek_y = 0;

static bool zmiana_kierunku_w_tym_kroku = false;
static bool gra_trwa = true;
static bool gra_wygrana = false;
static bool zamknij_aplikacje = false;

static uint64_t rng_stan = UINT64_C(0x9E3779B97F4A7C15);
static uint64_t ostatni_krok_timestamp = 0;
static uint32_t fallback_timer_ticks = 0;

enum class StanAnsi : uint8_t {
    BRAK,
    ESC,
    CSI
};

static StanAnsi ansi = StanAnsi::BRAK;

/* ========================================================================
 * Losowanie - RDTSC jest tu tylko ziarnem gry, a NIE zegarem logiki.
 * ======================================================================== */
static inline uint64_t pobierz_cykle() {
    uint32_t lo = 0;
    uint32_t hi = 0;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static uint32_t losuj_u32() {
    /* xorshift64* - wystarczajacy PRNG do gry, nie do kryptografii. */
    uint64_t x = rng_stan;
    if (x == 0) x = UINT64_C(0xD1B54A32D192ED03);
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_stan = x;
    return static_cast<uint32_t>((x * UINT64_C(2685821657736338717)) >> 32);
}

static bool punkt_na_wezu(int x, int y) {
    for (int i = 0; i < dlugosc_weza; ++i) {
        if (waz[i].x == x && waz[i].y == y) return true;
    }
    return false;
}

static void generuj_jablko() {
    if (dlugosc_weza >= MAKS_DLUGOSC_WEZA) {
        gra_trwa = false;
        gra_wygrana = true;
        return;
    }

    /* Najpierw szybkie losowanie. */
    for (int proba = 0; proba < MAKS_DLUGOSC_WEZA * 2; ++proba) {
        const int x = 1 + static_cast<int>(losuj_u32() % POLA_GRACZA_X);
        const int y = 1 + static_cast<int>(losuj_u32() % POLA_GRACZA_Y);
        if (!punkt_na_wezu(x, y)) {
            jablko = {x, y};
            return;
        }
    }

    /* Deterministyczny fallback - gwarantuje znalezienie wolnego pola. */
    for (int y = 1; y < PLANSZA_Y - 1; ++y) {
        for (int x = 1; x < PLANSZA_X - 1; ++x) {
            if (!punkt_na_wezu(x, y)) {
                jablko = {x, y};
                return;
            }
        }
    }

    gra_trwa = false;
    gra_wygrana = true;
}

static void resetuj_gre() {
    dlugosc_weza = 3;

    waz[0] = {PLANSZA_X / 2, PLANSZA_Y / 2};
    waz[1] = {PLANSZA_X / 2 - 1, PLANSZA_Y / 2};
    waz[2] = {PLANSZA_X / 2 - 2, PLANSZA_Y / 2};

    kierunek_x = 1;
    kierunek_y = 0;
    oczekiwany_kierunek_x = 1;
    oczekiwany_kierunek_y = 0;
    zmiana_kierunku_w_tym_kroku = false;

    gra_trwa = true;
    gra_wygrana = false;

    rng_stan ^= pobierz_cykle();
    if (rng_stan == 0) rng_stan = UINT64_C(0xA0761D6478BD642F);

    generuj_jablko();

    ostatni_krok_timestamp = 0;
    fallback_timer_ticks = 0;
    ansi = StanAnsi::BRAK;
}

/* ========================================================================
 * Sterowanie
 * ======================================================================== */
static void ustaw_kierunek(int nx, int ny) {
    if (!gra_trwa || zmiana_kierunku_w_tym_kroku) return;

    /* Nie wolno zawrocic o 180 stopni w szyje weza. */
    if (nx == -kierunek_x && ny == -kierunek_y) return;

    /* Pusty/niepoprawny kierunek odrzucamy. */
    if ((nx == 0 && ny == 0) || (nx != 0 && ny != 0)) return;

    oczekiwany_kierunek_x = nx;
    oczekiwany_kierunek_y = ny;
    zmiana_kierunku_w_tym_kroku = true;
}

static void obsluz_klawisz(char klawisz) {
    /* Strzalki moga przyjsc jako ESC [ A/B/C/D. */
    if (ansi == StanAnsi::ESC) {
        if (klawisz == '[') ansi = StanAnsi::CSI;
        else ansi = StanAnsi::BRAK;
        return;
    }

    if (ansi == StanAnsi::CSI) {
        ansi = StanAnsi::BRAK;
        if (klawisz == 'A') ustaw_kierunek(0, -1);
        else if (klawisz == 'B') ustaw_kierunek(0, 1);
        else if (klawisz == 'C') ustaw_kierunek(1, 0);
        else if (klawisz == 'D') ustaw_kierunek(-1, 0);
        return;
    }

    if (klawisz == '\x1B') {
        ansi = StanAnsi::ESC;
        return;
    }

    if (klawisz == 'q' || klawisz == 'Q') {
        zamknij_aplikacje = true;
        return;
    }

    if (!gra_trwa) {
        if (klawisz == 'r' || klawisz == 'R') resetuj_gre();
        return;
    }

    if (klawisz == 'w' || klawisz == 'W') ustaw_kierunek(0, -1);
    else if (klawisz == 's' || klawisz == 'S') ustaw_kierunek(0, 1);
    else if (klawisz == 'a' || klawisz == 'A') ustaw_kierunek(-1, 0);
    else if (klawisz == 'd' || klawisz == 'D') ustaw_kierunek(1, 0);
}

/* ========================================================================
 * Logika gry
 * ======================================================================== */
static void krok_gry() {
    if (!gra_trwa) return;

    kierunek_x = oczekiwany_kierunek_x;
    kierunek_y = oczekiwany_kierunek_y;
    zmiana_kierunku_w_tym_kroku = false;

    const Punkt stary_ogon = waz[dlugosc_weza - 1];

    for (int i = dlugosc_weza - 1; i > 0; --i) {
        waz[i] = waz[i - 1];
    }

    waz[0].x += kierunek_x;
    waz[0].y += kierunek_y;

    if (waz[0].x <= 0 || waz[0].x >= PLANSZA_X - 1 ||
        waz[0].y <= 0 || waz[0].y >= PLANSZA_Y - 1) {
        gra_trwa = false;
        return;
    }

    for (int i = 1; i < dlugosc_weza; ++i) {
        if (waz[0].x == waz[i].x && waz[0].y == waz[i].y) {
            gra_trwa = false;
            return;
        }
    }

    if (waz[0].x == jablko.x && waz[0].y == jablko.y) {
        if (dlugosc_weza < MAKS_DLUGOSC_WEZA) {
            /* Poprzedni kod tylko zwiekszal licznik, zostawiajac nowy segment
             * niezainicjalizowany. Nowy segment powstaje dokladnie w miejscu
             * starego ogona. */
            waz[dlugosc_weza] = stary_ogon;
            ++dlugosc_weza;
        }
        generuj_jablko();
    }
}

static bool timer_ma_wykonac_krok(const bws_zdarzenie& zdarzenie) {
    /* W aktualnym ABI timestamp jest zwykle wartoscia czasu w ns.
     * Zachowujemy fallback dla starszej wersji, w ktorej byl malym tickiem. */
    if (zdarzenie.timestamp >= UINT64_C(1000000000)) {
        if (ostatni_krok_timestamp == 0) {
            ostatni_krok_timestamp = zdarzenie.timestamp;
            return false;
        }

        if (zdarzenie.timestamp < ostatni_krok_timestamp ||
            zdarzenie.timestamp - ostatni_krok_timestamp >= KROK_NS) {
            ostatni_krok_timestamp = zdarzenie.timestamp;
            return true;
        }
        return false;
    }

    ++fallback_timer_ticks;
    if (fallback_timer_ticks >= TIMER_FALLBACK_DZIELNIK) {
        fallback_timer_ticks = 0;
        return true;
    }
    return false;
}

/* ========================================================================
 * Rysowanie / obsługa okna
 * ======================================================================== */
static void rysuj_gre();

static int ogranicz_int(int wartosc, int minimum, int maksimum) {
    if (maksimum < minimum) maksimum = minimum;
    if (wartosc < minimum) return minimum;
    if (wartosc > maksimum) return maksimum;
    return wartosc;
}

static void zakoncz_przeciaganie() {
    if (!przeciaganie_okna) return;
    przeciaganie_okna = false;
    gui_ustaw_capture_myszy(false);

    if (przerysuj_po_przeciaganiu) {
        przerysuj_po_przeciaganiu = false;
        rysuj_gre();
    }
}

static void rysuj_gre() {
    const int plansza_x = okno_x;
    const int plansza_y = okno_y + WYSOKOSC_BELKI;

    /*
     * W aktualnym GUI Ring 3 współrzędne rysowania są ekranowe.
     * grafika.cpp odejmuje pozycję warstwy i zapisuje do jej lokalnego bufora.
     */
    gui_rysuj_standardowa_belke(
        okno_x,
        okno_y,
        SZEROKOSC_OKNA,
        "Snake",
        false);

    /*
     * Aktualne ABI ma przesuwanie (BWS34) i minimalizację (BWS41),
     * ale nie ma zmiany rozmiaru warstwy. Nie udajemy więc maksymalizacji:
     * środkowy przycisk jest świadomie pokazany jako nieaktywny.
     */
    RysujPrzycisk(
        okno_x + SZEROKOSC_OKNA - 50,
        okno_y + 4,
        20, 20,
        0x00404040,
        0x00808080,
        "^");

    gui_rysuj_prostokat(
        plansza_x,
        plansza_y,
        SZEROKOSC_OKNA,
        WYSOKOSC_PLANSZY,
        KOLOR_TLO);

    /* Ramka planszy. */
    gui_rysuj_prostokat(
        plansza_x, plansza_y,
        SZEROKOSC_OKNA, 2, KOLOR_RAMKA);
    gui_rysuj_prostokat(
        plansza_x, plansza_y + WYSOKOSC_PLANSZY - 2,
        SZEROKOSC_OKNA, 2, KOLOR_RAMKA);
    gui_rysuj_prostokat(
        plansza_x, plansza_y,
        2, WYSOKOSC_PLANSZY, KOLOR_RAMKA);
    gui_rysuj_prostokat(
        plansza_x + SZEROKOSC_OKNA - 2, plansza_y,
        2, WYSOKOSC_PLANSZY, KOLOR_RAMKA);

    if (!gra_wygrana) {
        gui_rysuj_prostokat(
            plansza_x + jablko.x * ROZMIAR_KAFELKA + 3,
            plansza_y + jablko.y * ROZMIAR_KAFELKA + 3,
            ROZMIAR_KAFELKA - 6,
            ROZMIAR_KAFELKA - 6,
            KOLOR_JABLKO);
    }

    for (int i = dlugosc_weza - 1; i >= 0; --i) {
        const uint32_t kolor =
            (i == 0) ? KOLOR_GLOWA : KOLOR_CIALO;

        gui_rysuj_prostokat(
            plansza_x + waz[i].x * ROZMIAR_KAFELKA + 1,
            plansza_y + waz[i].y * ROZMIAR_KAFELKA + 1,
            ROZMIAR_KAFELKA - 2,
            ROZMIAR_KAFELKA - 2,
            kolor);
    }

    if (!gra_trwa) {
        constexpr int NAKLADKA_W = 250;
        constexpr int NAKLADKA_H = 82;

        const int x =
            plansza_x + (SZEROKOSC_OKNA - NAKLADKA_W) / 2;
        const int y =
            plansza_y + (WYSOKOSC_PLANSZY - NAKLADKA_H) / 2;

        gui_rysuj_prostokat(
            x, y,
            NAKLADKA_W, NAKLADKA_H,
            KOLOR_NAKLADKA);

        gui_rysuj_prostokat(
            x, y,
            NAKLADKA_W, 2,
            gra_wygrana ? KOLOR_RAMKA : KOLOR_JABLKO);

        gui_rysuj_prostokat(
            x, y + NAKLADKA_H - 2,
            NAKLADKA_W, 2,
            gra_wygrana ? KOLOR_RAMKA : KOLOR_JABLKO);

        rysuj_tekst_wysrodkowany(
            x,
            y + 8,
            NAKLADKA_W,
            28,
            1,
            KOLOR_BIALY,
            gra_wygrana ? "WYGRANA!" : "KONIEC GRY");

        rysuj_tekst_wysrodkowany(
            x,
            y + 40,
            NAKLADKA_W,
            26,
            1,
            KOLOR_SZARY,
            "R - ponownie   Q - wyjscie");
    }

    gui_odswiez();
}

static void obsluz_mysz_down(const bws_zdarzenie& zdarzenie) {
    const gui_akcja_belki akcja =
        gui_hit_test_belki(
            zdarzenie.x,
            zdarzenie.y,
            okno_x,
            okno_y,
            SZEROKOSC_OKNA);

    switch (akcja) {
        case GUI_BELKA_ZAMKNIJ:
            zakoncz_przeciaganie();
            zamknij_aplikacje = true;
            return;

        case GUI_BELKA_MINIMALIZUJ:
            zakoncz_przeciaganie();
            (void)gui_minimalizuj_okno();
            return;

        case GUI_BELKA_MAKSYMALIZUJ:
            /*
             * Brak resize warstwy w obecnym publicznym ABI.
             * Przycisk jest narysowany jako nieaktywny.
             */
            return;

        case GUI_BELKA_DRAG:
            przeciaganie_okna = true;
            chwyt_x = zdarzenie.x - okno_x;
            chwyt_y = zdarzenie.y - okno_y;
            gui_ustaw_capture_myszy(true);
            return;

        case GUI_BELKA_BRAK:
        default:
            return;
    }
}

static void obsluz_mysz_ruch(const bws_zdarzenie& zdarzenie) {
    if (!przeciaganie_okna) return;

    /* Capture powinien dostarczyć także ruch poza oknem. */
    if ((zdarzenie.przyciski & 1U) == 0U) {
        zakoncz_przeciaganie();
        return;
    }

    int nowy_x = zdarzenie.x - chwyt_x;
    int nowy_y = zdarzenie.y - chwyt_y;

    int max_x = ekran_w - SZEROKOSC_OKNA;
    int max_y = ekran_h - WYSOKOSC_PASKA_SYSTEMOWEGO - WYSOKOSC_OKNA;

    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    nowy_x = ogranicz_int(nowy_x, 0, max_x);
    nowy_y = ogranicz_int(nowy_y, 0, max_y);

    if (nowy_x == okno_x && nowy_y == okno_y) return;

    /*
     * Nie przerysowujemy całej gry podczas przeciągania.
     * Przesuwamy gotową warstwę, więc compositor może zrobić szybki drag.
     */
    bws_przesun_warstwe(nowy_x, nowy_y);
    okno_x = nowy_x;
    okno_y = nowy_y;
    gui_odswiez();
}

} // namespace

/* ========================================================================
 * Punkt wejscia Ring 3
 * ======================================================================== */
extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&ekran_w, &ekran_h);
    if (ekran_w <= 0 || ekran_h <= WYSOKOSC_PASKA_SYSTEMOWEGO) {
        gui_zakoncz_aplikacje();
    }

    gui_ustaw_przejecie_myszy(true);

    if (bws_utworz_warstwe(
            okno_x,
            okno_y,
            SZEROKOSC_OKNA,
            WYSOKOSC_OKNA,
            OKNO_Z_ORDER) < 0) {
        gui_zakoncz_aplikacje();
    }

    resetuj_gre();
    rysuj_gre();

    while (!zamknij_aplikacje) {
        bws_zdarzenie zdarzenie{};

        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;

        if (zdarzenie.typ == BWS_ZDARZENIE_ZAMKNIJ) {
            zakoncz_przeciaganie();
            zamknij_aplikacje = true;
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_BLUR) {
            zakoncz_przeciaganie();
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ) {
            obsluz_klawisz(static_cast<char>(zdarzenie.kod));

            /* Klawisz R może od razu zresetować ekran końca gry. */
            if (!przeciaganie_okna && !zamknij_aplikacje) {
                rysuj_gre();
            } else if (przeciaganie_okna) {
                przerysuj_po_przeciaganiu = true;
            }
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
            obsluz_mysz_down(zdarzenie);
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH) {
            obsluz_mysz_ruch(zdarzenie);
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP) {
            zakoncz_przeciaganie();
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_TIMER) {
            if (timer_ma_wykonac_krok(zdarzenie)) {
                krok_gry();

                if (przeciaganie_okna) {
                    /*
                     * Logika gry może iść dalej, ale render odkładamy do UP.
                     * Dzięki temu drag nie generuje pełnych redrawów planszy.
                     */
                    przerysuj_po_przeciaganiu = true;
                } else {
                    rysuj_gre();
                }
            }
            continue;
        }
    }

    zakoncz_przeciaganie();
    gui_zakoncz_aplikacje();
}
