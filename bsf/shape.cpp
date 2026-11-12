// BSF 2.0 shaper. Normalization, cmap + mirroring, Arabic joining, feature
// planning, GSUB, GPOS (or legacy kern fallback) and default-ignorable
// hiding. Bounded: input length, decomposition depth, mark reordering and
// lookup work are all capped.
#include "shape.h"
#include <new>
#ifdef BSF_NATIVE
#define BSF_NEW new
#else
#define BSF_NEW new (std::nothrow)
#endif
namespace BSF {
using OT::Tag;
namespace {
bool code_is(uint8_t s, const char *code) {
  const char *c = script_code(s);
  return c[0] == code[0] && c[1] == code[1] && c[2] == code[2] && c[3] == code[3];
}
enum ArabicAction : uint8_t { None, Isol, Fina, Fin2, Fin3, Medi, Med2, Init };
// Joining state machine (UAX #9-independent; ArabicShaping.txt semantics).
// Columns: U, L, R, D. Syriac Alaph/Dalath-Rish groups are not modelled.
struct Entry { uint8_t prev, cur, next; };
const Entry arabic_states[7][4] = {
    {{None, None, 0}, {None, Isol, 2}, {None, Isol, 1}, {None, Isol, 2}},
    {{None, None, 0}, {None, Isol, 2}, {None, Isol, 1}, {None, Isol, 2}},
    {{None, None, 0}, {None, Isol, 2}, {Init, Fina, 1}, {Init, Fina, 3}},
    {{None, None, 0}, {None, Isol, 2}, {Medi, Fina, 1}, {Medi, Fina, 3}},
    {{None, None, 0}, {None, Isol, 2}, {Med2, Isol, 1}, {Med2, Isol, 2}},
    {{None, None, 0}, {None, Isol, 2}, {Isol, Isol, 1}, {Isol, Isol, 2}},
    {{None, None, 0}, {None, Isol, 2}, {None, Isol, 1}, {None, Isol, 2}},
};
unsigned joining_column(Joining j) {
  return j == Joining::L ? 1 : j == Joining::R ? 2
         : (j == Joining::D || j == Joining::C) ? 3 : 0;
}
void arabic_joining(OT::Buffer &b, uint32_t before, uint32_t after) {
  unsigned prev = ~0u, state = 0;
  // Pre-context only seeds the state (transparent context is ignored).
  if (before && joining_type(before) != Joining::T)
    state = arabic_states[0][joining_column(joining_type(before))].next;
  for (unsigned i = 0; i < b.len; ++i) {
    Joining j = joining_type(b.info[i].codepoint);
    if (j == Joining::T) {
      b.info[i].action = None;
      continue;
    }
    unsigned col = joining_column(j);
    const Entry &e = arabic_states[state][col];
    if (e.prev != None && prev != ~0u)
      b.info[prev].action = e.prev;
    b.info[i].action = e.cur;
    prev = i;
    state = e.next;
  }
  // Post-context may only change the form of the last joining glyph.
  if (after && joining_type(after) != Joining::T) {
    const Entry &e = arabic_states[state][joining_column(joining_type(after))];
    if (e.prev != None && prev != ~0u)
      b.info[prev].action = e.prev;
  }
}
// Modified combining classes for mark reordering (Hebrew, Arabic, Thai,
// Tibetan), matching established OpenType shaping practice so that e.g.
// dagesh/shin dot precede vowel points and shadda precedes harakat.
uint8_t modified_ccc(uint8_t ccc) {
  static const uint8_t hebrew[] = {22, 15, 16, 17, 23, 18, 19, 20, 21,
                                   14, 24, 12, 25, 13, 10, 11, 26}; // 10..26
  static const uint8_t arabic[] = {28, 29, 30, 31, 32, 33, 27, 34, 35}; // 27..35
  if (ccc >= 10 && ccc <= 26)
    return hebrew[ccc - 10];
  if (ccc >= 27 && ccc <= 35)
    return arabic[ccc - 27];
  if (ccc == 103)
    return 3;
  if (ccc == 130)
    return 132;
  if (ccc == 132)
    return 131;
  return ccc;
}
constexpr uint32_t GlobalMask = 1;
constexpr uint32_t FormMask(unsigned action) { return 1u << action; } // bits 1..7
constexpr uint32_t RtlmMask = 1u << 8;
struct PlanKey {
  uint32_t face{}, script{}, language{}, features{};
  const uint8_t *gsub{}, *gpos{};
  uint8_t klass{};
  bool rtl{}, valid{};
};
struct PlanCache {
  PlanKey key[4];
  OT::Plan plan[4];
  unsigned cursor{};
};
PlanCache *plans{};
const OT::Plan *plan_for(const Face &f, const OT::Layout &L, uint8_t script,
                         ShapeClass klass, const ShapeOptions &o) {
  if (!plans) {
    plans = BSF_NEW PlanCache;
    if (!plans)
      return nullptr;
  }
  PlanKey k{f.id, script_tag(script), o.language, o.features, L.gsub.p,
            L.gpos.p, uint8_t(klass), o.rtl, true};
  for (unsigned i = 0; i < 4; ++i) {
    const PlanKey &c = plans->key[i];
    if (c.valid && c.face == k.face && c.script == k.script &&
        c.language == k.language && c.features == k.features &&
        c.gsub == k.gsub && c.gpos == k.gpos && c.rtl == k.rtl && c.klass == k.klass)
      return &plans->plan[i];
  }
  OT::FeatureRequest req[40];
  unsigned n = 0;
  auto add = [&](char a, char b, char c, char d, uint32_t mask, uint8_t stage,
                 bool gpos = false) {
    if (n < 40)
      req[n++] = {Tag(a, b, c, d), mask, stage, gpos};
  };
  add('r', 'v', 'r', 'n', GlobalMask, 0);
  if (o.rtl) {
    add('r', 't', 'l', 'a', GlobalMask, 1);
    add('r', 't', 'l', 'm', RtlmMask, 1);
  } else {
    add('l', 't', 'r', 'a', GlobalMask, 1);
    add('l', 't', 'r', 'm', GlobalMask, 1);
  }
  uint8_t last = 2;
  add('c', 'c', 'm', 'p', GlobalMask, 2);
  add('l', 'o', 'c', 'l', GlobalMask, 2);
  if (klass == ShapeClass::Arabic) {
    const char *forms[] = {"isol", "fina", "fin2", "fin3", "medi", "med2", "init"};
    const uint8_t actions[] = {Isol, Fina, Fin2, Fin3, Medi, Med2, Init};
    for (unsigned i = 0; i < 7; ++i)
      add(forms[i][0], forms[i][1], forms[i][2], forms[i][3],
          FormMask(actions[i]), uint8_t(3 + i));
    add('r', 'l', 'i', 'g', GlobalMask, 10);
    add('c', 'a', 'l', 't', (o.features & FeatureCalt) ? GlobalMask : 0, 11);
    last = 12;
    add('r', 'c', 'l', 't', GlobalMask, last);
    add('m', 's', 'e', 't', GlobalMask, last);
  } else {
    add('r', 'l', 'i', 'g', GlobalMask, last);
    if (o.features & FeatureCalt)
      add('c', 'a', 'l', 't', GlobalMask, last);
    add('r', 'c', 'l', 't', GlobalMask, last);
  }
  if (o.features & FeatureLiga)
    add('l', 'i', 'g', 'a', GlobalMask, last);
  if (o.features & FeatureClig)
    add('c', 'l', 'i', 'g', GlobalMask, last);
  if (o.features & FeatureDlig)
    add('d', 'l', 'i', 'g', GlobalMask, last);
  if (o.features & FeatureAalt)
    add('a', 'a', 'l', 't', GlobalMask, last);
  if (o.features & FeatureKern)
    add('k', 'e', 'r', 'n', GlobalMask, 0, true);
  if (o.features & FeatureMarks) {
    add('m', 'a', 'r', 'k', GlobalMask, 0, true);
    add('m', 'k', 'm', 'k', GlobalMask, 0, true);
  }
  add('c', 'u', 'r', 's', GlobalMask, 0, true);
  add('d', 'i', 's', 't', GlobalMask, 0, true);
  add('a', 'b', 'v', 'm', GlobalMask, 0, true);
  add('b', 'l', 'w', 'm', GlobalMask, 0, true);
  // Drop disabled (mask 0) requests.
  unsigned m = 0;
  for (unsigned i = 0; i < n; ++i)
    if (req[i].mask)
      req[m++] = req[i];
  uint32_t tags[2] = {script_tag(script), 0};
  unsigned tag_count = 1;
  // Indic v2 tags fall back to the v1 tag ("dev2" -> "deva").
  if ((tags[0] & 0xff) == '2') {
    const char *c = script_code(script);
    tags[1] = Tag(char(c[0] | 32), c[1], c[2], c[3]);
    tag_count = 2;
  }
  unsigned slot = plans->cursor++ % 4;
  plans->key[slot] = PlanKey{};
  OT::build_plan(L, tags, tag_count, o.language, req, m, plans->plan[slot]);
  plans->key[slot] = k;
  return &plans->plan[slot];
}
} // namespace

ShapeClass shape_class(uint8_t s) {
  static const char *joining[] = {"Arab", "Syrc", "Mong", "Nkoo", "Phag", "Mand",
                                  "Mani", "Adlm", "Rohg", "Sogd", "Chrs", "Yezi",
                                  "Ougr", "Hmnp"};
  for (const char *c : joining)
    if (code_is(s, c))
      return ShapeClass::Arabic;
  if (code_is(s, "Hebr"))
    return ShapeClass::Hebrew;
  static const char *indic[] = {"Deva", "Beng", "Guru", "Gujr", "Orya", "Taml",
                                "Telu", "Knda", "Mlym", "Sinh", "Khmr", "Mymr",
                                "Tibt", "Thai", "Laoo", "Bali", "Java"};
  for (const char *c : indic)
    if (code_is(s, c))
      return ShapeClass::Indic;
  return ShapeClass::Default;
}

bool shape(const Face &f, const OT::Layout &L, const uint32_t *cps,
           const uint32_t *clusters, size_t n, const ShapeOptions &o,
           OT::Buffer &b, ShapeReport *report) {
  ShapeReport local;
  ShapeReport &r = report ? *report : local;
  r = ShapeReport{};
  b.clear();
  if (!n)
    return true;
  if (!cps || !clusters || n > OT::MaxShapeGlyphs || !f.id)
    return false;
  if (!b.reserve(unsigned(n) * 2 + 16))
    return false;
  // 1. Normalization: decompose scalars the font cannot map directly.
  for (size_t i = 0; i < n; ++i) {
    uint32_t stack[4] = {cps[i]};
    unsigned depth = 1;
    // Expand depth-first, keeping order, bounded at 3 levels.
    uint32_t out[8];
    unsigned produced = 0;
    while (depth && produced < 8) {
      uint32_t c = stack[--depth];
      uint32_t a, d;
      // Shortest-form decomposition: every produced component must map,
      // except `a`, which may decompose further.
      uint32_t a2, d2;
      if (!f.glyph(c) && depth < 3 && decompose(c, a, d) && (!d || f.glyph(d)) &&
          (f.glyph(a) || (decompose(a, a2, d2) && f.glyph(a2) && (!d2 || f.glyph(d2))))) {
        r.normalized = true;
        if (d)
          stack[depth++] = d;
        stack[depth++] = a;
        continue;
      }
      out[produced++] = c;
    }
    for (unsigned k = 0; k < produced; ++k) {
      OT::GlyphInfo g{};
      g.codepoint = out[k];
      g.cluster = clusters[i];
      g.ccc = modified_ccc(uint8_t(combining_class(out[k])));
      if (!b.push(g))
        return false;
    }
  }
  // 2. Canonical ordering of combining marks (runs capped at 32 marks).
  for (unsigned i = 0; i < b.len;) {
    if (!b.info[i].ccc) {
      ++i;
      continue;
    }
    unsigned e = i;
    while (e < b.len && b.info[e].ccc)
      ++e;
    if (e - i <= 32)
      for (unsigned a = i + 1; a < e; ++a) {
        OT::GlyphInfo v = b.info[a];
        unsigned j = a;
        while (j > i && b.info[j - 1].ccc > v.ccc) {
          b.info[j] = b.info[j - 1];
          --j;
        }
        b.info[j] = v;
      }
    i = e;
  }
  // 3. Recompose starter + unblocked mark when the font has the composite.
  {
    unsigned w = 0, starter = ~0u;
    uint8_t last_ccc = 0;
    for (unsigned i = 0; i < b.len; ++i) {
      OT::GlyphInfo g = b.info[i];
      if (starter != ~0u && g.ccc && mark_category(g.codepoint) &&
          !(last_ccc && last_ccc >= g.ccc)) {
        uint32_t composite = compose(b.info[starter].codepoint, g.codepoint);
        if (composite && f.glyph(composite)) {
          b.info[starter].codepoint = composite;
          b.info[starter].ccc = modified_ccc(uint8_t(combining_class(composite)));
          r.normalized = true;
          continue; // mark consumed; blocking state unchanged
        }
      }
      if (!g.ccc) {
        starter = w;
        last_ccc = 0;
      } else
        last_ccc = g.ccc;
      b.info[w++] = g;
    }
    b.len = w;
  }
  // 4. cmap, mirroring (L4) and glyph properties.
  for (unsigned i = 0; i < b.len; ++i) {
    OT::GlyphInfo &g = b.info[i];
    g.mask = GlobalMask;
    uint32_t cp = g.codepoint;
    if (o.rtl) {
      uint32_t m = bidi_mirror(cp);
      if (m && f.glyph(m))
        cp = m;
      else if (m)
        g.mask |= RtlmMask;
    }
    g.glyph = uint16_t(f.glyph(cp));
    g.props = L.props(g.glyph, g.codepoint);
    if (default_ignorable(g.codepoint))
      g.props |= OT::PropIgnorable;
  }
  ShapeClass klass = shape_class(o.script);
  if (klass == ShapeClass::Arabic) {
    arabic_joining(b, o.context_before, o.context_after);
    for (unsigned i = 0; i < b.len; ++i)
      if (b.info[i].action)
        b.info[i].mask |= FormMask(b.info[i].action);
  }
  const OT::Plan *plan = (L.gsub.n || L.gpos.n) ? plan_for(f, L, o.script, klass, o) : nullptr;
  OT::Budget budget;
  budget.remaining = o.budget;
  if (plan && plan->gsub_count) {
    r.gsub = true;
    if (!OT::substitute(L, *plan, b, budget, &r.stats))
      r.budget_failed = true;
  }
  // 5. Default advances, then GPOS or legacy kern.
  for (unsigned i = 0; i < b.len; ++i) {
    b.pos[i] = OT::GlyphPosition{};
    b.pos[i].x_advance = int32_t(f.advance_units(b.info[i].glyph));
  }
  if (plan && plan->gpos_count) {
    r.gpos = true;
    r.gpos_marks = plan->has_gpos_mark;
    if (!OT::position(L, *plan, b, budget, o.rtl, &r.stats))
      r.budget_failed = true;
  } else {
    for (unsigned i = 0; i < b.len; ++i) {
      if (b.info[i].props & OT::PropMark)
        b.pos[i].x_advance = 0;
      if (b.info[i].props & OT::PropIgnorable)
        b.pos[i] = OT::GlyphPosition{};
    }
  }
  if ((o.features & FeatureKern) && !(plan && plan->has_gpos_kern) && f.kern.n) {
    r.legacy_kern = true;
    for (unsigned i = 0; i < b.len; ++i) {
      if (b.info[i].props & (OT::PropMark | OT::PropIgnorable))
        continue;
      unsigned j = i + 1;
      while (j < b.len && (b.info[j].props & (OT::PropMark | OT::PropIgnorable)))
        ++j;
      if (j < b.len)
        b.pos[i].x_advance += f.kerning_units(b.info[i].glyph, b.info[j].glyph);
    }
  }
  // 6. Hidden default ignorables render as the zero-advance space glyph.
  unsigned space = f.glyph(' ');
  if (space) {
    for (unsigned i = 0; i < b.len; ++i)
      if (b.info[i].props & OT::PropIgnorable)
        b.info[i].glyph = uint16_t(space);
  } else {
    // No space glyph (e.g. emoji fonts): delete hidden ignorables, as
    // HarfBuzz does, instead of keeping visible .notdef entries.
    unsigned kept = 0;
    for (unsigned i = 0; i < b.len; ++i)
      if (!(b.info[i].props & OT::PropIgnorable)) {
        b.info[kept] = b.info[i];
        b.pos[kept] = b.pos[i];
        ++kept;
      }
    b.len = kept;
  }
  if (o.rtl)
    b.reverse(0, b.len);
  return !r.budget_failed;
}
} // namespace BSF
