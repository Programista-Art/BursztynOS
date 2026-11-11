# BSF 2.0 — security and memory isolation

## Where untrusted font data is processed

| Stage | Ring | Notes |
|---|---|---|
| Directory listing, file reads of `/czcionki/ttf/*.ttf` | Ring 0 serves bytes; policy check only | `BSF::trusted_asset` (unchanged): exact prefix, validated UTF-8 filename, `.ttf` suffix, size caps. |
| sfnt/cmap/glyf/kern/name parsing, rasterization | **Ring 3** (each GUI process) | Unchanged from BSF 1.x. |
| GDEF/GSUB/GPOS parsing and application, UAX #9, layout | **Ring 3** (each full-engine GUI process) | New. Nothing new runs in Ring 0. |
| Unicode property blob | Ring 0 → Ring 3 copy-out | Compile-time constant generated from the pinned UCD; Ring 0 only copies it (BWS 51/2). Ring 3 validates header and table index before use. |

No BSF 2.0 parser runs in the kernel, so a malformed font can at worst fault
the GUI process that opened it; the kernel, other processes and the
compositor are unaffected (existing Ring 3 fault containment). A dedicated
single font service process was **not** introduced: fonts are already parsed
per process in Ring 3, and a shared service would add an IPC surface and a
shared cache across trust boundaries without reducing kernel exposure.

## Kernel change review (BWS 51 command 2)

- Requires `PRAWO_GUI` (same as command 1).
- Output size must equal `BSF::UnicodeBlob::Size` exactly; pointer
  overflow (`bufor > UINT64_MAX - rozmiar`) is rejected.
- `czy_bezpieczny_zakres_uzytkownika_do_zapisu` validates the user range;
  copies use `skopiuj_do_przestrzeni_uzytkownika` (SMAP-aware, fault-safe).
- Body copied first, 16-byte header last: a partial copy never validates.
- No kernel pointers, no writable kernel mappings, no parsing; additive ABI
  (commands 0 and 1 unchanged).

## Bounds in the new code

- **Offsets/lengths**: all table access through `Bytes::has/u16/u32/sub`;
  out-of-range reads return 0 and every count is validated against the
  available bytes before indexing (`has(offset, count * record)`).
- **Integer overflow**: sizes computed in `size_t`/`uint64_t`; fixed-point
  scaling in `int64_t`; GPOS values are 16-bit inputs accumulated in 32 bit.
- **Work**: per-run `Budget` (4 Mi steps) charged by every matching loop,
  coverage/class binary search and buffer move; exhaustion stops shaping of
  that run with a well-formed buffer and increments `budget_failures`.
- **Recursion**: nested contextual lookups ≤ 6 (64 KiB Ring 3 stacks; worst
  frame chain measured with `-fstack-usage` ≈ 1.3 KiB per level); cursive
  chain reversal and attachment resolution are iterative with cycle guards.
- **Expansion**: multiple substitution ≤ 32 glyphs, buffer capacity fixed at
  2× input (≤ 8,192 glyphs); ligature components ≤ 64.
- **Allocation**: every allocation is sized from bounded inputs (≤ 65,536
  bytes per layout) and checked; `new` failure aborts the operation cleanly
  (`-fcheck-new` in Ring 3).
- **UAX #9**: iterative, explicit-depth stack (127 entries), input ≤ 65,536
  scalars.
- **No libc `mem*` emission**: BSF 2.0 objects are built with
  `-fno-tree-loop-distribute-patterns` and use explicit loops; Ring 3 links
  with `--no-undefined`, so any stray dependency fails the build.
- **Glyph IDs** from GSUB are validated against `numGlyphs` before use.

## Cache isolation

All BSF caches (glyph bitmaps, layouts, plans, resolution caches) are
per-process. There is no cross-process font cache, so a page or app cannot
poison another process's shaping results. Layout keys include the full text
(exact compare after hash), size, style, family, direction, width and the
font generation/epoch.

## Hardening preserved

No change to NX/W^X, SMEP/SMAP, Ring 3 ASLR (BUR v2), stack protector or
signature verification. Ring 3 objects keep `RING3_FLAGS` (including
`HARDEN_FLAGS`, PIE, hidden visibility); the kernel object keeps
`KERNEL_CXXFLAGS`. No executable or writable+executable mappings were added.
The 15 MiB kernel image guard was kept (not relaxed).

## Adversarial testing

- `fuzz_bsf_otl`: arbitrary GSUB/GPOS/GDEF attached to a valid face, four
  scripts, budget 200k; regressions for empty, truncated and all-0xFF tables.
- `fuzz_bsf_bidi`: arbitrary/malformed UTF-8 and raw class sequences,
  200 nested isolates regression; permutation/inverse invariants.
- `fuzz_bsf_layout`: arbitrary text, sizes, directions, wrapping, every
  caret/hit/selection API with boundary invariants.
- All with ASan + UBSan (`halt_on_error`) and leak detection; existing
  `fuzz_bsf_coverage` (TrueType parser) unchanged.

Results are in `bsf2testreport.md`. No crash was found; no bug required a
crash-reproducer regression file. The defects found during development came
from conformance/oracle tests and are covered by those tests permanently.
