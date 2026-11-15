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

## Aktualizacja 2026-11-15: mniejsze kursory i ikony Shell/Tapety

Właściciel uznał kursory (32 px) za zbyt duże względem ikon i kontrolek. Generator
`narzedzia/generuj_kursor_rozciagania.py` (jedyne miejsce; aplikacje nadal wybierają tylko typ kursora):

| Kursor | Przed | Po | Hotspot |
|---|---|---|---|
| strzałka (źródło 53×61) | 28×32 | **16×18** (`DOCELOWA_STRZALKA` 32→18) | (1,0) — czubek (najwyższy kryjący wiersz), liczony na nowo z obrazu docelowego |
| ↔ (źródło 128×57) | 32×15 | **19×9** (`DOCELOWA_SZEROKOSC` 32→19) | środek |
| ↕ | 15×32 | **9×19** | środek |
| ukośne ⤡ ⤢ | 33×33 | **20×20** | środek |

Proporcje źródeł zachowane (zmiana skali, nie rozciąganie), próbkowanie podniesione z 4×4 do 8×8 na piksel,
alfa nadal binarna (>=128) z powodu kursora jądra, kolory i ciemny kontur bez zmian. Wszystkie rozmiary
<= 64×64. Hitboxy zmiany rozmiaru (5 px od krawędzi) i logika wejścia **bez zmian**. Brak PNG ⇒ wbudowana
strzałka/kształty (test generatora). Kursory tekst/ręka/zajęty/celownik nietknięte. Dane są generowane w
czasie budowania; brak dekodowania PNG, alokacji ani konwersji podczas ruchu myszy.

### Ikony aplikacji
* `ikonki/app_shell.png` → wpis „Powłoka Bursztyna” w menu Start, `ikonki/app_tapety.png` → „Tapety”
  (`jadro/gui/start_menu_model.h`; wcześniej `nullptr`, czyli brak ikony).
* Pakowanie jak dla pozostałych ikon aplikacji: reguły `ikona_app_shell_blob.o` / `ikona_app_tapety_blob.o`
  w Makefile, symbole i zapis do `/ikonki/` w `kernel.cpp` (tablica `power_assets`, ta sama ścieżka w live i przy
  instalacji). Koszt w obrazie jądra ~64 KiB (blob PNG, bez drugiego dekodera). Brak pliku ⇒ zwykła ikona zastępcza.
* Wymiana ikony = podmiana PNG w `ikonki/` (128×128 RGBA) i przebudowa; kod aplikacji bez zmian.
* Shell i Tapety nie mają skrótów na pulpicie (`desktop_entries.h` ich nie zawiera) — nie dodawano.
* Ścieżki wykonywalne i manifesty bez zmian.

### Testy
Host: `make test-resize-cursor-host` (generator + C++ ASan/UBSan: wymiary 16×18 / 19×9 / 9×19 / 20×20,
hotspot, proporcje, alfa), `make test-ikony-aplikacji` (rejestracja ścieżek, blob, zapis, format 128×128 RGBA).
Natywnie (QEMU/TCG, UEFI 1280×720, tablet VirtIO): strzałka ma czubek dokładnie na współrzędnej wskaźnika;
↔ pojawia się w strefie 1135–1138 (1134 = strzałka, hitbox bez zmian); zmiana rozmiaru E, W, N, NW działa
(zmierzone na zrzutach), strzałka wraca po puszczeniu; Shell i Tapety startują z menu i mają nowe ikony.
Zrzuty: `docs/zrzuty-kursor-2026-11-15/` (powiększone wycinki, porównanie 32→18 na jasnym/ciemnym tle).
Nie sprawdzono natywnie: mysz USB, BIOS/wyższe rozdzielczości, jasny motyw w gościu (jasne tło tylko na
porównaniu syntetycznym), strefy S/SE/SW/NE tym przebiegiem (wcześniej zweryfikowane, logika nietknięta).
Jądro: `__kernel_end` ≈ 0xB16000 (~11,1 MiB), zapas do limitu 15 MiB ≈ 3,9 MiB.

## Aktualizacja 2026-11-15 (2): kursory zmiany rozmiaru nieco większe
Po uwagach właściciela: ↔ 19×9 → **24×11**, ↕ **11×24**, ukośne 20×20 → **25×25** (`DOCELOWA_SZEROKOSC` = 24).
Strzałka 16×18 bez zmian. Zob. `docs/window-resize-synchronization.md`.
