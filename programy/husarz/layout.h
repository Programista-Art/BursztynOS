#pragma once
#include "css.h"
#include "forms.h"
namespace Husarz {
struct Flow {
    uint16_t node{};
    int x{}, y{}, width{}, pen{}, line_height{}, line_first{}, previous_margin{}, row_height{}, column{},
        columns{1};
    bool pending_space{};
};
inline int advance(const Metrics &m, uint32_t cp, const Style &s) {
    int base = m.advance ? m.advance(m.user, cp, s.font, s.bold, s.italic) : (s.font * 9 + 8) / 16;
    return clamp64(int64_t(base) + s.letter_spacing + (cp == ' ' ? s.word_spacing : 0), 0, 2048);
}
inline uint32_t transformed(uint32_t cp, Transform t, bool word_start) {
    bool upper = t == Transform::Upper || (t == Transform::Capitalize && word_start);
    if (upper && cp >= 'a' && cp <= 'z') return cp - 32;
    if (t == Transform::Lower && cp >= 'A' && cp <= 'Z') return cp + 32;
    static constexpr uint32_t lo[] = {0x105,0x107,0x119,0x142,0x144,0xf3,0x15b,0x17a,0x17c};
    static constexpr uint32_t hi[] = {0x104,0x106,0x118,0x141,0x143,0xd3,0x15a,0x179,0x17b};
    for (unsigned i=0;i<9;++i) { if (upper && cp == lo[i]) return hi[i]; if (t == Transform::Lower && cp == hi[i]) return lo[i]; }
    return cp;
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
            if (g.flow_node != f.node) continue;
            const auto &s = d.styles[g.node];
            bool replaced = d.tag(g.node, "input") || d.tag(g.node, "textarea") || d.tag(g.node, "select") ||
                            d.tag(g.node, "img") || d.tag(g.node,"button");
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
        id, off, len, clamp64(int64_t(f.x) + f.pen, -MaxDimension), f.y, width, font_height(m, s), {}, f.node};
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
            width = clamp64(int64_t(width) + advance(m, transformed(cp,s.transform,p == start), s));
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
            int w = advance(m, transformed(cp,s.transform,k == start), s);
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
inline unsigned positive_attribute(const Document &d, uint16_t id, const char *name, unsigned fallback, unsigned cap) {
    const char *s = d.attr(id,name); if (!*s) return fallback;
    unsigned value = 0;
    for (unsigned i = 0; s[i]; ++i) {
        if (i == 8 || s[i] < '0' || s[i] > '9') return fallback;
        value = value * 10 + unsigned(s[i]-'0'); if (value > cap) return cap;
    }
    return value ? value : fallback;
}
inline const TablePlan *table_plan(const Document &d, uint16_t id) {
    for (uint16_t p = id; p; p = d.nodes[p].parent)
        if (d.styles[p].display == Display::Table) {
            for (unsigned i=0;i<d.table_count;++i) if (d.tables[i].node == p) return &d.tables[i];
            break;
        }
    return nullptr;
}
inline unsigned table_columns(const Document &d, uint16_t id) {
    auto *plan = table_plan(d,id); return plan ? plan->columns : 1;
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
            char reserved[40]{};
            for (unsigned i=0;i<16;++i) reserved[i]=marker[i];
            n.text = d.store(reserved,39);
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
    initialize_controls(d);
}
inline int intrinsic_width(const Document &d, uint16_t id, const Metrics &metrics) {
    int sum = 0;
    for (uint16_t c = id; c < d.node_count && (c == id || d.nodes[c].depth > d.nodes[id].depth); ++c) {
        const Node &n = d.nodes[c];
        if (!n.is_text) continue;
        for (size_t p = 0; p < n.text_length;) {
            size_t used; uint32_t cp = utf8(d.str(n.text)+p,n.text_length-p,used);
            sum = clamp64(int64_t(sum)+advance(metrics,cp,d.styles[c]),0,8192); p += used;
        }
    }
    return sum;
}
inline void list_markers(Document &d) {
    for (uint16_t id=1;id<d.node_count;++id) if (d.tag(id,"li") && d.nodes[id].text) {
        auto &node=d.nodes[id]; char *out=d.arena+node.text; unsigned n=0;
        auto kind=d.styles[id].list_type;
        unsigned value=positive_attribute(d,node.parent,"start",1,3999);
        for (uint16_t p=node.previous;p;p=d.nodes[p].previous) if (d.tag(p,"li")) ++value;
        if (kind==ListType::Disc || kind==ListType::Circle || kind==ListType::Square) {
            uint32_t cp=kind==ListType::Disc?0x2022:kind==ListType::Circle?0x25e6:0x25aa;
            n=encode(cp,out);
        } else if (kind==ListType::LowerAlpha || kind==ListType::UpperAlpha) {
            char reversed[8]; unsigned k=0;
            do { --value; reversed[k++]=char((kind==ListType::LowerAlpha?'a':'A')+value%26); value/=26; } while (value && k<8);
            while (k) out[n++]=reversed[--k];
            out[n++]='.';
        } else if ((kind==ListType::LowerRoman || kind==ListType::UpperRoman) && value<=3999) {
            const char *digits[]={"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};
            unsigned weights[]={1000,900,500,400,100,90,50,40,10,9,5,4,1};
            for (unsigned i=0;i<13;++i) while (value>=weights[i] && n<35) {
                value-=weights[i]; for (unsigned j=0;digits[i][j];++j) out[n++]=kind==ListType::LowerRoman?lower(digits[i][j]):digits[i][j];
            }
            out[n++]='.';
        } else if (kind!=ListType::None) {
            char reversed[12]; unsigned k=0; do { reversed[k++]=char('0'+value%10); value/=10; } while (value && k<12);
            while (k) out[n++]=reversed[--k];
            out[n++]='.';
        }
        out[n]=0; node.text_length=n;
    }
}
inline void plan_tables(Document &d, const Metrics &metrics) {
    d.table_count = 0;
    for (uint16_t id=1;id<d.node_count;++id) if (d.styles[id].display == Display::Table) {
        if (d.table_count == 32) { d.truncated = true; break; }
        auto &plan = d.tables[d.table_count++]; plan = {}; plan.node = id;
        unsigned rows = 0;
        for (uint16_t row=id+1;row<d.node_count && d.nodes[row].depth>d.nodes[id].depth;++row) {
            if (d.styles[row].display != Display::Row) continue;
            // Nested tables have independent plans.
            uint16_t table = d.nodes[row].parent;
            while (table && d.styles[table].display != Display::Table) table = d.nodes[table].parent;
            if (table != id) continue;
            if (++rows > 256) { d.truncated = true; break; }
            unsigned col = 0;
            for (uint16_t cell=d.nodes[row].first;cell && col<32;cell=d.nodes[cell].next) {
                if (d.styles[cell].display != Display::Cell) continue;
                unsigned span = positive_attribute(d,cell,"colspan",1,32-col);
                const auto &s = d.styles[cell];
                int width = intrinsic_width(d,cell,metrics)+8;
                if (s.width.unit == Unit::Px) width = resolve(s.width,0,s.font,d.root_font);
                width = clamp64(width,8,8192)/int(span);
                for (unsigned c=col;c<col+span;++c) if (width>plan.weights[c]) plan.weights[c]=width;
                col += span;
            }
            if (col>plan.columns) plan.columns=uint16_t(col);
        }
        if (!plan.columns) plan.columns=1;
        for (unsigned col=0;col<plan.columns;++col) {
            if (!plan.weights[col]) plan.weights[col]=8;
            plan.total += plan.weights[col];
        }
    }
}
inline void reflow(Document &d, int width, const Metrics &metrics) {
    width = clamp64(width, 1, 8192);
    if (d.styles_dirty)
        compute_styles(d);
    list_markers(d);
    plan_tables(d,metrics);
    d.fragment_count = 0;
    d.offset_x[0] = d.offset_y[0] = 0;
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
                                       d.tag(id, "img") || d.tag(id,"button"));
        bool is_block = s.display == Display::Block || s.display == Display::Table ||
                        s.display == Display::Row || s.display == Display::Cell || s.display == Display::InlineBlock || s.position == Position::Absolute;
        if (replaced) is_block=false;
        block[depth] = is_block;
        active[depth] = uint16_t(fidx);
        outer_margin[depth] = 0;
        if (!skip[depth]) {
            if (n.is_text)
                layout_text(d, parent, id, n.text, n.text_length, metrics);
            else if (d.tag(id, "br"))
                finish_line(d, parent, true);
            else if (is_block) {
                bool inline_box = s.display == Display::InlineBlock && s.position != Position::Absolute;
                if (!inline_box && s.position != Position::Absolute) finish_line(d, parent);
                int mt = resolve(s.margin[0], parent.width, s.font, d.root_font),
                    mb = resolve(s.margin[2], parent.width, s.font, d.root_font);
                int ml = resolve(s.margin[3], parent.width, s.font, d.root_font),
                    mr = resolve(s.margin[1], parent.width, s.font, d.root_font);
                int pl = resolve(s.padding[3], parent.width, s.font, d.root_font),
                    pr = resolve(s.padding[1], parent.width, s.font, d.root_font),
                    pt = resolve(s.padding[0], parent.width, s.font, d.root_font);
                int extra = clamp64(int64_t(pl) + pr + s.border[1] + s.border[3]);
                int available = parent.width;
                if (inline_box && s.width.unit == Unit::Auto)
                    available = clamp64(int64_t(intrinsic_width(d,id,metrics))+extra,1,parent.width);
                int inline_outer=resolve(s.width,parent.width,s.font,d.root_font,available);
                if (s.width.unit!=Unit::Auto && !s.border_box) inline_outer=clamp64(int64_t(inline_outer)+extra);
                if (inline_box && parent.pen && int64_t(parent.pen)+inline_outer > parent.width) finish_line(d,parent);
                if (inline_box && parent.pending_space) { parent.pen=clamp64(int64_t(parent.pen)+advance(metrics,' ',s)); parent.pending_space=false; }
                bool cell = s.display == Display::Cell && d.styles[parent.node].display == Display::Row;
                int cell_offset = 0;
                if (cell) {
                    const auto *plan = table_plan(d,id);
                    unsigned col = unsigned(parent.column), span = positive_attribute(d,id,"colspan",1,col<32 ? 32-col : 1);
                    int before = 0, weight = 0;
                    if (plan) {
                        for (unsigned c=0;c<plan->columns;++c) {
                            if (c<col) before += plan->weights[c];
                            else if (c<col+span) weight += plan->weights[c];
                        }
                        cell_offset = int(int64_t(parent.width)*before/plan->total);
                        available = int(int64_t(parent.width)*(before+weight)/plan->total)-cell_offset;
                    } else { cell_offset = parent.column*(parent.width/parent.columns); available = parent.width/parent.columns; }
                    int gap = d.styles[parent.node].border_collapse ? 0 : d.styles[parent.node].border_spacing;
                    available = clamp64(int64_t(available)-gap);
                }
                int content = resolve(s.width, available, s.font, d.root_font,
                                      clamp64(int64_t(available) - ml - mr - extra));
                if (cell) content = clamp64(int64_t(available)-extra);
                if (!cell && s.width.unit != Unit::Auto && s.border_box)
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
                    clamp64(int64_t(parent.x) + (inline_box ? parent.pen : 0) +
                                (cell ? cell_offset : 0) + ml,
                            -MaxDimension);
                int previous=parent.previous_margin;
                int positive=(mt>0?mt:0)>(previous>0?previous:0)?(mt>0?mt:0):(previous>0?previous:0);
                int negative=(mt<0?mt:0)<(previous<0?previous:0)?(mt<0?mt:0):(previous<0?previous:0);
                int collapse=positive+negative-previous;
                int y = clamp64(int64_t(parent.y) + (cell ? mt : collapse), -MaxDimension);
                box = {x,
                       y,
                       total,
                       0,
                       clamp64(int64_t(x) + pl + s.border[3], -MaxDimension),
                       clamp64(int64_t(y) + pt + s.border[0]),
                       content,
                       0};
                if (s.height.unit!=Unit::Auto && s.height.unit!=Unit::Percent) {
                    box.content_height=clamp64(resolve(s.height,0,s.font,d.root_font));
                    if (s.border_box) box.content_height=clamp64(int64_t(box.content_height)-pt-s.border[0]-s.border[2]-resolve(s.padding[2],parent.width,s.font,d.root_font));
                } else if (s.height.unit==Unit::Percent && d.boxes[parent.node].content_height>0)
                    box.content_height=clamp64(resolve(s.height,d.boxes[parent.node].content_height,s.font,d.root_font));
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
                if (s.display==Display::Block && s.position!=Position::Absolute) finish_line(d,parent);
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
                const auto *image = d.image(id);
                Length image_width=s.width, image_height=s.height;
                if (d.tag(id,"img")) {
                    if (image_width.unit==Unit::Auto && d.has_attr(id,"width")) image_width={int(positive_attribute(d,id,"width",1,8192))*1000,Unit::Px};
                    if (image_height.unit==Unit::Auto && d.has_attr(id,"height")) image_height={int(positive_attribute(d,id,"height",1,8192))*1000,Unit::Px};
                    if (image) {
                        intrinsic=int(image->width);
                        if (image_width.unit==Unit::Auto && image_height.unit!=Unit::Auto)
                            intrinsic=clamp64(int64_t(resolve(image_height,0,s.font,d.root_font))*image->width/image->height);
                    }
                }
                int cw = resolve(image_width, parent.width, s.font, d.root_font, intrinsic),
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
                bool toggle=input_type(d,id,"checkbox") || input_type(d,id,"radio");
                if (!image && !toggle) layout_text(d, local, id, n.text, n.text_length, metrics);
                finish_line(d, local);
                int intrinsic_height=image ? clamp64(int64_t(cw)*image->height/image->width) : clamp64(int64_t(local.y)-box.content_y);
                int ch = resolve(image_height, 0, s.font, d.root_font, intrinsic_height);
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
                if (image && d.fragment_count<MaxFragments)
                    d.fragments[d.fragment_count++]={id,0,0,box.x,box.y,box.width,box.height,{},parent.node};
                if (s.position!=Position::Absolute) {
                    parent.pen = clamp64(int64_t(parent.pen) + outer);
                    if (box.height > parent.line_height) parent.line_height = box.height;
                    if (s.display==Display::Block) finish_line(d,parent);
                }
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
                    int containing_height=d.boxes[pf.node].content_height;
                    if (st.height.unit != Unit::Auto && (st.height.unit!=Unit::Percent || containing_height>0)) {
                        content = resolve(st.height, containing_height, st.font, d.root_font, content);
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
                    if (st.position == Position::Absolute) {
                        // The containing-block translation is applied after all dimensions are known.
                    } else if (st.display == Display::InlineBlock) {
                        pf.pen = clamp64(int64_t(pf.pen) + b.width);
                        if (b.height > pf.line_height) pf.line_height = b.height;
                    } else if (st.display == Display::Cell && d.styles[pf.node].display == Display::Row) {
                        int h = clamp64(int64_t(b.y) - pf.y + b.height + outer_margin[dep]);
                        if (h > pf.row_height)
                            pf.row_height = h;
                        pf.column += int(positive_attribute(d,id,"colspan",1,32));
                    } else {
                        pf.y = clamp64(int64_t(b.y) + b.height + outer_margin[dep]);
                        pf.previous_margin = outer_margin[dep];
                        pf.line_first = int(d.fragment_count);
                    }
                } else if (!end.is_text && !d.tag(id, "input") && !d.tag(id, "textarea") &&
                           !d.tag(id, "select") && !d.tag(id, "img") && !d.tag(id,"button")) {
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
    // Parent-first transform pass. Absolute boxes do not contribute to normal flow.
    for (uint16_t i = 1; i < d.node_count; ++i) {
        auto &b = d.boxes[i]; const auto &s = d.styles[i];
        int dx = d.offset_x[d.nodes[i].parent], dy = d.offset_y[d.nodes[i].parent];
        if (s.position == Position::Relative) {
            const auto &cb = d.boxes[d.nodes[i].parent];
            dx += s.left.unit != Unit::Auto ? resolve(s.left,cb.content_width,s.font,d.root_font) : -resolve(s.right,cb.content_width,s.font,d.root_font);
            dy += s.top.unit != Unit::Auto ? resolve(s.top,cb.content_height,s.font,d.root_font) : -resolve(s.bottom,cb.content_height,s.font,d.root_font);
        } else if (s.position == Position::Absolute) {
            uint16_t containing = d.nodes[i].parent;
            while (containing && d.styles[containing].position == Position::Static) containing = d.nodes[containing].parent;
            const auto &cb = d.boxes[containing];
            if (s.left.unit != Unit::Auto) dx = cb.content_x + resolve(s.left,cb.content_width,s.font,d.root_font) - b.x;
            else if (s.right.unit != Unit::Auto) dx = cb.content_x + cb.content_width - resolve(s.right,cb.content_width,s.font,d.root_font) - b.width - b.x;
            if (s.top.unit != Unit::Auto) dy = cb.content_y + resolve(s.top,cb.content_height,s.font,d.root_font) - b.y;
            else if (s.bottom.unit != Unit::Auto) dy = cb.content_y + cb.content_height - resolve(s.bottom,cb.content_height,s.font,d.root_font) - b.height - b.y;
        }
        dx = clamp64(dx,-MaxDimension); dy = clamp64(dy,-MaxDimension);
        d.offset_x[i] = dx; d.offset_y[i] = dy;
        b.x = clamp64(int64_t(b.x)+dx,-MaxDimension); b.content_x = clamp64(int64_t(b.content_x)+dx,-MaxDimension);
        b.y = clamp64(int64_t(b.y)+dy,-MaxDimension); b.content_y = clamp64(int64_t(b.content_y)+dy,-MaxDimension);
    }
    for (uint32_t i = 0; i < d.fragment_count; ++i) {
        auto &f = d.fragments[i];
        f.x = clamp64(int64_t(f.x)+d.offset_x[f.node],-MaxDimension);
        f.y = clamp64(int64_t(f.y)+d.offset_y[f.node],-MaxDimension);
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
        if (is_control(d,f.node)) {
            const auto &b=d.boxes[f.node];
            clip=intersect(clip,{b.content_x,b.content_y,b.content_width,b.content_height});
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
