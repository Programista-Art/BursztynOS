# Husarz next milestone — implementation checkpoint, 2026-10-23

**This milestone is incomplete.** This checkpoint implements form-control
selection/editing/clipboard/internal text viewports and first-frame GIF support.
It does not claim that the five-area acceptance criteria have been met.
JavaScript remains disabled at the existing ScriptExecutor boundary.

## Feature status

| Requested area | Current implementation status |
| --- | --- |
| Float placement/clear, formatting contexts and float work limits | Not implemented in this checkpoint |
| Fixed/sticky positioning and z-index | Not implemented in this checkpoint |
| Real nested element scrolling/scrollbars/wheel propagation | Not implemented in this checkpoint; form-control internal text viewports are separate |
| POST BWS API/body limits/redirect policy | Not implemented; existing GET API remains unchanged |
| ETag/Last-Modified conditional requests, 304, cache directives | Not implemented; existing cache behavior remains unchanged |
| Independent TCP objects/handles/generations/port allocator | Not implemented; existing single-session transport remains |
| Parallel HTTP limits/priorities/fairness | Not implemented; existing one-worker restriction remains |
| Parallel TLS session isolation | Not implemented or validated |
| WebP | Not implemented |
| GIF | Static GIF87a/89a and the first frame of animated GIF |
| SVG elements/path subset/safety limits | Not implemented |
| UTF-8 selection, internal text scrolling and clipboard | Implemented in this checkpoint; validation below |

## Form-control state and editing

`FormControl` retains UTF-8 byte-offset `anchor` and `cursor` (focus endpoint),
with ordered start/end derived from those endpoints. It also retains `scroll_x`,
`scroll_y`, preferred horizontal caret position and a pending reveal-caret flag.
This state belongs to the document and is copied by the existing cooperative
layout clone. Document replacement initializes new controls and clears focus.
Mouse dragging validates document generation, tab and focused node before use.

Left/right navigation, Shift extension, Home/End, Ctrl+A, replacement and
Backspace/Delete preserve codepoint boundaries. Textareas support up/down and
Page Up/Down with Shift extension, logical-line Home/End and Ctrl+Home/End.
Initial values are decoded/re-encoded in the cooperative initializer; invalid
UTF-8 becomes replacement characters. Editing rejects invalid Unicode scalar
values. Clipboard insertion accepts only a valid prefix fitting the existing
2,048-byte limit, stopping before a cut multibyte codepoint. CRLF is normalized;
line breaks and tabs become spaces for single-line controls.

A single field scan visits at most 2,048 source bytes, independent of document
size, with no per-pixel searches. Pointer selection supports both directions and
cross-line textarea selection. Out-of-field drag advances internal scrolling
by one bounded step per pointer event; it does not create a timer/busy loop.
Read-only controls permit selection/copy; cut, paste and edits cannot mutate them.

The native view uses the existing `clipboard_set`/`clipboard_read` text/plain
infrastructure. Failed clipboard writes leave a cut selection intact. Password
copy/cut is consumed without copying or deleting the secret. Paste remains
available. Temporary clipboard storage is wiped before release. Rendering uses
masked glyphs with the same underlying byte-offset selection endpoints.
No production value or clipboard-content logging was added. The QEMU fixture
reporter now redacts submitted values while checking raw expected values only
in test-process memory.

## Form-control rendering and invalidation

The text-control painter reads retained field values independently of document
text fragments. It yields through WorkPoint during geometry scans and
rasterization. Caret motion, selection changes, replacement and internal scrolling
invalidate only the outer control rectangle, without setting DirtyLayout or
changing document geometry/style revisions. Selection has stable web colors
(background #245ac4, foreground white), independent of the desktop theme.
Text, selection and caret are clipped to the control content box, ancestor clip,
web viewport and dirty rectangle. Hidden/offscreen boxes are culled first.

Single-line inputs retain their width and horizontally reveal the caret.
Textareas implement character wrapping (or horizontal scrolling under pre/nowrap),
vertical scrolling and retained offsets. Caret navigation reveals the caret;
manual wheel scrolling does not snap back to it. Wheel events over a text control
are consumed only if its offsets change; at a boundary they reach root scrolling.
Shift-wheel/horizontal wheel selects the horizontal direction. This is **not**
the requested general nested CSS scroll-container implementation.

The existing LayoutIdentity already checks paint revision/focus in addition to
navigation/document/style/viewport generations. A paint-only edit therefore
invalidates a pending layout clone before publication. Cooperative layout,
resize coalescing, dirty painting, retained state and culling remain enabled.

Limitations: textarea wrapping is per character; Home/End use logical newline
boundaries rather than soft-wrap boundaries. Drag autoscroll requires pointer
motion; no autonomous drag timer was added. Internal fields currently scroll
through caret navigation and wheel input; no dedicated field scrollbar thumb
is drawn. Author-defined auto intrinsic sizing retains the existing layout
behavior when a later geometry reflow occurs. General nested scrolling, and hence
controls within a true nested CSS scroller, remains pending.

## GIF implementation, bounds and ownership

No additional decoder library was imported. The shared image boundary enables
the repository's existing stb_image v2.30 GIF decoder, under its existing
public-domain/MIT dual license. Incorporated upstream files: none. Changed
vendored file: `obrazy/vendor/stb_image.h`.

Local hardening replaces recursive LZW expansion with an 8,192-entry iterative
prefix stack held in heap-owned decoder state, validates prefix ordering, and
moves first-frame decoder state off the native user stack. The eager all-frame
API is excluded by STBI_NO_GIF_ANIMATION. Structural validation checks the whole
compressed container, image rectangles, palettes, extensions/sub-block lengths,
LZW minimum code size and a maximum of 256 frame descriptors. Only the first
frame is decoded; no animation timers, frame arrays or disposal-mode sequence
are introduced. The first frame supports transparency and interlacing.

Generic bounds remain 16 MiB source, 8,192 per side and 8 Mi decoded pixels.
Husarz applies the tighter existing 240 KiB compressed source, 2,048 per side,
384 Ki decoded pixels, decode-workspace cap and document/cache memory budgets.
Decode occurs in the existing native resource worker, outside the GUI path.
There is no new synchronous GUI decoder. Cancellation/publication/cache pinning
follow the existing image-resource pipeline for IMG and background-image.

## Audit findings and remaining architecture work

`siec.cpp` still owns global per-connection TCP destination, ports, state,
sequence/acknowledgment and receive buffers. Adding browser workers over that
state would not provide independent sessions. A future transport step must
first replace these with bounded connection slots and generation-checked
handles, demultiplex packets by full tuple, allocate distinct ephemeral ports,
and isolate timeouts/close/reset. Only after that can request concurrency be
increased safely.

`tls.cpp` allocates mbedTLS SSL/config/DRBG/entropy/certificate objects per call,
but its BIO still uses the single TCP stream, and
`ostatni_certyfikat_zaufany` is shared last-result metadata. Both transport
binding and result ownership need changes before concurrency. This audit does not establish
parallel TLS isolation and no certificate validation change was made.
`ResourceEntry` already stores validators, but neither GET API currently sends
them nor does the worker publish a reusable 304 result. Conditional caching and
POST need a length-checked, backward-compatible request API before browser use.

## Validation

All reported QEMU runs use the ordinary release ISO with HUSARZ_DEBUG=0 and
HUSARZ_PERF=0, without replacement browser payloads. Artifact copies are retained
in `.husarz-test/next-release-artifacts/`.

| Host group | ASan/UBSan result / checks |
| --- | --- |
| Selection / clipboard / field viewports | PASS / 987,380 |
| Engine | PASS / 127,432 |
| Runtime, including physical USB clipboard shortcuts and pointer drag | PASS / 7,438 |
| Fidelity / cache baseline | PASS / 3,697 |
| Layout | PASS / 259,225 |
| Cooperative performance | PASS / 4,594 |
| GIF including malformed mutations and truncations | PASS / 11,656 |
| Total Husarz checks above | **1,401,422** |
| Fault policy | PASS / 32,768 |
| Native keyboard / TEXTINPUT / dialogs | PASS / 7,291 |
| NetworkDevice baseline | PASS / 12 test groups; this is not a multi-session TCP test |
| Browser packaging / GUI architecture | PASS / 18 tests |
| Shared image suite | PASS: JPEG, PNG, packaging A–O and manifest regression |

The image suite uses the pre-existing deterministic generated reference
`BURSZTYN_TEST_PNG=.husarz-test/png-reference.png`, because the optional default
`obrazy/tapety/nowatapeta.png` is absent. The initial missing-reference invocation
is not counted as a passing run. JPEG source hashes remain unchanged.

| Release-QEMU group | Result |
| --- | --- |
| UTF-8 selection, copy/cut/paste, password protection, GIF/cache | PASS |
| Full browser regression | PASS |
| Heavy pages / repeated maximize-restore / cancellation | PASS |
| Table geometry / spans / margins | PASS |

Machine-readable results: [checkpoint validation](../.husarz-test/next-final-validation.json).
Native records: [full](../.husarz-test/qemu-next-full/result.json),
[performance](../.husarz-test/qemu-next-heavy/result.json),
[layout](../.husarz-test/qemu-next-layout/result.json) and
[editing/GIF](../.husarz-test/qemu-next-editing-final/result.json).

All four match the same release hashes:

- ISO: `81710bed6fff54fe2972ba447032d3e2e22d117d985584783dbd8aaa288a32c2`
- Kernel: `d793fb8a67be51e9a7149b4734bce49e3d41ad602bb3f4076f295374e4b39c89`
- Browser: `b26d2d609db83b1ec3429ced0c5cae06b791abba96fe9cde1d2a01f698155ccc`
- Worker: `2f8628d3f5a0b039d5d4dfcadccf89467994d94b3d05c7146a561ea6efd7811e`

The heavy native run passes all six fixtures, eight maximize/restore transitions,
long-page wheel/thumb/keyboard scrolling without extra fetches, app switching,
Stop and tab close during work. The native geometry run preserves spans, collapsed
borders and signed-margin pixels through maximize/restore. These are regression
results for this checkpoint, not validation of the unimplemented milestone areas.

The GIF capture contains 12,288 pixels of the exact fixture color: one 96×64 IMG
and one 96×64 CSS background. The edited Polish text round-trips through the
native OS clipboard and fixture GET submission; password copy/cut cannot replace
that public clipboard value. Full regression covers native root scrolling,
maximize/restore without extra fetches, PNG/JPEG/cache across tabs, UTF-8 forms,
stylesheet/image tab closure, live light/dark themes, Stop/stale HTML rejection,
and window closure during a request. No unexpected guest fault was recorded.

### Memory, stack and scheduling

Measured release `sizeof(Document)` changes **2,619,608 → 2,620,632 bytes**
(+1,024). `FormControl` changes 2,068 → 2,084 bytes; 64 retained controls account
for the full delta. The instrumented host Document is 2,620,976 bytes. Clipboard
scratch is a temporary 2,049-byte heap object, wiped on release. The GIF malformed
fixture suite observed a peak 931,312-byte decoder workspace; production retains
the existing separate 16 MiB workspace cap and all tighter browser image bounds.

The largest native browser compiler stack frame remains **4,176 bytes**.
Measured helper frames: form replacement 128, clipboard 112, pointer selection
176, field hit testing 160 and wheel routing 208 bytes (some are compiler-labeled
`dynamic,bounded`). This is a per-function audit, not a proof of a whole call graph.
GIF decoder state/prefix storage uses the heap rather than recursive/native stack
frames. No increase to the native 16 KiB process stack was made.

The deterministic coroutine work limit remains **2,048 units**. All six existing
heavy host fixtures pass, and peak coroutine frame storage remains 13,952 bytes
under the unchanged 512 KiB cap. New field paint scans/raster loops yield through
the existing driver. Event-side field operations are bounded by one 2,048-byte
field, independent of DOM size; they do not invoke parsing, cascade or reflow.
No strict 2 ms wall-time guarantee is established by this checkpoint. The prior
milestone's documented timing limitation remains applicable.

Full milestone acceptance remains **not met**, regardless of checkpoint test
results: the pending features in the status table still require implementation
and their own host/native acceptance coverage.

## Files changed

Production: `programy/husarz/document.h`, `forms.h`, `control_text.h`, `css.h`,
`layout.h`, `paint.h`, `programy/przegladarka.cpp`, `obrazy/dekoder_obrazu.h`,
`obrazy/dekoder_obrazu.cpp`, `obrazy/vendor/stb_image.h`.

Tests/build/docs: `Makefile`, `narzedzia/test_husarz_selection.cpp`,
`narzedzia/test_husarz_gif.cpp`, `narzedzia/test_husarz_runtime.cpp`,
`narzedzia/test_husarz_performance.cpp`, `narzedzia/test_husarz_qemu.py`,
`narzedzia/fixtures/husarz/editing.html`, locally generated GIF fixtures,
`obrazy/README.md` and this report.
