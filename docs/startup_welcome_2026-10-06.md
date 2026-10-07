# System welcome overlay — 2026-10-06

Implementation continues the current worktree after the manually accepted legacy-terminal/first-frame fix (`7e3e98b`). Host validation is complete; guest visual acceptance remains manual.

## Audited lifecycle

| Lifecycle point | Existing implementation / integration |
|---|---|
| Reliable framebuffer and scene surface | `kernel_main → InicjalizujGrafike`: validated mode/pitch, graphics allocator, backbuffer, initial neutral presentation. The UI Unicode glyph tables are compiled into the kernel; no optional font-file load is needed. |
| Reliable animation clock | Existing ACPI/HPET initialization. `powitanie_inicjalizuj` immediately afterwards presents the welcome surface through `PrzeniesNaEkran`, before scheduler setup and WM creation. |
| Compositor availability | Existing compositor globals and graphics backbuffer permit boot/modeset composition before Ring 3. Its PID 0 dirty service later advances the welcome state. No additional task or timer is installed. |
| Active theme | `motyw_inicjalizuj` after PSF/program provisioning, before `tapety_inicjalizuj`, `ekran_inicjalizuj` and WM process creation. This ordering remains intact. The welcome has an intentional fixed dark brand background independent of an unresolved saved theme. |
| Wallpaper initialization | `tapety_inicjalizuj` prepares resources and the existing default/fallback. Persisted JPEG restore continues in the asynchronous Tapety `--restore` worker. Successful publication uses existing graphics wallpaper state and dirty invalidation. |
| WM initialization | `/menedzer_okien.bur` starts after initial processes are created and multitasking is enabled. WM resolves monitor/DPI/work area, registers its root and takes input ownership. Root registration alone is not readiness. |
| Desktop model / launchers / saved positions | `odswiez_wpisy_pulpitu`: filesystem enumeration, static `DesktopEntries` catalog, layout construction, saved settings load/apply (or valid default positions), then `hash_pulpitu_wazny=true`. |
| Taskbar and first desktop frame | `RysujPulpit(true)` paints icons and `RysujTaskbar`, registers the drop target and calls `gui_odswiez`. The existing first-frame publication gate must have accepted the root before the kernel handshake succeeds. |
| Usable desktop | WM calls the existing readiness syscall only after a valid model and full paint. Kernel validates PID/root ownership, active z-order-zero full-screen surface, geometry and presentability. Catalog failure retries on the existing WM timer. Wallpaper-ready **or intentional valid fallback** is sufficient. |

The welcome does not await the asynchronous wallpaper worker, its success flag, or optional files. The explicit background result is `Tlo::Tapeta` or `Tlo::Zastepcze`; pending/failed wallpaper uses the existing valid fallback. Wallpaper failure cannot hold the overlay indefinitely. Model/root failures are critical and are not misreported as ready.

## Architecture and presentation

- `ekran_powitalny.h`: small deterministic clock-driven state machine and clipped Unicode rendering helpers.
- `ekran_powitalny.cpp`: kernel orchestration, retained supervisor graphics surface, readiness validation, dirty scheduling and RGB presentation-source selection.
- `ekran_powitalny_api.h`: minimal kernel integration interface.
- This is a dedicated **system presentation overlay**, outside the BWS window table. No PID, normal app window, titlebar, taskbar entry, Alt+Tab entry, fake Shell, focus registration or app-title matching exists.
- Both full-screen and partial backend copies select the welcome surface. The ordinary compositor continues constructing its scene underneath; no welcome pixels enter the normal scene backbuffer.
- The existing `created → registered → first committed frame → presentable` lifecycle is preserved. The compositor remains application-independent.
- During welcome, graphics input delivery consumes application pointer/key/text events; timer and lifecycle events continue. Pointer coordinates, movement, cursor transactions and input drivers are unchanged. The overlay receives no app focus.
- One full-screen surface is allocated, reused and resized only when required by framebuffer geometry/pitch. It is released after completion. No buffer is allocated per animation frame.
- Rendering uses `CzcionkaUI::dekoduj_utf8`, `bitmapa_znaku`, `szerokosc_znaku`, `postep_znaku`, and `zmierz_tekst`, with the existing `SkalaUI` rounding. Text centering uses measured proportional glyph advances. No ASCII-only renderer or character-count centering exists.
- The title is `Bursztyn OS`; the background is `0x101720`, with restrained amber title/rule and light message text. No version string, optional image, fake percentage or debug text is included.

## Timing and removal

States: `Nieaktywny → Widoczny → Zanikanie → Zakonczony`. A final presentation latch retains the preceding overlay until a **whole-screen** scene is presented.

| Elapsed monotonic time | Message |
|---|---|
| 0 ≤ t < 1.6 s | Witamy w Bursztyn OS |
| 1.6 ≤ t < 3.2 s | Polski system operacyjny |
| t ≥ 3.2 s | Niezależny system |

Around 1.6 s and 3.2 s, message RGB fades out over 150 ms and in over 150 ms. The title/background stay fixed. Normal minimum sequence time is **5,000,000 µs**, measured from welcome initialization/first presentation. If the desktop is late, the third message stays visible until readiness.

Final fade starts only after elapsed ≥ 5 s **and** desktop readiness, and takes **400,000 µs**. Earliest normal desktop reveal completion is approximately 5.4 s. Presentation sampling is limited to once per 33,333 µs, independent of refresh rate and scheduler frequency; normal compositor pacing is retained. Message animation dirties only the central band. Full-screen work is limited to the final fade and required lifecycle/geometry changes.

This uses **computed RGB interpolation**, not framebuffer alpha: each channel is `(welcome*(255-weight) + scene*weight + 127)/255`. Every presented region is regenerated before blending, preventing cumulative blending artifacts. The ordinary scene is retained throughout.

After the fade clock completes, a cursor-only/partial presentation keeps the preceding overlay. The queued full-screen compositor transaction paints the complete scene, then the presentation-source latch removes the welcome and releases its buffer. There is no neutral intermediate framebuffer or stale welcome background. Calls after completion cannot restart it.

Readiness is revalidated against the published WM root and current framebuffer size. A modeset that invalidates the root pauses/restarts the final fade until WM republishes. Revoking a fade also regenerates the entire opaque welcome surface so old blended desktop pixels cannot leak. Resolution, pitch and scale changes rebuild/recenter the surface safely.

No sleep, busy-wait, kernel delay, scheduler stop, new IRQ exclusion, filesystem delay or wallpaper pause is introduced.

## Diagnostic and exceptional boots

Append `bursztyn.splash=0` to the existing GRUB `multiboot2` kernel command line for a clean diagnostic boot. No GRUB defaults were changed. The existing bounded Multiboot command-line walk is reused; `opcje_startowe.h` supplies exact whitespace-delimited token matching, rejecting prefixes and values such as `bursztyn.splash=01`.

`bursztyn.test=automated` disables the overlay/presentation delay. `bursztyn.install=1` also bypasses it; installer code is unchanged. Host timing tests inject clock values and never sleep five seconds. Serial/kernel/WM diagnostic logging is preserved. Disabling welcome never reactivates the legacy kernel terminal.

The available calibrated monotonic source is HPET; scheduler ticks explicitly have no wall-time unit. If HPET is absent, welcome is bypassed with a serial explanation, preserving clean startup rather than inventing a time conversion. Graphics-surface allocation failure also logs a clean-startup fallback and releases retained memory. These exceptional boots do not guarantee the normal five-second welcome sequence.

## Files

Production/build modified: `grafika.cpp`, `kernel.cpp`, `menedzer_okien.cpp`, `skladacz_obrazu.cpp`, `Makefile`.

Production added: `ekran_powitalny.cpp`, `ekran_powitalny.h`, `ekran_powitalny_api.h`, `opcje_startowe.h`.

New tests: `narzedzia/test_powitanie.cpp`, `narzedzia/test_powitanie_policy.py`.

Existing tests extended: `narzedzia/test_boot_graphics.cpp` (real presentation-source selection/input/readiness), `narzedzia/test_compositor_cursor.cpp` (welcome tick fixture), `narzedzia/test_shell_terminal.cpp` (compare version against `BURSZTYN_NAZWA_WERSJI` instead of obsolete literal 1.0.1.6). Modern Shell production code is unchanged.

No BWS ABI, installer, BSP2, PZB, desktop persistence format/implementation, scheduler frequency, HPET implementation, GPU timeout, VirtIO Input, cursor movement/renderer or GTK/SDL policy changes were made.

## Automated validation

| Suite | Final host result |
|---|---:|
| Welcome lifecycle, timings, RGB boundaries, Unicode/centering, resource fallback, reconfiguration, final-frame latch, exact options; 1280×720 / 1920×1080 × 100% / 125% | 822/822 PASS |
| Welcome integration policy | 15/15 PASS |
| Existing startup policy / theme order | 19/19 PASS |
| Existing first-frame compositor visibility | 86/86 PASS |
| Real kernel graphics / frame commit / full and partial welcome presentation | 33/33 PASS |
| Production compositor scene/lifecycle | 73,890/73,890 PASS |
| WM routing / manual Start-menu Shell launch | 172/172 PASS |
| WM decorations | 3,353,022/3,353,022 PASS |
| Shell host | 1913/1913 PASS |
| Desktop file actions / layout | 6739/6739 PASS |
| Desktop settings BWS/PZB | 41/41 PASS |
| Desktop selection/theme | 161/161 PASS |
| BursztynUI, including Unicode font/UTF-8 robustness | 1379/1379 PASS |
| Command bindings | 23/23 PASS |
| Theme unit, ordinary and separate sanitizer build | 155/155 PASS each |
| Theme GUI integration | 25/25 PASS |
| Cursor host and cursor hover | PASS (suites do not emit assertion totals) |
| Windows GTK / Linux SDL launcher policy | 7/7 PASS |
| `git diff --check` | PASS |

Compositor popup restoration's 73,816 assertions are included in the 73,890 compositor total, not an additional total.

**ASan/UBSan: all final C++ sanitizer suites passed without memory or undefined-behavior reports.** Python source/configuration checks are not sanitizer tests. Most suites run with `detect_leaks=0`; the production compositor suite used its existing `detect_leaks=1` and also passed. No guest-memory safety claim is made.

Compiled welcome function frames are bounded and small (largest individual frame: 192 bytes in glyph rendering). No large local framebuffer or Ring 3 stack allocation is added.

The existing Shell test initially failed because its literal version expectation lagged the current central version 1.0.1.7. Its final central-version assertion passed. A host-fixture compilation conflict with freestanding placement-new was fixed by using a heap-backed raw scanout instead of an STL container. Final suites are clean.

Final build details are appended below. No QEMU, `make rm` or `make clean` was run.

Logs: `/tmp/welcome-focused-final.log`, `/tmp/welcome-graphics-cursor-final.log`, `/tmp/welcome-regression-core.log`, `/tmp/welcome-wm-final.log`, `/tmp/welcome-regression-shell.log`, `/tmp/welcome-regression-desktop.log`, `/tmp/welcome-theme-sanitized.log`, `/tmp/welcome-launcher-policy.log`, `/tmp/welcome-build-final.log`. An actual renderer preview at 1280×720 / 125% was inspected at `/tmp/welcome-preview.png`.

## Manual runtime acceptance

Boot the final ISO normally and observe all three Polish messages followed directly by the complete desktop. Verify no legacy brown terminal, old Shell, partially drawn desktop/taskbar or visible technical loading. Repeat at saved 1280×720 and 1920×1080, 100% and 125%, and launch modern Powłoka from Start after the welcome. Also verify `bursztyn.splash=0` remains a clean diagnostic boot. **Runtime PASS is not claimed.**

**Final `make -j2 all iso`: PASS, exit 0.** No warnings/errors in the final build log. ISO size: 35237888 bytes. SHA-256: `0753bd03df8372fda2db86a04065bc83406571c24655d7d5acd3d523febe88a2`.
