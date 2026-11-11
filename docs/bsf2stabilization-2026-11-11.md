# BSF 2.0 stabilization — 2026-11-11

Branch `feature/bsf-2-stabilization`, created from `test` @ `36cc9da` (BSF 2.0
integration merge). Working tree was clean at the start; nothing committed,
pushed or merged. `main` and `UNITRONIX` untouched. Every number here was
produced in this session on Linux x86-64, QEMU 8.2.2 (TCG), OVMF; host tests
use ASan + UBSan (`halt_on_error=1`).

## Phase 1 — independent verification

### Reproduction of the reported results (at `36cc9da`, before any change)

| Suite | Reported | Reproduced |
|---|---|---|
| UAX #9 BidiTest / BidiCharacterTest 15.1.0 | 770,241 / 91,707 | **770,241 / 91,707**, 0 failures (4,887,109 checks) |
| GSUB/GPOS vs HarfBuzz oracle | 110/110 rows, 1,164 glyphs | **110/110**, 1,164 glyphs |
| Shared layout | 409,086 checks | **409,086** |
| `test-bsf-host` / `-ui-host` / `-graphemes-host` | 12,204 / 9,084 / 1,187 | **12,204 / 9,084 / 1,187** |
| Notepad Unicode (production code) | 60,662 checks | **60,662** (60,000 keys 9.7 s; reported 11.6 s) |
| `test-bsf-resources-host` / `test-shell-ttf-host` | 4,604 / 2,201 | **4,604 / pass** |
| `fuzz-bsf2-smoke` | 0 crashes | **0 crashes**: bidi 20,000 iters (710 edges), layout 5,000 (2,831), OTL 3,000 (1,802) |

### Discrepancy found: lookup types "present" were not "applied"

New instrumentation (`OT::Statistics::gsub_applied/gpos_applied[type][format]`,
counted on every successful subtable application; extension lookups counted
under 7/9 and the resolved type) showed that the original corpus **never
applied GSUB 3 or GSUB 8**. HarfBuzz did not apply them on those rows either,
which is why the exact-match oracle could not reveal it. The previous report's
statement that the Coptic rows exercised GSUB 8 was wrong.

- GSUB 8 (Noto Sans Coptic, lookup 17, reverse chaining) needs a plain overline
  followed by an already-connected one (lowercase then uppercase letter). Found
  with the counters, confirmed against HarfBuzz's message trace; 4 rows added.
- GSUB 3 exists in the oracle fonts only under `aalt`, which is not a default
  feature. Added opt-in `FeatureAalt` (first alternate, = HarfBuzz `aalt=1`) and
  5 rows with an optional 7th oracle column for features.

Test expectations were not changed to pass: all 110 original rows are
byte-identical in the regenerated fixture; 10 rows were added. The shaping test
now **fails if any GSUB 1–8 / GPOS 1–9 type is never applied**. Result:
**120/120 rows exact, 1,222 glyphs, 0 types unapplied** (table in
`bsf2opentypegsubgpos.md`). Formats still without a real-font row: GSUB 5 fmt 3,
GSUB 6 fmt 1, GPOS 7 fmt 1/3, GPOS 8 fmt 1/2.

Native: `TextStatistics` gained `gsub_type_mask`/`gpos_type_mask` (appended, old
offsets unchanged). The Settings → Fonts preview natively applies GSUB 1 and 4
and GPOS 2 (DejaVu Sans; the font has no GPOS mark lookups for those samples).

## Phase 2 — kernel image size

The 15 MiB limit (`jadro/arch/linker.ld`: `__kernel_end <= RING3_BASE − 1 MiB`)
was **not** changed. The ~2.1 MiB of embedded BMP test assets are
`obrazy/test.bmp` (750,056 B) and `obrazy/test1.bmp` (1,350,056 B) — the two
largest objects linked into the kernel (next: `test_blob.o`, 611,208 B). They
populate the desktop sample files `/uzytkownicy/Pulpit/test.bmp`/`test1.bmp`.

They now ship as Multiboot2 modules (`narzedzia/package_desktop_images.py`,
labels `pulpit/test.bmp`, `pulpit/test1.bmp`, added to every GRUB entry of the
production, CI and 8K ISOs). The kernel matches the exact label, validates the
module (`jadro/rdzen/obrazy_startowe.h`) and writes the same bytes to the same
paths through the same code as before (including the `BURSZTYN_TESTY_PSF`
diagnostics and the installer plan). Natively verified: both files appear on
the desktop, the kernel logs both imports, and the image viewer decodes
`test1.bmp` (window title gains the file name only after a successful decode).

| Build | `__kernel_end` | Headroom |
|---|---|---|
| `950352a` (pre-BSF 2.0) | 0xE29000 | 860 KiB |
| `36cc9da` (BSF 2.0; 11 apps LTR-only) | 0xEA6000 | **360 KiB** |
| BMPs → modules, same app split | 0xCA7000 | 2,404 KiB (+2,044 KiB) |
| **Final**: BMPs → modules, BSF 2.0 in every GUI app | 0xD1A000 | **1,944 KiB** (+1,584 KiB vs `36cc9da`) |

With the space recovered, calculator, snake, test, demo_ui, kontrolki 2/3/4,
test_dzwieku, tapety and ekran were switched to the full engine (`repro` has no
text drawing; the linker drops the engine). The LTR library stays as a
documented, empty opt-out list. App budgets hold (tapety 360,320 B,
test_dzwieku 282,432 B of 512 KiB). Terminal cell order unchanged.

## Phase 3 — security audit

Detailed table in `bsf2security.md`. Summary: BWS 51/2 checks `PRAWO_GUI`
first, requires the exact size, rejects pointer overflow, validates the user
range with a PTE walk (write permission) re-checked per page, copies inside the
SMAP window only, publishes the header last, returns 0 on any partial copy, and
exports only a fully initialised `.rodata` constant. Commands 0/1 are unchanged
relative to `950352a`; command 2 was previously rejected, so the ABI change is
additive. Font parsing remains entirely in Ring 3.

Improvement made: Ring 3 previously validated only the blob header and table
directory; a kernel and app built from different tables of equal sizes would
have been accepted. `unicode_layout_valid` now also checks a whole-content
FNV-1a hash generated with the tables (`UnicodeBlob::ContentHash`; blob bytes
unchanged, verified with `cmp`).

New tests: `make test-bsf2-kernel-abi-host` — production BWS 51 handler with a
simulated Ring 3 address space and fault injection, blob corruption sweep, BMP
module validator boundaries and 20,000 mutated headers: **41,018 checks pass**.

Hardening re-checked on the final build: no RWX `LOAD` segment in the kernel or
any app ELF, all `GNU_STACK` RW, apps PIE, `__stack_chk_fail` referenced in
every app. NX, W^X, SMEP, SMAP, ASLR, stack protection and executable
verification code untouched (`git diff` shows no change in those files).

Residual risk (pre-existing): copy-to-user has no fault fixup; safe today
because the scheduler is uniprocessor and there are no Ring 3 threads.

## Phase 4 — VirtIO-GPU control-command timeouts

### A/B: `950352a` vs `36cc9da`

Harness: `narzedzia/virtio_gpu_ab_qemu.py` runs both ISOs **concurrently** each
round (same host conditions) with `bsf2_native_qemu.py --workload apps`
(identical boot → Settings → Notepad → Shell sequence, no BSF 2.0-specific
checks), UEFI, virtio-vga 1280×720, `-smp 1`, TCG.

| Host condition | `950352a` | `36cc9da` |
|---|---|---|
| Idle host (rounds 1–5) | 0 / 5 timeouts | 0 / 5 timeouts |
| + one busy host process (rounds 6–10, host test suite) | 3 / 5 | 4 / 5 |
| Saturated: + 2 busy loops, 3 VMs (rounds 1–7) | 7 / 7 | 7 / 7 |

Conclusion: **pre-existing, load-dependent, not a BSF 2.0 regression**. The
rate follows host CPU contention and is the same for both commits.

### Investigation

Every timeout has the same signature: command submitted and kicked
(`avail = used + 1`), 2 descriptors in flight, `used` never advances within
500 ms of guest HPET time, response header still zero. Affected commands vary
(`RESOURCE_CREATE_2D` at boot, `SET_SCANOUT`, `TRANSFER_TO_HOST_2D`,
`RESOURCE_FLUSH`). No queue corruption, no descriptor reuse, no missed
completion (the used index is re-read after the deadline). After a timeout the
driver deliberately keeps the DMA buffers (device may still own them) and
latches `has_error`, so the display stops updating for the rest of the boot.

Hypothesis tested and **rejected**: the wait loop reads the HPET (MMIO, which
takes QEMU's global lock under TCG) ~1.4 million times per second, possibly
starving the device thread. A candidate with 64 `pause` instructions after each
clock read cut the polls ~15× (≈30k vs ≈700k per 500 ms) but timed out exactly
as often (7/7 under the saturated condition). The change was **reverted**; the
driver is byte-identical to `36cc9da`.

Interpretation: under TCG, guest HPET time follows host wall time, so when the
QEMU process's device-model thread is not scheduled for 500 ms the guest
deadline expires. This is an emulation-environment effect; it does not show
that the deadline is wrong for real hardware, and no timeout was disabled or
relaxed.

### Proposed follow-up (not implemented — needs its own validation)

Bounded late-completion recovery for idempotent commands only
(`TRANSFER_TO_HOST_2D`, `RESOURCE_FLUSH`): on timeout keep the descriptors
outstanding and refuse new commands; on the next submission attempt reap a
late completion, validate its type and length, and clear the error; declare
permanent failure after a hard limit (e.g. 5 s). Non-idempotent commands
(create/attach/scanout) keep today's fail-closed behaviour. This needs the
`test-virtio-pci` host suite repaired first (see below).

### Pre-existing test breakage found

`make test-virtio-pci` does not link at `950352a` or `36cc9da`
(`graphics_perf.h` uses `hpet_dostepny`/`czas_monotoniczny_ns`, which the host
test did not provide). A host stub was added to `testy/test_virtio_pci.cpp`;
the suite now links and runs 90 tests, then hits two pre-existing harness
defects (ASan stack-use-after-return in `test_virtio_gpu_init_and_cleanup`,
UBSan invalid `bool` load in `virtio_pci.cpp:416` during the net stress test),
reproduced identically with the unmodified `36cc9da` driver. Not fixed here.

## Phase 5 — native integration

See `bsf2testreport.md` §7a for the per-mode table, ISO SHA-256 and artefacts.

## Changed files

- `bsf/otl.{h,cpp}`, `bsf/layout.{h,cpp}`, `bsf/runtime.{h,cpp}`: application
  counters and type masks.
- `bsf/shape.{h,cpp}`: opt-in `FeatureAalt`.
- `bsf/unicode_layout.h`, `bsf/unicode_layout_index.h`,
  `narzedzia/generate_bsf_layout_data.py`: content hash.
- `jadro/rdzen/kernel.cpp`, new `jadro/rdzen/obrazy_startowe.h`, new
  `narzedzia/package_desktop_images.py`, `Makefile`: BMPs as modules; engine in
  all GUI apps; new test target.
- New `testy/test_bsf2_kernel_abi.cpp`; `testy/test_bsf_shaping.cpp`,
  `testy/fixtures/bsf_shaping_oracle.tsv`,
  `narzedzia/generate_bsf_shaping_oracle.py`: coverage assertion, new rows.
- `testy/test_virtio_pci.cpp`: host clock stub (link fix only).
- `narzedzia/bsf2_native_qemu.py`: `--root`, `--workload apps`, `--tag`,
  `--hmp-stress`, stage tracking, type masks, desktop-image and app smoke
  checks; new `narzedzia/virtio_gpu_ab_qemu.py`.
- Docs: this file, `bsf2testreport.md`, `bsf2supportmatrix.md`,
  `bsf2architecture.md`, `bsf2security.md`, `bsf2opentypegsubgpos.md`,
  `bsf2fontformats.md`.
