# Windows QEMU graphics regression investigation — 2026-11-09

Work is on `test`, starting at `3e72641`. No commit, push, protected-branch update or KASLR work is part of this change. The eight pre-existing untracked files are retained. Windows execution is unavailable on this Linux host; Windows results are **not** claimed.

## Findings and causal evidence

There are two independent problems: display-policy feedback and expensive software frame work. A short resize log does not establish an infinite loop.

The integrated VirtIO driver preferred `GET_DISPLAY_INFO` over the guest's boot policy. The compositor then polled that command every 500 ms **inside the screen lock**, and passed any difference of at least 32 pixels to the complete display transaction. Thus 3440×1322 and 3440×1440 both become accepted guest modes: their 118-pixel height difference exceeds the threshold. A transaction prepares graphics surfaces, a wallpaper, replacement desktop/window surfaces and a GPU resource/backing, publishes a full frame and releases the old state. Repeating this is expensive even when every allocation is eventually freed.

`GET_DISPLAY_INFO` describes QEMU's requested UI geometry, not a promise that the guest must select that resolution. QEMU 8.2.2 fills the reply from `req_state`, and its UI callback replaces that state. [QEMU VirtIO GPU source](https://github.com/qemu/qemu/blob/v8.2.2/hw/display/virtio-gpu-base.c). GTK supplies configure/drawing-area dimensions; SDL supplies window-resize event dimensions. [GTK source](https://github.com/qemu/qemu/blob/v8.2.2/ui/gtk.c), [SDL source](https://github.com/qemu/qemu/blob/v8.2.2/ui/sdl2.c). These callbacks explain how frontend changes can override `xres/yres`. The exact Windows event sequence producing 1322 versus 1440 remains an inference until the affected QEMU version, frontend, DPI, window/fullscreen events and host geometry trace are captured. A monitor/work-area conflict is plausible, not proven.

The original launchers both use `pc,hpet=on`, `-cpu max`, `tcg,thread=single`, one vCPU, 2 GiB and VirtIO 2D. They differ in default GPU hints: Linux 1920×1080 versus Windows 1280×720, with Windows originally rejecting higher dimensions. Windows explicitly uses GTK `zoom-to-fit=on` or SDL and performs a one-time outer-window cap. Linux previously left the frontend implicit. The Windows limiter exits after finding/capping the window; it is not a continuous resize loop. Windows retains its local UEFI assembly and muted HDA backend; Linux retains OVMF discovery and native audio.

The compositor already bounds window movement to old/new window damage, coalesces movements, retains buffers when dimensions are unchanged, keeps wallpaper in a retained surface and presents sparse GPU regions. Measurements at **stable 1280×720**, with **no modesets or framebuffer allocations during dragging**, reproduced severe latency. Therefore resize churn is not necessary to cause the performance failure.

Native sampling found the kernel's linked variable-length `memcpy` in `biblioteki/mbedtls/mbedtls_port.cpp` copying one byte per iteration. Graphics' supposed fast RAM scanline path called that implementation. The initial 720p run spent 8.11 seconds copying 68.44 MB during its drag window. Frame composition also copied the wallpaper, repeatedly scanned opaque window runs and visited transparent desktop pixels. The software cursor correctly preserves a clean scene, but its scene-to-driver copy added another large RAM pass. Busy GPU waits are measured separately; they were substantially smaller than composition plus copying in the initial drag run.

Input servicing and composition share the idle loop; GUI operations also use the screen lock. Large synchronous frames therefore delay event consumption. No scheduling policy was changed to collect measurements. The fix reduces work within that critical interval.

TrueType/Unicode rendering remains enabled. BSF retains its bounded glyph cache; compositor title coverage is retained rather than rerasterized on each move. The diagnostics app polls its model once a second and repaints only when model/log state changes. Its ordinary autostart window is deliberately included in the benchmark, so diagnostics redraws are included, not disabled to improve results.

## Source changes

| Source | Change |
|---|---|
| `jadro/gui/display_mode_policy.h`, `jadro/rdzen/kernel.cpp` | Explicit physical boot mode (default 1280×720), optional `bursztyn.video=WIDTHxHEIGHT`, stable opt-in host following, feedback-cycle suppression and guest-selection priority. `bursztyn.display=observe` records hints without following them. |
| `jadro/gui/skladacz_obrazu.cpp` | Move host polling outside the screen lock; skip transparent desktop groups; compose opaque RGB groups in one pass; retain existing alpha and damage semantics. Add a narrow SMAP access scope for the already validated BWS57 popup copy. |
| `sterowniki/grafika/grafika.cpp`, `.h` | Fixed-size general-register RAM copies; reuse driver backing as the cursor-free scene after startup; explicit borrowed-buffer ownership through modeset/rollback; flush cursor publication before restoring scratch pixels. Keep the splash on independent surfaces and keep firmware/MMIO copies volatile. |
| `jadro/gui/graphics_copy.h` | Bounded 64-byte RAM block copies with exact tails, without SIMD or the variable-length byte-copy fallback. |
| `jadro/gui/graphics_perf.h`, `jadro/gui/bufory_grafiki.cpp`, `jadro/gui/ekran.cpp` | Cumulative counters, a 128-frame sample ring, 32-event mode/hint histories and graphics-pool live/peak/allocation/free accounting. No rendering-path log output. |
| `sterowniki/virtio/virtio_gpu.cpp`, `.h` | Guest-owned initial mode, same-size resource reuse, command/wait/transfer telemetry. Remove the nonexistent GPU event queue 2: GPU has control/cursor queues; display events belong to device configuration. |
| `runwindows.ps1`, `rununix_virtio_latest.sh` | Matching 1280×720 GPU hints, explicit Linux GTK viewport scaling, larger selectable hints; independent Windows initial outer-window dimensions. Muted Linux audio retains HDA and its PCI slot. |
| `Makefile` | Explicit `BURSZTYN_VIDEO_MODE`, header dependencies, policy test and repaired moved PMM host-fixture rule. |
| `narzedzia/graphics_perf_qemu.py`, `graphics_perf_capture.py` | Native input/drag/lifecycle benchmark and platform-independent QMP TCP observer with exported symbol addresses. The benchmark keeps its QMP socket under a short temporary path so nested evidence directories do not exceed AF_UNIX limits. |
| `testy/test_display_mode_policy.cpp`, existing compositor/display tests | Feedback/jitter/user-priority cases; pixel-contract comparisons across transparency/marker/tail cases; same-mode resource reuse and real production display transactions. |
| `testy/test_husarz_qemu.py`, `power_png_qemu_observer.py`, `display_geometry_qemu_cases.py`, `narzedzia/qemu_process_layout.cpp`, `bsf_display_layout.cpp` | Repair the close-button sample, moved fixture include, BUR v2 observer relocation, actual process-layout metadata and bilinear PNG oracle. Navigate the ultrawide list entry through the native scrolled popup. Existing pixel/geometry assertions remain active. |

Physical framebuffer dimensions, UI scale and host viewport size remain separate. The release defaults to 720p; it does not choose the monitor's resolution. Display Settings can select 1080p, 1440p or ultrawide modes with the existing permission/confirmation/rollback policy. For a selected boot mode, build for example `make iso BURSZTYN_VIDEO_MODE=3440x1440`. Initial `virtio-vga,xres/yres` values are hints, not a guest-mode guarantee.

Host following is opt-in through `bursztyn.display=host`. A hint must remain stable for one second; a rapid A→B→A feedback cycle stops following. Saved configuration or an authorized guest mode selection pins the mode. The normal profile does not continually submit display-info commands.

## Telemetry and measurement boundaries

`GraphicsPerf::graphics_perf` is a bounded cumulative debugger snapshot. It records composition/presentation elapsed time, damage rectangles/pixels, full compositions, RAM copy bytes/time, GPU command busy-wait elapsed time, transfer/flush counts and pixel area, resource creation attempts/successes, scanout commands, host polls/hint changes, mode requests/commits and consumed input. Frame records contain completion time, composition/presentation time, damage and input-to-submission latency. Separate histories carry physical dimensions, scale, timestamp and source (boot/config/guest/host/rollback).

These are guest monotonic **elapsed** durations, including any preemption or host descheduling. They are not hardware CPU-cycle measurements. The benchmark separately records QEMU process CPU seconds and host elapsed time. `gpu_wait_ns` overlaps presentation and must not be added to it. Frames per second describes the scripted input workload, not an uncapped rendering throughput test; idle damage rates are intentionally low. Input timing starts when the common guest pointer path consumes a report. The visible-capture probe includes host screenshot/observation overhead and is an upper bound to an observed QEMU frame, not monitor photon latency.

Read-only snapshots use stop/read/continue to avoid blocking the QEMU backend with a large memory dump while a guest GPU command times out. No guest memory or scheduler state is patched. Hot paths perform bounded counter/sample updates and emit no per-frame serial lines. Aggregate counters cover the whole interval; the frame ring retains only the latest 128 samples (all paired benchmark frames fit). Counters for GPU commands/scanouts are attempts; successful framebuffer creations and runtime mode commits have separate counters.

## Measured results and acceptance

The production ISO is `BursztynOS.iso`, SHA-256:

```text
6c073268920fa647e85259ffe6697d16ac28eec3fa54f0c0ad5442f96e6ee442
```

The final full observer ELF SHA-256 is `c979f57415ccc0c9c0f5b97c77b189da5fe6b3054e7e11411ffeb4e706345919`; the stripped kernel inside the ISO is `7583cc8ada78d965d43dc208c25c5cd56b9bebef8cfa83ddba16f4805037c769`. Their entry point and PT_LOAD segments match exactly; see [release integrity](graphics-performance-2026-11-09/release-integrity.json). The original paired measurements below used the preceding optimized revision (`209c917bf5d8e41afc532690201a91a4a8ceffe907bdc1b0c9cc88f9469a87a5`, observer ELF `a42f746343b24e2833f63d539cebbb10671efccb74f3ef64b89481a6b1f6924d`). Final release validation after the firmware-fallback correction is recorded separately below. High-resolution boot fixtures change only GRUB configuration and retain the same kernel/assets; their distinct checksums are recorded. Production packaging and BSF asset verification pass.

Paired comparison: QEMU 8.2.2, `pc,hpet=on`, CPU `max`, `tcg,thread=single`, one vCPU, 2 GiB, modern VirtIO 2D, software cursor and the ordinary diagnostics window, display `none`. Both boots use the normal production menu and packaged TTFs. The 24 input reports produce the same final window displacement; damage volume is comparable (15.98 versus 16.18 million pixels). This host is itself virtualized and has no `/dev/kvm`.

| 1280×720 paired workload | Before | After |
|---|---:|---:|
| Mean composition | 297.01 ms | 74.89 ms |
| Mean presentation | 244.87 ms | 16.05 ms |
| Mean total frame work | 541.88 ms | 90.94 ms |
| p95 total frame work | 917.38 ms | 364.24 ms |
| Observed damage frames/s | 1.58 | 5.23 |
| Mean consumed-input → submitted-frame/cursor | 567.24 ms | 60.28 ms |
| Median host injection → guest input consumption | 528.82 ms | 47.26 ms |
| QEMU process CPU time during workload | 16.93 s | 4.45 s |
| Full-screen compositions during dragging | 0 | 0 |
| Framebuffer recreations during dragging | 0 | 0 |
| Scene-to-driver RAM copies during dragging | present | 0 bytes |

Mean frame work improves by about **83%** (5.96× faster); average input-to-submission latency improves by about 89%. These are measured improvements, not a claim of 60 FPS or of Windows performance. Remaining long frames are visible in the p95 evidence.

Other final observations are listed below. They exercise different resolutions/frontends and host load, so the table is **not** a controlled cross-frontend speed ranking:

| Configuration | Mean frame work | p95 frame work | Functional result |
|---|---:|---:|---|
| 1920×1080, headless | 185.55 ms | 380.10 ms | PASS |
| 2560×1440, headless | 53.50 ms | 121.46 ms | PASS |
| 3440×1440, headless | 97.26 ms | 391.06 ms | PASS |
| 1280×720, Linux GTK, zoom-to-fit | 261.93 ms | 550.81 ms | PASS |
| 1280×720, Linux SDL | 121.63 ms | 222.05 ms | PASS |

All four resolutions passed desktop startup, repeated dragging, idle geometry stability, minimize/restore, Start popup and the visible software-cursor/old-background test. Every drag interval recorded zero framebuffer recreation, zero full-screen composition and zero scene-to-driver copies. Live graphics-pool bytes were unchanged over each drag/idle interval. The 720p visible-capture observation was 106.94 ms; the raw per-run records contain the other observations and their boundaries. Native register reads on the 1080p/1440p runs show `CR4=0x300678`, `EFER=0xd01`: SMEP, SMAP and NX enabled.

GTK and SDL also passed three host viewport size changes and host maximize/restore with unchanged guest geometry. Both original frontend runs used ISO `209c917bf5d8e41afc532690201a91a4a8ceffe907bdc1b0c9cc88f9469a87a5`. Native VirtIO currently uses the pre-existing canonical software cursor; hardware cursor behavior is covered by host compositor tests, not claimed as a verified native cursor-plane visibility result.

A separate ordinary-ISO UI test passed live guest transitions **720p → 1080p → 2560×1440 → 3440×1440 → 720p**. It checks immediate/WM-completed frames, taskbar anchoring, physical cursor edges, stable geometry, Start/power popup bounds and all five bilinear PNG caches plus actual alpha-composited pixels at every mode. No guest-memory writes or fixed-ASLR override are used.

Evidence: [comparison and counters](graphics-performance-2026-11-09/comparison.json), [paired baseline](graphics-performance-2026-11-09/baseline-paired/result.json), [paired optimized 720p](graphics-performance-2026-11-09/candidate-720/result.json), [live mode transitions](graphics-performance-2026-11-09/native-modes-r6/result.json), [GTK](graphics-performance-2026-11-09/linux-gtk/result.json), [SDL](graphics-performance-2026-11-09/linux-sdl/result.json). Each run directory contains its serial log and available command/screenshot evidence. Baseline binaries are retained locally under `.graphics-perf/` for reproduction.

Host ASan/UBSan checks passed: 8,681,072 compositor checks, 73,816 popup restoration checks, 8,681,698 production startup/scanout checks, 634 final display transaction/edge/resource-reuse checks (598 before the firmware-fallback regression case), policy cases, 12,204 BSF checks, 9,084 BSF UI checks, all 1,187 Unicode conformance cases plus 10,000 mutations, 60,662 Notepad checks, 4,604 font-resource checks, 2,696 Settings/Start checks, HDA 241 diagnostic/160 isolation tests, both SSP configurations (2,007 checks each), 10,000 ASLR layout trials, package RSA verification/path boundaries, both production-loader trace configurations (31 scenarios and over 11.28 million checks each), and audio GUI/BWS/HDA host checks.

Native BWS abuse tests passed. Native HDA WAV output and muted `none` both passed in the final isolated rerun, retaining the existing deferred-stop/readback assertions; `none` stopped 2.14 ms after its deadline. An earlier concurrent-VM run passed WAV but missed the muted deadline by 177.03 ms and failed its unchanged assertion. Both attempts are preserved. This remains evidence of sensitivity to host contention, not a hard-real-time guarantee. The full nightly/fuzz/physical-hardware matrix was not rerun.

The original 3440×1440 baseline reached its 900-second desktop timeout, so there is no invented high-resolution baseline FPS or speedup. The final high-resolution test passed. Windows runtime and Windows GTK/SDL performance remain untested; the tools/procedure below are the handoff for that remaining platform acceptance. The GTK/tail figures above also show that additional tuning may be useful on constrained hosts.

The first `REP MOVSQ` experiment was insufficient and was replaced by fixed-size general-register blocks. Early intermediate figures are not final acceptance results. A GRUB-only test fixture initially omitted the release font/settings modules; it was rejected and corrected before the accepted baseline. The ordinary production ISO was then used for the paired 720p benchmark. A separate attempt exposed the SMAP popup fault described above. A first boot-mode policy using observed GOP dimensions selected 1920×1080 despite the GRUB request; explicit guest policy corrects that. The extended native test also exposed stale nearest-neighbor PNG expectations despite the integrated bilinear renderer; the pixel oracle was corrected while preserving exact cache/alpha assertions. The newly added ultrawide case required scrolling/navigation past the popup’s eight visible rows. An accidentally overlapping build lost a generated worker object; a subsequent single production build regenerates it. The missing moved PMM fixture rule and a test-only allocator-symbol mismatch were also repaired before rerunning the affected checks.

## Final release verification after firmware fallback correction

The final correction preserves the completed scene when returning from driver-owned VirtIO backing to firmware memory with a different pitch. The host regression writes a known scene pixel, switches backend and verifies both that pixel and the retained GPU resource. The updated display test passes **634 checks**, production startup/scanout passes **8,681,698 checks**, and mode-policy tests pass under ASan/UBSan; see [release host log](graphics-performance-2026-11-09/release-host.log).

The interrupted session had built the final ISO and started additional validation, but its temporary final-run files were unavailable when resumed. The retained earlier measurements above are therefore distinguished from these new persistent final-image runs. The production image was checked directly: its stripped kernel has exactly the same entry point and PT_LOAD segments as the observer ELF. Release metadata verification inspected 616 files with zero findings; launcher configuration tests passed 10 cases plus eight parity checks. See [package audit](graphics-performance-2026-11-09/release-packaging.log), [release inventory](graphics-performance-2026-11-09/release-inventory.json) and [launcher checks](graphics-performance-2026-11-09/release-launchers.log).

| Final configuration | Mean frame work | p95 frame work | Result |
|---|---:|---:|---|
| [1280×720, headless](graphics-performance-2026-11-09/release-720/result.json) | 92.28 ms | 205.77 ms | PASS |
| [1920×1080, headless](graphics-performance-2026-11-09/r1080/result.json) | 68.33 ms | 217.71 ms | PASS |
| [2560×1440, headless](graphics-performance-2026-11-09/r1440/result.json) | 52.06 ms | 183.91 ms | PASS |
| [3440×1440, headless](graphics-performance-2026-11-09/rultra/result.json) | 43.59 ms | 83.15 ms | PASS |
| [1280×720, Linux GTK](graphics-performance-2026-11-09/rgtk/result.json) | 63.41 ms | 149.61 ms | PASS |
| [1280×720, Linux SDL](graphics-performance-2026-11-09/rsdl/result.json) | 47.51 ms | 187.76 ms | PASS |

Final 720p consumed-input-to-submission latency averages 14.95 ms; median host-injection-to-consumption latency is 78.14 ms. The earlier paired benchmark remains the controlled before/after comparison; final reruns occurred in a resumed host session and the table is not a frontend or resolution speed ranking.

All six final drag/lifecycle runs passed stable geometry, visible software cursor and restored old background, minimize/restore and Start. Every drag interval reports **zero full-screen compositions, zero framebuffer creations, zero mode commits and zero scene-to-driver copied bytes**. Live graphics bytes match before and after each drag/idle interval. All six register snapshots show SMEP, SMAP and NX enabled. GTK and SDL each passed five host resize/maximize/restore observations with unchanged guest geometry. They use the exact final production checksum at the start of this section; the three higher-resolution boot fixtures alter only GRUB configuration and have separate recorded hashes. Actual argv, serial logs and screenshots accompany each result. The final GTK run also exercised the exported-symbol QMP TCP capture (two coherent samples); see [TCP metrics](graphics-performance-2026-11-09/rgtk/tcp-metrics.json) and [all final counters](graphics-performance-2026-11-09/release-comparison.json).

The final production ISO also passed the complete native **720p → 1080p → 2560×1440 → 3440×1440 → 720p** Display Settings sequence, with taskbar anchoring, cursor edges, immediate and WM-completed frames, popup bounds, all five bilinear PNG caches and actual composited pixels checked at every mode. See [final transitions](graphics-performance-2026-11-09/release-modes/result.json), [native run log](graphics-performance-2026-11-09/release-modes-run.log) and [actual QEMU argv](graphics-performance-2026-11-09/release-modes/command.json). This dedicated transaction test uses its existing i8042-off/XHCI/USB keyboard fixture plus VirtIO mouse, distinct from the launcher-parity drag profile.

One initial final-resolution attempt failed before QEMU startup because its nested output directory exceeded the AF_UNIX socket-path limit. Its failed result is retained at `release-1920x1080/result.json`. The benchmark now puts its socket in a short temporary directory; the subsequent native runs pass. This was an observer-path failure, not a guest performance measurement.

The retained audio, Unicode/font, BWS-abuse and broader host security results above precede the final firmware-only fallback correction; the focused graphics, packaging and launcher checks were rerun on the final state. Windows runtime and hardware cursor-plane visibility remain unverified.

## Reproduction on Windows

Copy the final `BursztynOS.iso`, `runwindows.ps1`, `narzedzia/graphics_perf_capture.py` and the matching exported `graphics-symbols.json`. Compare `Get-FileHash .\BursztynOS.iso -Algorithm SHA256` with the final evidence checksum before testing. Record Windows version, QEMU version, display backend, host monitor pixels, Windows DPI percentage and GPU/driver. Keep the same ISO and machine profile for GTK/SDL comparisons.

Start `./runwindows.ps1 -DisplayBackend gtk -GuestWidth 1280 -GuestHeight 720 -QmpPort 4444`, select the UEFI VirtIO Full profile, and retain its printed argv and serial log. In a second terminal run:

```powershell
python .\narzedzia\graphics_perf_capture.py --symbols .\graphics-symbols.json --port 4444 --seconds 120 --output windows-gtk-720.json
```

Spend 10 seconds idle, then move the pointer, repeatedly drag the diagnostics window, resize it, minimize/restore it and open/close Start. Use **guest Display Settings** to repeat at 1920×1080, 2560×1440 and 3440×1440. Resize, maximize and restore the host QEMU window independently; record timestamps. Physical geometry must stay selected through these host-only changes. Verify cursor visibility and restored old pixels, text, overlap, wallpaper and rounded decorations. Capture screenshots/video alongside telemetry; the observer alone cannot prove physical display quality.

Close QEMU normally and repeat with `-DisplayBackend sdl`, the identical ISO checksum and otherwise equivalent settings. `-WindowWidth`/`-WindowHeight` affect only the initial Windows outer-window cap, independently of guest physical mode. GPU hints do not select the final guest resolution. Record `Get-Process qemu-system-x86_64 | Select-Object Id,CPU,WorkingSet` before/after for host CPU/memory context. Monitor live graphics bytes after returning to the same mode/window state; distinguish a retained wallpaper/glyph cache from monotonic framebuffer/DMA growth.

For a host-geometry diagnosis, append `bursztyn.display=observe` to the normal GRUB entry temporarily at boot. The same ISO then records changing host hints without applying them. Correlate the hint history with Windows viewport/fullscreen events. Use `bursztyn.display=host` only to exercise deliberate dynamic following; stable hints may change mode, jitter must not thrash, and a guest selection must take priority. Changing boot arguments this way does not change the ISO checksum.

Windows GTK/SDL runtime, Windows DPI transitions and physical Windows cursor/video latency remain external acceptance tasks. Linux emulation cannot certify those results.

## Security preservation

No BUR v2, Ring 3 ASLR, NX/W^X, SMEP/SMAP, SSP, protected canary, BWS range validation, app permission or RSA/SHA-256 verification checks are removed or relaxed. The popup fix scopes access to a range already validated by the syscall and closes access before rendering. Graphics shared backing is supervisor-only and remains owned by the GPU driver; it is never exposed as a Ring 3 surface. No kernel SIMD/FPU state is introduced. Existing audio, font and Unicode paths remain enabled.
