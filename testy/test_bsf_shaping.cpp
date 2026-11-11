// BSF 2.0 GSUB/GPOS shaping verified against HarfBuzz-generated fixtures
// (testy/fixtures/bsf_shaping_oracle.tsv). Compares glyph IDs, clusters,
// advances and offsets exactly, in font units and visual order.
#include "../bsf/shape.h"
#include "../bsf/bsf.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
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
static std::vector<uint8_t> file(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
static uint8_t script_index(const std::string &code) {
  for (unsigned s = 0; s < 256; ++s)
    if (code == script_code(uint8_t(s)))
      return uint8_t(s);
  return ScriptCommon;
}
struct Loaded {
  std::vector<uint8_t> bytes;
  Face face;
  OT::Layout layout;
  bool ok{};
};
int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "testy/fixtures/bsf_shaping_oracle.tsv";
  bool verbose = std::getenv("BSF_SHAPING_VERBOSE") != nullptr;
  std::ifstream in(path);
  CHECK(in.good());
  std::map<std::string, Loaded *> fonts;
  std::string line;
  unsigned rows = 0, exact = 0, skipped = 0, glyphs = 0;
  std::vector<std::string> failures;
  OT::Buffer buf;
  uint64_t gsub_total[9][4]{}, gpos_total[10][4]{};
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string part;
    while (std::getline(ss, part, '\t'))
      f.push_back(part);
    if (f.size() == 5)
      f.push_back("");
    if (f.size() == 6)
      f.push_back("");
    CHECK(f.size() == 7);
    Loaded *&font = fonts[f[1]];
    if (!font) {
      font = new Loaded;
      font->bytes = file(f[1]);
      font->ok = !font->bytes.empty() &&
                 font->face.open(font->bytes.data(), font->bytes.size(), uint32_t(fonts.size()));
      if (font->ok)
        font->layout.open(font->face);
    }
    if (!font->ok) {
      ++skipped;
      continue;
    }
    std::string text;
    for (size_t i = 0; i + 1 < f[4].size(); i += 2)
      text += char(std::strtoul(f[4].substr(i, 2).c_str(), nullptr, 16));
    std::vector<uint32_t> cps, clusters;
    for (size_t at = 0; at < text.size();) {
      size_t end = next_cluster(text.data(), at, text.size());
      for (size_t p = at; p < end;) {
        uint32_t cp;
        p += utf8(text.data() + p, end - p, cp);
        cps.push_back(cp);
        clusters.push_back(uint32_t(at));
      }
      at = end;
    }
    ShapeOptions o;
    o.script = script_index(f[2]);
    o.rtl = f[3] == "rtl";
    // Column 7: opt-in features requested from both shapers.
    if (f[6].find("dlig") != std::string::npos)
      o.features |= FeatureDlig;
    if (f[6].find("aalt") != std::string::npos)
      o.features |= FeatureAalt;
    ShapeReport report;
    bool ok = shape(font->face, font->layout, cps.data(), clusters.data(),
                    cps.size(), o, buf, &report);
    ++rows;
    for (unsigned t = 0; t < 9; ++t)
      for (unsigned k = 0; k < 4; ++k)
        gsub_total[t][k] += report.stats.gsub_applied[t][k];
    for (unsigned t = 0; t < 10; ++t)
      for (unsigned k = 0; k < 4; ++k)
        gpos_total[t][k] += report.stats.gpos_applied[t][k];
    std::vector<std::vector<long>> expect;
    std::stringstream rs(f[5]);
    while (std::getline(rs, part, ';')) {
      std::vector<long> v;
      std::stringstream gs(part);
      std::string n;
      while (std::getline(gs, n, ','))
        v.push_back(std::strtol(n.c_str(), nullptr, 10));
      // BSF clusters are UAX #29 grapheme starts (editing semantics);
      // HarfBuzz keeps e.g. ZWNJ separate. Compare at grapheme level.
      if (v.size() > 1)
        v[1] = long(cluster_floor(text.data(), text.size(), size_t(v[1])));
      expect.push_back(v);
    }
    std::vector<std::vector<long>> got;
    for (unsigned i = 0; i < buf.len; ++i)
      got.push_back({long(buf.info[i].glyph), long(buf.info[i].cluster),
                     long(buf.pos[i].x_advance), long(buf.pos[i].y_advance),
                     long(buf.pos[i].x_offset), long(buf.pos[i].y_offset)});
    glyphs += unsigned(got.size());
    ++checks;
    if (ok && got == expect)
      ++exact;
    else {
      std::string msg = f[0] + " " + f[2] + " " + f[3] + " '" + text + "' " + f[6];
      failures.push_back(msg);
      if (verbose) {
        std::fprintf(stderr, "MISMATCH %s (ok=%d)\n  expect:", msg.c_str(), ok);
        for (auto &v : expect)
          std::fprintf(stderr, " [%ld c%ld %ld,%ld %ld,%ld]", v[0], v[1], v[2], v[3], v[4], v[5]);
        std::fprintf(stderr, "\n  got:   ");
        for (auto &v : got)
          std::fprintf(stderr, " [%ld c%ld %ld,%ld %ld,%ld]", v[0], v[1], v[2], v[3], v[4], v[5]);
        std::fprintf(stderr, "\n");
      }
    }
  }
  for (auto &kv : fonts)
    delete kv.second;
  for (auto &m : failures)
    std::fprintf(stderr, "shaping mismatch: %s\n", m.c_str());
  // Instrumentation: every supported lookup type must actually be applied
  // by the oracle corpus (not merely present in some font's LookupList).
  unsigned missing_types = 0;
  auto report_type = [&](const char *table, unsigned t, const uint64_t *f) {
    uint64_t sum = f[0] + f[1] + f[2] + f[3];
    std::printf("  %s %u applied %8llu (format1 %llu, format2 %llu, format3 %llu, other %llu)\n",
                table, t, (unsigned long long)sum, (unsigned long long)f[1],
                (unsigned long long)f[2], (unsigned long long)f[3],
                (unsigned long long)f[0]);
    if (!sum) {
      std::fprintf(stderr, "lookup type never applied: %s %u\n", table, t);
      ++missing_types;
    }
  };
  if (!skipped) {
    std::printf("Lookup application counts over the oracle corpus:\n");
    for (unsigned t = 1; t < 9; ++t)
      report_type("GSUB", t, gsub_total[t]);
    for (unsigned t = 1; t < 10; ++t)
      report_type("GPOS", t, gpos_total[t]);
    std::printf("Lookup types never applied: %u\n", missing_types);
  } else {
    std::printf("Lookup type coverage not asserted (%u rows skipped)\n", skipped);
  }
  std::printf("BSF shaping vs HarfBuzz oracle: %u/%u rows exact (%u glyphs), "
              "%u rows skipped (font unavailable), %u checks\n",
              exact, rows, glyphs, skipped, checks);
  return exact == rows && !missing_types ? 0 : 1;
}
