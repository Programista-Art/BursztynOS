// BSF 2.2 — WOFF 1.0 / WOFF2 decoding (W3C WOFF File Format 1.0, WOFF File
// Format 2.0). Every length read from the file is checked against the input
// size and the configured output limits before any allocation.
#include "webfont.h"
#include <new>
#include <string.h>
extern "C" {
#include "vendor/brotli/include/brotli/decode.h"
}
#ifdef BSF_NATIVE
#define WEB_NEW new
#else
#define WEB_NEW new (std::nothrow)
#endif
#if defined(BSF_NATIVE) && defined(__STDC_HOSTED__) && !__STDC_HOSTED__
// Ring 3 has no libc heap. Brotli is always created with BSF's bounded
// allocator (brotli_alloc); its default-allocator fallbacks fail closed.
extern "C" void *malloc(size_t) { return nullptr; }
extern "C" void free(void *) {}
#endif
namespace BSF {
bool inflate_zlib(const uint8_t *in, size_t n, uint8_t *out, size_t cap,
                  size_t *produced);
namespace {
constexpr uint32_t tag(const char *s) {
  return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
         (uint32_t(uint8_t(s[2])) << 8) | uint8_t(s[3]);
}
uint32_t rd32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
uint16_t rd16(const uint8_t *p) { return uint16_t((p[0] << 8) | p[1]); }
void wr16(uint8_t *p, unsigned v) {
  p[0] = uint8_t(v >> 8);
  p[1] = uint8_t(v);
}
void wr32(uint8_t *p, uint32_t v) {
  wr16(p, v >> 16);
  wr16(p + 2, v);
}
size_t pad4(size_t n) { return (n + 3) & ~size_t(3); }
constexpr unsigned MaxWebTables = 64;
struct OutTable {
  uint32_t tag{};
  uint8_t *data{}; // owned
  size_t length{};
};
// Assemble a plain sfnt from tables (sorted by tag here).
WebFontStatus assemble(uint32_t flavor, OutTable *t, unsigned n, const WebFontLimits &L,
                       DecodedWebFont &out) {
  for (unsigned i = 1; i < n; ++i) {
    OutTable v = t[i];
    unsigned j = i;
    while (j && t[j - 1].tag > v.tag) {
      t[j] = t[j - 1];
      --j;
    }
    t[j] = v;
  }
  for (unsigned i = 1; i < n; ++i)
    if (t[i].tag == t[i - 1].tag)
      return WebFontStatus::Malformed;
  size_t total = 12 + size_t(n) * 16;
  for (unsigned i = 0; i < n; ++i) {
    if (t[i].length > L.max_output || total > L.max_output - pad4(t[i].length))
      return WebFontStatus::TooLarge;
    total += pad4(t[i].length);
  }
  auto *bytes = WEB_NEW uint8_t[total];
  if (!bytes)
    return WebFontStatus::TooLarge;
  memset(bytes, 0, total);
  wr32(bytes, flavor);
  wr16(bytes + 4, n);
  unsigned power = 1, log2 = 0;
  while (power * 2 <= n) {
    power *= 2;
    ++log2;
  }
  wr16(bytes + 6, power * 16);
  wr16(bytes + 8, log2);
  wr16(bytes + 10, n * 16 - power * 16);
  size_t at = 12 + size_t(n) * 16;
  for (unsigned i = 0; i < n; ++i) {
    uint8_t *rec = bytes + 12 + i * 16;
    if (t[i].length)
      memcpy(bytes + at, t[i].data, t[i].length);
    uint32_t sum = 0;
    for (size_t k = 0; k < pad4(t[i].length); k += 4)
      sum += rd32(bytes + at + k);
    wr32(rec, t[i].tag);
    wr32(rec + 4, sum);
    wr32(rec + 8, uint32_t(at));
    wr32(rec + 12, uint32_t(t[i].length));
    at += pad4(t[i].length);
  }
  out.bytes = bytes;
  out.size = total;
  return WebFontStatus::Ok;
}
void release(OutTable *t, unsigned n) {
  for (unsigned i = 0; i < n; ++i)
    delete[] t[i].data;
}
// ------------------------------------------------------------- WOFF 1.0
WebFontStatus decode_woff(const uint8_t *in, size_t n, const WebFontLimits &L,
                          DecodedWebFont &out) {
  if (n < 44)
    return WebFontStatus::Malformed;
  uint32_t flavor = rd32(in + 4), length = rd32(in + 8);
  unsigned count = rd16(in + 12);
  uint32_t total_sfnt = rd32(in + 16), meta_off = rd32(in + 24), meta_len = rd32(in + 28),
           priv_off = rd32(in + 36), priv_len = rd32(in + 40);
  if (length != n || rd16(in + 14) != 0 || !count || count > MaxWebTables ||
      n < 44 + size_t(count) * 20)
    return WebFontStatus::Malformed;
  if (flavor == tag("ttcf"))
    return WebFontStatus::Unsupported;
  if (total_sfnt > L.max_output)
    return WebFontStatus::TooLarge;
  // Metadata / private blocks: bounds only (BSF does not use them).
  auto inside = [&](uint64_t off, uint64_t len) { return off <= n && len <= n - off; };
  if ((meta_len && (!inside(meta_off, meta_len) || meta_off < 44 + size_t(count) * 20)) ||
      (priv_len && (!inside(priv_off, priv_len) || priv_off < 44 + size_t(count) * 20)))
    return WebFontStatus::Malformed;
  // Pre-pass: declared output size and ratio are checked before any table
  // buffer is allocated (decompression-bomb guard).
  uint64_t declared = 12 + uint64_t(count) * 16;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t orig = rd32(in + 44 + i * 20 + 12);
    if (orig > L.max_output || (declared += pad4(orig)) > L.max_output)
      return WebFontStatus::TooLarge;
  }
  if (declared > uint64_t(n) * L.max_ratio)
    return WebFontStatus::TooLarge;
  OutTable tables[MaxWebTables];
  WebFontStatus status = WebFontStatus::Ok;
  unsigned made = 0;
  uint32_t previous = 0;
  for (unsigned i = 0; i < count && status == WebFontStatus::Ok; ++i) {
    const uint8_t *e = in + 44 + i * 20;
    uint32_t t = rd32(e), off = rd32(e + 4), comp = rd32(e + 8), orig = rd32(e + 12);
    if ((i && t <= previous) || off % 4 || !inside(off, comp) || comp > orig ||
        off < 44 + size_t(count) * 20) {
      status = WebFontStatus::Malformed;
      break;
    }
    previous = t;
    // Tables never overlap each other or the metadata/private blocks.
    for (unsigned j = 0; j < i; ++j) {
      const uint8_t *o = in + 44 + j * 20;
      uint64_t a = rd32(o + 4), b = a + rd32(o + 8);
      if (comp && off < b && a < uint64_t(off) + comp) {
        status = WebFontStatus::Malformed;
        break;
      }
    }
    if ((meta_len && off < uint64_t(meta_off) + meta_len && meta_off < uint64_t(off) + comp) ||
        (priv_len && off < uint64_t(priv_off) + priv_len && priv_off < uint64_t(off) + comp))
      status = WebFontStatus::Malformed;
    if (status != WebFontStatus::Ok)
      break;
    auto *buf = orig ? WEB_NEW uint8_t[orig] : nullptr;
    if (orig && !buf) {
      status = WebFontStatus::TooLarge;
      break;
    }
    tables[made++] = {t, buf, orig};
    if (comp == orig) {
      if (orig)
        memcpy(buf, in + off, orig);
    } else {
      size_t got = 0;
      if (!inflate_zlib(in + off, comp, buf, orig, &got) || got != orig)
        status = WebFontStatus::DecompressFailed;
    }
  }
  if (status == WebFontStatus::Ok)
    status = assemble(flavor, tables, made, L, out);
  release(tables, made);
  if (status == WebFontStatus::Ok)
    out.container = ContainerFormat::WOFF;
  return status;
}
// ------------------------------------------------------------- WOFF2
const char *const KnownTags[63] = {
    "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf",
    "loca", "prep", "CFF ", "VORG", "EBDT", "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT",
    "VDMX", "vhea", "vmtx", "BASE", "GDEF", "GPOS", "GSUB", "EBSC", "JSTF", "MATH", "CBDT",
    "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt", "avar", "bdat", "bloc", "bsln", "cvar",
    "fdsc", "feat", "fmtx", "fvar", "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd",
    "prop", "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill"};
struct Reader {
  const uint8_t *p{};
  size_t n{}, at{};
  bool bad{};
  bool has(size_t k) const { return !bad && at <= n && k <= n - at; }
  unsigned u8() {
    if (!has(1)) {
      bad = true;
      return 0;
    }
    return p[at++];
  }
  unsigned u16() {
    if (!has(2)) {
      bad = true;
      return 0;
    }
    unsigned v = rd16(p + at);
    at += 2;
    return v;
  }
  uint32_t u32() {
    if (!has(4)) {
      bad = true;
      return 0;
    }
    uint32_t v = rd32(p + at);
    at += 4;
    return v;
  }
  bool base128(uint32_t &v) {
    v = 0;
    for (unsigned i = 0; i < 5; ++i) {
      unsigned b = u8();
      if (bad || (i == 0 && b == 0x80) || (v & 0xfe000000u))
        return false;
      v = (v << 7) | (b & 0x7f);
      if (!(b & 0x80))
        return true;
    }
    return false;
  }
  bool u255(unsigned &v) {
    unsigned c = u8();
    if (bad)
      return false;
    if (c == 253)
      v = u16();
    else if (c == 255)
      v = 253 + u8();
    else if (c == 254)
      v = 506 + u8();
    else
      v = c;
    return !bad;
  }
  const uint8_t *take(size_t k) {
    if (!has(k)) {
      bad = true;
      return nullptr;
    }
    const uint8_t *r = p + at;
    at += k;
    return r;
  }
};
struct Entry {
  uint32_t tag{}, orig{}, transform_length{};
  unsigned version{};
  bool transformed{};
  size_t offset{}; // into the decompressed stream
};
// Growable output buffer with a hard cap.
struct Out {
  uint8_t *p{};
  size_t n{}, cap{}, limit{};
  bool bad{};
  bool reserve(size_t k) {
    if (bad || k > limit - n) {
      bad = true;
      return false;
    }
    if (n + k <= cap)
      return true;
    size_t next = cap ? cap * 2 : 4096;
    while (next < n + k)
      next *= 2;
    if (next > limit)
      next = limit;
    auto *q = WEB_NEW uint8_t[next];
    if (!q) {
      bad = true;
      return false;
    }
    if (n)
      memcpy(q, p, n);
    delete[] p;
    p = q;
    cap = next;
    return true;
  }
  void put8(unsigned v) {
    if (reserve(1))
      p[n++] = uint8_t(v);
  }
  void put16(unsigned v) {
    if (reserve(2)) {
      wr16(p + n, v);
      n += 2;
    }
  }
  void put32(uint32_t v) {
    if (reserve(4)) {
      wr32(p + n, v);
      n += 4;
    }
  }
  void put(const uint8_t *src, size_t k) {
    if (k && reserve(k)) {
      memcpy(p + n, src, k);
      n += k;
    }
  }
};
int with_sign(unsigned flag, int base) { return (flag & 1) ? base : -base; }
constexpr unsigned MaxWoff2Points = 65535, MaxWoff2Contours = 65535;
// Reconstruct glyf + loca from the WOFF2 transformed glyf table (§5.1).
WebFontStatus reconstruct_glyf(const uint8_t *data, size_t length, unsigned expected_glyphs,
                               const WebFontLimits &L, Out &glyf, Out &loca,
                               int16_t *x_min, unsigned &index_format) {
  Reader h{data, length};
  h.u16(); // reserved
  unsigned option = h.u16(), glyphs = h.u16();
  index_format = h.u16();
  uint32_t sizes[7];
  for (auto &s : sizes)
    s = h.u32();
  if (h.bad || index_format > 1 || glyphs != expected_glyphs || !glyphs)
    return WebFontStatus::TransformFailed;
  Reader streams[7];
  size_t at = h.at;
  for (unsigned i = 0; i < 7; ++i) {
    if (sizes[i] > length - at)
      return WebFontStatus::TransformFailed;
    streams[i] = Reader{data + at, sizes[i]};
    at += sizes[i];
  }
  Reader &contours = streams[0], &points = streams[1], &flags = streams[2],
         &glyph = streams[3], &composite = streams[4], &bbox = streams[5],
         &instructions = streams[6];
  const uint8_t *overlap = nullptr;
  if (option & 1) {
    size_t k = (size_t(glyphs) + 7) / 8;
    if (k > length - at)
      return WebFontStatus::TransformFailed;
    overlap = data + at;
  }
  size_t bitmap_bytes = 4 * ((size_t(glyphs) + 31) / 32);
  const uint8_t *bbox_bits = bbox.take(bitmap_bytes);
  if (!bbox_bits)
    return WebFontStatus::TransformFailed;
  glyf.limit = loca.limit = L.max_output;
  auto *end_points = WEB_NEW uint16_t[MaxWoff2Contours];
  auto *xs = WEB_NEW int32_t[MaxWoff2Points], *ys = WEB_NEW int32_t[MaxWoff2Points];
  auto *on = WEB_NEW uint8_t[MaxWoff2Points];
  WebFontStatus status = end_points && xs && ys && on ? WebFontStatus::Ok : WebFontStatus::TooLarge;
  for (unsigned g = 0; g < glyphs && status == WebFontStatus::Ok; ++g) {
    // loca entry for this glyph (start offset).
    if (index_format)
      loca.put32(uint32_t(glyf.n));
    else {
      if (glyf.n / 2 > 0xffff) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      loca.put16(unsigned(glyf.n / 2));
    }
    bool has_bbox = bbox_bits[g >> 3] & (0x80 >> (g & 7));
    int n_contours = int16_t(contours.u16());
    if (contours.bad) {
      status = WebFontStatus::TransformFailed;
      break;
    }
    x_min[g] = 0;
    if (n_contours == 0) {
      if (has_bbox)
        status = WebFontStatus::TransformFailed;
      continue;
    }
    size_t start = glyf.n;
    if (n_contours == -1) {
      if (!has_bbox) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      glyf.put16(0xffff);
      for (unsigned k = 0; k < 4; ++k)
        glyf.put16(bbox.u16());
      x_min[g] = int16_t(rd16(glyf.p + start + 2));
      // Component records: copy, tracking WE_HAVE_INSTRUCTIONS.
      bool more = true, instr = false;
      unsigned components = 0;
      while (more && !composite.bad) {
        unsigned f = composite.u16();
        unsigned component = composite.u16();
        size_t arg = (f & 1) ? 4 : 2;
        size_t scale = (f & 8) ? 2 : (f & 0x40) ? 4 : (f & 0x80) ? 8 : 0;
        const uint8_t *rec = composite.take(arg + scale);
        if (!rec || ++components > 1024)
          break;
        glyf.put16(f);
        glyf.put16(component);
        glyf.put(rec, arg + scale);
        instr |= (f & 0x100) != 0;
        more = (f & 0x20) != 0;
      }
      if (composite.bad || more) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      if (instr) {
        unsigned ilen;
        if (!glyph.u255(ilen)) {
          status = WebFontStatus::TransformFailed;
          break;
        }
        const uint8_t *ip = instructions.take(ilen);
        if (!ip) {
          status = WebFontStatus::TransformFailed;
          break;
        }
        glyf.put16(ilen);
        glyf.put(ip, ilen);
      }
    } else if (n_contours > 0) {
      if (unsigned(n_contours) > MaxWoff2Contours) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      unsigned total = 0;
      for (int c = 0; c < n_contours; ++c) {
        unsigned k;
        if (!points.u255(k) || total + k > MaxWoff2Points) {
          status = WebFontStatus::TransformFailed;
          break;
        }
        total += k;
        if (!total) {
          status = WebFontStatus::TransformFailed;
          break;
        }
        end_points[c] = uint16_t(total - 1);
      }
      if (status != WebFontStatus::Ok)
        break;
      int x = 0, y = 0;
      int bx0 = 32767, by0 = 32767, bx1 = -32768, by1 = -32768;
      for (unsigned p = 0; p < total; ++p) {
        unsigned fb = flags.u8();
        unsigned f = fb & 0x7f;
        unsigned bytes = f < 84 ? 1 : f < 120 ? 2 : f < 124 ? 3 : 4;
        const uint8_t *in = glyph.take(bytes);
        if (!in || flags.bad) {
          status = WebFontStatus::TransformFailed;
          break;
        }
        int dx, dy;
        if (f < 10) {
          dx = 0;
          dy = with_sign(f, ((f & 14) << 7) + in[0]);
        } else if (f < 20) {
          dx = with_sign(f, (((f - 10) & 14) << 7) + in[0]);
          dy = 0;
        } else if (f < 84) {
          unsigned b0 = f - 20, b1 = in[0];
          dx = with_sign(f, int(1 + (b0 & 0x30) + (b1 >> 4)));
          dy = with_sign(f >> 1, int(1 + ((b0 & 0x0c) << 2) + (b1 & 0x0f)));
        } else if (f < 120) {
          unsigned b0 = f - 84;
          dx = with_sign(f, int(1 + ((b0 / 12) << 8) + in[0]));
          dy = with_sign(f >> 1, int(1 + (((b0 % 12) >> 2) << 8) + in[1]));
        } else if (f < 124) {
          dx = with_sign(f, int((in[0] << 4) + (in[1] >> 4)));
          dy = with_sign(f >> 1, int(((in[1] & 0x0f) << 8) + in[2]));
        } else {
          dx = with_sign(f, int((in[0] << 8) + in[1]));
          dy = with_sign(f >> 1, int((in[2] << 8) + in[3]));
        }
        x += dx;
        y += dy;
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767) {
          status = WebFontStatus::TransformFailed;
          break;
        }
        xs[p] = x;
        ys[p] = y;
        on[p] = !(fb & 0x80);
        if (x < bx0) bx0 = x;
        if (x > bx1) bx1 = x;
        if (y < by0) by0 = y;
        if (y > by1) by1 = y;
      }
      if (status != WebFontStatus::Ok)
        break;
      unsigned ilen;
      if (!glyph.u255(ilen)) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      const uint8_t *ip = instructions.take(ilen);
      if (!ip) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      int box[4] = {bx0, by0, bx1, by1};
      if (has_bbox)
        for (int &b : box)
          b = int16_t(bbox.u16());
      if (bbox.bad) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      glyf.put16(unsigned(n_contours));
      for (int b : box)
        glyf.put16(uint16_t(b));
      x_min[g] = int16_t(box[0]);
      for (int c = 0; c < n_contours; ++c)
        glyf.put16(end_points[c]);
      glyf.put16(ilen);
      glyf.put(ip, ilen);
      // Flags (with repeat), then x and y coordinate arrays (short/same).
      bool overlap_first = overlap && (overlap[g >> 3] & (0x80 >> (g & 7)));
      // Flags use the shortest encoding: SHORT_VECTOR / IS_SAME and REPEAT.
      auto point_flag = [&](unsigned p) {
        int dx = xs[p] - (p ? xs[p - 1] : 0), dy = ys[p] - (p ? ys[p - 1] : 0);
        unsigned fl = on[p] ? 1 : 0;
        if (p == 0 && overlap_first)
          fl |= 0x40;
        if (dx == 0)
          fl |= 0x10;
        else if (dx > -256 && dx < 256)
          fl |= 0x02 | (dx > 0 ? 0x10 : 0);
        if (dy == 0)
          fl |= 0x20;
        else if (dy > -256 && dy < 256)
          fl |= 0x04 | (dy > 0 ? 0x20 : 0);
        return fl;
      };
      for (unsigned p = 0; p < total && !glyf.bad;) {
        unsigned fl = point_flag(p), run = 0;
        while (p + 1 + run < total && run < 255 && point_flag(p + 1 + run) == fl)
          ++run;
        if (run) {
          glyf.put8(fl | 0x08);
          glyf.put8(run);
        } else
          glyf.put8(fl);
        p += 1 + run;
      }
      for (unsigned axis = 0; axis < 2 && !glyf.bad; ++axis) {
        int prev = 0;
        for (unsigned p = 0; p < total; ++p) {
          int v = axis ? ys[p] : xs[p], d = v - prev;
          prev = v;
          if (d == 0)
            continue;
          if (d > -256 && d < 256)
            glyf.put8(unsigned(d < 0 ? -d : d));
          else
            glyf.put16(uint16_t(d));
        }
      }
    } else {
      status = WebFontStatus::TransformFailed;
      break;
    }
    while (!glyf.bad && glyf.n % 4)
      glyf.put8(0);
    if (glyf.bad || loca.bad)
      status = WebFontStatus::TooLarge;
  }
  if (status == WebFontStatus::Ok) {
    if (index_format)
      loca.put32(uint32_t(glyf.n));
    else if (glyf.n / 2 > 0xffff)
      status = WebFontStatus::TransformFailed;
    else
      loca.put16(unsigned(glyf.n / 2));
    if (glyf.bad || loca.bad)
      status = WebFontStatus::TooLarge;
  }
  for (unsigned i = 0; i < 7; ++i)
    if (streams[i].bad)
      status = WebFontStatus::TransformFailed;
  delete[] end_points;
  delete[] xs;
  delete[] ys;
  delete[] on;
  return status;
}
void *brotli_alloc(void *opaque, size_t size) {
  auto *budget = static_cast<size_t *>(opaque);
  // Brotli's internal state (ring buffer ≤ 16 MiB window + tables).
  if (size > *budget)
    return nullptr;
  auto *p = WEB_NEW uint8_t[size + 16];
  if (!p)
    return nullptr;
  *budget -= size;
  memcpy(p, &size, sizeof(size));
  return p + 16;
}
void brotli_free(void *opaque, void *address) {
  if (!address)
    return;
  auto *p = static_cast<uint8_t *>(address) - 16;
  size_t size;
  memcpy(&size, p, sizeof(size));
  *static_cast<size_t *>(opaque) += size;
  delete[] p;
}
WebFontStatus decode_woff2(const uint8_t *in, size_t n, const WebFontLimits &L,
                           DecodedWebFont &out) {
  Reader r{in, n};
  if (n < 48 || r.u32() != tag("wOF2"))
    return WebFontStatus::Malformed;
  uint32_t flavor = r.u32(), length = r.u32();
  unsigned count = r.u16();
  unsigned reserved = r.u16();
  uint32_t total_sfnt = r.u32(), total_compressed = r.u32();
  r.u32(); // version
  uint32_t meta_off = r.u32(), meta_len = r.u32();
  r.u32(); // metaOrigLength
  uint32_t priv_off = r.u32(), priv_len = r.u32();
  if (r.bad || length != n || reserved || !count || count > MaxWebTables)
    return WebFontStatus::Malformed;
  if (flavor == tag("ttcf"))
    return WebFontStatus::Unsupported; // WOFF2 collections: not supported
  if (total_sfnt > L.max_output)
    return WebFontStatus::TooLarge;
  Entry entries[MaxWebTables];
  uint64_t stream = 0;
  for (unsigned i = 0; i < count; ++i) {
    unsigned flags = r.u8();
    Entry &e = entries[i];
    unsigned index = flags & 63;
    if (index == 63)
      e.tag = r.u32();
    else
      e.tag = tag(KnownTags[index]);
    e.version = flags >> 6;
    if (!r.base128(e.orig))
      return WebFontStatus::Malformed;
    bool glyf_like = e.tag == tag("glyf") || e.tag == tag("loca");
    e.transformed = glyf_like ? e.version == 0 : e.version != 0;
    if (glyf_like && e.version != 0 && e.version != 3)
      return WebFontStatus::Malformed;
    if (!glyf_like && e.tag != tag("hmtx") && e.version != 0)
      return WebFontStatus::Unsupported; // unknown transform
    if (e.tag == tag("hmtx") && e.version > 1)
      return WebFontStatus::Unsupported;
    e.transform_length = e.orig;
    if (e.transformed && !r.base128(e.transform_length))
      return WebFontStatus::Malformed;
    if (e.tag == tag("loca") && e.transformed && e.transform_length)
      return WebFontStatus::Malformed;
    if (e.orig > L.max_output || e.transform_length > L.max_output)
      return WebFontStatus::TooLarge;
    for (unsigned j = 0; j < i; ++j)
      if (entries[j].tag == e.tag)
        return WebFontStatus::Malformed;
    e.offset = size_t(stream);
    stream += e.transform_length;
    if (stream > L.max_output)
      return WebFontStatus::TooLarge;
  }
  if (r.bad)
    return WebFontStatus::Malformed;
  // glyf and loca are transformed together or not at all.
  int glyf_i = -1, loca_i = -1, hmtx_i = -1, hhea_i = -1, maxp_i = -1, head_i = -1;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t t = entries[i].tag;
    if (t == tag("glyf")) glyf_i = int(i);
    if (t == tag("loca")) loca_i = int(i);
    if (t == tag("hmtx")) hmtx_i = int(i);
    if (t == tag("hhea")) hhea_i = int(i);
    if (t == tag("maxp")) maxp_i = int(i);
    if (t == tag("head")) head_i = int(i);
  }
  if ((glyf_i < 0) != (loca_i < 0) ||
      (glyf_i >= 0 && entries[glyf_i].transformed != entries[loca_i].transformed))
    return WebFontStatus::Malformed;
  if (hmtx_i >= 0 && entries[hmtx_i].transformed && (glyf_i < 0 || !entries[glyf_i].transformed))
    return WebFontStatus::Malformed;
  size_t compressed_at = r.at;
  if (total_compressed > n - compressed_at)
    return WebFontStatus::Malformed;
  if ((meta_len && (meta_off > n || meta_len > n - meta_off || meta_off < compressed_at + total_compressed)) ||
      (priv_len && (priv_off > n || priv_len > n - priv_off || priv_off < compressed_at + total_compressed)))
    return WebFontStatus::Malformed;
  if (stream > uint64_t(n) * L.max_ratio || uint64_t(total_sfnt) > uint64_t(n) * L.max_ratio)
    return WebFontStatus::TooLarge;
  // Brotli: exact output size, bounded allocator.
  auto *data = stream ? WEB_NEW uint8_t[size_t(stream)] : nullptr;
  if (stream && !data)
    return WebFontStatus::TooLarge;
  size_t budget = 40u * 1024 * 1024;
  BrotliDecoderState *dec = BrotliDecoderCreateInstance(brotli_alloc, brotli_free, &budget);
  WebFontStatus status = dec ? WebFontStatus::Ok : WebFontStatus::TooLarge;
  if (dec) {
    size_t avail_in = total_compressed, avail_out = size_t(stream);
    const uint8_t *next_in = in + compressed_at;
    uint8_t *next_out = data;
    BrotliDecoderResult res =
        BrotliDecoderDecompressStream(dec, &avail_in, &next_in, &avail_out, &next_out, nullptr);
    if (res != BROTLI_DECODER_RESULT_SUCCESS || avail_out || avail_in)
      status = WebFontStatus::DecompressFailed;
    BrotliDecoderDestroyInstance(dec);
  }
  OutTable tables[MaxWebTables];
  unsigned made = 0;
  Out glyf, loca;
  int16_t *x_min = nullptr;
  unsigned glyph_count = 0, metrics = 0, index_format = 0;
  if (status == WebFontStatus::Ok && glyf_i >= 0 && entries[glyf_i].transformed) {
    if (maxp_i < 0 || entries[maxp_i].transformed || entries[maxp_i].orig < 6 || head_i < 0 ||
        entries[head_i].orig < 54)
      status = WebFontStatus::TransformFailed;
    else {
      glyph_count = rd16(data + entries[maxp_i].offset + 4);
      x_min = glyph_count ? WEB_NEW int16_t[glyph_count] : nullptr;
      if (!x_min)
        status = WebFontStatus::TransformFailed;
      else
        status = reconstruct_glyf(data + entries[glyf_i].offset, entries[glyf_i].transform_length,
                                  glyph_count, L, glyf, loca, x_min, index_format);
    }
  }
  for (unsigned i = 0; i < count && status == WebFontStatus::Ok; ++i) {
    const Entry &e = entries[i];
    OutTable t{e.tag, nullptr, 0};
    if (e.tag == tag("glyf") && e.transformed) {
      t.data = glyf.p;
      t.length = glyf.n;
      glyf.p = nullptr;
    } else if (e.tag == tag("loca") && e.transformed) {
      t.data = loca.p;
      t.length = loca.n;
      loca.p = nullptr;
    } else if (e.tag == tag("hmtx") && e.transformed) {
      if (hhea_i < 0 || entries[hhea_i].orig < 36 || !x_min) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      metrics = rd16(data + entries[hhea_i].offset + 34);
      if (!metrics || metrics > glyph_count) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      Reader h{data + e.offset, e.transform_length};
      unsigned fl = h.u8();
      if ((fl & 0xfc) || !(fl & 3)) {
        status = WebFontStatus::TransformFailed;
        break;
      }
      size_t size = size_t(metrics) * 4 + size_t(glyph_count - metrics) * 2;
      t.data = WEB_NEW uint8_t[size];
      t.length = size;
      if (!t.data) {
        status = WebFontStatus::TooLarge;
        break;
      }
      for (unsigned g = 0; g < metrics; ++g)
        wr16(t.data + g * 4, h.u16());
      for (unsigned g = 0; g < metrics; ++g)
        wr16(t.data + g * 4 + 2, (fl & 1) ? uint16_t(x_min[g]) : h.u16());
      for (unsigned g = metrics; g < glyph_count; ++g)
        wr16(t.data + metrics * 4 + (g - metrics) * 2, (fl & 2) ? uint16_t(x_min[g]) : h.u16());
      if (h.bad || h.at != h.n)
        status = WebFontStatus::TransformFailed;
    } else {
      if (e.transformed) {
        status = WebFontStatus::Unsupported;
        break;
      }
      t.length = e.orig;
      t.data = e.orig ? WEB_NEW uint8_t[e.orig] : nullptr;
      if (e.orig && !t.data) {
        status = WebFontStatus::TooLarge;
        break;
      }
      if (e.orig)
        memcpy(t.data, data + e.offset, e.orig);
    }
    tables[made++] = t;
  }
  // head.indexToLocFormat must agree with the reconstructed loca.
  if (status == WebFontStatus::Ok && glyf_i >= 0 && entries[glyf_i].transformed)
    for (unsigned i = 0; i < made; ++i)
      if (tables[i].tag == tag("head") && tables[i].length >= 54)
        wr16(tables[i].data + 50, index_format);
  if (status == WebFontStatus::Ok)
    status = assemble(flavor, tables, made, L, out);
  release(tables, made);
  delete[] glyf.p;
  delete[] loca.p;
  delete[] x_min;
  delete[] data;
  if (status == WebFontStatus::Ok)
    out.container = ContainerFormat::WOFF2;
  return status;
}
} // namespace
const char *web_font_status_name(WebFontStatus s) {
  switch (s) {
  case WebFontStatus::Ok: return "ok";
  case WebFontStatus::Unsupported: return "unsupported";
  case WebFontStatus::Malformed: return "malformed";
  case WebFontStatus::TooLarge: return "too-large";
  case WebFontStatus::DecompressFailed: return "decompress-failed";
  case WebFontStatus::TransformFailed: return "transform-failed";
  case WebFontStatus::InvalidFont: return "invalid-font";
  }
  return "?";
}
WebFontStatus decode_web_font(const uint8_t *in, size_t n, DecodedWebFont &out,
                              const WebFontLimits &L) {
  out = DecodedWebFont{};
  if (!in || n < 12)
    return WebFontStatus::Malformed;
  if (n > L.max_input)
    return WebFontStatus::TooLarge;
  uint32_t sig = rd32(in);
  WebFontStatus status;
  if (sig == 0x10000 || sig == tag("true") || sig == tag("OTTO")) {
    if (n > L.max_output)
      return WebFontStatus::TooLarge;
    out.bytes = WEB_NEW uint8_t[n];
    if (!out.bytes)
      return WebFontStatus::TooLarge;
    memcpy(out.bytes, in, n);
    out.size = n;
    status = WebFontStatus::Ok;
  } else if (sig == tag("wOFF"))
    status = decode_woff(in, n, L, out);
  else if (sig == tag("wOF2"))
    status = decode_woff2(in, n, L, out);
  else
    return WebFontStatus::Unsupported;
  if (status != WebFontStatus::Ok) {
    delete[] out.bytes;
    out = DecodedWebFont{};
    return status;
  }
  // The decoded sfnt goes through the shared validation.
  Face check;
  if (!check.open(out.bytes, out.size, 1)) {
    delete[] out.bytes;
    out = DecodedWebFont{};
    return WebFontStatus::InvalidFont;
  }
  return WebFontStatus::Ok;
}
} // namespace BSF
