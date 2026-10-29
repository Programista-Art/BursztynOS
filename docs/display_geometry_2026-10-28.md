# Display geometry, cursor admission and desktop anchors

The display service now commits a bounded 64-byte `DisplayGeometry` containing
physical size, UI scale, work area, taskbar rectangle, full-screen pointer limits
and generation. Apply and rollback publish it under the existing screen lock,
after the prepared framebuffer has been presented. Event notifications follow
publication. Consumers read current state instead of replaying old event scales.
No delay, retry timer or cursor-range timer was added.

## Pointer audit and demonstrated failures

`grafika.cpp` owns physical pointer X/Y. Its shared relative and absolute entry
points clamp against the active physical graphics dimensions. PS/2, USB HID,
VirtualBox absolute input and VirtIO Input reach these entry points through
`InputDevice`. Absolute device ranges are mapped once to physical pixels;
relative deltas are not DPI-scaled. Cursor hotspot/bitmap clipping is independent
of logical pointer position. Taskbar/work-area limits constrain windows and
icon placement, never pointer movement. `mouse_last_max_x/y` cache diagnostics,
not movement limits. No audited taskbar or timer callback writes cursor bounds.
Boot/backend activation and framebuffer replacement also clamp the current
position. A display commit rechecks it and cancels kernel drag/capture state.

Two ordering problems were identified:

* Runtime VirtIO-GPU setup previously issued SET_SCANOUT before the display
  service exchanged active graphics buffers and geometry. Runtime setup now
  prepares the resource; the first complete framebuffer transfer activates it.
* VirtIO Input detected a changed generation on its next queue poll and treated
  every already-published completion as old. A QEMU test failed on the very first
  movement after Apply. The queue cutoff is now captured explicitly during the
  display commit, so later reports are new. Existing pre-desktop admission,
  malformed-event validation, SYN_DROPPED and partial-report resynchronization
  remain in force. The old fallback boundary remains for boot/readiness changes
  and callers outside the display service.

The synchronous order is: prepare scene/wallpaper/layers; prepare GPU resource;
exchange active graphics buffers; update BWS work area/scale and compositor
layers; compose/present; clamp the pointer and snapshot each VirtIO queue cutoff;
publish the geometry generation; unlock; notify clients. Rollback follows the
same ordering. Old resources remain protected by the existing confirmation and
rollback lifecycle.

There is no evidence of a timer that later corrects stale pointer limits. The
reported several-second symptom has not been conclusively attributed to such a
writer: the audit found no cached old clamp. The fixes above address demonstrated
publication/admission faults; QEMU edge tests verify the resulting behavior.
They do not establish the cause of every SDL/host viewport symptom.

## Desktop drift

The previous path multiplied already-rendered absolute X/Y by new DPI/old DPI,
then fed those pixels into layout and persisted repairs. Cell height is a sum
of individually rounded metrics. At font size 16 it is 124 pixels at 100% and
157 at 125%; scaling the old pitch gives only 155. This makes reserved footprints
overlap and triggers relocation. Repeated transitions and persisted emergency
repairs compounded the displacement. Scaling the old origin also disagreed with
`ui_skaluj(3, dpi)`.

A persistent `Desktop::Anchor` now contains column, row, manual offset X/Y and
the DPI at which that offset was intentionally placed. A runtime position is
always derived from that anchor and current grid metrics. Source-DPI integer
offsets preserve exact manual positions without repeated rounding. Returning to
an earlier configuration restores its exact pixels. No transformed pixel is fed
back into a saved anchor during a display transition.

Reflow reserves valid positions first and repairs only off-screen/colliding
runtime projections, using the existing bounded nearest-free search and stable
iteration order. Canonical anchors survive small-screen repairs and hidden
capacity overflow. They are retried on the next geometry change. User drag,
intentional placement and insertion establish new anchors. The stable reserved
footprint remains independent of wrapped labels and font family; the top origin
remains `ui_skaluj(3, dpi)`.

The checksummed version-1 placement document retains `anchors=1` and adds
`grid=1` plus exact anchor fields. Old anchor documents import at saved DPI;
older visual-coordinate documents retain their one-time validation/migration.
Pure display transforms and temporary repairs do not set placement dirty.
Saving another user edit writes canonical anchors for every entry, including
entries temporarily hidden by a smaller desktop.

## APIs and consumers

* BWS62 keeps its legacy monitor ABI. Passing the exact 64-byte output size
  requests the extended snapshot. Wrong sizes and unauthorized callers fail.
* BWS63 accepts an optional generation-qualified desktop acknowledgement. It
  rejects stale generations and mismatching work rectangles. Its old work-area
  interface remains supported.
* The WM reads one snapshot, updates desktop/taskbar/menu metrics, repaints, then
  acknowledges that generation. BursztynUI closes popups and capture on display
  events and obtains the current scale; Display Settings and the legacy adapter
  likewise read current state rather than trusting queued scale payloads.
* `virtio_gpu_setup_framebuffer` has a defaulted `prepare_only=false` parameter.
  Runtime graphics modesets request preparation. Existing callers retain their
  behavior. `virtio_input_commit_geometry` records the queue boundary without
  dispatching events, allocating memory or adding a timer.

Kernel geometry publication is synchronous. Ring 3 paints remain asynchronous:
an immediate screenshot can contain pixels painted before the client processes
its display event. `desktop_generation` explicitly distinguishes desktop repaint
completion from the kernel commit. This implementation does not claim an atomic
pixel repaint of every Ring 3 window or desktop in the same CPU instruction as
the physical modeset.

## Resource impact

At capacity 144, `Position` grows 168 -> 188 bytes and each Layout grows
24,248 -> 27,128 bytes. The active layout and selection snapshot add 5,760 static
bytes; the existing heap scratch layout adds 2,880. Removing two temporary pixel
arrays saves 1,152 static bytes. Settings State grows 306,660 -> 318,780 bytes,
including bounded serialization storage and exact records. Net desktop storage
increase for these objects is 19,608 bytes. Kernel snapshot/ack storage adds
68 bytes; no new per-frame or per-pointer-report allocation is introduced.

Release `.su` records: geometry commit 128 bytes, VirtIO queue commit 80 bytes,
Apply 48 bytes. These are individual frames, not a whole-call-chain maximum.
Large settings/layout objects remain static or on the heap.

## Validation

Artifacts are under `.bsf-test/display-geometry-*` (ignored build/test output).
The final results and hashes are recorded below when acceptance runs finish.

Rebuilt ASan/UBSan checks in this continuation:

| Coverage | Result |
| --- | --- |
| Production display transaction, relative/absolute edges, shrink clamp, rollback | 244 PASS |
| Extended snapshot ABI, stale acknowledgements, authorization | PASS |
| VirtIO transport/queue admission, including first post-commit report | 124,723 PASS |
| Real graphics/InputDevice/HID integration | 91 PASS |
| Desktop, Settings, Start, scale/family/resolution round trips | 131,251 PASS |
| Desktop persistence/PZB | 209 PASS |
| Rounded window decoration renderer | 3,356,387 PASS |
| Input coexistence and diagnostic parity | 12 parity comparisons PASS |

Unchanged regression executables were also rerun with ASan/UBSan: WM routing
222; compositor lifecycle/scene 8,677,783; popup restoration 73,816; BSF 12,201;
BSF UI/browser 5,876; desktop file actions 87,663; fault-isolation policy 32,768.
The earlier session additionally ran the PS/2 parser (330,443), signed cursor
arithmetic (396,600), desktop capacity/persistence fuzz (837,103), HID descriptor,
i8042, OHCI and xHCI regression suites; see
`.bsf-test/display-geometry-regressions.log`.

Ordinary release VirtIO-GPU QEMU, with real Display Settings controls and no guest
memory writes:

* `display-geometry-final-matrix-r2/result.json`: 13 transitions PASS, 20 icons
  including two folders created through the desktop UI. The full
  100 -> 125 -> 150 -> 200 -> 150 -> 125 -> 100 scale sequence, resolution round
  trips and combined transitions preserve every canonical anchor. Every repeated
  configuration has exactly equal rendered cell anchors. The final 1280x720/100%
  snapshot equals the initial snapshot.
* All 13 transitions reach bottom-left, bottom-right, top-left, top-right and
  bottom center in the committed generation, before the UI confirmation action.
  The first input follows the kernel commit signal; it does not wait for a
  desktop timer or desktop repaint acknowledgement.
* `display-geometry-final-virtio-r3/result.json`: the direct 1280x720/100% ->
  1920x1080/125% -> 1280x720/100% scenario also passes with 18 original icons.
* `display-geometry-final-usb-r2/result.json`: both combined transitions and
  all five cursor targets PASS with USB HID input.
* `display-geometry-final-ps2-r3/result.json`: both combined transitions and
  all five cursor targets PASS with the PS/2 parser and device path.

`display-geometry-final-matrix-r2/image-anchors.json` records exact icon image
anchors derived from the observed runtime cells and production icon inset.
`immediate-apply-0.png` through `immediate-apply-12.png` capture the first
post-commit frame, including the asynchronous client-paint state rather than
waiting for it to disappear. `confirmed-*.png` record completed desktop/client
updates. Health checks reject Ring 0 exceptions, critical GPU failures and
unexpected Ring 3 faults; successful runs contain none.

The matrix contains 260 canonical-anchor comparisons and 160 exact comparisons
at repeated configurations, all equal.

Host controls must not consume the existing 15-second guest confirmation timeout.
The harness sends one full displacement for VirtIO and observes completion;
HID/PS2 receive individual bounded deltas and acknowledge their consumption. Earlier harness
attempts resent distance before partial device reports finished, or exhausted
confirmation while sending many small reports. Their timeout/automatic-rollback
logs are retained as failed test attempts, not reported as passing regressions.
Likewise a popup selection of an already-selected resolution did not close the
native list; scale-only tests now leave the resolution control untouched.

Reproduce the host probes first with
`make test-display-geometry-host test-display-snapshot-host test-desktop-ttf-host`.
Then run `python3 narzedzia/test_husarz_qemu.py --display-geometry-only
--display-input virtio --output .bsf-test/display-geometry-check --timeout 2400`
from the same build as `menedzer_okien.elf`. Set `BURSZTYN_DISPLAY_SMOKE=1` for the
two-transition scenario; `--display-input usb` and `--display-input ps2` select
physical-protocol QEMU adapters.

Final release hashes:

```
f0471631c7d2418df9c635dee2f30f42c6c7cc7e786d95d3da1bff887a6cc888  system_operacyjny.bin
1cdd7f077316afcefa0a8a27a4fc69c198c1355fb57a3450f8e8a8462886d359  BursztynOS.iso
```

## Limits of acceptance

QEMU used OVMF/TCG with a VirtIO-GPU release image. Real hardware, the SDL host
frontend and installed-disk reboot were not retested in this continuation.
Manual offsets and serialization/reload are covered by host production-layout
and persistence tests. QEMU creates actual user folders and compares runtime
anchors; it does not simulate a manual drag at every scale. The full Explorer,
Notepad and Husarz interactive suites were not repeated after each of the 13
modesets. Their shared UI and fault-policy regressions are covered as above.
The remaining asynchronous Ring 3 repaint boundary is described explicitly;
this work does not claim every visual consumer paints atomically with scanout.
