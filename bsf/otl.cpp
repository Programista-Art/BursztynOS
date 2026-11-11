// BSF 2.0 OpenType Layout engine (GDEF/GSUB/GPOS).
// All table access goes through Bytes (bounds-checked, zero on overrun).
// Every loop that depends on font data is charged to a Budget so malformed
// or hostile tables terminate deterministically. Recursion is limited to
// MaxNesting nested contextual lookups; attachment chains are iterative.
#include "otl.h"
#include "text.h"
#include <new>
#ifdef BSF_NATIVE
#define BSF_NEW new
#else
#define BSF_NEW new (std::nothrow)
#endif
namespace BSF {
namespace OT {
namespace {
enum LookupFlag : unsigned {
  RightToLeft = 1, IgnoreBase = 2, IgnoreLigatures = 4, IgnoreMarks = 8,
  UseMarkFilteringSet = 16, MarkAttachMask = 0xff00
};
inline Bytes at(Bytes t, size_t off) {
  return off && off < t.n ? Bytes{t.p + off, t.n - off} : Bytes{};
}
inline Bytes at32(Bytes t, size_t off) { return at(t, off); }
int coverage(Bytes c, unsigned g, Budget &b) {
  unsigned fmt = c.u16(0), count = c.u16(2);
  if (fmt == 1) {
    if (!c.has(4, size_t(count) * 2))
      return -1;
    unsigned lo = 0, hi = count;
    while (lo < hi) {
      if (!b.spend())
        return -1;
      unsigned m = lo + (hi - lo) / 2, v = c.u16(4 + m * 2);
      if (g < v)
        hi = m;
      else if (g > v)
        lo = m + 1;
      else
        return int(m);
    }
  } else if (fmt == 2) {
    if (!c.has(4, size_t(count) * 6))
      return -1;
    unsigned lo = 0, hi = count;
    while (lo < hi) {
      if (!b.spend())
        return -1;
      unsigned m = lo + (hi - lo) / 2, o = 4 + m * 6;
      unsigned first = c.u16(o), last = c.u16(o + 2);
      if (g < first)
        hi = m;
      else if (g > last)
        lo = m + 1;
      else
        return int(c.u16(o + 4) + (g - first));
    }
  }
  return -1;
}
unsigned class_of(Bytes c, unsigned g, Budget &b) {
  unsigned fmt = c.u16(0);
  if (fmt == 1) {
    unsigned start = c.u16(2), count = c.u16(4);
    if (g >= start && g - start < count && c.has(6 + size_t(g - start) * 2, 2))
      return c.u16(6 + (g - start) * 2);
  } else if (fmt == 2) {
    unsigned count = c.u16(2);
    if (!c.has(4, size_t(count) * 6))
      return 0;
    unsigned lo = 0, hi = count;
    while (lo < hi) {
      if (!b.spend())
        return 0;
      unsigned m = lo + (hi - lo) / 2, o = 4 + m * 6;
      if (g < c.u16(o))
        hi = m;
      else if (g > c.u16(o + 2))
        lo = m + 1;
      else
        return c.u16(o + 4);
    }
  }
  return 0;
}
// Popcount of a ValueFormat (each set bit is one 16-bit field).
unsigned value_size(unsigned fmt) {
  unsigned n = 0;
  for (unsigned v = fmt & 0xff; v; v &= v - 1)
    ++n;
  return n * 2;
}
} // namespace

// ------------------------------------------------------------ Buffer
Buffer::~Buffer() {
  delete[] info;
  delete[] pos;
}
bool Buffer::reserve(unsigned capacity) {
  if (capacity > MaxShapeGlyphs * 2)
    return false;
  if (capacity <= cap)
    return true;
  auto *i = BSF_NEW GlyphInfo[capacity];
  auto *p = BSF_NEW GlyphPosition[capacity];
  if (!i || !p) {
    delete[] i;
    delete[] p;
    return false;
  }
  for (unsigned k = 0; k < len; ++k) {
    i[k] = info[k];
    p[k] = pos[k];
  }
  delete[] info;
  delete[] pos;
  info = i;
  pos = p;
  cap = capacity;
  return true;
}
bool Buffer::push(const GlyphInfo &g) {
  if (len == cap)
    return false;
  info[len] = g;
  pos[len] = GlyphPosition{};
  ++len;
  return true;
}
bool Buffer::splice(unsigned at, unsigned remove, const uint16_t *glyphs,
                    unsigned count, Budget &b) {
  if (at > len || remove > len - at || (count > remove && count - remove > cap - len))
    return false;
  unsigned tail = len - at - remove;
  if (!b.spend(1 + tail / 16))
    return false;
  GlyphInfo base = remove ? info[at] : (at ? info[at - 1] : GlyphInfo{});
  // Explicit element moves: Ring 3 provides no memmove symbol.
  if (count > remove)
    for (unsigned k = tail; k-- > 0;) {
      info[at + count + k] = info[at + remove + k];
      pos[at + count + k] = pos[at + remove + k];
    }
  else if (count < remove)
    for (unsigned k = 0; k < tail; ++k) {
      info[at + count + k] = info[at + remove + k];
      pos[at + count + k] = pos[at + remove + k];
    }
  for (unsigned k = 0; k < count; ++k) {
    info[at + k] = base;
    info[at + k].glyph = glyphs[k];
    pos[at + k] = GlyphPosition{};
  }
  len = len - remove + count;
  return true;
}
void Buffer::reverse(unsigned begin, unsigned end) {
  if (end > len)
    end = len;
  while (begin + 1 < end) {
    --end;
    GlyphInfo i = info[begin];
    info[begin] = info[end];
    info[end] = i;
    GlyphPosition p = pos[begin];
    pos[begin] = pos[end];
    pos[end] = p;
    ++begin;
  }
}

// ------------------------------------------------------------ GDEF
bool Layout::open(const Face &f) {
  *this = Layout{};
  glyph_count = f.glyphs;
  // Header sanity only; subtables are validated lazily on access.
  if (f.gdef.has(0, 12) && f.gdef.u16(0) == 1) {
    gdef = f.gdef;
    glyph_classes = at(gdef, gdef.u16(4));
    mark_classes = at(gdef, gdef.u16(10));
    if (gdef.u16(2) >= 2 && gdef.has(12, 2))
      mark_sets = at(gdef, gdef.u16(12));
    if (gdef.u16(2) >= 3 && gdef.has(14, 4))
      var_store = at32(gdef, gdef.u32(14));
  }
  auto header = [](Bytes t) {
    return t.has(0, 10) && t.u16(0) == 1 && t.u16(2) <= 1 ? t : Bytes{};
  };
  gsub = header(f.gsub);
  gpos = header(f.gpos);
  return true;
}
unsigned Layout::glyph_class(unsigned g) const {
  Budget b;
  return glyph_classes.n ? class_of(glyph_classes, g, b) : 0;
}
unsigned Layout::mark_class(unsigned g) const {
  Budget b;
  return mark_classes.n ? class_of(mark_classes, g, b) & 0xff : 0;
}
bool Layout::in_mark_set(unsigned set, unsigned g) const {
  if (!mark_sets.n || mark_sets.u16(0) != 1 || set >= mark_sets.u16(2) ||
      !mark_sets.has(4 + size_t(set) * 4, 4))
    return false;
  Budget b;
  return coverage(at32(mark_sets, mark_sets.u32(4 + set * 4)), g, b) >= 0;
}
uint16_t Layout::props(unsigned g, uint32_t cp) const {
  if (glyph_classes.n) {
    switch (glyph_class(g)) {
    case 1: return PropBase;
    case 2: return PropLigature;
    case 3: return uint16_t(PropMark | (mark_class(g) << 8));
    default: return 0;
    }
  }
  // No GDEF: synthesize from General_Category like common practice (Mn).
  return mark_category(cp) == 1 ? uint16_t(PropMark) : uint16_t(PropBase);
}

// ------------------------------------------------------------ plan
namespace {
Bytes find_script(Bytes list, uint32_t tag) {
  unsigned n = list.u16(0);
  if (!list.has(2, size_t(n) * 6))
    return {};
  for (unsigned i = 0; i < n; ++i)
    if (list.u32(2 + i * 6) == tag)
      return at(list, list.u16(2 + i * 6 + 4));
  return {};
}
Bytes find_langsys(Bytes script, uint32_t lang) {
  unsigned n = script.u16(2);
  if (lang && script.has(4, size_t(n) * 6))
    for (unsigned i = 0; i < n; ++i)
      if (script.u32(4 + i * 6) == lang)
        return at(script, script.u16(4 + i * 6 + 4));
  return at(script, script.u16(0));
}
unsigned collect(Bytes table, Bytes langsys, const FeatureRequest *features,
                 unsigned feature_count, bool gpos, LookupEntry *out,
                 unsigned capacity) {
  if (!langsys.n)
    return 0;
  Bytes flist = at(table, table.u16(6));
  unsigned feature_total = flist.u16(0);
  if (!flist.has(2, size_t(feature_total) * 6))
    return 0;
  Bytes llist = at(table, table.u16(8));
  unsigned lookup_total = llist.u16(0);
  unsigned count = 0;
  unsigned n = langsys.u16(4);
  if (!langsys.has(6, size_t(n) * 2))
    return 0;
  for (unsigned r = 0; r < feature_count; ++r) {
    const FeatureRequest &req = features[r];
    if (req.gpos != gpos)
      continue;
    // Required feature first (applies with the global bit), then listed.
    for (unsigned k = 0; k <= n; ++k) {
      unsigned fi = k == n ? langsys.u16(2) : langsys.u16(6 + k * 2);
      if (fi == 0xffff || fi >= feature_total)
        continue;
      if (flist.u32(2 + fi * 6) != req.tag)
        continue;
      Bytes feature = at(flist, flist.u16(2 + fi * 6 + 4));
      unsigned lc = feature.u16(2);
      if (!feature.has(4, size_t(lc) * 2))
        continue;
      for (unsigned j = 0; j < lc; ++j) {
        unsigned li = feature.u16(4 + j * 2);
        if (li >= lookup_total)
          continue;
        bool merged = false;
        for (unsigned e = 0; e < count; ++e)
          if (out[e].index == li && out[e].stage == req.stage) {
            out[e].mask |= req.mask;
            merged = true;
            break;
          }
        if (!merged && count < capacity)
          out[count++] = {uint16_t(li), req.stage, req.mask};
      }
    }
  }
  // Order by (stage, lookup index); insertion sort over <= MaxLookups.
  for (unsigned i = 1; i < count; ++i) {
    LookupEntry v = out[i];
    unsigned j = i;
    while (j && (out[j - 1].stage > v.stage ||
                 (out[j - 1].stage == v.stage && out[j - 1].index > v.index))) {
      out[j] = out[j - 1];
      --j;
    }
    out[j] = v;
  }
  return count;
}
} // namespace
bool build_plan(const Layout &L, const uint32_t *scripts, unsigned script_count,
                uint32_t language, const FeatureRequest *features,
                unsigned feature_count, Plan &plan) {
  plan.gsub_count = plan.gpos_count = 0;
  plan.has_gpos_kern = plan.has_gpos_mark = false;
  plan.script = plan.language = 0;
  for (int which = 0; which < 2; ++which) {
    Bytes t = which ? L.gpos : L.gsub;
    if (!t.n)
      continue;
    Bytes slist = at(t, t.u16(4));
    Bytes script{};
    uint32_t chosen = 0;
    for (unsigned i = 0; i < script_count && i < 4 && !script.n; ++i) {
      script = find_script(slist, scripts[i]);
      chosen = scripts[i];
    }
    const uint32_t fallbacks[] = {Tag('D', 'F', 'L', 'T'), Tag('d', 'f', 'l', 't'),
                                  Tag('l', 'a', 't', 'n')};
    for (unsigned i = 0; i < 3 && !script.n; ++i) {
      script = find_script(slist, fallbacks[i]);
      chosen = fallbacks[i];
    }
    if (!script.n)
      continue;
    if (!which)
      plan.script = chosen;
    Bytes langsys = find_langsys(script, language);
    if (which) {
      plan.gpos_count = collect(t, langsys, features, feature_count, true,
                                plan.gpos, MaxLookups);
      for (unsigned r = 0; r < feature_count; ++r) {
        if (!features[r].gpos)
          continue;
        LookupEntry probe[1];
        FeatureRequest one = features[r];
        if (!collect(t, langsys, &one, 1, true, probe, 1))
          continue;
        if (one.tag == Tag('k', 'e', 'r', 'n'))
          plan.has_gpos_kern = true;
        if (one.tag == Tag('m', 'a', 'r', 'k') || one.tag == Tag('m', 'k', 'm', 'k'))
          plan.has_gpos_mark = true;
      }
    } else
      plan.gsub_count = collect(t, langsys, features, feature_count, false,
                                plan.gsub, MaxLookups);
  }
  plan.language = language;
  return true;
}

// ------------------------------------------------------------ apply core
namespace {
struct Context {
  const Layout &L;
  Buffer &buf;
  Budget &budget;
  Statistics *stats;
  Bytes lookups;      // LookupList
  bool gpos;
  bool rtl;
  unsigned depth{};
};
struct LookupInfo {
  Bytes table{};
  unsigned type{}, flag{}, count{}, set{};
};
LookupInfo lookup_at(const Context &c, unsigned index) {
  LookupInfo r;
  unsigned total = c.lookups.u16(0);
  if (index >= total || !c.lookups.has(2 + size_t(index) * 2, 2))
    return r;
  r.table = at(c.lookups, c.lookups.u16(2 + index * 2));
  r.type = r.table.u16(0);
  r.flag = r.table.u16(2);
  r.count = r.table.u16(4);
  if (!r.table.has(6, size_t(r.count) * 2))
    r.count = 0;
  if (r.flag & UseMarkFilteringSet)
    r.set = r.table.u16(6 + r.count * 2);
  return r;
}
bool skip(const Context &c, const LookupInfo &l, const GlyphInfo &g,
          bool context) {
  unsigned p = g.props;
  if (context && (p & PropIgnorable))
    return true;
  if (p & l.flag & (IgnoreBase | IgnoreLigatures | IgnoreMarks))
    return true;
  if (p & PropMark) {
    if (l.flag & UseMarkFilteringSet)
      return !c.L.in_mark_set(l.set, g.glyph);
    if (l.flag & MarkAttachMask)
      return (l.flag & MarkAttachMask) != (p & MarkAttachMask);
  }
  return false;
}
// Next/previous index honoring lookup flags; -1 when none.
int next_index(const Context &c, const LookupInfo &l, int i) {
  for (int j = i + 1; j < int(c.buf.len); ++j) {
    if (!c.budget.spend())
      return -1;
    if (!skip(c, l, c.buf.info[j], true))
      return j;
  }
  return -1;
}
int prev_index(const Context &c, const LookupInfo &l, int i) {
  for (int j = i - 1; j >= 0; --j) {
    if (!c.budget.spend())
      return -1;
    if (!skip(c, l, c.buf.info[j], true))
      return j;
  }
  return -1;
}
uint16_t refreshed_props(const Context &c, uint16_t glyph, const GlyphInfo &old,
                         uint16_t extra) {
  uint16_t p = c.L.props(glyph, old.codepoint);
  return uint16_t(p | extra | (old.props & PropIgnorable));
}
bool apply_lookup(Context &c, unsigned index, unsigned at_index, int &next);
// Apply nested SequenceLookupRecords over matched positions.
bool apply_records(Context &c, Bytes records, unsigned record_count,
                   int *positions, unsigned count, int &end) {
  if (c.depth >= MaxNesting)
    return false;
  ++c.depth;
  for (unsigned r = 0; r < record_count; ++r) {
    if (!records.has(size_t(r) * 4, 4) || !c.budget.spend())
      break;
    unsigned seq = records.u16(r * 4), lookup = records.u16(r * 4 + 2);
    if (seq >= count)
      continue;
    unsigned before = c.buf.len;
    int ignored = 0;
    int p = positions[seq];
    if (p < 0 || p >= int(c.buf.len))
      continue;
    apply_lookup(c, lookup, unsigned(p), ignored);
    int delta = int(c.buf.len) - int(before);
    if (delta) {
      for (unsigned k = 0; k < count; ++k)
        if (positions[k] > p) {
          positions[k] += delta;
          if (positions[k] <= p)
            positions[k] = p + 1;
        }
      end += delta;
      if (end < p + 1)
        end = p + 1;
    }
  }
  --c.depth;
  if (end > int(c.buf.len))
    end = int(c.buf.len);
  return true;
}
// Generic rule matcher. kind: 0 glyph ids, 1 classes, 2 coverages.
struct Matcher {
  int kind;
  Bytes base;         // subtable (coverage offsets are relative to it)
  Bytes classdef;     // for kind 1
  bool match(Context &c, unsigned glyph, unsigned value) const {
    if (kind == 0)
      return glyph == value;
    if (kind == 1)
      return class_of(classdef, glyph, c.budget) == value;
    return coverage(at(base, value), glyph, c.budget) >= 0;
  }
};
// Match `count` input values (values[0] is implicit for kinds 0/1).
bool match_input(Context &c, const LookupInfo &l, const Matcher &m, Bytes values,
                 size_t value_offset, unsigned count, unsigned start,
                 int *positions, bool first_implicit) {
  if (!count || count > MaxContext)
    return false;
  positions[0] = int(start);
  if (!first_implicit &&
      !m.match(c, c.buf.info[start].glyph, values.u16(value_offset)))
    return false;
  int j = int(start);
  for (unsigned k = 1; k < count; ++k) {
    j = next_index(c, l, j);
    if (j < 0)
      return false;
    size_t o = value_offset + size_t(first_implicit ? k - 1 : k) * 2;
    if (!values.has(o, 2) || !m.match(c, c.buf.info[j].glyph, values.u16(o)))
      return false;
    positions[k] = j;
  }
  return true;
}
bool match_backtrack(Context &c, const LookupInfo &l, const Matcher &m,
                     Bytes values, size_t o, unsigned count, int start) {
  int j = start;
  for (unsigned k = 0; k < count; ++k) {
    j = prev_index(c, l, j);
    if (j < 0 || !values.has(o + size_t(k) * 2, 2) ||
        !m.match(c, c.buf.info[j].glyph, values.u16(o + k * 2)))
      return false;
  }
  return true;
}
bool match_lookahead(Context &c, const LookupInfo &l, const Matcher &m,
                     Bytes values, size_t o, unsigned count, int last) {
  int j = last;
  for (unsigned k = 0; k < count; ++k) {
    j = next_index(c, l, j);
    if (j < 0 || !values.has(o + size_t(k) * 2, 2) ||
        !m.match(c, c.buf.info[j].glyph, values.u16(o + k * 2)))
      return false;
  }
  return true;
}
// Contextual (GSUB 5 / GPOS 7) and chained contextual (GSUB 6 / GPOS 8).
bool apply_context(Context &c, const LookupInfo &l, Bytes s, unsigned i,
                   bool chained, int &next) {
  unsigned fmt = s.u16(0);
  unsigned glyph = c.buf.info[i].glyph;
  int positions[MaxContext];
  auto finish = [&](Bytes records, unsigned rc, unsigned count) {
    int end = positions[count - 1] + 1;
    apply_records(c, records, rc, positions, count, end);
    next = end;
    if (c.stats)
      ++c.stats->lookups_applied;
    return true;
  };
  if (fmt == 1 || fmt == 2) {
    Matcher back{fmt == 1 ? 0 : 1, s, {}}, in{fmt == 1 ? 0 : 1, s, {}},
        ahead{fmt == 1 ? 0 : 1, s, {}};
    int cov = coverage(at(s, s.u16(2)), glyph, c.budget);
    if (cov < 0)
      return false;
    unsigned set_index = unsigned(cov);
    size_t sets_at = 6;
    if (fmt == 2) {
      if (chained) {
        back.classdef = at(s, s.u16(4));
        in.classdef = at(s, s.u16(6));
        ahead.classdef = at(s, s.u16(8));
        sets_at = 12;
      } else {
        in.classdef = at(s, s.u16(4));
        sets_at = 8;
      }
      set_index = class_of(in.classdef, glyph, c.budget);
    } else
      sets_at = 6;
    unsigned set_count = s.u16(sets_at - 2);
    if (set_index >= set_count || !s.has(sets_at + size_t(set_index) * 2, 2))
      return false;
    Bytes set = at(s, s.u16(sets_at + set_index * 2));
    unsigned rules = set.u16(0);
    if (!set.has(2, size_t(rules) * 2))
      return false;
    for (unsigned r = 0; r < rules; ++r) {
      if (!c.budget.spend())
        return false;
      Bytes rule = at(set, set.u16(2 + r * 2));
      if (!rule.n)
        continue;
      if (!chained) {
        unsigned count = rule.u16(0), rc = rule.u16(2);
        if (!count || count > MaxContext || !rule.has(4, size_t(count - 1) * 2 + size_t(rc) * 4))
          continue;
        if (!match_input(c, l, in, rule, 4, count, i, positions, true))
          continue;
        return finish(rule.sub(4 + (count - 1) * 2, size_t(rc) * 4), rc, count);
      }
      size_t o = 0;
      unsigned bc = rule.u16(o);
      size_t back_at = o + 2;
      o = back_at + size_t(bc) * 2;
      unsigned ic = rule.u16(o);
      size_t in_at = o + 2;
      if (!ic || ic > MaxContext)
        continue;
      o = in_at + size_t(ic - 1) * 2;
      unsigned ac = rule.u16(o);
      size_t ahead_at = o + 2;
      o = ahead_at + size_t(ac) * 2;
      unsigned rc = rule.u16(o);
      if (!rule.has(o + 2, size_t(rc) * 4))
        continue;
      if (!match_input(c, l, in, rule, in_at, ic, i, positions, true) ||
          !match_backtrack(c, l, back, rule, back_at, bc, int(i)) ||
          !match_lookahead(c, l, ahead, rule, ahead_at, ac, positions[ic - 1]))
        continue;
      return finish(rule.sub(o + 2, size_t(rc) * 4), rc, ic);
    }
    return false;
  }
  if (fmt == 3) {
    Matcher m{2, s, {}};
    if (!chained) {
      unsigned count = s.u16(2), rc = s.u16(4);
      if (!count || count > MaxContext || !s.has(6, size_t(count) * 2 + size_t(rc) * 4))
        return false;
      if (!match_input(c, l, m, s, 6, count, i, positions, false))
        return false;
      return finish(s.sub(6 + count * 2, size_t(rc) * 4), rc, count);
    }
    size_t o = 2;
    unsigned bc = s.u16(o);
    size_t back_at = o + 2;
    o = back_at + size_t(bc) * 2;
    unsigned ic = s.u16(o);
    size_t in_at = o + 2;
    if (!ic || ic > MaxContext)
      return false;
    o = in_at + size_t(ic) * 2;
    unsigned ac = s.u16(o);
    size_t ahead_at = o + 2;
    o = ahead_at + size_t(ac) * 2;
    unsigned rc = s.u16(o);
    if (!s.has(o + 2, size_t(rc) * 4))
      return false;
    if (!match_input(c, l, m, s, in_at, ic, i, positions, false) ||
        !match_backtrack(c, l, m, s, back_at, bc, int(i)) ||
        !match_lookahead(c, l, m, s, ahead_at, ac, positions[ic - 1]))
      return false;
    return finish(s.sub(o + 2, size_t(rc) * 4), rc, ic);
  }
  return false;
}

// ------------------------------------------------------------ GSUB
bool gsub_single(Context &c, Bytes s, unsigned i) {
  unsigned g = c.buf.info[i].glyph;
  int cov = coverage(at(s, s.u16(2)), g, c.budget);
  if (cov < 0)
    return false;
  unsigned out;
  if (s.u16(0) == 1)
    out = (g + s.u16(4)) & 0xffff;
  else if (s.u16(0) == 2) {
    if (unsigned(cov) >= s.u16(4) || !s.has(6 + size_t(cov) * 2, 2))
      return false;
    out = s.u16(6 + cov * 2);
  } else
    return false;
  if (out >= c.L.glyph_count)
    return false;
  GlyphInfo &gi = c.buf.info[i];
  gi.props = refreshed_props(c, uint16_t(out), gi, PropSubstituted);
  gi.glyph = uint16_t(out);
  return true;
}
bool gsub_multiple(Context &c, Bytes s, unsigned i, int &next) {
  int cov = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (cov < 0 || s.u16(0) != 1 || unsigned(cov) >= s.u16(4) ||
      !s.has(6 + size_t(cov) * 2, 2))
    return false;
  Bytes seq = at(s, s.u16(6 + cov * 2));
  unsigned n = seq.u16(0);
  if (!seq.has(2, size_t(n) * 2) || n > 32)
    return false;
  uint16_t glyphs[32];
  for (unsigned k = 0; k < n; ++k) {
    glyphs[k] = seq.u16(2 + k * 2);
    if (glyphs[k] >= c.L.glyph_count)
      return false;
  }
  GlyphInfo old = c.buf.info[i];
  if (!c.buf.splice(i, 1, glyphs, n, c.budget))
    return false;
  for (unsigned k = 0; k < n; ++k) {
    GlyphInfo &g = c.buf.info[i + k];
    g.props = refreshed_props(c, glyphs[k], old, PropMultiplied | PropSubstituted);
    g.lig_comp = uint8_t(n > 1 ? k + 1 : old.lig_comp);
  }
  next = int(i + n);
  return true;
}
bool gsub_alternate(Context &c, Bytes s, unsigned i) {
  int cov = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (cov < 0 || s.u16(0) != 1 || unsigned(cov) >= s.u16(4) ||
      !s.has(6 + size_t(cov) * 2, 2))
    return false;
  Bytes set = at(s, s.u16(6 + cov * 2));
  if (!set.u16(0) || !set.has(2, 2))
    return false;
  unsigned out = set.u16(2); // feature value 1: first alternate
  if (out >= c.L.glyph_count)
    return false;
  GlyphInfo &gi = c.buf.info[i];
  gi.props = refreshed_props(c, uint16_t(out), gi, PropSubstituted);
  gi.glyph = uint16_t(out);
  return true;
}
bool gsub_ligature(Context &c, const LookupInfo &l, Bytes s, unsigned i,
                   int &next) {
  int cov = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (cov < 0 || s.u16(0) != 1 || unsigned(cov) >= s.u16(4) ||
      !s.has(6 + size_t(cov) * 2, 2))
    return false;
  Bytes set = at(s, s.u16(6 + cov * 2));
  unsigned count = set.u16(0);
  if (!set.has(2, size_t(count) * 2))
    return false;
  int positions[MaxContext];
  for (unsigned r = 0; r < count; ++r) {
    if (!c.budget.spend())
      return false;
    Bytes lig = at(set, set.u16(2 + r * 2));
    unsigned glyph = lig.u16(0), comps = lig.u16(2);
    if (!comps || comps > MaxContext || !lig.has(4, size_t(comps - 1) * 2) ||
        glyph >= c.L.glyph_count)
      continue;
    Matcher m{0, lig, {}};
    if (!match_input(c, l, m, lig, 4, comps, i, positions, true))
      continue;
    // Ligature found. Assign a ligature id; skipped marks inside the span
    // remember the component they follow (for mark-to-ligature).
    uint8_t id = c.buf.next_lig_id;
    c.buf.next_lig_id = uint8_t(c.buf.next_lig_id % 255 + 1);
    bool marks_only = true;
    for (unsigned k = 0; k < comps; ++k)
      marks_only &= (c.buf.info[positions[k]].props & PropMark) != 0;
    uint32_t cluster = c.buf.info[i].cluster;
    for (unsigned k = 0; k < comps; ++k)
      if (c.buf.info[positions[k]].cluster < cluster)
        cluster = c.buf.info[positions[k]].cluster;
    // Merge clusters monotonically: glyphs sharing the first or last
    // component's source cluster join the ligature's cluster.
    {
      uint32_t head = c.buf.info[i].cluster,
               tail = c.buf.info[positions[comps - 1]].cluster;
      for (int j = int(i); j-- > 0 && c.buf.info[j].cluster == head;)
        c.buf.info[j].cluster = cluster;
      for (unsigned j = unsigned(positions[comps - 1]) + 1;
           j < c.buf.len && c.buf.info[j].cluster == tail; ++j)
        c.buf.info[j].cluster = cluster;
    }
    unsigned comp = 1;
    for (int j = int(i) + 1; j <= positions[comps - 1]; ++j) {
      if (comp < comps && j == positions[comp]) {
        ++comp;
        continue;
      }
      GlyphInfo &m2 = c.buf.info[j];
      m2.lig_id = marks_only ? m2.lig_id : id;
      m2.lig_comp = uint8_t(comp);
      m2.cluster = cluster;
    }
    // Marks immediately after the ligature that belonged to its last
    // component now refer to component `comps`.
    for (int j = positions[comps - 1] + 1; j < int(c.buf.len); ++j) {
      GlyphInfo &m2 = c.buf.info[j];
      if (!(m2.props & PropMark))
        break;
      if (!m2.lig_comp) {
        m2.lig_id = marks_only ? m2.lig_id : id;
        m2.lig_comp = uint8_t(comps);
      }
    }
    GlyphInfo &first = c.buf.info[i];
    first.props = refreshed_props(c, uint16_t(glyph), first,
                                  PropLigated | PropSubstituted);
    first.glyph = uint16_t(glyph);
    first.lig_id = marks_only ? first.lig_id : id;
    first.lig_comp = 0;
    first.lig_count = uint8_t(comps);
    first.cluster = cluster;
    // Remove components back to front so earlier positions stay valid.
    for (unsigned k = comps; k-- > 1;)
      if (!c.buf.splice(unsigned(positions[k]), 1, nullptr, 0, c.budget))
        return false;
    next = int(i + 1);
    return true;
  }
  return false;
}
bool gsub_reverse(Context &c, const LookupInfo &l, Bytes s, unsigned i) {
  if (s.u16(0) != 1)
    return false;
  int cov = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (cov < 0)
    return false;
  Matcher m{2, s, {}};
  unsigned bc = s.u16(4);
  size_t ac_at = 6 + size_t(bc) * 2;
  unsigned ac = s.u16(ac_at);
  size_t g_at = ac_at + 2 + size_t(ac) * 2;
  unsigned gc = s.u16(g_at);
  if (unsigned(cov) >= gc || !s.has(g_at + 2 + size_t(cov) * 2, 2))
    return false;
  if (!match_backtrack(c, l, m, s, 6, bc, int(i)) ||
      !match_lookahead(c, l, m, s, ac_at + 2, ac, int(i)))
    return false;
  unsigned out = s.u16(g_at + 2 + cov * 2);
  if (out >= c.L.glyph_count)
    return false;
  GlyphInfo &gi = c.buf.info[i];
  gi.props = refreshed_props(c, uint16_t(out), gi, PropSubstituted);
  gi.glyph = uint16_t(out);
  return true;
}

// ------------------------------------------------------------ GPOS
struct Value {
  int32_t xp{}, yp{}, xa{}, ya{};
};
int device_delta(Bytes d, unsigned ppem) {
  unsigned start = d.u16(0), end = d.u16(2), fmt = d.u16(4);
  if (!ppem || fmt < 1 || fmt > 3 || ppem < start || ppem > end)
    return 0;
  unsigned idx = ppem - start, bits = 1u << fmt; // 2, 4, 8 bits
  unsigned per = 16 / bits, word = d.u16(6 + (idx / per) * 2);
  unsigned shift = 16 - bits * (idx % per + 1);
  int v = int((word >> shift) & ((1u << bits) - 1));
  if (v >= int(1u << (bits - 1)))
    v -= int(1u << bits);
  return v;
}
Value read_value(Bytes base, Bytes rec, size_t o, unsigned fmt, unsigned ppem) {
  Value v;
  auto field = [&]() {
    int16_t x = rec.s16(o);
    o += 2;
    return x;
  };
  if (fmt & 1) v.xp = field();
  if (fmt & 2) v.yp = field();
  if (fmt & 4) v.xa = field();
  if (fmt & 8) v.ya = field();
  // Device tables (ppem hinting) apply only when a ppem is requested;
  // VariationIndex tables (format 0x8000) need a variation instance.
  auto dev = [&]() {
    unsigned off = rec.u16(o);
    o += 2;
    return off ? device_delta(at(base, off), ppem) : 0;
  };
  if (fmt & 16) v.xp += dev();
  if (fmt & 32) v.yp += dev();
  if (fmt & 64) v.xa += dev();
  if (fmt & 128) v.ya += dev();
  return v;
}
void add(GlyphPosition &p, const Value &v) {
  p.x_offset += v.xp;
  p.y_offset += v.yp;
  p.x_advance += v.xa;
  p.y_advance += v.ya;
}
bool anchor(Bytes a, int32_t &x, int32_t &y) {
  unsigned fmt = a.u16(0);
  if (fmt < 1 || fmt > 3 || !a.has(0, 6))
    return false;
  x = a.s16(2);
  y = a.s16(4);
  return true;
}
bool gpos_single(Context &c, Bytes s, unsigned i) {
  int cov = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (cov < 0)
    return false;
  unsigned fmt = s.u16(4), size = value_size(fmt);
  if (s.u16(0) == 1) {
    if (!s.has(6, size))
      return false;
    add(c.buf.pos[i], read_value(s, s, 6, fmt, 0));
  } else if (s.u16(0) == 2) {
    if (unsigned(cov) >= s.u16(6) || !s.has(8 + size_t(cov) * size, size))
      return false;
    add(c.buf.pos[i], read_value(s, s, 8 + cov * size, fmt, 0));
  } else
    return false;
  return true;
}
bool gpos_pair(Context &c, const LookupInfo &l, Bytes s, unsigned i, int &next) {
  int cov = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (cov < 0)
    return false;
  int j = next_index(c, l, int(i));
  if (j < 0)
    return false;
  unsigned f1 = s.u16(4), f2 = s.u16(6), z1 = value_size(f1), z2 = value_size(f2);
  unsigned second = c.buf.info[j].glyph;
  if (s.u16(0) == 1) {
    if (unsigned(cov) >= s.u16(8) || !s.has(10 + size_t(cov) * 2, 2))
      return false;
    Bytes set = at(s, s.u16(10 + cov * 2));
    unsigned n = set.u16(0), rec = 2 + z1 + z2;
    if (!set.has(2, size_t(n) * rec))
      return false;
    unsigned lo = 0, hi = n;
    while (lo < hi) {
      if (!c.budget.spend())
        return false;
      unsigned m = lo + (hi - lo) / 2, o = 2 + m * rec, g = set.u16(o);
      if (second < g)
        hi = m;
      else if (second > g)
        lo = m + 1;
      else {
        add(c.buf.pos[i], read_value(s, set, o + 2, f1, 0));
        add(c.buf.pos[j], read_value(s, set, o + 2 + z1, f2, 0));
        next = z2 ? j + 1 : j;
        return true;
      }
    }
    return false;
  }
  if (s.u16(0) == 2) {
    Bytes cd1 = at(s, s.u16(8)), cd2 = at(s, s.u16(10));
    unsigned n1 = s.u16(12), n2 = s.u16(14);
    unsigned k1 = class_of(cd1, c.buf.info[i].glyph, c.budget),
             k2 = class_of(cd2, second, c.budget);
    size_t rec = z1 + z2;
    if (k1 >= n1 || k2 >= n2)
      return false;
    size_t o = 16 + (size_t(k1) * n2 + k2) * rec;
    if (!s.has(o, rec))
      return false;
    add(c.buf.pos[i], read_value(s, s, o, f1, 0));
    add(c.buf.pos[j], read_value(s, s, o + z1, f2, 0));
    next = z2 ? j + 1 : j;
    return true;
  }
  return false;
}
// Iteratively reverse a cursive chain so `i` can attach to a new parent.
void reverse_cursive(Buffer &b, unsigned i, unsigned new_parent, Budget &budget) {
  // Chains longer than one word are not meaningful; cap the walk.
  constexpr unsigned MaxChain = 256;
  unsigned path[MaxChain];
  unsigned n = 0, cur = i;
  while (n < MaxChain && budget.spend()) {
    GlyphPosition &p = b.pos[cur];
    if (!p.attach_chain || !(p.attach_type & AttachCursive))
      break;
    int64_t j = int64_t(cur) + p.attach_chain;
    if (j < 0 || j >= int64_t(b.len))
      break;
    path[n++] = cur;
    if (unsigned(j) == new_parent)
      break;
    cur = unsigned(j);
  }
  // Process from the far end back toward i (mirrors the recursive form).
  for (unsigned k = n; k--;) {
    unsigned a = path[k];
    GlyphPosition &p = b.pos[a];
    int chain = p.attach_chain;
    uint8_t type = p.attach_type;
    p.attach_chain = 0;
    unsigned j = unsigned(int64_t(a) + chain);
    if (j == new_parent)
      continue;
    b.pos[j].y_offset = -p.y_offset;
    b.pos[j].attach_chain = -chain;
    b.pos[j].attach_type = type;
  }
}
bool gpos_cursive(Context &c, const LookupInfo &l, Bytes s, unsigned i) {
  if (s.u16(0) != 1)
    return false;
  Bytes cov_table = at(s, s.u16(2));
  unsigned count = s.u16(4);
  if (!s.has(6, size_t(count) * 4))
    return false;
  int ci = coverage(cov_table, c.buf.info[i].glyph, c.budget);
  if (ci < 0 || unsigned(ci) >= count)
    return false;
  unsigned entry_off = s.u16(6 + ci * 4);
  if (!entry_off)
    return false;
  int pj = prev_index(c, l, int(i));
  if (pj < 0)
    return false;
  int cp = coverage(cov_table, c.buf.info[pj].glyph, c.budget);
  if (cp < 0 || unsigned(cp) >= count)
    return false;
  unsigned exit_off = s.u16(6 + cp * 4 + 2);
  if (!exit_off)
    return false;
  int32_t ex, ey, nx, ny;
  if (!anchor(at(s, exit_off), ex, ey) || !anchor(at(s, entry_off), nx, ny))
    return false;
  unsigned a = unsigned(pj), b = i; // a: previous (exit), b: current (entry)
  GlyphPosition *pos = c.buf.pos;
  if (!c.rtl) {
    pos[a].x_advance = ex + pos[a].x_offset;
    int32_t d = nx + pos[b].x_offset;
    pos[b].x_advance -= d;
    pos[b].x_offset -= d;
  } else {
    int32_t d = ex + pos[a].x_offset;
    pos[a].x_advance -= d;
    pos[a].x_offset -= d;
    pos[b].x_advance = nx + pos[b].x_offset;
  }
  unsigned child = a, parent = b;
  int32_t y = ny - ey;
  if (!(l.flag & RightToLeft)) {
    child = b;
    parent = a;
    y = -y;
  }
  reverse_cursive(c.buf, child, parent, c.budget);
  pos[child].attach_type = AttachCursive;
  pos[child].attach_chain = int32_t(parent) - int32_t(child);
  pos[child].y_offset = y;
  return true;
}
// Mark array lookup shared by mark-to-base/ligature/mark.
bool mark_anchor(Bytes mark_array, unsigned mark_index, unsigned &klass,
                 int32_t &x, int32_t &y) {
  unsigned n = mark_array.u16(0);
  if (mark_index >= n || !mark_array.has(2 + size_t(mark_index) * 4, 4))
    return false;
  klass = mark_array.u16(2 + mark_index * 4);
  return anchor(at(mark_array, mark_array.u16(2 + mark_index * 4 + 2)), x, y);
}
void attach_mark(Context &c, unsigned mark, unsigned base, int32_t bx,
                 int32_t by, int32_t mx, int32_t my) {
  GlyphPosition &o = c.buf.pos[mark];
  o.x_offset = bx - mx;
  o.y_offset = by - my;
  o.attach_type = AttachMark;
  o.attach_chain = int32_t(base) - int32_t(mark);
}
bool gpos_mark_base(Context &c, Bytes s, unsigned i) {
  if (s.u16(0) != 1)
    return false;
  int mi = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (mi < 0)
    return false;
  // Find the preceding non-mark glyph (marks are always skipped here).
  int j = int(i) - 1;
  for (; j >= 0; --j) {
    if (!c.budget.spend())
      return false;
    const GlyphInfo &g = c.buf.info[j];
    if (g.props & PropMark)
      continue;
    if (g.props & PropIgnorable)
      continue;
    break;
  }
  if (j < 0)
    return false;
  int bi = coverage(at(s, s.u16(4)), c.buf.info[j].glyph, c.budget);
  if (bi < 0)
    return false;
  unsigned classes = s.u16(6), klass;
  int32_t mx, my, bx, by;
  if (!mark_anchor(at(s, s.u16(8)), unsigned(mi), klass, mx, my) || klass >= classes)
    return false;
  Bytes bases = at(s, s.u16(10));
  unsigned nb = bases.u16(0);
  size_t o = 2 + (size_t(bi) * classes + klass) * 2;
  if (unsigned(bi) >= nb || !bases.has(o, 2) || !bases.u16(o))
    return false;
  if (!anchor(at(bases, bases.u16(o)), bx, by))
    return false;
  attach_mark(c, i, unsigned(j), bx, by, mx, my);
  return true;
}
bool gpos_mark_lig(Context &c, Bytes s, unsigned i) {
  if (s.u16(0) != 1)
    return false;
  int mi = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (mi < 0)
    return false;
  int j = int(i) - 1;
  for (; j >= 0; --j) {
    if (!c.budget.spend())
      return false;
    if (!(c.buf.info[j].props & (PropMark | PropIgnorable)))
      break;
  }
  if (j < 0)
    return false;
  int li = coverage(at(s, s.u16(4)), c.buf.info[j].glyph, c.budget);
  if (li < 0)
    return false;
  unsigned classes = s.u16(6), klass;
  int32_t mx, my, bx, by;
  if (!mark_anchor(at(s, s.u16(8)), unsigned(mi), klass, mx, my) || klass >= classes)
    return false;
  Bytes ligs = at(s, s.u16(10));
  if (unsigned(li) >= ligs.u16(0) || !ligs.has(2 + size_t(li) * 2, 2))
    return false;
  Bytes attach = at(ligs, ligs.u16(2 + li * 2));
  unsigned comps = attach.u16(0);
  if (!comps)
    return false;
  const GlyphInfo &lig = c.buf.info[j], &mark = c.buf.info[i];
  unsigned comp = comps - 1;
  if (lig.lig_id && lig.lig_id == mark.lig_id && mark.lig_comp)
    comp = (mark.lig_comp < comps ? mark.lig_comp : comps) - 1;
  size_t o = 2 + (size_t(comp) * classes + klass) * 2;
  if (!attach.has(o, 2) || !attach.u16(o))
    return false;
  if (!anchor(at(attach, attach.u16(o)), bx, by))
    return false;
  attach_mark(c, i, unsigned(j), bx, by, mx, my);
  return true;
}
bool gpos_mark_mark(Context &c, const LookupInfo &l, Bytes s, unsigned i) {
  if (s.u16(0) != 1)
    return false;
  int m1 = coverage(at(s, s.u16(2)), c.buf.info[i].glyph, c.budget);
  if (m1 < 0)
    return false;
  LookupInfo relaxed = l;
  relaxed.flag &= ~unsigned(IgnoreBase | IgnoreLigatures | IgnoreMarks);
  int j = prev_index(c, relaxed, int(i));
  if (j < 0 || !(c.buf.info[j].props & PropMark))
    return false;
  const GlyphInfo &a = c.buf.info[i], &b = c.buf.info[j];
  bool good = a.lig_id == b.lig_id ? (!a.lig_id || a.lig_comp == b.lig_comp)
                                   : ((a.lig_id && !a.lig_comp) || (b.lig_id && !b.lig_comp));
  if (!good)
    return false;
  int m2 = coverage(at(s, s.u16(4)), b.glyph, c.budget);
  if (m2 < 0)
    return false;
  unsigned classes = s.u16(6), klass;
  int32_t mx, my, bx, by;
  if (!mark_anchor(at(s, s.u16(8)), unsigned(m1), klass, mx, my) || klass >= classes)
    return false;
  Bytes arr = at(s, s.u16(10));
  size_t o = 2 + (size_t(m2) * classes + klass) * 2;
  if (unsigned(m2) >= arr.u16(0) || !arr.has(o, 2) || !arr.u16(o))
    return false;
  if (!anchor(at(arr, arr.u16(o)), bx, by))
    return false;
  attach_mark(c, i, unsigned(j), bx, by, mx, my);
  return true;
}

// Apply one subtable of the lookup at buffer index i.
bool apply_subtable(Context &c, const LookupInfo &l, unsigned type, Bytes s,
                    unsigned i, int &next) {
  next = int(i + 1);
  if (!c.gpos) {
    switch (type) {
    case 1: return gsub_single(c, s, i);
    case 2: return gsub_multiple(c, s, i, next);
    case 3: return gsub_alternate(c, s, i);
    case 4: return gsub_ligature(c, l, s, i, next);
    case 5: return apply_context(c, l, s, i, false, next);
    case 6: return apply_context(c, l, s, i, true, next);
    case 8: return gsub_reverse(c, l, s, i);
    }
    return false;
  }
  switch (type) {
  case 1: return gpos_single(c, s, i);
  case 2: return gpos_pair(c, l, s, i, next);
  case 3: return gpos_cursive(c, l, s, i);
  case 4: return gpos_mark_base(c, s, i);
  case 5: return gpos_mark_lig(c, s, i);
  case 6: return gpos_mark_mark(c, l, s, i);
  case 7: return apply_context(c, l, s, i, false, next);
  case 8: return apply_context(c, l, s, i, true, next);
  }
  return false;
}
// Resolve Extension subtables (GSUB 7 / GPOS 9) to (type, table).
bool resolve_subtable(const Context &c, const LookupInfo &l, unsigned k,
                      unsigned &type, Bytes &s) {
  s = at(l.table, l.table.u16(6 + k * 2));
  type = l.type;
  unsigned ext = c.gpos ? 9 : 7;
  if (type == ext) {
    if (s.u16(0) != 1)
      return false;
    type = s.u16(2);
    if (type == ext)
      return false; // extension of an extension is invalid
    s = at32(s, s.u32(4));
  }
  return s.n != 0;
}
void count_applied(Statistics &st, bool gpos, unsigned declared,
                   unsigned type, unsigned format) {
  ++(gpos ? st.positionings : st.substitutions);
  unsigned f = format <= 3 ? format : 0;
  if (gpos) {
    if (type < 10)
      ++st.gpos_applied[type][f];
    if (declared == 9 && type != 9)
      ++st.gpos_applied[9][1];
  } else {
    if (type < 9)
      ++st.gsub_applied[type][f];
    if (declared == 7 && type != 7)
      ++st.gsub_applied[7][1];
  }
}
bool apply_lookup_at(Context &c, const LookupInfo &l, unsigned i, int &next) {
  if (skip(c, l, c.buf.info[i], false))
    return false;
  for (unsigned k = 0; k < l.count; ++k) {
    if (!c.budget.spend())
      return false;
    unsigned type;
    Bytes s;
    if (!resolve_subtable(c, l, k, type, s))
      continue;
    if (apply_subtable(c, l, type, s, i, next)) {
      if (c.stats)
        count_applied(*c.stats, c.gpos, l.type, type, s.u16(0));
      return true;
    }
  }
  return false;
}
bool apply_lookup(Context &c, unsigned index, unsigned i, int &next) {
  LookupInfo l = lookup_at(c, index);
  if (!l.table.n || i >= c.buf.len)
    return false;
  return apply_lookup_at(c, l, i, next);
}
bool lookup_is_reverse(const Context &c, const LookupInfo &l) {
  if (c.gpos)
    return false;
  if (l.type == 8)
    return true;
  if (l.type == 7 && l.count) {
    unsigned type;
    Bytes s;
    return resolve_subtable(c, l, 0, type, s) && type == 8;
  }
  return false;
}
bool run_lookups(Context &c, const LookupEntry *entries, unsigned count,
                 unsigned stage_from, unsigned stage_to) {
  for (unsigned e = 0; e < count; ++e) {
    if (entries[e].stage < stage_from || entries[e].stage > stage_to)
      continue;
    LookupInfo l = lookup_at(c, entries[e].index);
    if (!l.table.n)
      continue;
    uint32_t mask = entries[e].mask;
    if (lookup_is_reverse(c, l)) {
      for (unsigned i = c.buf.len; i-- > 0;) {
        if (c.budget.exhausted)
          return false;
        int next;
        if (c.buf.info[i].mask & mask)
          apply_lookup_at(c, l, i, next);
      }
      continue;
    }
    for (unsigned i = 0; i < c.buf.len;) {
      if (c.budget.exhausted)
        return false;
      int next = int(i + 1);
      if ((c.buf.info[i].mask & mask) && apply_lookup_at(c, l, i, next)) {
        if (next <= int(i))
          next = int(i + 1);
        i = unsigned(next);
      } else
        ++i;
    }
  }
  return !c.budget.exhausted;
}
} // namespace

bool substitute(const Layout &L, const Plan &plan, Buffer &buf, Budget &budget,
                Statistics *stats, unsigned stage_from, unsigned stage_to) {
  if (!L.gsub.n)
    return true;
  Context c{L, buf, budget, stats, at(L.gsub, L.gsub.u16(8)), false, false};
  bool ok = run_lookups(c, plan.gsub, plan.gsub_count, stage_from, stage_to);
  if (!ok && stats)
    ++stats->budget_failures;
  return ok;
}
void finish_offsets(Buffer &b, bool rtl) {
  // Resolve each glyph's chain to its root iteratively (no recursion).
  unsigned n = b.len;
  uint8_t *done = BSF_NEW uint8_t[n ? n : 1];
  unsigned *path = BSF_NEW unsigned[n ? n : 1];
  if (!done || !path) {
    delete[] done;
    delete[] path;
    return;
  }
  for (unsigned k = 0; k < n; ++k)
    done[k] = 0;
  for (unsigned start = 0; start < n; ++start) {
    unsigned depth = 0, cur = start;
    while (!done[cur] && depth < n) {
      path[depth++] = cur;
      GlyphPosition &p = b.pos[cur];
      int64_t j = int64_t(cur) + p.attach_chain;
      if (!p.attach_chain || j < 0 || j >= int64_t(n))
        break;
      cur = unsigned(j);
      if (depth >= n)
        break; // cycle guard: chains longer than the buffer are invalid
    }
    for (unsigned k = depth; k-- > 0;) {
      unsigned i = path[k];
      if (done[i])
        continue;
      GlyphPosition &p = b.pos[i];
      int64_t j64 = int64_t(i) + p.attach_chain;
      done[i] = 1;
      if (!p.attach_chain || j64 < 0 || j64 >= int64_t(n))
        continue;
      unsigned j = unsigned(j64);
      if (!done[j]) // cycle: detach rather than loop
        continue;
      if (p.attach_type & AttachCursive)
        p.y_offset += b.pos[j].y_offset;
      if (p.attach_type & AttachMark) {
        p.x_offset += b.pos[j].x_offset;
        p.y_offset += b.pos[j].y_offset;
        if (j < i) {
          if (!rtl)
            for (unsigned k2 = j; k2 < i; ++k2) {
              p.x_offset -= b.pos[k2].x_advance;
              p.y_offset -= b.pos[k2].y_advance;
            }
          else
            for (unsigned k2 = j + 1; k2 <= i; ++k2) {
              p.x_offset += b.pos[k2].x_advance;
              p.y_offset += b.pos[k2].y_advance;
            }
        } else {
          for (unsigned k2 = i; k2 < j; ++k2) {
            p.x_offset += rtl ? -b.pos[k2].x_advance : b.pos[k2].x_advance;
          }
        }
      }
    }
  }
  delete[] done;
  delete[] path;
}
bool position(const Layout &L, const Plan &plan, Buffer &buf, Budget &budget,
              bool rtl, Statistics *stats) {
  bool ok = true;
  if (L.gpos.n) {
    Context c{L, buf, budget, stats, at(L.gpos, L.gpos.u16(8)), true, rtl};
    ok = run_lookups(c, plan.gpos, plan.gpos_count, 0, 255);
    if (!ok && stats)
      ++stats->budget_failures;
  }
  // Zero GDEF mark advances after GPOS (attachment already placed them).
  for (unsigned i = 0; i < buf.len; ++i) {
    if (buf.info[i].props & PropMark) {
      buf.pos[i].x_advance = 0;
      buf.pos[i].y_advance = 0;
    }
    if (buf.info[i].props & PropIgnorable)
      buf.pos[i] = GlyphPosition{};
  }
  finish_offsets(buf, rtl);
  return ok;
}
uint32_t Statistics::gsub_type_mask() const {
  uint32_t m = 0;
  for (unsigned t = 1; t < 9; ++t)
    for (unsigned f = 0; f < 4; ++f)
      if (gsub_applied[t][f])
        m |= 1u << t;
  return m;
}
uint32_t Statistics::gpos_type_mask() const {
  uint32_t m = 0;
  for (unsigned t = 1; t < 10; ++t)
    for (unsigned f = 0; f < 4; ++f)
      if (gpos_applied[t][f])
        m |= 1u << t;
  return m;
}
} // namespace OT
} // namespace BSF
