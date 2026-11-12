# BSF 2.2 — security and memory safety

Every font is hostile input, local or downloaded. All new parsing and
decoding runs in Ring 3; the kernel only validates paths, sizes and the
fixed-size Config record (no table parsing in Ring 0). Ring 0/3 separation,
NX, W^X, SMEP, SMAP, Ring 3 ASLR, stack protection, BWS syscall validation,
executable verification and the 15 MiB kernel-image limit are unchanged.

## Parser rules (all new code)

- All reads go through `Bytes` (offset/length checked, `has()` before
  access) or a bounded `FileReader`; table offsets/lengths are checked with
  64-bit arithmetic against the containing buffer.
- Counts read from files are capped before use (see limits) and products
  are computed in `size_t`/`uint64_t` before allocation.
- No recursion on attacker-controlled depth without a bound: CFF
  subroutines (10), TrueType composites (8, existing), COLR paint graph (32),
  PaintColrGlyph (cycle detection + 64), sbix `dupe` (1).
- Work budgets: CFF 200,000 operators/glyph, COLR 8,192 paints/glyph,
  inflate bounded by the fixed output size, Brotli by the exact output
  buffer, shaping budget (existing).
- Allocations are bounded before they happen (WOFF pre-pass, WOFF2 ratio
  check, PNG dimensions, gvar points, canvases).
- Failure is local: a malformed glyph fails that glyph; a face failing
  validation is rejected or marked `FontLoadFailed`; fallback stays.
- Floating point: no libm; NaN/Inf from fonts cannot reach indices
  (coordinates are range-checked before conversion to integers).

## Limits

| Resource | Limit | Justification |
|---|---|---|
| font file (read) | 64 MiB (`MaxFontFile`) | largest realistic CJK collection 26.3 MB |
| one table / one allocation | 16 MiB (`MaxFile`) | Ring 3 allocation limit (BWS 35); Noto CJK CFF is 15.4 MB |
| resident font data per process | 40 MiB | one CJK face (16.2 MB) + system fonts |
| tables per face / faces per collection | 64 / 256 | real fonts use < 30 tables, CJK collections 10 faces |
| catalog | 128 faces | existing |
| glyphs | 65,535 | sfnt limit |
| outline vertices / points / contours | 8,192 / 4,096 / 128 (TT), 1,024 contours (CFF) | existing raster bound |
| CFF stack / subr depth / ops / stems | 48 (513 CFF2) / 10 / 200,000 / 384 | Type 2 spec limits + budget |
| CFF INDEX count / FDs | 2^20 / 256 | |
| variation axes / regions / gvar tuples / points | 16 / 64 / 64 / 4,100 | |
| WOFF/WOFF2 output / ratio / tables | 16 MiB (Husarz 1.79 MB) / 128 / 64 | |
| Brotli allocator | 40 MiB | 16 MiB window + tables |
| PNG | 1,024 px side, 512×512 px, 8 MiB IDAT | colour strikes are ≤ 160 px |
| colour bitmap | 128×128 | existing glyph bitmap bound |
| COLR | depth 32, 8,192 paints, 4,096 layers, 2,048 glyph refs, 8 canvases, 64 stops | |
| scoped (web) faces | 32 faces, 24 MiB per browser process | |
| colour cache | 16 entries × 64 KiB | allocated on first colour glyph |

## Cache ownership

- Glyph and colour caches are keyed by face id, glyph id, px, style /
  foreground colour. Ids: catalog `FNV(path[#index])`; variation instances
  hash the coordinates (different id per instance); browser-scoped faces
  `0x80000000|serial`, never reused within the process. A stale key can
  therefore only miss.
- Callers never hold `Face*` across events (ids only); `rescan()` and
  `release_scope()` free face data and invalidate resolution and shared
  layout caches; owners of layouts compare `generation()` (catalog/config)
  and Husarz rebuilds styles when a font scope changes.
- Collection tables are reference-counted buffers; eviction releases
  references; no table outlives its last face.
- Browser fonts are owned by the tab's `BrowserFonts` set and released on
  navigation, clearing and closing; they never enter the system catalog or
  the filesystem.

## Third-party code

Brotli decoder v1.1.0 (MIT, pinned by tarball SHA-256, unmodified, decoder
only, Husarz worker only). stb_truetype rasterizer (existing). No other
runtime dependency; FreeType, HarfBuzz, fontTools, Skia/blackrenderer and
Pillow are host test tools only.

## Fuzzing and sanitizers

All BSF 2.2 host tests run with ASan + UBSan (`halt_on_error=1`,
`detect_leaks=1`). Coverage-guided fuzzing (`make fuzz-bsf22`, trace-pc
engine `testy/fuzz/fuzz_engine.h`): fonts (sfnt/TTC/OTC, CFF/CFF2, gvar,
COLR/CBDT/sbix), WOFF/WOFF2 and PNG. Iteration counts, edges and corpus
sizes are in `bsf2-2-testreport.md`. These runs are short; they are not a
proof of the absence of bugs in the decoders.

## Known residual risks

- Large web fonts (> 240 KiB) are rejected instead of streamed.
- The CORS check is response-only (no `Origin` request header).
- Fonts from `/czcionki` are trusted for availability (any GUI process may
  read them) but not for content (fully validated).
- Colour rendering and CFF interpretation run in every GUI process that
  draws such glyphs (Ring 3, per process), not in a separate sandbox.
