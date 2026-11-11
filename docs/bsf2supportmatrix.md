# BSF 2.0 support matrix — 2026-11-11

Branch `feature/bsf-2-advanced-text-layout` (from `test` @ `950352a`).
Status vocabulary:

- **Verified** — implemented and verified by the tests/evidence named here.
- **Partially verified** — implemented; some aspects lack dedicated evidence.
- **Partial** — only part of the feature exists; limits are stated.
- **Experimental** — present but not suitable for release claims.
- **Not supported** — not implemented. No placeholder code exists for it.

## Unicode

| Capability | Status | Evidence / limits |
|---|---|---|
| Unicode data version | **15.1.0** (pinned, SHA-256 in `narzedzia/unicode/15.1.0/SHA256.json`) | Same version as the existing UAX #29 data; no mixed versions. |
| UTF-8 decoding, malformed input | **Verified** | Existing `bsf::utf8` (U+FFFD per bad byte); BidiText/layout fuzzing and malformed cases in `test_bsf_bidi`, `test_bsf_layout`. |
| UAX #29 graphemes | **Verified** (pre-existing) | Official GraphemeBreakTest 1187/1187 (`make test-bsf-graphemes-host`). |
| UAX #9 bidirectional algorithm (X1–X10, W1–W7, N0–N2, I1–I2, L1–L2, BD16 brackets, isolates, overrides, embeddings, overflow at depth 125) | **Verified** | Official **BidiTest.txt 770,241/770,241** and **BidiCharacterTest.txt 91,707/91,707** pass (`make test-bsf-bidi-host`). |
| L3 (combining marks reordering in RTL) | Not applied as a separate rule | Marks stay with their base through glyph-level L2 + GPOS; UAX #9 marks L3 optional. |
| L4 mirroring | **Verified** | `Bidi_Mirroring_Glyph` substitution when the font has the mirror, else `rtlm`; oracle rows with brackets in RTL (`[قوس]`, `(סוגריים)`). |
| Script itemization (Scripts.txt) | **Partial** | Common/Inherited inherit the neighbouring script; Script_Extensions not used. |
| Line breaking (UAX #14) | **Partial** | Documented subset: mandatory breaks, spaces, ZW, GL/WJ, OP/CL/CP/EX/IS/SY/NS, HY/BA, CJK ideographs/kana/Hangul. No dictionary breaking (Thai/Lao/Khmer), no LB rules for numbers/quotations beyond the subset. |
| Canonical normalization in shaping | **Verified** | Shortest-form decomposition + modified-CCC reordering + recomposition against the font's cmap (oracle rows incl. Vietnamese, Hebrew niqqud, `e◌́`). |

## OpenType layout

| Capability | Status | Evidence / limits |
|---|---|---|
| GDEF glyph classes, mark attachment classes, mark glyph sets | **Verified** | Used by every GPOS/GSUB oracle row. ItemVariationStore parsed only as an offset (no variations). |
| ScriptList / LangSys / FeatureList / LookupList, lookup flags | **Verified** | Script preference incl. `DFLT`/`dflt`/`latn` fallback; Indic v2→v1 tags. FeatureVariations (GSUB/GPOS 1.1) ignored. |
| GSUB 1 single, 2 multiple, 3 alternate, 4 ligature, 5 context, 6 chained context, 7 extension, 8 reverse chaining | **Verified** | Every type occurs in at least one oracle font whose rows all match HarfBuzz exactly (GSUB 2 Noto Naskh; 5 and 7 Noto Nastaliq Urdu — 131 extension lookups; 8 Noto Sans Coptic, visible as overline variants). Per-type application counters are not instrumented. Alternate uses the first alternate (no UI for others). |
| GPOS 1 single, 2 pair (fmt 1/2), 3 cursive, 4 mark-to-base, 5 mark-to-ligature, 6 mark-to-mark, 7 context, 8 chained context, 9 extension | **Verified** | Every type occurs in an oracle font with exact rows (3 and 7 Noto Nastaliq Urdu — up to 10 cursive y-offsets per word match exactly; 8 Noto Sans/Noto Sans Arabic; 9 Noto Sans). Device tables apply only with an explicit ppem (layout passes none, matching unhinted HarfBuzz). Anchor format 2 contour points are not used (same as unhinted shaping). |
| Feature set | **Verified** | `rvrn ltra ltrm rtla rtlm ccmp locl isol fina fin2 fin3 medi med2 init rlig calt rclt mset liga clig dlig(opt-in) kern mark mkmk curs dist abvm blwm`, staged like HarfBuzz's default/Arabic shapers. |
| Legacy `kern` table fallback | **Verified** | Used only when GPOS has no `kern` lookups (pre-existing behaviour preserved). |
| Shaping oracle agreement | **Verified** | **110/110 rows exact** (1,164 glyphs; glyph id, cluster, x/y advance, x/y offset) vs host HarfBuzz across 12 fonts (`make test-bsf-shaping-host`). |
| Mark positioning without GPOS data | **Partial (approximation)** | Flagged `GlyphFallbackMark`; centred/stacked by combining class. Distinct from real GPOS anchors. |

## Scripts

| Script | Status | Notes |
|---|---|---|
| Latin, Cyrillic, Greek (incl. combining marks, ligatures, kerning) | **Verified** | Oracle rows (Polish, Vietnamese, Russian, Greek polytonic); native Settings preview. |
| Hebrew (RTL, niqqud, mirroring) | **Verified** | Oracle (DejaVu, Noto Sans/Serif Hebrew); UAX #9 tests; native QEMU Settings preview. |
| Arabic script (joining, lam-alef, harakat, Arabic-Indic digits), Persian/Urdu/Uyghur letters, Nastaliq cursive | **Verified** | Oracle (DejaVu, Noto Naskh/Kufi/Sans Arabic, Noto Nastaliq Urdu); native QEMU Settings preview (DejaVu). Unicode presentation-form fallback for fonts **without** GSUB is not implemented. |
| Coptic | **Verified** (shaping) | Oracle rows incl. reverse-chaining overlines (Noto Sans Coptic). No Coptic font ships in the ISO. |
| Syriac | **Partial** | Joining types handled; Alaph/Dalath-Rish joining groups (fin2/fin3/med2 contexts) not modelled. |
| Mongolian, N'Ko, other joining scripts | **Partial** | Generic joining state machine only; no script-specific variation-selector logic. |
| CJK (Han, Kana, Hangul) | **Partial** | Unicode handling, graphemes, line breaking subset; only TrueType-outline fonts load (no CFF), so Noto CJK (CFF, TTC, >16 MiB) cannot be used. Hangul jamo composition via normalization only. |
| Indic (Devanagari, Bengali, Tamil, …), Thai/Lao/Khmer/Myanmar | **Not supported** (shaping) | Classified, but no reordering/syllable shaping. Generic GSUB/GPOS alone is **not** Indic support. Text is displayed in logical order without syllable reordering. |
| Emoji | **Partial** | Grapheme semantics (ZWJ/RI/modifiers) are correct for editing; no colour glyph formats, so emoji render only if a monochrome outline font covers them. |

## Font formats

| Format | Status |
|---|---|
| TrueType outlines (`glyf`/`loca`), cmap 4/12, `kern` | **Verified** (pre-existing, extended to 65,535 glyphs) |
| OpenType CFF (`OTTO`) | **Not supported** — faces without `glyf` are rejected at discovery as before. |
| CFF2 | **Not supported** |
| TrueType Collections (TTC) | **Not supported** |
| WOFF / WOFF2 | **Not supported** (Husarz does not load web fonts) |
| Variable fonts (`fvar avar gvar HVAR VVAR MVAR STAT`, CFF2 blends) | **Not supported** — `fvar`/`COLR`/`CPAL` table bytes are recorded on `Face` but never interpreted. |
| Colour fonts (COLR/CPAL v0/v1, CBDT/CBLC, sbix) | **Not supported** |

## Integration

| Component | Status | Notes |
|---|---|---|
| BursztynUI (labels, buttons, menus, dialogs, single-line fields, lists, …) | **Partially verified** | All BSF text drawing goes through `CzcionkaUI::rysuj_bsf`; measurement through `measure_text/measure_prefix/hit_test`. Complex strings use one shared cached layout for both. Single-line fields: caret and hit-test are bidi-correct; selection highlight is a single x-range and arrow keys are logical (limitation). |
| Notepad (`PoleWielowierszowe`) | **Partially verified** | Shaped wrapping, bidi caret, visual Left/Right, per-span bidi selection, grapheme Backspace/Delete, byte-exact save/reopen (host suite 60,662 checks). Each visual line resolves its own paragraph direction; RTL paragraphs are left-aligned. Native check: Notepad launches and accepts input; RTL editing natively is not yet automated (QMP cannot type non-ASCII). |
| Settings → Fonts | **Verified** | Hebrew + Arabic samples drawn natively by BSF 2.0 (QEMU counters + screenshots). Font format / axes / writing-system info: **not added**. |
| Terminal (Shell) | **Partial, by design** | Logical cell order is preserved; no visual reordering or shaping in the cell grid (keeps command text, paths and cursor addressing exact). |
| Husarz browser | **Not integrated** | Its coroutine layout/paint pipeline still shapes per code point (with existing grapheme-correct forms). CSS `direction`/`unicode-bidi` unsupported. See `bsf2complextextediting.md`. |
| Apps built with `BSF_COMPLEX_TEXT=0` (calculator, snake, demos, tests, sound test, wallpaper, splash) | **Legacy LTR path** | Excluded because every app is embedded in the 15 MiB kernel image (see `bsf2architecture.md`). |

## Platform verification

| Environment | Status |
|---|---|
| UEFI QEMU 1280×720 (production ISO) | **Partially verified** — 2/4 final runs pass; 2 stopped by the intermittent virtio-gpu control-command timeout already recorded in 2026-10-31 logs (pre-existing; see test report §7). BSF checks passed in every run that reached them. |
| UEFI QEMU 1920×1080 (grub.cfg-only mode fixture) | **Verified** |
| BIOS QEMU (guest chose 1920×1080) | **Verified** |
| UEFI QEMU 2560×1440 (grub.cfg-only mode fixture) | **Verified** (final run; earlier attempts failed for harness timing / host load, see report) |
| Installed-disk boot, Windows QEMU, physical hardware | **Not tested** |
