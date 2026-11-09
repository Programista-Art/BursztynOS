# Husarz: binary64, object semantics and bounded asynchronous APIs

Status: implemented; host and ordinary release-QEMU milestone acceptance complete.
The wider kernel-stack audit has three findings in unchanged functions, listed below.
This report supersedes
the numeric and unsupported-language sections of the first JavaScript report.
It does **not** claim ECMAScript conformance or modern website compatibility.

## Kernel floating-point ownership

`fpu.h` / `fpu.cpp` provide a kernel-owned, 16-byte-aligned 512-byte FXSAVE64
image for each of the 16 scheduler slots: **8,192 bytes** total. PID0 has an
independent idle image. The current scheduler is uniprocessor; SMP would require
per-CPU ownership and migration synchronization.

Initialization uses CPUID leaf 0 and leaf 1. It requires x87, FXSR, SSE and SSE2.
Missing required features stop startup before user execution. CR0 explicitly sets
MP and NE and clears EM and TS. CR4 sets OSFXSR and OSXMMEXCPT and clears OSXSAVE.
There is no lazy #NM switching. No XSETBV is issued and no AVX state is enabled.

This milestone deliberately uses **FXSAVE64/FXRSTOR64 on all supported CPUs**,
including CPUs advertising XSAVE. Supporting only x87/SSE/SSE2 permits one fixed
validated image format and avoids importing an extended-state layout into the
scheduler. XSAVE optimization remains unimplemented; there is no assumption that
XSAVE exists. The image includes x87 registers/control/status, XMM0–15 and MXCSR.
The control-bit and state-format policy follows the
[Intel system programming manual](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf).

The scheduler initializes FPU ownership before publishing PID0. The loader resets
an image while reserving an inactive process slot. Defaults are synthesized from
zeroed memory: FCW `0x037f`, empty x87 tags, MXCSR `0x1f80`, zero register payloads.
They do not copy whatever hardware state another process happened to leave.
Deferred process cleanup also resets the image before publishing an empty slot.

After selecting and validating an incoming context and completing its CR3/TSS
update, the scheduler saves the outgoing image and restores the incoming image
with interrupts disabled, before publishing the new current PID. Same-PID returns
need no save/restore. This also covers switches during the existing BWS38 wait.
Ordinary syscall and interrupt paths do not use SIMD and therefore preserve the
current owner's state without an additional trap-local FPU image. Explicit
network-wait HLT points use the same private kernel-stack mechanism as BWS38;
eager FPU ownership also covers those suspended syscalls.

Kernel C++ compilation adds `-mgeneral-regs-only` to the existing no-MMX/no-SSE
policy. The native browser and resource worker explicitly enable SSE2 double
arithmetic and disable AVX. Other Ring 3 programs automatically receive process
isolation regardless of their compiler flags. Kernel TLS AESNI/Padlock remain
disabled. `test-kernel-simd` disassembles kernel text, excluding embedded program
data, and rejects SIMD registers and x87 arithmetic outside the explicit context
machinery. Existing user-fault policy still isolates #MF and #XM, as well as #UD
and page faults; kernel faults/NMI/double fault/machine check remain fatal.

The ordinary ISO installs two native diagnostic programs, `fpu-a.cebula` and
`fpu-b.cebula`, from `programy/fpu_probe.cpp`. They check fresh state, distinct
values in all 16 XMM registers, x87 values/control words, MXCSR rounding modes,
syscall waits and interrupt-driven switching. Each performs 48 wait/busy-loop
rounds. The leader then runs a deliberately faulting partner and launches another
partner to check clean process reuse. These are native diagnostic capabilities,
not APIs available to JavaScript. The final QEMU runner permits only that one
expected partner #UD; every other user fault still fails acceptance.

## Numbers

`Js::Numeric` contains one **IEEE-754 binary64 `double`**, replacing the old
fixed-decimal payload. Arithmetic uses native SSE2 operations. There is no second
fixed-decimal number type or three-digit fractional limit. NaN, infinities,
signed zero, subnormals, unordered NaN comparisons and masked division by zero
are supported. Remainder uses a bounded power-of-two reduction, with at most the
binary64 exponent-range number of steps, without libc/libm or a host VM.

The lexer accepts decimal integers, fractions, exponent notation and hexadecimal
literals. Numeric token input is capped at 128 bytes. Bounded freestanding parsing
and formatting avoid host libc formatting. `Number()`, `String()`, strict
`Number.isNaN/isFinite`, coercing global `isNaN/isFinite`, global NaN/Infinity,
Number NaN/infinity constants, EPSILON, MAX_VALUE, MIN_VALUE and safe-integer
constants are present.

Parsing retains up to 19 significant decimal digits and applies binary64 decimal
scaling. Formatting searches bounded decimal candidates for a round trip. These
small conversion routines are **not a complete correctly-rounded ECMAScript
StringToNumber/shortest-decimal implementation for every possible input**.
Arithmetic itself is binary64. General ToPrimitive/ToNumber coercion is still
limited: arithmetic primarily expects Number operands, with the existing string
concatenation behavior. Strings use the browser's UTF-8/code-point model rather
than full ECMAScript UTF-16 indexing.

Native integer conversion checks finiteness and representable range before any
C++ cast. Timers additionally clamp delays and validate integral timer IDs.
Storage indices require finite nonnegative integers. Array property indices use
bounded decimal-name parsing. DOM handles remain typed integer identity records,
not numbers cast into pointers. NaN/infinity/negative-index tests run under UBSan.

## Receivers, prototypes, constructors and arrows

Method-call bytecode preserves the receiver separately from the callable. VM
frames save the previous receiver; `this` loads the current receiver. Plain
functions use undefined `this` in this strict practical subset. Detached functions
lose the old receiver. Native instance methods require an actual call receiver
instead of silently retaining their previous object. Global/static helpers use
only the capabilities attached to their native IDs.

Event callback bytecode supplies the safe currentTarget wrapper as the receiver.
The existing document/window listener alias uses the Document wrapper. Arrow
functions retain lexical `this`, including when invoked as event callbacks.

Plain objects have an optional Object/null prototype reference. Own-property
lookup precedes inherited lookup. The maximum chain is **16 object records**.
Creation checks depth; setPrototypeOf rejects cycles and prospectively checks all
existing descendants before publishing a mutation. Both lookup and mutation
validation consume bounded work. Object.create/getPrototypeOf/setPrototypeOf are
provided for the supported object model. Native browser wrappers cannot have
arbitrary prototypes installed.

Object, Array, Function, String and Number prototype roots are document-owned
objects. Supported string/array methods are installed on their prototype roots.
User function objects own bounded property storage and an ordinary `.prototype`
object with a `.constructor` relationship. `new F()` allocates the instance using
F.prototype when supported, invokes F with that receiver, and honors an object
return over the new instance. A primitive return preserves the instance. Arrow
and async functions are not constructors. Dynamic Function construction remains
disabled. Legacy `__proto__` is an ordinary property, not a native-memory setter.

Arrows accept one unparenthesized parameter or a parenthesized list, expression
bodies and block bodies. The existing limit of eight parameters applies. Their
captured scopes use the existing bounded retained-environment arena. No boundless
native recursion or new environment allocation policy is introduced.

## Exceptions

Throw/try/catch/finally use explicit VM completion records and validated handler
metadata. There are at most **64 handler metadata records and 64 active handlers**
per runtime. Handlers snapshot VM frame depth, operand depth and scope identity.
Unwinding occurs in the interpreter loop and bounded coroutine helpers, never
through native C++ exceptions.

Completions distinguish normal flow, return, throw and break/continue jumps.
Finally executes for supported normal and abrupt completions; a return/throw from
finally replaces the previous completion. Catch creates a scoped binding for the
thrown primitive/object. Supported runtime TypeError, ReferenceError, RangeError
and runtime SyntaxError (including JSON parsing) become catchable objects with
bounded `name` and `message`. Error constructors are present. Stack traces and
complete standard Error prototype relationships are absent.

Budget, cancellation and arena exhaustion remain fatal to the current job and
may bypass catch/finally. This prevents handlers from evading safety limits.
Allocation-failure guards prevent failed scope creation/string conversion/error
creation from publishing parameter bindings, storage mutations or DOM text/style
changes after an error.

## Promise, microtasks and async functions

Promises have pending/fulfilled/rejected states and settle once. Executors run in
explicit VM frames. Resolve/reject wrappers carry a validated promise identity,
not a C++ closure pointer. Async executors keep an explicit outer-promise return
identity while their own async promise is suspended or completed. Reactions are linked bounded records; settlement queues
reactions rather than running callbacks inline. Then returns a child promise;
values fulfill it, throws reject it, and returned native Husarz promises are
adopted. Direct self-resolution and bounded adoption cycles reject safely.

Supported APIs are new Promise(executor), Promise.resolve/reject, then and catch.
General thenable assimilation, Promise.finally and the aggregate Promise helpers
are not implemented.

The document owns a FIFO microtask ring. A checkpoint processes at most **16
microtasks**, within the normal instruction/work budgets. Additional checkpoints
are scheduled later. Pending microtasks receive two scheduler selections before
an ordinary-task selection; rendering, GUI input and network publication remain
outside the VM and run between slices. This is bounded fairness rather than an
unlimited drain-to-empty policy. Hostile chains eventually exhaust fixed document
arenas; they cannot allocate indefinitely or monopolize the native event loop.

Async function declarations/expressions return Husarz promises. Return fulfills
that promise; throw rejects it. Await performs Promise resolution, detaches the
async portion of the explicit VM state into a bounded continuation, and restores
the caller. A later reaction queues a continuation resume. Resumption restores
operand/frame/handler/scope/receiver state, then pushes a fulfilled value or
injects a rejected value through normal VM exception handling. There are no
native stacks retained for suspended JS functions.

A resumed async root uses a terminal frame, so a second await cannot reexecute its
old caller. Continuation copies have cancellation-safe heap owners. Async arrows,
top-level await and await in ordinary functions remain unsupported. Navigation,
Stop, tab close and runtime destruction cancel coroutine work, discard microtasks
and release all continuation records. Captured DOM values retain the existing
document-generation checks.

## Fetch and origin policy

`fetch(url)` and `fetch(url,{method:'GET'})` return Husarz promises. Other options
and methods, including POST, are rejected rather than silently converted. The
current native resource GET stack does not provide the required safe POST path.

The VM publishes bounded request records. Browser-native integration resolves
URLs using the existing navigation/resource resolver, compares normalized
scheme/host/effective-port origins and places requests in the ordinary resource
queue. Only HTTP/HTTPS and **same-origin** response access are allowed. Userinfo,
invalid authority/control characters and unsupported schemes are rejected.
Explicit ports remain subject to the native network stack's existing restrictions.
No CORS support is claimed.

Requests carry tab/navigation/document identity through the existing worker and
shared-memory completion protocol. They use the existing TCP/TLS/HTTP functions
and ResourceCache with a Fetch resource type, not a socket API or parallel JS
cache. Freshness, no-cache/no-store/transient handling reuse the existing cache
policy. ETag/Last-Modified metadata are retained, but conditional 304 revalidation
was not already implemented and is not added here.

Redirect count is deliberately **zero**: all fetch 3xx responses reject. No
redirect can cross an origin check. HTTP error responses otherwise produce a
Response with their status and ok=false. There are four fetches total per runtime
and a 4,096-byte body cap; this arena-based milestone does not reclaim request
slots during a long-lived document. Worker decoding rejects oversized fetch
bodies. Failed/oversized completions do not pin oversized response storage.

Responses expose ok, status, url, text() and json(). Body access is single-use.
Text/JSON return promises and copy into bounded VM strings; json() uses the JSON
codec below. No raw response address is exposed. Header maps, statusText, streaming,
AbortController, arbitrary request headers, request bodies, cookies/credentials
and cross-origin access are not implemented. Header override/CRLF injection is
therefore unavailable to scripts.

Stop deletes the document runtime and cancels/discards its fetch work. Old queue
completions cannot find a matching runtime in a newer navigation. Network-worker
failure/timeout rejects outstanding fetches instead of leaving them indefinitely
pending. Native completion validates message identity, payload kind and size.
A discarded in-flight transfer can retain the single native transport until its
completion or network deadline; cancellation does not claim immediate wire-level
socket abortion. GUI processing continues during the explicit network waits.

Worker startup requires a nonce-matched IPC hello. BWS10 can report success by
forwarding an argument to an older single-instance worker that is still exiting;
that return value alone cannot establish the new browser's worker ownership.
While awaiting hello, the browser retries at most **90 launches**, at **500 ms**
intervals, within the existing **45-second overall timeout**. Retries run from
the cooperative event loop and do not extend the timeout or create parallel
network workers. This bounds startup while an older worker exits.

The existing blocking native network calls now yield at explicit polling waits
for ARP/DNS/TCP/HTTP and TLS retry. A kernel-owned lease retains exclusive use of
the existing single transport across those waits; competing DNS/HTTP calls and
raw socket operations fail safely instead of replacing its state. No parallel
TCP/TLS transport or new script API is introduced. The timer may switch a Ring 0
network caller only during its explicit HLT, with its private kernel stack
preserved. IRQ handlers, interrupt-disabled critical sections and unleased boot
networking do not yield. Polling deadlines remain in force; the bounded socket
close poll additionally has a 100 ms wall-clock deadline. Process cleanup resets
the transport and releases a dead owner's lease.

## JSON

JSON.parse/stringify use a dedicated bounded codec, never eval or the JS compiler.
Parsing checks JSON punctuation, number grammar, string escapes/UTF-8 and Unicode
surrogate pairs. Source/output size is 4,096 bytes, depth 16, value visits 512,
with existing object/array/string arena caps. Recursive coroutine helpers have an
explicit depth cap; every scan/traversal consumes cooperative work.

Stringify handles null/booleans/numbers/strings/arrays/plain objects, own properties,
non-finite numbers as null, undefined/function object properties by omission and
unsupported array values as null. Ancestor tracking detects cycles. Custom
accessors, toJSON/replacer/reviver/space behavior and exotic objects are outside
the subset. Embedded NUL remains unsupported by the current browser string ABI.

## Browser-owned storage

Both storage APIs expose length, key(index), getItem, setItem, removeItem and
clear. Only strings cross the storage boundary, using the supported bounded
string conversion. Arbitrary property syntax and storage events are absent.
Quota errors are contained RangeErrors; unavailable/failed backing storage yields
TypeError. Replacing a value validates the full quota before changing an entry.

Origin keys are canonical `scheme://lowercase-host:effective-port` strings.
Local namespaces are shared only for identical origins. Session namespaces add a
stable tab identity, survive same-tab reload/navigation and disappear when that
tab is destroyed. Tabs are independent; cloning semantics are not implemented.

Persistent storage uses `/uzytkownicy/husarz/` on the existing native BSP2/PSF
filesystem. A 64-bit hash of the canonical origin chooses a hexadecimal basename;
web keys and values never choose path components. A complete origin string is
stored and compared in every record, so a hash collision cannot disclose another
origin's data. Both generations are checked before reads and writes; a valid
record for another origin makes the colliding namespace unavailable instead of
overwriting that origin. The worker has the required native file permissions; no general
filesystem capability is exposed by the VM.

The little-endian versioned format contains magic, total size, revision, entry
count, origin length and checksum, followed by the origin and length-prefixed
UTF-8 key/value records. It contains no pointers or handles. Decode validates the
entire bounded record before replacing a live namespace. Corruption is confined
to that origin and falls back to a valid alternate generation or an empty store.

Updates write the opposite of the newest valid committed `.a`/`.b` generation,
choosing the highest valid revision on load. Mutation revisions may skip when
writes coalesce; destination selection does not use mutation-revision parity.
The checksum covers both header and payload. Files are created through the
native API before writing. At most eight files are admitted in the dedicated
directory, each at most 20 KiB, giving a 160 KiB disk-file cap and at most eight
distinct on-disk namespaces (usually four two-generation namespaces). This is
recovery from a damaged record, not a claim of transactional filesystem metadata
or power-loss durability on every device.

JS mutates bounded browser-owned RAM; filesystem operations run in the existing
resource worker. Dirty snapshots are coalesced while a write is in flight. Initial
loads complete before native page scripts start. Graceful browser close cancels
JS and gives pending writes up to two seconds/512 cooperative waits to drain.
The browser window overrides close to flush storage and stop its worker before
the base GUI method's non-returning process-exit syscall. The File/Exit command
uses that same path. Cleanup placed only after the GUI loop would be bypassed.
Abrupt process loss or worker/device failure can lose an uncommitted latest
snapshot. Installed-system reboot durability depends on the underlying persistent
PSF volume; a live RAM filesystem naturally lasts only for that boot.

## Explicit bounds and scheduling

| Resource | Bound |
| --- | --- |
| VM operands / function frames | 256 / 32 |
| Script work budget / VM instructions per slice | 100,000 / 256 |
| Shared cooperative work per slice | 2,048 units |
| Script source / numeric token | 32,768 / 128 bytes |
| Bytecode instructions / function and closure records | 16,384 / 128 each |
| Scopes / bindings | 256 / 1,024 |
| Object / array records | 128 / 64 |
| Properties per object / elements per array | 32 / 128 |
| Strings / individual string / string arena | 2,048 / 4,096 bytes / 128 KiB |
| Prototype records per chain | 16 |
| Exception metadata / active handlers | 64 / 64 |
| Promises / reactions / queued microtasks | 128 / 256 / 64 |
| Microtasks per checkpoint / adoption walk | 16 / 32 links |
| Suspended async continuations | 8 |
| Fetch requests / retained response body per request | 4 total / 4,096 bytes |
| Fetch redirects | 0 |
| Worker startup retry interval / attempts / overall timeout | 500 ms / 90 / 45 seconds |
| JSON source/output / depth / visited values | 4,096 bytes / 16 / 512 |
| Local origins resident in a browser process | 4 |
| Session namespaces across tabs/origins | 24 |
| Storage entries / key / value / payload quota per namespace | 16 / 128 bytes / 1,024 bytes / 16 KiB |
| Persistent files / file size / global file-byte cap | 8 / 20 KiB / 160 KiB |
| Timers / minimum delay / maximum delay | 16 / 10 ms / 3,600,000 ms |

All capacities are compiled constants. A web number cannot select an allocation
capacity. There is no new host thread, STL runtime, RTTI, native C++ exception
unwinding or external JavaScript engine. Existing process fault cleanup, HTTP
workers, cooperative style/layout/paint publication, password/clipboard policy
and document handles remain in use. Arena exhaustion can terminate a job and
long-lived pages may need reload; no garbage collector is introduced.

## Remaining language and browser gaps

Besides the specific deviations above, this remains a small language subset:
no full hoisting/TDZ/ASI specification, general numeric coercion, typeof/instanceof,
bitwise/exponentiation operators, destructuring/default/rest/spread parameters,
template literals, classes, modules, generators, regexp syntax, descriptors,
getters/setters, symbols, proxies, iterators, Map/Set/Date/Math/typed arrays or
complete standard prototype libraries. call/apply/bind and arguments semantics
are absent. Array construction is the bounded element-list subset, and Number /
String constructors do not provide full boxed primitive semantics.

DOM/CSS/events/forms retain their existing supported subset. There is no layout
or web-platform conformance claim. Workers, WebSocket, unrestricted networking,
XHR, service workers, browser module loading, CORS, general file APIs and OS
capabilities remain unavailable to page scripts. SVG scripts and javascript: URLs
remain disabled. This milestone does not establish modern website compatibility.

## Validation and measured artifacts

All **eight final native groups pass** on the same ordinary release ISO with
`HUSARZ_DEBUG=0` and `HUSARZ_PERF=0`. Development and interrupted candidate runs
do not count as final acceptance. The modern and existing JavaScript suites are
separate groups, with their full respective checks retained.

| Release-QEMU group | Result and native record |
| --- | --- |
| FPU isolation, binary64, objects, exceptions, arrows, Promise/async, fetch/JSON, hostile workloads | [PASS](../.husarz-test/qemu-v3-modern-release/result.json) |
| Existing JavaScript, events/forms, timers, external scripts/cache, app switching, resize, theme, Stop/close | [PASS](../.husarz-test/qemu-v3-javascript/result.json) |
| Full browser regression | [PASS](../.husarz-test/qemu-v3-full-synchronized/result.json) |
| Six heavy pages, eight maximize/restore transitions, scrolling, cancellation and app switching | [PASS](../.husarz-test/qemu-v3-heavy/result.json) |
| Native table geometry, borders, signed margins and resize | [PASS](../.husarz-test/qemu-v3-layout/result.json) |
| UTF-8 editing, OS clipboard, password protection and GIF/cache | [PASS](../.husarz-test/qemu-v3-editing/result.json) |
| Storage reload, tab/origin isolation and three successive browser restarts | [PASS](../.husarz-test/qemu-v3-storage/result.json) |
| Delayed fetch, mouse movement while pending, navigation/tab-close cancellation and old completions | [PASS](../.husarz-test/qemu-v3-cancel/result.json) |

There is no unexpected guest fault or browser fault in these records. The modern
group permits exactly one deliberate #UD (vector 6) from the native FPU partner;
both normal FPU partners and clean slot reuse pass. This does not relax the
ordinary user-fault policy. Installed-volume reboot persistence was not tested;
these guests use the ordinary live filesystem without data disks.

The current root ISO and kernel match the frozen acceptance artifacts. The
kernel extracted from the ISO matches that ELF, and the embedded browser, worker
and FPU probe match their corresponding native payload files byte for byte.

- ISO SHA-256: `b175a9a745e00fa16af0930657763d3799b726aa07e21d34c46c952682d1f7cd`
- Kernel SHA-256: `6795efce7bbf961523f6ce729cc30971c62a396a3187eb69c54b96c050c1306c`

The [machine-readable validation manifest](../.husarz-test/modern-final-validation.json)
records all payload hashes, sizes, counts, limits, native records and the wider
kernel-stack findings. Its status is `PASS_WITH_KERNEL_STACK_FINDINGS`, not an
unqualified claim that every whole-system audit passes.

The resumed host run passes **1,412,538 Husarz checks** with ASan/UBSan and leak
detection enabled. It includes the coalesced-storage-revision regression: a
damaged newer write must leave the older valid generation readable.

| Host group | Result / checks |
| --- | --- |
| JavaScript, numeric boundaries, objects, exceptions, Promise/async, fetch/JSON/storage | PASS / 10,865 |
| Native browser runtime and real HTTP parser | PASS / 7,689 |
| Engine | PASS / 127,432 |
| Layout | PASS / 259,225 |
| Fidelity/cache | PASS / 3,697 |
| Cooperative performance | PASS / 4,594 |
| Selection/clipboard/password protection | PASS / 987,380 |
| GIF and malformed inputs | PASS / 11,656 |

Resumed logs: [JavaScript](../.husarz-test/resumed-javascript.log),
[runtime](../.husarz-test/resumed-runtime-final.log),
[core regressions and fault/FPU checks](../.husarz-test/resumed-core-host.log),
[stack/FPU audits](../.husarz-test/resumed-audits.log), and
[frozen kernel SIMD audit](../.husarz-test/resumed-frozen-kernel-audit.log).

Measured VM sizes (same x86-64 layout in the sanitizer build): Runtime **675,424
bytes**, BrowserRuntime **693,528**, Compiler **143,648**, Continuation **8,760**,
StorageNamespace **18,812**, release Document **2,632,928**. Compared with the
first milestone's report, fixed Runtime/BrowserRuntime storage decreases by
99,960/92,256 bytes despite the added features, primarily because Number and
Value records shrink. Eight continuation slots add at most **70,080 bytes** per
runtime on demand. Native worker retry bookkeeping adds **16 bytes** to the
browser's resource coordinator. Four local and 24 session namespaces together add at most
**526,736 bytes**, allocated only as namespaces are used. The existing coroutine
work-frame allocator retains its **512 KiB global cap**; DOM/parser staging stays
under its existing separate fixed limits. There is no page-selected heap capacity.

The latest completed sanitizer VM workload observed **256 operands**, **32 VM
frames**, **9,111 string bytes**, **17,024 live work-frame payload bytes**, and a
sampled ASan live heap peak of **6,805,504 bytes above process startup**. That heap
sample includes DOM/compiler/transaction staging and is not a whole-guest memory
measurement. Fatal budget tests still stop at **100,000 work units**, with at most
**256 VM instructions** and **2,048 shared work units** per observed slice.

Native compiler audits: largest browser frame **4,176 bytes**, worker frame
**3,712 bytes**; the native process stack remains **16 KiB**. These are compiler
per-function bounds, not a complete static call-graph proof. Browser/worker file
payloads remain **528,704 / 69,888 bytes**, the same padded payload sizes as the
first milestone. Actual used text is **458,944 / 51,408 bytes**;
reserved text segments remain 512 KiB / 64 KiB. The diagnostic probe
adds one **69,856-byte** embedded payload installed at two native program paths.
Kernel floating-point images add **8,192 bytes** of fixed kernel storage.
The network-wait flags and exclusive transport owner add another **20 bytes**.

The wider existing kernel-stack command (`analiza_stosu.py`, 6,144-byte hard
threshold) does **not** pass: it reports unchanged functions in `psf.cpp`
(8,592 bytes), `virtio_blk.cpp` (8,320), and the Ring 3 shell entry (6,896).
This is not presented as a passing whole-kernel stack audit. Relevant measured
network frames are HTTP 4,320 bytes, HTTPS 4,576, BWS HTTP wrapper 2,400 and the
new explicit wait helper 8 bytes; these are individual frames, not a complete
call-graph bound. See [full stack audit](../.husarz-test/resumed-kernel-stack.log).

Host FPU state roundtrip passes **20,387 checks**, including an ASan/UBSan build.
Native transport-lease ownership passes **7,693 checks** under ASan/UBSan,
covering competing owners, invalid PIDs, release authorization and reuse.
The unchanged user-fault policy passes **32,768 checks**. Input diagnostic parity
passes **12 comparisons** across production and diagnostic configurations.
BursztynUI command tests pass **23/23**; browser/worker packaging passes **18/18**.
PSF/bootstrap tests pass **25,186,168 and 25,185,893 checks**, covering 21 scenarios
each under ASan/UBSan. These auxiliary results are retained from the preceding
session; the resumed host totals above are fresh runs of the completed binaries.

The image regression's optional `obrazy/tapety/nowatapeta.png` is absent in this
checkout. Its supported BURSZTYN_TEST_PNG override uses a deterministic generated
1920x1080 RGB reference in `.husarz-test/regression-reference.png`; decoder,
malformed-input, scaling, alpha and packaging cases still run under ASan/UBSan.

## Changed production and test files

Kernel: `fpu.h`, `fpu.cpp`, `scheduler.cpp`, `scheduler.h`, `idt.cpp`,
`network_lease.h`, `siec.cpp`, `siec.h`, `syscalls.cpp`, `bws_nowoczesne.cpp`,
`tls.cpp`, `loader.cpp`, `kernel.cpp`, `psf_bootstrap.h`, `Makefile`;
native probe: `programy/fpu_probe.cpp`.

VM/browser: `programy/husarz/js_number.h`, `js_core.h`, `js_promise_types.h`,
`js_json.h`, `js_dom.h`, `origin.h`, `storage.h`, `resource.h`, `async.h`,
`async_runtime.inc`, `script_runtime.inc`, `worker.inc`, and
`programy/przegladarka.cpp`.

Tests: `narzedzia/test_fpu_host.cpp`, `test_kernel_simd.py`, `test_network_lease.cpp`,
`test_husarz_javascript.cpp`, `test_husarz_qemu.py`, new `js-number/prototype/
exceptions/arrow/promise/async/fetch/storage/session/microtask-hostile/modern-mixed`
fixtures, async-cancellation/async-hostile fixtures and controlled JSON bodies. Existing changes predating this continuation are
preserved; this list does not attribute those older edits to this milestone.
