// BSF 2.2 Milestone 7 — emoji through the production runtime (bsf/runtime.cpp:
// fallback with emoji presentation, shaped layout with GSUB emoji
// sequences, colour-glyph drawing) over a mocked Ring 3 catalog.
#include "bsf22_common.h"
#include "../bsf/asset_path.h"
#include "../bsf/runtime.h"
#include "../jadro/gui/bursztyn_gui.h"
#include <map>
static std::map<std::string, std::string> files;
static std::map<std::string, std::string> listings;
static BSF::Config stored;
void wypisz(const char *) {}
bool utworz(const char *) { return false; }
bool zapisz_plik(const char *, const char *, uint32_t) { return false; }
bool wylistuj_katalog_uzytkownika(const char *path, char *out, uint32_t n) {
  auto it = listings.find(path);
  if (it == listings.end() || it->second.size() >= n) return false;
  memcpy(out, it->second.c_str(), it->second.size() + 1);
  return true;
}
bool pobierz_rozmiar_pliku(const char *path, uint32_t *n) {
  auto it = files.find(path);
  if (it == files.end()) return false;
  *n = uint32_t(it->second.size());
  return true;
}
bool czytaj_plik(const char *path, char *out, uint32_t n) {
  auto it = files.find(path);
  if (it == files.end() || n != it->second.size()) return false;
  memcpy(out, it->second.data(), n);
  return true;
}
extern "C" uint64_t bws_wywolaj(uint64_t fn, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  if (fn == 92 && b && c == sizeof(BSF::Config)) {
    if (!a) { *(BSF::Config *)b = stored; return 1; }
    return 0;
  }
  (void)d;
  return 0;
}
static void put(const std::string &path, const std::string &source) {
  auto v = read(source);
  CHECK(!v.empty());
  files[path] = std::string(v.begin(), v.end());
  listings[path.substr(0, 13)] += "[PLIK] " + path.substr(14) + "\n";
}
static std::string family_of(uint32_t id) { const char *f = BSF::family_name(id); return f ? f : ""; }
struct Draw {
  unsigned masks{}, colors{}, color_ink{};
  int color_top{1 << 20}, color_bottom{-(1 << 20)};
};
static void mask_cb(void *u, int, int, int, int, const uint8_t *) { ++static_cast<Draw *>(u)->masks; }
static void color_cb(void *u, int, int y, int w, int h, const uint32_t *argb) {
  auto &d = *static_cast<Draw *>(u);
  ++d.colors;
  for (int i = 0; i < w * h; ++i) d.color_ink += (argb[i] >> 24) > 128;
  d.color_top = std::min(d.color_top, y);
  d.color_bottom = std::max(d.color_bottom, y + h);
}
int main() {
  stored.checksum = BSF::checksum(stored);
  listings["/czcionki/ttf"] = "";
  put("/czcionki/ttf/DejaVuSans.ttf", "czcionki/ttf/DejaVuSans.ttf");
  put("/czcionki/ttf/NotoColorEmoji.ttf", "testy/fixtures/bsf22/color/NotoColorEmoji-subset.ttf");
  BSF::shutdown();
  CHECK(BSF::enumerate() == 2);
  uint32_t emoji_id = 0, dejavu = 0;
  for (unsigned i = 0; i < 2; ++i) {
    auto *f = BSF::info(i);
    if (f->color) emoji_id = f->id; else dejavu = f->id;
    if (f->color) CHECK(f->outline == uint8_t(BSF::OutlineFormat::Bitmap) && (f->color & BSF::ColorCBDT));
  }
  CHECK(emoji_id && dejavu);
  // ---- presentation and fallback --------------------------------------
  struct Case { const char *text; bool color; } cases[] = {
      {"😀", true}, {"☺", false}, {"☺️", true}, {"☺︎", false}, {"❤️", true}, {"👍", true},
      {"👍🏽", true}, {"👨‍👩‍👧", true}, {"🇵🇱", true}, {"#️⃣", true}, {"1️⃣", true}, {"✌️", true}, {"🏳️‍🌈", true}};
  for (auto &c : cases) {
    size_t n = strlen(c.text);
    CHECK(BSF::cluster_count(c.text, n) == 1); // one grapheme (UAX #29)
    uint32_t id = BSF::resolved_cluster(c.text, n);
    if ((id == emoji_id) != c.color)
      std::fprintf(stderr, "  '%s': resolved to %s\n", c.text, family_of(id).c_str());
    CHECK((id == emoji_id) == c.color);
    CHECK(BSF::emoji_presentation(c.text, n) == (c.color ? 1 : strstr(c.text, "\xef\xb8\x8e") ? -1 : 0));
    // Shaped: emoji sequences become one colour glyph (GSUB ligatures).
    if (c.color) {
      BSF::TextLayout L;
      BSF::LayoutRequest r;
      r.px = 32;
      CHECK(BSF::build_layout(L, c.text, n, r));
      unsigned visible = 0;
      for (size_t g = 0; g < L.glyph_count(); ++g)
        if (!(L.glyphs()[g].flags & (BSF::GlyphHidden)) && L.glyphs()[g].advance) ++visible;
      if (visible != 1) std::fprintf(stderr, "  '%s': %u visible glyphs\n", c.text, visible);
      CHECK(visible == 1);
      for (size_t g = 0; g < L.glyph_count(); ++g)
        if (L.glyphs()[g].advance) CHECK(L.glyphs()[g].face == emoji_id && !(L.glyphs()[g].flags & BSF::GlyphMissing));
      Draw d;
      CHECK(BSF::draw_layout_line_color(L, 0, 0, 0, 32, false, false, nullptr, 0xff000000u, mask_cb, color_cb, &d));
      CHECK(d.colors == 1 && d.color_ink > 100 && d.masks == 0);
    }
  }
  // ---- mixed text and emoji, baseline alignment ---------------------
  {
    const char *text = "Zażółć 😀 gęślą 👍🏽!";
    BSF::TextLayout L;
    BSF::LayoutRequest r;
    r.px = 24;
    CHECK(BSF::build_layout(L, text, strlen(text), r));
    Draw d;
    CHECK(BSF::draw_layout_line_color(L, 0, 0, 0, 24, false, false, nullptr, 0xff000000u, mask_cb, color_cb, &d));
    CHECK(d.colors == 2 && d.masks >= 12);
    int asc = BSF::ascent(24), h = BSF::height(24);
    // Emoji sit on the text baseline: their box spans ascent..descent.
    CHECK(d.color_top >= -2 && d.color_top <= asc / 2);
    CHECK(d.color_bottom > asc && d.color_bottom <= h + 3);
    // Logical text is untouched; carets stay on grapheme boundaries.
    CHECK(L.bytes() == strlen(text) && !memcmp(L.text(), text, L.bytes()));
    for (size_t off = 0; off <= strlen(text); ++off)
      if (!BSF::cluster_boundary(text, strlen(text), off)) CHECK(L.caret_x(off) == L.caret_x(BSF::cluster_floor(text, strlen(text), off)));
    // Without a colour callback the old API still works (emoji skipped).
    Draw m;
    CHECK(BSF::draw_layout_line(L, 0, 0, 0, 24, false, false, nullptr, mask_cb, &m));
    CHECK(m.colors == 0 && m.masks >= 12);
  }
  // ---- missing colour glyph: emoji not in the font --------------------
  {
    const char *text = "🦄";
    CHECK(BSF::resolved_cluster(text, strlen(text)) == 0);
    BSF::TextLayout L;
    BSF::LayoutRequest r;
    r.px = 24;
    CHECK(BSF::build_layout(L, text, strlen(text), r));
    CHECK(L.glyph_count() >= 1 && (L.glyphs()[0].flags & BSF::GlyphMissing));
    Draw d;
    CHECK(BSF::draw_layout_line_color(L, 0, 0, 0, 24, false, false, nullptr, 0xff000000u, mask_cb, color_cb, &d));
    CHECK(d.colors == 0 && d.masks == 1); // bitmap fallback box, never a crash
  }
  // ---- colour cache bounded and invalidated by rescan ----------------
  for (int px = 8; px <= 128; px += 4) {
    const uint32_t *argb;
    BSF::TextLayout L;
    BSF::LayoutRequest r;
    r.px = px;
    CHECK(BSF::build_layout(L, "😀", 4, r));
    CHECK(BSF::color_glyph_id_face(L.glyphs()[0].face, L.glyphs()[0].glyph, px, 0xff000000u, &argb) && argb);
  }
  BSF::rescan();
  CHECK(BSF::enumerate() == 2);
  BSF::shutdown();
  std::printf("BSF 2.2 emoji runtime: %lu checks PASS\n", checks);
}
