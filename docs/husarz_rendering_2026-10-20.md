# Husarz document rendering and resource milestone

Implementation: 2026-10-20; resumed validation: 2026-10-20/21.
Native Bursztyn OS Ring 3 implementation; no STL, exceptions,
RTTI, host threads or Linux APIs in production. JavaScript execution remains
disabled behind the existing ScriptExecutor boundary. This is a bounded subset,
not full CSS 2.1, CSS3 or HTML5 compliance.

## Theme leakage: evidence and architectural repair

The earlier native screenshots `.husarz-test/qemu-validated/theme-before.png`
and `browser-themed.png` show the same wrapping page changing from a dark canvas
and white text to a white canvas and dark text when the desktop theme changes.
That was a renderer bug, not website CSS reacting to a preference.

Three explicit bridges caused it in `WidokStrony`: desktop text color was passed
to reflow/cascade, desktop text-field background was passed to paint, and theme
notification marked document styles dirty. All three bridges have been removed.
The renderer APIs no longer accept theme colors. View allocation/error fallbacks
also use web canvas/text defaults. Native chrome remains ordinary BursztynUI
controls and receives the normal theme callback.

`Document::user_agent` owns the canvas, default foreground and link colors.
`user_agent.h` contains a declarative user-agent stylesheet with an explicit UA
cascade origin. Semantic display and safe control fallbacks are in `defaults()`.
Normal author CSS outranks every normal UA declaration, including an author
universal selector against a UA type selector. Importance, specificity and source
order then select winners within the relevant cascade priority. Inline author
styles retain their specificity, while author `!important` defeats normal inline
styles. `inherit`, `initial`, and `unset` use property-level copying and the
inherited-property classification. External stylesheet source order comes from
the link's DOM position, not completion timing.

The default canvas is white, foreground black and unvisited links #0000ee.
Website backgrounds, foregrounds, links, borders, controls, alpha and nested
styles override applicable defaults. Body/HTML background propagation supplies
the document canvas. A dark website stays dark under a light desktop, and a white
website stays white under a dark desktop. There is no system-theme mapping or
implicit `prefers-color-scheme` support.

## CSS, text and layout

* All 148 named CSS colors; #RGB, #RGBA, #RRGGBB, #RRGGBBAA, comma-form rgb()/rgba()
  with numeric/percentage channels, and basic hsl()/hsla(). Straight alpha is
  retained separately for text, backgrounds and borders. Transparent text and
  backgrounds are supported. HSL hue is a unitless degree number; modern space,
  slash and alternate hue-unit syntax is not implemented.
* Attribute presence, equality, whitespace-token, prefix, suffix and substring
  selectors; child/descendant, adjacent and general sibling combinators;
  first/last/only-child, nth-child(an+b), nth-last-child(an+b), first/last-of-type,
  and bounded simple-selector :not(). Eight compounds and sixteen simple tests
  per compound; one simple :not argument, no recursive :not. Matching shares the
  existing four-million-unit per-style-pass budget. Unsupported syntax does not
  broaden a selector.
* Existing size, weight, style, family, line-height, alignment, underline,
  white-space and vertical alignment are retained. Added inherited letter and
  word spacing, text-transform (including Polish case pairs), and unset. Native
  glyph widths/bitmaps continue to drive measurement and painting. Unsupported
  font families fall back to the installed native browser font; CSS never changes
  a chrome font. Downloadable fonts, shaping and full font-family resolution are
  not implemented.
* Existing content/border-box, min/max dimensions, padding, borders and auto
  horizontal centering are retained. Sibling margin collapsing now handles
  negative margins. Known containing heights participate in percentage height
  resolution; indefinite percentage heights retain automatic flow. Full
  parent/child and empty-block margin collapsing is not implemented.
* Styled nested spans, strong, em and links continue in shared line flow.
  Fragments record their flow owner so an outer inline-block line does not
  independently reposition its descendants. Inline-block provides a bounded
  independent flow with explicit or estimated intrinsic width. Baseline and
  inline decoration behavior still differ from modern engines, especially with
  mixed sizes, multiple lines and deeply nested inline boxes.
* Relative positioning and an absolute subset translate boxes/fragments after
  dimensions are known. Absolute boxes leave normal flow and use their nearest
  positioned ancestor for offsets. Left/right/top/bottom offsets are bounded.
  Complex shrink-to-fit, CSS stacking contexts and z-index are not implemented.
  Paint always intersects the document viewport. Fixed, sticky and floats are
  not implemented.
* Overflow-x/y visible/hidden/auto/scroll clip descendants correctly. Auto/scroll
  currently clip without independent nested scrollbars or nested scroll input.
* Lists support disc, circle, square, decimal, lower/upper alpha, lower/upper
  Roman, and none, with reserved marker storage and bounded numbering.
* Tables estimate column weights from measured content and explicit pixel cell
  widths rather than equally dividing every row. The plan is limited to 32
  tables, 32 columns and 256 scanned rows per table, within the global 2,048-node
  document bound. Colspan distributes across the measured column plan. Tables
  replan on reflow. Cell padding, borders, alignment and inherited spacing are
  applied. Collapse currently removes spacing; it is not a complete collapsed-
  border conflict algorithm. Rowspan, full intrinsic min/max sizing, percentage
  column constraints and complete cell vertical alignment remain unsupported.

## PNG, JPEG, backgrounds and resource bounds

The existing `obrazy/dekoder_obrazu.cpp` is linked into the worker. No second PNG
or JPEG decoder was added. Metadata is checked before allocation; the existing
codec performs format validation. A new `dekoduj_obraz_z_alfa` entry point shares
that implementation while preserving PNG alpha. The legacy wallpaper entry
point retains its original opaque wallpaper compositing behavior. JPEG is opaque.

Resource steps are URL resolution/normalization, HTTP or HTTPS, exact response
byte count, MIME check, bounded dechunking/body storage, metadata validation,
decode, cache ownership, intrinsic layout and clipped image painting. BWS29/30
have an opt-in response-length bit in the packed capacity argument; ordinary
legacy callers keep boolean return semantics. Images never use strlen on binary
response bytes. PNG/JPEG MIME types are required; malformed/oversized images
leave alt text. HTML/CSS dimensions and intrinsic aspect ratios are supported.
CSS aspect-ratio and object-fit are not implemented.

The shared-memory transfer has to fit the existing 2 MiB BWS object limit:

| Limit | Value |
|---|---:|
| HTTP response including headers | 256 KiB |
| Cached compressed resource body | 240 KiB |
| Individual decoded side | 2,048 pixels |
| Individual decoded pixels | 393,216 |
| Images/bindings per document | 32 |
| Decoded images referenced per document | 8 MiB (conservative, counts bindings) |
| Decoder temporary allocation workspace | 16 MiB |
| Cached objects | 32 |
| Total cached raw bodies | 2 MiB |
| Total cached decoded bitmaps | 16 MiB |
| Pending tasks | 48; one active network request |
| External stylesheets per document | 8; existing parser limit 64 KiB per stylesheet |

`img` and CSS background-image share the same resource cache and decoder.
Background repeat/repeat-x/repeat-y/no-repeat, pixel/percentage sizes, contain,
cover and common positions are supported. The shorthand recognizes color, url
and repeat components; slash-size/position combinations, multiple backgrounds,
gradients and attachment modes are not implemented. External CSS background
URLs resolve against that stylesheet's URL. Background drawing samples directly
inside the clipped viewport rather than creating unbounded repeated tiles.
Large hero photographs above the pixel budget currently fall back rather than
being decoded and reduced. Animated images, SVG, WebP, GIF and image orientation
metadata are unsupported.

## Cache and ownership

A process-owned ResourceCache uses normalized URL plus resource type. URL
normalization folds scheme/host, removes fragments and handles dot segments,
preserving path/query case and query bytes. LRU eviction skips pinned image
entries. Stable slots do not move. A document binding increments the entry's
reference count; document reset/destruction decrements it. Closing one tab cannot
free a decoded image still used by another. CSS is copied into the DOM arena and
needs no surviving raw-body pointer. Transient no-store/no-cache resources are
not reusable and are removed after their last document reference disappears.

Entries carry type, body/decoded sizes, dimensions, use generation, creation and
expiry time. Default freshness is 60 seconds when a monotonic clock exists;
bounded max-age is honored up to one hour, and zero age is transient. ETag and
Last-Modified are collected by the HTTP helper and transferred with MIME metadata
to the GUI cache; conditional revalidation is not implemented.
There is no disk cache. Main HTML is fetched for navigation/refresh; CSS and
images are reused. The GUI cache is the sharing authority. The worker frees its
private decode/cache state after each request.

## Native asynchronous loading and cancellation

`async.h` defines bounded tasks and a fixed shared-memory protocol.
`husarz-worker.bur` is a separate native Ring 3 executable built from the same
translation unit with a headless entry point. It owns blocking BWS HTTP/HTTPS,
DNS and image decoding. It has GUI/IPC and network permissions, without file or
program-launch permissions. The browser manifest adds program-launch permission
only to start that worker; forms and ScriptExecutor expose no launch operation.
Both executables are packaged during the existing boot filesystem installation.
No parsing/rendering/codec work has moved to Ring 0.

The GUI holds an IPC endpoint, nonce, worker PID and one active shared-memory
mapping. Completion messages must match the endpoint session, sender PID,
request, tab identity, navigation generation and retained document generation.
Only then may data enter a document or cache. Tabs use stable numeric identities,
independent of the array slot changed by closing another tab. Reused tab slots
reset their identities. Reply lengths, dimensions and pixel counts are checked
again in the receiver. Documents and tabs are never sent as pointers.

New navigation invalidates the tab generation and removes queued older work.
Stop and close do the same. Active transport may finish, but its stale result is
discarded before publication. Shared mappings remain owned until completion or
shutdown; worker mappings/message references keep storage alive independently.
Window shutdown closes the reply endpoint and releases its mappings. A busy
worker exits if it cannot send its reply; an idle worker receives shutdown or
expires its idle wait. A 45-second GUI deadline bounds worker startup/request
waiting in addition to existing network timeouts. Restart can require waiting
for a previous timed-out singleton worker to exit.

Main HTML parses into a candidate and publishes atomically. It becomes usable
before secondary resources complete. Stylesheets apply in DOM order, then image
bindings reflow/repaint the retained document. Old content remains usable during
navigation. Scroll offsets are clamped after reflow. Worker requests are serialized
because the current native network stack has one global TCP session. Cancellation
is safe discard, not an immediate transport abort; a new request can wait behind
an older canceled request. Host runtime retains a synchronous transport fixture
for the earlier parser/UI regression; completion ordering is also tested directly
against the actual publisher and bounded queue. Native QEMU exercises the worker.

## Forms

Control state belongs to Document: at most 64 controls, each with 2,048 bytes of
UTF-8 value and a once-reserved display buffer. Text input never appends display
copies indefinitely to the DOM arena. Painting is by the web renderer and CSS;
no system-themed native child control is inserted into a page.

Text/password, textarea, checkbox, grouped radio, button/submit, select/option and
label bindings support mouse focus, Tab/Shift-Tab, cursor movement, Home/End,
Backspace/Delete and UTF-8 insertion. Password display buffers contain masks.
Textareas accept line breaks. Checkbox/radio marks use web paint colors. Select
has a bounded document-painted popup (up to eight visible rows from 64 options)
clipped to the document viewport and keyboard selection. Label `for` and direct
child association are supported. Focus/popup state resets with DOM replacement.
Selection ranges, clipboard editing, text-field horizontal scrolling, complete
label descendant association, validation, resets, disabled option handling and
advanced control types remain unsupported.

Submission serializes successful named fields as bounded
application/x-www-form-urlencoded data and navigates by GET. UTF-8 bytes are
percent encoded. Action URLs and resources honor the document base URL and normal HTTP/HTTPS resolution; no local
filesystem or privileged BWS interface is exposed. The current HTTP request API
has no safe POST request operation, so POST is explicitly rejected rather than
silently sent as GET. Multipart/file uploads are not implemented.

## Validation record

The resumed validation uses the following commands and saved logs. Native QEMU
results and artifact hashes are recorded below. Development diagnostics are off by default; rebuild with
`HUSARZ_DEBUG=1` to emit task URL, navigation/document generations and cache/image
counters through BWS output. Host diagnostics always print test cache counters.

Known initial harness/test issues are retained honestly: desktop timed double-
click did not open the browser on one TCG run; the harness now uses native Open.
A drag assertion observed an intermediate thumb frame, and the mouse cursor
obscured the lower half of the thumb at its target; it now waits for the thumb's
upper edge to reach that target and moves the cursor aside. The original image
suite's default reference PNG is absent from this checkout; an explicit
`BURSZTYN_TEST_PNG` override permits a generated 1920x1080 test reference without
creating or replacing a product wallpaper.

## Reproducing host and native validation

```
make -j3 all iso test-husarz-stack test-husarz-engine-host \
  test-husarz-runtime-host test-husarz-fidelity-host test-przegladarka-gui
make -j2 test-bursztyn-ui-polecenia test-gui-text-input-host \
  test-network-device test-virtio-input-events test-input-coexistence-host \
  test-motywy-unit
BURSZTYN_TEST_PNG="$PWD/.husarz-test/reference-1920.png" make test-image
python3 narzedzia/test_husarz_qemu.py --timeout 2400 --output .husarz-test/qemu-input-synchronized
python3 narzedzia/test_husarz_qemu.py --images-only --timeout 1200 \
  --output .husarz-test/qemu-jpeg-pixels
```

The generated PNG test reference is a 1920x1080 RGB image with color
(75,125,180), saved using Pillow. It substitutes only for the absent test
reference; no product image is replaced. Filesystem suites are not required by
the cache implementation: it is memory-only and creates no guest temporary files.

Host results on resumption:

* Native build and ISO generation: PASS. Native stack budget: PASS; largest
  browser frame 4,176 bytes within the 16 KiB stack policy.
* Engine ASan/UBSan: 127,432 checks PASS, including malformed input and the safe
  disabled script boundary.
* Fidelity ASan/UBSan: 3,697 checks PASS, including deterministic geometry/paint,
  color/cascade/theme independence, bounded selectors, tables, forms and cache.
  Cache diagnostic: hits=1, misses=1, evictions=66.
* Runtime ASan/UBSan with leak detection: 7,417 checks PASS, including the
  mouse-to-select regression. Includes 82 loading publications, 25 real HTTP
  parser requests and actual stale HTML/CSS/image completion rejection.
  Cache diagnostic: hits=17, misses=5, decoded=2, failures=2, raw=1,467 bytes,
  decoded=12,288 bytes. These deterministic host counts are distinct from native
  HTTP request observations.
* Husarz GUI/static native executable checks: 18/18 PASS.
* PNG/JPEG shared decoder, malformed rejection, scaling, alpha and packaging:
  PASS with ASan/UBSan. Logs: `.husarz-test/resumed-images.log`.
* Theme unit suite: 155/155 PASS. Network device suite: PASS.
* BursztynUI command routing and virtio input event suites: PASS. Production
  keyboard/TEXT_INPUT/dialog input: 7,291/7,291 PASS. Input coexistence and xHCI
  incremental/boot/physical-device suites: PASS, including twelve functional
  diagnostic-parity comparisons. These host suites exited successfully.
* Ring 3 fault-isolation policy ASan/UBSan: 32,768 checks PASS. Ring 0 faults,
  inactive PID and NMI/double-fault/machine-check remain fatal by policy.

Build/host logs are `.husarz-test/resumed-final-build.log`,
`.husarz-test/resumed-final-engine.log`, `.husarz-test/resumed-related.log`,
`.husarz-test/resumed-images.log` and `.husarz-test/resumed-fault-policy.log`.
Earlier mouse-route checks are in `.husarz-test/resumed-runtime-mouse.log`.

Native select regression: QEMU reproduced a GET submission with the first
option after Down. Waiting for the popup showed that painting latency was not
the only problem. Development diagnostics then recorded two native Down
press/repeat events (normalized key=80, same focused select), followed by Enter.
`form_select` used modulo arithmetic, so the second event wrapped the last
option back to the first. Arrow navigation now clamps at both ends. No-op repeats
do not create a document revision/reflow; int64 arithmetic also bounds extreme
step values. Tests cover repeated Down, repeated Up, both integer extremes, the
mouse-to-keyboard path and submitted values. Native input transport is unchanged.

The QEMU harness observes the popup's selected-row paint before Down and the
second-row highlight before Enter. This separates input injection from completion
of the native focus/paint update. Optional development-only form diagnostics log
normalized commands and numeric focus IDs, never form values or passwords.

Harness timing audit: running two TCG guests concurrently exceeded the QMP
client's three-second screenshot timeout. The Husarz harness now allows fifteen
seconds for monitor operations. A subsequent trace showed overlapping virtual
key releases corrupting the typed URL under host load. `send-key` releases keys
on guest virtual time, so the harness now waits for 48 observed native scheduler
ticks (250 Hz) per key press/release before injecting the next key, and 32
ticks for each mouse-button press and release. This uses a
read-only symbol observer, as the existing cursor observer does; it installs no
guest test interface. Final native validation runs sequentially.

## Native QEMU results

The full interactive regression passed on the release artifacts below:
`.husarz-test/qemu-input-synchronized/result.json`, with log
`.husarz-test/input-synchronized-qemu.log`. It drove native BursztynUI controls,
USB mouse/keyboard, the real worker process and HTTP fixture server; no private
browser or kernel test entry point was installed.

* 24 observed refresh frames retained the native status bar's geometry.
* Wheel scrolling, thumb drag, track clicks, Home/End and Page Up/Down passed.
* Native maximize/restore reflowed without a network fetch. Back/Forward passed.
* External CSS, PNG backgrounds, image requests, malformed image fallback and
  sharing across two native tabs passed. Exactly one request each for sample.png,
  sample.jpg and render.css preceded reload; those counts did not increase on
  reload or the second tab. Failed malformed.png is intentionally requested again.
* Native text/password, UTF-8 textarea, checkbox, radio and select submitted
  `/forms-native.html?q=a&p=p&note=x%0Ay%C5%82&check=on&r=two&s=second` by GET.
* Closing a tab during image/CSS loading left the surviving page unchanged.
* **White and black website document pixels each remained identical across live
  desktop theme changes. Native browser chrome changed.** The comparisons exclude
  native chrome and use exact cropped document pixel bytes, not visual similarity.
* Stop/new navigation discarded stale main HTML. Closing the window during a
  request left another native application usable. No guest fault or Ring 0 panic
  was observed in the completed full regression.

The original images screenshot captures progressive publication: PNG may be
visible before JPEG completes. The additional `--images-only` regression waits
for actual decoded JPEG pixels beside the PNG, distinguishing its lossy edge
colors from PNG's four-color palette and black alt text, then checks the cached
images after closing a second tab. This focused regression also passed:
`.husarz-test/qemu-jpeg-pixels/result.json`; both `png_jpeg_pixels` and
`image_cache_two_tabs` are true, with one request each for the valid PNG, JPEG
and CSS across navigation/reload/two tabs. `images-loaded.png` and
`images-cached.png` in that directory show all three decoded img bindings, the
repeated PNG background, and the malformed-image alt fallback.

Exact document SHA-256 values, equal before/after their respective live theme
switches (RGB crop 102,216 to 881,626):

```
white_document_sha256: 382dcbaf3a8d73ec7a534e458666b939a325fc620da4a7c593f1b43c0f5075c4
black_document_sha256: 93e7c907c8fb1595a00b78084b82d522bf563568c222359a44ebc56590d194ef
```

These are also saved in
`.husarz-test/qemu-input-synchronized/theme-pixel-hashes.json`.


## Final release artifacts

The release build uses `HUSARZ_DEBUG_RESOURCES=0`; form/resource debug strings
are absent from the browser payload. The following hashes identify the exact
artifacts supplied to final QEMU validation:

```
e684a5242406b3fcb2246d624543f22ce634c90dfcad2f3a53d0c8a880a8fe87  system_operacyjny.bin
fad3812293c5647370a49268f6584ae81422eefb90c15d936481619dd1fdceca  BursztynOS.iso
e36386585e68d698d5684d8a5d9cf1fa5c66338bf6339ebc2fda859248c4818b  przegladarka.bin
b9778bf0b687e79f6cf47e7d99b45bcf1f4cecc47de9061c9cdf6b56757e5687  husarz_worker.bin
```

## Recommended next work

1. Expand table intrinsic sizing, rowspan, collapsed borders and vertical alignment.
2. Finish parent/child margin collapsing, inline baselines and inline decorations.
3. Increase image transfer capacity through bounded multi-part shared-memory
   messages and add reduced-size decode paths where the existing codec permits.
4. Add conditional HTTP revalidation, richer response metadata and HTML caching
   with explicit freshness semantics.
5. Add safe POST support at the native HTTP request boundary, selection/editing
   behavior and more complete form success/validation rules.
6. Improve transport cancellation and concurrency when the native TCP stack can
   safely support independent sessions. Add stylesheet imports/media and a
   conformance corpus before considering new layout modes.
7. Keep JavaScript disabled until an independently budgeted sandboxed backend
   satisfies native stack, execution, event and mutation tests.
