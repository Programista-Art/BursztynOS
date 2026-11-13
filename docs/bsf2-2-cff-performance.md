# BSF 2.2 — wydajność CFF/CFF2

## Diagnoza (pomiar, nie założenie)

Zadanie sugerowało cache Private DICT / Subrs / FDArray. Pomiar rozbił czas
rasteryzacji glifu CFF (Noto Sans OTF, 16 px, host `-O2`) na etapy:

| etap | przed |
|---|---|
| `private_for()` (FDArray + Private DICT + skan INDEX Subrs) | ~1,0–1,4 µs |
| cały kontur (`glyph_path`: charstring + Private) | ~2,7 µs |
| **rasteryzacja całości** | **~32–42 µs** |

TrueType (DejaVu) rasteryzuje się w ~4,3 µs przy podobnym czasie konturu
(1,8 µs). Różnica była więc w rasteryzatorze, nie w parserze CFF.

Przyczyna: stb_truetype spłaszcza krzywe sześcienne (`stbtt__tesselate_cubic`)
z 4 wywołaniami `STBTT_sqrt` na każdy krok podziału. W BSF `STBTT_sqrt` był
mapowany na `square_root()` — 32 iteracje Newtona z dzieleniem (Ring 3 nie
ma libm). Krzywe kwadratowe TrueType nie wołają pierwiastka, stąd 8× różnica.

## Zmiany

1. **`square_root()` → `sqrtsd` (SSE2)** (`bsf/bsf.cpp`). Pierwiastek IEEE
   poprawnie zaokrąglony, jedna instrukcja. `bsf_core.o` (Ring 3) jest
   budowany z `-msse2 -mfpmath=sse`; jądro nie kompiluje `bsf.cpp`. Bez SSE2
   pozostaje Newton z warunkiem zbieżności. Wejście ≤ 0 oraz NaN → 0
   (wcześniej NaN propagował się). Używane też przez operator `sqrt`
   charstringów i gradienty COLRv1.
2. **Cache Private DICT per FD** (`bsf/bsf.h` `CffFace::privates[8]`,
   `bsf/cff.inc`). `CffFace::open()` i tak parsował i walidował każdy Private
   DICT; teraz zachowuje wynik (Subrs INDEX jako offsety względem tabeli —
   bezpieczne przy kopiowaniu `Face`, szerokości, `vsindex`). Wyodrębnione
   pola nie zależą od instancji wariacyjnej (blend w Private DICT dotyczy
   wartości hintów, których BSF nie używa; Subrs/vsindex nie są blendowane).
   FD > 8 (duże fonty CID) parsowane jak dotąd. Koszt: +320 B na `Face`
   (1152 → 1472 B). `private_for`: ~900 ns → 4 ns (CFF), 22 ns (CFF2).

Global Subrs, CharStrings INDEX i FDSelect były już przechowywane w
`CffFace` (wyszukiwanie FDSelect binarne).

## Wynik (`make bench-bsf22`, ten sam host)

| Face | 16 px przed | 16 px po | 48 px przed | 48 px po |
|---|---|---|---|---|
| TrueType DejaVu Sans | 5 123 | 4 270 | 7 556 | 6 427 |
| CFF Noto Sans OTF | 39 600 | **4 746** | 71 677 | **8 663** |
| CFF2 Source Serif 4 (default) | 46 113 | **8 167** | 83 024 | **12 340** |
| CFF2 Source Serif 4 wght=800 | 50 137 | **10 943** | 87 041 | **15 242** |

(ns/glyph, tylko chybienia cache; trafienia bez zmian.) CFF jest teraz na
poziomie TrueType; CFF2 z instancją wariacyjną ~2× TrueType (blend
deltas w charstringu).

## Poprawność

* `make test-bsf22-host`: 16 395/16 395 konturów identycznych z FreeType;
  średni błąd pokrycia rastra 0,0305, najgorszy 0,3240 — **bit w bit te same
  wartości co przed zmianą**; shaping vs HarfBuzz 120+44+51+22 wierszy exact;
  wariacje 2718+5192+6357 konturów = FreeType; kolor 1093 checks.
* `make test-bsf-host` (12 238), `test-desktop-ttf-host` (188 398, sygnatury
  pikselowe), `test-bsf-ui-host` (9 069) — PASS.
