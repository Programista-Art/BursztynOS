# BSF 2.1 — performance and size

Host: Linux VM (VirtualBox), 4 vCPU; numbers are wall-clock medians of
repeated runs and vary ±10–20 % with host load. Every comparison is A/B
against BSF 2.0 = a worktree of `test` @ `544a51e` built with the same
compiler flags, the same benchmark source and run back to back.

## Notepad (`make bench-notepad-bidi`, -O2, no sanitizers, 800×600)

| Case | Horizontal key | Vertical key | Insert (whole-paragraph relayout) |
|---|---|---|---|
| ASCII 20 KB, BSF 2.0 → 2.1 | 9 → 12 µs | 16 → 11 µs | 51–63 → 54–69 ms (noise band, both) |
| Polish 20 KB | 6 → 7 µs | 28 → 28 µs | 41–50 → 42–51 ms |
| Mixed RTL 20 KB (one paragraph) | 1 → 57 µs (2.0 was per-line and wrong) | 2 → 34 µs | 39–49 → 40–49 ms |
| 200 mixed paragraphs | 0 → 1 µs | 19 → 26 µs | 26–29 → 24–31 ms |

60,000-key ASCII benchmark (`make test-notepad-unicode-host`, ASan): baseline
8.37 s (quiet host); BSF 2.1 final sweep 8.40 s; under load A/B 10.42/10.45 s
(2.0) vs 10.13/10.33 s (2.1) — no regression. Insert latency of a large paragraph is dominated by the existing
full re-wrap per edit in both versions (BSF 2.2 item).

## Husarz (`make bench-husarz-bidi`, -O2, 800×600, 60 paragraphs per page)

Six alternated A/B rounds on a quiet host (both run orders), ms per
operation, range of the rounds (BSF 2.0 → BSF 2.1):

| Page | Full reflow (styles + bidi + layout) | Resize reflow | Paint (full frame) |
|---|---|---|---|
| Polish/English, 1,440 fragments | 2.8–3.6 → 3.2–4.2 | 2.5–3.0 → 2.8–3.6 | 2.1–2.6 → 2.3–3.1 |
| Arabic/Hebrew RTL, 840 fragments | 1.9–2.1 → 2.1–2.2 | 1.6–1.9 → 1.5–1.7 | 1.9–2.4 → 1.8–2.0 |
| Mixed LTR with RTL words, 780 fragments | 1.7–2.1 → 2.5–2.7 | 1.5–1.9 → 2.0–2.1 | 2.1–2.6 → 2.5 |

Interpretation:
- RTL pages are now shaped and reordered yet as fast or faster than BSF 2.0
  per-code-point rendering (shaped runs paint fewer, cached glyph runs).
- Mixed pages pay ~0.4–0.6 ms per reflow for the bidi paragraph pass and
  shaped measurement.
- **Plain Polish/English pages show a residual ~10–15 % layout and up to
  ~10 % paint overhead (≈0.3–0.4 ms per reflow at this size)** although they
  take the unchanged fast path; the measured cause was not isolated (larger
  `Fragment`/`Document`, extra per-word checks). Reported as a known minor
  regression; BSF 2.2 should profile it on the guest (host `perf` is
  unavailable here: perf_event access is restricted).

## Kernel image (15 MiB guard unchanged)

| Build | `__kernel_end` | Headroom |
|---|---|---|
| `544a51e` (BSF 2.0 stabilized, test) | 0xD1B000 | 1,940 KiB |
| BSF 2.1 (this branch) | 0xD39000 | **1,820 KiB** (−120 KiB) |

Growth comes from the BursztynUI editor headers compiled into the embedded
apps and from Husarz (`przegladarka.bin` 596,776 → 625,496 bytes, within its
884,736-byte text limit). The BSF library code itself is shared per app.

## Caches

Layout-cache behaviour from the host suites: Notepad long mixed paragraph
9,395 hits / 255 misses; Husarz shaped widths 2,444 hits / 69 misses, shaped
layouts 77 hits / 70 misses over the 12 pages + long/adversarial pages.

## Fast paths kept

- Plain LTR words in a document without bidi paragraphs take the unchanged
  BSF 2.0 per-cluster measurement; no layout object is built.
- `needs_complex_layout` skips well-formed U+0080–U+02FF (Latin-1/Extended,
  e.g. Polish letters) without table lookups.
- Bidi levels are reused for width-only reflows.
- Optimizations found by A/B during development: a per-word grapheme snap
  that rescanned from the node start (O(n²)), double per-cluster measurement
  of complex words, and an override scan for plain LTR parts were removed.
