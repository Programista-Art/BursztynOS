#pragma once
// BSF 2.0 OpenType Layout: GDEF, GSUB (lookup types 1-8) and GPOS (1-9).
// Untrusted-input design: every offset is validated through Bytes, every
// loop is charged to a Budget, nesting is bounded and buffers have fixed
// capacity. A failure leaves the buffer valid (never partially freed).
#include "bsf.h"
namespace BSF {
namespace OT {
constexpr unsigned MaxShapeGlyphs = 4096; // glyphs per shaping call
constexpr unsigned MaxNesting = 6;        // nested contextual lookups (64 KiB Ring 3 stacks)
constexpr unsigned MaxContext = 64;       // glyphs in one context rule
constexpr unsigned MaxLookups = 1024;     // lookups per plan
constexpr uint64_t DefaultBudget = 4u << 20; // matching steps per run
constexpr uint32_t Tag(char a, char b, char c, char d) {
  return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
         (uint32_t(uint8_t(c)) << 8) | uint8_t(d);
}
enum GlyphProps : uint16_t {
  PropBase = 2, PropLigature = 4, PropMark = 8, // LookupFlag ignore bits
  PropSubstituted = 16, PropLigated = 32, PropMultiplied = 64,
  PropIgnorable = 128, // hidden default ignorable (context matching skips)
  // mark attachment class lives in bits 8..15, like LookupFlag
};
struct GlyphInfo {
  uint32_t codepoint{};
  uint32_t cluster{};     // logical byte offset of the source cluster
  uint32_t mask{};
  uint16_t glyph{};
  uint16_t props{};
  uint8_t lig_id{}, lig_comp{}, lig_count{}; // ligature component tracking
  uint8_t action{};       // shaper-private (Arabic joining form)
  uint8_t ccc{};          // canonical combining class
  uint8_t reserved{};
};
enum AttachType : uint8_t { AttachNone = 0, AttachMark = 1, AttachCursive = 2 };
struct GlyphPosition {
  int32_t x_advance{}, y_advance{}, x_offset{}, y_offset{}; // font units
  int32_t attach_chain{};  // relative index of the attachment parent
  uint8_t attach_type{};
};
struct Budget {
  uint64_t remaining{DefaultBudget};
  bool exhausted{};
  bool spend(uint64_t n = 1) {
    if (exhausted || remaining < n) {
      exhausted = true;
      remaining = 0;
      return false;
    }
    remaining -= n;
    return true;
  }
};
// Fixed-capacity glyph buffer in logical order. Owns its storage.
class Buffer {
public:
  GlyphInfo *info{};
  GlyphPosition *pos{};
  unsigned len{}, cap{};
  uint8_t next_lig_id{1};
  Buffer() = default;
  ~Buffer();
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  bool reserve(unsigned capacity);
  void clear() { len = 0; next_lig_id = 1; }
  bool push(const GlyphInfo &);
  // Replace one glyph by `count` glyphs (0 deletes). Charged to budget.
  bool splice(unsigned at, unsigned remove, const uint16_t *glyphs,
              unsigned count, Budget &);
  void reverse(unsigned begin, unsigned end);
};
struct Layout {
  Bytes gdef{}, gsub{}, gpos{};
  Bytes glyph_classes{}, mark_classes{}, mark_sets{}, var_store{};
  uint16_t glyph_count{};
  // BSF 2.2: variation instance of the face (VariationIndex device tables).
  int16_t coords[MaxAxes]{};
  uint8_t axes{};
  bool open(const Face &);
  bool has_gsub() const { return gsub.n != 0; }
  bool has_gpos() const { return gpos.n != 0; }
  // 0 none, 1 base, 2 ligature, 3 mark, 4 component (GDEF classes)
  unsigned glyph_class(unsigned glyph) const;
  unsigned mark_class(unsigned glyph) const;
  bool in_mark_set(unsigned set, unsigned glyph) const;
  uint16_t props(unsigned glyph, uint32_t codepoint) const;
};
struct FeatureRequest {
  uint32_t tag{};
  uint32_t mask{};   // glyph mask bits this feature applies to
  uint8_t stage{};   // features in a lower stage complete first
  bool gpos{};       // GPOS feature (otherwise GSUB)
};
struct LookupEntry {
  uint16_t index{};
  uint8_t stage{};
  uint32_t mask{};
};
struct Plan {
  LookupEntry gsub[MaxLookups]{};
  LookupEntry gpos[MaxLookups]{};
  unsigned gsub_count{}, gpos_count{};
  bool has_gpos_kern{}, has_gpos_mark{};
  uint32_t script{}, language{};
};
// Select script/language and resolve features to lookup lists.
// `scripts` is a preference list (e.g. {'dev2','deva'}), count <= 4.
bool build_plan(const Layout &, const uint32_t *scripts, unsigned script_count,
                uint32_t language, const FeatureRequest *features,
                unsigned feature_count, Plan &);
struct Statistics {
  uint64_t lookups_applied{}, substitutions{}, positionings{},
      budget_failures{}, malformed{};
  // Successful subtable applications by lookup type and subtable format
  // (index 0 = unknown format). Extension lookups (GSUB 7 / GPOS 9) are
  // counted under their own type *and* under the resolved type.
  uint32_t gsub_applied[9][4]{}, gpos_applied[10][4]{};
  // Bit t set = lookup type t applied at least once.
  uint32_t gsub_type_mask() const;
  uint32_t gpos_type_mask() const;
};
// Apply all planned GSUB lookups in stage/lookup order. Returns false if
// the budget was exhausted (buffer remains well-formed, partially shaped).
bool substitute(const Layout &, const Plan &, Buffer &, Budget &,
                Statistics *stats = nullptr, unsigned stage_from = 0,
                unsigned stage_to = 255);
// Apply GPOS lookups, zero GDEF mark advances and resolve attachments.
// rtl: logical order buffer of a right-to-left run.
bool position(const Layout &, const Plan &, Buffer &, Budget &, bool rtl,
              Statistics *stats = nullptr);
// Resolve attachment chains into offsets (after GPOS). Iterative.
void finish_offsets(Buffer &, bool rtl);
} // namespace OT
} // namespace BSF
