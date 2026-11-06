# Desktop labels, edges, launchers and rounded dialogs — 2026-10-05

This change continues the existing desktop implementation. It does not change filesystem move APIs or their self/descendant/permission checks. New runtime visual acceptance remains manual; no QEMU was invoked.

## Selected foreground

The selected-label branch in `RysujWpisPulpitu` used `MOTYW_KOLOR_PULPIT_TEKST`. Its central GUI mapping returned `theme.tekst`, the window foreground (`#202124` in Bursztyn Jasny). Unselected wallpaper labels already used a bright foreground with a dark glyph shadow; selection incorrectly switched them to the window foreground.

The new derived central role is `MOTYW_KOLOR_PULPIT_TEKST_ZAZNACZONY` (23). `motyw_pulpit_tekst_zaznaczony` selects the brighter of theme panel/text, then computes each channel as `(channel + 3*255)/4`. For Bursztyn Jasny, panel `#EEECE7` produces **`#FAFAF9`**. This is a palette-derived wallpaper foreground, with no per-theme branch in WM. Existing glyph shadows remain. The previous selection fill/border blending is unchanged.

The existing live-theme event repaints the desktop using the current palette without resetting the entry model, selection or positions. Tests observe the actual selected-label paint color and unchanged positions/files across all five system palettes.

## Movement geometry

Previously:

- `cw = scale(120)`, `ch = scale(100)`, `x0 = scale(20)`.
- WM reserved the built-in launcher rows permanently: `y0 = scale(20) + ceil(13/cols)*ch`.
- `cols = floor((screenWidth - 2*x0)/cw)`; `rows = floor((taskbarTop-y0)/ch)`.
- Drag bounds used complete cells, including `span*ch`, within `[x0, x0+cols*cw) × [y0, y0+rows*ch)`.

This left the launcher reservation above filesystem items, leftover partial rows/columns and unused cell height/width outside the real icon/label. Label spans already used actual wrapped height, but rounding them to whole cells still reserved empty space.

Now all entries share the same layout. Initial grid placement uses `y0 = scale(3)`. Visual drag limits use the actual union of scaled icon, measured wrapped label and selection padding. For group union `B` and usable desktop `[0,W) × [0,taskbarTop)`, the single shared delta is:

- `dx ∈ [-B.x, W-B.right]`
- `dy ∈ [-B.y, taskbarTop-B.bottom]`

Grid-step snapping remains the normal group translation. Edge placement is clamped after snapping to the exact visual limit, allowing an edge residual instead of leaving an invisible strip. A collision searches for a nearby legal translation of the entire group; it never clamps members independently. Pixel offsets are stored alongside existing grid positions. Repaint, damage, hit-test and drop registration use their current positions. Actual painted content takes hit-test precedence when generous cell targets overlap near edges; cell click targets remain available.

Labels still use the Unicode font's measured longest visible wrapped line and real line height. Padding remains **3 physical px at 100%, 4 px at 125%**. At the top, this real padding may place the icon 3/4 px below a selection boundary at zero; it is not an unused grid margin. Bottom selection bounds can end exactly at taskbarTop. Tests cover short and multiline Polish labels, all four edges, whole groups, both scales and both requested resolutions.

## Shared protected launchers

Previously, built-in apps lived in a separate mutable `APLIKACJE_PULPITU` WM table and separate paint/click loops. Filesystem entries alone participated in `Desktop::Layout`. Explorer enumerated only BWS directory records, so it could not see that WM-only table.

No existing desktop shortcut/link-file abstraction was found. `desktop_entries.h` introduces the smallest shared typed representation: a catalog of 13 protected virtual launchers with display name, target BUR path, title prefix and native icon kind. Both WM and Explorer append these entries to the real Pulpit listing. WM retains loaded entries on listing failure and still exposes the protected catalog if its first filesystem listing fails, without creating any directory or files. They are not PSF files, package copies, BUR binaries or CEBULA payloads.

WM entries have a launcher identity independent of their display name. They use the existing single/Ctrl/rubber-band/Ctrl+A selection and gesture handler, measured labels, theme roles and native app icon renderer. Filesystem capacity remains 128 plus 13 launcher slots. A shared desktop click handler retains selection on the first click and opens the correct target on double-click after repositioning.

Explorer uses `EntryType::LAUNCHER`, displays **Skrót aplikacji**, and shows the target path in details. Opening calls the existing BWS program launcher, with the desktop's title-prefix policy for activating an existing window. Refresh preserves type and launcher identity even if a real file has the same name.

Filesystem-path resolution rejects launcher entries. Protected launcher/mixed-selection context menus disable filesystem mutation and clipboard operations. Command handlers enforce the same guards independently of the menu. Opening a launcher resolves its target only on the launch path. Visual drag changes only position; no installed binary is moved, renamed, deleted or copied.

**Persistence limitation:** visual positions survive directory refresh and theme repaint within the running WM, but do not persist across reboot. The launcher catalog is compiled into both views. Renaming, deleting or moving protected launchers between folders is disabled. This change adds no hidden persistence files and changes no BSP2/BUR/CEBULA format. Other filesystem consumers still enumerate actual files; these are virtual desktop entries shared by these two UI views.

## Rounded popups

`DialogPlikowy::rysuj_siebie` painted a rounded body, then called rectangular `rysuj_ramke`, placing a square contour back around every corner. Its inherited shadow used the menu radius while the body used the window radius. Popup manager scaling also converted the fractional DPI adapter to an integer before multiplying shadow offset/radius. Additionally, the shared rounded-frame renderer painted only one corner pixel per scanline, leaving gaps at tangents and where the circular boundary advanced several pixels; the folder chooser's header could paint over its top border.

`PowierzchniaPopup` now provides shared rounded body/frame rendering and a shape predicate. Body, border, hit classification and translated shadow use the same radius and proper fractional DPI multiplication. The rounded frame follows the outer rounded shape minus the inset inner shape; the chooser paints its border after the header. File-operation dialogs and `OknoDialogowe` reuse the same helpers.

The existing popup surface is cleared to **exact zero** before every complete repaint. Outside both body and translated rounded shadow, pixels remain zero, which the existing compositor treats as transparent. Body/frame/shadow use visible opaque RGB markers, including when a theme defines black. No fractional alpha behavior was invented and no compositor/device production code was changed. Modal routing consumes clicks outside the rounded body too, so dialog content cannot receive click-through events. Right-click handling remains unchanged and preserves the modal text field/focus.

The production compositor test verifies that transparent corners show the original scene, replacing old popup pixels with zero restores that scene, and closing restores the full region, at both scales/resolutions.

## Validation

| Check | Final result |
|---|---:|
| Desktop production suite, including launcher/edge/foreground cases | **4644/4644 PASS** |
| Explorer production suite, including virtual launcher cases | **12,496/12,496 PASS** |
| BursztynUI controls/dialogs | **1379/1379 PASS** |
| Rounded file popups: independent corner/body/shadow/border classification | **308,494/308,494 PASS** |
| WM routing, including all launcher targets | **164/164 PASS** |
| WM decorations | **PASS at 100%/125%** |
| Theme unit | **155/155 PASS** |
| Focused selection/theme suite | **161/161 PASS** |
| Theme GUI integration | **25/25 PASS** |
| Command bindings | **23/23 PASS** |
| Production compositor popup background restoration | **73,816/73,816 PASS**, plus existing compositor cases |
| Windows GTK / Linux SDL static launcher policy | **7/7 PASS** |
| `git diff --check` | **PASS** |
| `make -j2 all iso` | **PASS, exit 0**, final ISO generated |

The full current desktop, Explorer and BursztynUI suites were run with GCC at `-O0 -g0 -fsanitize=address,undefined`; the new popup suite and existing routing, decorations, binding and selection/theme targets used their Makefile sanitizer configurations. The theme unit suite was also run separately with ASan/UBSan. **No ASan/UBSan reports occurred.** Leak detection was disabled for the usual UI suites and enabled for the compositor; this is not a blanket leak-free claim.

Launcher tests cover all 13 catalog entries in both WM and Explorer, Ctrl selection with filesystem entries, rubber-band/Ctrl+A selection, launcher-only and mixed group gestures, double-click after repositioning, activation of a running target, same-name filesystem/launcher identities, protected mutation/clipboard rejection, unchanged binary/file maps, and initial directory-list failure. Existing RAM filesystem move/permission/self/descendant, clipboard, multiselection/group, menu and modal-right-click cases remain. No standalone PSF/BSP2 suite was needed: virtual entries do not introduce any disk mutation or storage-format integration.

The old rubber-band fixture terminated on the following row's selection padding. With launchers now selectable, that correctly selected additional entries. The fixture now defines its intended two-file layout and ends inside their actual visual bounds. Explorer assertion totals count individual captured paint operations as well; fixing the rounded-frame rasterizer changes that total while preserving the complete test cases.

The first build attempt exposed an incorrect prerequisite order while adding the new header: `$<` selected the header instead of the Explorer source. Source-first ordering was restored, the normal dependency rebuild replaced the malformed generated object, and the final incremental `make -j2 all iso` completed successfully. No `make rm`, `make clean` or QEMU invocation was used. The obsolete unused built-in-label truncation helper was removed; the final build emitted no warnings.

## Files changed

Production:

- `desktop_entries.h` — new shared protected virtual launcher catalog.
- `desktop_layout.h` — actual visual group bounds, pixel position offsets, edge snapping and whole-group collision placement.
- `menedzer_okien.cpp` — shared launcher entries in the existing desktop interaction/paint/launch paths, protected filesystem guards, selected foreground role, common double-click handler, removal of obsolete separate-label truncation.
- `motyw.h` — derived selected-desktop-text role and palette-derived bright foreground, without changing stored theme ABI.
- `bursztyn_gui.cpp` — central role lookup.
- `plik_popup_ui.h` — protected shortcut menu policy and shared rounded file dialog rendering.
- `bursztyn_ui/popup.h` — shared rounded body/frame/shape helpers, consistent shadow geometry and fractional DPI scaling.
- `bursztyn_ui/rysowanie.h` — continuous rounded border rasterization.
- `bursztyn_ui/okno_dialogowe.h` — shared rendering, with the border painted after the header.
- `programy/eksplorator/eksplorator-plikow.cpp` — typed launcher enumeration, display/open/activation, identity preservation and protection.

Build:

- `Makefile` — shared catalog dependencies and `test-file-popup-host`.

Tests:

- `narzedzia/desktop_launcher_edge_cases.h` — new focused desktop cases.
- `narzedzia/explorer_launcher_cases.h` — new shared-launcher model/open/protection cases.
- `narzedzia/test_file_popup_host.cpp` — new independent rounded-popup pixel classification and modal input cases.
- `narzedzia/test_compositor_cursor.cpp` — production popup opening/redraw/closing background restoration.
- `narzedzia/test_desktop_files_host.cpp` and `narzedzia/test_explorer_host.cpp` — integrate new cases.
- `narzedzia/desktop_usability_cases.h`, `narzedzia/desktop_selection_cases.h`, `narzedzia/desktop_remaining_cases.h` — retain existing filesystem/selection cases with shared launcher counts and actual visual geometry.
- `narzedzia/test_wm_click_routing.cpp` — exercise the shared catalog instead of the obsolete fixed icon table.
- `narzedzia/bursztyn_ui_host_fixture.h` — observe role lookup, popup pixels/registration and existing-window activation.
- `narzedzia/file_operations_host_fixture.h` — observe BUR launches in the RAM fixture.
- `docs/desktop_launchers_edges_dialogs_2026-10-05.md` — this report.

Generated ISO/object/stack-usage files are build artifacts. Test-generated tracked WM binaries were restored. No Shell, cursor, VirtIO, scheduler, HPET, installer, GRUB, reboot, PSF/BSP2 or Windows/Linux launcher-policy production source was changed.

Logs: `/tmp/desktop-final.log`, `/tmp/explorer-sanitized-final.log`, `/tmp/ui-sanitized-final.log`, `/tmp/popup-validation.log`, `/tmp/new-routing-theme.log`, `/tmp/new-theme-sanitized.log`, `/tmp/theme-gui-final.log`, `/tmp/command-bindings-final.log`, `/tmp/new-compositor.log`, `/tmp/desktop-launcher-policy.log`, `/tmp/desktop-build-accepted.log`.

**No runtime PASS is claimed.** Final visual/runtime acceptance remains manual for selected label contrast, usable-edge placement, movable launchers, Explorer Pulpit consistency and rounded popup appearance.
