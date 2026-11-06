#include "../bsf/bsf.h"
#include "../bsf/config.h"
#include "../bsf/asset_path.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
static unsigned checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                     \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
using namespace BSF;
static std::vector<uint8_t> read(const char *p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
static void put16(std::vector<uint8_t> &v, size_t o, unsigned n) {
  v[o] = uint8_t(n >> 8);
  v[o + 1] = uint8_t(n);
}
static void put32(std::vector<uint8_t> &v, size_t o, unsigned n) {
  put16(v, o, n >> 16);
  put16(v, o + 2, n);
}
static size_t table(const std::vector<uint8_t> &v, const char *name) {
  Bytes b{v.data(), v.size()};
  for (unsigned i = 0; i < b.u16(4); ++i) {
    unsigned o = 12 + i * 16;
    if (!std::memcmp(v.data() + o, name, 4))
      return o;
  }
  std::abort();
}
int main() {
  auto regular = read("czcionki/ttf/DejaVuSans.ttf"),
       bold = read("czcionki/ttf/DejaVuSans-Bold.ttf");
  CHECK(!regular.empty() && !bold.empty());
  Face a, b;
  CHECK(a.open(regular.data(), regular.size(), 1));
  CHECK(b.open(bold.data(), bold.size(), 2));
  CHECK(a.glyph('A') != 'A');
  CHECK(a.weight == 400 && b.weight >= 600);
  CHECK(a.advance(a.glyph('W'), 16) > a.advance(a.glyph('i'), 16));
  CHECK(a.ascent > 0 && a.descent < 0 && a.line_height(16) >= 16);
  CHECK(a.kerning(a.glyph('A'), a.glyph('V'), 48) < 0);
  auto cache = std::make_unique<Cache>();
  const uint32_t polish[] = {0x105, 0x107, 0x119, 0x142, 0x144, 0xf3,
                             0x15b, 0x17a, 0x17c, 0x104, 0x106, 0x118,
                             0x141, 0x143, 0xd3,  0x15a, 0x179, 0x17b,
                             'A',   'B',   'g',   '0'};
  for (int size : {10, 11, 12, 13, 14, 16, 18, 20, 24, 32, 48})
    for (auto cp : polish) {
      CHECK(a.glyph(cp) > 0);
      auto *g = cache->get(a, cp, size);
      CHECK(g && g->width > 0 && g->height > 0);
      bool ink = false, aa = false;
      for (int i = 0; i < g->width * g->height; ++i) {
        ink |= g->alpha[i] > 0;
        aa |= g->alpha[i] > 0 && g->alpha[i] < 255;
      }
      CHECK(ink && aa);
      auto hits = cache->stats.hits;
      CHECK(cache->get(a, cp, size) == g);
      CHECK(cache->stats.hits == hits + 1);
    }
  auto miss = cache->stats.misses;
  CHECK(cache->get(b, 'A', 16, 1));
  CHECK(cache->stats.misses == miss + 1);
  miss = cache->stats.misses;
  CHECK(cache->get(a, 'A', 17));
  CHECK(cache->stats.misses == miss + 1);
  for (unsigned cp = 32; cp < 1024; ++cp)
    cache->get(a, cp, 16);
  CHECK(cache->stats.evictions > 0);
  CHECK(cache->stats.bitmap_bytes <= CacheBytes);
  auto* held=const_cast<Glyph*>(cache->get(a,'B',16));CHECK(held);++held->pins;
  std::vector<uint8_t> held_mask(held->alpha,held->alpha+held->width*held->height);
  for(unsigned cp=32;cp<900;++cp)cache->get(a,cp,18);
  CHECK(!std::memcmp(held->alpha,held_mask.data(),held_mask.size()));
  cache->clear();for(unsigned cp=32;cp<900;++cp)cache->get(a,cp,20);
  CHECK(!std::memcmp(held->alpha,held_mask.data(),held_mask.size()));--held->pins;
  cache->clear();
  miss = cache->stats.misses;
  CHECK(cache->get(a, 'A', 16));
  CHECK(cache->stats.misses == miss + 1);
  CHECK(a.glyph(0x110000) == 0 && a.glyph(0xd800) == 0);
  CHECK(cache->get(a, 0x10ffff, 16));
  for (size_t n :
       {size_t(0), size_t(4), size_t(11), size_t(100), regular.size() - 1}) {
    Face f;
    CHECK(!f.open(regular.data(), n));
  }
  {
    auto v = regular;
    put16(v, 4, 65535);
    Face f;
    CHECK(!f.open(v.data(), v.size()));
  }
  {
    auto v = regular;
    put32(v, 20, 0xfffffff0);
    put32(v, 24, 0x100);
    Face f;
    CHECK(!f.open(v.data(), v.size()));
  }
  {
    auto v = regular;
    auto o = table(v, "loca");
    put32(v, Bytes{v.data(), v.size()}.u32(o + 8), 0xffffffff);
    Face f;
    CHECK(!f.open(v.data(), v.size()));
  }
  {
    auto v = regular;
    auto o = table(v, "cmap");
    put32(v, o + 12, 2);
    Face f;
    CHECK(!f.open(v.data(), v.size()));
  }
  {
    auto v = regular;
    auto o = table(v, "head");
    put16(v, Bytes{v.data(), v.size()}.u32(o + 8) + 18, 16);
    Face f;
    CHECK(!f.open(v.data(), v.size(), 9) || !cache->get(f, 'A', 96));
  }
  for (bool compound : {false, true}) {
    auto v = regular;
    Bytes g;
    CHECK(a.location(a.glyph('A'), g));
    size_t off = size_t(g.p - regular.data());
    put16(v, off, compound ? 65535 : MaxContours + 1);
    if (compound) {
      put16(v, off + 10, 3);
      put16(v, off + 12, a.glyph('A'));
      put32(v, off + 14, 0);
    }
    Face f;
    CHECK(f.open(v.data(), v.size(), compound ? 12 : 11));
    CHECK(!cache->get(f, 'A', 16));
  }
  // Acyclic compounds must still stop at the explicit dependency depth.
  {
    auto v = regular;
    for (unsigned i = 0; i < MaxCompoundDepth + 1; ++i) {
      Bytes g;
      unsigned id = a.glyph('A') + i;
      CHECK(a.location(id, g) && g.n >= 18);
      size_t off = size_t(g.p - regular.data());
      put16(v, off, 65535);
      put16(v, off + 10, 3);
      put16(v, off + 12, id + 1);
      put32(v, off + 14, 0);
    }
    Face f;
    CHECK(f.open(v.data(), v.size(), 31));
    CHECK(!cache->get(f, 'A', 16));
  }
  {
    auto v = regular;
    Bytes g;
    CHECK(a.location(a.glyph('A'), g));
    size_t off = size_t(g.p - regular.data());
    put16(v, off, 1);
    put16(v, off + 10, MaxPoints);
    Face f;
    CHECK(f.open(v.data(), v.size(), 32));
    CHECK(!cache->get(f, 'A', 16));
  }
  {
    auto v = regular;
    size_t off = Bytes{v.data(), v.size()}.u32(table(v, "kern") + 8);
    put16(v, off + 4 + 14, MaxGlyphs);
    Face f;
    CHECK(!f.open(v.data(), v.size(), 33));
  }
  uint32_t random = 123;
  for (unsigned i = 0; i < 1200; ++i) {
    auto v = regular;
    for (unsigned j = 0; j < 4; ++j) {
      random = random * 1664525 + 1013904223;
      size_t at = random % v.size();
      random = random * 1664525 + 1013904223;
      v[at] = uint8_t(random >> 16);
    }
    Face f;
    if (f.open(v.data(), v.size(), 100 + i)) {
      auto *g = cache->get(f, polish[i % 22], 16);
      CHECK(!g || (g->width <= 128 && g->height <= 128));
    }
  }
  uint32_t cp = 0;
  CHECK(trusted_asset("/czcionki/ttf",true));CHECK(trusted_asset("/czcionki/ttf/font.TTF"));
  CHECK(!trusted_asset("/czcionki/ttf/../private.ttf"));CHECK(!trusted_asset("/czcionki/ttf/sub/font.ttf"));
  CHECK(!trusted_asset("/czcionki/ttfx/font.ttf"));CHECK(!trusted_asset("/private/font.ttf"));CHECK(!trusted_asset("/czcionki/ttf/font.otf"));
  CHECK(utf8("\xf0\x9f", 2, cp) == 1 && cp == 0xfffd);
  CHECK(utf8("\xed\xa0\x80", 3, cp) == 1 && cp == 0xfffd);
  CHECK(utf8("\xc4\x85", 2, cp) == 2 && cp == 0x105);
  Config c;
  c.checksum = checksum(c);
  CHECK(valid(c));
  c.size = 100;
  CHECK(!valid(c));
  auto start = std::chrono::steady_clock::now();
  cache->clear();
  CHECK(cache->get(a, 'B', 16));
  auto first = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < 10000; ++i)
    CHECK(cache->get(a, 'B', 16));
  auto end = std::chrono::steady_clock::now();
  std::printf(
      "BSF: %u checks PASS; cache=%u bitmap-live=%zu hits=%llu misses=%llu "
      "evictions=%llu face=%zu glyph=%zu first-ns=%lld hit-average-ns=%lld\n",
      checks, CacheBytes, cache->stats.bitmap_bytes,
      (unsigned long long)cache->stats.hits,
      (unsigned long long)cache->stats.misses,
      (unsigned long long)cache->stats.evictions, sizeof(Face), sizeof(Glyph),
      (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(first -
                                                                      start)
          .count(),
      (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(end -
                                                                      first)
              .count() /
          10000);
}
