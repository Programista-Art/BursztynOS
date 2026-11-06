import re

with open("shell.cpp.bak", "r") as f:
    code = f.read()

# 1. Includes
includes_new = """#include "jadro/gui/bursztyn_gui.h"
#include "jadro/bws/bws_nowoczesne.h"
#include "jadro/gui/motyw.h"
#include "bursztyn_ui/czcionka.h"
#include "bursztyn_ui/rysowanie.h"
#include "bursztyn_ui/pasek_przewijania.h"

using namespace BursztynUI;
"""
code = code.replace('#include "jadro/gui/bursztyn_gui.h"', includes_new)

# 2. State and Constants
state_old = """constexpr int SHELL_X_START = 20;
constexpr int SHELL_Y_START = 20;
constexpr int SHELL_W = 660;
constexpr int SHELL_H = 360;
constexpr int SHELL_TITLE_H = 28;
constexpr int SHELL_TEXT_X = 8;
constexpr int SHELL_TEXT_Y = 34;
constexpr int SHELL_LINE_H = 16;
constexpr int SHELL_COLS = 70;
constexpr int SHELL_ROWS = 19;
constexpr size_t VISUAL_COLUMNS = static_cast<size_t>(SHELL_COLS);"""

state_new = """constexpr int MAXSCROLLBACKLINES = 1024;
constexpr int SCROLLBAR_WIDTH = 12;
constexpr int INPUT_LINE_HEIGHT = 30;

struct ShellLine {
    char tekst[256];
    int len;
};

ShellLine g_linie[MAXSCROLLBACKLINES];
int g_liczba_linii = 1;
int g_pierwsza_linia = 0;
int g_scroll_offset = 0;
bool g_scrolling = false;

char* g_obecny_bufor_wejscia = nullptr;
size_t g_obecny_bufor_dlugosc = 0;

PasekPrzewijania* g_pasek_przewijania = nullptr;"""

code = code.replace(state_old, state_new)

# Remove shell_ekran variables
code = code.replace("char shell_ekran[SHELL_ROWS][SHELL_COLS + 1] = {};\nint shell_wiersz = 0;\nint shell_kolumna = 0;", "")
code = code.replace("int shell_x = SHELL_X_START;\nint shell_y = SHELL_Y_START;\nint shell_w = SHELL_W;\nint shell_h = SHELL_H;", "int shell_x = 20;\nint shell_y = 20;\nint shell_w = 700;\nint shell_h = 450;")
code = code.replace("int shell_restore_x = SHELL_X_START;\nint shell_restore_y = SHELL_Y_START;\nint shell_restore_w = SHELL_W;\nint shell_restore_h = SHELL_H;", "int shell_restore_x = 20;\nint shell_restore_y = 20;\nint shell_restore_w = 700;\nint shell_restore_h = 450;")

# 3. Drawing functions
draw_old = code[code.find("void shell_dopisz_znak(char c);"):code.find("void wyzeruj(")]

draw_new = """void dodaj_nowa_linie() {
    if (g_liczba_linii < MAXSCROLLBACKLINES) {
        g_liczba_linii++;
    } else {
        g_pierwsza_linia = (g_pierwsza_linia + 1) % MAXSCROLLBACKLINES;
    }
    int act = (g_pierwsza_linia + g_liczba_linii - 1) % MAXSCROLLBACKLINES;
    g_linie[act].len = 0;
    g_linie[act].tekst[0] = '\\0';
}

void wpisz_do_bufora(uint32_t cp) {
    if (cp == '\\n' || cp == '\\r') {
        if (cp == '\\n') dodaj_nowa_linie();
        return;
    }
    char buf[5];
    size_t bajty = CzcionkaUI::zakoduj_utf8(cp, buf);
    
    int act = (g_pierwsza_linia + g_liczba_linii - 1) % MAXSCROLLBACKLINES;
    if (g_linie[act].len + bajty > 250) {
        dodaj_nowa_linie();
        act = (g_pierwsza_linia + g_liczba_linii - 1) % MAXSCROLLBACKLINES;
    }
    for (size_t i = 0; i < bajty; ++i) {
        g_linie[act].tekst[g_linie[act].len++] = buf[i];
    }
    g_linie[act].tekst[g_linie[act].len] = '\\0';
}

void shell_rysuj_terminal() {
    if (shell_minimized) return;

    motyw_inicjalizuj();
    const BursztynMotyw* motyw = motyw_pobierz_aktywny();
    if (!motyw) motyw = &g_domyslny_motyw_fallback;

    uint32_t tlo = motyw->oknotlo;
    uint32_t ramka = motyw->ramka;
    uint32_t tekst_kolor = motyw->tekst;
    uint32_t akcent = motyw->akcent;

    KontekstRysowania ctx;
    ctx.dodaj_przyciecie(Prostokat(shell_x, shell_y, shell_w, shell_h));

    ctx.rysuj_prostokat(Prostokat(shell_x, shell_y, shell_w, shell_h), tlo);

    int input_y = shell_y + shell_h - INPUT_LINE_HEIGHT;
    ctx.rysuj_linie_pozioma(shell_x, input_y, shell_w, ramka);

    int max_linii = (shell_h - INPUT_LINE_HEIGHT - 8) / 16;
    if (max_linii < 1) max_linii = 1;

    if (g_pasek_przewijania) {
        g_pasek_przewijania->ustaw_geometrie(shell_w - SCROLLBAR_WIDTH - 2, 4, SCROLLBAR_WIDTH, shell_h - INPUT_LINE_HEIGHT - 8);
        g_pasek_przewijania->ustaw_rozmiar_strony(max_linii);
        int max_scroll = g_liczba_linii - max_linii;
        if (max_scroll < 0) max_scroll = 0;
        g_pasek_przewijania->ustaw_zakres(0, max_scroll);
        
        if (!g_scrolling) {
            g_pasek_przewijania->ustaw_wartosc(max_scroll);
        }
        
        ctx.dodaj_przesuniecie(Punkt(shell_x, shell_y));
        g_pasek_przewijania->rysuj(ctx);
        ctx.zdejmij_przesuniecie();
        
        g_scroll_offset = max_scroll - g_pasek_przewijania->wartosc();
    }

    int pierwsza_do_rysowania = g_liczba_linii - max_linii - g_scroll_offset;
    if (pierwsza_do_rysowania < 0) pierwsza_do_rysowania = 0;

    int text_x = shell_x + 8;
    int cur_y = shell_y + 4;
    
    for (int i = 0; i < max_linii; ++i) {
        int idx = pierwsza_do_rysowania + i;
        if (idx >= g_liczba_linii) break;
        int real_idx = (g_pierwsza_linia + idx) % MAXSCROLLBACKLINES;
        
        if (g_linie[real_idx].len > 0) {
            gui_wypisz_tekst_kolor_skala(text_x, cur_y, tekst_kolor, 100, g_linie[real_idx].tekst);
        }
        cur_y += 16;
    }

    int input_text_x = shell_x + 8;
    int input_text_y = input_y + 7;
    gui_wypisz_tekst_kolor_skala(input_text_x, input_text_y, akcent, 100, "powłoka> ");
    int p_szer = CzcionkaUI::zmierz_tekst("powłoka> ");
    input_text_x += p_szer;

    if (g_obecny_bufor_wejscia) {
        gui_wypisz_tekst_kolor_skala(input_text_x, input_text_y, tekst_kolor, 100, g_obecny_bufor_wejscia);
        int wej_szer = CzcionkaUI::zmierz_tekst(g_obecny_bufor_wejscia);
        ctx.rysuj_prostokat(Prostokat(input_text_x + wej_szer + 2, input_text_y, 2, 16), tekst_kolor);
    }
    
    ctx.zdejmij_przyciecie();
}

void shell_wypisz(const char* tekst) {
    if (!tekst) return;
    size_t i = 0;
    while (tekst[i] != '\\0') {
        uint32_t cp = 0;
        size_t zuzyte = CzcionkaUI::dekoduj_utf8(tekst, i, &cp);
        if (zuzyte == 0) { i++; continue; }
        wpisz_do_bufora(cp);
        i += zuzyte;
    }
    shell_rysuj_terminal();
}

void wypisz_znak(char c) {
    char buf[2] = {c, '\\0'};
    shell_wypisz(buf);
}

void usun_znak_wejscia_wizualnie() {
}

void odtworz_widoczny_ogon_wejscia(const char* tekst,size_t len){
}
"""
code = code.replace(draw_old, draw_new)

# 4. Input loop
in_old = code[code.find("char pobierz_znak_blokujaco() {"):code.find("/* =========================================================================\n * 5. PARSER WIERSZA POLECEN")]

in_new = """char pobierz_znak_blokujaco() {
    for (;;) {
        bws_zdarzenie e{};
        if (!gui_czekaj_na_zdarzenie(&e)) continue;
        
        if (e.typ == BWS_ZDARZENIE_EKRAN) {
            gui_pobierz_rozdzielczosc(&shell_screen_w, &shell_screen_h);
            GuiMonitorInfo monitor{};
            if (gui_pobierz_monitor(0, &monitor)) {
                shell_work_x=monitor.work_x; shell_work_y=monitor.work_y;
                shell_work_w=monitor.work_szer; shell_work_h=monitor.work_wys;
            }
            if (shell_maximized) {
                shell_x=shell_work_x; shell_y=shell_work_y;
                shell_w=shell_work_w; shell_h=shell_work_h;
                gui_ustaw_geometrie_okna(shell_x,shell_y,shell_w,shell_h);
            }
            shell_rysuj_terminal();
            continue;
        }
        if (e.typ == BWS_ZDARZENIE_FOCUS && shell_minimized) shell_minimized = false;
        if (e.typ == BWS_ZDARZENIE_ZAMKNIJ) gui_zakoncz_aplikacje();
        
        if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN || e.typ == BWS_ZDARZENIE_MYSZ_RUCH || e.typ == BWS_ZDARZENIE_MYSZ_UP) {
            gui_akcja_belki akcja = gui_hit_test_belki(e.x, e.y, shell_x, shell_y, shell_w);
            if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN && akcja == GUI_BELKA_ZAMKNIJ) gui_zakoncz_aplikacje();
            
            if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN && akcja == GUI_BELKA_MINIMALIZUJ) {
                shell_minimized = gui_minimalizuj_okno();
                gui_ustaw_capture_myszy(false);
                continue;
            }
            if (e.typ == BWS_ZDARZENIE_MYSZ_DOWN && akcja == GUI_BELKA_MAKSYMALIZUJ) {
                if (!shell_maximized) {
                    shell_restore_x=shell_x; shell_restore_y=shell_y;
                    shell_restore_w=shell_w; shell_restore_h=shell_h;
                    shell_x=shell_work_x; shell_y=shell_work_y;
                    shell_w=shell_work_w; shell_h=shell_work_h;
                    shell_maximized=true;
                } else {
                    shell_x=shell_restore_x; shell_y=shell_restore_y;
                    shell_w=shell_restore_w; shell_h=shell_restore_h;
                    shell_maximized=false;
                }
                gui_ustaw_geometrie_okna(shell_x, shell_y, shell_w, shell_h);
                shell_rysuj_terminal();
                continue;
            }

            if (g_pasek_przewijania && e.x >= shell_x && e.y >= shell_y) {
                ZdarzenieMyszki mz{};
                mz.typ = e.typ == BWS_ZDARZENIE_MYSZ_DOWN ? TypZdarzenia::WcisniecieMyszy :
                         e.typ == BWS_ZDARZENIE_MYSZ_UP ? TypZdarzenia::ZwolnienieMyszy : TypZdarzenia::RuchMyszy;
                mz.x = e.x - shell_x;
                mz.y = e.y - shell_y;
                mz.przycisk = (e.kod == 1) ? 1 : 0;
                
                if (mz.typ == TypZdarzenia::WcisniecieMyszy) g_pasek_przewijania->przy_wcisnieciu_myszy(mz);
                else if (mz.typ == TypZdarzenia::ZwolnienieMyszy) g_pasek_przewijania->przy_zwolnieniu_myszy(mz);
                else if (mz.typ == TypZdarzenia::RuchMyszy) g_pasek_przewijania->przy_ruchu_myszy(mz);
                
                if (g_pasek_przewijania->czy_brudna()) {
                    g_scrolling = true;
                    shell_rysuj_terminal();
                    g_pasek_przewijania->uniewaznij();
                }
            }
            continue;
        }

        if (e.typ == BWS_ZDARZENIE_MYSZ_KOLO && g_pasek_przewijania) {
            ZdarzenieRolki zr{};
            zr.typ = TypZdarzenia::RolkaMyszy;
            zr.delta_y = (e.kod > 0) ? 1 : -1;
            g_pasek_przewijania->obsluz_rolke(zr);
            g_scrolling = true;
            shell_rysuj_terminal();
            continue;
        }

        if (e.typ == BWS_ZDARZENIE_KLAWISZ && e.kod != 0) {
            if (g_pasek_przewijania) {
                ZdarzenieKlawiatury zk{};
                zk.typ = TypZdarzenia::KlawiszWcisniety;
                zk.kod_klawisza = e.kod;
                g_pasek_przewijania->przy_wcisnieciu_klawisza(zk);
                if (g_pasek_przewijania->czy_brudna()) {
                    g_scrolling = true;
                    shell_rysuj_terminal();
                    g_pasek_przewijania->uniewaznij();
                    if (zk.obsluzone) continue;
                }
                if (zk.obsluzone) {
                    g_scrolling = true;
                    shell_rysuj_terminal();
                    continue;
                }
            }

            if ((e.kod & 0xFF000000) == 0) {
                return static_cast<char>(e.kod & 0xFF);
            }
            return static_cast<char>(e.kod);
        }
    }
}

bool pobierz_linie(char* bufor, size_t pojemnosc) {
    if (!bufor || pojemnosc < 2) return false;
    size_t pozycja = 0;
    bool przepelnienie = false;
    bufor[0] = '\\0';
    g_scrolling = false;
    
    for (;;) {
        g_obecny_bufor_wejscia = bufor;
        shell_rysuj_terminal();
        
        char c = pobierz_znak_blokujaco();
        
        if (c == '\\n' || c == '\\r') {
            bufor[pozycja] = '\\0';
            g_obecny_bufor_wejscia = nullptr;
            wypisz("powłoka> ");
            wypisz(bufor);
            wypisz("\\n");
            g_scrolling = false;
            return !przepelnienie;
        }
        
        if (c == '\\b' || static_cast<uint8_t>(c) == 0x7FU) {
            if (!przepelnienie && pozycja > 0) {
                --pozycja;
                while (pozycja > 0 && (static_cast<uint8_t>(bufor[pozycja]) & 0xC0U) == 0x80U) {
                    --pozycja;
                }
                bufor[pozycja] = '\\0';
            }
            continue;
        }
        
        if (!przepelnienie) {
            if (pozycja + 1 < pojemnosc) {
                bufor[pozycja++] = c;
                bufor[pozycja] = '\\0';
            } else {
                przepelnienie = true;
            }
        }
    }
}
"""
code = code.replace(in_old, in_new)

# 5. Main loop
start_old = code[code.find("void _start() {"):]

start_new = """void _start() {
    gui_legacy_wlacz();
    gui_pobierz_rozdzielczosc(&shell_screen_w, &shell_screen_h);
    GuiMonitorInfo monitor{};
    if (gui_pobierz_monitor(0, &monitor)) {
        shell_work_x = monitor.work_x;
        shell_work_y = monitor.work_y;
        shell_work_w = monitor.work_szer;
        shell_work_h = monitor.work_wys;
    } else {
        shell_work_w = shell_screen_w;
        shell_work_h = shell_screen_h;
    }
    
    if (bws_utworz_warstwe(shell_x, shell_y, shell_w, shell_h, 10) < 0)
        gui_zakoncz_aplikacje();
    gui_ustaw_tytul_okna("Powłoka Bursztynowa");
    gui_ustaw_przejecie_myszy(true);
    
    motyw_inicjalizuj();
    g_pasek_przewijania = new PasekPrzewijania();
    g_pasek_przewijania->ustaw_orientacje(Orientacja::Pionowa);

    wypisz(
        "\\n"
        "==================================================\\n"
        " Powłoka Bursztynowa bsh v3.0 (Unicode)\\n"
        " Autor: Dymitr Wygowski\\n"
        " Ring 3 / BWS / BSP2 / PZB\\n"
        "==================================================\\n"
        "Wpisz 'pomoc', aby zobaczyć polecenia.\\n"
    );

    char linia[SHELL_INPUT_CAPACITY] = {};

    for (;;) {
        const bool cala_linia = pobierz_linie(linia, sizeof(linia));

        if (!cala_linia) {
            wypisz("Błąd: polecenie przekracza limit 2047 bajtów i zostało odrzucone.\\n");
            continue;
        }
        if (linia[0] == '\\0') {
            continue;
        }

        Argumenty argumenty{};
        if(!kopiuj_tekst(komenda_dla_historii, sizeof(komenda_dla_historii), linia)){
            wypisz("Błąd: nie można zapisać komendy w historii.\\n");
            continue;
        }

        if (!parse_argumenty(linia, &argumenty)) {
            wypisz("Błąd składni: za dużo argumentów lub brakuje zamykającego cudzysłowu.\\n");
            continue;
        }
        if (argumenty.liczba == 0) continue;

        dodaj_do_historii(komenda_dla_historii);
        wykonaj_polecenie(argumenty);
    }
}
"""

code = code.replace(start_old, start_new)

# 6. Strings and Aliases
pomoc_start = code.find("void wypisz_pomoc() {")
pomoc_end = code.find("}\\n\\n} // namespace", pomoc_start)
if pomoc_end == -1: pomoc_end = code.find("}\\n\\n/*", pomoc_start)

new_pomoc = """void wypisz_pomoc() {
    wypisz(
        "Dostępne polecenia:\\n\\n"
        "  Sieć\\n"
        "    ping <IPv4|domena>           Sprawdza dostępność hosta przez ICMP.\\n"
        "    pobierz <host> <url> <plik>  Pobiera plik HTTP i zapisuje go w BSP2.\\n"
        "\\n"
        "  Pliki i katalogi\\n"
        "    utworz <plik>                Tworzy pusty plik.\\n"
        "    zapisz <plik> <tekst>        Zapisuje tekst do pliku.\\n"
        "    czytaj <plik>                Wyświetla zawartość pliku.\\n"
        "    pliki [katalog]              Wyświetla zawartość katalogu.\\n"
        "    usun <ścieżka>               Usuwa plik lub pusty katalog.\\n"
        "    zmiennazwe <ścieżka> <nazwa> Zmienia nazwę pliku lub katalogu.\\n"
        "\\n"
        "  Powłoka\\n"
        "    gdzie                        Wyświetla bieżący katalog.\\n"
        "    historia                     Wyświetla historię poleceń.\\n"
        "    czysc                        Czyści terminal.\\n"
        "    dzwiek                       Odtwarza test HDA 880 Hz.\\n"
        "    losuj                        Losuje liczbę.\\n"
        "    pisz <tekst>                 Wyświetla tekst.\\n"
        "\\n"
        "  System\\n"
        "    notatnik                     Uruchamia Notatnik.\\n"
        "    kalkulator                   Uruchamia Kalkulator.\\n"
        "    przegladarka                 Uruchamia przeglądarkę Husarz.\\n"
        "    uruchom <plik.bur>           Uruchamia program .bur.\\n"
        "    pulpit | wyjdz | exit        Kończy powłokę.\\n"
        "    czas                         Pokazuje czas RTC.\\n"
        "    system                       Informacje o systemie.\\n"
        "    wersja                       Pokazuje aktualną wersję systemu.\\n"
        "    aktualizacja [sprawdz]       Pokazuje wersję i stan aktualizacji.\\n"
        "    pci                          Wyświetla /logi/pci.txt.\\n"
        "    pomoc                        Pokazuje ten ekran pomocy.\\n"
    );
}"""

if pomoc_start != -1 and pomoc_end != -1:
    code = code[:pomoc_start] + new_pomoc + "\\n" + code[pomoc_end+1:]

code = code.replace('"usun")', '"usun") || tekst_rowny(cmd, "usuń")')
code = code.replace('"czysc")', '"czysc") || tekst_rowny(cmd, "czyść")')
code = code.replace('"zmien_nazwe")', '"zmien_nazwe") || tekst_rowny(cmd, "zmiennazwe") || tekst_rowny(cmd, "zmieńnazwę")')

replacements = {
    "Blad: polecenie przekracza limit": "Błąd: polecenie przekracza limit",
    "zostalo odrzucone": "zostało odrzucone",
    "Blad: nie mozna zapisac komendy w bounded historii": "Błąd: nie można zapisać komendy w historii",
    "Blad skladni:": "Błąd składni:",
    "za duzo argumentow": "za dużo argumentów",
    "brakuje zamykajacego cudzyslowu": "brakuje zamykającego cudzysłowu",
    "Nieprawidlowy adres IPv4": "Nieprawidłowy adres IPv4",
    "Blad: nie mozna polaczyc sie z DNS": "Błąd: nie można połączyć się z serwerem DNS",
    "Blad serwera DNS": "Błąd serwera DNS",
    "Nie znaleziono domeny": "Nie znaleziono domeny",
    "Blad otwarcia socketa": "Błąd otwarcia gniazda",
    "Blad wysylania ICMP": "Błąd wysyłania zapytania ICMP",
    "Brak odpowiedzi": "Brak odpowiedzi",
    "Blad odczytu": "Błąd odczytu",
    "Wysylanie HTTP GET": "Wysyłanie HTTP GET",
    "Blad wysylania HTTP": "Błąd wysyłania żądania HTTP",
    "Odbieranie danych": "Odbieranie danych",
    "Zapisywanie pliku": "Zapisywanie pliku",
    "Plik HTTP zapisany:": "Plik pobrany i zapisany jako:",
    "Blad: nie mozna utworzyc pliku": "Błąd: nie można utworzyć pliku",
    "Blad: nie mozna usunac pliku/katalogu": "Błąd: nie można usunąć",
    "Blad: nie mozna zmienic nazwy": "Błąd: nie można zmienić nazwy",
    "Skladnia:": "Składnia:",
    "Nie znaleziono polecenia": "Nie znaleziono polecenia",
    "Aby zobaczyc pomoc, wpisz": "Aby zobaczyć pomoc, wpisz",
    "Katalog": "Katalog",
    "Czas RTC": "Czas RTC",
    "System": "System",
    "Wersja": "Wersja"
}

for k, v in replacements.items():
    code = code.replace(k, v)

with open("shell.cpp", "w") as f:
    f.write(code)

print("Patch complete!")
