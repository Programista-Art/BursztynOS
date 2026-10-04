# Windows QEMU frontend policy — 2026-10-04

The user supplied a Windows 11 runtime A/B result with the same Bursztyn OS
guest and VirtIO Tablet configuration:

- **GTK PASS:** boots correctly, QEMU Virtio Tablet works, the pointer reaches
  the entire desktop, and maximizing the QEMU window does not reproduce the
  restricted pointer area.
- **SDL:** boot stops after successful VirtIO Keyboard initialization and before
  QEMU Virtio Tablet registration.

Treat SDL in this Windows QEMU build as incompatible with the current VirtIO
Tablet configuration. The exact internal QEMU SDL cause is not established.
SDL has not been fixed. These results were supplied by the user; no QEMU session
was launched during this change.

## Accepted platform policy

Windows VirtIO Full now defaults to `-display gtk` instead of `-display sdl`.
It retains one `virtio-tablet-pci`, one `virtio-keyboard-pci` and
`virtio-vga,id=gpu0,disable-legacy=on,xres=1280,yres=720`. Its accelerator is
`tcg,thread=multi` (the audited Windows launcher previously used `thread=single`).
An empty display override also resolves to GTK. Startup shows `Display: GTK`.

Menu option 11 selects GTK; option 12 selects **SDL eksperymentalny /
diagnostyczny** for subsequent VirtIO launches. `-DisplayBackend sdl` remains
available for explicit diagnostics. The current worktree had no frontend menu
selector, so these options were added without replacing boot choices.

Linux Makefile remains SDL with VirtIO Tablet and Keyboard. Legacy USB HID
configuration, including its existing single-thread TCG setting, is unchanged.
No guest input, ABS mapping, cursor/compositor arithmetic or EV_REL support was
changed. Tablet remains canonical; relative-device support remains available in
Bursztyn OS. No second pointer device or guest workaround was added.

## Validation

`python3 narzedzia/test_windows_launcher_config.py` checks the Windows default,
empty override fallback, device list, GPU dimensions, TCG policy, explicit SDL
selection, status output, Legacy USB configuration and Linux policy. These are
static configuration regressions; PowerShell is not installed in this Linux
workspace, so they do not execute the Windows launcher.

Host validation uses `make test-virtio-pci`, `make test-cursor-host`,
`make test-cursor-hover` and `make test-compositor-cursor` with ASan/UBSan,
followed by `make -j2 all iso`.

Results for this change:

- Windows launcher static configuration: **7/7 PASS**.
- VirtIO/Input/GPU host suite: **144/144 PASS**, ASan/UBSan and leak detection.
- Cursor transaction/geometry/diagnostics host suite: **PASS**, ASan/UBSan.
- Cursor hover host suite: **PASS**, ASan/UBSan, 100% and 125% scale.
- Production compositor cursor host suite: **PASS**, ASan/UBSan and leak detection.
- `make -j2 all iso`: **PASS**, generated `BursztynOS.iso`.
- `git diff --check`: **PASS**.

Changed source/documentation files: `runwindows.ps1`,
`narzedzia/test_windows_launcher_config.py`, and this report. Build products were
generated normally. No Linux launch configuration or guest driver was edited.
