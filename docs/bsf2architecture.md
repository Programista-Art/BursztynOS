# BSF 2.0 architecture — 2026-11-11

Bursztyn Font System 2.0 adds a native bidirectional + OpenType text layout
pipeline to the existing BSF engine. It extends, not replaces, BSF 1.x: the
TrueType parser, rasterizer, catalog, fallback and caches are unchanged in
behaviour, and plain LTR text keeps the exact legacy path.

## Starting state (audit, before any change)

- Branch `test` @ `950352a`, three uncommitted user files
  (`narzedzia/bsf_live_font_qemu_cases.py`, `narzedzia/bsf_process_observer.py`,
  `testy/test_husarz_qemu.py`) — preserved untouched on the feature branch.
- `bsf/bsf.cpp` (793 lines): own sfnt directory, cmap 4/12, `glyf`/`loca`,
  `kern`, `name` (UTF-16BE) parsers; `stb_truetype` 1.26 used **only** as a
  rasterizer of BSF-validated vertices; bounded glyph cache (128 slots).
- `bsf/runtime.cpp`: `/czcionki/ttf` discovery (≤128 faces), family/style
  choice, per-scalar and per-cluster fallback, 16 MiB resident budget.
- Unicode 15.1.0 UAX #29 graphemes (official test passing), `MarkPen`
  approximate mark placement, `TextDirection::ExperimentalRTL` declared but
  unused.
- **No** GSUB, GPOS, BiDi, CFF/CFF2, WOFF, variable or colour support.
- Execution: BSF is compiled with `RING3_FLAGS` into the Ring 3 GUI library
  (`bursztyn_gui.o`) and runs inside each GUI process. Ring 0 only enforces
  the read-only asset policy (`BSF::trusted_asset`) and stores the BSF
  configuration (BWS 92). No font byte is parsed in the kernel.
- Every GUI app except Settings is linked into the kernel image as a blob;
  the image must end below 15 MiB (`RING3_BASE` − 1 MiB guard). Before this
  work the image ended at 0xE287F0 (≈ 860 KiB headroom).

## Pipeline

```
UTF-8 ─► scalars (+byte offsets) ─► graphemes (UAX #29)
      ─► Bidi_Class ─► UAX #9 levels per paragraph (BidiText)
      ─► scripts (Common/Inherited resolved) ─► runs: (level, script) at
         grapheme boundaries ─► font runs: one face for the whole run if any
         face covers it, else per-grapheme fallback merged by face
      ─► shaper per run: normalize ─► cmap + L4 mirroring ─► Arabic joining
         ─► GSUB (staged) ─► advances ─► GPOS (or legacy kern) ─► hide
         default ignorables ─► glyphs in logical order with byte clusters
      ─► line breaking on grapheme widths (UAX #14 subset)
      ─► per line: L1 levels ─► glyph-level L2 reordering ─► 16.16 pen
      ─► visual clusters (ligatures split per grapheme) ─► caret table
      ─► draw (rasterize glyph ids) / measure / caret / hit-test / select
```

Shaping happens before line breaking (widths need shaped advances); L1/L2
are applied per line after breaking, as UAX #9 requires. Glyph-level L2
works because each run's glyphs are stored in logical order: reversing an
RTL level span restores the shaper's visual order, and marks stay attached.

## Modules

| File | Role |
|---|---|
| `bsf/text.h`, `bsf/unicode_props.cpp` | Unicode 15.1.0 properties: Bidi_Class, mirroring, paired brackets, joining type, script, line break, General_Category marks, canonical (de)composition incl. Hangul. |
| `bsf/bidi.cpp` | UAX #9: `resolve_paragraph_level`, `resolve_bidi_levels`, `bidi_line_levels` (L1), `bidi_reorder` (L2), `bidi_runs`, `bidi_reorder_runs`, `BidiText`, `bidi_line_map` (logical↔visual). |
| `bsf/otl.h`, `bsf/otl.cpp` | OpenType Layout: GDEF, GSUB 1–8, GPOS 1–9, plan builder, budgets. |
| `bsf/shape.h`, `bsf/shape.cpp` | Run shaper (normalization, mirroring, Arabic joining, feature stages, kern fallback). |
| `bsf/layout.h`, `bsf/layout.cpp` | `TextLayout`: the single authoritative layout used for measuring, drawing, caret, hit-testing, visual movement, selection and wrapping. |
| `bsf/unicode_layout*.{h,cpp,inc}` | Packed Unicode blob (61,730 bytes), its index, static provider (host + kernel) and BWS provider (Ring 3). |
| `bsf/runtime.{h,cpp}` | Font resolver for layouts, 4-slot LRU layout cache, `draw_layout`, `glyph_id_face`, `text_statistics`; complex-aware `measure_text`, `measure_prefix`, `hit_test`. |

Conceptual API names map as: ResolveParagraphDirection →
`resolve_paragraph_level`; ResolveBidiLevels → `resolve_bidi_levels`;
BuildDirectionalRuns → `bidi_runs`; ReorderVisualRuns → `bidi_reorder_runs`
/ `bidi_reorder`; MapLogicalToVisual / MapVisualToLogical → `bidi_line_map`.

## When is the BSF 2.0 path used?

`needs_complex_layout(text)` is true when a string contains a strong RTL or
Arabic-number scalar, any explicit bidi control (LRE…PDI), a joining script
letter (R/D/L joining type) or a General_Category mark. Pure ASCII returns
immediately. Everything else (Latin, Cyrillic, Greek, CJK without marks)
keeps the legacy per-scalar path, so existing pixel geometry is unchanged.
Measurement and drawing make the same decision from the same function, and
complex strings are measured and drawn from **the same cached layout**.

## Coordinates and rounding

Shaping works in font design units. Layout converts each advance/offset to
16.16 fixed-point pixels (`units * px * 65536 / upem`, rounded) and
accumulates the pen in 64-bit 16.16 without per-glyph rounding, so long
lines do not drift. Pixel positions are rounded once at draw time;
`measure_text` rounds the total width to nearest (same policy as legacy
advances), `measure_prefix` returns the rounded primary caret.

## Unicode tables: one copy in the kernel, fetched by GUI processes

The blob (bidi classes, joining, scripts, line breaks, marks, brackets,
mirroring, decompositions, compositions, script codes) would cost ~70 KiB in
every app binary, i.e. ~1.4 MiB of the kernel image. Instead:

- `jadro/gui/czcionka_systemowa.cpp` adds **BWS 51 command 2**: with
  `PRAWO_GUI`, an exact-size output buffer and `czy_bezpieczny_zakres_uzytkownika_do_zapisu`,
  it copies the compile-time constant blob out with
  `skopiuj_do_przestrzeni_uzytkownika`, body first and the 16-byte header
  last. Ring 0 never parses it. Same pattern and checks as command 1
  (system bitmap font).
- `bsf/unicode_layout_bws.cpp` fetches it once per process into heap and
  validates magic, version, size, Unicode version and every table offset.
  On any failure complex layout is disabled for that process and the legacy
  LTR path remains authoritative (logged once).

## Kernel image budget and `BSF_COMPLEX_TEXT`

Nearly every GUI app is linked into the kernel image as a raw blob, and
`jadro/arch/linker.ld` asserts `__kernel_end <= 15 MiB` (RING3_BASE 16 MiB
minus a 1 MiB guard). The guard has never been raised.

**Original integration (`36cc9da`).** The engine is ~40 KiB of code per
statically linked app; linking it into all embedded apps overflowed the
guard, so a compile-time switch kept calculator, snake, test, demo_ui,
kontrolki2/3/4, test_dzwieku, repro, tapety and ekran on the legacy LTR path
(`bursztyn_gui_ltr.o`, `-DBSF_COMPLEX_TEXT=0`). Image end 0xEA6000
(page-aligned `__kernel_end`), **360 KiB** below the guard. (Earlier
revisions of this document quoted 0xEA47F0/366 KiB and the test report
0xEA57F0/362 KiB — those were the last `LOAD` segment ends of two different
builds; the page-aligned symbol is used from now on.)

**Stabilization (2026-11-11).** The two desktop sample images
(`obrazy/test.bmp` 750,056 B and `obrazy/test1.bmp` 1,350,056 B, together
2,100,112 B ≈ 2.0 MiB) were the largest embedded assets. They are no longer
linked into the kernel: `narzedzia/package_desktop_images.py` stages them as
`/boot/pulpit/*.bmp` and adds `module2 … pulpit/test.bmp` /
`pulpit/test1.bmp` to every GRUB entry of the production ISO, the CI ISOs and
the 8K ISO. At boot the kernel finds each module by exact label, validates it
(`jadro/rdzen/obrazy_startowe.h`, see `bsf2security.md`) and writes the same
bytes to `/uzytkownicy/Pulpit/test.bmp` / `test1.bmp` exactly as the old blob
path did (same PSF diagnostics under `BURSZTYN_TESTY_PSF`, same installation
plan entry). The module memory stays PMM-reserved because the step-wise
installer keeps a pointer to the data. On an installed system booting from
disk (no modules) the files already exist from installation; the kernel
logs `[BSP] Brak modulu obrazu pulpitu` and leaves them untouched.

| Build | `__kernel_end` | Headroom below 15 MiB |
|---|---|---|
| `950352a` (before BSF 2.0) | 0xE29000 | 860 KiB |
| `36cc9da` (BSF 2.0, 11 apps LTR-only) | 0xEA6000 | 360 KiB |
| BMPs as modules, same app split | 0xCA7000 | 2,404 KiB (+2,044 KiB) |
| BMPs as modules, **engine in every GUI app** (final) | 0xD1A000 | **1,944 KiB** |

With that space the LTR split was removed: every GUI app now links
`$(BSF_APP_GUI)` = `bursztyn_gui.o` (`repro` contains no text drawing, so
`--gc-sections` drops the engine there). The LTR library and
`BSF_LTR_APPS_TMP` remain as an explicit, documented opt-out (empty list).
App size budgets still hold (tapety 360,320 B and test_dzwieku 282,432 B of
their 512 KiB limits). The terminal's logical cell order is unchanged (the
Shell already had the full engine; its cell grid never reorders).

## Caches and budgets

| Cache / limit | Bound | Invalidation |
|---|---|---|
| Layout cache (runtime) | 4 layouts per process, LRU | key: FNV-1a hash + exact text compare, px, bold/italic, family, direction, max width, `generation()` (font config + catalog epoch) |
| Shaping plans | 4 per process | key: face id, table pointers, script tag, language, features, direction, shaper class |
| Caret table | per layout, O(clusters) | rebuilt with the layout |
| Glyph bitmaps | existing 128-slot cache, keyed by face/glyph id/px/style | existing rules (rescan, config change) |
| Layout input | 65,536 bytes; UAX #9 65,536 scalars | larger input is rejected (legacy path) |
| Shaping run | 4,096 glyphs (runs split at 2,048 scalars on grapheme boundaries) | |
| OTL work budget | 4 Mi matching steps per run | exhaustion → run keeps partial, well-formed output, counted |
| Contextual nesting / context length | 6 / 64 glyphs | |
| Multiple substitution | ≤ 32 glyphs per sequence; buffer cap 2× input | |

Shaped glyph metrics are converted per layout; shaping results are not cached
independently of layouts (documented future work).

## Profiling counters

`BSF::text_statistics()` (per process): layout hits/misses/failures, shaped
runs, shaped glyphs, fallback searches, budget failures, GSUB runs, GPOS runs,
longest run, layout bytes. They are readable from QEMU through the
ProcessObserver (`narzedzia/bsf2_native_qemu.py`). Existing `BSF::statistics()`
(glyph cache) and `BSF::resources()` remain. No logging occurs in the paint
path; the only BSF 2.0 log line is the one-time "tables unavailable" notice.

## Build

```
make iso                    # production ISO (verifies BSF assets)
make test-bsf2-host         # UAX #9 conformance, shaping oracle, layout
make fuzz-bsf2-smoke        # coverage-guided fuzzing, ASan/UBSan
make bsf-shaping-oracle     # regenerate HarfBuzz expectations (host only)
python3 narzedzia/generate_bsf_layout_data.py   # regenerate Unicode blob
python3 narzedzia/bsf2_native_qemu.py [--bios] [--video WxH]
```
