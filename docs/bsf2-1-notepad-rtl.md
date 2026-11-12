# BSF 2.1 — Notepad paragraph-level RTL

Source: `bursztyn_ui/pole_wielowierszowe.h` (Notepad's editor control,
`programy/notatnik/notatnik.cpp` unchanged).

## Paragraph model (A1)

- A **logical paragraph** is the text between `\n` (a preceding `\r` of a
  CRLF pair is excluded from the paragraph, as before). Inside a Notepad
  paragraph, Unicode paragraph separators (U+2029, U+0085, U+001C–U+001E) are
  UAX #9 paragraph separators: the layout gives each part its own paragraph
  level while it stays in the same Notepad line.
- Soft wrapping (automatic line breaking) never starts a new paragraph.
- Each paragraph that needs complex layout (`needs_complex_layout`, or an
  explicit RTL direction) is laid out **once** with `BSF::build_layout`
  (wrap width = text view width when wrapping is on, else unbounded). Its
  visual lines become Notepad visual lines (`Linia::akapit`, `linia_ukladu`,
  `poziom`). Drawing, caret, hit testing and selection all use that same
  paragraph layout (`uklad_linii`), so P2/P3 and X1–I2 are resolved over the
  whole paragraph and L1/L2 per visual line.
- Plain LTR paragraphs keep the BSF 2.0 per-cluster path (unchanged pixels
  and the 60,000-character benchmark path).
- Paragraphs longer than 65,536 bytes (`MaxLayoutBytes`) or with no usable
  face fall back to the legacy LTR path (documented limit, no crash).

## Direction and alignment (A2)

- `ustaw_kierunek_tekstu(KierunekTekstu)` per control: `Automatyczny`
  (first strong character, P2/P3), `OdLewej`, `OdPrawej`. Explicit values
  override detection for every paragraph of the control; an explicit RTL
  paragraph of plain ASCII also uses the bidi layout (punctuation order and
  alignment).
- RTL paragraphs are right-aligned within the text view width (wrapping) or
  within max(view width, widest line) (horizontal scrolling); LTR lines keep
  x = 0. The alignment shift (`Linia::przesuniecie`) is applied identically
  to drawing, caret x, hit testing and selection spans.

## Caret (A3)

- Caret positions are grapheme boundaries; inside a ligature (lam-alef)
  graphemes get equal visual cells (BSF 2.0 model).
- A byte at a **soft-wrap boundary** is both the end of line *k* and the
  start of line *k+1*. Notepad keeps a caret **affinity**: downstream (line
  *k+1*) by default; upstream (end of line *k*) after End, after a click past
  the end of line *k* and after Up/Down landing on a line end. Every edit and
  every other movement resets it. (BSF 2.0 always showed such a caret at the
  end of the previous line, so Home on a continuation line displayed the
  caret one line too high.)
- Where two logical offsets share one visual position (e.g. the RTL space
  before an LTR number), the position is displayed once and arrows visit it
  once (deterministic: the logically closest offset wins).

## Keyboard (A4)

| Key | Behaviour |
|---|---|
| Left / Right | visual movement on the caret's visual line (`move_visual_on`). At the visual edge the caret continues on the adjacent visual line in the **reading order of the current paragraph** (Right is forward in LTR, backward in RTL) and lands on the edge from which the same key keeps progressing (Right → leftmost caret, Left → rightmost). A **backward** crossing into a line of the opposite direction is refused. Proof of termination: a held key moves monotonically through a run of same-direction lines and only enters a new run forward, so it can never revisit a position (`[LTR][RTL]` paragraphs would otherwise cycle LTR-end → RTL-start → LTR-end). |
| Shift+Left/Right | same movement, extends the logical selection |
| Left/Right with a selection | collapse to the visually left/right side (logical start/end, swapped in RTL paragraphs) |
| Ctrl+Left/Right | word movement (space-delimited, grapheme-safe), logical direction swapped in RTL paragraphs |
| Up / Down / PageUp / PageDown | preferred visual x (kept across short lines; only horizontal moves update it) |
| Home / End | logical start / end of the caret's **visual** line (shown at the right/left edge of an RTL line); End keeps the caret on that line |
| Ctrl+Home / Ctrl+End | document start / end |
| Backspace / Delete | one grapheme, logical (unchanged) |

## Selection (A5)

`spany_zaznaczenia(line, …)` returns the visual spans of the logical
selection on a line from `TextLayout::selection`; a selection crossing a
direction boundary yields several disjoint rectangles; drawing paints each
span and re-draws the text clipped to it in the selection colour.

## Clipboard and files (A6)

Copy/cut/paste/save/save-as/reopen/undo/redo operate on the logical buffer
only; tests compare bytes for mixed RTL/LTR text, CRLF, isolates, niqqud,
lam-alef, U+2029 and malformed UTF-8 (`\xff \xc0\xaf`, truncated sequences).

## Performance (A7)

- No layout per keypress for caret movement: the paragraph layout is reused
  from the owned cache (long mixed paragraph: 9,395 hits / 255 misses in the
  host test).
- Edits rebuild only the edited paragraph's layout (content-keyed cache).
- The 60,000-character benchmark (`make test-notepad-unicode-host`) is the
  pure-ASCII legacy path; see `docs/bsf2-1-performance.md` for numbers.

## Tests

`make test-notepad-bidi-host` (production Notepad + PoleWielowierszowe +
BSF runtime with DejaVu fonts, ASan/UBSan); `make test-notepad-unicode-host`
(BSF 2.0 suite, unchanged); `make bench-notepad-bidi` (latency, no
sanitizers). Results: `docs/bsf2-1-testreport.md`.

## Limitations

- No UI command in the Notepad app to switch the direction (the control API
  exists; adding a menu entry is left to the app owner).
- Word movement uses spaces, not UAX #29 word boundaries (unchanged rule).
- A held arrow key does not cross backward into an opposite-direction
  paragraph (by design, see above); Up/Down, Home/End and the mouse reach it.
- Paragraph layouts are rebuilt in full after an edit inside that paragraph
  (BSF 2.0 did the same); a 20 KB paragraph costs tens of ms per keystroke on
  the host — incremental relayout is a BSF 2.2 item.
