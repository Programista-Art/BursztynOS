#include "../bursztyn_ui/czcionka.h"
#include "../programy/husarz/paint.h"
#include "../programy/husarz/script.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace Husarz;
static unsigned checks = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x)) {                                                                                          \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                             \
            std::abort();                                                                                    \
        }                                                                                                    \
    } while (0)
static int glyph_width(void *, uint32_t cp) { return BursztynUI::CzcionkaUI::szerokosc_znaku(cp); }
static const uint16_t *glyph_bitmap(void *, uint32_t cp) { return BursztynUI::CzcionkaUI::bitmapa_znaku(cp); }
static int glyph_advance(void *, uint32_t cp, int font, bool bold, bool) {
    return ((glyph_width(nullptr, cp) + 1) * font + 8) / 16 + (bold ? 1 : 0);
}
static int glyph_height(void *, int font) { return font; }
static Metrics metrics{nullptr, glyph_advance, glyph_height};
static Glyphs glyphs{nullptr, glyph_bitmap, glyph_width};
static void load(Document &d, const std::string &html, int width = 320, bool is_html = true) {
    parse_html(d, html.data(), html.size(), is_html);
    collect_styles(d);
    prepare_controls(d);
    reflow(d, width, metrics);
}
static uint16_t find(Document &d, const char *id) { return ScriptDom(d).get_element_by_id(id).id; }
static uint64_t checksum(Document &d, int width = 320, int height = 240) {
    std::vector<uint32_t> pixels(size_t(width) * height, 0);
    Surface surface{pixels.data(), width, height};
    paint(d, surface, 0, metrics, glyphs, 0xffffff);
    CHECK(!surface.truncated);
    uint64_t hash = 1469598103934665603ULL;
    for (auto p : pixels) {
        hash ^= p;
        hash *= 1099511628211ULL;
    }
    return hash;
}
static void invariant(const Document &d) {
    CHECK(d.node_count <= MaxNodes);
    CHECK(d.attr_count <= MaxAttrs);
    CHECK(d.arena_used <= ArenaSize);
    CHECK(d.rule_count <= MaxRules);
    CHECK(d.declaration_count <= MaxDeclarations);
    CHECK(d.fragment_count <= MaxFragments);
    CHECK(d.height >= 0 && d.height <= MaxDimension);
    for (unsigned i = 1; i < d.node_count; ++i) {
        const auto &n = d.nodes[i];
        CHECK(n.parent < i);
        CHECK(n.depth <= MaxDepth);
        CHECK(n.attr_start + n.attr_count <= d.attr_count);
        CHECK(n.text < d.arena_used);
        CHECK(n.text_length <= ArenaSize - n.text);
    }
    for (unsigned i = 0; i < d.fragment_count; ++i) {
        const auto &f = d.fragments[i];
        CHECK(f.node < d.node_count);
        CHECK(f.text + f.length < d.arena_used);
        CHECK(f.width >= 0);
        CHECK(f.height > 0);
    }
}
int main() {
    auto owned = std::make_unique<Document>();
    auto &d = *owned;
    load(d, "<!DOCTYPE html><!-- comment --><HTML><HeAd><TITLE>Żółć &amp; tytuł</TITLE></HeAd><BODY><DIV ID "
            "= 'a' CLASS=box hidden data-empty='' href='/path?a=1&amp;b=%2F'><p>A &lt; B &gt; C &quot; "
            "&apos; &nbsp; &#123; &#x1F642; &123; &x1F;</DIV></BODY></HTML>");
    CHECK(equal(d.str(d.title), "Żółć & tytuł"));
    auto a = find(d, "a");
    CHECK(a);
    CHECK(equal(d.attr(a, "class"), "box"));
    CHECK(d.has_attr(a, "hidden"));
    CHECK(d.has_attr(a, "data-empty"));
    CHECK(equal(d.attr(a, "href"), "/path?a=1&b=%2F"));
    char content[2048];
    CHECK(ScriptDom(d).text_content({d.generation, a}, content, sizeof(content)));
    CHECK(std::strstr(content, "A < B > C"));
    CHECK(std::strstr(content, "🙂"));
    CHECK(std::strstr(content, "\xc2\xa0"));
    invariant(d);
    load(d, "<body><style>div {color: red} .box {color:green} div.box {color:blue} #a{color:#123456} "
            "#a{color:#abcdef} body > div {padding:4px} div p {font-size:2em} div > p:first-child "
            "{font-weight:700} p:last-child {text-decoration:underline}</style><div id=a class=box><p "
            "id=b>Hello</p><p id=c>World</p></div></body>");
    a = find(d, "a");
    auto b = find(d, "b"), c = find(d, "c");
    CHECK(d.styles[a].color == 0xabcdef);
    CHECK(d.styles[a].padding[0].value == 4000);
    CHECK(d.styles[b].font == 32);
    CHECK(d.styles[b].bold);
    CHECK(d.styles[c].underline);
    CHECK(d.styles[b].color == 0xabcdef);
    load(d, "<style>p{color:red;color:nonsense;width:10px;width:-2px} .a{color:green!important} "
            "#a{color:blue}</style><p class=a id=a style='color:purple'>A</p><p id=b "
            "style='color:inherit'>B</p>");
    a = find(d, "a");
    b = find(d, "b");
    CHECK(d.styles[a].color == 0x008000);
    CHECK(resolve(d.styles[a].width, 100, 16, 16) == 10);
    CHECK(d.styles[b].color == d.styles[0].color);
    load(d, "<style>html{font-size:20px} body{margin:0} #a{font-size:2rem;width:50%;padding:1em "
            "2px;margin:3px 4px 5px 6px;border:2px solid "
            "#123;box-sizing:border-box;min-height:20px;max-height:100px;overflow-x:hidden;overflow-y:auto;"
            "opacity:.5;border-radius:9px} #b{font-size:50%;line-height:1.5;visibility:hidden}</style><div "
            "id=a><span id=b>Test</span></div>");
    a = find(d, "a");
    b = find(d, "b");
    CHECK(d.styles[a].font == 40);
    CHECK(d.styles[b].font == 20);
    CHECK(d.styles[b].line == 30);
    CHECK(!d.styles[b].visible);
    CHECK(d.styles[a].opacity == 127);
    CHECK(d.styles[a].radius == 9);
    CHECK(d.boxes[a].width == 160);
    CHECK(d.boxes[a].content_width == 152);
    CHECK(d.styles[a].border_color == 0x112233);
    CHECK(d.styles[a].overflow_x == Overflow::Hidden);
    load(d,
         "<style>p,span{color:rgb(100%,0%,0%)} #a{color:initial} p {padding:2px "
         "/*comment*/;color:red;garbage; {nested:bad} height:20px}</style><p id=a>A</p><span id=b>B</span>");
    CHECK(d.styles[find(d, "a")].color == 0x202020);
    CHECK(d.styles[find(d, "b")].color == 0xff0000);
    load(d,
         "<style>body{margin:0}</style><div id=a style='padding:8px;border:2px solid "
         "blue;margin:10px;width:50%'>Words that wrap across lines on a small display. Żółć.</div>",
         300);
    a = find(d, "a");
    CHECK(d.boxes[a].x == 10);
    CHECK(d.boxes[a].content_x == 20);
    CHECK(d.boxes[a].content_width == 150);
    CHECK(d.boxes[a].width == 170);
    auto parse_count = d.parse_count;
    auto old_height = d.height;
    auto arena = d.arena_used;
    reflow(d, 160, metrics);
    CHECK(d.height > old_height);
    CHECK(d.parse_count == parse_count);
    for (int i = 0; i < 10; ++i) {
        d.styles_dirty = true;
        reflow(d, 160, metrics);
    }
    CHECK(d.arena_used == arena);
    load(d,
         "<style>body{margin:0}</style><table id=t><thead><tr><th id=a>One</th><th "
         "id=b>Two</th></tr></thead><tbody><tr><td>Three</td><td>Four</td></tr></tbody></table>",
         300);
    a = find(d, "a");
    b = find(d, "b");
    CHECK(d.boxes[a].y == d.boxes[b].y);
    CHECK(d.boxes[b].x == d.boxes[a].x + d.boxes[a].width);
    CHECK(d.boxes[a].width == 150);
    invariant(d);
    load(d, "<p><a id=a href='/next'>A useful link</a></p><img id=img alt='Opis "
            "obrazu'><form><label>Name<input value=Jan></label><button>OK</button><textarea>Żółć\nnew "
            "line</textarea><select><option>First</option><option selected>Second</option></select></form>");
    a = find(d, "a");
    auto text = d.nodes[a].first;
    CHECK(text);
    CHECK(hit_test(d, d.boxes[text].x, d.boxes[text].y) == a);
    CHECK(d.nodes[find(d, "img")].text_length > 0);
    invariant(d);
    load(d, "<div id=a>A<script src='/app.js' type='text/javascript'>alert('x')</script><script>let x = "
            "'<p>'; if (x) { x = 1; }</script><style>p{display:none}</style><p>Hidden</p>B</div>");
    CHECK(d.script_count == 2);
    CHECK(equal(d.str(d.scripts[0].url), "/app.js"));
    CHECK(std::strstr(d.str(d.scripts[1].source), "let x"));
    CHECK(execute_script(d, d.scripts[1], nullptr) == ScriptResult::Unavailable);
    auto handle = ScriptDom(d).get_element_by_id("a");
    CHECK(ScriptDom(d).valid(handle));
    load(d, "<div id=a>Replacement</div>");
    CHECK(!ScriptDom(d).valid(handle));
    ScriptBudget budget({3, 100, 2, 0});
    CHECK(budget.consume(3));
    CHECK(!budget.consume());
    CHECK(budget.enter());
    CHECK(budget.enter());
    CHECK(!budget.enter());
    budget.leave();
    CHECK(budget.enter());
    load(d, "<style>.x.x.x.x.x.x.x.x.x.x.x{color:green} #b{color:blue} "
            "#a#a#a#a#a#a#a#a#a#a#a#a#a#a#a#a{color:red!important}</style><div id=b class=x>B</div><div id=a "
            "style='color:purple!important'>A</div>");
    CHECK(d.styles[find(d, "b")].color == 0x0000ff);
    CHECK(d.styles[find(d, "a")].color == 0x800080);
    std::string long_rule = "<style>p{";
    for (unsigned i = 0; i < 250; ++i)
        long_rule += "color:red;";
    long_rule += "} p{color:blue}</style><p id=a>A</p>";
    load(d, long_rule);
    CHECK(d.styles[find(d, "a")].color == 0x0000ff);
    load(d, "<div id=a style='display:table-row;width:8000000px'>" + std::string(2040 * 20, ' ') +
                "<div style='display:table-cell'>A</div><div style='display:table-cell'>B</div></div>");
    invariant(d);
    std::string unicode;
    for (unsigned i = 0; i < 10000; ++i)
        unicode += "Ż";
    load(d, unicode, 320, false);
    std::vector<char> all_text(unicode.size() + 1);
    CHECK(ScriptDom(d).text_content({d.generation, 1}, all_text.data(), all_text.size()));
    CHECK(unicode == all_text.data());
    load(d, "<script>" + std::string(MaxScriptSource + 100, 'x') + "</script>");
    CHECK(d.script_count == 1 && d.scripts[0].truncated);
    CHECK(execute_script(d, d.scripts[0], nullptr) == ScriptResult::SourceLimit);
    load(d, "<div id=a><input id=b value=Test style='width:100px;height:40px;padding:4px;border:2px solid "
            "blue;box-sizing:border-box'></div>");
    b = find(d, "b");
    CHECK(d.boxes[b].width == 100);
    CHECK(d.boxes[b].height == 40);
    CHECK(d.boxes[b].content_width == 88);
    for (unsigned i = 0; i < d.fragment_count; ++i)
        if (d.fragments[i].node == b) {
            CHECK(d.fragments[i].y >= d.boxes[b].y);
            CHECK(d.fragments[i].y + d.fragments[i].height <= d.boxes[b].y + d.boxes[b].height);
        }
    std::vector<std::string> malformed = {
        "<",
        "<!--",
        "<!DOCTYPE",
        "<div x='unclosed",
        "<a href=><unknown>text</wrong>",
        "<style>p {color:;width:999999999999999999999px; height:-99px; margin:-999999em} {{{{}",
        "&; &#; &#x; &#-1; &#999999999999999999; &missing;",
        "<style>/*unclosed",
        "<style>p {a:'\\\"'; color: blue",
        std::string("<p>\xff\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80")};
    std::string deep;
    for (int i = 0; i < 2000; ++i)
        deep += "<div>";
    deep += "Text";
    for (int i = 0; i < 2000; ++i)
        deep += "</div>";
    malformed.push_back(deep);
    malformed.push_back("<a href='" + std::string(20000, 'a') + "'>Text</a>");
    std::string many;
    for (int i = 0; i < 4000; ++i)
        many += "<p>A</p>";
    malformed.push_back(many);
    for (auto &s : malformed) {
        load(d, s);
        invariant(d);
        checksum(d);
    }
    // Deterministic byte fuzz, including embedded NULs, truncated UTF-8 and tag/CSS delimiters.
    uint32_t random = 0x12345678;
    const char alphabet[] = "<>/='\"{}:;#&![]() abcdef0123456789\n\t\xff\x80";
    for (unsigned sample = 0; sample < 600; ++sample) {
        std::string bytes;
        unsigned n = sample % 257;
        for (unsigned j = 0; j < n; ++j) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            bytes += alphabet[random % (sizeof(alphabet) - 1)];
        }
        load(d, bytes, 80 + int(sample % 500));
        invariant(d);
        parse_css(d, bytes.data(), bytes.size());
        reflow(d, 80, metrics);
        invariant(d);
    }
    const char *fixtures[] = {"plain.txt",     "headings.html", "nested.html", "inline.html", "lists.html",
                              "tables.html",   "links.html",    "images.html", "box.html",    "percent.html",
                              "wrapping.html", "radius.html",   "scroll.html"};
    std::ifstream golden("narzedzia/fixtures/husarz/expected.tsv");
    CHECK(bool(golden));
    for (auto name : fixtures) {
        std::ifstream in(std::string("narzedzia/fixtures/husarz/") + name, std::ios::binary);
        CHECK(bool(in));
        std::string source((std::istreambuf_iterator<char>(in)), {});
        load(d, source, 320, !equal(name, "plain.txt"));
        auto hash = checksum(d);
        std::string expected_name;
        int expected_height;
        unsigned expected_fragments;
        uint64_t expected_hash;
        CHECK(bool(golden >> expected_name >> expected_height >> expected_fragments >> std::hex >>
                   expected_hash >> std::dec));
        CHECK(expected_name == name);
        CHECK(d.height == expected_height);
        CHECK(d.fragment_count == expected_fragments);
        CHECK(hash == expected_hash);
        auto height = d.height;
        auto count = d.parse_count;
        reflow(d, 320, metrics);
        CHECK(checksum(d) == hash);
        CHECK(d.height == height);
        CHECK(d.parse_count == count);
        std::printf("fixture %-14s height=%d fragments=%u checksum=%016llx\n", name, height, d.fragment_count,
                    (unsigned long long)hash);
    }
    std::printf("Husarz engine: %u checks PASS; malformed input, DOM/CSS/layout/paint/JS boundaries\n",
                checks);
}
