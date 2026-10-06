# Persistent desktop layout — 2026-10-05

Implementation continues the manually accepted desktop model; runtime acceptance of persistence is still manual. No QEMU, `make rm`, or `make clean` is used.

## Settings audit and storage

Existing system UI settings are textual files: wallpaper `/ustawienia/pulpit.cfg` (`tapety_format.h`, `tapety.cpp`), display `/ustawienia/ekran.cfg` (`ekran_format.h`, `ekran.cpp`), theme `/system/konfig/motyw.cfg` (`motyw.cpp`). They use bounded, purpose-specific serializers rather than a generic configuration service. No logged-in user or per-user settings API exists in this layer. `/uzytkownicy/Pulpit` is the shared desktop directory, not a profile for a currently authenticated user.

The new system-wide settings file is **`/ustawienia/uklad-pulpitu.cfg`**. `desktop_layout_settings.h` implements serialization and storage separately from geometry and WM orchestration. It uses existing **BWS 44/5 for reads, BWS 46/2/3 for directory/file creation and writes**. WM retains its existing PZB_USLUGI level and rights; new objects inherit the existing parent/process label policy (including default-PZB boot directories), and no authorization or filesystem internals are bypassed, and no BWS number is added.

BWS 44 returns size+1; an absent file and an empty file both return 1 through the present implementation. The creation attempt therefore accepts either case and lets the existing authorized write decide whether an object can be written. Existing nonempty objects are overwritten through the authorized BWS write.

PSF `zapisz_do_pliku` ends with `bsp_zapisz_zmiany`, including the existing Storage flush on writable block-backed BSP2. BWS3 returns that result. A successful write is also checked through a size query and readback checksum. RAM-only filesystems retain their existing successful in-memory write semantics; reboot persistence needs a writable persistent BSP2 filesystem.

**Storage guarantee:** existing PSF rename rejects an existing destination, so it cannot provide atomic replace. This uses the same direct whole-file settings write approach as existing settings, with an explicit checksum/terminator and readback verification. It does **not** claim atomicity, journaling, rollback, or arbitrary power-loss recovery. A failed or interrupted write can lose the previous settings document; invalid/torn documents safely fall back to automatic layout. A 32-bit FNV-1a checksum detects accidental truncation/torn content, not malicious modification or cryptographic collisions. Authorization still comes from BWS/PZB.

## Version 1 text format

```text
BursztynDesktopLayout=1
width=1920
height=1040
dpi=100
L\tnotatnik\t300\t200\t0
F\tzażółć.txt\t440\t200\t0
end=XXXXXXXX
```

The `\t` notation above represents actual TAB separators. UTF-8 is retained. `%` is encoded as `%25`; the decoder also understands `%HH` byte escapes. `end` contains eight hexadecimal digits: FNV-1a over every preceding byte, starting at 2166136261 and applying `(hash XOR byte) * 16777619` modulo 2^32. The document ends in a newline.

Records contain type, identity, x, y and reserved future flags. Coordinates are **the upper-left corner of the actual visual icon/label/selection bounds**, in physical pixels at saved DPI. Width and height are saved work-area context; loading does not require matching them. Only the current work-area dimensions constrain positions. DPI rescales x/y by `currentDpi / savedDpi`; actual current icon and wrapped-label geometry is always recalculated. Selection, capture, hover and gesture state are absent.

- `L`: stable ASCII **`Launcher::id`** in the shared launcher catalog. IDs: `notatnik`, `kalkulator`, `husarz`, `testy-systemu`, `snake`, `eksplorator`, `obrazy`, `demo-ui`, `kontrolki2`, `kontrolki3`, `aktualizacje`, `motywy`, `kontrolki4`. Their order, translated name and target binary path are not their persistent identities.
- `F`: exact UTF-8 path relative to `/uzytkownicy/Pulpit`. Current desktop enumeration has immediate children, so the relative path is its filename. Files and folders share this filesystem namespace. BWS metadata exposes no stable inode identity, so **rename becomes a new entry**. No path guessing or position encoding in filenames is used.

The parser bounds the document to 225,856 bytes, records to 141 (128 filesystem entries + 13 launchers), decoded IDs to 511 bytes and coordinates to unsigned decimal 0..1,000,000. Negative, overflowing and malformed numbers, invalid identities, embedded NUL/control bytes, separators in IDs and malformed percent escapes are rejected for that record. **The first valid duplicate record wins.** Unknown metadata, types and trailing fields are ignored. A malformed header, unsupported version, invalid known numeric metadata (width/height 1..8192, DPI 50..400), missing terminator or mismatched checksum rejects the whole document. Record parsing never creates a desktop entry.

## Load, reflow and writes

The first coherent desktop enumeration loads settings once. Saved records match existing typed identities. Stale records are ignored; entries missing from settings get free automatic positions after saved entries reserve their space. Fresh boot without settings does not create a settings file. Renamed/new entries auto-place, and absent/deleted/moved-away files are never resurrected. The next save serializes only currently placed entries, pruning stale records.

Geometry code keeps valid positions first, rejects out-of-bounds or colliding positions and relocates only rejected entries. Nearest free grid candidates are ordered by squared distance from the desired visual location, with row-major ties. Partial edge cells are clipped to the exact work-area boundary using actual visual height/width, so no obsolete cell margin returns. Collision tests use actual visual rectangles; no two entries occupy overlapping visual space. There is no placement underneath taskbarTop. On physically overfull screens, entries with no free candidate remain unplaced rather than overlap or extend beyond the desktop; this preserves the previous capacity limitation.

Runtime display events continue querying current BWS monitor information. Before geometry/DPI changes, existing visual positions are preserved in bounded static arrays, then current labels are measured and invalid positions reflowed. Valid positions remain unchanged at unchanged DPI. Corrections set dirty and are saved once after modeset repaint and capture release. Initial saved-layout corrections likewise save after initial desktop painting. New auto-placed entries alone do not trigger a write.

A meaningful completed single/group visual drag marks dirty. After MouseUp commits positions, finishes the gesture, releases capture and refreshes the GUI, it performs **one BWS3 layout-content write transaction**, followed by verification reads. First creation also uses the existing directory/file metadata operations; this is not a claim of one physical sector write. Motion events perform zero settings writes. Selection, rubber-band selection, themes, clicks and launching do not trigger writes. A file drop into a folder follows the existing filesystem path and does not save that departed item's previous coordinates.

There are no WM graphics locks around storage. Synchronous bounded BWS storage runs in the WM caller after capture release, outside compositor rendering. **Existing infrastructure limitation:** `BlokadaPSF` in `psf.cpp` disables local interrupts for the whole filesystem operation, including write-through/flush. The current single-core IRQ-preempted scheduler therefore cannot run the compositor during each such critical section. A slow filesystem can delay both the WM caller and guest scheduling until the existing operation returns. This implementation cannot promise uninterrupted compositor execution during save through the current BWS API. Moving the caller to another task alone would not remove that global PSF critical section; a real fix needs separate filesystem synchronization/flush work and is not implemented here. No graphics lock, scheduler or interrupt-policy change is introduced. Host RAM tests check capture ordering and failure recovery, not guest storage latency.

On failure, positions remain in memory. The pending attempt is consumed and one `[DESKTOP-LAYOUT] save failed` message is sent through BWS logging. The next meaningful layout transaction can try again; there is no timer retry, motion retry or redraw loop. Normal success is quiet. `BURSZTYNDIAGDESKTOPLAYOUT=1` enables bounded lifecycle logs for path, record/match/stale/reflow counts and save results; default is 0.

Protected launcher semantics and installed app paths are unchanged. Explorer continues using the shared catalog and receives no coordinate columns. No BSP2, BUR, CEBULA, users subsystem, installed binaries, renderer, input driver, scheduler, HPET or boot/installer policy is changed.

## Tests and stack usage

New `narzedzia/desktop_persistence_cases.h`, integrated into the existing desktop production suite, tests missing settings, singleton launcher/file restoration, multiple launchers/files/folders, UTF-8 and escapes, group transactions, transient-state exclusion, new/stale IDs, rename/delete/drop behavior, malformed/unsupported/truncated/huge input, first-valid duplicate policy, checksum corruption, 100%/125% and short/multiline edge restoration, 1920→1280 and 1280→1920, DPI changes, collision reflow, full capacity, save/read failures, interrupted writes, capture ordering and modeset re-save. It retains all existing desktop, launcher, filesystem drop and modal cases.

New `narzedzia/test_desktop_settings_authorization.cpp` runs the production serializer/storage through **actual BWS file helpers and actual RAM-backed PSF**, checking write/read rights, PZB labels and denied mutations, missing/empty file behavior and released PSF locks. `make test-desktop-settings-host` runs it with ASan/UBSan. The generic file-action fixture treats unimplemented settings storage as unavailable; persistence cases install their dedicated I/O hook.

Production `-Os -fstack-usage` measured the largest modified WM function `_start` at **1280 bytes**; `aktualizuj_layout_pulpitu` **368**, enumeration/load orchestration **272**, parser **176**, serializer **160**, geometry reflow **160**, gesture handler **144**, storage save **64**, and save orchestration **16** bytes. The largest emitted frame anywhere in the WM translation unit is the unchanged PNG inflater **3552** bytes; the unchanged file-dialog path normalizer is **2624** bytes. These are individual compiler frame measurements, not an assertion that an entire nested call chain has zero additional stack use. No large record/document array is local. Persistent state is about 300 KiB on the heap; bounded coordinate scratch arrays are static. Diagnostic mode was also compiled explicitly.

Final host results and ISO build are appended below.

## Changed files

Production: `desktop_entries.h`, `desktop_layout.h`, `desktop_layout_settings.h` (new), `menedzer_okien.cpp`.

Build: `Makefile` (WM header dependency and settings authorization host target).

Tests: `narzedzia/desktop_persistence_cases.h` (new), `narzedzia/test_desktop_settings_authorization.cpp` (new), `narzedzia/test_desktop_files_host.cpp`, `narzedzia/file_operations_host_fixture.h`.

Documentation: this report.

## Manual acceptance

No runtime PASS is claimed. Move several built-in launchers and user files/folders, reboot, verify restoration, change resolution, verify usable bounds, reboot again and verify corrected positions remain stable. Repeat on writable persistent storage at 100% and 125% scaling.

## Final validation results

| Suite | Result |
|---|---:|
| Desktop production, including persistence and all launcher/gesture cases | **6739/6739 PASS** |
| Explorer, including shared launcher catalog | **12,496/12,496 PASS** |
| BursztynUI controls/dialogs | **1379/1379 PASS** |
| Rounded popup/dialog geometry and modal routing | **308,494/308,494 PASS** |
| WM routing and launcher targets | **164/164 PASS** |
| WM decorations | **PASS at 100%/125%**, suite emits no count |
| Theme unit (normal and separate sanitizer run) | **155/155 PASS** |
| Desktop selection/theme | **161/161 PASS** |
| Theme GUI/static integration | **25/25 PASS** |
| Command bindings | **23/23 PASS** |
| Storage/BSP2 | **10,493 PASS, 0 FAIL** |
| Native filesystem moves | **138 assertions PASS** |
| Production BWS move/clipboard authorization | **438 assertions PASS** |
| New settings serialization/storage through production BWS/PZB/PSF | **41/41 PASS** |
| Windows GTK / Linux SDL policy | **7/7 PASS** |
| Explicit diagnostic-mode compilation | **PASS** |
| `git diff --check` | **PASS** |

The final current desktop suite used GCC `-O0 -g0 -fsanitize=address,undefined` (large UI translation units compile much faster without optimization). Settings authorization used its Makefile ASan/UBSan target; Explorer, BursztynUI, popup, routing, decorations, selection/theme, storage/move authorization and command suites used their existing sanitizer Makefile targets. Theme unit was also compiled and run separately with ASan/UBSan. **No sanitizer reports occurred.** Leak detection is disabled for these existing host fixtures; this is not a leak-free runtime claim. Python GUI/policy checks are static host validation.

Logs: `/tmp/layout-desktop-current.log`, `/tmp/layout-settings-auth-final.log`, `/tmp/layout-explorer-fs.log`, `/tmp/layout-ui-controls.log`, `/tmp/layout-routing-ui.log`, `/tmp/layout-theme-sanitized.log`, `/tmp/layout-launcher-policy.log`, `/tmp/layout-stack-final.log`, `/tmp/layout-diag-compile.log`, `/tmp/layout-build-final.log`. Test-generated tracked WM executables were restored; build outputs are generated artifacts.

**`make -j2 all iso`: PASS, exit 0.** `BursztynOS.iso` generated (35,190,784 bytes). The final build emitted no warnings or errors. No QEMU, `make rm`, or `make clean` was run. No runtime PASS is claimed.
