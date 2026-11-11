# BSF 2.0 — font formats

## Supported (unchanged parser, extended limits)

- sfnt with TrueType outlines (`0x00010000` / `true`), `glyf`/`loca`
  (short/long), compound glyphs (depth ≤ 8, cycle detection), cmap formats 4
  and 12 (platform 0 and 3/1, 3/10), `hmtx`, `hhea`, `OS/2`, `post`, `name`
  (UTF-16BE incl. surrogate pairs, strict validation), `kern` format 0.
- `MaxGlyphs` raised from 16,384 to **65,535** (the sfnt maximum) so large
  multilingual/CJK TrueType faces are not rejected for glyph count. File size
  limits (16 MiB per file, 16 MiB resident) and the 128-face catalog are
  unchanged; the expanded-catalog/large-file suite still passes.
- `GDEF`, `GSUB`, `GPOS` are now located and used (see
  `bsf2opentypegsubgpos.md`). `fvar`, `COLR`, `CPAL` byte ranges are recorded
  on `Face` but **not interpreted**.

## Not supported (no placeholder implementations exist)

| Format | Current behaviour | Required work |
|---|---|---|
| OpenType CFF (`OTTO`) | Rejected at discovery (no `glyf`), reported and skipped as before | Type 2 CharString interpreter (stack ≤ 48, subr depth ≤ 10, op budget), INDEX/DICT parsers, CID FDSelect, cubic outlines into the stb rasterizer (`vcubic`), metrics. |
| CFF2 | Rejected | CFF2 INDEX/DICT, `blend`/`vsindex` with ItemVariationStore. |
| TTC collections | Rejected (`ttcf` tag) | Collection header + face index selection; also needed for Noto CJK (which additionally exceeds 16 MiB and is CFF). |
| WOFF | Not loaded (Husarz has no web-font loading) | Bounded zlib inflate with per-table length checks, isolated in the browser process. |
| WOFF2 | Not loaded | Brotli decoder (external code: licence review needed), `glyf`/`loca` transform reconstruction, decompression-bomb limits. |
| Variable fonts | Default instance only for TrueType outlines (variation data ignored) | `fvar`/`avar` axis normalization, `gvar` deltas with IUP, `HVAR`/`MVAR`, cache keys including coordinates. |
| Colour (COLR/CPAL v0/v1, CBDT/CBLC, sbix) | Not rendered | Layered/painted glyph composition into colour bitmaps; the glyph cache is alpha-only today. |

## Why these were not implemented in this change

1. **Priority order** given for the work: audit → BiDi → GSUB/GPOS →
   integration → formats. The first four were completed with verified
   evidence; formats were reached last.
2. **Kernel image budget**: every format parser is linked into each
   full-engine app, all embedded in the 15 MiB kernel image (366 KiB
   headroom remains). A CFF interpreter (~10–15 KiB code) fits; WOFF2
   (Brotli) and COLRv1 do not without first changing how apps are packaged
   (see `bsf2architecture.md`).
3. Shipping untested parsers for untrusted input would be worse than
   rejecting the formats, which is the current, safe behaviour.

## Font discovery and location policy

Unchanged: `/czcionki/ttf/*.ttf` only (kernel policy `BSF::trusted_asset`),
validated filenames, sorted bounded directory scan, damaged fonts reported
and skipped, explicit rescan from Settings. A future `/czcionki/otf/`
directory would require extending `trusted_asset`; downloaded web fonts must
stay inside the browser process and never be installed system-wide.
