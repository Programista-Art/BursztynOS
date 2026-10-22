# Husarz engine development, 2026-10-21

## Scope and status

This is an **incremental implementation**, not completion of the requested major
browser milestone. This increment changes tables and practical block margin
collapsing. The remaining requested subsystems are explicitly listed below.
It makes no HTML5, CSS3 or ECMAScript conformance claim.

Production changes are in the document, layout and paint headers compiled into
the existing native Ring 3 browser. No parser, renderer, image codec, script
engine or networking functionality was added to Ring 0. There are no new
production dependencies, STL uses, exceptions, RTTI, host threads, Linux APIs or
host networking. Fault handling and the web/desktop theme boundary are unchanged.

## Changed source files

* `programy/husarz/document.h`: retained table plans, logical occupancy grids,
  per-cell placement, bounded scratch storage and collapsed-margin metadata.
* `programy/husarz/layout.h`: grid construction, intrinsic measurements,
  column constraints, rowspan row heights, cell alignment and margin struts.
* `programy/husarz/css.h`: explicit row-group display defaults and declarations.
* `programy/husarz/paint.h`: collapsed-border edge arbitration and painting.
* `narzedzia/test_husarz_layout.cpp`: deterministic geometry/pixel checks,
  malformed spans, capacity limits, nested tables and margin regressions.
* `narzedzia/fixtures/husarz/layout-grid.html`: native QEMU layout fixture.
* `narzedzia/test_husarz_qemu.py`: `--layout-only` release-ISO pixel and
  maximize/restore checks, screenshots and machine-readable results.
* `narzedzia/fixtures/husarz/expected.tsv`: explicit expected geometry/image
  changes explained below; comparisons remain exact.
* `Makefile`: `test-husarz-layout-host`, ASan/UBSan with leak checking.

## Table algorithm

Each table owns a logical 256-by-32 occupancy grid. Rows are gathered before
cells are placed; nested-table rows belong exclusively to their nearest table.
For each row, placement skips coordinates already reserved by earlier rowspans.
The cell's complete rectangular span is checked before any reservation is
written. An overlapping cell is rejected and omitted from layout, and the
bounded-document truncation flag is set. Earlier reservations remain intact.

Spans are clamped to 32, the remaining columns and existing rows. A rowspan
cannot cross an explicit row-group parent boundary. Zero/malformed spans use a
one-cell fallback; this is not HTML's special rowspan=0 behavior. `thead`,
`tbody`, and `tfoot` have explicit row-group layout boxes updated from finalized
row geometry and retain source order; footer reordering is not implemented.
Tables or rows exceeding their limits, and cells exceeding the document cell
limit, are omitted rather than allowed to fall back to overlapping geometry.

Intrinsic measurements track the longest unbreakable run and preferred line
width using existing UTF-8 glyph metrics, font style, inline descendants,
replaced image widths, cell padding and borders. Single-column constraints are
applied first, then spanning inequalities in one additional pass. Each span's
positive deficit is distributed deterministically over its columns. Column
`width` styles, `col` width attributes, and practical cell/column percentages
participate. Preferred widths never fall below minima. Available width is
allocated between minimum and preferred widths, with bounded rounding; extra
space in explicitly sized tables is distributed across columns. Automatic
tables shrink toward preferred width. Minimum constraints can overflow the
containing viewport rather than silently destroying the column plan.

Every reflow rebuilds the plan using retained DOM/styles. No network fetch or
HTML reparse is required on resize. A final table pass starts with non-spanning
row heights, distributes remaining rowspan deficits across the covered rows,
then translates cell contents to their final row positions. Top/middle/bottom
alignment moves content while preserving the cell's border rectangle.

Separate borders include spacing between columns/rows and around the grid.
Collapsed tables choose a winner for each logical edge segment. Wider borders
win; equal widths prefer solid, dashed, then dotted; remaining ties prefer the
earlier DOM node. Table perimeter borders participate. Interior edges within a
single spanning cell are suppressed. Winning edges are drawn once, centered on
the shared boundary, after backgrounds/content. This is a practical conflict
model, not the complete CSS border model: row/row-group/column borders, hidden
border dominance, double/groove/ridge/inset/outset styles, separate per-side
colors/styles and full stacking/opacity-group semantics are not implemented.
Outer-edge half-border sizing also differs from full CSS collapsed tables.

Intrinsic sizing remains approximate for complex nested blocks/tables,
percentage padding, whitespace normalization, replaced form controls and
indefinite percentage constraints. `table-layout:fixed`, caption layout,
`colgroup` width distribution, automatic anonymous cell/row repair and full
baseline alignment remain unsupported. Explicit table heights do not distribute
extra height through the rows. These limitations prevent calling the complete
requested table subsystem finished.

## Margin collapsing

A margin strut stores its largest positive and most negative component
separately. Combining struts takes the respective extrema; physical spacing is
their sum. This retains information across mixed-sign empty-block chains that a
single max() or a repeatedly combined scalar would lose.

A bounded parent-first width estimate followed by an iterative postorder walk
computes exposed top/bottom struts and empty-block passthrough. Parent/first-child
top margins and eligible parent/last-child bottom margins join the exposed
parent strut. Internal child contributions are suppressed at layout, so they are
not counted twice. Empty blocks preserve the complete adjoining sibling strut.
Inline content interrupts sibling collapsing. Coordinates stay signed when
negative margins move content above zero; dimensions stay nonnegative.

Borders, padding, inline content and overflow formatting contexts prevent
collapsing through the relevant parent edge. Explicit/minimum height constrains
bottom/empty collapsing. Positioned elements, inline-blocks and table cells do
not participate as ordinary collapsing blocks. Root `html` is a collapse
boundary. Both walks are iterative and bounded by the DOM node limit.

Remaining limits: widths used to resolve percentage margins are estimated before
final table/min-max layout, empty inline wrappers are conservatively treated as
inline content, and floats/clearance have no implementation yet. Consequently,
this is a practical block subset, not complete CSS 2.1 margin behavior.

## Resource bounds and audit

| Resource | Bound |
| --- | ---: |
| Tables per document | 32 |
| Rows per table | 256 |
| Columns per table | 32 |
| Total accepted table cells per document | 1,024 |
| Span in either direction | 32 |
| Grid/intrinsic planning work | 2,000,000 charged units per reflow |
| Grid storage | 16,384 bytes per table |
| TablePlan size on current x86-64 build | 19,600 bytes |
| Complete Document size on current x86-64 build | 3,244,544 bytes |

The existing browser limit is six tabs. Six retained Document objects therefore
occupy 19,467,264 bytes, before other browser/resource allocations; replacing a
document can temporarily retain both old and new objects.

No website-controlled allocation sizes are introduced. Table grids, span
placements and margin metadata have fixed document-owned capacities. Large
plans are initialized in place to avoid large Ring 3 stack temporaries. Size
arithmetic uses bounded dimensions and 64-bit intermediates where products or
sums require them. Finalization and border passes have fixed loop limits derived
from the table/DOM/fragment capacities; fragment membership uses parser preorder
indices rather than repeated ancestor walks. Offscreen border dash iteration is
clipped to visible pixels and paint retains its existing pixel budget.

## Reference-image changes

No existing assertion or malformed-input iteration was removed. Table fixture
height changes from 106 to 116: the four-row separate-border table now includes
five vertical gaps of two pixels. The table checksum changes accordingly.

Parent/child collapsing intentionally changes the headings, inline, lists,
links, images, box, wrapping and scroll reference images. For example, the box
fixture changes from height 78 to 62 because each body margin of 8 collapses
with the child's margin of 10 rather than adding to it, removing 8 at both ends.
The images fixture retains its height but moves content as adjoining inline flow
and block margins are handled separately. Plain text, nested, percent and radius
reference images remain identical. Exact height, fragment count and pixel
checksum comparisons remain enabled for every fixture. New geometry tests
independently assert the collapsing rules and border winners.

## Validation

Final host/release/QEMU outcomes and artifact hashes are recorded below.
Earlier baseline results must not be interpreted as validation of a later source
change.

## Requested subsystems still outstanding

| Subsystem | Status in this increment |
| --- | --- |
| Float left/right and clear | Not implemented |
| Fixed/sticky, z-index ordering | Not implemented |
| Independent nested scrolling and scrollbars | Not implemented |
| HTTP POST API/body/header/redirect changes | Not implemented; GET remains as before |
| Full selection, mouse drag, clipboard and control viewports | Not implemented; existing caret editing retained |
| WebP | No decoder added; no new source/license |
| GIF/static or animated | No decoder added |
| Bounded SVG subset | Not implemented |
| Conditional HTTP revalidation / 304 | Not implemented |
| Parallel TCP/worker sessions | Not implemented; maximum active request remains **one** |
| Parallel TLS | Not implemented or validated |
| JavaScript lexer/parser/executor | Not implemented; JavaScript remains disabled |
| Script DOM mutation/events/timers/external scripts | Not implemented |

The native networking inspection confirms that `siec.cpp` still owns global
per-connection TCP state and `siec.h` explicitly documents one active socket.
No worker-pool facade was added over that state. Parallel networking requires a
separate connection-handle/state refactor and tests before enabling concurrency.
Existing ScriptExecutor/ScriptDom/ScriptBudget boundaries remain scaffolding,
not a functioning JavaScript interpreter. Existing defaults are 100,000 script
budget units, 32 KiB source and depth 32, with no timers; these do not constitute
implemented language syntax or executable DOM APIs. SVG/script URL security
behavior is unchanged.

No claims are made for the requested POST, selection/clipboard, new codecs,
304, parallel transport or hostile JavaScript QEMU cases. Those require actual
implementations and corresponding release-ISO validation in later increments.

## Final source validation for this increment

All four host targets passed with ASan/UBSan and leak detection enabled:

| Suite | Checks |
| --- | ---: |
| New layout | 259,225 |
| Existing engine | 127,432 |
| Existing fidelity | 3,697 |
| Existing native runtime | 7,417 |
| Total | 397,771 |

Runtime also recorded 82 loading publications and 25 real HTTP-parser requests.
No sanitizer diagnostics were reported. Release `make iso` and
`make test-husarz-stack` passed; the largest browser stack frame is 4,272 bytes
(`reflow`), below the existing 6,144-byte per-frame cap. This is the existing
frame-size check, not a formal complete call-graph stack proof.

Machine-readable host results: `.husarz-test/milestone/host-results.json`.
Final host log: `.husarz-test/milestone-host-final.log`.
Native build/stack log: `.husarz-test/milestone-release-build.log`.

Release artifact SHA-256 values:

```
94e09d42b4f23003b2eb3b8f5f35524f47e01340361081f4ac08e5f9badbde3c  system_operacyjny.bin
fbd8d5fc90ffd2f8650d31c0c010e85ce145ffbb34e21c666b162f05095d5733  BursztynOS.iso
a6cc16abb63395ad216010aa92b0df9a8bfed2b991aaa0e4ddb12229ee621548  przegladarka.bin
b9778bf0b687e79f6cf47e7d99b45bcf1f4cecc47de9061c9cdf6b56757e5687  husarz_worker.bin
```

The focused release-ISO QEMU run **passed**:
`.husarz-test/qemu-milestone-layout-verified/result.json`. It checks all nine
color samples after initial load, native maximize and native restore: rowspan,
combined rowspan/colspan, three pixels across the wider shared blue border,
and the mixed-sign empty-block margin strut. Resize generated no additional
HTTP requests. No guest fault was observed. Screenshots are `layout-grid.png`,
`layout-maximized.png`, and `layout-restored.png` in that result directory.
The kernel/ISO hashes match the release hashes above.

An initial focused run failed because the new harness assumed the document
started two pixels inside the native window. Screenshot inspection established
that its x origin is the window x itself. Only that observer coordinate was
corrected; all nine assertions remain, and the verified run passed them.

## Resumed full regression, 2026-10-22

The interrupted full release-ISO regression was rerun to completion and **passed**.
Machine-readable results and screenshots are in
`.husarz-test/qemu-resume-regression/`; the complete observer log is
`.husarz-test/resume-qemu-regression.log`.

This run covers 24 anchored refresh frames, wheel/scrollbar/keyboard scrolling,
maximize/restore without another fetch, back/forward, malformed HTML, layout
fixtures, PNG/JPEG pixels and shared image cache across tabs, GET form submission
with UTF-8, closing tabs during image/CSS loading, live desktop themes and white/
black document theme isolation, Stop/navigation without stale HTML, and closing
the browser window during a request. No guest fault or forbidden script URL
request was observed. This does not change the outstanding subsystem list above.

`make iso` and `make test-husarz-stack` also passed. The largest browser frame
remains 4,272 bytes. Build log: `.husarz-test/resume-release-build.log`;
stack log: `.husarz-test/resume-stack.log`.

The rebuilt kernel matches the earlier validated kernel. The ISO hash below
identifies the newly generated image used for this full regression:

```
94e09d42b4f23003b2eb3b8f5f35524f47e01340361081f4ac08e5f9badbde3c  system_operacyjny.bin
45f46886dfbfb703d393e03b8cc0c64766f917dd22bf7ef08100a03920e8e37a  BursztynOS.iso
```
