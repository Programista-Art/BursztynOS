# BSF Unicode completion — 2026-11-07

Work in progress. No stage is declared fully accepted until its native tests
and evidence are recorded. Previous uncommitted caret work was preserved.
Reports read: `bsfunicodefontfallback2026-11-06.md` and
`bsfunicodephase22026-11-07.md`. Advanced GSUB/GPOS/RTL are outside scope.

## Stage one: shared clusters and editing

Production changes currently under validation:

- Husarz TextCluster keeps source begin/end and an exact BSF face identity.
  Layout, intrinsic/table/generated-content metrics, fragment painting and
  menu labels reuse it. Native glyph painting uses glyph_face with cache
  leases and yields before uncached raster work. CSS family/style/color
  remain document owned. Text transforms resolve transformed coverage while
  retaining original source offsets; transformed clusters over 128 bytes use
  the explicit scalar fallback path. No new font parser or shaping engine.
- Browser forms navigate and delete whole BSF graphemes; hit testing, wrapping,
  password masking and highlights operate on the same clusters. Insertion
  reconciles a caret when neighboring clusters merge.
- Shared fields truncate at source cluster boundaries, reconcile insertion and
  deletion carets, and count password masks by clusters. Selected text is
  repainted using clipping over the full original line, preserving font/kerning
  geometry instead of remeasuring independent substrings.
- Notepad's production PoleWielowierszowe uses BSF for wrapping, measurement,
  hit testing and selection endpoints. A 12,320-byte bounded boundary/count index
  accelerates previous boundaries for documents up to 65,536 bytes; larger
  instances use bounded-source scanning. A fixed checkpoint table stores
  prefix pens every 64 source bytes to avoid rescanning long lines for caret
  geometry. Checkpoints and boundaries rebuild on text/layout changes.
- Input/save does not normalize Unicode. Original malformed bytes remain in
  text storage and are displayed with the existing replacement policy.
  CRLF source bytes are preserved while visual line ranges omit CRLF controls.
  Notepad rejects files above 65,535 bytes and embedded NUL rather than silently
  overwriting a truncated representation. File persistence uses existing BWS.

Full NFC/NFD and canonical reordering remain unsupported; none is required for
byte-preserving grapheme editing. Combining marks retain the existing Unicode
15.1 combining-class/ink-bound geometric positioning, without GPOS anchors.

## Stage two: larger fonts and catalog

Implemented and under native acceptance. `bsf/limits.h` sets 128 catalog
faces, 1,024 examined records, a 64 KiB directory response, a 16 MiB maximum
file and a 16 MiB aggregate resident-file budget. Four loaded faces remain LRU;
eviction precedes allocation. Files above 1 MiB use existing BWS59 in 64 KiB
chunks with exact short-read and final-size checks. Discovery releases each
candidate before reading the next. The catalog occupies 44,544 heap bytes;
the old static allocation overflowed WM DATA+BSS by 9,280 bytes, so it now uses
bounded process-local allocation instead of expanding WM's BUR reservation.
Directory sort transient storage peaks at 139,264 bytes (two 64 KiB buffers
and 1,024 pointers). Discovery scratch font data is at most 16 MiB. Existing
2 MiB glyph cache, four Face records, 128 scalar resolver records and 16 cluster
records remain bounded. Parser limits (16,384 glyphs, 64 tables, 8 compound
levels, 64 dependencies, point/contour/vertex limits and 1 MiB raster scratch)
are unchanged. Existing cycle checks reject a cyclic outline when requested;
Face::open does not eagerly rasterize every glyph.

Settings retains its page and refresh action, expands representative IDs to
128, reports catalog/directory limits, and adds import through ordinary BWS
creation/write permissions. No overwrite is allowed. Rescan clears loaded
buffers and lookup identities; refresh of an unknown configured face also
uses rescan, avoiding discovery while retaining old resident buffers. Refresh
is process-local; other applications need their own rescan or restart.

ASan/UBSan resource harness passed 4,593 checks: deterministic catalogs of
32/64/128 independently named valid fonts, rescans, 3 MiB and exactly 16 MiB
TTFs with their table offsets moved past the former limit, identical A glyph
pixels and advance, short reads, allocation failure and over-limit rejection.
See `bsf-completion-2026-11-07/resources-resumed.txt`. These host observations
are not a native 128-font installation verdict.

## Stage three: Terminal

Implemented and under validation. `shell_font.h` is an adapter over native
BSF, not another font engine. A primary face must contain all printable ASCII
and equal unrounded hmtx advances. A proportional system UI face does not
become the primary grid font. The selected suitable face or first suitable
regular monospaced catalog face supplies the grid; DejaVu Sans Mono is bundled.
`czcionka` reports selection; `czcionka ttf`, `czcionka bitmap` and
`czcionka "family"` select the session renderer. Selection is session-local;
the shared persisted size and display scale still apply. Bitmap remains a
recovery path on missing face/mask or allocation failure. UI chrome still uses
the shared UI font adapter.

Unicode 15.1 EastAsianWidth W/F ranges are generated offline by
`narzedzia/generate_bsf_terminal_width.py`. Each extended grapheme consumes
the maximum width of its bases (W/F two, other/ambiguous one); combining-class
marks, ignorables and controls have no independent cells. Tabs retain help
column alignment or advance four cells. Flags and complex emoji have no
special width/shaping rules beyond this policy. EGC members share the cell
origin; marks use existing MarkPen positioning. This is a documented grid
policy, not full wcwidth, emoji or complex-script conformance.

Rendering uses whole-cluster face selection, immediate borrowed cached masks,
a common baseline and clipping to the reserved cells. Fallback natural advance
never changes the next cell's origin. Row height includes face line height.
Layout, hit testing, input scrolling, selection and cursor use the same cell
widths. Left/right, deletion, clipboard truncation and insertion use existing
BSF grapheme helpers. Ctrl+A and Ctrl+V use the same input buffer as the native
popup clipboard adapter. History and 512-line scrollback remain in place.

## Stage four: acceptance

Results, exact commands, artifacts, hashes and remaining limitations will be
recorded as validation completes. Host tests are not native GUI acceptance.


## Diagnostic findings so far

The first native save/reopen run failed: Notepad called write on a nonexistent
file, while BWS correctly requires an existing object. Production Save As now
calls the existing unprivileged file-creation API first, with unchanged PZB
checks. Native logs then confirm `file_create=ALLOW` and `file_write=ALLOW` for
`/uzytkownicy/unicode.txt`. Earlier failures are retained as failures.

A separate native left/delete byte comparison still fails. Initial source and
Notepad paste bytes match exactly; read-only QMP observation shows the expected
cluster boundaries (including 199 and 204) but a left command ends at byte 198.
Host production-editor tests with actual bundled fonts pass the corresponding
case. This is under investigation, not native acceptance. Later stages have
not been enabled while this stage-one failure remains unresolved.

The first count-index build hit an unresolved `__popcountdi2` in the
freestanding app. A bounded SWAR bit count replaces that host-runtime dependency;
no new native library is required. The official 1,187-case suite also validates
the index's floor, previous and prefix-rank operations.

Coverage-guided parser smoke campaign: GCC `-fsanitize-coverage=trace-pc`,
ASan and UBSan, fixed seed 0x425346, 10,001 executions, 602 coverage hash buckets,
8 retained corpus members / 4,997,016 bytes, no reported sanitizer finding.
This is a bounded custom coverage-guided campaign, not libFuzzer/AFL, and not a
complete parser audit. Host corpora remain under `.bsf-test/fuzz-corpus` and
are excluded from the release asset manifest.


Native input root cause: the unchanged USB HID report was emitted as an
immediate repeat. Private diagnostic logs show input value 1, value 2, value 0
for one short QEMU left-key action, with two matching editor commands. The
first editor command correctly moves 204 -> 199; the repeat moves 199 -> 198.
This was not a grapheme segmentation defect. `sterowniki/usb/usb_hid.h` now
uses a per-device 400 ms initial delay and 33 ms repeat interval for unchanged
reports. `InputDevice` adds 24 bytes of internal typematic state; no BWS or BUR
ABI changes. Existing PS/2 and VirtIO repeat semantics are preserved. This is
a verified dependency of native editing, not unrelated paging/security work.
The private trace is disabled/absent in the rebuilt public kernel; its run is
marked DIAGNOSTIC, not PASS. The ordinary acceptance scenario keeps 80 ms QEMU
key holds and unchanged byte/cursor assertions.

Production keyboard/TEXTINPUT/dialog host target: 7,293/7,293 PASS.
VirtIO/input host target: 168 groups PASS, zero FAIL (ASan/UBSan).
Dedicated USB HID timing target passes early-report suppression, delayed
repeat, rate limiting, release and a fresh subsequent press. Tests update the
old immediate-repeat expectation rather than dropping repeat coverage.
The current production Notepad host target, using actual bundled TTF files,
passes 60,662 assertions, including exact fixture editing, file creation,
save/reopen, clipboard, malformed input and 60,000 backward movements.
Observed sanitizer host time for that backward-key workload was 16.15 seconds
under concurrent build load; this is not a native latency result or speedup.

## Resumed verification — 2026-11-08

The ordinary native stage-one test was repeated after catalog expansion:
`.bsf-test/resume-stage2-native/result.json` reports PASS for initial clipboard
bytes, cluster-left/Delete and exact save/reopen bytes, with no guest fault.
This run used ISO SHA-256
`1ee5ca6c91c44878a61a75b168b0098e0618516d92509ba0b3c4880add074b69`;
a later Terminal build supersedes it. The original failure/diagnostic history
above is retained as history, not the current verdict.

The 1,187 official cases, index boundaries and 10,000 malformed/mutation
inputs pass again. The bitmap Shell host regression passes 2,000 assertions
before addition of the new grapheme/TTF-specific assertions. TTF native,
installed-disk, matrix and final-release results are still pending.

### Current resumed results

- Native Terminal: PASS on Linux QEMU/OVMF at 1280x720, 100%, 16 px.
  Actual DejaVu Sans Mono grid: 10 px cells, 21 px rows. Read-only QMP
  observed exact pasted bytes, two left movements across quote and five-byte
  grapheme, whole-cluster Delete, replacement, exact command-output bytes,
  history recall and TTF -> bitmap -> TTF recovery. See
  `terminal-native-result.json`, `terminal-native-observed.json`,
  `terminal-ttf-edited.png`, `terminal-ttf-output.png`,
  `terminal-bitmap-recovery.png` in the evidence directory. This interim
  ISO hash is in its result; final build verification follows below.
- Native-font Terminal host harness: 2,192 assertions PASS under ASan/UBSan,
  including uniform ASCII advances, proportional rejection, marks-only/wide/
  supplementary/emoji/RI grid policies, exact editor operations, clipped
  real masks, repeated-mask cache reuse and recovery selection.
- BSF core: 12,204 checks PASS. UI/browser: 9,084 checks PASS.
- Production Notepad editor/save/reopen: 60,662 checks PASS. The 60,000
  backwards movement workload took 29,669,903,126 ns in this concurrent
  sanitizer run. This is not native latency or a speedup claim.
- Expanded-resource tests: 4,601 checks PASS after adding catalog-allocation
  failure and default-family stability on installing an earlier-sorted family.
  Host discovery: 32 faces 0.207 s; 64 faces 0.694 s; 128 faces 1.380 s.
  16 MiB discovery 0.088 s, load+raster 0.037 s in this run. Different host
  load/sanitizers mean these are observations, not controlled comparisons.
- Coverage-guided GCC trace-pc campaign: 10,001 executions, 602 hash buckets,
  eight retained inputs / 4,997,016 bytes, fixed seed 0x425346, no reported
  ASan/UBSan finding. This remains a bounded custom campaign, not exhaustive.
- Loader BUR: 17 groups PASS. Storage/BSP2: 10,512 PASS, 0 FAIL;
  directory/file moves: 138 assertions PASS. Settings themes/Start: 2,696
  checks PASS. Desktop TTF/Settings/Start: 188,032 checks PASS; power PNG
  geometry contributes a further 975 checks PASS.
- All 23 generated BUR v1 files pass offset/VA/entry/alignment/file bounds;
  native linker assertions also cover TEXT+rodata, DATA+BSS and unsupported
  sections. `bur-layouts-resumed.json` records interim hashes.

Native acceptance automation corrections are retained with evidence:

1. First installer GUI run used fixed host sleeps. The file named `typed.png`
   actually still showed the disk-selection page, so no install had begun.
   QMP click/key sequencing now waits native clock ticks between GUI steps.
2. Second run accepted final confirmation and wrote GPT (progress 10), then
   the driver incorrectly demanded clock progress while installation was in
   synchronous kernel disk I/O. It timed out in the driver. Final confirmation
   now enters the existing serial-progress/COMPLETE wait directly; no installer
   completion assertion or timeout was relaxed. Completion remains unproven.
3. A display run passed 800x600 and 1024x768 transitions but captured Start
   before its already-open power submenu had been composited at 1280x720.
   The full icon alpha-pixel oracle failed. The driver now waits for that same
   complete pixel oracle before final capture. Assertions are unchanged.
4. Installed-disk observation now selects the matching private ELF by the
   installed public kernel's hash, preserving the stripped production kernel.

These failures are not recorded as installation, reboot or matrix PASS.
The first two installer directories and the failed display run are retained
under `.bsf-test/installed-cxvyo3vp`, `.bsf-test/installed-x6m3wduy` and
`.bsf-test/resume-display-matrix` respectively.

### Native maximum-allocation finding

The exact 16 MiB native fixture was rejected, despite passing the host parser
harness. `gui_malloc` adds an aligned 32-byte `BlokPamieci` header and rounds
the raw request to pages. BWS35's existing non-Tapety limit is 16 MiB per raw
allocation, so that payload would require 16 MiB + 4 KiB. No heap, security or
executable limits were increased. The production file-payload cap is therefore
**16,777,184 bytes (16 MiB minus 32 bytes)**, with the aggregate resident-file
budget still 16 MiB. This is the largest payload that fits the current native
single-allocation API and its actual allocator header. Native testing at this
corrected boundary is pending; earlier exact-16-MiB host results must not be
read as native acceptance.

The first private resource ISO also omitted the separate `bsf-settings`
Multiboot module. Its missing Settings process was a fixture-packaging failure,
not loss of other apps or RAM-filesystem exhaustion. The resource-image tool
now preserves that production module. Both failures are retained.

## Continuation — 2026-11-09

Resumed from thread `01a1b5bf-4aff-7502-bde4-eebef6d7a2db` after inspecting
the actual tree and later evidence. Subsequent independent loader/security
changes already exist in the repository. They are preserved, not attributed
to BSF work. This continuation explicitly builds `BUR_FORMAT=1`; the repository
default is not changed. All 23 generated application contracts pass the BUR
segment verifier. The linker enforces their existing segment capacities.

Settings' production font preview still wrapped scalar by scalar. It now
appends complete BSF graphemes and measures each candidate line with
`BSF::measure_text`, retaining the same fallback and kerning as its painter.
The fixed sample strings remain byte-preserved. Explicit bounded copying and
length counting keep this path freestanding: an initial build exposed unwanted
host `strlen`/`__memcpy_chk` references, which were removed before release.

Fresh ASan/UBSan results in `bsf-completion-2026-11-09/`:

- Official Unicode 15.1: 1,187 cases plus 10,000 deterministic malformed and
  mutation inputs PASS (`host.txt`).
- Expanded catalog/resource/failure handling: 4,604 checks PASS, including
  32/64/128 records, 3 MiB and 16,777,184-byte fixtures (`host.txt`).
- Production Notepad: 60,662 checks PASS, including exact save/reopen bytes;
  60,000 backward movements took 10,734,644,436 ns in this sanitizer run.
- Native-font Shell host harness: 2,201 checks PASS (`host.txt`).
- Core: 12,204; shared UI/browser: 9,084; Husarz selection/clipboard:
  987,380 checks PASS. BUR loader: 17 groups PASS (`regressions.txt`).
- GCC trace-pc parser campaign: 10,001 executions, 602 coverage buckets,
  eight retained inputs / 4,997,016 bytes, seed `0x425346`, no reported
  sanitizer finding (`fuzz.txt`).

Build command: `make -j4 BUR_FORMAT=1 iso`; kernel and application sources
were rebuilt after the initial absence of production build artifacts. ISO
asset and release metadata verification passed. Exact artifact hashes and
native acceptance status are recorded in
`bsf-completion-2026-11-09/release.json`. Fresh native acceptance is running;
earlier installed-disk results must not be attributed to this new kernel.

### Fresh native findings and harness corrections

- Editing on the new release ISO: PASS, including cluster-left/Delete and
  exact saved/reopened UTF-8 bytes (`editing-result.json`).
- Terminal UEFI: PASS, exact native clipboard/input/output/history plus
  TTF/bitmap recovery (`terminal-result.json`, `terminal-terminal-observed.json`).
- Desktop/Settings/Start host regression: 188,032 checks PASS; themes and
  power controls: 2,696 checks PASS (`desktop.txt`).
- The first IDE-CD installer run timed out before installer readiness. Its
  serial log progressed through GRUB module loading and RAM provisioning;
  it did not complete an installation. Evidence is retained as
  `installer-ide-failed.txt` and `installer-ide-serial.txt`. The harness now
  offers explicit `--installer-media virtio`, using the same supported
  read-only VirtIO ISO backend as the ordinary live acceptance driver. The
  IDE default, 300-second boot bound, installer completion assertion,
  sentinel checks and disk-only reboot requirements remain unchanged.
- At 1366x768 the QEMU 8.2 PPM probe had skewed RGB rows, while PNG at the
  same native geometry was correct (`display-1366-ppm-probe.png`
  versus `display-1366-native-png.png`). Captures now consistently use PNG;
  pixel assertions remain unchanged. No OS display code was modified.
- A subsequent matrix run rolled back 800x600: the driver sent Enter after
  kernel commit but before the app consumed its pending-state event. The
  driver now observes `[EKRAN] Tryb zatwierdzony.` before proceeding and
  rejects a rollback; an ignored early Enter can be sent again through
  ordinary input. The failed run remains in the evidence directory.
- The initial resource run did not launch Settings after browser activation,
  so it is not a native catalog verdict. Start launches now explicitly focus
  the search field and capture the typed query before Enter. The catalog,
  parsing, resident-memory and pixel assertions are retained.

The two matrix failures and the catalog launch failure remain FAIL, regardless
of subsequent rerun outcomes. Host profiling (`profile.txt`) records a
2,097,152-byte mask capacity, 200,000 cache hits and one miss in its repeated
mask workload. Timings are observations under concurrent QEMU/build load,
not a controlled baseline comparison or a claim of improved performance.

### Bounded lookup acceleration and final rebuild

Further read-only observation corrected the initial launch diagnosis: Settings
had an active process before it published a window. The observer now optionally
identifies the process by its executable path during initialization, while
still requiring actual native window pixels for acceptance. An interrupted
diagnostic run sampled Ring 3 execution in repeated family comparisons and
text metrics; it found 128 catalog entries, only two resident face loads and
3,693,376 I/O bytes. Its later window capture and stack snapshots are
diagnostic evidence, not PASS (`catalog-stack-full.json`).

Production BSF now retains four system-style choices keyed by primary face ID,
the default catalog index, and the most recent face-ID index. These add exactly
40 static bytes, keep catalog indices rather than file-backed Face pointers,
and are cleared with existing resolver invalidation at rescan/shutdown. Choice
keys account for preview/configured-family changes. CSS family parsing,
cluster/scalar fallback order, glyph masks and advances are unchanged. Settings
also keeps at most 128 temporary family-name pointers on its reload stack
(1,024 bytes on x86-64), eliminating repeated ID-to-family scans during grouping.
Those pointers never survive reload or rescan.

ASan/UBSan after this change: UI/browser 9,084; resource handling 4,604;
Terminal 2,201 checks PASS (`cache-host.txt`). The first rebuild measured
Husarz TEXT+rodata 448 bytes past its old boundary; the next measured Settings
32 bytes past its boundary. Each received exactly one aligned 4 KiB TEXT page,
with its BUR v1 header and DATA offset/VA updated together. No DATA/BSS, heap,
parser or cache budget was expanded. All 23 binary contracts pass again
(`bur-layouts-cached.json`). The final rebuilt ISO passed font asset and release
metadata verification (`cache-build3.txt`). Its exact hashes are in
`release.json`; results for the earlier image remain tied to
`release-initial.json`.

The initial-image display run passed all ten transitions, including 1366x768,
2560x1440 and all five scales (`display-result.json`). The initial-image
installer reached `COMPLETE verified` at 100%. A fresh installation and native
regressions were started for the final cached build; completion is still
pending. Installed fixtures now snapshot matching app ELFs as well as selecting
the kernel ELF by public-kernel hash, so a later local rebuild cannot corrupt
read-only observation of an older installed image.
