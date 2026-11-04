# Start theme repair and QEMU windowed pointer investigation — 2026-11-01

**Power-button regression fixed. Windows pointer regression is not yet established or fixed.** The Linux host cannot certify a Windows frontend/DPI correction. No guest coordinate offset, range reduction, delay, frontend default change or speculative driver calibration was introduced.

## Power button: cause and repair

[Diagnosis recorded before production edits](power-pointer-2026-11-01/diagnosis-before-changes.md). `PrzyciskObrazkowy` already inherits the ordinary `Przycisk` semantic palette. Its PNG contains no blue backing. Search, rather than Power, initially owns keyboard focus. There was no Start-specific fixed-blue style to replace.

The actual defect was retained lifecycle: the WM constructs Start once. Theme broadcasts rebuild the `Motyw` singleton, but `Surface::refresh()` traverses only surfaces registered in `ZarzadcaPopupow`. A closed Start is detached. Its button retains the palette copied at construction or its last visible theme. Opening reused that stale palette without refreshing controls. This explains a blue-looking button in an otherwise different theme.

The new regression constructs one persistent Start, changes themes while it is closed, calls the real refresh/open path and compares the button's role value. It [failed before the change](power-pointer-2026-11-01/theme-before.txt). `ZarzadcaPopupow::dodaj_wewnetrznie()` now refreshes the whole retained control tree before layout and painting. This fixes the shared lifecycle instead of adding a palette keyed by theme names.

Power's open appearance uses the ordinary button's active style. This does not assign keyboard focus to it or store an additional selected/focus flag. The popup owns focus while open; Escape retains the existing return to Power.

| State | Background | Border | PNG |
|---|---|---|---|
| Normal | `MOTYW_KOLOR_PRZYCISK` | `MOTYW_KOLOR_RAMKA` | Original straight alpha |
| Hover | `MOTYW_KOLOR_PRZYCISK_NAJECHANY` | `MOTYW_KOLOR_AKCENT_NAJECHANY` | Original straight alpha |
| Pressed | `MOTYW_KOLOR_AKCENT_AKTYWNY` | Active Accent | Original straight alpha |
| Keyboard focus | Button | `MOTYW_KOLOR_AKCENT` | Original straight alpha |
| Popup open | Active Accent | Active Accent; Accent if truly focused | Original straight alpha |
| Disabled | Button | Border | Existing alpha multiplier 96/255 |

Text/fallback state, border thickness and corner radius continue using ordinary BursztynUI theme styles. No font, geometry, PNG cache/scaler/decoder or trusted action routing was changed. The existing 36 px logical square, bottom/right 8 px insets, 8 px visible popup gap, shadow reservation, BSF measured popup width, disabled Lock/Sleep and semantic native fallbacks are preserved.

[Pixel state sheet](power-pointer-2026-11-01/power-theme-states.png): rows are Bursztyn Nocny, Bałtycka Noc, Grafit, Bursztyn Klasyczny, Bursztyn Jasny, additional green fixture. Columns are normal, hover, pressed, keyboard focus, open, disabled. Every one of **46,656 pixels** is compared against an ordinary BursztynUI button plus an independently calculated composition of the unchanged PNG; assertions also compare background and border to current role values. [Machine-readable role/state results](power-pointer-2026-11-01/power-theme-states.json) cover all 36 combinations. The extra unknown theme is also exercised through the Settings directory/metadata enumerator. Live changes while Start and its submenu are open verify complete current palette replacement while preserving the popup.

## Pointer: actual coordinate path

The [executed PowerShell launcher argv](power-pointer-2026-11-01/windows-default-argv.json) is the default Windows profile generated on Linux using PowerShell 7.4.6. It is **not a Windows QEMU execution**. The user's actual Windows QEMU version and DPI are still missing.

Canonical configuration: `pc,hpet=on`, TCG single thread, one vCPU, 2 GiB; HDA; VirtIO block/network/RNG; `-vga none`; `virtio-vga,disable-legacy=on,xres=1280,yres=720`; VirtIO keyboard and **relative `virtio-mouse-pci`**. Windows defaults to **GTK**; SDL is an explicit menu selection. No fixed host window size, tablet, fullscreen startup or custom grab option is specified. GTK chooses its own initial drawable/window geometry. The legacy profile uses **relative USB mouse** with xHCI, not USB tablet. PS/2 remains available through i8042.

For that default, the path is:

```text
host motion / GTK drawable and capture
  -> QEMU relative handler -> VirtIO EV_REL X/Y -> SYN_REPORT
  -> signed InputDevice report -> physical cursor accumulation/clamp
  -> full DisplayGeometry cursor rectangle
```

Absolute normalization is not active in the canonical profile. In the actual Linux probe, capability diagnostics show REL X/Y and no ABS axes. Therefore an absolute-axis calibration patch cannot by itself explain or fix this default launcher's symptom.

The optional absolute paths remain VirtIO EV_ABS or a descriptor-decoded USB tablet report, then the common InputDevice publication. For each axis the existing mapping already is:

```text
clamped = clamp(raw, source_min, source_max)
mapped  = (int64(clamped) - source_min) * (physical_size - 1)
          / int64(source_max - source_min)
```

The implementation casts before subtraction. The largest signed 32-bit source span times the largest supported signed destination extent fits int64. Invalid geometry waits behind the existing readiness barrier. Mapping happens once, independently per axis. It uses full physical framebuffer dimensions, not work area or UI scale. The real graphics transaction/adapter tests confirm committed DisplayGeometry limits and immediate endpoint behavior across modesets.

VirtIO copies the advertised signed min/max/fuzz/flat/resolution metadata. [QEMU 8.2.2's VirtIO HID source](https://github.com/qemu/qemu/blob/v8.2.2/hw/input/virtio-input-hid.c) uses static tablet ABS_INFO tables with `INPUT_EVENT_ABS_MIN/MAX` (0/32767) and zero resolution. Fullscreen is not an axis calibration update in that model. No dynamic source-range change was observed or established for the user's Windows binary. The default relative mouse has **no absolute min/max**.

[QEMU GTK source](https://github.com/qemu/qemu/blob/v8.2.2/ui/gtk.c) computes drawable scale and letterbox translation before input dispatch. Its relative branch subtracts successive transformed positions and depends on pointer capture; it attempts to warp the host pointer at monitor edges. Its fullscreen path changes host presentation/scale, not guest axis descriptors. These mechanisms make capture/viewport behavior a plausible area to test, **not proof of the Windows root cause**. The reported Ctrl+Alt+H shortcut also needs confirmation: upstream GTK 8.2.2 assigns fullscreen to Ctrl+Alt+F.

## Frontend experiment and its limits

[Test harness](../narzedzia/test_qemu_gtk_pointer.py) exercises **real GTK 8.2.2 on Linux/X11**, under a private Xephyr 1920×1200 display and Metacity. It uses the canonical device order and a disposable disk; audio is `none` as on Windows. Host XTest supplies pointer/keyboard events to GTK. QMP only reads physical cursor/geometry and device identity. This differs from existing headless QMP input injection and from physical Windows input.

[Results](power-pointer-2026-11-01/gtk-relative/result.json): exact four corners and center passed for small 800×600, large 1500×950, maximized 1920×1163, and fullscreen 1920×1200 windows. The guest stayed 1280×720, scale 100%, generation 1. **Returning from fullscreen failed** in this probe: host coordinates became 0,0, QEMU's title still indicated a pointer grab, and guest coordinates stopped at 638,350. Subsequent host moves at that boundary did not produce new guest deltas. The [raw stream](power-pointer-2026-11-01/gtk-relative/serial.txt) and final trace retain this failure. An earlier run independently stopped after returning at 679,377; its [result](power-pointer-2026-11-01/gtk-relative-first/result.json) is retained.

This localizes the observed Linux/XTest stop before guest mapping. It does **not** prove the original Windows issue is the same GTK defect, and it does not certify all relative frontend transitions. No kernel calibration or titlebar subtraction was made to conceal it. A [focused capture-reset experiment](power-pointer-2026-11-01/gtk-regrab/result.json) then passed large → fullscreen → returned-windowed: after returning, the harness explicitly released the GTK grab with Ctrl+Alt+G and reacquired it by clicking the drawable. All four exact corners and center within one physical pixel were reachable again, with unchanged guest geometry/generation. This is a diagnostic host intervention, not a launcher or guest fix. One preliminary trial required correcting the center-only assertion for scaled relative-motion quantization (641 rather than 640); [that result is retained](power-pointer-2026-11-01/gtk-center-quantization/result.json). Corner assertions remain exact. The default transition test still reports its failure.

## Diagnostics and Windows continuation

`BURSZTYNDIAGABSMAP=1` now samples **both relative and absolute** accepted reports. Fields include device, coordinate mode, raw X/Y, ABS ranges when applicable, mapped coordinates, physical screen dimensions, generation, raw extrema, endpoint hits and session mapped-Y extrema. Relative `raw` means deltas; zero ABS ranges are explicitly inapplicable. Collection is fixed-size and aggregated at most once per second per device, with no idle repeats. Release builds have it disabled.

The [read-only observer](../narzedzia/qemu_pointer_probe.py) records QMP mouse mode, physical guest X/Y, committed DisplayGeometry, stage minimum Y, actual Windows QEMU window/client bounds, host cursor and per-window DPI, plus the Win32 capture window and host cursor clipping rectangle. It does not label the outer/client window as the guest drawable or change QEMU's DPI awareness. Its own DPI measurement override success is recorded. It handles UTF-8 and Windows PowerShell UTF-16 serial logs, including split writes. On Linux it explicitly marks Windows measurements unavailable.

The Windows launcher adds **opt-in** diagnostic device selection (`relative`, `virtio-tablet`, `usb-tablet`, `ps2`), serial logging and localhost-only QMP. [All four generated profiles](power-pointer-2026-11-01/windows-verification.md) were executed through PowerShell and checked; the [canonical parity test](power-pointer-2026-11-01/launcher-parity.txt) passes. Default frontend, devices and PCI order are preserved. These options are comparisons, **not a verified Windows correction**.

[Windows verification procedure and commands](power-pointer-2026-11-01/windows-verification.md) accompany [the diagnostic ISO](../BursztynOS-pointer-window-diag.iso) and its [matching symbol manifest](power-pointer-2026-11-01/pointer-diag-symbols.json). Required outstanding evidence: actual Windows QEMU version/frontend/DPI; raw input before/after fullscreen; five points for small/large/maximized/fullscreen/returned windows; ABS/relative device comparisons; Windows DPI 100/125/150% where feasible. Without that evidence there is no honest basis to assert a Windows root cause or change the default frontend/device as a verified fix.

## Validation

All listed host suites completed with ASan/UBSan unless stated otherwise. No sanitizer findings occurred.

| Check | Result |
|---|---|
| Retained Start, theme lifecycle, Settings, keyboard/hover/grace, extra enumerated theme | 2,696 checks PASS |
| Desktop/Settings/Start, BSF/PNG geometry, all six theme fixtures and pixel states | 187,776 checks PASS; includes 975 dedicated PNG/BSF geometry checks |
| Signed relative pointer | 396,600 checks PASS |
| Absolute endpoints, independent X/Y, signed min/max, midpoint, clamping, odd ranges, 1/2/3 px and INT_MAX screens; 1280×720/1920×1080 at all four scales | 3,864 checks PASS |
| Input modes/readiness/API | 56 checks PASS |
| Physical DisplayGeometry transaction / immediate edges | 304 checks PASS |
| Real graphics + InputDevice + HID | 91 checks PASS |
| Stale DisplayGeometry snapshot / authorization | PASS |
| PS/2 parser / i8042+IRQ initialization | 330,443 / 4,165 checks PASS |
| HID descriptor/report / USB configuration | 6,051 / 48 checks PASS |
| OHCI production / xHCI pointer completion paths | 5,855 / 9,484 checks PASS; shared tablet streams also PASS |
| Full PCI/VirtIO/GPU/input/keyboard | 168 named cases PASS, 0 FAIL |
| Input event owner/queue paths | PASS |
| Cursor clipping/coalescing/repaint | PASS |
| BursztynUI controls | 1,379/1,379 PASS |
| Theme engine | 155/155 PASS (optimized, unsanitized target) |
| Bounded PNG decoding/allocation failures | 3,446 checks PASS |
| BSF / BSF UI-browser | 12,201 / 5,876 checks PASS |
| Rounded popup geometry/input | 308,494 checks PASS |
| User fault isolation | 32,768 checks PASS |
| Bounded raw diagnostics / observer protocol+ABI+encoding | PASS / 4 tests PASS |
| Windows launcher policy / actual PowerShell argv and parity | 10 tests PASS / PASS |
| Release QEMU Start/themes/search/countdown/rollback/Apply/PNG alpha and keyboard | PASS, five themes, nine PNG/geometry observations |
| Real GTK frontend transition test | Partial: four stages PASS, return FAIL; focused release/regrab experiment PASS; not Windows acceptance |

[Host logs and artifacts](power-pointer-2026-11-01/) contain exact commands and totals. The boot call-site audit initially encountered an old private `.bsf-test` source snapshot; it now excludes hidden generated directories, then the full VirtIO suite passed. No production activation-order code was changed.

[Release QEMU result](power-pointer-2026-11-01/qemu-themes/result.json) checks the unchanged PNGs/alpha, disabled actions, Start search, 8 px insets/gap, theme preview countdown, rollback and Apply. Five source icons match the packaged ISO and embedded kernel resources [byte for byte](power-pointer-2026-11-01/packaging.json). Production and diagnostic builds passed. [ISO extraction](power-pointer-2026-11-01/iso-verification.json) confirms the production kernel matches its ISO, the diagnostic ISO matches its symbol manifest, and raw input diagnostics are absent from production. No Ring 0 panic or unexpected Ring 3 fault was observed in these runs. The frontend pointer failure is retained separately instead of being reported as an overall input PASS.

[Release resolution/scale QEMU matrix](power-pointer-2026-11-01/qemu-scales/result.json): **PASS** for all eight combinations of 1280×720 / 1920×1080 and 100/125/150/200%. Checks include physical cursor bounds, immediate taskbar geometry, Start inset/gap, popup bounds and PNG alpha/cache pixels. This focused smoke run deliberately skips the longer desktop-anchor walk (`desktop_anchor_validation=false`); the full desktop host suite covers anchors. Guest font/scale changes do not constitute a host Windows DPI test.

## Code, memory, stack and artifacts

No BWS ABI, syscall, permission, pointer ownership, input wire ABI or power-routing change. The ordinary button/Start object sizes and PNG memory budgets are unchanged. The WM fixed code envelope remains 294,912 bytes (288 KiB). Release InputDevice has no new diagnostic array or sampling work; its coordinate formula is unchanged.

Diagnostic `AbsMapSample` grows from 96 to 112 bytes: **1,024 bytes** across 64 slots. This state is absent from release builds. Measured individual compiler frames: Power's themed-background override **48 bytes**, popup attach **48 bytes**, diagnostic report formatting **816 bytes**, diagnostic report publication **208 bytes**, diagnostic service **48 bytes**. These are per-function stack reports, not a total stack high-water claim. No PNG buffers or variable-sized allocations were added.

SHA-256 ([manifest](power-pointer-2026-11-01/hashes.json)):

```text
fb63913f0be63a67fd70e0c1dd037efcf4ca320bd80d3249ea7ecf520e7e51d6  system_operacyjny.bin
eefde8b6bd68b0506ab8888cefa4a990955e54d5c3387cdd141d98d93b4627d0  BursztynOS.iso
445cc73fe6e2cfe54bd3196f1a1f17b52d9ae9969b7535065369f1a3fe3354e5  BursztynOS-pointer-window-diag.iso
```

The Windows acceptance criteria remain open. The deliverable is a verified theme repair, preserved guest mapping, reproducible input evidence and a usable Windows diagnostic path—not an unproven Windows kernel fix.
