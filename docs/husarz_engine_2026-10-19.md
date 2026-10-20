# Husarz: retained browser engine and native UI repair

Date: 2026-10-19. Implementation remains a native BursztynUI Ring 3 application.
This is a bounded small-browser engine, not an HTML Living Standard or complete
CSS 2.1 implementation. JavaScript execution is deliberately unavailable.

## Root causes and repairs

### Refresh status-panel jump

`odswiez_pasek_stanu_natychmiast()` used to paint `PasekStanu` directly with a new
`KontekstRysowania`. This bypassed the owning window and parent controls' origin
translations and clipping. The panel's local rectangle was consequently painted
at the wrong screen position during synchronous loading; the subsequent normal
window repaint put it back. Changing document height would not fix this cause.

The immediate loading update now recalculates the owning window's layout and
paints through its complete translated, clipped control tree. Root docking orders
the top chrome, bottom status panel, then the remaining document viewport. The
status panel uses only the window client rectangle. Page layout, document height
and scroll offset never enter its placement calculation. Controls survive
navigation and refresh; no page-reset intermediate geometry is published.

A host observer checks every actual `gui_odswiez()` publication, including loading,
resource fetches, errors, navigation and refresh. It asserts status-bar bottom ==
client bottom and that emitted rectangles stay within translated window bounds.
The latter catches the original direct-child-paint bug even if the stored local
rectangle itself happens to be correct.

### Scrolling

The previous implementation had a tab offset plus a scrollbar offset, interpreted
wheel direction incorrectly, ignored wheel magnitude, and changed scrollbar
visibility/range while painting. The scrollbar lacked the viewport page size.
HTML measurement was also entangled with painting, making geometry unstable.

The native scrollbar's `ModelPrzewijania` is now the sole active page offset.
`WidokStrony`, wheel handlers, keyboard handlers and native thumb/track operations
all use that model. There is no tab-owned duplicate. Range is
`max(0, document.height - viewport.height)`, recalculated before painting. The
native model clamps every change, including range changes on resize. Arithmetic
uses 64-bit intermediates and bounded document coordinates.

Positive wheel deltas move up. A detent moves three root-font line heights;
conventional +/-120 units are normalized and multi-detent magnitude is retained.
Home/End, Page Up/Down and arrows use the same model. New navigation, refresh and
tab selection reset to zero. Existing history stores URLs only, so Back/Forward
reload and reset rather than claiming to restore unavailable page state.
Short pages keep a disabled native scrollbar with a full thumb. Its reserved
width avoids an extra reflow caused by changing control visibility during paint.

### Native stack and process containment

Native QEMU loading exposed a real stack fault that host tests could not detect.
Ring 3 has a 16 KiB stack. Long-lived event/startup URL arrays plus nested resource
and redirect scratch frames exceeded it. Large temporary URL/resource storage and
the window object now live on the process heap; publication returns before
reflow. No stack-size increase was made. The largest compiler-reported individual
browser frame is 4,160 bytes. A build test bounds all frames and places tighter
budgets on startup, event dispatch, link input and document publication. This is
a regression guard, not a mathematical whole-call-graph stack proof.

Inspection also showed that the old IDT path panicked the whole OS for ordinary
Ring 3 CPU exceptions. A small kernel repair sends exceptions from an active
user process through existing process-exit GUI/IPC/socket/deferred-memory cleanup.
It does not dereference the faulting user stack. Ring 0 faults and NMI, double
fault and machine check retain the fatal path. This changes no public BWS ABI.
Private QEMU test kernels substitute only a deliberately faulting browser payload;
production browser/kernel/ISO artifacts are not overwritten by those probes.

### Presentation timeout found during native validation

Repeated refresh and scroll runs exposed a pre-existing VirtIO-GPU deadline race.
Serial diagnostics showed `RESOURCE_FLUSH` with the expected success response,
`used == avail`, and `command_done == 1`, yet the driver had declared a timeout.
Completion arrived between polling the queue and finishing the monotonic clock
read; TCG scheduling could make that read cross the 500 ms deadline. The fatal
error flag then prevented further presentation, leaving a stale browser frame
although HTTP and the guest scheduler continued running.

Both synchronous GPU command paths now poll completion once after observing an
expired deadline or exhausting their spin budget. Missing completions still time
out, corrupt completions still fail validation, and the deadline/spin limits are
unchanged. A deterministic host regression delivers the response inside the clock
read and advances time by 600 ms, covering both ordinary commands and resource
backing attachment. This narrow graphics correction was required to validate
visible browser behavior; it does not modify networking or the UI architecture.

## Architecture and changed files

| Files | Responsibility |
|---|---|
| `programy/przegladarka.cpp` | Retained native chrome, loading publication, document lifetime, resource integration, measured-font callbacks, canonical scrolling and viewport painting |
| `programy/husarz/document.h` | Bounded DOM, attribute arena, computed styles, layout boxes, fragments and script metadata |
| `programy/husarz/html.h` | Iterative tokenizer/parser, entities, recovery, implied structural elements |
| `programy/husarz/css.h` | CSS lexical handling, stylesheet/declaration parser, selectors, cascade and computed styles |
| `programy/husarz/layout.h` | Text measurement, inline/block/table layout, clipping, hit testing and form/image fallback text |
| `programy/husarz/paint.h` | Bounded software surface, native glyph painting, backgrounds, borders, radius and opacity |
| `programy/husarz/script.h` | Numeric DOM handles, read-only capability boundary, executor interface and execution budgets |
| `idt.cpp`, `syscalls.cpp`, `wyjatek_uzytkownika.h` | User exception isolation through existing process exit |
| `narzedzia/test_husarz_engine.cpp` | Sanitized engine, malformed-input, golden fixture and boundary tests |
| `narzedzia/test_husarz_runtime.cpp` | Real browser/control/HTTP integration with host transport fixtures |
| `narzedzia/test_husarz_stack.py` | Native compiler stack-budget checks |
| `narzedzia/test_husarz_qemu.py` | Real ISO, USB input, HTTP fixture server and screenshot assertions |
| `narzedzia/test_user_fault_policy.cpp` | Exception-policy matrix |
| `virtio_gpu.cpp`, `narzedzia/test_virtio_pci.cpp` | Completion-versus-deadline race and deterministic regression |
| `narzedzia/fixtures/husarz/` | HTML/CSS/text fixtures, pinned checksums, private fault payload |
| `Makefile`, `.gitignore` | Build dependencies, test targets and ignored test artifacts |

Pre-existing diagnostics work in the checkout was preserved and is outside this
browser change.

## HTML and document model

Tokenization, document building, style calculation, reflow and painting are separate
steps. Parsing is iterative with a bounded open-element stack. Layout also uses
bounded explicit frames rather than recursive tree descent. Unknown elements
retain their children. Truncated tags, unclosed quotes, mismatched closing tags,
common omitted closing tags, comments and DOCTYPE are tolerated. DOCTYPE is
ignored; the document is not placed into multiple standards/quirks modes.

Recognized elements/defaults include `html head title meta body div span p br hr`,
`h1`–`h6`, `a img strong b em i u small pre code blockquote`, `ul ol li`,
`table thead tbody tfoot tr th td`, `form label input button textarea select option`,
and `section article header footer nav main`. Style, link and script elements have
resource/metadata roles. Forms and controls have static presentation only;
editing, submission and native per-element control instances are future work.

Element/attribute names are ASCII case-insensitive. Attributes support both quote
styles, unquoted values, booleans, empty values and whitespace around `=`. Class
and style attributes are separate retained data. URL attributes decode HTML
entities once, preserving percent escapes and URL structure; no URI percent
unescaping is performed. Named entities include amp/lt/gt/quot/apos/nbsp and common
punctuation. Decimal/hex numeric references support both normal `&#...;` forms
and the requested simplified `&123;`/`&x1F;` forms. Invalid Unicode becomes U+FFFD.
Bounded UTF-8 iteration and chunk boundaries preserve supported Polish characters.
Glyph coverage remains that of the Bursztyn font; decoding an emoji does not imply
that this native font contains an emoji bitmap.

Images had no existing browser resource/codec pipeline. `img` therefore has an
alt-text replacement box, not newly invented image decoding or image fetching.
Unknown/unsupported HTML includes embedded media, canvas, SVG, MathML, iframe
browsing contexts, object/embed, template semantics, custom-element lifecycle and
complex form behavior. Links remain navigable. No full HTML5 adoption-agency or
foster-parenting algorithm is claimed.

## CSS support and rendering

Sources: user-agent defaults, style elements, style attributes and at most four
same-origin, same-scheme external stylesheets through the existing HTTP path.
`text/css` is accepted as textual content. External CSS redirects, cross-origin
CSS and `@import` are not followed. Stylesheet order follows document source order.
Comments, strings and declaration/block delimiters are handled with bounded
lexical state; malformed/unsupported rules can be skipped without rejecting the
whole document.

Cascade compares `!important`, specificity and source order; inline styles have
higher normal specificity and appropriate properties inherit. Invalid declaration
values do not displace valid earlier declarations. Element, `.class`, `#id`,
`element.class`, `element#id`, multiple compound classes/IDs, descendant, child,
comma lists, `:first-child` and `:last-child` selectors are supported. Unsupported
selector syntax rejects that selector rather than broadening its matches.

| Property group | Implemented behavior |
|---|---|
| `display`, `visibility` | block, inline, none, simple table/row/cell; hidden preserves layout |
| `color`, `background-color` | Named/common hex/rgb colors; simple color-only `background` shorthand |
| `width`, `height`, `min-width`, `max-width`, `min-height`, `max-height` | Bounded practical box dimensions |
| `margin`, `padding`, four side properties | 1–4-value shorthand, auto horizontal block margins, bounded box spacing |
| `border`, `border-width`, `border-style`, `border-color`, side widths | Rectangular/circular-corner borders, solid/dashed/dotted patterns |
| `font-family` | Retained cascade property, rendered with the current native font fallback |
| `font-size`, `font-weight`, `font-style` | Native metrics, scaled glyphs, bold and italic presentation |
| `text-align`, `text-decoration`, `line-height`, `white-space` | left/center/right, underline, measured line boxes, normal/nowrap/pre/pre-wrap |
| `overflow`, `overflow-x`, `overflow-y` | Ancestor paint clipping for non-visible overflow; root viewport scrolls |
| `vertical-align` | Limited inline alignment within line boxes |
| `border-radius` | One circular radius, not full elliptical/per-corner shorthand |
| `opacity` | Effective inherited per-primitive alpha, not isolated group compositing |
| `box-sizing` | content-box and border-box |

Lengths support px, percent, em and rem, plus practical auto/inherit/initial cases.
Percentages and margins use bounded simplified box rules; this is not exhaustive
CSS conformance. Default styles cover body margins, headings, paragraphs, links,
lists, blockquotes, pre/code, tables, buttons and static form controls.

Inline text wraps using actual current Bursztyn glyph widths and line metrics,
including mixed inline formatting. Blocks respect margins/padding/borders. Tables
use a bounded equal-column layout suitable for simple rows/cells, without a full
intrinsic table algorithm or rowspan/colspan. Viewport width changes trigger reflow
of the retained DOM, without fetching or reparsing the network document. Painting
uses viewport pixels and clips each primitive; document coordinates never place
browser chrome.

Unsupported CSS: flexbox, grid, float/clear layout, absolute/fixed/sticky positioning,
complex table spans/intrinsic sizing, complete margin collapse, counters/generated
content, attribute/sibling/complex pseudo selectors, media queries, custom
properties, calc, web fonts, background images, gradients, shadows, transforms,
filters, animations, transitions and full stacking/compositing. Nested overflow
containers do not have independent interactive scrollbars. `overflow:auto/scroll`
therefore supplies clipping rather than a complete nested scrolling implementation.
These limitations are deliberate; no unstable partial flexbox was introduced.

## JavaScript boundary

No reusable script engine was found. The calculator has stateful UI arithmetic,
not a suitable parser/interpreter. Scripts are recognized and kept as inline
source/external URL metadata. They are never evaluated or fetched. An infinite
loop fixture and a `must-not-run.js` URL exercise this boundary.

`ScriptExecutor` is an explicit future backend interface; absence returns
`Unavailable`. `ScriptDom` exposes bounded reads through generation-checked numeric
handles: getElementById, simple querySelector and textContent copying. A document
replacement changes generation. No raw application/kernel pointers, BWS calls,
network internals, syscalls or privileged capabilities are exposed to scripts.
No eval, mutation, innerHTML execution, onclick, function language or timers exist.
The future implementation must consume `ScriptBudget` for execution and recursion;
the interface alone cannot enforce a dishonest backend. Default policy is 100,000
instructions, recursion 32, source 32 KiB and zero timers. Future text/style/event
mutations must invalidate revisions and safely reparse through the bounded parser.

## Safety and resource limits

| Resource | Bound |
|---|---:|
| Source document | 256 KiB |
| DOM elements/text nodes | 2,048 total |
| Open nesting | 64 |
| Attributes | 8,192 total, 24 per element |
| Attribute name/value | 64 / 2,048 bytes |
| Text chunk | 16,384 bytes |
| String arena | 768 KiB per document |
| Stylesheet input | 64 KiB each |
| CSS rules/declarations | 384 / 4,096 total |
| Selector | 256 bytes, 8 compounds, 16 atoms per compound |
| Selector computation | 4,000,000 work units |
| Text fragments | 16,384 |
| Layout dimension | 8,000,000 pixels |
| Font size / line height | 6–128 / up to 1,024 pixels |
| View surface | 4,194,304 pixels; each dimension <=4,096 |
| Paint work | 16,000,000 pixel/glyph work units per frame |
| Script metadata | 32 scripts, 32 KiB inline source each |

Limit exhaustion marks a truncated document or returns a safe failure, with a
native status message for parse/resource limits. Existing capped network buffers
remain in place. URL resolution keeps bounded capacities. Numeric CSS parsing,
rectangle edges, scroll deltas and pixel products use bounded/64-bit calculations.
Navigation retains the old document while fetching, parses a candidate, replaces
ownership once, resets the native model, reflows and repaints. Loading errors
produce in-view native text/error content. Synchronous loading rejects reentrant
tab mutation/navigation; this avoids stale tab references. It is not an async
cancellation implementation: stop/close responsiveness during a blocking request
still follows the existing transport behavior.

HTTP/HTTPS/TLS/DNS implementations and the BWS ABI were retained. The low-address
256 KiB static network bounce buffer remains necessary because BWS29/30 encode a
32-bit buffer address. The only adjacent URL change recognizes valid IPv4 literals
instead of incorrectly sending them to DNS. No external runtime/engine dependency
was added.

## Validation

Host validation completed:

| Command | Result |
|---|---|
| `make test-husarz-engine-host` | PASS, 127,444 checks; ASan/UBSan and leak checking; 600 deterministic malformed byte samples; 13 fixed layout/paint goldens |
| `make test-husarz-runtime-host` | PASS, 5,970 checks; 84 actual loading publications; 24 real browser HTTP parser requests; ASan/UBSan and leak checking |
| `make test-husarz-stack` | PASS; native frame budgets, maximum individual frame 4,160 bytes |
| `make test-user-fault-policy` | PASS, 32,768 vector/CS/process-state combinations; ASan/UBSan |
| `make test-przegladarka-gui` | PASS, 18 existing browser packaging/ABI/UI/network-preservation checks |
| `make test-bursztyn-ui-polecenia test-kontrolki4` | PASS, commands 23/23 and controls suite |
| `make test-wm-click-routing test-wm-dekoracje` | PASS, 182 routing and 3,353,022 decoration checks |
| `make test-pointing-host` | PASS, production PS/2, HID, i8042, ownership, OHCI and xHCI paths |
| `make test-network-device test-virtio-pci` | PASS; NetworkDevice suite and 168 VirtIO tests, including the deadline race |
| `make test-irq-allocation-host test-xhci-multi-host` | PASS, 3,740 IRQ and 10,770 multi-controller checks after the IDT change |
| `make -j2 all iso` | PASS, native full build and canonical hybrid ISO; no compiler warnings in final build log |
| `git diff --check`, Python syntax compilation | PASS |

The host runtime test covers native wheel magnitude/direction, negative/large
clamping, short/large pages, thumb drag, track clicks, Home/End/Page Up/Down,
resize while scrolled, navigation resets, retained controls, loading failures,
external CSS and theme changes. Fixture files cover plain text, headings,
paragraphs, nested blocks, inline formatting, lists, tables, links, image fallback,
box spacing, percentages, text wrapping, radius and a long scrolling page.
Malformed samples include deep nesting, truncated/unclosed tags/quotes, unknown
tags, empty/long attributes, invalid entities/UTF-8, missing/nested CSS blocks and
huge/negative numeric declarations.

Resumed-run logs are retained in `.husarz-test/logs/`: sanitizer/runtime/stack
checks, full build, private fault-kernel build, related suites and QEMU probes.
The engine/runtime counts above were reproduced on resumption.
No tests were weakened. The earlier diagnostics-wide run still records unrelated
`test-image` (missing PNG fixture) and `test-windows-launcher` (literal Makefile
configuration expectation) failures in `diagnostyka_systemu_2026-10-18.md`; this
browser work does not claim to repair those targets.
This does not claim every repository test suite was run,
or claim real remote HTTPS/TLS endpoint coverage from local HTTP fixtures.

QEMU process-containment validation passes for exception vectors 6 (invalid
instruction) and 14 (page fault). In both private images, the test process exits,
the desktop remains usable, and Notatnik launches and accepts keyboard input.
Machine-readable results and screenshots are in
`.husarz-test/qemu-user-fault-ready/` and
`.husarz-test/qemu-page-fault-ready/`.

The QEMU harness uses an unprivileged loopback fixture server behind QEMU
guestfwd, keeping real browser HTTP/BWS transport and avoiding production test hooks. It
boots read-only without guest data disks, drives USB keyboard/mouse input, observes
native screenshots and rejects unexpected kernel/user faults. It waits for both
USB interrupt endpoints before sending input and waits for each wheel detent to
repaint before checking the next one. This prevents queued down events under slow
TCG from contaminating the up-direction assertion. Drag, track and keyboard
assertions also wait for the native repaint rather than assuming a one-second
rendering deadline.

Native window-width changes are exercised with maximize and restore, checking
that the scrollbar is repainted at each new window edge without another network
request. The shared BursztynUI window has no corner-drag resize handler; arbitrary
width/height combinations and retained-DOM reflow are additionally covered in the
host runtime suite.

Final interactive native QEMU run: **PASS**. Command:
`python3 narzedzia/test_husarz_qemu.py --output .husarz-test/qemu-validated`.

| Native check | Result |
|---|---|
| ISO boot and ordinary desktop launch | PASS |
| Page load through existing HTTP/BWS and external stylesheet | PASS |
| Three refreshes, eight observed frames each | PASS; all 24 status panels anchored, no duplicate panel |
| Wheel up/down, thumb dragging, track click | PASS; page and native thumb move together |
| Home, End, Page Up, Page Down | PASS |
| Maximize/restore while scrolled | PASS; visible text reflow and scrollbar geometry update; zero network requests during resize |
| Back and Forward | PASS |
| Malformed HTML | PASS; unknown children preserved, no crash |
| CSS box spacing, percentages, tables, radius, wrapping pages | PASS |
| Bursztyn Jasny activation and browser chrome update | PASS |
| Script URL remains unrequested | PASS |
| Unexpected Ring 3/kernel fault or fatal GPU timeout | None |

The run made 17 fixture HTTP requests. Results and screenshots are retained in
`.husarz-test/qemu-validated/`; the full log is in
`.husarz-test/logs/husarz-validated-qemu.log`.

Validated artifacts (SHA-256):

- Kernel: `ec04cbf9f418c5e1c73d1dc0b112b0ca9b2bae40dd0146f9f3890abb8027136a`
- Canonical `BursztynOS.iso`: `fae471a3a6492107b49e8c2beb29bdb0f732dd729c4c180ba9610c6986cb83f7`

Physical hardware remains untested. The local HTTP fixtures do not establish
remote HTTPS endpoint compatibility. JavaScript execution, interactive forms,
image decoding, flexbox and grid remain outside the implemented subset.

Earlier QEMU attempts are retained rather than counted as passes: native-stack
faults prompted the heap-lifetime repair, stale presentation exposed the GPU
completion race, fixed input delays overlapped queued events, and the initial
harness assumed unsupported corner resizing and stale theme-button coordinates.
The harness now uses actual native maximize/restore, observed repaint completion,
and an explicit light-theme activation assertion. No browser assertion was
removed to turn a product failure into a pass. Fault-probe startup also waits for
the USB endpoints; a desktop first frame alone does not mean enumeration finished.

## Next milestones

1. Move synchronous navigation/resources to a bounded cancellable task model with
   tab/document generation checks at completion; preserve atomic publication.
2. Add interactive native form bindings and a small image-resource pipeline using
   existing OS codecs only when available, with resource count/byte budgets.
3. Expand deterministic box-model conformance: margin collapse, intrinsic table
   sizing/spans and inline metrics before adding new layout modes.
4. Add bounded stylesheet media/import support and more selector/property tests;
   improve opacity only with an explicit compositing budget.
5. Evaluate a small sandboxable interpreter separately. Require native-stack,
   instruction-budget, mutation/event and hostile-script tests before enabling
   even a minimal language. Keep JS disabled until that backend exists.
