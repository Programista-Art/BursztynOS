/*
 * Bursztyn OS - Kalkulator (BursztynUI Modern)
 *
 * Nowoczesna aplikacja Ring 3 oparta na frameworku BursztynUI.
 * Korzysta z centralnego silnika motywów bez prywatnych palet barwnych.
 * Reaguje automatycznie na BWS_ZDARZENIE_MOTYW_ZMIENIONY w czasie rzeczywistym.
 */

#include "loader.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "bursztyn_gui.h"
#include <stddef.h>
#include <stdint.h>

using namespace BursztynUI;

// =============================================================================
// Nagłówek formatu .bur
// =============================================================================

extern "C" __attribute__((noreturn)) void _start();

static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0x30000ULL; // 192 KiB
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0x31000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 0x20000ULL; // 128 KiB
static constexpr uint64_t BUR_DANE_WIRTUALNY       = 0x1031000ULL;

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(&_start),
    BUR_TEKST_PRZESUNIECIE,
    BUR_TEKST_ROZMIAR,
    BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE,
    BUR_DANE_ROZMIAR,
    BUR_DANE_WIRTUALNY
};
}

// =============================================================================
// Silnik Arytmetyczny 64-bit
// =============================================================================

enum StanKalkulatora : uint8_t {
    STAN_PIERWSZA_LICZBA = 0,
    STAN_OPERATOR = 1,
    STAN_DRUGA_LICZBA = 2,
    STAN_WYNIK = 3,
    STAN_BLAD = 4
};

static constexpr int64_t I64_MIN_WARTOSC = (-9223372036854775807LL - 1LL);
static constexpr int64_t I64_MAX_WARTOSC = 9223372036854775807LL;
static constexpr uint64_t I64_MIN_MODUL = 0x8000000000000000ULL;
static constexpr uint64_t I64_MAX_MODUL = 0x7FFFFFFFFFFFFFFFULL;

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
        const uint64_t cyfra = static_cast<uint64_t>(str[i] - '0');
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
            *wynik = -static_cast<int64_t>(wartosc);
        }
    } else {
        *wynik = static_cast<int64_t>(wartosc);
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
        modul = static_cast<uint64_t>(-(val + 1));
        modul += 1ULL;
    } else {
        modul = static_cast<uint64_t>(val);
    }
    char odwrotnie[32];
    size_t n = 0;
    while (modul > 0 && n < sizeof(odwrotnie)) {
        odwrotnie[n++] = static_cast<char>('0' + (modul % 10ULL));
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

// =============================================================================
// Komponenty GUI Kalkulatora
// =============================================================================

class WyswietlaczKalkulatora : public Kontrolka {
public:
    WyswietlaczKalkulatora() {
        ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        ustaw_wyrownanie_pionowe(WyrownaniePionowe::Poczatek);
        ustaw_preferowany_rozmiar(Rozmiar(-1, 52));
    }

    void ustaw_tekst_wyswietlacza(const char* t) {
        ::ustaw_tekst(tekst_, sizeof(tekst_), t);
        uniewaznij();
    }

    const char* pobierz_tekst_wyswietlacza() const {
        return tekst_;
    }

    void zaktualizuj_motyw() override {
        uniewaznij();
        Kontrolka::zaktualizuj_motyw();
    }

protected:
    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        uint32_t c_tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        uint32_t c_ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        uint32_t c_tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
        int promien = motyw_promien(MOTYW_ROG_POLA);

        ctx.rysuj_zaokraglony_prostokat(r, c_tlo, promien);
        ctx.rysuj_zaokraglona_ramke(r, c_ramka, promien, 1);

        SkalaUI skala_czcionki(2);
        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala_czcionki);
        int pad = 12 * s;
        int dostepna = r.szerokosc - 2 * pad;
        if (txt_w > dostepna && dostepna > 0) {
            skala_czcionki = SkalaUI(1);
            txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala_czcionki);
        }
        int tx = r.x + r.szerokosc - pad - txt_w;
        if (tx < r.x + pad) tx = r.x + pad;
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala_czcionki);
        int ty = r.y + (r.wysokosc - txt_h) / 2;

        ctx.rysuj_tekst(Punkt(tx, ty), tekst_, c_tekst, skala_czcionki);
    }

private:
    char tekst_[64]{"0"};
};

enum class RolaPrzyciskuKalkulatora : uint8_t {
    Cyfra,
    Operator,
    RownaSie,
    Wyczysc
};

class PrzyciskKalkulatora : public Przycisk {
public:
    explicit PrzyciskKalkulatora(const char* label, RolaPrzyciskuKalkulatora rola)
        : Przycisk(label), rola_(rola) {
        ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        zastosuj_role();
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_przycisku;
        zastosuj_role();
        Kontrolka::zaktualizuj_motyw();
    }

    void zastosuj_role() {
        switch (rola_) {
            case RolaPrzyciskuKalkulatora::Cyfra:
                break;
            case RolaPrzyciskuKalkulatora::Operator: {
                styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_PANEL);
                styl_.normalny.tekst = motyw_kolor(MOTYW_KOLOR_AKCENT);
                styl_.najechany.tlo = motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY);
                styl_.najechany.tekst = motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY);
                styl_.wcisniety.tlo = motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY);
                styl_.wcisniety.tekst = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);
                break;
            }
            case RolaPrzyciskuKalkulatora::RownaSie: {
                styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_AKCENT);
                styl_.normalny.tekst = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);
                styl_.najechany.tlo = motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY);
                styl_.najechany.tekst = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);
                styl_.wcisniety.tlo = motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY);
                styl_.wcisniety.tekst = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);
                break;
            }
            case RolaPrzyciskuKalkulatora::Wyczysc: {
                styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_BLAD);
                styl_.normalny.tekst = 0x00FFFFFF;
                styl_.najechany.tlo = 0x00E04040;
                styl_.najechany.tekst = 0x00FFFFFF;
                styl_.wcisniety.tlo = 0x00C02020;
                styl_.wcisniety.tekst = 0x00FFFFFF;
                break;
            }
        }
    }

private:
    RolaPrzyciskuKalkulatora rola_{RolaPrzyciskuKalkulatora::Cyfra};
};

class OknoKalkulatora;

struct KalkulatorAppState {
    AplikacjaGUI* app{nullptr};
    OknoKalkulatora* okno{nullptr};
    WyswietlaczKalkulatora* wyswietlacz{nullptr};

    char display[64]{"0"};
    char num_buf[32]{"0"};
    int64_t op1{0};
    char op{0};
    StanKalkulatora state{STAN_PIERWSZA_LICZBA};

    void ustaw_blad() {
        ustaw_tekst(display, sizeof(display), "BŁĄD");
        ustaw_tekst(num_buf, sizeof(num_buf), "0");
        op1 = 0;
        op = 0;
        state = STAN_BLAD;
        if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
    }

    void resetuj() {
        ustaw_tekst(display, sizeof(display), "0");
        ustaw_tekst(num_buf, sizeof(num_buf), "0");
        op1 = 0;
        op = 0;
        state = STAN_PIERWSZA_LICZBA;
        if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
    }

    bool oblicz_biezace(int64_t* wynik) {
        if (!wynik || state != STAN_DRUGA_LICZBA || op == 0) {
            return false;
        }
        int64_t op2 = 0;
        if (!calc_str_to_int64(num_buf, &op2)) return false;
        return wykonaj_operacje(op1, op2, op, wynik);
    }

    void klik(char btn) {
        if (btn >= '0' && btn <= '9') {
            if (state == STAN_WYNIK || state == STAN_BLAD) {
                char tmp[2] = {btn, '\0'};
                ustaw_tekst(num_buf, sizeof(num_buf), tmp);
                ustaw_tekst(display, sizeof(display), tmp);
                op = 0;
                state = STAN_PIERWSZA_LICZBA;
                if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
                return;
            }

            if (state == STAN_OPERATOR) {
                char tmp[2] = {btn, '\0'};
                ustaw_tekst(num_buf, sizeof(num_buf), tmp);
                if (!dopisz_znak(display, sizeof(display), btn)) {
                    ustaw_blad();
                    return;
                }
                state = STAN_DRUGA_LICZBA;
                if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
                return;
            }

            // Pierwsza lub druga liczba.
            if (num_buf[0] == '0' && num_buf[1] == '\0') {
                num_buf[0] = btn;
                num_buf[1] = '\0';
                if (state == STAN_PIERWSZA_LICZBA) {
                    display[0] = btn;
                    display[1] = '\0';
                } else if (state == STAN_DRUGA_LICZBA) {
                    const size_t len = dlugosc_tekstu(display, sizeof(display));
                    if (len > 0 && len < sizeof(display)) {
                        display[len - 1] = btn;
                    }
                }
                if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
                return;
            }

            if (!dopisz_znak(num_buf, sizeof(num_buf), btn)) {
                return;
            }
            if (!dopisz_znak(display, sizeof(display), btn)) {
                ustaw_blad();
                return;
            }
            if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
            return;
        }

        if (btn == 'C' || btn == 'c') {
            resetuj();
            return;
        }

        if (btn == '+' || btn == '-' || btn == '*' || btn == '/') {
            if (state == STAN_BLAD) {
                resetuj();
            }

            if (state == STAN_OPERATOR) {
                op = btn;
                const size_t len = dlugosc_tekstu(display, sizeof(display));
                if (len > 0 && len < sizeof(display)) {
                    display[len - 1] = btn;
                }
                if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
                return;
            }

            if (state == STAN_DRUGA_LICZBA) {
                int64_t wynik = 0;
                if (!oblicz_biezace(&wynik)) {
                    ustaw_blad();
                    return;
                }
                op1 = wynik;
                calc_int64_to_str(wynik, num_buf, sizeof(num_buf));
                calc_int64_to_str(wynik, display, sizeof(display));
                if (!dopisz_znak(display, sizeof(display), btn)) {
                    ustaw_blad();
                    return;
                }
                op = btn;
                state = STAN_OPERATOR;
                if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
                return;
            }

            int64_t wartosc = 0;
            if (!calc_str_to_int64(num_buf, &wartosc)) {
                ustaw_blad();
                return;
            }
            op1 = wartosc;
            op = btn;
            if (state == STAN_WYNIK) {
                calc_int64_to_str(op1, display, sizeof(display));
            }
            if (!dopisz_znak(display, sizeof(display), btn)) {
                ustaw_blad();
                return;
            }
            state = STAN_OPERATOR;
            if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
            return;
        }

        if (btn == '=' || btn == '\n' || btn == '\r') {
            if (state != STAN_DRUGA_LICZBA) return;
            int64_t wynik = 0;
            if (!oblicz_biezace(&wynik)) {
                ustaw_blad();
                return;
            }
            op1 = wynik;
            op = 0;
            calc_int64_to_str(wynik, num_buf, sizeof(num_buf));
            calc_int64_to_str(wynik, display, sizeof(display));
            state = STAN_WYNIK;
            if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
            return;
        }

        if (btn == '\b') {
            if (state == STAN_PIERWSZA_LICZBA || state == STAN_DRUGA_LICZBA) {
                size_t len_num = dlugosc_tekstu(num_buf, sizeof(num_buf));
                size_t len_disp = dlugosc_tekstu(display, sizeof(display));
                if (len_num > 1) {
                    num_buf[len_num - 1] = '\0';
                    if (len_disp > 0) display[len_disp - 1] = '\0';
                } else {
                    num_buf[0] = '0';
                    num_buf[1] = '\0';
                    if (len_disp > 0) display[len_disp - 1] = '0';
                }
                if (wyswietlacz) wyswietlacz->ustaw_tekst_wyswietlacza(display);
            }
            return;
        }
    }
};

static KalkulatorAppState g_kalkulator;

class OknoKalkulatora : public Okno {
public:
    explicit OknoKalkulatora(const char* tytul, int szer = 300, int wys = 380)
        : Okno(tytul, szer, wys) {
        // Stale proporcje przyciskow: okno mozna powiekszac, ale nie zmniejszac.
        ustaw_minimalny_rozmiar(szer, wys);
    }

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        if (ev.typ == BWS_ZDARZENIE_KLAWISZ || ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY) {
            bool fizyczny = (ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY);
            bool wcisniety = !fizyczny || ev.x != 0;
            if (wcisniety) {
                uint32_t kod = ev.kod;
                bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
                uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

                if (kod >= '0' && kod <= '9') {
                    g_kalkulator.klik(static_cast<char>(kod));
                    return;
                }
                if (kod == '+' || kod == '-' || kod == '*' || kod == '/' || kod == '=') {
                    g_kalkulator.klik(static_cast<char>(kod));
                    return;
                }
                if (kod == '\n' || kod == '\r' || (usb && baza == 40U) || (!usb && baza == 0x1CU)) {
                    g_kalkulator.klik('=');
                    return;
                }
                if (kod == 'c' || kod == 'C' || kod == 27U || (usb && baza == 41U) || (!usb && baza == 1U)) {
                    g_kalkulator.klik('C');
                    return;
                }
                if (kod == 8U || (usb && baza == 42U) || (!usb && baza == 0x0EU)) {
                    g_kalkulator.klik('\b');
                    return;
                }
            }
        }

        Okno::obsluz_zdarzenie(ev);
    }
};

// =============================================================================
// Punkt Wejścia
// =============================================================================

extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    g_kalkulator.app = &app;

    OknoKalkulatora okno("Kalkulator", 300, 380);
    g_kalkulator.okno = &okno;
    app.ustaw_glowne_okno(&okno);

    Panel* root = new Panel(Orientacja::Pionowa);
    root->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    root->ustaw_odstep(6);
    okno.ustaw_kontrolke_glowna(root);

    WyswietlaczKalkulatora& display = root->dodaj<WyswietlaczKalkulatora>();
    g_kalkulator.wyswietlacz = &display;

    const char* const btn_labels[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"}
    };

    for (int row = 0; row < 4; ++row) {
        Panel& row_panel = root->dodaj<Panel>(Orientacja::Pozioma);
        row_panel.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        row_panel.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        row_panel.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
        row_panel.ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        row_panel.ustaw_odstep(6);

        for (int col = 0; col < 4; ++col) {
            const char* label = btn_labels[row][col];
            char c = label[0];
            RolaPrzyciskuKalkulatora rola = RolaPrzyciskuKalkulatora::Cyfra;
            if (c == 'C') {
                rola = RolaPrzyciskuKalkulatora::Wyczysc;
            } else if (c == '=') {
                rola = RolaPrzyciskuKalkulatora::RownaSie;
            } else if (c == '/' || c == '*' || c == '-' || c == '+') {
                rola = RolaPrzyciskuKalkulatora::Operator;
            }

            PrzyciskKalkulatora& btn = row_panel.dodaj<PrzyciskKalkulatora>(label, rola);
            btn.przy_kliknieciu([](Przycisk*, void* d) {
                char ch = static_cast<char>(reinterpret_cast<uintptr_t>(d));
                g_kalkulator.klik(ch);
            }, reinterpret_cast<void*>(static_cast<uintptr_t>(c)));
        }
    }

    g_kalkulator.resetuj();
    app.uruchom(okno);
    bws_zakoncz_proces();
}
