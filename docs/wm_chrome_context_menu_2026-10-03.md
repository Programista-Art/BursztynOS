# WM decorations and compact shared context menus

The previous Shell client theme fix remains intact. The runtime titlebar finding
exposed an ownership gap: `gui_rysuj_standardowa_belke` painted normal app-layer
pixels, whereas `menedzer_okien.cpp` handled theme events only by refreshing the
desktop/taskbar. Shell's geometry/scale cache in `shell_rysuj_terminal` deliberately
skipped the titlebar helper on theme changes. The compositor retained old pixels;
there was no per-window BursztynMotyw or decoration-color cache to update.
New windows got current colors from the GUI helper, existing Shell windows did not.

Normal titled, positive-z-order window decorations are now composed by the
kernel WM/compositor from `motyw_pobierz_aktywny`, using retained window metadata
and the system Unicode font. The GUI helper registers title metadata but does
not paint normal window chrome. Its legacy drawing path remains unchanged.
The renderer is shared (`wm_dekoracje.h`) and runs in both full and dirty-region
composition, immediately after the respective app layer so normal z-order and
occlusion remain intact. Popups and desktop overlays are excluded by checking
both the layer identity and buffer identity. Old app titlebar pixels are skipped
rather than copied, including transparent rounded corners.

`motyw_rozglos_zmiane` now directly invalidates existing WM decoration strips
before its existing process notification broadcast. No new BWS/BUR ABI is added.
The Ring 3 WM already receives the event; that event alone previously did not
refresh window decorations. Title metadata changes also invalidate decorations.
Each visible window invalidates its 28-DIP titlebar, 1-DIP side borders and bottom
border. The normal dirty-region compositor recomposes these regions with current
colors; it does not request client repaint or mutate any app pixel buffers.
Positions, dimensions, focus, z-order and minimized state are unchanged. Restored
windows resolve the current theme when composed. Shell does not paint chrome.

The menu cause was shared `WidokMenuPopup` layout: a 150-DIP minimum width,
label+accelerator measurement plus a permanent 92-DIP allowance, and labels at
row.x+45 DIP whether checkmarks/icons existed or not. There was no centering
operation; the vacant columns made the labels appear centered. The popup manager
adds an external shadow, not internal padding, and its edge clamping/hit testing
has not been changed.

Plain-menu layout now has 8-DIP left and right padding plus border (previously
45 DIP on each side, including reserved feature space). Width = 2 DIP border +
16 DIP padding + maximum measured visible nonseparator label width. There is no
150-DIP minimum. Measurements use CzcionkaUI at the supplied SkalaUI, with Unicode
and 100%/125% scaling. All labels use the same explicit left x coordinate.
Optional checkmark (18 DIP), icon (20 DIP), accelerator (12-DIP gap + maximum
accelerator width) and submenu arrow (14 DIP) columns are reserved only when a
visible row needs them. Hidden rows do not reserve width. Shell needs none of
these columns. Row height remains 24 DIP; full usable row highlighting and the
shared render/hit rectangles remain unchanged. Existing disabled logic, clipboard,
click consumption, screen-edge clamping and popup theme restyling remain intact.

Host tests exercise the production decoration renderer at both scales, theme
color changes, clipped drawing and retained client pixels. The existing Shell
harness checks compact menu widths, conditional columns, UTF-8, clipboard actions,
edge clamping and live popup theme refresh. These are host checks only. Final
visual/runtime verification is pending manual user testing; QEMU is not launched.

For the Shell labels, the longest measured text is `Usuń zaznaczone`: 102 pixels
at 100% and 131 pixels at 125%. Content popup width changes from 194 to 120 pixels
at 100%, and from 246 to 154 pixels at 125% (external shadow excluded).

Validation in this session:
- Affected compositor/theme/GUI/Shell component builds: PASS.
- Extended Shell host harness: PASS (100%, 125%).
- WM decoration renderer host checks and ASan/UBSan: PASS (100%, 125%).
- Shell ASan/UBSan: PASS, with detect_leaks=0 as in the existing harness.
- Existing test-kontrolki4 ASan/UBSan: 1379/1379 PASS (existing detect_leaks=0).
- Existing test-bursztyn-ui-polecenia ASan/UBSan: 23/23 PASS.
- Existing test-motywy-unit: 155/155 PASS.
- make -j2 all iso: PASS, exit 0; BursztynOS.iso written successfully.
- git diff --check: PASS.

Commands for the standalone new regression:
```
g++ -std=c++17 -O1 narzedzia/test_wm_dekoracje.cpp -o /tmp/test-wm-dekoracje
/tmp/test-wm-dekoracje
g++ -std=c++17 -O1 -g -fsanitize=address,undefined narzedzia/test_wm_dekoracje.cpp -o /tmp/test-wm-dekoracje-san
/tmp/test-wm-dekoracje-san
```
No runtime PASS is claimed; manual visual testing remains pending.
