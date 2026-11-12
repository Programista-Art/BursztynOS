// Shared helpers for the BSF 2.2 host tests: file reading and outline
// comparison against FreeType (test-only reference).
#pragma once
#include "../bsf/bsf.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace BSF;
static unsigned long checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);        \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
static std::vector<uint8_t> read(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
struct Seg {
  int kind;
  double x, y, c1x, c1y, c2x, c2y;
};
using Path = std::vector<std::vector<Seg>>; // contours
static void bsf_segment(void *ctx, int kind, double x, double y, double c1x,
                        double c1y, double c2x, double c2y) {
  auto &p = *static_cast<Path *>(ctx);
  if (kind == 1) {
    p.push_back({});
    p.back().push_back({1, x, y, 0, 0, 0, 0});
    return;
  }
  if (p.empty())
    p.push_back({});
  p.back().push_back({kind, x, y, c1x, c1y, c2x, c2y});
}
struct FtCtx {
  Path p;
};
static int ft_move(const FT_Vector *to, void *u) {
  auto &p = static_cast<FtCtx *>(u)->p;
  p.push_back({});
  p.back().push_back({1, double(to->x), double(to->y), 0, 0, 0, 0});
  return 0;
}
static int ft_line(const FT_Vector *to, void *u) {
  static_cast<FtCtx *>(u)->p.back().push_back(
      {2, double(to->x), double(to->y), 0, 0, 0, 0});
  return 0;
}
static int ft_conic(const FT_Vector *c, const FT_Vector *to, void *u) {
  static_cast<FtCtx *>(u)->p.back().push_back(
      {3, double(to->x), double(to->y), double(c->x), double(c->y), 0, 0});
  return 0;
}
static int ft_cubic(const FT_Vector *a, const FT_Vector *b, const FT_Vector *to,
                    void *u) {
  static_cast<FtCtx *>(u)->p.back().push_back(
      {4, double(to->x), double(to->y), double(a->x), double(a->y),
       double(b->x), double(b->y)});
  return 0;
}
// Drop zero-length closing lines and empty contours (both engines may or may
// not emit an explicit closing segment).
static Path normalize(const Path &in, double tol) {
  Path out;
  for (const auto &c : in) {
    std::vector<Seg> k;
    for (const auto &s : c) {
      if (s.kind == 2 && !k.empty() && std::fabs(s.x - k.back().x) <= tol &&
          std::fabs(s.y - k.back().y) <= tol)
        continue;
      k.push_back(s);
    }
    // Closing line back to the start point is implicit.
    if (k.size() > 1 && k.back().kind == 2 &&
        std::fabs(k.back().x - k[0].x) <= tol &&
        std::fabs(k.back().y - k[0].y) <= tol)
      k.pop_back();
    if (k.size() > 1)
      out.push_back(k);
  }
  return out;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static bool same_path(const Path &a, const Path &b, double tol, std::string &why) {
  if (a.size() != b.size()) {
    why = "contours " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    return false;
  }
  for (size_t c = 0; c < a.size(); ++c) {
    if (a[c].size() != b[c].size()) {
      why = "segments in contour " + std::to_string(c) + ": " +
            std::to_string(a[c].size()) + " vs " + std::to_string(b[c].size());
      return false;
    }
    for (size_t i = 0; i < a[c].size(); ++i) {
      const auto &s = a[c][i], &t = b[c][i];
      bool ok = s.kind == t.kind && near(s.x, t.x, tol) && near(s.y, t.y, tol);
      if (ok && s.kind >= 3)
        ok = near(s.c1x, t.c1x, tol) && near(s.c1y, t.c1y, tol);
      if (ok && s.kind == 4)
        ok = near(s.c2x, t.c2x, tol) && near(s.c2y, t.c2y, tol);
      if (!ok) {
        char b2[256];
        std::snprintf(b2, sizeof b2,
                      "contour %zu seg %zu kind %d/%d (%.2f,%.2f) vs (%.2f,%.2f)",
                      c, i, s.kind, t.kind, s.x, s.y, t.x, t.y);
        why = b2;
        return false;
      }
    }
  }
  return true;
}
// Compare every glyph outline of face `index` with FreeType (NO_SCALE).
static unsigned compare_outlines(FT_Library lib, const std::vector<uint8_t> &bytes,
                                 unsigned index, const Face &f, const char *label) {
  FT_Face ft;
  CHECK(!FT_New_Memory_Face(lib, bytes.data(), FT_Long(bytes.size()), FT_Long(index), &ft));
  CHECK(unsigned(ft->num_glyphs) == f.glyphs);
  CHECK(ft->units_per_EM == f.units);
  FT_Outline_Funcs funcs{ft_move, ft_line, ft_conic, ft_cubic, 0, 0};
  unsigned mismatches = 0;
  for (unsigned gid = 0; gid < f.glyphs; ++gid) {
    Path mine;
    CHECK(glyph_path(f, gid, PathSink{bsf_segment, &mine}));
    CHECK(!FT_Load_Glyph(ft, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING));
    FtCtx ref;
    CHECK(!FT_Outline_Decompose(&ft->glyph->outline, &funcs, &ref));
    std::string why;
    if (!same_path(normalize(mine, .55), normalize(ref.p, .55), 1.01, why) &&
        ++mismatches <= 3)
      std::fprintf(stderr, "  %s face %u gid %u: %s\n", label, index, gid, why.c_str());
  }
  FT_Done_Face(ft);
  return mismatches;
}
