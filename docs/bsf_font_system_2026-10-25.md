# Bursztynowy System Czcionek (BSF)

Status: native implementation delivered; functional acceptance results are recorded
below, including installed persistence and real Tomato observations. Tests span
several release revisions; the evidence table identifies their exact kernels.
Controlled same-kernel BSF/bitmap desktop repaint and text-heavy page timings
are recorded in the final profiling section. This compares current rendering
paths; it does not benchmark a historical pre-BSF operating system build.

## Ownership and data flow

`bsf/bsf.cpp` is a freestanding Ring 3 library. A checked native TrueType decoder
produces bounded quadratic outline vertices; only `stbtt_Rasterize` receives those
vertices. No stb raw font parser is called. Glyph coverage is grayscale alpha,
independent of foreground color. Native BursztynUI and Husarz use the same library.

Ring 0 provides ordinary file/memory/process operations, a copied 24-byte font
configuration, and bounded alpha-mask transport for server decorations. The
kernel never reads TTF tables. GRUB bootstrap imports font files as opaque bytes.
Early boot retains its built-in bitmap font. Font initialization is optional for
GUI startup and rejected/missing fonts retain the bitmap fallback.

Each GUI process owns its catalog, loaded faces and glyph cache. No parser or
heap pointer crosses IPC or appears in persistent settings. GUI processes without
general filesystem rights receive read-only access to the dedicated flat font
directory through `bsf/asset_path.h`; traversal and nested directories are rejected.

## Assets and licenses

Source and installed directory: `czcionki/ttf/` (native absolute path
`/czcionki/ttf/`). Enumeration is flat, at most 32 TTF files, case-insensitive
extension. Embedded family/style names are metadata, never filesystem paths.
Stable nonzero FNV-1a IDs derive from the exact controlled path; collisions are
rejected. Missing persisted IDs fall back automatically.

Bundled fonts: DejaVu Sans Regular and Bold 2.37, plus the existing DejaVu Sans
Mono and DejaVu Serif Condensed Italic (existing assets, version 2.33). These are upstream DejaVu designs, not
original Bursztyn fonts. Exact notices and provenance ship beside the fonts in
`SOURCE.txt` and `LICENSE-DejaVu.txt`. The source package is Ubuntu
`fonts-dejavu-core` 2.37-8; upstream is https://dejavu-fonts.github.io/ .

Rasterizer: stb_truetype 1.26, upstream https://github.com/nothings/stb , commit
`003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`. Incorporated files:
`bsf/vendor/stb_truetype.h`, `LICENSE`, `SOURCE.txt`. The MIT license alternative is
selected; its notice also ships as `czcionki/ttf/LICENSE-stb.txt`. No GPL font stack
or proprietary Windows font was added.

`narzedzia/package_bsf.py` packages the files and notices as ISO assets and GRUB
modules. Settings is a separate Ring 3 BUR module, not a kernel-linked program
blob. `narzedzia/verify_bsf_iso.py` compares packaged bytes with source assets and
the Settings binary. Bootstrap installs those assets into the live filesystem;
the normal installer copies that filesystem into the installed system.

## TrueType support and bounds

Supported: SFNT TrueType outlines; Unicode cmap formats 4 and 12; head, maxp,
hhea, hmtx, loca, glyf; name metadata; OS/2 weight/PANOSE; post mono flag;
horizontal format-0 kern pairs. Rendering uses real advances, bearings,
ascent/descent/gap, quadratic outlines, affine XY compound components and
on-demand antialiasing. Unicode is mapped through cmap, never used as a glyph ID.

Checked bounds include file/table ranges and overlaps, arithmetic overflow,
metrics, cmap ranges/mappings, loca ordering, glyph streams, contour endpoints,
point/flag expansion, compound child IDs and dependency cycles, kerning indices,
and raster bounds. Limits:

| Resource | Limit |
| --- | ---: |
| TTF file | 2 MiB |
| table directory | 64 |
| glyphs per face | 16,384 |
| contours / decoded points / vertices | 128 / 4,096 / 8,192 per glyph tree |
| compound depth / dependencies | 8 / 64 |
| enumerated files / resident faces | 32 / 4 per process |
| pixel sizes / glyph bitmap | 1–128 px / 128×128 |
| rasterizer scratch arena | 1 MiB, operation-local heap |
| glyph alpha cache | 2 MiB per GUI process |

Unsupported: OTF/CFF, collections, WOFF/WOFF2, downloaded webfonts, variable/color
fonts, hint execution, GSUB/GPOS/complex shaping, compound point attachments.
Unsupported compound placement rejects that glyph safely and uses fallback.
The reusable borrowed-byte Face API permits a future sandboxed webfont loader.

## Cache and rendering

128 fixed slots, each with 16 KiB alpha storage, deterministic least-recently-used
eviction. Key: stable face ID, cmap glyph ID, pixel size, bold/italic identity.
Metadata includes dimensions, baseline-relative bearings, advance, last-use
counter and alpha pointer. Color is applied at paint time. Cooperative browser
painting pins its current glyph across suspension; eviction skips pinned entries.
Generation invalidation invalidates keys without freeing a pinned mask.

Native widgets share UTF-8 decoding, measurement, kerning and rendering.
Malformed UTF-8 consumes a bounded sequence and produces a replacement/fallback
glyph. Caret hit-testing and byte offsets use proportional advances and preserve
codepoint boundaries. UI scale converts logical font size to physical pixels.
Alpha composition uses caller color, existing background and existing clipping.
The compositor receives already-rendered title coverage, never font bytes.

Fallback: selected face → usable bundled TTF → built-in Unicode bitmap. Requested
missing Bold/Italic uses the closest available face, preferring an upright face
of matching weight; no synthesized professionally designed face is claimed.
Terminal content deliberately retains the existing mono bitmap path, while its
window title uses the selected UI font.

## Settings and live updates

Ustawienia is a normal BursztynUI Ring 3 application, installed as
`/programy/ustawienia.cebula/ustawienia.bur`, present in Start and system apps.
Left navigation: Wygląd, Czcionki, System. Only the font page implements settings;
other pages identify themselves as placeholders.

“Ustawienia czcionki” enumerates installed families, offers sizes 12–24 and shows
Regular/Bold previews including “Zażółć gęślą jaźń”, ASCII alphabets and digits.
Changing controls updates local preview only. Zastosuj publishes the validated
selection. Restore defaults changes the local preview pending Apply.

`/system/bsf.cfg` is a bounded 24-byte versioned/checksummed native file containing
stable family identity, default size and generation. Invalid files use defaults.
The kernel accepts writes only from the canonical Settings process and broadcasts
the existing safe theme notification after persistence succeeds. Processes
refresh their local config, invalidate glyph keys, recompute UI geometry and
repaint. There is no cross-process global font pointer.

## Husarz desktop/web separation

Browser chrome uses the selected UI font. Web styles retain their author font
family, size, weight, line-height and color. Family resolution follows the author
list: an available explicit family takes priority; missing names continue to the
next fallback. Default/sans-serif uses the selected system family. Serif uses an
installed serif when suitable, otherwise the selected family. Monospace uses a
real installed mono face or the mono bitmap fallback.

Weights 100–500 map to Regular; 600–900 to Bold. Web layout uses proportional
advances and real font line metrics. UI/core support kern pairs; web fragment
layout currently measures codepoints without pair kerning, consistently with
its caret and paint positions. A system font change cancels pending painting and
reflows retained page styles, without replacing explicit website typography or
author colors. Cold glyph rasterization is charged before work, at most one cold
glyph per 2,048-unit cooperative paint slice; cached glyphs use the fast path.

## Test evidence

ASan/UBSan core: 12,201 checks passed, including valid fonts, truncation,
malformed offsets/tables/cmap/loca/glyf/metrics, compound cycles and acyclic depth limits, excess points, invalid kerning indices,
1,200 deterministic
mutations, Polish uppercase/lowercase, several pixel sizes, antialiased masks,
Regular/Bold identities, cache hits/eviction/pinning and invalid UTF-8.

ASan/UBSan BursztynUI/browser: 5,876 checks passed: large-font status-bar geometry at 100%/125%, restore to 16 px, generic discovery, local preview,
apply/generation, restart and invalid config, all-corrupt font fallback,
proportional button/label/caret geometry, clip/color/alpha, explicit CSS family
priority, Polish bold text and cooperative paint budget.

Release ISO verification: four TTF files and three notice/provenance files match
source bytes; Settings payload also matches. Native release QEMU confirms alpha
coverage, author colors, the Settings font page, local preview, Apply to 24 px,
and visible live geometry reflow. The original tested release artifact is
identified by SHA-256 (the later repackaged ISO is identified below):

- kernel: `9e34b08356fc3598e1432d3ed27e042a1598fe21a9609eab2f34c1697df0a216`
- ISO: `0fa3bedfa0dfd9c67caa3673795570a457c36e8106f15ca54435eb9839bf764f`

A separate ISO containing the same unmodified release kernel and four deliberately
invalid font assets boots a readable bitmap desktop, opens Husarz and Settings,
and applies configuration without a guest fault. This is a fault fixture, not the
production ISO. Native UTF-8 selection, clipboard copy/cut/paste, password
copy/cut protection and GIF first-frame/cache behavior pass on the release
revision identified in the evidence table.

The release CSS run passes external stylesheet redirects/query URLs, variables,
author text/button/footer colors, stylesheet-relative images, narrow/wide flex
layout, maximize/restore, and stale-resource cancellation. Its exact artifact
is recorded below; subsequent changes include browser toolbar invalidation and
glyph-size cache reuse. Rendering, scrolling and font application were rerun
with the final kernel.

## Memory and performance

Host x86-64 sizes: Face 320 bytes, glyph slot 56 bytes; 128 slots = 7,168 bytes.
Cache masks reserve 2,097,152 bytes on the heap; usage stats track actual occupied
bitmap rectangles separately. Four resident file buffers are capped at 8 MiB
combined. Catalog and face metadata are bounded per process. Outline and raster
scratch allocations are transient heap allocations, never multi-megabyte stack
objects. Server title masks allocate at most 64 KiB for each of 16 windows,
maximum 1 MiB across the compositor; no static 1 MiB kernel BSS array was added.

Preliminary sanitizer run under concurrent compilation: first glyph 1,832,027 ns,
cache hit average 2,056 ns; hits 10,464, misses 3,682, evictions 3,296 across the
stress test. These are host stress-test costs, not desktop repaint timings.
Later host profiling, stack audit, installed persistence, Tomato observations
and regression results are recorded below. Controlled QEMU desktop/page timing
comparisons were subsequently collected in the final profiling section.

Additional checks: all 24 generated BUR headers agree with their ELF text/data
contracts. A kernel symbol audit confirms no BSF Face parser, rasterizer or cache
symbols in Ring 0. Host Husarz fidelity passed 3,697 checks; CSS compatibility
passed 362. Further completed suites are listed below.

Exact supplementary sizes on x86-64: FontInfo 316 bytes (32-entry catalog
10,112 bytes), Cache object 72 bytes, transient Outline 114,736 bytes, Scratch
1,048,592 bytes including header/alignment. Resident Sans Regular file is 759,720
bytes; Bold 708,920; Mono 333,636; Serif Condensed Italic 338,140. All four font
files total 2,140,416 bytes. The maximum outlined recursion adds at most eight
272-byte frames; measured rasterize frame is 752 bytes, Face::open 672,
Face::parse 560 and runtime init 592. These are individual frame measurements,
not an assertion that the entire OS stack audit passed.

Optimized host microbenchmark (`make profile-bsf-host`): the initial observation
was 58,870 ns for the first B at 16 px, average 1,547 ns over 200,000 cache hits,
108 occupied alpha pixels and one miss. This is a microbenchmark under competing
host load; it does not substitute for native repaint or page profiling.

The direct BursztynUI desktop text path now batches a clipped run into one alpha
blit. Its temporary ARGB buffer is bounded to 262,144 pixels (1 MiB), up to
2,048×256 pixels, with the prior glyph-row path retained for allocation failure
or larger runs. A host assertion checks one submission, clipping and unbaked
coverage/color for Polish text. Release QEMU repaint and heavy-page functional
checks pass; this does not establish an end-to-end timing improvement.


## Resumed validation and observed typography

The browser toolbar measures button labels using the shared font engine. Its
height follows the UI line height. When full Back/Forward labels would crowd the
address field, they become arrows with explanatory tooltips. This recomputes
on measurement, resize and live font updates. Settings selector containers use
natural measured height so font-family and size controls are not clipped.

On the real HTTPS Tomato page, the final release shows smooth proportional
48 px bold headings, proportional body/button/navigation text, and the retained
red author button color. The historical bitmap comparison is
`.husarz-test/css-complete-validation/real-sites/real-tomato.png`; final screenshots
are `.bsf-test/complete-tomato-qemu/real-tomato.png` and `real-tomato-footer.png`.
This is visual observation of the public page, not a pixel-equivalence assertion
against Chrome. Webfont icon faces remain unsupported, and navigation spacing
still reflects the current CSS engine's limitations.

Additional host regressions completed after resumption:

| Area | Result |
| --- | --- |
| PSF bootstrap, normal / diagnostic | 25,186,534 / 25,186,244 checks, 21 scenarios each, ASan/UBSan |
| production keyboard / TEXTINPUT / dialogs | 7,291 checks |
| WM click routing | 183 checks |
| Explorer and desktop behavior | 15,546 checks |
| native network ownership | 7,693 checks |
| user-fault isolation policy | 32,768 checks |
| Husarz engine | 127,432 checks |
| Husarz selection / clipboard | 987,380 checks |
| Husarz native runtime | 7,727 checks, 26 real HTTP parser requests |
| Husarz JavaScript | 10,865 checks |
| Husarz GIF | 11,656 checks |
| storage / BSP2 | 10,512 checks plus 138 directory/file move assertions |
| heap/PMM/PCI input initialization locks | 4 scenario groups |
| Husarz stack budget | PASS; largest individual frame 4,576 bytes |

The bootstrap addition preserves the pre-existing directory creation order by
appending the font/Settings directories. This also preserves the existing
bootstrap fault fixtures' inode identities.

Earlier optimized host profiling: first B/16 rasterization 55,637 ns; mean cache
hit 1,533 ns over 200,000 hits and one miss (99.9995% hit ratio), about 36 times
less time than first use. Native `BSF::measure_text` averages 106,518 ns for a
64-byte Polish/Latin UTF-8 sample measured at 461 px. Host filesystem loading is
outside the warm measurement loop. These are host microbenchmarks under concurrent
VM load, not native desktop repaint timings or an isolated CPU benchmark.

Reproduce using `make test-bsf-host test-bsf-ui-host profile-bsf-host`,
`python3 narzedzia/test_husarz_qemu.py --fonts-only`,
`--font-fallback-only`, `--editing-only`, `--css-only`, `--performance`,
`--modern-javascript` and `--tomato-only` in separate invocations.
`python3 narzedzia/test_bsf_installed_qemu.py` installs only into newly created
private `.bsf-test` images, verifies packaged font/Settings bytes, applies a size
change and checks a second disk-only boot. Its persistent observer uses physical
A/B paths; `/system` and `/programy` are runtime PSF aliases.


The status-bar line box and vertical centering now use the shared font metrics.
A 24 px legacy preferred height expands when needed, and returns to its requested
height after restoring a smaller font. The host regression covers parent layout,
24 px text, 125% scaling, and restoring 16 px without retained excess height.

JPEG regression passes under ASan/UBSan. PNG decoder, malformed input, scaling,
alpha and packaging scenarios A–O also pass. The repository's historical
`obrazy/tapety/nowatapeta.png` photo is absent, so the PNG runner used its existing
`BURSZTYN_TEST_PNG` override with a generated 1920×1080 RGB pattern at
`.bsf-test/png-regression-fixture.png`. No production image was replaced.
The JPEG wallpaper and manifest host regressions pass separately.

Boot memory detail: the bundled TTF payloads total 2,140,416 bytes as opaque
Multiboot modules, plus Settings and notices. The current importer retains those
module source buffers; Live PSF stores file copies, and userspace loads its own
bounded resident face buffers. There is no globally shared glyph cache. The
per-process resident metadata/cache subtotal, excluding font file bytes and
small scalar globals, is 2,115,896 bytes (Cache/masks/slots + catalog + four Loaded
records + two Config records). Transient direct-draw buffers can add at most
1 MiB per active text call; outline/raster scratch is separate transient storage.


## Final validation evidence and scope

The final packaging run (`.bsf-test/release-packaging-final.log`) succeeded.
`python3 narzedzia/verify_bsf_iso.py BursztynOS.iso` was rerun after resuming and
passed: all seven font/license/source assets and the Settings binary match.
The final kernel and ISO hashes above were independently rechecked. The QEMU
runs below retain their original hashes; rebuilding an ISO may change its hash
without changing the kernel. No claim of byte-identical tested ISOs is made.

| Test | Evidence under `.bsf-test/` | Kernel SHA-256 prefix | Result |
| --- | --- | --- | --- |
| Native AA, preview, Apply 24 px, Start, Notepad/Calculator/Explorer repaint | `release-accepted-native-qemu/result.json` | `9e34b08356fc` | PASS |
| Scroll, thumb/track, Home/End, Page Up/Down, maximize/restore, history | `final-scroll-qemu/result.json` | `9e34b08356fc` | PASS |
| Images/cache, forms/UTF-8, CSS, theme isolation, request cancellation | `final-render-qemu/result.json` | `9e34b08356fc` | PASS |
| Public Tomato heading/body/navigation/button/footer | `complete-tomato-qemu/result.json` | `9e34b08356fc` | OBSERVED; no guest fault |
| Basic JS, timers, events, cancellation, tab isolation | `verified-js-qemu/result.json` | `082e74932d8f` | PASS |
| Modern JS, FPU isolation, Promise/async/fetch/storage | `verified-modern-js-qemu/result.json` | `082e74932d8f` | PASS; deliberate fault probe expected |
| Six heavy pages, eight maximize/restore actions, scrolling, cancellation | `release-heavy-qemu/result.json` | `1eaa461fde5d` | PASS |
| Author CSS, variables, external resources, responsive layout | `release-css-second-qemu/result.json` | `1eaa461fde5d` | PASS |
| Install assets, Apply, restart, disk-only restore to 24 px | `installed-r6pivhb_/result.json` | `1eaa461fde5d` | PASS |
| UTF-8 editing, clipboard/password protection, GIF reuse | `final-editing-qemu/result.json` | `f1b133605593` | PASS |
| All four TTF assets corrupt; readable bitmap desktop and Apply | `final-fallback-qemu/result.json` | `f1b133605593` | PASS |

Earlier failed or timed-out attempts remain in the artifact directory for
traceability. They are not PASS evidence; the table selects completed reruns.
The scroll rerun follows a real toolbar bug fix: layout measurement must report
invalidation only when geometry changes. The UI host regression now checks this.
Changing only size retains cache entries for other pixel sizes; changing family
invalidates the appropriate face generation.

The installed test used a newly created private image, verified all seven asset
hashes and 202,368 Settings bytes, and booted twice without ISO. It predates the
last toolbar/cache adjustments and is not a final-kernel installation rerun.
The native application suite demonstrates live resizing of Notepad, Calculator,
Explorer and browser chrome. It does not individually exercise every legacy
application/dialog at every available font/scale combination.

Remaining limitations: no OTF/CFF, WOFF, downloaded webfonts, hint execution or
complex-script shaping; web fragment placement currently omits pair kerning;
Terminal content deliberately keeps bitmap monospaced text. Host cache and
measurement microbenchmarks and same-kernel native BSF/bitmap timing comparisons
are available below. Heavy-page responsiveness and successful repaint alone
remain functional evidence, distinct from those timings.


Resume verification (`.bsf-test/resume-verification.log`): the freshly rebuilt
ASan/UBSan core suite passed 12,201 checks and UI/browser suite passed 5,876.
The optimized host rerun measured first glyph 2,913,519 ns, mean cache hit
1,731 ns (200,000 hits, one miss), and 111,165 ns per 64-byte text measurement.
The cold sample varied substantially from the earlier run; these single cold
samples do not establish a stable latency distribution. Capacity remains
2,097,152 bytes with 108 occupied alpha pixels for the one-glyph benchmark.
`.bsf-test/resume-evidence.json` indexes the then-current artifact hashes and the
11 completed QEMU records above. `git diff --check` passed after report updates.

## Controlled native BSF versus bitmap profiling

`narzedzia/profile_bsf_qemu.py` runs the existing native profile harness twice,
sequentially, using one private `BSF_PROFILE=1 HUSARZ_PERF=1` kernel. The BSF
image retains the bundled fonts; the bitmap image replaces all four TTF payloads
with invalid bytes. The harness verifies matching kernel bytes and observes
the explicit no-usable-TTF fallback message. No data disks attach. This is a
comparison of current render paths, not the historical pre-BSF OS or a benchmark
of the desktop text batching change in isolation.

Environment: QEMU TCG, `pc,hpet=on,i8042=off`, one `max` vCPU, 2 GiB RAM,
OVMF, VirtIO VGA at 1280×720, browser viewport 782×410. The same loopback HTTP
fixtures and USB input automation run in both guests. No compilation runs during
these measurements. Each guest supplies eight full desktop repaints with Start
closed and 18 entries, followed by three alternating visits to each page.
The first desktop sample follows startup/layout and is not a cold-glyph cost.
Desktop timing spans `RysujPulpit` through its GUI submission call; it does not
observe physical display scanout.

Times below are median [minimum–maximum], in milliseconds. Desktop rows have
seven subsequent samples; each page row has three samples including first visit.

| Native measurement | Bitmap fallback | BSF |
| --- | ---: | ---: |
| First observed full desktop repaint | 24.970 | 65.858 |
| Subsequent full desktop repaint | 21.230 [21.079–21.464] | 50.662 [50.507–50.741] |
| Mixed page navigation | 30,758 [30,369–41,254] | 26,436 [24,105–31,351] |
| Mixed page layout | 4,321 [4,294–4,411] | 4,255 [3,786–4,317] |
| Mixed page paint | 454 [337–701] | 570 [175–1,726] |
| Text-heavy page navigation | 19,675 [19,312–34,622] | 8,931 [7,832–10,782] |
| Text-heavy page layout | 985 [971–1,674] | 2,955 [2,757–3,089] |
| Text-heavy page paint | 628 [501–1,709] | 568 [381–953] |

BSF adds measurable desktop and text-layout cost. Navigation includes scheduling,
HTTP/resource completion and publication; it is not the sum of layout and paint
CPU counters. Its lower observed median does not establish a rendering speedup.
The text fixture has 246 nodes and 6,788 fragments in both paths, but proportional
metrics change document height from 35,408 bitmap pixels to 18,212 BSF pixels,
with 64 versus 128 visible paint candidates. Thus paint rows also compare
different visible work. The mixed fixture has 1,266 nodes and 2,377 fragments.
These small TCG samples describe this run, not physical-hardware latency.

Both guests completed all six navigations with PASS and no guest fault. Every
page sample respected the 2,048-unit work cap. Maximum observed slice duration
was 380 ms with BSF and 261 ms with bitmap under TCG; bounded work is not a
guarantee of a 2 ms wall-clock slice. Browser `cachehits/cachemisses` in the raw
records are resource-cache counters, not glyph-cache counters.

Evidence: `.bsf-test/resume-native-comparison-3/comparison.json`, both mode
directories' `result.json`, `serial.log`, and six screenshots each. The exact
instrumented artifacts are retained as `profile-kernel.bin` and `profile.iso`:

- kernel: `d1a3141c7ae4d85b348c41b47a55c62ef559f5f99a9fecf2928b71d31c36cc59`
- ISO: `6b6c7037dcb34b1b347ca2e537442161ec10d225add290329207b7658771f0b2`

The previously empty `bsf-text-heavy.html` fixture now contains a 48 px heading
and 119 long Polish/Latin paragraphs. The native parser confirms 246 nodes.
The performance host suite now covers this fixture and checks that node count;
the QEMU profile mode rejects missing/empty fixtures before booting. Earlier
interrupted attempts are not PASS evidence.

Reproduce with a private `make BSF_PROFILE=1 HUSARZ_PERF=1 iso` build and:

```sh
python3 narzedzia/profile_bsf_qemu.py \
  --iso .bsf-test/resume-native-comparison-3/profile.iso \
  --kernel .bsf-test/resume-native-comparison-3/profile-kernel.bin \
  --output .bsf-test/native-comparison-new
```

The output directory must be new. The wrapper retains both profile artifacts,
packages its bitmap fixture, verifies both runs, and writes raw samples plus
median/minimum/maximum summaries. Production builds default to disabled profile
instrumentation.

After profiling, a normal `BSF_PROFILE=0 HUSARZ_PERF=0` build restored the root
`BursztynOS.iso`, `system_operacyjny.bin`, and Settings binary, which were absent
at resumption. The rebuilt release kernel is byte-identical to the previously
accepted kernel, SHA-256
`9e34b08356fc3598e1432d3ed27e042a1598fe21a9609eab2f34c1697df0a216`.
The newly packaged ISO is
`1b2fdd0ac23fd90fc6abad930a6b511362c8300013c6dfd6db835f3d612ae7c4`.
The root ISO verifier passes all seven font/notice assets and Settings bytes.
Neither desktop nor browser profile-log marker remains in the production kernel.

Final host validation: `make test-husarz-performance-host` passes 4,621 checks
under ASan/UBSan, including all seven heavy fixtures. The restored text fixture
passes with 246 nodes and 166 cooperative yields. Peak coroutine storage is
33,024 bytes and the deterministic slice cap remains 2,048. Evidence is
`.bsf-test/resume-performance-host.log`. Python syntax checks and final
`git diff --check` pass. `.bsf-test/resume-native-comparison-3/evidence.json`
indexes the rebuilt release hashes and completed native comparison records.
