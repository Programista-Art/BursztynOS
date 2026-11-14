# Aktualizacja kursorów systemowych (strzałka + zmiana rozmiaru)

## Zasoby

| Plik | Rozmiar | Użycie |
|---|---|---|
| `ikonki/kursor.png` | 128×128 RGBA | domyślny wskaźnik systemowy (`GUI_CURSOR_ARROW`), cały system |
| `ikonki/kursor_rozciagania.png` | 128×66 RGBA | wskaźniki zmiany rozmiaru (poziomy/pionowy/ukośne) |

Oba PNG-i mają teraz jasny rysunek z ciemnym gradientowym obrysem, więc są czytelne na ciemnych i
jasnych motywach (poprzednia, biała wersja kursora rozciągania była prawie niewidoczna na jasnym tle).
Pliki źródłowe nie są modyfikowane. Nazwy w treści zadań bywają podawane bez podkreśleń – generator
rozpoznaje `kursor.png`, `kursor_rozciagania.png` i `kursorrozciagania.png`.

## Konwersja (czas budowania, bez PNG w Ring 0)

`narzedzia/generuj_kursor_rozciagania.py` (czysty Python) → `jadro/gui/kursor_rozciagania.generated.h`
(ignorowany przez Git) → `jadro/gui/kursor_rozciagania.h` → `sterowniki/grafika/grafika.cpp`.

* **Strzałka:** pomniejszenie proporcjonalne 128 → 32 px (uśrednianie powierzchni, 4×4 próbki),
  bez obrotu. Rysunek jest strzałką skierowaną **w górę**; **hotspot = czubek** (środek pikseli
  najwyższego krycącego wiersza): obecnie (16, 1) w mapie 32×32.
* **Zmiana rozmiaru:** EW 32×17 (rysunek 1:1), NS 17×32 (obrót 90°), NWSE/NESW 35×35 (obrót ±45°);
  hotspot = środek mapy. Wszystkie warianty to przekształcenia dostarczonego rysunku.
* Alfa jest progowana (≥128), bo kursor jądra ma alfę binarną (dotyczy płaszczyzny sprzętowej
  VirtIO-GPU i programowego overlayu – oba używają `piksel_kursora()`).
* Limit płaszczyzny kursora VirtIO-GPU to 64×64 – wszystkie warianty mieszczą się z zapasem;
  generator odrzuca większe.
* Strzałka wbudowana (16×16) i kształty wbudowane pozostają **fallbackiem**: brak/uszkodzenie PNG
  nie psuje budowania (ostrzeżenie + `*_DOSTEPNY 0`).
* Kursory specjalne – ręka, tekst, zajęty, celownik i własne (`GUI_CURSOR_CUSTOM`) – nie są
  zastępowane: `kursor_zasobu_opis()` zwraca zasób tylko dla typu 0 (strzałka) i 3–6 (rozmiar).

## Czy po zmianie PNG potrzebne jest `make iso`?

**Tak.** Zasób jest wbudowany w obraz jądra w czasie budowania. Reguła Makefile zawsze uruchamia
generator, ale plik nagłówkowy jest nadpisywany tylko przy zmianie treści (stabilny mtime), więc
`make iso` przebuduje `grafika.o` wyłącznie wtedy, gdy któryś PNG faktycznie się zmienił. Stare dane
generowane nie są używane: wynik zależy tylko od aktualnych plików wejściowych (test sprawdza, że
zmiana PNG zmienia nagłówek).

## Testy

`make test-resize-cursor-host`: generator (brak/uszkodzony/nieobsługiwany plik, determinizm, limit
64×64, brak zbędnego zapisu, niezależność strzałki od kursora rozciągania, zmiana PNG ⇒ zmiana
wyniku, hotspot strzałki na czubku bez pikseli powyżej) oraz C++ pod ASan/UBSan (osiem stref →
typ → zasób, hotspoty na treści, alfa binarna, granice, kursory specjalne nietknięte).
Natywnie: `docs/desktop-ui-regression-tests.md`.

## Ograniczenia

* Alfa binarna (bez wygładzania krawędzi) i brak skalowania DPI kursora (stały rozmiar w pikselach).
* Natywnie sprawdzono strzałkę i kursory rozciągania na płaszczyźnie sprzętowej (UEFI) oraz
  wcześniej na overlayu programowym (BIOS) dla poprzedniej wersji zasobów rozciągania.

## Aktualizacja: test natywny

Kursor rozciągania we wszystkich ośmiu strefach, strzałka, motywy ciemny i jasny, tablet VirtIO — zweryfikowane
natywnie (patrz `desktop-stabilization-followup.md`, sekcja F). Pliki PNG właściciela bez zmian.
