# Husarz native scripting — 2026-10-23

Validation status: **PASS for the documented native JavaScript subset**.
All five final ordinary-release QEMU suites and host regression suites completed.
This is not ECMAScript conformance or a claim of modern website compatibility.
The numeric model, arena ownership, browser API subset and remaining limitations
below are part of the supported implementation.

## Architecture and ownership

`Js::Runtime` implements the existing `ScriptExecutor` interface and uses
`ScriptBudget`, `ScriptDom` identities, the existing `Work` coroutine ABI and
`WorkDriver`. There is no embedded host interpreter, STL container, RTTI,
exception unwinder, executable code generation, OS thread or new BWS interface.
Code compiles into document-owned bytecode once. The VM has explicit operand
and call stacks; JavaScript recursion does not recurse on the C++ stack.
Compiler recursion is implemented as bounded coroutine continuations.

Each `BrowserRuntime` belongs to a tab identity, navigation generation and
Document generation. The native browser owns it. Layout clones retain the
Document generation and rebind the idle runtime at publication. A layout job
and a suspended script cannot retain conflicting versions of the same document.
Navigation/Stop and tab/window cleanup cancel the coroutine tree before freeing
its source, document or pinned cache entries. Timers, queued events, listeners,
closures and all arena storage disappear with the runtime.

The VM has no filesystem, program execution, shell, kernel memory, hardware,
process enumeration, arbitrary IPC, raw network, WebAssembly or dynamic code
execution capability. Native browser functions are an explicit dispatch table.
Node values contain integer identities and a document generation, never native
pointers. Native function receivers also contain bounded opaque identities.

## Lexer, compiler and bytecode

The lexer recognizes ASCII identifiers, keywords, bounded decimal numbers,
single/double quoted strings, common escapes (`n`, `r`, `t`, backslash and both
quotes), punctuation, operators, line comments and block comments. Strings
validate UTF-8. Unknown escapes, unterminated strings/comments and malformed
UTF-8 fail safely. Embedded NUL strings are unsupported. Tokens retain source
line and column. Source and token caps apply before interpretation.

A bounded Pratt expression compiler and structured statement compiler emit
stack bytecode, with patched branches and explicit scope operations. The
compiler supports declarations, expressions, if/else, while, for, break,
continue, blocks, function declarations, anonymous function expressions and
return. It supports assignment, compound assignment, arithmetic, comparisons,
short-circuit logical operators, conditional expressions, member/index access,
function calls, array/object literals and postfix increment/decrement. Compound
member assignments evaluate their receiver/key once. Calls have at most eight
arguments/parameters. Explicit semicolons are required except at block/source
end. Function declarations execute in source order; full declaration hoisting
and temporal-dead-zone semantics are not implemented.

Bytecode uses constants, variable load/declaration/store, stack operations,
member load/store, binary/unary operators, jumps, scope entry/exit, closure
creation, calls/returns and literal object/array construction. Compilation
validates opcode, constant/function indices and branch targets. Runtime checks
instruction pointer, stack operations, frame limits and every native receiver.
There is no public script-facing bytecode injection interface.

## Values, scopes and memory

Values are tagged undefined, null, boolean, number, string, object, array,
function or native function, plus opaque browser wrappers. Objects use bounded
linear property tables; arrays use bounded indexed storage. No prototype chain
or unbounded hash table exists. Objects/arrays can reference themselves without
recursive traversal or recursive destruction.

Global, function and block environments use explicit scope records and linked
binding slots. `var` targets the enclosing function/global environment;
`let`/`const` target the current block and constant writes fail. Closed-over
scope chains are retained, including their parents. Uncaptured scope bindings
return to a bounded free list. Captured scopes and function values remain until
document cleanup. Runtime errors and budget aborts release uncaptured scope state using a bounded
pass over at most 256 scopes and 1,024 bindings. Repeated failed callbacks
therefore do not leak uncaptured bindings or VM frames.

Memory uses fixed document-generation arenas, **not garbage collection**.
Immutable strings, bytecode, constants, objects, arrays and closures have hard
caps. Allocation exhaustion raises a contained error; it never grows an arena.
Long-lived pages may exhaust their arena and must navigate/reload to reclaim it.
No mark stack or recursive graph tracing exists.

The native application ABI disables FPU/SSE and does not establish FPU context
ownership. Numbers therefore use checked **fixed decimal arithmetic, three
fractional digits, range ±10^12**, without changing kernel scheduling. Arithmetic
overflow and division by zero produce RangeError. This is a substantial language
limitation, not IEEE-754 JavaScript Number semantics. Additional nonzero decimal
places in literals are rejected; division truncates to the supported precision.

Strings use UTF-8 and codepoint-based `length`, `charAt`, `slice`/`substring`,
`indexOf` and casing. Casing covers ASCII and Polish letters. Slicing and form
assignment preserve complete UTF-8 sequences. This is not ECMAScript UTF-16
indexing or comprehensive Unicode case mapping. Concatenation checks capacity
before copying and charges work while copying; no unbounded builder exists.
Arrays support length, indexed read/write, push and pop. Object property reads,
writes and comparisons validate all indices and bounds.

| Limit | Value |
| --- | ---: |
| Source per script | 32,768 bytes |
| Tokens per compilation | 8,192 |
| Identifier/property name | 64 bytes |
| Compiler nesting | 32 |
| Bytecode per document runtime | 16,384 instructions |
| Constants | 2,048 |
| String count / individual length / string bytes | 2,048 / 4,096 / 128 KiB |
| Bindings / scope records | 1,024 / 256 |
| Compiled functions / retained closure values | 128 / 128 |
| Arguments/parameters | 8 |
| Objects / properties per object | 128 / 32 |
| Arrays / elements per array | 64 / 128 |
| Operand stack / call frames | 256 / 32 |
| Work budget per top-level execution/callback | 100,000 |
| VM instructions per execution slice | at most 256 |
| Shared deterministic work slice | at most 2,048 units |
| Listeners / timers / queued events | 64 / 16 / 32 |
| Timer delay | 10 ms to 3,600,000 ms |
| Selector input / result limit | 256 bytes / 64 nodes |
| innerHTML input / serialized output | 4,096 bytes |
| DOM nodes / depth / attributes | existing 2,048 / 64 / 8,192 limits |

Instruction budget includes charged native bridge work. Selector work also has
a bounded CSS matching budget. When the total budget is exhausted, the owning
execution is canceled and its coroutine-owned temporary buffers are released.
An infinite handler can abort its current dispatch; an unprevented native
default action is preserved. Other jobs continue. A slice yield keeps execution
state and returns to the native animation/event loop. Stop destroys the runtime
and its callbacks, then finishes any committed DOM mutations through a new
cooperative style/layout publication rather than leaving repaint blocked on
stale geometry.

Runtime storage is fixed-size and measured by the host suite. Compiler token
storage is a separate bounded allocation. Existing coroutine frame storage is
capped at 512 KiB **across the browser**. Structural DOM transaction staging is
bounded by MaxNodes; innerHTML additionally owns one bounded temporary Document.
These temporary DOM/compiler allocations are distinct from the JS value arena.
Measured release sizes: Runtime **775,384 bytes**, BrowserRuntime
**785,784 bytes**, Compiler **209,176 bytes**, TreeMutation **174,088 bytes**.
Release Document is **2,632,928 bytes**, up 12,296 bytes from the editing/GIF
checkpoint, for stable identities/indexing and detached-node state. FormControl
remains 2,084 bytes. The instrumented performance Document is 2,633,272 bytes.
The runtime allocation is a hard bound per scripting document (at most six tabs);
compiler and structural staging allocations are temporary. innerHTML temporarily
allocates one additional bounded Document. These are separate from the shared
512 KiB coroutine-frame cap.

## DOM bridge and mutation

`NodeHandle` extends the existing generation/index representation with a stable
node identity. `ScriptDom::by_identity` resolves the current physical index.
Tree nodes never reuse an identity within a document. Moving a subtree preserves
identity; replaced descendants lose theirs. Every native receiver checks runtime
ownership, document generation and current identity. An obsolete receiver cannot
reach a newly occupied physical index.

The bridge supports getElementById, querySelector/querySelectorAll, body,
documentElement, createElement and createTextNode. Queries reuse the existing
bounded CSS selector compiler/matcher. Node APIs include textContent, innerHTML,
id, className, get/set/removeAttribute and append/remove/insertBefore/replaceChild.
`classList` exposes add/remove/contains/toggle. Style wrappers translate camelCase
names and use the existing CSS declaration parser. Raw renderer structs are not
exposed. Form wrappers read/write value and checked; select values use option
value attributes. Newly created controls receive bounded native control state
without resetting existing form edits.

Structural mutation stages the tree, validates ancestry/depth, computes a
bounded iterative preorder and an old/new index map, then publishes one bounded
node transaction. Controls, focus, images and script metadata are remapped.
Removed descendants invalidate script handles and listeners. Detached nodes can
remain referenced and reattached; detached nodes do not participate in layout or
queries. Old native mouse selection is canceled when a structural revision
changes. This preserves the renderer's preorder assumptions.

innerHTML uses the existing bounded HTML parser in a temporary Document, imports
bounded nodes/attributes and invalidates replaced descendant identities. It does
not register or execute imported script elements. SVG ancestor scripts are also
skipped for initial inline/external script execution. Event handler string
attributes are inert. Fragment parsing uses the existing document parser's
repair behavior; complete contextual HTML5 fragment parsing is not claimed.

Mutations update revision/dirty flags and coalesce browser work. Text and
attribute/class/style changes conservatively request cooperative style/layout;
form value/checked changes use native paint invalidation. The style wrapper does
not yet optimize geometry-independent CSS declarations to paint-only. No setter
calls synchronous parse/reflow/paint of the live page. A long append loop cannot
perform a complete layout on every append. After a mutating job, the scheduler
lets its requested layout/paint publish before starting another callback, so a
fast interval cannot permanently starve painting. The existing resize coalescing,
dirty rectangles, viewport culling and private layout publication remain active.

## Events, defaults and forms

Listener records are bounded and document-owned. Dispatch snapshots stable
ancestor identities and a listener serial cutoff. Capture walks root to target;
target and bubble walk back to the root. Removed/invalid identities are skipped;
new listeners do not enter an already-started dispatch. removeEventListener
checks callback identity and the boolean capture flag. Event properties such as
onclick/oninput/onchange/onsubmit/onload use separate listener records and do not
remove listeners installed by addEventListener.

Events expose type, target, currentTarget, eventPhase, defaultPrevented,
preventDefault and stopPropagation. Keyboard events expose normalized key/code
strings and ctrl/shift/alt flags, without a privileged hardware interface.
Native hooks cover click, mouse down/up/move, input, change, submit, focus/blur,
key down/up and load. Mousemove jobs coalesce. There is no stopImmediatePropagation,
custom dispatchEvent API or listener options object; capture accepts a boolean.
Window and Document listeners share the document root in this subset.

Click activation uses the browser's existing mouse-press activation timing.
Native link/form/checkbox defaults run after handlers. If a target disappears,
its default is discarded. An inactive tab cannot hijack the visible tab's UI.
Checkbox state changes happen after the cancellable handler, rather than using
full HTML pre-activation rollback semantics. Submits use the existing form URL
construction and current transport support; scripting does not add POST.

Keyboard command defaults are deferred through the same queue. A text insertion
is associated with its preceding normalized keydown; prevented keydown suppresses
that insertion. Native clipboard/password protections remain in the existing
form editing path. Successful typing/cut/paste/deletion and textarea newline edits emit input.
Blocked password cuts, failed clipboard operations, full fields and no-op
deletions do not emit input. Checkbox/select operations emit events only when
the selected state changes. Text changes
are tracked until blur for change, while checkbox/select changes emit change
at their native operation. Script assignments do not automatically synthesize
user input events. IME composition APIs are not implemented.

Load is emitted after ordinary scripts have been processed. This is a practical
script-ready event, not the complete HTML load lifecycle for all images/resources.

## Timers, scheduler and resources

setTimeout/clearTimeout and setInterval/clearInterval use monotonic browser time.
Timer IDs do not reuse within a runtime; invalid IDs do nothing. Due timers
receive a reserved scheduling turn even with continuously queued input, and a
keydown waiting for its text pair does not block unrelated callbacks. All callbacks
consume the ordinary execution budget. An interval schedules from the current
time and never runs an unbounded missed-tick catch-up loop. Events/timers and
ordinary scripts have bounded scheduling opportunities; tabs get round-robin
JS slices. Layout and painting retain their own cooperative work slices.

Ordinary inline and external scripts execute in source order after the initial
DOM has been parsed. A later script waits for an earlier unresolved external
script. Full parser-blocking loading, defer and async semantics are unsupported;
async/defer attributes do not enable reordering. External JavaScript is a new
type in the existing ResourceQueue/worker/ResourceCache pipeline. It uses the same
URL resolution, HTTP success checks, MIME checks, source cap, worker identity,
request ID, tab/navigation/document checks and cancellation. Accepted MIME types
are text/javascript, application/javascript and application/x-javascript.
HTTP error pages are not executed.

Script source entries are pinned until runtime cleanup. Cache freshness and
transient no-cache/no-store handling follow this branch's existing conservative
implementation. Conditional ETag/Last-Modified/304 revalidation was **not present**
in this branch and is not invented or claimed here. There is no separate JS
cache or direct script networking path.

location.href reads the owning document's URL. Assignment is handed to the normal
browser URL resolver/navigation pipeline and its generation cancellation. Only
supported HTTP(S) navigation can proceed; javascript: URLs are rejected.
The navigation mailbox publishes the complete URL atomically after a bounded
copy checkpoint. A test forces a yield at that boundary with a 1,899-byte URL
and verifies that no partial prefix becomes visible to browser navigation.

console.log/warn/error are bounded no-op development call counters (64 retained
calls), without serializing arguments. This deliberately avoids exporting
passwords or arbitrary form/script data to BWS diagnostics. Development error
records contain source script index, error category, line and column, without
source excerpts. Release builds disable verbose diagnostics.

## Unsupported features

This is not ECMAScript compliance. In addition to the numeric/string differences:
no eval, Function constructor, executable strings, modules, dynamic import,
WebAssembly, classes/prototypes/constructors, this binding, generators,
async/await, Promises/microtasks, Proxy/Reflect, SharedArrayBuffer, regex literal,
try/throw/catch, switch, destructuring, template strings, arrow functions or
prefix increment/decrement. Named function-expression self-binding and full
hoisting/TDZ/loop-per-iteration lexical semantics are not provided. Operators use
restricted typed coercion rather than the full ECMAScript conversion rules.

No fetch/XMLHttpRequest, raw sockets, storage API, browser extension API,
getComputedStyle snapshot, CSS animation API, arbitrary navigation privilege,
filesystem or OS API is exposed. Dynamic network-resource discovery after arbitrary
DOM insertion is not a full modern loader. The fixed arenas are not reclaimed by
GC, so allocation-heavy long-lived pages can reach a contained resource error.

## Native packaging

The browser TEXT reservation grows from 384 KiB to 512 KiB to accommodate the
engine. Its DATA reservation remains 512 KiB. The worker gets its own 64 KiB TEXT
reservation through the same parameterized linker script; it does not retain
unused VM code. Its own .bur header declares its corresponding DATA address.
This removes unused worker padding, keeping the embedded release kernel under
the existing 14 MiB guard. The kernel/Ring 3 boundary, process stack, process
fault policy and BWS buffer width are unchanged. Packaging tests inspect both
headers and the worker's own low BSS buffer bounds.

## Validation record

All final native runs use the ordinary release ISO with HUSARZ_DEBUG=0 and
HUSARZ_PERF=0, without replacement browser payloads. The unchanged artifact
copies are in `.husarz-test/javascript-release/`. The full build and ISO build
completed; repeating the full build reproduced the same kernel hash.
Earlier diagnostic runs are excluded from final acceptance.

| Final host group | Result / checks |
| --- | --- |
| JavaScript | PASS / 9,974 |
| Native browser runtime / forms / worker publication | PASS / 7,689 |
| Engine | PASS / 127,432 |
| Layout | PASS / 259,225 |
| Cooperative performance | PASS / 4,594 |
| Fidelity / cache | PASS / 3,697 |
| UTF-8 selection / clipboard / field viewports | PASS / 987,380 |
| GIF | PASS / 11,656 |
| Husarz total above | **PASS / 1,411,647** |
| NetworkDevice | PASS / 12 groups |
| Native keyboard / TEXTINPUT / dialogs | PASS / 7,291 |
| BursztynUI commands | PASS / 23 |
| Ring 3 fault policy | PASS / 32,768 |
| Browser/worker packaging | PASS / 18 |
| Shared images | PASS / JPEG, PNG, packaging A–O and manifest regression |

Final host logs: `.husarz-test/js-release-host.log` (JS/native runtime),
`.husarz-test/js-verified-regression-host.log` (remaining Husarz groups),
`.husarz-test/javascript-auxiliary-host.log` (network/input/UI/fault policy),
`.husarz-test/js-final-image.log` and `.husarz-test/javascript-packaging.log`.
The image run uses the pre-existing deterministic
`BURSZTYN_TEST_PNG=.husarz-test/png-reference.png` because the optional default
wallpaper source is absent. No missing-source invocation counts as PASS.

Final JavaScript ASan/UBSan suite: **PASS, 9,974 checks**, including mutated
compiler input, token/string/source limits, operand-stack exhaustion, recursion,
arena exhaustion, large numeric boundaries, invalid timer IDs, event-path mutation,
repeated runtime-error recovery, timer fairness and native edit-result reporting.
Observed maximum operands: **256**, call frames: **32**, retained string bytes:
**9,016**, live coroutine-frame payload peak: **17,024 bytes**. The hostile-loop
tests terminate at the 100,000-work-unit limit, with every observed slice within
256 VM instructions and 2,048 shared work units.

ASan allocator snapshots after each VM slice observed a live allocation peak of
**6,963,288 bytes above process startup** in this suite. This includes the live
Document, JS runtime, compiler, innerHTML fragment and transaction staging; it is
a sampled host-workload peak, not a whole-browser guest memory measurement.
The deterministic arena bounds above do not depend on this measurement.

Native compiler stack audit: **PASS**, largest frame **4,176 bytes**
(`ustaw_url_z_location`); the 16 KiB native process stack is unchanged.
This is a per-function compiler audit rather than a complete call-graph proof.
Native browser/worker packaging: **18/18 PASS**. Release browser payload is
528,704 bytes; worker payload is 69,888 bytes. No private replacement payload is
used in release acceptance.

## Final release-QEMU acceptance

| Group | Result / record |
| --- | --- |
| JavaScript / DOM / events / forms / timers / external cache | [PASS](../.husarz-test/qemu-javascript-release/result.json) |
| Full native browser regression | [PASS](../.husarz-test/qemu-javascript-full/result.json) |
| Heavy pages / maximize-restore / responsiveness | [PASS](../.husarz-test/qemu-javascript-heavy/result.json) |
| UTF-8 editing / clipboard / password protection / GIF | [PASS](../.husarz-test/qemu-javascript-editing/result.json) |
| Table geometry / signed margins / resize | [PASS](../.husarz-test/qemu-javascript-layout/result.json) |

JavaScript acceptance verifies visible inline/function/loop output, DOM creation,
text/style mutation, click handlers, bubbling, link and submit prevention,
keydown cancellation, native input events, timers and ordered external scripts.
The external source is fetched once and reused on reload. Infinite-loop budget
termination and syntax/runtime errors leave subsequent scripts and another tab
usable. The run exercises real app switching, maximize/restore during script-driven
updates, Stop, navigation cancellation, closing a tab with an active timer,
closing the browser with active script state and desktop/web theme independence.
No Ring 0 panic or unexpected Ring 3 fault occurred in any final suite.

The heavy run covers all six heavy fixtures and eight maximize/restore
transitions. Wheel/thumb/keyboard scrolling makes no additional resource fetches;
app switching, Stop and closing during work pass. Full regression completes
resource/cache/form/history/scroll/resize/theme/cancellation/window-close paths.
Geometry and editing suites preserve the prior table, margin, GIF and clipboard
behavior. Native automation waits in guest time and requires stable idle frames
before examining a newly navigated page, avoiding observations of old pixels.
These are completed release runs, not partial or interrupted results.

[Machine-readable final validation](../.husarz-test/javascript-validation.json)
contains host counts, limits, measured peaks, hashes and links to all records.

Final hashes:

- `BursztynOS.iso`: `c8757532cf316393d14906261933cbe12596f41fb94d381213e205caabba9802`
- `system_operacyjny.bin`: `03ba5da4408d343cb40200918bf66ec3216aaa022b7e1eccff83c90796acd698`
- `przegladarka.bin`: `291f1091a8087a91ced928af2798a86b0d8ccdb958d7792aed1528cd73ee0123`
- `husarz_worker.bin`: `945e9850985df4dd273655558b6c64af3c8c101d17688cab676db6de1078613c`

## Files

New production files: `programy/husarz/js_number.h`, `js_core.h`, `js_dom.h`,
`script_runtime.inc`.

Integration: `programy/husarz/script.h`, `document.h`, `css.h`, `pipeline.h`,
`pipeline_runtime.inc`, `resource.h`, `forms.h`, `async.h`, `async_runtime.inc`, `worker.inc`,
`programy/przegladarka.cpp`, `programy/przegladarka_linker.ld`.

Tests/build/docs: `Makefile`, `narzedzia/test_husarz_javascript.cpp`,
`narzedzia/test_husarz_runtime.cpp`,
`narzedzia/test_przegladarka_gui.py`, `narzedzia/test_husarz_qemu.py`,
`narzedzia/fixtures/husarz/js-*.html`, `js-external.js` and this report.
