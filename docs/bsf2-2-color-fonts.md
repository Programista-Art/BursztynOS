# BSF 2.2 — colour fonts and emoji

Code: `bsf/color.inc`, `bsf/png.inc`, `bsf/inflate.inc` (BSF core, Ring 3),
colour cache and drawing in `bsf/runtime.cpp`, BursztynUI
(`bursztyn_ui/czcionka.h`, `rysowanie.h`), Husarz (`programy/husarz/paint.h`).

## Formats

| Format | Status | Notes |
|---|---|---|
| COLRv0 / CPAL | Verified | layer records, palettes (index validated), foreground `0xFFFF`, layer order, alpha compositing; layer glyphs rasterized by the shared outline rasterizer |
| COLRv1 | Verified (host, vs Skia) | every paint format 1–32: ColrLayers, Solid, Linear/Radial/Sweep gradients (pad/repeat/reflect colour lines), Glyph (clip), ColrGlyph, Transform/Translate/Scale/Rotate/Skew (+ around centre), Composite (all 28 modes: Porter–Duff, separable and HSL blend modes), Var* variants through the COLR ItemVariationStore and DeltaSetIndexMap at the face instance, ClipList boxes (incl. variable boxes) |
| CBDT/CBLC | Verified for PNG image formats 17, 18, 19 | index subtables 1–5, strike selection (smallest ≥ px, else largest), area-average scaling; monochrome/uncompressed CBDT image formats are **not** supported |
| sbix | Verified for `png ` | strike selection, `dupe`; `jpg `, `tiff`, `pdf `, `mask` are **not** decoded (glyph falls back to its outline) |
| SVG (`SVG `) | Not supported | |

Priority per glyph: COLRv1 → COLRv0 → sbix → CBDT → monochrome outline.
Faces that only carry colour strikes (no `glyf`/CFF, e.g. Noto Color Emoji
CBDT) are accepted as `OutlineFormat::Bitmap`.

## PNG

Own bounded decoder (`decode_png`): signature, chunk walk (≤ 4,096 chunks,
CRC not checked), IHDR first, colour types 0/2/3/4/6 with their legal bit
depths (1/2/4/8/16), PLTE/tRNS, non-interlaced only (interlaced → reject),
≤ 1,024 px per side and ≤ 512×512 pixels, IDAT total ≤ 8 MiB, zlib output
exactly `(stride+1)·height`, filters 0–4 (Paeth), palette indices
validated. No PNG decoding in Ring 0.

## Safety limits (COLR)

paint depth 32, 8,192 paints visited per glyph, 4,096 layers, 2,048 glyph
references, PaintColrGlyph cycle detection (path of up to 64 glyphs),
8 temporary canvases (each ≤ 128×128), bitmap ≤ 128×128, colour-line ≤ 64
stops, transforms with non-invertible matrices paint nothing, gradient
math without libm (bounded series for sin/cos/atan2).

## Emoji behaviour

- Grapheme clusters are UAX #29 (unchanged): ZWJ sequences, skin-tone
  modifiers, regional-indicator flags, keycaps and VS16 sequences are one
  cluster for editing, carets and selection.
- Shaping: emoji strings take the shaped layout (`needs_complex_layout` is
  true for U+1F000.., ZWJ, keycap); GSUB `ccmp` ligatures of the emoji font
  form the sequence glyphs; default ignorables are matched as ligature
  components when expected ("skip maybe") and deleted when the font has no
  space glyph (both as HarfBuzz). 22/22 HarfBuzz rows exact on the CBDT and
  COLRv1 emoji fonts.
- Fallback: clusters with emoji presentation (VS16, keycap, regional
  indicators, skin tones, ZWJ between pictographs, Extended_Pictographic
  scalars ≥ U+1F000) prefer a colour face that covers the whole cluster;
  VS15 prefers a monochrome face. Characters below U+1F000 without VS16 (☺,
  ✌, ❤) keep text presentation when a text font covers them. This is an
  approximation of the Unicode `Emoji_Presentation` property (not in the
  BSF Unicode data); e.g. ⌚ (U+231A, Emoji_Presentation=Yes) renders as
  text if a text font covers it.
- Missing colour glyph: an emoji no installed face covers is drawn with the
  bitmap fallback box (GlyphMissing); a colour glyph that fails to render
  falls back to its monochrome outline when it has one.
- Baseline: bitmaps are placed with their strike metrics (CBDT bearings,
  sbix origin offsets) or the COLR clip box in font units, on the text
  baseline.
- Drawing: `draw_layout_line_color` (runtime) → BursztynUI (buttons, menus,
  labels, single-line fields, Notepad: every `rysuj_tekst_bsf` /
  `rysuj_linie_ukladu` path) and Husarz text fragments and form controls.
  The old mask-only API is unchanged (colour glyphs are skipped there).
- The Terminal grid keeps its own renderer: emoji occupy their cells but
  are drawn monochrome/bitmap (no colour in the cell renderer).

## System emoji font

The production ISO ships **Noto Color Emoji COLRv1** (googlefonts/noto-emoji
v2.047, OFL, 4.8 MB, unmodified) as `/czcionki/ttf/NotoColorEmoji-COLRv1.ttf`.
It is loaded only by processes that draw emoji (whole-file, shared by faces).

## Verification

`make test-bsf22-color-host` (1,093 checks): CBDT — 53 emoji of a Noto Color
Emoji subset identical to FreeType's BGRA bitmaps at the 109-ppem strike,
scaled sizes 16/24/48 with consistent placement; sbix — exact pixels of the
generated strikes, `dupe`, strike selection, `jpg ` and corrupt PNG
handled; COLRv0 — layers, palettes and foreground vs FreeType at 24/48/96 px
(mean difference < 4 %); COLRv1 — 52 Noto COLRv1 emoji vs blackrenderer
0.6.0 + Skia at 64 px (mean difference 0.38 %, worst 0.72 %; one glyph
skipped because the reference renderer cannot draw it); cycle and depth
limits; oversized PNG; 6,000 table mutations. `make test-bsf22-emoji-host`
(192 checks): presentation selectors, ZWJ/skin tone/flag/keycap sequences as
one cluster and one colour glyph through the runtime, mixed text and emoji
with baseline checks, missing emoji, colour cache.
