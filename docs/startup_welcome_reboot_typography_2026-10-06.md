# Welcome reboot lifecycle and typography — 2026-10-06

Work continued from the existing, uncommitted welcome implementation on top of `7e3e98b`. No QEMU was started. All results below are host results; guest/QEMU/VirtualBox/Linux repeated-reboot acceptance remains manual.

## Stability conclusion and reproduced defect

The reported freeze after approximately three guest reboots, including an unresponsive Linux development host inside VirtualBox, was **not reproduced**. No emulator stress loop was attempted. This change does **not** establish that the Linux freeze is fixed, nor that QEMU or the splash was innocent.

A separate deterministic hang in the real production reboot function **was reproduced**: `virtio_gpu_quiesce_for_reboot()` used `while (true)` to acquire `drv->lock`. Its only exit for contention depended on a nonzero monotonic clock reaching a deadline. When the clock returns zero (the production behavior without HPET), a held lock makes that loop infinite. `narzedzia/test_gpu_quiesce_host.cpp` injected precisely these conditions in the real driver, without QEMU. Before the fix, an external two-second host watchdog terminated the probe with exit **124**; the function never returned. The watchdog is a test bound, not a production delay.

The fix replaces only this acquisition loop with the driver's existing `VIRTIO_GPU_MAX_SPINS_GUARD` bound. The existing 200 ms reboot deadline, command timeouts, and `RESOURCE_FLUSH` timeouts are unchanged. On exhaustion, the existing error path returns without touching a queue another owner may still use. The regression now passes for a held lock/no clock, idle queue/no clock, and pending command/no clock. This is evidence for that conditional guest-side defect, **not evidence that it explains the observed nested virtualization host freeze**. Normal welcome presentation with a working HPET does not itself reproduce the missing-clock condition.

## Splash audit: previous behavior versus final behavior

- The old welcome code allocated a supervisor CPU presentation surface, lazily from `bufor_grafiki_alokuj`. It normally freed it when the final whole-screen presentation removed the overlay, on allocation failure, or on another welcome initialization. Reboot/shutdown previously called no welcome destructor or quiesce function. An interrupted welcome therefore retained its allocation and state until hardware reset or a subsequent initialization.
- This was a missing teardown hook, **not a demonstrated leak of an additional guest buffer through every hardware reboot**. A hardware reboot reloads the kernel image and its static storage. Previous initialization already reset message/timing state and freed an existing surface on a simulated reinitialization. The old wallpaper classification flag was not explicitly reset; it now is. There is no evidence that old timestamps or pointers actually survived a complete hardware boot.
- No splash BWS/compositor layer is registered. There is no splash process, window, GPU resource ID, DMA backing attachment, IRQ, HPET timer registration, callback record, allocated desktop-ready observer, or retained WM layer pointer. `pulpit_pid` and a readiness boolean are values; layer lookup is transient under the screen lock. The splash does not own mouse capture or focus.
- `powitanie_tick()` is called by the existing PID 0 compositor service. It registers no timer and changes no scheduler/HPET frequency. The retained normal scene and asynchronous wallpaper worker remain independent.
- New teardown stops the state machine before releasing its surface. It clears timestamps, fade/message state, desktop readiness, wallpaper classification, PID, final-frame latch, geometry/pitch/DPI cache, next-frame timestamp and drawing cache. Normal completion and allocation failure also release resources and clear session state. Reinitialization always starts at the first message with the new start timestamp.
- `powitanie_reset_ekranu()` releases the previous presentation surface and invalidates readiness when compositor geometry is reset. An unfinished welcome returns to its opaque state and waits for the WM to republish readiness; a completed welcome is not resurrected. A new buffer is reconstructed from current dimensions. The splash never caches the scene/framebuffer address supplied to `powitanie_zrodlo()`.
- Both whole and partial graphics presentation entry points reject work after graphics quiesce. Compositor quiesce empties bounded dirty/cursor/geometry work and rejects further composition/invalidation. Existing layer lifetime and generic first-frame publication gating are preserved.

The lifecycle is `Nieaktywny → Widoczny → Zanikanie → Zakonczony`, with `Wyciszanie → Zniszczony` for power teardown. A whole-frame completion latch prevents cursor-only presentations exposing a partially composed desktop. Minimum visibility remains five seconds, followed by the existing 400 ms fade; pacing remains 33,333 µs. Mode changes during an unfinished welcome invalidate readiness without inheriting old surface geometry.

### Resources and accounting

| Splash-owned resource | Creation | Release / accounting |
|---|---|---|
| CPU RGB surface, `pitch × height` bytes | First visible presentation; reused between frames | Final complete scene presentation, allocation failure, graphics/compositor reinitialization, reboot or shutdown |
| Temporary replacement surface | Only when a larger geometry requires it | Old surface released after successful replacement; failure releases retained surface |
| BWS/compositor layers | None | Zero splash layer registrations |
| Timer/IRQ/callback records | None | Zero registrations; no callback can retain destroyed state |
| Desktop-ready observer allocations | None | Static values reset; late readiness calls are ignored after finish/quiesce |
| Splash GPU/DMA resources | None | Uses ordinary backend CPU copy; no resource ID/queue registration to release |
| Dirty work | Existing bounded compositor queue | Reboot quiesce empties it and gates new work; host production compositor checks this over 100 cycles |

A surface with packed 1280×720 geometry is 3,686,400 bytes; 1920×1080 is 8,294,400 bytes. The graphics pool page-rounds allocations. Its global cumulative allocation counter is not a live allocation count; tests explicitly count live splash surfaces and use LeakSanitizer as well.

The deterministic suite executes **1,200 interrupted startup/teardown cycles** (100 per timing/readiness combination) plus **500 disabled/automated/installer/allocation/clock bypass cycles**, with zero live splash surfaces after every teardown. Coverage includes all three messages, fade, finished state, before/after readiness, pending wallpaper fallback, resolution changes, reset timestamps and backwards inter-generation clocks, idempotent destruction, and attempts to tick/publish/readiness after destruction. A separate production power-order test runs **100 generations** with concurrent host rendering and teardown serialized by a mutex representing the existing screen lock. The real compositor also runs **100 quiesce/reinitialization cycles**, checking that pending queues and presentations do not grow.

### Synchronization and power ordering

The existing external screen lock covers compositor service, BWS graphics operations, surface-source selection and synchronous consumption by the display backend. IDT scheduling already prevents PID 0 from switching while that lock is held, and ordinary Ring 0 syscalls are not preempted into another process. This was audited in `kernel.cpp`, `syscalls.cpp`, `syscall.S`, `scheduler.cpp`, and `idt.cpp`. The new module introduces no spinlock, timer callback, interrupt masking, or GPU wait. These tests verify the existing serialized ownership contract; they do not establish unrestricted future SMP safety for the rest of the compositor.

No splash lock-order inversion was found. Normal presentation retains the existing compositor/graphics locking architecture. For reboot/shutdown the final order is explicit and shared by both paths:

1. Acquire the existing screen lock, finishing any current surface consumer.
2. Stop splash work; release its CPU surface and clear all session state. No layer unregister is needed because no layer exists.
3. Quiesce compositor work, clear pending queues, and gate graphics presentation.
4. **Release the screen lock.** No graphics/compositor lock is held during GPU quiesce.
5. Stop GPU submissions and perform the existing GPU quiesce; no device-status reset was introduced.
6. Flush the existing filesystem cache.
7. Disable interrupts for the existing ACPI/8042 reboot or ACPI shutdown handoff.

The existing PS/2 fallback failure branch could return from `reset_ps2()` after `cli` without restoring the prior IF state. It now saves the prior flags and restores IF on that returning error path. The new splash code never disables interrupts. Successful nonreturning power handoff still deliberately keeps interrupts disabled.

Previously the splash could continue being considered active while only the GPU submission gate had been closed. This ownership gap is now removed; it is not proven to have produced an illegal GPU command or the host freeze. GPU ISR completion does not require the screen lock. GPU DMA/queue destruction remains the existing driver's responsibility, and the splash buffer is not its scanout DMA backing.

### Optional diagnostics

`BURSZTYNDIAGSTARTUPREBOOT=1` enables lifecycle logs at compile time through the Makefile; default is **0**. Logs cover init (with a session generation number), active state, surface allocation/release, desktop readiness, fade, completion, splash/compositor/graphics quiesce, GPU quiesce entry/return, and power handoff. There is no per-frame or per-flush logging. Existing serial/failure messages remain available. The generation counter is per loaded kernel/session and is not a persistent counter across physical resets. The diagnostic-enabled isolated build was also compiled and executed successfully.

## Typography and font availability

The exact font is **extronic16Bunicode**, provided by `czcionki/extronic16Bunicode.h → extronic16B_unicode.h`. Its Unicode glyph and proportional-width tables are compiled into the kernel and available before the first welcome frame, independent of the later PSF/file setup. No temporary legacy text frame or visible font switch is needed.

The audit found that the existing BursztynUI font path is a **16-row monochrome bitmap font**, not a scalable outline font with existing grayscale smoothing. Claiming that it already supplies Windows-like vector typography would be inaccurate. The fix extends the shared `BursztynUI::CzcionkaUI` infrastructure with `zmierz_tekst_wygladzony()` and the reusable `rysuj_tekst_wygladzony()` path. The welcome calls that shared renderer and no longer implements its own bitmap expansion loop. Existing UI font decoding, Unicode glyphs and metrics are reused; existing apps opt into no visual change.

Rendering uses integer bilinear interpolation of glyph coverage and grayscale RGB blending, without FPU, a second font, subpixel RGB rendering or color fringes. It smooths the bitmap's boundaries; it **does not turn the bitmap source into a vector font**. Final visual acceptance of that smoothing remains manual. No font download, raster asset, large logo or new graphic was added to the welcome.

The original line boxes were **32 logical px for the heading and 16 for the message**. Final line boxes are **56 and 36 logical px**, chosen from actual raster bounds: at 1280×720 / 100%, the heading's visible ink is **47 px** tall and the first message **30 px** tall. Tests require visible heading ink in the requested 40–52 logical-pixel band and each message in the 24–32 band, proportionally at 125%. DPI conversion uses existing `ui_skaluj`; enlarged font percentages are measured by the shared font renderer rather than rounded to integer magnification.

Text is centered using measured Unicode advances. Line spacing is 24 logical px (30 physical at 125%). The fixed-width underline was removed. Changing messages only clear/redraw the maximum measured message rectangle, leaving the heading and background intact. Final fade still processes the presented scene region with the existing RGB interpolation formula; larger text introduces no larger full-screen fade buffer or additional GPU flush sequence.

The exact strings remain: `Witamy w Bursztyn OS`, `Polski system operacyjny`, `Niezależny system`. UTF-8 metrics and actual raster pixels are tested for every string.

### Geometry: measured advance / line boxes

| Framebuffer / DPI | Heading x,y,w,h | First message x,y,w,h | Message width range | Message dirty rectangle x,y,w,h |
|---|---|---|---|---|
| 1280×720 / 100% | 512,302,256,56 | 493,382,293,36 | 256–342 | 469,382,342,36 |
| 1280×720 / 125% | 481,288,317,70 | 459,388,361,45 | 317–424 | 428,388,424,45 |
| 1920×1080 / 100% | 832,482,256,56 | 813,562,293,36 | 256–342 | 789,562,342,36 |
| 1920×1080 / 125% | 801,468,317,70 | 779,568,361,45 | 317–424 | 748,568,424,45 |

The 100% first heading raster occupies x=512…764, y=311…357; the first message x=493…783, y=388…417. Line boxes include font whitespace. Layout stays grouped about the center at both resolutions. An actual renderer preview is saved at `docs/startup_welcome_typography_2026-10-06.png` (1280×720, 100%).

## Changed files and preserved behavior

Production changed for this repair:

- `ekran_powitalny.cpp`, `ekran_powitalny.h`, `ekran_powitalny_api.h`: lifecycle, cleanup, readiness invalidation, diagnostics and typography integration.
- `bursztyn_ui/czcionka.h`: shared optional Unicode grayscale renderer and enlarged-font metrics.
- `grafika.cpp`, `grafika.h`: presentation quiesce gate and graphics startup reset hook.
- `skladacz_obrazu.cpp`, `skladacz_obrazu.h`: pending-work quiesce/restart and welcome invalidation on screen reset.
- `syscalls.cpp`: shared reboot/shutdown ordering and IF restoration on a returning PS/2 error.
- `virtio_gpu.cpp`: bounded reboot lock acquisition only; no command-timeout changes or driver redesign.
- New `cykl_wylaczania_grafiki.h`: common, host-testable power ordering.
- New `diagnostyka_startu.h`: compile-time lifecycle diagnostics.
- `Makefile`: dependencies, diagnostic option and new host targets.

The current worktree's earlier welcome changes in `kernel.cpp`, `menedzer_okien.cpp`, `opcje_startowe.h`, and its existing Shell **test** version correction were preserved. No production Shell or Explorer code was modified. No BWS ABI, BSP2/PZB format, desktop persistence, theme policy, scheduler/HPET frequency, VirtIO Input, cursor rendering, GTK/SDL policy, or GPU command/RESOURCE_FLUSH timeout changed. Diagnostic `bursztyn.splash=0`, automated and installer bypasses, desktop-ready gating, asynchronous wallpaper/fallback, serial logging, and generic first-frame publication remain covered. The legacy brown kernel terminal stays hidden; no `guilegacywlacz` call was introduced.

New tests: `narzedzia/test_powitanie_reboot.cpp`, `narzedzia/test_gpu_quiesce_host.cpp`.
Extended tests: `narzedzia/test_powitanie.cpp`, `narzedzia/test_powitanie_policy.py`, `narzedzia/test_boot_graphics.cpp`, `narzedzia/test_compositor_cursor.cpp`.

## Final automated results

Counts below are suite-reported assertions unless explicitly identified as test cases. Popup assertions are included in the compositor total and are not counted twice.

| Suite | Result |
|---|---:|
| Welcome lifecycle, 1700 cycles, typography, UTF-8, bounds, dirty regions, fade and options | 13,706 / 13,706 PASS |
| Concurrent splash / real shared reboot-shutdown teardown order, 100 generations | 601 / 601 PASS |
| Production GPU quiesce: absent clock, lock contention, pending command | 4 / 4 PASS |
| First-frame/startup visibility | 86 / 86 PASS |
| Real kernel graphics presentation, startup reset and quiesce | 36 / 36 PASS |
| Existing startup policy / theme order | 19 / 19 PASS |
| Existing welcome integration policy | 15 / 15 PASS |
| Added reboot/typography integration contracts | 7 / 7 PASS |
| Production compositor, including 100 quiesce/reset cycles | 74,290 / 74,290 PASS |
| Popup restoration subset of compositor | 73,816 / 73,816 PASS (included above) |
| WM routing / later Start-menu Shell launch | 172 / 172 PASS |
| WM decorations, 100% / 125% | 3,353,022 / 3,353,022 PASS |
| Desktop file actions / layout | 6739 / 6739 PASS |
| Desktop settings BWS/PZB | 41 / 41 PASS |
| Desktop selection/theme | 161 / 161 PASS |
| Modern Shell host | 1913 / 1913 PASS |
| BursztynUI, including Unicode system-font and malformed UTF-8 tests | 1379 / 1379 PASS |
| Command bindings | 23 / 23 PASS |
| Theme unit: ordinary and separate ASan/UBSan build | 155 / 155 PASS each |
| Theme GUI integration | 25 / 25 PASS |
| Full VirtIO host suite, including GPU state/reset/resource tests | 144 test cases PASS, 0 FAIL |
| Windows GTK / Linux SDL policy | 7 test cases PASS |
| Diagnostic-enabled isolated splash power build | 601 / 601 PASS |
| `git diff --check` | PASS |

**ASan/UBSan:** final instrumented host suites passed without memory or undefined-behavior reports. LeakSanitizer was explicitly enabled for the complete welcome cycle suite, concurrent power teardown, production compositor, GPU quiesce and separate theme build; these also passed. Existing other targets retain their `detect_leaks=0` setting. Python contract checks are not sanitizer execution. No guest memory-safety result is claimed.

**ThreadSanitizer:** the isolated concurrent splash test compiled, but the runtime failed before test execution with `FATAL: ThreadSanitizer: unexpected memory mapping`. Thus no TSan PASS is claimed and no project dependency was added.

Early test iterations caught outdated small-text geometry expectations, missing host fixture linkage, and a missing partial-presentation quiesce guard; each was corrected before the final runs. The before-fix GPU watchdog timeout is intentional reproduction evidence, not a final suite failure.

Final `make -j2 all iso`: **PASS, exit 0**, after final source changes. No warnings/errors in the release build log. No QEMU, `make rm`, `make clean`, reboot sleep or stress-delay loop was used.

ISO size: 35241984 bytes. SHA-256: `3df119c29a2fee0bc2d4db8a8d2d19fdbb8a42ebc749773ef35f3ab9aca2f247`.

Logs: `/tmp/welcome-gpu-quiesce-before.log`, `/tmp/welcome-reboot-complete-final2.log`, `/tmp/welcome-reboot-leak-final.log`, `/tmp/welcome-reboot-gpu-quiesce-final.log`, `/tmp/welcome-reboot-virtio-final.log`, `/tmp/welcome-reboot-regressions.log`, `/tmp/welcome-reboot-theme-sanitized.log`, `/tmp/welcome-reboot-launcher.log`, `/tmp/welcome-reboot-diag.log`, `/tmp/welcome-reboot-tsan.log`, `/tmp/welcome-reboot-build-release.log`.

## Manual acceptance remaining

Boot the final ISO; confirm larger smoothed system-font text, all three Polish messages, and normal desktop publication. Repeat at 1280×720 and 1920×1080 with 100%/125% as available. Manually reboot several times, confirming the splash always starts at the first message, there is no retained visual state, and the guest, QEMU, development Linux and outer VirtualBox host remain responsive. Confirm modern Shell launch and existing desktop behavior afterwards, plus a clean `bursztyn.splash=0` boot. Collect transition diagnostics if a failure recurs.

**Runtime PASS and a fix for the reported Linux-host freeze are not claimed.**
