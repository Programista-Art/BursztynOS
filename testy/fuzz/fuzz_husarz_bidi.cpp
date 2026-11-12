// BSF 2.1 Husarz bidi fuzzer (coverage-guided): token-built HTML with
// dir/bdi/bdo/unicode-bidi/direction, br/pre/plaintext, RTL/LTR/number/
// mark/ligature text, raw malformed bytes and form controls, through the
// production parser, styles, bidi paragraph pass, layout (various widths)
// and paint. Invariants: grapheme-aligned fragments, bounded levels and
// runs, logical DOM text unchanged by layout.
#define BSF_NATIVE 1
#include "fuzz_engine.h"
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "../../programy/husarz/paint.h"
#define BURSZTYN_HOST_CUSTOM_ALPHA 1
#include "../bursztyn_ui_host_fixture.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
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

static const char *tokens[] = {"<p>", "</p>", "<span dir=rtl>", "<span dir=ltr>", "<span dir=auto>", "</span>",
  "<bdo dir=rtl>", "</bdo>", "<bdi>", "</bdi>", "<br>", "<b>", "</b>", "<a href=x>", "</a>",
  "<div style='direction:rtl'>", "<div style='unicode-bidi:plaintext'>", "<div dir=rtl style='text-align:end'>", "</div>",
  "<span style='unicode-bidi:bidi-override;direction:rtl'>", "<span style='unicode-bidi:embed;direction:rtl'>",
  "<span style='unicode-bidi:isolate-override'>", "<span style='display:inline-block;direction:rtl'>",
  "<input dir=rtl value='\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d'>", "<input dir=auto value='abc \xd8\xa7'>", "<pre>", "</pre>", "\n", " ", "abc",
  "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", "123", "\xd9\xa1\xd9\xa2", "\xd9\x84\xd8\xa7", "e\xcc\x81",
  "\xe2\x80\xae", "\xe2\x81\xa7", "\xe2\x81\xa9", "<img alt=x>", "<table><tr><td>", "</td></tr></table>", "&amp;", "\xd7\x90\xd6\xb8"};
static void target(const uint8_t *data, size_t size) {
  if (!size) return;
  std::string html = "<html><body style='margin:0'>";
  for (size_t i = 1; i < size && html.size() < 6000; ++i) {
    uint8_t b = data[i];
    if (b >= 0xF0) html += char(b ^ 0x5a); // raw (often malformed) byte
    else html += tokens[b % (sizeof tokens / sizeof *tokens)];
  }
  auto d = std::make_unique<Husarz::Document>();
  Husarz::parse_html(*d, html.data(), html.size());
  Husarz::finish_work(Husarz::initialize_controls_work(*d));
  Husarz::collect_styles(*d);
  // Snapshot the logical text of every text node.
  std::vector<std::string> before;
  for (unsigned i = 0; i < d->node_count; ++i) before.emplace_back(d->str(d->nodes[i].text), d->nodes[i].text_length);
  int width = 40 + data[0] * 3;
  Husarz::reflow(*d, width, metrics);
  for (unsigned i = 0; i < d->fragment_count; ++i) {
    const auto &f = d->fragments[i];
    const char *t = d->str(f.text);
    if (f.length && !BSF::cluster_boundary(t, f.length, f.length)) abort();
    if (f.level > 126 || f.paragraph_level > 1) abort();
  }
  if (d->bidi_run_count > Husarz::MaxBidiRuns) abort();
  for (unsigned i = 0; i < d->node_count && i < before.size(); ++i)
    if (std::string(d->str(d->nodes[i].text), d->nodes[i].text_length) != before[i] && d->nodes[i].is_text) abort();
  Husarz::reflow(*d, width / 2 + 1, metrics); // resize reuses levels
  std::vector<uint32_t> px(size_t(200) * 120);
  Husarz::Surface s{px.data(), 200, 120};
  Husarz::paint(*d, s, 0, metrics, glyphs);
}
int main(int argc, char **argv) {
  g_test_bws_hook = hook; saved.checksum = BSF::checksum(saved);
  for (const char *name : {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf"}) {
    std::ifstream f(std::string("czcionki/ttf/") + name, std::ios::binary);
    files[std::string("/czcionki/ttf/") + name] = {std::istreambuf_iterator<char>(f), {}};
    directory += std::string("[PLIK] ") + name + "\n";
  }
  if (BSF::enumerate() != 3 || !BSF::ready()) return 2;
  Husarz::font_family_resolver = BSF::resolve_family;
  FuzzEngine engine("fuzz_husarz_bidi", 0x48425344, 512);
  engine.parse_args(argc, argv);
  const uint8_t seeds[][12] = {{60, 0, 30, 28, 2, 31, 5, 1, 10, 32, 33, 1}, {100, 15, 30, 32, 19, 29, 5, 18, 23, 7, 6, 18},
                               {7, 16, 30, 11, 29, 12, 10, 31, 25, 26, 27, 18}};
  for (auto &seed : seeds) engine.add_seed(std::vector<uint8_t>(seed, seed + 12));
  return engine.run(target);
}
