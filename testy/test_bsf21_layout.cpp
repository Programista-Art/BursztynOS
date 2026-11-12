// BSF 2.1 shared layout API: paragraph-level direction across wrapped lines,
// line-aware carets (caret_x_on / move_visual_on / line_edge), CSS letter-
// and word-spacing, cursive joining context and build generations. Runs on
// the production TextLayout with real fonts under ASan/UBSan.
#include "../bsf/layout.h"
#include "../bsf/bsf.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>
using namespace BSF;
static unsigned checks;
#define CHECK(x)                                                              \
  do {                                                                        \
    ++checks;                                                                 \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                    \
      std::abort();                                                           \
    }                                                                         \
  } while (0)
struct Font {
  std::vector<uint8_t> bytes;
  Face face;
};
struct Fonts {
  std::vector<Font *> list;
  ~Fonts() {
    for (auto *f : list)
      delete f;
  }
  bool add(const char *path) {
    std::ifstream in(path, std::ios::binary);
    auto *f = new Font;
    f->bytes.assign(std::istreambuf_iterator<char>(in), {});
    if (f->bytes.empty() || !f->face.open(f->bytes.data(), f->bytes.size(),
                                          uint32_t(list.size() + 1))) {
      delete f;
      return false;
    }
    list.push_back(f);
    return true;
  }
  static bool covers(const Face &f, const char *t, size_t a, size_t b) {
    for (size_t at = a; at < b;) {
      uint32_t cp;
      at += utf8(t + at, b - at, cp);
      if (!default_ignorable(cp) && !f.glyph(cp))
        return false;
    }
    return true;
  }
  static uint32_t run(void *u, const char *t, size_t a, size_t b) {
    for (auto *f : static_cast<Fonts *>(u)->list)
      if (covers(f->face, t, a, b))
        return f->face.id;
    return 0;
  }
  static const Face *face(void *u, uint32_t id) {
    for (auto *f : static_cast<Fonts *>(u)->list)
      if (f->face.id == id)
        return &f->face;
    return nullptr;
  }
  static int32_t fallback(void *, uint32_t, int px) { return int32_t(px / 2) * 65536; }
  FontResolver resolver() {
    FontResolver r;
    r.user = this;
    r.run_face = run;
    r.cluster_face = run;
    r.face = face;
    r.fallback_advance = fallback;
    return r;
  }
};

static bool boundary(const std::string &s, size_t b) { return cluster_boundary(s.data(), s.size(), b); }
// Walk a line with move_visual_on in one direction from its edge: strictly
// monotone x, every step a boundary, terminates within the stop count.
static size_t walk_line(const TextLayout &t, const std::string &s, size_t line, int dir) {
  size_t b = t.line_edge(line, -dir);
  int64_t x = t.caret_x_on(b, line);
  size_t steps = 0;
  for (;;) {
    bool moved = false;
    size_t m = t.move_visual_on(b, line, dir, &moved);
    if (!moved) {
      CHECK(m == b);
      break;
    }
    CHECK(boundary(s, m));
    int64_t nx = t.caret_x_on(m, line);
    CHECK(dir > 0 ? nx > x : nx < x);
    x = nx;
    b = m;
    CHECK(++steps <= s.size() + 2);
  }
  CHECK(b == t.line_edge(line, dir) || t.caret_x_on(b, line) == t.caret_x_on(t.line_edge(line, dir), line));
  return steps;
}
int main() {
  Fonts fonts;
  CHECK(fonts.add("czcionki/ttf/DejaVuSans.ttf"));
  FontResolver fr = fonts.resolver();
  LayoutOptions o;
  o.px = 16;
  TextLayout t, w;
  // 1. A wrapped RTL paragraph whose continuation line starts with English.
  // The paragraph level is resolved once (P2/P3 on the whole paragraph), so
  // every visual line keeps level 1. Laying the second line out alone
  // (the BSF 2.0 Notepad behaviour) would resolve it as LTR.
  std::string para = "שלום עולם זה טקסט ארוך Hello world 2026 ושוב עברית בסוף";
  o.max_width = 120 * Fixed;
  CHECK(t.build(para.data(), para.size(), o, fr));
  CHECK(t.line_count() >= 3);
  CHECK(t.base_level() == 1);
  size_t english_line = SIZE_MAX;
  for (size_t l = 0; l < t.line_count(); ++l) {
    CHECK(t.lines()[l].paragraph_level == 1);
    const LayoutLine &L = t.lines()[l];
    if (para.compare(L.byte_begin, 5, "Hello") == 0)
      english_line = l;
  }
  CHECK(english_line != SIZE_MAX);
  {
    const LayoutLine &L = t.lines()[english_line];
    LayoutOptions alone = o;
    alone.max_width = 0;
    CHECK(w.build(para.data() + L.byte_begin, L.byte_end - L.byte_begin, alone, fr));
    CHECK(w.base_level() == 0); // per-line auto detection gets it wrong
    LayoutOptions fixed = alone;
    fixed.direction = BaseDirection::RTL;
    CHECK(w.build(para.data() + L.byte_begin, L.byte_end - L.byte_begin, fixed, fr));
    CHECK(w.base_level() == 1);
  }
  // Alignment option right-aligns every RTL line within max_width.
  {
    LayoutOptions al = o;
    al.align_to_direction = true;
    CHECK(w.build(para.data(), para.size(), al, fr));
    for (size_t l = 0; l < w.line_count(); ++l)
      CHECK(w.lines()[l].x_shift + w.lines()[l].width == al.max_width ||
            w.lines()[l].width > al.max_width);
  }
  // 2. Soft-wrap boundary: the same byte has a caret at the end of line k
  // and at the start of line k+1; caret_x() keeps the BSF 2.0 (downstream)
  // answer, caret_x_on() exposes both.
  for (size_t l = 0; l + 1 < t.line_count(); ++l) {
    size_t b = t.lines()[l + 1].byte_begin;
    CHECK(t.lines()[l].byte_end == b);
    size_t line = 99;
    int64_t x = t.caret_x(b, &line);
    CHECK(line == l + 1 && x == t.caret_x_on(b, l + 1));
    // RTL line end (logical end of line l) is at its left edge.
    CHECK(t.caret_x_on(b, l) <= t.caret_x_on(t.lines()[l].byte_begin, l));
  }
  // 3. Visual walking per line in both directions terminates and covers
  // each displayable position once.
  for (size_t l = 0; l < t.line_count(); ++l) {
    size_t right = walk_line(t, para, l, 1), left = walk_line(t, para, l, -1);
    CHECK(right == left);
  }
  // line_edge: RTL paragraph line starts at the right edge.
  CHECK(t.line_edge(0, 1) == 0);
  // 4. Mixed paragraph with digits and punctuation, LTR base.
  std::string mixed = "Polski tekst: مرحبا بالعالم 123، שלום! koniec.";
  o.max_width = 0;
  CHECK(t.build(mixed.data(), mixed.size(), o, fr));
  CHECK(t.base_level() == 0 && t.line_count() == 1);
  walk_line(t, mixed, 0, 1);
  walk_line(t, mixed, 0, -1);
  CHECK(t.line_edge(0, -1) == 0 && t.line_edge(0, 1) == mixed.size());
  // 5. letter-spacing / word-spacing.
  {
    std::string abc = "ab cd";
    LayoutOptions sp = o;
    CHECK(w.build(abc.data(), abc.size(), sp, fr));
    int64_t base = w.width();
    sp.letter_spacing = 2 * Fixed;
    CHECK(w.build(abc.data(), abc.size(), sp, fr));
    CHECK(w.width() == base + 5 * 2 * Fixed);
    sp.letter_spacing = 0;
    sp.word_spacing = 7 * Fixed;
    CHECK(w.build(abc.data(), abc.size(), sp, fr));
    CHECK(w.width() == base + 7 * Fixed);
    // Cursive script: letter-spacing is not applied (CSS Text 3).
    std::string arabic = "مرحبا";
    sp.word_spacing = 0;
    CHECK(w.build(arabic.data(), arabic.size(), sp, fr));
    int64_t ar = w.width();
    sp.letter_spacing = 3 * Fixed;
    CHECK(w.build(arabic.data(), arabic.size(), sp, fr));
    CHECK(w.width() == ar);
    // Hebrew is not cursive: spacing per grapheme (niqqud stays attached).
    std::string he = "שָׁלוֹם";
    sp.letter_spacing = 0;
    CHECK(w.build(he.data(), he.size(), sp, fr));
    int64_t heb = w.width();
    size_t graphemes = cluster_count(he.data(), he.size());
    sp.letter_spacing = 3 * Fixed;
    CHECK(w.build(he.data(), he.size(), sp, fr));
    CHECK(w.width() == heb + int64_t(graphemes) * 3 * Fixed);
  }
  // 6. Cursive joining context across a run boundary (e.g. two DOM nodes):
  // "بب" split as "ب"|"ب" with context reproduces the joined glyphs.
  {
    std::string both = "بب", one = "ب";
    CHECK(w.build(both.data(), both.size(), o, fr));
    CHECK(w.glyph_count() == 2);
    uint16_t joined_first = 0, joined_second = 0;
    for (size_t i = 0; i < w.glyph_count(); ++i)
      (w.glyphs()[i].cluster == 0 ? joined_first : joined_second) = w.glyphs()[i].glyph;
    LayoutOptions c = o;
    CHECK(w.build(one.data(), one.size(), c, fr));
    uint16_t isolated = w.glyphs()[0].glyph;
    CHECK(isolated != joined_first && isolated != joined_second);
    c.context_after = 0x0628;
    CHECK(w.build(one.data(), one.size(), c, fr));
    CHECK(w.glyphs()[0].glyph == joined_first);
    c.context_after = 0;
    c.context_before = 0x0628;
    CHECK(w.build(one.data(), one.size(), c, fr));
    CHECK(w.glyphs()[0].glyph == joined_second);
    // A non-joining neighbour (space, Latin) changes nothing.
    c.context_before = 'a';
    CHECK(w.build(one.data(), one.size(), c, fr));
    CHECK(w.glyphs()[0].glyph == isolated);
    // Transparent context (a mark) is ignored.
    c.context_before = 0x064E;
    CHECK(w.build(one.data(), one.size(), c, fr));
    CHECK(w.glyphs()[0].glyph == isolated);
  }
  // 7. Generations: unique per build, 0 after clear.
  {
    std::string a = "abc";
    CHECK(w.build(a.data(), a.size(), o, fr));
    uint32_t g1 = w.generation();
    CHECK(w.build(a.data(), a.size(), o, fr));
    CHECK(g1 && w.generation() && w.generation() != g1);
    w.clear();
    CHECK(w.generation() == 0);
  }
  // 8. Randomized: mixed-direction wrapped paragraphs, every line walkable,
  // every caret_x_on finite, every selection span inside its line.
  {
    const char *pieces[] = {"abc", " ", "שלום", "مرحبا", "123", "،", ".", "(", ")",
                            "\xE2\x81\xA7", "\xE2\x81\xA9", "\xE2\x80\xAB", "\xE2\x80\xAC", "é", "לא", "لا",
                            "ָ", "!", "Zażółć", "\t", "\xE2\x80\x8F", "\xE2\x80\x8E"};
    std::mt19937 rng(2101);
    for (unsigned round = 0; round < 1500; ++round) {
      std::string s;
      unsigned n = rng() % 30;
      for (unsigned k = 0; k < n; ++k)
        s += pieces[rng() % (sizeof pieces / sizeof *pieces)];
      LayoutOptions r = o;
      r.max_width = (rng() % 4) ? int64_t(20 + rng() % 200) * Fixed : 0;
      r.direction = BaseDirection(rng() % 3);
      r.letter_spacing = int32_t(rng() % 3) * Fixed;
      if (!t.build(s.data(), s.size(), r, fr))
        continue;
      for (size_t l = 0; l < t.line_count(); ++l) {
        walk_line(t, s, l, 1);
        walk_line(t, s, l, -1);
        const LayoutLine &L = t.lines()[l];
        for (size_t b = L.byte_begin; b <= L.byte_end && b <= s.size();
             b = b == s.size() ? b + 1 : next_cluster(s.data(), b, s.size())) {
          int64_t x = t.caret_x_on(b, l);
          CHECK(x >= -4 * Fixed && x <= t.width() + L.x_shift + 4 * Fixed);
        }
      }
      size_t a = rng() % (s.size() + 1), b = rng() % (s.size() + 1);
      TextLayout::Span spans[64];
      size_t ns = t.selection(a, b, spans, 64);
      for (size_t k = 0; k < ns; ++k)
        CHECK(spans[k].line < t.line_count() && spans[k].x0 <= spans[k].x1);
    }
  }
  std::printf("BSF 2.1 layout API: %u checks PASS (paragraph direction across wraps, line carets, spacing, joining context, generations, 1500 fuzz rounds)\n", checks);
  return 0;
}
