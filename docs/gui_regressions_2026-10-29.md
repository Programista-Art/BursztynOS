# GUI regression investigation

## Before implementation

Selection: `obsluz_gest_pulpitu(MouseMove)` calls `odtworz_obrys` before
calculating the next selection. Each of four border restorations calls
`odrysuj_obszar_pulpitu`, which clears a strip in the live desktop layer, then
paints **whole** intersecting icons without a strip clip. For a selected icon,
that includes painting its selection backdrop over the entire label, followed
by separate BSF text calls. The live surface therefore contains a temporarily
blank label between those calls. Previously queued scene damage can compose
that live surface even before the final BWS17. Border crossings can reconstruct
the same label four times although its selected bit has not changed. This is
not a glyph-cache invalidation. The previous selection snapshot already exists
and logical selection is diffed; the border-restoration route defeats that
optimization.

The initial audit also considered repeated alpha overdraw outside the cleared
strip. Follow-up testing separates that possibility from the demonstrated
cause: the selected backdrop itself removes text, so alpha accumulation is not
needed to explain this regression.
Display: `skladacz_przygotuj_ekran` copies every old root-layer pixel to the same
X/Y in the new root surface. This includes the old taskbar strip. The display
service presents that surface before notifying the WM. Consequently even a
correct new DisplayGeometry describes a frame containing old taskbar pixels at
the old Y (or a cropped bar when shrinking). The WM's subsequent full repaint
clears and reconstructs that surface. The defect precedes asynchronous client
repaints and does not require a wrong DisplayGeometry value.

Slider: `Suwak` initializes its three paint colors to zero. Only
`zaktualizuj_motyw` assigns the theme roles; creation/attachment of a control
does not invoke that method. The demo supplies no custom slider colors. Thus
the initial enabled track, active track and thumb write transparent zero into
the off-screen dirty surface. Okno submits this through a zero-skipping blit,
retaining the previous published pixels in precisely those shape fragments.
Hover/pressed thumb painting uses a nonzero theme role, so states take different
paths and leave irregular retained pieces. Rectangle painting in the software
context uses assignment, not alpha source-over. The proposed repeated-alpha
explanation is not the cause in this path. These are uninitialized theme colors
combined with zero-skipping publication.

The source audit above was recorded before behavior edits. Follow-up runtime
evidence and validation are below.

## Desktop selection: final update order

The handler computes the next band and the selection-bit diff first. The
existing snapshot is retained; an icon is not deselected/reselected merely
because the border crosses its text. Damage consists of the old four border
strips, the new four strips, and the compact visual bounds of changed icons.
Every region is clipped to the work area and split only for BWS57's 2048-pixel
per-region bound.

`DesktopSelectionPaint` reuses bounded patch storage. Each patch starts
transparent, receives the complete underlying icons clipped to that patch,
then the final band border. One screen-locked BWS57 batch replaces all patches,
including transparent pixels. The compositor cannot sample a clear-before-text
stage. This is retained off-screen restoration, not a new full-screen overlay
or a second desktop-sized surface. The rectangle has an opaque border and no
translucent interior, so repeated fill blending is not involved.

The old generic damage merger joined intersecting border strips into their
bounding box. Batch damage now uses an exact-area merger. The bounded damage
capacity is 256; the unchanged generic repaint policy remains available for
ordinary window damage. The compositor owns its reusable damage snapshot
under its existing composition guard before consuming pending work.

`BSF::statistics().rasterizations` and system generation remain unchanged
during the warmed drag tests. Stored label metrics are byte-identical before
and after each movement. Selected labels retain no shadow; unselected labels
retain the single `0x80101010` shadow. No face reload, font invalidation,
refresh-rate change, motion throttle, or AA change was introduced.

The affected set in the old path was **icons whose visual bounds intersected
the restored border**, plus icons whose selection state changed. An icon's
entire label could disappear even when only its icon silhouette intersected
that border. Unrelated desktop labels were not globally invalidated.

The production handler from the original HEAD was compiled against the same
native BSF fixture. Sampling its live surface at valid drawing-syscall
boundaries detected **37 blank-label intermediate states** at Sans 12 px when
the band reached Y=72. The corrected ASan/UBSan handler passes 1022 checks with
no such intermediate writes. See [original evidence](gui-regressions-2026-10-29/selection-before-interleave.log)
and [corrected evidence](gui-regressions-2026-10-29/selection-final-interleave.log).
This is a deterministic interleaving test, not a claim that 37 blank QEMU frames
were captured. Whole-image equality also checks the final retained result;
minor differences between direct-run and software-target AA rounding are not
used as the proof of the flicker cause.

QEMU exercised the real press-on-empty-desktop, held-button drag, repeated
label crossings, and release path. All 51 sampled final frames retain visible
label ink. The baseline sampled sequence did not catch a wholly blank label;
the interleaving test above supplies that causal evidence. Host drag tests use
Sans and Sans Mono at 12/16/24 px, compare every retained pixel with a clean
reference, and preserve the same alpha surface over light or dark wallpaper.
The QEMU wallpaper itself includes light and dark regions. Consecutive raw
frames: [0](gui-regressions-2026-10-29/selection-0.png),
[1](gui-regressions-2026-10-29/selection-1.png),
[2](gui-regressions-2026-10-29/selection-2.png).

## Taskbar: committed geometry and retained pixels

The wrong component was the compositor's **prepared root surface**, not a
timer changing the canonical rectangle. The original first 1920×1080 frame
contained the old bar at Y=680..719 while the committed taskbar rectangle was
Y=1030, height=50. [Original first frame](gui-regressions-2026-10-29/taskbar-before.png).

The display service now constructs the next canonical DisplayGeometry before
preparing layers. Layer preparation receives that snapshot. Desktop pixels
retain their physical positions until WM reflow, but the old taskbar strip is
excluded from the work-area copy and immediately projected into the new
canonical taskbar rectangle. This uses the replacement root allocation that
already existed. Rollback restores the pinned original surface.

WM stores the complete snapshot, takes bar height/Y from it, and paints the bar
as one retained patch transaction. A full desktop clear clears only the work
area; it leaves the already-correct bar intact until the complete replacement
arrives. Menu-button and clock patch submissions carry the display generation,
so queued old-geometry work is rejected rather than painted at an old Y.
The atomic clock surface remains in use.

The old area becomes transparent root content. Normal composition restores
wallpaper and layers in their existing order; no stale old strip is copied
into the enlarged work area. Display transactions already damage the whole
new scene, so both visible old/new areas are covered. Client repainting remains
asynchronous, independently of the taskbar's immediate correct geometry.

QEMU PASS: 13 resolution/scale transitions with 20 icons and exact canonical
anchor comparisons, including 1280×720/100% → 1920×1080/125% → 1280×720/100%,
125/150/200% round trips, and 1280×720/125%. Display Settings was dragged near
the old lower work-area edge before Apply. All 26 recorded commit/WM frames
have `taskbar.y + taskbar.height == screen.height`; the bottom bar pixels are
checked too. Every observed WM-stage window fits inside the committed work
area. Pointer checks reach all four corners and the bottom midpoint after
each change. Generations are 2 through 14.

Evidence: [lower window](gui-regressions-2026-10-29/settings-near-bottom.png),
[first committed frame](gui-regressions-2026-10-29/taskbar-first-commit.png),
[first observed WM stage](gui-regressions-2026-10-29/taskbar-first-wm.png),
[frame geometry](gui-regressions-2026-10-29/display-first-frames.json),
[complete matrix](gui-regressions-2026-10-29/display-result.json).
The host test independently checks the real prepared root and first modeled
GPU scanout before any WM event, timer, or acknowledgement: 304 checks PASS,
including shrink, scale-only transitions, input edges, and rollback.

The harness confirms through the native Enter action immediately after
observing Apply. Waiting only for the client's new outer size and then
clicking a predicted button location proved insufficient to establish that
the asynchronous button layout was ready; those earlier attempts rolled back
and were not counted as PASS. No guest confirmation timeout was extended.

## Slider: deterministic colors

Creation now initializes track, active track and thumb from the same roles
used by live theme updates: PANEL, AKCENT_AKTYWNY, and AKCENT. Hover/pressed use
AKCENT_NAJECHANY; disabled uses WYLACZONY. Border/focus continue to use RAMKA
and the existing accent focus role. No hardcoded amber was added.

Each paint writes a deterministic opaque control background, then the complete
base track, the active color through a clip of that same rounded track, the
thumb, border, and focus indicator. Short active ranges cannot produce square
pixels outside the rounded capsule. Value-to-thumb positioning remains derived
from the logical value and current DPI metrics. The existing bounded dirty
area covers old/new thumb, active range and optional value caption; parent
dirty traversal still restores surrounding content. No heap allocation occurs
inside slider paint. The inset focus rectangle is intentional, remains
visible, and has no accumulating alpha operation.

Pixel tests PASS for 0/1/25/50/75/99/100, normal/hover/pressed/focus/disabled,
all five bundled themes, and 100/125/150/200%. For every combination, 100
unchanged value-50 paints exactly equal the first image; 50→60→50 exactly
restores it. The native suite repeats the matrix at system font sizes
12/16/20/24 and compares geometry/image signatures across those sizes.

QEMU PASS in the actual component demo, using mouse capture and keyboard
focus. Observed values: 23, 50, 77, 50, 23, 50, followed by a keyboard update.
The track, active segment and thumb stay clean. [Mouse frame](gui-regressions-2026-10-29/slider-drag.png),
[keyboard frame](gui-regressions-2026-10-29/slider-keyboard.png),
[combined selection/demo result](gui-regressions-2026-10-29/selection-slider-result.json).

## Shared primitive, bounds, and memory

BWS57 adds mode 4 for checked, generation-qualified region replacements.
Modes 0..3 and all existing syscall numbers are preserved. The request is
16 bytes; each descriptor is 24 bytes. A batch has at most 192 regions,
2048×2048 pixels per region, and 16,777,216 total pixels. All user mappings,
destinations, and generation are checked before any destination write.
Descriptors use 4608 bytes of bounded kernel BSS; no kernel allocation or
user-sized stack array is needed. Invalid/stale batches change no pixels.

Each reused WM painter has 4648 bytes of metadata and one heap drawing context.
The drag matrix's pixel allocation peaks at **262,144 reused bytes**, with
geometric growth instead of reallocating for each new one-pixel maximum.
The taskbar painter stores only its strip; at 1920×1080/200% its reusable
power-of-two allocation is 1 MiB. Growth can temporarily retain the old smaller
allocation until the new allocation succeeds. The allocation cap is 64 MiB per
painter; normal selection damage is far smaller. There is no rectangle history,
full-screen selection duplicate, or per-slider-paint allocation.

Measured linked changes versus the original baseline:

| Item | Change |
|---|---:|
| Kernel text | +1408 bytes |
| Kernel data | +64 bytes |
| Kernel BSS | +14,912 bytes |
| WM data | +64 bytes |
| WM BSS | +32 bytes |

The fixed WM text segment remains 256 KiB. Measured stack frames: compositor
dirty path 4912 bytes; batch-copy helper 144 bytes; syscall dispatcher 1104
bytes; selection transaction 96 bytes. Larger damage arrays are retained in
BSS under the existing composition guard. There is no new unbounded stack use.

## Validation and limits

ASan/UBSan PASS includes native desktop/BSF/Settings/Start (139003 checks),
selection interleaving (1022), display transaction/scanout (304), slider pixel
matrix (1705 standalone plus native font-size repeats), controls (1379/1379),
desktop capacity/persistence (837103), file actions (87663), theme selection
(165), desktop settings/PZB (209), WM decorations (3356387), routing (222),
compositor (8677785), popup restoration (73816), BSF (12201), BSF UI (5876),
Explorer (15546), Husarz runtime (2259), and Husarz selection (987380).
Keyboard/TEXTINPUT/dialog, cursor, signed mouse deltas, InputDevice readiness,
theme, Notepad GUI, Husarz engine/layout, and fault-isolation policy regressions
also pass. [Host summary](gui-regressions-2026-10-29/host-summary.txt) and
[corrected controls run](gui-regressions-2026-10-29/controls-asan-ubsan.log).

The controls suite had an old assertion expecting opaque `0x00101010`; it now
checks the already-established `0x80101010` shadow contract. Production shadow
color was preserved. The corrected controls run uses ASan/UBSan at O0; earlier
O1 runs with that stale assertion are retained in local logs as failed runs.

Successful release QEMU runs report no Ring 0 panic, unexpected Ring 3 fault,
or GPU control timeout. Two early recording attempts did hit GPU command
timeouts; they were rejected, and no deadline or refresh rate was changed.
The first-paint observations are sampled QEMU frames, supplemented by the
pre-notification modeled GPU checks and deterministic live-surface interleaving
test. Physical hardware and an exhaustive continuous scanout recording are not
claimed. Full 51-frame selection sequences remain under `.bsf-test/gui-final/`.

The whole-tree 6144-byte stack diagnostic is **not a PASS**: it also inspects
unchanged functions whose reported frames exceed that threshold:
`psf_przenies_twor_wynik` 8592, `virtio_blk_stress_test` 8320, and Ring 3 Shell
`_start` 6896 bytes. The changed kernel frames remain below the threshold; these
pre-existing diagnostics were not expanded into unrelated changes.

Final SHA-256:

```text
c5328362b6f7db728813efb1ca5398c8a3ad894ac65bf1fdd1fc4273ecc76070  system_operacyjny.bin
afd85424cd62716a8d5031e3a415ff50c83c9ea06b216f69ff7b820bb0337232  BursztynOS.iso
```

[Artifact hash manifest](gui-regressions-2026-10-29/sha256.json).
