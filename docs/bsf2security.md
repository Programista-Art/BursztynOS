# BSF 2.0 — security and memory isolation

## Where untrusted font data is processed

| Stage | Ring | Notes |
|---|---|---|
| Directory listing, file reads of `/czcionki/ttf/*.ttf` | Ring 0 serves bytes; policy check only | `BSF::trusted_asset` (unchanged): exact prefix, validated UTF-8 filename, `.ttf` suffix, size caps. |
| sfnt/cmap/glyf/kern/name parsing, rasterization | **Ring 3** (each GUI process) | Unchanged from BSF 1.x. |
| GDEF/GSUB/GPOS parsing and application, UAX #9, layout | **Ring 3** (each full-engine GUI process) | New. Nothing new runs in Ring 0. |
| Unicode property blob | Ring 0 → Ring 3 copy-out | Compile-time constant generated from the pinned UCD; Ring 0 only copies it (BWS 51/2). Ring 3 validates header, table index **and a whole-content FNV-1a hash** before use (hash added 2026-11-11). |
| Desktop sample BMPs (`test.bmp`, `test1.bmp`) | Ring 0 copies Multiboot2 module bytes into PSF | Moved out of the kernel image 2026-11-11. Ring 0 checks range, size, BMP header consistency (`jadro/rdzen/obrazy_startowe.h`) but never decodes pixels; decoding stays in the Ring 3 image viewer, as before. |

No BSF 2.0 parser runs in the kernel, so a malformed font can at worst fault
the GUI process that opened it; the kernel, other processes and the
compositor are unaffected (existing Ring 3 fault containment). A dedicated
single font service process was **not** introduced: fonts are already parsed
per process in Ring 3, and a shared service would add an IPC surface and a
shared cache across trust boundaries without reducing kernel exposure.

## Kernel change review (BWS 51 command 2) — re-audited 2026-11-11

Handler: `jadro/gui/czcionka_systemowa.cpp`, dispatched from
`jadro/bws/syscalls.cpp` case 51 with the calling process's rights.

| Property | Finding |
|---|---|
| Privilege | `PRAWO_GUI` is checked before any argument is looked at, for every command (0, 1, 2). Without it the call returns 0 and touches nothing. |
| Exact sizing | `rozmiar` must equal `BSF::UnicodeBlob::Size` (61,730); 0, ±1, 2×, `UINT32_MAX`, `UINT64_MAX` are rejected before any copy. |
| Integer overflow | `bufor > UINT64_MAX - rozmiar` rejected; the user-range check (`zakres_numerycznie_poprawny`) compares against the remaining space instead of computing `start + size`. |
| User pointer validation | `czy_bezpieczny_zakres_uzytkownika_do_zapisu` = canonical/user-half check + VMM PTE walk requiring USER\|PRESENT\|WRITABLE for every page; re-checked per page inside the copy. |
| SMAP | The copy runs inside `OknoUzytkownika` (STAC/CLAC) only after validation; no other code in the handler dereferences the user pointer. |
| Partial copies | Body first, 16-byte header last. Any failing page aborts with return 0; the header is then never written, so Ring 3 validation rejects the buffer, and `unicode_layout_bws.cpp` frees it and disables complex layout (no retry loop). |
| Data exposure | Copies only a `static const` `.rodata` array (no kernel pointers, no uninitialised bytes: exact size, fully initialised constant). |
| ABI | Commands 0 and 1 byte-for-byte identical to `950352a` (diffed); command 2 was previously rejected (returned 0), so older apps are unaffected; unknown commands still return 0. |
| Parsing | None in Ring 0. |

Residual risk (pre-existing, shared by every copy-to-user syscall, not
introduced by BSF 2.0): the PTE check and the copy are not atomic with
respect to a concurrent unmap by another thread of the same process; the
copy path has no exception-table fixup. Today no concurrent unmap is
possible: the scheduler is uniprocessor (`jadro/arch/fpu.cpp`) and there is
no Ring 3 thread-creation syscall, so a process's mappings cannot change
while it is inside a syscall. SMP or user threads would require copy fixups.

New negative/boundary tests: `make test-bsf2-kernel-abi-host`
(`testy/test_bsf2_kernel_abi.cpp`) compiles the production handler with a
simulated Ring 3 address space and fault injection: privilege (all rights
except `PRAWO_GUI`), sizes, null/wrapping/straddling pointers, exact end
boundary, faults after 0/1/4095/N/2/N−17 body bytes and inside the header,
no writes outside the buffer, unknown commands (3, 4, 255, 2^32, 2^64−1),
command 1 ABI and its partial-copy behaviour. Also: every single-byte
corruption of the blob (all header bytes, every 97th byte after) is rejected
by Ring 3 validation thanks to the content hash, and 20,000 mutated BMP
headers. 41,018 checks, ASan + UBSan.

## Desktop BMP Multiboot2 modules (2026-11-11)

`test.bmp`/`test1.bmp` are loaded by GRUB with exact labels
`pulpit/test.bmp`/`pulpit/test1.bmp`. Before copying into PSF the kernel
requires: non-zero, non-inverted module range below 4 GiB; 54 B ≤ size ≤
8 MiB; `BM` signature; `bfSize` == module size; DIB header ≥ 40 B inside the
file; pixel offset after the headers and inside the file; width > 0,
height ≠ 0 and ≠ INT32_MIN; planes == 1; bpp ∈ {1,4,8,16,24,32};
compression BI_RGB/BI_BITFIELDS (pixel array must fit, 64-bit arithmetic)
or RLE4/RLE8; anything else is rejected and logged. The module memory stays
reserved in the PMM (the step-wise installer keeps a pointer to it), exactly
like the Settings payload and font modules.

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
The 15 MiB kernel image guard was kept (not relaxed). Re-checked
2026-11-11 after enabling the engine in every GUI app: no ELF (kernel or
any app) has an RWX `LOAD` segment, every `GNU_STACK` is `RW`, apps are PIE
(`ET_DYN`, randomised by the BUR v2 loader) and reference
`__stack_chk_fail`.

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
