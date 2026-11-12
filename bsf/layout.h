#pragma once
// BSF 2.0 unified text layout. One authoritative pipeline for measuring,
// drawing, caret placement, hit testing, selection and wrapping:
//   UTF-8 -> scalars -> graphemes (UAX #29) -> scripts -> UAX #9 levels ->
//   font runs (shaping-run aware fallback) -> GSUB/GPOS -> line breaking ->
//   L1/L2 per line (glyph level) -> visual glyphs + visual clusters.
// Logical text is never modified. Coordinates are 16.16 fixed-point pixels
// accumulated without per-glyph rounding (no drift on long lines).
#include "shape.h"
namespace BSF {
constexpr size_t MaxLayoutBytes = 65536;
constexpr int64_t Fixed = 65536;
enum LayoutGlyphFlags : uint8_t {
  GlyphMark = 1,         // GDEF/Unicode mark (zero advance)
  GlyphGposMark = 2,     // placed by GPOS mark attachment (real anchors)
  GlyphFallbackMark = 4, // mark without GPOS data: renderer approximates
  GlyphMissing = 8,      // no face covers it: renderer draws bitmap fallback
  GlyphHidden = 16,      // default ignorable / paragraph separator
};
struct LayoutGlyph {
  uint32_t face{};        // BSF face identity (0 for bitmap fallback)
  uint16_t glyph{};
  uint8_t level{};        // resolved bidi level (after L1)
  uint8_t flags{};
  uint32_t codepoint{};   // first source scalar (bitmap fallback / marks)
  uint32_t cluster{};     // logical byte range [cluster, cluster_end)
  uint32_t cluster_end{};
  int64_t x{};            // pen position on the line, 16.16 px
  int32_t advance{};      // 16.16 px
  int32_t x_offset{}, y_offset{}; // 16.16 px, y up (font convention)
};
struct LayoutLine {
  uint32_t glyph_begin{}, glyph_end{};   // visual glyph range
  uint32_t cluster_begin{}, cluster_end{}; // VisualCluster range
  uint32_t byte_begin{}, byte_end{};     // logical bytes (incl. separator)
  int64_t width{};                       // 16.16, trailing spaces excluded
  int64_t x_shift{};                     // alignment offset (RTL right)
  uint8_t paragraph_level{};
};
// A visual caret cell: one grapheme (ligatures are split evenly).
struct VisualCluster {
  uint32_t begin{}, end{}; // logical bytes
  int64_t x0{}, x1{};      // 16.16, x0 <= x1, line-relative incl. x_shift
  bool rtl{};
};
struct FontResolver {
  void *user{};
  // Face able to render bytes [begin,end) as one shaping run, or 0.
  uint32_t (*run_face)(void *, const char *, size_t begin, size_t end){};
  // Face for one grapheme cluster, or 0 (bitmap fallback).
  uint32_t (*cluster_face)(void *, const char *, size_t begin, size_t end){};
  const Face *(*face)(void *, uint32_t id){};
  // Advance in 16.16 px for a scalar drawn by the bitmap fallback.
  int32_t (*fallback_advance)(void *, uint32_t cp, int px){};
};
struct LayoutOptions {
  int px{16};
  BaseDirection direction{BaseDirection::Auto};
  uint32_t language{};
  uint32_t features{FeatureDefault};
  int64_t max_width{};  // 16.16; 0 = no wrapping
  bool align_to_direction{}; // right-align RTL paragraphs within max_width
  // BSF 2.1 (all default to the BSF 2.0 behaviour):
  // CSS letter-spacing, 16.16 px, added after every shaped cluster (never
  // inside a grapheme or ligature); not applied to cursive (joining) scripts.
  int32_t letter_spacing{};
  // CSS word-spacing, 16.16 px, added to word-separator characters.
  int32_t word_spacing{};
  // Logical scalars just before/after the text (0 = none), e.g. the
  // neighbouring DOM text node; used only for cursive joining context.
  uint32_t context_before{}, context_after{};
};
struct LayoutStatistics {
  uint64_t runs{}, shaped_glyphs{}, fallback_searches{}, budget_failures{},
      gsub_runs{}, gpos_runs{}, legacy_kern_runs{}, longest_run{};
  // Union of OT::Statistics type masks over all runs (bit t = type t).
  uint32_t gsub_type_mask{}, gpos_type_mask{};
};
class TextLayout {
public:
  TextLayout() = default;
  ~TextLayout();
  TextLayout(const TextLayout &) = delete;
  TextLayout &operator=(const TextLayout &) = delete;
  bool build(const char *text, size_t bytes, const LayoutOptions &,
             const FontResolver &);
  void clear();
  // Results (valid until the next build/clear).
  const LayoutGlyph *glyphs() const { return glyphs_; }
  size_t glyph_count() const { return glyph_count_; }
  const LayoutLine *lines() const { return lines_; }
  size_t line_count() const { return line_count_; }
  const VisualCluster *clusters() const { return clusters_; }
  size_t cluster_count() const { return cluster_count_; }
  int64_t width() const { return width_; }
  size_t bytes() const { return bytes_; }
  const char *text() const { return text_; } // owned logical copy
  const LayoutStatistics &statistics() const { return stats_; }
  // Editing geometry. Offsets snap to grapheme boundaries.
  size_t line_of(size_t byte) const;
  int64_t caret_x(size_t byte, size_t *line = nullptr) const;
  size_t hit_test(int64_t x, size_t line = 0) const;
  size_t move_visual(size_t byte, int direction) const; // -1 left, +1 right
  size_t move_logical(size_t byte, int direction) const;
  // Selection as visual spans on each line: out[k] = {line, x0, x1}.
  struct Span { uint32_t line; int64_t x0, x1; };
  size_t selection(size_t a, size_t b, Span *out, size_t capacity) const;
  uint8_t base_level() const { return base_level_; }
  // ---- BSF 2.1 line-aware editing geometry ------------------------------
  // A byte at a soft-wrap boundary is both the end of line k and the start
  // of line k+1; these queries take the visual line explicitly so editors
  // can keep a caret affinity. Results are grapheme boundaries.
  int64_t caret_x_on(size_t byte, size_t line) const;
  // One strictly visual step on `line` (no logical fallback). Returns the
  // new byte and sets *moved; at the visual edge returns `byte`, moved=false.
  size_t move_visual_on(size_t byte, size_t line, int direction, bool *moved) const;
  // Leftmost (direction < 0) or rightmost (> 0) caret byte of a line.
  size_t line_edge(size_t line, int direction) const;
  // Monotonic build identity (never 0 for a built layout); lets callers
  // detect a borrowed layout that was rebuilt or evicted.
  uint32_t generation() const { return generation_; }

private:
  const char *text_{};
  size_t bytes_{};
  LayoutGlyph *glyphs_{};
  size_t glyph_count_{}, glyph_cap_{};
  LayoutLine *lines_{};
  size_t line_count_{}, line_cap_{};
  VisualCluster *clusters_{};
  size_t cluster_count_{}, cluster_cap_{};
  int64_t width_{};
  uint8_t base_level_{};
  uint32_t generation_{};
  LayoutStatistics stats_{};
  // Caret table built once per layout: every displayable caret position of
  // each line (sorted by byte) plus an x-ordered index, so caret_x,
  // move_visual and line_of are O(log k) instead of rescanning clusters.
  struct CaretStop { uint32_t byte, line; int64_t x; bool owned; };
  CaretStop *carets_{};
  uint32_t *caret_x_order_{};
  uint32_t *line_caret_begin_{}; // per line, range into carets_ (+1 entry)
  size_t caret_count_{};
  bool build_carets();
  size_t nearest_on_line(size_t li, int64_t x, int direction, size_t exclude, bool *found) const;
  int64_t caret_x_scan(size_t byte, size_t line) const;
  size_t floor_boundary(size_t byte, size_t line) const;
  bool grow_glyphs(size_t);
};
} // namespace BSF
