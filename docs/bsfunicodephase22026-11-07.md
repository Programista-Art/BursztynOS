# BSF Unicode phase 2 — 2026-11-07

## Scope and stage gates

This continuation preserves all prior uncommitted BSF/release work and newly
present font files. It implements a production component of stage 1, not all
five requested stages. Stage 1 is not fully accepted: browser cluster layout,
normalization and comprehensive editor/layout integration remain incomplete.
Consequently stages 2–5 are not advanced or advertised as complete. No GSUB,
GPOS or RTL feature has been enabled. A passing segmentation conformance suite
is distinct from correct shaping or full language support.

Baseline ISO SHA-256:
`40eb7e2f5c2f2549f64e801cd62457a924dffcbe16d4691798759f884fcfc5ac`.
Baseline public kernel SHA-256:
`48472ba99e6cc7181572b20d68f5484da2d3cc8f44812685d3cb3b27aeb29c83`.
Previous implementation and evidence remain in
[the original report](bsfunicodefontfallback2026-11-06.md).

## Stage 1 — Unicode graphemes and cluster fallback

### Versioned properties and segmentation

`bsf/unicode.inc` implements Unicode **15.1.0** default extended grapheme
boundaries from [UAX #29 revision 43](https://www.unicode.org/reports/tr29/tr29-43.html).
It handles CR/LF/Control, Extend, ZWJ, Prepend, SpacingMark, Hangul L/V/T/LV/LVT,
regional-indicator parity, Extended_Pictographic ZWJ sequences, and Indic
Conjunct Break rule GB9c. Hangul syllable properties are calculated
algorithmically. Segmentation scans forward with constant state, no heap
allocation, and binary-search property lookups. Previous-boundary queries scan
from the beginning of the supplied prefix; they are linear, not constant-time.

`narzedzia/generate_bsf_unicode.py` generates packed 7-byte sorted range
records in `bsf/unicode_data.inc`. Its inputs are checked in under
`narzedzia/unicode/15.1.0/`, with SHA-256 records, so builds need no network or
host Unicode library. Sources were extracted from
`https://www.unicode.org/Public/15.1.0/ucd/UCD.zip`.
The native arrays occupy 8,799 bytes: GCB 4,011, Extended_Pictographic 546,
InCB 1,407, Default_Ignorable 119, and canonical combining classes 2,716.
Unicode license attribution is staged under `/licenses/Unicode.txt`.

Existing `next_cluster` / `previous_cluster` signatures are preserved.
`cluster_floor`, `cluster_boundary`, and `cluster_count` provide bounded
whole-buffer offset validation and counting. `previous_cluster` expects a
boundary offset; callers handling arbitrary offsets must first use
`cluster_floor(text, size, offset)` with the full buffer. Scalar iteration uses
the existing bounded, scalar-validating `utf8`. Invalid UTF-8 retains the
existing one-byte replacement policy; original bytes are never normalized,
reversed or rewritten by segmentation. Native shared navigation helpers that
already called these APIs now use full EGC segmentation.

All **1,187 official GraphemeBreakTest cases pass**, including next/previous,
cluster counts and classification of every UTF-8 byte offset. Additional cases
exercise malformed bytes and geometric mark placement. Passing this data set
is evidence for the default 15.1 segmentation algorithm, not an assertion of
full Unicode text-system compliance or shaped emoji/Indic rendering.

### Cluster font selection and positioned glyphs

`BSF::resolved_cluster` tests every required scalar of the supplied cluster
against a single face's validated cmap. Default-ignorable characters do not
require standalone glyphs. Selection tries the requested face, same-family
faces, matching styles, and remaining installed faces, in existing catalog
order. Each candidate is examined once per search. Glyph zero is not coverage.
This does not prove the face can shape emoji or complex scripts.

16 fixed cluster cache entries retain up to 128 original bytes each. Keys
include exact bytes/length, primary face and style; there is no hash-only text
identity. Both successes and failures are cached. Catalog refresh invalidates
all entries with existing scalar/cache invalidation. Longer clusters are not
cached or split; input is bounded at 65,536 bytes by this API. No new unlimited
text or glyph cache was introduced.

New `glyph_face`, `advance_face`, and `kern_face` APIs use the selected exact
face for drawing and measuring. BursztynUI's immediate text painter and
`measure_text` both resolve whole clusters. Kerning across different faces is
suppressed. If no face covers the entire cluster, they explicitly fall back to
per-scalar selection; marks remain zero-advance with geometric placement.
Browser scalar callbacks remain compatible, but its independent word/fragment
layout has not yet been migrated to whole-cluster resolution. This is an
outstanding stage-1 acceptance item, not claimed as complete.

`PositionedGlyph` stores face, glyph ID, advance, offsets, original source byte
range, cluster start and direction metadata. BursztynUI's painter uses these
records transiently. This is not yet a retained shaped-run engine or a complete
logical-to-visual hit-testing map. Direction defaults to logical LTR; the RTL
enumerator is metadata only and does not enable bidirectional rendering.

### Accents and normalization

`MarkPen` now uses actual base-glyph ink bounds rather than only its advance.
Above/below/attached/overlay decisions use canonical combining classes from
UnicodeData 15.1.0. Above and below stacks retain bounded geometric positions;
attached marks use no extra gap. Overlay marks retain their font baseline
offset. Marks have zero advance, and default-ignorable scalars do not produce
independent boxes or advances in the native paths updated here.
This remains approximate geometry: no anchor-based GPOS or universal
pixel-equivalence between precomposed and decomposed accents is claimed.

NFC/NFD, canonical decomposition/composition and canonical reordering are **not
implemented**. Canonical combining class data is used for positioning, not
presented as normalization. Source storage and source byte offsets are
preserved. Mark placement tests do not imply complete normalization support.

## Stage 2 — Editors, fields and Terminal (gate not passed)

No app-specific Notepad storage/save/undo/clipboard logic or Terminal grid code
was changed. Shared native next/previous helpers inherit the new boundaries,
but that is not full Notepad or text-field acceptance. All caret/hit-test,
selection, truncation, browser forms and address-bar operations still require
an explicit audit and migration. Terminal still calls `BSF::monospace(true)`
and uses its existing bitmap cell renderer. Native TTF terminal support, width
properties and multilingual command editing remain unimplemented.

## Stage 3 — Larger fonts and security (gate not passed)

Existing limits are preserved: 2 MiB per font, four loaded faces (up to 8 MiB
file buffers), 32 examined TTF files, 8 KiB directory buffer, 16,384 glyphs,
64 tables, 4,096 points, 128 contours, 8,192 vertices, compound depth 8, and
1 MiB raster scratch. The existing 128-slot / 2 MiB alpha cache is retained.
The cluster resolver adds 16 fixed entries, not unbounded allocation. No
untrusted parsing moved into Ring 0; BWS/PZB/BZL and paging policies are unchanged.

This continuation does not claim >32-font discovery, >2-MiB font support,
allocation-failure coverage or a dedicated coverage-guided fuzzing campaign.
Existing 1,200 mutation cases remain a sanitizer smoke test. Full parser audit,
large-font resource policy and native install/removal acceptance remain open.

### Release asset selection

Additional untracked Alice/BPmono/Caviar font files and license documents were
present at task start. They were preserved unchanged. Packaging previously
blindly enumerated the source directory; notice filenames with spaces also
broke Make dependency parsing. `czcionki/release-fonts.txt` now selects the
seven previously audited DejaVu font/notice/source assets. Both packaging and
ISO verification use `narzedzia/bsf_release_assets.py`. Native runtime discovery
remains independent of this release manifest. The extra fonts are not removed
from the workspace and are not silently published without a completed license
review. The manifest lists release assets, not every font a user may install.

## Stage 4 — Tests and native acceptance

Commands:

```
python3 narzedzia/generate_bsf_unicode.py
make test-bsf-graphemes-host test-bsf-host test-bsf-ui-host
make test-husarz-engine-host
make -k -j4 iso
```

Conformance and host results are recorded below after final validation.
Native GUI testing is regression evidence for the production artifact, not
approval to bypass stage gates. No disk installer, disk-only reboot, live font
installation/removal, full resolution/DPI/theme matrix or controlled native
performance benchmark is claimed unless explicitly logged below.

## Stage 5 — OpenType and RTL (disabled)

No GSUB or GPOS lookup types are implemented in this continuation. Legacy kern
remains supported. No UAX 9 processing, Arabic/Indic shaping or RTL editing is
enabled. Later-stage work is deliberately not started before stage-1 through
stage-4 acceptance. CFF/CFF2, WOFF/WOFF2 and variable fonts remain unsupported.

## Validation results and artifacts

- `make test-bsf-graphemes-host`: **PASS**, all 1,187 official cases,
  next/previous/count/every-byte-offset checks, malformed input and 10,000
  deterministic mutation inputs. ASan/UBSan enabled.
- `make test-bsf-host test-bsf-ui-host`: **PASS**, 12,204 parser/cache checks
  and 8,013 UI/browser checks. Includes exact-face cluster fallback for
  `A + U+0301`, with the controlled primary covering A but not the mark.
- `make test-husarz-engine-host`: **PASS**, 127,432 checks with ASan/UBSan.
- `make test-desktop-ttf-host test-settings-themes-power-host`: **PASS**,
  187,840 desktop/font/layout and 2,696 Settings/Start power checks.
- `python3 narzedzia/test_release_metadata.py`: **PASS**, 10 gate tests.
- `python3 narzedzia/test_bur_loader.py`: **PASS**, 17 groups.
- `python3 narzedzia/verify_bsf_bur_layouts.py .../bur-layouts.json`:
  **PASS**, actual file/entry/VA/segment alignment bounds for 23 BUR v1 files.
- `make -B -j4 iso`: **PASS**, forced production rebuild, including the BSF
  asset gate and final metadata gate (616 inventory entries, zero findings).
  This is not a claim of hermetic or bit-for-bit reproducible builds.

[Font sanitizer log](bsf-unicode-phase2-2026-11-07/host-fonts.txt),
[official conformance and mutation log](bsf-unicode-phase2-2026-11-07/grapheme-conformance.txt),
[Husarz regression log](bsf-unicode-phase2-2026-11-07/host-husarz.txt),
[desktop/Settings regression log](bsf-unicode-phase2-2026-11-07/host-desktop.txt),
[metadata tests](bsf-unicode-phase2-2026-11-07/metadata-tests.txt),
[loader tests](bsf-unicode-phase2-2026-11-07/loader-tests.txt),
[BUR segment evidence](bsf-unicode-phase2-2026-11-07/bur-layouts.json),
[public release hashes/sections/font inventory](bsf-unicode-phase2-2026-11-07/release.json).

Toolchain: x86_64-linux-gnu-g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1),
GNU ld 2.42. Existing production optimization, stack protection and release
symbol separation are preserved. Full build output is locally available at
`/tmp/bsf-phase2-clean-build.log`.

Public kernel SHA-256:
`6141f3ef1eefa1979ee11880af4ce2c35e5cb7a63bcd7a158bc90dceedb2a109`.
ISO SHA-256:
`41d88c080d372c2de5d1cea35c769401e64509edc5ac54f6b22b57aa3e79ef04`.
Matching private developer ELF is archived under the public kernel hash in
`.release-private/`. Actual byte sizes and stripped-section evidence are in
`release.json`.

### Build failures resolved

An initial build failed because Make split newly present notice filenames at
spaces. Manifest-based audited release dependencies remove that failure while
preserving those source files. Subsequent link checks exposed fixed TEXT
reservation overflows. Eleven explicit header/linker pairs were adjusted using
observed usage, page rounding and one page of headroom; see
[measured code reservations](bsf-unicode-phase2-2026-11-07/code-capacities.json).
Two apps (HDA test and diagnostics) already generate their headers from actual
linker segment sizes; their TEXT ceilings were raised by 16 KiB for shared
Unicode code/data. Their 512 KiB file and existing DATA/BSS ceilings remain.
No heap limit was raised, segment checks were not removed and BUR v1 ABI was
not changed. The final forced rebuild passes all linker assertions.

### Source changes in this continuation

Core: `bsf/bsf.h`, `bsf/bsf.cpp`, `bsf/runtime.h`, `bsf/runtime.cpp`,
new `bsf/unicode.inc` and generated `bsf/unicode_data.inc`.
Integration: `bursztyn_ui/czcionka.h`, `programy/husarz/layout.h`,
`programy/husarz/paint.h`.
Tools/data: new `narzedzia/generate_bsf_unicode.py`,
`narzedzia/test_bsf_graphemes.cpp`, `narzedzia/verify_bsf_bur_layouts.py`,
`narzedzia/bsf_release_assets.py`, `narzedzia/unicode/` data/license/hash files,
`czcionki/release-fonts.txt`; modified `narzedzia/test_bsf_ui.cpp`,
`narzedzia/package_bsf.py`, `narzedzia/verify_bsf_iso.py`,
`narzedzia/package_release_notices.py` and `Makefile`.

Code-capacity/header changes: Explorer, Shell, test app, Kontrolki 3/4,
Settings, installer, display Settings app, Tapety, Husarz and window manager.
Corresponding source/linker paths are recorded in the capacity JSON and the
working-tree diff. HDA test and diagnostics change only linker TEXT ceilings;
their executable headers remain generated from the linker layout.

No coverage-guided fuzzer or full stage-3 font campaign was run; deterministic
mutations are explicitly not described as coverage-guided fuzzing. No new
sanitizer memory-safety finding was observed in the checks above. No native
performance improvement, RSS budget validation, process-isolation retest,
installed-disk success, full Unicode normalization or full shaping acceptance
is inferred from those results.

### Native production GUI result

Command:

```
python3 narzedzia/test_husarz_qemu.py --fonts-only --native-font-apps \
  --output .bsf-test/unicode-phase2-native --timeout 900
```

**PASS** on Linux QEMU/TCG/OVMF at 1280x720. Settings local preview remained
local; Apply published generation 2 and 24 px. Husarz, Notepad, Calculator and
Explorer repainted; Start was captured after the font update. The harness
reports no guest fault. Visual inspection confirms readable Polish, Russian,
Ukrainian and Greek samples and visible attached accents in the browser and
native Settings preview. These screenshots are not a GPOS-positioning oracle.

[Native verdict](bsf-unicode-phase2-2026-11-07/qemu-result.json),
[run log](bsf-unicode-phase2-2026-11-07/qemu-run.txt),
[serial log](bsf-unicode-phase2-2026-11-07/qemu-serial.txt),
[browser screenshot](bsf-unicode-phase2-2026-11-07/native-browser.png),
[Settings screenshot](bsf-unicode-phase2-2026-11-07/native-settings.png).
The QEMU JSON's `kernel_sha256` is the private unstripped observer ELF,
`c706f4f202f5e8b0b946f2e49085b5a1cb2144cfb1c7162815efc16ea6f3ea6f`;
the VM boots the production ISO containing the stripped public kernel above.

Skipped/incomplete: native forced cluster fallback using an installed custom
font, Notepad multilingual edit/save/reopen and clipboard workflows, Terminal
TTF cells, live font installation/removal, installed-disk boot/persistence,
all other resolutions/DPI/themes, Windows QEMU frontend and the full
performance/RSS/fuzz/security matrix. Later stages remain gated by incomplete
stage-1 integration. A no-fault GUI regression run is not complete editor,
installer, process-isolation or multilingual shaping acceptance.

Final additional cache regression: after replacing the controlled primary's
cmap at the same path/face identity, `rescan()` invalidates both a positive
cluster fallback and a cached negative supplementary-plane lookup. The
updated UI/browser sanitizer target passes **8,023 checks**; this supersedes
the earlier 8,013-count run above. See
[cluster invalidation evidence](bsf-unicode-phase2-2026-11-07/cluster-invalidation.txt).

Final artifact sizes: ISO **40,314,880 bytes**, public kernel **10,506,960 bytes**.
The increase from the recorded baseline is 186,368 and 172,032 bytes respectively;
these are size observations, not performance or security scores. The font mask
cache remains 2 MiB, loaded-file budget 8 MiB, and cluster cache adds 2,368
bytes of fixed entry storage (16 entries). Native process RSS and rendering
latency were not measured; no speedup is claimed.
