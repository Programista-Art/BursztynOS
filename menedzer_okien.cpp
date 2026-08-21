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
constexpr int MENU_WYS = 210;
constexpr int MENU_GORNY_MARGINES = 10;
constexpr int MENU_ELEMENT_WYS = 25;
constexpr int MENU_ELEMENTOW = 8;

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
constexpr int PULPIT_MAX_WPISOW = 128;
constexpr int PULPIT_LIST_CAP = 16384;
constexpr int PULPIT_NAZWA_CAP = 56;
constexpr int PULPIT_GRID_W = 82;
constexpr int PULPIT_GRID_H = 82;
constexpr int PULPIT_GRID_Y = 140;
constexpr int PULPIT_REFRESH_TICKS = 3;

const char* const SCIEZKA_POWLOKA = "/shell.bur";
const char* const SCIEZKA_EKSPLORATOR =
    "/programy/eksplorator.cebula/eksplorator-plikow.bur";
const char* const SCIEZKA_NOTATNIK = "/programy/notatnik.cebula/notatnik.bur";
const char* const SCIEZKA_KALKULATOR = "/programy/kalkulator.cebula/kalkulator.bur";
const char* const SCIEZKA_HUSSAR = "/programy/przegladarka.cebula/przegladarka.bur";
const char* const SCIEZKA_TEST = "/programy/test.cebula/test.bur";
const char* const SCIEZKA_PULPIT = "/uzytkownicy/Pulpit";

struct WpisPulpitu {
    char nazwa[PULPIT_NAZWA_CAP];
    bool folder;
};

int screen_w = 1024;
int screen_h = 768;
bool menu_start_otwarte = false;
char ostatni_czas[BUFOR_CZASU] = {};
GuiOknoInfo okna_taskbara[SKLADACZ_MAKS_WARSTW] = {};
uint32_t liczba_okien_taskbara = 0;
WpisPulpitu wpisy_pulpitu[PULPIT_MAX_WPISOW] = {};
char lista_pulpitu[PULPIT_LIST_CAP] = {};
int liczba_wpisow_pulpitu = 0;
int zaznaczony_wpis_pulpitu = -1;
uint64_t hash_pulpitu = 0;
bool hash_pulpitu_wazny = false;
bool pulpit_drop_hover = false;
int pulpit_refresh_ticks = 0;
BwsCelDrop cele_drop_pulpitu[BWS_DROP_CELE_MAX] = {};

enum class PulpitContextAction : uint8_t {
    NONE, OPEN, RUN, COPY, CUT, PASTE, REFRESH
};
struct PulpitContextItem { const char* label; PulpitContextAction action; };
bool pulpit_context_open = false;
int pulpit_context_x = 0;
int pulpit_context_y = 0;
int pulpit_context_w = 184;
int pulpit_context_count = 0;
int pulpit_context_hover = -1;
PulpitContextItem pulpit_context_items[8] = {};

bool punkt_w_prostokacie(int px, int py, int x, int y, int w, int h);
void wyzeruj_bufor(char* bufor, int rozmiar);

int dlugosc_limit(const char* tekst, int limit) {
    if (!tekst) return limit;
    for (int i = 0; i < limit; ++i) if (tekst[i] == '\0') return i;
    return limit;
}

bool kopiuj_tekst(char* cel, int pojemnosc, const char* zrodlo) {
    if (!cel || pojemnosc <= 0 || !zrodlo) return false;
    const int n = dlugosc_limit(zrodlo, pojemnosc);
    if (n >= pojemnosc) return false;
    for (int i = 0; i <= n; ++i) cel[i] = zrodlo[i];
    return true;
}

bool konczy_sie(const char* tekst, const char* sufiks) {
    const int n = dlugosc_limit(tekst, 512);
    const int s = dlugosc_limit(sufiks, 32);
    if (n >= 512 || s >= 32 || s > n) return false;
    for (int i = 0; i < s; ++i) if (tekst[n - s + i] != sufiks[i]) return false;
    return true;
}

bool sciezka_wpisu_pulpitu(int indeks, char* wynik, int pojemnosc) {
    if (!wynik || indeks < 0 || indeks >= liczba_wpisow_pulpitu) return false;
    const int baza = dlugosc_limit(SCIEZKA_PULPIT, pojemnosc);
    const int nazwa = dlugosc_limit(wpisy_pulpitu[indeks].nazwa, PULPIT_NAZWA_CAP);
    if (baza >= pojemnosc || nazwa >= PULPIT_NAZWA_CAP ||
        baza + 1 + nazwa + 1 > pojemnosc) return false;
    int out = 0;
    for (int i = 0; i < baza; ++i) wynik[out++] = SCIEZKA_PULPIT[i];
    wynik[out++] = '/';
    for (int i = 0; i < nazwa; ++i) wynik[out++] = wpisy_pulpitu[indeks].nazwa[i];
    wynik[out] = '\0';
    return true;
}

uint64_t hash_listy(const char* tekst, int n) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; i < n; ++i) {
        hash ^= static_cast<uint8_t>(tekst[i]);
        hash *= UINT64_C(1099511628211);
    }
    return hash ^ static_cast<uint64_t>(n);
}

bool odswiez_wpisy_pulpitu() {
    wyzeruj_bufor(lista_pulpitu, PULPIT_LIST_CAP);
    if (!wylistuj_katalog_uzytkownika(
            SCIEZKA_PULPIT, lista_pulpitu, PULPIT_LIST_CAP)) return false;
    const int length = dlugosc_limit(lista_pulpitu, PULPIT_LIST_CAP);
    if (length >= PULPIT_LIST_CAP || (length > 0 && lista_pulpitu[length - 1] != '\n'))
        return false;
    const uint64_t nowy_hash = hash_listy(lista_pulpitu, length);
    if (hash_pulpitu_wazny && nowy_hash == hash_pulpitu) return false;

    WpisPulpitu nowe[PULPIT_MAX_WPISOW] = {};
    int count = 0;
    int pos = 0;
    while (pos < length) {
        int end = pos;
        while (end < length && lista_pulpitu[end] != '\n') ++end;
        if (end - pos <= 7 || count >= PULPIT_MAX_WPISOW) return false;
        const bool folder = lista_pulpitu[pos] == '[' && lista_pulpitu[pos + 1] == 'K';
        const bool file = lista_pulpitu[pos] == '[' && lista_pulpitu[pos + 1] == 'P';
        const int name_length = end - pos - 7;
        if ((!folder && !file) || name_length <= 0 || name_length >= PULPIT_NAZWA_CAP)
            return false;
        nowe[count].folder = folder;
        for (int i = 0; i < name_length; ++i)
            nowe[count].nazwa[i] = lista_pulpitu[pos + 7 + i];
        ++count;
        pos = end + 1;
    }
    for (int i = 0; i < count; ++i) wpisy_pulpitu[i] = nowe[i];
    liczba_wpisow_pulpitu = count;
    if (zaznaczony_wpis_pulpitu >= count) zaznaczony_wpis_pulpitu = -1;
    hash_pulpitu = nowy_hash;
    hash_pulpitu_wazny = true;
    return true;
}

void prostokat_wpisu_pulpitu(int indeks, int* x, int* y) {
    int kolumny = (screen_w - 40) / PULPIT_GRID_W;
    if (kolumny < 1) kolumny = 1;
    if (x) *x = 30 + (indeks % kolumny) * PULPIT_GRID_W;
    if (y) *y = PULPIT_GRID_Y + (indeks / kolumny) * PULPIT_GRID_H;
}

int wpis_pulpitu_w_punkcie(int x, int y) {
    for (int i = 0; i < liczba_wpisow_pulpitu; ++i) {
        int px = 0, py = 0;
        prostokat_wpisu_pulpitu(i, &px, &py);
        if (punkt_w_prostokacie(x, y, px, py, 68, 72)) return i;
    }
    return -1;
}

void podpis_wpisu(const char* nazwa, char* wynik, int pojemnosc) {
    if (!wynik || pojemnosc < 5) return;
    int src = 0, out = 0, znaki = 0;
    while (nazwa[src] && znaki < 9 && out + 4 < pojemnosc) {
        const uint8_t first = static_cast<uint8_t>(nazwa[src]);
        int bytes = first < 0x80U ? 1 : ((first & 0xE0U) == 0xC0U ? 2 :
                    ((first & 0xF0U) == 0xE0U ? 3 : 4));
        for (int b = 0; b < bytes && nazwa[src] && out + 1 < pojemnosc; ++b)
            wynik[out++] = nazwa[src++];
        ++znaki;
    }
    if (nazwa[src] && out + 4 <= pojemnosc) {
        wynik[out++] = '.'; wynik[out++] = '.'; wynik[out++] = '.';
    }
    wynik[out] = '\0';
}

bool podwojny_klik(uint64_t previous, uint64_t current) {
    if (!previous || current < previous) return false;
    const uint64_t difference = current - previous;
    return current >= UINT64_C(1000000000)
        ? difference <= UINT64_C(500000000) : difference <= 50;
}

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

void zarejestruj_pulpit_jako_cel_drop() {
    for (uint32_t i = 0; i < BWS_DROP_CELE_MAX; ++i)
        cele_drop_pulpitu[i] = BwsCelDrop{};
    uint32_t count = 0;
    for (int i = 0; i < liczba_wpisow_pulpitu &&
         count + 1U < BWS_DROP_CELE_MAX; ++i) {
        if (!wpisy_pulpitu[i].folder) continue;
        int x = 0, y = 0;
        prostokat_wpisu_pulpitu(i, &x, &y);
        BwsCelDrop& cel = cele_drop_pulpitu[count];
        cel.x = x; cel.y = y; cel.szer = 68; cel.wys = 72;
        if (!sciezka_wpisu_pulpitu(i, cel.folder, sizeof(cel.folder))) continue;
        ++count;
    }
    BwsCelDrop& pulpit = cele_drop_pulpitu[count];
    pulpit.x = 0; pulpit.y = 0; pulpit.szer = screen_w;
    pulpit.wys = screen_h > PASEK_WYS ? screen_h - PASEK_WYS : 0;
    if (pulpit.wys > 0 && kopiuj_tekst(
            pulpit.folder, static_cast<int>(sizeof(pulpit.folder)), SCIEZKA_PULPIT))
        ++count;
    (void)gui_rejestruj_cele_drop(cele_drop_pulpitu, count);
}

void RysujWpisyPulpitu() {
    for (int i = 0; i < liczba_wpisow_pulpitu; ++i) {
        int x = 0, y = 0;
        prostokat_wpisu_pulpitu(i, &x, &y);
        if (y + 72 >= screen_h - PASEK_WYS) break;
        if (i == zaznaczony_wpis_pulpitu)
            gui_rysuj_prostokat(x, y, 68, 72, 0x00603800);
        uint32_t kolor = wpisy_pulpitu[i].folder ? 0x00FFBF00 : 0x00708090;
        if (konczy_sie(wpisy_pulpitu[i].nazwa, ".bur")) kolor = 0x0038B000;
        else if (konczy_sie(wpisy_pulpitu[i].nazwa, ".cebula")) kolor = 0x00D050A0;
        else if (konczy_sie(wpisy_pulpitu[i].nazwa, ".txt")) kolor = 0x004C9BE8;
        gui_rysuj_prostokat(x + 14, y + 4, 40, 40, kolor);
        if (wpisy_pulpitu[i].folder)
            gui_rysuj_prostokat(x + 18, y, 18, 8, kolor);
        char podpis[40] = {};
        podpis_wpisu(wpisy_pulpitu[i].nazwa, podpis, sizeof(podpis));
        rysuj_tekst_wysrodkowany(x, y + 49, 68, 18, 1, 0x00FFFFFF, podpis);
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
    //Test apka
    gui_rysuj_prostokat(290, 50, IKONA_W, IKONA_H, 0x00CC33CC);
    gui_rysuj_prostokat(292, 52, 44, 44, 0x00881188);
    rysuj_tekst_wysrodkowany(290, 65, IKONA_W, 16,
                             1, 0x00FFFFFF, "Test");
    rysuj_tekst_wysrodkowany(290, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Test");

    // Ikona Eksploratora Plikow.
    gui_rysuj_prostokat(370, 50, IKONA_W, IKONA_H, 0x00FFBF00);
    gui_rysuj_prostokat(372, 58, 44, 38, 0x00D59600);
    gui_rysuj_prostokat(376, 52, 20, 10, 0x00FFBF00);
    rysuj_tekst_wysrodkowany(370, 104, IKONA_W, 16,
                             1, 0x00FFFFFF, "Eksplorator");

    RysujWpisyPulpitu();


    if (menu_start_otwarte) {
        const int menu_y = screen_h - PASEK_WYS - MENU_WYS;

        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, MENU_WYS, 0x00301500);
        gui_rysuj_prostokat(MENU_X, menu_y, MENU_W, 1, 0x00E58A00);
        gui_rysuj_prostokat(MENU_X, menu_y, 1, MENU_WYS, 0x00E58A00);
        gui_rysuj_prostokat(MENU_X + MENU_W - 1, menu_y,
                            1, MENU_WYS, 0x00E58A00);

        const char* menu_elementy[MENU_ELEMENTOW] = {
            "> Powloka Bursztyna",
            "> Eksplorator Plików",
            "> Notatnik",
            "> Kalkulator",
            "> Przegladarka Husarz",
            "> Test",
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
    zarejestruj_pulpit_jako_cel_drop();
    gui_odswiez();
}

void zamknij_menu_i_odtworz_pulpit() {
    if (!menu_start_otwarte) return;
    const int menu_y = screen_h - PASEK_WYS - MENU_WYS;
    menu_start_otwarte = false;
    gui_ustaw_system_overlay(false, MENU_X, menu_y, MENU_W, MENU_WYS);
    RysujPulpit(true);
}

bool aktywuj_istniejace(const char* prefiks_tytulu);

void uruchom_program_gui(const char* sciezka) {
    if (!sciezka) return;

    zamknij_menu_i_odtworz_pulpit();

    // Nie zerujemy globalnego trybu Ring 3 przed aplikacja GUI. Obecna
    // implementacja w grafika.cpp uzywa jednej globalnej flagi, a aplikacje
    // GUI i tak ustawiaja ten tryb na true po starcie.
    gui_ustaw_przejecie_myszy(true);
    bws_wywolaj(10, (uint64_t)sciezka);
}

void otworz_wpis_pulpitu(int indeks) {
    char sciezka[512] = {};
    if (!sciezka_wpisu_pulpitu(indeks, sciezka, sizeof(sciezka))) return;
    if (wpisy_pulpitu[indeks].folder || konczy_sie(sciezka, ".cebula")) {
        zamknij_menu_i_odtworz_pulpit();
        gui_ustaw_przejecie_myszy(true);
        (void)uruchom_program_z_argumentem_uzytkownika(
            SCIEZKA_EKSPLORATOR, sciezka);
        (void)aktywuj_istniejace("Eksplorator Plików");
        return;
    }
    if (konczy_sie(sciezka, ".bur")) {
        (void)uruchom_program_uzytkownika(sciezka);
        return;
    }
    (void)otworz_plik_skojarzony(sciezka);
}

void dodaj_pulpit_context(const char* label, PulpitContextAction action) {
    if (pulpit_context_count >= 8) return;
    pulpit_context_items[pulpit_context_count++] = {label, action};
}

int pulpit_context_h() { return pulpit_context_count * 24 + 4; }

void rysuj_pulpit_context_row(int index) {
    if (index < 0 || index >= pulpit_context_count) return;
    const int y = pulpit_context_y + 2 + index * 24;
    gui_rysuj_prostokat(pulpit_context_x + 2, y,
                        pulpit_context_w - 4, 24,
                        index == pulpit_context_hover ? 0x00603800 : 0x00301500);
    gui_wypisz_tekst_kolor(pulpit_context_x + 10, y + 5,
                           0x00FFFFFF, pulpit_context_items[index].label);
}

void zamknij_pulpit_context() {
    if (!pulpit_context_open) return;
    (void)gui_ustaw_popup_aplikacji(false, 0, 0, 0, 0);
    pulpit_context_open = false;
    pulpit_context_count = 0;
    pulpit_context_hover = -1;
    zarejestruj_pulpit_jako_cel_drop();
    gui_odswiez();
}

bool otworz_pulpit_context(int x, int y, int wpis) {
    zamknij_pulpit_context();
    pulpit_context_count = 0;
    if (wpis >= 0 && wpis < liczba_wpisow_pulpitu) {
        dodaj_pulpit_context("Otwórz", PulpitContextAction::OPEN);
        if (konczy_sie(wpisy_pulpitu[wpis].nazwa, ".bur"))
            dodaj_pulpit_context("Uruchom", PulpitContextAction::RUN);
        dodaj_pulpit_context("Kopiuj", PulpitContextAction::COPY);
        dodaj_pulpit_context("Wytnij", PulpitContextAction::CUT);
    } else {
        dodaj_pulpit_context("Wklej", PulpitContextAction::PASTE);
        dodaj_pulpit_context("Odśwież", PulpitContextAction::REFRESH);
    }
    const int height = pulpit_context_h();
    pulpit_context_x = x;
    pulpit_context_y = y;
    if (pulpit_context_x + pulpit_context_w > screen_w)
        pulpit_context_x = screen_w - pulpit_context_w;
    if (pulpit_context_y + height > screen_h - PASEK_WYS)
        pulpit_context_y = screen_h - PASEK_WYS - height;
    if (pulpit_context_x < 0) pulpit_context_x = 0;
    if (pulpit_context_y < 0) pulpit_context_y = 0;
    pulpit_context_hover = -1;
    if (!gui_ustaw_popup_aplikacji(true, pulpit_context_x, pulpit_context_y,
                                   pulpit_context_w, height)) return false;
    pulpit_context_open = true;
    (void)gui_rejestruj_cele_drop(nullptr, 0);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y,
                        pulpit_context_w, height, 0x00301500);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y,
                        pulpit_context_w, 2, 0x00E58A00);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y,
                        2, height, 0x00E58A00);
    gui_rysuj_prostokat(pulpit_context_x + pulpit_context_w - 2,
                        pulpit_context_y, 2, height, 0x00794400);
    gui_rysuj_prostokat(pulpit_context_x, pulpit_context_y + height - 2,
                        pulpit_context_w, 2, 0x00794400);
    for (int i = 0; i < pulpit_context_count; ++i) rysuj_pulpit_context_row(i);
    gui_odswiez();
    return true;
}

PulpitContextAction pulpit_context_action(int x, int y) {
    if (!pulpit_context_open || !punkt_w_prostokacie(
            x, y, pulpit_context_x, pulpit_context_y,
            pulpit_context_w, pulpit_context_h())) return PulpitContextAction::NONE;
    const int index = (y - pulpit_context_y - 2) / 24;
    return index >= 0 && index < pulpit_context_count
        ? pulpit_context_items[index].action : PulpitContextAction::NONE;
}

void wykonaj_pulpit_context(PulpitContextAction action) {
    const int selected = zaznaczony_wpis_pulpitu;
    char source[512] = {};
    if (selected >= 0) (void)sciezka_wpisu_pulpitu(
        selected, source, static_cast<int>(sizeof(source)));
    zamknij_pulpit_context();
    if (action == PulpitContextAction::OPEN) otworz_wpis_pulpitu(selected);
    else if (action == PulpitContextAction::RUN && source[0])
        (void)uruchom_program_uzytkownika(source);
    else if (action == PulpitContextAction::COPY && source[0])
        (void)ustaw_schowek_plikow(source, BWS_SCHOWEK_COPY);
    else if (action == PulpitContextAction::CUT && source[0])
        (void)ustaw_schowek_plikow(source, BWS_SCHOWEK_CUT);
    else if (action == PulpitContextAction::PASTE) {
        BwsSchowekPlikow clipboard{};
        if (pobierz_schowek_plikow(&clipboard) &&
            clipboard.wersja == BWS_SCHOWEK_WERSJA) {
            bool ok = false;
            if (clipboard.operacja == BWS_SCHOWEK_COPY)
                ok = kopiuj_twor_uzytkownika(clipboard.sciezka, SCIEZKA_PULPIT);
            else if (clipboard.operacja == BWS_SCHOWEK_CUT) {
                ok = przenies_twor_uzytkownika(clipboard.sciezka, SCIEZKA_PULPIT);
                if (ok) (void)wyczysc_schowek_plikow(clipboard.generacja);
            }
        }
    }
    if (action == PulpitContextAction::PASTE ||
        action == PulpitContextAction::REFRESH) {
        hash_pulpitu_wazny = false;
        (void)odswiez_wpisy_pulpitu();
        RysujPulpit(true);
    } else {
        gui_odswiez();
    }
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
            uruchom_lub_aktywuj(SCIEZKA_EKSPLORATOR,
                                "Eksplorator Plików");
            break;
        case 2:
            uruchom_lub_aktywuj(SCIEZKA_NOTATNIK, "Notatnik");
            break;
        case 3:
            uruchom_lub_aktywuj(SCIEZKA_KALKULATOR, "Kalkulator");
            break;
        case 4:
            uruchom_lub_aktywuj(SCIEZKA_HUSSAR, "Husarz");
            break;
        case 5:
            uruchom_lub_aktywuj(SCIEZKA_TEST, "Test");
            break;
        case 6:
            wykonaj_restart();
            break;
        case 7:
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

    if (punkt_w_prostokacie(mx, my, 290, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_TEST, "Test");
        return;
    }

    if (punkt_w_prostokacie(mx, my, 370, 50, IKONA_W, IKONA_H)) {
        uruchom_lub_aktywuj(SCIEZKA_EKSPLORATOR, "Eksplorator Plików");
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
    (void)odswiez_wpisy_pulpitu();

    RysujPulpit(true);

    bool drag_candidate = false;
    bool dragging_file = false;
    int drag_index = -1;
    int drag_start_x = 0;
    int drag_start_y = 0;
    char drag_path[512] = {};
    int last_desktop_click = -1;
    uint64_t last_desktop_click_time = 0;

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

        if (zdarzenie.typ == BWS_ZDARZENIE_BLUR) {
            zamknij_pulpit_context();
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_PLIKI_ZMIENIONE) {
            zamknij_pulpit_context();
            if (odswiez_wpisy_pulpitu()) RysujPulpit(true);
            continue;
        }

        if (pulpit_context_open) {
            if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH) {
                int next = -1;
                if (punkt_w_prostokacie(mx, my, pulpit_context_x,
                                        pulpit_context_y, pulpit_context_w,
                                        pulpit_context_h())) {
                    next = (my - pulpit_context_y - 2) / 24;
                    if (next < 0 || next >= pulpit_context_count) next = -1;
                }
                if (next != pulpit_context_hover) {
                    const int old = pulpit_context_hover;
                    pulpit_context_hover = next;
                    if (old >= 0) rysuj_pulpit_context_row(old);
                    if (next >= 0) rysuj_pulpit_context_row(next);
                    gui_odswiez();
                }
                continue;
            }
            if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_DOWN) {
                const PulpitContextAction action = pulpit_context_action(mx, my);
                if (action == PulpitContextAction::NONE) zamknij_pulpit_context();
                else wykonaj_pulpit_context(action);
                continue;
            }
            if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN ||
                (zdarzenie.typ == BWS_ZDARZENIE_KLAWISZ &&
                 static_cast<char>(zdarzenie.kod) == '\x1B')) {
                zamknij_pulpit_context();
                continue;
            }
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_PRAWY_DOWN) {
            if (menu_start_otwarte) zamknij_menu_i_odtworz_pulpit();
            drag_candidate = false;
            dragging_file = false;
            gui_ustaw_capture_myszy(false);
            const int desktop_entry = wpis_pulpitu_w_punkcie(mx, my);
            zaznaczony_wpis_pulpitu = desktop_entry;
            RysujPulpit(true);
            if (my < screen_h - PASEK_WYS)
                (void)otworz_pulpit_context(mx, my, desktop_entry);
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_DRAG_HOVER) {
            /* Wizualny stan drag jest osobnym, paced overlayem skladacza.
             * Pulpit nie rysuje juz pelnoekranowej ramki nad taskbarem. */
            pulpit_drop_hover = true;
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_DRAG_LEAVE) {
            pulpit_drop_hover = false;
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_DRAG_DROP) {
            pulpit_drop_hover = false;
            hash_pulpitu_wazny = false;
            (void)odswiez_wpisy_pulpitu();
            RysujPulpit(true);
            continue;
        }

        const bool left_button = (zdarzenie.przyciski & 1U) != 0;
        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_RUCH &&
            drag_candidate && left_button) {
            const int dx = mx - drag_start_x;
            const int dy = my - drag_start_y;
            if (!dragging_file && (dx > 4 || dx < -4 || dy > 4 || dy < -4))
                dragging_file = true;
            if (dragging_file)
                (void)gui_aktualizuj_drag(drag_path, mx, my, false);
            continue;
        }
        if (zdarzenie.typ == BWS_ZDARZENIE_MYSZ_UP && drag_candidate) {
            if (dragging_file) {
                const BwsWynikDrop result = gui_aktualizuj_drag(
                    drag_path, mx, my, true);
                if (result == BWS_DROP_PRZENIESIONO) {
                    hash_pulpitu_wazny = false;
                    (void)odswiez_wpisy_pulpitu();
                    zaznaczony_wpis_pulpitu = -1;
                    RysujPulpit(true);
                }
            }
            drag_candidate = false;
            dragging_file = false;
            drag_index = -1;
            drag_path[0] = '\0';
            gui_ustaw_capture_myszy(false);
            continue;
        }

        if (zdarzenie.typ == BWS_ZDARZENIE_TIMER) {
            zarejestruj_pulpit_jako_cel_drop();
            if (!pulpit_context_open &&
                ++pulpit_refresh_ticks >= PULPIT_REFRESH_TICKS) {
                pulpit_refresh_ticks = 0;
                if (odswiez_wpisy_pulpitu()) RysujPulpit(true);
            }
        }

        // Zegar jest nakladka jadra rysowana przez skladacz obrazu. Gdy czas
        // sie zmieni, wystarczy zlozyc nowa klatke; nie trzeba ponownie
        // rysowac wszystkich ikon i paska do warstwy pulpitu.
        if (pobierz_i_zaktualizuj_czas()) {
            gui_odswiez();
        }

        if (klik) {
            const int desktop_entry = wpis_pulpitu_w_punkcie(mx, my);
            if (desktop_entry >= 0) {
                const bool double_click = desktop_entry == last_desktop_click &&
                    podwojny_klik(last_desktop_click_time, zdarzenie.timestamp);
                zaznaczony_wpis_pulpitu = desktop_entry;
                RysujPulpit(true);
                last_desktop_click = desktop_entry;
                last_desktop_click_time = zdarzenie.timestamp;
                drag_index = desktop_entry;
                drag_candidate = sciezka_wpisu_pulpitu(
                    drag_index, drag_path, sizeof(drag_path));
                dragging_file = false;
                drag_start_x = mx;
                drag_start_y = my;
                if (drag_candidate) gui_ustaw_capture_myszy(true);
                if (double_click) {
                    gui_ustaw_capture_myszy(false);
                    drag_candidate = false;
                    otworz_wpis_pulpitu(desktop_entry);
                    last_desktop_click = -1;
                    last_desktop_click_time = 0;
                }
                continue;
            }
            last_desktop_click = -1;
            last_desktop_click_time = 0;
            obsluz_klikniecie(mx, my);
        }

    }
}
