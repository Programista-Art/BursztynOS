// BSF 2.2 micro-benchmarks (host, -O2, no sanitizers). Production code only.
#include "../bsf/bsf.h"
#include "../bsf/shape.h"
#include "../bsf/webfont.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
using namespace BSF;
using Clock = std::chrono::steady_clock;
static std::vector<uint8_t> read(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
static double ns_since(Clock::time_point t) {
  return double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count());
}
static void raster(const char *label, Face &f, int px) {
  std::vector<uint8_t> mask(MaxBitmap * MaxBitmap);
  unsigned n = 0;
  auto t = Clock::now();
  for (unsigned rep = 0; rep < 3; ++rep)
    for (uint32_t cp = 0x21; cp < 0x17f; ++cp) {
      unsigned g = f.glyph(cp);
      if (!g) continue;
      Glyph out;
      if (rasterize(f, g, px, out, mask.data(), mask.size())) ++n;
    }
  std::printf("  raster %-34s %3d px: %8.0f ns/glyph (%u glyphs)\n", label, px, ns_since(t) / n, n);
}
static void open_face(const char *label, const std::vector<uint8_t> &b) {
  auto t = Clock::now();
  for (unsigned i = 0; i < 200; ++i) {
    Face f;
    f.open(b.data(), b.size(), 1);
  }
  std::printf("  open   %-34s %8.0f us/open (%zu B)\n", label, ns_since(t) / 200 / 1000, b.size());
}
int main() {
  const std::string fx = "testy/fixtures/bsf22/fonts/", web = "testy/fixtures/bsf22/web/", col = "testy/fixtures/bsf22/color/";
  auto dejavu = read("czcionki/ttf/DejaVuSans.ttf"), noto_otf = read(fx + "NotoSans-Regular.otf"),
       noto_ttf = read(web + "NotoSans-Latin.ttf"), vf = read(fx + "NotoSansHebrew-VF.ttf"),
       cff2 = read(fx + "SourceSerif4Variable-Roman.otf");
  Face tt, cff, nt, var, c2;
  tt.open(dejavu.data(), dejavu.size(), 1);
  cff.open(noto_otf.data(), noto_otf.size(), 2);
  nt.open(noto_ttf.data(), noto_ttf.size(), 3);
  var.open(vf.data(), vf.size(), 4);
  c2.open(cff2.data(), cff2.size(), 5);
  for (int px : {16, 48}) {
    raster("TrueType DejaVu Sans", tt, px);
    raster("TrueType Noto Sans (Latin subset)", nt, px);
    raster("CFF Noto Sans OTF", cff, px);
    raster("CFF2 Source Serif 4 (default)", c2, px);
    Face w = c2;
    VariationSetting s{0x77676874, 800};
    set_variations(w, &s, 1);
    raster("CFF2 Source Serif 4 wght=800", w, px);
    Face hv = var;
    VariationSetting h{0x77676874, 700};
    set_variations(hv, &h, 1);
    raster("TrueType Hebrew VF default", var, px);
    raster("TrueType Hebrew VF wght=700 (gvar)", hv, px);
  }
  open_face("TrueType DejaVu Sans", dejavu);
  open_face("CFF Noto Sans OTF", noto_otf);
  open_face("CFF2 Source Serif 4", cff2);
  auto cjk = read(fx + "NotoSansCJK-subset.ttc");
  open_face("OTC CJK subset (face 0)", cjk);
  for (const char *w : {"NotoSans-Latin.woff", "NotoSans-Latin.woff2", "NotoSans-CFF.woff", "NotoSans-CFF.woff2",
                        "NotoNaskhArabic.woff2"}) {
    auto b = read(web + w);
    auto t = Clock::now();
    unsigned reps = 20;
    for (unsigned i = 0; i < reps; ++i) {
      DecodedWebFont out;
      decode_web_font(b.data(), b.size(), out);
      delete[] out.bytes;
    }
    std::printf("  decode %-34s %8.2f ms (%zu B input)\n", w, ns_since(t) / reps / 1e6, b.size());
  }
  std::vector<uint32_t> argb(MaxBitmap * MaxBitmap);
  for (const char *c : {"NotoCOLRv1-subset.ttf", "NotoColorEmoji-subset.ttf", "BSF-COLRv0-test.ttf"}) {
    auto b = read(col + c);
    Face f;
    f.open(b.data(), b.size(), 9);
    for (int px : {20, 64}) {
      unsigned n = 0;
      auto t = Clock::now();
      for (unsigned g = 0; g < f.glyphs; ++g) {
        if (color_glyph_format(f, g) == ColorFormatNone) continue;
        ColorGlyph out;
        if (render_color_glyph(f, g, px, 0xff000000u, 0, out, argb.data(), argb.size())) ++n;
      }
      if (n) std::printf("  colour %-34s %3d px: %8.0f us/glyph (%u glyphs)\n", c, px, ns_since(t) / n / 1000, n);
    }
  }
  // Shaping: TrueType vs CFF (same Noto Sans design, Latin text).
  const char *text = "Zażółć gęślą jaźń. The quick brown fox jumps over the lazy dog office affinity";
  std::vector<uint32_t> cps, cl;
  for (size_t at = 0; text[at];) {
    uint32_t cp;
    size_t k = utf8(text + at, 1000, cp);
    cps.push_back(cp);
    cl.push_back(uint32_t(at));
    at += k;
  }
  for (Face *f : {&nt, &cff}) {
    OT::Layout L;
    L.open(*f);
    OT::Buffer buf;
    ShapeOptions o;
    auto t = Clock::now();
    for (unsigned i = 0; i < 2000; ++i)
      shape(*f, L, cps.data(), cl.data(), cps.size(), o, buf);
    std::printf("  shape  %-34s %8.0f ns/run (%zu scalars)\n", f == &cff ? "CFF Noto Sans OTF" : "TrueType Noto Sans",
                ns_since(t) / 2000, cps.size());
  }
}
