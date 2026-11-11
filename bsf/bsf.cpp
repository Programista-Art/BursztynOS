#include "bsf.h"
#include <new>
#include <string.h>
#ifdef BSF_NATIVE
#define BSF_NEW new
#else
#define BSF_NEW new (std::nothrow)
#endif
namespace {
// A fixed arena caps all scratch allocations inside the third-party rasterizer.
struct Scratch {
  unsigned used{};
  bool failed{};
  alignas(16) uint8_t bytes[1024 * 1024];
};
void *raster_alloc(size_t n, void *u) {
  auto *a = static_cast<Scratch *>(u);
  n = (n + 15) & ~size_t(15);
  if (n > sizeof(a->bytes) - a->used) {
    a->failed = true;
    return nullptr;
  }
  void *p = a->bytes + a->used;
  a->used += unsigned(n);
  return p;
}
void raster_free(void *, void *) {}
int floor_int(double x) {
  int i = int(x);
  return i - (x < i);
}
int ceil_int(double x) {
  int i = int(x);
  return i + (x > i);
}
double square_root(double x) {
  if (x <= 0)
    return 0;
  double r = x > 1 ? x : 1;
  for (unsigned i = 0; i < 32; ++i)
    r = (r + x / r) * .5;
  return r;
}
} // namespace
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_RASTERIZER_VERSION 1
#define STBTT_malloc(n, u) raster_alloc(n, u)
#define STBTT_free(p, u) raster_free(p, u)
#define STBTT_ifloor(x) floor_int(x)
#define STBTT_iceil(x) ceil_int(x)
#define STBTT_sqrt(x) square_root(x)
#define STBTT_pow(x, y) 0.0 /* unused raw parser/SDF APIs */
#define STBTT_fmod(x, y) 0.0
#define STBTT_cos(x) 0.0
#define STBTT_acos(x) 0.0
#define STBTT_fabs(x) ((x) < 0 ? -(x) : (x))
#define STBTT_assert(x) ((void)0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "vendor/stb_truetype.h"
#pragma GCC diagnostic pop
namespace BSF {
#include "unicode.inc"

static uint32_t tag(const char *s) {
  return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
         (uint32_t(uint8_t(s[2])) << 8) | uint8_t(s[3]);
}
// Unicode name records only. Prefer Windows English, then platform Unicode.
static void name(Bytes table, unsigned kind, char *out, unsigned cap) {
  if (!table.has(0, 6) || table.u16(0) > 1) return;
  unsigned count=table.u16(2), strings=table.u16(4);
  if (count>512 || !table.has(6,size_t(count)*12) || strings<6+count*12) return;
  int best=-1;
  for(unsigned i=0;i<count;++i){
    unsigned o=6+i*12, platform=table.u16(o), encoding=table.u16(o+2);
    if(table.u16(o+6)!=kind || !(platform==0 || (platform==3 && (encoding==1 || encoding==10)))) continue;
    unsigned len=table.u16(o+8), off=table.u16(o+10);
    if(!len || len%2 || !table.has(size_t(strings)+off,len)) continue;
    int rank=platform==3 && table.u16(o+4)==0x409 ? 3 : platform==0 ? 2 : 1;
    if(rank<=best) continue;
    char decoded[128]{}; unsigned used=0; bool valid=true;
    for(unsigned j=0;j<len;j+=2){
      uint32_t cp=table.u16(strings+off+j);
      if(cp>=0xd800 && cp<=0xdbff){
        if(j+2>=len){valid=false;break;}
        unsigned low=table.u16(strings+off+j+2);
        if(low<0xdc00 || low>0xdfff){valid=false;break;}
        cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00); j+=2;
      }else if(cp>=0xdc00 && cp<=0xdfff){valid=false;break;}
      if(cp<32 || cp==127){valid=false;break;}
      unsigned n=cp<128?1:cp<2048?2:cp<65536?3:4;
      // Reject truncation rather than splitting a scalar or conflating families.
      if(used+n>=cap || used+n>=sizeof(decoded)){valid=false;break;}
      if(n==1) decoded[used++]=char(cp);
      else {decoded[used++]=char((n==2?0xc0:n==3?0xe0:0xf0)|(cp>>(6*(n-1))));
        for(unsigned k=n-1;k;--k) decoded[used++]=char(0x80|((cp>>(6*(k-1)))&63));}
    }
    if(valid && used){memcpy(out,decoded,used+1);best=rank;}
  }
}
bool Face::open(const void *ptr, size_t size, uint32_t identity) {
  Face next;
  if (next.parse(ptr, size, identity)) {
    *this = next;
    return true;
  }
  *this = Face{};
  return false;
}
bool Face::parse(const void *ptr, size_t size, uint32_t identity) {
  *this = Face{};
  if (!ptr || size < 12 || size > MaxFile)
    return false;
  data = {static_cast<const uint8_t *>(ptr), size};
  if (data.u32(0) != 0x10000 && data.u32(0) != tag("true"))
    return false;
  unsigned count = data.u16(4);
  if (!count || count > MaxTables || !data.has(12, count * 16))
    return false;
  Bytes maxp{}, post{}, full_cmap{};
  for (unsigned i = 0; i < count; ++i) {
    unsigned o = 12 + i * 16;
    uint32_t idtag = data.u32(o), off = data.u32(o + 8), len = data.u32(o + 12);
    if (!data.has(off, len) || off < 12 + count * 16)
      return false;
    for (unsigned j = 0; j < i; ++j) {
      unsigned p = 12 + j * 16;
      uint32_t a = data.u32(p + 8), b = data.u32(p + 12);
      if (data.u32(p) == idtag ||
          (len && b && uint64_t(off) < uint64_t(a) + b &&
           uint64_t(a) < uint64_t(off) + len))
        return false;
    }
    Bytes t = data.sub(off, len);
    if (idtag == tag("head"))
      head = t;
    else if (idtag == tag("hhea"))
      hhea = t;
    else if (idtag == tag("hmtx"))
      hmtx = t;
    else if (idtag == tag("loca"))
      loca = t;
    else if (idtag == tag("glyf"))
      glyf = t;
    else if (idtag == tag("maxp"))
      maxp = t;
    else if (idtag == tag("cmap"))
      full_cmap = t;
    else if (idtag == tag("kern"))
      kern = t;
    else if (idtag == tag("name"))
      names = t;
    else if (idtag == tag("OS/2"))
      os2 = t;
    else if (idtag == tag("GDEF"))
      gdef = t;
    else if (idtag == tag("GSUB"))
      gsub = t;
    else if (idtag == tag("GPOS"))
      gpos = t;
    else if (idtag == tag("fvar"))
      fvar = t;
    else if (idtag == tag("COLR"))
      colr = t;
    else if (idtag == tag("CPAL"))
      cpal = t;
    else if (idtag == tag("post"))
      post = t;
  }
  if (!head.has(0, 54) || head.u32(12) != 0x5f0f3cf5 || !hhea.has(0, 36) ||
      !maxp.has(0, 6))
    return false;
  units = head.u16(18);
  glyphs = maxp.u16(4);
  metrics = hhea.u16(34);
  long_loca = head.s16(50) == 1;
  if (head.s16(50) < 0 || head.s16(50) > 1 || units < 16 || units > 16384 ||
      !glyphs || glyphs > MaxGlyphs || !metrics || metrics > glyphs)
    return false;
  if (!hmtx.has(0, size_t(metrics) * 4 + size_t(glyphs - metrics) * 2) ||
      !loca.has(0, size_t(glyphs + 1) * (long_loca ? 4 : 2)) || !glyf.p)
    return false;
  uint32_t previous = 0;
  for (unsigned i = 0; i <= glyphs; ++i) {
    uint32_t next = long_loca ? loca.u32(i * 4) : uint32_t(loca.u16(i * 2)) * 2;
    if (next < previous || next > glyf.n)
      return false;
    previous = next;
  }
  ascent = hhea.s16(4);
  descent = hhea.s16(6);
  gap = hhea.s16(8);
  if (ascent <= descent || ascent - descent > 65535)
    return false;
  if (ascent - descent + (gap > 0 ? gap : 0) > int(units) * 4)
    return false;
  for (unsigned i = 0; i < metrics; ++i)
    if (hmtx.u16(i * 4) > unsigned(units) * 8)
      return false;
  if (os2.has(0, 6))
    weight = os2.u16(4);
  if (weight < 100 || weight > 900)
    weight = 400;
  italic = (head.u16(44) & 2) != 0 || (os2.has(62, 2) && (os2.u16(62) & 1));
  mono = post.has(0, 16) && post.u32(12) != 0;
  serif =
      os2.has(32, 10) && os2.p[32] == 2 && os2.p[33] >= 2 && os2.p[33] <= 10;
  if (kern.p && kern.has(0, 4) && kern.u16(0) == 0) {
    size_t at = 4;
    if (kern.u16(2) > 32)
      return false;
    for (unsigned i = 0; i < kern.u16(2); ++i) {
      if (!kern.has(at, 6))
        return false;
      unsigned len = kern.u16(at + 2);
      if (len < 6 || !kern.has(at, len))
        return false;
      Bytes k = kern.sub(at, len);
      if ((k.u16(4) & 0xff01) == 1) {
        if (!k.has(0, 14) || !k.has(14, size_t(k.u16(6)) * 6))
          return false;
        uint32_t previous_pair = 0;
        for (unsigned j = 0; j < k.u16(6); ++j) {
          unsigned o = 14 + j * 6;
          uint32_t pair = k.u32(o);
          if (k.u16(o) >= glyphs || k.u16(o + 2) >= glyphs ||
              (j && pair <= previous_pair))
            return false;
          previous_pair = pair;
        }
      }
      at += len;
    }
  }
  if (!full_cmap.has(0, 4) || full_cmap.u16(2) > 64 ||
      !full_cmap.has(4, full_cmap.u16(2) * 8))
    return false;
  for (unsigned i = 0; i < full_cmap.u16(2); ++i) {
    unsigned o = 4 + i * 8, platform = full_cmap.u16(o),
             encoding = full_cmap.u16(o + 2);
    uint32_t off = full_cmap.u32(o + 4);
    if (!(platform == 0 ||
          (platform == 3 && (encoding == 1 || encoding == 10))) ||
        !full_cmap.has(off, 4))
      continue;
    unsigned format = full_cmap.u16(off);
    uint32_t len = format == 12 ? full_cmap.u32(size_t(off) + 4)
                                : full_cmap.u16(size_t(off) + 2);
    Bytes c = full_cmap.sub(off, len);
    if (format == 12) {
      if (!c.has(0, 16) || c.u32(12) > 65536 ||
          !c.has(16, size_t(c.u32(12)) * 12))
        return false;
      uint32_t last = 0;
      for (unsigned j = 0; j < c.u32(12); ++j) {
        unsigned p = 16 + j * 12;
        uint32_t a = c.u32(p), b = c.u32(p + 4), g = c.u32(p + 8);
        if (a > b || b > 0x10ffff || (j && a <= last) || g >= glyphs ||
            uint64_t(g) + b - a >= glyphs)
          return false;
        last = b;
      }
      cmap = c;
    } else if (format == 4 && (!cmap.p || cmap.u16(0) != 12)) {
      unsigned segments = c.u16(6) / 2;
      if (!c.has(0, 16) || (c.u16(6) & 1) || !segments || segments > 4096 ||
          !c.has(14, segments * 8 + 2))
        return false;
      unsigned last = 0;
      for (unsigned j = 0; j < segments; ++j) {
        unsigned end = c.u16(14 + j * 2),
                 start = c.u16(16 + segments * 2 + j * 2),
                 ro = 16 + segments * 6 + j * 2, range = c.u16(ro);
        if (start > end || (j && start <= last) ||
            (range & 1) || (range && !c.has(size_t(ro) + range, size_t(end - start + 1) * 2)))
          return false;
        last = end;
      }
      cmap = c;
    }
  }
  if (!cmap.p)
    return false;
  id = identity;
  name(names, 1, family, sizeof(family));
  name(names, 2, style, sizeof(style));
  name(names, 16, family, sizeof(family));
  name(names, 17, style, sizeof(style));
  name(names, 4, full_name, sizeof(full_name));
  name(names, 6, postscript, sizeof(postscript));
  if (!family[0])
    memcpy(family, "Unnamed TrueType", 17);
  return true;
}
unsigned Face::glyph(uint32_t cp) const {
  if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || !cmap.p)
    return 0;
  if (cmap.u16(0) == 12) {
    unsigned lo = 0, hi = cmap.u32(12);
    while (lo < hi) {
      unsigned mid = lo + (hi - lo) / 2, o = 16 + mid * 12;
      if (cp < cmap.u32(o))
        hi = mid;
      else if (cp > cmap.u32(o + 4))
        lo = mid + 1;
      else {
        uint64_t g = uint64_t(cmap.u32(o + 8)) + cp - cmap.u32(o);
        return g < glyphs ? unsigned(g) : 0;
      }
    }
    return 0;
  }
  if (cp > 0xffff)
    return 0;
  unsigned n = cmap.u16(6) / 2;
  for (unsigned i = 0; i < n; ++i) {
    if (cp > cmap.u16(14 + i * 2))
      continue;
    unsigned start = cmap.u16(16 + n * 2 + i * 2);
    if (cp < start)
      return 0;
    int delta = cmap.s16(16 + n * 4 + i * 2);
    unsigned ro = 16 + n * 6 + i * 2, range = cmap.u16(ro),
             g = range ? cmap.u16(size_t(ro) + range + (cp - start) * 2) : cp;
    if (!range || g)
      g = unsigned(int(g) + delta) & 65535;
    return g < glyphs ? g : 0;
  }
  return 0;
}
bool Face::location(unsigned g, Bytes &out) const {
  if (g >= glyphs)
    return false;
  uint32_t a = long_loca ? loca.u32(g * 4) : loca.u16(g * 2) * 2,
           b = long_loca ? loca.u32(g * 4 + 4) : loca.u16(g * 2 + 2) * 2;
  if (b < a || !glyf.has(a, b - a))
    return false;
  out = glyf.sub(a, b - a);
  return true;
}
int Face::advance(unsigned g, int px) const {
  if (g >= glyphs || px < 1 || px > 128)
    return 0;
  unsigned index = g < metrics ? g : metrics - 1;
  return int((int64_t(hmtx.u16(index * 4)) * px + units / 2) / units);
}
unsigned Face::advance_units(unsigned g) const {
  if (g >= glyphs || !metrics)
    return 0;
  return hmtx.u16((g < metrics ? g : metrics - 1) * 4);
}
int Face::bearing(unsigned g, int px) const {
  if (g >= glyphs)
    return 0;
  unsigned o = g < metrics ? g * 4 + 2 : metrics * 4 + (g - metrics) * 2;
  return int(int64_t(hmtx.s16(o)) * px / units);
}
int Face::line_height(int px) const {
  return int(
      (int64_t(ascent - descent + (gap > 0 ? gap : 0)) * px + units - 1) /
      units);
}
int Face::kerning(unsigned left, unsigned right, int px) const {
  return int(int64_t(kerning_units(left, right)) * px / units);
}
int Face::kerning_units(unsigned left, unsigned right) const {
  if (left >= glyphs || right >= glyphs || !kern.has(0, 4) ||
      kern.u16(0) != 0 || kern.u16(2) > 32)
    return 0;
  size_t at = 4;
  for (unsigned i = 0; i < kern.u16(2); ++i) {
    if (!kern.has(at, 6))
      return 0;
    unsigned len = kern.u16(at + 2), coverage = kern.u16(at + 4);
    if (len < 6 || !kern.has(at, len))
      return 0;
    Bytes k = kern.sub(at, len);
    if ((coverage & 0xff01) == 1 && k.has(0, 14)) {
      unsigned count = k.u16(6);
      if (!k.has(14, size_t(count) * 6))
        return 0;
      uint32_t key = (left << 16) | right;
      unsigned lo = 0, hi = count;
      while (lo < hi) {
        unsigned m = lo + (hi - lo) / 2, o = 14 + m * 6;
        uint32_t pair = k.u32(o);
        if (pair < key)
          lo = m + 1;
        else if (pair > key)
          hi = m;
        else
          return k.s16(o + 4);
      }
    }
    at += len;
  }
  return 0;
}
struct Point {
  int x{}, y{};
  bool on{};
};
struct Outline {
  stbtt_vertex vertices[MaxVertices]{};
  unsigned count{}, points{}, contours{}, dependencies{};
  unsigned path[MaxCompoundDepth]{};
  bool vertex(int kind, double x, double y, double cx = 0, double cy = 0) {
    if (count == MaxVertices || x < -32767 || x > 32767 || y < -32767 ||
        y > 32767 || cx < -32767 || cx > 32767 || cy < -32767 || cy > 32767)
      return false;
    auto &v = vertices[count++];
    v.type = uint8_t(kind);
    v.x = short(x);
    v.y = short(y);
    v.cx = short(cx);
    v.cy = short(cy);
    return true;
  }
};
struct Transform {
  double a{1}, b{}, c{}, d{1}, x{}, y{};
};
static bool outline(const Face &f, unsigned glyph, Outline &out, unsigned depth,
                    Transform t) {
  if (depth >= MaxCompoundDepth || ++out.dependencies > 64)
    return false;
  for (unsigned i = 0; i < depth; ++i)
    if (out.path[i] == glyph)
      return false;
  out.path[depth] = glyph;
  Bytes g;
  if (!f.location(glyph, g))
    return false;
  if (!g.n)
    return true;
  if (!g.has(0, 10))
    return false;
  int contours = g.s16(0);
  if (contours < 0) {
    if (contours != -1)
      return false;
    size_t at = 10;
    unsigned flags = 0;
    do {
      if (!g.has(at, 4))
        return false;
      flags = g.u16(at);
      unsigned child = g.u16(at + 2);
      at += 4;
      int x, y;
      if (!(flags & 2))
        return false;
      if (flags & 1) {
        if (!g.has(at, 4))
          return false;
        x = g.s16(at);
        y = g.s16(at + 2);
        at += 4;
      } else {
        if (!g.has(at, 2))
          return false;
        x = int8_t(g.p[at]);
        y = int8_t(g.p[at + 1]);
        at += 2;
      }
      Transform local;
      local.x = x;
      local.y = y;
      if (flags & 8) {
        if (!g.has(at, 2))
          return false;
        local.a = local.d = g.s16(at) / 16384.0;
        at += 2;
      } else if (flags & 64) {
        if (!g.has(at, 4))
          return false;
        local.a = g.s16(at) / 16384.0;
        local.d = g.s16(at + 2) / 16384.0;
        at += 4;
      } else if (flags & 128) {
        if (!g.has(at, 8))
          return false;
        local.a = g.s16(at) / 16384.0;
        local.b = g.s16(at + 2) / 16384.0;
        local.c = g.s16(at + 4) / 16384.0;
        local.d = g.s16(at + 6) / 16384.0;
        at += 8;
      }
      if (flags & 0x800) {
        double xx = local.a * local.x + local.c * local.y,
               yy = local.b * local.x + local.d * local.y;
        local.x = xx;
        local.y = yy;
      }
      Transform n{t.a * local.a + t.c * local.b,
                  t.b * local.a + t.d * local.b,
                  t.a * local.c + t.c * local.d,
                  t.b * local.c + t.d * local.d,
                  t.a * local.x + t.c * local.y + t.x,
                  t.b * local.x + t.d * local.y + t.y};
      if (!outline(f, child, out, depth + 1, n))
        return false;
    } while (flags & 32);
    if (flags & 256) {
      if (!g.has(at, 2) || !g.has(at + 2, g.u16(at)))
        return false;
    }
    return true;
  }
  if (unsigned(contours) > MaxContours - out.contours ||
      !g.has(10, unsigned(contours) * 2 + 2))
    return false;
  if (!contours)
    return true;
  unsigned count = unsigned(g.u16(10 + (contours - 1) * 2)) + 1;
  if (count > MaxPoints - out.points)
    return false;
  unsigned last = 0;
  for (int i = 0; i < contours; ++i) {
    unsigned end = g.u16(10 + i * 2);
    if ((i && end <= last) || end >= count)
      return false;
    last = end;
  }
  size_t at = 10 + unsigned(contours) * 2;
  unsigned instructions = g.u16(at);
  at += 2;
  if (!g.has(at, instructions))
    return false;
  at += instructions;
  auto *points = BSF_NEW Point[count];
  if (!points)
    return false;
  bool ok = true;
  auto fail = [&]() {
    delete[] points;
    return false;
  };
  // Flags are bounded and expanded before either coordinate stream is read.
  auto *flags = BSF_NEW uint8_t[count];
  if (!flags)
    return fail();
  for (unsigned i = 0; i < count;) {
    if (!g.has(at, 1)) {
      ok = false;
      break;
    }
    unsigned fl = g.p[at++], repeat = 0;
    if (fl & 8) {
      if (!g.has(at, 1)) {
        ok = false;
        break;
      }
      repeat = g.p[at++];
    }
    if (repeat >= count - i) {
      ok = false;
      break;
    }
    do {
      flags[i++] = uint8_t(fl);
    } while (repeat--);
  }
  for (unsigned axis = 0; axis < 2 && ok; ++axis) {
    int value = 0;
    for (unsigned i = 0; i < count; ++i) {
      unsigned fl = flags[i], short_bit = axis ? 4 : 2,
               same_bit = axis ? 32 : 16;
      int delta = 0;
      if (fl & short_bit) {
        if (!g.has(at, 1)) {
          ok = false;
          break;
        }
        delta = g.p[at++];
        if (!(fl & same_bit))
          delta = -delta;
      } else if (!(fl & same_bit)) {
        if (!g.has(at, 2)) {
          ok = false;
          break;
        }
        delta = g.s16(at);
        at += 2;
      }
      value += delta;
      if (value < -32767 || value > 32767) {
        ok = false;
        break;
      }
      if (axis)
        points[i].y = value;
      else
        points[i].x = value;
      points[i].on = fl & 1;
    }
  }
  delete[] flags;
  if (!ok)
    return fail();
  out.points += count;
  out.contours += unsigned(contours);
  auto v = [&](int kind, Point p, Point c = Point{}) {
    return out.vertex(kind, t.a * p.x + t.c * p.y + t.x,
                      t.b * p.x + t.d * p.y + t.y, t.a * c.x + t.c * c.y + t.x,
                      t.b * c.x + t.d * c.y + t.y);
  };
  unsigned start = 0;
  for (int contour = 0; contour < contours && ok; ++contour) {
    unsigned end = g.u16(10 + contour * 2);
    Point first = points[start], lastp = points[end], origin;
    unsigned pos = start;
    if (first.on) {
      origin = first;
      ++pos;
    } else if (lastp.on)
      origin = lastp;
    else
      origin = {(first.x + lastp.x) / 2, (first.y + lastp.y) / 2, true};
    ok = v(STBTT_vmove, origin);
    Point control{};
    bool pending = false;
    for (unsigned i = pos; i <= end && ok; ++i) {
      Point p = points[i];
      if (p.on) {
        ok = pending ? v(STBTT_vcurve, p, control) : v(STBTT_vline, p);
        pending = false;
      } else {
        if (pending) {
          Point mid{(control.x + p.x) / 2, (control.y + p.y) / 2, true};
          ok = v(STBTT_vcurve, mid, control);
        }
        control = p;
        pending = true;
      }
    }
    if (ok)
      ok = pending ? v(STBTT_vcurve, origin, control) : v(STBTT_vline, origin);
    start = end + 1;
  }
  delete[] points;
  return ok;
}
bool rasterize(const Face &f, unsigned id, int px, Glyph &result, uint8_t *mask,
               size_t capacity) {
  if (px < 1 || px > 128 || !mask || capacity < MaxBitmap * MaxBitmap)
    return false;
  auto *shape = BSF_NEW Outline;
  if (!shape)
    return false;
  bool ok = outline(f, id, *shape, 0, {});
  if (!ok) {
    delete shape;
    return false;
  }
  result.width = result.height = 0;
  result.advance = f.advance(id, px);
  if (!shape->count) {
    delete shape;
    return true;
  }
  int xmin = 32767, ymin = 32767, xmax = -32767, ymax = -32767;
  for (unsigned i = 0; i < shape->count; ++i) {
    auto &v = shape->vertices[i];
    if (v.x < xmin)
      xmin = v.x;
    if (v.x > xmax)
      xmax = v.x;
    if (v.y < ymin)
      ymin = v.y;
    if (v.y > ymax)
      ymax = v.y;
    if (v.type == STBTT_vcurve) {
      if (v.cx < xmin)
        xmin = v.cx;
      if (v.cx > xmax)
        xmax = v.cx;
      if (v.cy < ymin)
        ymin = v.cy;
      if (v.cy > ymax)
        ymax = v.cy;
    }
  }
  double scale = double(px) / f.units;
  int x = floor_int(xmin * scale), y = floor_int(-ymax * scale),
      w = ceil_int(xmax * scale) - x, h = ceil_int(-ymin * scale) - y;
  if (w < 0 || h < 0 || w > int(MaxBitmap) || h > int(MaxBitmap)) {
    delete shape;
    return false;
  }
  result.x = x;
  result.y = y;
  result.width = w;
  result.height = h;
  memset(mask, 0, size_t(w) * h);
  auto *arena = BSF_NEW Scratch;
  if (!arena) {
    delete shape;
    return false;
  }
  stbtt__bitmap bitmap{w, h, w, mask};
  // stb never receives font bytes, offsets, cmap or unchecked compound data.
  stbtt_Rasterize(&bitmap, .35f, shape->vertices, int(shape->count),
                  float(scale), float(scale), 0, 0, x, y, 1, arena);
  ok = !arena->failed;
  delete arena;
  delete shape;
  return ok;
}
Cache::~Cache() {
  delete[] slots_;
  delete[] storage_;
}
void Cache::clear() {
  if (slots_)
    for (unsigned i = 0; i < CacheSlots; ++i)
      slots_[i].face = 0;
  stats.bitmap_bytes = 0;
}
const Glyph *Cache::get(const Face &face, uint32_t cp, int px, unsigned style) {
  if (px < 1 || px > 128 || !face.id)
    return nullptr;
  return get_glyph(face, face.glyph(cp), px, style);
}
const Glyph *Cache::get_glyph(const Face &face, unsigned id, int px,
                              unsigned style) {
  if (px < 1 || px > 128 || !face.id || id >= face.glyphs)
    return nullptr;
  if (!slots_) {
    slots_ = BSF_NEW Glyph[CacheSlots];
    storage_ = BSF_NEW uint8_t[CacheBytes];
    if (!slots_ || !storage_) {
      delete[] slots_;
      delete[] storage_;
      slots_ = nullptr;
      storage_ = nullptr;
      return nullptr;
    }
  }
  unsigned oldest = CacheSlots;
  for (unsigned i = 0; i < CacheSlots; ++i) {
    auto &g = slots_[i];
    if (g.face == face.id && g.id == id && g.px == px && g.style == style) {
      g.used = ++clock_;
      ++stats.hits;
      return &g;
    }
    if (!g.pins && (oldest == CacheSlots || !g.face ||
                    (slots_[oldest].face && g.used < slots_[oldest].used)))
      oldest = i;
  }
  if (oldest == CacheSlots)
    return nullptr;
  ++stats.misses;
  auto &g = slots_[oldest];
  if (g.face) {
    ++stats.evictions;
    stats.bitmap_bytes -= size_t(g.width) * g.height;
  }
  g = Glyph{};
  g.alpha = storage_ + size_t(oldest) * MaxBitmap * MaxBitmap;
  if (!rasterize(face, id, px, g, g.alpha, MaxBitmap * MaxBitmap)) {
    ++stats.rejected;
    return nullptr;
  }
  g.face = face.id;
  g.id = uint16_t(id);
  g.px = uint16_t(px);
  g.style = uint16_t(style);
  g.used = ++clock_;
  ++stats.rasterizations;
  stats.bitmap_bytes += size_t(g.width) * g.height;
  return &g;
}
bool Cache::contains(const Face &face, uint32_t cp, int px,
                     unsigned style) const {
  return contains_glyph(face, face.glyph(cp), px, style);
}
bool Cache::contains_glyph(const Face &face, unsigned id, int px,
                           unsigned style) const {
  if (!slots_)
    return false;
  for (unsigned i = 0; i < CacheSlots; ++i) {
    const auto &g = slots_[i];
    if (g.face == face.id && g.id == id && g.px == px && g.style == style)
      return true;
  }
  return false;
}
size_t utf8(const char *s, size_t n, uint32_t &cp) {
  cp = 0xfffd;
  if (!s || !n)
    return 0;
  unsigned a = uint8_t(s[0]);
  if (a < 128) {
    cp = a;
    return 1;
  }
  unsigned len = a >= 0xc2 && a <= 0xdf   ? 2
                 : a >= 0xe0 && a <= 0xef ? 3
                 : a >= 0xf0 && a <= 0xf4 ? 4
                                          : 0;
  if (!len || len > n)
    return 1;
  uint32_t c = a & ((1u << (7 - len)) - 1);
  for (unsigned i = 1; i < len; ++i) {
    unsigned b = uint8_t(s[i]);
    if ((b & 0xc0) != 0x80)
      return 1;
    c = (c << 6) | (b & 63);
  }
  if ((len == 2 && c < 128) || (len == 3 && c < 2048) ||
      (len == 4 && c < 65536) || (c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff)
    return 1;
  cp = c;
  return len;
}
} // namespace BSF
