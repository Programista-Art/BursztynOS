# Startup legacy terminal flash — 2026-10-06

Continues commit `c28f720` in the current worktree. The manually accepted desktop, Explorer, Shell editor, themes, drag/drop and persistent layout are retained. No guest runtime PASS is claimed.

## Exact cause and boot audit

The brown window is a **kernel-owned legacy terminal**, not a `/shell.bur` process and not stale modern Shell pixels. `grafika.cpp` statically initializes `okna[0]` with title `Powłoka Bursztynowa`, position `(20,20)`, size `660×360`, client background `0x001A0B00`, and previously `widoczne=true`.

The exact painting function is **`RysujOkno(0)`** in `grafika.cpp`. It uses fixed legacy colors: frame `0x008A5A00`, active titlebar `0x00FFBF00`, title text/client background `0x001A0B00`, button fills `0x00E58A00`. These colors bypass the modern theme API.

The compositor's full-frame path (`skladacz_obrazu_zloz_klatke`) and dirty-region path (`skladacz_obrazu_obsluz_dirty`) both call **`grafika_naloz_okno_terminala_region`**, which used to call `RysujOkno(0)` and `RysujTekstZBufora` while `pid_przejmujacy_mysz == -1` and the legacy visibility flag was true. The compositor does not recognize any application by title.

The audited boot sequence was:

1. `kernel_main` disables IRQ delivery, initializes GDT/TSS/IDT/VMM/syscalls, then `InicjalizujGrafike` sets up the boot display and backbuffer. The legacy terminal was already marked visible in static data.
2. Heap, kernel stacks, HPET and `InicjalizujPlaniste` prepare PID 0. Multitasking remains disabled until all initial Ring 3 processes exist. Hardware/display probing may switch to the VirtIO backend.
3. PSF is initialized and built-in binaries installed. Installing `/shell.bur` is file provisioning, not process creation.
4. Previously, `tapety_inicjalizuj` and **`ekran_inicjalizuj` ran before `motyw_inicjalizuj`**. With a saved display mode, `ekran_inicjalizuj → zastosuj → skladacz_obrazu_zloz_klatke` can compose immediately, before WM exists. This renders the kernel terminal through the overlay call above. Wallpaper publication also schedules composition. On boots without an immediate modeset composition, PID 0's dirty processing can reach the same overlay before WM takes input ownership.
5. The kernel creates `/menedzer_okien.bur`. Persisted JPEG restoration may create the Ring 3 Tapety worker with `--restore`. Automated tests and the installer are launched only under their explicit boot modes. **Normal boot does not create a Shell process.** The scheduler itself has no Shell spawn/window rendering path.
6. After enabling multitasking/IRQ delivery, WM `_start` resolves monitor/work-area geometry, creates its root layer through BWS33, and takes GUI input ownership. The old `bws_gui_ustaw_przejecie_myszy(true)` hides the legacy terminal, explaining its disappearance.
7. WM enumerates/layouts desktop entries, seeds clock/window state, executes `RysujPulpit(true)` and `gui_odswiez`, then calls the desktop readiness handshake. PID 0 presents the composed desktop.
8. The Start-menu action `uruchom_powloke → uruchom_lub_aktywuj → uruchom_program_gui` launches `/shell.bur` on demand. Modern Shell `_start` allocates scrollback, resolves monitor/DPI geometry, creates its private layer, sets its title/geometry, creates the scrollbar and paints the initial terminal via `shell_wypisz → shell_rysuj_terminal → shell_publish`. Colors are read through `motyw_kolor`, whose first lookup requests the resolved kernel palette.

Other audited references: `syscalls.cpp` grants the Shell's existing file/network/program rights but does not autostart it; `programy/test/ekran_testy.h` explicitly launches Shell in display regression tests, not normal boot. `backend.cpp` and `missing_backend.cpp` contain older source fragments including a legacy adapter invocation, but are not compiled into Shell or the boot targets. `shell.cpp` is the sole Shell source in the Makefile rule. `boot_log_gui` had a separate direct legacy redraw/present sequence but has no current caller; it is now headless so it cannot revive the artifact later. The legacy BWS window drawing ABI remains for existing clients; WM startup and modern Shell do not call it.

## Fix and final lifecycle

This combines removal of automatic kernel-terminal presentation with a **generic first-frame publication gate**, not an application/title-specific compositor workaround.

- The kernel terminal starts hidden. Input release, missing-owner repair and process exit never reactivate it. Kernel text/boot diagnostics may retain terminal text internally but cannot publish the legacy window.
- Boot graphics presents an intentional modern neutral background. Backend activation rebuilds the background before presentation rather than importing a newly allocated device framebuffer as valid desktop pixels. Wallpaper loading/publication remains unchanged and JPEG restoration remains asynchronous.
- `motyw_inicjalizuj` now runs before wallpaper/display initialization, so saved-mode startup composition uses the resolved palette. No sleeps, delays or wallpaper decoding synchronization were added.
- Registration allocates and clears a surface, assigns its existing ID/generation, and permits app drawing and initial focus events. **`klatka_gotowa=false` excludes it from both composition paths, visibility snapshots and pointer hit testing.** Focus events remain available to apps that paint in response to focus.
- Existing BWS17 (`gui_odswiez`) commits the first frame only after drawing has produced a pending dirty rectangle. An empty refresh does not publish a new window. `skladacz_obrazu_publikuj_klatke` marks readiness and invalidates the entire window including WM chrome. BWS emits the existing `BWS_ZDARZENIE_OKNO_POKAZANE` once for that transition.
- The compositor includes a window only when its allocation/metadata are valid, visibility is requested, and a frame has been committed. Apps are responsible for committing their complete initial frame through the existing refresh contract; modern Shell already blits its complete client frame before refreshing.
- Recreating/reusing a cleared surface or changing its buffer size resets readiness. Failed creation preserves the previously valid surface. Minimize/restore preserves readiness and cannot bypass the gate for a never-painted window. Destruction resets readiness; a reused PID receives a new generation and rejects old window IDs. Existing modeset transactions preserve readiness when copying valid retained frames.

Boot flow: **kernel initialization → neutral modern background → resolved theme/display/wallpaper initialization → WM registration (not presentable) → complete themed desktop paint/commit → composition/presentation → desktop readiness acknowledgement**. Wallpaper appears through its existing readiness/publication path. Shell registration/first publication occurs only after explicit user launch (or an existing explicit developer test).

## Changed files

Production:

- `grafika.cpp`: headless legacy terminal state/ownership transitions and boot logger; modern fallback/initial background; no import of fresh scanout pixels; BWS frame commit and existing lifecycle notification; presentable-only pointer hit testing.
- `kernel.cpp`: resolve theme before display initialization can compose.
- `skladacz_obrazu.cpp`: generic frame readiness, publication, composition filtering, effective visibility snapshots and lifecycle reset.
- `skladacz_obrazu.h`: document/export the internal first-frame commit API.

Build: `Makefile` adds `test-startup-visibility` (host only) and tracks the compositor API header for graphics/compositor/kernel objects.

New regression sources: `narzedzia/test_startup_visibility.cpp`, `narzedzia/test_boot_graphics.cpp`, `narzedzia/test_startup_policy.py`.

Existing regression sources: compositor fixture models desktop commit and reports total assertions; Shell tests exercise first complete client paint with dark/light palettes at 100%/125% and count assertions; WM routing tests manually launch Shell from Start at both scales; decoration tests report assertion totals.

The unmodified baseline Shell host test independently failed its popup-width assertion at 125%: it computed shadow width as `2 * int(scale)` instead of scaling the two logical pixels before rounding (`2 * scale`: 3 physical pixels at 125%). Only the width/height test expectations were corrected; Shell/menu/editor production code is unchanged.

No scheduler frequency, HPET, VirtIO command timeouts, input driver, cursor renderer, BSP2 format, layout persistence or GTK/SDL policy changes were made. No QEMU, `make rm` or `make clean` was run.

## Validation

These totals are executed host assertions or source-contract checks, not visual guest acceptance. All final checks passed.

| Suite | Final result |
|---|---:|
| New compositor startup visibility/lifecycle, 100%/125% | 86/86 PASS |
| New production kernel graphics/headless boot/first BWS commit | 19/19 PASS |
| Normal startup policy and theme initialization order (static) | 19/19 PASS |
| WM routing and manual Start-menu Shell launch at 100%/125% | 172/172 PASS |
| WM decorations and retained clients at 100%/125% | 3,353,022/3,353,022 PASS |
| Shell editor, first themed paint, clipboard, popup, UTF-8, history, wsparcie, live theme | 1913/1913 PASS |
| Production compositor lifecycle/scene | 73,890/73,890 PASS |
| Compositor popup restoration (included in preceding total) | 73,816/73,816 PASS |
| Rounded file popups/shadows/modal input at 100%/125% | 308,494/308,494 PASS |
| Desktop production including persistent layout | 6739/6739 PASS |
| BursztynUI | 1379/1379 PASS |
| Command bindings | 23/23 PASS |
| Theme unit, both ordinary and ASan/UBSan builds | 155/155 PASS each |
| Desktop selection/theme | 161/161 PASS |
| Theme GUI integration (static) | 25/25 PASS |
| Windows GTK / Linux SDL policy (static, no emulator) | 7/7 PASS |
| git diff --check | PASS |

**ASan/UBSan: no memory/undefined-behavior reports in the final suites.** C++ host suites above were run with both sanitizers, including a separate sanitized theme-unit build. Static Python checks do not use sanitizers. The desktop host suite was compiled with `-O0 -g0`; the other existing suites used their Makefile sanitizer commands. New startup suites use `-O0 -g0` with both sanitizers.

**Final post-test `make -j2 all iso`: PASS, exit 0.** No warnings/errors in the final build log. `BursztynOS.iso` is 35190784 bytes. SHA-256: `297e0903731130107590ee2989c430c44e8bc9992846c2937cb14e6fee4ac47c`. No QEMU, `make rm` or `make clean` was run.

Validation logs: `/tmp/startup-final-visibility.log`, `/tmp/startup-wm-shell.log` (routing; also records the initial obsolete Shell assertion), `/tmp/startup-shell-baseline.log` (independent baseline failure), `/tmp/startup-shell-decoration-final.log` (corrected final Shell/decorations), `/tmp/startup-decorations-final.log`, `/tmp/startup-compositor-final.log`, `/tmp/startup-popup-final.log`, `/tmp/startup-desktop.log`, `/tmp/startup-ui-theme.log`, `/tmp/startup-theme-sanitized.log`, `/tmp/startup-launcher-policy.log`, `/tmp/startup-build-final.log`.

Tracked host-test executables are restored to their pre-task versions after validation; the generated ISO/build artifacts remain available.

LeakSanitizer encountered its documented ptrace restriction after the environment switched to managed execution. Subsequent sanitizer runs use `detect_leaks=0`, retaining AddressSanitizer and UndefinedBehaviorSanitizer. This is not a leak-free guest claim.

## Manual acceptance

Boot the generated ISO in the user's normal environment and watch the entire startup: no brown Shell, legacy terminal/test window or stale app pixels before the normal desktop. Repeat with saved resolution and 100%/125% scale; confirm wallpaper readiness still follows its existing loading behavior. Launch modern Powłoka Bursztynowa from Start afterwards and verify its first frame and normal behavior. **Runtime acceptance is manual; no runtime PASS is asserted here.**
