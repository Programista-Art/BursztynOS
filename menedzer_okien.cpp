/*
 * Menedzer Okien (Pulpit i Pasek Zadan) dla Bursztyn OS
 * Wersja uporzadkowana: poprawne hitboxy, Menu Start i odswiezanie zegara.
 */

#include "bursztyn_gui.h"

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

extern "C" {
    __attribute__((section(".naglowek"), used))
    struct NaglowekBur naglowek = {
        {'B', 'U', 'R', '\0'},
        (uint64_t)&_start,
        4096, 32768, 0x601000,
        36864, 131072, 0x609000
    };
}

namespace {

constexpr int PASEK_WYS = 40;
constexpr int MENU_X = 10;
constexpr int MENU_W = 220;
constexpr int MENU_WYS = 185;
constexpr int MENU_GORNY_MARGINES = 10;
constexpr int MENU_ELEMENT_WYS = 25;
constexpr int MENU_ELEMENTOW = 6;

constexpr int PRZYCISK_MENU_X = 10;
constexpr int PRZYCISK_MENU_W = 80;
constexpr int PRZYCISK_MENU_H = 30;

constexpr int IKONA_W = 48;
constexpr int IKONA_H = 48;
constexpr int SKROT_W = 32;
constexpr int SKROT_H = 32;

constexpr int BUFOR_CZASU = 32;
constexpr int TASK_BUTTON_X = 100;
constexpr int TASK_BUTTON_W = 112;
constexpr int TASK_BUTTON_GAP = 4;

const char* const SCIEZKA_POWLOKA = "/shell.bur";
const char* const SCIEZKA_NOTATNIK = "/programy/notatnik.cebula/notatnik.bur";
const char* const SCIEZKA_KALKULATOR = "/programy/kalkulator.cebula/kalkulator.bur";
const char* const SCIEZKA_HUSSAR = "/programy/przegladarka.cebula/przegladarka.bur";

int screen_w = 1024;
int screen_h = 768;
bool menu_start_otwarte = false;
char ostatni_czas[BUFOR_CZASU] = {};
GuiOknoInfo okna_taskbara[SKLADACZ_MAKS_WARSTW] = {};
uint32_t liczba_okien_taskbara = 0;

bool tekst_zaczyna_sie(const char* tekst, const char* prefiks) {
    if (!tekst || !prefiks) return false;
    for (int i = 0; prefiks[i] != '\0'; ++i)
        if (tekst[i] != prefiks[i]) return false;
    return true;
}

uint64_t sygnatura_okien() {
    uint64_t h = liczba_okien_taskbara;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        const GuiOknoInfo& o = okna_taskbara[i];
        h ^= o.window_id + (h << 6U) + (h >> 2U);
        h ^= (static_cast<uint64_t>(o.stan) << 1U) | o.aktywne;
    }
    return h;
}

void odswiez_snapshot_okien() {
    liczba_okien_taskbara = gui_pobierz_okna(
        okna_taskbara, SKLADACZ_MAKS_WARSTW);
}

bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return false;

    const int64_t prawa = (int64_t)x + (int64_t)w;
    const int64_t dol = (int64_t)y + (int64_t)h;

    return (int64_t)px >= (int64_t)x &&
           (int64_t)py >= (int64_t)y &&
           (int64_t)px < prawa &&
           (int64_t)py < dol;
}

void wyzeruj_bufor(char* bufor, int rozmiar) {
    if (!bufor || rozmiar <= 0) return;
    for (int i = 0; i < rozmiar; i++) bufor[i] = '\0';
}

bool pobierz_i_zaktualizuj_czas() {
    char obecny_czas[BUFOR_CZASU];
    wyzeruj_bufor(obecny_czas, BUFOR_CZASU);

    // BWS 9 w obecnym ABI wypelnia bufor czasu przekazany przez Ring 3.
    bws_wywolaj(9, (uint64_t)obecny_czas);

    // Obrona na wypadek, gdy implementacja BWS 9 zapomni o terminatorze.
    obecny_czas[BUFOR_CZASU - 1] = '\0';

    bool zmieniony = false;
    for (int i = 0; i < BUFOR_CZASU; i++) {
        if (ostatni_czas[i] != obecny_czas[i]) zmieniony = true;
        ostatni_czas[i] = obecny_czas[i];
    }

    return zmieniony;
}

void RysujTaskbar() {
    gui_rysuj_prostokat(0, screen_h - PASEK_WYS,
                        screen_w, PASEK_WYS, 0x001A0B00);
    gui_rysuj_prostokat(0, screen_h - PASEK_WYS,
                        screen_w, 2, 0x00E58A00);
    gui_rysuj_prostokat(PRZYCISK_MENU_X, screen_h - 35,
                        PRZYCISK_MENU_W, PRZYCISK_MENU_H, 0x00E58A00);
    rysuj_tekst_wysrodkowany(PRZYCISK_MENU_X, screen_h - 35,
                             PRZYCISK_MENU_W, PRZYCISK_MENU_H,
                             1, 0x001A0B00, "Menu");
    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        const GuiOknoInfo& o = okna_taskbara[i];
        if (o.tytul[0] == '\0' || task_x + TASK_BUTTON_W > screen_w - 150)
            continue;
        const uint32_t kolor = o.aktywne ? 0x00E58A00 :
            (o.stan == GUI_OKNO_ZMINIMALIZOWANE ? 0x00452B00 : 0x008A5A00);
        gui_rysuj_prostokat(task_x, screen_h - 36,
                            TASK_BUTTON_W, SKROT_H, kolor);
        char podpis[15] = {};
        for (int c = 0; c < 14 && o.tytul[c] != '\0'; ++c)
            podpis[c] = o.tytul[c];
        rysuj_tekst_wysrodkowany(task_x, screen_h - 36,
                                 TASK_BUTTON_W, SKROT_H, 1,
                                 0x00FFFFFF, podpis);
        task_x += TASK_BUTTON_W + TASK_BUTTON_GAP;
    }
}

void RysujPulpit(bool wymus_pelne_odswiezenie) {
    if (wymus_pelne_odswiezenie) {
        // Dla procesu warstwowego BWS 19 zeruje prywatna warstwe.
        gui_odswiez_pulpit();
    }

    RysujTaskbar();

    // Ikona Notatnika.
    gui_rysuj_prostokat(50, 50, IKONA_W, IKONA_H, 0x00FFBF00);
    gui_rysuj_prostokat(52, 52, 44, 44, 0x00FFFFFF);
    gui_rysuj_prostokat(56, 58, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 64, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 70, 20, 2, 0x00000000);
    gui_rysuj_prostokat(56, 76, 32, 2, 0x00000000);
    gui_rysuj_prostokat(56, 82, 24, 2, 0x00000000);
    rysuj_tekst_wysrodkowany(50, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Notatnik");

    // Ikona Kalkulatora.
    gui_rysuj_prostokat(130, 50, IKONA_W, IKONA_H, 0x008A5A00);
    gui_rysuj_prostokat(132, 52, 44, 44, 0x001A0B00);
    rysuj_tekst_wysrodkowany(130, 60, IKONA_W, 16,
                             1, 0x00FFBF00, "+ -");
    rysuj_tekst_wysrodkowany(130, 80, IKONA_W, 16,
                             1, 0x00FFBF00, "* =");
    rysuj_tekst_wysrodkowany(130, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Kalkulator");

    // Ikona przegladarki Hussar.
    gui_rysuj_prostokat(210, 50, IKONA_W, IKONA_H, 0x000078D7);
    gui_rysuj_prostokat(212, 52, 44, 44, 0x000055AA);
    rysuj_tekst_wysrodkowany(210, 65, IKONA_W, 16,
                             1, 0x00FFFFFF, "WWW");
    rysuj_tekst_wysrodkowany(210, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Husarz");

    if (menu_start_otwarte) {
        const int menu_y = screen_h - PASEK_WYS - MENU_WYS;

        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, MENU_WYS, 0x00301500);
        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, 1, 0x00E58A00);
        gui_rysuj_prostokat(MENU_X, menu_y, 1, MENU_WYS, 0x00E58A00);
        gui_rysuj_prostokat(MENU_X + MENU_W - 1, menu_y,
                            1, MENU_WYS, 0x00E58A00);

        const char* menu_elementy[MENU_ELEMENTOW] = {
            "> Powloka Bursztyna",
            "> Notatnik",
            "> Kalkulator",
            "> Przegladarka Husarz",
            "> Uruchom ponownie",
            "> Zamknij"
        };

        for (int i = 0; i < MENU_ELEMENTOW; i++) {
            const int item_y = menu_y + MENU_GORNY_MARGINES +
                               (i * MENU_ELEMENT_WYS);
            gui_wypisz_tekst_kolor(MENU_X + 10, item_y + 5,
                                   0x00FFFFFF, menu_elementy[i]);
        }
    }

    // BWS 17 sklada warstwy i prezentuje gotowa klatke.
    gui_odswiez();
}

void zamknij_menu_i_odtworz_pulpit() {
    if (!menu_start_otwarte) return;
    const int menu_y = screen_h - PASEK_WYS - MENU_WYS;
    menu_start_otwarte = false;
    gui_ustaw_system_overlay(false, MENU_X, menu_y, MENU_W, MENU_WYS);
    RysujPulpit(true);
}

void uruchom_program_gui(const char* sciezka) {
    if (!sciezka) return;

    zamknij_menu_i_odtworz_pulpit();

    // Nie zerujemy globalnego trybu Ring 3 przed aplikacja GUI. Obecna
    // implementacja w grafika.cpp uzywa jednej globalnej flagi, a aplikacje
    // GUI i tak ustawiaja ten tryb na true po starcie.
    gui_ustaw_przejecie_myszy(true);
    bws_wywolaj(10, (uint64_t)sciezka);
}

bool aktywuj_istniejace(const char* prefiks_tytulu) {
    odswiez_snapshot_okien();
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        if (tekst_zaczyna_sie(okna_taskbara[i].tytul, prefiks_tytulu)) {
            zamknij_menu_i_odtworz_pulpit();
            return gui_aktywuj_okno(okna_taskbara[i].window_id);
        }
    }
    return false;
}

void uruchom_lub_aktywuj(const char* sciezka, const char* prefiks_tytulu) {
    if (!aktywuj_istniejace(prefiks_tytulu)) uruchom_program_gui(sciezka);
}

void uruchom_powloke() {
    uruchom_lub_aktywuj(SCIEZKA_POWLOKA, "Powloka Bursztynowa");
}

void wykonaj_restart() {
    bws_wywolaj(25);
    while (true) asm volatile("pause");
}

void wykonaj_zamkniecie() {
    bws_wywolaj(26);
    while (true) asm volatile("pause");
}

void obsluz_element_menu(int item_index) {
    switch (item_index) {
        case 0:
            uruchom_powloke();
            break;
        case 1:
            uruchom_lub_aktywuj(SCIEZKA_NOTATNIK, "Notatnik");
            break;
        case 2:
            uruchom_lub_aktywuj(SCIEZKA_KALKULATOR, "Kalkulator");
            break;
        case 3:
            uruchom_lub_aktywuj(SCIEZKA_HUSSAR, "Husarz");
            break;
        case 4:
            wykonaj_restart();
            break;
        case 5:
            wykonaj_zamkniecie();
            break;
        default:
            break;
    }
}

void obsluz_klikniecie(int mx, int my) {
    const int menu_y = screen_h - PASEK_WYS - MENU_WYS;
    const int lista_y = menu_y + MENU_GORNY_MARGINES;
    const int lista_h = MENU_ELEMENTOW * MENU_ELEMENT_WYS;

    // Przycisk Menu ma pierwszenstwo niezaleznie od aktualnego stanu menu.
    if (punkt_w_prostokacie(mx, my,
                            PRZYCISK_MENU_X, screen_h - 35,
                            PRZYCISK_MENU_W, PRZYCISK_MENU_H)) {
        menu_start_otwarte = !menu_start_otwarte;

        gui_ustaw_system_overlay(menu_start_otwarte, MENU_X,
                                 screen_h - PASEK_WYS - MENU_WYS,
                                 MENU_W, MENU_WYS);

        // Przy zamykaniu trzeba wyczyscic stary prostokat menu z warstwy.
        RysujPulpit(!menu_start_otwarte);
        return;
    }

    if (menu_start_otwarte) {
        // Klikalne sa tylko rzeczywiste wiersze listy. Gorny margines nie
        // moze zostac zinterpretowany jako element numer 0.
        if (punkt_w_prostokacie(mx, my,
                                MENU_X, lista_y,
                                MENU_W, lista_h)) {
            const int item_index = (my - lista_y) / MENU_ELEMENT_WYS;
            if (item_index >= 0 && item_index < MENU_ELEMENTOW) {
                obsluz_element_menu(item_index);
            }
            return;
        }

        // Klik poza lista zamyka Menu Start i usuwa jego stare piksele.
        zamknij_menu_i_odtworz_pulpit();
        return;
    }

    // Ikony na pulpicie.
    if (punkt_w_prostokacie(mx, my, 50, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_NOTATNIK, "Notatnik");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 130, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_KALKULATOR, "Kalkulator");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 210, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_HUSSAR, "Husarz");
        return;
    }

    int task_x = TASK_BUTTON_X;
    for (uint32_t i = 0; i < liczba_okien_taskbara; ++i) {
        if (okna_taskbara[i].tytul[0] == '\0' ||
            task_x + TASK_BUTTON_W > screen_w - 150) continue;
        if (punkt_w_prostokacie(mx, my, task_x, screen_h - 36,
                                TASK_BUTTON_W, SKROT_H)) {
            (void)gui_aktywuj_okno(okna_taskbara[i].window_id);
            return;
        }
        task_x += TASK_BUTTON_W + TASK_BUTTON_GAP;
    }
}

} // namespace

extern "C" __attribute__((noreturn)) void _start() {
    gui_pobierz_rozdzielczosc(&screen_w, &screen_h);

    if (screen_w <= 0 || screen_h <= PASEK_WYS) {
        gui_zakoncz_aplikacje();
    }

    // Pulpit jest warstwa tla. Aplikacje GUI uzywaja wyzszego z_order.
    if (bws_utworz_warstwe(0, 0, screen_w, screen_h, 0) < 0) {
        gui_zakoncz_aplikacje();
    }

    gui_ustaw_przejecie_myszy(true);

    // Zasiej stan zegara, aby pierwsza iteracja nie wymuszala od razu
    // dodatkowej, niepotrzebnej kompozycji calej klatki.
    (void)pobierz_i_zaktualizuj_czas();
    odswiez_snapshot_okien();

    RysujPulpit(true);

    while (true) {
        bws_zdarzenie zdarzenie{};
        if (!gui_czekaj_na_zdarzenie(&zdarzenie)) continue;
        const uint64_t stara_sygnatura = sygnatura_okien();
        odswiez_snapshot_okien();
        const bool lifecycle =
            zdarzenie.typ >= BWS_ZDARZENIE_OKNO_UTWORZONE &&
            zdarzenie.typ <= BWS_ZDARZENIE_OKNO_TYTUL;
        if (lifecycle || sygnatura_okien() != stara_sygnatura) {
            RysujTaskbar();
            gui_odswiez();
        }
        const int mx = zdarzenie.x;
        const int my = zdarzenie.y;
        const bool klik = zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN;

        // Zegar jest nakladka jadra rysowana przez skladacz obrazu. Gdy czas
        // sie zmieni, wystarczy zlozyc nowa klatke; nie trzeba ponownie
        // rysowac wszystkich ikon i paska do warstwy pulpitu.
        if (pobierz_i_zaktualizuj_czas()) {
            gui_odswiez();
        }

        if (klik) {
            obsluz_klikniecie(mx, my);
        }

    }
}
