# BSF 2.2 architecture — font formats, collections, web fonts, variations, colour

Branch `feature/bsf-2-2-font-formats`, created from `feature/bsf-2-1-text-integration`
@ `58131c0` (the validated BSF 2.1 commit; BSF 2.1 is **not** merged into `test`,
whose head is `544a51e`). Nothing is committed. BSF 2.2 extends BSF 2.1; the
UAX #9 implementation, the GSUB/GPOS engine and the shared `TextLayout` are the
same code (only extended, see "Changes to shared code").

## Pipeline

```
font source ──► container decoder ──► validated face ──► outline provider ──► variation instance
 /czcionki/*      sfnt / TTC / OTC       Face::assemble     glyf │ CFF │ CFF2      gvar │ CFF2 blend
 Husarz HTTP      WOFF / WOFF2           (one validator)    bitmap strikes       HVAR, MVAR, GPOS deltas
                  (worker process)                                  │
                                                                    ▼
          shared shaping (bsf/shape.cpp + otl.cpp) ──► TextLayout ──► glyph renderer ──► caches ──► apps
                                                                      mask: stb rasterizer
                                                                      colour: COLRv0/v1, CBDT, sbix
```

| Stage | Code | Notes |
|---|---|---|
| Discovery | `bsf/runtime.cpp` (`init`, `examine`) | `/czcionki/{ttf,otf,ttc,otc}`; one catalog entry per usable face |
| Container | `bsf/bsf.cpp` (`collection_faces`, `face_directory`), `bsf/webfont.cpp` | sfnt/TTC/OTC directory validation via a bounded `FileReader`; WOFF/WOFF2 → plain sfnt |
| Validation | `Face::assemble` | one validator for every source (file, collection slice, web font) |
| Outlines | `outline()` (TrueType, + `gvar`), `bsf/cff.inc` (CFF 1.0, CFF2) | outline technology chosen from validated tables, never from the file name |
| Variations | `bsf/variations.inc` | fvar/avar axis model, ItemVariationStore, gvar, HVAR, MVAR; CFF2 blend; GPOS VariationIndex |
| Shaping | `bsf/shape.cpp`, `bsf/otl.cpp` (unchanged engine) | same for every outline format |
| Mask raster | `rasterize()` → stb rasterizer (vertices only) | quadratic and cubic segments; TrueType path bit-identical to BSF 2.1 |
| Colour | `bsf/color.inc`, `bsf/png.inc`, `bsf/inflate.inc` | COLRv0/CPAL, COLRv1 paint graphs, CBDT/CBLC, sbix |
| Caches | `Cache` (masks), colour cache (`runtime.cpp`), layout caches (2.1) | keys use face ids; instance and scoped ids are distinct and never reused |
| Web fonts | `programy/husarz/webfonts.h`, `webfont_runtime.inc`, `worker.inc` | browser-scoped faces (`register_scoped_face`) |

Container decoding never implies support: a WOFF2 face, a collection face or
an OTF is usable only after `Face::assemble` validated it and the outline
provider for its technology exists. A face whose tables validate but whose
outlines fail is marked `FontLoadFailed` and leaves fallback intact.

## Face model (bsf/bsf.h)

`Face` keeps its BSF 2.1 fields (layout of the first fields unchanged) and
appends: `outline` (`OutlineFormat::{TrueType, CFF, CFF2, Bitmap}`),
`cff_face` (parsed CFF/CFF2 state), collection index/count, variation
coordinates (`axis_count`, `coords[16]` normalized F2Dot14 after avar),
`color_tables`, `browser_scope`, `metadata_only` and the extra table views
(`avar gvar HVAR VVAR MVAR STAT CBDT CBLC sbix`). `Face::id` changes with the
variation instance (hash of the base id and the coordinates), so every
cache keyed by face id separates instances.

Open paths: `Face::open` (plain sfnt, = face 0), `Face::open_index` (face *n*
of an in-memory TTC/OTC), `Face::open_tables` (individually supplied tables:
lazy collection faces and catalog metadata).

## Changes to shared code (BSF 2.1 behaviour preserved)

| Area | Change | Why |
|---|---|---|
| `Face::assemble` | advance sanity limit 8 em → 32 em | Noto Naskh basmalah ligature is 12.2 em (legitimate); 16.16 arithmetic is safe up to ~255 em at 128 px |
| `otl.cpp` PairPos format 1 | device offsets relative to the PairSet (spec); was the subtable | no effect before (device tables were never applied); required for VariationIndex |
| `otl.cpp` input matching | default ignorables are "skip maybe" (HarfBuzz semantics) | emoji ZWJ sequences |
| `otl.cpp` anchors | 26.6 fixed point internally, one rounding per offset | identical results for static fonts (120/120 rows), exact for variable fonts |
| `shape.cpp` | hidden ignorables are deleted when the font has no space glyph (HarfBuzz) | emoji fonts |
| `needs_complex_layout` | true for U+1F000.., ZWJ, keycap | emoji go through the shaped layout (GSUB ligatures, colour) |
| `runtime.cpp` resolver | emoji presentation preference (VS16 → colour face, VS15 → monochrome) | emoji fallback |
| draw API | `draw_layout_line_color` (+ colour callback); old functions unchanged | colour glyphs |
| Config | v2 record (+ Terminal font), kernel migrates v1 files | separate Terminal font |

All BSF 2.0/2.1 suites pass unchanged except where a test asserted the old
contract (documented in `bsf2-2-testreport.md`).

## Kernel (Ring 0) changes — no font parsing

- `bsf/asset_path.h`: four trusted font directories, each with exactly one
  extension; traversal and wrong extensions are rejected (shared with Ring 3).
- `jadro/bws/syscalls.cpp` BWS 59: GUI-only offset reads of trusted font
  paths bounded by `MaxFontFile` (64 MiB) instead of `MaxFile` (16 MiB).
- `bsf/bootstrap.inc`: boot modules `bsf/<name>` routed to the directory of
  their extension; bytes copied verbatim, budgets unchanged.
- `jadro/fs/psf_bootstrap.h`: `/czcionki/{otf,ttc,otc}` created at boot.
- `bsf/config_kernel.inc`: fixed-size Config v2 transport, v1 migration.

## Kernel image

| Build | `__kernel_end` | Headroom to 15 MiB |
|---|---|---|
| BSF 2.1 baseline `58131c0` (built in a worktree) | `0xD39000` | 1,820 KiB |
| BSF 2.2 final | `0xE9F000` | 388 KiB |

Growth: `bsf_core.o` text 24.8 KB → 81.1 KB (CFF/CFF2, gvar, colour, PNG,
inflate), `bsf_runtime.o` 15.6 → 24.6 KB, linked into ~20 embedded apps;
the Husarz resource worker gained the WOFF/WOFF2 decoder and Brotli
(+190 KB, only there). The 15 MiB limit is unchanged. The remaining headroom
is small: further shared-library growth needs the per-app duplication
addressed (see recommendations in the test report).
