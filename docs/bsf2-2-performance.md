# BSF 2.2 — performance

Host: `make bench-bsf22` (x86-64, `-O2`, no sanitizers, production code;
4-core host, numbers per operation, single run). Native timings are not
benchmarked separately (TCG QEMU); native runs completed without slices
exceeding budgets or GPU timeouts.

## Rasterization (glyph-cache misses only; hits are unchanged from 2.1)

| Face | 16 px ns/glyph | 48 px ns/glyph |
|---|---|---|
| TrueType DejaVu Sans | 4,270 | 6,427 |
| TrueType Noto Sans (Latin subset) | 3,918 | 6,472 |
| CFF Noto Sans OTF | 4,746 | 8,663 |
| CFF2 Source Serif 4 default | 8,167 | 12,340 |
| CFF2 Source Serif 4 wght=800 | 10,943 | 15,242 |
| TrueType Hebrew VF default / wght=700 (gvar) | 2,620 / 3,452 | 2,845 / 3,537 |

Updated during BSF 2.2 stabilization (see `docs/bsf2-2-cff-performance.md`):
CFF previously cost 8–10× a TrueType miss (39.6 / 71.7 µs). The cause was
not the CFF parser but cubic flattening: stb calls `STBTT_sqrt` 4× per
subdivision and BSF mapped it to a 32-iteration Newton loop. It is now SSE2
`sqrtsd`; Private DICTs are also cached per FD at open. A miss happens once
per glyph/size/style (128-slot cache); ordinary text with DejaVu (TrueType)
takes exactly the BSF 2.1 path (no CFF/variation/colour code executed:
colour lookups short-circuit on `color_tables == 0`, `variable()` false).
gvar adds ~30 % per miss.

## Face open (validation)

| Face | µs/open |
|---|---|
| TrueType DejaVu Sans (760 KB) | 47 |
| CFF Noto Sans OTF (332 KB) | 26 |
| CFF2 Source Serif 4 (1.9 MB) | 17 |
| OTC CJK subset face 0 | 7 |

Catalog discovery reads metadata only for collections > 4 MiB (Noto CJK
19.5 MB: 5.8 MB read instead of the whole file; see collections doc).

## Web fonts (decode in the Husarz worker)

| Input | ms |
|---|---|
| WOFF Latin 12 KB | 0.35 |
| WOFF2 Latin 9 KB | 0.22 |
| WOFF CFF 210 KB | 5.43 |
| WOFF2 CFF 168 KB | 2.26 |
| WOFF2 Naskh Arabic 62 KB | 1.39 |

## Colour glyphs (per glyph, colour-cache miss)

| Font | 20 px µs | 64 px µs |
|---|---|---|
| Noto COLRv1 subset | 192 | 1,413 |
| Noto Color Emoji CBDT subset (PNG decode + scale) | 262 | 398 |
| COLRv0 test font | 17 | 34 |

COLRv1 at large sizes is the most expensive path (float canvases per
PaintGlyph); the 16-entry colour cache bounds repeat cost.

## Shaping

Noto Sans TrueType Latin subset 14.9 µs vs Noto Sans OTF 51.8 µs for the
same 78-scalar string. This is **not** a CFF cost (shaping never touches
outlines): the OTF carries the full GSUB/GPOS of all scripts while the
TrueType fixture is a subset. Shaping code is otherwise unchanged except
for the emoji ZWJ matching and fixed-point anchors (no measurable change in
`test-bsf-layout-host` / `test-notepad-bidi-host` timings).

## Not measured (open)

- A/B of Husarz page layout and Notepad typing latency against the BSF 2.1
  baseline worktree (`bench-husarz-bidi`, `bench-notepad-bidi`) were not run
  for this report; the ~10–15 % Husarz overhead reported for BSF 2.1 is
  therefore neither re-measured nor attributed.
- Peak memory natively per process (counters exist: `resources()`).

## Kernel image

`__kernel_end` 0xD39000 (2.1) → 0xE9F000 (2.2), 388 KiB headroom (see
architecture doc).
