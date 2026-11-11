#pragma once
// BSF 2.0 shaper: one script/direction/font run of scalars -> positioned
// glyphs (font units), HarfBuzz-compatible semantics for the implemented
// scripts. Output is in visual order (reversed for RTL), with clusters
// holding the logical byte offset of each source cluster.
#include "otl.h"
#include "text.h"
namespace BSF {
enum ShapeFeatures : uint32_t {
  FeatureLiga = 1, FeatureKern = 2, FeatureCalt = 4, FeatureClig = 8,
  FeatureDlig = 16, FeatureMarks = 32,
  FeatureAalt = 64, // opt-in: first alternate of every 'aalt' lookup (GSUB 1/3)
  FeatureDefault = FeatureLiga | FeatureKern | FeatureCalt | FeatureClig | FeatureMarks
};
struct ShapeOptions {
  uint8_t script{ScriptCommon};
  bool rtl{};
  uint32_t language{};               // OpenType LangSys tag, 0 = default
  uint32_t features{FeatureDefault};
  uint64_t budget{OT::DefaultBudget};
};
struct ShapeReport {
  bool gsub{}, gpos{}, gpos_marks{}, legacy_kern{}, budget_failed{},
      normalized{};
  OT::Statistics stats{};
};
enum class ShapeClass : uint8_t { Default, Arabic, Hebrew, Indic, Unsupported };
ShapeClass shape_class(uint8_t script);
// Shape cps[0..n) (clusters[k] = logical byte offset of scalar k, already
// merged to grapheme starts by the caller). n <= OT::MaxShapeGlyphs.
bool shape(const Face &, const OT::Layout &, const uint32_t *cps,
           const uint32_t *clusters, size_t n, const ShapeOptions &,
           OT::Buffer &out, ShapeReport *report = nullptr);
} // namespace BSF
