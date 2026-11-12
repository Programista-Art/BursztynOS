// BSF 2.1 Husarz integration (native BSF metrics, real fonts, production
// Husarz parser/CSS/layout/paint/forms): CSS direction + unicode-bidi, HTML
// dir/bdi/bdo, paragraph-level UAX #9 across inline elements, L2 per line,
// direction-aware alignment, shaped Arabic across DOM nodes, link hit
// testing, RTL forms with logical values, DOM text integrity, adversarial
// and long pages under the cooperative scheduler, LTR pages unchanged.
// Pages: testy/husarz_bidi/*.html. ASan/UBSan.
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
static std::string page(const char *name) {
  std::ifstream in(std::string("testy/husarz_bidi/") + name, std::ios::binary);
  std::string s{std::istreambuf_iterator<char>(in), {}}; CHECK(!s.empty()); return s;
}
static std::unique_ptr<Husarz::Document> load(const std::string &html, int width = 600) {
  auto d = std::make_unique<Husarz::Document>();
  Husarz::parse_html(*d, html.data(), html.size()); Husarz::finish_work(Husarz::initialize_controls_work(*d));
  Husarz::collect_styles(*d); Husarz::reflow(*d, width, metrics);
  return d;
}
struct F { std::string text; int x, y, w; uint8_t level, para, flags; uint16_t node; };
static std::vector<F> frags(const Husarz::Document &d) {
  std::vector<F> v;
  for (unsigned i = 0; i < d.fragment_count; ++i) { const auto &f = d.fragments[i];
    v.push_back({std::string(d.str(f.text), f.length), f.x, f.y, f.width, f.level, f.paragraph_level, f.flags, f.node}); }
  return v;
}
static const F &get(const std::vector<F> &v, const std::string &t, unsigned nth = 0) {
  for (auto &f : v) if (f.text == t && !nth--) return f;
  fprintf(stderr, "missing fragment '%s'\n", t.c_str()); for (auto &f : v) fprintf(stderr, "  '%s' x=%d y=%d w=%d l=%d\n", f.text.c_str(), f.x, f.y, f.w, f.level);
  abort();
}
// Visual order of the fragments on one line (y), left to right.
static std::vector<std::string> line(const std::vector<F> &v, int y) {
  std::vector<F> l; for (auto &f : v) if (f.y == y && !f.text.empty()) l.push_back(f);
  std::sort(l.begin(), l.end(), [](const F &a, const F &b) { return a.x < b.x; });
  std::vector<std::string> out; for (auto &f : l) out.push_back(f.text); return out;
}
static void no_overlap(const std::vector<F> &v) {
  for (size_t i = 0; i < v.size(); ++i) for (size_t j = i + 1; j < v.size(); ++j)
    if (v[i].y == v[j].y && v[i].w && v[j].w) CHECK(v[i].x + v[i].w <= v[j].x || v[j].x + v[j].w <= v[i].x);
}
static void paint_ok(Husarz::Document &d, int w = 600, int h = 400) {
  std::vector<uint32_t> px(size_t(w) * h); Husarz::Surface s{px.data(), w, h};
  Husarz::paint(d, s, 0, metrics, glyphs); CHECK(!s.truncated);
  size_t ink = 0; for (auto p : px) ink += (p & 0xffffff) != 0xffffff; CHECK(ink > 0);
}
int main() {
  g_test_bws_hook = hook; saved.checksum = BSF::checksum(saved);
  for (const char *name : {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf"}) {
    std::ifstream f(std::string("czcionki/ttf/") + name, std::ios::binary);
    files[std::string("/czcionki/ttf/") + name] = {std::istreambuf_iterator<char>(f), {}};
    directory += std::string("[PLIK] ") + name + "\n";
  }
  CHECK(BSF::enumerate() == 3 && BSF::ready());
  Husarz::font_family_resolver = BSF::resolve_family;
  // 01 Arabic paragraph: one RTL paragraph, shaped, right aligned.
  { auto d = load(page("01-arabic-paragraph.html")); auto v = frags(*d);
    const F &a = get(v, "مرحبا"), &b = get(v, "بالعالم");
    CHECK(d->has_bidi && a.level == 1 && b.level == 1 && a.para == 1);
    CHECK((a.flags & Husarz::FragmentComplex) && (b.flags & Husarz::FragmentComplex));
    CHECK(a.x > b.x); // first logical word is rightmost
    CHECK(a.x + a.w >= 600 - 16 - 2); // right aligned in the body (8px margins default 0 here)
    // Shaped: the fragment is measured from contextual forms, not isolated ones.
    const auto *L = BSF::layout_text("مرحبا", strlen("مرحبا"), 20, false, nullptr, false, 0, BSF::BaseDirection::RTL);
    CHECK(L && a.w == int((L->width() + BSF::Fixed / 2) / BSF::Fixed));
    int isolated = 0; for (uint32_t cp : {0x0645u, 0x0631u, 0x062Du, 0x0628u, 0x0627u}) isolated += BSF::measure_cp(cp, 20);
    CHECK(a.w != isolated);
    auto before = Husarz::shaped_layout_misses + Husarz::shaped_layout_hits; paint_ok(*d);
    CHECK(Husarz::shaped_layout_misses + Husarz::shaped_layout_hits > before); // painted through BSF layouts
    // DOM text is untouched (logical order, byte exact).
    bool found = false; for (unsigned i = 0; i < d->node_count; ++i) if (d->nodes[i].is_text && std::string(d->str(d->nodes[i].text), d->nodes[i].text_length) == "مرحبا بالعالم") found = true;
    CHECK(found); }
  // 02 Hebrew with punctuation: '!' and ',' stay with their words in RTL.
  { auto d = load(page("02-hebrew-punctuation.html")); auto v = frags(*d);
    auto l = line(v, v[0].y); CHECK(l.size() == 2 && l[0] == "עולם!" && l[1] == "שלום,");
    const F &w = get(v, "עולם!"); CHECK(w.level == 1 && (w.flags & Husarz::FragmentComplex)); }
  // 03 RTL paragraph with an English phrase and a number.
  { auto d = load(page("03-rtl-english-numbers.html")); auto v = frags(*d);
    auto l = line(v, v[0].y);
    std::vector<std::string> expect = {"עולם", "Hello", "world", "123", "שלום"};
    CHECK(l == expect);
    CHECK(get(v, "Hello").level == 2 && get(v, "123").level == 2 && get(v, "שלום").level == 1);
    no_overlap(v); paint_ok(*d); }
  // 04 Nested dir: isolates in both directions.
  { auto d = load(page("04-nested-dir.html")); auto v = frags(*d);
    int y0 = get(v, "abc").y, y1 = get(v, "אבג", 0).y;
    std::vector<std::string> e0 = {"abc", "דהו", "אבג", "def"}; CHECK(line(v, y0) == e0);
    int yr = get(v, "abc", 1).y; CHECK(yr != y0);
    std::vector<std::string> e1 = {"דהו", "abc", "def", "אבג"}; CHECK(line(v, yr) == e1);
    (void)y1; no_overlap(v); }
  // 05 dir=auto: first strong character, skipping dir'd descendants.
  { auto d = load(page("05-dir-auto.html")); auto v = frags(*d);
    unsigned ps[3], k = 0; for (unsigned i = 0; i < d->node_count && k < 3; ++i) if (d->tag(i, "p")) ps[k++] = i;
    CHECK(k == 3 && d->styles[ps[0]].direction == 1 && d->styles[ps[1]].direction == 0 && d->styles[ps[2]].direction == 1);
    CHECK(get(v, "world").x < get(v, "שלום").x); // RTL paragraph: world on the left
    CHECK(get(v, "Hello").x < get(v, "שלום", 1).x); }
  // 06 CSS direction inheritance and start/end/left alignment.
  { auto d = load(page("06-css-direction.html")); auto v = frags(*d);
    const F &a = get(v, "שלום", 0), &b = get(v, "שלום", 1), &c = get(v, "abc");
    CHECK(a.x + a.w >= 590 && b.x <= 2 && c.x <= 2); }
  // 07 unicode-bidi embed / isolate / bidi-override / bdo / bdi.
  { auto d = load(page("07-unicode-bidi.html")); auto v = frags(*d);
    const F &o = get(v, "xyz", 0); CHECK(o.level == 1 && (o.flags & Husarz::FragmentOverride));
    // isolate-override = RLI RLO: X5 raises the RLO content to level 3.
    const F &bdo = get(v, "xyz", 1); CHECK(bdo.level == 3 && (bdo.flags & Husarz::FragmentOverride));
    // embed: "שלום world" inside an RTL embedding of an LTR paragraph
    const F &w = get(v, "world"); CHECK(w.level == 2);
    CHECK(get(v, "world").x < get(v, "שלום", 0).x);
    // isolate: 123 is isolated from the following 456 (no number run merge)
    CHECK(get(v, "123", 0).level == 2 || get(v, "123", 0).level == 1);
    CHECK(get(v, "456").level == 0);
    // bdi: Hebrew isolated, the number keeps LTR order after it
    CHECK(get(v, "שלום", 1).x < get(v, "123", 1).x);
    no_overlap(v); paint_ok(*d); }
  // 08 plaintext: each paragraph takes its own direction and alignment.
  { auto d = load(page("08-plaintext.html")); auto v = frags(*d);
    CHECK(get(v, "שלום").para == 1 && get(v, "Hello").para == 0);
    CHECK(get(v, "שלום").x > get(v, "עולם").x);
    CHECK(get(v, "Hello").x < get(v, "world").x); }
  // 09 Arabic shaping across inline elements: "بي" + "ت" join.
  { auto d = load(page("09-arabic-inline-shaping.html")); auto v = frags(*d);
    const F &a = get(v, "بي"), &b = get(v, "ت");
    unsigned ia = 0, ib = 0; for (unsigned i = 0; i < d->fragment_count; ++i) { std::string t(d->str(d->fragments[i].text), d->fragments[i].length); if (t == "بي") ia = i; if (t == "ت") ib = i; }
    CHECK(d->fragments[ia].context_after == 0x062A && d->fragments[ib].context_before == 0x064A);
    // Width equals the joined word's glyphs, not the isolated pieces.
    auto *joined = Husarz::shaped_layout("بي", strlen("بي"), d->styles[a.node], 1, 0, 0x062A);
    CHECK(joined && a.w == int((joined->width() + BSF::Fixed / 2) / BSF::Fixed));
    const BSF::TextLayout *word = BSF::layout_text("بيت", strlen("بيت"), 24, true, nullptr, false, 0, BSF::BaseDirection::RTL);
    uint16_t yeh_in_word = 0, yeh_piece = 0;
    for (size_t i = 0; i < word->glyph_count(); ++i) if (word->glyphs()[i].cluster == 2) yeh_in_word = word->glyphs()[i].glyph;
    for (size_t i = 0; i < joined->glyph_count(); ++i) if (joined->glyphs()[i].cluster == 2) yeh_piece = joined->glyphs()[i].glyph;
    CHECK(yeh_in_word && yeh_in_word == yeh_piece);
    CHECK(a.x > b.x && a.x == b.x + b.w); // adjacent, RTL order
    CHECK(get(v, "كت").x == get(v, "اب").x + get(v, "اب").w);
    paint_ok(*d); }
  // 10 Links in RTL text: clicking the visible text activates the right <a>.
  { auto d = load(page("10-arabic-links.html")); auto v = frags(*d);
    auto href = [&](const F &f) { uint16_t n = Husarz::hit_test(*d, f.x + f.w / 2, f.y + 2); return n ? std::string(d->attr(n, "href")) : std::string(); };
    CHECK(href(get(v, "رابط", 0)) == "/ar" && href(get(v, "عربي")) == "/ar");
    CHECK(href(get(v, "Link")) == "/mix" && href(get(v, "123")) == "/mix" && href(get(v, "رابط", 1)) == "/mix");
    CHECK(href(get(v, "קישור")) == "/he" && href(get(v, "نص")) == "");
    CHECK(get(v, "Link").x < get(v, "123").x); // LTR run inside the RTL link
    unsigned wrapped_lines = 0; int last_y = -1;
    for (auto &f : v) if (href(f) == "/wrap") { if (f.y != last_y) ++wrapped_lines; last_y = f.y; }
    CHECK(wrapped_lines >= 2);
    no_overlap(v); paint_ok(*d); }
  // 11 Forms: logical values, RTL geometry, visual keys, password isolation.
  { auto d = load(page("11-forms.html")); paint_ok(*d);
    Husarz::FormControl *a = nullptr, *b = nullptr, *pw = nullptr, *t = nullptr;
    for (unsigned i = 0; i < d->control_count; ++i) { auto &c = d->controls[i]; const char *n = d->attr(c.node, "name");
      if (!strcmp(n, "a")) a = &c; if (!strcmp(n, "b")) b = &c; if (!strcmp(n, "c")) pw = &c; if (!strcmp(n, "t")) t = &c; }
    if(!(a&&b&&pw&&t)){for(unsigned i=0;i<d->control_count;++i)fprintf(stderr,"ctrl %u node=%u tag=%s name=%s\n",i,d->controls[i].node,d->str(d->nodes[d->controls[i].node].tag),d->attr(d->controls[i].node,"name"));}
    CHECK(a && b && pw && t);
    // The parser keeps <textarea> content on the element itself (existing
    // behaviour); give the control its value through the editing API.
    d->focused_control = t->node; CHECK(Husarz::form_replace(*d, *t, "שורה Hello\nשנייה", strlen("שורה Hello\nשנייה")));
    CHECK(Husarz::control_layout(*d, *a, metrics) && Husarz::control_layout(*d, *b, metrics) && Husarz::control_layout(*d, *t, metrics));
    CHECK(!Husarz::control_layout(*d, *pw, metrics)); // password never laid out
    { // the search field's Arabic is shaped with joining forms
      const auto *Lb = Husarz::control_layout(*d, *b, metrics);
      const auto &bs = d->styles[b->node];
      const auto *W = BSF::layout_text("مرحبا 123", strlen("مرحبا 123"), bs.font, bs.bold, BSF::family_name(bs.native_family), false, 0, BSF::BaseDirection::RTL);
      CHECK(Lb && W && Lb->glyph_count() == W->glyph_count());
      for (size_t i = 0; i < Lb->glyph_count(); ++i) CHECK(Lb->glyphs()[i].glyph == W->glyphs()[i].glyph);
    }
    d->focused_control = a->node; Husarz::form_move(*d, *a, 0, false);
    auto g0 = Husarz::control_text_geometry(*d, *a, metrics);
    CHECK(g0.caret_x >= d->boxes[a->node].content_width - 8); // logical start of RTL value at the right
    // Left in RTL = forward; walk to the visual left edge and back.
    std::set<int> xs; xs.insert(g0.caret_x); int x = g0.caret_x;
    for (int i = 0; i < 40; ++i) { CHECK(Husarz::control_horizontal_key(*d, 0x4b, false, metrics)); auto g = Husarz::control_text_geometry(*d, *a, metrics); if (g.caret_x == x) break; CHECK(g.caret_x < x); x = g.caret_x; }
    CHECK(a->cursor == a->length);
    for (int i = 0; i < 40; ++i) CHECK(Husarz::control_horizontal_key(*d, 0x4d, false, metrics));
    CHECK(a->cursor == 0);
    // Shift selection, typing and Backspace keep a logical UTF-8 value.
    Husarz::control_horizontal_key(*d, 0x4b, true, metrics); Husarz::control_horizontal_key(*d, 0x4b, true, metrics);
    CHECK(Husarz::selection_start(*a) == 0 && Husarz::selection_end(*a) > 0);
    CHECK(Husarz::form_replace(*d, *a, "ש", 2)); CHECK(BSF::cluster_boundary(a->value, a->length, a->cursor));
    CHECK(Husarz::form_key(*d, 8)); 
    // Hit testing: every caret x maps back to that visual position.
    for (unsigned p = 0; p <= a->length; p = p == a->length ? p + 1 : unsigned(BSF::next_cluster(a->value, p, a->length))) {
      Husarz::form_move(*d, *a, p, false); auto g = Husarz::control_text_geometry(*d, *a, metrics);
      unsigned h = Husarz::control_text_hit(*d, *a, metrics, g.caret_x, g.caret_y);
      Husarz::form_move(*d, *a, h, false); CHECK(Husarz::control_text_geometry(*d, *a, metrics).caret_x == g.caret_x); }
    // Submission is the logical value.
    char q[512]; uint16_t form = 0; for (unsigned i = 0; i < d->node_count; ++i) if (d->tag(i, "form")) form = i;
    CHECK(Husarz::form_query(*d, form, 0, q, sizeof q));
    CHECK(strstr(q, "b=%D9%85%D8%B1%D8%AD%D8%A8%D8%A7+123") || strstr(q, "b=%D9%85%D8%B1%D8%AD%D8%A8%D8%A7%20123"));
    CHECK(strstr(q, "t=%D7%A9%D7%95%D7%A8%D7%94"));
    // Textarea: RTL lines, visual walk terminates.
    d->focused_control = t->node; Husarz::form_move(*d, *t, 0, false);
    unsigned steps = 0; for (; steps < 200; ++steps) { unsigned before = t->cursor; Husarz::control_horizontal_key(*d, 0x4b, false, metrics); if (t->cursor == before) break; }
    CHECK(steps < 200);
    paint_ok(*d); }
  // 12 Polish/English page: no bidi work, no complex fragments, LTR order.
  { auto d = load(page("12-polish-english.html")); auto v = frags(*d);
    CHECK(!d->has_bidi && d->bidi_paragraphs == 0);
    for (auto &f : v) CHECK(f.level == 0 && !(f.flags & Husarz::FragmentOverride));
    CHECK(get(v, "quick").x < get(v, "brown").x && get(v, "Zażółć").x < get(v, "gęślą").x);
    paint_ok(*d); }
  // Long bidi paragraph under the cooperative scheduler, bounded slices.
  { std::string html = "<html><body dir=\"rtl\" style=\"margin:0\"><p>";
    for (int i = 0; i < 1500; ++i) html += i % 3 ? "שלום مرحبا " : "Hello 123 ";
    html += "</p></body></html>";
    auto d = std::make_unique<Husarz::Document>(); Husarz::parse_html(*d, html.data(), html.size()); Husarz::collect_styles(*d);
    Husarz::WorkDriver driver; auto job = Husarz::reflow_work(*d, 600, metrics); unsigned slices = 0;
    while (!job.step(driver)) { ++slices; CHECK(slices < 200000); }
    CHECK(driver.maximum <= Husarz::MaxSliceWork);
    CHECK(!driver.failed && slices > 10);
    auto v = frags(*d); CHECK(v.size() > 3000); no_overlap(v);
    for (auto &f : v) CHECK(f.x >= -1 && f.x + f.w <= 601); }
  // Adversarial: malformed UTF-8, deep nesting beyond UAX #9 depth, run and
  // paragraph budgets, mixed controls. Must stay bounded and well-formed.
  { std::string html = "<p dir=rtl>\xff\xfe\xd7 \xd8\xa7\xe2\x80\xae abc \xe2\x81\xa7 x</p>";
    for (int i = 0; i < 200; ++i) html += "<bdo dir=rtl><span dir=ltr>";
    html += "deep שלום";
    std::string many; for (int i = 0; i < 20000; ++i) many += i & 1 ? "a" : "א";
    html += "<p>" + many + "</p><pre style=\"unicode-bidi:plaintext\">" + std::string(40000, 'x') + "\nשלום</pre>";
    auto d = load(html); auto v = frags(*d); CHECK(!v.empty());
    for (auto &f : v) CHECK(BSF::cluster_boundary(f.text.data(), f.text.size(), f.text.size()));
    CHECK(d->bidi_run_count <= Husarz::MaxBidiRuns);
    paint_ok(*d, 600, 600); }
  printf("Husarz BSF 2.1: %u checks PASS (CSS direction/unicode-bidi, HTML dir/bdi/bdo, paragraph bidi + L2 lines, shaped Arabic across nodes, links, RTL forms, long/adversarial pages); shaped widths hit=%llu miss=%llu layouts hit=%llu miss=%llu\n",
         checks, (unsigned long long)Husarz::shaped_width_hits, (unsigned long long)Husarz::shaped_width_misses,
         (unsigned long long)Husarz::shaped_layout_hits, (unsigned long long)Husarz::shaped_layout_misses);
  Husarz::font_family_resolver = nullptr;
  return 0;
}
