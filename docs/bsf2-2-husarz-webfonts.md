# BSF 2.2 — Husarz CSS @font-face

Code: `programy/husarz/css.h` (descriptor capture), `webfonts.h` (pure
policy/parsing helpers), `webfont_runtime.inc` (browser process),
`worker.inc` (resource worker), `async.h` (`RequestKind::Font`),
`pipeline_runtime.inc` (restyle), `paint.h` (colour glyphs), BSF
`register_scoped_face`/`release_scope`.

## Supported CSS

`@font-face` with `font-family`, `src` (`url()` with `format()` and
`tech()`, `local()`), `font-weight` (keyword, number, range), `font-style`
(normal/italic/oblique), `font-stretch` (keywords/percent; recorded, used
for matching only in a later milestone), `unicode-range` (single, range,
wildcards; an invalid descriptor drops the rule), `font-display` (parsed).

`src` is tried in CSS order: `local()` matches an installed family by name;
`url()` with `format()` of `truetype`, `opentype`, `woff`, `woff2` (and
their `-variations` forms) is requested; `embedded-opentype`, `svg`,
`collection` and unknown formats are skipped without a request. A failed or
invalid source moves to the next one; when none works the page keeps the
local fallback.

Matching: faces of one family are chosen by weight distance and style
(`font-weight: 700` selects the 700 face); `unicode-range` faces of one
family form a composite (scalars outside every range fall back to the next
family / system fonts). Synthetic bold/italic are not generated.

`font-display`: every value renders the fallback immediately and swaps in
the web font when it is ready (auto/block/swap/fallback behave like
`swap`; there is no blocking period). `optional` never swaps after the
first layout of the navigation.

## Loading pipeline

1. CSS is parsed and styles computed with fallback fonts (no blocking).
2. `queue_fonts` (with image queueing, after stylesheets) creates the tab's
   `BrowserFonts` set (scope = unpredictable 64-bit id per navigation) and
   queues one request per used `@font-face` (family referenced by a computed
   style).
3. The existing asynchronous resource queue sends `RequestKind::Font` to the
   **separate resource worker process**: HTTP/HTTPS with the existing TLS
   validation and redirect limit (`MAX_PRZEKIEROWAN`), ≤ 240 KiB response,
   MIME check (font/*, application/font-*, application/x-font-*,
   vnd.ms-opentype, octet-stream or none — never HTML/CSS/JS/images), then
   **WOFF/WOFF2/sfnt decoding and validation in the worker**. The decoded
   sfnt (≤ 1.79 MB) returns through the bounded shared-memory payload.
4. The browser validates the payload again (`Face::open` inside
   `register_scoped_face`), applies the CORS decision and registers the
   face in the tab scope.
5. `fonts_changed` → the tab's next document job marks styles dirty: font
   families re-resolve (scoped name inserted before the CSS family), the
   document reflows and repaints. Other tabs are not relaid out. Layout
   caches never reuse glyph ids of another face (new face id; ids are never
   reused).
6. Navigation (new navigation number), clearing or closing a tab releases
   the scope: face bytes freed, resolution/layout caches invalidated.

Non-blocking: downloads and decoding run in the worker; the browser's
cooperative publication only copies the payload (bounded) and registers
the face. Cancellation: navigation cancels queued tasks
(`cancel_async_tab`); results for an old navigation are discarded.

## Security

- No kernel font parsing; untrusted bytes are decompressed/reconstructed in
  the worker process and re-validated in the browser process.
- TLS validation unchanged (no bypass); HTTP/HTTPS only; redirect limit.
- Size limits: 240 KiB download, decode ratio ≤ 128, decoded ≤ 1.79 MB per
  font, 32 scoped faces and 24 MiB scoped bytes per browser process.
- **CORS (response side)**: same-origin fonts are accepted; a cross-origin
  font is used only if the response carries `Access-Control-Allow-Origin: *`
  or the document's origin (scheme, host, port compared). The kernel HTTP
  client sends a fixed request without an `Origin` header and without
  credentials, so servers that echo the request Origin cannot be satisfied;
  such fonts are treated as denied (safe failure, fallback stays). Missing
  dependency: an `Origin` request header in the kernel HTTP client.
- Isolation: scoped faces never appear in `enumerate()/info()`, Settings,
  `/czcionki` or other processes; they are addressed only through
  `"\x01<scope>:<family>"` names produced by the browser — page CSS cannot
  name them (such tokens are stripped by `rewrite_font_families`), and
  another tab/document has a different scope. Web pages cannot change
  system font preferences.
- Cache isolation: there is no cross-document web-font cache (each
  navigation downloads again) — no cross-site sharing.

## Tests

- Host: `make test-bsf22-husarz-webfonts-host` (185 checks): descriptor
  capture by the production CSS parser; `src` order and format filtering;
  unicode-range, weight, style, stretch, display parsing; MIME and CORS
  decisions (scheme/port/null/`*`/exact origin); WOFF2/WOFF/OTF/TTF faces
  registered and resolved through the production rewrite; layout widths
  change after the web font arrives and equal the web face's own shaped
  layout (every glyph of the word from the web face); unicode-range
  composite; weight selection; isolation between scopes; forged scoped names
  stripped; invalid data never registers; release restores fallback; ids
  never reused; bounded registry.
- Native: `python3 narzedzia/bsf2_native_qemu.py --workload bsf22` (pages in
  `testy/husarz_webfonts/`, served on two origins `10.0.2.100` and
  `10.0.2.101`); results in `bsf2-2-testreport.md`.

## Not supported

`font-variation-settings` / mapping `font-weight` to a variable axis (web
variable fonts render their default instance), `font-feature-settings`,
`size-adjust`/`ascent-override`, the CSS Font Loading API (`document.fonts`),
synthetic bold/italic, WOFF/WOFF2 collections, `@font-face` inside
`@supports`/`@media` blocks beyond what the parser already flattens,
fonts referenced only by styles created later by scripts.
