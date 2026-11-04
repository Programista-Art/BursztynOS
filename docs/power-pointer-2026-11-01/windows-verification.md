# Windows frontend verification still required

This is a measurement procedure, not a claim that Windows was reproduced on Linux. Keep the QEMU binary/version, Windows DPI and frontend identical to the failing setup. The launcher default remains GTK + relative VirtIO mouse. Do not change the frontend and pointing device together.

1. From the project directory in PowerShell, capture the actual installation:

   ```powershell
   New-Item -ItemType Directory -Force .\logi_qemu | Out-Null
   .\runwindows.ps1 -Audit > .\logi_qemu\windows-audit.json
   ```

   Create `logi_qemu` first if absent. `-Audit` queries QEMU version, machines, properties and final argv without starting a VM. The audit's version is the Windows binary, unlike the Linux QEMU version in our report.

2. Boot the provided diagnostic ISO, selecting the same firmware/profile that reproduces the problem:

   ```powershell
   .\runwindows.ps1 -Iso .\BursztynOS-pointer-window-diag.iso -PointerDiagnostics -QmpPort 4444
   ```

   Select option 2 for normal UEFI VirtIO. The launcher prints the serial log filename. QMP listens only on localhost, only when explicitly requested. The disk remains the launcher's normal disk; use the project's normal live boot without invoking the installer. To protect an existing disk from live-system changes, use a private copy of the launcher/project with a disposable `wirtualny_dysk.img`.

3. In a second terminal, collect 30 seconds for each state. Use the diagnostic symbols shipped with this exact ISO:

   ```powershell
   py .\narzedzia\qemu_pointer_probe.py --symbols .\docs\power-pointer-2026-11-01\pointer-diag-symbols.json --pid (Get-Process qemu-system-x86_64).Id --port 4444 --stage windowed-small --seconds 30 --serial-log .\logi_qemu\ACTUAL_SERIAL_FILENAME.log --output .\logi_qemu\windowed-small.jsonl
   ```

   Move the pointer to **top-left, top-right, bottom-left, bottom-right, center** of the visible guest drawable. Sweep across the top edge. Keep the guest resolution unchanged. Repeat with `--stage windowed-large`, `maximized`, `fullscreen`, `returned-windowed` and a second small window. Use the fullscreen action that reproduces the issue and record its exact shortcut. The audited upstream GTK 8.2.2 shortcut is Ctrl+Alt+F; the reported Ctrl+Alt+H should be confirmed against the actual Windows build.

4. Record the Windows desktop scale (100/125/150%) separately from the guest UI scale. Repeat where feasible. The observer records per-window DPI, outer bounds, client origin/size, host cursor, actual Win32 capture window and cursor clipping rectangle, guest physical cursor and committed DisplayGeometry. GTK's Windows client rectangle can include frontend chrome; it is **not** labeled as the guest drawable. This observer never subtracts that chrome from guest coordinates, moves the host pointer, or changes QEMU's DPI awareness. Its DPI-awareness request affects only its own measurement thread/process, and the JSON records whether the override succeeded.

5. Preserve the raw serial log. `[ABS MAP] mode=REL` means `raw` contains signed deltas; `xrange=0,0 yrange=0,0` means no absolute range applies. `[ABS MAP] mode=ABS` includes raw values and descriptor ranges. Both contain mapped coordinates, physical screen dimensions, generation, interval raw extrema and session mapped Y extrema. Samples are aggregated at most once per second per active device and emitted only after input. The JSONL summary independently records the minimum guest Y observed during that stage; the required value is **0**. For zero-motion corners, QMP continues observing without requiring another report.

6. Without changing GTK, repeat launches with one explicit diagnostic device at a time:

   ```powershell
   .\runwindows.ps1 -Iso .\BursztynOS-pointer-window-diag.iso -PointerDevice virtio-tablet -PointerDiagnostics -QmpPort 4444
   .\runwindows.ps1 -Iso .\BursztynOS-pointer-window-diag.iso -PointerDevice usb-tablet -PointerDiagnostics -QmpPort 4444
   .\runwindows.ps1 -Iso .\BursztynOS-pointer-window-diag.iso -PointerDevice ps2 -PointerDiagnostics -QmpPort 4444
   ```

   These are opt-in comparisons, not verified Windows fixes. PS/2 uses the existing i8042. The tablet variants remove the canonical relative device; USB adds xHCI only for a VirtIO profile that lacks it. The canonical storage/GPU/keyboard remain unchanged. `query-mice` in each JSONL records which QEMU handler is current.

Interpretation:

- Full raw ABS endpoints with wrong mapped endpoints would implicate guest mapping. The current mapping is covered by sanitizer tests across signed 32-bit extremes and all requested physical sizes/scales.
- Contracted ABS raw Y only in the failing windowed state would implicate a transformation before the guest. Do not compensate using a guest Y offset.
- In the default **relative** profile, compare whether negative Y deltas cease when the host hits a boundary or grab changes. Absolute normalization is not active in that profile.
- Equal guest geometry/generation/ranges/source before and after fullscreen, with a changed incoming stream, places the difference before guest mapping. A Windows frontend cause still requires this actual Windows trace; the Linux tests cannot establish it.

The observer intentionally reports unavailable Windows fields when run on Linux. It does not fabricate host DPI, window state or a Windows success result. The symbols manifest contains the matching kernel hash. Booting a different kernel invalidates address-based observations.

Optional capture diagnostic: if relative motion stops after returning to windowed mode, record a separate stage before and after releasing/reacquiring QEMU mouse capture using its Grab Input action (upstream GTK: Ctrl+Alt+G, then click the drawable). Do not merge that intervention into an ordinary acceptance PASS. On the Linux/XTest comparison this restored the incoming delta stream without changing guest geometry. Whether it does so on Windows remains unmeasured.
