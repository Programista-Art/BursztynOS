#pragma once
#include "paint.h"
namespace Husarz {
enum Dirty : uint8_t { Clean = 0, DirtyPaint = 1, DirtyScroll = 2, DirtyLayout = 4, DirtyStyle = 8 };
inline uint8_t escalate_dirty(uint8_t flags) {
    if (flags & DirtyStyle)
        flags |= DirtyLayout;
    if (flags & DirtyLayout)
        flags |= DirtyPaint | DirtyScroll;
    return flags;
}
struct LayoutIdentity {
    uint32_t tab{}, navigation{}, document{}, revision{}, style{}, viewport{}, paint{};
    uint16_t focus{}, select{};
    bool operator==(const LayoutIdentity &b) const {
        return tab == b.tab && navigation == b.navigation && document == b.document &&
               revision == b.revision && style == b.style && viewport == b.viewport && paint == b.paint &&
               focus == b.focus && select == b.select;
    }
};
struct ViewportRequest {
    int width{1}, height{1};
    uint32_t generation{};
    bool pending{};
    uint64_t received{}, coalesced{}, performed{};
    bool request(int w, int h) {
        if (w < 1 || h < 1 || w > 4096 || h > 4096)
            return false;
        if (w == width && h == height)
            return false;
        ++received;
        if (pending)
            ++coalesced;
        width = w;
        height = h;
        ++generation;
        pending = true;
        return true;
    }
};
inline Work copy_document_bytes(void *out, const void *input, size_t n) {
    auto *dst = static_cast<uint8_t *>(out);
    auto *src = static_cast<const uint8_t *>(input);
    for (size_t i = 0; i < n;) {
        co_await WorkPoint{1};
        size_t end = i + 64;
        if (end > n)
            end = n;
        while (i < end) {
            dst[i] = src[i];
            ++i;
        }
    }
}
inline Work clone_layout_document(Document &dst, const Document &src) {
    dst.next_node_identity = src.next_node_identity;
    dst.node_count = src.node_count;
    dst.attr_count = src.attr_count;
    dst.arena_used = src.arena_used;
    dst.rule_count = src.rule_count;
    dst.declaration_count = src.declaration_count;
    dst.script_count = src.script_count;
    dst.title = src.title;
    dst.base_url = src.base_url;
    dst.generation = src.generation;
    dst.revision = src.revision;
    dst.styles_dirty = src.styles_dirty;
    dst.style_revision = src.style_revision;
    dst.user_agent_loaded = src.user_agent_loaded;
    dst.generated_count = src.generated_count;
    dst.font_count = src.font_count;
    co_await copy_document_bytes(dst.generated, src.generated, src.generated_count * sizeof(GeneratedBox));
    co_await copy_document_bytes(dst.fonts, src.fonts, src.font_count * sizeof(FontFace));
    dst.has_viewport_units = src.has_viewport_units;
    dst.has_media = src.has_media;
    dst.has_pseudo = src.has_pseudo;
    dst.limits = src.limits;
    dst.unsupported_selectors = src.unsupported_selectors;
    dst.unknown_at_rules = src.unknown_at_rules;
    dst.style_viewport_width = src.style_viewport_width;
    dst.style_viewport_height = src.style_viewport_height;
    dst.root_font = src.root_font;
    dst.user_agent = src.user_agent;
    dst.truncated = src.truncated;
    dst.parse_count = src.parse_count;
    dst.reflow_count = src.reflow_count;
    dst.paint_revision = src.paint_revision;
#if HUSARZ_PERF
    dst.perf = src.perf;
#endif
    co_await copy_document_bytes(dst.nodes, src.nodes, src.node_count * sizeof(Node));
    co_await copy_document_bytes(dst.identity_index, src.identity_index, sizeof(src.identity_index));
    co_await copy_document_bytes(dst.attrs, src.attrs, src.attr_count * sizeof(Attribute));
    co_await copy_document_bytes(dst.styles, src.styles, src.node_count * sizeof(Style));
    co_await copy_document_bytes(dst.rules, src.rules, src.rule_count * sizeof(Rule));
    co_await copy_document_bytes(dst.declarations, src.declarations,
                                 src.declaration_count * sizeof(Declaration));
    dst.custom_count = src.custom_count;
    co_await copy_document_bytes(dst.custom, src.custom, src.custom_count * sizeof(CustomProperty));
    co_await copy_document_bytes(dst.custom_heads, src.custom_heads, src.node_count * sizeof(uint16_t));
    co_await copy_document_bytes(dst.computed_text, src.computed_text, sizeof(src.computed_text));
    co_await copy_document_bytes(dst.arena, src.arena, src.arena_used);
    co_await copy_document_bytes(dst.scripts, src.scripts, src.script_count * sizeof(Script));
    dst.table_count = src.table_count;
    dst.table_cells = src.table_cells;
    dst.table_plan_revision = src.table_plan_revision;
    for (unsigned i = 0; i < src.table_count; ++i) {
        if (!dst.tables.ensure(i)) {
            dst.table_plan_revision = 0;
            dst.table_count = 0;
            break;
        }
        co_await copy_document_bytes(&dst.tables[i], &src.tables[i], sizeof(TablePlan));
    }
    co_await copy_document_bytes(dst.table_cell, src.table_cell, src.node_count * sizeof(TableCell));
    dst.control_count = src.control_count;
    dst.focused_control = src.focused_control;
    dst.open_select = src.open_select;
    co_await copy_document_bytes(dst.controls, src.controls, src.control_count * sizeof(FormControl));
    // Resource ownership is pinned by the caller after cloning. No borrowed
    // image owner is installed until that pin is acquired.
}
struct DocumentJob {
    LayoutIdentity identity{};
    Document *result{};
    Work task{};
    WorkDriver driver{};
    bool parse{}, ready{};
    int width{};
    unsigned phase{}, css_count{};
    uint32_t resource_revision{};
    DocumentJob() = default;
    ~DocumentJob() {
        task.cancel();
        delete result;
    }
    Work build(const Document *source, const char *html, size_t n, bool is_html) {
        if (parse) {
            co_await parse_html_work(*result, html, n, is_html);
            co_await collect_styles_work(*result);
            co_await prepare_controls_work(*result);
        } else
            co_await clone_layout_document(*result, *source);
        // Image bindings are installed by the caller before geometry starts.
    }
};
} // namespace Husarz
#if HUSARZ_PERF
namespace Husarz {
inline void report_performance(const Document &d, const ViewportRequest &v) {
    if (!perf_log)
        return;
    char line[1600];
    unsigned n = 0;
    perf_word(line, n, "[HUSARZ PERF]");
    auto field = [&](const char *key, uint64_t value) {
        perf_word(line, n, " ");
        perf_word(line, n, key);
        perf_word(line, n, "=");
        perf_number(line, n, value);
    };
    unsigned texts = 0;
    for (unsigned i = 0; i < d.node_count; ++i)
        if (d.nodes[i].is_text)
            ++texts;
    const auto &p = d.perf;
    field("nodes", d.node_count);
    field("textnodes", texts);
    field("cssrules", d.rule_count);
    field("cssdeclarations", d.declaration_count);
    field("selectorchecks", p.selector_checks);
    field("selectormatches", p.selector_matches);
    field("layoutboxes", d.node_count);
    field("textfragments", d.fragment_count);
    field("tables", d.table_count);
    field("tablecells", d.table_cells);
    field("tableplanwork", p.table_work);
    field("tablereplans", p.table_replans);
    field("images", d.image_count);
    field("controls", d.control_count);
    field("height", d.height);
    field("viewportwidth", v.width);
    field("viewportheight", v.height);
    field("fullreflows", p.full_reflows);
    field("partialreflows", p.partial_reflows);
    field("fullpaints", p.full_paints);
    field("partialpaints", p.partial_paints);
    field("paintcandidates", p.paint_candidates);
    field("paintvisible", p.paint_visible);
    field("paintculled", p.paint_culled);
    field("dirtyrectangles", p.dirty_rectangles);
    field("completions", p.resource_completions);
    field("cachehits", p.cache_hits);
    field("cachemisses", p.cache_misses);
    field("resizeeventsreceived", v.received);
    field("resizeeventscoalesced", v.coalesced);
    field("reflowsperformed", v.performed);
    field("maxslicework", p.max_slice_work);
    field("maxslicems", p.max_slice_ns / 1000000);
    field("generation", d.generation);
    field("revision", d.revision);
    field("stylerevision", d.style_revision);
    field("viewportgeneration", v.generation);
    for (unsigned i = 1; i < unsigned(Stage::Count); ++i) {
        perf_word(line, n, " ");
        perf_word(line, n, stage_name(Stage(i)));
        perf_word(line, n, "ms=");
        perf_number(line, n, p.duration[i] / 1000000);
    }
    line[n++] = '\n';
    line[n] = 0;
    perf_log(line);
}
} // namespace Husarz
#endif
namespace Husarz {
// A fixed-size control outside tables has no geometry dependency on its text.
// Every other geometry change conservatively escalates to full cooperative
// layout.
inline uint16_t local_control_update(const Document &d, int width) {
    if (d.styles_dirty || d.layout_width != width || d.fragment_count + MaxFormValue >= MaxFragments)
        return 0;
    uint16_t found = 0;
    for (uint16_t i = 1; i < d.node_count; ++i)
        if (d.node_dirty[i] & 12) {
            if (found || !is_control(d, i) || d.styles[i].width.unit == Unit::Auto ||
                d.styles[i].height.unit == Unit::Auto)
                return 0;
            for (uint16_t p = d.nodes[i].parent; p; p = d.nodes[p].parent)
                if (d.styles[p].display == Display::Table)
                    return 0;
            found = i;
        }
    return found;
}
inline Work update_control_layout(Document &d, const Document &old, uint16_t id, Metrics metrics) {
    co_await copy_document_bytes(d.boxes, old.boxes, old.node_count * sizeof(Box));
    co_await copy_document_bytes(d.fragments, old.fragments, old.fragment_count * sizeof(Fragment));
    d.fragment_count = old.fragment_count;
    d.layout_width = old.layout_width;
    d.height = old.height;
    unsigned first = 0;
    while (first < d.fragment_count && d.fragments[first].node < id) {
        co_await WorkPoint{1};
        ++first;
    }
    unsigned end = first;
    while (end < d.fragment_count && d.fragments[end].node == id) {
        co_await WorkPoint{1};
        ++end;
    }
    unsigned count = d.fragment_count;
    const auto &b = d.boxes[id];
    const auto &n = d.nodes[id];
    Flow flow{id, b.content_x, b.content_y, b.content_width, 0, 0, int(count), 0, 0, 0, 1, false};
    Box saved_box = d.boxes[id];
    d.boxes[id].height = 0;
    co_await layout_text_work(d, flow, id, n.text, n.text_length, metrics);
    co_await finish_line_work(d, flow);
    d.boxes[id] = saved_box;
    unsigned added = d.fragment_count - count;
    struct Temporary {
        Fragment *p{};
        ~Temporary() { delete[] p; }
    } temporary;
    temporary.p = new Fragment[added ? added : 1];
    if (!temporary.p) {
        running_work->failed = true;
        co_return;
    }
    co_await copy_document_bytes(temporary.p, d.fragments + count, added * sizeof(Fragment));
    Clip clip = intersect(ancestor_clip(d, id, 0, MaxDimension, MaxDimension),
                          {b.content_x, b.content_y, b.content_width, b.content_height});
    for (unsigned i = 0; i < added; ++i) {
        co_await WorkPoint{1};
        temporary.p[i].clip = clip;
    }
    int delta = int(added) - int(end - first);
    if (delta > 0)
        for (unsigned i = count; i-- > end;) {
            co_await WorkPoint{1};
            d.fragments[int(i) + delta] = d.fragments[i];
        }
    else
        for (unsigned i = end; i < count; ++i) {
            co_await WorkPoint{1};
            d.fragments[int(i) + delta] = d.fragments[i];
        }
    co_await copy_document_bytes(d.fragments + first, temporary.p, added * sizeof(Fragment));
    d.fragment_count = unsigned(int(count) + delta);
    d.layout_revision = d.revision;
    d.reflow_count = old.reflow_count + 1;
    d.damage.clear();
    d.invalidate(id, 1);
    d.dirty = 1;
    for (auto &f : d.node_dirty)
        f = 0;
    HUSARZ_COUNT(d, partial_reflows, 1);
}
} // namespace Husarz
