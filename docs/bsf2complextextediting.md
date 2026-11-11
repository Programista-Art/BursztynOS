# BSF 2.0 — complex text editing and application integration

## Editing model (`TextLayout`)

- **Logical storage is authoritative.** Documents, clipboards and saved
  files are the original UTF-8; layout keeps its own copy and never writes
  back. Copy/paste/save/reopen are byte-exact (Notepad host suite).
- **Caret positions are grapheme boundaries** (UAX #29). Ligatures that span
  several graphemes (e.g. lam-alef) are split into equal visual cells, in
  visual direction, so every grapheme has a caret position.
- **Primary caret**: at the leading edge of the grapheme starting at the
  offset (left edge for LTR, right edge for RTL); at a line end, the trailing
  edge of the logically preceding grapheme.
- **Hit-testing** returns the boundary nearest the half of the visual cell
  that was hit, respecting the cell's direction.
- **Visual movement** (`move_visual`): the nearest displayable caret strictly
  to the left/right, ties resolved to the logically closest offset; at the
  visual edge it continues logically in the paragraph direction. It is
  monotone in x, so it always terminates (a ping-pong at direction boundaries
  found during development was fixed this way). O(log k) via a caret table.
- **Logical movement** (`move_logical`): next/previous grapheme.
- **Deletion**: Backspace/Delete remove whole graphemes (existing behaviour,
  unchanged); UTF-8 is never split.
- **Selection** (`selection`): a logical range is returned as visual spans
  per line; a selection crossing a direction boundary yields multiple spans.

## Notepad (`bursztyn_ui/pole_wielowierszowe.h`)

- Paragraphs that need complex layout wrap through `layout_text` with the
  same shaped advances that draw them (Arabic contextual forms differ in
  width from isolated forms, so the old per-scalar metrics would disagree).
- Caret x, Left/Right (visual), selection highlight (per span, clipped
  redraw) use the shared layout of the visual line.
- The per-line "needs complex layout" flag is computed once per line rebuild;
  without it a 60,000-character line made each arrow key rescan the line
  (found by the host benchmark: 35 s → back to 10–11 s for 60,000 keys).
- Limitations: direction resolved per visual line; RTL paragraphs are
  left-aligned; Up/Down keep a preferred x (works with bidi carets but is not
  bidi-aware in the paragraph sense).

## BursztynUI

All BSF drawing goes through `CzcionkaUI::rysuj_bsf` (labels, buttons,
menus, dialogs, lists, tabs, single-line fields …). For complex strings it
calls `BSF::draw_layout` on the cached layout; measurement
(`zmierz_tekst`, `pozycja_x_z_bajtu`, `znak_z_pozycji_x`) goes through
`measure_text`, `measure_prefix` (primary caret) and `hit_test` on the same
layout. Font family and theme settings are unchanged. Limitation: single-line
fields highlight selection as one x-range and move the caret logically.

## Terminal

The Shell links the full engine, but the terminal grid intentionally keeps
logical cell order and one scalar per cell: no visual reordering and no
joining are applied there. This preserves command text, paths, history,
line editing and cursor addressing exactly. RTL text in the terminal is
therefore displayed in logical order (documented limitation, by design).

## Husarz browser — not integrated

Husarz has its own cooperative layout/paint pipeline (`programy/husarz/
layout.h`, `paint.h`): work-sliced coroutines, per-code-point glyph painting
with CSS letter/word spacing, text fragments that own selection and
JavaScript-visible offsets. Its forms already edit by grapheme. Integrating
BSF 2.0 requires: shaping per inline text fragment with a cached
`TextLayout`, bidi resolution across inline boxes (CSS `direction`,
`unicode-bidi`, isolates per element), mapping selection through visual
clusters while keeping DOM offsets logical, and yielding inside layout
builds for long paragraphs. This was not started to avoid destabilising the
browser; it is the next integration task.

## Settings → Fonts

The preview now includes `שלום עולם (2026)` and `مرحبا بالعالم لا`, wrapped
at spaces (never inside a shaping run). Native QEMU evidence:
`.bsf-test/bsf2-native/*/settings-fonts-bidi.png` and the BSF counters read
from the Settings process. Font format, axes and supported-writing-system
information was not added (no new formats to describe).
