# BSF 2.2 — OpenType CFF (PostScript Type 2) outlines

Code: `bsf/cff.inc` (included by `bsf/bsf.cpp`), outline selection in
`Face::assemble`, cubic vertices in `Outline::cubic`, rasterization through
the shared stb rasterizer (`rasterize`). Ring 3 only.

## Detection

The outline technology is chosen from validated tables: `glyf`+`loca` in a
non-`OTTO` sfnt → TrueType; else `CFF2` → CFF2; else `CFF ` → CFF; else
colour strikes only → `Bitmap`; otherwise the face is rejected. The `.otf`
extension only decides the directory; an `.otf` with `glyf` outlines loads as
TrueType and vice versa. sfnt versions accepted: `0x00010000`, `true`, `OTTO`.

## Parser (CffFace::open)

| Structure | Handling |
|---|---|
| Header | major must be 1 (CFF) or 2 (CFF2); header size bounded by the table |
| INDEX | count (u16 / u32 for CFF2) ≤ 2^20, offSize 1..4, first offset 1, offsets monotonic and inside the table, validated once at open; items re-checked on access |
| Name INDEX | exactly one font (OpenType CFF) |
| Top DICT | CharStrings (count must equal `maxp.numGlyphs`), Private, charset, FontMatrix (bounded, non-degenerate), CharstringType 2, ROS (CID), FDArray, FDSelect, CFF2 vstore |
| DICT operands | ints (28/29/32..254), reals (nibbles, bounded exponent), 48 operands (CFF) / 513 (CFF2), 4,096 operators per DICT |
| Private DICT | Subrs (relative to the Private DICT), defaultWidthX/nominalWidthX, CFF2 vsindex/blend |
| FDArray / FDSelect | ≤ 256 FDs; FDSelect formats 0, 3 and 4 (CFF2) fully validated (ranges sorted, sentinel = glyph count, FD < count); every FD Private DICT validated at open |
| charset | formats 0/1/2, used only for `seac`-style endchar accents |
| CID-keyed fonts | per-glyph FD → local subrs (verified with the Noto Sans CJK collection) |

## Type 2 interpreter (CffMachine)

Operators: hstem/vstem/hstemhm/vstemhm, hintmask/cntrmask (mask bytes from the
stem count, implicit vstem), rmoveto/hmoveto/vmoveto, rlineto/hlineto/vlineto,
rrcurveto/rcurveline/rlinecurve/vvcurveto/hhcurveto/vhcurveto/hvcurveto,
flex/hflex/hflex1/flex1, callsubr/callgsubr (bias 107/1131/32768), return,
endchar (width parity, 4/5-argument `seac` accents via Standard Encoding and
the charset), the arithmetic/storage operators (and, or, not, abs, add, sub,
div, neg, eq, drop, put, get, ifelse, random = 0.5 deterministic, mul, sqrt,
dup, exch, index, roll), CFF2 vsindex and blend (region scalars from the
vstore at the face instance). Hints are parsed and ignored (unhinted
rendering, like the TrueType path).

Bounds: operand stack 48/513, subroutine depth 10, 200,000 operators per
glyph (shared by all nested subroutines), 384 stems, 1,024 contours, the
shared 8,192-vertex limit, coordinates within ±32,767/precision. Reserved
operators, stack underflow/overflow, out-of-range subroutines, recursion,
missing endchar (CFF 1.0) and truncated operands fail the glyph only; the
face stays usable and other glyphs render.

## Rasterization

Cubic segments are passed to stb's rasterizer as `vcubic` vertices and
flattened by adaptive subdivision (never by joining control points).
Coordinates are kept in 1/8 (upem ≤ 1,000), 1/4 or 1/2 font unit inside the
16-bit vertex format, with a whole-unit retry for oversized glyphs; TrueType
stays at 1 unit (bit-identical output to BSF 2.1). Winding is non-zero for
both technologies.

## Verification (host)

- `make test-bsf22-cff-host`: every glyph outline of Noto Sans Regular/Bold
  OTF, Noto Naskh Arabic OTF, Noto Sans Hebrew OTF and the CID-keyed Noto
  Sans CJK subset (16,395 glyphs incl. DejaVu TrueType) is identical to
  FreeType (`FT_LOAD_NO_SCALE`) within 1 font unit; mean anti-aliased
  coverage difference vs FreeType 1.8–4.0 % for CFF (DejaVu TrueType: 4.4 %,
  i.e. the rasterizer, not CFF, dominates); synthetic adversarial
  charstrings (recursion, mutual recursion, stack overflow, operation budget,
  reserved operators, truncation, bad INDEX offSize/header) and 3,000 random
  CFF-table mutations under ASan/UBSan.
- `make test-bsf22-shaping-host`: 44/44 HarfBuzz rows exact on CFF fonts
  (Latin with Polish, Cyrillic, Greek, ligatures, kerning, marks; Arabic
  Naskh joining/lam-alef/marks; Hebrew niqqud; CJK).
- `make test-bsf22-collections-host`: 2,704 sampled outlines of the full
  19.5 MB Noto Sans CJK collection (65,535 glyphs, 4 of 10 faces) equal to
  FreeType.
- Natively: see `bsf2-2-testreport.md` (CFF rasterizations counted in the
  Husarz/Settings processes).

## Limitations

- Hinting (stems/masks) is not applied.
- FDArray Font DICT `FontMatrix` (rare) is not composed with the top
  matrix.
- Type 1 / bare CFF files outside an sfnt are not supported.
