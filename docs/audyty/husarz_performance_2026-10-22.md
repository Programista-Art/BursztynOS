# Husarz cooperative rendering and performance, 2026-10-22

## Validation status

The final release ISO has completed the full native regression, heavy-page
performance regression and table/margin geometry regression with machine-readable
**PASS** results and matching artifact hashes. The final host suites pass with
ASan/UBSan. The implementation establishes deterministic cooperative work bounds
and the tested UI interactions; it does not establish a strict wall-time latency
guarantee. Measured diagnostic stall warnings and remaining limits are retained
below rather than described as universally fixed performance.

## Inspected pipeline and original causes

The original event path was inspected before functional changes. Instrumented
copies of the original headers and baseline measurements are retained under
`.husarz-test/perf-baseline-headers/` and `.husarz-test/perf-before-measured.jsonl`.

| Operation | Original execution site and behavior |
| --- | --- |
| Network transport, TLS, decoding | Existing separate native resource worker; one network session. This was already outside the GUI process. |
| Network completion | IPC-ready GUI handler called `apply_worker_result` directly. HTTP response consumption, copies, HTML document construction and resource discovery could follow before returning. |
| HTML and DOM | `renderuj_html` called the entire `parse_html` synchronously. Tokenization, entity decoding, tree recovery, attributes and raw text were included. |
| CSS parsing | Inline sheets were collected during document construction; external sheets were parsed directly during completion. |
| Selectors, cascade, computed style | `compute_styles` parsed selectors repeatedly inside the node-by-rule scan. `queue_images` could call it synchronously. |
| Layout, tables, margins, text, fragments | One synchronous `reflow`: style if dirty, list markers, all table planning, margin passes, flow traversal, line alignment, table finalization, inline reconstruction, positioning and clipping. |
| Resize, maximize, restore | `WidokStrony::rozmiesc` called `przygotuj`, which could execute the complete reflow on the layout/event callback for each changed width. No retained pending-size request existed. |
| Resource publication | Each image binding advanced revision and refreshed UI, potentially triggering another complete layout. Cache-hit dispatch could process an entire queue synchronously. |
| Paint and scrolling | `rysuj_siebie` rasterized the document viewport on each draw. Existing vertical text and pixel clipping did not avoid all ancestor, border, image, control and horizontal glyph work. Chrome redraw could also rerasterize the page. |
| Forms | Editing called preparation; toggle state updated display/revision and could cause document reflow. |
| Tabs | Switching updated chrome and prepared the selected document synchronously. |
| Theme | The document/chrome boundary already existed. Theme notifications could cause repaint, but must not cause web restyling from desktop colors. |

The maximize problem was the synchronous width-dependent reflow directly under
native widget layout, followed by synchronous rasterization. Network transport
was not the cause of that call path. Ordinary resize already avoided cascade
when `styles_dirty` was false; this work preserves that property rather than
claiming to have introduced it.

## Diagnostics

`make HUSARZ_PERF=1 HUSARZ_DEBUG=1 iso` enables development counters, stage timing,
work traces and warnings. Defaults remain zero. A build configuration stamp
rebuilds both native browser and worker when these flags change.

Counters cover nodes/text nodes, rules/declarations, selector attempts/matches,
boxes/fragments, tables/cells/planning work/replans, images/controls, height and
viewport, full/partial layout and paint, retained paint candidates/visible/culled
entries, dirty rectangles, completions and cache hits/misses, resize requests,
coalescing, completed layouts and maximum work slice. Paint entries mean retained
boxes and text fragments, not individual pixel writes. Stage measurements cover
HTML, CSS, styles, layout, table planning, paint, publication, resize reflow,
first usable layout and complete navigation through the final resource paint. Layout timing includes nested style/table work when needed;
these overlapping stage columns must not be added together.

The native budget clock uses a user-mode cycle estimate sampled against the
existing BWS60 monotonic time supplied by the GUI loop. Calibration rejects
backwards counters, implausible rates and large rate changes. Work-unit limits
remain authoritative if calibration is unavailable. This avoids a kernel
syscall at every inner-loop checkpoint. Native duration estimates are diagnostic,
not timing-based correctness assertions. Host profiling uses `steady_clock`.

A slice over 50 ms produces a development stall warning with stage, navigation,
document, cursor, remaining estimate and charged units. No warning terminates the
browser. The watchdog is cooperative: it reports when control returns, and does
not require another thread or a lock. Disabled diagnostics remove document
counter storage and verbose output through preprocessing.

## Scheduling and publication

`work.h` implements stackless compiler continuations with a small native compiler
ABI shim. It uses no STL implementation, host threads, Linux calls, exceptions or
RTTI. Husarz targets enable GCC's `-fcoroutines`; other apps retain their compiler
settings. Continuations live in a single bounded 512 KiB process heap slab with
size-class reuse, including nested helper frames. Allocation failure prevents
publication. Destroying a task destroys its suspended children before releasing
its document.

The deterministic cap is 2,048 charged work units per step. Units include loop
visits, selector work estimates, characters and bounded raster/copy spans;
copy/raster spans cover at most 64 bytes/pixels per charged span. A calibrated
2 ms time guard additionally checks progress every 128 charged units. Stage
diagnostics use BWS60 at slice/stage boundaries independently of this optional
calibration, so rejected calibration does not silently erase stage timings. A glyph
row is bounded by the supported font dimensions. These are explicit software
work bounds, not a claim that the OS can schedule a process within exactly 2 ms.

Yield points include HTML tokenization/raw scans/entity decoding, CSS scans and
declarations, stylesheet collection, node/rule traversal, complex ancestor and
sibling matching, table grid initialization/reservation/intrinsic measurement,
margin planning, text/line layout, flow traversal, table finalization, fragment
reconstruction/transforms/clipping, image publication copies and raster loops.
Helper continuation state is not retained on the C++ stack.

GUI events continue through the existing native event loop. A control animation
callback advances bounded background work between event batches. During ready document/publication/paint work, Husarz
requests a one-tick GUI idle interval; network-only waits retain eight ticks.
Other applications retain their existing eight-tick default. The existing
input dispatch handles close, Stop, keyboard, mouse, window and tab commands;
none waits for complete document layout. Only one private document computation
is retained across all six tabs. Selection favors the visible tab, with bounded
opportunities for pending background tabs. No parallel TCP facade was added.

Native testing also exposed that Alt+Tab had no system handler: it reached the
active application's ordinary Tab focus traversal. `grafika.cpp` now consumes
Alt+Tab before app delivery and activates another titled window through the
existing compositor activation path. The fixed 16-window snapshot is bounded;
the untitled desktop is skipped and minimized windows use the existing restore
operation. Shift cycles backward; Ctrl+Alt combinations are left alone. Physical
release and legacy/text echoes are consumed without duplicate Tab delivery.
Application permissions and the BWS syscall authorization rules are unchanged.
This is a native input correction required by the requested interaction test,
not a web rendering feature. Production graphics/input host tests pass 91 checks
with ASan/UBSan, including switching without an application polling events.

The published document remains available for hit testing and painting while a
private result is constructed. Publication compares tab identity, navigation,
document, content/style, viewport and interaction state. New resize requests
supersede older results. Stop and tab destruction destroy work and invalidate
navigation; stale worker results still use the existing resource generation
checks. The last published URL is retained with the tab so links/forms in old
published content retain the correct base while another navigation is pending.

## Resize and invalidation

`ViewportRequest` keeps only the newest valid size and counts received/coalesced
requests. Widget layout updates chrome and scrollbar bounds immediately, then
requests work. It does not call native document reflow. The GUI event batch is
drained before the next continuation step, coalescing intermediate dimensions.
The final size remains pending until its valid result is published.

Dirty flags explicitly escalate style to geometry and geometry to paint and
scroll geometry. Per-node flags retain the local source of invalidation.
Computed styles are copied unchanged for ordinary width changes. Document and
layout revisions remain distinct from paint and viewport state.

The incremental branch is deliberately conservative. A fixed-dimension form
control outside tables can regenerate its own fragments while retaining all
other geometry. Checkbox/radio changes repaint their box. Caret movement by arrows/Home/End
only damages the control box, and deletion at an unchanged empty boundary does
not request layout. Select-option changes also damage
the open popup, preventing stale selection highlighting. Background images are
paint-only. Foreground images use paint-only publication only after proving
unchanged dimensions, scroll extent, line alignment and containment of the old
alt-text ink; their old alt fragments are replaced with an image hit rectangle.
Inline baseline dependencies, overflowing alt text, intrinsic image-size changes,
uncertain form geometry, stylesheet changes and general flow changes use a full
**cooperative** pass. This avoids unsafe assumptions about following
siblings, collapsed margins or table intrinsic widths.

Table intrinsic plans are retained for unchanged dependencies. Width-dependent
column constraints start from saved intrinsic minima/preferred widths, so a
previous wider percentage constraint cannot prevent a later shrink. Ordinary
documents allocate no table grids. Existing table work and dimension limits stay
unchanged.

## Painting

Eight bounded dirty rectangles merge overlapping/touching regions. Overflow
falls back to the full web viewport. The same complete painter runs clipped to a
dirty region, preserving ancestor backgrounds, stacking and table borders.
Coordinates are clipped to the web surface; cached content never includes chrome.

A pair of bounded viewport buffers retains the published pixels and an unfinished
paint. Their total limit is 32 MiB at the existing 4-million-pixel viewport cap.
There is no full-page bitmap or unbounded offscreen tile cache. Stale paint is
canceled when its source/viewport changes. Chrome-only invalidation exits without
copying or rasterizing unchanged page pixels.

Offscreen boxes are rejected before ancestor/image/border work. Text fragments
are rejected against visible and damaged bounds before glyph rasterization.
Collapsed tables reject invisible table extents and existing border pattern loops
remain clipped. Scrolling changes paint/viewport state without HTML/CSS parsing,
network fetching or cascade. A cheap bounded scan of retained entries remains;
there is no spatial index for that scan yet.

The native glyph width operation is already a bounded font table lookup plus
integer scaling (`CzcionkaUI::szerokosc_znaku` / `advance_font`). No website-string
measurement cache or tile system was added. Their memory and invalidation costs
are not justified by the current profiling evidence.

## Memory audit

The original release `sizeof(Document)` was 3,244,544 bytes, including 627,200
bytes of table plans even without a table. The new table store allocates slabs
of four plans only when needed. The limits remain 32 tables, 256 rows, 32 columns,
1,024 accepted cells and the existing 2,000,000-unit planning bound.

Memory figures exclude shared image-cache pixels and allocator headers:

| Retained document data | Before | After |
| --- | ---: | ---: |
| `sizeof(Document)`, release | 3,244,544 | 2,619,608 |
| Minimum before style index | 3,244,544 | 2,619,608 |
| Simple styled page | 3,244,544 | 2,705,680 |
| Mixed fixture, four table slots | 3,244,544 | 2,785,104 |
| Table fixture, twelve slots | 3,244,544 | 2,943,952 |
| Maximum per document including selector index/table slabs | 3,244,544 | 3,341,072 |
| Six minimum documents | 19,467,264 | 15,717,648 |
| Six maximum documents | 19,467,264 | 20,046,432 |

The other major always-present arrays were measured individually: fragments
786,432 bytes, text arena 786,432, computed styles 524,288, form controls 132,352,
CSS declarations 81,920, DOM nodes 73,728, attributes 65,536 and boxes 65,536.
Script metadata is only 640 bytes and image bindings 1,536; moving those first
would not materially improve the base footprint. These capacities are unchanged.

The worst case increases slightly because of the bounded selector index and
retained intrinsic table widths; safety capacities were not reduced. At most
one additional private document exists during computation, bounded by 3,341,072
bytes of retained document/index/table storage. Decoded images are pinned shared
cache entries, not duplicated by a layout snapshot. The work slab adds at most
512 KiB. Viewport buffers and the existing resource/decoder budgets are separate.
For a broader payload bound, six maximum documents plus their six 256 KiB HTML
sources and the shared 2 MiB raw/16 MiB decoded caches total 40,493,664 bytes
(before: 39,914,496). Adding the new maximum viewport-buffer pair reaches
74,048,096 bytes; add one private document and the 512 KiB continuation slab
while work is active. These are component payload bounds, not an RSS estimate:
allocator/page overhead, chrome/compositor surfaces, queue metadata, the existing
<2 MiB worker shared payload and separate worker/decoder memory are excluded.

The large arena, styles, form buffers and fragment arrays remain fixed; moving
more of them out of the base document is a remaining memory optimization.

## Stress fixtures and deterministic tests

The checked-in generator creates six bounded pages:

* `heavy-dom.html`: 2,006 nodes including text.
* `heavy-css.html`: 304 total rules including UA rules, hundreds of class combinations.
* `heavy-table.html`: twelve tables, rowspans and colspans.
* `heavy-long.html`: approximately 50,000 pixels of vertical content.
* `heavy-images.html`: 32 image elements with both repeated and distinct cache URLs.
* `heavy-mixed.html`: nested blocks, selectors, text, tables, forms, images, margins,
  borders and a deterministic width marker.

Host performance tests check slicing, cancellation, identity supersession,
resize bursts versus one layout, preserved styles on resize, indexed/reference
matching, long-page culling, synchronous/sliced pixels, partial-control geometry,
dirty-paint pixels, caret movement versus full paint, and percentage-table
shrink equivalence.

The HTTP fixture server advertises a 3,600-second freshness lifetime so cache
reuse assertions do not depend on completing TCG input and rendering within the
browser’s default 60-second lifetime. Production cache behavior is unchanged.

The additional `--diagnostics-only` mode waits for development serial counters
without screenshot polling during document work; it separately records mixed
page navigation, maximize/restore and long-page scrolling. This is a diagnostic
build and is never substituted for release-ISO validation.

Native performance automation loads the fixtures through the real worker,
repeatedly maximizes/restores and checks final width marker/scrollbar geometry,
checks mouse movement, exercises wheel/Page Down/thumb scrolling without extra
fetches, switches to a real second app, stops unfinished work and changes/closes
tabs. Polling screenshots use PPM, one capture per predicate and a one-second
interval during expensive waits. Saved review screenshots remain PNG. The
observer pauses virtual execution only during frame capture/encoding and resumes
it immediately afterward; host image IO must not spend the guest device
deadline while QMP blocks its backend. Input and document progress run between
observations. One earlier concurrent run hit the unchanged virtio-GPU 500 ms
TRANSFER_TO_HOST_2D timeout and is not counted as validation. Tests wait on visible completion state and pixels,
not fragile exact host durations. The Stop-state observer supports light/dark
chrome and moves the mouse away from the sampled label through native input,
so neither button background nor cursor pixels are mistaken for enabled text.

## Final measurements and verification

Host profiling uses the original instrumented pipeline and the final pipeline,
the same six fixtures, real native font metrics and an 800×600 viewport. Values
below are medians of five optimized host runs in milliseconds. Raw results are
`.husarz-test/perf-final-before.jsonl` and `perf-final-after.jsonl`. The final five pairs alternate baseline/final, run on the same host CPU, and
run without QEMU or compiler jobs. Other host activity still adds noise; these
are diagnostics, not pass/fail thresholds.
Layout includes style work. The baseline CSS fixture exhausts the existing match
budget and truncates, whereas the final pipeline completes it; its output is not
an equivalent complete-page timing comparison.

| Fixture | Selector checks before → after | HTML ms before → after | Style ms before → after | Layout ms before → after | Paint ms before → after |
| --- | ---: | ---: | ---: | ---: | ---: |
| DOM | 24,144 → 2,003 | 0.330 → 0.990 | 2.376 → 3.845 | 2.829 → 4.499 | 2.897 → 3.277 |
| CSS | 131,397 → 29,009 | 0.360 → 2.285 | 14.997 → 18.204 | 15.203 → 18.604 | 2.994 → 4.584 |
| Tables | 11,664 → 700 | 0.097 → 0.239 | 0.783 → 0.699 | 0.954 → 1.332 | 1.255 → 2.923 |
| Long | 16,944 → 703 | 0.341 → 0.854 | 1.175 → 1.249 | 1.551 → 1.699 | 1.337 → 2.966 |
| Images | 1,296 → 67 | 0.016 → 0.038 | 0.095 → 0.110 | 0.122 → 0.155 | 2.466 → 3.193 |
| Mixed | 103,044 → 17,377 | 0.260 → 0.661 | 7.615 → 6.668 | 8.071 → 7.539 | 2.384 → 3.300 |

Additional stage medians from the same runs (milliseconds):

| Fixture | CSS parse before → after | Table planning before → after |
| --- | ---: | ---: |
| DOM | 0.015 → 0.050 | 0.007 → 0.015 |
| CSS | 0.198 → 1.982 | 0.009 → 0.022 |
| Tables | 0.012 → 0.038 | 0.028 → 0.391 |
| Long | 0.013 → 0.038 | 0.006 → 0.009 |
| Images | 0.013 → 0.038 | 0.000 → 0.001 |
| Mixed | 0.037 → 0.117 | 0.021 → 0.174 |

Continuations and checkpoints have real throughput overhead, particularly in
small parses and pixel loops. The result is not a universal reduction of total
CPU time. The principal improvement is that work can yield and be canceled,
resize work is superseded, and unchanged styles/geometry/pixels are reused.

The final ASan+UBSan engine/layout/fidelity/runtime/performance suites passed
402,375 checks: 127,432 + 259,225 + 3,697 + 7,428 + 4,593. The clock regressions
verify stage timing without calibrated time and a deterministic yield at the
128-unit time polling boundary using a synthetic clock. The performance suite observed
13,952 bytes of simultaneously live continuation frames and enforces the 2,048
unit cap. Native stack analysis passed; the largest browser frame is 4,176 bytes
(`ustaw_url_z_location`), below the existing 6,144-byte cap. GUI integration passed
18/18 checks. Native build and ISO build completed.

Image decoder tests passed using `BURSZTYN_TEST_PNG=.husarz-test/png-reference.png`,
a deterministic generated 1920×1080 RGB PNG, because the default optional
`obrazy/tapety/nowatapeta.png` is absent from the workspace. This does not validate
that unavailable wallpaper file. Existing JPEG, malformed/alpha PNG and image
packaging checks also passed. GUI text input and input coexistence/parity suites
passed with ASan/UBSan.

Final native release validation is **complete: PASS**. All three results use
`HUSARZ_PERF=0 HUSARZ_DEBUG=0` and the same ordinary release ISO/browser/worker;
no private browser replacement or fault payload is counted in these results.

| Native release suite | Result | Machine-readable record |
| --- | --- | --- |
| Full regression | PASS | [full result](../.husarz-test/qemu-release-verified-v8-full/result.json) |
| Six heavy fixtures, maximize/restore, scrolling, Stop, tab close, app switching | PASS | [performance result](../.husarz-test/qemu-release-verified-v8-performance/result.json) |
| Rowspan/colspan, border conflict, signed margins, maximize/restore | PASS | [layout result](../.husarz-test/qemu-release-verified-v8-layout/result.json) |

The full run includes 24 refresh-frame anchoring observations; wheel/thumb/track/
Home/End/Page Up/Page Down; history; native PNG/JPEG/background pixels; reload and
two-tab cache reuse; UTF-8 form GET; closing image/CSS tabs during loading; both
white/black theme isolation; Stop and stale HTML rejection; closing the browser
during a request and continued use of another native application. The heavy run
completed four maximize/restore cycles (eight changed sizes), checked mouse input
before waiting for new geometry, exercised all six pages and rapid long-page
scrolling without fetches, and switched apps/stopped/closed a tab while work was
visibly pending. Each suite records `no_guest_fault=true`.

Final release SHA-256 values:

```text
ISO     1d3aae3942a4cd378820e58c566fa0813b2cc87cb99ffff617fa2e4f5a06a24a
kernel  0f4c722c037bbb86df2c27cb168dd2d131346163f077f59a2af4732b5339345f
browser 3c0ddbdbbaba35d569c8a7f219c025fedbe191764495ffb36e18553089ec75e2
worker  2a7bcf8d0ce5b8f0bfb0171ca749e7f5c8fe8dacf672263fba3f121422d1b58b
```

The immutable tested copies are in `.husarz-test/performance-release-final-v8/`;
the root release artifacts have the same hashes. The [aggregate verification
record](../.husarz-test/performance-final-validation.json) checks all three JSON
results against these hashes. Earlier failed/interrupted runs, including observer
bugs and a concurrent virtio-GPU timeout, are preserved and excluded from PASS
validation. No driver deadline was relaxed.

The deterministic resize-burst host test receives 100 changed extents, coalesces
99, performs one layout for the final 899×699 viewport and repeats no selectors.
An earlier instrumented eight-resize native sequence recorded nine viewport
requests including initialization, zero coalescing (each action waited for its
result), eleven completed layouts including the blank document, ten full reflows
on the mixed document, unchanged 17,377 selector checks and eight table replans,
and a 2,048-unit maximum slice. Its later UI-observer assertion failed; these
counters are diagnostics, not a replacement for the final release PASS run.
The maximum elapsed slice in that screenshot-polled diagnostic sequence was
2,677 ms, compared with 1,640 ms in the serial-observed diagnostic described below.


## Remaining limits

Full cooperative reflow remains the safe fallback for stylesheet changes,
viewport width changes, intrinsic image geometry, table-dependent form edits,
inline image baseline/alt-ink dependencies and general flow changes. Partial
geometry currently covers proven fixed-size form controls outside tables;
it is not a general dependency graph for arbitrary future DOM mutations.

Bounded synchronous operations remain: document/slab allocation and destruction,
resource URL discovery, bounded HTTP metadata/raw-cache copies, and publication
of the completed viewport through the existing BWS blit/compositor. Text width
lookup is cheap; selector continuation/checkpoint overhead and complex flow/table
passes are larger remaining CPU costs. A cheap scan of retained paint entries is
still proportional to retained entry count even though offscreen rasterization
is culled. Improving these limits should use the same identities and invalidation
rules rather than weakening safety capacities.

The serial-observed native diagnostic run completed with PASS after the
architecture changes and before the final caret-only invalidation fix. Its
parse/style/layout/resize code is the same; it is diagnostic evidence and is
not substituted for validation of the final release artifacts. Mixed content
used 17,377 selector checks, two full reflows for nine completions (eight unique
images plus HTML), and kept those selector checks/table plans unchanged through
maximize/restore. The long page was 50,416 pixels high: the initial paint considered
2,803 retained entries, drew 27 and culled 2,776. End scrolling added 2,803 candidates,
drew 24 and culled 2,779, without parsing, cascade or fetch work. Native maximum
charged slice work was 2,048.

The same diagnostic run recorded **1,640 ms maximum elapsed slice time** across
maximize/restore and **1,476 ms** on the long navigation; even without screenshot
polling during the work, wall-clock warnings remain. Mixed first layout took
52,756 ms and complete resource navigation 76,940 ms in that TCG run. Native stage
sums were HTML 899 ms, CSS 85 ms, style 10,746 ms, initial layout 12,847 ms (includes
style), and first paint 2,670 ms. These observations include guest scheduling and
TCG delays; this instrumentation does not establish how much of each warning was
uninterrupted Ring 3 CPU work. They must not be presented as meeting a strict
2 ms wall-time latency target or as proof that all native latency spikes are gone.
Input/cancellation tests and deterministic work bounds are separate assertions.
The full diagnostic data is `.husarz-test/qemu-development-final-v7-diagnostics/result.json`.

The 2 ms guard is best effort and depends on valid fast-clock calibration. The
2,048-unit boundary is deterministic; it does not guarantee an exact real-time
latency under scheduler contention, TCG or screenshot/debugger pauses. Diagnostic
stall warnings remain useful evidence and are not suppressed to obtain a clean
benchmark. The maxima above are reported with these limitations; strict
wall-time latency remains unproven.


## Separate fault isolation checks

`make test-user-fault-policy` passed 32,768 ASan/UBSan checks. Separate native
UD2/vector-6 and null-access/vector-14 probes both passed: the test Ring 3 process
terminated, the desktop survived and Notepad displayed/accepted input afterward.
These use deliberately replaced browser payloads in private kernels and are
**excluded** from the release-ISO validation above. Records:
[UD2 probe](../.husarz-test/qemu-user-fault-verified/result.json) and
[page-fault probe](../.husarz-test/qemu-user-page-fault-verified/result.json).
