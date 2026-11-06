# System Settings, window coverage and application identity

## System page

Navigation is now **System, Wygląd, Czcionki, Wsparcie**. System is visible in
the initial layout and selected by Settings initialization. Appearance retains
its link to the native theme application; Fonts and Support retain their
existing controls, live Apply, reload and bounded data models.

`system_identity.h` contains a read-only property model shared by Settings and
the shell `system` command. It supplies name, version, author Dymitr Wygowski,
contact programista.art@gmail.com, x86-64 architecture, monolithic kernel,
four-level paging, Ring 3/PZB/BZL, private address spaces, PSF/BSP2, BursztynUI,
the layer compositor, hardware-dependent E1000/VirtIO-net drivers and
DHCP/ARP/ICMP/DNS/TCP/HTTP. These are build capabilities, not live claims that
both NIC drivers are active. Relevant implementations are `vmm.cpp`, `tss.cpp`,
`loader.cpp`, `scheduler.cpp`, `pzb.h`, `psf.h`, `storage_format.cpp`,
`network_device.cpp`, `e1000.cpp`, `virtio_net.cpp`, `siec.cpp` and
`skladacz_obrazu.cpp`. HTTPS/TLS is not presented as a generic kernel feature.

The existing `wersja_systemu.h` remains the canonical version definition.
Audit found numeric revision **6** while the display string said **1.0.1.9**.
Revision is now 9 and the display string is generated from the four numeric
components. The existing three-component updater version ABI is unchanged;
the fourth component remains its documented presentation revision. Kernel boot
banner, shell, Settings, diagnostics and updater use the same header. The
updater's stale `0.1.0` fallback was replaced with the canonical current version.
Author/contact constants are shared rather than duplicated in active banners.
Per-application manifest versions and BWS/filesystem ABI versions remain
independent of the operating-system release version.

`SystemPage` uses standard BursztynUI `WidokPrzewijany`, `Panel` and `Etykieta`
controls. There are six sections with measured property/value columns and a
larger page title. Narrow content stacks properties above values. Long values
wrap at measured UTF-8/BSF boundaries. The existing label wrapping option had
never been implemented by `Etykieta`; measurement and drawing now use the same
bounded line iterator. Other labels retain their previous single-line behavior
unless wrapping is enabled. System content scrolls independently; the existing
outer Settings viewport still handles windows below the logical minimum.

No new privileged system-info API was introduced. All System page values are
static, non-sensitive build information. No CPU/RAM/boot-mode values are
invented, and no kernel structures or pointers are exposed to Ring 3.

## Rounded window corners: exact cause

Three rendering rules disagreed:

1. `wm_rysuj_dekoracje` rounded only the titlebar, but its bottom frame and side
   strips were rectangular. Compositor-owned chrome was drawn after client
   composition and could refill transparent corner pixels.
2. `Okno::rysuj` separately cleared the client's bottom corner strips using
   BWS rectangle operations only after full paints. Partial client paints could
   refill them. This made coverage depend on repaint history.
3. Both compositor paths skipped the complete titlebar band but applied no
   rounded mask to the remaining client. Stored opaque client pixels could
   therefore escape the intended outer shape. Theme damage covered only a
   one-pixel bottom strip, insufficient for changing corner coverage.

`window_shape.h` now defines one `WindowShape` from surface dimensions, theme
radius, UI scale and maximized state. Full-layer composition, dirty-region
composition, frame drawing, title-mask clipping and production input hit testing
use this coverage. Bottom frame strokes follow the same rounded boundary.
Settings contains no corner-specific rendering. BursztynUI's per-frame corner
erase syscalls were removed.

Outside the shape, no window pixel is composed: wallpaper or the actual lower
window remains visible. Inside, the existing native RGB/visible-black marker
semantics are preserved. Coverage is binary, using the existing integer-circle
theme policy; this does not reinterpret application surfaces as unrestricted
RGBA. BSF title masks retain their normal alpha blending, clipped by coverage.
Normal windows use scaled theme radius, bounded by half dimensions and a
16-logical-pixel control-safe maximum. This keeps existing close/minimize/
maximize buttons completely inside coverage even for custom themes asking for
32-pixel radii. Stock themes keep their existing 8/10-pixel radii.

Maximized windows use radius zero, including screen/work-area corners. Geometry
changes recompute coverage without a bitmap-mask allocation or mask cache.
Move/resize damage continues to restore both old and new bounds. Theme changes
damage full top/bottom corner bands, including a previously larger radius.
Popup surfaces keep their existing independent corner/shadow policy: dropdowns,
Start/context menus and file dialogs are not given normal-window masks.
There is no separate normal-window shadow surface in the audited compositor.

## Settings launch conflict: exact cause

The two executables already had distinct loader keys:

* Settings: `/programy/ustawienia.cebula/ustawienia.bur`.
* Display Settings: `/programy/ekran.cebula/ekran.bur`.

The loader atomically checks exact bounded executable paths when reserving a
process slot. BUR headers, process names, address spaces and Display Settings'
IPC endpoint were not colliding. Display Settings does not establish a global
secure modal or own the desktop. Its confirmation UI is local to its window.

The failure occurred earlier: WM `aktywuj_istniejace` used
`tekst_zaczyna_sie(window.tytul, requested_title)`. Consequently requesting
`Ustawienia` found `Ustawienia ekranu`, focused it and never invoked BWS10 for
the actual Settings executable. Explorer's protected desktop shortcuts had the
same title-prefix policy and have been corrected too.

`AppIdentity` documents the loader's existing executable-path identity scheme.
Activation now asks for a snapshot filtered by the exact executable path and
activates the returned generation-qualified window ID. Localized titles remain
display text. No singleton check was disabled. A second launch request focuses
only that executable's window; a request arriving before window registration is
still protected by the loader's atomic exact-path reservation.

BWS42 gained an optional third argument: a bounded, validated user path filter.
Zero retains the previous snapshot behavior and `GuiOknoInfo` layout. Existing
snapshot authorization and bounded copy-out remain in force. Filtering occurs
before applying the caller's output capacity, so a small output buffer can find
an app later in the window table. No process paths or kernel pointers are added
to the output. BWS10, BWS43 and the process/window ABIs are unchanged.

Taskbar and Alt+Tab already enumerate generation-qualified windows/PIDs; they
keep separate entries and focus targets for the two programs. No mutable state
is shared between their Ring 3 address spaces.

Direct shell launch is outside the existing supported authorization boundary:
the shell is PZB 4, Settings' installed manifest requests PZB 3 and Display
Settings requests PZB 3 plus system configuration rights. BWS10 rejects a PZB 4 parent
creating these more-trusted programs before invoking the loader. A native shell
attempt with Display Settings running produced the expected PZB denial, without
a fault or any effect on Display Settings. No privileges or singleton guards
were weakened to make that attempt succeed. Desktop and Start remain the
supported launch sources for these system apps. The conditional shell-launch
acceptance requirement is reported as unsupported, rather than as a successful
shell launch. This restriction is independent of the other Settings app running.

## Memory and stack

On the x86-64 host ABI, `SystemPage` is 2,408 bytes, `Etykieta` 2,048 bytes,
`Panel` 2,056 bytes and a scrollbar 2,176 bytes. The page creates 31 labels, two
panels and two scrollbars: approximately 73 KiB including the page object,
excluding allocator/child-vector overhead. There is no file buffer or dynamic
hardware-query cache for System info. Existing Support data storage is unchanged.

`WindowShape` is five integers (20 bytes) and allocates no heap surface/mask.
Release `.su` records: System page layout 128 bytes; label wrapping 368 bytes
per iterator frame with a fixed 256-byte scratch line; exact WM activation
1,552 bytes; BWS42 snapshot/filter 2,144 bytes. These are individual function
frames, not cumulative worst-case call-chain measurements. No large content
array or arbitrary-sized input is placed on a Ring 3/Ring 0 stack.

## Verification

Host coverage includes System default/navigation and shared identity, measured
layout at 12/16/20/24 px and 100/125/150/200% scale, existing font and Support
layouts, parser edge cases, virtual scrolling, desktop grid and Start search.
Actual BWS42 tests cover the unchanged legacy call, exact paths, prefix/suffix
rejection, filter-before-capacity and authorization. WM tests verify each app's
independent singleton activation even when its title changes.

Deterministic compositor tests deliberately fill the whole rectangular client
surface with opaque magenta, then compare outside-corner pixels with bright,
dark and colored lower surfaces. They exercise full/partial composition,
repeated geometry changes, minimize/restore, maximize/restore, scaled radii,
theme-radius changes and existing popup background restoration. Decoration tests
also check that large custom radii cannot clip window controls.

QEMU acceptance ran overnight on 2026-10-27/28, using the production artifacts
recorded below.

### Host results

| Suite | Result |
| --- | --- |
| Desktop TTF / Settings / Start, including System and Support | 129,875 checks PASS |
| Production compositor lifecycle/scene, including rounded windows | 8,677,783 checks PASS |
| Popup background restoration | 73,816 checks PASS |
| WM decorations, including large-radius control coverage | 3,356,387 checks PASS |
| WM routing / executable activation | 222 checks PASS |
| Actual BWS authorization, PSF and exact-window filtering | 209 checks PASS |
| Shell terminal / shared system identity | 1,921 checks PASS |
| BSF | 12,201 checks PASS |
| BSF UI/browser | 5,876 checks PASS |
| Explorer / shared desktop behavior | 15,546 checks PASS |
| Desktop production file operations / capacity | 86,673 checks PASS |
| User fault isolation policy | 32,768 checks PASS |
| BursztynUI commands | 23 tests PASS |
| Theme subsystem | 155 tests PASS |
| BUR loader and header checks | 35 + 17 tests PASS |
| Updater GUI integration | 35 tests PASS |
| Husarz GUI/header integration | 18 tests PASS |
| Notepad / Calculator GUI integration | 14 / 14 tests PASS |

Relevant C++ host suites ran with ASan/UBSan and reported no sanitizer findings.
Theme unit tests and Python source/binary integration checks use their existing
non-sanitized runners. Two old Python regression assumptions were corrected:
Start registration now lives in `start_menu_model.h`/`desktop_entries.h`, and
the existing Husarz text segment is 528 KiB, not the obsolete 512 KiB assertion.
No browser segment size or launcher registration was changed to satisfy those
checks. `verify_system_identity.py` additionally checks the compiled OS version
in kernel, shell, Settings and diagnostics, canonical updater fallback, and the
exact Settings payload staged in the ISO tree.

### Production artifacts

* Kernel SHA-256: `f3d5d5eb16d4199564e52402e92adae3d9e15e5397251ee6969d813101c65aa2`.
* ISO SHA-256: `b60f517a434223ef76721e542a31e00dfd61991b11f66a7b2ad8ce40f126b45a`.

The core ordinary-release QEMU acceptance passed on these hashes. Display
Settings ran as PID 2 while Settings ran as PID 3; both window records were
visible simultaneously. Repeated requests focused only the corresponding
executable without adding a process. Taskbar switching and native Alt+Tab
selected the two distinct PIDs. Desktop launch also passed with Display Settings
still running.

Pixel comparison checked 84 outside-corner coordinates across all four corners
for each normal-window geometry: initial `(100,100,950,540)`, moved
`(140,130,950,540)` and restored after maximize at `(140,130,950,540)`.
Both the original and minimize/restore frame equaled the captured lower scene
at every checked coordinate. A complete maximized frame covered both top screen
edges with its square accent strip. Evidence:
`.bsf-test/system-windows-core-qemu/result.json`, `concurrent-processes.json`,
`corner-pixels.json`, `settings-corners-*.png` and
`settings-maximized-square.png`.

The input driver waits for completed window publication and the native app's
event-wait state before inspecting a changed page. Font and scale values are
selected through actual popup rows and checked against read-only kernel config
observers. This avoids treating a button's pressed state or a queued earlier
frame as confirmation of the newly selected page. The helper
`qemu_process_layout.cpp` derives the observer offsets from the actual process
structure; no guest memory is written by the observers.

The final UI acceptance also passed on the same kernel and ISO hashes:
System-page screenshots at 12/16/24 px, display scale 100→125→100%, both
application launch orders and both close orders. After changing scale, all 84
sampled outside-corner pixels again matched the lower scene. Both applications
remained usable and no guest fault was recorded. Evidence:
`.bsf-test/system-windows-ui-accepted-qemu/result.json`,
`concurrent-processes.json`, `corner-pixels.json`, `settings-system-*.png` and
`both-settings-scale-*.png`. This run covers the late UI phase; singleton,
Alt+Tab and desktop launch results are recorded by the separate core run above.
The saved late-run result's older `PZB 3/2` restriction wording predates the
manifest correction in this report: both installed apps request PZB 3, with
additional configuration rights for Display Settings. Shell launch remains
unsupported for the PZB 4 parent.

On resuming the session on 2026-10-28, the saved core and final UI results were
confirmed as PASS, the current kernel/ISO SHA-256 values matched the recorded
artifacts, and `git diff --check` completed without errors.

The clock/Support acceptance on these exact artifacts passed: 100 consecutive
sampled taskbar frames, 14 distinct time images, no blank clock, stable neighbor
pixels; all three data categories, Refresh, live 16→24 px Apply, another Refresh
and Settings reopen passed without a guest fault. Evidence:
`.bsf-test/system-windows-production-clock-qemu/result.json`,
`clock-frames.json`, `clock-frames/` and `support-*.png`.

The separate ordinary-release native-app regression passed on the same hashes:
Husarz loaded the local font fixture with antialiased TTF text; Notepad,
Calculator and Explorer opened and repainted across live 16→24 px Apply.
Local font preview did not alter persisted configuration before Apply; the
Start menu also repainted at 24 px. No guest fault was recorded. Evidence:
`.bsf-test/system-windows-native-apps-qemu/result.json` and `*-font-*.png`.

### Scope and measurement limits

System info intentionally shows static build capabilities. Physical-hardware
runtime identification is not added. Window coverage follows the existing
binary rounded-pixel policy; this change does not add a new shadow engine.
The current shared `Okno` offers move and maximize/restore, without pointer-edge
resize handles. General resize coverage is exercised through compositor
geometry transactions and native maximize/display-scale changes.
The full font/scale matrix is verified on the host; native QEMU System-page
acceptance targets 12/16/24 px and display scale 100↔125%. QEMU is emulated
hardware, not a physical-machine acceptance result. Four-component display
versions do not change the updater's existing three-component comparison ABI.
Direct shell launch of these trusted Settings executables remains restricted by
the existing PZB policy described above.
