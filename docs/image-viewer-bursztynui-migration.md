# Przeglądarka Obrazów — migracja na BursztynUI

## Podział

| Plik | Odpowiedzialność |
|---|---|
| `programy/przegladarka-obrazow/silnik_obrazu.h` | **Silnik** (bez okna i kontrolek): strumieniowy parser BMP, limity, cache wierszy (zakresy plików), cache viewportu i dopasowania, renderery Fit i 1:1 z dirty-regionami i postępowym dekodowaniem, przesuwanie widoku |
| `programy/przegladarka-obrazow/przegladarka-obrazow.cpp` | **UI**: `OknoObrazow : Okno`, `MenuGlowne`, `PasekNarzedzi`, `PasekStanu`, `DialogPliku`, kontrolka widoku (`PowierzchniaRysunkowa`), polecenia |
| `programy/przegladarka-obrazow/przegladarka_obrazow_linker.ld` | układ `.bur` (TEXT 192 KiB, DATA/BSS 128 KiB, jak Kalkulator) |

Silnik został **przeniesiony 1:1** z poprzedniej, monolitycznej aplikacji (algorytmy dekodowania,
cache, limity nie zostały przepisane). Zmieniły się tylko trzy punkty styku z oknem:

1. prostokąt widoku — `g_obszar_obrazu` (lokalny, ustawiany przez kontrolkę przy każdym rysowaniu),
2. ujście gotowych fragmentów — `g_ujscie_obrazu` (UI unieważnia kontrolkę zamiast wołać `gui_blit`),
3. kolor tła — `kolor_tla` (z motywu; nigdy `0x00000000`, bo to klucz przezroczystości warstwy —
   motyw czarny daje `TECHNICZNY_CZARNY`).

Usunięto kod okna: własną belkę, przeciąganie, zmianę rozmiaru, maksymalizację, ręczne przyciski
„Dopasuj/1:1” i ręczny pasek statusu (≈ 700 linii).

## Interfejs (po polsku)

* **Plik:** Otwórz… (natywny `DialogPliku`, filtr „Obrazy BMP”), Zamknij obraz, Zakończ.
* **Widok:** Dopasuj do okna, Rozmiar rzeczywisty 1:1, Powiększ, Pomniejsz.
* **Pasek narzędzi:** Otwórz, Dopasuj, 1:1, Powiększ, Pomniejsz.
* **Pasek stanu:** nazwa pliku (albo komunikat błędu) | wymiary i głębia | tryb (Dopasuj / 1:1 z pozycją).
* Klawisze: `1` (1:1), `D`/`F` (dopasuj), `+`/`-`, WASD i strzałki, Home/End, PgUp/PgDn (przesuwanie).
* Przeciąganie myszą przesuwa obraz w trybie 1:1.

> „Powiększ/Pomniejsz”: silnik ma dwa poziomy skali (dopasowanie i 1:1), a zadanie zabrania
> zastępowania silnika — „Powiększ” przechodzi z Dopasuj do 1:1, „Pomniejsz” z 1:1 do Dopasuj.
> Dowolne współczynniki powiększenia nie są zaimplementowane.

## Rozmiar okna

Zmiana rozmiaru wszystkimi krawędziami i rogami, ruch częściowo poza ekran, maksymalizacja i
przywracanie działają przez `Okno`. Przy zmianie rozmiaru kontrolka przekazuje silnikowi nowy
rozmiar widoku: cache Fit/viewport jest unieważniany tylko, gdy rozmiar się zmienił; pozycja
przesunięcia jest przycinana do granic obrazu; stan obrazu (plik, tryb, pozycja) jest zachowany;
nowy rozmiar bez ponownego odczytu pliku dla Fit w tym samym rozmiarze (test hosta).
Zmiana motywu odświeża tylko tło (obraz jest dekodowany ponownie do bufora, jego piksele się nie zmieniają).

## Bezpieczeństwo i limity (bez zmian)

Walidacja nagłówka BMP (sygnatura, DIB, planes=1, 24/32 bit, BI_RGB), wymiary ≤ 8192×8192,
plik ≤ 1 GiB, arytmetyka 64-bitowa z wykrywaniem przepełnień (`mnoz_u64`, `dodaj_u64`),
odczyt strumieniowy zakresami, bufory alokowane raz przed pętlą zdarzeń (bez alokacji na ścieżce
krytycznej), widoki większe niż bufor (okno > 2× ekran) rysują komunikat zamiast dekodować.
Aplikacja pozostaje programem Ring 3 `.bur` (PZB, ASLR, NX, W^X bez zmian).

## Testy

`make test-przegladarka-obrazow-host` (ASan/UBSan, 112 sprawdzeń): parser (poprawne 24/32 bit,
góra-dół/dół-góra, 14 rodzajów uszkodzonych nagłówków, przepełnienia, limity), Fit (kolory
ćwiartek, tło z motywu), 1:1 (postępowe dekodowanie, piksele zgodne ze źródłem po przesunięciu),
granice przesuwania, zmiana rozmiaru okna (12 kombinacji), cache Fit (brak odczytu przy ponownym
rysowaniu), Powiększ/Pomniejsz, obsługa błędów (brak pliku, uszkodzony, pusty, nagłówek zbyt duży,
ścieżka względna), zamknięcie obrazu, zdarzenie otwarcia pliku, dwa motywy + motyw czarny (kolory
obrazu bez zmian), panning myszą. Testy natywne: `docs/snake-image-viewer-native-tests.md`.

## Ograniczenia

* Tylko BMP 24/32 bit BI_RGB (jak wcześniej).
* Dwa poziomy skali (patrz wyżej).
* Pamięć: bufory widoku i cache wierszy (do 48 × 1 MiB) jak w poprzedniej wersji.
