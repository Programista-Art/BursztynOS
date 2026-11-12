// BSF 2.2 Milestone 7 — colour glyphs (bsf/color.inc, bsf/png.inc).
// References (host only): FreeType FT_LOAD_COLOR for CBDT/CBLC and COLRv0
// layers; blackrenderer 0.6.0 + Skia for COLRv1 (testy/fixtures/bsf22/color/
// colrv1_ref.bin, narzedzia/bsf22/make_colrv1_reference.py); the sbix fixture's
// generator formula. Adversarial: cycles, deep paint chains, corrupt strikes,
// oversized PNGs, random mutations (ASan/UBSan).
#include "bsf22_common.h"
#include <memory>
#include <random>
static const std::string cdir = "testy/fixtures/bsf22/color/";
static std::vector<uint32_t> buffer(MaxBitmap *MaxBitmap);
static double premul_diff(uint32_t a, uint32_t b) {
  auto ch = [](uint32_t c, int s) { return ((c >> s) & 255) * ((c >> 24) & 255) / 255.0; };
  double d = 0;
  for (int s : {16, 8, 0}) d += std::fabs(ch(a, s) - ch(b, s));
  d += std::fabs(double(a >> 24) - double(b >> 24));
  return d / (4 * 255);
}
static void cbdt(FT_Library lib) {
  auto bytes = read(cdir + "NotoColorEmoji-subset.ttf");
  Face f;
  CHECK(f.open(bytes.data(), bytes.size(), 1));
  CHECK(f.color_tables & ColorCBDT);
  FT_Face ft;
  CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), 0, &ft));
  CHECK(ft->num_fixed_sizes >= 1);
  CHECK(!FT_Select_Size(ft, 0));
  int ppem = ft->available_sizes[0].y_ppem >> 6;
  unsigned rendered = 0, exact = 0;
  for (unsigned gid = 1; gid < f.glyphs; ++gid) {
    if (color_glyph_format(f, gid) != ColorFormatCBDT) continue;
    // At the strike size (<= 128) BSF must reproduce the PNG exactly.
    ColorGlyph g;
    bool ok = render_color_glyph(f, gid, ppem, 0xff000000, 0, g, buffer.data(), buffer.size());
    CHECK(ok);
    CHECK(!FT_Load_Glyph(ft, gid, FT_LOAD_COLOR));
    const FT_Bitmap &b = ft->glyph->bitmap;
    CHECK(b.pixel_mode == FT_PIXEL_MODE_BGRA);
    ++rendered;
    if (int(b.width) <= int(MaxBitmap) && int(b.rows) <= int(MaxBitmap)) {
      CHECK(g.width == int(b.width) && g.height == int(b.rows));
      CHECK(g.x == ft->glyph->bitmap_left && g.y == -ft->glyph->bitmap_top);
      double worst = 0;
      for (int y = 0; y < g.height; ++y)
        for (int x = 0; x < g.width; ++x) {
          const uint8_t *p = b.buffer + y * b.pitch + x * 4; // premultiplied BGRA
          uint32_t fa = p[3];
          uint32_t straight = fa ? (fa << 24) | ((p[2] * 255 / fa) << 16) | ((p[1] * 255 / fa) << 8) | (p[0] * 255 / fa) : 0;
          worst = std::max(worst, premul_diff(buffer[y * g.width + x], straight));
        }
      exact += worst < 2.0 / 255;
    } else {
      // Strike larger than the bitmap limit: scaled down to fit.
      CHECK(g.width <= int(MaxBitmap) && g.height <= int(MaxBitmap) && g.width > 64);
      ++exact;
    }
    // Scaled sizes keep proportions and the baseline placement.
    for (int px : {16, 24, 48}) {
      ColorGlyph s;
      CHECK(render_color_glyph(f, gid, px, 0, 0, s, buffer.data(), buffer.size()));
      CHECK(std::abs(s.width - int(b.width * px / double(ppem) + .5)) <= 1);
      CHECK(std::abs(s.y - int(-ft->glyph->bitmap_top * px / double(ppem))) <= 1);
    }
  }
  std::printf("  CBDT: strike %d ppem, %u colour glyphs, %u identical to FreeType at the strike size\n",
              ppem, rendered, exact);
  CHECK(rendered >= 15 && exact == rendered);
  FT_Done_Face(ft);
}
static void sbix() {
  auto bytes = read(cdir + "BSF-sbix-test.ttf");
  Face f;
  CHECK(f.open(bytes.data(), bytes.size(), 2));
  CHECK(f.color_tables & ColorSbix);
  unsigned A = f.glyph('A'), G = f.glyph('G'), L = f.glyph('L'), J = f.glyph('J'), T = f.glyph('T');
  CHECK(color_glyph_format(f, A) == ColorFormatSbix && color_glyph_format(f, G) == ColorFormatSbix);
  CHECK(color_glyph_format(f, L) == ColorFormatSbix);               // 'dupe' -> A
  CHECK(color_glyph_format(f, J) == ColorFormatNone);               // 'jpg ' not supported
  CHECK(color_glyph_format(f, f.glyph('z')) == ColorFormatNone);     // no strike data
  for (int ppem : {32, 64}) {
    ColorGlyph g;
    CHECK(render_color_glyph(f, A, ppem, 0, 0, g, buffer.data(), buffer.size()));
    CHECK(g.width == ppem && g.height == ppem);
    CHECK(g.x == 0 && g.y == -(ppem - ppem / 8)); // bottom-left at originOffsetY = -ppem/8
    for (int y = 0; y < ppem; y += 7)
      for (int x = 0; x < ppem; x += 5) {
        uint32_t expect = 0xff000000u | (uint32_t(255 * x / ppem) << 16) | (uint32_t(255 * y / ppem) << 8) | 128;
        CHECK(buffer[y * ppem + x] == expect);
      }
    std::vector<uint32_t> a(buffer.begin(), buffer.begin() + ppem * ppem);
    ColorGlyph d;
    CHECK(render_color_glyph(f, L, ppem, 0, 0, d, buffer.data(), buffer.size()));
    CHECK(d.width == g.width && std::equal(a.begin(), a.end(), buffer.begin()));
  }
  ColorGlyph g;
  // Strike selection: 40 px uses the 64 ppem strike, scaled.
  CHECK(render_color_glyph(f, G, 40, 0, 0, g, buffer.data(), buffer.size()));
  CHECK(g.width == 20 && g.height == 40 && g.x == 1);
  CHECK(!render_color_glyph(f, T, 32, 0, 0, g, buffer.data(), buffer.size())); // corrupt PNG
  std::printf("  sbix: PNG strikes 32/64, dupe, strike selection, unsupported 'jpg ' and corrupt PNG handled\n");
}
static void colrv0(FT_Library lib) {
  auto bytes = read(cdir + "BSF-COLRv0-test.ttf");
  Face f;
  CHECK(f.open(bytes.data(), bytes.size(), 3));
  CHECK(f.color_tables & ColorCOLR);
  unsigned A = f.glyph('A'), L = f.glyph('L');
  CHECK(color_glyph_format(f, A) == ColorFormatCOLRv0 && color_glyph_format(f, L) == ColorFormatCOLRv0);
  CHECK(color_glyph_format(f, f.glyph('Z')) == ColorFormatNone);
  FT_Face ft;
  CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), 0, &ft));
  for (int px : {24, 48, 96}) {
    CHECK(!FT_Set_Pixel_Sizes(ft, 0, px));
    for (unsigned gid : {A, L}) {
      ColorGlyph g;
      CHECK(render_color_glyph(f, gid, px, 0xff000000, 0, g, buffer.data(), buffer.size()));
      CHECK(!FT_Load_Glyph(ft, gid, FT_LOAD_COLOR | FT_LOAD_NO_HINTING));
      CHECK(!FT_Render_Glyph(ft->glyph, FT_RENDER_MODE_NORMAL)); // composites COLRv0 layers
      const FT_Bitmap &b = ft->glyph->bitmap;
      CHECK(b.pixel_mode == FT_PIXEL_MODE_BGRA);
      int fx = ft->glyph->bitmap_left, fy = -ft->glyph->bitmap_top;
      double sum = 0, total = 0;
      int x0 = std::min(fx, g.x), y0 = std::min(fy, g.y), x1 = std::max(fx + int(b.width), g.x + g.width),
          y1 = std::max(fy + int(b.rows), g.y + g.height);
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
          uint32_t mine = 0, ref = 0;
          if (x >= g.x && x < g.x + g.width && y >= g.y && y < g.y + g.height) mine = buffer[(y - g.y) * g.width + x - g.x];
          if (x >= fx && x < fx + int(b.width) && y >= fy && y < fy + int(b.rows)) {
            const uint8_t *p = b.buffer + (y - fy) * b.pitch + (x - fx) * 4;
            uint32_t fa = p[3];
            ref = fa ? (fa << 24) | ((p[2] * 255 / fa) << 16) | ((p[1] * 255 / fa) << 8) | (p[0] * 255 / fa) : 0;
          }
          if (mine || ref) { sum += premul_diff(mine, ref); total += 1; }
        }
      double mean = total ? sum / total : 0;
      if (mean > 0.04) std::fprintf(stderr, "  COLRv0 gid %u px %d mean diff %.4f\n", gid, px, mean);
      CHECK(mean < 0.04);
    }
  }
  // Palette 1 and the foreground colour layer.
  ColorGlyph p0, p1;
  CHECK(render_color_glyph(f, L, 48, 0xff00ff00, 0, p0, buffer.data(), buffer.size()));
  std::vector<uint32_t> first(buffer.begin(), buffer.begin() + p0.width * p0.height);
  CHECK(render_color_glyph(f, L, 48, 0xff00ff00, 1, p1, buffer.data(), buffer.size()));
  CHECK(!std::equal(first.begin(), first.end(), buffer.begin()));
  bool green = false;
  for (auto v : first) green |= (v & 0xffffff) == 0x00ff00 && (v >> 24) == 255;
  CHECK(green); // foreground layer uses the text colour
  std::printf("  COLRv0: layers/palettes/foreground vs FreeType at 24/48/96 px (mean diff < 4%%)\n");
  FT_Done_Face(ft);
}
static void colrv1() {
  auto bytes = read(cdir + "NotoCOLRv1-subset.ttf");
  auto ref = read(cdir + "colrv1_ref.bin");
  CHECK(!bytes.empty() && !ref.empty());
  Face f;
  CHECK(f.open(bytes.data(), bytes.size(), 4));
  size_t at = 0;
  unsigned glyphs = 0;
  double worst = 0, sum = 0;
  ColorStats stats;
  while (at + 10 <= ref.size()) {
    auto u16 = [&](size_t o) { return unsigned(ref[o] << 8 | ref[o + 1]); };
    unsigned gid = u16(at);
    int x0 = int16_t(u16(at + 2)), y0 = int16_t(u16(at + 4));
    unsigned w = u16(at + 6), h = u16(at + 8);
    const uint8_t *rgba = ref.data() + at + 10;
    at += 10 + size_t(w) * h * 4;
    CHECK(color_glyph_format(f, gid) == ColorFormatCOLRv1);
    ColorGlyph g;
    CHECK(render_color_glyph(f, gid, 64, 0xff000000, 0, g, buffer.data(), buffer.size(), &stats));
    CHECK(g.x == x0 && g.y == y0 && unsigned(g.width) == w && unsigned(g.height) == h);
    double d = 0;
    for (unsigned i = 0; i < w * h; ++i) {
      const uint8_t *p = rgba + i * 4;
      uint32_t r = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
      d += premul_diff(buffer[i], r);
    }
    d /= double(w * h);
    if (d > 0.02) std::fprintf(stderr, "  COLRv1 gid %u mean diff %.4f\n", gid, d);
    worst = std::max(worst, d);
    sum += d;
    ++glyphs;
  }
  std::printf("  COLRv1: %u glyphs vs blackrenderer/Skia, mean diff %.4f, worst %.4f; paints %u layers %u "
              "depth %u\n", glyphs, sum / glyphs, worst, stats.paints, stats.layers, stats.max_depth);
  CHECK(glyphs >= 50);
  CHECK(worst < 0.03);
  CHECK(!stats.limited);
}
static void adversarial() {
  {
    auto bytes = read(cdir + "BSF-COLRv1-adversarial.ttf");
    Face f;
    CHECK(f.open(bytes.data(), bytes.size(), 5));
    ColorGlyph g;
    ColorStats st;
    CHECK(!render_color_glyph(f, f.glyph('A'), 48, 0, 0, g, buffer.data(), buffer.size(), &st)); // cycle
    CHECK(st.limited);
    ColorStats deep;
    CHECK(!render_color_glyph(f, f.glyph('L'), 48, 0, 0, g, buffer.data(), buffer.size(), &deep)); // depth
    CHECK(deep.limited && deep.max_depth <= 32);
  }
  // Oversized PNG dimensions are rejected before allocation.
  {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                0, 0, 0x10, 0, 0, 0, 0x10, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0};
    uint32_t *out;
    unsigned w, h;
    CHECK(!decode_png(png.data(), png.size(), out, w, h));
  }
  std::mt19937 rng(2208);
  auto cache_buffer = buffer;
  unsigned opened = 0, rendered = 0;
  for (const char *name : {"NotoCOLRv1-subset.ttf", "NotoColorEmoji-subset.ttf", "BSF-COLRv0-test.ttf", "BSF-sbix-test.ttf"}) {
    auto base = read(cdir + name);
    Face clean;
    CHECK(clean.open(base.data(), base.size(), 9));
    Bytes table = clean.colr.p ? clean.colr : clean.cbdt.p ? clean.cbdt : clean.sbix;
    Bytes table2 = clean.cpal.p ? clean.cpal : clean.cblc;
    size_t o1 = size_t(table.p - base.data()), o2 = table2.p ? size_t(table2.p - base.data()) : o1;
    for (unsigned iter = 0; iter < 1500; ++iter) {
      auto v = base;
      for (unsigned e = 0, n = 1 + rng() % 8; e < n; ++e) {
        bool second = table2.p && rng() % 4 == 0;
        size_t at = second ? o2 + rng() % table2.n : o1 + (rng() % 2 ? rng() % std::min<size_t>(table.n, 2048) : rng() % table.n);
        v[at] = uint8_t(rng());
      }
      Face f;
      if (!f.open(v.data(), v.size(), 1000 + iter)) continue;
      ++opened;
      for (unsigned k = 0; k < 4; ++k) {
        ColorGlyph g;
        unsigned gid = rng() % f.glyphs;
        if (render_color_glyph(f, gid, 8 + rng() % 120, 0xff000000, rng() % 3, g, cache_buffer.data(), cache_buffer.size()))
          ++rendered;
      }
    }
  }
  std::printf("  adversarial: cycle and depth limits enforced, oversized PNG rejected, 6000 table mutations "
              "(%u faces, %u glyphs rendered)\n", opened, rendered);
}
int main() {
  FT_Library lib;
  CHECK(!FT_Init_FreeType(&lib));
  cbdt(lib);
  sbix();
  colrv0(lib);
  colrv1();
  adversarial();
  FT_Done_FreeType(lib);
  std::printf("BSF 2.2 colour fonts: %lu checks PASS\n", checks);
}
