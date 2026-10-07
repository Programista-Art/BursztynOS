# Windows startup, splash transitions and reboot audit — 2026-10-06

Continued from the current worktree, preserving the preceding welcome/reboot/typography work. No QEMU process, guest boot, Windows test or repeated emulator reboot loop was started. All execution results here are Linux host tests. **Windows runtime acceptance is pending, including the splash-disabled comparison.**

## Findings and evidence

The Windows VirtIO profile already defaulted to GTK, `tcg,thread=multi`, modern `virtio-vga`, `virtio-tablet-pci` and `virtio-keyboard-pci`. There was no `virtio-mouse-pci`. SDL existed only as an explicit backend parameter/menu option 12, labeled experimental/diagnostic; there was no automatic GTK-to-SDL fallback. However, Standard VGA profiles omitted `-display` entirely, ignored the selected backend, and used TCG single-thread. Thus their actual frontend depended on the Windows QEMU build rather than the script's GTK default. We cannot identify that implicit frontend without Windows runtime evidence.

Every Windows profile now specifies the selected display, default GTK, and TCG multi-thread. A capability query checks the requested backend before disk preparation; missing GTK produces an explicit error and never selects SDL automatically. The query is executed only when the user starts a profile; it was not executed in this environment. SDL remains an explicitly labeled diagnostic choice. Existing Windows paths, ISO/disk selection, firmware search, menu options and audio backend/device parameters remain. Audio device insertion now precedes storage/VirtIO, and GPU precedes keyboard/tablet, matching the maintained Linux/Makefile PCI device order. This is alignment with the supplied known-good policy, **not proof that device ordering caused the freeze**. Existing DirectSound/HDA is retained; no new audio backend is introduced and its Windows runtime stability is not inferred from these host tests.

Two independent defects were found:

1. The old message animation in `Powitanie::Sekwencja::aktualizuj()` faded the old text out over 150 ms, changed the message at 1600/3200 ms, and faded the new text in over 150 ms. At each exact boundary `jasnosc` was zero. `Powitanie::rysuj()` cleared the message rectangle and then rendered text in exactly the background color. A probe against the saved pre-change production header produced **brightness=0, center ink pixels=0 at 1600 ms**. Clearing and drawing already occurred before the same source publication; there was no separately published clear step. The empty result itself was the defect. This establishes a splash contribution to flicker, not the Windows freeze's root cause.
2. `virtio_gpu_quiesce_for_reboot()` ignored `virtqueue_pop_used()` errors in its drain after `SET_SCANOUT(resource_id=0)`. The real queue implementation rejects malformed completions with a negative return without advancing `last_used_idx`; `virtqueue_has_used()` consequently stays true. The inner drain could loop forever even though lock acquisition and outer wait loops were bounded. An isolated test of the production GPU function injected that documented error return after the scanout command: the pre-fix process timed out after two seconds (**exit 124**). The same probe now returns within that watchdog (**exit 0**). This is a reproducible conditional guest-side hang, **not evidence that Windows QEMU actually supplies the malformed completion in the reported freeze**.

The fix bounds both reboot drains to the queue's capacity, checks rejected completions, records failure, and releases the GPU lock. An error before scanout aborts further device work. Normal queue submission/recycling, completion validation and command semantics are unchanged. No GPU timeout values changed: command/RESOURCE_FLUSH timeout remains 500 ms; reboot quiesce timeout remains 200 ms; existing spin guards remain.

## Exact Windows arguments before and after

These are the argument arrays derived from the script, **not a Windows runtime capture**. `DISK` means `Join-Path $ScriptDir "wirtualny_dysk.img"`; `ISO` means the existing normal/default ISO or the selected `-Iso`/CI path; `OVMF` means the existing firmware-search result. The Windows path strings and quoting are supplied as separate PowerShell array elements. `NAME` is the existing selected mode name. UEFI includes the indicated `-bios OVMF` pair, BIOS omits it. No-ISO modes omit the `-cdrom ISO` pair.

Previous default VirtIO Full:

```text
-machine pc,hpet=on -cpu max -accel tcg,thread=multi -smp 1 -m 2G
-drive id=disk,file=DISK,format=raw,if=none
-device virtio-blk-pci,drive=disk,disable-legacy=on
-netdev user,id=n1 -device virtio-net-pci,netdev=n1,disable-legacy=on
-device virtio-rng-pci,disable-legacy=on
-device virtio-keyboard-pci,disable-legacy=on
-device virtio-tablet-pci,disable-legacy=on
-vga none -device virtio-vga,id=gpu0,disable-legacy=on,xres=1280,yres=720
-display gtk
-audiodev dsound,id=snd -device intel-hda -device hda-output,audiodev=snd
[-bios OVMF] [-cdrom ISO] -serial stdio -name "Bursztyn OS - NAME"
```

New default VirtIO Full:

```text
-machine pc,hpet=on -cpu max -accel tcg,thread=multi -smp 1 -m 2G -display gtk
-audiodev dsound,id=snd -device intel-hda -device hda-output,audiodev=snd
-drive id=disk,file=DISK,format=raw,if=none
-device virtio-blk-pci,drive=disk,disable-legacy=on
-netdev user,id=n1 -device virtio-net-pci,netdev=n1,disable-legacy=on
-device virtio-rng-pci,disable-legacy=on
-vga none -device virtio-vga,id=gpu0,disable-legacy=on,xres=1280,yres=720
-device virtio-keyboard-pci,disable-legacy=on
-device virtio-tablet-pci,disable-legacy=on
[-bios OVMF] [-cdrom ISO] -serial stdio -name "Bursztyn OS - NAME"
```

Previous Standard VGA:

```text
-machine pc,hpet=on -cpu max -accel tcg,thread=single -smp 1 -m 2G
-drive id=disk,file=DISK,format=raw,if=none
-device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0,unit=0
-netdev user,id=n1 -device e1000,netdev=n1
-device qemu-xhci,id=xhci -device usb-kbd,id=usbkbd,bus=xhci.0
-device usb-mouse,id=usbmouse,bus=xhci.0 -vga std -global VGA.vgamem_mb=64
-audiodev dsound,id=snd -device intel-hda -device hda-output,audiodev=snd
[-bios OVMF] [-cdrom ISO] -serial stdio -name "Bursztyn OS - NAME"
```

New Standard VGA (explicit compatibility menu remains, with its existing devices):

```text
-machine pc,hpet=on -cpu max -accel tcg,thread=multi -smp 1 -m 2G -display gtk
-audiodev dsound,id=snd -device intel-hda -device hda-output,audiodev=snd
-drive id=disk,file=DISK,format=raw,if=none
-device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0,unit=0
-netdev user,id=n1 -device e1000,netdev=n1
-device qemu-xhci,id=xhci -device usb-kbd,id=usbkbd,bus=xhci.0
-device usb-mouse,id=usbmouse,bus=xhci.0 -vga std -global VGA.vgamem_mb=64
[-bios OVMF] [-cdrom ISO] -serial stdio -name "Bursztyn OS - NAME"
```

Explicit SDL diagnostics replace only the display value with `sdl`; `-AudioBackend` retains its existing substitution. There is no production `virtio-mouse-pci`. Standard VGA's explicitly selected USB compatibility input remains.

## Linux/Windows comparison

| Component | Linux maintained VirtIO launcher / Makefile | Windows VirtIO after update |
|---|---|---|
| Display | Explicit SDL | Explicit GTK; SDL diagnostic only |
| TCG | `rununix_virtio_latest.sh`: single; Makefile: multi | Multi |
| Machine / CPU / vCPUs / RAM | `pc,hpet=on` / `max` / 1 / 2G | Same |
| GPU | `-vga none`, `virtio-vga,disable-legacy=on,xres=1280,yres=720` | Same, plus existing `id=gpu0` |
| Input | VirtIO keyboard + tablet, implicit modern device properties | Same devices, existing `disable-legacy=on` |
| Audio | ALSA + Intel HDA + HDA output | Existing DirectSound + same HDA devices |
| Disk | Raw `if=none`, modern VirtIO-BLK; installed Linux boot adds `bootindex=1` | Existing raw `if=none`, modern VirtIO-BLK for VirtIO profiles |
| Network | User network + modern VirtIO-NET | Same |
| RNG | Explicit `/dev/urandom` object + VirtIO-RNG | Existing default host RNG + modern VirtIO-RNG |
| UEFI | Monolithic `-bios`, or readonly CODE pflash + separate writable runtime VARS; copies VARS from template | Existing firmware search and `-bios`; no separate VARS/pflash pair existed |
| Timer flags | HPET enabled, no `-icount`/`-rtc` timing override in these profiles | Same |
| PCI device insertion | HDA, BLK, NET, RNG, GPU, keyboard, tablet | Now same order |

`rununix.sh` is the separate legacy AHCI/E1000/USB launcher: TCG single, Standard VGA, existing mono/split UEFI logic, and no explicit display option. It remains untouched; no implicit frontend is claimed for that script. The maintained Linux VirtIO launcher and Makefile retain their explicit SDL policy. Both Linux script contents were verified identical to their original committed contents. Windows menu options 14/15 retain their existing compatibility storage/input selection and existing ISO/UEFI behavior rather than receiving a menu redesign.

## Timing and rendering

Old boundaries: 1.6 s and 3.2 s; 150 ms fade out + 150 ms fade in per message; minimum visibility 5.0 s; final fade 400 ms; earliest complete normal reveal about 5.4 s. Old final fade started from the tick detecting eligibility, so a delayed tick could also push its origin back.

New normal schedule with an already-ready desktop:

| Absolute elapsed time | Visible state |
|---|---|
| 0–2.0 s | `Witamy w Bursztyn OS`, full opacity |
| 2.0–2.4 s | Direct old/new message crossfade |
| 2.4–4.2 s | `Polski system operacyjny`, full opacity |
| 4.2–4.6 s | Direct old/new message crossfade |
| 4.6–6.4 s | `Niezależny system`, full opacity |
| 6.4–7.0 s | 600 ms welcome-to-complete-desktop fade |

First hold is 2.0 s, subsequent fully opaque holds are 1.8 s, with 400 ms transitions, chosen to achieve the requested approximately two-second reading stages and a seven-second overall presentation. Minimum visual duration before final fade is 6,400,000 µs, final fade 600,000 µs. Existing 33,333 µs pacing is unchanged.

The shared smoothed system Unicode renderer and typography (56/36 logical px line boxes) remain. Rendering clears the retained message band, draws the old text at `255-weight` and the new at `weight`, then lets the existing presentation consume that completed surface. At every weight some message coverage exists; actual pixel tests check all 256 interpolation samples at both 100% and 125%. The heading is unchanged throughout message transitions. No temporary text buffer, separate clear publication or extra GPU flush was added. A logical message step requests one dirty rectangle; existing compositor merging/presentation performs its ordinary coherent transaction. Existing unrelated scene/cursor updates are not redesigned or claimed to disappear.

All progression uses absolute monotonic time. A sample at 2500 ms selects the second hold; 5200 ms selects the third; 7500 ms selects completion if the desktop was already ready. No loop replays missed animation frames. Tests compare sequential jumps with direct evaluations, plus 500/1500/3000 ms stalls in the real welcome module, with at most one dirty action per sample. A stall can skip an entire visual phase; absolute-time logic does not promise reading time while QEMU/host execution is paused.

The final fade origin is the later of minimum-duration expiry and readiness, not a delayed animation sample. After mode/readiness invalidation, republished desktop readiness establishes a fresh eligible fade. Completion retains the existing whole-frame latch: partial/cursor presentations keep the preceding welcome until a full composed scene can be selected. There is no background-only intermediate handoff. Initialization and asynchronous wallpaper loading proceed underneath with no sleeps, clock-frequency changes or blocking startup wait.

## Diagnostics and manual Windows comparison

`BURSZTYNDIAGSPLASHTIMING` defaults to **0**. When enabled it logs only start, sampled message-transition/hold changes, desktop readiness, fade start/end and final removal, with monotonic `now_us`, elapsed time and the count of completed compositor scene transactions during the overlay. No per-animation-frame output occurs. Cursor-only operations are not scene transactions; the removal log reports transactions completed before the final handoff. A stalled sample logs only the reached state, not artificial replay events. Timing gaps can be assessed against the schedule and presentation counts; **no Windows gap measurements were collected here**.

To build a diagnostic ISO explicitly, use `make -B -j2 all iso BURSZTYNDIAGSPLASHTIMING=1`; `-B` ensures already-built objects receive the flag without `make clean`. The default final ISO supplied by this task has diagnostics disabled. Host diagnostic execution checked sparse event output; isolated freestanding splash/compositor objects compiled with the flag as well.

On Windows, use updated menu 1 or 2 with GTK and preserve the same disk, firmware and audio for both trials. The script prints the exact JSON argv. First boot normally; for the comparison edit that boot's GRUB `multiboot2` line and append `bursztyn.splash=0`, then boot it. No permanent splash disable or new ISO/menu policy is required. The host tests verify bypass selects the ordinary scene immediately and does not reactivate the legacy terminal. **Windows boot without splash has not been executed or verified here.**

If Windows freezes with the splash disabled too, the splash timing path is not sufficient to explain it. If only splash-enabled boot fails, collect serial timing/lifecycle and GPU messages before attributing the cause. This distinction is an investigative branch, not a root-cause declaration.

Final manual acceptance: GTK selected; guest and QEMU boot/reboot responsive; Windows 11 responsive; all three large/smooth messages readable; crossfades have no blank flashes; final fade exposes the complete desktop. Repeat the diagnostic comparison as needed; no automatic reboot stress loop is provided.

## Reboot lifecycle

The established ordering remains: acquire screen lock → stop/release splash and clear its state → quiesce compositor pending work → gate graphics presentations → release screen lock → stop GPU submissions → bounded GPU quiesce → filesystem flush → existing ACPI/PS2 power handoff. The new change is restricted to bounded, error-aware used-entry draining inside GPU reboot quiesce. Splash teardown and the recent bounded GPU lock acquisition remain. Normal GPU reset still disables scanout, detaches backing, unreferences resources, disables/reset IRQ/device state, then performs existing DMA cleanup; those operations and their order were not changed.

The combined host regression starts the real splash, injects a three-second scheduling gap, executes the shared real power-order helper with host lock/compositor hooks, and calls the real GPU quiesce. Idle success, held-lock/missing-clock failure and pending/missing-clock failure all return. Separate coverage tests invalid completion before/after scanout, valid completion, and queue-capacity exhaustion. The concurrent teardown test covers 100 generations, and the real compositor test retains its 100 quiesce/reset cycles.

## Files changed in this task

Production:

- `ekran_powitalny.h`: absolute schedule, complementary crossfade coverage, unchanged shared Unicode renderer/large geometry.
- `ekran_powitalny.cpp`: readiness timestamp/fade origin, retained-surface crossfade updates, sparse timing events.
- `ekran_powitalny_api.h`: internal optional compositor-count callback, no BWS ABI change.
- `diagnostyka_startu.h`: default-off timing macro.
- `skladacz_obrazu.cpp`: two default-off completed-scene diagnostic hooks only beyond its preexisting worktree changes.
- `virtio_gpu.cpp`: bounded, error-aware reboot drain only beyond the preexisting lock-acquisition fix.

Launcher: `runwindows.ps1` only. No Linux launcher changed.

Build: `Makefile`, adding timing diagnostic flag and host timing/launcher targets; Linux launch variables remain.

Tests added: `narzedzia/test_splash_timing.cpp`.

Tests updated: `narzedzia/test_powitanie.cpp` (new clock expectations/log fixture), `narzedzia/test_boot_graphics.cpp` (new fade times), `narzedzia/test_gpu_quiesce_host.cpp` (real splash/power/GPU integration and failing-completion probe), `narzedzia/test_windows_launcher_config.py` (all-profile explicit backend, no fallback, PCI order and Linux policy).

Report added: this file. Existing Shell, Explorer, layout, installer, font smoothing implementation, input drivers, scheduler/HPET frequencies, BSP2, PZB and BWS definitions were not edited for this task. All earlier worktree changes remain. Tracked generated host-test executables were restored to their original contents after validation; test logs preserve results.

## Automated validation

| Suite / distinct assertions or cases | Final result |
|---|---:|
| Welcome lifecycle / rendering / options; 1200 interrupted + 500 bypass/failure cycles | 13,703 / 13,703 PASS |
| New exact timing / all crossfade weights / stalls / final handoff / sparse diagnostics | 2185 / 2185 PASS |
| Concurrent splash power teardown, 100 generations | 601 / 601 PASS |
| Production GPU reboot quiesce, including combined long-gap shutdown and invalid completion | 38 / 38 PASS |
| Generic startup first-frame visibility, 100% / 125% | 86 / 86 PASS |
| Real kernel graphics / frame publication / splash presentation | 36 / 36 PASS |
| Startup/theme-order source contracts | 19 / 19 PASS |
| Welcome integration source contracts | 15 / 15 PASS |
| Reboot/typography source contracts | 7 / 7 PASS |
| Production compositor lifecycle/scene, including 100 quiesce/reset cycles | 74,290 / 74,290 PASS |
| Full VirtIO host suite, including 23 GPU command/state/resource/reset cases | 144 cases PASS, 0 FAIL |
| Windows/Linux launcher source-policy tests | 9 cases PASS |
| WM routing / Start launch | 172 / 172 PASS |
| Command binding host suite | 23 / 23 PASS |
| Timing-diagnostic freestanding compilation | PASS |
| `git diff --check` | PASS |

Compositor popup restoration's 73,816 assertions are included in its 74,290 total. Repeated startup-policy execution is reported once. Python launcher/source contracts are not PowerShell execution or guest tests. Neither `pwsh` nor `powershell` is installed here, so native PowerShell parser validation was unavailable; **no PowerShell syntax PASS is claimed**.

**ASan/UBSan: final instrumented C++ suites passed without reported memory/undefined-behavior errors.** LeakSanitizer was enabled for timing, concurrent power teardown, GPU quiesce, production compositor and full VirtIO suites, and passed. Welcome/first-frame/kernel-graphics/WM/command targets retain their existing `detect_leaks=0`. An initial graphics test still expected the old 400 ms midpoint; clock expectations were updated to 600 ms before final validation. The old invalid-completion watchdog failure is deliberate reproduction evidence, not a final suite failure.

**Final `make -j2 all iso`: PASS, exit 0.** No release-build warnings/errors. No `make rm`, `make clean`, automatic QEMU or guest reboot was used. Final ISO: 35,241,984 bytes; SHA-256 `796e3152365be55c1aed3a915d7c28154c2fce64afc9f4eeb484c0f05de41e8d`.

Logs: `/tmp/windows-splash-complete-final.log` (final welcome/timing/power/quiesce/startup results), `/tmp/windows-splash-timing-final.log`, `/tmp/windows-splash-lifecycle.log` (welcome/power final passes; superseded early graphics expectations also visible), `/tmp/windows-splash-quiesce-final.log` (final first-frame/graphics/policy), `/tmp/windows-splash-gpu-final.log`, `/tmp/windows-splash-compositor-virtio.log`, `/tmp/windows-splash-ui-checks.log`, `/tmp/windows-launcher-final.log`, `/tmp/windows-invalid-completion-before.log`, `/tmp/windows-invalid-completion-after.log`, `/tmp/windows-splash-build-final.log`. Pre-change production snapshots/probes are under `/tmp/bursztyn-windows-timing-before`.
