// BSF 2.2 — OpenType font variations, shared helpers (Ring 3 only).
// Included by bsf.cpp inside namespace BSF.
//
// Coordinates are normalized F2Dot14 values after avar mapping. Every table
// offset is bounds-checked; region/axis counts are capped (CffMaxRegions,
// MaxAxes) so the work per lookup is bounded.

// Scalar of one region axis (OpenType "Algorithm for interpolation").
static float axis_scalar(int start, int peak, int end, int coord) {
  if (start > peak || peak > end)
    return 1;
  if (start < 0 && end > 0 && peak != 0)
    return 1;
  if (peak == 0 || coord == peak)
    return 1;
  if (coord <= start || coord >= end)
    return 0;
  if (coord < peak)
    return float(coord - start) / float(peak - start);
  return float(end - coord) / float(end - peak);
}
bool ivs_scalars(Bytes store, unsigned outer, const int16_t *coords,
                 unsigned axes, float *out, unsigned &count) {
  count = 0;
  if (!store.has(0, 8) || store.u16(0) != 1)
    return false;
  uint32_t regions_at = store.u32(2);
  unsigned data_count = store.u16(6);
  if (outer >= data_count || !store.has(8, size_t(data_count) * 4) ||
      !store.has(regions_at, 4))
    return false;
  unsigned region_axes = store.u16(regions_at),
           region_count = store.u16(regions_at + 2);
  if (region_axes > MaxAxes ||
      !store.has(size_t(regions_at) + 4,
                 size_t(region_count) * region_axes * 6))
    return false;
  uint32_t data_at = store.u32(8 + outer * 4);
  if (!data_at || !store.has(data_at, 6))
    return false;
  unsigned indexes = store.u16(data_at + 4);
  if (indexes > CffMaxRegions || !store.has(size_t(data_at) + 6, indexes * 2))
    return false;
  for (unsigned i = 0; i < indexes; ++i) {
    unsigned r = store.u16(data_at + 6 + i * 2);
    if (r >= region_count)
      return false;
    float scalar = 1;
    for (unsigned a = 0; a < region_axes && scalar != 0; ++a) {
      size_t o = size_t(regions_at) + 4 + (size_t(r) * region_axes + a) * 6;
      int coord = a < axes && coords ? coords[a] : 0;
      scalar *= axis_scalar(store.s16(o), store.s16(o + 2), store.s16(o + 4),
                            coord);
    }
    out[i] = scalar;
  }
  count = indexes;
  return true;
}

// ---- fvar / avar ----------------------------------------------------------
static float fixed_value(uint32_t v) { return float(int32_t(v)) / 65536.f; }
unsigned variation_axes(const Face &f, VariationAxis *out, unsigned cap) {
  if (!f.axis_count || !f.fvar.p)
    return 0;
  unsigned at = f.fvar.u16(4), n = f.axis_count;
  for (unsigned a = 0; a < n && a < cap && out; ++a) {
    size_t o = at + size_t(a) * 20;
    out[a].tag = f.fvar.u32(o);
    out[a].min = fixed_value(f.fvar.u32(o + 4));
    out[a].def = fixed_value(f.fvar.u32(o + 8));
    out[a].max = fixed_value(f.fvar.u32(o + 12));
    out[a].flags = f.fvar.u16(o + 16);
    out[a].name_id = f.fvar.u16(o + 18);
  }
  return n;
}
unsigned named_instances(const Face &f, NamedInstance *out, unsigned cap) {
  if (!f.axis_count || !f.fvar.p)
    return 0;
  unsigned axes = f.axis_count, count = f.fvar.u16(12),
           size = f.fvar.u16(14);
  size_t at = f.fvar.u16(4) + size_t(axes) * 20;
  for (unsigned i = 0; i < count && i < cap && out; ++i) {
    size_t o = at + size_t(i) * size;
    out[i] = NamedInstance{};
    out[i].subfamily_name_id = f.fvar.u16(o);
    out[i].flags = f.fvar.u16(o + 2);
    for (unsigned a = 0; a < axes; ++a)
      out[i].coords[a] = fixed_value(f.fvar.u32(o + 4 + a * 4));
    if (size == axes * 4 + 6)
      out[i].postscript_name_id = f.fvar.u16(o + 4 + axes * 4);
  }
  return count;
}
int normalize_axis(const VariationAxis &a, double v) {
  if (v < a.min)
    v = a.min;
  if (v > a.max)
    v = a.max;
  double n = 0;
  if (v < a.def && a.def > a.min)
    n = (v - a.def) / (double(a.def) - a.min);
  else if (v > a.def && a.max > a.def)
    n = (v - a.def) / (double(a.max) - a.def);
  int q = floor_int(n * 16384 + .5); // floor(x + 0.5), as HarfBuzz
  return q < -16384 ? -16384 : q > 16384 ? 16384 : q;
}
static int avar_map(Bytes avar, unsigned axis, int v) {
  if (!avar.p)
    return v;
  size_t at = 8;
  for (unsigned a = 0; a < axis; ++a)
    at += 2 + size_t(avar.u16(at)) * 4;
  unsigned n = avar.u16(at);
  if (!n)
    return v;
  Bytes map = avar.sub(at + 2, size_t(n) * 4);
  if (v <= map.s16(0))
    return map.s16(2) + (v - map.s16(0));
  for (unsigned k = 1; k < n; ++k) {
    int from0 = map.s16((k - 1) * 4), to0 = map.s16((k - 1) * 4 + 2),
        from1 = map.s16(k * 4), to1 = map.s16(k * 4 + 2);
    if (v < from1) {
      if (from1 == from0)
        return to1;
      // Exact rational interpolation, rounded to the nearest F2Dot14.
      double q = double(to0) + double(to1 - to0) * double(v - from0) / double(from1 - from0);
      return floor_int(q + .5);
    }
    if (v == from1)
      return to1;
  }
  return map.s16((n - 1) * 4 + 2) + (v - map.s16((n - 1) * 4));
}
static float mvar_delta(const Face &f, uint32_t value_tag);
bool set_variations(Face &f, const VariationSetting *settings, unsigned count) {
  VariationAxis axes[MaxAxes];
  unsigned n = variation_axes(f, axes, MaxAxes);
  if (!n) {
    f.id = f.base_id;
    return count == 0 || !settings;
  }
  int16_t next[MaxAxes]{};
  for (unsigned a = 0; a < n; ++a) {
    double value = axes[a].def;
    for (unsigned i = 0; settings && i < count; ++i) {
      double v = settings[i].value;
      if (!(v == v) || v > 1e9 || v < -1e9)
        return false; // NaN / unbounded
      if (settings[i].tag == axes[a].tag)
        value = v;
    }
    int c = avar_map(f.avar, a, normalize_axis(axes[a], value));
    next[a] = int16_t(c < -16384 ? -16384 : c > 16384 ? 16384 : c);
  }
  uint32_t h = f.base_id ^ 0x9e3779b9u;
  bool any = false;
  for (unsigned a = 0; a < n; ++a) {
    f.coords[a] = next[a];
    any |= next[a] != 0;
    h = (h ^ uint16_t(next[a])) * 16777619u;
  }
  f.id = any ? (h ? h : 1) : f.base_id;
  // MVAR: line metrics of the instance (hhea ascender/descender/lineGap).
  f.ascent = f.hhea.s16(4);
  f.descent = f.hhea.s16(6);
  f.gap = f.hhea.s16(8);
  if (any && f.mvar.p) {
    f.ascent += floor_int(double(mvar_delta(f, 0x68617363)) + .5);  // 'hasc'
    f.descent += floor_int(double(mvar_delta(f, 0x68647363)) + .5); // 'hdsc'
    f.gap += floor_int(double(mvar_delta(f, 0x686c6770)) + .5);     // 'hlgp'
    if (f.ascent <= f.descent)
      f.ascent = f.descent + 1;
  }
  return true;
}

// ---- ItemVariationStore deltas ---------------------------------------------
// Sum of region scalar * delta for item (outer, inner) at the face instance.
float ivs_delta(Bytes store, unsigned outer, unsigned inner, const int16_t *coords,
                unsigned axes) {
  float scalars[CffMaxRegions];
  unsigned regions = 0;
  if (!ivs_scalars(store, outer, coords, axes, scalars, regions))
    return 0;
  uint32_t data_at = store.u32(8 + outer * 4);
  unsigned items = store.u16(data_at), word_field = store.u16(data_at + 2);
  bool long_words = (word_field & 0x8000) != 0;
  unsigned words = word_field & 0x7fff;
  if (inner >= items || words > regions)
    return 0;
  size_t big = long_words ? 4 : 2, small = long_words ? 2 : 1;
  size_t row = words * big + (regions - words) * small;
  size_t at = size_t(data_at) + 6 + size_t(regions) * 2 + size_t(inner) * row;
  if (!store.has(at, row))
    return 0;
  float sum = 0;
  for (unsigned r = 0; r < regions; ++r) {
    int32_t d;
    if (r < words) {
      d = long_words ? int32_t(store.u32(at)) : store.s16(at);
      at += big;
    } else {
      d = long_words ? store.s16(at) : int8_t(store.p[at]);
      at += small;
    }
    sum += scalars[r] * float(d);
  }
  return sum;
}
// DeltaSetIndexMap (HVAR/VVAR, COLRv1): glyph/item -> (outer, inner).
static bool delta_set_index(Bytes map, unsigned item, unsigned &outer, unsigned &inner) {
  if (!map.has(0, 4))
    return false;
  unsigned format = map.p[0], entry = map.p[1];
  uint32_t count;
  size_t at;
  if (format == 0) {
    count = map.u16(2);
    at = 4;
  } else if (format == 1) {
    if (!map.has(0, 6))
      return false;
    count = map.u32(2);
    at = 6;
  } else
    return false;
  if (!count)
    return false;
  unsigned size = ((entry >> 4) & 3) + 1, inner_bits = (entry & 15) + 1;
  if (item >= count)
    item = count - 1;
  if (!map.has(at + size_t(item) * size, size))
    return false;
  uint32_t v = 0;
  for (unsigned k = 0; k < size; ++k)
    v = (v << 8) | map.p[at + size_t(item) * size + k];
  outer = v >> inner_bits;
  inner = v & ((1u << inner_bits) - 1);
  return true;
}
// HVAR advance delta in font units (0 if the face has no HVAR).
static bool hvar_advance_delta(const Face &f, unsigned glyph, float &delta) {
  delta = 0;
  Bytes h = f.hvar;
  if (!h.has(0, 20) || h.u16(0) != 1)
    return false;
  uint32_t store_at = h.u32(4), map_at = h.u32(8);
  if (!store_at || store_at >= h.n)
    return false;
  Bytes store = h.sub(store_at, h.n - store_at);
  unsigned outer = 0, inner = glyph;
  if (map_at) {
    if (map_at >= h.n || !delta_set_index(h.sub(map_at, h.n - map_at), glyph, outer, inner))
      return false;
  }
  delta = ivs_delta(store, outer, inner, f.coords, f.axis_count);
  return true;
}
// MVAR value delta for a 4-byte value tag (e.g. 'hasc').
static float mvar_delta(const Face &f, uint32_t value_tag) {
  Bytes m = f.mvar;
  if (!m.has(0, 12) || m.u16(0) != 1)
    return 0;
  unsigned record = m.u16(6), count = m.u16(8);
  unsigned store_at = m.u16(10);
  if (record < 8 || !store_at || !m.has(12, size_t(count) * record))
    return 0;
  Bytes store = m.sub(store_at, m.n - store_at);
  for (unsigned i = 0; i < count; ++i) {
    size_t o = 12 + size_t(i) * record;
    if (m.u32(o) == value_tag)
      return ivs_delta(store, m.u16(o + 4), m.u16(o + 6), f.coords, f.axis_count);
  }
  return 0;
}
// ---- gvar --------------------------------------------------------------------
constexpr unsigned MaxGvarTuples = 64, MaxGvarPoints = 4096 + 4;
// Packed point numbers. count == 0 means "all points".
static bool gvar_points(Bytes d, size_t &at, unsigned total, uint16_t *out, unsigned &count, bool &all) {
  if (!d.has(at, 1))
    return false;
  unsigned n = d.p[at++];
  if (n & 0x80) {
    if (!d.has(at, 1))
      return false;
    n = ((n & 0x7f) << 8) | d.p[at++];
  }
  all = n == 0;
  count = n;
  if (all)
    return true;
  if (n > total)
    return false;
  unsigned got = 0, value = 0;
  while (got < n) {
    if (!d.has(at, 1))
      return false;
    unsigned control = d.p[at++], run = (control & 0x7f) + 1;
    bool words = control & 0x80;
    for (unsigned k = 0; k < run && got < n; ++k) {
      if (words) {
        if (!d.has(at, 2))
          return false;
        value += d.u16(at);
        at += 2;
      } else {
        if (!d.has(at, 1))
          return false;
        value += d.p[at++];
      }
      if (value >= total)
        return false;
      out[got++] = uint16_t(value);
    }
  }
  return true;
}
static bool gvar_deltas_packed(Bytes d, size_t &at, unsigned n, int16_t *out) {
  unsigned got = 0;
  while (got < n) {
    if (!d.has(at, 1))
      return false;
    unsigned control = d.p[at++], run = (control & 0x3f) + 1;
    for (unsigned k = 0; k < run && got < n; ++k) {
      if (control & 0x80)
        out[got++] = 0;
      else if (control & 0x40) {
        if (!d.has(at, 2))
          return false;
        out[got++] = d.s16(at);
        at += 2;
      } else {
        if (!d.has(at, 1))
          return false;
        out[got++] = int8_t(d.p[at++]);
      }
    }
    if (got > n)
      return false;
  }
  return true;
}
static float tuple_scalar(const int16_t *peak, const int16_t *start, const int16_t *end,
                          const int16_t *coords, unsigned axes) {
  float s = 1;
  for (unsigned a = 0; a < axes && s != 0; ++a) {
    int p = peak[a], c = coords[a];
    if (!p || c == p)
      continue;
    if (start) {
      int lo = start[a], hi = end[a];
      if (lo > p || p > hi || (lo < 0 && hi > 0))
        continue;
      if (c < lo || c > hi)
        return 0;
      s *= c < p ? float(c - lo) / float(p - lo) : float(hi - c) / float(hi - p);
    } else {
      if (!c || (c < 0) != (p < 0) || (c < 0 ? c < p : c > p))
        return 0;
      s *= float(c) / float(p);
    }
  }
  return s;
}
// Interpolate untouched points of one contour (OpenType IUP).
static void gvar_iup(const int *ox, const int *oy, float *dx, float *dy, const bool *touched,
                     unsigned first, unsigned last) {
  unsigned n = last - first + 1, start = n;
  for (unsigned i = 0; i < n; ++i)
    if (touched[first + i]) {
      start = i;
      break;
    }
  if (start == n)
    return; // no touched point: contour unchanged
  for (unsigned axis = 0; axis < 2; ++axis) {
    const int *o = axis ? oy : ox;
    float *d = axis ? dy : dx;
    unsigned i = start;
    for (unsigned visited = 0; visited < n;) {
      unsigned a = i, b = (i + 1) % n;
      unsigned steps = 1;
      while (!touched[first + b]) {
        b = (b + 1) % n;
        ++steps;
      }
      // points strictly between a and b get interpolated
      unsigned pa = first + a, pb = first + b;
      int x1 = o[pa], x2 = o[pb];
      float d1 = d[pa], d2 = d[pb];
      for (unsigned k = (a + 1) % n; k != b; k = (k + 1) % n) {
        unsigned p = first + k;
        int x = o[p];
        float v;
        if (x1 == x2)
          v = d1 == d2 ? d1 : 0;
        else if (x <= (x1 < x2 ? x1 : x2))
          v = x1 < x2 ? d1 : d2;
        else if (x >= (x1 > x2 ? x1 : x2))
          v = x1 > x2 ? d1 : d2;
        else
          v = d1 + (d2 - d1) * float(x - x1) / float(x2 - x1);
        d[p] = v;
      }
      visited += steps;
      i = b;
    }
  }
}
// Deltas for `total` points (outline points + 4 phantom points) of glyph g.
// `ends`/`contours` describe simple glyph contours (IUP); composites pass 0.
bool gvar_glyph_deltas(const Face &f, unsigned g, unsigned total, const uint16_t *ends,
                       unsigned contours, const int *ox, const int *oy, float *dx, float *dy) {
  for (unsigned i = 0; i < total; ++i)
    dx[i] = dy[i] = 0;
  Bytes v = f.gvar;
  if (!f.variable() || !v.has(0, 20) || v.u16(0) != 1 || total > MaxGvarPoints)
    return true;
  unsigned axes = v.u16(4), shared_count = v.u16(6), glyphs = v.u16(12), flags = v.u16(14);
  uint32_t shared_at = v.u32(8), data_at = v.u32(16);
  if (axes != f.axis_count || g >= glyphs)
    return axes == f.axis_count;
  bool long_offsets = flags & 1;
  size_t table = 20;
  auto offset = [&](unsigned i, uint32_t &o) {
    if (long_offsets) {
      if (!v.has(table + size_t(i) * 4, 4)) return false;
      o = v.u32(table + size_t(i) * 4);
    } else {
      if (!v.has(table + size_t(i) * 2, 2)) return false;
      o = uint32_t(v.u16(table + size_t(i) * 2)) * 2;
    }
    return true;
  };
  uint32_t a, b;
  if (!offset(g, a) || !offset(g + 1, b) || b < a)
    return false;
  if (a == b)
    return true;
  Bytes d = v.sub(size_t(data_at) + a, b - a);
  if (!d.p || !d.has(0, 4))
    return false;
  if (!v.has(shared_at, size_t(shared_count) * axes * 2))
    return false;
  unsigned tuple_field = d.u16(0), tuples = tuple_field & 0x0fff;
  if (tuples > MaxGvarTuples)
    return false;
  size_t serialized = d.u16(2), header = 4;
  auto *points = BSF_NEW uint16_t[MaxGvarPoints], *shared_points = BSF_NEW uint16_t[MaxGvarPoints];
  auto *delta_x = BSF_NEW int16_t[MaxGvarPoints], *delta_y = BSF_NEW int16_t[MaxGvarPoints];
  auto *touched = BSF_NEW bool[MaxGvarPoints];
  auto *tx = BSF_NEW float[MaxGvarPoints], *ty = BSF_NEW float[MaxGvarPoints];
  bool ok = points && shared_points && delta_x && delta_y && touched && tx && ty;
  unsigned shared_n = 0;
  bool shared_all = true;
  if (ok && (tuple_field & 0x8000))
    ok = gvar_points(d, serialized, total, shared_points, shared_n, shared_all);
  for (unsigned t = 0; ok && t < tuples; ++t) {
    if (!d.has(header, 4)) {
      ok = false;
      break;
    }
    unsigned size = d.u16(header), index = d.u16(header + 2);
    header += 4;
    int16_t peak[MaxAxes], start[MaxAxes], end[MaxAxes];
    if (index & 0x8000) {
      if (!d.has(header, size_t(axes) * 2)) { ok = false; break; }
      for (unsigned k = 0; k < axes; ++k) peak[k] = d.s16(header + k * 2);
      header += size_t(axes) * 2;
    } else {
      unsigned s = index & 0x0fff;
      if (s >= shared_count) { ok = false; break; }
      for (unsigned k = 0; k < axes; ++k) peak[k] = v.s16(shared_at + (size_t(s) * axes + k) * 2);
    }
    bool intermediate = index & 0x4000;
    if (intermediate) {
      if (!d.has(header, size_t(axes) * 4)) { ok = false; break; }
      for (unsigned k = 0; k < axes; ++k) {
        start[k] = d.s16(header + k * 2);
        end[k] = d.s16(header + (axes + k) * 2);
      }
      header += size_t(axes) * 4;
    }
    size_t data = serialized;
    serialized += size;
    if (!d.has(data, size)) { ok = false; break; }
    float scalar = tuple_scalar(peak, intermediate ? start : nullptr, intermediate ? end : nullptr,
                                f.coords, axes);
    if (scalar == 0)
      continue;
    Bytes tuple = d.sub(data, size);
    size_t at = 0;
    unsigned n = shared_n;
    bool all = shared_all;
    const uint16_t *which = shared_points;
    if (index & 0x2000) {
      if (!gvar_points(tuple, at, total, points, n, all)) { ok = false; break; }
      which = points;
    }
    unsigned count = all ? total : n;
    if (!gvar_deltas_packed(tuple, at, count, delta_x) || !gvar_deltas_packed(tuple, at, count, delta_y)) {
      ok = false;
      break;
    }
    if (all) {
      for (unsigned p = 0; p < total; ++p) {
        dx[p] += scalar * delta_x[p];
        dy[p] += scalar * delta_y[p];
      }
      continue;
    }
    for (unsigned p = 0; p < total; ++p) {
      touched[p] = false;
      tx[p] = ty[p] = 0;
    }
    for (unsigned k = 0; k < n; ++k) {
      touched[which[k]] = true;
      tx[which[k]] = delta_x[k];
      ty[which[k]] = delta_y[k];
    }
    // Inferred deltas for untouched outline points (simple glyphs only).
    if (contours && ox && oy) {
      unsigned first = 0;
      for (unsigned c = 0; c < contours; ++c) {
        gvar_iup(ox, oy, tx, ty, touched, first, ends[c]);
        first = ends[c] + 1u;
      }
    }
    for (unsigned p = 0; p < total; ++p) {
      dx[p] += scalar * tx[p];
      dy[p] += scalar * ty[p];
    }
  }
  delete[] points;
  delete[] shared_points;
  delete[] delta_x;
  delete[] delta_y;
  delete[] touched;
  delete[] tx;
  delete[] ty;
  return ok;
}
// Advance of a variation instance: HVAR when present, else the gvar deltas
// of the advance phantom point (point count + 1) minus the origin point.
static unsigned variable_advance(const Face &f, unsigned g, unsigned base) {
  float delta = 0;
  if (!hvar_advance_delta(f, g, delta)) {
    if (f.outline != OutlineFormat::TrueType || !f.gvar.p)
      return base;
    Bytes glyph;
    unsigned points = 0;
    if (!f.location(g, glyph))
      return base;
    if (glyph.n >= 10) {
      int contours = glyph.s16(0);
      if (contours > 0) {
        if (!glyph.has(10, size_t(contours) * 2))
          return base;
        points = unsigned(glyph.u16(10 + (contours - 1) * 2)) + 1;
      } else if (contours == -1) {
        size_t scan = 10;
        unsigned fl = 0;
        do {
          if (!glyph.has(scan, 4) || ++points > 256)
            return base;
          fl = glyph.u16(scan);
          scan += 4 + ((fl & 1) ? 4 : 2) + ((fl & 8) ? 2 : (fl & 64) ? 4 : (fl & 128) ? 8 : 0);
        } while (fl & 32);
      }
    }
    if (points + 4 > MaxGvarPoints)
      return base;
    float *dx = BSF_NEW float[points + 4], *dy = BSF_NEW float[points + 4];
    bool ok = dx && dy && gvar_glyph_deltas(f, g, points + 4, nullptr, 0, nullptr, nullptr, dx, dy);
    if (ok)
      delta = dx[points + 1] - dx[points];
    delete[] dx;
    delete[] dy;
    if (!ok)
      return base;
  }
  int v = int(base) + floor_int(double(delta) + .5);
  return v < 0 ? 0 : v > 65535 ? 65535 : unsigned(v);
}
