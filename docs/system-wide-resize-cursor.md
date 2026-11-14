# Systemowy kursor zmiany rozmiaru okna

## Architektura (jedno miejsce wyboru wyglądu)

```
aplikacja / BursztynUI::Okno / Eksplorator / Menedżer Okien
        │  gui_ustaw_kursor(GUI_CURSOR_RESIZE_{NS,EW,NWSE,NESW})   ← tylko TYP kursora
        ▼
BWS_GUI_CURSOR_SHAPE → bws_gui_ustaw_kursor_ksztalt()            (stan per proces: kursory_procesow[pid])
        ▼
sterowniki/grafika/grafika.cpp: parametry_kursora() / piksel_kursora()
        ▼
jadro/gui/kursor_rozciagania.h  ← tabela map bitowych z kursor_rozciagania.generated.h
        ▼
kursor sprzętowy (płaszczyzna VirtIO-GPU, ≤64×64) albo overlay programowy — ta sama funkcja piksel_kursora()
```

* **Wybór kierunku** robi wspólna geometria `jadro/gui/okno_geometria.h`
  (`krawedz_w_punkcie` → `kursor_dla_krawedzi`): osiem stref (lewa, prawa, górna, dolna, cztery rogi)
  → `EW`, `NS`, `NWSE`, `NESW`. Używają jej `BursztynUI::Okno` (wszystkie aplikacje BursztynUI)
  oraz Eksplorator i Menedżer Okien. Żadna aplikacja nie ładuje grafiki kursora i nie zna pliku PNG.
* **Wygląd** zapewnia wyłącznie jądro. Aplikacje wybierają typ; dzięki temu zmiana grafiki
  obejmuje jednocześnie Notatnik, Husarz, Eksplorator, Terminal, Ustawienia, Diagnostykę, Kalkulator,
  Snake, Przeglądarkę Obrazów, Aktualizacje, Test dźwięku i przyszłe aplikacje używające `Okno`.
* **Stan** jest per proces (`kursory_procesow[pid]`); aktywny jest kursor procesu z fokusem.
  Zamknięcie okna/procesu czyści stan, utrata fokusu przełącza na kursor procesu aktywnego,
  a `Okno::aktualizuj_kursor_krawedzi` przywraca strzałkę, gdy wskaźnik opuści strefę chwytania.
* Zmiana kształtu oznacza tylko `kursor_plane_ksztalt_brudny` (ponowne wgranie 1 małej mapy bitowej do
  płaszczyzny kursora lub przerysowanie okolicy kursora); **nie** powoduje przerysowania ekranu.
* Typy inne niż zmiana rozmiaru (strzałka, ręka, tekst, zajęty, celownik) i kursory własne
  (`GUI_CURSOR_CUSTOM`) nie są dotykane: `kursor_rozciagania_opis()` zwraca `nullptr` dla typów ≠ 3..6.

## Zasób graficzny `ikonki/kursor_rozciagania.png`

Nazwa w pliku: **`ikonki/kursor_rozciagania.png`** (z podkreśleniem; generator akceptuje też
`ikonki/kursorrozciagania.png`).

**Status: plik jest obecny w repozytorium** (128×66, 8 bit RGBA, bez przeplotu) i jest używany.
Rysunek to **pozioma, dwustronna biała strzałka** (kolor biały, bez obrysu, miękkie krawędzie alfa).

Konwersja odbywa się w czasie budowania (`narzedzia/generuj_kursor_rozciagania.py`, czysty Python,
bez dekodera PNG w Ring 0 i bez bibliotek zewnętrznych):

| Wariant | Jak powstaje | Rozmiar |
|---|---|---|
| `EW` (lewa/prawa krawędź) | rysunek 1:1, proporcjonalne pomniejszenie (uśrednianie powierzchni, 4×4 próbki) | 32×17 |
| `NS` (górna/dolna krawędź) | ten sam rysunek obrócony o 90° | 17×32 |
| `NWSE` (lewy-górny / prawy-dolny róg) | ten sam rysunek obrócony o 45° | 35×35 |
| `NESW` (prawy-górny / lewy-dolny róg) | ten sam rysunek obrócony o −45° | 35×35 |

* Nie powstaje żadna nowa grafika — wszystkie warianty są geometrycznym przekształceniem dostarczonego
  rysunku; proporcje nie są zniekształcone, kolor nie jest zmieniany (zawsze biały z PNG).
* Alfa jest progowana (≥128 → piksel kryjący), bo kursor jądra ma alfę binarną (`piksel_kursora`
  zwraca kolor albo „brak”). Skutek: krawędzie są bez wygładzania.
* **Hotspot** = środek mapy bitowej (środek strzałki), więc wskaźnik jest dokładnie na krawędzi okna.
* Pamięć: 4 tablice `uint32_t` (≈ 4,4 KiB danych łącznie w obrazie jądra), brak alokacji w czasie
  ruchu wskaźnika; limit 64×64 jest sprawdzany generatorem (błąd ⇒ użycie kształtów wbudowanych).
* **Skalowanie DPI:** kursor ma stały rozmiar w pikselach (tak samo jak strzałka 16×16).
  Płaszczyzna kursora VirtIO-GPU ma limit 64×64, więc 200% (wariant diagonalny 70×70) nie zmieści się;
  skalowanie kursora nie jest zaimplementowane (ograniczenie, patrz niżej).

### Wygląd i widoczność

PNG jest czysto biały i bez ciemnego obrysu. Na jasnych oknach (motywy jasne) kursor jest słabo
widoczny. Zgodnie z wymaganiem nie zmieniano koloru ani nie dorysowywano obrysu. Jeśli widoczność na
jasnym tle ma być poprawiona, trzeba dostarczyć wersję PNG z ciemnym obrysem.

## Jak podmienić grafikę

1. Zastąp `ikonki/kursor_rozciagania.png` (RGBA 8 bit, bez przeplotu, ≤1024×1024; rysunek poziomy,
   bo `EW` to rysunek bez obrotu; przy innej orientacji zmień kąty w `zbuduj()`).
2. **Przebuduj system** (`make iso`): zasób jest wbudowany w obraz jądra w czasie budowania;
   uruchomiony system nie ładuje PNG. Reguła Makefile zawsze uruchamia generator, ale plik nagłówkowy
   jest zapisywany tylko przy zmianie treści, więc `grafika.o` przebudowuje się wyłącznie wtedy, gdy
   rysunek faktycznie się zmienił.
3. Brak pliku albo plik uszkodzony **nie psuje budowania**: generator wypisuje ostrzeżenie, ustawia
   `KURSOR_ROZCIAGANIA_DOSTEPNY 0` i jądro używa dotychczasowych kształtów wbudowanych (fallback).

## Testy

* `make test-resize-cursor-host` — generator (brak pliku, plik uszkodzony, nieobsługiwany format,
  determinizm, limit 64×64, brak zbędnego zapisu pliku) + wybór/zasób w C++ pod ASan/UBSan:
  wszystkie osiem stref → typ → zasób, hotspot na treści, alfa binarna, granice, typy niezwiązane.
* Natywnie: patrz `docs/snake-image-viewer-native-tests.md`.

## Ograniczenia

* Brak wygładzania krawędzi (alfa binarna) i brak skalowania DPI kursora.
* Biały kursor bez obrysu jest słabo widoczny na jasnym tle.
* Zasób jest zbudowany w jądrze — zmiana grafiki wymaga `make iso`.
