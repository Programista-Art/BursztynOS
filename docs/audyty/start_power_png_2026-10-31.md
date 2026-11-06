# Start power button, PNG assets and measured popup — 2026-10-31

## Power button

The 36 × 36 logical-pixel `PrzyciskObrazkowy` moves from horizontal center to the bottom-right of Start's fixed lower region. Its right and bottom inset are both **8 logical pixels**, measured from the Start body (the manager's external shadow is excluded). The image is 22 logical pixels. The control's own rounded button bounds remain its hitbox; there is no full-width input region. Theme styling, focus order, Tab, Enter/Space and the “Zasilanie” tooltip use the existing BursztynUI button and popup infrastructure.

Search width, result width, scrollbar, top-aligned empty-result message and Start dimensions are independent of the transient power popup.

## Popup

Preferred placement aligns the popup's right body edge with the button's right edge, above the button. The popup body ends **8 logical pixels plus the 2-pixel shadow reservation** above the button. With shadows enabled the visible shadow-to-button gap is 8 logical pixels; without shadows the body gap is 10 logical pixels.

Geometry comes from the current DisplayGeometry work area, through the existing popup manager. Horizontal bounds retain 4 logical pixels of safe margin, enough for the translated shadow. If the preferred position is unavailable, placement tries below and then the nearest safe side. For a work area too small for any full placement, the available region above is clipped rather than covering the button. Start itself also handles nonzero work-area origins.

The existing 300 ms hover-open and 350 ms leave grace remain. Entering the popup cancels the leave deadline; crossing the visual gap receives the same grace. No transit surface or full-width hitbox is added.

The shared menu view measures each visible label and shortcut with BSF UTF-8 metrics. For these four actions the preferred body width is:

```
2 * scaled(1)                       border
+ 2 * scaled(8)                     horizontal padding / focus reservation
+ scaled(18) + scaled(6)            icon and icon/text gap, stored as scaled(24)
+ max(BSF_width(each visible label))
```

Actual integer rounding follows SkalaUI independently for each term. Minimum width is 150 logical pixels; maximum is work-area width minus two 4-pixel safe margins. The external shadow is reserved by placement. One-line labels exceeding the maximum are shortened at UTF-8 boundaries with an ellipsis and clipped to their text column. Row height is max(24 logical pixels, BSF font height + 8 logical pixels, PNG icon height + 8 logical pixels).

The longest bundled label is **Uruchom ponownie**. With DejaVu Sans at 100%:

| Font | BSF label width | Popup body width |
|---|---:|---:|
| 12 px | 118 px | 160 px |
| 16 px | 156 px | 198 px |
| 20 px | 194 px | 236 px |
| 24 px | 234 px | 276 px |

A live font refresh remeasures width and row height without redecoding PNGs. Theme repaint preserves the selected action and width when metrics are unchanged. Display-generation events continue closing Start and its transients; reopening derives coordinates and scale variants afresh.

## PNG loading and rendering

| Stable action | Runtime path | Fallback |
|---|---|---|
| Start power | `/ikonki/przyciskzasilania.png` | native power 102 |
| Lock, ID 22 | `/ikonki/przyciskzablokuj.png` | native lock 100 |
| Sleep, ID 23 | `/ikonki/przyciskuspij.png` | native sleep 101 |
| Shutdown, ID 19 | `/ikonki/przyciskzamknij.png` | native power 102 |
| Restart, ID 18 | `/ikonki/przyciskuruchomponownie.png` | native restart 103 |

Mapping never examines localized labels. Lock and Sleep remain disabled, and Shutdown/Restart keep the existing trusted dispatch.

The supplied files were named with underscores. Canonical files without underscores contain identical source bytes; only those five canonical names are packaged. All are RGBA PNGs, 128 × 128, totaling **38,957 bytes**.

The new shared `BursztynUI::IkonaPNG` cache calls the existing `obrazy/dekoder_obrazu.cpp` image decoder with alpha preservation. Start contains no PNG parser. The WM links a PNG-only compilation of that same decoder; other image consumers retain their existing format support.

Before decode: maximum file size **256 KiB**, maximum side **128**, maximum pixels **16,384**, complete chunk framing and CRC validation. Decoding uses a **2 MiB total live allocation budget**, including allocator headers, rather than an unlimited stb allocation callback. All size arithmetic is bounded before allocation. The returned original surface is at most 64 KiB.

There are exactly five owning cache entries in the session-long Start instance. Each entry binds one stable path and logical size, retains the original decoded asset and one current physical-size variant. Variant size derives from logical size and current UI scale. Reopening and hovering do not decode again. Scale changes regenerate directly from the original using the existing deterministic image scaler, never from a previously scaled bitmap. Failed assets are negative-cached independently for the session. Diagnostics enabled with `BURSZTYN_DIAG_PNG=1` report missing files, invalid PNGs, rejected sizes/dimensions and read/decode failures once per failed entry.

The shared source-over renderer blends straight PNG alpha over the current retained theme surface. Disabled rows multiply alpha by 96/255; originals and cached variants remain immutable. No background is flattened into the PNG, and artwork colors are preserved.

## Packaging, memory and stack

The five binary resources are embedded once in the kernel and written through the existing live/installed PSF file plan. `/ikonki` is appended to the shared bootstrap directory list. ISO staging copies only canonical filenames. [Packaging evidence](start-power-png-2026-10-31/packaging.json) compares source, extracted ISO files and the corresponding ELF load segments byte-for-byte.

Five original surfaces consume at most 327,680 bytes. Current scale variants consume 7,120 bytes at 100% or 28,480 bytes at 200%, plus 200 bytes for five cache entries and ordinary allocator overhead. Borrowed PNG references add 8 bytes per menu entry and image button (64 bytes for this menu’s eight-slot allocation and 8 bytes for the power button). The WM's fixed code segment grows by 16 KiB to 288 KiB; its BUR header and linker agree on the new data offset. No BWS ABI, permission, syscall or power-routing change is introduced.

Compiler stack-usage output: cache preparation 160 bytes; icon rendering 624 bytes including a 128-pixel scratch row; popup placement 112 bytes. The existing shared PNG decoder's largest individual frame is 4,160 bytes, with bounded nested decoder frames. These are individual frames, not a claimed whole-stack high-water measurement. Kernel built-in file installation uses a 544-byte frame. Pixel buffers and decoded images stay off the stack.

## Validation

Host ASan/UBSan ([machine-readable results](start-power-png-2026-10-31/host-results.json)):

- Native BSF / desktop / Settings / Start: **140,999 checks PASS**, including 974 dedicated PNG, fallback, alpha, cache and geometry checks. The font/scale/resolution matrix uses 12/16/20/24 px, 100/125/150/200%, 1280×720 and 1920×1080. It compares popup widths with actual BSF measurements, tests live size and family changes (including DejaVu Sans Mono), preserves Restart selection across refresh, and checks nonzero work-area origins.
- Focused PNG / BSF repeat: **975 checks PASS**, including atomic metrics artifact output.
- Theme lifecycle / Start power: **2,676 checks PASS**, including the explicit 349 ms gap transit and bounded UTF-8 label tests.
- Bounded PNG decoder: **3,446 checks PASS**, including real PNGs, partial transparency, truncation, bad CRC, overflow-sized chunks, rejected dimensions, excessive file size and every decoder allocation failure. No leaked allocations.
- CRC-valid compressed bomb fixture: **3,456 checks PASS**; 4 MiB inflated IDAT is rejected. Measured peak decoder-plus-copy heap: **1,580,080 bytes**; ordinary real icons peak at **132,272 bytes**.
- Rounded file popups: **308,494 checks PASS**.
- BursztynUI controls: **1,379/1,379 PASS**.
- BSF: **12,201 checks PASS**; BSF UI/browser: **5,876 checks PASS**.
- User fault isolation policy: **32,768 checks PASS**.
- DisplayGeometry transaction / physical edges: **304 checks PASS**; stale snapshot/generation/authorization tests PASS.
- PSF/bootstrap: **25,186,534** normal and **25,186,244** diagnostic checks PASS across 21 scenarios.
- Existing PNG decoder/scaler/alpha and image packaging scenarios A–O: PASS.
- Optimized theme-engine tests: **155/155 PASS** (this target is not sanitized).

Release QEMU:

- [Disk-only themes/search/countdown/power acceptance](start-power-png-2026-10-31/themes-installed/result.json): PASS after booting the fresh installed disk with no ISO attached. Five bundled previews and live rollback repaint, Apply without delayed rollback, Settings reopen/close rollback, mouse hover, gap transit, keyboard and outside-click dismissal. PNG cache bytes and rendered alpha/disabled pixels match the source assets for every theme.
- [Font acceptance](start-power-png-2026-10-31/fonts/result.json): PASS for 12/16/20/24 px, actual native Settings Apply, all five PNGs, disabled alpha and measured widths 160/198/236/276 px. Each transition uses a fresh Settings window and actual combo-popup bounds.

- [Resolution/scale acceptance](start-power-png-2026-10-31/scales/result.json): PASS for all eight combinations of 1280×720 / 1920×1080 and 100/125/150/200%. Every transition checks inset, gap, alignment, bounds, actual PNG cache bytes and screen alpha pixels, keyboard open/close, immediate work-area publication and cursor bounds. This focused run skips the long desktop-anchor walk; the full native desktop matrix covers anchor regressions.
- [Malformed-asset acceptance](start-power-png-2026-10-31/failures/result.json): PASS over three open/close cycles with missing Sleep, corrupted Shutdown and 129×128 Lock together. The other two PNGs remain loaded. Fallback glyphs are visible in the screenshots. The private ISO changes only resource bytes and one resource destination filename; its WM payload is byte-identical to release (`d9fd0913…`).

- [Fresh installed-file acceptance](start-power-png-2026-10-31/installed-files.json): PASS. The ordinary installer completed and verified a new disposable 4 GiB disk. All five `/ikonki/` files read directly from BSP2 match their source bytes and hashes; the sentinel disk is unchanged.

[Final acceptance manifest](start-power-png-2026-10-31/acceptance.json) and [successful-run health checks](start-power-png-2026-10-31/health.json) confirm no Ring 0 panic, unexpected Ring 3 fault or critical GPU marker in the four passing QEMU runs.

Screenshots: [Grafit](start-power-png-2026-10-31/themes-installed/power-grafit.png), [light theme](start-power-png-2026-10-31/themes-installed/power-bursztynjasny.png), [gap transit](start-power-png-2026-10-31/themes-installed/power-gap-transit.png), [24 px font](start-power-png-2026-10-31/fonts/power-font-24.png), [1920×1080 at 200%](start-power-png-2026-10-31/scales/power-scale-4.png), [fallbacks](start-power-png-2026-10-31/failures/independent-fallbacks-0.png).

## Production artifacts

```text
801e45c464b12273160f2f93d19ab844170fde4742b3a5f7b7156fde2751fa4c  system_operacyjny.bin
c056079324457db820ca83dc4a9be8e4cae633eec903fe6ed195bdbf4b51156a  BursztynOS.iso
```

## Limits

Scaling uses the existing nearest-neighbor implementation; there is no new resampler. Missing or invalid files are retried in a new WM session, not on repaint. The shared decoder retains its existing serialized allocator context. Tiny work areas unable to contain the four full rows are clipped; all requested supported geometries are checked separately. QEMU is run sequentially to avoid the previously documented parallel VirtIO-GPU timeout. The final pointer matrix uses VirtIO input with a USB keyboard. One [initial installed boot](start-power-png-2026-10-31/installed-gpu-timeout.txt) stopped on a VirtIO-GPU `TRANSFER_TO_HOST_2D` timeout before Start opened (expected response 4352, observed 0). The unchanged disk-only retry completed all five themes and power acceptance. Its underlying cause is not established; no GPU/kernel workaround was added. One preliminary USB-pointer boot timed out before entering Start. The observer was corrected to mask cursor pixels and wait for the actual submenu-open state rather than trusting stored geometry. These corrections change the test harness, not release UI code.


## Reproduction

Run the host suites before QEMU; they emit layout/BSF observer files under `.bsf-test`:

```sh
make test-settings-themes-power-host test-power-png-host test-desktop-ttf-host
python3 narzedzia/test_power_png_bomb.py
python3 narzedzia/power_icon_artifacts.py --output .bsf-test/power-package-check
python3 narzedzia/test_husarz_qemu.py --settings-themes-power-only --display-geometry-only --display-input virtio --output .bsf-test/power-themes --timeout 1500
python3 narzedzia/test_husarz_qemu.py --power-png-fonts-only --display-geometry-only --display-input virtio --output .bsf-test/power-fonts --timeout 1000
BURSZTYN_DISPLAY_SMOKE=1 python3 narzedzia/test_husarz_qemu.py --display-geometry-only --display-input virtio --output .bsf-test/power-scales --timeout 1800
python3 narzedzia/test_power_icons_installed.py
```

`BURSZTYN_POWER_PNG_ONLY=1 make test-desktop-ttf-host` runs the focused PNG/BSF/cache/geometry subset. `power_icon_artifacts.py --failures --output .bsf-test/power-fixtures` creates isolated malformed-asset media for `--power-png-failures-only`; it does not alter production artifacts.
