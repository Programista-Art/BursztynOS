// BSF 2.2 Milestone 6 — OpenType variable fonts.
// Production code: fvar/avar axis model, gvar (TrueType) and CFF2 blend
// outlines, HVAR/MVAR metrics (bsf/variations.inc, bsf/cff.inc, bsf/bsf.cpp).
// FreeType (host-only reference) supplies axes, named instances and outlines
// at the same design coordinates. Advances/GPOS at instances are compared with
// HarfBuzz by test-bsf22-shaping-host (testy/fixtures/bsf22/shaping_variations.tsv).
#include "bsf22_common.h"
#include FT_MULTIPLE_MASTERS_H
#include <memory>
#include <random>
static const std::string fx = "testy/fixtures/bsf22/fonts/";
static unsigned compare_instance(FT_Face ft, const Face &f, const std::vector<double> &design,
                                 unsigned stride, unsigned &compared, double tolerance) {
  std::vector<FT_Fixed> c;
  for (double v : design)
    c.push_back(FT_Fixed(v * 65536.0 + (v < 0 ? -.5 : .5)));
  CHECK(!FT_Set_Var_Design_Coordinates(ft, FT_UInt(c.size()), c.data()));
  FT_Outline_Funcs funcs{ft_move, ft_line, ft_conic, ft_cubic, 0, 0};
  unsigned bad = 0;
  for (unsigned gid = 0; gid < f.glyphs; gid += stride) {
    Path mine;
    CHECK(glyph_path(f, gid, PathSink{bsf_segment, &mine}));
    // FreeType's unscaled outlines are whole font units (16.16 values are
    // floored); BSF keeps 1/8 unit for CFF2 blends. Floor like FreeType so
    // sub-unit segments collapse identically in both engines.
    for (auto &c : mine)
      for (auto &seg : c)
        for (double *v : {&seg.x, &seg.y, &seg.c1x, &seg.c1y, &seg.c2x, &seg.c2y})
          *v = std::floor(*v + 1e-6);
    CHECK(!FT_Load_Glyph(ft, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING));
    FtCtx ref;
    CHECK(!FT_Outline_Decompose(&ft->glyph->outline, &funcs, &ref));
    std::string why;
    ++compared;
    if (!same_path(normalize(mine, .55), normalize(ref.p, .55), tolerance, why) && ++bad <= 3)
      std::fprintf(stderr, "    gid %u: %s\n", gid, why.c_str());
  }
  return bad;
}
static unsigned ink(Cache &cache, const Face &f, unsigned gid, int px) {
  const Glyph *g = cache.get_glyph(f, gid, px);
  CHECK(g);
  unsigned sum = 0;
  for (int i = 0; i < g->width * g->height; ++i)
    sum += g->alpha[i];
  return sum;
}
static void font(FT_Library lib, const char *file, OutlineFormat outline, unsigned stride,
                 double tolerance) {
  auto bytes = read(fx + file);
  CHECK(!bytes.empty());
  Face f;
  CHECK(f.open(bytes.data(), bytes.size(), 7));
  CHECK(f.outline == outline);
  FT_Face ft;
  CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), 0, &ft));
  FT_MM_Var *mm = nullptr;
  CHECK(!FT_Get_MM_Var(ft, &mm));
  VariationAxis axes[MaxAxes];
  unsigned n = variation_axes(f, axes, MaxAxes);
  CHECK(n == mm->num_axis && n == f.axis_count);
  for (unsigned a = 0; a < n; ++a) {
    CHECK(axes[a].tag == mm->axis[a].tag);
    CHECK(std::fabs(axes[a].min - mm->axis[a].minimum / 65536.0) < 1e-3);
    CHECK(std::fabs(axes[a].def - mm->axis[a].def / 65536.0) < 1e-3);
    CHECK(std::fabs(axes[a].max - mm->axis[a].maximum / 65536.0) < 1e-3);
  }
  NamedInstance named[64];
  unsigned instances = named_instances(f, named, 64);
  CHECK(instances == mm->num_namedstyles);
  for (unsigned i = 0; i < instances && i < 64; ++i)
    for (unsigned a = 0; a < n; ++a)
      CHECK(std::fabs(named[i].coords[a] - mm->namedstyle[i].coords[a] / 65536.0) < 1e-3);
  // Instances: default, every axis at min/max, every named instance, random.
  std::vector<std::vector<double>> designs;
  std::vector<double> def(n);
  for (unsigned a = 0; a < n; ++a) def[a] = axes[a].def;
  designs.push_back(def);
  for (unsigned a = 0; a < n; ++a) {
    auto lo = def, hi = def;
    lo[a] = axes[a].min;
    hi[a] = axes[a].max;
    designs.push_back(lo);
    designs.push_back(hi);
  }
  for (unsigned i = 0; i < instances && i < 64; ++i)
    designs.push_back(std::vector<double>(named[i].coords, named[i].coords + n));
  std::mt19937 rng(2206);
  for (unsigned k = 0; k < 4; ++k) {
    std::vector<double> d(n);
    for (unsigned a = 0; a < n; ++a)
      d[a] = axes[a].min + (axes[a].max - axes[a].min) * (rng() % 1000) / 999.0;
    designs.push_back(d);
  }
  unsigned compared = 0, bad = 0;
  for (auto &d : designs) {
    VariationSetting s[MaxAxes];
    for (unsigned a = 0; a < n; ++a) s[a] = {axes[a].tag, d[a]};
    Face v = f;
    CHECK(set_variations(v, s, n));
    unsigned b = compare_instance(ft, v, d, stride, compared, tolerance);
    if (b) {
      std::fprintf(stderr, "  %s instance", file);
      for (double x : d) std::fprintf(stderr, " %.2f", x);
      std::fprintf(stderr, ": %u outline mismatches\n", b);
    }
    bad += b;
  }
  std::printf("  %-32s %u axes, %u named instances, %zu instances x %u glyphs: %u/%u outlines "
              "equal to FreeType\n",
              file, n, instances, designs.size(), (f.glyphs + stride - 1) / stride, compared - bad, compared);
  CHECK(bad == 0);
  // Weight really changes the rendering; instances never share cache slots.
  unsigned w = 0;
  while (w < n && axes[w].tag != 0x77676874) ++w;
  if (w < n) {
    auto cache = std::make_unique<Cache>();
    Face light = f, heavy = f;
    VariationSetting lo{axes[w].tag, axes[w].min}, hi{axes[w].tag, axes[w].max};
    CHECK(set_variations(light, &lo, 1) && set_variations(heavy, &hi, 1));
    // An instance equal to the default keeps the default identity.
    CHECK(light.id != heavy.id);
    CHECK((light.variable() ? light.id != f.id : light.id == f.id) && heavy.id != f.id);
    unsigned gid = f.glyph(f.glyph('o') ? 'o' : 0x5e9);
    if (!gid) gid = f.glyph(0x627);
    CHECK(gid);
    unsigned a = ink(*cache, light, gid, 48), b = ink(*cache, heavy, gid, 48), c = ink(*cache, f, gid, 48);
    CHECK(b > a * 13 / 10);
    CHECK(ink(*cache, light, gid, 48) == a); // cached per instance, not mixed
    (void)c;
    std::printf("    ink of glyph %u at 48 px: wght %.0f -> %u, wght %.0f -> %u\n", gid, axes[w].min, a,
                axes[w].max, b);
  }
  // Clamping and rejection.
  {
    Face v = f, m = f;
    VariationSetting far{axes[0].tag, axes[0].max + 1000}, max{axes[0].tag, axes[0].max};
    CHECK(set_variations(v, &far, 1) && set_variations(m, &max, 1));
    CHECK(v.coords[0] == m.coords[0] && v.id == m.id);
    VariationSetting nan{axes[0].tag, std::nan("")};
    Face x = f;
    CHECK(!set_variations(x, &nan, 1));
    VariationSetting unknown{0x58585858, 5};
    CHECK(set_variations(x, &unknown, 1) && !x.variable() && x.id == f.id);
  }
  FT_Done_MM_Var(lib, mm);
  FT_Done_Face(ft);
}
static void malformed() {
  auto base = read(fx + "NotoSansHebrew-VF.ttf");
  Face f;
  CHECK(f.open(base.data(), base.size(), 1));
  size_t gvar = size_t(f.gvar.p - base.data()), fvar = size_t(f.fvar.p - base.data());
  std::mt19937 rng(2207);
  auto cache = std::make_unique<Cache>();
  unsigned opened = 0;
  for (unsigned iter = 0; iter < 3000; ++iter) {
    auto v = base;
    for (unsigned e = 0, n = 1 + rng() % 8; e < n; ++e) {
      size_t at = (rng() & 1) ? gvar + rng() % f.gvar.n : (rng() % 3 ? gvar + rng() % 64 : fvar + rng() % f.fvar.n);
      v[at] = uint8_t(rng());
    }
    Face m;
    if (!m.open(v.data(), v.size(), 100 + iter)) continue;
    ++opened;
    VariationAxis axes[MaxAxes];
    unsigned n = variation_axes(m, axes, MaxAxes);
    VariationSetting s[MaxAxes];
    for (unsigned a = 0; a < n; ++a) s[a] = {axes[a].tag, axes[a].min + (axes[a].max - axes[a].min) * (rng() % 100) / 99.0};
    set_variations(m, s, n);
    for (unsigned k = 0; k < 6; ++k) {
      unsigned gid = rng() % m.glyphs;
      Path p;
      glyph_path(m, gid, PathSink{bsf_segment, &p});
      cache->get_glyph(m, gid, 8 + rng() % 100);
      m.advance_units(gid);
    }
    cache->clear();
  }
  std::printf("  malformed gvar/fvar: 3000 mutations, %u faces opened, no memory errors\n", opened);
}
int main() {
  FT_Library lib;
  CHECK(!FT_Init_FreeType(&lib));
  font(lib, "NotoSansHebrew-VF.ttf", OutlineFormat::TrueType, 1, 1.01);
  font(lib, "NotoNaskhArabic-VF.ttf", OutlineFormat::TrueType, 3, 1.01);
  font(lib, "SourceSerif4Variable-Roman.otf", OutlineFormat::CFF2, 9, 1.01);
  malformed();
  FT_Done_FreeType(lib);
  std::printf("BSF 2.2 variable fonts: %lu checks PASS\n", checks);
}
