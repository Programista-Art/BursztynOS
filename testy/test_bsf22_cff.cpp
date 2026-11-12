// BSF 2.2 Milestone 1 — OpenType CFF outlines.
//
// Production code under test: bsf/bsf.cpp (sfnt directory, Face::assemble),
// bsf/cff.inc (CFF INDEX/DICT/FDSelect parser, Type 2 interpreter) and the
// shared stb rasterizer path. FreeType is linked ONLY into this host test as
// a reference: every glyph outline of every CFF fixture is compared segment
// by segment in font units, and rasterized coverage is compared at several
// pixel sizes. Malformed inputs are exercised under ASan/UBSan.
#include "bsf22_common.h"
#include <memory>
#include <random>
struct Stats {
  unsigned glyphs{}, outlines_equal{}, raster_glyphs{};
  double raster_error_sum{}, worst{};
};
// Coverage comparison against FreeType's anti-aliased renderer (no hinting).
// Returns sum|a-b| / sum max(a,b) over the union of both bitmaps.
static double raster_error(FT_Face ft, const Face &f, unsigned gid, int px,
                           Cache &cache, bool &empty) {
  empty = false;
  FT_Set_Pixel_Sizes(ft, 0, px);
  if (FT_Load_Glyph(ft, gid, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) ||
      FT_Render_Glyph(ft->glyph, FT_RENDER_MODE_NORMAL))
    return -1;
  const FT_Bitmap &b = ft->glyph->bitmap;
  const Glyph *g = cache.get_glyph(f, gid, px);
  if (!g)
    return -1;
  if (!b.width && !g->width) {
    empty = true;
    return 0;
  }
  int ax = ft->glyph->bitmap_left, ay = -ft->glyph->bitmap_top;
  int x0 = std::min(ax, g->x), y0 = std::min(ay, g->y),
      x1 = std::max(ax + int(b.width), g->x + g->width),
      y1 = std::max(ay + int(b.rows), g->y + g->height);
  double diff = 0, total = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      int fa = 0, ba = 0;
      if (x >= ax && x < ax + int(b.width) && y >= ay && y < ay + int(b.rows))
        fa = b.buffer[(y - ay) * b.pitch + (x - ax)];
      if (x >= g->x && x < g->x + g->width && y >= g->y && y < g->y + g->height)
        ba = g->alpha[(y - g->y) * g->width + (x - g->x)];
      diff += std::abs(fa - ba);
      total += std::max(fa, ba);
    }
  return total ? diff / total : 0;
}
static void compare_font(FT_Library lib, const char *path, OutlineFormat want,
                         unsigned id, Stats &st, unsigned raster_stride) {
  auto bytes = read(path);
  CHECK(!bytes.empty());
  Face f;
  bool opened = f.open(bytes.data(), bytes.size(), id);
  if (!opened)
    std::fprintf(stderr, "cannot open %s (%zu bytes)\n", path, bytes.size());
  CHECK(opened);
  CHECK(f.outline == want);
  FT_Face ft;
  CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), 0, &ft));
  CHECK(unsigned(ft->num_glyphs) == f.glyphs);
  CHECK(ft->units_per_EM == f.units);
  CHECK(f.family[0]);
  CHECK(!std::strcmp(f.family, ft->family_name));
  FT_Outline_Funcs funcs{ft_move, ft_line, ft_conic, ft_cubic, 0, 0};
  unsigned mismatches = 0;
  auto cache = std::make_unique<Cache>();
  double font_error = 0;
  unsigned font_samples = 0;
  for (unsigned gid = 0; gid < f.glyphs; ++gid) {
    Path mine;
    PathSink sink{bsf_segment, &mine};
    CHECK(glyph_path(f, gid, sink));
    CHECK(!FT_Load_Glyph(ft, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING));
    FtCtx ref;
    CHECK(!FT_Outline_Decompose(&ft->glyph->outline, &funcs, &ref));
    // FreeType (NO_SCALE) rounds CFF coordinates to whole font units; BSF
    // keeps 1/8 unit. Advance widths come from hmtx in both engines.
    std::string why;
    ++st.glyphs;
    if (same_path(normalize(mine, .55), normalize(ref.p, .55), 1.01, why))
      ++st.outlines_equal;
    else if (++mismatches <= 5)
      std::fprintf(stderr, "  %s gid %u outline differs: %s\n", path, gid,
                   why.c_str());
    CHECK(unsigned(ft->glyph->metrics.horiAdvance) == f.advance_units(gid));
    if (gid % raster_stride == 0)
      for (int px : {11, 16, 24, 48, 96}) {
        bool empty;
        double e = raster_error(ft, f, gid, px, *cache, empty);
        if (e < 0)
          continue; // glyph larger than the 128 px bitmap limit
        ++st.raster_glyphs;
        st.raster_error_sum += e;
        font_error += e;
        ++font_samples;
        if (e > st.worst)
          st.worst = e;
      }
  }
  std::printf("  %-48s %5u glyphs, outline mismatches %u, raster mean error "
              "%.4f over %u glyph-sizes\n",
              path, f.glyphs, mismatches,
              font_samples ? font_error / font_samples : 0.0, font_samples);
  CHECK(mismatches == 0);
  FT_Done_Face(ft);
}
// Build a minimal OTTO font around a synthetic CFF table (for adversarial
// charstrings), reusing hmtx/cmap/etc. from a real font.
static std::vector<uint8_t> replace_table(const std::vector<uint8_t> &src,
                                          const char *tag,
                                          const std::vector<uint8_t> &table) {
  Bytes b{src.data(), src.size()};
  unsigned n = b.u16(4);
  std::vector<std::pair<std::string, std::vector<uint8_t>>> tables;
  for (unsigned i = 0; i < n; ++i) {
    unsigned o = 12 + i * 16;
    std::string t(reinterpret_cast<const char *>(src.data() + o), 4);
    uint32_t off = b.u32(o + 8), len = b.u32(o + 12);
    std::vector<uint8_t> data(src.begin() + off, src.begin() + off + len);
    if (t == tag)
      data = table;
    tables.push_back({t, data});
  }
  std::vector<uint8_t> out(12 + 16 * n);
  std::memcpy(out.data(), src.data(), 12);
  for (unsigned i = 0; i < n; ++i) {
    while (out.size() % 4)
      out.push_back(0);
    size_t off = out.size();
    out.insert(out.end(), tables[i].second.begin(), tables[i].second.end());
    unsigned o = 12 + i * 16;
    std::memcpy(out.data() + o, tables[i].first.data(), 4);
    auto put32 = [&](size_t at, uint32_t v) {
      out[at] = uint8_t(v >> 24);
      out[at + 1] = uint8_t(v >> 16);
      out[at + 2] = uint8_t(v >> 8);
      out[at + 3] = uint8_t(v);
    };
    put32(o + 4, 0);
    put32(o + 8, uint32_t(off));
    put32(o + 12, uint32_t(tables[i].second.size()));
  }
  return out;
}
static std::vector<uint8_t> table_bytes(const std::vector<uint8_t> &src,
                                        const char *tag) {
  Bytes b{src.data(), src.size()};
  for (unsigned i = 0; i < b.u16(4); ++i) {
    unsigned o = 12 + i * 16;
    if (!std::memcmp(src.data() + o, tag, 4))
      return {src.begin() + b.u32(o + 8),
              src.begin() + b.u32(o + 8) + b.u32(o + 12)};
  }
  return {};
}
// CFF writer for synthetic fonts: Header, Name, TopDICT, String, GSubr,
// CharStrings, Private (+Subrs). Every glyph uses the given charstrings.
static void put_index(std::vector<uint8_t> &o,
                      const std::vector<std::vector<uint8_t>> &items) {
  o.push_back(uint8_t(items.size() >> 8));
  o.push_back(uint8_t(items.size()));
  if (items.empty())
    return;
  o.push_back(4);
  uint32_t off = 1;
  auto p32 = [&](uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
      o.push_back(uint8_t(v >> s));
  };
  p32(off);
  for (auto &i : items) {
    off += uint32_t(i.size());
    p32(off);
  }
  for (auto &i : items)
    o.insert(o.end(), i.begin(), i.end());
}
static void dict_int(std::vector<uint8_t> &d, int32_t v) {
  d.push_back(29);
  for (int s = 24; s >= 0; s -= 8)
    d.push_back(uint8_t(uint32_t(v) >> s));
}
static std::vector<uint8_t> synthetic_cff(unsigned glyphs,
                                          const std::vector<uint8_t> &charstring,
                                          const std::vector<std::vector<uint8_t>> &subrs,
                                          const std::vector<std::vector<uint8_t>> &gsubrs) {
  // Two passes: offsets depend on the Top DICT size, which uses fixed
  // 5-byte integers, so one layout pass suffices.
  std::vector<uint8_t> header{1, 0, 4, 4};
  std::vector<uint8_t> names;
  put_index(names, {{'T', 'e', 's', 't'}});
  std::vector<uint8_t> strings;
  put_index(strings, {});
  std::vector<uint8_t> gsub;
  put_index(gsub, gsubrs);
  std::vector<std::vector<uint8_t>> cs(glyphs, charstring);
  cs[0] = {14}; // .notdef: endchar
  std::vector<uint8_t> chars;
  put_index(chars, cs);
  std::vector<uint8_t> priv;
  if (!subrs.empty()) {
    dict_int(priv, 0); // patched below
    priv.push_back(19);
  }
  std::vector<uint8_t> local;
  if (!subrs.empty())
    put_index(local, subrs);
  // Top DICT: CharStrings(17) Private(18).
  auto top_for = [&](uint32_t charstrings_at, uint32_t private_at) {
    std::vector<uint8_t> t;
    dict_int(t, int32_t(charstrings_at));
    t.push_back(17);
    dict_int(t, int32_t(priv.size()));
    dict_int(t, int32_t(private_at));
    t.push_back(18);
    return t;
  };
  std::vector<uint8_t> top = top_for(0, 0), tops;
  put_index(tops, {top});
  uint32_t charstrings_at = uint32_t(header.size() + names.size() + tops.size() +
                                     strings.size() + gsub.size());
  uint32_t private_at = uint32_t(charstrings_at + chars.size());
  if (!subrs.empty()) {
    priv.clear();
    dict_int(priv, int32_t(5 + 1)); // Subrs right after Private
    priv.push_back(19);
  }
  tops.clear();
  put_index(tops, {top_for(charstrings_at, private_at)});
  std::vector<uint8_t> out = header;
  for (auto *p : {&names, &tops, &strings, &gsub, &chars, &priv, &local})
    out.insert(out.end(), p->begin(), p->end());
  return out;
}
static std::vector<uint8_t> num(int v) {
  if (v >= -107 && v <= 107)
    return {uint8_t(v + 139)};
  return {28, uint8_t(uint16_t(v) >> 8), uint8_t(v)};
}
static std::vector<uint8_t> cat(std::initializer_list<std::vector<uint8_t>> parts) {
  std::vector<uint8_t> o;
  for (auto &p : parts)
    o.insert(o.end(), p.begin(), p.end());
  return o;
}
static void adversarial(const std::vector<uint8_t> &base) {
  Bytes b{base.data(), base.size()};
  // Glyph count of the donor font (synthetic CFF must match maxp).
  Face donor;
  CHECK(donor.open(base.data(), base.size(), 1));
  unsigned glyphs = donor.glyphs;
  auto cache = std::make_unique<Cache>();
  auto build = [&](const std::vector<uint8_t> &cs,
                   const std::vector<std::vector<uint8_t>> &subrs,
                   const std::vector<std::vector<uint8_t>> &gsubrs) {
    return replace_table(base, "CFF ", synthetic_cff(glyphs, cs, subrs, gsubrs));
  };
  // 1. A valid synthetic square renders (sanity check of the writer).
  {
    auto font = build(cat({num(100), num(0), {21}, num(500), {6}, num(500),
                           {7}, num(-500), {6}, {14}}),
                      {}, {});
    Face f;
    CHECK(f.open(font.data(), font.size(), 900));
    CHECK(f.outline == OutlineFormat::CFF);
    Path p;
    CHECK(glyph_path(f, 1, PathSink{bsf_segment, &p}));
    CHECK(normalize(p, .1).size() == 1);
    CHECK(cache->get_glyph(f, 1, 32));
  }
  // 2. Recursive local subroutine (subr 0 calls itself): depth limit.
  {
    auto font = build(cat({num(0 - 107), {10}, {14}}),
                      {cat({num(0 - 107), {10}, {11}})}, {});
    Face f;
    CHECK(f.open(font.data(), font.size(), 901));
    Path p;
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p}));
    CHECK(!cache->get_glyph(f, 1, 32));
  }
  // 3. Mutual recursion through global and local subroutines.
  {
    auto font = build(cat({num(-107), {29}, {14}}),
                      {cat({num(-107), {29}, {11}})},
                      {cat({num(-107), {10}, {11}})});
    Face f;
    CHECK(f.open(font.data(), font.size(), 902));
    Path p;
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p}));
  }
  // 4. Operand stack overflow (49 operands in CFF 1.0).
  {
    std::vector<uint8_t> cs;
    for (int i = 0; i < 49; ++i)
      cs.push_back(139);
    cs.push_back(14);
    auto font = build(cs, {}, {});
    Face f;
    CHECK(f.open(font.data(), font.size(), 903));
    Path p;
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p}));
  }
  // 5. Out-of-range subroutine index and call with an empty subr INDEX.
  {
    auto font = build(cat({num(50), {10}, {14}}), {cat({{11}})}, {});
    Face f;
    CHECK(f.open(font.data(), font.size(), 904));
    Path p;
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p}));
    auto font2 = build(cat({num(0), {29}, {14}}), {}, {});
    CHECK(f.open(font2.data(), font2.size(), 905));
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p}));
  }
  // 6. Operation budget: a subroutine called many times (bounded work),
  //    and a charstring without endchar.
  {
    std::vector<uint8_t> loop;
    for (int i = 0; i < 4000; ++i)
      loop.insert(loop.end(), {139, 139, 5}); // rlineto 0 0
    loop.push_back(11);
    std::vector<uint8_t> cs = cat({num(0), num(0), {21}});
    for (int i = 0; i < 60; ++i)
      cs.insert(cs.end(), {uint8_t(139 - 107), 10});
    cs.push_back(14);
    auto font = build(cs, {loop}, {});
    Face f;
    CHECK(f.open(font.data(), font.size(), 906));
    Path p;
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p})); // vertex/op limit
    auto font2 = build(cat({num(0), num(0), {21}, num(10), num(10), {5}}), {}, {});
    CHECK(f.open(font2.data(), font2.size(), 907));
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p}));
  }
  // 7. Reserved operators, hintmask past the end, bad arithmetic.
  for (auto cs : {std::vector<uint8_t>{0, 14}, std::vector<uint8_t>{2, 14},
                  std::vector<uint8_t>{9, 14}, std::vector<uint8_t>{13, 14},
                  std::vector<uint8_t>{17, 14}, cat({num(1), num(2), {1}, {19}}),
                  cat({num(1), num(0), {12, 12}, {14}}),
                  cat({num(40), {12, 21}, {14}}), cat({{12}}), cat({{255, 0, 0}}),
                  cat({num(1), num(2), num(3), {12, 30}, {14}})}) {
    auto font = build(cs, {}, {});
    Face f;
    CHECK(f.open(font.data(), font.size(), 908));
    Path p;
    CHECK(!glyph_path(f, 1, PathSink{bsf_segment, &p}));
    CHECK(!cache->get_glyph(f, 1, 24));
  }
  // 8. Malformed structures must be rejected at open time.
  auto cff = table_bytes(base, "CFF ");
  CHECK(!cff.empty());
  {
    auto bad = cff;
    bad[0] = 2; // wrong major version for a CFF table
    auto font = replace_table(base, "CFF ", bad);
    Face f;
    CHECK(!f.open(font.data(), font.size(), 910));
  }
  {
    auto bad = cff;
    bad[2] = 200; // header size past the table
    auto font = replace_table(base, "CFF ", std::vector<uint8_t>(bad.begin(), bad.begin() + 100));
    Face f;
    CHECK(!f.open(font.data(), font.size(), 911));
  }
  for (size_t cut : {size_t(3), size_t(10), size_t(40), cff.size() / 2, cff.size() - 1}) {
    auto font = replace_table(base, "CFF ", std::vector<uint8_t>(cff.begin(), cff.begin() + cut));
    Face f;
    CHECK(!f.open(font.data(), font.size(), 912)); // truncated INDEX data
  }
  {
    // Name INDEX offSize 0 and 5.
    for (uint8_t bad_size : {uint8_t(0), uint8_t(5)}) {
      auto bad = cff;
      bad[bad[2] + 2] = bad_size;
      auto font = replace_table(base, "CFF ", bad);
      Face f;
      CHECK(!f.open(font.data(), font.size(), 913));
    }
  }
  // 9. Random mutation corpus (bounded; ASan/UBSan catch memory errors).
  std::mt19937 rng(2202);
  unsigned opened = 0, rendered = 0;
  for (unsigned iter = 0; iter < 3000; ++iter) {
    auto bad = cff;
    unsigned edits = 1 + rng() % 16;
    for (unsigned e = 0; e < edits; ++e) {
      size_t at = rng() % bad.size();
      // Bias mutations towards the header/INDEX area and charstring bytes.
      if (rng() % 2)
        at = rng() % std::min<size_t>(bad.size(), 4096);
      bad[at] = uint8_t(rng());
    }
    auto font = replace_table(base, "CFF ", bad);
    Face f;
    if (!f.open(font.data(), font.size(), 1000 + iter))
      continue;
    ++opened;
    for (unsigned k = 0; k < 8; ++k) {
      unsigned gid = rng() % f.glyphs;
      Path p;
      glyph_path(f, gid, PathSink{bsf_segment, &p});
      if (cache->get_glyph(f, gid, 8 + rng() % 120))
        ++rendered;
    }
  }
  std::printf("  mutation corpus: 3000 variants, %u opened, %u glyphs rendered\n",
              opened, rendered);
  CHECK(checks > 0);
}
int main() {
  FT_Library lib;
  CHECK(!FT_Init_FreeType(&lib));
  const std::string dir = "testy/fixtures/bsf22/fonts/";
  Stats st;
  compare_font(lib, (dir + "NotoSans-Regular.otf").c_str(), OutlineFormat::CFF, 1, st, 7);
  compare_font(lib, (dir + "NotoSans-Bold.otf").c_str(), OutlineFormat::CFF, 2, st, 13);
  compare_font(lib, (dir + "NotoNaskhArabic-Regular.otf").c_str(), OutlineFormat::CFF, 3, st, 5);
  compare_font(lib, (dir + "NotoSansHebrew-Regular.otf").c_str(), OutlineFormat::CFF, 4, st, 1);
  const char *cjk = "testy/fixtures/bsf22/fonts/NotoSansCJK-subset.otf";
  if (!read(cjk).empty())
    compare_font(lib, cjk, OutlineFormat::CFF, 5, st, 3);
  // Existing TrueType fonts keep their exact outlines (quadratic path).
  compare_font(lib, "czcionki/ttf/DejaVuSans.ttf", OutlineFormat::TrueType, 6, st, 11);
  double mean = st.raster_glyphs ? st.raster_error_sum / st.raster_glyphs : 0;
  std::printf("  outlines equal: %u/%u; raster compared %u glyph-sizes, mean "
              "coverage error %.4f, worst %.4f\n",
              st.outlines_equal, st.glyphs, st.raster_glyphs, mean, st.worst);
  CHECK(st.outlines_equal == st.glyphs);
  CHECK(mean < 0.06);
  adversarial(read(dir + "NotoSansHebrew-Regular.otf"));
  FT_Done_FreeType(lib);
  std::printf("BSF 2.2 CFF: %lu checks PASS\n", checks);
}
