# Desktop selection appearance — 2026-10-04

This change continues the current worktree and addresses selection appearance only. The user has already manually confirmed file/folder drag and drop, safe rejection of self/descendant moves, and Create Folder text-field right-click. Their production code is unchanged in this change. No QEMU session was started. Final acceptance of the new visual appearance remains manual.

## Root causes and role audit

`PULPITZAZNACZENIE` previously returned `0x20000000 | theme.tekst`; `PULPITRAMKA` returned `0x70000000 | theme.tekst`; `PULPITTEKST` returned `theme.tekst`. Bursztyn Jasny defines `tekst=#202124`, `panel=#EEECE7`, and `ramka=#C9C6BF`.

The desktop compositor genuinely composites the first two tags with alpha 32/255 (fill) and 112/255 (border). These weights were constant across themes, never zero. On a dark wallpaper pixel close to `#202124`, the fill barely changed the background: over `#101010`, the old fill became `#121212`. The old border became `#171718`, explaining the near-black contour. This was direct use of a dark text role, not a missing role, black fallback, or broken alpha path. Other themes generally have light text, so their old overlays contrasted better with dark wallpaper.

The role geometry always used the maximum label region width (`g.etykieta_szer`), even for very short labels. With padding, every selection was 118 px wide at 100% or 148 px at 125%, rather than following visible content.

## New color calculation

There are no theme-name branches or theme-specific color literals in production code.

1. Measure the relative brightness of theme panel/text RGB with integer weights `(54R + 183G + 19B) / 256`.
2. Derive a lighter and darker shade from those theme surfaces, mixing each with `theme.ramka`: weight 24/255 for the fill shade, 80/255 for the border shade.
3. `PULPITZAZNACZENIE` and `PULPITRAMKA` publish the lighter shades with their existing alpha tags. During root-desktop composition, choose the lighter shade over wallpaper darker than the midpoint of theme panel/text brightness, or the darker shade over brighter wallpaper.
4. Composite that shade with the actual background pixel using the existing alpha: 32/255 fill, 112/255 border. Apply the identical rule in full-frame and partial-region composition.

For Bursztyn Jasny:

| Wallpaper pixel | Old fill | New fill | Old border | New border |
|---|---|---|---|---|
| `#101010` | `#121212` | `#2B2B2A` | `#171718` | `#6C6B68` |
| `#F0F0F0` | `#D5D6D6` | `#D7D7D8` | `#949596` | `#ABABAB` |

The change restores contrast on dark wallpaper without a yellow block and uses frame-derived gray rather than a text-derived near-black border. Bright backgrounds receive a contrasting darker shade. `PULPITTEKST` remains the existing theme text role; label rendering/shadows are unchanged. Other layers retain their RGB semantics, exact zero remains transparent, and `0x01000000` remains visible black. No fake alpha is introduced.

## Compact geometry

`Desktop::visual` computes the union of:
- the current scaled icon rectangle;
- the centered visible label rectangle, whose width is `Label::width`, the maximum measured width of its rendered lines, and whose height includes all visible lines and line gaps.

Then it expands the union by 3 logical pixels on each side: **3 physical px at 100%, 4 physical px at 125%**. Existing Unicode font measurement supplies each line's width; the wrapper preserves UTF-8 boundaries and includes a measured ellipsis when necessary. A short label fits within the icon width (selection 54 px at 100%, 68 px at 125%); a wider label increases only that item's bounds. The union also handles integer centering of odd widths.

This same geometry already feeds selection paint, repaint damage, selection results and dragged-item outlines. It now produces compact bounds everywhere without altering those event handlers. Full grid-cell click targets, folder-drop hit-testing, group positions, filesystem move operations, safety guards, clipboard and modal routing are unchanged.

## Files changed

Production:
- `desktop_layout.h`: use actual measured label width for visual bounds.
- `motyw.h`: theme-derived light/dark selection shades and wallpaper-aware RGB mixing.
- `skladacz_obrazu.cpp`: supply the active theme once per desktop composition operation in both render paths.

Tests/build:
- `Makefile`: add sanitizer-enabled `test-desktop-selection-theme`; no QEMU configuration changes.
- `narzedzia/test_desktop_selection_theme.cpp`: 161 focused color/contrast assertions across five themes and seven grayscale background levels, custom frame role, alpha, RGB compatibility and visible-black marker.
- `narzedzia/desktop_selection_cases.h`: measured widths, short/long/Polish labels, one/multiple lines, icon/text width relationships, individual multiselection bounds, Ctrl-click, theme repaint with selection retained, comfortable click cells and compact drag outlines at 100%/125%, 1280×720/1920×1080.
- `narzedzia/test_desktop_files_host.cpp`: integrate new geometry cases with the existing desktop suite.
- `narzedzia/desktop_remaining_cases.h`: update the existing assertion to expect a frame-derived fill instead of raw text RGB.
- `narzedzia/test_compositor_cursor.cpp`: expected desktop composition now uses the active palette, matching production.
- `docs/desktop_selection_2026-10-04.md`: this report.

Existing drag-and-drop, invalid-move, group-drag, rubber-band, context-menu and Create Folder right-click tests remain in the desktop suite. No Explorer, Shell, cursor, device, scheduler, HPET, filesystem, installer, GRUB, reboot or display-policy source was edited. Test-generated tracked WM binaries are restored after validation; generated ISO and object files remain build artifacts.

## Validation

| Check | Result |
|---|---|
| Desktop host suite | 2502/2502 PASS |
| Focused selection/theme suite | 161/161 PASS, ASan/UBSan |
| Theme unit suite | 155/155 PASS |
| Theme GUI integration | 25/25 PASS |
| BursztynUI controls | 1379/1379 PASS |
| Command bindings | 23/23 PASS |
| WM routing | 152/152 PASS |
| WM decoration renderer | PASS at 100%/125% |
| Production compositor | PASS, full/partial mixing and existing scene/cursor checks |

The contrast assertions require a brightness change of at least 8/255 for fill and 16/255 for border on the tested background samples across all five system themes. This is a host-side numeric check, not a visual acceptance claim.

`make -j2 all iso`: **PASS, exit 0**, ISO generated. `git diff --check`: PASS.

ASan/UBSan reported no errors in the desktop, focused selection/theme, BursztynUI, command-binding, WM routing/decorations and compositor suites. The existing theme unit suite was also separately run with ASan/UBSan: 155/155 PASS. Leak detection followed existing targets (mostly disabled; enabled for the compositor); this is not a general no-leaks claim.

No `make rm`, `make clean`, or QEMU invocation was performed. **No runtime PASS is claimed.** Only the new visual selection appearance awaits the user's final manual runtime acceptance.

Logs (temporary host files): `/tmp/selection-desktop.log`, `/tmp/selection-theme-final.log`, `/tmp/selection-theme-unit-sanitized.log`, `/tmp/selection-ui.log`, `/tmp/selection-themes-wm.log`, `/tmp/selection-build.log`.
