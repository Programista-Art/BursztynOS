// Offline captures only: this tool never fetches public sites.
#define HUSARZ_PERF 1
#include "../programy/husarz/paint.h"
#include "../programy/husarz/url.h"
#include "../bursztyn_ui/czcionka.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
using namespace Husarz;
static std::string read(const char *path) {
    std::ifstream f(path);
    return {(std::istreambuf_iterator<char>(f)), {}};
}
static int advance_ui(void *, uint32_t cp, int font, bool bold, bool) {
    return clamp64(
        (int64_t(BursztynUI::CzcionkaUI::szerokosc_znaku(cp) + 1) * font + 8) / 16 + (bold ? 1 : 0), 1, 1024);
}
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    int width = 1000, height = 700;
    if (auto *v = std::getenv("HUSARZ_PROFILE_WIDTH"))
        width = clamp64(std::atoi(v), 320, 1920);
    if (auto *v = std::getenv("HUSARZ_PROFILE_HEIGHT"))
        height = clamp64(std::atoi(v), 200, 1080);
    Metrics metrics{nullptr, advance_ui, [](void *, int font) { return font; }};
    auto d = std::make_unique<Document>();
    auto html = read(argv[1]);
    if (html.empty())
        return 2;
    parse_html(*d, html.data(), html.size());
    collect_styles(*d);
    prepare_controls(*d);
    std::vector<uint16_t> sheets;
    for (uint16_t i = 1; i < d->node_count; ++i)
        if (d->tag(i, "link") && token_ci(d->attr(i, "rel"), "stylesheet"))
            sheets.push_back(i);
    std::printf("HTML nodes=%u attrs=%u arena=%u rules=%u declarations=%u truncated=%d sizeof_document=%zu "
                "sheets=%zu\n",
                d->node_count, d->attr_count, d->arena_used, d->rule_count, d->declaration_count,
                d->truncated, sizeof(*d), sheets.size());
    const char *base = std::getenv("HUSARZ_PROFILE_URL");
    if (!base)
        base = "https://fixture.test/";
    for (int i = 2; i < argc; ++i) {
        auto css = read(argv[i]);
        if (css.empty())
            return 2;
        unsigned r = d->rule_count, c = d->declaration_count;
        size_t slot = unsigned(i - 2);
        uint16_t node = slot < sheets.size() ? sheets[slot] : uint16_t(i);
        char source[2048]{};
        uint32_t url = 0;
        if (slot < sheets.size() && resolve_resource_url(base, d->attr(node, "href"), source, sizeof(source)))
            url = d->store(source, length(source, sizeof(source)));
        parse_css(*d, css.data(), css.size(), uint32_t(node) * 4096, url);
        std::printf("%s bytes=%zu rules=%u declarations=%u arena=%u limits=%u\n", argv[i], css.size(),
                    d->rule_count - r, d->declaration_count - c, d->arena_used, d->limits);
    }
    d->style_viewport_height = height;
    WorkDriver driver;
    driver.allow_clock = false;
    auto work = reflow_work(*d, width, metrics);
    while (!work.step(driver)) {
    };
    if (driver.failed)
        return 3;
    unsigned max_custom = 0;
    unsigned max_depth = 0;
    for (unsigned i = 0; i < d->node_count; ++i) {
        if (d->nodes[i].depth > max_depth)
            max_depth = d->nodes[i].depth;
        unsigned count = 0;
        for (auto p = d->custom_heads[i]; p; p = d->custom[p - 1].next)
            ++count;
        if (count > max_custom)
            max_custom = count;
    }
    std::printf(
        "totals rules=%u declarations=%u limits=%u generated=%u fonts=%u unsupported-selectors=%u arena=%u "
        "custom-total=%u custom-max-element=%u style-layout-slices=%u max-work=%u index-bytes=%zu\n",
        d->rule_count, d->declaration_count, d->limits, d->generated_count, d->font_count,
        d->unsupported_selectors, d->arena_used, d->custom_count, max_custom, driver.slices, driver.maximum,
        sizeof(SelectorIndex));
    std::printf("document nodes=%u attrs=%u depth=%u fragments=%u scripts=%u box-capacity=%u\n",
                d->node_count, d->attr_count, max_depth, d->fragment_count, d->script_count, MaxNodes);
    if (const char *path = std::getenv("HUSARZ_PROFILE_RENDER")) {
        std::vector<uint32_t> pixels(size_t(width) * height);
        Surface surface{pixels.data(), width, height};
        Glyphs glyphs{nullptr, [](void *, uint32_t cp) { return BursztynUI::CzcionkaUI::bitmapa_znaku(cp); },
                      [](void *, uint32_t cp) { return BursztynUI::CzcionkaUI::szerokosc_znaku(cp); }};
        paint(*d, surface, 0, metrics, glyphs);
        auto *out = std::fopen(path, "wb");
        if (!out)
            return 2;
        std::fprintf(out, "P6\n%d %d\n255\n", width, height);
        for (auto pixel : pixels) {
            unsigned char bytes[3] = {uint8_t(pixel >> 16), uint8_t(pixel >> 8), uint8_t(pixel)};
            std::fwrite(bytes, 1, 3, out);
        }
        std::fclose(out);
        std::printf("paint-budget-remaining=%u truncated=%d\n", surface.budget, surface.truncated);
    }
    for (unsigned i = 1; i < d->node_count; ++i)
        if (d->tag(i, "body") || d->tag(i, "footer") || d->tag(i, "h1") ||
            token_ci(d->attr(i, "class"), "btn-primary") || equal(d->attr(i, "id"), "download-button")) {
            auto &s = d->styles[i];
            auto &b = d->boxes[i];
            std::printf("node=%u tag=%s id=%s color=%06x color-alpha=%u background=%06x has-background=%d "
                        "opacity=%u flex=%d box=%d,%d %dx%d\n",
                        i, d->str(d->nodes[i].tag), d->attr(i, "id"), s.color, s.color_alpha, s.background,
                        s.has_background, s.paint_opacity, s.flex, b.x, b.y, b.width, b.height);
        }
}
