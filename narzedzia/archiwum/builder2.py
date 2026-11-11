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

sec2_3 = get_section("/* =========================================================================\n * 2. PROSTE WRAPPERY BWS", "/* =========================================================================\n * 4. WEJSCIE TERMINALA")

# Remove those 3 problematic UI functions from sec2_3
# They are between "void wypisz_znak(" and "void uint_do_str("
w_start = sec2_3.find("void wypisz_znak(")
u_start = sec2_3.find("void uint_do_str(")
if w_start != -1 and u_start != -1:
    sec2_3 = sec2_3[:w_start] + sec2_3[u_start:]

sec5 = get_section("/* =========================================================================\n * 5. PARSER WIERSZA POLECEN", "/* =========================================================================\n * 13. NAGLOWEK BINARNY APLIKACJI")

# Fix missing constants in sec5/sec2_3? They are just in my frontend_code!

# Fix CzcionkaUI in my frontend code!
with open("jadro/gui/frontend.cpp", "r") as f:
    front = f.read()
front = front.replace("Czcionka::", "CzcionkaUI::")
front = front.replace("g_pasek_przewijania->ustaw_geometrie(Prostokat(", "g_pasek_przewijania->ustaw_geometrie(shell_w - SCROLLBAR_WIDTH - 2, 4, SCROLLBAR_WIDTH, shell_h - INPUT_LINE_HEIGHT - 8);\n        // ")

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
    f.write(front)
    f.write(sec2_3)

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

    f.write(sec5)
    f.write(main_loop)

