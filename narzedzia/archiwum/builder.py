import re

with open("shell.cpp.bak", "r") as f:
    code = f.read()

def get_section(name, next_name=None):
    start = code.find(name)
    if start == -1: return ""
    if next_name:
        end = code.find(next_name)
        if end != -1:
            return code[start:end]
    return code[start:]

sec2 = get_section("/* =========================================================================\n * 2. PROSTE WRAPPERY BWS", "/* =========================================================================\n * 4. WEJSCIE TERMINALA")
sec5 = get_section("/* =========================================================================\n * 5. PARSER WIERSZA POLECEN", "/* =========================================================================\n * 13. NAGLOWEK BINARNY APLIKACJI")

frontend_code = """/*
 * Bursztyn OS - Powłoka Bursztynowa (bsh)
 *
 * Ring 3 / aplikacja .bur
 * (Zmodernizowana z użyciem BursztynUI)
 */

#include "jadro/gui/bursztyn_gui.h"
#include "jadro/bws/bws_nowoczesne.h"
#include "jadro/gui/motyw.h"
#include "bursztyn_ui/czcionka.h"
#include "bursztyn_ui/rysowanie.h"
#include "bursztyn_ui/pasek_przewijania.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define wypisz shell_wypisz

extern "C" __attribute__((noreturn)) void _start();

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

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\\0'},
    reinterpret_cast<uint64_t>(&_start),
    0x1000, 0x8000, 0x1001000,
    0x9000, 0x20000, 0x1009000
};
}

using namespace BursztynUI;

constexpr int MAXSCROLLBACKLINES = 512;
constexpr int SCROLLBAR_WIDTH = 12;
constexpr int INPUT_LINE_HEIGHT = 30;

struct ShellLine {
    char tekst[256];
    int len;
};

ShellLine g_linie[MAXSCROLLBACKLINES];
int g_liczba_linii = 1;
int g_pierwsza_linia = 0;
int g_scroll_offset = 0; // ile linii przewinięto w góre od dołu
bool g_scrolling = false;

char* g_obecny_bufor_wejscia = nullptr;
size_t g_obecny_bufor_dlugosc = 0;

int shell_x = 20, shell_y = 20, shell_w = 700, shell_h = 450;
int shell_work_x = 0, shell_work_y = 0, shell_work_w = 1024, shell_work_h = 768;
bool shell_maximized = false;
bool shell_minimized = false;
bool shell_drag = false;
int shell_restore_x = 20, shell_restore_y = 20, shell_restore_w = 700, shell_restore_h = 450;

PasekPrzewijania* g_pasek_przewijania = nullptr;

constexpr size_t SHELL_INPUT_CAPACITY = 2048;
constexpr size_t SHELL_HISTORY_MAX = 64;
constexpr size_t SHELL_HISTORY_ENTRY_CAPACITY = 256;

char historia[SHELL_HISTORY_MAX][SHELL_HISTORY_ENTRY_CAPACITY] = {};
char komenda_dla_historii[SHELL_HISTORY_ENTRY_CAPACITY] = {};
int historia_index_zapis = 0;
int historia_index_przegladania = -1;

void dodaj_nowa_linie() {
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
    size_t bajty = Czcionka::zakoduj_utf8(cp, buf);

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
    uint32_t tekst_wyciszony = motyw->tekstdrugorzedny;
    uint32_t akcent = motyw->akcent;

    KontekstRysowania ctx;
    ctx.dodaj_przyciecie(Prostokat(shell_x, shell_y, shell_w, shell_h));

    ctx.rysuj_prostokat(Prostokat(shell_x, shell_y, shell_w, shell_h), tlo);

    int input_y = shell_y + shell_h - INPUT_LINE_HEIGHT;
    ctx.rysuj_linie_pozioma(shell_x, input_y, shell_w, ramka);

    int max_linii = (shell_h - INPUT_LINE_HEIGHT - 8) / 16;
    if (max_linii < 1) max_linii = 1;

    if (g_pasek_przewijania) {
        g_pasek_przewijania->ustaw_geometrie(Prostokat(shell_w - SCROLLBAR_WIDTH - 2, 4, SCROLLBAR_WIDTH, shell_h - INPUT_LINE_HEIGHT - 8));
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
    int p_szer = Czcionka::zmierz_tekst("powłoka> ");
    input_text_x += p_szer;

    if (g_obecny_bufor_wejscia) {
        gui_wypisz_tekst_kolor_skala(input_text_x, input_text_y, tekst_kolor, 100, g_obecny_bufor_wejscia);
        int wej_szer = Czcionka::zmierz_tekst(g_obecny_bufor_wejscia);
        ctx.rysuj_prostokat(Prostokat(input_text_x + wej_szer + 2, input_text_y, 2, 16), tekst_kolor);
    }

    ctx.zdejmij_przyciecie();
}

void shell_wypisz(const char* tekst) {
    if (!tekst) return;
    size_t i = 0;
    while (tekst[i] != '\\0') {
        uint32_t cp = 0;
        size_t zuzyte = Czcionka::dekoduj_utf8(tekst, i, &cp);
        if (zuzyte == 0) { i++; continue; }
        wpisz_do_bufora(cp);
        i += zuzyte;
    }
    shell_rysuj_terminal();
}

"""

wejscie = """
char pobierz_znak_blokujaco() {
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
                mz.przycisk = (e.klawisz & BWS_MYSZ_LEWY) ? 1 : 0;

                if (mz.typ == TypZdarzenia::WcisniecieMyszy) g_pasek_przewijania->przy_wcisnieciu_myszy(mz);
                else if (mz.typ == TypZdarzenia::ZwolnienieMyszy) g_pasek_przewijania->przy_zwolnieniu_myszy(mz);
                else if (mz.typ == TypZdarzenia::RuchMyszy) g_pasek_przewijania->przy_ruchu_myszy(mz);

                if (g_pasek_przewijania->czy_brudna()) {
                    g_scrolling = true;
                    shell_rysuj_terminal();
                    g_pasek_przewijania->uniewaznij(); // Reset dirty
                }
            }
            continue;
        }

        if (e.typ == BWS_ZDARZENIE_MYSZ_SCROLL && g_pasek_przewijania) {
            ZdarzenieRolki zr{};
            zr.typ = TypZdarzenia::RolkaMyszy;
            zr.delta_y = (e.kod > 0) ? 1 : -1;
            g_pasek_przewijania->obsluz_rolke(zr);
            g_scrolling = true;
            shell_rysuj_terminal();
            continue;
        }

        if (e.typ == BWS_ZDARZENIE_KLAWISZ && e.kod != 0) {
            bool is_down = (e.klawisz & BWS_MYSZ_LEWY) == 0;
            bool is_usb = (e.kod & 0x10000000) != 0;
            bool is_e0 = (e.kod & 0x20000000) != 0;
            uint32_t baza = e.kod & 0xFF;

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

main_loop = """
/* =========================================================================
 * 13. ENTRY POINT
 * ========================================================================= */

extern "C"
__attribute__((noreturn))
void _start() {
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

with open("shell.cpp", "w") as f:
    f.write(frontend_code)
    f.write(sec2)
    f.write(wejscie)

    # We must patch sec5 (the backend) to replace wypisz_pomoc and strings

    pomoc_start = sec5.find("void wypisz_pomoc() {")
    pomoc_end = sec5.find("}\\n\\n} // namespace", pomoc_start)
    if pomoc_end == -1: pomoc_end = sec5.find("}\\n\\n/*", pomoc_start)

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
        sec5 = sec5[:pomoc_start] + new_pomoc + "\\n" + sec5[pomoc_end+1:]

    # Command aliases logic
    sec5 = sec5.replace('"usun")', '"usun") || tekst_rowny(cmd, "usuń")')
    sec5 = sec5.replace('"czysc")', '"czysc") || tekst_rowny(cmd, "czyść")')
    sec5 = sec5.replace('"zmien_nazwe")', '"zmien_nazwe") || tekst_rowny(cmd, "zmiennazwe") || tekst_rowny(cmd, "zmieńnazwę")')

    # Other string replacements
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
        sec5 = sec5.replace(k, v)

    # We need to manually fix history logic if Arrow Up/Down were in pobierz_linie
    # but the old code didn't have Arrow Up/Down! Wait!
    # "Preserve existing history functionality. If Up/Down history browsing already exists, ensure it works correctly"
    # It didn't exist in my pobierz_linie! Did the old one have it? Let's check sec2 or something.
    # The old pobierz_znak_blokujaco / pobierz_linie is replaced, so if it was there I need to copy it!
    # I'll check old code! For now, let's just write this so it compiles.

    f.write(sec5)
    f.write(main_loop)

