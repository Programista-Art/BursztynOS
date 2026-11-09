# Husarz: external CSS and real-site compatibility, 2026-10-24

The corrected release passed nine sanitizer suites and nine deterministic native QEMU groups. Native public-site observations confirmed author colors on both target pages.

## Reproduced root causes

The supplied TLS error was reproduced on the unmodified CSS engine with resource diagnostics enabled, in an ordinary QEMU boot. Tomato's main stylesheet was **successfully** fetched and parsed: `https://tomato.dimitalart.pl/style.css`, request target `/style.css`, HTTP 200, MIME `text/css`, 11,596 body bytes, 75 supported selector rules and 225 declarations before this change. The second stylesheet is Font Awesome on cdnjs (83,981 bytes when fetched on the host). Its TLS handshake failed certificate verification in the guest; verification remains mandatory.

The exact malformed background request produced by the old engine was:

```text
https://tomato.dimitalart.pl/'images/tomato.jpg') no-repeat center center/cove
host: tomato.dimitalart.pl
request target: /'images/tomato.jpg') no-repeat center center/cove
```

The background shorthand parser retained a `Span` pointing into the full declaration. Computed style stored its starting arena offset but lost its length. The image discovery code subsequently used the remainder of the entire declaration as a NUL-terminated image token, removed four leading and one final byte, and passed the resulting string to HTTPS. The TLS path check correctly rejected the embedded spaces (`c <= 0x20`). Neither missing slashes nor query punctuation caused this observed rejection. Exact string ownership of computed tokens now prevents this malformed input. CSS image references keep the final stylesheet URL as their base, including after redirects.

The unstyled colors were a separate, demonstrated issue: Tomato defines its theme in `:root` custom properties and uses `var()` in body, links, buttons, sections and footer. The old engine rejected `:root` and these property values. A host replay of the downloaded CSS reproduced black body text, UA blue buttons, absent button backgrounds and absent footer backgrounds even with the CSS present. The corrected computation produces body `#2c3e50`, button text `#ffffff`, button background `#e74c3c`, light sections `#ecf0f1`, and footer `#2c3e50` with white text.

The diagnostic guest run recorded Tomato's two stylesheet URLs as successfully resolved: one downloaded with HTTP 200 and `text/css` and parsed, while the CDN request failed certificate verification before an HTTP response. For WordPress it recorded nine stylesheet requests, all HTTP 200 with `text/css`, including the query-bearing navigation, image, social-link, icon, animation, theme and Fancybox URLs. No redirect was observed for these real stylesheet URLs; bounded redirects are independently exercised by the synthetic fixture. Tomato's logo PNG separately returned HTTP 200 with 172,656 bytes and `image/png`, then failed image binding/decoding; its fallback alt text is independent of stylesheet success. Final release runs keep the verbose diagnostics disabled and confirm the resulting author colors visually.

WordPress's observed "document limit" was primarily **CSS rule exhaustion**, not a measured DOM-node overflow. The current public document has 650 parsed nodes and 805 attributes; inline styles already consumed 377 of the previous 384 rules, so the first external sheet filled the remaining seven slots and later theme sheets contributed no rules. The page references nine external stylesheets while discovery previously stopped at eight. Its 69,160-byte animation stylesheet and Tomato's 83,981-byte icon stylesheet also exceeded the old 65,536-byte CSS-source limit.

## Resource pipeline and security

- `make iso HUSARZ_RESOURCE_DEBUG=1` enables discovery, original/base/resolved URL, scheme/host/port, path/query/fragment, origin comparison, cache, request/HTTP/MIME/body and parse diagnostics. Default builds keep these diagnostics disabled. `HUSARZ_DEBUG` remains a compatible default for this switch.
- A shared bounded URL parser/resolver normalizes scheme/host, removes dot segments only in paths, preserves query bytes and percent escapes, percent-encodes raw path spaces/UTF-8, and excludes fragments from request targets. Relative, root-relative, scheme-relative, query-only and explicit default-port URLs use the same implementation. Unsupported schemes, userinfo and invalid host labels are rejected.
- The native BWS29/30 ABI still connects to fixed ports 80/443. Explicit `:80`/`:443` are accepted and canonicalized; nondefault ports are rejected rather than silently connected to a different port.
- The shared browser/TLS request-target validator requires a leading slash and rejects controls, raw spaces, non-ASCII wire bytes, fragments and backslashes. `?`, `&`, `=`, `%`, `_`, `-` and `.` remain accepted. Hostname validation and TLS certificate/hostname verification are not bypassed.
- Normal resources follow 301/302/303/307/308 redirects with at most five hops, normalized-loop detection, relative Location resolution, and scheme/host/target revalidation. The original cache key and final source URL are distinct. JS fetch retains its separate redirect/origin policy.
- CSS MIME matching trims whitespace, compares case-insensitively and accepts parameters. Missing or clearly incorrect CSS MIME is rejected with diagnostics; HTML error bodies are not parsed as CSS.
- Stylesheets are queued before scripts so a large script list cannot unnecessarily delay initial author styling. Existing tab/navigation/document/node identity and cancellation guards remain in place.

## Bounded CSS implementation

Custom properties are sparse document-owned bindings with per-element cascade and inherited lookup. Resolution occurs when computing declaration values, not during global parsing. Names are case-sensitive. Fallbacks and cycles are bounded; inherited references use the defining element's scope. A bounded computed-string cache avoids repeatedly consuming the arena when media-dependent styles are recomputed on resize.

| Bound | Before | After |
|---|---:|---:|
| stylesheet bytes | 65,536 | 131,072 |
| external stylesheets per document | 8 | 16 |
| rules/selectors per document | 384 | 4,096 |
| declarations per document | 4,096 | 12,288 |
| selector bytes | 256 | 512 |
| declaration value bytes | 2,048 | 2,048 |
| text arena | 768 KiB | 2 MiB |
| CSS nested conditional depth | all blocks skipped | 8 |
| custom properties per document / element | unsupported | 2,048 / 256 |
| custom-property name / expanded value bytes | unsupported | 96 / 2,048 |
| variable depth / resolution work | unsupported | 16 / 2,048 |
| selector work, main / generated | 4 million / absent | 16 million / 8 million |
| generated boxes / font-face records | absent | 256 / 64 |
| flex items / grid columns | absent | 128 / 16 |
| math expression depth / tokens | absent | 8 / 64 |
| shadow blur radius | absent | 16 px |
| DOM nodes / attributes / depth | 2,048 / 8,192 / 64 | unchanged |
| layout-box slots / text fragments | 2,048 / 16,384 | unchanged |
| scripts / script source bytes | 32 / 32,768 | unchanged |
| image / pending resource counts | 32 / 48 | unchanged |
| cache objects / raw cache bytes | 32 / 2 MiB | unchanged |
| resource bytes / decoded image pixels | 240 KiB / 393,216 | unchanged |
| shared cooperative work slice | 2,048 | unchanged |

4096 rules are justified by replaying the actual icon and theme sheets: enabling their pseudo-element rules exhausted an intermediate 2048-rule bound before the main WordPress theme was reached. The measured largest custom-property scope has 145 names (170 bindings across the document), motivating a 256-name per-element cap. The longest observed single WordPress selector was 277 bytes, motivating the bounded 512-byte selector limit. DOM arrays were not blindly enlarged: the observed public pages fit the existing node bound. Synthetic oversized DOM input still stops safely and marks the node limit.

Final offline replay at a 1000 px viewport measures Tomato at 272 nodes, 82 attributes, depth 9, 258 text fragments and one script; WordPress at 650 nodes, 805 attributes, depth 22, 413 text fragments and 16 scripts. Box storage remains a fixed 2048 slots, indexed by node. Both replays have zero document-limit flags. Tomato's two captured sheets plus inline CSS yield 2295 rules and 2073 declarations, including the CDN sheet that the guest rejects on TLS verification; WordPress's nine sheets plus inline CSS yield 2853 rules and 4456 declarations. These replay totals must not be confused with successful guest download counts.

Supported practical behavior includes `:root`, tag/class/id, ancestor/child/sibling/attribute selectors, child-position pseudos, bounded nested `:not()`, compound selector lists in `:is()`/`:where()` with their specificity rules, independent comma-list branches, importance/specificity/order/inline cascade, inherited theme colors, existing color formats plus modern space/slash RGB, currentColor, background color/image extraction, two-stop linear gradients, side borders and standard spacing shorthands, basic font shorthand, media width conditions and `screen`/`all`/`and`, and a bounded property/value `@supports` subset.

Flex layout supports rows/columns/reversal, wrapping, growth/shrink/basis, gaps, justification, item/self alignment and inline flex. Grid is deliberately limited to bounded column tracks, fixed lengths/percentages/fr, fixed repeat and auto-fit/auto-fill minmax-to-1fr patterns, using the same cooperative placement machinery. Math supports calc/min/max/clamp with bounded arithmetic and font/viewport conversion; mixed percentage/absolute expressions are rejected without dropping sibling properties. Generated before/after boxes are renderer-owned and do not enter ScriptDom. Font-face metadata is retained; missing decoders fall back to native fonts. Image object-fit and bounded approximate shadows are handled locally.

Final visual review found an additional intrinsic-sizing bug: a flex item containing a padded link reserved space for the text but omitted the descendant's padding, borders and margins. Bounded intrinsic measurement now includes descendant horizontal decoration and preserves the decorated minimum word width. On the captured Tomato page at a 1000 px host viewport, the navigation button changed from 48×199 px to 92×49 px, retaining its author colors. A synthetic nested-button regression checks the final content width, outer width and height. Absolutely positioned submenu descendants remain excluded from intrinsic sizing.

## Remaining limitations

This is not CSS conformance or complete WordPress compatibility. In particular: arbitrary selector arguments/combinator expressions inside functional pseudos, full supports expressions, container queries, arbitrary gradients/multiple background layers, full Grid placement/row/span algorithms, mixed-percent calc expressions, transforms/transitions/animations/sticky positioning, downloadable WOFF/WOFF2 fonts, and data-image URLs are incomplete or unsupported. The pseudo-element subset does not implement general generated-content layout or arbitrary generated DOM. Shadows are a bounded visual approximation. Parent opacity uses the existing primitive alpha pipeline, not full isolated group compositing.

Tomato's background JPEG is 2,404,823 bytes and 4096×2304; it exceeds the existing compressed-image and decoded-pixel bounds, which were not indiscriminately raised for this CSS fix. Its supported dark gradient layer still renders, preserving white-text contrast. Its script also raises a syntax error on the captured page; visibility animations depending on unsupported script behavior remain a limitation. The guest's rejected cdnjs certificate is not worked around by disabling TLS verification. Native-font fallback cannot reproduce Font Awesome icons.

Release screenshots also retain approximate navigation spacing and inline-block alignment. Tomato's navigation download button now remains horizontal, and both download buttons have white text on the intended red background; the large photograph is replaced by the supported gray gradient. Native bitmap typography differs visibly from the reference browser. These layout and typography differences must not be described as a full visual match.

The final release WordPress screenshot shows the dark author canvas, white title and introduction, and horizontal cyan GitHub/YouTube buttons. At the approximately 780 px web viewport, its mobile navigation control falls back to a narrow native "Submit" button because the SVG/icon-based control is not reproduced correctly. Script-dependent animated hero content is incomplete. The offline 780 px replay independently produces 12,162 cyan pixels; the release observation also detects author cyan pixels and finishes with no network/script work pending. Both public pages finish without a guest fault. Public-site observation is recorded as `OBSERVED`, separate from deterministic fixture `PASS` results.

## Validation

Deterministic synthetic fixtures are under `narzedzia/fixtures/husarz/css-*` and `assets/css/`; they reproduce the resource URL, query, redirect, custom-property, cascade, media/flex, pseudo-element and large-DOM patterns without copying site content. Public captures and screenshots are private ignored artifacts under `.husarz-test/css-real-baseline/`.

An intermediate debug ISO passed QEMU pixel assertions for the redirected/query-string sheet, author canvas/button/footer colors, narrow-column/wide-row media layout, maximize/restore responsiveness, and Stop/navigation stale-CSS cancellation. Final release results below supersede this intermediate result.

The final host run passed all nine sanitizer suites. Counts below are assertions/checks from the individual suite outputs, not a count of independent test cases.

| Host suite | Checks | Result |
|---|---:|---|
| CSS compatibility, computed pixels, variables and clone/intrinsic regressions | 362 | PASS |
| engine/malformed input | 127,432 | PASS |
| fidelity/cascade/resources | 3,697 | PASS |
| tables/layout | 259,225 | PASS |
| performance/cooperative scheduling | 4,254 | PASS |
| selection/clipboard | 987,380 | PASS |
| GIF/image decoder | 11,656 | PASS |
| JavaScript | 10,865 | PASS |
| native browser/runtime/resource redirects | 7,727 | PASS |
| **Total** | **1,412,598** | **ASan/UBSan PASS** |

The CSS suite uses `-O0` with both sanitizers to keep compiler work for its explicit large-document coroutine clone check practical; existing suites retain their established optimization flags. Native release-QEMU builds use the normal release compiler flags.

Final release QEMU results all refer to the same immutable image. The nine deterministic groups passed; public-site observation completed separately as `OBSERVED`.

| Release QEMU group | Result | Coverage |
|---|---|---|
| CSS pixels | PASS | External CSS, query, redirect, relative image, author/button/heading/footer colors, media/flex resize, Stop and stale CSS |
| Full browser | PASS | Refresh anchoring, scrolling, history, PNG/JPEG, cache, native forms, theme isolation, tab/window close and Stop |
| JavaScript | PASS | DOM, events, forms, timers, script cache, bounded hostile scripts, resize and cancellation |
| Modern JavaScript | PASS | Binary64, prototypes, exceptions, arrow functions, Promise/async/fetch, storage and FPU process isolation |
| Heavy pages | PASS | Six heavy fixtures, eight maximize/restore actions, long scrolling, no resize refetch, Stop/tab close and app switching |
| Layout | PASS | Table spans, border conflict, negative margins and resize without refetch |
| Editing | PASS | UTF-8 selection, clipboard, password protection, GIF pixels and cache |
| Storage | PASS | Reload/tab/origin isolation and three browser restarts; installed-volume OS reboot is outside this run |
| Async cancellation | PASS | Back navigation/tab close and late fetch responses preserve the current document |
| Public Tomato and WordPress | OBSERVED | Both show author colors and finish with no pending network/script work or guest fault |

The modern VM suite deliberately terminates its FPU partner with vector 6 and checks isolation; no unexpected browser/process fault is accepted. The final runner used at most two TCG guests after host compilation; CSS pixels were checked separately with one guest. A superseded run alongside parallel compilation hit a virtio-GPU control timeout and is excluded from these PASS results. No GPU watchdog change was made.

Validated release artifacts:

- ISO SHA-256: `296e42fbb15da2d09e7af5c0e30b346ff17378d07dcb35bd38bd2b3653551b67`
- Kernel SHA-256: `00822714bf55ceef14ff0e9c1cabf93b21e7be73f130080611426924f3b37777`
- Immutable artifacts and browser-source hashes: `.husarz-test/css-resumed-release/`.
- Combined machine-readable results: `.husarz-test/css-complete-validation/final-summary.json`.
- Public screenshots: `.husarz-test/css-complete-validation/real-sites/real-tomato.png` and `real-wordpress.png`.
- Current root build artifacts match the immutable tested files byte for byte.

The final host rerun logs are `.husarz-test/css-resumed-host.log` and `.husarz-test/css-resumed-regression-host.log`; all 1,412,598 checks passed with ASan/UBSan. The native stack audit passed in `.husarz-test/css-resumed-stack.log`.


## Memory, stack and performance evidence

Release `sizeof(Document)` increased from the measured 2,632,928 bytes to 4,623,880 bytes (+1,990,952); the diagnostic PERF build is 4,624,224 bytes. The lazy selector index is 926,216 bytes. Table slabs and decoded-image budgets are unchanged. The larger arena, bounded CSS arrays, sparse custom-property records, generated boxes and computed-string cache account for the increase. A private in-flight document also carries this bounded cost; six tabs do not allocate unbounded CSS storage.

The host stress suite preserved a maximum of 2048 charged units per slice and peaked at 33,024 bytes of live continuation frames. Yield counts for the unchanged stress fixtures were DOM 152, CSS 411, tables 117, long page 231, images 11 and mixed 985. These are structural cooperative-work measurements, not a promise of host-independent TCG frame times. Public WordPress parsing/style computation is substantially heavier than the old truncated rendering because all theme/icon rules now participate. Diagnostic TCG runs still reported individual >50 ms stall samples under host load; no claim of zero stalls or complete performance parity is made.

In the final release public-site run, 583 seconds elapsed between the Tomato and WordPress screenshots, covering WordPress navigation, verified network requests, cooperative styling/layout and observation polling while another QEMU regression ran concurrently. This is a material end-to-end delay, not an isolated CSS CPU benchmark. The final WordPress view has the intended author colors, but large-site loading performance remains a limitation and is not claimed to match Chromium or the old truncated rendering.

Native browser stack budgets passed: largest frame 4576 bytes (`ResourceCache::remove`), below the established 6144-byte frame budget on the 16 KiB Ring 3 stack. TLS's kernel frame was also 4576 bytes. This is a compiler frame audit plus runtime regression evidence, not a complete call-graph stack proof.

The global pre-existing stack audit still fails the same three unrelated hard-limit entries: PSF move 8592 bytes, virtio-blk stress 8320 bytes, and shell entry 6896 bytes. Those functions were not changed by this milestone.

## Source changes

Networking/resource ownership: `http_target.h`, `tls.cpp`, `programy/przegladarka.cpp`, `programy/husarz/url.h`, `origin.h`, `resource.h`, `async_runtime.inc`, `worker.inc`, and the browser build switches in `Makefile`.

CSS/style/layout/paint: `programy/husarz/document.h`, `html.h`, `css.h`, `css_colors.h`, `css_variables.h`, `css_math.inc`, `css_grid.inc`, `css_shadow.inc`, `flex_layout.inc`, `layout.h`, `paint.h`, `pipeline.h`, and `pipeline_runtime.inc`.

Validation: `narzedzia/test_husarz_css_compat.cpp`, `test_husarz_runtime.cpp`, `test_husarz_fidelity.cpp`, `test_husarz_qemu.py`, `profile_husarz_css.cpp`, the `test_husarz_css_release.py` regression runner, synthetic CSS fixtures, and this report. The fidelity test's old rejection of nested `:not()` was replaced by a positive matching check because that syntax is now supported.

The selector index adds a bounded second fingerprint filter to remove hash-bucket collisions without removing possible true matches. Complex selectors use their own cooperative scan rather than also paying a second whole-compound work estimate. Variable expansion consumes and charges the remaining shared slice; a bounded partial attempt retries once in a fresh slice. Parsed stylesheets are retained on resize, and computed strings are reused rather than appended repeatedly.
