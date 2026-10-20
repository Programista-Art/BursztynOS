#pragma once
#include "css.h"
namespace Husarz {
struct Flow {
    uint16_t node{};
    int x{}, y{}, width{}, pen{}, line_height{}, line_first{}, previous_margin{}, row_height{}, column{},
        columns{1};
    bool pending_space{};
};
inline int advance(const Metrics &m, uint32_t cp, const Style &s) {
    return clamp64(m.advance ? m.advance(m.user, cp, s.font, s.bold, s.italic) : (s.font * 9 + 8) / 16, 1,
                   1024);
}
inline int font_height(const Metrics &m, const Style &s) {
    return clamp64(m.height ? m.height(m.user, s.font) : s.font, 1, MaxFont * 2);
}
inline void finish_line(Document &d, Flow &f, bool force = false) {
    if (f.pen || force) {
        int h = f.line_height ? f.line_height : d.styles[f.node].line;
        int extra = f.width - f.pen;
        if (extra < 0)
            extra = 0;
        Align a = d.styles[f.node].align;
        int shift = a == Align::Center ? extra / 2 : a == Align::Right ? extra : 0;
        uint16_t moved = 0;
        for (uint32_t i = uint32_t(f.line_first); i < d.fragment_count; ++i) {
            auto &g = d.fragments[i];
            const auto &s = d.styles[g.node];
            bool replaced = d.tag(g.node, "input") || d.tag(g.node, "textarea") || d.tag(g.node, "select") ||
                            d.tag(g.node, "img");
            int box_height = replaced && d.boxes[g.node].height ? d.boxes[g.node].height : g.height;
            int dy = s.vertical == Vertical::Top      ? 0
                     : s.vertical == Vertical::Middle ? (h - box_height) / 2
                                                      : h - box_height;
            if (replaced && d.boxes[g.node].height && moved != g.node) {
                auto &b = d.boxes[g.node];
                b.x = clamp64(int64_t(b.x) + shift, -MaxDimension);
                b.content_x = clamp64(int64_t(b.content_x) + shift, -MaxDimension);
                b.y = clamp64(int64_t(b.y) + (dy > 0 ? dy : 0));
                b.content_y = clamp64(int64_t(b.content_y) + (dy > 0 ? dy : 0));
                moved = g.node;
            }
            if (dy < 0)
                dy = 0;
            g.x = clamp64(int64_t(g.x) + shift, -MaxDimension);
            g.y = clamp64(int64_t(g.y) + dy);
        }
        f.y = clamp64(int64_t(f.y) + h);
        f.pen = 0;
        f.line_height = 0;
        f.line_first = int(d.fragment_count);
        f.pending_space = false;
    }
}
inline void emit(Document &d, Flow &f, uint16_t id, uint32_t off, uint32_t len, int width, const Metrics &m) {
    const auto &s = d.styles[id];
    if (d.fragment_count == MaxFragments) {
        d.truncated = true;
        return;
    }
    d.fragments[d.fragment_count++] = {
        id, off, len, clamp64(int64_t(f.x) + f.pen, -MaxDimension), f.y, width, font_height(m, s), {}};
    f.pen = clamp64(int64_t(f.pen) + width);
    int h = s.line;
    if (h < font_height(m, s))
        h = font_height(m, s);
    if (h > f.line_height)
        f.line_height = h;
}
inline void layout_text(Document &d, Flow &f, uint16_t id, uint32_t offset, uint32_t size, const Metrics &m) {
    const Style &s = d.styles[id];
    const char *text = d.str(offset);
    size_t p = 0;
    bool pre = s.white == WhiteSpace::Pre || s.white == WhiteSpace::PreWrap;
    bool wrap = s.white == WhiteSpace::Normal || s.white == WhiteSpace::PreWrap;
    while (p < size) {
        if (text[p] == '\r') {
            ++p;
            continue;
        }
        if (pre && text[p] == '\n') {
            finish_line(d, f, true);
            ++p;
            continue;
        }
        if (space(text[p])) {
            if (pre) {
                int w = advance(m, ' ', s);
                if (text[p] == '\t')
                    w *= 4;
                if (wrap && f.pen && int64_t(f.pen) + w > f.width)
                    finish_line(d, f);
                f.pen = clamp64(int64_t(f.pen) + w);
                if (s.line > f.line_height)
                    f.line_height = s.line;
            } else
                f.pending_space = f.pen > 0;
            ++p;
            continue;
        }
        size_t start = p;
        int width = 0;
        while (p < size && !space(text[p])) {
            size_t used;
            uint32_t cp = utf8(text + p, size - p, used);
            width = clamp64(int64_t(width) + advance(m, cp, s));
            p += used;
        }
        int gap = f.pending_space ? advance(m, ' ', s) : 0;
        if (wrap && f.pen && int64_t(f.pen) + gap + width > f.width) {
            finish_line(d, f);
            gap = 0;
        }
        f.pending_space = false;
        f.pen = clamp64(int64_t(f.pen) + gap);
        if (!wrap || width <= f.width) {
            emit(d, f, id, offset + uint32_t(start), uint32_t(p - start), width, m);
            continue;
        }
        size_t chunk = start;
        int chunk_width = 0;
        for (size_t k = start; k < p;) {
            size_t used;
            uint32_t cp = utf8(text + k, p - k, used);
            int w = advance(m, cp, s);
            if (chunk_width && int64_t(f.pen) + chunk_width + w > f.width) {
                emit(d, f, id, offset + uint32_t(chunk), uint32_t(k - chunk), chunk_width, m);
                finish_line(d, f);
                chunk = k;
                chunk_width = 0;
            }
            chunk_width = clamp64(int64_t(chunk_width) + w);
            k += used;
        }
        if (chunk < p)
            emit(d, f, id, offset + uint32_t(chunk), uint32_t(p - chunk), chunk_width, m);
    }
}
inline unsigned table_columns(const Document &d, uint16_t id) {
    for (uint16_t i = id; i > 0; i = d.nodes[i].parent)
        if (d.styles[i].display == Display::Table) {
            unsigned max = 1;
            uint16_t end = uint16_t(i + 1);
            while (end < d.node_count && d.nodes[end].depth > d.nodes[i].depth) {
                if (d.styles[end].display == Display::Row) {
                    unsigned n = 0;
                    for (uint16_t c = d.nodes[end].first; c; c = d.nodes[c].next)
                        if (d.styles[c].display == Display::Cell)
                            ++n;
                    if (n > max)
                        max = n;
                }
                ++end;
            }
            return max > 32 ? 32 : max;
        }
    return 1;
}
inline void prepare_controls(Document &d) {
    for (uint16_t id = 1; id < d.node_count; ++id) {
        Node &n = d.nodes[id];
        if (d.tag(id, "li")) {
            char marker[16] = {};
            if (d.tag(n.parent, "ol")) {
                unsigned count = 1;
                for (uint16_t p = n.previous; p; p = d.nodes[p].previous)
                    if (d.tag(p, "li"))
                        ++count;
                char digits[8];
                unsigned k = 0;
                do {
                    digits[k++] = char('0' + count % 10);
                    count /= 10;
                } while (count && k < 8);
                unsigned j = 0;
                while (k)
                    marker[j++] = digits[--k];
                marker[j++] = '.';
                marker[j] = 0;
            } else {
                marker[0] = char(0xe2);
                marker[1] = char(0x80);
                marker[2] = char(0xa2);
            }
            n.text = d.store(marker, length(marker, 16));
            n.text_length = uint32_t(length(marker, 16));
        } else if (d.tag(id, "input") || d.tag(id, "img")) {
            const char *text = d.attr(id, d.tag(id, "img") ? "alt" : "value");
            if (!*text)
                text = d.tag(id, "img")                        ? "[obraz]"
                       : equal(d.attr(id, "type"), "checkbox") ? (d.has_attr(id, "checked") ? "[x]" : "[ ]")
                       : equal(d.attr(id, "type"), "radio")    ? "( )"
                       : equal(d.attr(id, "type"), "submit")   ? "Wyślij"
                                                               : " ";
            n.text = d.store(text, length(text, MaxAttrLength));
            n.text_length = uint32_t(length(d.str(n.text), MaxAttrLength));
        } else if (d.tag(id, "select")) {
            for (uint16_t c = n.first; c; c = d.nodes[c].next)
                if (d.tag(c, "option")) {
                    uint16_t t = d.nodes[c].first;
                    if (t && d.nodes[t].is_text) {
                        if (!n.text || d.has_attr(c, "selected")) {
                            n.text = d.nodes[t].text;
                            n.text_length = d.nodes[t].text_length;
                        }
                        if (d.has_attr(c, "selected"))
                            break;
                    }
                }
        }
    }
}
inline void reflow(Document &d, int width, const Metrics &metrics, uint32_t text_color = 0x202020) {
    width = clamp64(width, 1, 8192);
    if (d.styles_dirty)
        compute_styles(d, text_color);
    d.fragment_count = 0;
    d.layout_width = width;
    ++d.reflow_count;
    Flow flow[MaxDepth + 2]{};
    uint16_t active[MaxDepth + 2]{};
    bool block[MaxDepth + 2]{}, skip[MaxDepth + 2]{};
    int outer_margin[MaxDepth + 2]{};
    flow[0] = {0, 0, 0, width, 0, 0, 0, 0, 0, 0, 1, false};
    block[0] = true;
    d.boxes[0] = {0, 0, width, 0, 0, 0, width, 0};
    uint16_t id = 1;
    while (id < d.node_count) {
        Node &n = d.nodes[id];
        unsigned depth = n.depth;
        if (depth > MaxDepth) {
            d.truncated = true;
            ++id;
            continue;
        }
        unsigned parent_depth = depth - 1;
        unsigned fidx = active[parent_depth];
        Flow &parent = flow[fidx];
        Style &s = d.styles[id];
        Box &box = d.boxes[id];
        box = {};
        skip[depth] = skip[parent_depth] || s.display == Display::None;
        bool replaced = !n.is_text && (d.tag(id, "input") || d.tag(id, "textarea") || d.tag(id, "select") ||
                                       d.tag(id, "img"));
        bool is_block = s.display == Display::Block || s.display == Display::Table ||
                        s.display == Display::Row || s.display == Display::Cell;
        block[depth] = is_block;
        active[depth] = uint16_t(fidx);
        outer_margin[depth] = 0;
        if (!skip[depth]) {
            if (n.is_text)
                layout_text(d, parent, id, n.text, n.text_length, metrics);
            else if (d.tag(id, "br"))
                finish_line(d, parent, true);
            else if (is_block) {
                finish_line(d, parent);
                int mt = resolve(s.margin[0], parent.width, s.font, d.root_font),
                    mb = resolve(s.margin[2], parent.width, s.font, d.root_font);
                int ml = resolve(s.margin[3], parent.width, s.font, d.root_font),
                    mr = resolve(s.margin[1], parent.width, s.font, d.root_font);
                int pl = resolve(s.padding[3], parent.width, s.font, d.root_font),
                    pr = resolve(s.padding[1], parent.width, s.font, d.root_font),
                    pt = resolve(s.padding[0], parent.width, s.font, d.root_font);
                int extra = clamp64(int64_t(pl) + pr + s.border[1] + s.border[3]);
                int available = parent.width;
                bool cell = s.display == Display::Cell && d.styles[parent.node].display == Display::Row;
                if (cell)
                    available = parent.width / (parent.columns ? parent.columns : 1);
                int content = resolve(s.width, available, s.font, d.root_font,
                                      clamp64(int64_t(available) - ml - mr - extra));
                if (s.width.unit != Unit::Auto && s.border_box)
                    content = clamp64(int64_t(content) - extra);
                int min = resolve(s.min_width, available, s.font, d.root_font),
                    max = resolve(s.max_width, available, s.font, d.root_font, MaxDimension);
                if (s.border_box) {
                    min = clamp64(int64_t(min) - extra);
                    if (s.max_width.unit != Unit::Auto)
                        max = clamp64(int64_t(max) - extra);
                }
                if (content > max)
                    content = max;
                if (content < min)
                    content = min;
                content = clamp64(content);
                int total = clamp64(int64_t(content) + extra);
                if (s.margin[3].unit == Unit::Auto || s.margin[1].unit == Unit::Auto) {
                    int remainder = clamp64(int64_t(available) - total - ml - mr);
                    if (s.margin[3].unit == Unit::Auto)
                        ml += s.margin[1].unit == Unit::Auto ? remainder / 2 : remainder;
                }
                int x =
                    clamp64(int64_t(parent.x) +
                                (cell ? int64_t(parent.column) * (parent.width / parent.columns) : 0) + ml,
                            -MaxDimension);
                int collapse = mt > parent.previous_margin ? mt - parent.previous_margin : 0;
                int y = clamp64(int64_t(parent.y) + (cell ? mt : collapse), -MaxDimension);
                box = {x,
                       y,
                       total,
                       0,
                       clamp64(int64_t(x) + pl + s.border[3], -MaxDimension),
                       clamp64(int64_t(y) + pt + s.border[0]),
                       content,
                       0};
                active[depth] = uint16_t(depth);
                flow[depth] = {id,
                               box.content_x,
                               box.content_y,
                               content,
                               0,
                               0,
                               int(d.fragment_count),
                               0,
                               0,
                               0,
                               int(table_columns(d, id)),
                               false};
                outer_margin[depth] = mb;
                if (d.tag(id, "li") && n.text) {
                    Flow marker = flow[depth];
                    marker.x = clamp64(int64_t(marker.x) - 24, -MaxDimension);
                    layout_text(d, marker, id, n.text, n.text_length, metrics);
                    flow[depth].line_first = int(d.fragment_count);
                }
            } else if (replaced) {
                int pl = resolve(s.padding[3], parent.width, s.font, d.root_font),
                    pr = resolve(s.padding[1], parent.width, s.font, d.root_font),
                    pt = resolve(s.padding[0], parent.width, s.font, d.root_font),
                    pb = resolve(s.padding[2], parent.width, s.font, d.root_font);
                int intrinsic = 0;
                for (size_t p = 0; p < n.text_length;) {
                    size_t used;
                    auto cp = utf8(d.str(n.text) + p, n.text_length - p, used);
                    intrinsic = clamp64(int64_t(intrinsic) + advance(metrics, cp, s));
                    p += used;
                }
                int cw = resolve(s.width, parent.width, s.font, d.root_font, intrinsic),
                    extra = clamp64(int64_t(pl) + pr + s.border[3] + s.border[1]);
                if (s.width.unit != Unit::Auto && s.border_box)
                    cw = clamp64(int64_t(cw) - extra);
                int min = resolve(s.min_width, parent.width, s.font, d.root_font),
                    max = resolve(s.max_width, parent.width, s.font, d.root_font, MaxDimension);
                if (cw > max)
                    cw = max;
                if (cw < min)
                    cw = min;
                if (s.border_box) {
                    min = clamp64(int64_t(min) - extra);
                    if (s.max_width.unit != Unit::Auto)
                        max = clamp64(int64_t(max) - extra);
                    if (cw > max)
                        cw = max;
                    if (cw < min)
                        cw = min;
                }
                int outer = clamp64(int64_t(cw) + extra);
                if (parent.pen && int64_t(parent.pen) + outer > parent.width)
                    finish_line(d, parent);
                int gap = parent.pending_space ? advance(metrics, ' ', s) : 0;
                parent.pending_space = false;
                parent.pen = clamp64(int64_t(parent.pen) + gap);
                box = {
                    clamp64(int64_t(parent.x) + parent.pen, -MaxDimension), parent.y, outer, 0, 0, 0, cw, 0};
                box.content_x = clamp64(int64_t(box.x) + pl + s.border[3], -MaxDimension);
                box.content_y = clamp64(int64_t(box.y) + pt + s.border[0]);
                Flow local{id, box.content_x, box.content_y, cw, 0, 0, int(d.fragment_count), 0, 0, 0,
                           1,  false};
                layout_text(d, local, id, n.text, n.text_length, metrics);
                finish_line(d, local);
                int ch = resolve(s.height, 0, s.font, d.root_font, clamp64(int64_t(local.y) - box.content_y));
                int vertical_extra = pt + pb + s.border[0] + s.border[2];
                if (s.height.unit != Unit::Auto && s.border_box)
                    ch = clamp64(int64_t(ch) - vertical_extra);
                int min_height = resolve(s.min_height, 0, s.font, d.root_font),
                    max_height = resolve(s.max_height, 0, s.font, d.root_font, MaxDimension);
                if (s.border_box) {
                    min_height = clamp64(int64_t(min_height) - vertical_extra);
                    if (s.max_height.unit != Unit::Auto)
                        max_height = clamp64(int64_t(max_height) - vertical_extra);
                }
                if (ch > max_height)
                    ch = max_height;
                if (ch < min_height)
                    ch = min_height;
                box.content_height = ch;
                box.height = clamp64(int64_t(ch) + vertical_extra);
                parent.pen = clamp64(int64_t(parent.pen) + outer);
                if (box.height > parent.line_height)
                    parent.line_height = box.height;
            } else {
                int left = resolve(s.margin[3], parent.width, s.font, d.root_font) +
                           resolve(s.padding[3], parent.width, s.font, d.root_font) + s.border[3];
                parent.pen = clamp64(int64_t(parent.pen) + left);
                if (n.text && !n.is_text) {
                    layout_text(d, parent, id, n.text, n.text_length, metrics);
                    if (s.width.unit != Unit::Auto) {
                        int desired = resolve(s.width, parent.width, s.font, d.root_font);
                        if (desired > parent.pen)
                            parent.pen = desired;
                    }
                    if (s.height.unit != Unit::Auto) {
                        int desired = resolve(s.height, 0, s.font, d.root_font);
                        if (desired > parent.line_height)
                            parent.line_height = desired;
                    }
                }
            }
        }
        uint16_t next = n.first;
        if (next && !skip[depth] && !replaced) {
            id = next;
            continue;
        }
        for (;;) {
            Node &end = d.nodes[id];
            unsigned dep = end.depth;
            if (!skip[dep]) {
                if (block[dep]) {
                    Flow &f = flow[dep];
                    finish_line(d, f);
                    Style &st = d.styles[id];
                    Box &b = d.boxes[id];
                    Flow &pf = flow[active[dep - 1]];
                    int content = clamp64(int64_t(f.y) - b.content_y);
                    if (st.display == Display::Row && f.row_height > content)
                        content = f.row_height;
                    int pb = resolve(st.padding[2], pf.width, st.font, d.root_font);
                    int extra = clamp64(int64_t(b.content_y) - b.y + pb + st.border[2]);
                    if (st.height.unit != Unit::Auto) {
                        content = resolve(st.height, 0, st.font, d.root_font, content);
                        if (st.border_box)
                            content = clamp64(int64_t(content) - extra);
                    }
                    int min = resolve(st.min_height, 0, st.font, d.root_font),
                        max = resolve(st.max_height, 0, st.font, d.root_font, MaxDimension);
                    if (st.border_box) {
                        min = clamp64(int64_t(min) - extra);
                        if (st.max_height.unit != Unit::Auto)
                            max = clamp64(int64_t(max) - extra);
                    }
                    if (content > max)
                        content = max;
                    if (content < min)
                        content = min;
                    b.content_height = content;
                    b.height = clamp64(int64_t(content) + extra);
                    if (st.display == Display::Cell && d.styles[pf.node].display == Display::Row) {
                        int h = clamp64(int64_t(b.y) - pf.y + b.height + outer_margin[dep]);
                        if (h > pf.row_height)
                            pf.row_height = h;
                        ++pf.column;
                    } else {
                        pf.y = clamp64(int64_t(b.y) + b.height + outer_margin[dep]);
                        pf.previous_margin = outer_margin[dep];
                        pf.line_first = int(d.fragment_count);
                    }
                } else if (!end.is_text && !d.tag(id, "input") && !d.tag(id, "textarea") &&
                           !d.tag(id, "select") && !d.tag(id, "img")) {
                    const Style &st = d.styles[id];
                    Flow &pf = flow[active[dep]];
                    pf.pen =
                        clamp64(int64_t(pf.pen) + resolve(st.padding[1], pf.width, st.font, d.root_font) +
                                resolve(st.margin[1], pf.width, st.font, d.root_font) + st.border[1]);
                }
            }
            if (end.next) {
                id = end.next;
                break;
            }
            id = end.parent;
            if (!id)
                break;
        }
        if (!id)
            break;
    }
    finish_line(d, flow[0]);
    d.boxes[0].height = d.boxes[0].content_height = flow[0].y;
    // Text and inline boxes are reconstructed after line alignment is final.
    for (uint32_t i = 0; i < d.fragment_count; ++i) {
        const auto &f = d.fragments[i];
        auto &b = d.boxes[f.node];
        if (d.styles[f.node].display != Display::Inline || d.tag(f.node, "input") ||
            d.tag(f.node, "textarea") || d.tag(f.node, "select") || d.tag(f.node, "img"))
            continue;
        if (!b.width && !b.height)
            b = {f.x, f.y, f.width, f.height, f.x, f.y, f.width, f.height};
        else {
            int l = b.x < f.x ? b.x : f.x, t = b.y < f.y ? b.y : f.y, r = clamp64(int64_t(b.x) + b.width),
                bottom = clamp64(int64_t(b.y) + b.height);
            if (int64_t(f.x) + f.width > r)
                r = clamp64(int64_t(f.x) + f.width);
            if (int64_t(f.y) + f.height > bottom)
                bottom = clamp64(int64_t(f.y) + f.height);
            b.x = l;
            b.y = t;
            b.width = clamp64(int64_t(r) - l);
            b.height = clamp64(int64_t(bottom) - t);
        }
    }
    for (uint32_t i = d.node_count; i-- > 1;) {
        uint16_t parent = d.nodes[i].parent;
        auto &b = d.boxes[i];
        auto &p = d.boxes[parent];
        if (d.styles[parent].display == Display::Inline && b.height) {
            if (!p.height)
                p = b;
            else {
                int x = p.x < b.x ? p.x : b.x, y = p.y < b.y ? p.y : b.y;
                int r = clamp64(int64_t(p.x) + p.width), bottom = clamp64(int64_t(p.y) + p.height);
                if (int64_t(b.x) + b.width > r)
                    r = clamp64(int64_t(b.x) + b.width);
                if (int64_t(b.y) + b.height > bottom)
                    bottom = clamp64(int64_t(b.y) + b.height);
                p = {x, y, clamp64(int64_t(r) - x), clamp64(int64_t(bottom) - y), x, y, 0, 0};
            }
        }
    }
    for (uint32_t i = 0; i < d.node_count; ++i) {
        auto &b = d.boxes[i];
        auto &st = d.styles[i];
        int basis = b.width < b.height ? b.width : b.height;
        st.radius = clamp64(resolve(st.radius_length, basis, st.font, d.root_font), 0, 4096);
    }
    d.height = d.boxes[0].height;
    for (uint32_t i = 0; i < d.fragment_count; ++i) {
        auto &f = d.fragments[i];
        Clip clip{-MaxDimension, 0, MaxDimension * 2, MaxDimension};
        for (uint16_t p = d.nodes[f.node].parent; p; p = d.nodes[p].parent) {
            const auto &s = d.styles[p];
            const auto &b = d.boxes[p];
            Clip c = clip;
            if (s.overflow_x != Overflow::Visible) {
                c.x = b.x;
                c.width = b.width;
            }
            if (s.overflow_y != Overflow::Visible) {
                c.y = b.y;
                c.height = b.height;
            }
            clip = intersect(clip, c);
        }
        f.clip = clip;
        int bottom = clamp64(int64_t(f.y) + f.height), limit = clamp64(int64_t(clip.y) + clip.height);
        if (bottom > limit)
            bottom = limit;
        if (bottom > d.height)
            d.height = bottom;
    }
    d.layout_revision = d.revision;
}
inline uint16_t hit_test(const Document &d, int x, int y) {
    for (uint32_t i = d.fragment_count; i-- > 0;) {
        const auto &f = d.fragments[i];
        if (!d.styles[f.node].visible || !d.styles[f.node].paint_opacity)
            continue;
        if (x >= f.x && int64_t(x) < int64_t(f.x) + f.width && y >= f.y &&
            int64_t(y) < int64_t(f.y) + f.height && x >= f.clip.x &&
            int64_t(x) < int64_t(f.clip.x) + f.clip.width && y >= f.clip.y &&
            int64_t(y) < int64_t(f.clip.y) + f.clip.height) {
            uint16_t n = f.node;
            while (n) {
                if (d.tag(n, "a") && *d.attr(n, "href"))
                    return n;
                n = d.nodes[n].parent;
            }
        }
    }
    return 0;
}
} // namespace Husarz
