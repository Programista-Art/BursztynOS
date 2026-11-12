// BSF 2.2 coverage-guided fuzzers (ASan + UBSan + trace-pc, fuzz_engine.h).
// Build with -DBSF22_TARGET=1 (font: sfnt/TTC/OTC, CFF/CFF2, gvar/fvar,
// COLR/CPAL/CBDT/sbix), 2 (WOFF/WOFF2 decode, then the font path) or 3 (PNG).
#include "fuzz_engine.h"
#include "../../bsf/bsf.h"
#if BSF22_TARGET == 2
#include "../../bsf/webfont.h"
#endif
#include <fstream>
#include <iterator>
#include <memory>
static std::vector<uint8_t> file(const char *p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
static BSF::Cache *cache;
static uint32_t *argb;
static void exercise(const uint8_t *data, size_t size, uint64_t salt) {
  BSF::Bytes b{data, size};
  unsigned faces = BSF::collection_faces(b);
  for (unsigned i = 0; i < faces && i < 4; ++i) {
    BSF::Face f;
    if (!f.open_index(data, size, i, uint32_t(1 + i + salt)))
      continue;
    // A few glyphs at a few sizes through every path.
    for (unsigned k = 0; k < 6 && f.glyphs; ++k) {
      unsigned gid = unsigned((salt * 2654435761u + k * 40503u) % f.glyphs);
      BSF::PathSink sink{[](void *, int, double, double, double, double, double, double) {}, nullptr};
      BSF::glyph_path(f, gid, sink);
      cache->get_glyph(f, gid, 8 + int((salt + k * 37) % 100));
      f.advance_units(gid);
      BSF::ColorGlyph g;
      BSF::render_color_glyph(f, gid, 16 + int(k * 17 % 100), 0xff000000u, unsigned(k & 1), g, argb,
                              size_t(BSF::MaxBitmap) * BSF::MaxBitmap);
    }
    BSF::VariationAxis axes[BSF::MaxAxes];
    unsigned n = BSF::variation_axes(f, axes, BSF::MaxAxes);
    if (n) {
      BSF::VariationSetting s[BSF::MaxAxes];
      for (unsigned a = 0; a < n; ++a)
        s[a] = {axes[a].tag, double(axes[a].min) + (double(axes[a].max) - axes[a].min) * ((salt >> a) & 7) / 7.0};
      if (BSF::set_variations(f, s, n))
        for (unsigned k = 0; k < 4 && f.glyphs; ++k) {
          unsigned gid = unsigned((salt + k * 977) % f.glyphs);
          cache->get_glyph(f, gid, 24);
          f.advance_units(gid);
        }
    }
    cache->clear();
  }
}
static uint64_t counter;
static void target(const uint8_t *data, size_t size) {
  ++counter;
#if BSF22_TARGET == 1
  exercise(data, size, counter);
#elif BSF22_TARGET == 2
  BSF::DecodedWebFont out;
  BSF::WebFontLimits limits;
  limits.max_output = 4u * 1024 * 1024;
  if (BSF::decode_web_font(data, size, out, limits) == BSF::WebFontStatus::Ok) {
    exercise(out.bytes, out.size, counter);
    delete[] out.bytes;
  }
#else
  uint32_t *pixels = nullptr;
  unsigned w = 0, h = 0;
  if (BSF::decode_png(data, size, pixels, w, h)) {
    if (!w || !h || w > BSF::MaxPngSide || h > BSF::MaxPngSide) abort();
    delete[] pixels;
  }
#endif
}
int main(int argc, char **argv) {
  auto storage = std::make_unique<BSF::Cache>();
  cache = storage.get();
  std::vector<uint32_t> buffer(size_t(BSF::MaxBitmap) * BSF::MaxBitmap);
  argb = buffer.data();
#if BSF22_TARGET == 1
  FuzzEngine engine("fuzz_bsf22_font", 0x42534632, 256 * 1024);
  engine.parse_args(argc, argv);
  for (const char *p : {"testy/fixtures/bsf22/fonts/NotoSansHebrew-Regular.otf",
                        "testy/fixtures/bsf22/fonts/NotoSansHebrew-VF.ttf",
                        "testy/fixtures/bsf22/fonts/NotoSans-subset.ttc",
                        "testy/fixtures/bsf22/fonts/NotoSans-mixed.otc",
                        "testy/fixtures/bsf22/color/BSF-COLRv0-test.ttf",
                        "testy/fixtures/bsf22/color/BSF-sbix-test.ttf",
                        "testy/fixtures/bsf22/color/BSF-COLRv1-adversarial.ttf"}) {
    auto v = file(p);
    if (v.empty()) return 2;
    engine.add_seed(v);
  }
  engine.add_regression(std::vector<uint8_t>(12, 0));
  std::vector<uint8_t> ttc = {'t', 't', 'c', 'f', 0, 2, 0, 0, 0xff, 0xff, 0xff, 0xff};
  engine.add_regression(ttc);
#elif BSF22_TARGET == 2
  FuzzEngine engine("fuzz_bsf22_webfont", 0x574f4632, 256 * 1024);
  engine.parse_args(argc, argv);
  for (const char *p : {"testy/fixtures/bsf22/web/NotoSans-Latin.woff", "testy/fixtures/bsf22/web/NotoSans-Latin.woff2",
                        "testy/fixtures/bsf22/web/NotoSansHebrew-VF.woff2", "testy/fixtures/bsf22/web/NotoSansHebrew-VF.woff",
                        "testy/fixtures/bsf22/web/bomb-brotli.woff2", "testy/fixtures/bsf22/web/bomb-zlib.woff"}) {
    auto v = file(p);
    if (v.empty()) return 2;
    engine.add_seed(v);
  }
  engine.add_regression(std::vector<uint8_t>{'w', 'O', 'F', '2'});
#else
  FuzzEngine engine("fuzz_bsf22_png", 0x504e4732, 64 * 1024);
  engine.parse_args(argc, argv);
  // Seeds: the first PNG images of the CBDT and sbix fixtures.
  for (const char *p : {"testy/fixtures/bsf22/color/NotoColorEmoji-subset.ttf", "testy/fixtures/bsf22/color/BSF-sbix-test.ttf"}) {
    auto v = file(p);
    unsigned found = 0;
    for (size_t i = 0; i + 8 < v.size() && found < 3; ++i)
      if (v[i] == 0x89 && v[i + 1] == 'P' && v[i + 2] == 'N' && v[i + 3] == 'G') {
        size_t end = i + 8;
        while (end + 12 <= v.size()) {
          uint32_t len = uint32_t(v[end]) << 24 | v[end + 1] << 16 | v[end + 2] << 8 | v[end + 3];
          if (len > v.size() - end - 12) break;
          bool iend = !memcmp(&v[end + 4], "IEND", 4);
          end += 12 + len;
          if (iend) break;
        }
        engine.add_seed(std::vector<uint8_t>(v.begin() + i, v.begin() + end));
        ++found;
        i = end;
      }
  }
#endif
  return engine.run(target);
}
