# Desktop grid / font family regression

The large jump had a direct origin bug: startup used `scale(3)`, while the live
font/theme path reset the origin to `scale(140)`. This shifted the grid by 137
logical units. Independently, visual-label collision checks could turn a wider
face's extra wrapped line into another whole-row relocation. The fix removes
both causes and stores font-independent placement anchors.

## Root cause recorded before implementation

`Layout::geometry` sets column pitch to `scale(120)` and row pitch to
`scale(100)`. Images are `scale(48)` square, centered horizontally; label top
is cell Y + image height + `scale(6)`, label width `scale(112)`.
`pulpit_metryki` exposes a nominal label region of `scale(34)` but the WM
ignores that region when wrapping: max lines is calculated from the whole
work-area row budget, capped at three. Actual label height is
`count * (BSF face line height + scale(2)) - scale(2)`.
WM computes `span = ceil((image height + gap + actual label height +
scale(6)) / scale(100))`. More critically, `reflow` compares actual visual
bounds, including the measured label, and moves colliding entries to nearest
free grid candidates. Mono's different advances can add a line, overflowing
one 100-pixel row. A family update calls this same reflow, causing whole-row
jumps; literal pitch does not change, occupied rows do.

Persistence stores actual visual rectangle top-left in pixels plus DPI,
then reconstructs the cell origin by subtracting the new face's visual offset.
This also makes horizontal anchors depend on the family at restore time.
Manual positions are represented internally as column/row plus pixel offsets.

Further event-path audit found a second, direct origin bug before its fix:
`metryki_ekranu()` resets `PULPIT_GRID_Y` to `scale(140)`, whereas startup's
`rozmiesc_ikony_ekranu()` overrides it to `scale(3)`. The live font/theme event
calls the former followed directly by `aktualizuj_layout_pulpitu()`, skipping
the override. Thus the live update changes the grid origin by 137 logical
pixels even without any metrics change. This is the direct large displacement
in the reported transition; visual collisions then compound it. Both paths
must use the same 3-unit safe top margin.

## Implemented geometry and update policy

The direct origin error is fixed: all startup, font/theme and display paths use
`y0 = scale(3)`. Placement metrics are explicit in `PulpitMetryki` and shared
by `Layout::geometry`, visual geometry and the WM. For configured logical size
S and integer DPI scaling function U:

- image: U(48) square; column pitch U(120); label width U(112);
- logical line budget: ceil(5*S/4), independent of the selected face;
- label region: 3*U(ceil(5*S/4)) + 2*U(2);
- row pitch: U(48) + U(6) + label region + 2*U(3);
- image X: cell X + (column pitch - image width)/2; image Y: cell Y;
- label top: image Y + image height + U(6); maximum three lines;
- available lines: min(3, floor((label region + U(2)) /
  (actual BSF line height + U(2)))).

At 100%, 12/16/20/24 px reserve 109/124/139/154 pixels per row. At 16 px,
100/125/150/200% give row pitches 124/157/187/248 pixels. Each scaled component
is summed after rounding, including both padding sides, preventing the previous
one-pixel overlap at fractional scale. Column pitch remains 120/150/180/240.
The logical budget is space allocation, never a substitute for actual font
metrics: real advances drive wrapping; the existing BSF renderer places its
baseline using the real ascent. Glyphs are neither stretched nor resampled.
Unusually tall metrics reduce available lines locally; visual rectangles are
bounded by the reserved region. A single over-wide glyph produces an ellipsis.

Placement/collision checks use a fixed reserved footprint (icon plus full label
budget and padding), independent of text width/line count. Paint/selection/hit
rectangles still use the icon and actual visible text union with small padding.
A long label cannot reserve multiple desktop rows (`span` is one). Auto placement
and edge repair use the same reserved footprint, so labels have room to rewrap.
Rows/columns and edge candidates remain bounded by the work area above taskbar.

Family/theme events recompute labels and repaint, retaining every placed
anchor, grid coordinate and manual offset. They neither invoke reflow for a
fully placed unchanged geometry nor mark placement settings dirty. A configured
size change changes logical pitch deterministically; scale/work-area changes
also permit bounded collision repair. Existing valid anchors win before new
or invalid items are placed. Waiting items can be admitted on a model update.

## Persistence and compatibility

Internal positions remain grid row/column plus pixel offsets. New saved files
retain the existing checked text format, adding `anchors=1` and saving cell
anchor pixels plus DPI, rather than text-dependent visual top-left. Signed
anchor coordinates are bounded: an anchor can be slightly negative at the left
edge while its centered image and reserved footprint are entirely visible.
Legacy visual-coordinate files are read with their original interpretation and
migrated once on load. Startup writes that conversion before any family update;
a family-only event does not save shifted positions. Saved anchors restore
without consulting new label width, including manual offsets.

Legacy files did not record their font family or original anchor, so migration
cannot reconstruct positions from before an already persisted regression. It
retains/revalidates the placement represented by the old file; it does not guess
an earlier arrangement. The larger reserved three-line cells can require a
one-time collision repair for genuinely overlapping old placements. Edge dragging
now reserves future label space rather than pushing the short current label
flush against the edge. Hit testing remains limited to visible icon/text bounds.

## Rendering preservation

The selection backdrop/border, icon, and foreground label ordering is unchanged.
Selected labels still omit shadows; unselected labels retain one alpha shadow.
The glyph cache remains an alpha-mask cache. Font updates use the existing full
layer repaint, clearing old glyph/selection pixels before drawing new metrics;
selection-only dirty updates still use actual visual rectangles.

## Host validation

Sanitizer runs (ASan/UBSan) cover real BSF font loading and the actual WM font
update path, not a duplicate of its layout formula. At each of 12/16/20/24 px
and 100/125/150/200%, the test repeatedly selects all four bundled faces
(Sans, Sans Bold, Sans Mono, Serif Condensed Italic), compares exact cell/image
anchors and pitches, bounds wrapped text, compares serialized placement bytes,
and reconstructs the model from the save under each new face. This includes
Polish/long labels, `gg`, `gggggg`, a manual off-grid/edge placement, negative
cell-anchor persistence, legacy migration and an over-wide glyph ellipsis.

| Suite | Result |
| --- | --- |
| Desktop TTF / Settings / Start, including family invariance | 123,378 checks PASS |
| Desktop files, launcher/file selection, drag, placement persistence | 86,673 checks PASS |
| Production PSF/BWS placement-file authorization | 197 checks PASS |
| BSF parser/cache/raster | 12,201 checks PASS |
| BSF UI/browser, fallback, preview, font persistence | 5,876 checks PASS |
| Desktop selection/theme | 165 checks PASS |
| Window-manager click routing and Start input | 214 checks PASS |
| Explorer/shared file behavior | 15,546 checks PASS |
| Keyboard/TEXTINPUT/dialogs | 7,291 checks PASS |
| BursztynUI commands | 23 checks PASS |
| Husarz CSS | 362 checks PASS |
| User fault policy | 32,768 checks PASS |
| ISO fonts/licenses/Settings payload | PASS |

The monolithic desktop-file suite was built with `-O0 -g0` and ASan/UBSan;
this does not remove sanitizer instrumentation. The fixed reserved cell reduces
1280×720 capacity to 50 entries at 100% and 32 at 125%; overflow stays in the
filesystem and the bounded waiting model. Previously measured short labels
could pack extra partial cells, making capacity and placement face-dependent.
Logs and artifact hashes are indexed by `.bsf-test/grid-evidence/evidence.json`.

Desktop geometry/capacity/persistence fuzz additionally passes 837,103 checks
across 600 deterministic layouts with bounded candidate searches.

## Ordinary release QEMU: exact coordinate comparison

The final kernel is `0555c8f0ae4f3a994a4f037fa3d9af1be5491d6dcaff4f2a1d2d5eb087e02dac`.
The final ISO is `68fec9a12ad4f9009e86d2df15706b614e628940579eb8cef4829c4a66e70e2b`.
No profiling or development instrumentation is enabled.

`.bsf-test/grid-production-qemu/result.json` records PASS for five live
transitions at 16 px: Sans → Mono → Sans → Mono → Sans → Mono.
`all-icon-anchors.json` independently searches the full desktop image for
static image strips of **all 21 actual icons** (all 16 launchers plus five
user file/folder entries), including the third row. Every X/Y anchor is
identical in all five post-Apply frames. The first three row anchors are
Y=3,127,251; columns start X=56 and advance 120 image pixels. User-created
folders were entered through the ordinary desktop context menu. This compares
image positions programmatically; label/glyph changes remain allowed.

The screenshot observer waits for the full queued repaint after Apply, avoiding
partial frames. It excludes embedded font glyphs from icon matching so changing
those glyphs is not misreported as an image-anchor change. The independent
verifier uses ten image rows, including Explorer's folder outline.

`.bsf-test/grid-production-apps-qemu-r2/result.json` also records PASS on the
same final kernel/ISO: real TTF alpha and web author colors, local preview,
Apply 24 px, live browser/Notepad/Calculator/Explorer repaint, and large-font
Start. No kernel panic or unexpected Ring 3 fault occurred. The native-app
harness now waits for queued browser activation before sending its address
shortcut; an earlier run typed while Explorer still held focus and timed out
without issuing an HTTP request. That failed observation is not PASS evidence.

## Final disk-only persistence acceptance

`.bsf-test/grid-installed-final-7htt1sb3/result.json` records PASS for live
family changes and a second, disk-only boot with Mono 16 still selected. Both
boots use the exact final kernel hash above. The independent screenshot verifier
compares **all 23 actual icons** across five live transitions and the reboot
frame; `apply/all-icon-anchors.json` records `reboot_verified=true`. Native
`/ustawienia/uklad-pulpitu.cfg` contains `anchors=1`. Readback confirms the WM
payload and Settings exactly match the final build.

The fixture is a private copy of an already completed ordinary installation,
not a user disk. Before either acceptance boot, `prepare_desktop_grid_fixture.py`
updates the copied EFI kernel and the existing WM/Settings files to final bytes,
verifies readback and preserves their existing direct-block allocation and PZB
permissions. Only this copied fixture's preferences are initialized to Sans 16
before the first boot. No offline preference or position changes occur between
live transitions and the restart. The original installation is retained.
All font/license assets are verified byte-for-byte. The sentinel image is
unchanged. This validates final desktop persistence; it is not a new installer
acceptance claim.

## Reproduction and scope limits

```sh
make -j4 iso
make test-desktop-ttf-host test-desktop-capacity-host test-desktop-settings-host
make test-desktop-files-host test-desktop-selection-theme test-wm-click-routing
make test-bsf-host test-bsf-ui-host test-explorer-host test-gui-text-input-host
make test-bursztyn-ui-polecenia test-husarz-css-host test-user-fault-policy
python3 narzedzia/verify_bsf_iso.py BursztynOS.iso
python3 narzedzia/test_husarz_qemu.py --desktop-grid-only --output .bsf-test/grid-production-qemu --timeout 1800
python3 narzedzia/verify_desktop_grid_screenshots.py .bsf-test/grid-production-qemu
python3 narzedzia/test_husarz_qemu.py --fonts-only --native-font-apps --output .bsf-test/grid-production-apps-qemu-r2 --timeout 2400
python3 narzedzia/test_bsf_installed_qemu.py --resume .bsf-test/grid-installed-final-7htt1sb3 --desktop-grid --refresh-grid-payloads --reset-grid-preferences
python3 narzedzia/verify_desktop_grid_screenshots.py .bsf-test/grid-installed-final-7htt1sb3/apply --restart .bsf-test/grid-installed-final-7htt1sb3/restart
```

Family and 12–24 px / 100–200% combinations are covered on the host. Native
coordinate comparison is at 16 px / 100%, 1280×720, OVMF, VirtIO VGA and one
TCG vCPU; native size changes additionally cover 24 px. No real-hardware claim
is made. Extreme metrics can reduce visible lines; labels never enlarge grid
ownership. Previously persisted erroneous positions cannot be inferred back to
an unknown earlier layout. The reserved three-line policy intentionally gives
short labels the same placement footprint as long ones.

Python syntax checks and `git diff --check` pass. Final test logs, payload hashes,
QEMU results and coordinate artifacts are indexed by
`.bsf-test/grid-evidence/evidence.json`.
