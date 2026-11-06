#define BSF_NATIVE 1
#include "../bursztyn_ui/bursztyn_ui.h"
#include "../programy/husarz/paint.h"
#define BURSZTYN_HOST_CUSTOM_ALPHA 1
#include "bursztyn_ui_host_fixture.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
using namespace BursztynUI;
static unsigned checks;
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
static unsigned alpha_calls;
static Prostokat alpha_bounds;
static std::vector<uint32_t> alpha_pixels;
bool gui_blit_alpha(int x, int y, int w, int h, const uint32_t* pixels) {
  ++alpha_calls;
  alpha_bounds = {x,y,w,h};
  alpha_pixels.assign(pixels,pixels+size_t(w)*h);
  return true;
}
static int native_advance(void *, uint32_t cp, int px, bool bold, bool,
                          uint32_t id) {
  int a = BSF::measure_cp(cp, px, bold, BSF::family_name(id));
  return a < 0 ? px : a;
}
static int native_height(void *, int px, uint32_t id) {
  int h = BSF::height(px, BSF::family_name(id));
  return h < 0 ? px : h;
}
static bool native_cached(void *, uint32_t cp, int px, bool bold, bool italic,
                          uint32_t id) {
  return BSF::cached(cp, px, bold, BSF::family_name(id), italic);
}
static bool native_alpha(void *, uint32_t cp, int px, bool bold, bool italic,
                         uint32_t id, Husarz::AlphaGlyph &out) {
  const char *family = BSF::family_name(id);
  auto *g = BSF::glyph(cp, px, bold, family, italic);
  if (!g)
    return false;
  BSF::pin(g);
  out.width = g->width;
  out.height = g->height;
  out.x = g->x;
  out.y = BSF::ascent(px, family) + g->y;
  out.pixels = g->alpha;
  out.lease = const_cast<BSF::Glyph *>(g);
  out.release = BSF::unpin;
  return true;
}
int main() {
  saved.checksum = BSF::checksum(saved);
  g_test_bws_hook = hook;
  for (const char *name :
       {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf"}) {
    std::ifstream f(std::string("czcionki/ttf/") + name, std::ios::binary);
    files[std::string("/czcionki/ttf/") + name] = {
        std::istreambuf_iterator<char>(f), {}};
    directory += std::string("[PLIK] ") + name + "\n";
  }
  directory += "[PLIK] ../../outside.ttf\n[PLIK] ignored.otf\n";
  CHECK(BSF::enumerate() == 3);
  CHECK(directory_reads == 1);
  CHECK(BSF::ready());
  CHECK(BSF::default_id() != 0);
  uint32_t sans =
               BSF::resolve_family("MissingWebFont, 'DejaVu Sans', sans-serif"),
           mono = BSF::resolve_family("monospace");
  CHECK(sans && mono && sans != mono);
  CHECK(BSF::measure_cp('W', 16, false, "DejaVu Sans") >
        BSF::measure_cp('i', 16, false, "DejaVu Sans"));
  CHECK(BSF::measure_cp('W', 16, false, "monospace") ==
        BSF::measure_cp('i', 16, false, "monospace"));
  auto previous = BSF::configuration();
  BSF::preview(mono, 24);
  CHECK(BSF::ui_size() == 24);
  CHECK(BSF::configuration().family == previous.family);
  BSF::end_preview();
  CHECK(BSF::ui_size() == 16);
  {
    CHECK(BSF::glyph('W',16));
    CHECK(BSF::cached('W',16,false,nullptr,false));
    auto resized=previous;resized.size=20;resized.family=BSF::default_id();
    CHECK(BSF::apply(resized));
    CHECK(BSF::cached('W',16,false,nullptr,false));
    CHECK(!BSF::cached('W',20,false,nullptr,false));
    CHECK(BSF::glyph('W',20));
    auto hits=BSF::statistics().hits;
    CHECK(BSF::glyph('W',16));
    CHECK(BSF::statistics().hits==hits+1);
    CHECK(BSF::apply(previous));
  }
  {
    Panel parent;
    auto& bar=parent.dodaj<PasekStanu>();
    bar.ustaw_preferowany_rozmiar({-1,24});
    bar.dodaj_sekcje("Zażółć gęślą jaźń gy");
    CHECK(bar.zmierz({800,600}).wysokosc==24);
    BSF::preview(sans,24);
    parent.zmierz({800,600});parent.rozmiesc({0,0,800,600});
    CHECK(bar.obszar().wysokosc>=CzcionkaUI::pobierz_wysokosc()+4);
    TabelaWirtualna table;
    CHECK(table.wysokosc_wiersza()>=CzcionkaUI::pobierz_wysokosc()+2);
    CHECK(table.wysokosc_naglowka()>=CzcionkaUI::pobierz_wysokosc()+4);
    bar.ustaw_skale_dpi(SkalaUI::procent(125));
    CHECK(bar.zmierz({1000,750}).wysokosc>=CzcionkaUI::pobierz_wysokosc(SkalaUI::procent(125))+5);
    BSF::end_preview();
    bar.ustaw_skale_dpi(SkalaUI::procent(100));
    CHECK(bar.zmierz({800,600}).wysokosc==24);
  }
  {
    Przycisk wide("WWWW"), narrow("iiii");
    CHECK(wide.zmierz({800, 600}).szerokosc >
          narrow.zmierz({800, 600}).szerokosc);
    Etykieta label("Zażółć gęślą jaźń");
    CHECK(label.zmierz({800, 600}).wysokosc >= BSF::height(16));
    const char *polish = "Wi ąęłóśźż";
    for (size_t at = 0; polish[at];) {
      uint32_t cp;
      size_t n = CzcionkaUI::dekoduj_utf8(polish, at, &cp);
      CHECK(CzcionkaUI::znak_z_pozycji_x(
                polish, CzcionkaUI::pozycja_x_z_bajtu(polish, at)) == at);
      at += n;
    }
    CHECK(CzcionkaUI::pozycja_x_z_bajtu("ą", 1) == 0);
    uint32_t replacement;
    CHECK(CzcionkaUI::dekoduj_utf8("\xff", 0, &replacement) == 1);
    CHECK(replacement == 0xfffd);
    CHECK(CzcionkaUI::pozycja_x_z_bajtu("\xff", 1) ==
          BSF::measure_cp(0xfffd, 16));
    std::vector<uint32_t> pixels(300 * 90, 0xffeef0f2);
    KontekstRysowania ctx({0, 0, 300, 90});
    ctx.ustaw_cel(pixels.data(), {0, 0, 300, 90});
    ctx.dodaj_przyciecie({30, 10, 200, 45});
    ctx.rysuj_tekst({20, 10}, "Zażółć gęślą jaźń", 0x112233);
    unsigned ink = 0, aa = 0;
    for (int y = 0; y < 90; ++y)
      for (int x = 0; x < 300; ++x) {
        uint32_t c = pixels[y * 300 + x];
        if (c != 0xffeef0f2) {
          CHECK(x >= 30 && x < 230 && y >= 10 && y < 55);
          ++ink;
          if ((c & 0xffffff) != 0x112233)
            ++aa;
        }
      }
    CHECK(ink > 100 && aa > 10);
    ctx.ustaw_cel(nullptr, {});
    alpha_calls = 0;
    ctx.rysuj_tekst({20, 10}, "Zażółć gęślą jaźń", 0x112233);
    CHECK(alpha_calls == 1);
    CHECK(alpha_bounds.x >= 30 && alpha_bounds.y >= 10);
    CHECK(alpha_bounds.prawy() <= 230 && alpha_bounds.dolny() <= 55);
    aa = 0;
    for (uint32_t pixel : alpha_pixels) {
      CHECK(!(pixel >> 24) || (pixel & 0xffffff) == 0x112233);
      aa += (pixel >> 24) > 0 && (pixel >> 24) < 255;
    }
    CHECK(aa > 10);
    PoleTekstowe field;
    field.ustaw_tekst("Zażółć");
    CHECK(!strcmp(field.tekst(), "Zażółć"));
    auto config = previous;
    config.family = mono;
    config.size = 20;
    CHECK(BSF::apply(config));
    CHECK(BSF::generation() != previous.generation);
    CHECK(BSF::ui_size() == 20);
    CHECK(BSF::measure_cp('W', 16) == BSF::measure_cp('i', 16));
    CHECK(BSF::resolve_family("'DejaVu Sans', sans-serif") == sans);
    CHECK(BSF::resolve_family("MissingWebFont, sans-serif") == mono);
    CHECK(wide.zmierz({800, 600}).wysokosc > BSF::height(16));
  }
  Husarz::font_family_resolver = BSF::resolve_family;
  Husarz::Metrics metrics{nullptr, nullptr, nullptr, native_advance,
                          native_height};
  Husarz::Glyphs glyphs{nullptr, nullptr, nullptr, native_alpha, native_cached};
  auto d = std::make_unique<Husarz::Document>();
  const char *html =
      "<html><body "
      "style=\"margin:0;background:#112233;color:#e74c3c;font-family:"
      "MissingWebFont,sans-serif;font-size:16px\"><h1 "
      "style=\"font-size:48px;font-weight:700;font-family:'DejaVu "
      "Sans'\">Bursztyn ąęłóźż</h1><p>WWWW iiii WWWW iiii</p></body></html>";
  Husarz::parse_html(*d, html, strlen(html));
  Husarz::collect_styles(*d);
  Husarz::reflow(*d, 780, metrics);
  CHECK(!d->truncated);
  bool found = false;
  for (unsigned i = 0; i < d->node_count; ++i)
    if (d->tag(i, "h1")) {
      CHECK(d->styles[i].native_family == sans);
      CHECK(d->styles[i].bold);
      CHECK(d->styles[i].font == 48);
      found = true;
    }
  CHECK(found);
  std::vector<uint32_t> page(780 * 420);
  Husarz::Surface surface{page.data(), 780, 420};
  Husarz::WorkDriver driver;
  driver.allow_clock = false;
  auto paint =
      Husarz::paint_work(*d, surface, 0, metrics, glyphs, {0, 0, 780, 420});
  while (!paint.step(driver)) {
  }
  CHECK(!driver.failed && driver.maximum <= 2048);
  unsigned red = 0, aa = 0;
  for (auto pixel : page) {
    if ((pixel & 0xffffff) == 0xe74c3c)
      ++red;
    if ((pixel & 0xffffff) != 0xe74c3c && (pixel & 0xffffff) != 0x112233)
      ++aa;
  }
  CHECK(red > 100 && aa > 100);
  CHECK(BSF::statistics().hits > 0);
  {
    Husarz::Style style;
    style.font = 127;
    style.native_family = sans;
    bool rendered = false;
    Husarz::WorkDriver cancelled;
    cancelled.allow_clock = false;
    auto work = Husarz::paint_alpha_glyph_work(surface, 'B', 0, 0, style,
                                              glyphs, {0,0,780,420}, rendered);
    CHECK(!work.step(cancelled));
    const auto* lease = BSF::glyph('B', 127, false, BSF::family_name(sans));
    CHECK(lease && lease->pins == 1);
    work.cancel();
    CHECK(lease->pins == 0);
    CHECK(!cancelled.failed && cancelled.maximum <= 2048);
  }

  BSF::shutdown();
  CHECK(BSF::ui_size() == 20);
  CHECK(BSF::resolve_family("sans-serif") == mono); // process restart
  BSF::shutdown();
  saved.magic = 0;
  CHECK(BSF::ui_size() == 16);
  CHECK(BSF::ready());
  BSF::shutdown();
  for (auto &f : files)
    f.second = "bad";
  CHECK(!BSF::ready());
  CHECK(CzcionkaUI::pobierz_wysokosc() == 16);
  CHECK(CzcionkaUI::zmierz_tekst("Czytelny pulpit") > 0);
  BSF::shutdown();
  Husarz::font_family_resolver = nullptr;
  printf("BSF UI/browser: %u checks PASS; proportional geometry, clipping/AA, "
         "Polish UTF-8, previews, persistence, font priority, cooperative "
         "paint and corrupt-font fallback\n",
         checks);
}
