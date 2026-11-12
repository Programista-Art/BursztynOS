# BSF 2.1 — security review

BSF 2.1 changes only Ring 3 code (BSF library, BursztynUI headers, Husarz
headers, `przegladarka.cpp` key routing) plus host tests, fuzzers, tools and
documentation. No kernel source, syscall, BWS handler, loader, bootloader,
driver, filesystem or protection setting was modified.

## Preserved boundaries

| Property | Status |
|---|---|
| Ring 0 / Ring 3 separation | unchanged; all shaping, bidi and layout run in the GUI process |
| Font parsing | Ring 3 only (unchanged); web content never supplies font bytes (no web fonts) |
| Kernel-served data | the Unicode blob (BWS 51/2) is unchanged; no new syscall |
| NX, W^X, SMEP, SMAP, Ring 3 ASLR, stack canaries, executable verification | untouched (no linker, loader, CR4/EFER or page-table code changed); confirmed natively, see test report |
| BWS validation, safe user-memory access | untouched |
| 15 MiB kernel image guard | unchanged (`jadro/arch/linker.ld` not modified) |

## Untrusted web content

- Every new browser stage has an explicit budget (bytes per analysed
  paragraph chunk, text segments, level runs, nested contexts, open inline
  bidi elements, items per line, parts per word, `dir=auto` scan bytes/nodes);
  exceeding one records `LimitBidi` and degrades to level 0, never to
  unbounded work. UAX #9 itself caps embedding depth at 125.
- Synchronous engine calls are bounded (≤ 32 KiB paragraph, ≤ 16 KiB
  fragment) and charged to the cooperative scheduler, capped at one slice.
- No recursion was added (the DOM walk is iterative with depth-indexed
  stacks bounded by `MaxDepth`).
- Integer arithmetic on coordinates uses the existing `clamp64`; spacing is
  clamped before conversion to 16.16; glyph advance sums are 64-bit.
- Allocation: the bidi scratch contexts are allocated with `new`
  (Ring 3 `-fcheck-new` → nullptr on failure, recorded as a limit); all other
  new storage is fixed-size inside `Document` or bounded static arrays.
- Stale references: level runs are indexed by node id and reset with each
  document (`Document::reset`) and recomputed when `revision` or the style
  generation changes; shaped-run caches store copies of text (exact compare)
  or 64-bit content hashes, never DOM pointers; form-control layouts are keyed
  by value content. A navigation therefore cannot leave a pointer into a
  freed document.
- The shaped-width cache is keyed by a 64-bit hash without the text; a
  collision could only produce a wrong advance width for one fragment
  (cosmetic, no memory effect). Layout caches compare bytes exactly.

## Password fields

`PoleTekstowe` and Husarz password inputs never build a layout of the real
value: only mask characters are measured/painted. Host tests assert that
`BSF::text_statistics().owned_builds` does not change for a BursztynUI
password field holding Hebrew and that `control_layout` returns null for a
Husarz password input. No new log line contains text content.

## Caches

All caches are per process (static or per control/document), bounded, and
never shared across processes. Font generation (`BSF::generation()`) is part
of every key, so a font configuration or catalog change cannot reuse glyph
ids of another face.

## Memory-safety evidence

ASan + UBSan host suites (`test-bsf21-layout-host`, `test-notepad-bidi-host`,
`test-bursztynui-bidi-host`, `test-husarz-bidi-host`, all BSF 2.0 and Husarz
suites) and the coverage-guided fuzzers `fuzz_bsf21_editing` and
`fuzz_husarz_bidi` (`make fuzz-bsf21-smoke`), see `docs/bsf2-1-testreport.md`.

## Findings fixed during development

- A `WorkPoint` larger than `MaxSliceWork` fails a cooperative job; the new
  synchronous steps now use `work_units()` (capped).
- `std::nothrow` does not exist in Ring 3; the browser link failed until the
  allocation used the project's `-fcheck-new` convention.
- The fragment-layout cache initially hashed past the end of the DOM bytes
  for overridden runs; the hash now covers only the DOM bytes.
