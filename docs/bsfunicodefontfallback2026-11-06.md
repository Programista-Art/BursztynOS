# Native BSF Unicode and fallback implementation — 2026-11-06

Implementation continued on November 7. This is a partial milestone, not a
claim of complete Unicode, OpenType, terminal, or GUI-matrix acceptance.

## Baseline and existing architecture

The existing `bsf/bsf.cpp` implements checked SFNT directory parsing, cmap 4/12,
loca/glyf decoding with bounded compound recursion, horizontal metrics, legacy
kern, and a native wrapper around the existing bundled stb mask rasterizer.
The rasterizer receives validated vertices, not permission to parse raw TTF.
`bsf/runtime.cpp` performs Ring 3 file access through existing BWS APIs, maintains
per-process font catalog/config/preview state and four LRU loaded faces.
`bursztyn_ui/czcionka.h` is the shared native UI drawing/measurement adapter.
Husarz uses this same runtime through callbacks in `programy/przegladarka.cpp`;
it has no separate font parser. `bsf/config.h` and BWS 92 persist selection.
Theme and desktop geometry remain owned by existing UI components.

Before this work, discovery already enumerated `/czcionki/ttf`; it was not a
hardcoded font list. Limitations were directory-order-dependent selection,
partial Unicode name decoding, no explicit catalog refresh, and separate
fallback loops for measuring and drawing with no negative lookup cache.
Combining marks were advanced/drawn as independent glyphs.

Baseline production ISO SHA-256:
`ff2dd94f2497536f7d1ed865b66f33ac71544e2e24574572a40497682b6be20f`.
Baseline public kernel SHA-256:
`6a621c4be13c139dbb30009ff2408fb346190ace8ffd604e01fef25ca5007803`.
The initial host run passed 12,201 core and 5,876 UI/browser assertions.
Edits overlapped that initial build, so it is not a controlled before/after
performance baseline. Native QEMU baseline was not repeated before editing.

## Implemented discovery and metadata

Directory records are sorted before applying the existing 32-file examination
cap. The directory buffer remains 8 KiB; only safe flat filenames and
case-insensitive `.ttf` extensions are considered. Header/table validation is
still mandatory. Invalid files are independently rejected. Catalog entries
are sorted by family and path for deterministic fallback and presentation.
Duplicate identities are rejected. No new filesystem privilege or BWS was added.

Name IDs 1/2, 4, 6, 16/17 supply family/subfamily, full name, PostScript name,
and preferred typographic family/subfamily. Platform 0 and Windows Unicode
encodings 1/10 are decoded as UTF-16BE, including surrogate pairs. Windows
English is preferred, followed by platform Unicode, then other supported
Windows records. Invalid offsets, odd lengths, invalid surrogate sequences,
controls and names exceeding output capacity reject that record. Names are
never silently cut inside a scalar. Filename is not the family identity.
OS/2 weight and italic flags plus head style flags are used; no artificial
bold/italic rendering was introduced. Existing matching stays within the chosen
family before falling back to another family for missing coverage.

`BSF::rescan()` explicitly rescans at an idle event boundary. It clears lookup
and mask identities and discards loaded file buffers. Pinned masks retain their
storage until released by existing cache leases. A local catalog epoch also changes `BSF::generation()` so retained text
invalidates after refresh even if persisted config is unchanged. Shared config
schema is unchanged. Refresh is process-local; other running processes need their own
rescan/restart to observe newly installed files. This does not implement a
system-wide font-install notification protocol.

## Coverage, fallback and caching

The checked cmap lookup still accepts full Unicode scalars, including
supplementary planes, and rejects surrogates and values above U+10FFFF. Glyph
zero is not coverage. Format 4 now additionally rejects odd segment counts,
odd range offsets and overlapping/unsorted segments. Unsupported outline
formats remain rejected by the existing parser.

One resolver is used by glyph rendering, metrics, kerning and cache queries.
Priority is requested face, other faces in the same family, faces matching the
requested bold/italic flags, then other installed faces in stable catalog order.
A missing result remains explicit; existing bitmap fallback handles it.
Kerning is applied only if both scalars resolve to the same face. The selected
system font is never changed by per-character fallback. Fallback is currently
per scalar, not a whole-cluster coverage search; a base and accent can use
different faces. No configurable fallback list has been introduced.

128 fixed lookup entries cache both positive and negative resolutions, keyed
by primary face identity, scalar and style. Eviction is round robin. Explicit
rescan invalidates every entry, including negative results. First encounters
may load files while resolving; this is not a guarantee of zero repaint-path
I/O. Working sets exceeding the four loaded faces may still cause I/O churn.

The existing alpha-mask cache retains 128 slots of at most 128x128 pixels:
2,097,152 bytes, plus slot metadata. Its key already includes face, glyph,
pixel size and style; color is excluded. Existing LRU and pin rules remain.
Per-file limit remains 2 MiB, maximum loaded file buffers 8 MiB, maximum 16,384
glyphs, 64 tables, 4,096 points, 128 contours, 8,192 vertices and compound depth
8. Raster scratch remains bounded at 1 MiB. Larger-font policy was deliberately
not loosened without a process-budget and parser-resource audit. These are
remaining milestone limits, not newly claimed large-font support.

## Combining marks, editing and shaping limits

`BSF::combining` explicitly supports U+0300–U+036F except CGJ U+034F. This is a
small documented subset, not a versioned UAX 29 property database. Marks have
zero advance in shared measurement. `BSF::MarkPen` centers their ink over the
base advance and stacks successive above/below marks using raster dimensions.
The same helper is used by native UI and Husarz paragraph/menu alpha painting.
This is approximate positioning, not GPOS anchors or pixel-equivalent NFC/NFD.
Leading marks have zero advance and are drawn near the initial pen; dotted-circle
insertion is not implemented. Cross-font baselines use the primary ascent.

Reusable `next_cluster`/`previous_cluster` operate on the supported mark subset.
Native `CzcionkaUI` next/previous editing helpers use them, preserving the original
UTF-8 storage. Individual application editors, hit testing, selections, browser
forms and all terminal paths have not been comprehensively migrated. Generic
UTF-8 decoding remains scalar-validating with replacement on malformed input.

No normalization, canonical reordering, full grapheme segmentation, script or
language itemization, UBA/BiDi, GSUB, GPOS, variation fonts, CFF/OTF, WOFF or
WOFF2 support is claimed. These require future versioned Unicode data and
bounded shaping stages producing glyph IDs/positions and logical byte-cluster
maps before the existing rasterizer. Source text must remain unchanged. Arabic,
Indic and RTL shaping are not enabled by this work. No document-scoped web-font
registration or @font-face download is implemented.

## Integration and verification status

Settings retains its existing Czcionki page, groups discovered family rows,
retains incomplete style families, adds “Odśwież fonty”, and previews Polish,
Cyrillic, Greek and decomposed marks. It does not yet show variant inventories
or whether preview characters used fallback.

Terminal explicitly disables TTF (`shell.cpp`, `BSF::monospace(true)`) and keeps
its original bitmap grid. Native TTF fallback and Unicode cell-width/cluster
policies have not been implemented there. Terminal acceptance is unfinished.
No font file was added; existing DejaVu assets and licenses remain packaged by
the existing ISO/installer font staging. Controlled subset fonts exist only in
host test memory and are never added to production media.

Tests include a real bundled font with a valid one-glyph cmap and independently
named family; the primary contains A but lacks Omega. The test asserts the
actual fallback face ID and matching draw/measure advance, cached negative
results, runtime discovery of custom.TTF without rebuilding, valid scalar
handling, supported cluster boundaries, and Russian/Ukrainian/Greek samples.
Core sanitizer tests include the existing 1,200 deterministic font mutations,
plus Unicode name and malformed-surrogate regressions. This is mutation smoke
testing, not a new dedicated coverage-guided fuzz campaign.

Final test/build/QEMU results and artifact hashes are appended after validation.

Build integration: the added shared runtime code exceeded Tapety's fixed TEXT
segment by 2,400 bytes. Its header and linker reservation were increased by one
4 KiB page (TEXT 0x44000 to 0x45000, DATA offset/VA moved by one page). DATA
capacity, heap limits and the 512 KiB executable budget were preserved. The
lookup cache was limited to 128 entries (2,560 bytes) rather than retaining the
initial 512-entry implementation.
Shell's fixed TEXT reservation also overflowed by 192 bytes. Its header/linker
TEXT reservation grows by one 4 KiB page (0x24000 to 0x25000); DATA starts at
0x1026000, with unchanged DATA capacity and heap policy. This is executable-code
capacity for the shared implementation, not an increased font allocation limit.

## Current validation evidence

- Core ASan/UBSan: **12,204 checks PASS**, including 1,200 font mutations.
- UI/browser ASan/UBSan: **8,009 checks PASS**, including controlled cmap
  fallback, Unicode sample mapping, mark placement, rescan generation and
  unchanged following-character position after a combining mark.
- Settings/themes/Start power ASan/UBSan: **2,696 checks PASS**.
- Desktop TTF/Settings/Start ASan/UBSan: **187,840 checks PASS**, including
  repeated font transitions and existing icon/selection geometry assertions.
- Production `make -j4 iso`: **PASS**. BSF asset/font/license gate passes;
  metadata gate inventories **615 entries, zero findings**. Public kernel
  symbols and DWARF remain stripped; matching private ELF is archived by hash.

[Core and UI sanitizer log](bsf-unicode-2026-11-06/host-core-ui.txt),
[final layout sanitizer log](bsf-unicode-2026-11-06/host-layout.txt),
[regression log](bsf-unicode-2026-11-06/host-regressions.txt),
[release assets and hashes](bsf-unicode-2026-11-06/release.json).

Current public kernel: 10,334,928 bytes, SHA-256
`48472ba99e6cc7181572b20d68f5484da2d3cc8f44812685d3cb3b27aeb29c83`.
Current ISO: 40,128,512 bytes, SHA-256
`40eb7e2f5c2f2549f64e801cd62457a924dffcbe16d4691798759f884fcfc5ac`.
Private matching ELF is under
`.release-private/48472ba99e6cc7181572b20d68f5484da2d3cc8f44812685d3cb3b27aeb29c83/`.

Core host measurement in the sanitizer run: first raster 96,201 ns, average
cached lookup 709 ns, 10,464 hits / 3,683 misses / 3,295 evictions. Those are
host observations under concurrent build load, not a native performance
comparison. Discovery, fallback latency, process RSS, desktop repaint and Husarz
paragraph cost were not separately benchmarked. No speedup is claimed.
Catalog storage is 10,112 bytes, resolver entries 2,560 bytes, loaded-face
metadata 2,240 bytes. Directory sorting has a transient 24 KiB buffer budget.

## Source changes and API locations

- `bsf/bsf.h`: additional name metadata, mark positioning and subset cluster helpers.
- `bsf/bsf.cpp`: UTF-16 name records, preferred family/style, OS/2 italic,
  stricter cmap 4 validation. Rasterizer/outline implementation is preserved.
- `bsf/runtime.h`, `bsf/runtime.cpp`: `rescan`, `resolved_face`,
  `measure_styled_cp`; original `measure_cp` signature retained. Sorted catalog,
  unified cached scalar fallback, same-face kerning, local generation invalidation.
- `bursztyn_ui/czcionka.h`: zero-advance mark painting and subset editing boundaries.
- `programy/husarz/layout.h`, `programy/husarz/paint.h`: native mark advance
  and shared mark-pen positioning in paragraphs/menu labels.
- `programy/przegladarka.cpp`: italic-aware metrics use shared runtime resolver.
- `programy/ustawienia/layout.h`, `programy/ustawienia/ustawienia.cpp`:
  existing font-page refresh action, family grouping and multilingual preview.
- `programy/tapety/tapety.cpp`, `programy/tapety/tapety_linker.ld`:
  matching one-page code-segment adjustment.
- `shell.cpp`, `programy/shell_bsf_linker.ld`: matching one-page code reservation.
- `narzedzia/test_bsf.cpp`, `narzedzia/test_bsf_ui.cpp`: parser and fallback/layout regressions.
- `narzedzia/fixtures/husarz/fonts.html`: multilingual native browser fixture.
- This report and `docs/bsf-unicode-2026-11-06/`: new evidence artifacts.

Previously uncommitted release-audit changes in Makefile, metadata verifier,
its tests and audit documentation were preserved; they are not font changes.

## Native GUI acceptance and remaining work

The production ISO passed the existing Linux QEMU/OVMF/TCG 1280x720 native
font test with `--fonts-only --native-font-apps --timeout 900`. The native
browser requested `/fonts.html`; Settings preview remained local, Apply changed
font generation and published 24 px; Notepad, Calculator and Explorer repainted;
Start was captured after the update. The harness reports `PASS` and
`no_guest_fault: true`. The hash called `kernel_sha256` in its JSON is the private
unstripped observation ELF (`6abbd78804eff956f910c5654b70e548b293ab50adb72836c3df2aeef22c591a`),
not the stripped public kernel hash reported above.

[QEMU verdict](bsf-unicode-2026-11-06/qemu-result.json),
[run log](bsf-unicode-2026-11-06/qemu-run.txt),
[serial log](bsf-unicode-2026-11-06/qemu-serial.txt),
[native browser screenshot](bsf-unicode-2026-11-06/native-husarz-multilingual.png),
[native Settings screenshot](bsf-unicode-2026-11-06/native-settings-multilingual.png).

Visual inspection of actual GUI screenshots confirms readable Polish/English,
`Привет мир`, `Привіт світ`, `Україна Київ Ґґ Єє Іі Її Ўў`,
`Καλημέρα κόσμε`, `Ελληνικά Αθήνα Ωμέγα`, accented Greek samples and decomposed
`é ä ä́ ά ώ й` in Husarz, without visible missing-glyph boxes for these
samples. Settings shows Cyrillic, Greek and attached accents in its actual
Regular/Bold preview. This is visual evidence of these fixtures at the tested
size/scale, not a pixel oracle for every font or canonical equivalent.
Notepad's font reflow was exercised; inserting/deleting/displaying these exact
multilingual document samples in native Notepad was not tested. The runtime
font-install refresh and forced missing-glyph fallback are validated by host
integration tests, not a live font installation in this QEMU run.

**Milestone remains incomplete.** Required follow-up includes whole-cluster
fallback, versioned Unicode properties and full grapheme editing, normalization,
GPOS positioning, script/direction/run abstractions, Terminal TTF cell policy,
complete editor/forms/selection integration, first-touch I/O improvements,
larger-font resource policy, dedicated coverage-guided parser/layout fuzzing,
variant/fallback indicators in Settings, retaining an uncommitted local preview
selection across refresh, controlled performance/memory benchmarks, and the
full 1920x1080 / 100–200% DPI / 12–24 px / installed-disk/reboot matrix.
The three installed families are DejaVu Sans, DejaVu Sans Mono and DejaVu Serif
(the latter represented by the bundled Condensed Italic face). Default assets
and licenses remain unchanged. No Windows QEMU frontend was available or tested.
No new claim is made about audio, network, BWS isolation or unrelated regression
suites beyond the existing native font-test execution and listed host results.
