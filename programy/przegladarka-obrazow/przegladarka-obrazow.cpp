/*
 * Bursztyn OS - Przeglądarka Obrazów (BursztynUI)
 *
 * Aplikacja Ring 3 na frameworku BursztynUI: Okno (dekoracje, zmiana rozmiaru
 * krawędziami i rogami, ruch, maksymalizacja, motywy, DPI), MenuGlowne, PasekNarzedzi,
 * PasekStanu oraz natywny dialog otwierania plików.
 *
 * Podział odpowiedzialności:
 *   silnik_obrazu.h        - parser BMP, cache wierszy/viewportu, renderery Fit i 1:1,
 *                            logika przesuwania widoku (bez UI, bez okna)
 *   WidokObrazu (tu)       - kontrolka: rozmiar widoku -> silnik, bufor -> ctx, panning myszą
 *   AplikacjaObrazow (tu)  - menu/pasek narzędzi/status, otwieranie plików, tryby
 *
 * Silnik dekoduje do bufora (aw x ah); kontrolka tylko kopiuje wiersze bufora do
 * kontekstu rysowania. Kolory obrazu nigdy nie zależą od motywu; z motywu pochodzi
 * wyłącznie tło wokół obrazu.
 */
#include "loader.h"
#include "bursztyn_ui/bursztyn_ui.h"
#include "bursztyn_gui.h"
#include "silnik_obrazu.h"

using namespace BursztynUI;

#ifndef PRZEGLADARKA_HOST_TEST
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

constexpr int OKNO_SZER = 740;
constexpr int OKNO_WYS = 500;
constexpr int MIN_SZER = 360;
constexpr int MIN_WYS = 260;
constexpr int MARGINES_OBRAZU = 2;
constexpr uint32_t OKRES_KROKU_TIKI = 4;          // ~16 ms przy 250 Hz: krok postępowego dekodowania 1:1
constexpr int KROK_PRZESUNIECIA = 64;

class WidokObrazu;

struct AplikacjaObrazow {
    AplikacjaGUI* app{nullptr};
    Okno* okno{nullptr};
    PowierzchniaRysunkowa* widok{nullptr};
    PasekStanu* pasek{nullptr};
    bool timer_wlaczony{false};
    bool przesuwanie{false};
    int przesuw_mx{0}, przesuw_my{0}, przesuw_vx{0}, przesuw_vy{0};
    bool bufory_ok{false};

    bool przygotuj_bufory() {
        int sw = 0, sh = 0;
        gui_pobierz_rozdzielczosc(&sw, &sh);
        uint64_t piksele = 0, bajty = 0;
        if (sw <= 0 || sh <= 0 || !mnoz_u64(static_cast<uint64_t>(sw), static_cast<uint64_t>(sh), &piksele) ||
            !mnoz_u64(piksele, sizeof(uint32_t), &bajty) || piksele > static_cast<uint64_t>(SIZE_MAX) ||
            bajty > static_cast<uint64_t>(ULONG_MAX)) return false;
        viewport_capacity = static_cast<size_t>(piksele);
        uint64_t sloty = (static_cast<uint64_t>(sh) + ROW_CACHE_ROWS - 1U) / ROW_CACHE_ROWS + 8ULL;
        if (sloty > MAX_ROW_CACHE_SLOTS) sloty = MAX_ROW_CACHE_SLOTS;
        if (sloty < 16) sloty = 16;
        row_cache_slot_count = static_cast<uint32_t>(sloty);
        kafel_buf = static_cast<uint32_t*>(profilowany_malloc(sizeof(uint32_t) * TILE_PIXELS));
        viewport_buf = static_cast<uint32_t*>(profilowany_malloc(static_cast<unsigned long>(bajty)));
        fit_buf = static_cast<uint32_t*>(profilowany_malloc(static_cast<unsigned long>(bajty)));
        bool ok = kafel_buf && viewport_buf && fit_buf;
        for (uint32_t i = 0; ok && i < row_cache_slot_count; ++i) {
            row_cache_slots[i].data = static_cast<uint8_t*>(profilowany_malloc(static_cast<unsigned long>(ROW_CACHE_BYTES)));
            if (!row_cache_slots[i].data) ok = false;
        }
        if (!ok) { zwolnij_bufory(); return false; }
        uniewaznij_cache_wierszy();
        bufory_ok = true;
        return true;
    }

    void zwolnij_bufory() {
        if (kafel_buf) { gui_free(kafel_buf); kafel_buf = nullptr; }
        if (viewport_buf) { gui_free(viewport_buf); viewport_buf = nullptr; }
        if (fit_buf) { gui_free(fit_buf); fit_buf = nullptr; }
        for (uint32_t i = 0; i < row_cache_slot_count; ++i)
            if (row_cache_slots[i].data) { gui_free(row_cache_slots[i].data); row_cache_slots[i].data = nullptr; }
        bufory_ok = false;
    }

    // ---- zegar postępowego dekodowania 1:1 ------------------------------------
    static void tykniecie(void* ctx) { static_cast<AplikacjaObrazow*>(ctx)->tyk(); }

    void synchronizuj_timer() {
        const bool potrzebny = render_1do1.active && tryb == TrybSkalowania::JEDEN_DO_JEDNEGO;
        if (potrzebny == timer_wlaczony || !app) return;
        timer_wlaczony = potrzebny;
        app->ustaw_zadanie_okresowe(potrzebny ? OKRES_KROKU_TIKI : 0, &AplikacjaObrazow::tykniecie, this);
    }

    void tyk() {
        if (render_1do1.active) (void)krok_renderowania_1do1();
        if (!render_1do1.active) {
            odswiez_status();
            if (widok) widok->uniewaznij();
        }
        synchronizuj_timer();
    }

    // ---- stan -> UI -----------------------------------------------------------
    void odswiez_status() {
        if (!pasek) return;
        char b[160] = {};
        if (!img_valid) {
            pasek->ustaw_tekst(0, blad_txt[0] ? blad_txt : "Brak obrazu");
            pasek->ustaw_tekst(1, "");
            pasek->ustaw_tekst(2, "");
            return;
        }
        const char* nazwa = aktualna_sciezka;
        for (const char* q = aktualna_sciezka; *q; ++q) if (*q == '/') nazwa = q + 1;
        pasek->ustaw_tekst(0, nazwa);
        b[0] = '\0';
        dopisz_u32(b, sizeof(b), img_w); dopisz_txt(b, sizeof(b), " x "); dopisz_u32(b, sizeof(b), img_h);
        dopisz_txt(b, sizeof(b), ", "); dopisz_u32(b, sizeof(b), img_bpp); dopisz_txt(b, sizeof(b), " bit");
        pasek->ustaw_tekst(1, b);
        b[0] = '\0';
        if (tryb == TrybSkalowania::JEDEN_DO_JEDNEGO) {
            dopisz_txt(b, sizeof(b), "1:1 (");
            dopisz_u32(b, sizeof(b), static_cast<uint32_t>(view_x)); dopisz_txt(b, sizeof(b), ", ");
            dopisz_u32(b, sizeof(b), static_cast<uint32_t>(view_y)); dopisz_txt(b, sizeof(b), ")");
        } else {
            dopisz_txt(b, sizeof(b), "Dopasuj do okna");
        }
        pasek->ustaw_tekst(2, b);
    }

    void ustaw_tytul() {
        if (!okno) return;
        char t[96] = {};
        kopiuj_txt(t, sizeof(t), "Przeglądarka Obrazów");
        if (img_valid) {
            const char* n = aktualna_sciezka;
            for (const char* q = aktualna_sciezka; *q; ++q) if (*q == '/') n = q + 1;
            dopisz_txt(t, sizeof(t), " - ");
            dopisz_txt(t, sizeof(t), n);
        }
        okno->ustaw_tytul(t);
        (void)gui_ustaw_tytul_okna(t);
    }

    void zmiana_widoku() {
        odswiez_status();
        if (widok) widok->uniewaznij();
        synchronizuj_timer();
    }

    // ---- polecenia ------------------------------------------------------------
    bool otworz(const char* sciezka) {
        const bool ok = wczytaj_obraz(sciezka);   // silnik: walidacja, limity, cache FIT
        ustaw_tytul();
        zmiana_widoku();
        return ok;
    }

    void zamknij_obraz() {
        zwolnij_bmp();
        blad_txt[0] = '\0';
        aktualna_sciezka[0] = '\0';
        view_x = view_y = 0;
        przesuwanie = false;
        ustaw_tytul();
        zmiana_widoku();
    }

    void dopasuj() { if (aktywuj_tryb(TrybSkalowania::DOPASUJ)) zmiana_widoku(); }
    void jeden_do_jednego() { if (aktywuj_tryb(TrybSkalowania::JEDEN_DO_JEDNEGO)) zmiana_widoku(); }
    // Silnik ma dwa poziomy skali (dopasowanie i 1:1): powiększenie = 1:1, pomniejszenie = dopasowanie.
    void powieksz() { if (tryb == TrybSkalowania::DOPASUJ) jeden_do_jednego(); }
    void pomniejsz() { if (tryb == TrybSkalowania::JEDEN_DO_JEDNEGO) dopasuj(); }

    void przesun(int dx, int dy) { if (przesun_widok(dx, dy)) zmiana_widoku(); }

    void pokaz_dialog_otwierania() {
        if (!okno) return;
        DialogPliku::otworz(okno->zarzadca_popupow(), "/uzytkownicy",
            "Obrazy BMP|*.bmp;;Wszystkie pliki|*.*",
            [](DialogPliku*, WynikDialoguPliku wynik, const char* sciezka, void*) {
                if (wynik == WynikDialoguPliku::Wybrano && sciezka && sciezka[0]) g_obrazy_app_otworz(sciezka);
            }, nullptr, okno->kontrolka_glowna(), "Otwórz obraz");
    }

    static void g_obrazy_app_otworz(const char* sciezka);
};

AplikacjaObrazow g_obrazy;

void AplikacjaObrazow::g_obrazy_app_otworz(const char* sciezka) { g_obrazy.otworz(sciezka); }

// Ujście silnika: gotowy fragment bufora => unieważnij kontrolkę (rysuje ją framework).
bool ujscie_do_widoku(int, int, int, int, const uint32_t*) {
    if (g_obrazy.widok) g_obrazy.widok->uniewaznij();
    return true;
}

void ustaw_obszar_widoku(int szer, int wys) {
    ProstokatObrazu r{0, 0, szer - 2 * MARGINES_OBRAZU, wys - 2 * MARGINES_OBRAZU};
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    g_obszar_obrazu = r;
}

// Rysowanie widoku: silnik odświeża bufor dla bieżącego rozmiaru, kontrolka kopiuje wiersze.
void rysuj_widok(PowierzchniaRysunkowa* pow, KontekstRysowania& ctx, void*) {
    AplikacjaObrazow& s = g_obrazy;
    ustaw_kolor_tla(motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE));
    ustaw_obszar_widoku(pow->szerokosc(), pow->wysokosc());
    const ProstokatObrazu a = pobierz_prostokat_obrazu();
    if (a.w <= 0 || a.h <= 0) return;
    const Prostokat ekran = pow->prostokat_do_ekranu(Prostokat(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h));

    auto komunikat = [&](const char* tekst, uint32_t kolor) {
        // Prostokaty i tekst: wspolrzedne lokalne kontrolki (ctx dodaje przesuniecie); wiersze obrazu: ekranowe.
        const Prostokat lokalny(MARGINES_OBRAZU, MARGINES_OBRAZU, a.w, a.h);
        ctx.rysuj_prostokat(lokalny, kolor_tla);
        ctx.rysuj_tekst_wysrodkowany(lokalny, tekst, kolor, 1);
    };

    if (!img_valid) {
        if (blad_txt[0]) komunikat(blad_txt, motyw_kolor(MOTYW_KOLOR_BLAD));
        else komunikat("Brak obrazu - Plik > Otwórz...", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
        return;
    }
    uint64_t piksele = 0;
    if (!s.bufory_ok || !mnoz_u64(static_cast<uint64_t>(a.w), static_cast<uint64_t>(a.h), &piksele) ||
        piksele > viewport_capacity) {
        komunikat("Okno zbyt duże dla bufora obrazu", motyw_kolor(MOTYW_KOLOR_BLAD));
        return;
    }
    const bool dopasuj = tryb == TrybSkalowania::DOPASUJ;
    const bool ok = dopasuj ? renderuj_dopasowany(a.w, a.h) : renderuj_1do1(a.w, a.h);
    if (!ok) { komunikat("Nie można zdekodować obrazu", motyw_kolor(MOTYW_KOLOR_BLAD)); return; }
    const uint32_t* bufor = dopasuj ? fit_buf : viewport_buf;
    for (int y = 0; y < a.h; ++y)
        ctx.rysuj_wiersz_ekranu(ekran.x, ekran.y + y, a.w, bufor + static_cast<size_t>(y) * static_cast<size_t>(a.w));
    s.synchronizuj_timer();
}

void mysz_wcisnieta(PowierzchniaRysunkowa*, const ZdarzenieMyszki& e, void*) {
    AplikacjaObrazow& s = g_obrazy;
    if (e.przycisk_myszy() != PrzyciskMyszy::Lewy || tryb != TrybSkalowania::JEDEN_DO_JEDNEGO || !img_valid) return;
    s.przesuwanie = true;
    s.przesuw_mx = e.x; s.przesuw_my = e.y; s.przesuw_vx = view_x; s.przesuw_vy = view_y;
}

void mysz_ruch(PowierzchniaRysunkowa*, const ZdarzenieMyszki& e, void*) {
    AplikacjaObrazow& s = g_obrazy;
    if (!s.przesuwanie) return;
    if ((e.modyfikatory & 1U) == 0U) { s.przesuwanie = false; return; }
    const int nx = s.przesuw_vx - (e.x - s.przesuw_mx);
    const int ny = s.przesuw_vy - (e.y - s.przesuw_my);
    s.przesun(nx - view_x, ny - view_y);        // przycięcie do granic obrazu robi silnik
}

void mysz_zwolniona(PowierzchniaRysunkowa*, const ZdarzenieMyszki&, void*) { g_obrazy.przesuwanie = false; }

class OknoObrazow : public Okno {
public:
    OknoObrazow(const char* tytul, int w, int h) : Okno(tytul, w, h) { ustaw_minimalny_rozmiar(MIN_SZER, MIN_WYS); }

    void obsluz_zdarzenie(const bws_zdarzenie& ev) override {
        AplikacjaObrazow& s = g_obrazy;
        if (ev.typ == BWS_ZDARZENIE_OTWORZ_PLIK) {
            char sciezka[512] = {};
            if (pobierz_argument_startowy(sciezka, sizeof(sciezka))) s.otworz(sciezka);
            return;
        }
        if (ev.typ == BWS_ZDARZENIE_MOTYW_ZMIENIONY) {
            Okno::obsluz_zdarzenie(ev);
            // Tło widoku zależy od motywu: unieważnij cache buforów (piksele obrazu bez zmian).
            uniewaznij_cache_rozmiaru();
            fit_cache = CacheDopasowania{};
            s.zmiana_widoku();
            return;
        }
        if (ev.typ == BWS_ZDARZENIE_KLAWISZ && !czy_zminimalizowane()) {
            switch (ev.kod) {
                case '1': s.jeden_do_jednego(); return;
                case 'D': case 'f': case 'F': s.dopasuj(); return;
                case '+': case '=': s.powieksz(); return;
                case '-': case '_': s.pomniejsz(); return;
                case 'w': case 'W': s.przesun(0, -KROK_PRZESUNIECIA); return;
                case 's': case 'S': s.przesun(0, KROK_PRZESUNIECIA); return;
                case 'a': case 'A': s.przesun(-KROK_PRZESUNIECIA, 0); return;
                case 'd': s.przesun(KROK_PRZESUNIECIA, 0); return;
                default: break;
            }
        } else if (ev.typ == BWS_ZDARZENIE_KLAWISZ_FIZYCZNY && ev.x != 0 && !czy_zminimalizowane() &&
                   !(ev.przyciski & (BWS_MOD_CTRL | BWS_MOD_ALT))) {
            const ProstokatObrazu a = pobierz_prostokat_obrazu();
            switch (klawisz_polecenia(ev)) {          // strzałki, Home/End, PgUp/PgDn
                case 0x48: s.przesun(0, -KROK_PRZESUNIECIA); return;
                case 0x50: s.przesun(0, KROK_PRZESUNIECIA); return;
                case 0x4b: s.przesun(-KROK_PRZESUNIECIA, 0); return;
                case 0x4d: s.przesun(KROK_PRZESUNIECIA, 0); return;
                case 0x49: s.przesun(0, -a.h / 2); return;
                case 0x51: s.przesun(0, a.h / 2); return;
                case 0x47: s.przesun(-view_x, -view_y); return;
                case 0x4f: s.przesun(1 << 20, 1 << 20); return;
                default: break;
            }
        }
        Okno::obsluz_zdarzenie(ev);
    }
};

} // namespace

#ifndef PRZEGLADARKA_HOST_TEST
extern "C" {
void* __dso_handle = nullptr;
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
}
#endif

static void obrazy_zbuduj_ui(AplikacjaGUI& app, OknoObrazow& okno) {
    g_obrazy.app = &app;
    g_obrazy.okno = &okno;
    app.ustaw_glowne_okno(&okno);
    g_ujscie_obrazu = &ujscie_do_widoku;

    Panel* korzen = new Panel(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(2, 2, 2, 0));
    korzen->ustaw_odstep(2);
    okno.ustaw_kontrolke_glowna(korzen);

    MenuGlowne& menu = korzen->dodaj<MenuGlowne>();
    Menu& m_plik = menu.menu().dodaj_podmenu("Plik", 100);
    m_plik.dodaj_element("Otwórz...", 101, [](ElementMenu&, void*) { g_obrazy.pokaz_dialog_otwierania(); });
    m_plik.dodaj_element("Zamknij obraz", 102, [](ElementMenu&, void*) { g_obrazy.zamknij_obraz(); });
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zakończ", 103, [](ElementMenu&, void*) { if (g_obrazy.app) g_obrazy.app->zakoncz(); });
    Menu& m_widok = menu.menu().dodaj_podmenu("Widok", 200);
    m_widok.dodaj_element("Dopasuj do okna", 201, [](ElementMenu&, void*) { g_obrazy.dopasuj(); });
    m_widok.dodaj_element("Rozmiar rzeczywisty 1:1", 202, [](ElementMenu&, void*) { g_obrazy.jeden_do_jednego(); });
    m_widok.dodaj_separator();
    m_widok.dodaj_element("Powiększ", 203, [](ElementMenu&, void*) { g_obrazy.powieksz(); });
    m_widok.dodaj_element("Pomniejsz", 204, [](ElementMenu&, void*) { g_obrazy.pomniejsz(); });

    PasekNarzedzi& pasek = korzen->dodaj<PasekNarzedzi>();
    pasek.dodaj_przycisk("Otwórz", 0, [](ElementPaskaNarzedzi*, void*) { g_obrazy.pokaz_dialog_otwierania(); });
    pasek.dodaj_separator();
    pasek.dodaj_przycisk("Dopasuj", 0, [](ElementPaskaNarzedzi*, void*) { g_obrazy.dopasuj(); });
    pasek.dodaj_przycisk("1:1", 0, [](ElementPaskaNarzedzi*, void*) { g_obrazy.jeden_do_jednego(); });
    pasek.dodaj_separator();
    pasek.dodaj_przycisk("Powiększ", 0, [](ElementPaskaNarzedzi*, void*) { g_obrazy.powieksz(); });
    pasek.dodaj_przycisk("Pomniejsz", 0, [](ElementPaskaNarzedzi*, void*) { g_obrazy.pomniejsz(); });

    PowierzchniaRysunkowa* widok = new PowierzchniaRysunkowa();
    g_obrazy.widok = widok;
    widok->ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    widok->ustaw_mozliwosc_fokusu(false);
    widok->przy_rysowaniu(&rysuj_widok);
    widok->przy_wcisnieciu(&mysz_wcisnieta);
    widok->przy_ruchu(&mysz_ruch);
    widok->przy_zwolnieniu(&mysz_zwolniona);
    korzen->dodaj_kontrolke(widok);

    PasekStanu& sb = korzen->dodaj<PasekStanu>();
    g_obrazy.pasek = &sb;
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    sb.dodaj_sekcje("Brak obrazu", 160, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("", 150, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("", 170, RozmiarSekcjiStanu::Stala);
    g_obrazy.odswiez_status();
}

#ifndef PRZEGLADARKA_HOST_TEST
extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    if (!g_obrazy.przygotuj_bufory()) bws_zakoncz_proces();
    OknoObrazow okno("Przeglądarka Obrazów", OKNO_SZER, OKNO_WYS);
    obrazy_zbuduj_ui(app, okno);

    char arg[512] = {};
    if (pobierz_argument_startowy(arg, sizeof(arg)) && arg[0]) g_obrazy.otworz(arg);

    app.uruchom(okno);
    zwolnij_bmp();
    g_obrazy.zwolnij_bufory();
    bws_zakoncz_proces();
}
#endif
