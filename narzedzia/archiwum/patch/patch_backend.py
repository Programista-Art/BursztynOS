import re

with open("jadro/gui/backend.cpp", "r") as f:
    code = f.read()

# Replace vypisz_pomoc() entirely
pomoc_start = code.find("void wypisz_pomoc() {")
pomoc_end = code.find("}\n\n} // namespace", pomoc_start)
if pomoc_end == -1:
    pomoc_end = code.find("}\n\n/*", pomoc_start)

new_pomoc = """void wypisz_pomoc() {
    wypisz(
        "Dostępne polecenia:\n\n"
        "  Sieć\n"
        "    ping <IPv4|domena>           Sprawdza dostępność hosta przez ICMP.\n"
        "    pobierz <host> <url> <plik>  Pobiera plik HTTP i zapisuje go w BSP2.\n"
        "\n"
        "  Pliki i katalogi\n"
        "    utworz <plik>                Tworzy pusty plik.\n"
        "    zapisz <plik> <tekst>        Zapisuje tekst do pliku.\n"
        "    czytaj <plik>                Wyświetla zawartość pliku.\n"
        "    pliki [katalog]              Wyświetla zawartość katalogu.\n"
        "    usun <ścieżka>               Usuwa plik lub pusty katalog.\n"
        "    zmiennazwe <ścieżka> <nazwa> Zmienia nazwę pliku lub katalogu.\n"
        "\n"
        "  Powłoka\n"
        "    gdzie                        Wyświetla bieżący katalog.\n"
        "    historia                     Wyświetla historię poleceń.\n"
        "    czysc                        Czyści terminal.\n"
        "    dzwiek                       Odtwarza test HDA 880 Hz.\n"
        "    losuj                        Losuje liczbę.\n"
        "    pisz <tekst>                 Wyświetla tekst.\n"
        "\n"
        "  System\n"
        "    notatnik                     Uruchamia Notatnik.\n"
        "    kalkulator                   Uruchamia Kalkulator.\n"
        "    przegladarka                 Uruchamia przeglądarkę Husarz.\n"
        "    uruchom <plik.bur>           Uruchamia program .bur.\n"
        "    pulpit | wyjdz | exit        Kończy powłokę.\n"
        "    czas                         Pokazuje czas RTC.\n"
        "    system                       Informacje o systemie.\n"
        "    wersja                       Pokazuje aktualną wersję systemu.\n"
        "    aktualizacja [sprawdz]       Pokazuje wersję i stan aktualizacji.\n"
        "    pci                          Wyświetla /logi/pci.txt.\n"
        "    pomoc                        Pokazuje ten ekran pomocy.\n"
    );
}"""

if pomoc_start != -1 and pomoc_end != -1:
    code = code[:pomoc_start] + new_pomoc + "\n" + code[pomoc_end+1:]

# Command aliases logic
dispatch_start = code.find("void wykonaj_polecenie(")

# Replaces inside the command dispatch to handle aliases
code = code.replace('"usun")', '"usun") || tekst_rowny(cmd, "usuń")')
code = code.replace('"czysc")', '"czysc") || tekst_rowny(cmd, "czyść")')
code = code.replace('"zmien_nazwe")', '"zmien_nazwe") || tekst_rowny(cmd, "zmiennazwe") || tekst_rowny(cmd, "zmieńnazwę")')

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
    code = code.replace(k, v)

# Update prompt text in main
code = code.replace('wypisz("\\npowloka> ");', '')
code = code.replace('wypisz("powloka> ");', '')
# The new UI will handle the prompt, so we don't need the prompt printed before pobierz_linie
# We will just print the typed command to the history log!

with open("jadro/gui/backend.cpp", "w") as f:
    f.write(code)

print("Backend patched.")
