#pragma once
#include <stddef.h>
#include <stdint.h>
// Bursztynowy System Czcionek. This API and all parsers are userspace only.
namespace BSF {
constexpr unsigned MaxFile = 2 * 1024 * 1024, MaxTables = 64, MaxGlyphs = 16384;
constexpr unsigned MaxPoints = 4096, MaxContours = 128, MaxVertices = 8192,
                   MaxCompoundDepth = 8;
constexpr unsigned MaxFiles = 32, MaxFaces = 4, MaxBitmap = 128,
                   CacheSlots = 128;
constexpr unsigned CacheBytes = CacheSlots * MaxBitmap * MaxBitmap;
struct Bytes {
  const uint8_t *p{};
  size_t n{};
  bool has(size_t o, size_t z) const { return o <= n && z <= n - o; }
  uint16_t u16(size_t o) const {
    return has(o, 2) ? uint16_t((unsigned(p[o]) << 8) | p[o + 1]) : 0;
  }
  int16_t s16(size_t o) const { return int16_t(u16(o)); }
  uint32_t u32(size_t o) const {
    return has(o, 4) ? (uint32_t(u16(o)) << 16) | u16(o + 2) : 0;
  }
  Bytes sub(size_t o, size_t z) const {
    return has(o, z) ? Bytes{p + o, z} : Bytes{};
  }
};
struct Face {
  Bytes data{}, head{}, hhea{}, hmtx{}, loca{}, glyf{}, cmap{}, kern{}, names{};
  uint32_t id{};
  uint16_t glyphs{}, units{}, metrics{}, weight{400};
  int ascent{}, descent{}, gap{};
  bool long_loca{}, italic{}, mono{}, serif{};
  char family[96]{}, style[48]{};
  bool open(const void *, size_t, uint32_t identity = 1);
  unsigned glyph(uint32_t) const;
  bool location(unsigned, Bytes &) const;
  int advance(unsigned, int) const;
  int bearing(unsigned, int) const;
  int kerning(unsigned, unsigned, int) const;
  int line_height(int px) const;

private:
  bool parse(const void *, size_t, uint32_t);
};
struct Glyph {
  uint32_t face{};
  uint16_t id{}, px{}, style{}, pins{};
  uint64_t used{};
  int width{}, height{}, x{}, y{}, advance{}; // x/y relative to baseline
  uint8_t *alpha{};
};
struct Statistics {
  uint64_t hits{}, misses{}, evictions{}, rasterizations{}, rejected{};
  size_t bitmap_bytes{};
};
class Cache {
  Glyph *slots_{};
  uint8_t *storage_{};
  uint64_t clock_{};

public:
  Statistics stats{};
  Cache() = default;
  ~Cache();
  Cache(const Cache &) = delete;
  Cache &operator=(const Cache &) = delete;
  void clear();
  // Borrowed until the next lookup. Paint immediately; never retain in
  // DOM/layout.
  const Glyph *get(const Face &, uint32_t cp, int px, unsigned style = 0);
  bool contains(const Face &, uint32_t cp, int px, unsigned style = 0) const;
};
bool rasterize(const Face &, unsigned glyph, int px, Glyph &, uint8_t *mask,
               size_t capacity);
size_t utf8(const char *, size_t available, uint32_t &);
} // namespace BSF
