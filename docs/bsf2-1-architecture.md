# BSF 2.1 architecture — shared bidirectional text integration (2026-11-12)

Branch `feature/bsf-2-1-text-integration`, created from `test` @ `544a51e`
(clean tree; `544a51e` already contains the BSF 2.0 stabilization, the
VirtIO-GPU recovery and the virtio-tablet/mouse fixes — none of them were
touched). Nothing is committed.

BSF 2.1 extends BSF 2.0; it does not replace any part of it. There is still
exactly one UAX #9 implementation (`bsf/bidi.cpp`), one shaper
(`bsf/shape.cpp`, `bsf/otl.cpp`) and one layout engine (`bsf/layout.cpp`).
Notepad, BursztynUI and Husarz all call into them.

## What was wrong in BSF 2.0

| Component | BSF 2.0 behaviour | Consequence |
|---|---|---|
| Notepad | wrapped a paragraph with `layout_text`, then re-laid out **every visual line separately** with `BaseDirection::Auto` for drawing, caret, hit testing and selection | each wrapped line re-detected its direction: an RTL paragraph whose continuation line began with Latin/digits flipped to LTR; RTL paragraphs were left-aligned |
| BursztynUI `PoleTekstowe` | caret/hit-test via `measure_prefix`/`hit_test`, but Left/Right logical and selection drawn as **one** x-range | wrong highlight for mixed selections, arrows moved logically in RTL |
| Husarz | words measured/painted **per code point** (no contextual Arabic forms), no CSS `direction`/`unicode-bidi`, no HTML `dir`, no reordering | Arabic rendered in isolated forms, all text left-to-right |

## Layers

```
              ┌──────────── one UAX #9, one shaper, one TextLayout ───────────┐
 bsf/bidi.cpp  bsf/shape.cpp+otl.cpp  bsf/layout.cpp  bsf/runtime.cpp (fonts,
                                                       build_layout, draw_layout_line)
              └───────────────────────────────────────────────────────────────┘
        ▲                    ▲                         ▲
 Notepad (PoleWielowierszowe)  BursztynUI (PoleTekstowe,     Husarz (bidi.h paragraph pass,
 paragraph layouts, owned LRU  CzcionkaUI, rysuj_linie_ukladu) layout.h shaped runs + L2,
                                                              paint.h, control_text.h forms)
```

## Shared API changes (backward compatible)

All additions are new members/functions or fields with defaults equal to
BSF 2.0 behaviour; no existing signature changed.

`bsf/shape.h` — `ShapeOptions::context_before/context_after`: logical
neighbours outside the run, read only by the cursive joining state machine
(HarfBuzz pre/post-context semantics; transparent context ignored). Used by
the layout for interior run boundaries (font fallback, level split) and by
Husarz for fragments of different DOM nodes. The HarfBuzz oracle rows are
unchanged (context defaults to 0).

`bsf/layout.h`
- `LayoutOptions::letter_spacing` (16.16 px, after every shaped cluster,
  never inside a grapheme or ligature, not applied to cursive scripts),
  `word_spacing` (CSS word-separator characters), `context_before/after`.
- `TextLayout::caret_x_on(byte, line)`, `move_visual_on(byte, line, dir,
  &moved)` (strictly visual, no logical fallback), `line_edge(line, dir)`,
  `generation()` (unique per build; 0 when cleared). `caret_x`,
  `move_visual`, `hit_test`, `selection` keep their BSF 2.0 semantics
  (`move_visual` now shares the nearest-caret search with `move_visual_on`).

`bsf/runtime.h`
- `LayoutRequest` + `build_layout(TextLayout&, …)`: builds into a
  caller-owned layout with the runtime font resolver (same shaping, fallback
  and counters as `layout_text`), so editors/browser keep bounded caches of
  their own instead of thrashing the shared 4-slot cache.
- `draw_layout_line(layout, line, x, y, px, bold, italic, family, mask,
  ctx)`: one visual line (italic now honoured); `draw_layout` is unchanged
  and implemented on the same internal line renderer.
- `TextStatistics::owned_builds` (appended field; observer offsets kept).

`bursztyn_ui/czcionka.h` — `enum class KierunekTekstu { Automatyczny,
OdLewej, OdPrawej }` (per control, never global), `CzcionkaUI::kierunek_bsf`,
`CzcionkaUI::rysuj_uklad_bsf`.

`bursztyn_ui/rysowanie.h` — `KontekstRysowania::rysuj_linie_ukladu(pt,
layout, line, colour, px)`; the BSF draw path was factored into
`rysuj_maski_bsf` shared by `rysuj_tekst_bsf` (unchanged output).

## Paragraph-level bidi (all three integrations)

UAX #9 P2/P3 and X1–I2 are resolved **once per paragraph** over the whole
paragraph text; line breaking uses shaped advances; L1/L2 are applied per
visual line afterwards (`TextLayout::build`, already true in BSF 2.0 for a
single layout). BSF 2.1 makes every client use that model:

- Notepad keeps one layout per logical paragraph and draws/edits its lines
  (`docs/bsf2-1-notepad-rtl.md`).
- Husarz resolves levels per block-container paragraph over the inline DOM
  content with CSS-generated controls, then applies L2 per formatted line
  (`docs/bsf2-1-husarz.md`).

## Logical vs visual

Storage, clipboard, files, DOM text, form values and submissions are always
logical UTF-8. Visual order exists only in layouts (glyph arrays) and in
fragment x positions. The tests compare bytes before/after every operation.

## Caches and invalidation

| Cache | Bound | Key / invalidation |
|---|---|---|
| Notepad paragraph layouts | 4 per field, LRU | paragraph bytes (hash + exact compare), px, wrap width, direction, `BSF::generation()` (font config / catalog). An edit rebuilds only the edited paragraph. |
| `PoleTekstowe` layout | 1 per field | value bytes, px, direction, generation; never for password fields |
| Husarz shaped widths | 1,024 entries (direct-mapped) | 64-bit hash of fragment bytes + font px/bold/italic/family, level parity, letter/word spacing, joining context, override flag, generation |
| Husarz fragment layouts | 32, LRU | same key + exact byte compare |
| Husarz form-control layouts | 4, LRU | value bytes + style + width + direction + generation; passwords excluded |
| Husarz bidi level runs | 16,384 runs/document | recomputed when `Document::revision` or the style generation changes; a width-only reflow (resize) reuses them |
| shared `layout_text` | 4 (BSF 2.0, unchanged) | unchanged |

All caches are per process (Ring 3); nothing is shared between processes and
no font data is parsed in the kernel.

## Kernel image

See `docs/bsf2-1-performance.md` for the measured `__kernel_end`.
