#pragma once
#include <stddef.h>
#include <stdint.h>
#include "limits.h"
// Bursztynowy System Czcionek. This API and all parsers are userspace only.
namespace BSF {
constexpr unsigned MaxTables = 64, MaxGlyphs = 16384;
constexpr unsigned MaxPoints = 4096, MaxContours = 128, MaxVertices = 8192,
                   MaxCompoundDepth = 8;
constexpr unsigned MaxFaces = 4, MaxBitmap = 128,
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
  char family[96]{}, style[48]{}, full_name[128]{}, postscript[96]{};
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
enum class TextDirection : uint8_t { LogicalLTR, ExperimentalRTL };
// Position records retain logical source offsets; no visual reordering is enabled.
struct PositionedGlyph {
  uint32_t face{}, glyph{};
  int advance{}, offset_x{}, offset_y{};
  size_t byte_begin{}, byte_end{}, cluster_begin{};
  TextDirection direction{TextDirection::LogicalLTR};
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
// Unicode 15.1.0 properties and extended grapheme boundaries (UAX #29).
unsigned combining_class(uint32_t);
bool default_ignorable(uint32_t);
bool combining(uint32_t);
size_t next_cluster(const char*,size_t at,size_t size);
size_t previous_cluster(const char*,size_t at);
size_t cluster_floor(const char*,size_t size,size_t offset);
bool cluster_boundary(const char*,size_t size,size_t offset);
size_t cluster_count(const char*,size_t size);
// Bounded approximate positioning when no OpenType anchors are available.
struct MarkPen {
  int base{}, advance{}, above{}, below{}, top{}, bottom{}, center{};
  bool has_base{};
  void place(uint32_t cp,int pen,int& x,int& y,int width,int height,int step){
    if(combining(cp)){
      x=(has_base?center:base+advance/2)-width/2;
      unsigned ccc=combining_class(cp);
      if(ccc==1||!has_base)return;
      bool low=ccc==200||ccc==202||ccc==218||ccc==220||ccc==222||ccc==233||ccc==240;
      int gap=ccc==200||ccc==202||ccc==214||ccc==216?0:1;
      if(low){y=bottom+gap+below;below+=height+gap;}
      else {y=top-height-gap-above;above+=height+gap;}
    }else{base=pen;advance=step;above=below=0;top=y;bottom=y+height;center=width?x+width/2:pen+step/2;has_base=true;}
  }
};
} // namespace BSF
