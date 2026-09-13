/* BursztynUI Kontrolki 3 - osiem kontrolek retained-mode. */
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR = 0x30000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE = 0x31000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR = 0x20000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY = 0x1031000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, (uint64_t)&_start,
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");

namespace {
struct Stan {
    PasekStanu* status{nullptr};
    Etykieta* stan_przelacznika{nullptr};
    Panel* podglad_koloru{nullptr};
};
Stan g;

void status(const char* t) { if (g.status) g.status->ustaw_tekst(0, t); }

void liczba(char* b, size_t cap, int n) {
    if (!b || cap < 2) return;
    char tmp[16]; size_t p = 0;
    do { tmp[p++] = static_cast<char>('0' + n % 10); n /= 10; } while (n > 0 && p < sizeof(tmp));
    size_t o = 0; while (p && o + 1 < cap) b[o++] = tmp[--p]; b[o] = '\0';
}

void zbuduj_dane(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa); p.ustaw_odstep(5);
    Etykieta& t = p.dodaj<Etykieta>("WidokDrzewiastoKolumnowy — hierarchia i kolumny");
    t.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    WidokDrzewiastoKolumnowy& w = p.dodaj<WidokDrzewiastoKolumnowy>();
    w.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    w.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    w.dodaj_kolumne("Nazwa", 280); w.dodaj_kolumne("Typ", 150);
    w.dodaj_kolumne("Rozmiar", 110, WyrownaniePoziome::Koniec); w.dodaj_kolumne("Stan", 130);
    auto programy = w.dodaj_korzen("programy", "Katalog", "—", "Dostępny");
    w.dodaj_dziecko(programy, "notatnik.cebula", "Aplikacja", "176 KiB", "Gotowy");
    w.dodaj_dziecko(programy, "husarz.cebula", "Aplikacja", "312 KiB", "Gotowy");
    w.dodaj_dziecko(programy, "test.cebula", "Aplikacja", "448 KiB", "Gotowy");
    auto gry = w.dodaj_korzen("gry", "Katalog", "—", "Dostępny");
    w.dodaj_dziecko(gry, "snake.cebula", "Gra", "96 KiB", "Gotowy");
    w.przy_zmianie_zaznaczenia([](WidokDrzewiastoKolumnowy* d, UchwytWezlaDrzewaKolumnowego u, void*) {
        if (d && u.czy_poprawny()) status("Wybrano węzeł drzewa kolumnowego");
    });

    Etykieta& i = p.dodaj<Etykieta>("InspektorWlasciwosci — Enter edytuje, Escape anuluje");
    i.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    InspektorWlasciwosci& ins = p.dodaj<InspektorWlasciwosci>();
    ins.ustaw_preferowany_rozmiar(Rozmiar(-1, 210));
    static const char* const tryby[] = {"Zwykły", "Kompaktowy", "Rozszerzony"};
    ins.dodaj_tekst(1, "Nazwa", "Przycisk główny", "Geometria");
    ins.dodaj_liczbe(2, "Szerokość", 240, "Geometria");
    ins.dodaj_liczbe(3, "Wysokość", 36, "Geometria", true);
    ins.dodaj_logiczna(4, "Widoczny", true, "Zachowanie");
    ins.dodaj_logiczna(5, "Aktywny", true, "Zachowanie");
    ins.dodaj_wybor(6, "Tryb", tryby, 3, 0, "Zachowanie");
    ins.dodaj_kolor(7, "Kolor tła", 0x00FFBF00, "Wygląd");
    ins.przy_zmianie([](InspektorWlasciwosci*, uint32_t, const InspektorWlasciwosci::Wlasciwosc&, void*) {
        status("Zmieniono właściwość obiektu");
    });
}

void zbuduj_wyszukiwanie(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa); p.ustaw_odstep(12);
    Etykieta& a = p.dodaj<Etykieta>("PoleWyszukiwania"); a.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    PoleWyszukiwania& s = p.dodaj<PoleWyszukiwania>("Szukaj w Bursztyn OS…");
    s.ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
    s.przy_wyszukaniu([](PoleWyszukiwania*, const char* tekst, void*) {
        status(tekst && tekst[0] ? "Wyszukiwanie uruchomione" : "Brak wyników");
    });
    Etykieta& b = p.dodaj<Etykieta>("PoleSugestii — wpisz „Bur”, „Pa” lub „U”"); b.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    PoleSugestii& ps = p.dodaj<PoleSugestii>("Wpisz polską sugestię…");
    ps.ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
    static const char* const sugestie[] = {
        "Bursztyn", "BursztynUI", "Bursztynowy System Plików",
        "Bursztynowe Wywołania Systemowe", "Pamięć", "Przeglądarka", "Ustawienia"
    };
    ps.ustaw_sugestie(sugestie, sizeof(sugestie) / sizeof(sugestie[0]));
    ps.przy_wybraniu([](PoleSugestii*, int, const char*, void*) { status("Wybrano sugestię"); });
    Etykieta& utf = p.dodaj<Etykieta>("UTF-8: ą ć ę ł ń ó ś ź ż   Ą Ć Ę Ł Ń Ó Ś Ź Ż");
    utf.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
}

void zbuduj_interakcje(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa); p.ustaw_odstep(12);
    Etykieta& a = p.dodaj<Etykieta>("PrzyciskPodzielony"); a.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    PrzyciskPodzielony& sp = p.dodaj<PrzyciskPodzielony>("Utwórz dokument");
    sp.ustaw_preferowany_rozmiar(Rozmiar(240, 38));
    sp.przy_kliknieciu([](PrzyciskPodzielony*, void*) { status("Wykonano domyślną akcję"); });
    sp.przy_otwarciu_menu([](PrzyciskPodzielony*, void*) { status("Otwarto menu przycisku"); });
    sp.menu().dodaj_element("Nowy dokument", 1, [](ElementMenu&, void*) { status("Nowy dokument"); });
    sp.menu().dodaj_element("Nowy katalog", 2, [](ElementMenu&, void*) { status("Nowy katalog"); });
    sp.menu().dodaj_element("Nowy projekt", 3, [](ElementMenu&, void*) { status("Nowy projekt"); });

    Etykieta& b = p.dodaj<Etykieta>("Przelacznik"); b.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    Panel& row = p.dodaj<Panel>(Orientacja::Pozioma); row.ustaw_preferowany_rozmiar(Rozmiar(-1, 42)); row.ustaw_odstep(12);
    Przelacznik& sw = row.dodaj<Przelacznik>(true);
    Etykieta& st = row.dodaj<Etykieta>("Włączony"); g.stan_przelacznika = &st;
    sw.przy_zmianie([](Przelacznik*, bool v, void*) {
        if (g.stan_przelacznika) g.stan_przelacznika->ustaw_tekst(v ? "Włączony" : "Wyłączony");
        status(v ? "Ustawienie włączone" : "Ustawienie wyłączone");
    });
}

void zbuduj_grafike(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa); p.ustaw_odstep(6);
    Etykieta& a = p.dodaj<Etykieta>("SelektorKoloru — HSV, RGB i zapis szesnastkowy"); a.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    SelektorKoloru& sk = p.dodaj<SelektorKoloru>(0x00FFBF00);
    sk.ustaw_preferowany_rozmiar(Rozmiar(260, 36));
    Panel& preview = p.dodaj<Panel>(); preview.ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
    preview.ustaw_kolor_tla(0x00FFBF00); g.podglad_koloru = &preview;
    sk.przy_zmianie([](SelektorKoloru*, uint32_t c, void*) {
        if (g.podglad_koloru) g.podglad_koloru->ustaw_kolor_tla(c == 0 ? 0x01000000U : c);
        status("Wybrano kolor");
    });
    Etykieta& b = p.dodaj<Etykieta>("WidokMiniatur — 1000 logicznych elementów"); b.ustaw_kolor_tekstu(Kolory::AkcentZloty);
    WidokMiniatur& wm = p.dodaj<WidokMiniatur>(); wm.ustaw_preferowany_rozmiar(Rozmiar(-1, -1)); wm.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    static uint32_t wzory[4][16 * 16];
    static const uint32_t barwy[4] = {0x00FFBF00, 0x002070D0, 0x0020A040, 0x00C030A0};
    for (int k = 0; k < 4; ++k) for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        wzory[k][y * 16 + x] = ((x / 4 + y / 4 + k) & 1) ? barwy[k] : 0x00302010;
    wm.rozpocznij_aktualizacje();
    for (int i = 0; i < 1000; ++i) { char n[32] = "Miniatura "; liczba(n + 10, sizeof(n) - 10, i + 1); wm.dodaj_element(static_cast<uint64_t>(i + 1), n, wzory[i & 3], 16, 16); }
    wm.zakoncz_aktualizacje();
    wm.przy_zmianie_zaznaczenia([](WidokMiniatur*, int, uint64_t, void*) { status("Wybrano miniaturę"); });
    wm.przy_aktywacji([](WidokMiniatur*, int, uint64_t, void*) { status("Aktywowano miniaturę"); });
}
}

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    Okno okno("Kontrolki 3", 820, 620);
    Panel* korzen = static_cast<Panel*>(okno.kontrolka_glowna());
    if (!korzen) gui_zakoncz_aplikacje();
    korzen->ustaw_orientacje(Orientacja::Pionowa); korzen->ustaw_wypelnienie(Marginesy(5));
    Karty& k = korzen->dodaj<Karty>(); k.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    k.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    Panel* dane = new Panel(); dane->ustaw_wypelnienie(Marginesy(8)); zbuduj_dane(*dane); k.dodaj_karte("Dane", dane);
    Panel* wysz = new Panel(); wysz->ustaw_wypelnienie(Marginesy(8)); zbuduj_wyszukiwanie(*wysz); k.dodaj_karte("Wyszukiwanie", wysz);
    Panel* inter = new Panel(); inter->ustaw_wypelnienie(Marginesy(8)); zbuduj_interakcje(*inter); k.dodaj_karte("Interakcja", inter);
    Panel* graf = new Panel(); graf->ustaw_wypelnienie(Marginesy(8)); zbuduj_grafike(*graf); k.dodaj_karte("Grafika", graf);
    PasekStanu& sb = korzen->dodaj<PasekStanu>(); g.status = &sb; sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.dodaj_sekcje("Gotowy — osiem interaktywnych kontrolek", 300, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("Kontrolki 3", 100, RozmiarSekcjiStanu::Stala);
    (void)app.uruchom(okno); bws_zakoncz_proces();
}
