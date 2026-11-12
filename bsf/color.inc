// BSF 2.2 — colour glyphs: COLRv0/CPAL, COLRv1 paint graphs, CBDT/CBLC and
// sbix PNG strikes. Included by bsf.cpp inside namespace BSF (Ring 3 only).
//
// Output is a straight-alpha ARGB bitmap of at most MaxBitmap x MaxBitmap,
// positioned like Glyph (x/y relative to the pen and the baseline). All
// tables are read through Bytes; paint traversal is bounded by depth, visit,
// layer and glyph-reference budgets; PaintColrGlyph cycles are detected;
// temporary canvases are capped (MaxColorCanvases).
namespace {
constexpr unsigned MaxColorCanvases = 8;
constexpr double Pi = 3.14159265358979323846;
double c_floor(double v) { double i = double(int64_t(v)); return i > v ? i - 1 : i; }
double c_abs(double v) { return v < 0 ? -v : v; }
// sin/cos/atan2 without libm (|error| < 1e-9 for the ranges used).
double c_sin(double x) {
  x = x - 2 * Pi * c_floor(x / (2 * Pi));
  if (x > Pi) x -= 2 * Pi;
  double t = x, s = x, x2 = x * x;
  for (int n = 1; n < 14; ++n) { t *= -x2 / ((2 * n) * (2 * n + 1)); s += t; }
  return s;
}
double c_cos(double x) { return c_sin(x + Pi / 2); }
double c_atan(double x) {
  // Reduce to |x| <= tan(pi/12) then use the series.
  bool neg = x < 0; if (neg) x = -x;
  bool inv = x > 1; if (inv) x = 1 / x;
  bool shift = x > 0.2679491924311228; // tan(15°)
  if (shift) x = (x - 0.5773502691896258) / (1 + x * 0.5773502691896258);
  double x2 = x * x, t = x, s = x;
  for (int n = 1; n < 30; ++n) { t *= -x2; s += t / (2 * n + 1); }
  if (shift) s += Pi / 6;
  if (inv) s = Pi / 2 - s;
  return neg ? -s : s;
}
double c_atan2(double y, double x) {
  if (x > 0) return c_atan(y / x);
  if (x < 0) return y >= 0 ? c_atan(y / x) + Pi : c_atan(y / x) - Pi;
  return y > 0 ? Pi / 2 : y < 0 ? -Pi / 2 : 0;
}
struct Affine {
  double a{1}, b{}, c{}, d{1}, e{}, f{}; // x' = a x + c y + e, y' = b x + d y + f
  Affine operator*(const Affine &t) const {
    return {a * t.a + c * t.b, b * t.a + d * t.b, a * t.c + c * t.d, b * t.c + d * t.d,
            a * t.e + c * t.f + e, b * t.e + d * t.f + f};
  }
  bool invert(Affine &o) const {
    double det = a * d - b * c;
    if (c_abs(det) < 1e-12) return false;
    o = {d / det, -b / det, -c / det, a / det, (c * f - d * e) / det, (b * e - a * f) / det};
    return true;
  }
  void apply(double x, double y, double &X, double &Y) const { X = a * x + c * y + e; Y = b * x + d * y + f; }
};
struct Rgba { float r{}, g{}, b{}, a{}; }; // premultiplied
struct Canvas {
  int w{}, h{};
  Rgba *px{};
};
struct ColorContext {
  const Face &face;
  Bytes colr, base_list, layer_list, var_map, var_store, palette;
  uint32_t foreground{};
  int w{}, h{};
  unsigned paints{}, layers{}, glyph_refs{}, canvases{}, depth{}, max_depth{};
  bool failed{};
  uint16_t glyph_path[64]{};
  unsigned path_n{};
  ColorStats *stats{};
};
Rgba palette_color(const ColorContext &cx, unsigned index, float alpha) {
  uint32_t c = 0;
  if (index == 0xffff)
    c = cx.foreground;
  else if (cx.palette.has(size_t(index) * 4, 4)) {
    const uint8_t *p = cx.palette.p + index * 4; // BGRA
    c = (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
  } else
    return Rgba{};
  float a = float(c >> 24) / 255.f * (alpha < 0 ? 0 : alpha > 1 ? 1 : alpha);
  return {float((c >> 16) & 255) / 255.f * a, float((c >> 8) & 255) / 255.f * a, float(c & 255) / 255.f * a, a};
}
// Variation delta for COLR varIndexBase + k (0xFFFFFFFF: none).
float colr_delta(const ColorContext &cx, uint32_t base, unsigned k) {
  if (base == 0xffffffffu || !cx.face.variable() || !cx.var_store.p)
    return 0;
  uint32_t index = base + k;
  unsigned outer, inner;
  if (cx.var_map.p) {
    if (!delta_set_index(cx.var_map, index, outer, inner))
      return 0;
  } else {
    outer = index >> 16;
    inner = index & 0xffff;
  }
  return ivs_delta(cx.var_store, outer, inner, cx.face.coords, cx.face.axis_count);
}
uint32_t off24(Bytes t, size_t o) {
  return t.has(o, 3) ? (uint32_t(t.p[o]) << 16) | (uint32_t(t.p[o + 1]) << 8) | t.p[o + 2] : 0;
}
Bytes child(Bytes paint, size_t o) {
  uint32_t off = off24(paint, o);
  if (!off || off >= paint.n)
    return Bytes{};
  return paint.sub(off, paint.n - off); // paint tables extend to the end of COLR
}
float f2dot14(int v) { return float(v) / 16384.f; }
// ---- colour lines -----------------------------------------------------------
struct Stop { float offset; Rgba color; };
constexpr unsigned MaxStops = 64;
bool color_line(ColorContext &cx, Bytes line, bool var, Stop *stops, unsigned &n, unsigned &extend) {
  if (!line.has(0, 3))
    return false;
  extend = line.p[0];
  unsigned count = line.u16(1), rec = var ? 10 : 6;
  if (!count || count > MaxStops || !line.has(3, size_t(count) * rec))
    return false;
  n = count;
  for (unsigned i = 0; i < count; ++i) {
    size_t o = 3 + size_t(i) * rec;
    uint32_t vb = var ? line.u32(o + 6) : 0xffffffffu;
    float offset = f2dot14(line.s16(o)) + colr_delta(cx, vb, 0) / 16384.f;
    float alpha = f2dot14(line.s16(o + 4)) + colr_delta(cx, vb, 1) / 16384.f;
    stops[i] = {offset, palette_color(cx, line.u16(o + 2), alpha)};
  }
  // Stable sort by offset.
  for (unsigned i = 1; i < n; ++i) {
    Stop v = stops[i];
    unsigned j = i;
    while (j && stops[j - 1].offset > v.offset) { stops[j] = stops[j - 1]; --j; }
    stops[j] = v;
  }
  return true;
}
Rgba sample_line(const Stop *s, unsigned n, unsigned extend, float t) {
  float lo = s[0].offset, hi = s[n - 1].offset;
  if (n == 1 || hi <= lo)
    return t < lo ? s[0].color : s[n - 1].color;
  float span = hi - lo, u = (t - lo) / span;
  if (extend == 1) // repeat
    u = u - float(c_floor(u));
  else if (extend == 2) { // reflect
    float k = u - 2 * float(c_floor(u / 2));
    u = k > 1 ? 2 - k : k;
  }
  t = lo + u * span;
  if (t <= lo) return s[0].color;
  if (t >= hi) return s[n - 1].color;
  for (unsigned i = 1; i < n; ++i)
    if (t <= s[i].offset) {
      float d = s[i].offset - s[i - 1].offset, f = d > 0 ? (t - s[i - 1].offset) / d : 1;
      const Rgba &a = s[i - 1].color, &b = s[i].color;
      return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.a + (b.a - a.a) * f};
    }
  return s[n - 1].color;
}
// ---- compositing ------------------------------------------------------------
float blend_channel(unsigned mode, float cb, float cs) {
  switch (mode) {
  case 13: return cb + cs - cb * cs;                                  // screen
  case 14: return cb <= .5f ? 2 * cs * cb : 1 - 2 * (1 - cs) * (1 - cb); // overlay
  case 15: return cb < cs ? cb : cs;                                  // darken
  case 16: return cb > cs ? cb : cs;                                  // lighten
  case 17: return cb == 0 ? 0 : cs >= 1 ? 1 : (cb / (1 - cs) > 1 ? 1 : cb / (1 - cs)); // dodge
  case 18: return cb >= 1 ? 1 : cs <= 0 ? 0 : 1 - ((1 - cb) / cs > 1 ? 1 : (1 - cb) / cs); // burn
  case 19: return cs <= .5f ? 2 * cs * cb : 1 - 2 * (1 - cs) * (1 - cb); // hard light
  case 20: {                                                         // soft light
    if (cs <= .5f) return cb - (1 - 2 * cs) * cb * (1 - cb);
    float d = cb <= .25f ? ((16 * cb - 12) * cb + 4) * cb : float(square_root(cb));
    return cb + (2 * cs - 1) * (d - cb);
  }
  case 21: return cb > cs ? cb - cs : cs - cb;                        // difference
  case 22: return cb + cs - 2 * cb * cs;                              // exclusion
  case 23: return cb * cs;                                           // multiply
  }
  return cs;
}
float lum(float r, float g, float b) { return .3f * r + .59f * g + .11f * b; }
void clip_color(float &r, float &g, float &b) {
  float l = lum(r, g, b), n = r < g ? (r < b ? r : b) : (g < b ? g : b), x = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (n < 0) { r = l + (r - l) * l / (l - n); g = l + (g - l) * l / (l - n); b = l + (b - l) * l / (l - n); }
  if (x > 1) { r = l + (r - l) * (1 - l) / (x - l); g = l + (g - l) * (1 - l) / (x - l); b = l + (b - l) * (1 - l) / (x - l); }
}
void set_lum(float &r, float &g, float &b, float l) { float d = l - lum(r, g, b); r += d; g += d; b += d; clip_color(r, g, b); }
float sat(float r, float g, float b) { float x = r > g ? (r > b ? r : b) : (g > b ? g : b), n = r < g ? (r < b ? r : b) : (g < b ? g : b); return x - n; }
void set_sat(float &r, float &g, float &b, float s) {
  float *c[3] = {&r, &g, &b};
  // sort pointers: max, mid, min
  for (int i = 0; i < 2; ++i) for (int j = 0; j < 2 - i; ++j) if (*c[j] < *c[j + 1]) { float *t = c[j]; c[j] = c[j + 1]; c[j + 1] = t; }
  float &mx = *c[0], &md = *c[1], &mn = *c[2];
  if (mx > mn) { md = (md - mn) * s / (mx - mn); mx = s; } else md = mx = 0;
  mn = 0;
}
Rgba composite(unsigned mode, Rgba s, Rgba b) {
  float as = s.a, ab = b.a;
  auto pd = [&](float fs, float fb) { return Rgba{s.r * fs + b.r * fb, s.g * fs + b.g * fb, s.b * fs + b.b * fb, as * fs + ab * fb}; };
  switch (mode) {
  case 0: return Rgba{};
  case 1: return s;
  case 2: return b;
  case 3: return pd(1, 1 - as);
  case 4: return pd(1 - ab, 1);
  case 5: return pd(ab, 0);
  case 6: return pd(0, as);
  case 7: return pd(1 - ab, 0);
  case 8: return pd(0, 1 - as);
  case 9: return pd(ab, 1 - as);
  case 10: return pd(1 - ab, as);
  case 11: return pd(1 - ab, 1 - as);
  case 12: {
    Rgba r{s.r + b.r, s.g + b.g, s.b + b.b, as + ab};
    r.r = r.r > 1 ? 1 : r.r;
    r.g = r.g > 1 ? 1 : r.g;
    r.b = r.b > 1 ? 1 : r.b;
    r.a = r.a > 1 ? 1 : r.a;
    return r;
  }
  default: break;
  }
  if (mode > 27)
    return pd(1, 1 - as);
  // Blend modes: unpremultiply, blend, recombine (W3C Compositing 1).
  float sr = as > 0 ? s.r / as : 0, sg = as > 0 ? s.g / as : 0, sb = as > 0 ? s.b / as : 0;
  float br = ab > 0 ? b.r / ab : 0, bg = ab > 0 ? b.g / ab : 0, bb = ab > 0 ? b.b / ab : 0;
  float rr, rg, rb;
  if (mode <= 23) {
    rr = blend_channel(mode, br, sr); rg = blend_channel(mode, bg, sg); rb = blend_channel(mode, bb, sb);
  } else {
    rr = sr; rg = sg; rb = sb;
    if (mode == 24) { rr = sr; rg = sg; rb = sb; set_sat(rr, rg, rb, sat(br, bg, bb)); set_lum(rr, rg, rb, lum(br, bg, bb)); }
    else if (mode == 25) { rr = br; rg = bg; rb = bb; set_sat(rr, rg, rb, sat(sr, sg, sb)); set_lum(rr, rg, rb, lum(br, bg, bb)); }
    else if (mode == 26) { set_lum(rr, rg, rb, lum(br, bg, bb)); }
    else { rr = br; rg = bg; rb = bb; set_lum(rr, rg, rb, lum(sr, sg, sb)); }
  }
  float ar = as + ab - as * ab;
  return {(1 - ab) * s.r + (1 - as) * b.r + as * ab * rr, (1 - ab) * s.g + (1 - as) * b.g + as * ab * rg,
          (1 - ab) * s.b + (1 - as) * b.b + as * ab * rb, ar};
}
// ---- glyph clip masks -----------------------------------------------------
// Coverage of glyph `id` under M (font units -> canvas pixels, y down).
bool glyph_mask(ColorContext &cx, unsigned id, const Affine &M, uint8_t *mask) {
  if (++cx.glyph_refs > 2048 || id >= cx.face.glyphs)
    return false;
  auto *shape = BSF_NEW Outline;
  if (!shape)
    return false;
  double precision = 1;
  bool ok = build_outline(cx.face, id, *shape, precision);
  for (size_t i = 0, n = size_t(cx.w) * cx.h; i < n; ++i) mask[i] = 0;
  if (ok && shape->count) {
    constexpr double sub = 32; // pixel coordinates in 1/32 px
    for (unsigned i = 0; ok && i < shape->count; ++i) {
      auto &v = shape->vertices[i];
      double X, Y;
      auto map = [&](short &x, short &y) {
        M.apply(x / precision, y / precision, X, Y);
        X *= sub; Y *= sub;
        if (X < -32000 || X > 32000 || Y < -32000 || Y > 32000) return false;
        x = short(X < 0 ? X - .5 : X + .5); y = short(Y < 0 ? Y - .5 : Y + .5);
        return true;
      };
      ok = map(v.x, v.y) && (v.type < STBTT_vcurve || map(v.cx, v.cy)) && (v.type != STBTT_vcubic || map(v.cx1, v.cy1));
    }
    auto *arena = ok ? BSF_NEW Scratch : nullptr;
    if (ok && arena) {
      stbtt__bitmap bitmap{cx.w, cx.h, cx.w, mask};
      stbtt_Rasterize(&bitmap, .35f, shape->vertices, int(shape->count), float(1 / sub), float(1 / sub), 0, 0, 0, 0, 0, arena);
      ok = !arena->failed;
    } else
      ok = false;
    delete arena;
  }
  delete shape;
  return ok;
}
bool alloc_canvas(ColorContext &cx, Canvas &c) {
  if (cx.canvases >= MaxColorCanvases)
    return false;
  c.w = cx.w;
  c.h = cx.h;
  c.px = BSF_NEW Rgba[size_t(cx.w) * cx.h];
  if (!c.px)
    return false;
  ++cx.canvases;
  for (int i = 0; i < c.w * c.h; ++i) c.px[i] = Rgba{};
  return true;
}
void free_canvas(ColorContext &cx, Canvas &c) {
  if (c.px) { delete[] c.px; --cx.canvases; }
  c.px = nullptr;
}
bool paint(ColorContext &cx, Bytes p, const Affine &M, Canvas &dst);
bool colr_glyph(ColorContext &cx, unsigned glyph, const Affine &M, Canvas &dst);
// Fill the whole canvas with a solid/gradient paint (composited src-over).
bool fill(ColorContext &cx, Bytes p, unsigned format, const Affine &M, Canvas &dst) {
  Affine inv;
  if (!M.invert(inv)) return true; // degenerate transform paints nothing
  bool var = format == 3 || format == 5 || format == 7 || format == 9;
  if (format == 2 || format == 3) {
    if (!p.has(0, var ? 9 : 5)) return false;
    uint32_t vb = var ? p.u32(5) : 0xffffffffu;
    Rgba c = palette_color(cx, p.u16(1), f2dot14(p.s16(3)) + colr_delta(cx, vb, 0) / 16384.f);
    for (int i = 0; i < dst.w * dst.h; ++i) dst.px[i] = composite(3, c, dst.px[i]);
    return true;
  }
  Stop stops[MaxStops];
  unsigned n = 0, extend = 0;
  if (!color_line(cx, child(p, 1), var, stops, n, extend)) return false;
  if (format == 4 || format == 5) {
    if (!p.has(0, var ? 20 : 16)) return false;
    uint32_t vb = var ? p.u32(16) : 0xffffffffu;
    double x0 = p.s16(4) + colr_delta(cx, vb, 0), y0 = p.s16(6) + colr_delta(cx, vb, 1),
           x1 = p.s16(8) + colr_delta(cx, vb, 2), y1 = p.s16(10) + colr_delta(cx, vb, 3),
           x2 = p.s16(12) + colr_delta(cx, vb, 4), y2 = p.s16(14) + colr_delta(cx, vb, 5);
    // p3: p1 projected onto the line through p0 perpendicular to p0p2.
    double nx = -(y2 - y0), ny = x2 - x0, dx = x1 - x0, dy = y1 - y0;
    double nn = nx * nx + ny * ny;
    double px3 = x0, py3 = y0;
    if (nn > 0) { double k = (dx * nx + dy * ny) / nn; px3 = x0 + k * nx; py3 = y0 + k * ny; }
    double vx = px3 - x0, vy = py3 - y0, vv = vx * vx + vy * vy;
    for (int y = 0; y < dst.h; ++y)
      for (int x = 0; x < dst.w; ++x) {
        double fx, fy;
        inv.apply(x + .5, y + .5, fx, fy);
        float t = vv > 0 ? float(((fx - x0) * vx + (fy - y0) * vy) / vv) : 0;
        auto &d = dst.px[y * dst.w + x];
        d = composite(3, sample_line(stops, n, extend, t), d);
      }
    return true;
  }
  if (format == 6 || format == 7) {
    if (!p.has(0, var ? 20 : 16)) return false;
    uint32_t vb = var ? p.u32(16) : 0xffffffffu;
    double x0 = p.s16(4) + colr_delta(cx, vb, 0), y0 = p.s16(6) + colr_delta(cx, vb, 1),
           r0 = p.u16(8) + colr_delta(cx, vb, 2), x1 = p.s16(10) + colr_delta(cx, vb, 3),
           y1 = p.s16(12) + colr_delta(cx, vb, 4), r1 = p.u16(14) + colr_delta(cx, vb, 5);
    double cdx = x1 - x0, cdy = y1 - y0, dr = r1 - r0, a = cdx * cdx + cdy * cdy - dr * dr;
    for (int y = 0; y < dst.h; ++y)
      for (int x = 0; x < dst.w; ++x) {
        double fx, fy;
        inv.apply(x + .5, y + .5, fx, fy);
        double pdx = fx - x0, pdy = fy - y0, b = pdx * cdx + pdy * cdy + r0 * dr, c = pdx * pdx + pdy * pdy - r0 * r0;
        double t;
        bool have = false;
        if (c_abs(a) < 1e-9) {
          if (c_abs(b) > 1e-12) { t = c / (2 * b); have = r0 + t * dr >= 0; }
        } else {
          double disc = b * b - a * c;
          if (disc >= 0) {
            double s = square_root(disc), t1 = (b + s) / a, t2 = (b - s) / a;
            double hi = t1 > t2 ? t1 : t2, lo = t1 > t2 ? t2 : t1;
            if (r0 + hi * dr >= 0) { t = hi; have = true; }
            else if (r0 + lo * dr >= 0) { t = lo; have = true; }
          }
        }
        if (!have) continue;
        auto &d = dst.px[y * dst.w + x];
        d = composite(3, sample_line(stops, n, extend, float(t)), d);
      }
    return true;
  }
  // Sweep gradient (8/9).
  if (!p.has(0, var ? 16 : 12)) return false;
  uint32_t vb = var ? p.u32(12) : 0xffffffffu;
  double cxp = p.s16(4) + colr_delta(cx, vb, 0), cyp = p.s16(6) + colr_delta(cx, vb, 1);
  double start = (f2dot14(p.s16(8)) + colr_delta(cx, vb, 2) / 16384.) * 180,
         end = (f2dot14(p.s16(10)) + colr_delta(cx, vb, 3) / 16384.) * 180;
  for (int y = 0; y < dst.h; ++y)
    for (int x = 0; x < dst.w; ++x) {
      double fx, fy;
      inv.apply(x + .5, y + .5, fx, fy);
      double angle = c_atan2(fy - cyp, fx - cxp) * 180 / Pi;
      if (angle < 0) angle += 360;
      float t = end != start ? float((angle - start) / (end - start)) : 0;
      auto &d = dst.px[y * dst.w + x];
      d = composite(3, sample_line(stops, n, extend, t), d);
    }
  return true;
}
bool paint(ColorContext &cx, Bytes p, const Affine &M, Canvas &dst) {
  if (cx.failed || !p.has(0, 1) || ++cx.paints > 8192 || cx.depth >= 32)
    return cx.failed = true, false;
  ++cx.depth;
  if (cx.depth > cx.max_depth) cx.max_depth = cx.depth;
  struct Leave { unsigned &d; ~Leave() { --d; } } leave{cx.depth};
  unsigned format = p.p[0];
  switch (format) {
  case 1: { // PaintColrLayers
    if (!p.has(0, 6)) return false;
    unsigned n = p.p[1];
    uint32_t first = p.u32(2);
    if (!cx.layer_list.has(0, 4)) return false;
    uint32_t total = cx.layer_list.u32(0);
    if (first > total || n > total - first || (cx.layers += n) > 4096) return false;
    for (unsigned i = 0; i < n; ++i) {
      uint32_t off = cx.layer_list.u32(4 + size_t(first + i) * 4);
      if (!off || off >= cx.layer_list.n || !cx.layer_list.has(4 + size_t(first + i) * 4, 4)) return false;
      if (!paint(cx, cx.layer_list.sub(off, cx.layer_list.n - off), M, dst)) return false;
    }
    return true;
  }
  case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
    return fill(cx, p, format, M, dst);
  case 10: { // PaintGlyph: clip child to the glyph outline
    if (!p.has(0, 6)) return false;
    auto *mask = BSF_NEW uint8_t[size_t(cx.w) * cx.h];
    Canvas layer;
    bool ok = mask && glyph_mask(cx, p.u16(4), M, mask) && alloc_canvas(cx, layer) &&
              paint(cx, child(p, 1), M, layer);
    if (ok)
      for (int i = 0; i < cx.w * cx.h; ++i) {
        float k = mask[i] / 255.f;
        Rgba s = layer.px[i];
        s.r *= k; s.g *= k; s.b *= k; s.a *= k;
        dst.px[i] = composite(3, s, dst.px[i]);
      }
    free_canvas(cx, layer);
    delete[] mask;
    return ok;
  }
  case 11: // PaintColrGlyph
    return p.has(0, 3) && colr_glyph(cx, p.u16(1), M, dst);
  case 32: { // PaintComposite
    if (!p.has(0, 8)) return false;
    unsigned mode = p.p[4];
    Canvas src, backdrop;
    bool ok = alloc_canvas(cx, src) && alloc_canvas(cx, backdrop) && paint(cx, child(p, 1), M, src) &&
              paint(cx, child(p, 5), M, backdrop);
    if (ok)
      for (int i = 0; i < cx.w * cx.h; ++i)
        dst.px[i] = composite(3, composite(mode, src.px[i], backdrop.px[i]), dst.px[i]);
    free_canvas(cx, src);
    free_canvas(cx, backdrop);
    return ok;
  }
  default: break;
  }
  if (format < 12 || format > 31)
    return false; // reserved formats
  // Transforms: child at offset 1, parameters after it.
  Affine T;
  auto need = [&](size_t n) { return p.has(0, n); };
  if (format == 12 || format == 13) {
    uint32_t to = off24(p, 4);
    Bytes t = to ? p.sub(to, p.n - to) : Bytes{};
    if (!to || !t.has(0, 24)) return false;
    uint32_t vb = format == 13 && t.has(24, 4) ? t.u32(24) : 0xffffffffu;
    auto fx = [&](unsigned k) { return double(int32_t(t.u32(k * 4))) / 65536. + colr_delta(cx, vb, k) / 65536.; };
    T = {fx(0), fx(1), fx(2), fx(3), fx(4), fx(5)};
  } else {
    // format pairs: 14/15 translate, 16..23 scale, 24..27 rotate, 28..31 skew
    unsigned base = format & ~1u;
    bool around = base == 18 || base == 22 || base == 26 || base == 30;
    size_t at = 4;
    unsigned params = base == 14 ? 2 : base == 16 || base == 18 ? 2 : base == 20 || base == 22 ? 1 : base == 24 || base == 26 ? 1 : 2;
    size_t size = at + params * 2 + (around ? 4 : 0) + (format & 1 ? 4 : 0);
    if (!need(size)) return false;
    uint32_t vb = (format & 1) ? p.u32(size - 4) : 0xffffffffu;
    auto v = [&](unsigned k) { return double(p.s16(at + k * 2)) + colr_delta(cx, vb, k); };
    double cxp = 0, cyp = 0;
    if (around) { cxp = v(params); cyp = v(params + 1); }
    if (base == 14)
      T = {1, 0, 0, 1, v(0), v(1)};
    else if (base == 16 || base == 18)
      T = {v(0) / 16384., 0, 0, v(1) / 16384., 0, 0};
    else if (base == 20 || base == 22)
      T = {v(0) / 16384., 0, 0, v(0) / 16384., 0, 0};
    else if (base == 24 || base == 26) {
      double angle = v(0) / 16384. * Pi;
      double s = c_sin(angle), c = c_cos(angle);
      T = {c, s, -s, c, 0, 0};
    } else {
      double ax = v(0) / 16384. * Pi, ay = v(1) / 16384. * Pi;
      double tx = c_sin(ax) / c_cos(ax), ty = c_sin(ay) / c_cos(ay);
      T = {1, ty, -tx, 1, 0, 0};
    }
    if (around) {
      Affine to{1, 0, 0, 1, cxp, cyp}, from{1, 0, 0, 1, -cxp, -cyp};
      T = to * T * from;
    }
  }
  return paint(cx, child(p, 1), M * T, dst);
}
bool colr_glyph(ColorContext &cx, unsigned glyph, const Affine &M, Canvas &dst) {
  for (unsigned i = 0; i < cx.path_n; ++i)
    if (cx.glyph_path[i] == glyph)
      return cx.failed = true, false; // PaintColrGlyph cycle
  if (cx.path_n >= 64 || !cx.base_list.has(0, 4))
    return false;
  uint32_t count = cx.base_list.u32(0);
  if (!cx.base_list.has(4, size_t(count) * 6))
    return false;
  uint32_t lo = 0, hi = count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    unsigned g = cx.base_list.u16(4 + size_t(mid) * 6);
    if (g < glyph) lo = mid + 1;
    else if (g > glyph) hi = mid;
    else {
      uint32_t off = cx.base_list.u32(4 + size_t(mid) * 6 + 2);
      if (!off || off >= cx.base_list.n) return false;
      cx.glyph_path[cx.path_n++] = uint16_t(glyph);
      bool ok = paint(cx, cx.base_list.sub(off, cx.base_list.n - off), M, dst);
      --cx.path_n;
      return ok;
    }
  }
  return false;
}
// ---- table lookups --------------------------------------------------------
Bytes cpal_palette(const Face &f, unsigned palette) {
  Bytes c = f.cpal;
  if (!c.has(0, 12)) return Bytes{};
  unsigned entries = c.u16(2), palettes = c.u16(4), records = c.u16(6);
  uint32_t array = c.u32(8);
  if (!palettes) return Bytes{};
  if (palette >= palettes) palette = 0;
  if (!c.has(12, size_t(palettes) * 2)) return Bytes{};
  unsigned first = c.u16(12 + palette * 2);
  if (first > records || entries > records - first) return Bytes{};
  return c.sub(size_t(array) + size_t(first) * 4, size_t(entries) * 4);
}
bool colr_v0_layers(const Face &f, unsigned glyph, unsigned &first, unsigned &count) {
  Bytes c = f.colr;
  if (!c.has(0, 14)) return false;
  unsigned n = c.u16(2);
  uint32_t bases = c.u32(4), layers = c.u32(8);
  unsigned total = c.u16(12);
  if (!c.has(bases, size_t(n) * 6) || !c.has(layers, size_t(total) * 4)) return false;
  unsigned lo = 0, hi = n;
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2, g = c.u16(bases + mid * 6);
    if (g < glyph) lo = mid + 1;
    else if (g > glyph) hi = mid;
    else {
      first = c.u16(bases + mid * 6 + 2);
      count = c.u16(bases + mid * 6 + 4);
      return count && first <= total && count <= total - first && count <= 256;
    }
  }
  return false;
}
bool colr_v1_base(const Face &f, unsigned glyph) {
  Bytes c = f.colr;
  if (!c.has(0, 34) || c.u16(0) != 1) return false;
  uint32_t list = c.u32(14);
  if (!list || !c.has(list, 4)) return false;
  Bytes b = c.sub(list, c.n - list);
  uint32_t count = b.u32(0);
  if (!b.has(4, size_t(count) * 6)) return false;
  uint32_t lo = 0, hi = count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    unsigned g = b.u16(4 + size_t(mid) * 6);
    if (g < glyph) lo = mid + 1;
    else if (g > glyph) hi = mid;
    else return true;
  }
  return false;
}
bool colr_clip_box(const ColorContext &cx, unsigned glyph, double box[4]) {
  Bytes c = cx.colr;
  uint32_t clip = c.has(0, 34) ? c.u32(22) : 0;
  if (!clip || !c.has(clip, 5) || c.p[clip] != 1) return false;
  Bytes l = c.sub(clip, c.n - clip);
  uint32_t n = l.u32(1);
  if (!l.has(5, size_t(n) * 7)) return false;
  for (uint32_t i = 0; i < n; ++i) {
    size_t o = 5 + size_t(i) * 7;
    if (glyph < l.u16(o) || glyph > l.u16(o + 2)) continue;
    uint32_t bo = off24(l, o + 4);
    if (!bo || !l.has(bo, 9)) return false;
    unsigned format = l.p[bo];
    uint32_t vb = format == 2 && l.has(bo, 13) ? l.u32(bo + 9) : 0xffffffffu;
    for (unsigned k = 0; k < 4; ++k) box[k] = l.s16(bo + 1 + k * 2) + colr_delta(cx, vb, k);
    return true;
  }
  return false;
}
// CBLC strike + glyph location. Returns PNG bytes and bitmap metrics.
struct StrikeImage {
  Bytes png{};
  int bearing_x{}, bearing_y{}, ppem{};
  bool bottom_left{}; // sbix: origin offsets are to the bottom-left corner
};
bool cbdt_image(const Face &f, unsigned glyph, int px, StrikeImage &img) {
  Bytes l = f.cblc, d = f.cbdt;
  if (!l.has(0, 8) || !d.has(0, 4)) return false;
  uint32_t sizes = l.u32(4);
  if (sizes > 64 || !l.has(8, size_t(sizes) * 48)) return false;
  int best = -1, best_ppem = 0;
  for (uint32_t i = 0; i < sizes; ++i) {
    size_t o = 8 + size_t(i) * 48;
    unsigned start = l.u16(o + 40), end = l.u16(o + 42), ppem = l.p[o + 44];
    if (glyph < start || glyph > end || l.p[o + 46] != 32) continue;
    // Smallest strike >= px, else the largest one.
    bool better = best < 0 || (int(ppem) >= px ? (best_ppem < px || int(ppem) < best_ppem) : (best_ppem < px && int(ppem) > best_ppem));
    if (better) { best = int(i); best_ppem = int(ppem); }
  }
  if (best < 0) return false;
  size_t o = 8 + size_t(best) * 48;
  uint32_t array = l.u32(o), subtables = l.u32(o + 8);
  if (subtables > 4096 || !l.has(array, size_t(subtables) * 8)) return false;
  for (uint32_t s = 0; s < subtables; ++s) {
    size_t e = array + size_t(s) * 8;
    unsigned first = l.u16(e), last = l.u16(e + 2);
    if (glyph < first || glyph > last) continue;
    size_t sub = size_t(array) + l.u32(e + 4);
    if (!l.has(sub, 8)) return false;
    unsigned index_format = l.u16(sub), image_format = l.u16(sub + 2);
    uint32_t image_base = l.u32(sub + 4), start = 0, end = 0;
    unsigned k = glyph - first;
    const uint8_t *big = nullptr;
    if (index_format == 1) {
      if (!l.has(sub + 8, size_t(last - first + 2) * 4)) return false;
      start = l.u32(sub + 8 + k * 4); end = l.u32(sub + 12 + k * 4);
    } else if (index_format == 3) {
      if (!l.has(sub + 8, size_t(last - first + 2) * 2)) return false;
      start = l.u16(sub + 8 + k * 2); end = l.u16(sub + 10 + k * 2);
    } else if (index_format == 2) {
      if (!l.has(sub + 8, 12)) return false;
      uint32_t size = l.u32(sub + 8);
      start = size * k; end = start + size;
      big = l.p + sub + 12;
    } else if (index_format == 4 || index_format == 5) {
      bool fixed = index_format == 5;
      size_t at = sub + 8;
      uint32_t size = 0;
      if (fixed) { if (!l.has(at, 12)) return false; size = l.u32(at); big = l.p + at + 4; at += 12; }
      uint32_t n = l.u32(at); at += 4;
      if (n > 65536) return false;
      bool found = false;
      for (uint32_t i = 0; i < n && !found; ++i) {
        if (fixed) {
          if (!l.has(at + size_t(i) * 2, 2)) return false;
          if (l.u16(at + i * 2) == glyph) { start = size * i; end = start + size; found = true; }
        } else {
          if (!l.has(at + size_t(i) * 4, 8)) return false;
          if (l.u16(at + i * 4) == glyph) { start = l.u16(at + i * 4 + 2); end = l.u16(at + i * 4 + 6); found = true; }
        }
      }
      if (!found) return false;
    } else
      return false;
    if (end <= start || !d.has(size_t(image_base) + start, end - start)) return false;
    Bytes data = d.sub(size_t(image_base) + start, end - start);
    size_t at = 0;
    if (image_format == 17) { // small metrics + PNG
      if (!data.has(0, 9)) return false;
      img.bearing_x = int8_t(data.p[2]); img.bearing_y = int8_t(data.p[3]); at = 5;
    } else if (image_format == 18) { // big metrics + PNG
      if (!data.has(0, 12)) return false;
      img.bearing_x = int8_t(data.p[2]); img.bearing_y = int8_t(data.p[3]); at = 8;
    } else if (image_format == 19) { // metrics in CBLC (format 2/5)
      if (!big) return false;
      img.bearing_x = int8_t(big[2]); img.bearing_y = int8_t(big[3]); at = 0;
    } else
      return false; // monochrome / uncompressed CBDT formats: not supported
    if (!data.has(at, 4)) return false;
    uint32_t len = data.u32(at);
    if (!data.has(at + 4, len)) return false;
    img.png = data.sub(at + 4, len);
    img.ppem = best_ppem;
    return true;
  }
  return false;
}
bool sbix_image(const Face &f, unsigned glyph, int px, StrikeImage &img, unsigned depth = 0) {
  Bytes s = f.sbix;
  if (!s.has(0, 8) || depth > 1) return false;
  uint32_t strikes = s.u32(4);
  if (!strikes || strikes > 64 || !s.has(8, size_t(strikes) * 4)) return false;
  int best = -1, best_ppem = 0;
  for (uint32_t i = 0; i < strikes; ++i) {
    uint32_t o = s.u32(8 + i * 4);
    if (!s.has(o, 4 + (size_t(f.glyphs) + 1) * 4)) continue;
    int ppem = s.u16(o);
    uint32_t a = s.u32(o + 4 + size_t(glyph) * 4), b = s.u32(o + 8 + size_t(glyph) * 4);
    if (b <= a) continue; // no data for this glyph in this strike
    bool better = best < 0 || (ppem >= px ? (best_ppem < px || ppem < best_ppem) : (best_ppem < px && ppem > best_ppem));
    if (better) { best = int(o); best_ppem = ppem; }
  }
  if (best < 0) return false;
  uint32_t a = s.u32(best + 4 + size_t(glyph) * 4), b = s.u32(best + 8 + size_t(glyph) * 4);
  Bytes data = s.sub(size_t(best) + a, b - a);
  if (!data.p || !data.has(0, 8)) return false;
  uint32_t type = data.u32(4);
  if (type == 0x64757065) { // 'dupe'
    if (!data.has(8, 2)) return false;
    unsigned target = data.u16(8);
    return target != glyph && target < f.glyphs && sbix_image(f, target, px, img, depth + 1);
  }
  if (type != 0x706e6720) return false; // only 'png ' (jpg/tiff/pdf/mask not supported)
  img.bearing_x = data.s16(0);
  img.bearing_y = data.s16(2);
  img.png = data.sub(8, data.n - 8);
  img.ppem = best_ppem;
  img.bottom_left = true;
  return true;
}
bool render_bitmap(const StrikeImage &img, int px, ColorGlyph &out, uint32_t *argb, ColorStats *stats) {
  uint32_t *pixels = nullptr;
  unsigned w = 0, h = 0;
  if (!img.ppem || !decode_png(img.png.p, img.png.n, pixels, w, h)) return false;
  if (stats) ++stats->images;
  double scale = double(px) / img.ppem;
  int ow = int(w * scale + .5), oh = int(h * scale + .5);
  if (ow > int(MaxBitmap) || oh > int(MaxBitmap)) {
    double k = double(MaxBitmap) / (ow > oh ? ow : oh);
    scale *= k; ow = int(w * scale + .5); oh = int(h * scale + .5);
  }
  if (ow < 1) ow = 1;
  if (oh < 1) oh = 1;
  // Area-average resampling in premultiplied space.
  for (int y = 0; y < oh; ++y)
    for (int x = 0; x < ow; ++x) {
      double x0 = x / scale, x1 = (x + 1) / scale, y0 = y / scale, y1 = (y + 1) / scale;
      double r = 0, g = 0, b = 0, a = 0, total = 0;
      for (int sy = int(y0); sy < int(y1 + .999999) && sy < int(h); ++sy)
        for (int sx = int(x0); sx < int(x1 + .999999) && sx < int(w); ++sx) {
          double wx = (sx + 1 < x1 ? sx + 1 : x1) - (sx > x0 ? sx : x0), wy = (sy + 1 < y1 ? sy + 1 : y1) - (sy > y0 ? sy : y0);
          if (wx <= 0 || wy <= 0) continue;
          double k = wx * wy;
          uint32_t c = pixels[size_t(sy) * w + sx];
          double ca = (c >> 24) / 255.;
          r += ((c >> 16) & 255) * ca * k; g += ((c >> 8) & 255) * ca * k; b += (c & 255) * ca * k; a += ca * k; total += k;
        }
      uint32_t v = 0;
      if (total > 0 && a > 0) {
        double alpha = a / total;
        auto ch = [&](double s) { double q = s / a; return unsigned(q > 255 ? 255 : q + .5); };
        v = (unsigned(alpha * 255 + .5) << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
      }
      argb[y * ow + x] = v;
    }
  delete[] pixels;
  out.width = ow;
  out.height = oh;
  out.x = int(c_floor(img.bearing_x * scale + .5));
  out.y = img.bottom_left ? -int(c_floor((img.bearing_y + double(h)) * scale + .5)) : -int(c_floor(img.bearing_y * scale + .5));
  return true;
}
} // namespace
unsigned color_glyph_format(const Face &f, unsigned glyph) {
  if (glyph >= f.glyphs) return ColorFormatNone;
  if (f.colr.p && f.cpal.p) {
    if (colr_v1_base(f, glyph)) return ColorFormatCOLRv1;
    unsigned first, count;
    if (colr_v0_layers(f, glyph, first, count)) return ColorFormatCOLRv0;
  }
  StrikeImage img;
  if (f.sbix.p && sbix_image(f, glyph, 64, img)) return ColorFormatSbix;
  if (f.cbdt.p && f.cblc.p && cbdt_image(f, glyph, 64, img)) return ColorFormatCBDT;
  return ColorFormatNone;
}
bool render_color_glyph(const Face &f, unsigned glyph, int px, uint32_t foreground, unsigned palette,
                        ColorGlyph &out, uint32_t *argb, size_t capacity, ColorStats *stats) {
  out = ColorGlyph{};
  if (px < 1 || px > 128 || !argb || capacity < MaxBitmap * MaxBitmap || glyph >= f.glyphs || f.metadata_only)
    return false;
  unsigned format = color_glyph_format(f, glyph);
  out.format = uint8_t(format);
  out.advance = f.advance(glyph, px);
  if (format == ColorFormatSbix || format == ColorFormatCBDT) {
    StrikeImage img;
    bool found = format == ColorFormatSbix ? sbix_image(f, glyph, px, img) : cbdt_image(f, glyph, px, img);
    return found && render_bitmap(img, px, out, argb, stats);
  }
  if (format != ColorFormatCOLRv0 && format != ColorFormatCOLRv1)
    return false;
  ColorContext cx{f, f.colr, {}, {}, {}, {}, cpal_palette(f, palette), foreground};
  cx.stats = stats;
  double s = double(px) / f.units;
  double box[4] = {0, double(f.descent), double(f.advance_units(glyph)), double(f.ascent)};
  if (format == ColorFormatCOLRv1) {
    Bytes c = f.colr;
    uint32_t list = c.u32(14), layers = c.u32(18), map = c.u32(26), store = c.u32(30);
    cx.base_list = list ? c.sub(list, c.n - list) : Bytes{};
    cx.layer_list = layers ? c.sub(layers, c.n - layers) : Bytes{};
    cx.var_map = map ? c.sub(map, c.n - map) : Bytes{};
    cx.var_store = store ? c.sub(store, c.n - store) : Bytes{};
    colr_clip_box(cx, glyph, box);
  } else {
    // COLRv0: union of layer outline boxes.
    unsigned first, count;
    colr_v0_layers(f, glyph, first, count);
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (unsigned i = 0; i < count; ++i) {
      unsigned g = f.colr.u16(f.colr.u32(8) + (first + i) * 4);
      struct B { double x0, y0, x1, y1; } bb{1e9, 1e9, -1e9, -1e9};
      PathSink sink{[](void *u, int kind, double x, double y, double c1x, double c1y, double c2x, double c2y) {
                      auto *b = static_cast<B *>(u);
                      double xs[3] = {x, c1x, c2x}, ys[3] = {y, c1y, c2y};
                      unsigned used = kind == 3 ? 2 : kind == 4 ? 3 : 1; // on-curve + controls
                      for (unsigned k = 0; k < used; ++k) {
                        if (xs[k] < b->x0) b->x0 = xs[k];
                        if (xs[k] > b->x1) b->x1 = xs[k];
                        if (ys[k] < b->y0) b->y0 = ys[k];
                        if (ys[k] > b->y1) b->y1 = ys[k];
                      }
                    }, &bb};
      if (!glyph_path(f, g, sink)) return false;
      if (bb.x0 < x0) x0 = bb.x0;
      if (bb.y0 < y0) y0 = bb.y0;
      if (bb.x1 > x1) x1 = bb.x1;
      if (bb.y1 > y1) y1 = bb.y1;
    }
    if (x0 > x1) return true; // only empty layers
    box[0] = x0; box[1] = y0; box[2] = x1; box[3] = y1;
  }
  int bx = int(c_floor(box[0] * s)), by = int(c_floor(-box[3] * s));
  int bw = int(c_floor(box[2] * s) + 1) - bx, bh = int(c_floor(-box[1] * s) + 1) - by;
  if (bw < 1 || bh < 1) return true;
  if (bw > int(MaxBitmap)) bw = int(MaxBitmap);
  if (bh > int(MaxBitmap)) bh = int(MaxBitmap);
  cx.w = bw;
  cx.h = bh;
  Affine M{s, 0, 0, -s, -double(bx), -double(by)};
  Canvas dst;
  if (!alloc_canvas(cx, dst)) return false;
  bool ok;
  if (format == ColorFormatCOLRv1)
    ok = colr_glyph(cx, glyph, M, dst);
  else {
    unsigned first, count;
    colr_v0_layers(f, glyph, first, count);
    auto *mask = BSF_NEW uint8_t[size_t(bw) * bh];
    ok = mask != nullptr;
    for (unsigned i = 0; ok && i < count; ++i) {
      size_t at = f.colr.u32(8) + size_t(first + i) * 4;
      ok = glyph_mask(cx, f.colr.u16(at), M, mask);
      Rgba c = palette_color(cx, f.colr.u16(at + 2), 1);
      for (int k = 0; ok && k < bw * bh; ++k) {
        float m = mask[k] / 255.f;
        dst.px[k] = composite(3, Rgba{c.r * m, c.g * m, c.b * m, c.a * m}, dst.px[k]);
      }
      ++cx.layers;
    }
    delete[] mask;
  }
  if (ok && !cx.failed) {
    for (int k = 0; k < bw * bh; ++k) {
      Rgba v = dst.px[k];
      float a = v.a > 1 ? 1 : v.a < 0 ? 0 : v.a;
      uint32_t A = uint32_t(a * 255 + .5f), R = 0, G = 0, B = 0;
      if (a > 0) {
        auto ch = [&](float c) { float q = c / a; return uint32_t((q > 1 ? 1 : q < 0 ? 0 : q) * 255 + .5f); };
        R = ch(v.r); G = ch(v.g); B = ch(v.b);
      }
      argb[k] = (A << 24) | (R << 16) | (G << 8) | B;
    }
    out.width = bw;
    out.height = bh;
    out.x = bx;
    out.y = by;
  }
  if (stats) {
    stats->paints += cx.paints;
    stats->layers += cx.layers;
    stats->glyphs += cx.glyph_refs;
    if (cx.max_depth > stats->max_depth) stats->max_depth = cx.max_depth;
    stats->limited |= cx.failed;
  }
  free_canvas(cx, dst);
  return ok && !cx.failed;
}
