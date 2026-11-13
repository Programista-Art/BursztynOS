# BSF 2.2 — zmienne fonty w UI i CSS: stan i plan

**Status: NIE zaimplementowano w cyklu stabilizacji.** Priorytety 1–5
(rozmiar jądra, test pulpitu, wydajność CFF, duże pliki, Origin) pochłonęły
cykl; poniżej rzetelny stan kodu i plan, bez deklarowania obsługi.

## Co działa (zweryfikowane w BSF 2.2)

* Silnik: fvar/avar (avar 1.0), gvar, CFF2 blend, HVAR, MVAR, GPOS
  VariationIndex (`bsf/variations.inc`, `docs/bsf2-2-variable-fonts.md`);
  kontury instancji = FreeType (2718 + 5192 + 6357 porównań).
* Husarz: instancje wynikające z `font-weight` / `font-stretch` (oś `wght`,
  `wdth`) dla fontów z `@font-face` (strona `09-variable.html`, natywnie).

## Czego brakuje

| funkcja | stan |
|---|---|
| CSS `font-variation-settings` | brak parsowania (`programy/husarz/css.h` obsługuje tylko `font-weight`/`font-stretch`) |
| CSS `font-optical-sizing` / oś `opsz` | brak |
| Ustawienia: suwaki osi dla fontu systemowego | brak (Config v2 nie przechowuje współrzędnych osi) |
| GSUB/GPOS `FeatureVariations` | brak — podstawienia zależne od instancji (np. `rvrn`) nie są stosowane |
| avar 2.0 | brak — tabela avar v2 jest traktowana jak v1 (tylko segmenty osi) |

## Plan

1. `font-variation-settings`: lista `"tag" liczba` → `VariationSetting[]`
   przekazywane do `set_variations` przy tworzeniu instancji scoped face;
   klucz cache instancji już obejmuje współrzędne (`Face::id`).
2. FeatureVariations: w `OT::build_plan` wybór `FeatureTableSubstitution`
   wg `ConditionSet` dla bieżących współrzędnych (F2Dot14 po avar).
3. Config v3: do 4 par (tag, wartość) dla rodziny systemowej; suwaki w
   `SettingsUI::FontPage` generowane z `variation_axes()`; walidacja zakresów
   w Ring 3, zapis przez BWS 92 z sumą kontrolną.
4. avar2: macierz delt na znormalizowanych współrzędnych (ItemVariationStore
   już jest parsowany).
5. Testy: HarfBuzz (`hb_font_set_variations`) jako wyrocznia shapingu dla
   FeatureVariations, FreeType dla konturów avar2.
