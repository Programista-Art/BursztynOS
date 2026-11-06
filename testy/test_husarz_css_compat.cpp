#include "../programy/husarz/pipeline.h"
#include "../programy/husarz/url.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
using namespace Husarz;
static unsigned checks;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x)) {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                             \
            std::abort();                                                                                    \
        }                                                                                                    \
    } while (0)
static uint16_t node(const Document &d, const char *name) {
    for (unsigned i = 1; i < d.node_count; ++i)
        if (equal(d.attr(i, "id"), name))
            return uint16_t(i);
    std::abort();
}
static std::unique_ptr<Document> load(const std::string &html, const char *css, int width = 900) {
    auto d = std::make_unique<Document>();
    parse_html(*d, html.data(), html.size());
    collect_styles(*d);
    parse_css(*d, css, std::strlen(css), 10000, d->store("https://site.test/assets/css/main.css", 36));
    reflow(*d, width, {});
    return d;
}
static void urls() {
    char out[2048];
    struct Case {
        const char *reference, *expected;
    };
    Case cases[] = {{"/css/main.css?v=7&theme=dark", "https://site.test/css/main.css?v=7&theme=dark"},
                    {"../img/bg%20one.png#x", "https://site.test/assets/img/bg%20one.png"},
                    {"./main.css?url=/a/../b", "https://site.test/assets/css/main.css?url=/a/../b"},
                    {"//cdn.test/main.css", "https://cdn.test/main.css"},
                    {"?x=2", "https://site.test/assets/css/main.css?x=2"},
                    {"#frag", "https://site.test/assets/css/main.css?old=1"},
                    {"https://SITE.test:443/a.css", "https://site.test/a.css"},
                    {"http://site.test:80/a.css", "http://site.test/a.css"},
                    {"a b.css", "https://site.test/assets/css/a%20b.css"}};
    for (auto &c : cases) {
        CHECK(resolve_resource_url("https://site.test/assets/css/main.css?old=1", c.reference, out,
                                   sizeof(out)));
        CHECK(equal(out, c.expected));
        Url u;
        CHECK(parse_absolute_url(out, u));
        CHECK(!HttpTarget::target_error(u.target));
    }
    CHECK(!resolve_resource_url("https://site.test/", "javascript:alert(1)", out, sizeof(out)));
    CHECK(!normalize_resource_url("https://bad-.test/a", out, sizeof(out)));
    CHECK(!normalize_resource_url("https://site.test/a\r\nInjected: true", out, sizeof(out)));
    CHECK(HttpTarget::target_error("a.css"));
    CHECK(HttpTarget::target_error("/a#fragment"));
    CHECK(!HttpTarget::target_error("/wp-content/a-b_c.%20css?ver=6.8&theme=dark"));
    CHECK(mime_is("  TeXt/CsS ; charset=utf-8 ", "text/css"));
    CHECK(!mime_is("text/html", "text/css"));
    for (unsigned c = 1; c < 256; ++c) {
        char target[] = {'/', 'a', char(c), 'b', 0};
        bool expected = c > 32 && c < 127 && c != '#' && c != '\\';
        CHECK((!HttpTarget::target_error(target)) == expected);
    }
}
static void styles() {
    auto d = load("<html><body id=page><h1 id=heading>Heading</h1><div id=row><a "
                  "id=button class=btn>Go</a><span id=second>Next</span></div><footer "
                  "id=foot>Footer</footer><div id=cycle>Cycle</div><div id=scope><p "
                  "id=inherit>Inherited</p></div></body></html>",
                  R"CSS(
 :root {--primary:#f59e0b;--ink:#fff;--page:#111827;--a:var(--b);--b:var(--a)}
 body {margin:0;background:var(--page);color:var(--ink)}
 h1 {color:rgb(255 0 0 / 50%)}
 #row {display:flex;justify-content:space-between;align-items:center;width:400px;gap:20px}
 .btn {color:white;background:var(--primary) url('../img/bg.png') center/cover no-repeat; padding:10px 20px 30px 40px;border:1px solid #333;unsupported:foo}
 #second{width:60px} footer {background:#243; color:var(--absent, #abcdef)}
 #cycle {color:var(--a, #123456)} #scope{--ink:#00ff00} #inherit{color:var(--ink)}
 @media screen and (max-width:600px){#row{flex-direction:column}body{background:#090a0b}}
 @unknown { .fake{color:red} }
 @supports (display:flex){#foot{padding:3px}}
 #button:unsupported, #foot {font-weight:700}
 )CSS");
    CHECK(!d->truncated);
    auto page = node(*d, "page"), button = node(*d, "button"), heading = node(*d, "heading"),
         row = node(*d, "row"), second = node(*d, "second"), foot = node(*d, "foot");
    CHECK(d->styles[page].background == 0x111827);
    CHECK(d->styles[button].background == 0xf59e0b);
    CHECK(d->styles[button].color == 0xffffff);
    CHECK(d->styles[heading].color == 0xff0000);
    CHECK(d->styles[heading].color_alpha == 127);
    CHECK(equal(d->str(d->styles[button].background_image), "url('../img/bg.png')"));
    CHECK(d->styles[button].padding[3].value == 40000);
    CHECK(d->styles[button].border[0] == 1);
    CHECK(d->styles[button].border_color == 0x333333);
    CHECK(d->styles[node(*d, "cycle")].color == 0x123456);
    CHECK(d->styles[node(*d, "inherit")].color == 0x00ff00);
    CHECK(d->styles[foot].background == 0x224433);
    CHECK(d->styles[foot].color == 0xabcdef);
    CHECK(d->styles[foot].bold);
    CHECK(d->styles[foot].padding[0].value == 3000);
    auto cloned=std::make_unique<Document>();finish_work(clone_layout_document(*cloned,*d));char variable[MaxResolvedValue+1]{};size_t used=0;VariableResolver resolver{*cloned};CHECK(resolver.expand(button,{"var(--primary)",14},variable,sizeof(variable),used));CHECK(equal(variable,"#f59e0b"));
    CHECK(d->styles[row].flex);
    CHECK(d->boxes[second].x > d->boxes[button].x);
    CHECK(d->boxes[second].x + d->boxes[second].width ==
          d->boxes[row].content_x + d->boxes[row].content_width);
    std::vector<uint32_t> pixels(900 * 600);
    Surface surface{pixels.data(), 900, 600};
    paint(*d, surface, 0, {}, {});
    CHECK((pixels[899] & 0xffffff) == 0x111827);
    auto b = d->boxes[button];
    CHECK((pixels[size_t(b.y + 2) * 900 + b.x + 2] & 0xffffff) == 0xf59e0b);
    auto parsecount = d->parse_count;
    reflow(*d, 500, {});
    CHECK(d->parse_count == parsecount);
    CHECK(d->styles[page].background == 0x090a0b);
    CHECK(d->styles[row].flex_column);
    CHECK(d->boxes[second].y > d->boxes[button].y);
    auto arena = d->arena_used;
    for (unsigned i = 0; i < 30; ++i)
        reflow(*d, i % 2 ? 500 : 900, {});
    CHECK(d->arena_used == arena);
    CHECK(!d->truncated);
}
static void modern_styles() {
    auto d =
        load("<html><body><div id=units>Units</div><div id=grid><div id=ga>A</div><div id=gb>B</div></div><a "
             "id=link>Link</a><div id=decoration>Box</div><p id=is class=foo>Selector</p></body></html>",
             R"CSS(
 body{margin:0} #units{width:calc(200px + 20px);height:min(30px,40px);padding:max(2px,4px);font-size:clamp(16px,2vw,24px)}
 #grid{display:grid;grid-template-columns:repeat(2,1fr);gap:20px;width:400px}#ga,#gb{height:30px;background:#789}
 #link{color:red!important}a{color:blue} #decoration{width:80px;height:40px;background:linear-gradient(#000,#fff);border-top:2px solid red;border-right:3px solid green}
 #decoration::before{content:"";width:4px;height:4px;background:#ff00ff;position:absolute;left:3px;top:3px}
 :is(.foo,.bar){color:#123456}:where(#is){background:#112233}
 )CSS",
             1000);
    CHECK(d->styles[node(*d, "units")].width.value == 220000);
    CHECK(d->styles[node(*d, "units")].height.value == 30000);
    CHECK(d->styles[node(*d, "units")].font == 20);
    CHECK(d->styles[node(*d, "units")].padding[0].value == 4000);
    CHECK(d->styles[node(*d, "link")].color == 0xff0000);
    CHECK(d->styles[node(*d, "is")].color == 0x123456);
    CHECK(d->styles[node(*d, "is")].background == 0x112233);
    CHECK(d->styles[node(*d, "decoration")].has_gradient);
    CHECK(d->styles[node(*d, "decoration")].border[0] == 2);
    CHECK(d->styles[node(*d, "decoration")].border_colors[0] == 0xff0000);
    CHECK(d->styles[node(*d, "decoration")].border_colors[1] == 0x008000);
    auto a = d->boxes[node(*d, "ga")], b = d->boxes[node(*d, "gb")];
    CHECK(a.width == 190);
    CHECK(b.width == 190);
    CHECK(b.x - a.x == 210);
    CHECK(a.y == b.y);
    CHECK(d->generated_count == 1);
    CHECK(d->generated[0].width == 4);
    CHECK(d->generated[0].style.background == 0xff00ff);
    std::vector<uint32_t> pixels(1000 * 600);
    Surface surface{pixels.data(), 1000, 600};
    paint(*d, surface, 0, {}, {});
    auto &g = d->generated[0];
    CHECK((pixels[size_t(g.y + 1) * 1000 + g.x + 1] & 0xffffff) == 0xff00ff);
    Length l;
    const char *bad = "calc(1px / 0)";
    CHECK(!css_length({bad, std::strlen(bad)}, l));
    bad = "calc(100% - 20px)";
    CHECK(!css_length({bad, std::strlen(bad)}, l));
    std::string nested = "1px";
    for (unsigned i = 0; i < 20; ++i)
        nested = "calc(" + nested + ")";
    CHECK(!css_length({nested.data(), nested.size()}, l));
}
static void flex_intrinsic_regression() {
    const char *css = "body{margin:0}#nav{display:flex;width:240px;gap:8px}#menu{position:relative}#drop{"
                      "position:absolute;left:0}a{padding:10px;background:orange}";
    auto a = load("<html><body><div id=nav><div id=menu>Menu<div id=drop>Short</div></div><a "
                  "id=go>Download</a></div></body></html>",
                  css);
    std::string html = "<html><body><div id=nav><div id=menu>Menu<div id=drop>";
    for (unsigned i = 0; i < 100; ++i)
        html += "A very long off-flow submenu ";
    html += "</div></div><a id=go>Download</a></div></body></html>";
    auto b = load(html, css);
    CHECK(a->boxes[node(*a, "go")].width == b->boxes[node(*b, "go")].width);
    CHECK(b->boxes[node(*b, "go")].content_width >= 64);
    CHECK(!b->truncated);
    auto nested = load(
        "<html><body><div id=nav><div id=item><a id=go>Download</a></div>"
        "<div>Other</div></div></body></html>",
        "body{margin:0}#nav{display:flex;width:180px}a{display:inline-block;"
        "padding:10px 24px;background:orange}");
    auto go = node(*nested, "go");
    CHECK(nested->boxes[go].content_width >= 64);
    CHECK(nested->boxes[go].width >= 112);
    CHECK(nested->boxes[go].height < 60);
}
static void bounded_inputs() {
    auto d = std::make_unique<Document>();
    const char *html = "<html><body><a id=x>X</a></body></html>";
    parse_html(*d, html, std::strlen(html));
    collect_styles(*d);
    const char *css = "\xef\xbb\xbf/* } */ @unknown {x{content:'}';} } "
                      "#x{unknown:value;color:red;bad:!bad;background:black}";
    parse_css(*d, css, std::strlen(css));
    reflow(*d, 800, {});
    CHECK(d->styles[node(*d, "x")].color == 0xff0000);
    CHECK(d->styles[node(*d, "x")].background == 0);
    std::string many;
    for (unsigned i = 0; i < MaxRules + 32; ++i)
        many += ".r" + std::to_string(i) + "{color:red}";
    parse_css(*d, many.data(), many.size());
    CHECK(d->limits & LimitRules);
    CHECK(d->rule_count == MaxRules);
    d->reset();
    std::string large = "<body>";
    for (unsigned i = 0; i < MaxNodes + 30; ++i)
        large += "<i>x</i>";
    parse_html(*d, large.data(), large.size());
    CHECK(d->node_count == MaxNodes);
    CHECK(d->limits & LimitNodes);
}
int main() {
    urls();
    styles();
    modern_styles();
    bounded_inputs();
    flex_intrinsic_regression();
    std::printf("Husarz CSS compatibility: %u checks PASS\n", checks);
}
