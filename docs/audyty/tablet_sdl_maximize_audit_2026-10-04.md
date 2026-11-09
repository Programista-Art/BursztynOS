# SDL maximize / absolute tablet audit — 2026-10-04

## Result and evidence limit

No QEMU VM was started. The supplied normal -> maximized -> restored observation
is accepted, but it contains no raw ABS values. It does not yet establish whether
QEMU sends a reduced range or the guest mishandles a complete range. The exact
runtime root cause, endpoint coverage when maximized, and geometry-generation
changes therefore remain pending one user-controlled transition test.

No guest coordinate correction was made. Both axes were already mapped
independently with signed 64-bit multiplication. No square normalization,
bitmap-size subtraction, work-area margin, host-window dependency or guest DPI
transform was found in the tablet coordinate path.

## Production pipeline

1. `virtio_input_query_config` selects EV_BITS with subselect EV_ABS; ABS_X and
   ABS_Y bits are tested separately by `virtio_input_classify_device`.
2. Separate ABS_INFO requests for X and Y populate `input_dev.abs_x` and
   `input_dev.abs_y` min/max/fuzz/flat/res. The usual QEMU declaration is 0..32767;
   successful capability queries populate each axis independently.
3. `virtio_input_poll_events` copies each used event buffer to `InputEvent`, then
   calls `input_dispatch_event`. EV_ABS updates only its own `abs_val_x/y` and
   `has_abs_x/y`. Repeated values before SYN_REPORT intentionally use the latest.
4. SYN_REPORT checks pointer ownership, reads fresh physical dimensions from
   `grafika_pobierz_szerokosc/wysokosc`, clamps raw values to their own declared
   ranges and maps `(int64(raw)-min)*(extent-1)/(int64(max)-min)` separately.
   An axis absent from the report preserves its current logical coordinate.
5. `zaktualizuj_mysze_pozycja_zrodlo` commits through the shared pointer update.
   The internal relative Y sign is reversed consistently by the absolute setter;
   it is not a second scaling operation. Final clamps are exactly 0..width-1 and
   0..height-1. Bounds logs describe this final clamp, not incoming raw coverage.
6. WM hit testing/routing uses physical global coordinates. Local app coordinates
   are derived afterward. UI scale changes control layout, not global coordinates.
   The previous outside-focused-Shell motion correction/capture rules remain.
7. `skladacz_obrazu_oznacz_ruch_kursora` keeps a bounded latest-position mailbox.
   Composition consumes the newest sample; `grafika_prezentuj_scene_z_kursorem`
   clips the bitmap after subtracting its hotspot. The logical position is never
   moved inward to preserve the complete image. The previous clean-background,
   old/new union and one-final-present transaction remains unchanged.

Searches of this path found no shared axis scale, min/max(width,height), aspect
ratio normalization, letterbox/pillarbox/titlebar/SDL offset or host viewport
input. Guest graphics dimensions are the sole destination geometry.

## Host frontend and DPI investigation

The installed Linux binary is QEMU 8.2.2 (Ubuntu package
`1:8.2.2+ds-0ubuntu1.18`); `-display help` lists SDL. Device property help confirms
both virtio-tablet-pci and virtio-keyboard-pci are supported. These help queries do
not boot a VM. Windows QEMU/SDL versions and Windows scaling were requested and
have not been supplied. No new display option or SDL environment hint was added.

Official SDL2 documentation says `SDL_RenderSetLogicalSize` transforms mouse
positions into renderer logical coordinates and handles differing aspect ratios:
https://wiki.libsdl.org/SDL2/SDL_RenderSetLogicalSize
Window coordinates, drawable pixels and scanout/renderer logical dimensions are
separate spaces. Windows DPI scaling can change their relationship:
https://wiki.libsdl.org/SDL2/SDL_HINT_WINDOWS_DPI_SCALING
https://wiki.libsdl.org/SDL2/SDL_GetWindowSizeInPixels

The QEMU 8.2.2 2D renderer calls SDL_RenderSetLogicalSize with guest surface width
and height. Its mouse handler forwards event coordinates directly, normalizing
against the surface dimensions:
https://github.com/qemu/qemu/blob/v8.2.2/ui/sdl2-2d.c
https://github.com/qemu/qemu/blob/v8.2.2/ui/sdl2.c

Upstream commit `30aa105640b0` ("ui/sdl: Consider scaling in mouse event handling")
adds `event.x * surface_width / SDL_GetWindowSize.width` and the corresponding Y
conversion, to both motion and buttons. This remains in the inspected upstream
master, whose 2D renderer also still sets SDL logical size:
https://github.com/qemu/qemu/commit/30aa105640b0
https://github.com/qemu/qemu/blob/master/ui/sdl2.c
https://github.com/qemu/qemu/blob/master/ui/sdl2-2d.c

**Inference, not a confirmed runtime cause:** on a build where SDL already
converts events into guest logical coordinates, this additional conversion can
scale them twice. For example, a logical event near (1279,719), divided again by
a 1920x1080 host client size, becomes approximately (852,479). Normal size has a
factor of one; maximizing restricts the range; restoring removes the restriction.
That matches the observation without any guest geometry change. OpenGL uses a
different frontend path; the exact Windows version/backend must be established
before prescribing an upstream patch or changing frontend settings. Neither a
QEMU patch nor a guest compensation factor has been applied on this inference.

Host resize in QEMU calls `dpy_set_ui_info`; that is distinct from the guest
issuing SET_SCANOUT. This guest GPU implementation queries GET_DISPLAY_INFO at
initialization, negotiates VERSION_1 only (not EDID), and has no display-config
change consumer that automatically modesets on host maximize. Expected guest
geometry remains unchanged. Actual gen/WM event behavior still needs the log;
no GPU, EDID, compositor geometry or scheduler implementation was changed.

## Launcher audit and corrections

- Makefile runuefi already uses `-display sdl`, virtio-tablet-pci and
  virtio-keyboard-pci. Those production arguments remain unchanged.
- The checked-in `runwindows.ps1` previously defaulted DisplayBackend to empty
  (QEMU auto selection) and used virtio-mouse-pci in its VirtIO Full profile.
  It now defaults to SDL and uses virtio-tablet-pci, retaining explicit overrides.
- `rununix_virtio_latest.sh` previously auto-selected display and required/launched
  virtio-mouse-pci. Its VirtIO profile now defaults to SDL and requires/launches
  virtio-tablet-pci. User-supplied QEMU_DISPLAY_ARGS still take precedence.
- Windows uses `tcg,thread=single`, dsound and an explicit keyboard modern flag;
  Makefile uses `tcg,thread=multi`, ALSA and the keyboard's device default. No
  launcher defines a Windows DPI-awareness/scaling hint, viewport correction or
  letterbox option. These unrelated accelerator/audio settings were retained.
- Legacy VGA/USB profiles and `run_xdotool.sh` are unchanged. Launcher consistency
  does not explain or fix the reported case with an already-correct tablet.

## Default-off diagnostics

Build flag: `BURSZTYNDIAGABSMAP=1` (default 0).

- `[INPUT CAPS]` records discovery bitmaps/axis classification and declared ranges.
- `[ABS MAP]` aggregates accepted SYN reports per device at no more than once per
  second, including interior movement. It reports device, mode=ABS, raw X/Y,
  independent ranges, axis validity, mapped and actual global logical positions,
  screen size, observed raw minima/maxima, endpoint-hit flags for each axis,
  near-edge activity and raw-changing/mapped-unchanged counts.
- Endpoint hits survive rate limiting. `virtio_input_obsluz` in the production idle loop flushes a pending
  sample after the interval even if the user stops moving at a corner. An idle
  device with no new report generates no repeated line. No unbounded queue.
- `stalled` also counts normal subpixel integer quantization; it is evidence to
  compare with raw coverage, not proof of a defect by itself.
- The paired `[ABS GEOM]` line reports framebuffer, compositor clip dimensions,
  desktop layer dimensions, guest UI scale and graphics generation. `comp` is
  obtained from the actual graphics getters used by compositor clipping, not an
  invented second screen cache. Unavailable desktop geometry is explicit.
- `[ABS WM]` reports WM's actual cached dimensions/scale at startup and each
  BWS display event. Kernel does not guess private Ring 3 state; correlate these
  lines with the paired input/graphics records. WM diagnostic output is serial
  only, so recording it does not create a display repaint.
- Timing uses the existing HPET monotonic clock. With no HPET, timed ABS summaries
  are unavailable; capability and WM logs still exist. HPET is not changed.

## New regressions

`test_virtio_input_asymmetric_abs_mapping` uses production capability discovery,
VirtIO event reception and input dispatch, with X=101..60001, Y=503..9503:

- 1280x720, 1920x1080, 1024x768, 800x600 and unusual 2560x540.
- Four corners, center, near-edge points, edge midpoints and out-of-range clamps.
- External harness logical/pixel sizes change normal -> maximized -> restored,
  including 125% host scaling, without changing guest dimensions/raw samples.
  All mapped results remain identical. This checks guest independence, not QEMU's
  frontend behavior; no external-window variable exists in the production API.
- X-only/Y-only reports preserve the other logical axis; repeated X before SYN
  consumes its newest value. Full signed 32-bit artificial ranges exercise the
  actual 64-bit mapping multiplication.
- Existing tablet modeset, cursor hotspot and compositor tests run at 100% and
  125% guest UI scale. The input API deliberately takes no UI scale parameter.

`test_abs_map_diag.cpp` tests one-second throttling, raw observed extrema, all
endpoint flags surviving 1000 reports, unchanged-mapped counters, pending corner
flush without further motion, silence on idle, and per-interval reset. The enabled production dispatcher/serial formatter also
verifies corner flush with no new packets and contracted interior raw coverage.

## Validation

- Asymmetric absolute mapping and all VirtIO Input/GPU/transport tests: **144/144
  PASS**, ASan/UBSan with leak detection enabled.
- Enabled ABS dispatcher/formatter/accumulator tests: **PASS**, ASan/UBSan with
  leak detection enabled.
- Cursor transactions, latest-position coalescing, bitmap hotspot clipping,
  one-final-present and clean-background tests: **PASS**, ASan/UBSan.
- Production compositor tests: **PASS**, ASan/UBSan with leak detection enabled.
- WM routing tests: **PASS**, ASan/UBSan; captured selection/scrollbar behavior and
  outside-focused-Shell routing remain covered.
- UI controls: **1379/1379 PASS**, ASan/UBSan.
- Themes: **155/155 PASS**, existing target and additional ASan/UBSan build
  with leak detection enabled.
- Additional Shell tests: **PASS**, ASan/UBSan, both guest scales.
- Additional command bindings: **23/23 PASS**, ASan/UBSan.
- Default and diagnostic-enabled affected components: **PASS**.
- `make -j2 all iso`: **PASS** after restoring default-off affected objects.
  Separate diagnostic ISO build: **PASS**. Extracted diagnostic kernel contains
  ABS MAP/GEOM/WM and CAPS markers; ordinary kernel contains none of them.
- `bash -n rununix_virtio_latest.sh` and `git diff --check`: **PASS**.
- PowerShell reviewed statically; pwsh/Windows frontend unavailable on this host.

No runtime PASS is claimed. Existing core/UI/Shell host targets retain their
`detect_leaks=0` configuration. Tracked generated test binaries were restored;
source and generated ISO/build outputs remain in the current worktree.

Final ISO SHA-256:

- ordinary: `a2621102cc4363520ed1c39c7b3beb2b0c51da5ad3ac81f485edc7c8d95b752c`
- ABS diagnostic: `fb09ac57d4d7ddd4a5c1ee1d384693ba911202d4050553315a06d20a730e2747`

## One manual transition test

Use `BursztynOS-abs-map-diag.iso` with the existing SDL/tablet command. Windows
launcher accepts `./runwindows.ps1 -Iso ./BursztynOS-abs-map-diag.iso` (choose
UEFI VirtIO Full, option 2), and prints exact executable/argv. Unix launcher accepts
`BURSZTYN_ISO=./BursztynOS-abs-map-diag.iso ./rununix_virtio_latest.sh`.
These commands are instructions for the user; they were not executed here.

Record the
full command, QEMU version, SDL DLL version, Windows display scale and whether
OpenGL is enabled. Select the diagnostic ISO in the CD-ROM argument; changing
launcher/device/display settings between phases would invalidate the comparison.

In one boot, keep guest mode at 1280x720. Visit and hold each visible guest corner
for at least one second in normal, maximized, then restored host-window states.
Record the time of each transition alongside serial output; do not reboot. Include
startup CAPS/WM lines and all ABS MAP/GEOM lines. Inspect these independent facts:

1. Does declared ABS_INFO remain the same? The guest currently queries it on device
   initialization, so the log records that declaration, not a fresh runtime query.
2. Across each phase, do observed raw values reach each declared axis endpoint?
   Use observed extrema as well as exact endpoint flags; host rounding can miss
   the last raw unit even without a large restriction.
3. With full raw coverage, does mapped/global position cover all guest corners?
   If raw covers its declaration but mapped does not, investigate the guest.
4. If only maximized raw coverage contracts while guest size/gen remains unchanged,
   restriction already exists before guest mapping, in QEMU/frontend input.
5. Did fb/comp/desktop/gen or ABS WM BWS-display-event change? Host maximize alone
   must not be treated as a request to resize the guest desktop.

No before/after runtime endpoint values have been collected yet.

## Changed files

`Makefile`, `input_device.cpp`, `abs_map_diag.h`, `virtio_input.cpp` (capability
logging guard and diagnostic idle flush only), `grafika.cpp` (diagnostic snapshot only), `menedzer_okien.cpp`
(WM diagnostic hooks only), `syscalls.cpp` (serial-only diagnostic prefix),
`runwindows.ps1`, `rununix_virtio_latest.sh`, `narzedzia/test_virtio_pci.cpp`,
`narzedzia/test_abs_map_diag.cpp`, this report; diagnostic ISO generated separately.

Shell, scheduler, HPET, GPU implementation/timeouts, cursor presentation/mailbox,
WM motion routing, context menus, app launch, capture, themes, reboot, BSP2 and
installer behavior were preserved.
