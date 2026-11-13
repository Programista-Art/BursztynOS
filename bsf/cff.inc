// BSF 2.2 — OpenType CFF 1.0 / CFF2 outline decoder (Ring 3 only).
// Included by bsf.cpp inside namespace BSF after the Outline definition.
//
// Every structure is read through Bytes (bounds-checked). The interpreter is
// bounded by an explicit operator budget, a subroutine depth limit, the
// operand stack limit, the hint count limit and the shared vertex limit of
// the rasterizer. No allocation happens while interpreting a charstring.
//
// References: Adobe TN #5176 (CFF), TN #5177 (Type 2 charstrings), OpenType
// 1.9 "CFF2 — Compact Font Format (CFF) Version 2".

namespace {
// INDEX: count (CFF1 uint16, CFF2 uint32), offSize, offsets, data.
bool cff_index(Bytes t, size_t at, bool cff2, CffIndex &out, size_t &end) {
  out = CffIndex{};
  if (!t.has(at, cff2 ? 4 : 2))
    return false;
  uint32_t count = cff2 ? t.u32(at) : t.u16(at);
  size_t p = at + (cff2 ? 4 : 2);
  if (!count) {
    out.present = true;
    end = p;
    return true;
  }
  if (count > CffMaxIndexCount || !t.has(p, 1))
    return false;
  unsigned size = t.p[p];
  if (size < 1 || size > 4)
    return false;
  size_t offsets = p + 1, table = size_t(count + 1) * size;
  if (!t.has(offsets, table))
    return false;
  auto offset = [&](uint32_t i) {
    uint32_t v = 0;
    for (unsigned k = 0; k < size; ++k)
      v = (v << 8) | t.p[offsets + size_t(i) * size + k];
    return v;
  };
  // Offsets are 1-based relative to the byte before the data.
  size_t data = offsets + table - 1;
  if (offset(0) != 1)
    return false;
  uint32_t last = offset(count);
  if (last < 1 || !t.has(data, last))
    return false;
  // Monotonic offsets are validated lazily per item (item()). A full scan of
  // a 65,535-glyph CJK INDEX at open time is still cheap and gives early
  // rejection, so do it once here.
  uint32_t previous = 1;
  for (uint32_t i = 1; i <= count; ++i) {
    uint32_t v = offset(i);
    if (v < previous)
      return false;
    previous = v;
  }
  out.present = true;
  out.count = count;
  out.off_size = uint8_t(size);
  out.offsets = uint32_t(offsets);
  out.data = uint32_t(data);
  end = data + last;
  return end <= t.n;
}
} // namespace
bool CffIndex::item(Bytes t, uint32_t i, Bytes &out) const {
  out = Bytes{};
  if (!present || i >= count || off_size < 1 || off_size > 4)
    return false;
  auto offset = [&](uint32_t k, uint32_t &v) {
    size_t at = size_t(offsets) + size_t(k) * off_size;
    if (!t.has(at, off_size))
      return false;
    v = 0;
    for (unsigned b = 0; b < off_size; ++b)
      v = (v << 8) | t.p[at + b];
    return true;
  };
  uint32_t a, b;
  if (!offset(i, a) || !offset(i + 1, b) || a < 1 || b < a ||
      !t.has(size_t(data) + a, b - a))
    return false;
  out = t.sub(size_t(data) + a, b - a);
  // An empty item is valid (empty glyph / empty subroutine).
  if (!out.p)
    out.p = t.p + data + a;
  return true;
}
namespace {
// DICT operand stack (operands are numbers; reals are parsed exactly enough
// for FontMatrix and blend deltas).
struct DictValues {
  double v[CffMaxStack]{};
  unsigned n{};
};
bool dict_real(Bytes d, size_t &at, double &out) {
  // Nibble-encoded real: digits, '.', E, E-, '-', end (0xf).
  double mantissa = 0, scale = 1;
  int exponent = 0, esign = 1;
  bool negative = false, fraction = false, in_exp = false, any = false;
  for (unsigned guard = 0; guard < 64; ++guard) {
    if (!d.has(at, 1))
      return false;
    unsigned b = d.p[at++];
    for (unsigned half = 0; half < 2; ++half) {
      unsigned nib = half ? b & 15 : b >> 4;
      if (nib <= 9) {
        any = true;
        if (in_exp) {
          if (exponent < 1000)
            exponent = exponent * 10 + int(nib);
        } else if (fraction) {
          scale /= 10;
          mantissa += nib * scale;
        } else if (mantissa < 1e15)
          mantissa = mantissa * 10 + nib;
      } else if (nib == 0xa)
        fraction = true;
      else if (nib == 0xb)
        in_exp = true, esign = 1;
      else if (nib == 0xc)
        in_exp = true, esign = -1;
      else if (nib == 0xe)
        negative = true;
      else if (nib == 0xf) {
        if (!any)
          return false;
        int e = esign * exponent;
        if (e > 30 || e < -30)
          return false;
        double r = mantissa;
        for (; e > 0; --e)
          r *= 10;
        for (; e < 0; ++e)
          r /= 10;
        out = negative ? -r : r;
        return true;
      } else
        return false; // 0xd reserved
    }
  }
  return false;
}
// Visit each operator of a DICT. op = byte, or 1200 + second byte for escape.
// The callback sees the operands accumulated since the previous operator.
// CFF2 blend (op 23) is resolved in-place using the supplied region count
// and scalars (null = default instance: deltas dropped).
template <class F>
bool cff_dict(Bytes d, bool cff2, unsigned regions, const float *scalars,
              F &&visit) {
  DictValues s;
  size_t at = 0;
  unsigned ops = 0;
  while (at < d.n) {
    if (++ops > 4096)
      return false;
    unsigned b = d.p[at];
    if (b <= 21 || (cff2 && (b == 22 || b == 23 || b == 24))) {
      ++at;
      unsigned op = b;
      if (b == 12) {
        if (!d.has(at, 1))
          return false;
        op = 1200 + d.p[at++];
      }
      if (cff2 && op == 23) { // blend in a DICT
        if (!s.n)
          return false;
        double count = s.v[s.n - 1];
        if (count < 0 || count > CffMaxStack)
          return false;
        unsigned n = unsigned(count);
        uint64_t need = uint64_t(n) * (regions + 1) + 1;
        if (need > s.n)
          return false;
        unsigned base = s.n - unsigned(need);
        for (unsigned i = 0; i < n; ++i) {
          double value = s.v[base + i];
          if (scalars)
            for (unsigned r = 0; r < regions; ++r)
              value += scalars[r] * s.v[base + n + i * regions + r];
          s.v[base + i] = value;
        }
        s.n = base + n;
        continue;
      }
      if (!visit(op, s))
        return false;
      s.n = 0;
      continue;
    }
    if (s.n == (cff2 ? CffMaxStack : 48u))
      return false;
    double value;
    if (b == 28) {
      if (!d.has(at, 3))
        return false;
      value = int16_t(d.u16(at + 1));
      at += 3;
    } else if (b == 29) {
      if (!d.has(at, 5))
        return false;
      value = int32_t(d.u32(at + 1));
      at += 5;
    } else if (b == 30) {
      ++at;
      if (!dict_real(d, at, value))
        return false;
    } else if (b >= 32 && b <= 246) {
      value = int(b) - 139;
      ++at;
    } else if (b >= 247 && b <= 250) {
      if (!d.has(at, 2))
        return false;
      value = (int(b) - 247) * 256 + d.p[at + 1] + 108;
      at += 2;
    } else if (b >= 251 && b <= 254) {
      if (!d.has(at, 2))
        return false;
      value = -(int(b) - 251) * 256 - d.p[at + 1] - 108;
      at += 2;
    } else
      return false; // 22..27, 31, 255 reserved in DICT data
    s.v[s.n++] = value;
  }
  return true;
}
bool dict_offset(double v, size_t limit, uint32_t &out) {
  if (!(v >= 0) || v > double(limit) || v != double(int64_t(v)))
    return false;
  out = uint32_t(v);
  return true;
}
// ItemVariationStore: number of regions referenced by a VariationData entry.
// The store itself is validated by the variations module; here only the
// count is needed (bounded reads).
unsigned cff2_region_count(Bytes ivs, unsigned vsindex) {
  if (!ivs.has(0, 8) || ivs.u16(0) != 1)
    return 0;
  unsigned count = ivs.u16(6);
  if (vsindex >= count || !ivs.has(8, size_t(count) * 4))
    return 0;
  uint32_t off = ivs.u32(8 + vsindex * 4);
  if (!ivs.has(off, 6))
    return 0;
  unsigned regions = ivs.u16(off + 4);
  return regions <= CffMaxRegions ? regions : 0;
}
} // namespace

bool CffFace::open(Bytes table, unsigned glyphs, bool cff2) {
  *this = CffFace{};
  if (!table.has(0, 4) || table.n > MaxFile)
    return false;
  version = cff2 ? 2 : 1;
  unsigned major = table.p[0], header = table.p[2];
  if (major != version || header < 4 || !table.has(0, header))
    return false;
  this->table = table;
  Bytes top{};
  size_t at = header;
  if (cff2) {
    if (!table.has(0, 5))
      return false;
    unsigned length = table.u16(3);
    top = table.sub(header, length);
    if (!top.p || !length)
      return false;
    at = header + length;
    size_t end = 0;
    if (!cff_index(table, at, true, global_subrs, end))
      return false;
  } else {
    CffIndex names{}, tops{}, strings{};
    size_t end = 0;
    if (!cff_index(table, at, false, names, end) || names.count != 1)
      return false; // OpenType CFF holds exactly one font
    if (!cff_index(table, end, false, tops, end) || tops.count != 1 ||
        !tops.item(table, 0, top))
      return false;
    if (!cff_index(table, end, false, strings, end))
      return false;
    string_count = strings.count;
    if (!cff_index(table, end, false, global_subrs, end))
      return false;
  }
  uint32_t charstrings_at = 0, private_size = 0, private_at = 0,
           fdarray_at = 0, fdselect_at = 0, charset_at = 0, vstore_at = 0;
  bool have_private = false, have_charstrings = false;
  double m[6]{};
  bool have_matrix = false;
  unsigned charstring_type = 2;
  bool ok = cff_dict(top, cff2, 0, nullptr, [&](unsigned op, DictValues &s) {
    switch (op) {
    case 17:
      have_charstrings = s.n == 1 && dict_offset(s.v[0], table.n, charstrings_at);
      return have_charstrings;
    case 18:
      if (cff2 || s.n != 2)
        return false;
      have_private = dict_offset(s.v[0], table.n, private_size) &&
                     dict_offset(s.v[1], table.n, private_at);
      return have_private;
    case 15:
      if (s.n != 1 || !dict_offset(s.v[0], table.n, charset_at))
        return false;
      return true;
    case 24:
      return cff2 && s.n == 1 && dict_offset(s.v[0], table.n, vstore_at);
    case 1206:
      if (s.n != 1)
        return false;
      charstring_type = unsigned(s.v[0]);
      return true;
    case 1207:
      if (s.n != 6)
        return false;
      for (unsigned i = 0; i < 6; ++i)
        m[i] = s.v[i];
      have_matrix = true;
      return true;
    case 1230:
      cid = true;
      return true;
    case 1236:
      return s.n == 1 && dict_offset(s.v[0], table.n, fdarray_at);
    case 1237:
      return s.n == 1 && dict_offset(s.v[0], table.n, fdselect_at);
    default:
      return true; // metadata operators are ignored, operands consumed
    }
  });
  if (!ok || !have_charstrings || charstring_type != 2)
    return false;
  size_t end = 0;
  if (!cff_index(table, charstrings_at, cff2, charstrings, end) ||
      charstrings.count != glyphs || !glyphs)
    return false;
  if (have_matrix) {
    // Only a non-degenerate, bounded matrix is honoured.
    double det = m[0] * m[3] - m[1] * m[2];
    if (!(det > 1e-12 || det < -1e-12))
      return false;
    for (double v : m)
      if (v > 64 || v < -64)
        return false;
    for (unsigned i = 0; i < 6; ++i)
      matrix[i] = m[i];
    custom_matrix = !(m[0] == .001 && m[1] == 0 && m[2] == 0 &&
                      m[3] == .001 && m[4] == 0 && m[5] == 0);
  }
  if (cff2 && vstore_at) {
    if (!table.has(vstore_at, 2))
      return false;
    // CFF2 vstore: uint16 length followed by an ItemVariationStore.
    vstore = table.sub(vstore_at + 2, table.u16(vstore_at));
    if (!vstore.p || !vstore.has(0, 8) || vstore.u16(0) != 1)
      return false;
  }
  if (fdarray_at) {
    if (!cff_index(table, fdarray_at, cff2, fd_array, end) ||
        !fd_array.count || fd_array.count > 256)
      return false;
    if (fdselect_at) {
      if (!table.has(fdselect_at, 1))
        return false;
      fd_select = table.sub(fdselect_at, table.n - fdselect_at);
      unsigned format = fd_select.p[0];
      if (format == 0) {
        if (!fd_select.has(1, glyphs))
          return false;
        fd_select.n = 1 + glyphs;
        for (unsigned g = 0; g < glyphs; ++g)
          if (fd_select.p[1 + g] >= fd_array.count)
            return false;
      } else if (format == 3 || (cff2 && format == 4)) {
        bool wide = format == 4;
        size_t head = wide ? 5 : 3, rec = wide ? 6 : 3;
        if (!fd_select.has(0, head))
          return false;
        uint32_t ranges = wide ? fd_select.u32(1) : fd_select.u16(1);
        if (!ranges || ranges > glyphs ||
            !fd_select.has(head, size_t(ranges) * rec + (wide ? 4 : 2)))
          return false;
        uint32_t previous = 0;
        for (uint32_t r = 0; r < ranges; ++r) {
          size_t o = head + size_t(r) * rec;
          uint32_t first = wide ? fd_select.u32(o) : fd_select.u16(o);
          unsigned fd = wide ? fd_select.u16(o + 4) : fd_select.p[o + 2];
          if ((r == 0 && first != 0) || (r && first <= previous) ||
              fd >= fd_array.count)
            return false;
          previous = first;
        }
        uint32_t sentinel = wide ? fd_select.u32(head + size_t(ranges) * rec)
                                 : fd_select.u16(head + size_t(ranges) * rec);
        if (sentinel != glyphs || previous >= sentinel)
          return false;
        fd_select.n = head + size_t(ranges) * rec + (wide ? 4 : 2);
      } else
        return false;
    } else if (fd_array.count != 1)
      return false; // CFF2 without FDSelect uses FD 0 only
    // Every FD must have a usable Private DICT (validated now, used later).
    cached_privates = 0;
    for (unsigned fd = 0; fd < fd_array.count; ++fd) {
      CffPrivate p{};
      if (!private_for(fd, p, nullptr, 0))
        return false;
      if (fd < CachedPrivates) {
        privates[fd] = p;
        cached_privates = uint8_t(fd + 1);
      }
    }
  } else if (cid || cff2)
    return false;
  if (!fdarray_at) {
    if (!have_private)
      return false;
    private_dict = table.sub(private_at, private_size);
    if (private_size && !private_dict.p)
      return false;
    CffPrivate p{};
    if (!parse_private(private_dict, private_at, p, nullptr, 0))
      return false;
    privates[0] = p;
    cached_privates = 1;
  }
  if (!cff2 && !cid && charset_at > 2) {
    // Custom charset, used only to resolve seac accent components.
    charset = table.sub(charset_at, table.n - charset_at);
    if (!charset.p)
      charset = Bytes{};
  } else if (!cff2 && !cid)
    charset_predefined = uint8_t(charset_at); // 0 ISOAdobe, 1/2 Expert
  glyph_count = glyphs;
  return true;
}
bool CffFace::parse_private(Bytes dict, uint32_t dict_at, CffPrivate &out,
                            const int16_t *coords, unsigned axes) const {
  out = CffPrivate{};
  uint32_t subrs = 0;
  unsigned vsindex = 0;
  bool cff2 = version == 2;
  // A CFF2 Private DICT may select vsindex before blending; region count is
  // resolved from the vstore for the active vsindex.
  float scalars[CffMaxRegions]{};
  unsigned regions = 0;
  bool scaled = cff2 && coords && axes &&
                ivs_scalars(vstore, 0, coords, axes, scalars, regions);
  if (cff2 && !scaled)
    regions = cff2_region_count(vstore, 0);
  bool ok = cff_dict(dict, cff2, regions, scaled ? scalars : nullptr,
                     [&](unsigned op, DictValues &s) {
    if (op == 19) {
      if (s.n != 1 || !dict_offset(s.v[0], table.n, subrs))
        return false;
      return true;
    }
    if (op == 20 && s.n == 1)
      out.default_width = s.v[0];
    if (op == 21 && s.n == 1)
      out.nominal_width = s.v[0];
    if (cff2 && op == 22) {
      if (s.n != 1 || s.v[0] < 0 || s.v[0] > 65535)
        return false;
      vsindex = unsigned(s.v[0]);
    }
    return true;
  });
  if (!ok)
    return false;
  out.vsindex = vsindex;
  if (subrs) {
    uint64_t at = uint64_t(dict_at) + subrs;
    size_t end = 0;
    if (at > table.n || !cff_index(table, size_t(at), cff2, out.subrs, end))
      return false;
  }
  return true;
}
bool CffFace::private_for(unsigned fd, CffPrivate &out,
                          const int16_t *coords, unsigned axes) const {
  if (fd < cached_privates) {
    out = privates[fd];
    return true;
  }
  if (!fd_array.present)
    return parse_private(private_dict,
                         uint32_t(private_dict.p ? private_dict.p - table.p : 0),
                         out, coords, axes);
  Bytes font{};
  if (!fd_array.item(table, fd, font))
    return false;
  uint32_t size = 0, at = 0;
  bool found = false;
  bool ok = cff_dict(font, version == 2, 0, nullptr,
                     [&](unsigned op, DictValues &s) {
    if (op == 18) {
      found = s.n == 2 && dict_offset(s.v[0], table.n, size) &&
              dict_offset(s.v[1], table.n, at);
      return found;
    }
    return true;
  });
  if (!ok || !found)
    return false;
  Bytes dict = table.sub(at, size);
  if (size && !dict.p)
    return false;
  return parse_private(dict, at, out, coords, axes);
}
unsigned CffFace::fd_for(unsigned glyph) const {
  if (!fd_select.p || glyph >= glyph_count)
    return 0;
  unsigned format = fd_select.p[0];
  if (format == 0)
    return fd_select.p[1 + glyph];
  bool wide = format == 4;
  size_t head = wide ? 5 : 3, rec = wide ? 6 : 3;
  uint32_t ranges = wide ? fd_select.u32(1) : fd_select.u16(1);
  uint32_t lo = 0, hi = ranges;
  while (lo + 1 < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    uint32_t first = wide ? fd_select.u32(head + size_t(mid) * rec)
                          : fd_select.u16(head + size_t(mid) * rec);
    if (first <= glyph)
      lo = mid;
    else
      hi = mid;
  }
  size_t o = head + size_t(lo) * rec;
  return wide ? fd_select.u16(o + 4) : fd_select.p[o + 2];
}
// Standard Encoding code -> SID (only the codes used by seac accents/bases
// matter; the full Adobe StandardEncoding table is reproduced for 32..255).
static uint16_t standard_sid(unsigned code) {
  static const uint8_t low[95] = { // codes 32..126 map to SID 1..95
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
      20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
      38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
      56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
      74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91,
      92, 93, 94, 95};
  if (code >= 32 && code <= 126)
    return low[code - 32];
  static const struct {
    uint8_t code, sid;
  } high[] = {{161, 96},  {162, 97},  {163, 98},  {164, 99},  {165, 100},
              {166, 101}, {167, 102}, {168, 103}, {169, 104}, {170, 105},
              {171, 106}, {172, 107}, {173, 108}, {174, 109}, {175, 110},
              {177, 111}, {178, 112}, {179, 113}, {180, 114}, {182, 115},
              {183, 116}, {184, 117}, {185, 118}, {186, 119}, {187, 120},
              {188, 121}, {189, 122}, {191, 123}, {193, 124}, {194, 125},
              {195, 126}, {196, 127}, {197, 128}, {198, 129}, {199, 130},
              {200, 131}, {202, 132}, {203, 133}, {205, 134}, {206, 135},
              {207, 136}, {208, 137}, {225, 138}, {227, 139}, {232, 140},
              {233, 141}, {234, 142}, {235, 143}, {241, 144}, {245, 145},
              {248, 146}, {249, 147}, {250, 148}, {251, 149}};
  for (const auto &h : high)
    if (h.code == code)
      return h.sid;
  return 0;
}
unsigned CffFace::glyph_for_sid(unsigned sid) const {
  if (!sid)
    return 0;
  if (charset_predefined == 0) {
    // ISOAdobe charset: glyph id == SID for SID <= 228.
    return sid < glyph_count && sid <= 228 ? sid : 0;
  }
  if (!charset.p || !charset.has(0, 1))
    return 0;
  unsigned format = charset.p[0];
  unsigned gid = 1;
  size_t at = 1;
  if (format == 0) {
    for (; gid < glyph_count; ++gid, at += 2) {
      if (!charset.has(at, 2))
        return 0;
      if (charset.u16(at) == sid)
        return gid;
    }
    return 0;
  }
  if (format != 1 && format != 2)
    return 0;
  while (gid < glyph_count) {
    size_t rec = format == 1 ? 3 : 4;
    if (!charset.has(at, rec))
      return 0;
    unsigned first = charset.u16(at),
             left = format == 1 ? charset.p[at + 2] : charset.u16(at + 2);
    if (sid >= first && sid <= first + left)
      return gid + (sid - first) < glyph_count ? gid + (sid - first) : 0;
    gid += left + 1;
    at += rec;
  }
  return 0;
}

namespace {
// Sink: outline points in font units (doubles), forwarded to the shared
// vertex buffer with the face matrix applied.
struct CffSink {
  Outline &out;
  const CffFace &font;
  double scale_units; // matrix scale factor (upem) when custom_matrix
  double precision;
  double ox{}, oy{}; // seac/accent offset
  bool open{};
  double sx{}, sy{}; // contour start
  double cx{}, cy{}; // current point
  bool map(double x, double y, double &X, double &Y) const {
    x += ox;
    y += oy;
    if (font.custom_matrix) {
      const double *m = font.matrix;
      double tx = (m[0] * x + m[2] * y + m[4]) * scale_units,
             ty = (m[1] * x + m[3] * y + m[5]) * scale_units;
      x = tx;
      y = ty;
    }
    X = x * precision;
    Y = y * precision;
    return X > -32767 && X < 32767 && Y > -32767 && Y < 32767;
  }
  bool close() {
    if (open && (cx != sx || cy != sy)) {
      double X, Y;
      if (!map(sx, sy, X, Y) || !out.vertex(STBTT_vline, X, Y))
        return false;
    }
    open = false;
    return true;
  }
  bool move(double x, double y) {
    if (!close())
      return false;
    double X, Y;
    if (!map(x, y, X, Y) || !out.vertex(STBTT_vmove, X, Y))
      return false;
    if (++out.contours > CffMaxContours)
      return false;
    open = true;
    sx = cx = x;
    sy = cy = y;
    return true;
  }
  bool ensure_open() { return open || move(cx, cy); }
  bool line(double x, double y) {
    if (!ensure_open())
      return false;
    double X, Y;
    if (!map(x, y, X, Y) || !out.vertex(STBTT_vline, X, Y))
      return false;
    cx = x;
    cy = y;
    return true;
  }
  bool curve(double x1, double y1, double x2, double y2, double x3,
             double y3) {
    if (!ensure_open())
      return false;
    double X1, Y1, X2, Y2, X3, Y3;
    if (!map(x1, y1, X1, Y1) || !map(x2, y2, X2, Y2) || !map(x3, y3, X3, Y3) ||
        !out.cubic(X3, Y3, X1, Y1, X2, Y2))
      return false;
    cx = x3;
    cy = y3;
    return true;
  }
};
struct CffMachine {
  const CffFace &font;
  CffSink &sink;
  CffPrivate local{};
  const int16_t *coords{}; // CFF2 normalized instance (F2Dot14), or null
  unsigned axes{};
  double stack[CffMaxStack]{};
  unsigned sp{}, limit{};
  double transient[32]{};
  unsigned stems{}, ops{}, depth{};
  bool width_done{}, ended{}, seac_depth{};
  double width{};
  double x{}, y{};
  unsigned vsindex{};
  bool cff2() const { return font.version == 2; }
  bool push(double v) {
    if (sp >= limit)
      return false;
    stack[sp++] = v;
    return true;
  }
  // The first stack-clearing operator may carry the advance width (CFF1).
  void take_width(bool odd_means_width, unsigned expected) {
    if (width_done || cff2())
      return;
    width_done = true;
    bool has = odd_means_width ? (sp & 1) : sp > expected;
    if (has && sp) {
      width = local.nominal_width + stack[0];
      for (unsigned i = 1; i < sp; ++i)
        stack[i - 1] = stack[i];
      --sp;
    } else
      width = local.default_width;
  }
  bool hints() {
    take_width(true, 0);
    if (sp & 1)
      return false;
    stems += sp / 2;
    sp = 0;
    return stems <= CffMaxStems;
  }
  unsigned regions_for(unsigned index) const {
    return cff2_region_count(font.vstore, index);
  }
  bool blend() {
    if (!cff2() || !sp)
      return false;
    double count = stack[sp - 1];
    if (count < 0 || count > CffMaxStack)
      return false;
    unsigned n = unsigned(count), k = regions_for(vsindex);
    uint64_t need = uint64_t(n) * (k + 1) + 1;
    if (need > sp)
      return false;
    unsigned base = sp - unsigned(need);
    float scalars[CffMaxRegions]{};
    unsigned got = 0;
    bool scaled = coords && axes &&
                  ivs_scalars(font.vstore, vsindex, coords, axes, scalars, got);
    if (scaled && got != k)
      return false;
    for (unsigned i = 0; i < n; ++i) {
      double value = stack[base + i];
      if (scaled)
        for (unsigned r = 0; r < k; ++r)
          value += double(scalars[r]) * stack[base + n + i * k + r];
      stack[base + i] = value;
    }
    sp = base + n;
    return true;
  }
  bool run(Bytes code) {
    if (++depth > CffMaxSubrDepth)
      return false;
    size_t at = 0;
    while (at < code.n && !ended) {
      if (++ops > CffMaxOperations)
        return false;
      unsigned b = code.p[at++];
      if (b >= 32 || b == 28) {
        double v;
        if (b == 28) {
          if (!code.has(at, 2))
            return false;
          v = int16_t(code.u16(at));
          at += 2;
        } else if (b <= 246)
          v = int(b) - 139;
        else if (b <= 250) {
          if (!code.has(at, 1))
            return false;
          v = (int(b) - 247) * 256 + code.p[at++] + 108;
        } else if (b <= 254) {
          if (!code.has(at, 1))
            return false;
          v = -(int(b) - 251) * 256 - code.p[at++] - 108;
        } else {
          if (!code.has(at, 4))
            return false;
          v = int32_t(code.u32(at)) / 65536.0;
          at += 4;
        }
        if (!push(v))
          return false;
        continue;
      }
      switch (b) {
      case 1:  // hstem
      case 3:  // vstem
      case 18: // hstemhm
      case 23: // vstemhm
        if (!hints())
          return false;
        break;
      case 19: // hintmask
      case 20: // cntrmask
        if (sp) {
          // Implicit vstem operands before the first mask.
          if (!hints())
            return false;
        } else
          take_width(true, 0);
        {
          size_t bytes = (stems + 7) / 8;
          if (!code.has(at, bytes))
            return false;
          at += bytes;
        }
        break;
      case 21: // rmoveto
        take_width(false, 2);
        if (sp < 2)
          return false;
        x += stack[sp - 2];
        y += stack[sp - 1];
        sp = 0;
        if (!sink.move(x, y))
          return false;
        break;
      case 22: // hmoveto
        take_width(false, 1);
        if (sp < 1)
          return false;
        x += stack[sp - 1];
        sp = 0;
        if (!sink.move(x, y))
          return false;
        break;
      case 4: // vmoveto
        take_width(false, 1);
        if (sp < 1)
          return false;
        y += stack[sp - 1];
        sp = 0;
        if (!sink.move(x, y))
          return false;
        break;
      case 5: // rlineto
        if (sp < 2 || (sp & 1))
          return false;
        for (unsigned i = 0; i < sp; i += 2) {
          x += stack[i];
          y += stack[i + 1];
          if (!sink.line(x, y))
            return false;
        }
        sp = 0;
        break;
      case 6: // hlineto
      case 7: // vlineto
      {
        if (!sp)
          return false;
        bool horizontal = b == 6;
        for (unsigned i = 0; i < sp; ++i) {
          if (horizontal)
            x += stack[i];
          else
            y += stack[i];
          horizontal = !horizontal;
          if (!sink.line(x, y))
            return false;
        }
        sp = 0;
        break;
      }
      case 8: // rrcurveto
        if (sp < 6 || sp % 6)
          return false;
        for (unsigned i = 0; i < sp; i += 6)
          if (!curve(stack[i], stack[i + 1], stack[i + 2], stack[i + 3],
                     stack[i + 4], stack[i + 5]))
            return false;
        sp = 0;
        break;
      case 24: // rcurveline
        if (sp < 8 || (sp - 2) % 6)
          return false;
        for (unsigned i = 0; i + 2 < sp; i += 6)
          if (!curve(stack[i], stack[i + 1], stack[i + 2], stack[i + 3],
                     stack[i + 4], stack[i + 5]))
            return false;
        x += stack[sp - 2];
        y += stack[sp - 1];
        if (!sink.line(x, y))
          return false;
        sp = 0;
        break;
      case 25: // rlinecurve
        if (sp < 8 || (sp - 6) & 1)
          return false;
        for (unsigned i = 0; i + 6 < sp; i += 2) {
          x += stack[i];
          y += stack[i + 1];
          if (!sink.line(x, y))
            return false;
        }
        if (!curve(stack[sp - 6], stack[sp - 5], stack[sp - 4], stack[sp - 3],
                   stack[sp - 2], stack[sp - 1]))
          return false;
        sp = 0;
        break;
      case 26: // vvcurveto: dx1? {dya dxb dyb dyc}+
      {
        unsigned i = 0;
        double dx1 = 0;
        if (sp & 1)
          dx1 = stack[i++];
        if ((sp - i) < 4 || (sp - i) % 4)
          return false;
        for (; i < sp; i += 4) {
          if (!curve(dx1, stack[i], stack[i + 1], stack[i + 2], 0,
                     stack[i + 3]))
            return false;
          dx1 = 0;
        }
        sp = 0;
        break;
      }
      case 27: // hhcurveto: dy1? {dxa dxb dyb dxc}+
      {
        unsigned i = 0;
        double dy1 = 0;
        if (sp & 1)
          dy1 = stack[i++];
        if ((sp - i) < 4 || (sp - i) % 4)
          return false;
        for (; i < sp; i += 4) {
          if (!curve(stack[i], dy1, stack[i + 1], stack[i + 2], stack[i + 3],
                     0))
            return false;
          dy1 = 0;
        }
        sp = 0;
        break;
      }
      case 30: // vhcurveto
      case 31: // hvcurveto
      {
        if (sp < 4)
          return false;
        unsigned rest = sp % 8;
        if (rest != 0 && rest != 1 && rest != 4 && rest != 5)
          return false;
        bool horizontal = b == 31;
        unsigned i = 0;
        while (sp - i >= 4) {
          bool last = sp - i == 5;
          double d1 = stack[i], d2 = stack[i + 1], d3 = stack[i + 2],
                 d4 = stack[i + 3], d5 = last ? stack[i + 4] : 0;
          bool ok = horizontal ? curve(d1, 0, d2, d3, d5, d4)
                               : curve(0, d1, d2, d3, d4, d5);
          if (!ok)
            return false;
          i += last ? 5 : 4;
          horizontal = !horizontal;
        }
        if (i != sp)
          return false;
        sp = 0;
        break;
      }
      case 10: // callsubr
      case 29: // callgsubr
      {
        if (!sp)
          return false;
        const CffIndex &index = b == 10 ? local.subrs : font.global_subrs;
        if (!index.present || !index.count)
          return false;
        uint32_t count = index.count;
        int bias = count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
        double v = stack[--sp] + bias;
        if (v < 0 || v >= count || v != double(int64_t(v)))
          return false;
        Bytes sub{};
        if (!index.item(font.table, uint32_t(v), sub))
          return false;
        if (!run(sub))
          return false;
        if (ended)
          return true;
        break;
      }
      case 11: // return
        if (cff2())
          return false;
        --depth;
        return true;
      case 14: // endchar
        if (cff2())
          return false;
        take_width(true, 0); // 1 or 5 operands: the first is the width
        if (sp == 4 || sp == 5) {
          // seac-like: adx ady bchar achar (Type 1 compatibility).
          unsigned base = sp - 4;
          double adx = stack[base], ady = stack[base + 1];
          unsigned bchar = unsigned(stack[base + 2]),
                   achar = unsigned(stack[base + 3]);
          sp = 0;
          if (seac_depth)
            return false;
          if (!sink.close())
            return false;
          if (!seac(bchar, achar, adx, ady))
            return false;
          ended = true;
          return true;
        }
        if (sp)
          return false;
        if (!sink.close())
          return false;
        ended = true;
        return true;
      case 15: // vsindex (CFF2)
        if (!cff2() || sp != 1 || stack[0] < 0 || stack[0] > 65535)
          return false;
        vsindex = unsigned(stack[0]);
        sp = 0;
        break;
      case 16: // blend (CFF2)
        if (!blend())
          return false;
        break;
      case 12: {
        if (!code.has(at, 1))
          return false;
        unsigned e = code.p[at++];
        if (!escape(e))
          return false;
        break;
      }
      default:
        return false; // reserved operators
      }
    }
    --depth;
    return true;
  }
  bool curve(double dxa, double dya, double dxb, double dyb, double dxc,
             double dyc) {
    double x1 = x + dxa, y1 = y + dya, x2 = x1 + dxb, y2 = y1 + dyb,
           x3 = x2 + dxc, y3 = y2 + dyc;
    x = x3;
    y = y3;
    return sink.curve(x1, y1, x2, y2, x3, y3);
  }
  bool escape(unsigned e) {
    auto need = [&](unsigned n) { return sp >= n; };
    switch (e) {
    case 35: { // flex: dx1 dy1 dx2 dy2 dx3 dy3 dx4 dy4 dx5 dy5 dx6 dy6 fd
      if (sp != 13)
        return false;
      double *s = stack;
      sp = 0;
      return curve(s[0], s[1], s[2], s[3], s[4], s[5]) &&
             curve(s[6], s[7], s[8], s[9], s[10], s[11]);
    }
    case 34: { // hflex: dx1 dx2 dy2 dx3 dx4 dx5 dx6
      if (sp != 7)
        return false;
      double *s = stack, y0 = y;
      sp = 0;
      if (!curve(s[0], 0, s[1], s[2], s[3], 0))
        return false;
      return curve(s[4], 0, s[5], y0 - y, s[6], 0);
    }
    case 36: { // hflex1: dx1 dy1 dx2 dy2 dx3 dx4 dx5 dy5 dx6
      if (sp != 9)
        return false;
      double *s = stack, y0 = y;
      sp = 0;
      if (!curve(s[0], s[1], s[2], s[3], s[4], 0))
        return false;
      double dy6 = y0 - (y + s[7]);
      return curve(s[5], 0, s[6], s[7], s[8], dy6);
    }
    case 37: { // flex1: dx1 dy1 ... dx5 dy5 d6
      if (sp != 11)
        return false;
      double *s = stack, x0 = x, y0 = y, dx = 0, dy = 0;
      for (unsigned i = 0; i < 10; i += 2) {
        dx += s[i];
        dy += s[i + 1];
      }
      sp = 0;
      double ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
      if (!curve(s[0], s[1], s[2], s[3], s[4], s[5]))
        return false;
      double dx6, dy6;
      if (ax > ay) {
        dx6 = s[10];
        dy6 = y0 - (y + s[7] + s[9]);
      } else {
        dy6 = s[10];
        dx6 = x0 - (x + s[6] + s[8]);
      }
      return curve(s[6], s[7], s[8], s[9], dx6, dy6);
    }
    // Arithmetic / storage operators (Type 2 §4.4-4.6). Deprecated in CFF2.
    case 3: // and
      if (cff2() || !need(2))
        return false;
      stack[sp - 2] = (stack[sp - 2] != 0 && stack[sp - 1] != 0);
      --sp;
      return true;
    case 4: // or
      if (cff2() || !need(2))
        return false;
      stack[sp - 2] = (stack[sp - 2] != 0 || stack[sp - 1] != 0);
      --sp;
      return true;
    case 5: // not
      if (cff2() || !need(1))
        return false;
      stack[sp - 1] = stack[sp - 1] == 0;
      return true;
    case 9: // abs
      if (cff2() || !need(1))
        return false;
      if (stack[sp - 1] < 0)
        stack[sp - 1] = -stack[sp - 1];
      return true;
    case 10: // add
      if (cff2() || !need(2))
        return false;
      stack[sp - 2] += stack[sp - 1];
      --sp;
      return bounded(stack[sp - 1]);
    case 11: // sub
      if (cff2() || !need(2))
        return false;
      stack[sp - 2] -= stack[sp - 1];
      --sp;
      return bounded(stack[sp - 1]);
    case 12: // div
      if (cff2() || !need(2) || stack[sp - 1] == 0)
        return false;
      stack[sp - 2] /= stack[sp - 1];
      --sp;
      return bounded(stack[sp - 1]);
    case 14: // neg
      if (cff2() || !need(1))
        return false;
      stack[sp - 1] = -stack[sp - 1];
      return true;
    case 15: // eq
      if (cff2() || !need(2))
        return false;
      stack[sp - 2] = stack[sp - 2] == stack[sp - 1];
      --sp;
      return true;
    case 18: // drop
      if (cff2() || !need(1))
        return false;
      --sp;
      return true;
    case 20: { // put
      if (cff2() || !need(2))
        return false;
      double i = stack[sp - 1];
      if (i < 0 || i >= 32)
        return false;
      transient[unsigned(i)] = stack[sp - 2];
      sp -= 2;
      return true;
    }
    case 21: { // get
      if (cff2() || !need(1))
        return false;
      double i = stack[sp - 1];
      if (i < 0 || i >= 32)
        return false;
      stack[sp - 1] = transient[unsigned(i)];
      return true;
    }
    case 22: // ifelse s1 s2 v1 v2
      if (cff2() || !need(4))
        return false;
      stack[sp - 4] = stack[sp - 2] <= stack[sp - 1] ? stack[sp - 4]
                                                     : stack[sp - 3];
      sp -= 3;
      return true;
    case 23: // random: deterministic (rendering must be reproducible)
      if (cff2())
        return false;
      return push(.5);
    case 24: // mul
      if (cff2() || !need(2))
        return false;
      stack[sp - 2] *= stack[sp - 1];
      --sp;
      return bounded(stack[sp - 1]);
    case 26: { // sqrt
      if (cff2() || !need(1) || stack[sp - 1] < 0)
        return false;
      stack[sp - 1] = square_root(stack[sp - 1]);
      return true;
    }
    case 27: // dup
      if (cff2() || !need(1))
        return false;
      return push(stack[sp - 1]);
    case 28: { // exch
      if (cff2() || !need(2))
        return false;
      double t = stack[sp - 1];
      stack[sp - 1] = stack[sp - 2];
      stack[sp - 2] = t;
      return true;
    }
    case 29: { // index
      if (cff2() || !need(1))
        return false;
      double i = stack[sp - 1];
      if (i < 0)
        i = 0;
      if (i >= sp - 1)
        return false;
      stack[sp - 1] = stack[sp - 2 - unsigned(i)];
      return true;
    }
    case 30: { // roll: N J
      if (cff2() || !need(2))
        return false;
      double nn = stack[sp - 2], jj = stack[sp - 1];
      sp -= 2;
      if (nn < 0 || nn > sp || nn != double(int64_t(nn)) ||
          jj != double(int64_t(jj)) || jj > 1e6 || jj < -1e6)
        return false;
      unsigned n = unsigned(nn);
      if (!n)
        return true;
      long j = long(jj) % long(n);
      if (j < 0)
        j += n;
      double tmp[CffMaxStack];
      for (unsigned i = 0; i < n; ++i)
        tmp[(i + unsigned(j)) % n] = stack[sp - n + i];
      for (unsigned i = 0; i < n; ++i)
        stack[sp - n + i] = tmp[i];
      return true;
    }
    default:
      return false;
    }
  }
  static bool bounded(double v) { return v > -1e9 && v < 1e9; }
  bool seac(unsigned bchar, unsigned achar, double adx, double ady);
};
bool cff_charstring(const CffFace &font, unsigned glyph, CffSink &sink,
                    double &width, const int16_t *coords, unsigned axes,
                    bool nested);
bool CffMachine::seac(unsigned bchar, unsigned achar, double adx, double ady) {
  if (font.version != 1 || font.cid)
    return false;
  unsigned bsid = standard_sid(bchar), asid = standard_sid(achar);
  unsigned base = font.glyph_for_sid(bsid), accent = font.glyph_for_sid(asid);
  if (!base || !accent)
    return false;
  double w = 0;
  double ox = sink.ox, oy = sink.oy;
  if (!cff_charstring(font, base, sink, w, nullptr, 0, true))
    return false;
  // Accent offset: adx - sbx; Type 2 has no sbx, so it is the plain offset.
  sink.ox = ox + adx;
  sink.oy = oy + ady;
  bool ok = cff_charstring(font, accent, sink, w, nullptr, 0, true);
  sink.ox = ox;
  sink.oy = oy;
  return ok;
}
bool cff_charstring(const CffFace &font, unsigned glyph, CffSink &sink,
                    double &width, const int16_t *coords, unsigned axes,
                    bool nested) {
  Bytes code{};
  if (glyph >= font.glyph_count || !font.charstrings.item(font.table, glyph, code))
    return false;
  CffMachine m{font, sink};
  m.limit = font.version == 2 ? CffMaxStack : 48;
  m.seac_depth = nested;
  m.coords = coords;
  m.axes = axes;
  if (!font.private_for(font.fd_for(glyph), m.local, coords, axes))
    return false;
  m.vsindex = m.local.vsindex;
  sink.cx = sink.cy = 0;
  if (!m.run(code))
    return false;
  if (font.version == 1 && !m.ended)
    return false; // CFF1 charstrings must end with endchar
  if (!sink.close())
    return false;
  width = m.width;
  return true;
}
} // namespace
bool cff_outline(const Face &f, unsigned glyph, Outline &out, double precision) {
  const CffFace &font = f.cff_face;
  if (!font.version)
    return false;
  CffSink sink{out, font, double(f.units), precision};
  double width = 0;
  return cff_charstring(font, glyph, sink, width,
                        f.variable() ? f.coords : nullptr, f.axis_count, false);
}
