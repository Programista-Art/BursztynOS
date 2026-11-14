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
#include "gry/snake/snake_ustawienia.h"
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
constexpr const char* APP_ID_USTAWIEN = "snake";
constexpr const char* KLUCZ_USTAWIEN = "stan";

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

const char* nazwa_predkosci(Predkosc p) {
    return p == Predkosc::Wolno ? "Wolno" : (p == Predkosc::Szybko ? "Szybko" : "Normalnie");
}

struct AplikacjaSnake {
    AplikacjaGUI* app{nullptr};
    Okno* okno{nullptr};
    PowierzchniaRysunkowa* plansza{nullptr};
    Przycisk* btn_pauza{nullptr};
    Przycisk* btn_nowa{nullptr};
    Etykieta* lbl_wynik{nullptr};
    Etykieta* lbl_rekord{nullptr};
    PasekStanu* pasek{nullptr};
    Menu* menu_ustawien{nullptr};
    Gra gra{};
    UstawieniaSnake ust{};
    uint64_t ostatni_krok_ns{0};
    uint64_t licznik_losowania{0x9E3779B97F4A7C15ULL};
    Stan pokazany_stan{Stan::Gotowa};
    int pokazany_wynik{-1};
    uint32_t pokazany_rekord{0xFFFFFFFFu};
    int pokazana_komorka{-1};
    bool timer_wlaczony{false};
    bool rekord_brudny{false};        // rekord/ustawienia zmienione od ostatniego udanego zapisu
    bool nowy_rekord{false};          // w biezacej grze pobito rekord
    bool zapis_nieudany{false};       // ostatnia proba zapisu nie powiodla sie (system plikow odmowil)

    uint64_t ziarno() {
        // Ziarno tylko dla jablek; zegar monotoniczny + licznik, bez RDTSC.
        licznik_losowania = licznik_losowania * 6364136223846793005ULL + 1442695040888963407ULL;
        return licznik_losowania ^ gui_czas_monotoniczny_ns();
    }

    // ---- trwale ustawienia i rekord ----------------------------------------------
    void wczytaj() {
        char buf[USTAWIENIA_MAKS_BAJTOW + 8] = {};
        UstawieniaSnake u;
        if (ustawienia_wczytaj(APP_ID_USTAWIEN, KLUCZ_USTAWIEN, buf, sizeof(buf))) {
            size_t n = 0;
            while (n < sizeof(buf) && buf[n]) ++n;
            u = wczytaj_ustawienia(buf, n);          // uszkodzone dane => wartosci domyslne
        }
        ust = u;
        ust.poprawne = u.poprawne;
        rekord_brudny = false;
        zapis_nieudany = false;
    }

    // Zapisuje tylko, gdy cos sie zmienilo (nigdy co klatke). Weryfikuje odczytem zwrotnym.
    bool zapisz() {
        if (!rekord_brudny) return true;
        char buf[USTAWIENIA_MAKS_BAJTOW + 8] = {};
        if (zapisz_ustawienia(ust, buf, sizeof(buf)) == 0) return false;
        bool ok = ustawienia_zapisz(APP_ID_USTAWIEN, KLUCZ_USTAWIEN, buf);
        if (ok) {
            char kontrola[USTAWIENIA_MAKS_BAJTOW + 8] = {};
            ok = ustawienia_wczytaj(APP_ID_USTAWIEN, KLUCZ_USTAWIEN, kontrola, sizeof(kontrola));
            for (size_t i = 0; ok && i < sizeof(kontrola); ++i) {
                if (kontrola[i] != buf[i]) ok = false;
                if (!buf[i]) break;
            }
        }
        zapis_nieudany = !ok;
        if (ok) rekord_brudny = false;
        odswiez_interfejs(true);
        return ok;
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
        const uint64_t krok = krok_ns(ust.predkosc);
        int kroki = 0;
        while (teraz - ostatni_krok_ns >= krok && gra.stan == Stan::Gra) {
            ostatni_krok_ns += krok;
            gra.krok();
            sprawdz_rekord();
            if (++kroki >= MAKS_KROKOW_NA_WYWOLANIE) { ostatni_krok_ns = teraz; break; }
        }
        if (kroki) po_zmianie_stanu();
    }

    // Rekord rosnie na biezaco; zapis dopiero na koncu gry/wyjsciu (bez zapisu co krok).
    void sprawdz_rekord() {
        if (gra.wynik() > 0 && static_cast<uint32_t>(gra.wynik()) > ust.rekord) {
            ust.rekord = static_cast<uint32_t>(gra.wynik());
            nowy_rekord = true;
            rekord_brudny = true;
        }
    }

    void nowa_gra(bool od_razu) {
        zapisz();
        nowy_rekord = false;
        gra.nowa_gra(ziarno(), od_razu);
        po_zmianie_stanu();
    }

    void przelacz_pauze() {
        if (gra.zakonczona()) { nowa_gra(true); return; }
        if (gra.przelacz_pauze()) po_zmianie_stanu();
    }

    void kierunek(Kierunek k) {
        if (gra.ustaw_kierunek(k)) po_zmianie_stanu();
    }

    void ustaw_predkosc(Predkosc p) {
        if (p == ust.predkosc) return;
        ust.predkosc = p;                 // bez restartu gry: tylko nowy odstep miedzy krokami
        ostatni_krok_ns = 0;
        rekord_brudny = true;
        zapisz();
        odswiez_interfejs(true);
        if (menu_ustawien) menu_ustawien->zaznacz_radio(1, 201U + static_cast<uint32_t>(p));
        if (plansza) plansza->uniewaznij();
    }

    void przelacz_siatke() {
        ust.siatka = !ust.siatka;
        rekord_brudny = true;
        zapisz();
        if (plansza) plansza->uniewaznij();
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
        if (gra.zakonczona()) zapisz();               // rekord zapisujemy raz, na koncu gry
        synchronizuj_timer();
        odswiez_interfejs(false);
        if (plansza) plansza->uniewaznij();
    }

    void odswiez_interfejs(bool wymus) {
        char b[48];
        if (lbl_wynik && (wymus || pokazany_wynik != gra.wynik())) {
            size_t p = dopisz_tekst(b, sizeof(b), 0, "Wynik: ");
            dopisz_liczbe(b, sizeof(b), p, gra.wynik());
            lbl_wynik->ustaw_tekst(b);
        }
        if (lbl_rekord && (wymus || pokazany_rekord != ust.rekord)) {
            size_t p = dopisz_tekst(b, sizeof(b), 0, "Rekord: ");
            dopisz_liczbe(b, sizeof(b), p, static_cast<int>(ust.rekord));
            lbl_rekord->ustaw_tekst(b);
        }
        if (btn_pauza && (wymus || pokazany_stan != gra.stan)) {
            btn_pauza->ustaw_tekst(gra.stan == Stan::Pauza ? "Wznów" : (gra.zakonczona() ? "Jeszcze raz" : "Pauza"));
        }
        if (pasek) {
            if (wymus || pokazany_stan != gra.stan || zapis_nieudany) {
                pasek->ustaw_tekst(0, zapis_nieudany ? "Nie zapisano rekordu" : tekst_stanu());
            }
            if (wymus) {
                size_t p = dopisz_tekst(b, sizeof(b), 0, nazwa_predkosci(ust.predkosc));
                p = dopisz_tekst(b, sizeof(b), p, " ");
                p = dopisz_liczbe(b, sizeof(b), p, static_cast<int>(krok_ns(ust.predkosc) / 1000000ULL));
                dopisz_tekst(b, sizeof(b), p, " ms");
                pasek->ustaw_tekst(1, b);
            }
        }
        pokazany_stan = gra.stan;
        pokazany_wynik = gra.wynik();
        pokazany_rekord = ust.rekord;
    }

    void ustaw_rozmiar_komorki(int k) { pokazana_komorka = k; }

    void pokaz_pomoc(const char* tytul, const char* tekst) {
        if (!okno) return;
        Komunikat::informacja(okno->zarzadca_popupow(), tytul, tekst, nullptr, nullptr, okno->kontrolka_glowna());
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

int jasnosc(uint32_t rgb) {
    return static_cast<int>((((rgb >> 16) & 255U) * 30U + ((rgb >> 8) & 255U) * 59U + (rgb & 255U) * 11U) / 100U);
}

// Rysowanie planszy. Widoczne jest pole 18x13 (logiczna siatka 20x15 ma sciany na obwodzie,
// bez rysowania), cienka obwodka 1 px (skalowana DPI) w kolorze ramki motywu.
void rysuj_plansze(PowierzchniaRysunkowa* pow, KontekstRysowania& ctx, void*) {
    AplikacjaSnake& s = g_snake;
    const int margines = ui_skaluj(6, ui_skala_procent);
    const UkladPlanszy u = uklad_planszy(pow->szerokosc(), pow->wysokosc(), margines);
    s.ustaw_rozmiar_komorki(u.komorka);
    // Delikatne oddzielenie: tlo okna wokol, tlo pola gry wewnatrz cienkiej ramki.
    ctx.rysuj_prostokat(Prostokat(0, 0, pow->szerokosc(), pow->wysokosc()), motyw_kolor(MOTYW_KOLOR_OKNOTLO));
    if (u.komorka <= 0) return;                         // okno za male: nic wiecej nie rysujemy
    const uint32_t tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
    const uint32_t tekst = motyw_kolor(MOTYW_KOLOR_TEKST);
    const uint32_t siatka = mieszaj(tlo, tekst, 8);
    const uint32_t cialo = motyw_kolor(MOTYW_KOLOR_SUKCES);
    const uint32_t glowa = mieszaj(cialo, jasnosc(cialo) > 128 ? 0x00000000U : 0x00FFFFFFU, 25);
    const uint32_t jablko = motyw_kolor(MOTYW_KOLOR_BLAD);
    const int k = u.komorka;
    const int ox = u.x, oy = u.y;
    const int g = ui_skaluj(1, ui_skala_procent);       // grubosc obwodki: ~1 px fizyczny
    ctx.rysuj_prostokat(Prostokat(ox, oy, u.szer, u.wys), tlo);
    if (s.ust.siatka) {
        for (int gx = 1; gx < WIDOCZNE_X; ++gx) ctx.rysuj_linie_pionowa(ox + gx * k, oy, u.wys, siatka);
        for (int gy = 1; gy < WIDOCZNE_Y; ++gy) ctx.rysuj_linie_pozioma(ox, oy + gy * k, u.szer, siatka);
    }
    ctx.rysuj_ramke(Prostokat(ox - g, oy - g, u.szer + 2 * g, u.wys + 2 * g), motyw_kolor(MOTYW_KOLOR_RAMKA), g);

    auto komorka = [&](const PunktPlanszy& p, int margines_k, uint32_t kolor, int promien) {
        const int px = p.x - 1, py = p.y - 1;                 // pole (1,1) logiki = lewy gorny widoczny
        if (px < 0 || py < 0 || px >= WIDOCZNE_X || py >= WIDOCZNE_Y) return;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(ox + px * k + margines_k, oy + py * k + margines_k,
                                                  k - 2 * margines_k, k - 2 * margines_k), kolor, promien);
    };
    if (s.gra.stan != Stan::Wygrana) {
        const int m = k >= 10 ? 3 : (k >= 6 ? 2 : 1);
        komorka(s.gra.jablko, m, jablko, k >= 12 ? 3 : 1);
    }
    const int wc = k >= 8 ? 1 : 0;
    for (int i = s.gra.dlugosc - 1; i >= 0; --i)
        komorka(s.gra.waz[i], wc, i == 0 ? glowa : cialo, k >= 14 ? 2 : 0);
    if (k >= 10) {                                      // oczy glowy w kolorze tekstu motywu
        const PunktPlanszy& h = s.gra.waz[0];
        const int e = k / 5 > 1 ? k / 5 : 2;
        const int hx = ox + (h.x - 1) * k, hy = oy + (h.y - 1) * k;
        const bool poziomo = s.gra.kx != 0;
        const int ax = poziomo ? (s.gra.kx > 0 ? k - k / 3 - e : k / 3) : k / 4;
        const int ay = poziomo ? k / 4 : (s.gra.ky > 0 ? k - k / 3 - e : k / 3);
        const int bx = poziomo ? ax : k - k / 4 - e;
        const int by = poziomo ? k - k / 4 - e : ay;
        ctx.rysuj_prostokat(Prostokat(hx + ax, hy + ay, e, e), tekst);
        ctx.rysuj_prostokat(Prostokat(hx + bx, hy + by, e, e), tekst);
    }

    // Kompaktowa nakladka stanu (Gotowa / Pauza / Koniec gry / Wygrana) - kolory motywu.
    const char* tytul = nullptr;
    const char* podpis = nullptr;
    char rekord_txt[40] = {};
    switch (s.gra.stan) {
        case Stan::Pauza: tytul = "Pauza"; podpis = "Spacja - wznów"; break;
        case Stan::KoniecGry: tytul = "Koniec gry"; podpis = "R - nowa gra"; break;
        case Stan::Wygrana: tytul = "Wygrana!"; podpis = "R - nowa gra"; break;
        case Stan::Gotowa: tytul = nullptr; podpis = "Strzałki lub WASD - start"; break;
        case Stan::Gra: break;
    }
    if (s.nowy_rekord && s.gra.zakonczona()) {
        size_t p = dopisz_tekst(rekord_txt, sizeof(rekord_txt), 0, s.zapis_nieudany ? "Nowy rekord: " : "Nowy rekord: ");
        p = dopisz_liczbe(rekord_txt, sizeof(rekord_txt), p, static_cast<int>(s.ust.rekord));
        if (s.zapis_nieudany) dopisz_tekst(rekord_txt, sizeof(rekord_txt), p, " (nie zapisano)");
    }
    if (podpis) {
        const char* wiersze[3] = {tytul, rekord_txt[0] ? rekord_txt : nullptr, podpis};
        int tw = 0, n = 0;
        for (const char* w : wiersze) if (w) { const int sz = ctx.zmierz_tekst(w, 1).szerokosc; if (sz > tw) tw = sz; ++n; }
        const int fh = ctx.pobierz_wysokosc_czcionki(1);
        int nw = tw + 28, nh = n * (fh + 4) + 14;
        if (nw > u.szer - 8) nw = u.szer - 8;
        if (nh > u.wys - 8) nh = u.wys - 8;
        const int nx = ox + (u.szer - nw) / 2, ny = oy + (u.wys - nh) / 2;
        const uint32_t kol = s.gra.stan == Stan::KoniecGry ? jablko : (s.gra.stan == Stan::Wygrana ? cialo : motyw_kolor(MOTYW_KOLOR_AKCENT));
        ctx.rysuj_prostokat(Prostokat(nx, ny, nw, nh), motyw_kolor(MOTYW_KOLOR_PANEL));
        ctx.rysuj_ramke(Prostokat(nx, ny, nw, nh), kol, 1);
        int y = ny + 7;
        for (int i = 0; i < 3; ++i) {
            if (!wiersze[i]) continue;
            const uint32_t kolor = i == 0 ? tekst : (i == 1 ? motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE) : motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
            ctx.rysuj_tekst_wysrodkowany(Prostokat(nx, y, nw, fh + 4), wiersze[i], kolor, 1);
            y += fh + 4;
        }
    }
}

const char* TEKST_STEROWANIE =
    "Strzałki lub W, A, S, D - kierunek ruchu węża.\n"
    "Spacja - pauza / wznowienie.\n"
    "R - rozpocznij od nowa.\n"
    "Esc - zamknij menu.\n"
    "Wąż skręca najwyżej raz na krok gry.";
const char* TEKST_ZASADY =
    "Zjadaj jabłka, aby urosnąć i zdobyć punkty.\n"
    "Uderzenie w ścianę lub we własne ciało kończy grę.\n"
    "Wygrywasz, gdy wąż wypełni całą planszę.\n"
    "Najlepszy wynik jest zapamiętywany.";
const char* TEKST_O_GRZE =
    "Snake dla Bursztyn OS\n"
    "Aplikacja BursztynUI (Ring 3).\n"
    "Plansza 20 x 15, trzy prędkości gry.";

class OknoSnake : public Okno {
public:
    OknoSnake(const char* tytul, int w, int h) : Okno(tytul, w, h) {
        ustaw_minimalny_rozmiar(MIN_SZER, MIN_WYS);
    }

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        AplikacjaSnake& s = g_snake;
        // Menu/dialog otwarty => klawisze naleza do niego (strzalki nie moga sterowac wezem).
        const bool modalne = zarzadca_popupow().czy_otwarty();
        if (!modalne && ev.typ == BWS_ZDARZENIE_KLAWISZ) {
            switch (ev.kod) {
                case 'w': case 'W': s.kierunek(Kierunek::Gora); return;
                case 's': case 'S': s.kierunek(Kierunek::Dol); return;
                case 'a': case 'A': s.kierunek(Kierunek::Lewo); return;
                case 'd': case 'D': s.kierunek(Kierunek::Prawo); return;
                case ' ': s.przelacz_pauze(); return;
                case 'p': case 'P': if (!s.gra.zakonczona()) s.przelacz_pauze(); return;
                case 'r': case 'R': s.nowa_gra(false); return;
                default: break;
            }
        } else if (!modalne && ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && ev.x != 0) {
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
    g_snake.wczytaj();

    Panel* korzen = new Panel(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(2, 2, 2, 0));
    korzen->ustaw_odstep(4);
    okno.ustaw_kontrolke_glowna(korzen);

    MenuGlowne& menu = korzen->dodaj<MenuGlowne>();
    Menu& m_gra = menu.menu().dodaj_podmenu("Gra", 100);
    m_gra.dodaj_element("Nowa gra", 101, [](ElementMenu&, void*) { g_snake.nowa_gra(true); });
    m_gra.dodaj_element("Pauza / Wznów (Spacja)", 102, [](ElementMenu&, void*) { g_snake.przelacz_pauze(); });
    m_gra.dodaj_element("Rozpocznij od nowa (R)", 103, [](ElementMenu&, void*) { g_snake.nowa_gra(false); });
    m_gra.dodaj_separator();
    m_gra.dodaj_element("Zakończ", 109, [](ElementMenu&, void*) { g_snake.zapisz(); if (g_snake.app) g_snake.app->zakoncz(); });

    Menu& m_ust = menu.menu().dodaj_podmenu("Ustawienia", 200);
    Menu& m_pred = m_ust.dodaj_podmenu("Prędkość gry", 210);
    g_snake.menu_ustawien = &m_pred;
    static const char* const nazwy[3] = {"Wolno (300 ms)", "Normalnie (220 ms)", "Szybko (150 ms)"};
    for (int i = 0; i < LICZBA_PREDKOSCI; ++i) {
        ElementMenu& e = m_pred.dodaj_element(nazwy[i], 201U + static_cast<uint32_t>(i), [](ElementMenu& el, void*) {
            g_snake.ustaw_predkosc(static_cast<Predkosc>(el.identyfikator() - 201U));
        });
        e.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Radio).ustaw_grupe_radialna(1);
    }
    m_pred.zaznacz_radio(1, 201U + static_cast<uint32_t>(g_snake.ust.predkosc));
    ElementMenu& e_siatka = m_ust.dodaj_element("Widoczność siatki", 220, [](ElementMenu& el, void*) {
        g_snake.przelacz_siatke();
        el.ustaw_zaznaczony(g_snake.ust.siatka);
    });
    e_siatka.ustaw_typ_zaznaczenia(TypZaznaczeniaMenu::Pole).ustaw_zaznaczony(g_snake.ust.siatka);

    Menu& m_pomoc = menu.menu().dodaj_podmenu("Pomoc", 300);
    m_pomoc.dodaj_element("Sterowanie", 301, [](ElementMenu&, void*) { g_snake.pokaz_pomoc("Sterowanie", TEKST_STEROWANIE); });
    m_pomoc.dodaj_element("Zasady gry", 302, [](ElementMenu&, void*) { g_snake.pokaz_pomoc("Zasady gry", TEKST_ZASADY); });
    m_pomoc.dodaj_element("O grze", 303, [](ElementMenu&, void*) { g_snake.pokaz_pomoc("O grze", TEKST_O_GRZE); });

    Panel& info = korzen->dodaj<Panel>(Orientacja::Pozioma);
    info.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    info.ustaw_preferowany_rozmiar(Rozmiar(-1, 30));
    info.ustaw_wypelnienie(Marginesy(6, 0, 6, 0));
    info.ustaw_odstep(8);

    Etykieta& wynik = info.dodaj<Etykieta>("Wynik: 0");
    g_snake.lbl_wynik = &wynik;
    wynik.ustaw_preferowany_rozmiar(Rozmiar(90, 26));
    wynik.ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);

    Etykieta& rekord = info.dodaj<Etykieta>("Rekord: 0");
    g_snake.lbl_rekord = &rekord;
    rekord.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    rekord.ustaw_wyrownanie_pionowe_tekstu(WyrownaniePionowe::Srodek);

    Przycisk& nowa = info.dodaj<Przycisk>("Nowa gra");
    g_snake.btn_nowa = &nowa;
    nowa.ustaw_preferowany_rozmiar(Rozmiar(92, 26));
    nowa.ustaw_mozliwosc_fokusu(false);                 // klawisze gry zostaja przy oknie
    nowa.przy_kliknieciu([](Przycisk*, void*) { g_snake.nowa_gra(true); });

    Przycisk& pauza = info.dodaj<Przycisk>("Pauza");
    g_snake.btn_pauza = &pauza;
    pauza.ustaw_preferowany_rozmiar(Rozmiar(100, 26));
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
    sb.dodaj_sekcje("Gotowy", 150, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("Normalnie 220 ms", 150, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Spacja: pauza, R: nowa", 190, RozmiarSekcjiStanu::Stala);

    g_snake.gra.nowa_gra(g_snake.ziarno(), false);
    g_snake.pokazany_wynik = -1;
    g_snake.odswiez_interfejs(true);
}

#ifndef SNAKE_HOST_TEST
extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    OknoSnake okno("Snake", OKNO_SZER, OKNO_WYS);
    snake_zbuduj_ui(app, okno);
    app.uruchom(okno);
    g_snake.zapisz();                                   // rekord/ustawienia przy zamknieciu
    bws_zakoncz_proces();
}
#endif
