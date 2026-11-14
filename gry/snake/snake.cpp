/*
 * Bursztyn OS - Snake (BursztynUI)
 *
 * Aplikacja Ring 3 oparta na frameworku BursztynUI: standardowe okno (Okno) z
 * dekoracjami, zmiana rozmiaru wszystkimi krawedziami i rogami, ruch, maksymalizacja,
 * motywy systemowe i DPI zapewnia framework. Logika gry jest w snake_logika.h
 * (bez GUI, testowana na hoscie); tu sa tylko: UI, czas i rysowanie planszy.
 *
 * Podzial odpowiedzialnosci:
 *   snake_logika.h   - stan gry, ruch, kolizje, jablko, pauza, uklad planszy w widoku
 *   AplikacjaSnake   - zegar gry (krok co KROK_NS niezaleznie od klatek), stan UI
 *   rysuj_plansze()  - rysowanie w PowierzchniaRysunkowa kolorami z motywu
 *   OknoSnake        - mapowanie klawiszy (WASD, strzalki, Spacja, R)
 */

#include "loader.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "bursztyn_gui.h"
#include "gry/snake/snake_logika.h"
#include <stddef.h>
#include <stdint.h>

using namespace BursztynUI;
using namespace SnakeLogika;

#ifndef SNAKE_HOST_TEST
extern "C" __attribute__((noreturn)) void _start();

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    reinterpret_cast<uint64_t>(&_start),
    0x1000ULL, 0x30000ULL, 0x1001000ULL,
    0x31000ULL, 0x20000ULL, 0x1031000ULL
};
}
#endif

namespace {

constexpr int OKNO_SZER = 560;
constexpr int OKNO_WYS = 560;
constexpr int MIN_SZER = 440;
constexpr int MIN_WYS = 380;
constexpr uint32_t OKRES_TIMERA_TIKI = 8;            // ~32 ms przy 250 Hz; krok gry liczony zegarem
constexpr int MAKS_KROKOW_NA_WYWOLANIE = 3;          // po dlugiej przerwie nie "doganiamy" gry

// ---- formatowanie liczb bez libc -------------------------------------------
size_t dopisz_tekst(char* d, size_t cap, size_t poz, const char* s) {
    while (*s && poz + 1 < cap) d[poz++] = *s++;
    d[poz] = '\0';
    return poz;
}
size_t dopisz_liczbe(char* d, size_t cap, size_t poz, int v) {
    char t[12]; int n = 0;
    unsigned u = v < 0 ? 0u : static_cast<unsigned>(v);
    do { t[n++] = static_cast<char>('0' + u % 10); u /= 10; } while (u && n < 11);
    while (n && poz + 1 < cap) d[poz++] = t[--n];
    d[poz] = '\0';
    return poz;
}

// Kolor -> jasnosc (0..255), do wyboru czytelnego koloru oczu glowy weza.
int jasnosc(uint32_t rgb) {
    return static_cast<int>((((rgb >> 16) & 255U) * 30U + ((rgb >> 8) & 255U) * 59U + (rgb & 255U) * 11U) / 100U);
}

struct AplikacjaSnake {
    AplikacjaGUI* app{nullptr};
    Okno* okno{nullptr};
    PowierzchniaRysunkowa* plansza{nullptr};
    Przycisk* btn_pauza{nullptr};
    Przycisk* btn_nowa{nullptr};
    Etykieta* lbl_wynik{nullptr};
    PasekStanu* pasek{nullptr};
    Gra gra{};
    uint64_t ostatni_krok_ns{0};
    uint64_t licznik_losowania{0x9E3779B97F4A7C15ULL};
    Stan pokazany_stan{Stan::Gotowa};
    int pokazany_wynik{-1};
    int pokazana_komorka{-1};
    bool timer_wlaczony{false};

    uint64_t ziarno() {
        // Ziarno tylko dla jablek; zegar monotoniczny + licznik, bez RDTSC.
        licznik_losowania = licznik_losowania * 6364136223846793005ULL + 1442695040888963407ULL;
        return licznik_losowania ^ gui_czas_monotoniczny_ns();
    }

    void synchronizuj_timer() {
        // Timer tylko dla aktywnej gry: pauza/koniec/gotowa => proces czeka na zdarzenia (0% CPU).
        const bool potrzebny = gra.stan == Stan::Gra;
        if (potrzebny == timer_wlaczony) return;
        timer_wlaczony = potrzebny;
        ostatni_krok_ns = 0;
        if (app) app->ustaw_zadanie_okresowe(potrzebny ? OKRES_TIMERA_TIKI : 0, &AplikacjaSnake::tykniecie, this);
    }

    static void tykniecie(void* ctx) { static_cast<AplikacjaSnake*>(ctx)->tyk(); }

    void tyk() {
        if (gra.stan != Stan::Gra) return;
        if (okno && okno->czy_zminimalizowane()) {          // minimalizacja wstrzymuje gre
            gra.przelacz_pauze();
            po_zmianie_stanu();
            return;
        }
        const uint64_t teraz = gui_czas_monotoniczny_ns();
        if (ostatni_krok_ns == 0 || teraz < ostatni_krok_ns) { ostatni_krok_ns = teraz; return; }
        int kroki = 0;
        while (teraz - ostatni_krok_ns >= KROK_NS && gra.stan == Stan::Gra) {
            ostatni_krok_ns += KROK_NS;
            gra.krok();
            if (++kroki >= MAKS_KROKOW_NA_WYWOLANIE) { ostatni_krok_ns = teraz; break; }
        }
        if (kroki) po_zmianie_stanu();
    }

    void nowa_gra() {
        gra.nowa_gra(ziarno(), true);
        po_zmianie_stanu();
    }

    void przelacz_pauze() {
        if (gra.zakonczona()) { nowa_gra(); return; }
        if (gra.przelacz_pauze()) po_zmianie_stanu();
    }

    void kierunek(Kierunek k) {
        if (gra.ustaw_kierunek(k)) po_zmianie_stanu();
    }

    const char* tekst_stanu() const {
        switch (gra.stan) {
            case Stan::Gotowa: return "Gotowy";
            case Stan::Gra: return "Gra trwa";
            case Stan::Pauza: return "Pauza";
            case Stan::KoniecGry: return "Koniec gry";
            case Stan::Wygrana: return "Wygrana!";
        }
        return "";
    }

    void po_zmianie_stanu() {
        synchronizuj_timer();
        odswiez_interfejs();
        if (plansza) plansza->uniewaznij();
    }

    void odswiez_interfejs() {
        char b[48];
        if (lbl_wynik && pokazany_wynik != gra.wynik()) {
            size_t p = dopisz_tekst(b, sizeof(b), 0, "Wynik: ");
            dopisz_liczbe(b, sizeof(b), p, gra.wynik());
            lbl_wynik->ustaw_tekst(b);
        }
        if (btn_pauza && pokazany_stan != gra.stan) {
            btn_pauza->ustaw_tekst(gra.stan == Stan::Pauza ? "Wznów" : (gra.zakonczona() ? "Jeszcze raz" : "Pauza"));
            btn_pauza->ustaw_wlaczona(true);
        }
        if (pasek) {
            if (pokazany_stan != gra.stan) pasek->ustaw_tekst(0, tekst_stanu());
            if (pokazany_wynik != gra.wynik()) {
                size_t p = dopisz_tekst(b, sizeof(b), 0, "Dł.: ");
                dopisz_liczbe(b, sizeof(b), p, gra.dlugosc);
                pasek->ustaw_tekst(1, b);
            }
        }
        pokazany_stan = gra.stan;
        pokazany_wynik = gra.wynik();
    }

    void ustaw_rozmiar_komorki(int k) {
        if (!pasek || k == pokazana_komorka) return;
        pokazana_komorka = k;
        char b[48];
        size_t p = dopisz_tekst(b, sizeof(b), 0, "Pole: ");
        p = dopisz_liczbe(b, sizeof(b), p, k);
        dopisz_tekst(b, sizeof(b), p, " px");
        pasek->ustaw_tekst(2, b);
    }
};

AplikacjaSnake g_snake;

uint32_t mieszaj(uint32_t a, uint32_t b, int procent_b) {
    uint32_t wynik = 0;
    for (int s = 0; s <= 16; s += 8) {
        const uint32_t ca = (a >> s) & 255U, cb = (b >> s) & 255U;
        wynik |= ((ca * static_cast<uint32_t>(100 - procent_b) + cb * static_cast<uint32_t>(procent_b)) / 100U) << s;
    }
    return wynik;
}

// Rysowanie planszy: tlo/ramka narysowane przez PowierzchniaRysunkowa z motywu; tu komorki.
void rysuj_plansze(PowierzchniaRysunkowa* pow, KontekstRysowania& ctx, void*) {
    AplikacjaSnake& s = g_snake;
    const UkladPlanszy u = uklad_planszy(pow->szerokosc(), pow->wysokosc(), 6);
    s.ustaw_rozmiar_komorki(u.komorka);
    if (u.komorka <= 0) return;                         // okno za male: nic nie rysujemy
    const uint32_t tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
    const uint32_t tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
    const uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
    const uint32_t sciana = mieszaj(tlo, akcent, 55);
    const uint32_t siatka = mieszaj(tlo, tekst, 8);
    const uint32_t cialo = motyw_kolor(MOTYW_KOLOR_SUKCES);
    const uint32_t glowa = mieszaj(cialo, jasnosc(cialo) > 128 ? 0x00000000U : 0x00FFFFFFU, 25);
    const uint32_t jablko = motyw_kolor(MOTYW_KOLOR_BLAD);
    const int k = u.komorka;
    const int ox = u.x, oy = u.y;

    // Plansza: sciany (obwod), wnetrze z delikatna siatka.
    ctx.rysuj_prostokat(Prostokat(ox, oy, u.szer, u.wys), sciana);
    ctx.rysuj_prostokat(Prostokat(ox + k, oy + k, u.szer - 2 * k, u.wys - 2 * k), tlo);
    for (int gx = 2; gx < PLANSZA_X - 1; ++gx) ctx.rysuj_linie_pionowa(ox + gx * k, oy + k, u.wys - 2 * k, siatka);
    for (int gy = 2; gy < PLANSZA_Y - 1; ++gy) ctx.rysuj_linie_pozioma(ox + k, oy + gy * k, u.szer - 2 * k, siatka);

    const int wc = k >= 8 ? 1 : 0;                      // odstep komorki od siatki
    if (s.gra.stan != Stan::Wygrana) {
        const int m = k >= 10 ? 3 : (k >= 6 ? 2 : 1);
        ctx.rysuj_zaokraglony_prostokat(Prostokat(ox + s.gra.jablko.x * k + m, oy + s.gra.jablko.y * k + m,
                                                  k - 2 * m, k - 2 * m), jablko, k >= 12 ? 3 : 1);
    }
    for (int i = s.gra.dlugosc - 1; i >= 0; --i) {
        const PunktPlanszy& p = s.gra.waz[i];
        ctx.rysuj_prostokat(Prostokat(ox + p.x * k + wc, oy + p.y * k + wc, k - 2 * wc, k - 2 * wc),
                            i == 0 ? glowa : cialo);
    }
    if (k >= 10) {                                      // oczy glowy w kolorze tekstu motywu
        const PunktPlanszy& h = s.gra.waz[0];
        const int e = k / 5 > 1 ? k / 5 : 2;
        const int hx = ox + h.x * k, hy = oy + h.y * k;
        const bool poziomo = s.gra.kx != 0;
        const int ax = poziomo ? (s.gra.kx > 0 ? k - k / 3 - e : k / 3) : k / 4;
        const int ay = poziomo ? k / 4 : (s.gra.ky > 0 ? k - k / 3 - e : k / 3);
        const int bx = poziomo ? ax : k - k / 4 - e;
        const int by = poziomo ? k - k / 4 - e : ay;
        ctx.rysuj_prostokat(Prostokat(hx + ax, hy + ay, e, e), tekst);
        ctx.rysuj_prostokat(Prostokat(hx + bx, hy + by, e, e), tekst);
    }

    // Nakladka stanu (Gotowa / Pauza / Koniec gry / Wygrana) - kolory motywu.
    const char* tytul = nullptr;
    const char* podpis = nullptr;
    switch (s.gra.stan) {
        case Stan::Pauza: tytul = "PAUZA"; podpis = "Spacja lub 'Wznów'"; break;
        case Stan::KoniecGry: tytul = "KONIEC GRY"; podpis = "R lub 'Nowa gra'"; break;
        case Stan::Wygrana: tytul = "WYGRANA!"; podpis = "R lub 'Nowa gra'"; break;
        case Stan::Gotowa: tytul = "SNAKE"; podpis = "Strzałki/WASD lub Spacja"; break;
        case Stan::Gra: break;
    }
    if (tytul) {
        // Rozmiar nakladki wynika z zmierzonego tekstu (czytelna takze przy malych komorkach).
        const int tw1 = ctx.zmierz_tekst(tytul, 1).szerokosc, tw2 = ctx.zmierz_tekst(podpis, 1).szerokosc;
        const int fh = ctx.pobierz_wysokosc_czcionki(1);
        int nw = (tw1 > tw2 ? tw1 : tw2) + 40, nh = fh * 2 + 36;
        if (nw > u.szer - 8) nw = u.szer - 8;
        if (nh > u.wys - 8) nh = u.wys - 8;
        const int nx = ox + (u.szer - nw) / 2, ny = oy + (u.wys - nh) / 2;
        const uint32_t kol = s.gra.stan == Stan::KoniecGry ? jablko : (s.gra.stan == Stan::Wygrana ? cialo : akcent);
        ctx.rysuj_prostokat(Prostokat(nx, ny, nw, nh), motyw_kolor(MOTYW_KOLOR_PANEL));
        ctx.rysuj_ramke(Prostokat(nx, ny, nw, nh), kol, 2);
        const int polowa = nh / 2;
        ctx.rysuj_tekst_wysrodkowany(Prostokat(nx, ny + 2, nw, polowa - 2), tytul, tekst, 1);
        ctx.rysuj_tekst_wysrodkowany(Prostokat(nx, ny + polowa, nw, polowa - 2), podpis,
                                     motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), 1);
    }
}

class OknoSnake : public Okno {
public:
    OknoSnake(const char* tytul, int w, int h) : Okno(tytul, w, h) {
        ustaw_minimalny_rozmiar(MIN_SZER, MIN_WYS);
    }

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        AplikacjaSnake& s = g_snake;
        if (ev.typ == BWS_ZDARZENIE_KLAWISZ) {
            switch (ev.kod) {
                case 'w': case 'W': s.kierunek(Kierunek::Gora); return;
                case 's': case 'S': s.kierunek(Kierunek::Dol); return;
                case 'a': case 'A': s.kierunek(Kierunek::Lewo); return;
                case 'd': case 'D': s.kierunek(Kierunek::Prawo); return;
                case ' ': s.przelacz_pauze(); return;
                case 'p': case 'P': if (!s.gra.zakonczona()) s.przelacz_pauze(); return;
                case 'r': case 'R': s.nowa_gra(); return;
                default: break;
            }
        } else if (ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && ev.x != 0) {
            switch (klawisz_polecenia(ev)) {          // strzalki (kody 0x48/0x50/0x4b/0x4d)
                case 0x48: s.kierunek(Kierunek::Gora); return;
                case 0x50: s.kierunek(Kierunek::Dol); return;
                case 0x4b: s.kierunek(Kierunek::Lewo); return;
                case 0x4d: s.kierunek(Kierunek::Prawo); return;
                default: break;
            }
        }
        Okno::obsluz_zdarzenie(ev);
    }
};

} // namespace

#ifndef SNAKE_HOST_TEST
extern "C" {
void* __dso_handle = nullptr;
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}
#endif

// Budowa interfejsu (wspolna dla aplikacji i testow hosta).
static void snake_zbuduj_ui(AplikacjaGUI& app, OknoSnake& okno) {
    g_snake.app = &app;
    g_snake.okno = &okno;
    app.ustaw_glowne_okno(&okno);

    Panel* korzen = new Panel(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(8, 8, 8, 0));
    korzen->ustaw_odstep(6);
    okno.ustaw_kontrolke_glowna(korzen);

    Panel& gora = korzen->dodaj<Panel>(Orientacja::Pozioma);
    gora.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    gora.ustaw_preferowany_rozmiar(Rozmiar(-1, 32));
    gora.ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
    gora.ustaw_odstep(8);

    Etykieta& tytul = gora.dodaj<Etykieta>("Snake");
    tytul.ustaw_preferowany_rozmiar(Rozmiar(56, 28));
    tytul.ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);

    Etykieta& wynik = gora.dodaj<Etykieta>("Wynik: 0");
    g_snake.lbl_wynik = &wynik;
    wynik.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    wynik.ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);

    Przycisk& nowa = gora.dodaj<Przycisk>("Nowa gra");
    g_snake.btn_nowa = &nowa;
    nowa.ustaw_preferowany_rozmiar(Rozmiar(116, 28));
    nowa.ustaw_mozliwosc_fokusu(false);                 // klawisze gry zostaja przy oknie
    nowa.przy_kliknieciu([](Przycisk*, void*) { g_snake.nowa_gra(); });

    Przycisk& pauza = gora.dodaj<Przycisk>("Pauza");
    g_snake.btn_pauza = &pauza;
    pauza.ustaw_preferowany_rozmiar(Rozmiar(116, 28));
    pauza.ustaw_mozliwosc_fokusu(false);
    pauza.przy_kliknieciu([](Przycisk*, void*) { g_snake.przelacz_pauze(); });

    PowierzchniaRysunkowa* pole = new PowierzchniaRysunkowa();
    g_snake.plansza = pole;
    pole->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    pole->ustaw_mozliwosc_fokusu(false);
    pole->przy_rysowaniu(&rysuj_plansze);
    korzen->dodaj_kontrolke(pole);

    PasekStanu& sb = korzen->dodaj<PasekStanu>();
    g_snake.pasek = &sb;
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    sb.dodaj_sekcje("Gotowy", 120, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("Dł.: 3", 70, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Pole: 0 px", 100, RozmiarSekcjiStanu::Stala);

    g_snake.gra.nowa_gra(g_snake.ziarno(), false);
    g_snake.pokazany_wynik = -1;
    g_snake.pokazany_stan = Stan::Pauza;                // wymusza pierwsze odswiezenie napisow
    g_snake.odswiez_interfejs();
}

#ifndef SNAKE_HOST_TEST
extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    OknoSnake okno("Snake", OKNO_SZER, OKNO_WYS);
    snake_zbuduj_ui(app, okno);
    app.uruchom(okno);
    bws_zakoncz_proces();
}
#endif
