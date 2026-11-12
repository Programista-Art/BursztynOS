# BSF 2.2 — OpenType variable fonts

Code: `bsf/variations.inc` (axis model, ItemVariationStore, gvar, HVAR, MVAR),
`bsf/cff.inc` (CFF2 blend), `bsf/otl.cpp` (GPOS VariationIndex),
`bsf/bsf.cpp` (outline integration, variable advances).

## Axis model

`variation_axes()` / `named_instances()` read `fvar` (validated at open:
version 1, 20-byte axis records, instance size `4·axes+4` or `+6`,
min ≤ default ≤ max). Any axis tag is accepted (generic model; wght, wdth,
slnt, ital, opsz need no special casing); faces with more than 16 axes stay
usable as their default instance only. `set_variations(face, settings)`
takes user-space values: unknown tags ignored, values clamped to the axis
range, non-finite values rejected, axes not mentioned use their default.
Normalization `(v − def)/(def − min|max − def)` → F2Dot14 with
`floor(x + 0.5)`, then `avar` 1.0 segment maps (validated: per-axis maps,
monotonic; malformed `avar` is ignored, not trusted). `avar` 2.0
(variation store in avar) is **not supported** (its v1 part is used).

The instance becomes part of the face identity: `Face::id` is a hash of the
base id and the coordinates (the default instance keeps the base id), so
glyph-bitmap, colour and layout caches never mix instances. Coordinates are
quantized to F2Dot14, which bounds the number of distinct instances.

## Outlines

- **TrueType (`gvar`)**: shared and embedded peak tuples, intermediate
  regions, shared/private packed point numbers, packed deltas (zero, byte,
  word runs), IUP for untouched points of simple glyphs, composite
  component offsets as delta points, 4 phantom points. Deltas accumulate in
  float and are rounded once per point (`floor(x + 0.5)`).
  Limits: 64 tuples per glyph, 4,100 points, axis count must equal `fvar`.
- **CFF2**: `vsindex`/`blend` in charstrings and Private DICTs with the
  region scalars of the active VariationData at the instance (see
  `bsf2-2-cff.md`). CFF2 is a separate decoder path from CFF 1.0 (INDEX
  counts u32, no Name/String INDEX, mandatory FDArray, implicit end, no
  width, no `return`/`endchar`, deprecated arithmetic operators rejected).

## Metrics

- Advances: `HVAR` (DeltaSetIndexMap formats 0/1 or direct glyph index) when
  present; otherwise the `gvar` advance phantom point. Applied in
  `Face::advance_units`, i.e. in shaping, layout and measurement.
- Line metrics: `MVAR` `hasc`/`hdsc`/`hlgp` adjust ascender/descender/line gap
  of the instance. Other MVAR tags are not applied.
- `VVAR` (vertical) is not used (BSF has no vertical layout).

## OpenType layout variations

- GPOS ValueRecord and Anchor (format 3) **VariationIndex** device tables
  use the GDEF ItemVariationStore at the instance (HarfBuzz rounding:
  per device value, `floor(x + 0.5)`).
- **FeatureVariations** (GSUB/GPOS 1.1 condition sets, e.g. `rvrn`
  substitutions at heavy weights) are **not supported**: such fonts use
  their default feature set at every instance. None of the test fonts uses
  them; fonts that do may differ from HarfBuzz.

## Where instances are used

The API is complete in BSF (`set_variations`). The system catalog renders
variable fonts at their default instance; Settings does not expose axis
sliders, and Husarz does not map `font-weight`/`font-variation-settings`
to axes (web variable fonts render at their default instance). These are
documented limitations, not simulated support.

## Verification (host)

`make test-bsf22-variations-host` (43,107 checks): axes and named instances
equal to FreeType's `FT_Get_MM_Var` for Noto Sans Hebrew VF (wght, wdth),
Noto Naskh Arabic VF (wght) and Source Serif 4 Variable (CFF2, wght, opsz,
30 named instances); outlines at the default, every axis min/max, every
named instance and 4 random instances equal to FreeType at the same design
coordinates — 2,718 + 5,192 + 6,357 glyph outlines, 0 mismatches; ink at
48 px grows with weight (e.g. Source Serif 4: 20,993 → 103,043); instances
keep separate cache entries; clamping, NaN rejection, unknown tags; 3,000
gvar/fvar mutations under ASan/UBSan.
`make test-bsf22-shaping-host`: 51/51 HarfBuzz rows exact at 13 instances
(HVAR advances, GPOS kerning and mark anchors with VariationIndex deltas).
