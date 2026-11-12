# BSF 2.1 — Husarz browser integration

Husarz keeps its architecture (cooperative `Work` coroutines, bounded
documents, fragment-based inline layout, damage-driven paint). BSF 2.1 adds a
bidi paragraph pass and replaces the per-code-point measurement and painting
of complex text with shaped BSF runs. Everything bidi/shaping-related is
compiled only in native builds (`HUSARZ_BIDI` = `BSF_NATIVE &&
BSF_COMPLEX_TEXT`); non-native host builds (the existing Husarz host tests)
keep the previous code path and only gain the CSS/HTML parsing and
direction-aware `text-align`.

## Audit (C1) — where code points were handled one by one

| Stage | File | BSF 2.0 behaviour |
|---|---|---|
| HTML/DOM text | `html.h`, `document.h` | text nodes hold logical UTF-8 in the arena (kept) |
| CSS | `css.h` | no `direction`, no `unicode-bidi`; `text-align: start/end` mapped statically to left/right |
| inline layout | `layout.h` `layout_text_work` | words between collapsible spaces; widths = sum of per-cluster advances (`text_cluster` + `cluster_width`, isolated forms); fragments in logical order, x left-to-right |
| line finish | `finish_line_work` | alignment shift only |
| paint | `paint.h` `paint_work` | one glyph per code point (`paint_alpha_glyph_work`), `MarkPen` approximation for marks |
| forms | `control_text.h`, `paint.h` | `ControlTextWalker` per cluster, LTR only |
| hit testing | `layout.h` `hit_test` | fragment rectangles (kept; follows the new x) |
| selection | forms only (page text has no selection) | — |
| JS | `js_dom.h` | sees DOM strings/values only (no selection offset API) |

## Design (C2–C6)

1. **CSS / HTML direction** (`css.h`, `document.h`): `direction` (inherited)
   and `unicode-bidi` (`normal|embed|isolate|bidi-override|isolate-override|
   plaintext`, not inherited) are parsed in the cascade; `text-align` keeps
   `start`/`end` as keywords resolved against the block's direction at line
   layout. HTML mapping at presentational level (`html_direction`, author CSS
   still wins): `[dir=ltr|rtl|auto]` → `unicode-bidi: isolate` + direction;
   `bdi` → isolate with automatic direction; `bdo` → isolate-override;
   `textarea/pre[dir=auto]` → plaintext; `dir=auto` = first strong character of
   the element text, skipping `bdi/script/style/textarea` and descendants with
   their own `dir` (bounded scan: 4,096 bytes / 512 nodes); `input[dir=auto]`
   uses its value attribute at style time (the editing path below re-detects
   from the live value).
2. **Bidi paragraphs** (`bidi.h`, new): a paragraph is the inline content of
   a block container. The pass walks the DOM in tree order and builds the
   paragraph text in a bounded scratch buffer: text nodes (collapsible
   newlines/tabs as spaces unless `white-space: pre*`), U+FFFC for atomic
   inlines (img, input, button, select, textarea, inline-block), `\n` for
   `<br>` (HTML renders br as LF, a UAX #9 paragraph separator), and the
   controls CSS prescribes for inline `unicode-bidi`
   (embed LRE/RLE…PDF, isolate LRI/RLI…PDI, bidi-override LRO/RLO…PDF,
   isolate-override LRI/RLI+LRO/RLO…PDF+PDI, plaintext FSI…PDI). Block-level
   `plaintext` makes every paragraph auto (P2/P3), block-level overrides wrap
   the whole content. Open controls are re-emitted after a separator or an
   interrupting block. Levels come from the one BSF implementation
   (`BSF::BidiText`) and are stored as per-node level runs
   (`Document::bidi_runs`, `text_level`, `text_level_end`). The DOM text is
   never modified. Paragraphs that are LTR with no RTL/control characters and
   no RTL bidi elements are not analysed (all level 0).
3. **Inline layout** (`layout.h`): a word that needs complex layout (or any
   word when the document has bidi paragraphs) goes through
   `layout_word_bidi_work`: it is split where the level changes (never inside
   a grapheme), every part is measured from its **shaped** BSF layout (explicit
   direction = level parity, CSS letter/word spacing, joining context) or by the
   unchanged per-cluster path for plain LTR parts, and the line-fit decision
   uses those shaped widths. Adjacent fragments of different DOM nodes with
   nothing between them (`<b>بي</b>ت`) exchange their neighbouring scalars as
   cursive context and are re-measured, so shaping crosses compatible inline
   boundaries without merging DOM offsets. Overlong words still break at
   grapheme boundaries (each chunk shaped with its neighbours as context).
   Overridden runs (a level contradicting the strong characters) are shaped
   inside LRO/RLO…PDF so the override survives (`FragmentOverride`).
4. **Lines** (`finish_line_work` → `bidi_line_work`): UAX #9 L2 for each
   formatted line over the line's fragments and the collapsed spaces between
   them (each with its resolved level); x positions are reassigned inside the
   same horizontal extent (no box moves outside it). `text-align: start/end`
   resolve against the block direction (paragraph direction for `plaintext`),
   so RTL blocks are right-aligned by default.
5. **Paint** (`paint.h`): complex fragments are drawn with
   `BSF::draw_layout_line` from the cached shaped layout (contextual forms,
   GSUB/GPOS marks, mirrored brackets), composited with the existing clip,
   damage, opacity and pixel budget. Plain fragments keep the per-glyph path.

## Forms (C8)

`control_text.h`: for text/search inputs and textareas whose value needs
complex layout or whose direction is rtl, one BSF layout of the **logical**
value provides caret geometry, hit testing, painting (per-span selection
highlight), right alignment of RTL lines, wrapping at the content width and
visual Left/Right (`control_horizontal_key`, routed from
`przegladarka.cpp` before the logical `form_key`). `dir=auto` controls
re-detect the direction from the current value. Typing, Backspace/Delete,
clipboard, `input`/`change` events, JS-visible values and `form_query`
submissions are unchanged and logical. Password inputs never take this path:
only `*` masks are measured and painted, so no secret reaches a layout cache
or log.

## DOM offsets and JavaScript (C9)

Husarz exposes no JS selection/offset API (no `selectionStart`,
`Range`, `getSelection`); JS sees `textContent`/`value` strings, which stay
logical. Internally fragments, form cursors and selections are UTF-8 byte
offsets at grapheme boundaries; no UTF-16 conversion layer exists or was
needed. Visual clusters are used only for painting, hit testing and caret
movement.

## Cooperative scheduling (C10)

All new stages are `Work` coroutines with `WorkPoint`s: the bidi pass yields
per node and per 256 bytes, the line pass per item, word layout per part,
paint per fragment. Synchronous BSF calls (one paragraph analysis ≤ 32 KiB,
one fragment shaping ≤ 16 KiB) are charged with `work_units()`, capped to one
slice (`MaxSliceWork` = 2,048), so a tick never fails or exceeds its budget;
the long-page test asserts `driver.maximum <= MaxSliceWork`. Levels are reused
across width-only reflows; shaped widths/layouts are cached per process;
nothing holds DOM pointers across documents (runs are indexed by node id and
reset with the document; caches hold copies of text, not pointers).

## Budgets (adversarial input)

`MaxBidiParagraph` 32 KiB per analysed chunk (a longer paragraph is analysed
in consecutive chunks; the chunk boundary acts as a paragraph boundary),
`MaxBidiSegments` 2,048 text pieces, `MaxBidiRuns` 16,384 runs per document,
`MaxBidiNesting` 8 nested block contexts, `MaxBidiOpen` 32 open inline bidi
elements per context (UAX #9 itself caps depth at 125), 512 items per line
for L2, 32 level parts per word. Exceeding a budget records `LimitBidi`
(`BIDI_RUNS_OR_PARAGRAPH` in the limit log) and leaves the rest at level 0.

## Not supported / limitations

- Atomic inline boxes that are not fragments (inline-block, input, button,
  select, textarea) are not reordered relative to text on an RTL line: L2 runs
  independently on each segment between such boxes (no overlap possible).
  Images are reordered with their boxes.
- Table column order is not mirrored for `direction: rtl` tables; cell
  content is bidi-correct and right-aligned.
- `unicode-bidi` on a block other than plaintext/override has no effect beyond
  `direction` (as in CSS); `text-align: justify` is not supported (unchanged).
- Intrinsic (shrink-to-fit) widths of inline-blocks/tables still use
  per-cluster widths, so shaped Arabic can differ by a few pixels there.
- `input[dir=auto]` style-time direction uses the value attribute; editing
  re-detects live.
- No page-text selection exists in Husarz (unchanged); RTL selection is
  provided in form controls.
- The parser keeps `<textarea>` content on the element, so
  `initialize_controls` starts textareas empty (pre-existing, unchanged).
- Web fonts are not loaded (no WOFF/WOFF2); text uses the installed catalog.

## Tests

`make test-husarz-bidi-host` with the pages in `testy/husarz_bidi/`
(01 Arabic paragraph … 12 Polish/English page), long and adversarial pages;
`make fuzz-bsf21-smoke` (Husarz bidi fuzzer); all previous Husarz host suites.
Results: `docs/bsf2-1-testreport.md`.
