// BSF 2.2 Milestone 5 — Husarz CSS @font-face (host).
// Production code: Husarz CSS parser (@font-face descriptors), the pure
// policy helpers (programy/husarz/webfonts.h: src order/format filtering,
// unicode-range, weight/style/stretch/display, MIME, response-side CORS,
// family rewriting), BSF browser-scoped faces (bsf/runtime.cpp), WOFF/WOFF2
// decoding (bsf/webfont.cpp) and the production Husarz layout/paint with real
// BSF metrics. The network/worker transport is native-only and covered by the
// QEMU pages in testy/husarz_webfonts/.
#define BSF_NATIVE 1
#include "../bursztyn_ui/bursztyn_ui.h"
#include "../programy/husarz/paint.h"
#include "../programy/husarz/webfonts.h"
#include "../bsf/webfont.h"
#define BURSZTYN_HOST_CUSTOM_ALPHA 1
#include "bursztyn_ui_host_fixture.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
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
static uint64_t hook(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t) {
  if (n == 92) {
    auto *config = reinterpret_cast<BSF::Config *>(b);
    if (c != sizeof(*config)) return 0;
    if (!a) { *config = saved; return 1; }
    return 0;
  }
  if (n == 6) {
    if (strcmp(reinterpret_cast<const char *>(a), "/czcionki/ttf")) return 0;
    if (directory.size() + 1 > c) return 0;
    memcpy(reinterpret_cast<void *>(b), directory.c_str(), directory.size() + 1);
    return 1;
  }
  if (n == 4) {
    auto it = files.find(reinterpret_cast<const char *>(a));
    if (it == files.end()) return 0;
    *reinterpret_cast<uint32_t *>(b) = uint32_t(it->second.size());
    return 1;
  }
  return 0;
}
bool czytaj_plik(const char *path, char *out, uint32_t capacity) {
  auto i = files.find(path);
  if (i == files.end() || i->second.size() > capacity) return false;
  memcpy(out, i->second.data(), i->second.size());
  return true;
}
void wypisz(const char *) {}
bool gui_blit_alpha(int, int, int, int, const uint32_t *) { return true; }
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
static Husarz::Metrics metrics{nullptr, nullptr, nullptr, native_advance, native_height};
static Husarz::Glyphs glyphs{nullptr, nullptr, nullptr, native_alpha, native_cached};
static std::vector<uint8_t> file(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
// The production rewrite with a test lookup equivalent to the browser's
// (programy/husarz/webfont_runtime.inc): family -> scoped name of `scope`.
struct WebFace { std::string family; };
static uint64_t current_scope;
static std::vector<WebFace> current_faces;
static uint32_t resolver(const char *list) {
  char out[1024], name[96];
  bool changed = Husarz::rewrite_font_families(list, out, sizeof(out), [&](const char *f, size_t n) -> const char * {
    for (auto &w : current_faces)
      if (Husarz::font_ci(f, n, w.family.c_str()))
        return BSF::scoped_family(current_scope, w.family.c_str(), name, sizeof(name)) ? name : nullptr;
    return nullptr;
  });
  return BSF::resolve_family(changed ? out : list);
}
static std::unique_ptr<Husarz::Document> load(const std::string &html, int width = 600) {
  auto d = std::make_unique<Husarz::Document>();
  Husarz::parse_html(*d, html.data(), html.size());
  Husarz::finish_work(Husarz::initialize_controls_work(*d));
  Husarz::collect_styles(*d);
  Husarz::reflow(*d, width, metrics);
  return d;
}
static int text_width(const Husarz::Document &d, const std::string &needle) {
  for (unsigned i = 0; i < d.fragment_count; ++i)
    if (std::string(d.str(d.fragments[i].text), d.fragments[i].length) == needle) return d.fragments[i].width;
  return -1;
}
static uint32_t register_web(uint64_t scope, const char *family, const std::string &path, BSF::ScopedDescriptor desc = {}) {
  auto bytes = file(path);
  CHECK(!bytes.empty());
  BSF::DecodedWebFont out;
  CHECK(BSF::decode_web_font(bytes.data(), bytes.size(), out) == BSF::WebFontStatus::Ok);
  uint32_t id = BSF::register_scoped_face(scope, family, desc, out.bytes, out.size);
  if (!id) delete[] out.bytes;
  return id;
}
static void helpers() {
  // src: order kept, unsupported formats recognised, local() separate.
  const char *src = "local(\"Noto Sans Web\"), url(a.eot) format('embedded-opentype'), url( \"b.svg#x\" ) format(svg),"
                    " url(fonts/c.woff2) format(\"woff2\"), url('d.woff') format(woff), url(e.ttf) format(truetype),"
                    " url(f.otf) format(opentype-variations) tech(variations), url(g.ttf)";
  Husarz::FontSource s[Husarz::MaxFontSources];
  unsigned n = Husarz::parse_font_sources(src, strlen(src), s);
  CHECK(n == 8);
  CHECK(s[0].local && !strcmp(s[0].value, "Noto Sans Web"));
  CHECK(s[1].format == Husarz::FontFormat::Unsupported && !strcmp(s[1].value, "a.eot"));
  CHECK(s[2].format == Husarz::FontFormat::Unsupported && !strcmp(s[2].value, "b.svg#x"));
  CHECK(s[3].format == Husarz::FontFormat::Woff2 && !strcmp(s[3].value, "fonts/c.woff2"));
  CHECK(s[4].format == Husarz::FontFormat::Woff && s[5].format == Husarz::FontFormat::TrueType);
  CHECK(s[6].format == Husarz::FontFormat::OpenType && s[7].format == Husarz::FontFormat::Any);
  for (const char *bad : {"url(unterminated", "url()", "url(\"\")", "nonsense", ""})
    CHECK(Husarz::parse_font_sources(bad, strlen(bad), s) == 0);
  // unicode-range
  uint32_t r[Husarz::MaxUnicodeRanges][2];
  const char *ur = "U+0000-00FF, U+0131, u+4??, U+1F600-1F64F";
  CHECK(Husarz::parse_unicode_ranges(ur, strlen(ur), r, Husarz::MaxUnicodeRanges) == 4);
  CHECK(r[0][0] == 0 && r[0][1] == 0xff && r[1][0] == 0x131 && r[1][1] == 0x131);
  CHECK(r[2][0] == 0x400 && r[2][1] == 0x4ff && r[3][1] == 0x1f64f);
  for (const char *x : {"U+00FF-0000", "U+110000", "0041", "U+4?1", "U+00-", "U+0041 junk"})
    CHECK(Husarz::parse_unicode_ranges(x, strlen(x), r, Husarz::MaxUnicodeRanges) == 0);
  // descriptors
  uint16_t lo, hi;
  Husarz::parse_font_weight("bold", 4, lo, hi); CHECK(lo == 700 && hi == 700);
  Husarz::parse_font_weight("100 900", 7, lo, hi); CHECK(lo == 100 && hi == 900);
  Husarz::parse_font_weight("950 2000", 8, lo, hi); CHECK(lo == 950 && hi == 1000);
  CHECK(Husarz::parse_font_italic("italic", 6) && Husarz::parse_font_italic("oblique 10deg", 13) && !Husarz::parse_font_italic("normal", 6));
  CHECK(Husarz::parse_font_stretch("condensed", 9) == 75 && Husarz::parse_font_stretch("87.5%", 5) == 87 && Husarz::parse_font_stretch("300%", 4) == 100);
  CHECK(Husarz::parse_font_display("optional", 8) == Husarz::FontDisplay::Optional && Husarz::parse_font_display("x", 1) == Husarz::FontDisplay::Auto);
  // MIME
  for (const char *m : {"font/woff2", "font/ttf; charset=binary", "application/font-woff", "application/x-font-ttf",
                        "application/vnd.ms-opentype", "application/octet-stream", ""})
    CHECK(Husarz::font_mime_allowed(m));
  for (const char *m : {"text/html", "text/css", "application/javascript", "image/png", "text/plain"})
    CHECK(!Husarz::font_mime_allowed(m));
  // CORS (response side)
  CHECK(Husarz::font_cors_allowed("https://a.test/p.html", "https://a.test/f.woff2", ""));
  CHECK(!Husarz::font_cors_allowed("https://a.test/p.html", "https://b.test/f.woff2", ""));
  CHECK(!Husarz::font_cors_allowed("https://a.test/p.html", "http://a.test/f.woff2", ""));   // scheme
  CHECK(!Husarz::font_cors_allowed("https://a.test/p.html", "https://a.test:8443/f.woff2", "")); // port
  CHECK(Husarz::font_cors_allowed("https://a.test/p.html", "https://b.test/f.woff2", " * "));
  CHECK(Husarz::font_cors_allowed("https://a.test/p.html", "https://b.test/f.woff2", "https://a.test"));
  CHECK(!Husarz::font_cors_allowed("https://a.test/p.html", "https://b.test/f.woff2", "https://c.test"));
  CHECK(!Husarz::font_cors_allowed("https://a.test/p.html", "https://b.test/f.woff2", "null"));
  CHECK(!Husarz::font_cors_allowed("https://a.test/p.html", "javascript:alert(1)", "*"));
  // Rewriting never passes a scoped name written by the page.
  char out[256];
  bool changed = Husarz::rewrite_font_families("\"\x01" "0123456789abcdef:x\", serif", out, sizeof(out),
                                               [](const char *, size_t) -> const char * { return nullptr; });
  CHECK(changed && !strchr(out, 1) && strstr(out, "serif"));
  CHECK(!Husarz::rewrite_font_families("Foo, serif", out, sizeof(out), [](const char *, size_t) -> const char * { return nullptr; }));
}
int main() {
  g_test_bws_hook = hook;
  saved.checksum = BSF::checksum(saved);
  for (const char *name : {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf"}) {
    std::ifstream f(std::string("czcionki/ttf/") + name, std::ios::binary);
    files[std::string("/czcionki/ttf/") + name] = {std::istreambuf_iterator<char>(f), {}};
    directory += std::string("[PLIK] ") + name + "\n";
  }
  CHECK(BSF::enumerate() == 3 && BSF::ready());
  Husarz::font_family_resolver = resolver;
  helpers();
  const std::string web = "testy/fixtures/bsf22/web/", fx = "testy/fixtures/bsf22/fonts/";
  // ---- @font-face descriptors parsed by the production CSS parser ------
  const std::string html =
      "<html><head><style>"
      "@font-face{font-family:\"Naskh Web\";src:url(a.eot) format('embedded-opentype'),url(naskh.woff2) format('woff2');"
      "font-weight:400;font-style:normal;unicode-range:U+0600-06FF,U+FE70-FEFF;font-display:swap}"
      "@font-face{font-family:'Web Sans';src:url(sans.woff2) format(woff2);font-weight:400}"
      "@font-face{font-family:'Web Sans';src:url(sansb.otf) format(opentype);font-weight:700}"
      "p{font-family:\"Naskh Web\",sans-serif;font-size:24px} h1{font-family:'Web Sans',serif;font-size:24px}"
      "</style></head><body><p>مرحبا بالعالم abc</p><h1>Zażółć gęślą</h1><h1><b>Bold</b></h1></body></html>";
  current_faces.clear();
  auto before = load(html);
  CHECK(before->font_count == 3);
  {
    const auto &f = before->fonts[0];
    CHECK(!strcmp(before->str(f.family), "\"Naskh Web\""));
    CHECK(f.unicode_range && strstr(before->str(f.unicode_range), "U+0600-06FF"));
    CHECK(f.display && !strcmp(before->str(f.display), "swap"));
    Husarz::FontSource s[Husarz::MaxFontSources];
    const char *src = before->str(f.sources);
    CHECK(Husarz::parse_font_sources(src, strlen(src), s) == 2);
    CHECK(s[0].format == Husarz::FontFormat::Unsupported && s[1].format == Husarz::FontFormat::Woff2);
  }
  int arabic_fallback = text_width(*before, "مرحبا"), latin_fallback = text_width(*before, "Zażółć");
  CHECK(arabic_fallback > 0 && latin_fallback > 0);
  // ---- Web fonts arrive: decode + register in scope A -----------------
  const uint64_t A = 0x1111222233334444ull, B = 0x5555666677778888ull;
  BSF::ScopedDescriptor arabic;
  arabic.range_count = 2;
  arabic.ranges[0][0] = 0x600; arabic.ranges[0][1] = 0x6ff;
  arabic.ranges[1][0] = 0xfe70; arabic.ranges[1][1] = 0xfeff;
  uint32_t naskh = register_web(A, "Naskh Web", web + "NotoNaskhArabic.woff2", arabic);
  uint32_t sans = register_web(A, "Web Sans", web + "NotoSans-CFF.woff2");
  BSF::ScopedDescriptor bold;
  bold.weight_min = bold.weight_max = 700;
  uint32_t sans_bold = register_web(A, "Web Sans", fx + "NotoSans-Bold.otf", bold);
  CHECK(naskh && sans && sans_bold && naskh != sans && sans != sans_bold);
  CHECK(BSF::scoped_faces(A) == 3);
  // Never part of the system catalog.
  CHECK(BSF::enumerate() == 3);
  for (unsigned i = 0; i < BSF::enumerate(); ++i)
    CHECK(BSF::info(i)->id != naskh && BSF::info(i)->id != sans && BSF::info(i)->directory != 0xff);
  current_scope = A;
  current_faces = {{"Naskh Web"}, {"Web Sans"}};
  before->styles_dirty = true;
  Husarz::reflow(*before, 600, metrics); // restyle + reflow (what the browser does)
  int arabic_web = text_width(*before, "مرحبا"), latin_web = text_width(*before, "Zażółć");
  printf("  fallback widths: arabic %d latin %d; web font widths: arabic %d latin %d\n",
         arabic_fallback, latin_fallback, arabic_web, latin_web);
  CHECK(arabic_web > 0 && arabic_web != arabic_fallback);
  CHECK(latin_web > 0 && latin_web != latin_fallback);
  // The measured width equals the shaped layout of the web face itself.
  char name[96];
  CHECK(BSF::scoped_family(A, "Naskh Web", name, sizeof(name)));
  {
    BSF::TextLayout L;
    BSF::LayoutRequest r;
    r.px = 24; r.family = name;
    const char *word = "مرحبا";
    CHECK(BSF::build_layout(L, word, strlen(word), r));
    int expect = int((L.width() + BSF::Fixed / 2) / BSF::Fixed);
    CHECK(std::abs(expect - arabic_web) <= 1);
    // Every glyph of the shaped word comes from the web face.
    for (size_t g = 0; g < L.glyph_count(); ++g) CHECK(L.glyphs()[g].face == naskh);
  }
  // unicode-range composite: Latin in the Arabic family falls back.
  CHECK(BSF::resolved_face(0x645, false, name) == naskh);
  uint32_t latin_face = BSF::resolved_face('a', false, name);
  CHECK(latin_face && latin_face != naskh);
  // Weight selection among faces of one family.
  CHECK(BSF::scoped_family(A, "Web Sans", name, sizeof(name)));
  CHECK(BSF::resolved_face('B', true, name) == sans_bold);
  CHECK(BSF::resolved_face('B', false, name) == sans);
  {
    std::vector<uint32_t> px(600 * 200);
    Husarz::Surface s{px.data(), 600, 200};
    Husarz::paint(*before, s, 0, metrics, glyphs);
    size_t ink = 0;
    for (auto p : px) ink += (p & 0xffffff) != 0xffffff;
    CHECK(ink > 0);
  }
  // ---- Isolation: document B (scope B) cannot use scope A's fonts -----
  current_scope = B;
  current_faces = {{"Naskh Web"}}; // same family name, nothing registered in B
  auto other = load(html);
  CHECK(text_width(*other, "مرحبا") == arabic_fallback);
  // Page CSS naming scope A's internal family directly is stripped.
  char forged[96];
  CHECK(BSF::scoped_family(A, "Naskh Web", forged, sizeof(forged)));
  std::string evil = "<html><body><p style=\"font-family:'" + std::string(forged) + "',sans-serif;font-size:24px\">مرحبا</p></body></html>";
  current_faces.clear();
  auto forged_doc = load(evil);
  CHECK(text_width(*forged_doc, "مرحبا") == arabic_fallback);
  // ---- Invalid web font data never registers --------------------------
  {
    auto bytes = file(web + "NotoSans-Latin.woff2");
    bytes[60] ^= 0xff;
    bytes[200] ^= 0x55;
    BSF::DecodedWebFont out;
    if (BSF::decode_web_font(bytes.data(), bytes.size(), out) == BSF::WebFontStatus::Ok) delete[] out.bytes;
    uint8_t junk[64]{'w', 'O', 'F', '2'};
    CHECK(BSF::decode_web_font(junk, sizeof(junk), out) != BSF::WebFontStatus::Ok);
    auto *raw = new uint8_t[100]{};
    CHECK(!BSF::register_scoped_face(A, "Junk", BSF::ScopedDescriptor{}, raw, 100));
    delete[] raw;
  }
  // ---- Release (navigation away / tab closed) ------------------------
  BSF::release_scope(A);
  CHECK(BSF::scoped_faces(A) == 0);
  CHECK(!BSF::family_name(naskh));
  CHECK(BSF::scoped_family(A, "Naskh Web", name, sizeof(name)));
  CHECK(BSF::resolved_face(0x645, false, name) != naskh);
  current_scope = A;
  current_faces = {{"Naskh Web"}};
  before->styles_dirty = true;
  Husarz::reflow(*before, 600, metrics);
  CHECK(text_width(*before, "مرحبا") == arabic_fallback);
  // Identifiers are never reused after release.
  uint32_t again = register_web(A, "Naskh Web", web + "NotoNaskhArabic.woff2", arabic);
  CHECK(again && again != naskh && again != sans && again != sans_bold);
  BSF::release_scope(A);
  // ---- Other formats through the same pipeline -----------------------
  for (const char *f : {"NotoSans-Latin.woff", "NotoSans-CFF.woff", "NotoSansHebrew-VF.woff2", "NotoSans-Latin.ttf"}) {
    uint32_t id = register_web(B, "Fmt", web + f);
    CHECK(id);
    BSF::release_scope(B);
  }
  // ---- Bounded registry ------------------------------------------------
  unsigned registered = 0;
  for (unsigned i = 0; i < BSF::MaxScopedFaces + 4; ++i)
    registered += register_web(B, "Many", web + "NotoSans-Latin.woff2") != 0;
  CHECK(registered == BSF::MaxScopedFaces);
  BSF::release_scope(B);
  CHECK(BSF::scoped_faces(0) == 0);
  Husarz::font_family_resolver = nullptr;
  BSF::shutdown();
  printf("BSF 2.2 Husarz web fonts: %u checks PASS\n", checks);
}
