# Snake — migracja na BursztynUI

## Co się zmieniło

Dawny Snake (`gry/snake/snake.cpp`, 651 linii) rysował własną belkę, miał stałe okno 400×328 px,
ręczną obsługę przeciągania i brak zmiany rozmiaru. Teraz jest aplikacją `BursztynUI::Okno`:

| Obszar | Wcześniej | Teraz |
|---|---|---|
| Okno, belka, ruch, minimalizacja, maksymalizacja, zamknięcie | ręcznie (`gui_rysuj_standardowa_belke`, własny drag) | `Okno` (framework) |
| Zmiana rozmiaru (4 krawędzie + 4 rogi) | brak | framework + systemowy kursor zmiany rozmiaru |
| Ruch częściowo poza ekran | blokada do ekranu | framework (uchwytny pasek tytułu) |
| Motywy | stałe kolory | `motyw_kolor(...)`, zmiana motywu bez restartu gry |
| DPI | brak | framework (`SkalaUI`) |
| Interfejs | belka + plansza | tytuł, wynik, „Nowa gra”, „Pauza/Wznów”, plansza, pasek stanu |
| Plansza | prostokąty 20 px | `PowierzchniaRysunkowa`, kwadratowe komórki skalowane do okna |

Cały tekst jest po polsku i renderowany przez BSF (silnik tekstu BursztynUI).

## Podział plików

* `gry/snake/snake_logika.h` — **czysta logika** (bez GUI/BWS): stan `Gra`, kroki, kolizje, wzrost,
  jabłko (nigdy na wężu), zwycięstwo, pauza, stany `Gotowa/Gra/Pauza/KoniecGry/Wygrana`,
  oraz `uklad_planszy()` (geometria planszy w widoku).
* `gry/snake/snake.cpp` — UI (`OknoSnake`, przyciski, pasek stanu), zegar gry i rysowanie planszy.
* `gry/snake/snake_linker.ld` — skrypt linkera (taki sam układ nagłówka `.bur` jak Kalkulator:
  TEXT 192 KiB, DATA/BSS 128 KiB).

## Zachowanie gry (bez zmian względem poprzedniej wersji)

Plansza logiczna 20×15 (pole gracza 18×13 = 234), ściany na obwodzie, WASD i strzałki,
brak zawracania o 180°, jeden skręt na krok, wzrost w miejscu starego ogona, zwycięstwo po
zapełnieniu planszy, restart klawiszem `R` albo „Nowa gra”. Nowość: spacja = start/pauza/wznowienie,
`P` = pauza, stan „Gotowa” (gra czeka na pierwszy skręt).

* **Prędkość nie zależy od klatek**: zadanie okresowe `AplikacjaGUI::ustaw_zadanie_okresowe`
  budzi proces co ~32 ms, a kroki gry liczone są zegarem monotonicznym (`KROK_NS` = 125 ms,
  maks. 3 kroki na wywołanie — po długiej przerwie gra nie „goni”). Brak RDTSC jako zegara, brak
  aktywnego czekania.
* **Poza `Gra` timer jest wyłączony** (pauza, koniec, gotowość) — proces blokuje się na zdarzeniach
  (0% CPU), a pauza nie może przesunąć gry. Minimalizacja okna automatycznie wstrzymuje grę.
* Zmiana rozmiaru okna zmienia **tylko rysowanie** (`uklad_planszy`: komórka =
  `min(w/20, h/15)`, plansza wyśrodkowana, proporcje 4:3, komórki kwadratowe). Stan gry,
  współrzędne węża, jabłko, wynik i prędkość pozostają bez zmian (test hosta).
* Przyciski „Nowa gra” i „Pauza” nie przejmują fokusu klawiatury, żeby sterowanie klawiszami
  działało po kliknięciu.

## Motywy

Tło planszy = `MOTYW_KOLOR_POLETEKSTOWE`, ściany = mieszanka tła i akcentu, siatka = mieszanka tła
i tekstu, ciało węża = `MOTYW_KOLOR_SUKCES`, jabłko = `MOTYW_KOLOR_BLAD`, oczy głowy = `MOTYW_KOLOR_TEKST`,
nakładki = `MOTYW_KOLOR_PANEL` / `TEKST_DRUGI`. Okno, przyciski, etykiety i pasek stanu pochodzą od
frameworka. Zdarzenie `BWS_ZDARZENIE_MOTYW_ZMIENIONY` przerysowuje interfejs bez restartu gry.

## Testy

`make test-snake-host` (ASan/UBSan): logika (inicjalizacja, ruch, 180°, jeden skręt na krok,
kolizje ze ścianą i ciałem, wzrost, jabłko nie na wężu, zwycięstwo, pauza, restart), geometria
planszy (kwadratowe komórki, wyśrodkowanie, maksymalność, wartości skrajne bez przepełnień),
UI (klawisze WASD/strzałki/spacja/R, timer czasowy, brak ruchu w pauzie, zmiana rozmiaru okna
bez zmiany stanu gry, dwa motywy → kolory planszy/węża/jabłka z motywu, nakładka końca gry,
pauza przy minimalizacji). Testy natywne: `docs/snake-image-viewer-native-tests.md`.

## Ograniczenia

* Przy kolizji głowa jest rysowana w polu ściany (jak w poprzedniej wersji).
* Maksymalizacja korzysta z frameworka; plansza skaluje się, ale komórki są całkowitoliczbowe
  (marginesy wyrównujące).
