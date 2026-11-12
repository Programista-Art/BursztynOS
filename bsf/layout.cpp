// BSF 2.0 unified text layout engine (see layout.h for the pipeline).
#include "layout.h"
#include <new>
#ifdef BSF_NATIVE
#define BSF_NEW new
#else
#define BSF_NEW new (std::nothrow)
#endif
namespace BSF {
namespace {
template <class T> struct Array {
  T *p{};
  size_t n{}, cap{};
  ~Array() { delete[] p; }
  bool reserve(size_t c) {
    if (c <= cap)
      return true;
    T *q = BSF_NEW T[c];
    if (!q)
      return false;
    for (size_t i = 0; i < n; ++i)
      q[i] = p[i];
    delete[] p;
    p = q;
    cap = c;
    return true;
  }
  bool push(const T &v) {
    if (n == cap && !reserve(cap ? cap * 2 : 64))
      return false;
    p[n++] = v;
    return true;
  }
  T &operator[](size_t i) { return p[i]; }
  const T &operator[](size_t i) const { return p[i]; }
};
int64_t scale(int64_t units, int px, unsigned upem) {
  if (!upem)
    return 0;
  int64_t v = units * px * Fixed;
  return v >= 0 ? (v + upem / 2) / upem : -((-v + upem / 2) / upem);
}
bool is_space_lb(LineBreak lb) { return lb == LineBreak::SP; }
bool cjk_lb(LineBreak lb) {
  return lb == LineBreak::ID || lb == LineBreak::H2 || lb == LineBreak::H3 ||
         lb == LineBreak::JL || lb == LineBreak::JV || lb == LineBreak::JT ||
         lb == LineBreak::CJ || lb == LineBreak::EB || lb == LineBreak::EM;
}
bool no_break_before(LineBreak lb) {
  return lb == LineBreak::CL || lb == LineBreak::CP || lb == LineBreak::EX ||
         lb == LineBreak::IS || lb == LineBreak::SY || lb == LineBreak::NS ||
         lb == LineBreak::GL || lb == LineBreak::WJ || lb == LineBreak::ZWJ ||
         lb == LineBreak::CM;
}
// Simplified UAX #14 opportunity between two graphemes (documented subset).
bool break_between(LineBreak before, LineBreak after) {
  if (before == LineBreak::ZW)
    return true;
  if (before == LineBreak::GL || before == LineBreak::WJ || before == LineBreak::OP ||
      no_break_before(after) || is_space_lb(after))
    return false;
  if (is_space_lb(before))
    return true;
  if (before == LineBreak::HY || before == LineBreak::BA)
    return after != LineBreak::NU;
  if (cjk_lb(before) || cjk_lb(after))
    return true;
  if (after == LineBreak::BB)
    return true;
  return false;
}
uint8_t level_or(const uint8_t *levels, size_t i, uint8_t fallback) {
  return levels[i] == BidiRemoved ? fallback : levels[i];
}
// CSS Text 3 word-separator characters (word-spacing applies to these).
bool word_separator(uint32_t cp) {
  return cp == 0x20 || cp == 0xA0 || cp == 0x1361 || cp == 0x10100 ||
         cp == 0x10101 || cp == 0x1039F || cp == 0x1091F;
}
uint32_t layout_generation_counter;
} // namespace

TextLayout::~TextLayout() { clear(); }
void TextLayout::clear() {
  delete[] text_;
  delete[] glyphs_;
  delete[] lines_;
  delete[] clusters_;
  delete[] carets_;
  delete[] caret_x_order_;
  delete[] line_caret_begin_;
  carets_ = nullptr;
  caret_x_order_ = nullptr;
  line_caret_begin_ = nullptr;
  caret_count_ = 0;
  text_ = nullptr;
  glyphs_ = nullptr;
  lines_ = nullptr;
  clusters_ = nullptr;
  bytes_ = glyph_count_ = glyph_cap_ = line_count_ = line_cap_ = 0;
  cluster_count_ = cluster_cap_ = 0;
  width_ = 0;
  base_level_ = 0;
  generation_ = 0;
  stats_ = LayoutStatistics{};
}

bool TextLayout::build(const char *text, size_t bytes, const LayoutOptions &o,
                       const FontResolver &fr) {
  clear();
  if ((!text && bytes) || bytes > MaxLayoutBytes || o.px < 1 || o.px > 4096 ||
      !fr.face)
    return false;
  char *copy = BSF_NEW char[bytes + 1];
  if (!copy)
    return false;
  for (size_t i = 0; i < bytes; ++i)
    copy[i] = text[i];
  copy[bytes] = 0;
  text_ = copy;
  bytes_ = bytes;
  BidiText bt;
  if (!bt.analyze(text_, bytes, o.direction)) {
    clear();
    return false;
  }
  base_level_ = bt.base_level;
  const size_t n = bt.n;
  // --- graphemes (byte starts) and per-scalar grapheme starts ------------
  Array<uint32_t> gstart;  // grapheme start bytes (+ sentinel)
  Array<uint32_t> scalar_grapheme; // scalar -> grapheme index
  if (!gstart.reserve(n + 2) || !scalar_grapheme.reserve(n + 1)) {
    clear();
    return false;
  }
  for (size_t at = 0; at < bytes; at = next_cluster(text_, at, bytes))
    gstart.push(uint32_t(at));
  gstart.push(uint32_t(bytes));
  const size_t G = gstart.n - 1;
  for (size_t s = 0, g = 0; s < n; ++s) {
    while (g + 1 < gstart.n && gstart[g + 1] <= bt.offset[s])
      ++g;
    scalar_grapheme.push(uint32_t(g));
  }
  auto grapheme_of_byte = [&](uint32_t byte) {
    size_t lo = 0, hi = G;
    while (lo < hi) {
      size_t m = lo + (hi - lo + 1) / 2;
      if (gstart[m] <= byte)
        lo = m;
      else
        hi = m - 1;
    }
    return lo;
  };
  // --- paragraph levels per scalar ---------------------------------------
  Array<uint8_t> para;
  if (!para.reserve(n + 1)) {
    clear();
    return false;
  }
  for (size_t p = 0; p < n;) {
    size_t e = p;
    while (e < n && bt.cls[e] != uint8_t(BidiClass::B))
      ++e;
    uint8_t lv = o.direction == BaseDirection::Auto
                     ? resolve_paragraph_level(bt.cls + p, e - p, 0)
                     : uint8_t(o.direction == BaseDirection::RTL);
    if (e < n)
      ++e;
    for (size_t k = p; k < e; ++k)
      para.push(lv);
    p = e;
  }
  // --- scripts with Common/Inherited resolution --------------------------
  Array<uint8_t> scr;
  if (!scr.reserve(n + 1)) {
    clear();
    return false;
  }
  {
    uint8_t current = ScriptCommon;
    for (size_t s = 0; s < n; ++s) {
      uint8_t v = script(bt.cp[s]);
      if (v != ScriptCommon && v != ScriptInherited && v != ScriptUnknown)
        current = v;
      scr.push(v);
    }
    // Leading commons adopt the first real script; others the previous.
    uint8_t first = ScriptCommon;
    for (size_t s = 0; s < n && first == ScriptCommon; ++s)
      if (scr[s] != ScriptCommon && scr[s] != ScriptInherited && scr[s] != ScriptUnknown)
        first = scr[s];
    current = first;
    for (size_t s = 0; s < n; ++s) {
      uint8_t v = scr[s];
      if (v == ScriptCommon || v == ScriptInherited || v == ScriptUnknown)
        scr[s] = current;
      else
        current = v;
    }
    (void)current;
  }
  // --- itemize runs at grapheme boundaries and shape ---------------------
  Array<LayoutGlyph> logical;
  if (!logical.reserve(n + 16)) {
    clear();
    return false;
  }
  OT::Buffer buf;
  Array<uint32_t> cps, clusters;
  if (!cps.reserve(OT::MaxShapeGlyphs) || !clusters.reserve(OT::MaxShapeGlyphs)) {
    clear();
    return false;
  }
  auto glyph_level = [&](size_t s) { return level_or(bt.level, s, para[s]); };
  // CSS spacing on glyphs [first, logical.n) of one run, in logical order:
  // letter-spacing after the last glyph of each cluster (so never inside a
  // grapheme or ligature, and skipped for cursive scripts), word-spacing on
  // word separators. Both default to 0 (BSF 2.0 behaviour).
  auto apply_spacing = [&](size_t first, bool cursive) {
    if (!o.letter_spacing && !o.word_spacing)
      return;
    for (size_t i = first; i < logical.n; ++i) {
      LayoutGlyph &g = logical[i];
      if (g.flags & GlyphHidden)
        continue;
      int64_t add = 0;
      if (o.word_spacing && word_separator(g.codepoint))
        add += o.word_spacing;
      bool last = i + 1 == logical.n || logical[i + 1].cluster != g.cluster;
      if (o.letter_spacing && !cursive && last)
        add += o.letter_spacing;
      int64_t v = int64_t(g.advance) + add;
      if (v < -int64_t(INT32_MAX)) v = -int64_t(INT32_MAX);
      if (v > INT32_MAX) v = INT32_MAX;
      g.advance = int32_t(v);
    }
  };
  auto emit_missing = [&](size_t s0, size_t s1, uint8_t level) {
    size_t first = logical.n;
    for (size_t s = s0; s < s1; ++s) {
      LayoutGlyph g{};
      g.codepoint = bt.cp[s];
      g.cluster = gstart[scalar_grapheme[s]];
      g.level = level;
      bool mark = combining(bt.cp[s]);
      bool hidden = default_ignorable(bt.cp[s]);
      g.flags = uint8_t(GlyphMissing | (mark ? GlyphMark | GlyphFallbackMark : 0) |
                        (hidden ? GlyphHidden : 0));
      g.advance = (mark || hidden || !fr.fallback_advance)
                      ? 0
                      : fr.fallback_advance(fr.user, bt.cp[s], o.px);
      if (!logical.push(g))
        return false;
    }
    apply_spacing(first, false);
    return true;
  };
  auto shape_run = [&](size_t s0, size_t s1, uint32_t face_id, uint8_t level,
                       uint8_t script_index) {
    const Face *face = face_id ? fr.face(fr.user, face_id) : nullptr;
    if (!face)
      return emit_missing(s0, s1, level);
    cps.n = clusters.n = 0;
    for (size_t s = s0; s < s1; ++s) {
      cps.push(bt.cp[s]);
      clusters.push(gstart[scalar_grapheme[s]]);
    }
    OT::Layout L;
    L.open(*face);
    ShapeOptions so;
    so.script = script_index;
    so.rtl = level & 1;
    so.language = o.language;
    so.features = o.features;
    so.context_before = s0 ? bt.cp[s0 - 1] : o.context_before;
    so.context_after = s1 < n ? bt.cp[s1] : o.context_after;
    ShapeReport rep;
    bool ok = shape(*face, L, cps.p, clusters.p, cps.n, so, buf, &rep);
    ++stats_.runs;
    stats_.shaped_glyphs += buf.len;
    if (s1 - s0 > stats_.longest_run)
      stats_.longest_run = s1 - s0;
    stats_.gsub_runs += rep.gsub;
    stats_.gpos_runs += rep.gpos;
    stats_.legacy_kern_runs += rep.legacy_kern;
    stats_.gsub_type_mask |= rep.stats.gsub_type_mask();
    stats_.gpos_type_mask |= rep.stats.gpos_type_mask();
    if (!ok) {
      ++stats_.budget_failures;
      if (!buf.len)
        return emit_missing(s0, s1, level);
    }
    // Store in logical order (shaper emits visual order for RTL).
    size_t first = logical.n;
    for (unsigned k = 0; k < buf.len; ++k) {
      unsigned i = so.rtl ? buf.len - 1 - k : k;
      const OT::GlyphInfo &gi = buf.info[i];
      const OT::GlyphPosition &gp = buf.pos[i];
      LayoutGlyph g{};
      g.face = face_id;
      g.glyph = gi.glyph;
      g.level = level;
      g.codepoint = gi.codepoint;
      g.cluster = gi.cluster;
      bool mark = gi.props & OT::PropMark;
      g.flags = uint8_t((mark ? GlyphMark : 0) |
                        (mark ? (gp.attach_type == OT::AttachMark ? GlyphGposMark
                                                                  : GlyphFallbackMark)
                              : 0) |
                        ((gi.props & OT::PropIgnorable) ? GlyphHidden : 0));
      g.advance = int32_t(scale(gp.x_advance, o.px, face->units));
      g.x_offset = int32_t(scale(gp.x_offset, o.px, face->units));
      g.y_offset = int32_t(scale(gp.y_offset, o.px, face->units));
      if (!logical.push(g))
        return false;
    }
    apply_spacing(first, shape_class(script_index) == ShapeClass::Arabic);
    return true;
  };
  const size_t run_limit = OT::MaxShapeGlyphs / 2;
  // First scalar of each grapheme (+ sentinel n).
  Array<uint32_t> gscalar;
  if (!gscalar.reserve(G + 1)) {
    clear();
    return false;
  }
  for (size_t g = 0; g < G; ++g)
    gscalar.push(uint32_t(bt.scalar_at(gstart[g])));
  gscalar.push(uint32_t(n));
  auto separator_g = [&](size_t g) {
    return bt.cls[gscalar[g + 1] - 1] == uint8_t(BidiClass::B);
  };
  for (size_t g = 0; g < G;) {
    if (separator_g(g)) {
      ++g; // paragraph separators produce no glyph
      continue;
    }
    size_t sa = gscalar[g];
    uint8_t level = glyph_level(sa), sc = scr[sa];
    size_t h = g + 1;
    while (h < G && !separator_g(h) && glyph_level(gscalar[h]) == level &&
           scr[gscalar[h]] == sc && gscalar[h + 1] - sa <= run_limit)
      ++h;
    size_t run_end = gscalar[h];
    size_t b0 = bt.offset[sa], b1 = bt.offset[run_end];
    uint32_t whole = fr.run_face ? fr.run_face(fr.user, text_, b0, b1) : 0;
    ++stats_.fallback_searches;
    if (whole) {
      if (!shape_run(sa, run_end, whole, level, sc)) {
        clear();
        return false;
      }
    } else {
      // Per-grapheme fallback; merge neighbours resolved to the same face.
      for (size_t x = g; x < h;) {
        uint32_t id = fr.cluster_face ? fr.cluster_face(fr.user, text_, gstart[x], gstart[x + 1]) : 0;
        size_t z = x + 1;
        while (z < h && (fr.cluster_face ? fr.cluster_face(fr.user, text_, gstart[z], gstart[z + 1]) : 0) == id)
          ++z;
        if (!shape_run(gscalar[x], gscalar[z], id, level, sc)) {
          clear();
          return false;
        }
        x = z;
      }
    }
    g = h;
  }
  // --- cluster ends (logical order is cluster-monotonic) -----------------
  for (size_t i = 0; i < logical.n; ++i) {
    uint32_t c = logical[i].cluster, end = uint32_t(bytes);
    for (size_t j = i + 1; j < logical.n; ++j)
      if (logical[j].cluster > c) {
        end = logical[j].cluster;
        break;
      }
    // Never extend across a paragraph separator or into following graphemes
    // without glyphs beyond the run.
    size_t gi = grapheme_of_byte(c);
    for (size_t g = gi + 1; g < G && gstart[g] < end; ++g) {
      size_t sidx = bt.scalar_at(gstart[g]);
      if (bt.cls[sidx] == uint8_t(BidiClass::B)) {
        end = gstart[g];
        break;
      }
    }
    logical[i].cluster_end = end;
    // Fast path for consecutive glyphs of one cluster.
    while (i + 1 < logical.n && logical[i + 1].cluster == c) {
      logical[i + 1].cluster_end = end;
      ++i;
    }
  }
  // --- grapheme widths for line breaking --------------------------------
  Array<int64_t> gw;
  if (!gw.reserve(G + 1)) {
    clear();
    return false;
  }
  for (size_t g = 0; g < G; ++g)
    gw.push(0);
  for (size_t i = 0; i < logical.n; ++i) {
    const LayoutGlyph &g = logical[i];
    size_t a = grapheme_of_byte(g.cluster), b = grapheme_of_byte(g.cluster_end ? g.cluster_end - 1 : 0);
    if (b < a)
      b = a;
    size_t k = b - a + 1;
    for (size_t x = a; x <= b && x < G; ++x)
      gw[x] += g.advance / int64_t(k);
  }
  // --- line breaking ------------------------------------------------------
  struct Break { size_t g0, g1; };
  Array<Break> breaks;
  {
    size_t line_start = 0, last_break = 0;
    int64_t width = 0;
    auto lb_first = [&](size_t g) { return line_break(bt.cp[bt.scalar_at(gstart[g])]); };
    auto lb_last = [&](size_t g) { return line_break(bt.cp[bt.scalar_at(gstart[g + 1] - 1)]); };
    auto separator = [&](size_t g) {
      return bt.cls[bt.scalar_at(gstart[g + 1] - 1)] == uint8_t(BidiClass::B);
    };
    for (size_t g = 0; g < G; ++g) {
      if (o.max_width > 0 && g > line_start) {
        if (break_between(lb_last(g - 1), lb_first(g)))
          last_break = g;
        bool space = is_space_lb(lb_first(g));
        if (!space && width + gw[g] > o.max_width) {
          size_t brk = last_break > line_start ? last_break : g;
          breaks.push({line_start, brk});
          line_start = brk;
          width = 0;
          for (size_t k = brk; k < g; ++k)
            width += gw[k];
          last_break = line_start;
        }
      }
      width += gw[g];
      if (separator(g)) {
        breaks.push({line_start, g + 1});
        line_start = g + 1;
        last_break = line_start;
        width = 0;
      }
    }
    if (line_start < G || !breaks.n || (G && separator(G - 1)))
      breaks.push({line_start, G});
  }
  // --- per line: L1 levels, glyph-level L2, positions, visual clusters ---
  line_cap_ = breaks.n;
  lines_ = BSF_NEW LayoutLine[line_cap_ ? line_cap_ : 1];
  glyph_cap_ = logical.n;
  glyphs_ = BSF_NEW LayoutGlyph[glyph_cap_ ? glyph_cap_ : 1];
  Array<VisualCluster> vc;
  Array<uint8_t> line_levels, glevels;
  Array<uint32_t> order;
  if (!lines_ || !glyphs_ || !line_levels.reserve(n + 1) ||
      !glevels.reserve(logical.n + 1) || !order.reserve(logical.n + 1) ||
      !vc.reserve(logical.n + 1)) {
    clear();
    return false;
  }
  size_t lg = 0; // cursor into logical glyphs
  for (size_t li = 0; li < breaks.n; ++li) {
    size_t g0 = breaks[li].g0, g1 = breaks[li].g1;
    uint32_t b0 = gstart[g0], b1 = gstart[g1];
    size_t s0 = b0 < bytes ? bt.scalar_at(b0) : n, s1 = b1 < bytes ? bt.scalar_at(b1) : n;
    uint8_t pl = s0 < n ? para[s0] : (n ? para[n - 1] : uint8_t(o.direction == BaseDirection::RTL));
    if (s0 >= n && n && bt.cls[n - 1] == uint8_t(BidiClass::B))
      pl = o.direction == BaseDirection::RTL ? 1 : 0; // empty final paragraph
    line_levels.n = 0;
    for (size_t s = s0; s < s1; ++s)
      line_levels.push(0);
    if (s1 > s0) {
      bidi_line_levels(bt.cls, bt.level, s0, s1, pl, line_levels.p);
      bidi_fill_removed(line_levels.p, s1 - s0, pl);
    }
    LayoutLine &L = lines_[li];
    L = LayoutLine{};
    L.byte_begin = b0;
    L.byte_end = b1;
    L.paragraph_level = pl;
    L.glyph_begin = uint32_t(glyph_count_);
    size_t first = lg;
    while (lg < logical.n && logical[lg].cluster < b1)
      ++lg;
    size_t count = lg - first;
    glevels.n = 0;
    for (size_t k = 0; k < count; ++k) {
      size_t s = bt.scalar_at(logical[first + k].cluster);
      uint8_t lv = (s >= s0 && s < s1) ? line_levels[s - s0] : pl;
      logical[first + k].level = lv;
      glevels.push(lv);
    }
    size_t m = bidi_reorder(glevels.p, count, order.p);
    int64_t pen = 0;
    for (size_t k = 0; k < m; ++k) {
      LayoutGlyph g = logical[first + order[k]];
      g.x = pen;
      pen += g.advance;
      glyphs_[glyph_count_++] = g;
    }
    L.glyph_end = uint32_t(glyph_count_);
    L.width = pen;
    if (o.align_to_direction && (pl & 1) && o.max_width > pen)
      L.x_shift = o.max_width - pen;
    for (size_t k = L.glyph_begin; k < L.glyph_end; ++k)
      glyphs_[k].x += L.x_shift;
    if (pen > width_)
      width_ = pen;
    // Visual clusters.
    L.cluster_begin = uint32_t(vc.n);
    for (size_t k = L.glyph_begin; k < L.glyph_end;) {
      const LayoutGlyph &g = glyphs_[k];
      VisualCluster c{g.cluster, g.cluster_end, g.x, g.x + g.advance, bool(g.level & 1)};
      size_t e = k + 1;
      while (e < L.glyph_end && glyphs_[e].cluster == g.cluster) {
        int64_t a = glyphs_[e].x, b = glyphs_[e].x + glyphs_[e].advance;
        if (a < c.x0) c.x0 = a;
        if (b > c.x1) c.x1 = b;
        ++e;
      }
      if (c.x1 < c.x0)
        c.x1 = c.x0;
      // Split ligatures across their graphemes.
      size_t ga = grapheme_of_byte(c.begin), gb = c.end ? grapheme_of_byte(c.end - 1) : ga;
      size_t parts = gb >= ga ? gb - ga + 1 : 1;
      if (parts > 1 && parts < 256) {
        int64_t step = (c.x1 - c.x0) / int64_t(parts);
        for (size_t p = 0; p < parts; ++p) {
          size_t gidx = c.rtl ? gb - p : ga + p;
          VisualCluster part{gstart[gidx], gstart[gidx + 1], c.x0 + step * int64_t(p),
                             p + 1 == parts ? c.x1 : c.x0 + step * int64_t(p + 1), c.rtl};
          if (part.end > c.end)
            part.end = c.end;
          vc.push(part);
        }
      } else
        vc.push(c);
      k = e;
    }
    L.cluster_end = uint32_t(vc.n);
    line_count_ = li + 1;
  }
  cluster_cap_ = vc.n;
  clusters_ = vc.p;
  cluster_count_ = vc.n;
  vc.p = nullptr;
  vc.n = vc.cap = 0;
  if (!build_carets()) {
    clear();
    return false;
  }
  generation_ = ++layout_generation_counter;
  if (!generation_)
    generation_ = ++layout_generation_counter;
  return true;
}

namespace {
// Heap sort (no recursion, O(n log n) worst case, no allocation).
template <class Less>
void heap_sort(uint32_t *a, size_t n, Less less) {
  auto sift = [&](size_t root, size_t end) {
    for (;;) {
      size_t child = root * 2 + 1;
      if (child >= end)
        return;
      if (child + 1 < end && less(a[child], a[child + 1]))
        ++child;
      if (!less(a[root], a[child]))
        return;
      uint32_t t = a[root];
      a[root] = a[child];
      a[child] = t;
      root = child;
    }
  };
  for (size_t i = n / 2; i-- > 0;)
    sift(i, n);
  for (size_t end = n; end > 1; --end) {
    uint32_t t = a[0];
    a[0] = a[end - 1];
    a[end - 1] = t;
    sift(0, end - 1);
  }
}
} // namespace

// Primary caret of `byte` on `line` by scanning that line's clusters; used
// only while building the caret table (each line scanned once per entry
// kind, see build_carets) and as a defensive fallback.
int64_t TextLayout::caret_x_scan(size_t byte, size_t li) const {
  const LayoutLine &L = lines_[li];
  for (size_t k = L.cluster_begin; k < L.cluster_end; ++k)
    if (clusters_[k].begin == byte)
      return clusters_[k].rtl ? clusters_[k].x1 : clusters_[k].x0;
  const VisualCluster *best = nullptr;
  for (size_t k = L.cluster_begin; k < L.cluster_end; ++k)
    if (clusters_[k].end <= byte && (!best || clusters_[k].begin > best->begin))
      best = &clusters_[k];
  if (best)
    return best->rtl ? best->x0 : best->x1;
  return (L.paragraph_level & 1) ? L.x_shift + L.width : L.x_shift;
}

bool TextLayout::build_carets() {
  size_t candidates = cluster_count_ * 2 + line_count_ * 2 + 1;
  carets_ = BSF_NEW CaretStop[candidates];
  caret_x_order_ = BSF_NEW uint32_t[candidates];
  line_caret_begin_ = BSF_NEW uint32_t[line_count_ + 1];
  uint32_t *order = BSF_NEW uint32_t[candidates];
  struct Entry { uint32_t byte, aux; int64_t x; uint8_t prio; };
  Entry *e = BSF_NEW Entry[candidates];
  if (!carets_ || !caret_x_order_ || !line_caret_begin_ || !order || !e) {
    delete[] order;
    delete[] e;
    return false;
  }
  caret_count_ = 0;
  for (size_t li = 0; li < line_count_; ++li) {
    const LayoutLine &L = lines_[li];
    line_caret_begin_[li] = uint32_t(caret_count_);
    size_t n = 0;
    // Leading edge of a cluster starting at a byte wins (priority 2); else
    // the trailing edge of the logically last cluster ending there (1).
    for (size_t k = L.cluster_begin; k < L.cluster_end; ++k) {
      const VisualCluster &c = clusters_[k];
      e[n++] = {c.begin, 0xffffffffu, c.rtl ? c.x1 : c.x0, 2};
      e[n++] = {c.end, c.begin, c.rtl ? c.x0 : c.x1, 1};
    }
    e[n++] = {L.byte_begin, 0, (L.paragraph_level & 1) ? L.x_shift + L.width : L.x_shift, 0};
    for (size_t k = 0; k < n; ++k)
      order[k] = uint32_t(k);
    heap_sort(order, n, [&](uint32_t a, uint32_t b) {
      if (e[a].byte != e[b].byte)
        return e[a].byte < e[b].byte;
      if (e[a].prio != e[b].prio)
        return e[a].prio > e[b].prio;
      return e[a].aux > e[b].aux;
    });
    for (size_t k = 0; k < n; ++k) {
      const Entry &x = e[order[k]];
      if (x.byte < L.byte_begin || x.byte > L.byte_end)
        continue;
      if (caret_count_ > line_caret_begin_[li] && carets_[caret_count_ - 1].byte == x.byte)
        continue; // first (highest priority) entry per byte wins
      bool owned = li + 1 >= line_count_ || x.byte < lines_[li + 1].byte_begin;
      carets_[caret_count_++] = {x.byte, uint32_t(li), x.x, owned};
    }
  }
  line_caret_begin_[line_count_] = uint32_t(caret_count_);
  delete[] order;
  delete[] e;
  for (size_t k = 0; k < caret_count_; ++k)
    caret_x_order_[k] = uint32_t(k);
  heap_sort(caret_x_order_, caret_count_, [&](uint32_t a, uint32_t b) {
    const CaretStop &p = carets_[a], &q = carets_[b];
    if (p.line != q.line)
      return p.line < q.line;
    if (p.x != q.x)
      return p.x < q.x;
    return p.byte < q.byte;
  });
  return true;
}

size_t TextLayout::line_of(size_t byte) const {
  // Last line whose byte_begin <= byte (line starts are non-decreasing).
  size_t lo = 0, hi = line_count_;
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (lines_[m].byte_begin <= byte)
      lo = m + 1;
    else
      hi = m;
  }
  return lo ? lo - 1 : 0;
}
// Grapheme floor via the caret table (every caret is a grapheme boundary),
// avoiding a rescan of the text from byte 0; falls back to UAX #29 only for
// offsets inside graphemes that produced no glyphs.
size_t TextLayout::floor_boundary(size_t byte, size_t li) const {
  size_t lo = line_caret_begin_[li], hi = line_caret_begin_[li + 1];
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (carets_[m].byte <= byte)
      lo = m + 1;
    else
      hi = m;
  }
  if (lo > line_caret_begin_[li]) {
    size_t at = carets_[lo - 1].byte;
    if (at == byte || next_cluster(text_, at, bytes_) > byte)
      return at;
  }
  return cluster_floor(text_, bytes_, byte);
}
int64_t TextLayout::caret_x(size_t byte, size_t *line_out) const {
  if (byte > bytes_)
    byte = bytes_;
  if (!line_count_) {
    if (line_out)
      *line_out = 0;
    return 0;
  }
  size_t li = line_of(byte);
  byte = floor_boundary(byte, li);
  li = line_of(byte);
  if (line_out)
    *line_out = li;
  size_t lo = line_caret_begin_[li], hi = line_caret_begin_[li + 1];
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (carets_[m].byte < byte)
      lo = m + 1;
    else
      hi = m;
  }
  if (lo < line_caret_begin_[li + 1] && carets_[lo].byte == byte)
    return carets_[lo].x;
  return caret_x_scan(byte, li); // grapheme without glyphs (defensive)
}
size_t TextLayout::hit_test(int64_t x, size_t li) const {
  if (!line_count_)
    return 0;
  if (li >= line_count_)
    li = line_count_ - 1;
  const LayoutLine &L = lines_[li];
  if (L.cluster_begin == L.cluster_end)
    return L.byte_begin;
  const VisualCluster *first = &clusters_[L.cluster_begin],
                      *last = &clusters_[L.cluster_end - 1];
  if (x <= first->x0)
    return first->rtl ? first->end : first->begin;
  for (size_t k = L.cluster_begin; k < L.cluster_end; ++k) {
    const VisualCluster &c = clusters_[k];
    if (x < c.x1 || (k + 1 == L.cluster_end)) {
      if (x >= c.x1)
        break;
      bool left = x < (c.x0 + c.x1) / 2;
      return left == !c.rtl ? c.begin : c.end;
    }
  }
  return last->rtl ? last->begin : last->end;
}
size_t TextLayout::move_logical(size_t byte, int direction) const {
  if (byte > bytes_)
    byte = bytes_;
  return direction > 0 ? next_cluster(text_, byte, bytes_)
                       : previous_cluster(text_, cluster_floor(text_, bytes_, byte));
}
// Nearest displayable (owned) caret strictly left/right of x on line li
// (O(log k) on the x-ordered caret table). Ties prefer the logically closest
// offset to `exclude`. Monotone in x, hence always terminating.
size_t TextLayout::nearest_on_line(size_t li, int64_t x, int direction, size_t byte,
                                   bool *found_out) const {
  const uint32_t *ord = caret_x_order_;
  size_t first = line_caret_begin_[li], last = line_caret_begin_[li + 1];
  size_t lo = first, hi = last;
  while (lo < hi) { // first entry with x > current (or >= for leftward)
    size_t m = lo + (hi - lo) / 2;
    bool before = direction > 0 ? carets_[ord[m]].x <= x : carets_[ord[m]].x < x;
    if (before)
      lo = m + 1;
    else
      hi = m;
  }
  bool found = false;
  size_t best = byte;
  int64_t best_x = 0;
  auto better = [&](const CaretStop &c) {
    size_t dist = c.byte > byte ? c.byte - byte : byte - c.byte;
    size_t best_dist = best > byte ? best - byte : byte - best;
    return !found || c.x != best_x || dist < best_dist;
  };
  auto consider = [&](const CaretStop &c) {
    if (!c.owned || c.byte == byte)
      return true;
    if (found && c.x != best_x)
      return false;
    if (better(c)) {
      found = true;
      best = c.byte;
      best_x = c.x;
    }
    return true;
  };
  if (direction > 0) {
    for (size_t k = lo; k < last; ++k)
      if (!consider(carets_[ord[k]]))
        break;
  } else {
    for (size_t k = lo; k-- > first;)
      if (!consider(carets_[ord[k]]))
        break;
  }
  *found_out = found;
  return best;
}
size_t TextLayout::move_visual(size_t byte, int direction) const {
  if (!line_count_)
    return byte;
  if (byte > bytes_)
    byte = bytes_;
  byte = floor_boundary(byte, line_of(byte));
  size_t li = line_of(byte);
  // Offsets are displayed only at their primary caret, so visual movement
  // goes to the nearest displayable caret strictly left/right of the
  // current one; off the visual edge it continues logically in the
  // paragraph direction.
  bool found = false;
  size_t best = nearest_on_line(li, caret_x(byte), direction, byte, &found);
  if (found)
    return best;
  int logical = (lines_[li].paragraph_level & 1) ? -direction : direction;
  return move_logical(byte, logical);
}
int64_t TextLayout::caret_x_on(size_t byte, size_t li) const {
  if (!line_count_)
    return 0;
  if (li >= line_count_)
    li = line_count_ - 1;
  const LayoutLine &L = lines_[li];
  if (byte < L.byte_begin)
    byte = L.byte_begin;
  if (byte > L.byte_end)
    byte = L.byte_end;
  byte = floor_boundary(byte, li);
  size_t lo = line_caret_begin_[li], hi = line_caret_begin_[li + 1];
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (carets_[m].byte < byte)
      lo = m + 1;
    else
      hi = m;
  }
  if (lo < line_caret_begin_[li + 1] && carets_[lo].byte == byte)
    return carets_[lo].x;
  return caret_x_scan(byte, li);
}
size_t TextLayout::move_visual_on(size_t byte, size_t li, int direction, bool *moved) const {
  bool found = false;
  size_t best = byte;
  if (line_count_ && direction) {
    if (li >= line_count_)
      li = line_count_ - 1;
    const LayoutLine &L = lines_[li];
    if (byte < L.byte_begin)
      byte = L.byte_begin;
    if (byte > L.byte_end)
      byte = L.byte_end;
    byte = floor_boundary(byte, li);
    best = nearest_on_line(li, caret_x_on(byte, li), direction, byte, &found);
  }
  if (moved)
    *moved = found;
  return found ? best : byte;
}
size_t TextLayout::line_edge(size_t li, int direction) const {
  if (!line_count_)
    return 0;
  if (li >= line_count_)
    li = line_count_ - 1;
  size_t first = line_caret_begin_[li], last = line_caret_begin_[li + 1];
  if (first == last)
    return lines_[li].byte_begin;
  // Prefer an owned stop (a wrapped line's end is displayed on the next line).
  if (direction > 0) {
    for (size_t k = last; k-- > first;)
      if (carets_[caret_x_order_[k]].owned)
        return carets_[caret_x_order_[k]].byte;
    return carets_[caret_x_order_[last - 1]].byte;
  }
  for (size_t k = first; k < last; ++k)
    if (carets_[caret_x_order_[k]].owned)
      return carets_[caret_x_order_[k]].byte;
  return carets_[caret_x_order_[first]].byte;
}
size_t TextLayout::selection(size_t a, size_t b, Span *out, size_t capacity) const {
  if (a > b) {
    size_t t = a;
    a = b;
    b = t;
  }
  size_t count = 0;
  for (size_t li = 0; li < line_count_; ++li) {
    const LayoutLine &L = lines_[li];
    if (L.byte_end <= a || L.byte_begin >= b)
      continue;
    for (size_t k = L.cluster_begin; k < L.cluster_end; ++k) {
      const VisualCluster &c = clusters_[k];
      if (c.begin < a || c.end > b || c.begin == c.end)
        continue;
      if (count && out[count - 1].line == li && out[count - 1].x1 == c.x0)
        out[count - 1].x1 = c.x1;
      else if (count < capacity)
        out[count++] = {uint32_t(li), c.x0, c.x1};
    }
  }
  return count;
}
} // namespace BSF
