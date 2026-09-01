/*
 * Bursztyn OS - BursztynUI Zaawansowane Demo Komponentow (Ring 3)
 * Pokaz pelnego zestawu zaawansowanych kontrolek GUI:
 *  - MenuGlowne & MenuKontekstowe & Popupy
 *  - PasekNarzedzi z przyciskami, przelacznikami i separatorem
 *  - Karty z podzialem tematycznym na zakladki
 *  - PasekPostepu (deterministyczny + nieokreslony animowany)
 *  - PoleNumeryczne (kroki, spinner, prefiks/sufiks, int64 safe)
 *  - Drzewo hierarchiczne z wezlami i rozwijaniem
 *  - WidokPrzewijany z kontenerem i scrollem
 *  - ListaWyboru & ListaRozwijana (ComboBox)
 *  - PoleTekstowe & PoleWielowierszowe (Memo)
 *  - PasekStanu z dynamicznymi sekcjami
 *  - Podpowiedz (Tooltips) dla kontrolek
 */

#include "../../loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

// =============================================================================
// Naglowek formatu .bur
// =============================================================================

extern "C" void _start();

static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0x48000ULL; // 288 KiB
static constexpr uint64_t BUR_TEKST_PRZESUNIECIE = 0x1000ULL;
static constexpr uint64_t BUR_TEKST_WIRTUALNY     = 0x1001000ULL;
static constexpr uint64_t BUR_DANE_PRZESUNIECIE   = 0x49000ULL;
static constexpr uint64_t BUR_DANE_WIRTUALNY       = 0x1049000ULL;
static constexpr uint64_t BUR_DANE_ROZMIAR         = 131072ULL;

extern "C" {
    __attribute__((section(".naglowek"), used))
    NaglowekBur naglowek = {
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

// =============================================================================
// Stan Aplikacji Demo
// =============================================================================

struct StanDemo {
    Okno* okno{nullptr};
    PasekStanu* pasek_stanu{nullptr};
    PasekPostepu* postep_deterministyczny{nullptr};
    PasekPostepu* postep_animowany{nullptr};
    PoleNumeryczne* pole_liczbowe{nullptr};
    PoleNumeryczne* pole_waga{nullptr};
    Drzewo* drzewo{nullptr};
    PoleTekstowe* txt_szukaj{nullptr};
    PoleWielowierszowe* memo{nullptr};
    ListaWyboru* lista{nullptr};
    ListaRozwijana* combo{nullptr};
    Suwak* suwak{nullptr};
};

static StanDemo g_demo;

static void dopisz_liczbe(char* bufor, size_t& pos, size_t pojemnosc, int n) {
    if (n < 0) {
        if (pos + 1 < pojemnosc) bufor[pos++] = '-';
        n = -n;
    }
    char tmp[16];
    size_t ile = 0;
    if (n == 0) tmp[ile++] = '0';
    else {
        while (n > 0 && ile < sizeof(tmp)) {
            tmp[ile++] = static_cast<char>('0' + (n % 10));
            n /= 10;
        }
    }
    while (ile > 0 && pos + 1 < pojemnosc) bufor[pos++] = tmp[--ile];
    bufor[pos] = '\0';
}

static void aktualizuj_status(const char* komunikat) {
    if (g_demo.pasek_stanu) {
        g_demo.pasek_stanu->ustaw_tekst(0, komunikat);
    }
}

// =============================================================================
// Budowa UI
// =============================================================================

static MenuGlowne* zbuduj_menu(Panel& panel_nadrzedny) {
    MenuGlowne& menu = panel_nadrzedny.dodaj<MenuGlowne>();
    menu.ustaw_preferowany_rozmiar(Rozmiar(300, 26));

    Menu& m_plik = menu.menu().dodaj_podmenu("Plik", 100);
    m_plik.dodaj_element("Nowy", 101, [](ElementMenu&, void*) { aktualizuj_status("Plik -> Nowy"); });
    m_plik.dodaj_element("Otworz...", 102, [](ElementMenu&, void*) { aktualizuj_status("Plik -> Otworz..."); });
    m_plik.dodaj_element("Zapisz", 103, [](ElementMenu&, void*) { aktualizuj_status("Plik -> Zapisz"); });
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zakoncz", 109, [](ElementMenu&, void*) {
        bws_zakoncz_proces();
    });

    Menu& m_edycja = menu.menu().dodaj_podmenu("Edycja", 200);
    m_edycja.dodaj_element("Cofnij", 201, [](ElementMenu&, void*) {
        if (g_demo.memo) g_demo.memo->cofnij();
        aktualizuj_status("Edycja -> Cofnij");
    });
    m_edycja.dodaj_element("Ponow", 202, [](ElementMenu&, void*) {
        if (g_demo.memo) g_demo.memo->ponow();
        aktualizuj_status("Edycja -> Ponow");
    });

    Menu& m_widok = menu.menu().dodaj_podmenu("Widok", 300);
    m_widok.dodaj_element("Odswiez", 301, [](ElementMenu&, void*) {
        aktualizuj_status("Widok -> Odswiezono");
    });

    Menu& m_pomoc = menu.menu().dodaj_podmenu("Pomoc", 400);
    m_pomoc.dodaj_element("O BursztynUI...", 401, [](ElementMenu&, void*) {
        aktualizuj_status("BursztynUI v2.0 - Retained-Mode GUI dla Bursztyn OS");
    });

    return &menu;
}

static PasekNarzedzi* zbuduj_pasek_narzedzi(Panel& panel_nadrzedny) {
    PasekNarzedzi& tb = panel_nadrzedny.dodaj<PasekNarzedzi>();
    tb.ustaw_preferowany_rozmiar(Rozmiar(300, 32));
    tb.ustaw_margines(Marginesy(0, 0, 0, 4));

    tb.dodaj_przycisk("Nowy", 1, [](ElementPaskaNarzedzi*, void*) { aktualizuj_status("Pasek: Nowy"); });
    tb.dodaj_przycisk("Otworz", 2, [](ElementPaskaNarzedzi*, void*) { aktualizuj_status("Pasek: Otworz"); });
    tb.dodaj_przycisk("Zapisz", 3, [](ElementPaskaNarzedzi*, void*) { aktualizuj_status("Pasek: Zapisz"); });
    tb.dodaj_separator();
    tb.dodaj_przelacznik("Siatka", true, [](ElementPaskaNarzedzi*, bool s, void*) {
        aktualizuj_status(s ? "Siatka wlaczona" : "Siatka wylaczona");
    });
    tb.dodaj_separator();

    PoleTekstowe* szukaj = new PoleTekstowe();
    szukaj->ustaw_preferowany_rozmiar(Rozmiar(120, 24));
    szukaj->ustaw_podpowiedz("Szukaj w zawartosci");
    tb.dodaj_kontrolke(szukaj);

    return &tb;
}

static void zbuduj_zakladke_podstawowe(Panel& strona) {
    Kolumna& glowna = strona.dodaj<Kolumna>();
    glowna.ustaw_odstep(8);

    // Sekcja postepu
    Wiersz& w_postep = glowna.dodaj<Wiersz>();
    w_postep.ustaw_odstep(12);

    Kolumna& col_prog = w_postep.dodaj<Kolumna>();
    col_prog.ustaw_odstep(4);
    col_prog.dodaj<Etykieta>("Pasek postepu (deterministyczny):");
    PasekPostepu& pb1 = col_prog.dodaj<PasekPostepu>(0, 100);
    pb1.ustaw_wartosc(65);
    pb1.ustaw_pokazywanie_procentu(true);
    pb1.ustaw_podpowiedz("Wskazuje postep operacji (65%)");
    g_demo.postep_deterministyczny = &pb1;

    Kolumna& col_prog_anim = w_postep.dodaj<Kolumna>();
    col_prog_anim.ustaw_odstep(4);
    col_prog_anim.dodaj<Etykieta>("Pasek postepu (animowany/nieokreslony):");
    PasekPostepu& pb2 = col_prog_anim.dodaj<PasekPostepu>(0, 100);
    pb2.ustaw_tryb_nieokreslony(true);
    pb2.ustaw_podpowiedz("Pasek w trybie ciaglej animacji oczekiwania");
    g_demo.postep_animowany = &pb2;

    // Sekcja pol numerycznych
    Wiersz& w_num = glowna.dodaj<Wiersz>();
    w_num.ustaw_odstep(12);

    Kolumna& col_num1 = w_num.dodaj<Kolumna>();
    col_num1.ustaw_odstep(4);
    col_num1.dodaj<Etykieta>("Pole numeryczne (0..100, krok 5):");
    PoleNumeryczne& pn1 = col_num1.dodaj<PoleNumeryczne>(0, 100, 25);
    pn1.ustaw_krok(5);
    pn1.ustaw_duzy_krok(20);
    pn1.ustaw_podpowiedz("Wartosc liczbowa z przyciskami zwieksz/zmniejsz");
    pn1.przy_zmianie_wartosci([](PoleNumeryczne*, int64_t w, void*) {
        if (g_demo.postep_deterministyczny) g_demo.postep_deterministyczny->ustaw_wartosc(w);
    });
    g_demo.pole_liczbowe = &pn1;

    Kolumna& col_num2 = w_num.dodaj<Kolumna>();
    col_num2.ustaw_odstep(4);
    col_num2.dodaj<Etykieta>("Pole numeryczne z prefiksem i sufiksem:");
    PoleNumeryczne& pn2 = col_num2.dodaj<PoleNumeryczne>(10, 500, 75);
    pn2.ustaw_prefiks("Waga: ");
    pn2.ustaw_sufiks(" kg");
    pn2.ustaw_krok(1);
    pn2.ustaw_podpowiedz("Sformatowana wartosc z prefiksem i sufiksem");
    g_demo.pole_waga = &pn2;

    // Sekcja suwaka i przyciskow
    Wiersz& w_controls = glowna.dodaj<Wiersz>();
    w_controls.ustaw_odstep(12);

    Kolumna& col_slider = w_controls.dodaj<Kolumna>();
    col_slider.ustaw_odstep(4);
    col_slider.dodaj<Etykieta>("Suwak (0..100):");
    Suwak& swk = col_slider.dodaj<Suwak>();
    swk.ustaw_zakres(0, 100);
    swk.ustaw_wartosc(65);
    swk.ustaw_preferowany_rozmiar(Rozmiar(160, 22));
    swk.ustaw_podpowiedz("Przeciagnij suwak, aby plynnie zmienic wartosc");
    swk.przy_zmianie_wartosci([](Suwak*, int val, void*) {
        if (g_demo.postep_deterministyczny) g_demo.postep_deterministyczny->ustaw_wartosc(val);
        if (g_demo.pole_liczbowe) g_demo.pole_liczbowe->ustaw_wartosc(val);
    });
    g_demo.suwak = &swk;

    RamkaGrupy& grupa_opcji = w_controls.dodaj<RamkaGrupy>("Opcje wyswietlania");
    Kolumna& col_opcje = grupa_opcji.dodaj<Kolumna>();
    col_opcje.ustaw_odstep(4);
    PrzyciskOpcji& opc1 = col_opcje.dodaj<PrzyciskOpcji>("Standardowa jakosc", 1);
    opc1.ustaw_zaznaczony(true);
    col_opcje.dodaj<PrzyciskOpcji>("Wysoka jakosc (2x DPI)", 2);
    col_opcje.dodaj<PrzyciskOpcji>("Tryb oszczedny", 3);

    // Pola wyboru i przyciski akcji
    Wiersz& w_checks = glowna.dodaj<Wiersz>();
    w_checks.ustaw_odstep(16);
    w_checks.dodaj<PoleWyboru>("Wlacz akceleracje sprzetowa", true);
    PoleWyboru& chk3 = w_checks.dodaj<PoleWyboru>("Stan nieokreslony", false);
    chk3.ustaw_trojstanowe(true);
    chk3.ustaw_stan_wyboru(StanWyboru::Nieokreslone);

    Przycisk& btn_test = w_checks.dodaj<Przycisk>("Zastosuj zmiany");
    btn_test.ustaw_podpowiedz("Zastosuj wprowadzone parametry konfiguracyjne");
    btn_test.przy_kliknieciu([](Przycisk*, void*) {
        aktualizuj_status("Zastosowano biezace ustawienia.");
    });
}

static void zbuduj_zakladke_tekst(Panel& strona) {
    Kolumna& glowna = strona.dodaj<Kolumna>();
    glowna.ustaw_odstep(8);

    glowna.dodaj<Etykieta>("Wielowierszowy edytor tekstu (PoleWielowierszowe / Memo):");

    PoleWielowierszowe& memo = glowna.dodaj<PoleWielowierszowe>();
    memo.ustaw_tekst(
        "Bursztyn OS - Niezalezny 64-bitowy system operacyjny.\n"
        "BursztynUI jest natywnym frameworkiem retained-mode w Ring 3.\n"
        "Wszystkie kontrolki wspieraja UTF-8: Zazółć gęślą jaźń!\n"
        "\n"
        "Skroty klawiszowe w edytorze:\n"
        " - Ctrl+A: Zaznacz calosc\n"
        " - Ctrl+C / Ctrl+X / Ctrl+V: Kopiuj / Wytnij / Wklej\n"
        " - Ctrl+Z / Ctrl+Y: Cofnij / Ponow\n"
        " - Shift + Strzalki: Zaznaczanie blokowe tekstu\n"
        " - PageUp / PageDown / Home / End: Szybka nawigacja\n"
    );
    memo.ustaw_preferowany_rozmiar(Rozmiar(-1, 220));
    memo.ustaw_podpowiedz("Pole wielowierszowe z obsluga schowka i historii edycji");
    g_demo.memo = &memo;

    Wiersz& w_input = glowna.dodaj<Wiersz>();
    w_input.ustaw_odstep(8);
    w_input.dodaj<Etykieta>("Jednowierszowe wejscie:");
    PoleTekstowe& txt = w_input.dodaj<PoleTekstowe>("Wpisz tekst do zatwierdzenia...");
    txt.ustaw_preferowany_rozmiar(Rozmiar(240, 24));
    txt.ustaw_podpowiedz("Pojedyncza linia edycji tekstu UTF-8");

    Przycisk& btn_dodaj = w_input.dodaj<Przycisk>("Wyslij do edytora");
    btn_dodaj.przy_kliknieciu([](Przycisk*, void* dane) {
        PoleTekstowe* t = static_cast<PoleTekstowe*>(dane);
        if (t && g_demo.memo) {
            g_demo.memo->dopisz_tekst(t->tekst());
            g_demo.memo->dopisz_tekst("\n");
            aktualizuj_status("Wstawiono tekst do edytora.");
        }
    }, &txt);
}

static void zbuduj_zakladke_listy_drzewo(Panel& strona) {
    Wiersz& glowna = strona.dodaj<Wiersz>();
    glowna.ustaw_odstep(12);

    // Drzewo hierarchiczne
    Kolumna& col_tree = glowna.dodaj<Kolumna>();
    col_tree.ustaw_odstep(4);
    col_tree.dodaj<Etykieta>("Struktura plikow (Drzewo):");

    Drzewo& tree = col_tree.dodaj<Drzewo>();
    tree.ustaw_preferowany_rozmiar(Rozmiar(220, 260));
    tree.ustaw_podpowiedz("Wirtualizowany widok drzewiasty (do 4096 wezlow)");

    UchwytWezlaDrzewa r_komp = tree.dodaj_korzen("Moj Komputer");
    UchwytWezlaDrzewa d_c = tree.dodaj_dziecko(r_komp, "Dysk Systemowy (C:)");
    UchwytWezlaDrzewa d_c_dok = tree.dodaj_dziecko(d_c, "Dokumenty");
    tree.dodaj_dziecko(d_c_dok, "raport_2026.txt");
    tree.dodaj_dziecko(d_c_dok, "notatki_gui.md");

    UchwytWezlaDrzewa d_c_sys = tree.dodaj_dziecko(d_c, "System");
    tree.dodaj_dziecko(d_c_sys, "kernel.elf");
    tree.dodaj_dziecko(d_c_sys, "bursztyn_ui.sys");
    tree.dodaj_dziecko(d_c_sys, "czcionka.psf");

    UchwytWezlaDrzewa d_c_usr = tree.dodaj_dziecko(d_c, "Uzytkownicy");
    tree.dodaj_dziecko(d_c_usr, "Programista");
    tree.dodaj_dziecko(d_c_usr, "Gosc");

    UchwytWezlaDrzewa r_siec = tree.dodaj_korzen("Siec Lokalna (LAN)");
    tree.dodaj_dziecko(r_siec, "Serwer Plikow (10.0.2.2)");
    tree.dodaj_dziecko(r_siec, "Brama Glowna (10.0.2.1)");

    tree.ustaw_rozwiniety(r_komp, true);
    tree.ustaw_rozwiniety(d_c, true);
    tree.ustaw_rozwiniety(d_c_dok, true);

    tree.przy_zmianie_wyboru([](Drzewo* t, UchwytWezlaDrzewa w, void*) {
        if (t) {
            char buf[128] = "Wybrano wezel: ";
            size_t p = 15;
            const char* name = t->tekst(w);
            while (*name && p + 1 < sizeof(buf)) buf[p++] = *name++;
            buf[p] = '\0';
            aktualizuj_status(buf);
        }
    });
    g_demo.drzewo = &tree;

    // Prawa kolumna z lista wyboru i combo
    Kolumna& col_lists = glowna.dodaj<Kolumna>();
    col_lists.ustaw_odstep(6);

    col_lists.dodaj<Etykieta>("Lista wyboru (ListaWyboru):");
    ListaWyboru& lst = col_lists.dodaj<ListaWyboru>();
    lst.ustaw_preferowany_rozmiar(Rozmiar(200, 160));
    lst.ustaw_podpowiedz("Wirtualizowana lista elementow z przewijaniem");
    for (int i = 1; i <= 50; ++i) {
        char item_buf[32] = "Element listy #";
        size_t p = 15;
        dopisz_liczbe(item_buf, p, sizeof(item_buf), i);
        lst.dodaj_element(item_buf);
    }
    lst.ustaw_wybrany_indeks(0);
    g_demo.lista = &lst;

    col_lists.dodaj<Etykieta>("Lista rozwijana (ListaRozwijana):");
    ListaRozwijana& combo = col_lists.dodaj<ListaRozwijana>();
    combo.ustaw_preferowany_rozmiar(Rozmiar(200, 24));
    combo.ustaw_podpowiedz("Rozwijane menu wyboru pojedynczej pozycji");
    combo.dodaj_element("Motyw: Bursztynowy Klasyczny");
    combo.dodaj_element("Motyw: Ciemny Grafit");
    combo.dodaj_element("Motyw: Wysoki Kontrast");
    combo.ustaw_wybrany_indeks(0);
    combo.przy_zmianie_wyboru([](ListaRozwijana*, int idx, void*) {
        char buf[64] = "Wybrano motyw nr ";
        size_t p = 17;
        dopisz_liczbe(buf, p, sizeof(buf), idx);
        aktualizuj_status(buf);
    });
    g_demo.combo = &combo;
}

static void zbuduj_zakladke_kontenery(Panel& strona) {
    Kolumna& glowna = strona.dodaj<Kolumna>();
    glowna.ustaw_odstep(8);

    glowna.dodaj<Etykieta>("Widok przewijany z duza zawartoscia (WidokPrzewijany):");

    WidokPrzewijany& scroll_view = glowna.dodaj<WidokPrzewijany>();
    scroll_view.ustaw_preferowany_rozmiar(Rozmiar(440, 220));
    scroll_view.ustaw_podpowiedz("Kontener przewijajacy z dwukierunkowym scrollem");

    // Dodaj wiele wierszy do wnetrza widoku przewijanego
    for (int r = 1; r <= 15; ++r) {
        char label_buf[48] = "Wiersz zawartosci widoku przewijanego #";
        size_t p = 39;
        dopisz_liczbe(label_buf, p, sizeof(label_buf), r);

        Etykieta* lbl = new Etykieta(label_buf);
        lbl->rozmiesc(Prostokat(10, (r - 1) * 28 + 6, 320, 22));
        scroll_view.dodaj_do_zawartosci(lbl);

        Przycisk* btn = new Przycisk("Akcja");
        btn->rozmiesc(Prostokat(340, (r - 1) * 28 + 4, 70, 22));
        btn->przy_kliknieciu([](Przycisk*, void* dane) {
            int nr = static_cast<int>(reinterpret_cast<intptr_t>(dane));
            char buf[64] = "Kliknieto przycisk w wierszu ";
            size_t pos = 29;
            dopisz_liczbe(buf, pos, sizeof(buf), nr);
            aktualizuj_status(buf);
        }, reinterpret_cast<void*>(static_cast<intptr_t>(r)));
        scroll_view.dodaj_do_zawartosci(btn);
    }
    scroll_view.ustaw_rozmiar_zawartosci(Rozmiar(460, 450));
}

static void zbuduj_zakladke_info(Panel& strona) {
    Kolumna& glowna = strona.dodaj<Kolumna>();
    glowna.ustaw_odstep(8);

    glowna.dodaj<Etykieta>("O architekturze BursztynUI:");

    PoleWielowierszowe& info_memo = glowna.dodaj<PoleWielowierszowe>();
    info_memo.ustaw_tylko_do_odczytu(true);
    info_memo.ustaw_tekst(
        "BursztynUI Framework v2.0\n"
        "========================\n"
        "1. Architektura: Ring 3 Retained-Mode GUI dla Bursztyn OS\n"
        "2. Brak zaleznosci od Linuksa i bibliotek zewnetrznych\n"
        "3. Zero alokacji sterty na goracych sciezkach (scroll, hover, render)\n"
        "4. Zaawansowane komponenty:\n"
        "   - PasekPostepu: deterministyczny + ciagla animacja\n"
        "   - PoleNumeryczne: spinner, wrap, formatowanie prefiks/sufiks\n"
        "   - Karty & StronaKarty: wielozakladkowy interfejs z przewijaniem\n"
        "   - WidokPrzewijany: uniwersalny kontener przewijany\n"
        "   - Drzewo: do 4096 wezlow, wirtualizacja wierszy\n"
        "   - PasekNarzedzi: akcje, przelaczniki, separatory, nadmiar\n"
        "   - PasekStanu: sekcje stala, dopasowana, rozciagana\n"
        "   - Podpowiedz: system tooltipow z precyzyjnym pozycjonowaniem\n"
        "   - MenuGlowne, MenuKontekstowe, ListaRozwijana\n"
    );
    info_memo.ustaw_preferowany_rozmiar(Rozmiar(-1, 240));
}

static PasekStanu* zbuduj_pasek_stanu(Panel& panel_nadrzedny) {
    PasekStanu& sb = panel_nadrzedny.dodaj<PasekStanu>();
    sb.ustaw_preferowany_rozmiar(Rozmiar(300, 24));
    sb.dodaj_sekcje("Gotowy", 100, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("UTF-8", 60, RozmiarSekcjiStanu::DopasujDoTresci);
    sb.dodaj_sekcje("DPI: 100%", 70, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Bursztyn OS", 90, RozmiarSekcjiStanu::Stala);
    return &sb;
}

// =============================================================================
// Punkt Wejscia Aplikacji
// =============================================================================

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;

    Okno okno("BursztynUI - Pokaz Komponentow Retained-Mode", 720, 520);
    g_demo.okno = &okno;

    Panel* korzen = static_cast<Panel*>(okno.kontrolka_glowna());
    if (!korzen) {
        korzen = new Panel(Orientacja::Pionowa);
        okno.ustaw_kontrolke_glowna(korzen);
    }
    korzen->ustaw_orientacje(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(4, 4, 4, 4));

    // Menu glowne
    (void)zbuduj_menu(*korzen);

    // Pasek narzedzi
    (void)zbuduj_pasek_narzedzi(*korzen);

    // Glowny kontener kart
    Karty& karty = korzen->dodaj<Karty>();
    karty.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    karty.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    // Zakladka 1: Podstawowe
    Panel* strona_podstawowe = new Panel();
    strona_podstawowe->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_podstawowe(*strona_podstawowe);
    karty.dodaj_karte("Podstawowe", strona_podstawowe);

    // Zakladka 2: Tekst i Edycja
    Panel* strona_tekst = new Panel();
    strona_tekst->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_tekst(*strona_tekst);
    karty.dodaj_karte("Tekst i Edycja", strona_tekst);

    // Zakladka 3: Listy i Drzewo
    Panel* strona_listy = new Panel();
    strona_listy->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_listy_drzewo(*strona_listy);
    karty.dodaj_karte("Listy i Drzewo", strona_listy);

    // Zakladka 4: Kontenery
    Panel* strona_kontenery = new Panel();
    strona_kontenery->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_kontenery(*strona_kontenery);
    karty.dodaj_karte("Kontenery", strona_kontenery);

    // Zakladka 5: Informacje
    Panel* strona_info = new Panel();
    strona_info->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_info(*strona_info);
    karty.dodaj_karte("Informacje", strona_info);

    // Pasek stanu
    g_demo.pasek_stanu = zbuduj_pasek_stanu(*korzen);

    app.uruchom(okno);
    bws_zakoncz_proces();
}
