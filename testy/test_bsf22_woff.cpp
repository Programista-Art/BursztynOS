// BSF 2.2 Milestone 4 — WOFF 1.0 and WOFF2 decoding (bsf/webfont.cpp,
// bsf/inflate.inc, vendored Brotli decoder).
//
// Each fixture (made by fontTools from the source font) is decoded by BSF and
// compared with the SOURCE font: untransformed tables byte for byte; every
// glyph outline of the decoded face against FreeType on the source font;
// hmtx advances and side bearings exactly. Malformed/bomb inputs are checked
// under ASan/UBSan.
#include "bsf22_common.h"
#include "../bsf/webfont.h"
#include <map>
#include <memory>
#include <random>
static const std::string web = "testy/fixtures/bsf22/web/";
static const std::string fonts = "testy/fixtures/bsf22/fonts/";
static std::map<uint32_t, std::vector<uint8_t>> tables_of(const std::vector<uint8_t> &sfnt) {
  std::map<uint32_t, std::vector<uint8_t>> out;
  Bytes b{sfnt.data(), sfnt.size()};
  TableDirectory d;
  CHECK(face_directory(b, 0, d));
  for (unsigned i = 0; i < d.count; ++i)
    out[d.records[i].tag] = std::vector<uint8_t>(sfnt.begin() + d.records[i].offset,
                                                 sfnt.begin() + d.records[i].offset + d.records[i].length);
  return out;
}
static unsigned rd16_be(const uint8_t *p) { return unsigned(p[0] << 8 | p[1]); }
static uint32_t T(const char *s) {
  return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
         (uint32_t(uint8_t(s[2])) << 8) | uint8_t(s[3]);
}
struct Summary {
  unsigned files{}, glyphs{}, hmtx_transformed{}, glyf_transformed{};
};
static void check_font(FT_Library lib, const std::string &file, const std::string &source,
                       ContainerFormat want, Summary &sum) {
  auto in = read(web + file);
  auto src = read(source);
  CHECK(!in.empty() && !src.empty());
  DecodedWebFont d;
  WebFontStatus st = decode_web_font(in.data(), in.size(), d);
  if (st != WebFontStatus::Ok)
    std::fprintf(stderr, "decode %s: %s\n", file.c_str(), web_font_status_name(st));
  CHECK(st == WebFontStatus::Ok);
  CHECK(d.container == want);
  std::vector<uint8_t> out(d.bytes, d.bytes + d.size);
  delete[] d.bytes;
  Face f, s;
  CHECK(f.open(out.data(), out.size(), 1));
  CHECK(s.open(src.data(), src.size(), 2));
  CHECK(f.glyphs == s.glyphs && f.outline == s.outline && f.units == s.units);
  CHECK(!std::strcmp(f.family, s.family) && !std::strcmp(f.style, s.style));
  auto a = tables_of(out), b = tables_of(src);
  bool woff2 = want == ContainerFormat::WOFF2;
  if (woff2)
    b.erase(T("DSIG")); // WOFF2 encoders drop DSIG (signature invalidated)
  if (a.size() != b.size()) {
    std::fprintf(stderr, "  %s: %zu decoded tables vs %zu in source:", file.c_str(), a.size(), b.size());
    for (auto &kv : a)
      std::fprintf(stderr, " %c%c%c%c", char(kv.first >> 24), char(kv.first >> 16), char(kv.first >> 8), char(kv.first));
    std::fprintf(stderr, "\n");
  }
  CHECK(a.size() == b.size());
  for (auto &kv : b) {
    CHECK(a.count(kv.first));
    uint32_t t = kv.first;
    if (woff2 && (t == T("glyf") || t == T("loca")))
      continue; // re-encoded; compared through outlines below
    if (t == T("head")) {
      // checkSumAdjustment (8..11) and indexToLocFormat may differ.
      auto x = a[t], y = kv.second;
      CHECK(x.size() == y.size());
      // The encoder (fontTools) rewrites checkSumAdjustment (8..11) and the
      // modified date (28..35) when saving; WOFF2 encoders also set
      // head.flags bit 11 ("converted losslessly", byte 16 bit 3). Those
      // come from the file itself; every other head byte must be identical.
      for (size_t i = 0; i < x.size(); ++i)
        if (!(i >= 8 && i < 12) && !(i >= 28 && i < 36) && !(woff2 && i >= 50 && i < 52))
          CHECK((woff2 && i == 16) ? (x[i] & ~8) == (y[i] & ~8) : x[i] == y[i]);
      continue;
    }
    if (a[t] != kv.second)
      std::fprintf(stderr, "  %s: table %c%c%c%c differs\n", file.c_str(), char(t >> 24),
                   char(t >> 16), char(t >> 8), char(t));
    CHECK(a[t] == kv.second);
  }
  // Outlines of the decoded face vs FreeType on the source font.
  CHECK(compare_outlines(lib, src, 0, f, file.c_str()) == 0);
  // FreeType also accepts the decoded sfnt (independent validation).
  FT_Face ft;
  CHECK(!FT_New_Memory_Face(lib, out.data(), FT_Long(out.size()), 0, &ft));
  CHECK(unsigned(ft->num_glyphs) == f.glyphs);
  FT_Done_Face(ft);
  for (unsigned g = 0; g < f.glyphs; ++g) {
    CHECK(f.advance_units(g) == s.advance_units(g));
    CHECK(f.bearing(g, 1000) == s.bearing(g, 1000));
  }
  // Which WOFF2 transforms were present (directory flag bits).
  if (woff2) {
    size_t at = 48;
    unsigned count = rd16_be(in.data() + 12);
    for (unsigned i = 0; i < count; ++i) {
      unsigned flags = in[at++], index = flags & 63, version = flags >> 6;
      if (index == 63)
        at += 4;
      auto skip = [&]() { while (in[at++] & 0x80) {} };
      skip();
      bool glyf_like = index == 10 || index == 11;
      bool transformed = glyf_like ? version == 0 : version != 0;
      if (transformed) {
        skip();
        if (index == 10)
          ++sum.glyf_transformed;
        if (index == 3)
          ++sum.hmtx_transformed;
      }
    }
  }
  ++sum.files;
  sum.glyphs += f.glyphs;
  std::printf("  %-36s -> %7zu B sfnt, %5u glyphs, outlines/metrics equal to source\n",
              file.c_str(), out.size(), f.glyphs);
}
static void adversarial() {
  // Decompression bombs.
  {
    auto bomb = read(web + "bomb-brotli.woff2");
    CHECK(!bomb.empty());
    DecodedWebFont d;
    WebFontStatus st = decode_web_font(bomb.data(), bomb.size(), d);
    CHECK(st != WebFontStatus::Ok && !d.bytes);
    std::printf("  Brotli bomb (32 MiB of zeros in %zu B): %s\n", bomb.size(), web_font_status_name(st));
    auto zb = read(web + "bomb-zlib.woff");
    st = decode_web_font(zb.data(), zb.size(), d);
    CHECK(st != WebFontStatus::Ok && !d.bytes);
    std::printf("  zlib table larger than origLength: %s\n", web_font_status_name(st));
  }
  // Declared sizes far above the input (ratio guard, before allocation).
  {
    auto w = read(web + "NotoSans-Latin.woff");
    auto v = w;
    v[16] = 0x7f; // totalSfntSize huge
    DecodedWebFont d;
    CHECK(decode_web_font(v.data(), v.size(), d) == WebFontStatus::TooLarge);
    v = w;
    // First table origLength = 15 MiB.
    v[44 + 12] = 0x00; v[44 + 13] = 0xf0; v[44 + 14] = 0; v[44 + 15] = 0;
    CHECK(decode_web_font(v.data(), v.size(), d) == WebFontStatus::TooLarge);
    WebFontLimits tiny;
    tiny.max_output = 4096;
    CHECK(decode_web_font(w.data(), w.size(), d, tiny) == WebFontStatus::TooLarge);
    tiny = WebFontLimits{};
    tiny.max_input = 100;
    CHECK(decode_web_font(w.data(), w.size(), d, tiny) == WebFontStatus::TooLarge);
  }
  // Structural errors.
  {
    auto w = read(web + "NotoSans-Latin.woff");
    DecodedWebFont d;
    auto v = w;
    v[8] ^= 1; // length != file size
    CHECK(decode_web_font(v.data(), v.size(), d) == WebFontStatus::Malformed);
    v = w;
    v[44 + 4 + 3] |= 2; // misaligned table offset
    CHECK(decode_web_font(v.data(), v.size(), d) != WebFontStatus::Ok);
    v = w;
    std::memcpy(v.data() + 64, v.data() + 44, 4); // duplicate/unsorted tags
    CHECK(decode_web_font(v.data(), v.size(), d) != WebFontStatus::Ok);
    v = w;
    v[14] = 1; // reserved != 0
    CHECK(decode_web_font(v.data(), v.size(), d) == WebFontStatus::Malformed);
    v = w;
    std::memcpy(v.data(), "wOFX", 4);
    CHECK(decode_web_font(v.data(), v.size(), d) == WebFontStatus::Unsupported);
  }
  {
    auto w = read(web + "NotoSans-Latin.woff2");
    DecodedWebFont d;
    auto v = w;
    v[14] = 1; // reserved
    CHECK(decode_web_font(v.data(), v.size(), d) == WebFontStatus::Malformed);
    v = w;
    v[49] = 0x80; // UIntBase128 with a leading zero byte (first entry origLength)
    CHECK(decode_web_font(v.data(), v.size(), d) != WebFontStatus::Ok);
    std::memcpy(v.data() + 4, "ttcf", 4);
    v = w;
    std::memcpy(v.data() + 4, "ttcf", 4);
    CHECK(decode_web_font(v.data(), v.size(), d) == WebFontStatus::Unsupported);
  }
  // Truncation at many points and random mutations (no crash, no leak, and a
  // successful decode always yields a font that passes Face validation).
  std::mt19937 rng(2204);
  unsigned ok = 0, total = 0;
  auto cache = std::make_unique<Cache>();
  for (const char *name : {"NotoSans-Latin.woff", "NotoSans-Latin.woff2", "NotoSans-CFF.woff2",
                           "NotoSansHebrew-VF.woff2", "NotoSansHebrew-VF.woff"}) {
    auto base = read(web + name);
    for (size_t cut = 0; cut < base.size(); cut += 1 + base.size() / 97) {
      DecodedWebFont d;
      std::vector<uint8_t> v(base.begin(), base.begin() + cut);
      CHECK(decode_web_font(v.data(), v.size(), d) != WebFontStatus::Ok);
    }
    for (unsigned iter = 0; iter < 1500; ++iter) {
      auto v = base;
      for (unsigned e = 0, n = 1 + rng() % 6; e < n; ++e) {
        size_t at = rng() % 3 ? rng() % std::min<size_t>(v.size(), 400) : rng() % v.size();
        v[at] = uint8_t(rng());
      }
      DecodedWebFont d;
      ++total;
      if (decode_web_font(v.data(), v.size(), d) == WebFontStatus::Ok) {
        ++ok;
        Face f;
        CHECK(f.open(d.bytes, d.size, 5000 + iter));
        for (unsigned k = 0; k < 4; ++k)
          cache->get_glyph(f, rng() % f.glyphs, 24);
        cache->clear();
        delete[] d.bytes;
      }
    }
  }
  std::printf("  malformed: truncations + %u mutations, %u decoded (all re-validated)\n", total, ok);
}
int main() {
  FT_Library lib;
  CHECK(!FT_Init_FreeType(&lib));
  Summary sum;
  struct Case {
    const char *file;
    std::string source;
    ContainerFormat format;
  } cases[] = {
      {"NotoSans-Latin.woff", web + "NotoSans-Latin.ttf", ContainerFormat::WOFF},
      {"NotoSans-Latin.woff2", web + "NotoSans-Latin.ttf", ContainerFormat::WOFF2},
      {"NotoSans-Latin-notransform.woff2", web + "NotoSans-Latin.ttf", ContainerFormat::WOFF2},
      {"NotoSans-CFF.woff", fonts + "NotoSans-Regular.otf", ContainerFormat::WOFF},
      {"NotoSans-CFF.woff2", fonts + "NotoSans-Regular.otf", ContainerFormat::WOFF2},
      {"NotoSansHebrew-VF.woff", fonts + "NotoSansHebrew-VF.ttf", ContainerFormat::WOFF},
      {"NotoSansHebrew-VF.woff2", fonts + "NotoSansHebrew-VF.ttf", ContainerFormat::WOFF2},
      {"NotoSansHebrew-VF-notransform.woff2", fonts + "NotoSansHebrew-VF.ttf", ContainerFormat::WOFF2},
      {"NotoNaskhArabic.woff", web + "NotoNaskhArabic.ttf", ContainerFormat::WOFF},
      {"NotoNaskhArabic.woff2", web + "NotoNaskhArabic.ttf", ContainerFormat::WOFF2},
  };
  for (auto &c : cases)
    check_font(lib, c.file, c.source, c.format, sum);
  std::printf("  WOFF2 transforms exercised: glyf/loca in %u files, hmtx in %u files\n",
              sum.glyf_transformed, sum.hmtx_transformed);
  CHECK(sum.glyf_transformed >= 3);
  CHECK(sum.hmtx_transformed >= 1);
  adversarial();
  FT_Done_FreeType(lib);
  std::printf("BSF 2.2 WOFF/WOFF2: %u fonts (%u glyphs) decoded exactly, %lu checks PASS\n",
              sum.files, sum.glyphs, checks);
}
