/*
 * BursztynUI Kontrolki 4 - Pokaz i weryfikacja nowych komponentow frameworka:
 * Siatka, PanelDokowany, Przeplyw, OknoDialogowe, PasekInformacyjny,
 * WskaznikZajetosci, PowierzchniaRysunkowa, Wykres, ListaWirtualna,
 * TabelaWirtualna, Powiadomienie i MenedzerPowiadomien,
 * oraz infrastruktura PolecenieUI, MenuKontekstowe i motywy systemowe.
 */
#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

extern "C" void _start();
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0x40000ULL; // 256 KiB
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0x41000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR       = 0x20000ULL; // 128 KiB
static constexpr uint64_t BUR_DANE_WIRTUALNY     = 0x1041000ULL;

extern "C" {
__attribute__((section(".naglowek"), used)) NaglowekBur naglowek = {
    {'B','U','R','\0'}, (uint64_t)&_start,
    BUR_TEKST_PRZESUNIECIE, BUR_TEKST_ROZMIAR, BUR_TEKST_WIRTUALNY,
    BUR_DANE_PRZESUNIECIE, BUR_DANE_ROZMIAR, BUR_DANE_WIRTUALNY
};
}
static_assert(sizeof(NaglowekBur) == 60U, "Naglowek .bur musi miec 60 bajtow");

namespace {

// Pomocnicza funkcja formatowania liczb bez stdlib
void formatuj_liczbe(char* b, size_t cap, int64_t n) {
    if (!b || cap < 2) return;
    bool ujemna = n < 0;
    if (ujemna) n = -n;
    char tmp[32];
    size_t p = 0;
    do {
        tmp[p++] = static_cast<char>('0' + (n % 10));
        n /= 10;
    } while (n > 0 && p < sizeof(tmp));
    if (ujemna && p < sizeof(tmp)) tmp[p++] = '-';
    size_t o = 0;
    while (p > 0 && o + 1 < cap) b[o++] = tmp[--p];
    b[o] = '\0';
}

void kopiuj_napis(char* cel, size_t cap, const char* zr) {
    if (!cel || cap == 0) return;
    if (!zr) { cel[0] = '\0'; return; }
    size_t i = 0;
    while (zr[i] && i + 1 < cap) {
        cel[i] = zr[i];
        ++i;
    }
    cel[i] = '\0';
}

// Globalny stan aplikacji (POD — bez konstruktorów czasu ładowania)
struct StanAplikacji {
    Okno* okno{nullptr};
    PasekStanu* status{nullptr};
    MenedzerPowiadomien* powiadomienia{nullptr};

    // Komendy
    PolecenieUI* cmd_nowy{nullptr};
    PolecenieUI* cmd_otworz{nullptr};
    PolecenieUI* cmd_zapisz{nullptr};
    PolecenieUI* cmd_usun{nullptr};
    PolecenieUI* cmd_odswiez{nullptr};
    PolecenieUI* cmd_auto{nullptr};

    // Referencje kontrolek do dynamicznej aktualizacji
    Etykieta* etykieta_wyniku_dialogu{nullptr};
    WskaznikZajetosci* wskaznik_zajetosci{nullptr};
    PasekPostepu* postep_deterministyczny{nullptr};
    PasekPostepu* postep_nieokreslony{nullptr};
    Wykres* wykres_demo{nullptr};
    Etykieta* diagnoza_listy{nullptr};
    Etykieta* diagnoza_tabeli{nullptr};

    DostawcaDanychListy* dostawca_listy{nullptr};
    DostawcaDanychTabeli* dostawca_tabeli{nullptr};

    // Dane powierzchni rysunkowej
    static constexpr size_t MAKS_PUNKTOW_RYSUNKU = 32;
    Punkt punkty_rysunku[MAKS_PUNKTOW_RYSUNKU]{};
    size_t liczba_punktow_rysunku{0};
    Punkt kursor_myszy{0, 0};
    bool kursor_na_powierzchni{false};
};
StanAplikacji g;

void status(const char* t) {
    if (g.status) g.status->ustaw_tekst(0, t);
}

// -----------------------------------------------------------------------------
// DOSTAWCA DANYCH LISTY WIRTUALNEJ (100 000 ELEMENTÓW)
// -----------------------------------------------------------------------------
class DostawcaListyWirtualnej : public DostawcaDanychListy {
public:
    static constexpr size_t LICZBA_ELEMENTOW = 100000;

    size_t liczba_elementow() override {
        return LICZBA_ELEMENTOW;
    }

    const char* pobierz_tekst(size_t indeks, char* bufor, size_t rozmiar_bufora) override {
        if (!bufor || rozmiar_bufora < 4) return "";
        char num[24];
        formatuj_liczbe(num, sizeof(num), static_cast<int64_t>(indeks + 1));

        kopiuj_napis(bufor, rozmiar_bufora, "Element #");
        size_t len = 0;
        while (bufor[len]) ++len;

        size_t ni = 0;
        while (num[ni] && len + 1 < rozmiar_bufora) {
            bufor[len++] = num[ni++];
        }

        const char* sufiks = " | Typ: Zapis systemowy | Priorytet: Normalny";
        if (indeks % 10 == 0) sufiks = " | Typ: Krytyczny | Priorytet: Wysoki";
        else if (indeks % 5 == 0) sufiks = " | Typ: Usługa sieciowa | Priorytet: Normalny";

        size_t si = 0;
        while (sufiks[si] && len + 1 < rozmiar_bufora) {
            bufor[len++] = sufiks[si++];
        }
        bufor[len] = '\0';
        return bufor;
    }
};

// -----------------------------------------------------------------------------
// DOSTAWCA DANYCH TABELI WIRTUALNEJ (50 000 WIERSZY)
// -----------------------------------------------------------------------------
class DostawcaTabeliWirtualnej : public DostawcaDanychTabeli {
public:
    static constexpr size_t LICZBA_WIERSZY = 50000;
    static constexpr size_t LICZBA_KOLUMN = 4;

    size_t liczba_wierszy() override { return LICZBA_WIERSZY; }
    size_t liczba_kolumn() override { return LICZBA_KOLUMN; }

    const char* naglowek_kolumny(size_t c) override {
        switch (c) {
            case 0: return "ID";
            case 1: return "Usługa / Proces";
            case 2: return "Pamięć (KiB)";
            case 3: return "Stan wątku";
            default: return "";
        }
    }

    int szerokosc_kolumny(size_t c) override {
        switch (c) {
            case 0: return 70;
            case 1: return 180;
            case 2: return 120;
            case 3: return 130;
            default: return 80;
        }
    }

    const char* pobierz_komorke(size_t w, size_t c, char* buf, size_t cap) override {
        if (!buf || cap < 2) return "";
        size_t id = rosnaco_ ? (w + 1) : (LICZBA_WIERSZY - w);

        switch (c) {
            case 0:
                formatuj_liczbe(buf, cap, static_cast<int64_t>(id));
                break;
            case 1: {
                static const char* const PROCESY[] = {
                    "menedzer_okien", "eksplorator", "btp_serwer", "stos_sieci",
                    "sterownik_hda", "sterownik_e1000", "bursztyn_ui", "scheduler"
                };
                kopiuj_napis(buf, cap, PROCESY[id % (sizeof(PROCESY) / sizeof(PROCESY[0]))]);
                break;
            }
            case 2: {
                int64_t mem = 64 + (static_cast<int64_t>(id * 17) % 4096);
                formatuj_liczbe(buf, cap, mem);
                break;
            }
            case 3: {
                static const char* const STANY[] = {
                    "Działa", "Oczekiwanie", "Uśpiony", "Gotowy"
                };
                kopiuj_napis(buf, cap, STANY[id % 4]);
                break;
            }
            default:
                buf[0] = '\0';
                break;
        }
        return buf;
    }

    void zazadaj_sortowania(size_t kolumna, bool rosnaco) override {
        kolumna_sort_ = kolumna;
        rosnaco_ = rosnaco;
        status(rosnaco ? "Sortowanie tabeli: rosnąco" : "Sortowanie tabeli: malejąco");
    }

private:
    size_t kolumna_sort_{0};
    bool rosnaco_{true};
};

// -----------------------------------------------------------------------------
// KARTA 1: UKŁADY (Siatka, PanelDokowany, Przeplyw)
// -----------------------------------------------------------------------------
void zbuduj_karte_uklady(Panel& panel) {
    panel.ustaw_orientacje(Orientacja::Pionowa);
    panel.ustaw_odstep(10);
    panel.ustaw_wypelnienie(Marginesy(8, 8, 8, 8));

    // 1. Siatka
    Etykieta& t_siatka = panel.dodaj<Etykieta>("Siatka — wiersze i kolumny: Piksele, Auto, Gwiazdka (*), łączenie komórek");
    t_siatka.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    Siatka& siatka = panel.dodaj<Siatka>();
    siatka.ustaw_preferowany_rozmiar(Rozmiar(-1, 140));
    siatka.ustaw_odstep(6, 6);

    siatka.dodaj_kolumne(DefinicjaWymiaru::piksele(100)); // Kol 0: 100px
    siatka.dodaj_kolumne(DefinicjaWymiaru::automatyczny()); // Kol 1: Auto
    siatka.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(2));    // Kol 2: 2*

    siatka.dodaj_wiersz(DefinicjaWymiaru::automatyczny()); // Wiersz 0
    siatka.dodaj_wiersz(DefinicjaWymiaru::piksele(36));    // Wiersz 1
    siatka.dodaj_wiersz(DefinicjaWymiaru::gwiazdka(1));    // Wiersz 2

    // Komórki siatki
    Przycisk& k00 = siatka.dodaj_komorke<Przycisk>(0, 0, "100px");
    k00.przy_kliknieciu([](Przycisk*, void*) { status("Siatka: komórka (0,0) 100px"); });

    Przycisk& k01 = siatka.dodaj_komorke<Przycisk>(0, 1, "Auto szerokość");
    k01.przy_kliknieciu([](Przycisk*, void*) { status("Siatka: komórka (0,1) Auto"); });

    Przycisk& k02 = siatka.dodaj_komorke<Przycisk>(0, 2, "Gwiazdka (*2)");
    k02.przy_kliknieciu([](Przycisk*, void*) { status("Siatka: komórka (0,2) Gwiazdka 2*"); });

    // Wiersz 1 z łączeniem kolumn 0..1 (span = 2)
    Przycisk& k10 = siatka.dodaj_komorke<Przycisk>(1, 0, 1, 2, "Scalone 2 kolumny (span=2)");
    k10.przy_kliknieciu([](Przycisk*, void*) { status("Siatka: komórka scalona (1,0..1)"); });

    Przycisk& k12 = siatka.dodaj_komorke<Przycisk>(1, 2, "Komórka 1,2");
    k12.przy_kliknieciu([](Przycisk*, void*) { status("Siatka: komórka (1,2)"); });

    // Wiersz 2 scalający wszystkie 3 kolumny
    Przycisk& k20 = siatka.dodaj_komorke<Przycisk>(2, 0, 1, 3, "Wiersz elastyczny — scalony przez 3 kolumny");
    k20.przy_kliknieciu([](Przycisk*, void*) { status("Siatka: dolna komórka (2,0..2)"); });

    // 2. PanelDokowany
    Etykieta& t_dok = panel.dodaj<Etykieta>("PanelDokowany — układ stylizowany na środowisko IDE (Góra, Lewo, Prawo, Dół, Wypełnij)");
    t_dok.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    PanelDokowany& dok = panel.dodaj<PanelDokowany>();
    dok.ustaw_preferowany_rozmiar(Rozmiar(-1, 150));
    dok.ustaw_odstep(4);

    Przycisk& gora = dok.dodaj<Przycisk>(Dokowanie::Gora, "Pasek menu / akcji (Dokowanie::Gora)");
    gora.ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
    gora.przy_kliknieciu([](Przycisk*, void*) { status("PanelDokowany: Góra"); });

    Przycisk& lewo = dok.dodaj<Przycisk>(Dokowanie::Lewo, "Drzewo (Lewo)");
    lewo.ustaw_preferowany_rozmiar(Rozmiar(120, -1));
    lewo.przy_kliknieciu([](Przycisk*, void*) { status("PanelDokowany: Lewo"); });

    Przycisk& prawo = dok.dodaj<Przycisk>(Dokowanie::Prawo, "Właściwości (Prawo)");
    prawo.ustaw_preferowany_rozmiar(Rozmiar(150, -1));
    prawo.przy_kliknieciu([](Przycisk*, void*) { status("PanelDokowany: Prawo"); });

    Przycisk& dol = dok.dodaj<Przycisk>(Dokowanie::Dol, "Konsola wyjściowa (Dokowanie::Dół)");
    dol.ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
    dol.przy_kliknieciu([](Przycisk*, void*) { status("PanelDokowany: Dół"); });

    Przycisk& srodek = dok.dodaj<Przycisk>(Dokowanie::Wypelnij, "Centralny edytor (Dokowanie::Wypełnij)");
    srodek.przy_kliknieciu([](Przycisk*, void*) { status("PanelDokowany: Wypełnij"); });

    // 3. Przepływ
    Etykieta& t_przeplyw = panel.dodaj<Etykieta>("Przeplyw — automatyczne zawijanie elementów wierszami");
    t_przeplyw.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    Przeplyw& flow = panel.dodaj<Przeplyw>();
    flow.ustaw_preferowany_rozmiar(Rozmiar(-1, 80));
    flow.ustaw_odstep(6, 6);

    static const char* const TAGI[] = {
        "BursztynOS", "BursztynUI", "Ring 3", "Bare-Metal",
        "Wielozadaniowość", "C++17", "Retained-Mode", "Motywy"
    };
    for (size_t i = 0; i < sizeof(TAGI) / sizeof(TAGI[0]); ++i) {
        Przycisk& b = flow.dodaj<Przycisk>(TAGI[i]);
        b.przy_kliknieciu([](Przycisk* btn, void*) {
            if (btn) status(btn->tekst());
        });
    }
}

// -----------------------------------------------------------------------------
// KARTA 2: POLECENIA I MENU (PolecenieUI, MenuGlowne, MenuKontekstowe)
// -----------------------------------------------------------------------------
void zbuduj_karte_polecenia(Panel& panel) {
    panel.ustaw_orientacje(Orientacja::Pionowa);
    panel.ustaw_odstep(12);
    panel.ustaw_wypelnienie(Marginesy(10, 10, 10, 10));

    Etykieta& t = panel.dodaj<Etykieta>("Wielopunktowe powiązanie PolecenieUI (Menu, Pasek narzędzi, Przyciski, Menu kontekstowe)");
    t.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    // Wiersz przełącznika aktywności polecenia Zapisz
    Panel& wiersz_sw = panel.dodaj<Panel>(Orientacja::Pozioma);
    wiersz_sw.ustaw_preferowany_rozmiar(Rozmiar(-1, 36));
    wiersz_sw.ustaw_odstep(10);
    Przelacznik& sw_zapisz = wiersz_sw.dodaj<Przelacznik>(g.cmd_zapisz ? g.cmd_zapisz->czy_wlaczona() : true);
    Etykieta& et_sw = wiersz_sw.dodaj<Etykieta>("Polecenie „Zapisz” jest włączone");
    sw_zapisz.przy_zmianie([](Przelacznik*, bool v, void* d) {
        Etykieta* e = static_cast<Etykieta*>(d);
        if (g.cmd_zapisz) g.cmd_zapisz->ustaw_wlaczona(v);
        if (e) e->ustaw_tekst(v ? "Polecenie „Zapisz” jest włączone" : "Polecenie „Zapisz” jest wyłączone");
        status(v ? "Włączono polecenie Zapisz" : "Wyłączono polecenie Zapisz");
    }, &et_sw);

    // Grupa przycisków powiązanych z poleceniami
    Panel& wiersz_btn = panel.dodaj<Panel>(Orientacja::Pozioma);
    wiersz_btn.ustaw_preferowany_rozmiar(Rozmiar(-1, 38));
    wiersz_btn.ustaw_odstep(10);

    Przycisk& btn_nowy = wiersz_btn.dodaj<Przycisk>();
    if (g.cmd_nowy) btn_nowy.ustaw_polecenie(g.cmd_nowy);

    Przycisk& btn_otworz = wiersz_btn.dodaj<Przycisk>();
    if (g.cmd_otworz) btn_otworz.ustaw_polecenie(g.cmd_otworz);

    Przycisk& btn_zapisz = wiersz_btn.dodaj<Przycisk>();
    if (g.cmd_zapisz) btn_zapisz.ustaw_polecenie(g.cmd_zapisz);

    PrzyciskPodzielony& split = wiersz_btn.dodaj<PrzyciskPodzielony>();
    if (g.cmd_zapisz) split.ustaw_polecenie(g.cmd_zapisz);
    split.menu().dodaj_element("Zapisz jako...", 31, [](ElementMenu&, void*) { status("Zapisz jako..."); });
    split.menu().dodaj_element("Eksportuj...", 32, [](ElementMenu&, void*) { status("Eksportuj..."); });

    Przycisk& btn_usun = wiersz_btn.dodaj<Przycisk>();
    if (g.cmd_usun) btn_usun.ustaw_polecenie(g.cmd_usun);

    // Obszar demonstracji Menu Kontekstowego
    Etykieta& t_ctx = panel.dodaj<Etykieta>("Dziedziczenie i nadpisywanie Menu Kontekstowego (Prawy klik myszy lub Shift+F10):");
    t_ctx.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    Panel& rodzic_ctx = panel.dodaj<Panel>(Orientacja::Pionowa);
    rodzic_ctx.ustaw_preferowany_rozmiar(Rozmiar(-1, 160));
    rodzic_ctx.ustaw_wypelnienie(Marginesy(10, 10, 10, 10));
    rodzic_ctx.ustaw_odstep(8);
    rodzic_ctx.ustaw_kolor_tla(motyw_kolor(MOTYW_KOLOR_PODNIESIONY));

    // Menu kontekstowe rodzica
    MenuKontekstowe* menu_rodzica = new MenuKontekstowe();
    if (g.cmd_nowy) menu_rodzica->menu().dodaj_polecenie(*g.cmd_nowy);
    if (g.cmd_otworz) menu_rodzica->menu().dodaj_polecenie(*g.cmd_otworz);
    if (g.cmd_zapisz) menu_rodzica->menu().dodaj_polecenie(*g.cmd_zapisz);
    menu_rodzica->menu().dodaj_separator();
    menu_rodzica->menu().dodaj_element("Właściwości kontenera", 101, [](ElementMenu&, void*) {
        status("Wybrano: Właściwości kontenera");
    });
    rodzic_ctx.ustaw_menu_kontekstowe(menu_rodzica);

    Etykieta& et_rodzica = rodzic_ctx.dodaj<Etykieta>("Kontener nadrzędny — kliknij prawym przyciskiem, aby otworzyć menu nadrzędne.");
    (void)et_rodzica;

    // Kontrolka potomna dziedzicząca menu nadrzędne
    Przycisk& b_dziedzicz = rodzic_ctx.dodaj<Przycisk>("Przycisk bez własnego menu (dziedziczy menu rodzica)");
    b_dziedzicz.przy_kliknieciu([](Przycisk*, void*) { status("Kliknięto przycisk dziedziczący"); });

    // Kontrolka potomna z własnym menu kontekstowym (nadpisanie i dynamiczny hook)
    class PrzyciskZDynamicznymMenu : public Przycisk {
    public:
        explicit PrzyciskZDynamicznymMenu(const char* t) : Przycisk(t) {}
        void przygotuj_menu_kontekstowe(MenuKontekstowe& m) override {
            m.menu().wyczysc();
            if (g.cmd_odswiez) m.menu().dodaj_polecenie(*g.cmd_odswiez);
            if (g.cmd_usun) m.menu().dodaj_polecenie(*g.cmd_usun);
            m.menu().dodaj_element("Dynamiczna akcja potomka", 201, [](ElementMenu&, void*) {
                status("Dynamiczna akcja potomka wykonana!");
            });
            status("Przygotowano dynamiczne menu potomka");
        }
    };

    PrzyciskZDynamicznymMenu& b_nadpisz = rodzic_ctx.dodaj<PrzyciskZDynamicznymMenu>(
        "Przycisk z dynamicznym własnym menu (Shift+F10 lub prawy klik)");
    MenuKontekstowe* menu_dziecka = new MenuKontekstowe();
    b_nadpisz.ustaw_menu_kontekstowe(menu_dziecka);
}

void zglos_wynik_dialogu(OknoDialogowe*, WynikDialogu w, void*) {
    const char* res = "Brak";
    switch (w) {
        case WynikDialogu::OK: res = "OK"; break;
        case WynikDialogu::Anuluj: res = "Anuluj"; break;
        case WynikDialogu::Tak: res = "Tak"; break;
        case WynikDialogu::Nie: res = "Nie"; break;
        case WynikDialogu::Ponow: res = "Ponów"; break;
        case WynikDialogu::Przerwij: res = "Przerwij"; break;
        case WynikDialogu::Ignoruj: res = "Ignoruj"; break;
        case WynikDialogu::Wlasny: res = "Własny"; break;
        default: res = "Brak"; break;
    }
    char buf[128];
    kopiuj_napis(buf, sizeof(buf), "Wynik ostatniego dialogu: ");
    size_t len = 0;
    while (buf[len]) ++len;
    size_t oi = 0;
    while (res[oi] && len + 1 < sizeof(buf)) buf[len++] = res[oi++];
    buf[len] = '\0';
    if (g.etykieta_wyniku_dialogu) g.etykieta_wyniku_dialogu->ustaw_tekst(buf);
    status(res);
}

// -----------------------------------------------------------------------------
// KARTA 3: KOMUNIKATY I DIALOGI (OknoDialogowe, PasekInformacyjny, Toast)
// -----------------------------------------------------------------------------
void zbuduj_karte_komunikaty(Panel& panel) {
    panel.ustaw_orientacje(Orientacja::Pionowa);
    panel.ustaw_odstep(10);
    panel.ustaw_wypelnienie(Marginesy(8, 8, 8, 8));

    // Okna Dialogowe
    Etykieta& t_dlg = panel.dodaj<Etykieta>("OknoDialogowe — modalne okna z blokowaniem rodzica i skrótami Enter/Escape:");
    t_dlg.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    Panel& row_dlg1 = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_dlg1.ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
    row_dlg1.ustaw_odstep(8);

    Przycisk& b_dlg0 = row_dlg1.dodaj<Przycisk>("Tylko OK");
    b_dlg0.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno) return;
        OknoDialogowe* dlg = new OknoDialogowe("Informacja",
                                               "Operacja zakończona pomyślnie. Pamięć podręczna odświeżona.",
                                               PrzyciskiDialogu::TylkoOk, TypDialogu::Informacja);
        dlg->przy_wyniku(zglos_wynik_dialogu, nullptr);
        dlg->pokaz(g.okno->zarzadca_popupow(), g.okno->kontrolka_glowna());
    });

    Przycisk& b_dlg1 = row_dlg1.dodaj<Przycisk>("OK / Anuluj");
    b_dlg1.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno) return;
        OknoDialogowe* dlg = new OknoDialogowe("Zapis dokumentu",
                                               "Czy chcesz zapisać zmiany w bieżącym dokumencie przed wyjściem?",
                                               PrzyciskiDialogu::OkAnuluj, TypDialogu::Pytanie);
        dlg->przy_wyniku(zglos_wynik_dialogu, nullptr);
        dlg->pokaz(g.okno->zarzadca_popupow(), g.okno->kontrolka_glowna());
    });

    Przycisk& b_dlg2 = row_dlg1.dodaj<Przycisk>("Tak / Nie");
    b_dlg2.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno) return;
        OknoDialogowe* dlg = new OknoDialogowe("Potwierdzenie operacji",
                                               "Czy na pewno usunąć wybrany plik konfiguracyjny z dysku?",
                                               PrzyciskiDialogu::TakNie, TypDialogu::Ostrzezenie);
        dlg->przy_wyniku(zglos_wynik_dialogu, nullptr);
        dlg->pokaz(g.okno->zarzadca_popupow(), g.okno->kontrolka_glowna());
    });

    Przycisk& b_dlg3 = row_dlg1.dodaj<Przycisk>("Tak / Nie / Anuluj");
    b_dlg3.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno) return;
        OknoDialogowe* dlg = new OknoDialogowe("Modyfikacja profilu",
                                               "Wykryto niezapisane zmiany parametrów systemu.\nCzy chcesz zapisać modyfikacje profilu?",
                                               PrzyciskiDialogu::TakNieAnuluj, TypDialogu::Pytanie);
        dlg->przy_wyniku(zglos_wynik_dialogu, nullptr);
        dlg->pokaz(g.okno->zarzadca_popupow(), g.okno->kontrolka_glowna());
    });

    Panel& row_dlg2 = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_dlg2.ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
    row_dlg2.ustaw_odstep(8);

    Przycisk& b_dlg4 = row_dlg2.dodaj<Przycisk>("Ponów / Anuluj");
    b_dlg4.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno) return;
        OknoDialogowe* dlg = new OknoDialogowe("Błąd połączenia BTP",
                                               "Nie udało się połączyć z serwerem usług systemowych.\nUpewnij się, że usługa działa poprawnie.",
                                               PrzyciskiDialogu::PonowAnuluj, TypDialogu::Blad);
        dlg->przy_wyniku(zglos_wynik_dialogu, nullptr);
        dlg->pokaz(g.okno->zarzadca_popupow(), g.okno->kontrolka_glowna());
    });

    Przycisk& b_dlg5 = row_dlg2.dodaj<Przycisk>("Przerwij / Ponów / Ignoruj");
    b_dlg5.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno) return;
        OknoDialogowe* dlg = new OknoDialogowe("Krytyczny błąd nośnika",
                                               "Wystąpił błąd sumy kontrolnej w sektorze 0x8F4A.\nWybierz akcję naprawczą:",
                                               PrzyciskiDialogu::PrzerwijPonowIgnoruj, TypDialogu::Blad);
        dlg->przy_wyniku(zglos_wynik_dialogu, nullptr);
        dlg->pokaz(g.okno->zarzadca_popupow(), g.okno->kontrolka_glowna());
    });

    Przycisk& b_dlg6 = row_dlg2.dodaj<Przycisk>("Komunikat Sukces");
    b_dlg6.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno) return;
        Komunikat::sukces(g.okno->zarzadca_popupow(),
                          "Zakończono pomyślnie",
                          "Wszystkie komponenty BursztynUI zostały pomyślnie zweryfikowane.",
                          zglos_wynik_dialogu, nullptr, g.okno->kontrolka_glowna());
    });

    Panel& row_dlg3 = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_dlg3.ustaw_preferowany_rozmiar(Rozmiar(-1, 28));

    Etykieta& res_lbl = row_dlg3.dodaj<Etykieta>("Wynik ostatniego dialogu: (brak)");
    g.etykieta_wyniku_dialogu = &res_lbl;

    // Paski informacyjne w 4 stanach semantycznych
    Etykieta& t_inf = panel.dodaj<Etykieta>("PasekInformacyjny — stany semantyczne, akcje i zamykanie:");
    t_inf.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    PasekInformacyjny& pi_info = panel.dodaj<PasekInformacyjny>(
        TypKomunikatu::Informacja, "Informacja systemowa", "Dostępna jest nowa wersja pakietu Bursztyn OS.");
    pi_info.ustaw_akcje("Więcej", [](PasekInformacyjny*, void*) { status("Kliknięto akcję: Więcej"); });

    PasekInformacyjny& pi_sukces = panel.dodaj<PasekInformacyjny>(
        TypKomunikatu::Sukces, "Sukces operacji", "Wszystkie komponenty zostały poprawnie zainicjalizowane.");
    pi_sukces.ustaw_akcje("OK", [](PasekInformacyjny*, void*) { status("Potwierdzono sukces"); });

    PasekInformacyjny& pi_ostrz = panel.dodaj<PasekInformacyjny>(
        TypKomunikatu::Ostrzezenie, "Ostrzeżenie", "Zużycie bufora grafiki przekroczyło 80%.");
    (void)pi_ostrz;

    PasekInformacyjny& pi_blad = panel.dodaj<PasekInformacyjny>(
        TypKomunikatu::Blad, "Błąd wejścia/wyjścia", "Nie można otworzyć pliku konfiguracji.");
    pi_blad.przy_zamknieciu([](PasekInformacyjny*, void*) { status("Zamknięto pasek błędu"); });

    // Powiadomienia (Toast notifications)
    Etykieta& t_toast = panel.dodaj<Etykieta>("MenedzerPowiadomien — kafelki powiadomień w rogu ekranu:");
    t_toast.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    Panel& row_toast = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_toast.ustaw_preferowany_rozmiar(Rozmiar(-1, 36));
    row_toast.ustaw_odstep(8);

    Przycisk& b_t1 = row_toast.dodaj<Przycisk>("Toast Sukces");
    b_t1.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno || !g.powiadomienia) return;
        g.powiadomienia->pokaz(g.okno->zarzadca_popupow(), TypPowiadomienia::Sukces,
                               "Kompilacja udana", "Aplikacja kontrolki4.bur jest gotowa.", 4000);
        status("Wysłano powiadomienie o sukcesie");
    });

    Przycisk& b_t2 = row_toast.dodaj<Przycisk>("Toast Ostrzeżenie");
    b_t2.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno || !g.powiadomienia) return;
        g.powiadomienia->pokaz(g.okno->zarzadca_popupow(), TypPowiadomienia::Ostrzezenie,
                               "Niski stan pamięci", "Zalecane zamknięcie nieużywanych aplikacji.", 5000);
        status("Wysłano powiadomienie ostrzegawcze");
    });

    Przycisk& b_t3 = row_toast.dodaj<Przycisk>("Toast Błąd");
    b_t3.przy_kliknieciu([](Przycisk*, void*) {
        if (!g.okno || !g.powiadomienia) return;
        g.powiadomienia->pokaz(g.okno->zarzadca_popupow(), TypPowiadomienia::Blad,
                               "Błąd połączenia", "Serwer usług GUI nie odpowiada.", 5000,
                               "Ponów", [](Powiadomienie*, void*) { status("Ponawianie próby..."); });
        status("Wysłano powiadomienie o błędzie");
    });
}

// -----------------------------------------------------------------------------
// KARTA 4: RYSOWANIE I WYKRESY (PowierzchniaRysunkowa, Wykres)
// -----------------------------------------------------------------------------
void zbuduj_karte_grafika(Panel& panel) {
    panel.ustaw_orientacje(Orientacja::Pionowa);
    panel.ustaw_odstep(8);
    panel.ustaw_wypelnienie(Marginesy(8, 8, 8, 8));

    // Górny podział: Rysownica i Wykres obok siebie
    Panel& row_split = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_split.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    row_split.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    row_split.ustaw_odstep(10);

    // Kolumna lewa: PowierzchniaRysunkowa
    Panel& kol_rys = row_split.dodaj<Panel>(Orientacja::Pionowa);
    kol_rys.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    kol_rys.ustaw_preferowany_rozmiar(Rozmiar(360, -1));
    kol_rys.ustaw_odstep(4);

    Etykieta& t_rys = kol_rys.dodaj<Etykieta>("PowierzchniaRysunkowa — kliknij, by postawić punkt:");
    t_rys.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    PowierzchniaRysunkowa& rys = kol_rys.dodaj<PowierzchniaRysunkowa>();
    rys.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    rys.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    rys.przy_rysowaniu([](PowierzchniaRysunkowa* p, KontekstRysowania& ctx, void*) {
        if (!p) return;
        Prostokat b(0, 0, p->obszar().szerokosc, p->obszar().wysokosc);
        SkalaUI s = p->skala_dpi();

        // Siatka tła
        uint32_t c_siatka = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
        for (int x = 20 * s; x < b.szerokosc; x += 20 * s) {
            ctx.rysuj_linie_pionowa(x, 0, b.wysokosc, c_siatka);
        }
        for (int y = 20 * s; y < b.wysokosc; y += 20 * s) {
            ctx.rysuj_linie_pozioma(0, y, b.szerokosc, c_siatka);
        }

        // Postawione punkty
        for (size_t i = 0; i < g.liczba_punktow_rysunku; ++i) {
            Punkt pt = g.punkty_rysunku[i];
            ctx.rysuj_zaokraglony_prostokat(
                Prostokat(pt.x - 4 * s, pt.y - 4 * s, 9 * s, 9 * s),
                motyw_kolor(MOTYW_KOLOR_AKCENT), 4 * s);
        }

        // Połącz punkty liniami
        for (size_t i = 0; i + 1 < g.liczba_punktow_rysunku; ++i) {
            Punkt p1 = g.punkty_rysunku[i];
            Punkt p2 = g.punkty_rysunku[i + 1];
            // Rysuj odcinek łączący
            int dx = p2.x - p1.x; if (dx < 0) dx = -dx;
            int dy = p2.y - p1.y; if (dy < 0) dy = -dy;
            int sx = (p1.x < p2.x) ? 1 : -1;
            int sy = (p1.y < p2.y) ? 1 : -1;
            int err = dx - dy;
            int x = p1.x; int y = p1.y;
            while (true) {
                ctx.rysuj_prostokat(Prostokat(x, y, 2, 2), motyw_kolor(MOTYW_KOLOR_INFORMACJA));
                if (x == p2.x && y == p2.y) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx) { err += dx; y += sy; }
            }
        }

        // Celownik kursora
        if (g.kursor_na_powierzchni) {
            ctx.rysuj_linie_pozioma(g.kursor_myszy.x - 8 * s, g.kursor_myszy.y, 17 * s, motyw_kolor(MOTYW_KOLOR_SUKCES));
            ctx.rysuj_linie_pionowa(g.kursor_myszy.x, g.kursor_myszy.y - 8 * s, 17 * s, motyw_kolor(MOTYW_KOLOR_SUKCES));
        }
    });

    rys.przy_wcisnieciu([](PowierzchniaRysunkowa* p, const ZdarzenieMyszki& e, void*) {
        if (!p || !e.czy_lewy_przycisk()) return;
        int lx = e.x - p->obszar_ekranu().x;
        int ly = e.y - p->obszar_ekranu().y;
        if (g.liczba_punktow_rysunku < StanAplikacji::MAKS_PUNKTOW_RYSUNKU) {
            g.punkty_rysunku[g.liczba_punktow_rysunku++] = Punkt(lx, ly);
            p->uniewaznij();
            status("Dodano punkt na powierzchni rysunkowej");
        }
    });

    rys.przy_ruchu([](PowierzchniaRysunkowa* p, const ZdarzenieMyszki& e, void*) {
        if (!p) return;
        g.kursor_myszy = Punkt(e.x - p->obszar_ekranu().x, e.y - p->obszar_ekranu().y);
        g.kursor_na_powierzchni = true;
        p->uniewaznij();
    });

    Przycisk& b_clear = kol_rys.dodaj<Przycisk>("Wyczyść rysunek");
    b_clear.ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
    b_clear.przy_kliknieciu([](Przycisk*, void* d) {
        PowierzchniaRysunkowa* p = static_cast<PowierzchniaRysunkowa*>(d);
        g.liczba_punktow_rysunku = 0;
        if (p) p->uniewaznij();
        status("Wyczyszczono powierzchnię rysunkową");
    }, &rys);

    // Kolumna prawa: Wykres
    Panel& kol_wyk = row_split.dodaj<Panel>(Orientacja::Pionowa);
    kol_wyk.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    kol_wyk.ustaw_preferowany_rozmiar(Rozmiar(360, -1));
    kol_wyk.ustaw_odstep(4);

    Etykieta& t_wyk = kol_wyk.dodaj<Etykieta>("Wykres — renderowanie serii (Linia, Słupki, Obszar):");
    t_wyk.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    Wykres& wyk = kol_wyk.dodaj<Wykres>(TypWykresu::Linia);
    wyk.ustaw_tytul("Obciążenie systemu (%)");
    wyk.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    wyk.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    g.wykres_demo = &wyk;

    static const int DANE_CPU[] = { 15, 22, 45, 68, 85, 72, 60, 48, 35, 55, 78, 92, 80, 58, 42, 30 };
    static const int DANE_RAM[] = { 35, 36, 38, 40, 42, 45, 47, 49, 50, 52, 53, 55, 57, 58, 60, 62 };
    wyk.ustaw_punkty(0, DANE_CPU, sizeof(DANE_CPU) / sizeof(DANE_CPU[0]), "CPU", 0);
    wyk.ustaw_punkty(1, DANE_RAM, sizeof(DANE_RAM) / sizeof(DANE_RAM[0]), "RAM", 0);

    // Przyciski zmiany trybu wykresu
    Panel& row_tryby = kol_wyk.dodaj<Panel>(Orientacja::Pozioma);
    row_tryby.ustaw_preferowany_rozmiar(Rozmiar(-1, 30));
    row_tryby.ustaw_odstep(6);

    Przycisk& b_linia = row_tryby.dodaj<Przycisk>("Linia");
    b_linia.przy_kliknieciu([](Przycisk*, void*) {
        if (g.wykres_demo) g.wykres_demo->ustaw_typ_wykresu(TypWykresu::Linia);
        status("Wykres: tryb liniowy");
    });

    Przycisk& b_slupki = row_tryby.dodaj<Przycisk>("Słupki");
    b_slupki.przy_kliknieciu([](Przycisk*, void*) {
        if (g.wykres_demo) g.wykres_demo->ustaw_typ_wykresu(TypWykresu::Slupki);
        status("Wykres: tryb słupkowy");
    });

    Przycisk& b_obszar = row_tryby.dodaj<Przycisk>("Obszar");
    b_obszar.przy_kliknieciu([](Przycisk*, void*) {
        if (g.wykres_demo) g.wykres_demo->ustaw_typ_wykresu(TypWykresu::Obszar);
        status("Wykres: tryb warstwowy/obszarowy");
    });
}

// -----------------------------------------------------------------------------
// KARTA 5: DUŻE DANE (ListaWirtualna, TabelaWirtualna)
// -----------------------------------------------------------------------------
void zbuduj_karte_duze_dane(Panel& panel) {
    panel.ustaw_orientacje(Orientacja::Pionowa);
    panel.ustaw_odstep(6);
    panel.ustaw_wypelnienie(Marginesy(8, 8, 8, 8));

    // Wskaźniki diagnostyczne
    Panel& diag_row = panel.dodaj<Panel>(Orientacja::Pozioma);
    diag_row.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));
    diag_row.ustaw_odstep(12);

    Etykieta& diag_l = diag_row.dodaj<Etykieta>("Lista: 100 000 el. | Widoczne: [0..10] | Zapytań: 0");
    diag_l.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));
    g.diagnoza_listy = &diag_l;

    Etykieta& diag_t = diag_row.dodaj<Etykieta>("Tabela: 50 000 wierszy | Widoczne: [0..10] | Zapytań: 0");
    diag_t.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_SUKCES));
    g.diagnoza_tabeli = &diag_t;

    // Podział na listę i tabelę
    Panel& row_split = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_split.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    row_split.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    row_split.ustaw_odstep(10);

    // Lewo: Lista wirtualna
    Panel& kol_l = row_split.dodaj<Panel>(Orientacja::Pionowa);
    kol_l.ustaw_preferowany_rozmiar(Rozmiar(320, -1));
    kol_l.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    kol_l.dodaj<Etykieta>("ListaWirtualna (100 000 pozycji):");

    ListaWirtualna& lw = kol_l.dodaj<ListaWirtualna>();
    lw.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    lw.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    lw.ustaw_dostawce(g.dostawca_listy);

    lw.przy_zmianie_wyboru([](ListaWirtualna* l, size_t idx, void*) {
        if (!l) return;
        char num[24];
        formatuj_liczbe(num, sizeof(num), static_cast<int64_t>(idx + 1));
        char msg[64] = "Wybrano element listy #";
        size_t mlen = 0; while (msg[mlen]) ++mlen;
        size_t ni = 0; while (num[ni] && mlen + 1 < sizeof(msg)) msg[mlen++] = num[ni++];
        msg[mlen] = '\0';
        status(msg);

        // Aktualizacja etykiety diagnostycznej
        if (g.diagnoza_listy) {
            char diag[96] = "Lista: 100k | Wid: [";
            size_t dlen = 0; while (diag[dlen]) ++dlen;
            char b1[16]; formatuj_liczbe(b1, sizeof(b1), static_cast<int64_t>(l->pierwszy_widoczny_indeks()));
            char b2[16]; formatuj_liczbe(b2, sizeof(b2), static_cast<int64_t>(l->ostatni_widoczny_indeks()));
            char b3[20]; formatuj_liczbe(b3, sizeof(b3), static_cast<int64_t>(l->liczba_zapytan_dostawcy()));

            size_t i = 0; while (b1[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = b1[i++];
            if (dlen + 2 < sizeof(diag)) { diag[dlen++] = '.'; diag[dlen++] = '.'; }
            i = 0; while (b2[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = b2[i++];
            const char* mid = "] | Zapytań: ";
            i = 0; while (mid[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = mid[i++];
            i = 0; while (b3[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = b3[i++];
            diag[dlen] = '\0';
            g.diagnoza_listy->ustaw_tekst(diag);
        }
    });

    // Prawo: Tabela wirtualna
    Panel& kol_t = row_split.dodaj<Panel>(Orientacja::Pionowa);
    kol_t.ustaw_preferowany_rozmiar(Rozmiar(420, -1));
    kol_t.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    kol_t.dodaj<Etykieta>("TabelaWirtualna (50 000 wierszy, sortowanie kolumn):");

    TabelaWirtualna& tw = kol_t.dodaj<TabelaWirtualna>();
    tw.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    tw.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    tw.ustaw_dostawce(g.dostawca_tabeli);

    tw.przy_zmianie_wyboru([](TabelaWirtualna* t, size_t row, void*) {
        if (!t) return;
        char num[24];
        formatuj_liczbe(num, sizeof(num), static_cast<int64_t>(row + 1));
        char msg[64] = "Wybrano wiersz tabeli #";
        size_t mlen = 0; while (msg[mlen]) ++mlen;
        size_t ni = 0; while (num[ni] && mlen + 1 < sizeof(msg)) msg[mlen++] = num[ni++];
        msg[mlen] = '\0';
        status(msg);

        // Aktualizacja etykiety diagnostycznej tabeli
        if (g.diagnoza_tabeli) {
            char diag[96] = "Tabela: 50k | Wid: [";
            size_t dlen = 0; while (diag[dlen]) ++dlen;
            char b1[16]; formatuj_liczbe(b1, sizeof(b1), static_cast<int64_t>(t->pierwszy_widoczny_wiersz()));
            char b2[16]; formatuj_liczbe(b2, sizeof(b2), static_cast<int64_t>(t->ostatni_widoczny_wiersz()));
            char b3[20]; formatuj_liczbe(b3, sizeof(b3), static_cast<int64_t>(t->liczba_zapytan_dostawcy()));

            size_t i = 0; while (b1[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = b1[i++];
            if (dlen + 2 < sizeof(diag)) { diag[dlen++] = '.'; diag[dlen++] = '.'; }
            i = 0; while (b2[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = b2[i++];
            const char* mid = "] | Zapytań: ";
            i = 0; while (mid[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = mid[i++];
            i = 0; while (b3[i] && dlen + 1 < sizeof(diag)) diag[dlen++] = b3[i++];
            diag[dlen] = '\0';
            g.diagnoza_tabeli->ustaw_tekst(diag);
        }
    });
}

// -----------------------------------------------------------------------------
// KARTA 6: ZAJĘTOŚĆ I POSTĘP (WskaznikZajetosci, PasekPostepu)
// -----------------------------------------------------------------------------
void zbuduj_karte_postep(Panel& panel) {
    panel.ustaw_orientacje(Orientacja::Pionowa);
    panel.ustaw_odstep(14);
    panel.ustaw_wypelnienie(Marginesy(12, 12, 12, 12));

    // Wskaźnik zajętości
    Etykieta& t_busy = panel.dodaj<Etykieta>("WskaznikZajetosci — animowany obracający się spinner:");
    t_busy.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    Panel& row_busy = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_busy.ustaw_preferowany_rozmiar(Rozmiar(-1, 48));
    row_busy.ustaw_odstep(12);

    WskaznikZajetosci& wz = row_busy.dodaj<WskaznikZajetosci>(true);
    wz.ustaw_preferowany_rozmiar(Rozmiar(40, 40));
    g.wskaznik_zajetosci = &wz;

    Przycisk& b_start = row_busy.dodaj<Przycisk>("Start");
    b_start.przy_kliknieciu([](Przycisk*, void*) {
        if (g.wskaznik_zajetosci) g.wskaznik_zajetosci->start();
        status("WskaznikZajetosci: start");
    });

    Przycisk& b_stop = row_busy.dodaj<Przycisk>("Stop");
    b_stop.przy_kliknieciu([](Przycisk*, void*) {
        if (g.wskaznik_zajetosci) g.wskaznik_zajetosci->stop();
        status("WskaznikZajetosci: zatrzymany");
    });

    Przycisk& b_step = row_busy.dodaj<Przycisk>("Pojedynczy krok");
    b_step.przy_kliknieciu([](Przycisk*, void*) {
        if (g.wskaznik_zajetosci) g.wskaznik_zajetosci->krok_animacji(0);
        status("WskaznikZajetosci: wykonano krok");
    });

    // Paski postępu deterministyczny i nieokreślony
    Etykieta& t_bar = panel.dodaj<Etykieta>("PasekPostepu — tryb deterministyczny (procentowy) i nieokreślony (indet):");
    t_bar.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    PasekPostepu& p1 = panel.dodaj<PasekPostepu>(50);
    p1.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    p1.ustaw_pokazywanie_tekstu(true);
    g.postep_deterministyczny = &p1;

    Panel& row_p_ctrl = panel.dodaj<Panel>(Orientacja::Pozioma);
    row_p_ctrl.ustaw_preferowany_rozmiar(Rozmiar(-1, 32));
    row_p_ctrl.ustaw_odstep(10);

    Przycisk& b_minus = row_p_ctrl.dodaj<Przycisk>("- 10%");
    b_minus.przy_kliknieciu([](Przycisk*, void*) {
        if (g.postep_deterministyczny) {
            int w = g.postep_deterministyczny->wartosc() - 10;
            if (w < 0) w = 0;
            g.postep_deterministyczny->ustaw_wartosc(w);
            status("Zmniejszono postęp");
        }
    });

    Przycisk& b_plus = row_p_ctrl.dodaj<Przycisk>("+ 10%");
    b_plus.przy_kliknieciu([](Przycisk*, void*) {
        if (g.postep_deterministyczny) {
            int w = g.postep_deterministyczny->wartosc() + 10;
            if (w > 100) w = 100;
            g.postep_deterministyczny->ustaw_wartosc(w);
            status("Zwiększono postęp");
        }
    });

    PasekPostepu& p2 = panel.dodaj<PasekPostepu>();
    p2.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    p2.ustaw_tryb_nieokreslony(true);
    g.postep_nieokreslony = &p2;
}

// -----------------------------------------------------------------------------
// KARTA 7: MOTYWY (Wizualizacja palety i parametrów motywu)
// -----------------------------------------------------------------------------
void zbuduj_karte_motywy(Panel& panel) {
    panel.ustaw_orientacje(Orientacja::Pionowa);
    panel.ustaw_odstep(10);
    panel.ustaw_wypelnienie(Marginesy(10, 10, 10, 10));

    Etykieta& t = panel.dodaj<Etykieta>("Weryfikacja ról semantycznych aktywnego motywu systemowego:");
    t.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));

    // Siatka próbek kolorów motywu
    Siatka& siatka = panel.dodaj<Siatka>();
    siatka.ustaw_preferowany_rozmiar(Rozmiar(-1, 180));
    siatka.ustaw_odstep(8, 8);

    siatka.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));
    siatka.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));
    siatka.dodaj_kolumne(DefinicjaWymiaru::gwiazdka(1));

    siatka.dodaj_wiersz(DefinicjaWymiaru::piksele(50));
    siatka.dodaj_wiersz(DefinicjaWymiaru::piksele(50));
    siatka.dodaj_wiersz(DefinicjaWymiaru::piksele(50));

    struct Probka {
        const char* nazwa;
        MotywKolorRola rola;
        int r; int c;
    };
    static const Probka PROBKI[] = {
        {"Akcent", MOTYW_KOLOR_AKCENT, 0, 0},
        {"Panel", MOTYW_KOLOR_PANEL, 0, 1},
        {"Podniesiony", MOTYW_KOLOR_PODNIESIONY, 0, 2},
        {"Zaznaczenie", MOTYW_KOLOR_ZAZNACZENIE, 1, 0},
        {"Informacja", MOTYW_KOLOR_INFORMACJA, 1, 1},
        {"Sukces", MOTYW_KOLOR_SUKCES, 1, 2},
        {"Ostrzeżenie", MOTYW_KOLOR_OSTRZEZENIE, 2, 0},
        {"Błąd", MOTYW_KOLOR_BLAD, 2, 1},
        {"Tło okna", MOTYW_KOLOR_OKNOTLO, 2, 2}
    };

    for (size_t i = 0; i < sizeof(PROBKI) / sizeof(PROBKI[0]); ++i) {
        Panel& p = siatka.dodaj_komorke<Panel>(PROBKI[i].r, PROBKI[i].c);
        p.ustaw_kolor_tla(motyw_kolor(PROBKI[i].rola));
        p.ustaw_kolor_ramki(motyw_kolor(MOTYW_KOLOR_RAMKA));
        p.ustaw_grubosc_ramki(1);
        p.ustaw_wypelnienie(Marginesy(6, 6, 6, 6));

        Etykieta& lbl = p.dodaj<Etykieta>(PROBKI[i].nazwa);
        lbl.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
    }

    // Informacja o geometrii motywu
    Etykieta& t_geom = panel.dodaj<Etykieta>("Geometria motywu: zaokrąglenie okna, panelu i przycisku, grubość ramki, cienie okien.");
    t_geom.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));

    Etykieta& t_info = panel.dodaj<Etykieta>(
        "Kontrolki 4 automatycznie reagują na zdarzenie BWS_ZDARZENIE_MOTYW_ZMIENIONY bez przeładowywania procesu.");
    t_info.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));
}

} // namespace

// -----------------------------------------------------------------------------
// PUNKT WEJŚCIA APLIKACJI
// -----------------------------------------------------------------------------
extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;
    Okno okno("Kontrolki 4 — BursztynUI Showroom", 860, 640);
    g.okno = &okno;

    // Alokacja dynamiczna obiektów retained-mode (brak konstruktorów czasu ładowania)
    g.dostawca_listy = new DostawcaListyWirtualnej();
    g.dostawca_tabeli = new DostawcaTabeliWirtualnej();
    g.powiadomienia = new MenedzerPowiadomien();

    g.cmd_nowy = new PolecenieUI(1, "Nowy");
    g.cmd_otworz = new PolecenieUI(2, "Otwórz");
    g.cmd_zapisz = new PolecenieUI(3, "Zapisz");
    g.cmd_usun = new PolecenieUI(4, "Usuń");
    g.cmd_odswiez = new PolecenieUI(5, "Odśwież");
    g.cmd_auto = new PolecenieUI(6, "Tryb auto");

    g.cmd_nowy->ustaw_skrot(SkrotPolecenia::usb(17, BWS_MOD_CTRL), "Ctrl+N");
    g.cmd_otworz->ustaw_skrot(SkrotPolecenia::usb(18, BWS_MOD_CTRL), "Ctrl+O");
    g.cmd_zapisz->ustaw_skrot(SkrotPolecenia::usb(22, BWS_MOD_CTRL), "Ctrl+S");
    g.cmd_usun->ustaw_skrot(SkrotPolecenia::usb(76, 0), "Del");
    g.cmd_odswiez->ustaw_skrot(SkrotPolecenia::usb(62, 0), "F5");

    Panel* korzen = static_cast<Panel*>(okno.kontrolka_glowna());
    if (!korzen) gui_zakoncz_aplikacje();

    korzen->ustaw_orientacje(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(2, 2, 2, 2));

    // 1. Menu Główne
    MenuGlowne& menu_gorne = korzen->dodaj<MenuGlowne>();
    menu_gorne.ustaw_preferowany_rozmiar(Rozmiar(-1, 26));

    Menu& m_plik = menu_gorne.menu().dodaj_podmenu("Plik");
    m_plik.dodaj_polecenie(*g.cmd_nowy);
    m_plik.dodaj_polecenie(*g.cmd_otworz);
    m_plik.dodaj_polecenie(*g.cmd_zapisz);
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zakończ", 99, [](ElementMenu&, void*) {
        gui_zakoncz_aplikacje();
    });

    Menu& m_edycja = menu_gorne.menu().dodaj_podmenu("Edycja");
    m_edycja.dodaj_polecenie(*g.cmd_usun);
    m_edycja.dodaj_polecenie(*g.cmd_odswiez);

    Menu& m_pomoc = menu_gorne.menu().dodaj_podmenu("Pomoc");
    m_pomoc.dodaj_element("O programie...", 10, [](ElementMenu&, void*) {
        if (!g.okno) return;
        Komunikat::informacja(g.okno->zarzadca_popupow(),
                              "O programie Kontrolki 4",
                              "BursztynUI Kontrolki 4\nKompletny zestaw nowych komponentów dla Bursztyn OS.\nAutor: Programista Art",
                              nullptr, nullptr, g.okno->kontrolka_glowna());
    });

    // 2. Pasek Narzędzi
    PasekNarzedzi& toolbar = korzen->dodaj<PasekNarzedzi>();
    toolbar.ustaw_preferowany_rozmiar(Rozmiar(-1, 34));
    toolbar.dodaj_polecenie(*g.cmd_nowy);
    toolbar.dodaj_polecenie(*g.cmd_otworz);
    toolbar.dodaj_polecenie(*g.cmd_zapisz);
    toolbar.dodaj_separator();
    toolbar.dodaj_polecenie(*g.cmd_usun);
    toolbar.dodaj_polecenie(*g.cmd_odswiez);

    // Inicjalizacja reakcji poleceń
    g.cmd_nowy->przy_wykonaniu([](PolecenieUI&, void*) { status("Wykonano polecenie: Nowy"); });
    g.cmd_otworz->przy_wykonaniu([](PolecenieUI&, void*) { status("Wykonano polecenie: Otwórz"); });
    g.cmd_zapisz->przy_wykonaniu([](PolecenieUI&, void*) { status("Wykonano polecenie: Zapisz"); });
    g.cmd_usun->przy_wykonaniu([](PolecenieUI&, void*) { status("Wykonano polecenie: Usuń"); });
    g.cmd_odswiez->przy_wykonaniu([](PolecenieUI&, void*) { status("Wykonano polecenie: Odśwież"); });

    // 3. Karty (7 stron)
    Karty& karty = korzen->dodaj<Karty>();
    karty.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    karty.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));

    Panel* p_uklady = new Panel(); zbuduj_karte_uklady(*p_uklady); karty.dodaj_karte("Układy", p_uklady);
    Panel* p_polecenia = new Panel(); zbuduj_karte_polecenia(*p_polecenia); karty.dodaj_karte("Polecenia i Menu", p_polecenia);
    Panel* p_komunikaty = new Panel(); zbuduj_karte_komunikaty(*p_komunikaty); karty.dodaj_karte("Komunikaty", p_komunikaty);
    Panel* p_grafika = new Panel(); zbuduj_karte_grafika(*p_grafika); karty.dodaj_karte("Grafika i Wykres", p_grafika);
    Panel* p_dane = new Panel(); zbuduj_karte_duze_dane(*p_dane); karty.dodaj_karte("Duże dane", p_dane);
    Panel* p_postep = new Panel(); zbuduj_karte_postep(*p_postep); karty.dodaj_karte("Zajętość i Postęp", p_postep);
    Panel* p_motywy = new Panel(); zbuduj_karte_motywy(*p_motywy); karty.dodaj_karte("Motywy", p_motywy);

    // 4. Pasek Stanu
    PasekStanu& sb = korzen->dodaj<PasekStanu>();
    g.status = &sb;
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.dodaj_sekcje("Gotowy — Kontrolki 4 załadowane", 380, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("BursztynUI 2.0", 140, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Ring 3", 80, RozmiarSekcjiStanu::Stala);

    (void)app.uruchom(okno);
    bws_zakoncz_proces();
}
