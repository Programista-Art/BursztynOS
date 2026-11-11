# BSF 2.0 — OpenType GSUB/GPOS shaping

Implementation: `bsf/otl.{h,cpp}` (tables), `bsf/shape.{h,cpp}` (shaper).

## Verification method

The host's HarfBuzz is used **only as a development oracle** through
PyGObject (`narzedzia/generate_bsf_shaping_oracle.py`); Bursztyn OS never
links it. The generator writes `testy/fixtures/bsf_shaping_oracle.tsv`
(font, script, direction, UTF-8, expected glyph records). `make
test-bsf-shaping-host` shapes every row with BSF and compares **glyph IDs,
clusters, x/y advances and x/y offsets exactly**, in font units and visual
order. Clusters are compared at grapheme granularity: BSF clusters are
UAX #29 grapheme starts (the editing model), while HarfBuzz keeps e.g. ZWNJ
as its own cluster.

Result: **120/120 rows exact, 1,222 glyphs** (2026-11-11; 110/1,164 before
the GSUB 3/8 rows were added), fonts: DejaVu Sans, DejaVu Sans
Bold, DejaVu Serif Condensed Italic, Alice (repository), Noto Naskh Arabic,
Noto Kufi Arabic, Noto Sans Arabic, Noto Nastaliq Urdu, Noto Sans Coptic,
Noto Sans Hebrew, Noto Serif Hebrew, Noto Sans (host fonts; rows are skipped, not failed, if a host font is
absent — the report prints the skip count).

Coverage of the corpus: Polish, Vietnamese stacked diacritics, Latin
ligatures and kerning, Cyrillic, Greek (incl. polytonic marks), Arabic
contextual forms, lam-alef ligatures, harakat with shadda, the Allah
ligature with marks, Nastaliq cursive chains (GPOS 3) and contextual
substitution/positioning (GSUB 5, GPOS 7, GSUB 7 extensions), Coptic overlines
(GSUB 8 reverse chaining), tatweel, Persian/Urdu/Uyghur
letters, Arabic-Indic digits, ZWJ/ZWNJ, brackets in RTL, Hebrew niqqud with
dagesh/shin dot.

Defects found by the oracle and fixed during development:

1. `CompositionExclusions.txt` entries were not parsed (no `;` separator),
   so Hebrew presentation forms were wrongly composed.
2. Decomposition must require the second component's glyph (shortest form).
3. Ligature cluster merging must extend over trailing marks sharing the last
   component's cluster.
4. Mark reordering must use the established *modified* combining classes
   (Hebrew points, Arabic shadda).

### Lookup application instrumentation (2026-11-11)

`OT::Statistics` now counts every *successful* subtable application by
lookup type and subtable format (`gsub_applied[type][format]`,
`gpos_applied[type][format]`; extension lookups count under GSUB 7 / GPOS 9
and under the resolved type). `make test-bsf-shaping-host` sums them over
the corpus, prints the table and **fails if any GSUB type 1–8 or GPOS type
1–9 was never applied** (asserted only when no row was skipped). Natively,
`TextStatistics.gsub_type_mask/gpos_type_mask` (appended fields) expose the
same information per process; the QEMU harness reports them.

This instrumentation corrected the earlier report: presence in a font's
LookupList is not application. With the original 110 rows, **GSUB 3 and
GSUB 8 were never applied** (HarfBuzz did not apply them either — both
engines agreed on every row, so the oracle could not reveal it):

- GSUB 8 (reverse chaining) in Noto Sans Coptic fires only when a plain
  overline precedes an already-connected one (lowercase then uppercase
  letter, e.g. `ⲁ̅Ⲛ̅`); the earlier Coptic rows never formed that context.
  Four rows were added; HarfBuzz's message trace confirms lookup 17 runs on
  the old rows without substituting.
- GSUB 3 (alternate) appears in the oracle fonts only under `aalt`, which no
  default feature plan includes. A new opt-in `FeatureAalt` (first alternate,
  same as HarfBuzz `aalt=1`) and five oracle rows with an optional seventh
  column `aalt` now exercise it.

Applications over the 120-row corpus (format 1 / 2 / 3):

| Type | GSUB | GPOS |
|---|---|---|
| 1 | 259 (26 / 233 / –) | 2 (2 / 0 / –) |
| 2 | 68 (68) | 268 (14 / 254) |
| 3 | 25 (25) | 24 (24) |
| 4 | 49 (49) | 129 (129) |
| 5 | 13 (4 / 9 / 0) | 7 (7) |
| 6 | 49 (0 / 29 / 20) | 17 (17) |
| 7 | 51 (extension) | 1 (0 / 1 / 0) |
| 8 | 4 (4) | 1 (0 / 0 / 1) |
| 9 | – | 7 (extension) |

Formats **not** exercised by real fonts in the corpus: GSUB 5 format 3,
GSUB 6 format 1, GPOS 7 formats 1 and 3, GPOS 8 formats 1 and 2. They share
`apply_context` with the exercised formats and are reached by the OTL
fuzzer, but have no exact-oracle row (follow-up: synthetic fonts).

## Tables and lookups

- **GDEF**: glyph class, mark attachment class, mark glyph sets. Without
  GDEF, glyph classes are synthesized from General_Category Mn.
- **ScriptList/LangSys/FeatureList/LookupList**: script preference list
  (Indic `dev2`→`deva`), fallbacks `DFLT`, `dflt`, `latn`; optional LangSys
  tag; required feature honoured. FeatureVariations are ignored.
- **Lookup flags**: RightToLeft, IgnoreBase/Ligatures/Marks, mark filtering
  sets, mark attachment type; default ignorables (except ZWJ/ZWNJ) are skipped
  in context matching.
- **GSUB** 1 single (fmt 1/2), 2 multiple (incl. deletion), 3 alternate
  (first alternate; reached through the opt-in `aalt` feature only), 4 ligature (ligature id/component tracking for marks),
  5 context and 6 chained context (fmt 1/2/3 with nested lookup records),
  7 extension, 8 reverse chaining single (applied back-to-front).
- **GPOS** 1 single, 2 pair (fmt 1 pair sets, fmt 2 class pairs; second
  value record skips the pair glyph), 3 cursive (entry/exit, RTL flag, chain
  reversal), 4 mark-to-base, 5 mark-to-ligature (component selection via
  ligature id/component), 6 mark-to-mark, 7/8 context, 9 extension. Mark
  advances are zeroed by GDEF after GPOS; attachment chains are resolved
  iteratively with cycle detection.

## Feature plan

Stage order follows HarfBuzz's default and Arabic shapers:
`rvrn` | `ltra ltrm` or `rtla rtlm` | `ccmp locl` | (Arabic: `isol`,
`fina`, `fin2`, `fin3`, `medi`, `med2`, `init` each in its own stage, then
`rlig`, then `calt`, then `rclt mset`) | `rlig calt rclt liga clig`
(+`dlig`, `aalt` when requested). GPOS: `kern mark mkmk curs dist abvm blwm`.
Lookups run in (stage, lookup index) order with per-glyph feature masks
(Arabic forms come from the joining state machine).

## Arabic joining

Joining types from `DerivedJoiningType.txt` (U, R, D, C→D, L, T
transparent) drive the standard state machine producing isol/fina/medi/init
masks. Syriac Alaph and Dalath-Rish groups (fin2/fin3/med2) are not
modelled. Fonts without GSUB do **not** get the Unicode presentation-form
fallback.

## Security bounds

Every read goes through `Bytes` (offset+length validated, zero on overrun);
every loop driven by font data spends from a per-run `Budget` (default 4 Mi
steps); contextual recursion depth ≤ 6; context length ≤ 64; multiple
substitution ≤ 32 glyphs; buffer capacity fixed at twice the input; extension
of an extension is rejected; glyph IDs ≥ numGlyphs are rejected. Exhaustion
leaves a well-formed, partially shaped buffer and is counted in
`budget_failures`.

## Fallback positioning

When a run's font has no GPOS mark lookups, mark glyphs are flagged
`GlyphFallbackMark` and the renderer centres/stacks them by combining class
(the pre-existing approximation). GPOS-positioned marks are flagged
`GlyphGposMark` and use the font's anchors.

## Not covered

Indic/Thai/Khmer/Myanmar syllable shaping, Hangul jamo shaping beyond
normalization, USE, `aalt`/stylistic sets UI, variation-indexed device
tables, `BASE`/`JSTF`/`MATH`, vertical layout (`vert`, `vhea`).
