# BSF 2.1 — BursztynUI bidirectional editing

## Single-line field (`bursztyn_ui/pole_tekstowe.h`)

When the value needs complex layout (`needs_complex_layout`) or the field
direction is explicitly RTL, the field owns one `BSF::TextLayout` of its
logical value (`uklad()`, rebuilt only when value bytes, px, direction or
`BSF::generation()` change). That single layout is used for:

| Function | BSF 2.1 |
|---|---|
| drawing | `KontekstRysowania::rysuj_linie_ukladu` (shaped glyphs, visual order) |
| caret x | `caret_x_on(byte, 0)` + alignment shift |
| hit testing (click, drag) | `hit_test(x - shift, 0)` |
| selection highlight | `TextLayout::selection` → one rectangle **per visual span**; text re-drawn clipped to each span |
| Left / Right | `move_visual_on` (visual); at the visual edge the caret stays |
| Left / Right with selection | collapse to the visually left/right side |
| Shift+Left / Shift+Right | visual movement extending the logical selection |
| Home / End (+Shift) | logical start / end (unchanged) |
| Backspace / Delete | one grapheme, logical (unchanged) |
| horizontal scrolling | `dopasuj_przewiniecie` with the same caret x (unchanged algorithm) |
| alignment | an RTL value that fits is right-aligned in the client width |

Plain LTR values never build a layout: geometry is pixel-identical to BSF
2.0 (`x_kursora() == CzcionkaUI::pozycja_x_z_bajtu` is asserted for Polish
text). Password fields never lay out the real value (only mask characters
are measured), so no plaintext enters any layout cache; the test asserts that
`owned_builds` does not change for a password field holding Hebrew.

## Direction API

`PoleTekstowe::ustaw_kierunek_tekstu(KierunekTekstu)` and
`PoleWielowierszowe::ustaw_kierunek_tekstu(KierunekTekstu)`:
`Automatyczny` (default, existing apps unchanged), `OdLewej`, `OdPrawej`.
The value is a member of each control — no global application state.
Diagnostics used by the tests: `x_kursora()`, `poziom_bazowy()`,
`czy_uklad_zlozony()`, `przewiniecie()`, `spany_zaznaczenia()`.

## Other controls (B3 audit)

All other text in BursztynUI — labels, buttons, menus, list items, tabs,
dialogs, search/settings fields that derive from `PoleTekstowe`, tooltips,
tables — is drawn by `KontekstRysowania::rysuj_tekst` → `CzcionkaUI::rysuj_bsf`,
which for complex strings draws the shared shaped layout (BSF 2.0) with
automatic direction; measurement uses the same layout. Plain Polish/English
labels keep the legacy path and geometry. Multi-line fields are
`PoleWielowierszowe` (Notepad model, `docs/bsf2-1-notepad-rtl.md`).
Read-only controls do not need caret logic. Not changed in BSF 2.1: label
alignment of RTL captions (controls keep their configured alignment;
direction-based mirroring of whole widgets is out of scope).

## Tests

`make test-bursztynui-bidi-host` (production `PoleTekstowe` inside the
production Notepad binary with the BSF runtime and DejaVu fonts,
ASan/UBSan): RTL and mixed values, visual walks (strictly monotone x, no
revisits, every distinct caret position reached), Shift selection, multiple
spans, Home/End, mouse click = caret geometry, drag selection, grapheme
Backspace/Delete, logical copy/cut/paste, Arabic typing order, scrolling in a
narrow field, explicit direction, password isolation, font catalog refresh,
label measurement. `make test-bsf-ui-host`, `make test-gui-text-input-host`
(BSF 2.0 suites) still pass. Results: `docs/bsf2-1-testreport.md`.

## Limitations

- Double-click selects all (existing behaviour, not word-wise).
- Native non-ASCII keyboard input cannot be injected through QMP; native
  RTL typing is covered by host tests and the manual procedure in the test
  report.
