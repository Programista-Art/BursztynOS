// Husarz text layout/paint benchmark (BSF 2.0 vs 2.1). Public API only, so
// the same file builds in both trees: make bench-husarz-bidi
#define BSF_NATIVE 1
#include "../bursztyn_ui/bursztyn_ui.h"
#include "../programy/husarz/paint.h"
#define BURSZTYN_HOST_CUSTOM_ALPHA 1
#include "bursztyn_ui_host_fixture.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
using namespace BursztynUI;
static unsigned checks;
#include <chrono>
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d %s\n", __LINE__, #x);                           \
      abort();                                                                 \
    }                                                                          \
  } while (0)
static std::map<std::string, std::string> files;
static BSF::Config saved;
static std::string directory;
static unsigned directory_reads;
static uint64_t hook(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t) {
  if (n == 92) {
    auto *config = reinterpret_cast<BSF::Config *>(b);
    if (c != sizeof(*config))
      return 0;
    if (!a) {
      *config = saved;
      return 1;
    }
    if (!BSF::valid(*config))
      return 0;
    uint32_t generation=saved.generation+1;
    saved = *config;
    saved.generation=generation?generation:1;
    return 1;
  }
  if (n == 6) {
    // BSF 2.2 also scans the optional /czcionki/{otf,ttc,otc} directories;
    // this fixture provides /czcionki/ttf only.
    if (strcmp(reinterpret_cast<const char *>(a), "/czcionki/ttf"))
      return 0;
    CHECK(!strcmp(reinterpret_cast<const char *>(a), "/czcionki/ttf"));
    ++directory_reads;
    if (directory.size() + 1 > c)
      return 0;
    memcpy(reinterpret_cast<void *>(b), directory.c_str(),
           directory.size() + 1);
    return 1;
  }
  if (n == 4) {
    auto it = files.find(reinterpret_cast<const char *>(a));
    if (it == files.end())
      return 0;
    *reinterpret_cast<uint32_t *>(b) = uint32_t(it->second.size());
    return 1;
  }
  return 0;
}
bool czytaj_plik(const char *path, char *out, uint32_t capacity) {
  CHECK(!strncmp(path, "/czcionki/ttf/", 14));
  auto i = files.find(path);
  if (i == files.end() || i->second.size() > capacity)
    return false;
  memcpy(out, i->second.data(), i->second.size());
  return true;
}
void wypisz(const char *) {}

static int native_advance(void *, uint32_t cp, int px, bool bold, bool, uint32_t id) {
  int a = BSF::measure_cp(cp, px, bold, BSF::family_name(id)); return a < 0 ? px : a;
}
static int native_height(void *, int px, uint32_t id) { int h = BSF::height(px, BSF::family_name(id)); return h < 0 ? px : h; }
static bool native_cached(void *, uint32_t cp, int px, bool bold, bool italic, uint32_t id) { return BSF::cached(cp, px, bold, BSF::family_name(id), italic); }
static bool native_alpha(void *, uint32_t cp, int px, bool bold, bool italic, uint32_t id, Husarz::AlphaGlyph &out) {
  const char *family = BSF::family_name(id); auto *g = BSF::glyph(cp, px, bold, family, italic); if (!g) return false;
  BSF::pin(g); out.width = g->width; out.height = g->height; out.x = g->x; out.y = BSF::ascent(px, family) + g->y;
  out.pixels = g->alpha; out.lease = const_cast<BSF::Glyph *>(g); out.release = BSF::unpin; return true;
}
bool gui_blit_alpha(int, int, int, int, const uint32_t*) { return true; }
static Husarz::Metrics metrics{nullptr, nullptr, nullptr, native_advance, native_height};
static Husarz::Glyphs glyphs{nullptr, nullptr, nullptr, native_alpha, native_cached};

static double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
int main() {
  g_test_bws_hook = hook; saved.checksum = BSF::checksum(saved);
  for (const char *name : {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf"}) {
    std::ifstream f(std::string("czcionki/ttf/") + name, std::ios::binary);
    files[std::string("/czcionki/ttf/") + name] = {std::istreambuf_iterator<char>(f), {}};
    directory += std::string("[PLIK] ") + name + "\n";
  }
  CHECK(BSF::enumerate() == 3);
  Husarz::font_family_resolver = BSF::resolve_family;
  struct Case { const char *name; std::string html; } cases[3];
  std::string pl = "<html><body>", ar = "<html><body dir=rtl>", mix = "<html><body>";
  for (int p = 0; p < 60; ++p) {
    pl += "<p>Zażółć gęślą jaźń. The quick brown fox jumps over the lazy dog, <a href=x>link</a> <b>bold</b> 2026. Pchnąć w tę łódź jeża lub ośm skrzyń fig.</p>";
    ar += "<p>مرحبا بالعالم، هذا نص عربي طويل <a href=x>رابط</a> <b>غامق</b> 2026 שלום עולם וגם עברית כאן.</p>";
    mix += "<p>Polski tekst z hebrajskim שלום עולם i arabskim مرحبا بالعالم oraz liczbą 123.</p>";
  }
  cases[0] = {"polish-english", pl + "</body></html>"};
  cases[1] = {"arabic-hebrew-rtl", ar + "</body></html>"};
  cases[2] = {"mixed-ltr", mix + "</body></html>"};
  for (auto &c : cases) {
    auto d = std::make_unique<Husarz::Document>();
    Husarz::parse_html(*d, c.html.data(), c.html.size()); Husarz::collect_styles(*d);
    Husarz::reflow(*d, 800, metrics); // warm caches
    std::vector<uint32_t> px(size_t(800) * 600); Husarz::Surface s{px.data(), 800, 600};
    Husarz::paint(*d, s, 0, metrics, glyphs);
    const int R = 20;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < R; ++i) { d->styles_dirty = true; ++d->revision; Husarz::reflow(*d, 800 - (i & 1) * 40, metrics); }
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < R; ++i) { Husarz::reflow(*d, 800 - (i & 1) * 40, metrics); }
    auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < R; ++i) { Husarz::Surface t{px.data(), 800, 600}; Husarz::paint(*d, t, i * 7, metrics, glyphs); }
    auto t3 = std::chrono::steady_clock::now();
    printf("%s: full reflow %.2f ms, resize reflow %.2f ms, paint %.2f ms, fragments %u\n", c.name, ms(t0, t1) / R, ms(t1, t2) / R, ms(t2, t3) / R, d->fragment_count);
  }
  return 0;
}
