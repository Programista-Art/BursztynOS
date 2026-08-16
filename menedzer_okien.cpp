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

const char* const SCIEZKA_POWLOKA = "/shell.bur";
const char* const SCIEZKA_NOTATNIK = "/programy/notatnik.cebula/notatnik.bur";
const char* const SCIEZKA_KALKULATOR = "/programy/kalkulator.cebula/kalkulator.bur";
const char* const SCIEZKA_HUSSAR = "/programy/przegladarka.cebula/przegladarka.bur";

int screen_w = 1024;
int screen_h = 768;
bool menu_start_otwarte = false;
char ostatni_czas[BUFOR_CZASU] = {};

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

void RysujPulpit(bool wymus_pelne_odswiezenie) {
    if (wymus_pelne_odswiezenie) {
        // Dla procesu warstwowego BWS 19 zeruje prywatna warstwe.
        gui_odswiez_pulpit();
    }

    // Pasek zadan.
    gui_rysuj_prostokat(0, screen_h - PASEK_WYS,
                        screen_w, PASEK_WYS, 0x001A0B00);
    gui_rysuj_prostokat(0, screen_h - PASEK_WYS,
                        screen_w, 2, 0x00E58A00);

    // Przycisk Menu.
    gui_rysuj_prostokat(PRZYCISK_MENU_X, screen_h - 35,
                        PRZYCISK_MENU_W, PRZYCISK_MENU_H, 0x00E58A00);
    rysuj_tekst_wysrodkowany(PRZYCISK_MENU_X, screen_h - 35,
                             PRZYCISK_MENU_W, PRZYCISK_MENU_H,
                             1, 0x001A0B00, "Menu");

    // Skroty na pasku zadan.
    gui_rysuj_prostokat(100, screen_h - 36, SKROT_W, SKROT_H, 0x00FFBF00);
    rysuj_tekst_wysrodkowany(100, screen_h - 36, SKROT_W, SKROT_H,
                             1, 0x00000000, "N");

    gui_rysuj_prostokat(140, screen_h - 36, SKROT_W, SKROT_H, 0x008A5A00);
    rysuj_tekst_wysrodkowany(140, screen_h - 36, SKROT_W, SKROT_H,
                             1, 0x00FFFFFF, "+-");

    gui_rysuj_prostokat(180, screen_h - 36, SKROT_W, SKROT_H, 0x000078D7);
    rysuj_tekst_wysrodkowany(180, screen_h - 36, SKROT_W, SKROT_H,
                             1, 0x00FFFFFF, "W");

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
                             1, 0x00FFFFFF, "Hussar");

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
            "> Przegladarka Hussar",
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
    menu_start_otwarte = false;
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

void uruchom_powloke() {
    zamknij_menu_i_odtworz_pulpit();

    // Powloka korzysta z dotychczasowego trybu terminalowego Bursztyna.
    // Zachowujemy zgodnosc z poprzednim menedzerem okien.
    gui_ustaw_przejecie_myszy(false);
    uint64_t wynik = bws_wywolaj(10, (uint64_t)SCIEZKA_POWLOKA);

    // Jezeli uruchomienie sie nie powiodlo, pulpit musi odzyskac tryb GUI.
    if (wynik == 0) {
        gui_ustaw_przejecie_myszy(true);
        RysujPulpit(true);
    }
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
            uruchom_program_gui(SCIEZKA_NOTATNIK);
            break;
        case 2:
            uruchom_program_gui(SCIEZKA_KALKULATOR);
            break;
        case 3:
            uruchom_program_gui(SCIEZKA_HUSSAR);
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
        uruchom_program_gui(SCIEZKA_NOTATNIK);
        return;
    }

    if (punkt_w_prostokacie(mx, my, 130, 50, IKONA_W, IKONA_H)) {
        uruchom_program_gui(SCIEZKA_KALKULATOR);
        return;
    }

    if (punkt_w_prostokacie(mx, my, 210, 50, IKONA_W, IKONA_H)) {
        uruchom_program_gui(SCIEZKA_HUSSAR);
        return;
    }

    // Skroty na pasku zadan.
    if (punkt_w_prostokacie(mx, my, 100, screen_h - 36, SKROT_W, SKROT_H)) {
        uruchom_program_gui(SCIEZKA_NOTATNIK);
        return;
    }

    if (punkt_w_prostokacie(mx, my, 140, screen_h - 36, SKROT_W, SKROT_H)) {
        uruchom_program_gui(SCIEZKA_KALKULATOR);
        return;
    }

    if (punkt_w_prostokacie(mx, my, 180, screen_h - 36, SKROT_W, SKROT_H)) {
        uruchom_program_gui(SCIEZKA_HUSSAR);
        return;
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

    RysujPulpit(true);

    uint8_t poprz_przycisk = 0;

    while (true) {
        int mx = 0;
        int my = 0;
        uint8_t mb = 0;

        gui_pobierz_mysz(&mx, &my, &mb);

        const bool lewy_wcisniety = (mb & 0x01) != 0;
        const bool poprzednio_wcisniety = (poprz_przycisk & 0x01) != 0;
        const bool klik = lewy_wcisniety && !poprzednio_wcisniety;

        // Zegar jest nakladka jadra rysowana przez skladacz obrazu. Gdy czas
        // sie zmieni, wystarczy zlozyc nowa klatke; nie trzeba ponownie
        // rysowac wszystkich ikon i paska do warstwy pulpitu.
        if (pobierz_i_zaktualizuj_czas()) {
            gui_odswiez();
        }

        if (klik) {
            obsluz_klikniecie(mx, my);
        }

        poprz_przycisk = mb;

        // Nie jest to pelny scheduler-yield, ale ogranicza koszt goracej
        // petli pollingu do czasu dodania blokujacego BWS zdarzen GUI.
        asm volatile("pause");
    }
}
