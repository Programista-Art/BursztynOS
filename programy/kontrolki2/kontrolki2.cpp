/*
 * Bursztyn OS - BursztynUI Zaawansowane Kontrolki 2 (Ring 3)
 * Pokaz pelnego zestawu komponentow nowej generacji BursztynUI:
 *  - Tabela: retained-mode wielo-kolumnowa z wirtualizacja (1000 wierszy)
 *  - Rozdzielacz: plynny dragger horyzontalny i wertykalny
 *  - WidokIkon: siatka ikon i etykiet z wirtualizacja
 *  - Obraz: retained-mode bitmap viewer (Dopasuj, Naturalny, Wycentrowany, Rozciagnij)
 *  - PrzyciskObrazkowy: kompozycja ikony i tekstu w roznych trybach
 *  - PasekSciezki: nawigacja okruszkowa (breadcrumbs) z overflow
 *  - Kalendarz: siatka miesiaca, dni tygodnia, nawigacja dat
 *  - PoleDaty / PoleCzasu: precyzyjne strukturalne edytory czasu i daty
 * EdytorFormatowany i WidokDrzewiastoKolumnowy pozostaja odroczone do czasu
 * zaprojektowania wspolnego modelu tekstu/drzewa bez naruszania stabilnych
 * PoleWielowierszowe, Drzewo i Tabela.
 */

#include "loader.h"
#include "../../bursztyn_ui/bursztyn_ui.h"

using namespace BursztynUI;

// =============================================================================
// Naglowek formatu .bur
// =============================================================================

extern "C" void _start();

static constexpr uint64_t BUR_TEKST_ROZMIAR     = 0x48000ULL; // profil DemoUI
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
// Stan Aplikacji Kontrolki 2
// =============================================================================

struct StanKontrolki2 {
    Okno* okno{nullptr};
    PasekStanu* pasek_stanu{nullptr};
    Tabela* tabela{nullptr};
    Rozdzielacz* rozdzielacz{nullptr};
    WidokIkon* widok_ikon{nullptr};
    PasekSciezki* pasek_sciezki{nullptr};
    Obraz* obraz{nullptr};
    PrzyciskObrazkowy* btn_obrazkowy{nullptr};
    Kalendarz* kalendarz{nullptr};
    PoleDaty* pole_daty{nullptr};
    PoleCzasu* pole_czasu{nullptr};
};

static StanKontrolki2 g_k2;

static void aktualizuj_status(const char* komunikat) {
    if (g_k2.pasek_stanu) {
        g_k2.pasek_stanu->ustaw_tekst(0, komunikat);
    }
}

static void formatuj_liczbe(char* buf, size_t cap, int n) {
    if (cap < 2) return;
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[16];
    size_t len = 0;
    while (n > 0 && len < sizeof(tmp)) {
        tmp[len++] = static_cast<char>('0' + (n % 10));
        n /= 10;
    }
    size_t bi = 0;
    while (len > 0 && bi + 1 < cap) {
        buf[bi++] = tmp[--len];
    }
    buf[bi] = '\0';
}

// =============================================================================
// Zakładka 1: Dane (Tabela)
// =============================================================================

static void zbuduj_zakladke_dane(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa);

    // Etykieta sekcji
    Etykieta& lbl_tabela = p.dodaj<Etykieta>("Tabela Wielokolumnowa (Wirtualizacja 1000 wierszy):");
    lbl_tabela.ustaw_kolor_tekstu(0x00FFD700);
    lbl_tabela.ustaw_margines(Marginesy(0, 0, 0, 4));

    // 1. Tabela
    Tabela& tab = p.dodaj<Tabela>();
    g_k2.tabela = &tab;
    tab.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    tab.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    tab.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    tab.dodaj_kolumne("ID", 50, WyrownaniePoziome::Poczatek);
    tab.dodaj_kolumne("Proces / Usługa", 160, WyrownaniePoziome::Poczatek);
    tab.dodaj_kolumne("Pamięć (RAM)", 100, WyrownaniePoziome::Koniec);
    tab.dodaj_kolumne("PZB Poziom", 80, WyrownaniePoziome::Srodek);
    tab.dodaj_kolumne("Status", 100, WyrownaniePoziome::Poczatek);

    static const char* const PROCESY[] = {
        "menedzer_okien.bur", "shell.bur", "notatnik.bur", "kalkulator.bur",
        "hussar.bur", "snake.bur", "przegladarka.bur", "eksplorator.bur",
        "sterownik_ahci", "sterownik_e1000", "stos_tcp_ip", "serwer_bws"
    };

    tab.rozpocznij_aktualizacje();
    char buf_id[16];
    char buf_ram[24];
    for (int i = 0; i < 1000; ++i) {
        formatuj_liczbe(buf_id, sizeof(buf_id), i + 1);
        int ram_kb = 64 + (i * 37) % 512;
        formatuj_liczbe(buf_ram, sizeof(buf_ram), ram_kb);
        size_t l = 0; while (buf_ram[l]) ++l;
        buf_ram[l++] = ' '; buf_ram[l++] = 'K'; buf_ram[l++] = 'B'; buf_ram[l] = '\0';

        const char* nazwa = PROCESY[i % 12];
        const char* pzb = (i % 3 == 0) ? "PZB-1" : ((i % 3 == 1) ? "PZB-2" : "PZB-4");
        const char* status = (i % 4 == 0) ? "Czuwanie" : ((i % 4 == 1) ? "Aktywny" : "Uśpiony");

        tab.dodaj_wiersz(buf_id, nazwa, buf_ram, pzb, status);
    }
    tab.zakoncz_aktualizacje();

    tab.przy_zmianie_zaznaczenia([](Tabela* t, int row, void*) {
        if (t && row >= 0) {
            aktualizuj_status("Tabela: Zaznaczono wiersz");
        }
    });

    tab.przy_aktywacji([](Tabela*, int, void*) {
        aktualizuj_status("Tabela: Podwójne kliknięcie (Aktywacja)");
    });

}

// =============================================================================
// Zakładka 2: Nawigacja (Rozdzielacz + WidokIkon + PasekSciezki)
// =============================================================================

static void zbuduj_zakladke_nawigacja(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa);

    // 1. PasekSciezki
    Etykieta& lbl_path = p.dodaj<Etykieta>("Pasek Ścieżki (Nawigacja Okruszkowa):");
    lbl_path.ustaw_kolor_tekstu(0x00FFD700);
    lbl_path.ustaw_margines(Marginesy(0, 0, 0, 4));

    PasekSciezki& ps = p.dodaj<PasekSciezki>();
    g_k2.pasek_sciezki = &ps;
    ps.ustaw_preferowany_rozmiar(Rozmiar(-1, 28));
    ps.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    ps.dodaj_segment("Komputer");
    ps.dodaj_segment("Dysk Główny (AHCI)");
    ps.dodaj_segment("System");
    ps.dodaj_segment("Programy");
    ps.dodaj_segment("Kontrolki 2");

    ps.przy_wyborze_segmentu([](PasekSciezki*, int idx, const char* nazwa, void*) {
        (void)idx;
        (void)nazwa;
        aktualizuj_status("Pasek Ścieżki: Kliknięto segment");
    });

    // 2. Rozdzielacz
    Etykieta& lbl_split = p.dodaj<Etykieta>("Rozdzielacz (Przeciągnij pasek między panelami):");
    lbl_split.ustaw_kolor_tekstu(0x00FFD700);
    lbl_split.ustaw_margines(Marginesy(0, 8, 0, 4));

    Rozdzielacz& rz = p.dodaj<Rozdzielacz>(Orientacja::Pozioma);
    g_k2.rozdzielacz = &rz;
    rz.ustaw_preferowany_rozmiar(Rozmiar(-1, 260));
    rz.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    rz.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    // Lewy panel w rozdzielaczu: Drzewo katalogów
    Panel* panel_lewy = new Panel(Orientacja::Pionowa);
    panel_lewy->ustaw_wypelnienie(Marginesy(4, 4, 4, 4));
    Etykieta& lbl_l = panel_lewy->dodaj<Etykieta>("Drzewo Folderów:");
    lbl_l.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));

    Drzewo& tree = panel_lewy->dodaj<Drzewo>();
    tree.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    tree.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    tree.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    auto r1 = tree.dodaj_korzen("Pulpit", 1);
    tree.dodaj_dziecko(r1, "Moje Dokumenty", 2);
    tree.dodaj_dziecko(r1, "Obrazy", 3);
    auto r2 = tree.dodaj_korzen("Dysk Systemowy (C:)", 4);
    tree.dodaj_dziecko(r2, "Programy", 5);
    tree.dodaj_dziecko(r2, "Sterowniki", 6);
    tree.dodaj_dziecko(r2, "Logi", 7);

    // Prawy panel w rozdzielaczu: WidokIkon
    Panel* panel_prawy = new Panel(Orientacja::Pionowa);
    panel_prawy->ustaw_wypelnienie(Marginesy(4, 4, 4, 4));
    Etykieta& lbl_r = panel_prawy->dodaj<Etykieta>("Zawartość (Widok Ikon):");
    lbl_r.ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST));

    WidokIkon& wi = panel_prawy->dodaj<WidokIkon>();
    g_k2.widok_ikon = &wi;
    wi.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));
    wi.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    wi.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);

    wi.rozpocznij_aktualizacje();
    wi.dodaj_element("Notatnik", TypIkonyWidoku::Aplikacja);
    wi.dodaj_element("Kalkulator", TypIkonyWidoku::Aplikacja);
    wi.dodaj_element("Husarz", TypIkonyWidoku::Aplikacja);
    wi.dodaj_element("Dokument.txt", TypIkonyWidoku::Dokument);
    wi.dodaj_element("Projekt.bsp", TypIkonyWidoku::Plik);
    wi.dodaj_element("Zdjecie.bmp", TypIkonyWidoku::Obraz);
    wi.dodaj_element("Dysk AHCI", TypIkonyWidoku::Dysk);
    wi.dodaj_element("Ustawienia", TypIkonyWidoku::Ustawienia);
    wi.dodaj_element("Ulubione", TypIkonyWidoku::Gwiazda);
    wi.dodaj_element("Katalog 1", TypIkonyWidoku::Folder);
    wi.dodaj_element("Katalog 2", TypIkonyWidoku::Folder);
    wi.dodaj_element("Katalog 3", TypIkonyWidoku::Folder);
    for (int i = 12; i < 1000; ++i) {
        char nazwa[24] = "Element ";
        formatuj_liczbe(nazwa + 8, sizeof(nazwa) - 8, i + 1);
        wi.dodaj_element(nazwa, TypIkonyWidoku::Plik,
                         static_cast<uint64_t>(i));
    }
    wi.zakoncz_aktualizacje();

    wi.przy_zmianie_zaznaczenia([](WidokIkon*, int, void*) {
        aktualizuj_status("Widok Ikon: Zmieniono zaznaczenie");
    });

    wi.przy_aktywacji([](WidokIkon*, int, void*) {
        aktualizuj_status("Widok Ikon: Podwójne kliknięcie");
    });

    rz.ustaw_pierwszy(panel_lewy);
    rz.ustaw_drugi(panel_prawy);
    rz.ustaw_pozycje_podzialu(180);
}

// =============================================================================
// Zakładka 3: Grafika (Obraz + PrzyciskObrazkowy)
// =============================================================================

static void zbuduj_zakladke_grafika(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa);

    Etykieta& lbl_img = p.dodaj<Etykieta>("Obraz (Retained-Mode Graphics & Scaling):");
    lbl_img.ustaw_kolor_tekstu(0x00FFD700);
    lbl_img.ustaw_margines(Marginesy(0, 0, 0, 4));

    // Kontener poziomy dla Obrazu i Kontrolek trybu
    Panel& row_img = p.dodaj<Panel>(Orientacja::Pozioma);
    row_img.ustaw_preferowany_rozmiar(Rozmiar(-1, 180));
    row_img.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    // Generowany syntetyczny obraz testowy (64x64 szachownica bursztynowa)
    static uint32_t s_test_pixels[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            bool check = ((x / 8) + (y / 8)) % 2 == 0;
            if (check) {
                s_test_pixels[y * 64 + x] = 0xFFE58A00U; // Bursztynowy
            } else {
                s_test_pixels[y * 64 + x] = 0xFF2B1D0EU; // Ciemny brąz
            }
        }
    }

    Obraz& img = row_img.dodaj<Obraz>();
    g_k2.obraz = &img;
    img.ustaw_obraz(s_test_pixels, 64, 64, false);
    img.ustaw_tryb(TrybObrazu::Dopasuj);
    img.ustaw_preferowany_rozmiar(Rozmiar(200, 160));
    img.ustaw_kolor_ramki(Kolory::RamkaDomyslna);
    img.ustaw_grubosc_ramki(1);

    // Panel opcji trybu
    Panel& p_opt = row_img.dodaj<Panel>(Orientacja::Pionowa);
    p_opt.ustaw_wypelnienie(Marginesy(16, 0, 0, 0));

    Przycisk& btn_mode_fit = p_opt.dodaj<Przycisk>("Tryb: Dopasuj");
    btn_mode_fit.ustaw_preferowany_rozmiar(Rozmiar(150, 30));
    btn_mode_fit.przy_kliknieciu([](Przycisk*, void*) {
        if (g_k2.obraz) g_k2.obraz->ustaw_tryb(TrybObrazu::Dopasuj);
        aktualizuj_status("Obraz: Ustawiono tryb Dopasuj");
    });

    Przycisk& btn_mode_center = p_opt.dodaj<Przycisk>("Tryb: Wycentruj");
    btn_mode_center.ustaw_preferowany_rozmiar(Rozmiar(150, 30));
    btn_mode_center.ustaw_margines(Marginesy(0, 6, 0, 0));
    btn_mode_center.przy_kliknieciu([](Przycisk*, void*) {
        if (g_k2.obraz) g_k2.obraz->ustaw_tryb(TrybObrazu::Wycentrowany);
        aktualizuj_status("Obraz: Ustawiono tryb Wycentruj");
    });

    Przycisk& btn_mode_stretch = p_opt.dodaj<Przycisk>("Tryb: Rozciągnij");
    btn_mode_stretch.ustaw_preferowany_rozmiar(Rozmiar(150, 30));
    btn_mode_stretch.ustaw_margines(Marginesy(0, 6, 0, 0));
    btn_mode_stretch.przy_kliknieciu([](Przycisk*, void*) {
        if (g_k2.obraz) g_k2.obraz->ustaw_tryb(TrybObrazu::Rozciagnij);
        aktualizuj_status("Obraz: Ustawiono tryb Rozciągnij");
    });

    // Sekcja Przyciski Obrazkowe
    Etykieta& lbl_btn = p.dodaj<Etykieta>("Przyciski Obrazkowe (Różne konfiguracje i pozycje ikon):");
    lbl_btn.ustaw_kolor_tekstu(0x00FFD700);
    lbl_btn.ustaw_margines(Marginesy(0, 16, 0, 6));

    Panel& row_btn = p.dodaj<Panel>(Orientacja::Pozioma);
    row_btn.ustaw_preferowany_rozmiar(Rozmiar(-1, 50));
    row_btn.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    PrzyciskObrazkowy& b1 = row_btn.dodaj<PrzyciskObrazkowy>("Otwórz", TypIkonyWidoku::Folder);
    b1.ustaw_preferowany_rozmiar(Rozmiar(110, 36));
    b1.przy_kliknieciu([](Przycisk*, void*) { aktualizuj_status("Przycisk: Otwórz folder"); });

    PrzyciskObrazkowy& b2 = row_btn.dodaj<PrzyciskObrazkowy>("Zapisz", TypIkonyWidoku::Dysk);
    b2.ustaw_preferowany_rozmiar(Rozmiar(110, 36));
    b2.ustaw_margines(Marginesy(8, 0, 0, 0));
    b2.przy_kliknieciu([](Przycisk*, void*) { aktualizuj_status("Przycisk: Zapisz plik"); });

    PrzyciskObrazkowy& b3 = row_btn.dodaj<PrzyciskObrazkowy>("Ustawienia", TypIkonyWidoku::Ustawienia);
    b3.ustaw_preferowany_rozmiar(Rozmiar(130, 36));
    b3.ustaw_margines(Marginesy(8, 0, 0, 0));
    b3.ustaw_pozycja_ikony(PozycjaIkony::Prawo);
    b3.przy_kliknieciu([](Przycisk*, void*) { aktualizuj_status("Przycisk: Ustawienia systemowe"); });

    PrzyciskObrazkowy& b4 = row_btn.dodaj<PrzyciskObrazkowy>("", TypIkonyWidoku::Gwiazda);
    b4.ustaw_tryb(TrybPrzyciskuObrazkowego::TylkoIkona);
    b4.ustaw_preferowany_rozmiar(Rozmiar(44, 36));
    b4.ustaw_margines(Marginesy(8, 0, 0, 0));
    b4.przy_kliknieciu([](Przycisk*, void*) { aktualizuj_status("Przycisk ikony: Ulubione"); });
}

// =============================================================================
// Zakładka 4: Data i czas (Kalendarz + PoleDaty + PoleCzasu)
// =============================================================================

static void zbuduj_zakladke_data_czas(Panel& p) {
    p.ustaw_orientacje(Orientacja::Pionowa);

    // Kontener poziomy: Kalendarz po lewej, PoleDaty i PoleCzasu po prawej
    Panel& row = p.dodaj<Panel>(Orientacja::Pozioma);
    row.ustaw_preferowany_rozmiar(Rozmiar(-1, 300));
    row.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    // Lewa strona: Kalendarz
    Panel& p_cal = row.dodaj<Panel>(Orientacja::Pionowa);
    p_cal.ustaw_preferowany_rozmiar(Rozmiar(260, 280));

    Etykieta& lbl_cal = p_cal.dodaj<Etykieta>("Kalendarz Miesięczny:");
    lbl_cal.ustaw_kolor_tekstu(0x00FFD700);
    lbl_cal.ustaw_margines(Marginesy(0, 0, 0, 4));

    Kalendarz& cal = p_cal.dodaj<Kalendarz>(DataKalendarza{2026, 9, 2});
    g_k2.kalendarz = &cal;
    cal.ustaw_preferowany_rozmiar(Rozmiar(250, 230));

    cal.przy_zmianie_daty([](Kalendarz*, DataKalendarza d, void*) {
        // Odbicie wartosci do powiazanego pola nie jest drugim zdarzeniem
        // uzytkownika i nie moze ponownie uruchamiac lancucha callbackow.
        if (g_k2.pole_daty) g_k2.pole_daty->ustaw_date(d, false);
    });

    // Prawa strona: PoleDaty i PoleCzasu
    Panel& p_fields = row.dodaj<Panel>(Orientacja::Pionowa);
    p_fields.ustaw_wypelnienie(Marginesy(20, 0, 0, 0));

    Etykieta& lbl_date = p_fields.dodaj<Etykieta>("Pole Daty (RRRR-MM-DD):");
    lbl_date.ustaw_kolor_tekstu(0x00FFD700);
    lbl_date.ustaw_margines(Marginesy(0, 0, 0, 4));

    PoleDaty& pdate = p_fields.dodaj<PoleDaty>(DataKalendarza{2026, 9, 2});
    g_k2.pole_daty = &pdate;
    pdate.ustaw_preferowany_rozmiar(Rozmiar(180, 32));

    pdate.przy_zmianie_daty([](PoleDaty*, DataKalendarza d, void*) {
        if (g_k2.kalendarz) g_k2.kalendarz->ustaw_wybrana_data(d, false);
    });

    Etykieta& lbl_time = p_fields.dodaj<Etykieta>("Pole Czasu (HH:MM:SS):");
    lbl_time.ustaw_kolor_tekstu(0x00FFD700);
    lbl_time.ustaw_margines(Marginesy(0, 16, 0, 4));

    PoleCzasu& ptime = p_fields.dodaj<PoleCzasu>(CzasDnia{14, 30, 0});
    g_k2.pole_czasu = &ptime;
    ptime.ustaw_preferowany_rozmiar(Rozmiar(180, 32));

    // PoleCzasu pokazuje wartosc bezposrednio. Brak odleglego callbacku
    // statusu utrzymuje spinner jako lokalna operacje dirty-redraw.
}

// =============================================================================
// Menu Główne i Pasek Stanu
// =============================================================================

static MenuGlowne* zbuduj_menu(Panel& panel_nadrzedny) {
    MenuGlowne& menu = panel_nadrzedny.dodaj<MenuGlowne>();
    menu.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    menu.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);

    Menu& m_plik = menu.menu().dodaj_podmenu("Plik", 100);
    m_plik.dodaj_element("Nowy", 101, [](ElementMenu&, void*) {
        aktualizuj_status("Wybrano: Plik -> Nowy");
    });
    m_plik.dodaj_separator();
    m_plik.dodaj_element("Zamknij", 109, [](ElementMenu&, void*) {
        gui_zakoncz_aplikacje();
    });

    Menu& m_widok = menu.menu().dodaj_podmenu("Widok", 200);
    m_widok.dodaj_element("Odśwież", 201, [](ElementMenu&, void*) {
        aktualizuj_status("Odświeżono widok");
    });

    Menu& m_pomoc = menu.menu().dodaj_podmenu("Pomoc", 300);
    m_pomoc.dodaj_element("O programie", 301, [](ElementMenu&, void*) {
        aktualizuj_status("BursztynUI Kontrolki 2 v0.1");
    });

    return &menu;
}

static PasekStanu* zbuduj_pasek_stanu(Panel& panel_nadrzedny) {
    PasekStanu& sb = panel_nadrzedny.dodaj<PasekStanu>();
    sb.ustaw_preferowany_rozmiar(Rozmiar(-1, 24));
    sb.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    sb.dodaj_sekcje("Gotowy", 120, RozmiarSekcjiStanu::Rozciagnij);
    sb.dodaj_sekcje("UTF-8", 60, RozmiarSekcjiStanu::DopasujDoTresci);
    sb.dodaj_sekcje("Kontrolki 2", 90, RozmiarSekcjiStanu::Stala);
    sb.dodaj_sekcje("Bursztyn OS", 90, RozmiarSekcjiStanu::Stala);
    return &sb;
}

// =============================================================================
// Punkt Wejścia Aplikacji
// =============================================================================

extern "C" __attribute__((noreturn)) void _start() {
    AplikacjaGUI app;

    Okno okno("BursztynUI Kontrolki 2", 780, 580);
    g_k2.okno = &okno;

    Panel* korzen = static_cast<Panel*>(okno.kontrolka_glowna());
    if (!korzen) {
        korzen = new Panel(Orientacja::Pionowa);
        okno.ustaw_kontrolke_glowna(korzen);
    }
    korzen->ustaw_orientacje(Orientacja::Pionowa);
    korzen->ustaw_wypelnienie(Marginesy(4, 4, 4, 4));

    // Menu główne
    (void)zbuduj_menu(*korzen);

    // Główny kontener kart
    Karty& karty = korzen->dodaj<Karty>();
    karty.ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
    karty.ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
    karty.ustaw_preferowany_rozmiar(Rozmiar(-1, -1));

    // Zakładka 1: Dane
    Panel* strona_dane = new Panel(Orientacja::Pionowa);
    strona_dane->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_dane(*strona_dane);
    karty.dodaj_karte("Dane", strona_dane);

    // Zakładka 2: Nawigacja
    Panel* strona_nawigacja = new Panel(Orientacja::Pionowa);
    strona_nawigacja->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_nawigacja(*strona_nawigacja);
    karty.dodaj_karte("Nawigacja", strona_nawigacja);

    // Zakładka 3: Grafika
    Panel* strona_grafika = new Panel(Orientacja::Pionowa);
    strona_grafika->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_grafika(*strona_grafika);
    karty.dodaj_karte("Grafika", strona_grafika);

    // Zakładka 4: Data i czas
    Panel* strona_data_czas = new Panel(Orientacja::Pionowa);
    strona_data_czas->ustaw_wypelnienie(Marginesy(8, 8, 8, 8));
    zbuduj_zakladke_data_czas(*strona_data_czas);
    karty.dodaj_karte("Data i czas", strona_data_czas);

    // Pasek stanu
    g_k2.pasek_stanu = zbuduj_pasek_stanu(*korzen);

    app.uruchom(okno);
    bws_zakoncz_proces();
}
