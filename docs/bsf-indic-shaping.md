# BSF — shaping indyjski (Devanagari): stan i plan

**Status: NIE zaimplementowano.** Nie deklarujemy obsługi Devanagari.

## Stan kodu

* `bsf/shape.cpp`: plan cech dla skryptów indyjskich wybiera tag `dev2`
  z fallbackiem do `deva`; dodawane są cechy globalne (`ccmp`, `locl`,
  `curs`, `dist`, `abvm`, `blwm`, …). Brakuje maszyny stanów Indic:
  * kategoryzacji znaków (Indic syllabic/positional category z UCD),
  * podziału na sylaby (konsonant + virama + … , ZWJ/ZWNJ),
  * zmiany kolejności: **reph** (Ra+virama na początku → na koniec
    sylaby), matry przed-bazowe (np. U+093F ि przed spółgłoskę bazową),
  * maskowania cech per pozycja: `nukt`, `akhn`, `rphf`, `rkrf`, `pref`,
    `blwf`, `abvf`, `half`, `pstf`, `vatu`, `cjct` (faza podstawowa) oraz
    `init`, `pres`, `abvs`, `blws`, `psts`, `haln` (faza prezentacji),
  * obsługi koszyczka (dotted circle U+25CC) dla sylab niepoprawnych.
* Bez tego tekst Devanagari jest kształtowany „liniowo”: ligatury
  spółgłoskowe z `ccmp`/`akhn` mogą zadziałać przypadkowo, ale ि (i-matra)
  i reph są w złej pozycji — wynik nieczytelny dla użytkownika.

## Plan

1. Tabela kategorii Indic generowana z UCD (`IndicSyllabicCategory.txt`,
   `IndicPositionalCategory.txt`) do `bsf/unicode_props.cpp` (zakres
   U+0900–U+097F + U+A8E0–U+A8FF najpierw).
2. Segmentacja sylab (maszyna stanów jak w HarfBuzz
   `hb-ot-shaper-indic-machine.rl`, napisana ręcznie, ograniczona długość).
3. Initial reordering → GSUB faza podstawowa per maska → final reordering
   (reph, pre-base matra, pref) → GSUB prezentacyjna → GPOS.
4. Wyrocznia: HarfBuzz na Noto Sans Devanagari (OFL) — nowy korpus
   `testy/fixtures/bsf22/shaping_devanagari.tsv` w istniejącym teście
   `test-bsf22-shaping-host` (wymóg: 100% wierszy exact).
5. Dopiero po zielonym korpusie: wpis w `bsf2supportmatrix.md`.
