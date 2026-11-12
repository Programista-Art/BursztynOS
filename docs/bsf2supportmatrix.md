# BSF support matrix — BSF 2.1 update 2026-11-12

BSF 2.1 rows (branch `feature/bsf-2-1-text-integration`, from `test` @
`544a51e`) are marked **[2.1]**; evidence: `docs/bsf2-1-testreport.md`.
Earlier history follows.

# BSF 2.0 support matrix — 2026-11-11 (stabilization update)

Originally branch `feature/bsf-2-advanced-text-layout` (merged into `test`
as `36cc9da`); updated on `feature/bsf-2-stabilization` (from `36cc9da`).
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
| GSUB 1 single, 2 multiple, 3 alternate, 4 ligature, 5 context, 6 chained context, 7 extension, 8 reverse chaining | **Verified** | Every type is **applied** (per-type/per-format counters, asserted by `make test-bsf-shaping-host`) by rows that match HarfBuzz exactly. The earlier claim that Coptic rows exercised GSUB 8 was wrong (corrected 2026-11-11: new rows `ⲁ̅Ⲛ̅` …). GSUB 3 is reached only through the opt-in `aalt` feature (first alternate). Not exercised by any real-font row: GSUB 5 fmt 3, GSUB 6 fmt 1. |
| GPOS 1 single, 2 pair (fmt 1/2), 3 cursive, 4 mark-to-base, 5 mark-to-ligature, 6 mark-to-mark, 7 context, 8 chained context, 9 extension | **Verified** | Every type is applied (counted) by exact rows; GPOS 7 fmt 1/3 and GPOS 8 fmt 1/2 have no real-font row. Rows (3 and 7 Noto Nastaliq Urdu — up to 10 cursive y-offsets per word match exactly; 8 Noto Sans/Noto Sans Arabic; 9 Noto Sans). Device tables apply only with an explicit ppem (layout passes none, matching unhinted HarfBuzz). Anchor format 2 contour points are not used (same as unhinted shaping). |
| Feature set | **Verified** | `rvrn ltra ltrm rtla rtlm ccmp locl isol fina fin2 fin3 medi med2 init rlig calt rclt mset liga clig dlig(opt-in) kern mark mkmk curs dist abvm blwm`, staged like HarfBuzz's default/Arabic shapers. |
| Legacy `kern` table fallback | **Verified** | Used only when GPOS has no `kern` lookups (pre-existing behaviour preserved). |
| Shaping oracle agreement | **Verified** | **120/120 rows exact** (1,222 glyphs; glyph id, cluster, x/y advance, x/y offset) vs host HarfBuzz across 12 fonts (`make test-bsf-shaping-host`). |
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

## BSF 2.1 additions

| Capability | Status | Evidence / limits |
|---|---|---|
| **[2.1]** Paragraph-level direction across wrapped lines (shared `TextLayout`) | **Verified** (host) | `test-bsf21-layout-host`, `test-notepad-bidi-host` (continuation line starting with Latin stays RTL) |
| **[2.1]** Line-aware carets (`caret_x_on`, `move_visual_on`, `line_edge`) | **Verified** (host, fuzzed) | 241,797 asserts incl. 1,500 random wrapped paragraphs; `fuzz_bsf21_editing` |
| **[2.1]** CSS letter-/word-spacing in the layout (not inside graphemes/ligatures, not for cursive scripts) | **Verified** (host) | `test-bsf21-layout-host` |
| **[2.1]** Cursive joining context across run / DOM-node boundaries | **Verified** (host) | `test-bsf21-layout-host` (glyph ids equal the joined word), `test-husarz-bidi-host` page 09 |
| **[2.1]** Caller-owned layouts (`build_layout`), line drawing (`draw_layout_line`, italic) | **Verified** (host) | all 2.1 suites; native counters `owned_builds` (test report) |
| **[2.1]** Unicode paragraph separators inside a Notepad line (U+2029 etc.) | **Partially verified** | handled by the layout (own paragraph level per part); host save/reopen sample; not a separate Notepad line count |

## Integration

| Component | Status | Notes |
|---|---|---|
| **[2.1]** BursztynUI single-line field (`PoleTekstowe`) | **Verified** (host); native: see test report | visual Left/Right, Shift selection, multi-span highlight, Home/End, click/drag hit testing, scrolling, right alignment, direction API, password isolation (`test-bursztynui-bidi-host`). Native non-ASCII typing not injectable (QMP). |
| **[2.1]** Notepad (`PoleWielowierszowe`) paragraph model | **Verified** (host); **Partially verified** natively | one direction per logical paragraph, RTL right alignment, explicit direction, affinity at soft wraps, terminating visual navigation across lines, multi-span selection, byte-exact clipboard/save/undo (`test-notepad-bidi-host`). Native: RTL text pasted through the real clipboard, BSF 2.1 paragraph layouts counted in the Notepad process (test report). No Notepad menu command for direction. |
| **[2.1]** Husarz HTML/CSS bidi | **Verified** (host, 12 local pages + long/adversarial + fuzzer); native: see test report | CSS `direction`, `unicode-bidi` (all six values), `text-align:start/end`, HTML `dir=ltr/rtl/auto`, `bdi`, `bdo`, paragraph UAX #9 over inline content, L2 per line, shaped Arabic across inline elements, link hit testing, RTL forms with logical values. Limits: atomic inline boxes (inline-block, form controls) not reordered against text, RTL table column order not mirrored, intrinsic widths per cluster (`docs/bsf2-1-husarz.md`). |
| BursztynUI (labels, buttons, menus, dialogs, single-line fields, lists, …) | **Partially verified** | All BSF text drawing goes through `CzcionkaUI::rysuj_bsf`; measurement through `measure_text/measure_prefix/hit_test`. Complex strings use one shared cached layout for both. Single-line fields: caret and hit-test are bidi-correct; selection highlight is a single x-range and arrow keys are logical (limitation). |
| Notepad (`PoleWielowierszowe`) — BSF 2.0 row (superseded by the [2.1] row above) | **Partially verified** | Shaped wrapping, bidi caret, visual Left/Right, per-span bidi selection, grapheme Backspace/Delete, byte-exact save/reopen (host suite 60,662 checks). Each visual line resolves its own paragraph direction; RTL paragraphs are left-aligned. Native check: Notepad launches and accepts input; RTL editing natively is not yet automated (QMP cannot type non-ASCII). |
| Settings → Fonts | **Verified** | Hebrew + Arabic samples drawn natively by BSF 2.0 (QEMU counters + screenshots). Font format / axes / writing-system info: **not added**. |
| Terminal (Shell) | **Partial, by design** | Logical cell order is preserved; no visual reordering or shaping in the cell grid (keeps command text, paths and cursor addressing exact). |
| Husarz browser — BSF 2.0 row (superseded by the [2.1] row above) | **Not integrated** (2.0) | Its coroutine layout/paint pipeline still shapes per code point (with existing grapheme-correct forms). CSS `direction`/`unicode-bidi` unsupported. See `bsf2complextextediting.md`. |
| Calculator, snake, demos (demo_ui, kontrolki 2/3/4), system tests, sound test, wallpaper, splash | **Full engine** (since 2026-11-11) | Enabled after the desktop BMPs left the kernel image; calculator, snake and Kontrolki 4 launch, render and close natively (QEMU). `BSF_COMPLEX_TEXT=0` remains available as an explicit per-app opt-out; no app uses it. |

## Platform verification

| Environment | Status |
|---|---|
| UEFI QEMU 1280×720 (production ISO) | **Partially verified** — final ISO 2/3 full passes; the third stopped at the Snake stage on the intermittent virtio-gpu timeout, which A/B shows is pre-existing and host-load dependent (`950352a` and `36cc9da` equally affected). |
| UEFI QEMU 1920×1080 (grub.cfg-only mode fixture) | **Verified** (final ISO) |
| BIOS QEMU (guest chose 1920×1080) | **Verified** (final ISO) |
| UEFI QEMU 2560×1440 (grub.cfg-only mode fixture) | **Verified** (final ISO) |
| Installed-disk boot, Windows QEMU, physical hardware | **Not tested** |
