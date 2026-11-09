# Resolution edges and Shell idle audit — 2026-10-04

## Outcome and limits

The user confirmed that cursor flicker is gone. No QEMU session was launched in
this task. The exact cause of the remaining real top-edge restriction and periodic
Shell-dependent stutter is **not established**. The audited guest mapping and host
regressions reach every edge; changing working bounds or guessing a new input
configuration would not be supported by that evidence.

A concrete routing defect was corrected: uncaptured pointer motion previously
went to the keyboard-focused window even outside its bounds. Motion now follows
the hit window, with one departure event to clear the previous owner's hover.
Capture retains ownership for selection, scrollbar and window dragging; button
edges, keyboard focus, modal popup ownership and launch routing retain their
existing behavior. This reduces irrelevant Shell event traffic. It is not claimed
as a proven cure for the reported periodic runtime freezes.

Default-off aggregate instrumentation and a separate diagnostic ISO are provided
for the next manual run. The ordinary ISO and build objects have been restored to
default-off diagnostics.

## Current QEMU devices and the inconsistent runtime log

`QEMU_VIRTIO_COMMON_ARGS` in the current Makefile contains:

```
-device virtio-vga,disable-legacy=on,xres=1280,yres=720
-device virtio-keyboard-pci
-device virtio-tablet-pci
```

Its production BIOS/UEFI/live/installed profiles use this common device set;
`QEMUDISPLAYARGS` defaults to `-display sdl`. No device or display configuration
was changed in this task. There is no Makefile reversion to relative mouse.
Commit `8ce21e3` (2026-10-01, "Poprawiony program shell i dodany sdl") replaced
`virtio-mouse-pci` with `virtio-tablet-pci` in Makefile.

However, `rununix_virtio_latest.sh::build_virtio_args` still sets
`mouse_dev="virtio-mouse-pci"`, and its environment validator still requires that
device. Its device list was present before the Makefile tablet change (the latest
relevant script commit is `6591d2d`, 2026-09-27). `run_xdotool.sh` also explicitly
launches `virtio-mouse-pci`; that script is a separate automated launcher.
These launchers were not kept in sync with the Makefile device change. Neither
was modified or executed here.

The exact command used for the supplied runtime log remains unconfirmed; the
user was asked which launcher produced it. `QEMU Virtio Mouse` is read from
VirtIO configuration `ID_NAME`, not invented from Bursztyn's role classification.
It therefore matches the separate relative-mouse launchers. That is an evidence-
based explanation of the inconsistency, not proof of the actual launch command.

Classification queries KEY, REL and ABS capability bitmaps separately. REL_X/Y
set `has_rel_mouse`; ABS_X/Y set `has_abs_tablet` and query ABS_INFO ranges.
There is also a buttons-only fallback to mouse, so a role label alone does not
prove the exact REL bitmap. The supplied log shows relative classification, but
contains insufficient raw capability data to report the actual bitmap or ABS
ranges. Existing mocked relative/absolute classification tests pass. With
`BURSZTYNDIAGGUIRUNTIME=1`, new one-time `[INPUT CAPS]` output records queried
REL/ABS sizes, first bitmap bytes, axis flags and min/max ranges, without changing
capability parsing or input behavior.

## Coordinate and resolution pipeline

- Physical scanout: `grafika_zmien_sprzet`/VirtIO-GPU modeset apply the new
  resource and SETSCANOUT. Existing GPU host tests cover 1280x720 -> 1920x1080,
  confirmation, stale resource disposal and rollback.
- Graphics geometry: `grafika_wymien_stan` updates active driver width/height,
  pitch, `aktywny_stan`, scene backbuffer, wallpaper and screen generation.
  Existing logical pointer position is clamped only to 0..width-1/height-1;
  overlay visibility is invalidated. Its old geometry is not used for movement.
- Central desktop geometry: `bws_aktualizuj_ekran` and
  `skladacz_zastosuj_ekran` apply new scale/work area/layers.
  `skladacz_reset_ekranu` clears pending old geometry and queues a new full-screen
  dirty rectangle using the current graphics dimensions.
- WM: its EKRAN handler reads current `GuiMonitorInfo` rather than trusting stale
  event dimensions, updates `screen_w/h`, work area, UI metrics, root desktop
  layout and overlay/menu geometry. Its reserved taskbar/work area restricts
  windows, not pointer coordinates.
- Shell: its EKRAN handler refreshes physical screen and monitor/work dimensions,
  then updates its own window geometry. Shell's logical width/height and client
  titlebar offset are app layout values, not global cursor bounds.
- Absolute input: `input_dispatch_event` stores raw ABS axes until SYN_REPORT,
  chooses the pointer owner, reads the current physical graphics width/height,
  and maps device min/max to exactly 0..width-1 and 0..height-1 using int64 math.
  There is no cached 1280x720 transform, work-area subtraction or DPI conversion.
- Relative input: REL_X/Y deltas update the logical position with consistent Y
  sign conversion; `zaktualizuj_mysze_zrodlo` reads fresh screen dimensions and
  clamps only to zero and the last pixel. The absolute setter converts target
  coordinates to deltas through this same path.
- Hotspot and rasterization: logical coordinates are independent of shape and
  DPI. `parametry_kursora` supplies bitmap size/hotspot; rasterization subtracts
  the hotspot and clips the resulting bitmap to the screen. It never moves the
  logical cursor inward to keep the bitmap visible. Arrow hotspot is (0,0);
  other shapes and custom cursors can have nonzero hotspots.
- Presentation: the prior clean-scene transaction, clipped union, latest-position
  mailbox and single final cursor present remain in use. Mode-change reset
  discards pending old scene/cursor state.

No audited guest input/cursor/compositor clamp still uses 1280x720 after the
modeset. Cached `mouse_last_max_x/y` values control bounds logging only. Window
minimum Y, titlebar dimensions, taskbar and work-area margins do not clamp the
logical pointer. This is a code-audit conclusion; the actual runtime host
viewport, SDL scaling/grab behavior, launched device and observed coordinates
remain to be checked. A bounds log alone does not establish correct visual
mapping.

The diagnostic ISO emits `[CURSOR GEOM]` approximately once per second with
physical, driver, GPU and desktop layer sizes, logical position, zero minima,
last-pixel maxima, hotspot, bitmap dimensions, scale and generation. Kernel
compositor clipping reads that same current physical geometry. The WM event
handler was audited; its private cached geometry is not inferred from this log.

## Shell idle, redraw and scheduling

`pobierz_znak_blokujaco` calls `gui_czekaj_na_zdarzenie`, which uses BWS38 with
zero timeout. The syscall marks an empty queue blocked and waits using its
existing HLT/scheduler mechanism. There is no Ring 3 mouse/keyboard polling loop,
no newly created Shell timer and no sleep workaround.

The text caret is currently static. It is drawn as part of input-row or full-
client painting; there is **no caret blink timer**, no 250 Hz caret toggle and no
caret-only idle repaint to optimize. No blink was introduced by this change.
Shell ignores TIMER and unused precise-pointer events after normal dispatch.
When no selection/window/scrollbar drag or popup is active, ordinary mouse motion
also performs no terminal repaint.

The shared kernel GUI timer broadcasts at 1 Hz with HPET. The existing no-HPET
fallback fires every 100 scheduler ticks (2.5 Hz at 250 Hz). It can wake an idle
Shell, but Shell does no paint for that event. That timer behavior and scheduler
frequency were not changed. Actual Shell wakeups/s are not inferred from a timer
constant because event queues coalesce and other events can wake the process.

Before the routing change, an uncaptured motion sent normal MOVE plus precise
pointer events to focused Shell, even over the desktop or another window. After
it, irrelevant motion is directed to the hit window. A single departure MOVE
clears old hover; further motion there does not wake Shell. Capture and popup
owner overrides preserve existing interactions. Raw events, consumed events and
actual blocked->ready transitions are counted separately.

Screen serialization is shared by app drawing and compositor/pointer rendering.
Normal syscalls are not preempted mid-call; expensive app buffer copies can delay
PID 0 input polling/composition. VirtIO-GPU presentation has synchronous transfer
and flush commands. These are possible contention/latency mechanisms, not measured
causes of the reported stutter. Necessary locks, scheduler and HPET were retained.

## Diagnostic measurements

`BURSZTYNDIAGGUIRUNTIME` defaults to 0 in Makefile and headers. When enabled:

- `[GUI PERF]` records approximately one-second intervals per PID/path, accounted
  scheduled CPU nanoseconds excluding explicitly blocked intervals, genuine
  event wake transitions, internal wait attempts, GUI/event/clipboard BWS calls,
  MOVE/TIMER/precise enqueue requests, scene dirty requests, present/cursor/copy
  counts, composition time, actual contended screen-lock wait time and maximum,
  framebuffer copy time, and VirtIO transfer+flush time and maximum.
- `[SHELL PERF]` records wait calls, consumed event/MOVE/precise/TIMER counts,
  full-client paints, input-row paints, caret drawing calls, scrollbar drawing
  calls and publication calls. Reports are triggered by the existing timer;
  clock reads and one diagnostic log syscall do not occur per mouse event.
- Diagnostic Shell reports use existing BWS1 and go to serial only in diagnostic
  builds; they do not repaint the kernel terminal or contaminate GPU counters.
- `[INPUT CAPS]` and `[CURSOR GEOM]` expose the coordinate/configuration evidence
  described above. Existing `BURSZTYNDIAGCURSORPERF` anomaly timestamps remain
  available in the diagnostic ISO to measure dequeue -> queue -> compose ->
  cursor rasterization -> present latency.

Intervals are logged explicitly; divide counts by interval_ns / 1e9 for rates.
Kernel requested event counts can exceed consumed events because queues coalesce.
CPU accounting measures scheduled elapsed time, including executing syscalls,
not hardware instruction/cycle utilization; PID 0 residency can include idle.
The present timer includes backend work; `gpu_ns` isolates synchronous VirtIO
transfer+flush and `copy_ns` isolates the framebuffer copy. The internal GPU queue
lock itself was not separately instrumented or changed. Aggregate serial output
has bounded frequency but still introduces diagnostic overhead.

No real before/after Shell CPU/wakeup/redraw/GPU rates, lock waits or input/GPU
latency were obtained. Thus caret/full repaint, lock contention, command latency
and input delay cannot be assigned as real runtime stutter causes from this task.

## Automated tests and results

- Extended `test_shell_terminal.cpp`: production event loop consumes 250 synthetic
  timer events plus 100 outside mouse moves at each scale with **0 client paints,
  0 refresh calls and 0 popup paints**. Window geometry remains unchanged.
  Existing UTF-8, editor, clipboard, selection, navigation, scrollback, live theme,
  popup, support resource and version checks remain PASS.
- Extended `test_wm_click_routing.cpp`: focused Shell no longer owns uncaptured
  outside motion; capture/button semantics remain. Exactly one departure event,
  then zero additional departures for 100 same-target moves. Existing desktop,
  Start, launch, transparency, chrome and popup-owner checks remain PASS.
- Extended `test_cursor_host.cpp`: bitmap hotspots at all four physical edges
  through 1280x720 -> 1920x1080 at 100% and 125%, with unchanged logical coordinates
  and clipped raster bounds. Existing 100-position coalescing, clean background,
  old/new union and one-final-present cases remain PASS.
- Extended `test_virtio_pci.cpp`: the same initialized tablet traverses every edge
  before and after modeset with no re-registration/reset and no DPI margin, using
  production VirtIO event reception and generic input dispatch. Existing relative/
  absolute discovery and GPU modeset tests also pass. **143/143 cases**, ASan/UBSan
  and leak detection enabled.
- New `test_compositor_cursor.cpp`: production compositor with mocked graphics
  backend and a host-only IRQ shim tests fresh full-screen modeset dirty geometry,
  a Shell-equivalent titled layer preserving cursor/physical geometry/generation,
  rapid latest-position coalescing, pending decoration/theme/full-scene work,
  one final full-scene present and cursor bypass of the 60 Hz scene deadline.
  **PASS with ASan/UBSan and leak detection enabled**.
- New `test_gui_runtime_diag.cpp`: mocked clock verifies blocked wait is excluded
  from CPU accounting, max/cumulative counters, PID/path formatting, interval
  aggregation and counter reset. **PASS with ASan/UBSan**.
- Existing WM decoration renderer and cursor hover sanitizer checks: **PASS**.
- Existing theme tests: **155/155 PASS**.
- Existing UI controls: **1379/1379 PASS**, ASan/UBSan.
- Existing command bindings: **23/23 PASS**, ASan/UBSan.
- Affected default component builds and all six diagnostic-enabled translation
  units: **PASS**.
- `make -j2 all iso`: **PASS**. Diagnostic ISO build: **PASS**. Default-off object,
  kernel/Shell and ISO restoration build: **PASS**.
- `git diff --check`: **PASS**.

Host Shell/routing/core/UI harnesses retain their existing detect_leaks=0 setting;
VirtIO and new compositor harnesses use detect_leaks=1. No runtime PASS is claimed.

## Artifacts and next manual run

- `BursztynOS.iso`: ordinary build, both diagnostics default off.
- `BursztynOS-runtime-diag.iso`: same device-independent OS build with
  BURSZTYNDIAGGUIRUNTIME=1 and BURSZTYNDIAGCURSORPERF=1.

The diagnostic ISO does not configure QEMU devices or SDL. Use the existing
launcher and collect serial output; record its exact command line first. Compare
stable desktop idle with Shell closed/open, then inspect boundaries before and
after the resolution change. The requested real rate and latency comparisons can
then be made from the explicit interval counters. Final visual verification still
requires the real cursor reaching all four screen edges and staying smooth with
Shell open. No automatic runtime session was performed.

To regenerate the diagnostic ISO, force the flag-bearing objects (make does not
track flag changes automatically), then link/build the image:

```
make -B -j2 BURSZTYNDIAGGUIRUNTIME=1 BURSZTYNDIAGCURSORPERF=1 grafika.o skladacz_obrazu.o scheduler.o syscalls.o shell_tmp.o virtio_input.o
make -j2 BURSZTYNDIAGGUIRUNTIME=1 BURSZTYNDIAGCURSORPERF=1 all iso
```

Force those objects with both flags set to 0 and run `make -j2 all iso` to restore
the ordinary build. This restoration was already performed in this task.

## Files changed in this follow-up

`Makefile`, `grafika.cpp`, `skladacz_obrazu.cpp`, `scheduler.cpp`, `syscalls.cpp`,
`virtio_input.cpp` (diagnostics only), `wm_input.h`, `shell.cpp` and
`shell_terminal.h` (diagnostics only); new `gui_runtime_diag.h` and
`shell_runtime_diag.h`; extensions to `narzedzia/test_shell_terminal.cpp`,
`narzedzia/test_wm_click_routing.cpp`, `narzedzia/test_virtio_pci.cpp` and
`narzedzia/test_cursor_host.cpp`; new `narzedzia/test_gui_runtime_diag.cpp` and
`narzedzia/test_compositor_cursor.cpp`; this report and the diagnostic ISO.
Prior uncommitted cursor changes are preserved. Generated tracked test binaries
are restored after testing. SDL/device launch profiles, context-menu behavior,
reboot, network, BSP2, GRUB, installer, HPET/scheduler frequency and BUR ABI were
not modified.
