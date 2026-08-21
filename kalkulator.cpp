/*
 * Kalkulator GUI dla Bursztyn OS (Ring 3)
 *
 * Wersja poprawiona:
 *  - bezpieczne bufory tekstowe,
 *  - kontrola overflow int64_t,
 *  - poprawne hitboxy [x, x+w),
 *  - bitowa obsluga lewego przycisku myszy,
 *  - brak pelnego redraw przy samym ruchu myszy,
 *  - przesuwanie gotowej warstwy przez BWS 34,
 *  - funkcjonalna lokalna minimalizacja/przywracanie,
 *  - warstwa jest tworzona przed przejeciem GUI/myszy.
 */

#include "bursztyn_gui.h"
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Format .bur
// -----------------------------------------------------------------------------

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

extern "C" __attribute__((noreturn)) void _start();

/*
 * UWAGA: BUR_TEKST_ROZMIAR musi odpowiadac skryptowi linkera aplikacji.
 * Domyslnie zachowujemy aktualny uklad repozytorium:
 *   TEXT: offset 0x1000, VA 0x601000, rozmiar 0x8000 (32768)
 *   DATA: offset 0x9000, VA 0x609000, rozmiar 0x20000 (131072)
 *
 * Po przejsciu linkera na 60 KiB tekstu zmien tylko BUR_TEKST_ROZMIAR
 * na 61440ULL; offset i VA danych wylicza sie automatycznie:
 *   DATA offset = 0x10000, DATA VA = 0x610000.
 */
#ifndef BUR_TEKST_ROZMIAR
#define BUR_TEKST_ROZMIAR 32768ULL
#endif

static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 4096ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x601000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   =
    BUR_TEKST_PRZESUNIECIE + BUR_TEKST_ROZMIAR;
static constexpr uint64_t BUR_DANE_WIRTUALNY       =
    BUR_TEKST_WIRTUALNY + BUR_TEKST_ROZMIAR;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 131072ULL;

static_assert((BUR_TEKST_PRZESUNIECIE & 0xFFFULL) == 0,
              "Offset TEXT musi byc wyrownany do strony");
static_assert((BUR_TEKST_WIRTUALNY & 0xFFFULL) == 0,
              "VA TEXT musi byc wyrownane do strony");
static_assert((BUR_DANE_PRZESUNIECIE & 0xFFFULL) == 0,
              "Offset DATA musi byc wyrownany do strony");
static_assert((BUR_DANE_WIRTUALNY & 0xFFFULL) == 0,
              "VA DATA musi byc wyrownane do strony");

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        BUR_TEKST_PRZESUNIECIE,
        BUR_TEKST_ROZMIAR,
        BUR_TEKST_WIRTUALNY,
        BUR_DANE_PRZESUNIECIE,
        BUR_DANE_ROZMIAR,
        BUR_DANE_WIRTUALNY
    };
}

// -----------------------------------------------------------------------------
// Stan kalkulatora
// -----------------------------------------------------------------------------

static char calc_display[64] __attribute__((section(".data"))) = "0";
static char calc_num_buf[32] __attribute__((section(".data"))) = "0";

static int64_t calc_op1 = 0;
static char calc_op = 0;

enum StanKalkulatora : uint8_t {
    STAN_PIERWSZA_LICZBA = 0,
    STAN_OPERATOR = 1,
    STAN_DRUGA_LICZBA = 2,
    STAN_WYNIK = 3,
    STAN_BLAD = 4
};

static StanKalkulatora calc_state = STAN_PIERWSZA_LICZBA;

static const char* const calc_btns[16] = {
    "7", "8", "9", "/",
    "4", "5", "6", "*",
    "1", "2", "3", "-",
    "C", "0", "=", "+"
};

// -----------------------------------------------------------------------------
// Stan okna
// -----------------------------------------------------------------------------

static int WIN_X = 300;
static int WIN_Y = 150;
static int WIN_W = 320;
static int WIN_H = 380;
static int restore_x = 300, restore_y = 150, restore_w = 320, restore_h = 380;
static bool zmaksymalizowane = false;

static constexpr int PASEK_SYSTEMOWY_H = 40;
static constexpr int Z_ORDER_OKNA = 10;

static bool dragging = false;
static bool aplikacja_zminimalizowana = false;
static int drag_off_x = 0;
static int drag_off_y = 0;

static int screen_w = 1024;
static int screen_h = 768;

// -----------------------------------------------------------------------------
// Bezpieczne operacje tekstowe
// -----------------------------------------------------------------------------

static size_t dlugosc_tekstu(const char* tekst, size_t maks) {
    if (!tekst) return 0;
    size_t i = 0;
    while (i < maks && tekst[i] != '\0') i++;
    return i;
}

static bool ustaw_tekst(char* dest, size_t pojemnosc, const char* src) {
    if (!dest || !src || pojemnosc == 0) return false;

    size_t i = 0;
    while (src[i] != '\0' && i + 1 < pojemnosc) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';

    return src[i] == '\0';
}

static bool dopisz_tekst(char* dest, size_t pojemnosc, const char* src) {
    if (!dest || !src || pojemnosc == 0) return false;

    size_t i = dlugosc_tekstu(dest, pojemnosc);
    if (i >= pojemnosc) {
        dest[pojemnosc - 1] = '\0';
        return false;
    }

    size_t j = 0;
    while (src[j] != '\0' && i + 1 < pojemnosc) {
        dest[i++] = src[j++];
    }
    dest[i] = '\0';

    return src[j] == '\0';
}

static bool dopisz_znak(char* dest, size_t pojemnosc, char znak) {
    char tmp[2] = {znak, '\0'};
    return dopisz_tekst(dest, pojemnosc, tmp);
}

// -----------------------------------------------------------------------------
// Bezpieczna konwersja int64_t
// -----------------------------------------------------------------------------

static constexpr int64_t I64_MIN_WARTOSC = (-9223372036854775807LL - 1LL);
static constexpr int64_t I64_MAX_WARTOSC = 9223372036854775807LL;
static constexpr uint64_t I64_MIN_MODUL = 0x8000000000000000ULL;
static constexpr uint64_t I64_MAX_MODUL = 0x7FFFFFFFFFFFFFFFULL;

static bool calc_str_to_int64(const char* str, int64_t* wynik) {
    if (!str || !wynik) return false;

    size_t i = 0;
    bool ujemna = false;

    if (str[i] == '-') {
        ujemna = true;
        i++;
    }

    if (str[i] < '0' || str[i] > '9') return false;

    const uint64_t limit = ujemna ? I64_MIN_MODUL : I64_MAX_MODUL;
    uint64_t wartosc = 0;

    while (str[i] >= '0' && str[i] <= '9') {
        const uint64_t cyfra = (uint64_t)(str[i] - '0');

        if (wartosc > (limit - cyfra) / 10ULL) {
            return false;
        }

        wartosc = wartosc * 10ULL + cyfra;
        i++;
    }

    if (str[i] != '\0') return false;

    if (ujemna) {
        if (wartosc == I64_MIN_MODUL) {
            *wynik = I64_MIN_WARTOSC;
        } else {
            *wynik = -(int64_t)wartosc;
        }
    } else {
        *wynik = (int64_t)wartosc;
    }

    return true;
}

static void calc_int64_to_str(int64_t val, char* buf, size_t pojemnosc) {
    if (!buf || pojemnosc == 0) return;

    if (val == 0) {
        if (pojemnosc >= 2) {
            buf[0] = '0';
            buf[1] = '\0';
        } else {
            buf[0] = '\0';
        }
        return;
    }

    const bool ujemna = val < 0;
    uint64_t modul;

    if (ujemna) {
        // Dziala rowniez dla INT64_MIN bez wykonywania -INT64_MIN.
        modul = (uint64_t)(-(val + 1));
        modul += 1ULL;
    } else {
        modul = (uint64_t)val;
    }

    char odwrotnie[32];
    size_t n = 0;

    while (modul > 0 && n < sizeof(odwrotnie)) {
        odwrotnie[n++] = (char)('0' + (modul % 10ULL));
        modul /= 10ULL;
    }

    size_t out = 0;
    if (ujemna && out + 1 < pojemnosc) {
        buf[out++] = '-';
    }

    while (n > 0 && out + 1 < pojemnosc) {
        buf[out++] = odwrotnie[--n];
    }

    buf[out] = '\0';
}

static bool wykonaj_operacje(int64_t a, int64_t b, char op, int64_t* wynik) {
    if (!wynik) return false;

    switch (op) {
        case '+':
            return !__builtin_add_overflow(a, b, wynik);

        case '-':
            return !__builtin_sub_overflow(a, b, wynik);

        case '*':
            return !__builtin_mul_overflow(a, b, wynik);

        case '/':
            if (b == 0) return false;
            if (a == I64_MIN_WARTOSC && b == -1) return false;
            *wynik = a / b;
            return true;

        default:
            return false;
    }
}

static void ustaw_blad() {
    ustaw_tekst(calc_display, sizeof(calc_display), "ERR");
    ustaw_tekst(calc_num_buf, sizeof(calc_num_buf), "0");
    calc_op1 = 0;
    calc_op = 0;
    calc_state = STAN_BLAD;
}

static void resetuj_kalkulator() {
    ustaw_tekst(calc_display, sizeof(calc_display), "0");
    ustaw_tekst(calc_num_buf, sizeof(calc_num_buf), "0");
    calc_op1 = 0;
    calc_op = 0;
    calc_state = STAN_PIERWSZA_LICZBA;
}

static bool oblicz_biezace(int64_t* wynik) {
    if (!wynik || calc_state != STAN_DRUGA_LICZBA || calc_op == 0) {
        return false;
    }

    int64_t op2 = 0;
    if (!calc_str_to_int64(calc_num_buf, &op2)) return false;

    return wykonaj_operacje(calc_op1, op2, calc_op, wynik);
}

// -----------------------------------------------------------------------------
// Logika kalkulatora
// -----------------------------------------------------------------------------

static void KalkulatorKlik(char btn) {
    if (btn >= '0' && btn <= '9') {
        if (calc_state == STAN_WYNIK || calc_state == STAN_BLAD) {
            char tmp[2] = {btn, '\0'};
            ustaw_tekst(calc_num_buf, sizeof(calc_num_buf), tmp);
            ustaw_tekst(calc_display, sizeof(calc_display), tmp);
            calc_op = 0;
            calc_state = STAN_PIERWSZA_LICZBA;
            return;
        }

        if (calc_state == STAN_OPERATOR) {
            char tmp[2] = {btn, '\0'};
            ustaw_tekst(calc_num_buf, sizeof(calc_num_buf), tmp);

            if (!dopisz_znak(calc_display, sizeof(calc_display), btn)) {
                ustaw_blad();
                return;
            }

            calc_state = STAN_DRUGA_LICZBA;
            return;
        }

        // Pierwsza lub druga liczba.
        if (calc_num_buf[0] == '0' && calc_num_buf[1] == '\0') {
            calc_num_buf[0] = btn;
            calc_num_buf[1] = '\0';

            if (calc_state == STAN_PIERWSZA_LICZBA) {
                calc_display[0] = btn;
                calc_display[1] = '\0';
            } else if (calc_state == STAN_DRUGA_LICZBA) {
                // Drugi argument zaczynajacy sie od zera: zamiast tworzyc
                // np. "12+05", podmieniamy ostatnie zero na nowa cyfre.
                const size_t len = dlugosc_tekstu(calc_display, sizeof(calc_display));
                if (len > 0 && len < sizeof(calc_display)) {
                    calc_display[len - 1] = btn;
                }
            }
            return;
        }

        if (!dopisz_znak(calc_num_buf, sizeof(calc_num_buf), btn)) {
            // Osiagnieto maksymalna dlugosc liczby. Ignorujemy dalsze cyfry.
            return;
        }

        if (!dopisz_znak(calc_display, sizeof(calc_display), btn)) {
            ustaw_blad();
        }
        return;
    }

    if (btn == 'C' || btn == 'c') {
        resetuj_kalkulator();
        return;
    }

    if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
        if (calc_state == STAN_BLAD) {
            resetuj_kalkulator();
        }

        if (calc_state == STAN_OPERATOR) {
            // Zmiana operatora bez wpisania drugiego argumentu.
            calc_op = btn;
            const size_t len = dlugosc_tekstu(calc_display, sizeof(calc_display));
            if (len > 0 && len < sizeof(calc_display)) {
                calc_display[len - 1] = btn;
            }
            return;
        }

        if (calc_state == STAN_DRUGA_LICZBA) {
            int64_t wynik = 0;
            if (!oblicz_biezace(&wynik)) {
                ustaw_blad();
                return;
            }

            calc_op1 = wynik;
            calc_int64_to_str(wynik, calc_num_buf, sizeof(calc_num_buf));
            calc_int64_to_str(wynik, calc_display, sizeof(calc_display));

            if (!dopisz_znak(calc_display, sizeof(calc_display), btn)) {
                ustaw_blad();
                return;
            }

            calc_op = btn;
            calc_state = STAN_OPERATOR;
            return;
        }

        // Pierwsza liczba albo wynik poprzedniego dzialania.
        int64_t wartosc = 0;
        if (!calc_str_to_int64(calc_num_buf, &wartosc)) {
            ustaw_blad();
            return;
        }

        calc_op1 = wartosc;
        calc_op = btn;

        if (calc_state == STAN_WYNIK) {
            calc_int64_to_str(calc_op1, calc_display, sizeof(calc_display));
        }

        if (!dopisz_znak(calc_display, sizeof(calc_display), btn)) {
            ustaw_blad();
            return;
        }

        calc_state = STAN_OPERATOR;
        return;
    }

    if (btn == '=' || btn == '\n' || btn == '\r') {
        if (calc_state != STAN_DRUGA_LICZBA) return;

        int64_t wynik = 0;
        if (!oblicz_biezace(&wynik)) {
            ustaw_blad();
            return;
        }

        calc_op1 = wynik;
        calc_op = 0;
        calc_int64_to_str(wynik, calc_num_buf, sizeof(calc_num_buf));
        calc_int64_to_str(wynik, calc_display, sizeof(calc_display));
        calc_state = STAN_WYNIK;
    }
}

// -----------------------------------------------------------------------------
// Pomocnicze funkcje GUI
// -----------------------------------------------------------------------------

static bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void ogranicz_pozycje_okna(int* x, int* y) {
    if (!x || !y) return;

    int max_x = screen_w - WIN_W;
    int max_y = screen_h - PASEK_SYSTEMOWY_H - WIN_H;

    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;

    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x > max_x) *x = max_x;
    if (*y > max_y) *y = max_y;
}

static void RysujInterfejs(bool wyczysc_warstwe) {
    if (wyczysc_warstwe) {
        // Po poprawkach w grafika.cpp ta funkcja czysci tylko warstwe procesu.
        gui_odswiez_pulpit();
    }

    if (aplikacja_zminimalizowana) {
        return;
    }

    gui_rysuj_okno(WIN_X, WIN_Y, WIN_W, WIN_H, "Kalkulator Systemowy");
    gui_rysuj_standardowa_belke(WIN_X, WIN_Y, WIN_W,
                                "Kalkulator Systemowy", zmaksymalizowane);

    // Wyswietlacz.
    gui_rysuj_prostokat(WIN_X + 10, WIN_Y + 35, WIN_W - 20, 50, 0x00050200);
    gui_rysuj_prostokat(WIN_X + 10, WIN_Y + 84, WIN_W - 20, 2, 0x00E58A00);

    int szer_tekstu = oblicz_szerokosc_tekstu(calc_display, 2);
    int text_x = WIN_X + WIN_W - 30 - szer_tekstu;
    if (text_x < WIN_X + 15) text_x = WIN_X + 15;

    const int text_y = WIN_Y + 44;
    gui_wypisz_tekst_kolor_skala(text_x, text_y, 0x00FFBF00, 2, calc_display);

    // Siatka przyciskow.
    const int start_x = WIN_X + 10;
    const int start_y = WIN_Y + 95;
    const int bw = (WIN_W - 50) / 4;
    const int bh = (WIN_H - 140) / 4;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            const int bx = start_x + col * (bw + 10);
            const int by = start_y + row * (bh + 10);

            const char btn_char = calc_btns[row * 4 + col][0];
            uint32_t btn_color = 0x00242424;
            uint32_t txt_color = 0x00FFFFFF;

            if (btn_char == 'C') {
                btn_color = 0x00661100;
                txt_color = 0x00FF5555;
            } else if (btn_char == '=') {
                btn_color = 0x00E58A00;
                txt_color = 0x001A0B00;
            } else if (btn_char == '/' || btn_char == '*' ||
                       btn_char == '-' || btn_char == '+') {
                btn_color = 0x00302211;
                txt_color = 0x00E58A00;
            }

            gui_rysuj_prostokat(bx, by, bw, bh, btn_color);

            // Ramka 1 px.
            gui_rysuj_prostokat(bx, by, bw, 1, 0x00404040);
            gui_rysuj_prostokat(bx, by + bh - 1, bw, 1, 0x00101010);
            gui_rysuj_prostokat(bx, by, 1, bh, 0x00404040);
            gui_rysuj_prostokat(bx + bw - 1, by, 1, bh, 0x00101010);

            rysuj_tekst_wysrodkowany(bx, by, bw, bh, 2,
                                     txt_color, calc_btns[row * 4 + col]);
        }
    }

    gui_odswiez();
}

// -----------------------------------------------------------------------------
// Punkt wejscia aplikacji
// -----------------------------------------------------------------------------

extern "C" __attribute__((noreturn)) void _start() {
    resetuj_kalkulator();

    dragging = false;
    aplikacja_zminimalizowana = false;
    drag_off_x = 0;
    drag_off_y = 0;

    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);

    if (screen_w <= 0 || screen_h <= 0) {
        gui_zakoncz_aplikacje();
    }

    ogranicz_pozycje_okna(&WIN_X, &WIN_Y);

    // Najpierw tworzymy prywatna warstwe. Dopiero potem wlaczamy GUI/mysz.
    if (bws_utworz_warstwe(WIN_X, WIN_Y, WIN_W, WIN_H, Z_ORDER_OKNA) < 0) {
        gui_zakoncz_aplikacje();
    }

    gui_ustaw_przejecie_myszy(true);

    bool wyjdz = false;

    RysujInterfejs(true);

    while (!wyjdz) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        if (zdarzenie.typ == BWS_ZDARZENIE_FOCUS && aplikacja_zminimalizowana)
            aplikacja_zminimalizowana = false;
        const int mx = zdarzenie.x;
        const int my = zdarzenie.y;
        const bool lewy = (zdarzenie.przyciski & 0x01U) != 0;
        const bool klik_lewy = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;
        const bool puszczenie_lewego = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP;

        bool redraw = false;
        bool pelne_czyszczenie = false;

        // -------------------------------------------------------------
        // Ukryte okno przywraca menedzer przez stabilny window_id.
        // -------------------------------------------------------------
        if (aplikacja_zminimalizowana) {
            continue;
        }

        // -------------------------------------------------------------
        // Rozpoczecie klikniecia w normalnym oknie.
        // -------------------------------------------------------------
        if (klik_lewy &&
            punkt_w_prostokacie(mx, my, WIN_X, WIN_Y, WIN_W, WIN_H)) {
            const gui_akcja_belki akcja =
                gui_hit_test_belki(mx, my, WIN_X, WIN_Y, WIN_W);
            if (akcja == GUI_BELKA_MINIMALIZUJ) {
                aplikacja_zminimalizowana = gui_minimalizuj_okno();
                dragging = false;
                gui_ustaw_capture_myszy(false);
            }
            else if (akcja == GUI_BELKA_ZAMKNIJ) {
                wyjdz = true;
                dragging = false;
            }
            else if (akcja == GUI_BELKA_MAKSYMALIZUJ) {
                if (!zmaksymalizowane) {
                    restore_x = WIN_X; restore_y = WIN_Y;
                    restore_w = WIN_W; restore_h = WIN_H;
                    WIN_X = 0; WIN_Y = 0; WIN_W = screen_w;
                    WIN_H = screen_h - PASEK_SYSTEMOWY_H;
                    zmaksymalizowane = true;
                } else {
                    WIN_X = restore_x; WIN_Y = restore_y;
                    WIN_W = restore_w; WIN_H = restore_h;
                    zmaksymalizowane = false;
                }
                if (bws_utworz_warstwe(WIN_X, WIN_Y, WIN_W, WIN_H,
                                       Z_ORDER_OKNA) < 0) {
                    gui_zakoncz_aplikacje();
                }
                redraw = true;
                pelne_czyszczenie = true;
            }
            else if (akcja == GUI_BELKA_DRAG && !zmaksymalizowane) {
                dragging = true;
                gui_ustaw_capture_myszy(true);
                drag_off_x = mx - WIN_X;
                drag_off_y = my - WIN_Y;
            }
            else {
                const int start_x = WIN_X + 10;
                const int start_y = WIN_Y + 95;
                const int bw = (WIN_W - 50) / 4;
                const int bh = (WIN_H - 140) / 4;

                bool trafiono = false;
                for (int row = 0; row < 4 && !trafiono; row++) {
                    for (int col = 0; col < 4; col++) {
                        const int bx = start_x + col * (bw + 10);
                        const int by = start_y + row * (bh + 10);

                        if (punkt_w_prostokacie(mx, my, bx, by, bw, bh)) {
                            KalkulatorKlik(calc_btns[row * 4 + col][0]);
                            redraw = true;
                            trafiono = true;
                            break;
                        }
                    }
                }
            }
        }

        // -------------------------------------------------------------
        // Przesuwanie gotowej warstwy bez ponownego rysowania widgetow.
        // -------------------------------------------------------------
        if (dragging && lewy) {
            int nowy_x = mx - drag_off_x;
            int nowy_y = my - drag_off_y;
            ogranicz_pozycje_okna(&nowy_x, &nowy_y);

            if (nowy_x != WIN_X || nowy_y != WIN_Y) {
                WIN_X = nowy_x;
                WIN_Y = nowy_y;

                bws_przesun_warstwe(WIN_X, WIN_Y);
            }
        }

        if (puszczenie_lewego && dragging) {
            dragging = false;
            gui_ustaw_capture_myszy(false);
        }

        if (redraw && !wyjdz) {
            RysujInterfejs(pelne_czyszczenie);
        }
    }

    gui_zakoncz_aplikacje();
}
