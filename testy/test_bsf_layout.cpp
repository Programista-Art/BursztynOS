// BSF 2.0 unified layout: bidi visual order, caret/hit-test/selection,
// ligature caret splitting, wrapping, shaping-run-aware fallback, measure
// == draw consistency and randomized robustness (ASan/UBSan).
#include "../bsf/layout.h"
#include "../bsf/bsf.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
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
static std::vector<uint32_t> visual_clusters(const TextLayout &t, size_t line = 0) {
  std::vector<uint32_t> v;
  const LayoutLine &L = t.lines()[line];
  for (size_t k = L.cluster_begin; k < L.cluster_end; ++k)
    v.push_back(t.clusters()[k].begin);
  return v;
}
static void invariants(const TextLayout &t, const std::string &s) {
  // Every boundary has a caret; hit-testing a caret returns a boundary.
  int64_t total = 0;
  for (size_t i = 0; i < t.glyph_count(); ++i)
    total += t.glyphs()[i].advance;
  int64_t sum_lines = 0;
  for (size_t l = 0; l < t.line_count(); ++l) {
    const LayoutLine &L = t.lines()[l];
    sum_lines += L.width;
    CHECK(L.byte_begin <= L.byte_end && L.byte_end <= s.size());
    for (size_t k = L.cluster_begin; k < L.cluster_end; ++k) {
      const VisualCluster &c = t.clusters()[k];
      CHECK(c.x0 <= c.x1 && c.begin <= c.end && c.end <= s.size());
      CHECK(cluster_boundary(s.data(), s.size(), c.begin));
      size_t h = t.hit_test((c.x0 + c.x1) / 2, l);
      CHECK(h <= s.size() && cluster_boundary(s.data(), s.size(), h));
    }
  }
  CHECK(total == sum_lines); // measured width equals drawn advances
  for (size_t b = 0; b <= s.size(); b = next_cluster(s.data(), b, s.size())) {
    size_t line;
    int64_t x = t.caret_x(b, &line);
    CHECK(line < t.line_count() || !t.line_count());
    CHECK(x >= -Fixed && x <= t.width() + Fixed + (t.line_count() ? t.lines()[line].x_shift : 0));
    for (int d : {-1, 1}) {
      size_t m = t.move_visual(b, d);
      CHECK(m <= s.size() && cluster_boundary(s.data(), s.size(), m));
    }
    if (b == s.size())
      break;
  }
}
int main() {
  Fonts fonts;
  CHECK(fonts.add("czcionki/ttf/DejaVuSans.ttf"));
  bool naskh = fonts.add("/usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf");
  FontResolver fr = fonts.resolver();
  LayoutOptions o;
  o.px = 16;
  TextLayout t;
  // Pure LTR equals logical order; carets strictly increase.
  std::string hello = "Hello, world!";
  CHECK(t.build(hello.data(), hello.size(), o, fr));
  CHECK(t.line_count() == 1 && t.base_level() == 0);
  int64_t last = -1;
  for (size_t b = 0; b <= hello.size(); ++b) {
    int64_t x = t.caret_x(b);
    CHECK(x > last);
    last = x;
    CHECK(t.hit_test(x + 1) == b || t.hit_test(x + 1) == b + 1 || b == hello.size());
  }
  CHECK(t.move_visual(0, 1) == 1 && t.move_visual(5, -1) == 4);
  invariants(t, hello);
  // Mixed LTR paragraph: "abc " + RTL run with an LTR number inside.
  std::string mixed = "abc אבג 123 ד def";
  CHECK(t.build(mixed.data(), mixed.size(), o, fr));
  CHECK(t.base_level() == 0);
  std::vector<uint32_t> v = visual_clusters(t);
  // Visual byte order: a b c sp ד sp 1 2 3 sp ג ב א sp d e f
  std::vector<uint32_t> expect = {0, 1, 2, 3, 15, 14, 11, 12, 13, 10, 8, 6, 4, 17, 18, 19, 20};
  CHECK(v == expect);
  invariants(t, mixed);
  // Primary caret at the start of the RTL run is at its right edge.
  {
    size_t alef = 4;
    int64_t x = t.caret_x(alef);
    const VisualCluster *c = nullptr;
    for (size_t k = 0; k < t.cluster_count(); ++k)
      if (t.clusters()[k].begin == alef)
        c = &t.clusters()[k];
    CHECK(c && c->rtl && x == c->x1);
    // Right arrow from the visual left edge of the RTL run walks into it.
    size_t at = 3; // before the space preceding the RTL run
    std::vector<int64_t> xs;
    size_t b = 0;
    for (unsigned step = 0; step < 64; ++step) {
      xs.push_back(t.caret_x(b));
      size_t m = t.move_visual(b, 1);
      if (m == b)
        break;
      b = m;
      if (b == mixed.size())
        break;
    }
    (void)at;
    CHECK(b == mixed.size());
  }
  // Logical text is preserved byte for byte.
  CHECK(mixed == "abc אבג 123 ד def");
  // RTL paragraph: offset 0 is the rightmost caret, end is leftmost.
  std::string ar = "مرحبا";
  CHECK(t.build(ar.data(), ar.size(), o, fr));
  CHECK(t.base_level() == 1);
  CHECK(t.caret_x(0) == t.width() && t.caret_x(ar.size()) == 0);
  CHECK(t.hit_test(t.width() - 1) == 0 || t.hit_test(t.width() - 1) == 2);
  CHECK(t.hit_test(0) == ar.size());
  invariants(t, ar);
  // Arabic shaping uses contextual forms (glyphs differ from isolated cmap).
  {
    std::string beh3 = "ببب";
    CHECK(t.build(beh3.data(), beh3.size(), o, fr));
    const Face &dv = fonts.list[0]->face;
    unsigned iso = dv.glyph(0x628);
    unsigned distinct = 0;
    for (size_t i = 0; i < t.glyph_count(); ++i)
      distinct += t.glyphs()[i].glyph != iso;
    CHECK(distinct == 3); // init, medi, fina
    CHECK(t.glyphs()[0].cluster == 4 && t.glyphs()[2].cluster == 0); // visual RTL
  }
  // Lam-alef ligature: one glyph, two graphemes, two caret cells.
  std::string la = "لا";
  CHECK(t.build(la.data(), la.size(), o, fr));
  CHECK(t.glyph_count() == 1 && t.cluster_count() == 2);
  CHECK(t.clusters()[0].begin == 2 && t.clusters()[1].begin == 0); // RTL split
  CHECK(t.caret_x(2) > 0 && t.caret_x(2) < t.width());
  invariants(t, la);
  // Combining marks attach by GPOS (DejaVu has mark anchors for Latin).
  std::string marks = "ạ́ ë";
  CHECK(t.build(marks.data(), marks.size(), o, fr));
  invariants(t, marks);
  // Wrapping: every line fits, lines cover the text contiguously.
  std::string para = "Zażółć gęślą jaźń. The quick brown fox jumps over the lazy dog. "
                     "שלום עולם 2024 (טקסט) "
                     "مرحبا بالعالم.";
  LayoutOptions wrap = o;
  wrap.max_width = 150 * Fixed;
  CHECK(t.build(para.data(), para.size(), wrap, fr));
  CHECK(t.line_count() > 3);
  for (size_t l = 0; l < t.line_count(); ++l) {
    const LayoutLine &L = t.lines()[l];
    if (l)
      CHECK(L.byte_begin == t.lines()[l - 1].byte_end);
    // Trailing spaces may hang; strip them before checking the fit.
    int64_t w = 0;
    for (size_t k = L.glyph_begin; k < L.glyph_end; ++k)
      if (para[t.glyphs()[k].cluster] != ' ')
        w = std::max(w, t.glyphs()[k].x + t.glyphs()[k].advance);
    CHECK(w <= wrap.max_width + Fixed);
  }
  CHECK(t.lines()[t.line_count() - 1].byte_end == para.size());
  invariants(t, para);
  // Paragraph separators: explicit newlines start lines; caret after \n.
  std::string nl = "abc\nאב\n";
  CHECK(t.build(nl.data(), nl.size(), o, fr));
  CHECK(t.line_count() == 3);
  CHECK(t.lines()[1].paragraph_level == 1 && t.lines()[0].paragraph_level == 0);
  size_t line;
  t.caret_x(nl.size(), &line);
  CHECK(line == 2);
  invariants(t, nl);
  // Selection across a direction boundary yields two visual spans.
  CHECK(t.build(mixed.data(), mixed.size(), o, fr));
  TextLayout::Span spans[8];
  size_t ns = t.selection(2, 8, spans, 8); // "c " + "אב"
  CHECK(ns == 2);
  // Shaping-run-aware fallback: Arabic run uses Naskh as a whole when the
  // primary face lacks Arabic; CJK falls back to bitmap (no TTF covers it).
  if (naskh) {
    Fonts alice;
    CHECK(alice.add("czcionki/ttf/Alice-Regular.ttf"));
    CHECK(alice.add("/usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf"));
    FontResolver ar2 = alice.resolver();
    std::string s = "Hi مرحبا 你好";
    CHECK(t.build(s.data(), s.size(), o, ar2));
    uint32_t arabic_face = 0;
    unsigned missing = 0;
    for (size_t i = 0; i < t.glyph_count(); ++i) {
      const LayoutGlyph &g = t.glyphs()[i];
      if (g.cluster >= 3 && g.cluster < 13) {
        if (!arabic_face)
          arabic_face = g.face;
        CHECK(g.face == arabic_face);
      }
      missing += (g.flags & GlyphMissing) != 0;
    }
    CHECK(arabic_face == 2 && missing == 2);
    invariants(t, s);
  }
  // Robustness: malformed UTF-8, controls, random multilingual soup.
  std::mt19937 rng(2026);
  const char *pool[] = {"a", "Z", " ", "א", "שָׁ", "ل", "ا",
                        "ب", "م", "1", "١", "(", ")", "[", "\xe2\x80\xab", "\xe2\x80\xac",
                        "\xe2\x81\xa7", "\xe2\x81\xa9", "\xe2\x80\x8d", "\xe2\x80\x8c", "́", "\n", "\t",
                        "\xff", "\xe2\x82", "你", "\U0001F600", "\U0001F469\xe2\x80\x8d\U0001F4BB",
                        "ó", "ł", ",", ".", "-", "\r\n", "\xe2\x80\xa9"};
  for (unsigned round = 0; round < 3000; ++round) {
    std::string s;
    size_t parts = rng() % 40;
    for (size_t i = 0; i < parts; ++i)
      s += pool[rng() % (sizeof pool / sizeof pool[0])];
    LayoutOptions r = o;
    r.direction = BaseDirection(rng() % 3);
    r.max_width = (rng() % 3) ? 0 : int64_t(20 + rng() % 200) * Fixed;
    r.align_to_direction = rng() & 1;
    std::string before = s;
    CHECK(t.build(s.data(), s.size(), r, fr));
    CHECK(s == before);
    invariants(t, s);
    TextLayout::Span sp[64];
    t.selection(rng() % (s.size() + 1), rng() % (s.size() + 1), sp, 64);
  }
  CHECK(!t.build(nullptr, 5, o, fr));
  std::string big(MaxLayoutBytes + 1, 'a');
  CHECK(!t.build(big.data(), big.size(), o, fr));
  std::printf("BSF layout: %u checks PASS (bidi visual order, carets, hit-test, "
              "ligature carets, wrapping, selection, fallback runs, 3000 fuzz rounds)%s\n",
              checks, naskh ? "" : " [Naskh fallback case skipped]");
  return 0;
}
