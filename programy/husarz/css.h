#pragma once
#include "html.h"
namespace Husarz {
// Bounded lexical scans share quote/comment/bracket handling. No regex engine.
class CssTokenizer {
  public:
    const char *s;
    size_t n, p{};
    CssTokenizer(const char *text, size_t size) : s(text), n(size) {}
    void whitespace() {
        for (;;) {
            while (p < n && space(s[p]))
                ++p;
            if (p + 1 < n && s[p] == '/' && s[p + 1] == '*') {
                p += 2;
                while (p + 1 < n && !(s[p] == '*' && s[p + 1] == '/'))
                    ++p;
                p = p + 1 < n ? p + 2 : n;
            } else
                break;
        }
    }
    size_t scan(const char *delimiters) {
        size_t start = p;
        char quote = 0;
        unsigned brackets = 0;
        while (p < n) {
            char c = s[p];
            if (quote) {
                if (c == '\\' && p + 1 < n) {
                    p += 2;
                    continue;
                }
                if (c == quote)
                    quote = 0;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (c == '(' || c == '[') {
                if (brackets < 32)
                    ++brackets;
                else {
                    p = n;
                    break;
                }
            } else if (c == ')' || c == ']') {
                if (brackets)
                    --brackets;
            } else if (!brackets) {
                bool end = false;
                for (const char *d = delimiters; *d; ++d)
                    if (c == *d)
                        end = true;
                if (end)
                    break;
            }
            ++p;
        }
        return start;
    }
};
inline Span trimmed(const char *s, size_t n) {
    while (n && space(*s)) {
        ++s;
        --n;
    }
    while (n && space(s[n - 1]))
        --n;
    return {s, n};
}
inline bool value_is(Span v, const char *s) { return equal_ci(v.data, v.size, s); }
inline bool number(Span v, int &out, size_t &consumed) {
    size_t i = 0;
    bool negative = false;
    if (i < v.size && (v.data[i] == '+' || v.data[i] == '-'))
        negative = v.data[i++] == '-';
    bool any = false;
    int64_t val = 0;
    while (i < v.size && v.data[i] >= '0' && v.data[i] <= '9') {
        any = true;
        val = val * 10 + v.data[i++] - '0';
        if (val > 8000000)
            val = 8000000;
    }
    int fraction = 0, mul = 100;
    if (i < v.size && v.data[i] == '.') {
        ++i;
        while (i < v.size && v.data[i] >= '0' && v.data[i] <= '9') {
            any = true;
            if (mul) {
                fraction += (v.data[i] - '0') * mul;
                mul /= 10;
            }
            ++i;
        }
    }
    if (!any)
        return false;
    int64_t fixed = val * 1000 + fraction;
    if (fixed > INT32_MAX)
        fixed = INT32_MAX;
    out = int(negative ? -fixed : fixed);
    consumed = i;
    return true;
}
inline bool css_length(Span v, Length &l, bool allow_negative = false, bool allow_number = false) {
    v = trimmed(v.data, v.size);
    if (value_is(v, "auto")) {
        l = {};
        return true;
    }
    int num;
    size_t n;
    if (!number(v, num, n) || (!allow_negative && num < 0))
        return false;
    Span suffix{v.data + n, v.size - n};
    Unit unit;
    if (!suffix.size && (num == 0 || allow_number))
        unit = allow_number ? Unit::Number : Unit::Px;
    else if (value_is(suffix, "px"))
        unit = Unit::Px;
    else if (value_is(suffix, "%"))
        unit = Unit::Percent;
    else if (value_is(suffix, "em"))
        unit = Unit::Em;
    else if (value_is(suffix, "rem"))
        unit = Unit::Rem;
    else
        return false;
    l = {num, unit};
    return true;
}
inline int hex(char c) {
    c = lower(c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
inline bool color(Span v, uint32_t &out, bool &transparent) {
    transparent = false;
    if (value_is(v, "transparent")) {
        transparent = true;
        out = 0;
        return true;
    }
    if (v.size && *v.data == '#' && (v.size == 4 || v.size == 7)) {
        uint32_t c = 0;
        for (size_t i = 1; i < v.size; ++i) {
            int h = hex(v.data[i]);
            if (h < 0)
                return false;
            c = (c << (v.size == 4 ? 8 : 4)) | uint32_t(v.size == 4 ? h * 17 : h);
        }
        out = c;
        return true;
    }
    struct C {
        const char *name;
        uint32_t value;
    };
    static constexpr C names[] = {{"black", 0},         {"white", 0xffffff},   {"red", 0xff0000},
                                  {"green", 0x008000},  {"blue", 0x0000ff},    {"yellow", 0xffff00},
                                  {"gray", 0x808080},   {"grey", 0x808080},    {"silver", 0xc0c0c0},
                                  {"navy", 0x000080},   {"teal", 0x008080},    {"purple", 0x800080},
                                  {"orange", 0xffa500}, {"maroon", 0x800000},  {"lime", 0x00ff00},
                                  {"aqua", 0x00ffff},   {"fuchsia", 0xff00ff}, {"olive", 0x808000}};
    for (const auto &c : names)
        if (value_is(v, c.name)) {
            out = c.value;
            return true;
        }
    if (v.size > 5 && equal_ci(v.data, 4, "rgb(") && v.data[v.size - 1] == ')') {
        CssTokenizer t(v.data + 4, v.size - 5);
        uint32_t c = 0;
        for (int i = 0; i < 3; ++i) {
            t.whitespace();
            size_t b = t.p;
            t.scan(",");
            Span part = trimmed(t.s + b, t.p - b);
            int num;
            size_t used;
            if (!number(part, num, used))
                return false;
            bool percent = used + 1 == part.size && part.data[used] == '%';
            if (used != part.size && !percent)
                return false;
            int channel = clamp64(int64_t(num) * (percent ? 255 : 1) / (percent ? 100000 : 1000), 0, 255);
            c = (c << 8) | uint32_t(channel);
            if (i < 2) {
                if (t.p == t.n || t.s[t.p] != ',')
                    return false;
                ++t.p;
            } else if (t.p != t.n)
                return false;
        }
        out = c;
        return true;
    }
    return false;
}
inline unsigned words(Span v, Span *out, unsigned cap) {
    unsigned n = 0;
    CssTokenizer t(v.data, v.size);
    while (t.p < t.n) {
        t.whitespace();
        if (t.p == t.n)
            break;
        size_t start = t.p;
        t.scan(" \t\r\n");
        if (n == cap)
            return cap + 1;
        out[n++] = {v.data + start, t.p - start};
    }
    return n;
}
struct SelectorPart {
    Span compound;
    char relation{' '};
};
struct Selector {
    SelectorPart parts[8];
    unsigned count{};
    int specificity{};
    bool valid{};
};
inline Selector selector(Span v) {
    Selector out;
    size_t p = 0;
    char relation = ' ';
    while (p < v.size) {
        while (p < v.size && space(v.data[p]))
            ++p;
        if (p == v.size)
            break;
        if (v.data[p] == '>') {
            if (!out.count)
                return out;
            relation = '>';
            ++p;
            while (p < v.size && space(v.data[p]))
                ++p;
            if (p == v.size)
                return out;
        }
        size_t start = p;
        int identifiers = 0;
        bool tag = true;
        while (p < v.size && !space(v.data[p]) && v.data[p] != '>') {
            char c = v.data[p];
            if (c == '*' && tag) {
                ++p;
                tag = false;
                continue;
            }
            if (c == '.' || c == '#' || c == ':') {
                tag = false;
                ++p;
                size_t b = p;
                while (p < v.size && ident(v.data[p]))
                    ++p;
                if (p == b)
                    return out;
                if (c == ':') {
                    if (!equal_ci(v.data + b, p - b, "first-child") &&
                        !equal_ci(v.data + b, p - b, "last-child"))
                        return out;
                    out.specificity += 256;
                } else
                    out.specificity += c == '#' ? 65536 : 256;
            } else if (tag && ident(c)) {
                while (p < v.size && ident(v.data[p]))
                    ++p;
                tag = false;
                ++out.specificity;
            } else
                return out;
            if (++identifiers > 16)
                return out;
        }
        if (out.count == 8 || p == start)
            return out;
        out.parts[out.count++] = {{v.data + start, p - start}, relation};
        relation = ' ';
    }
    out.valid = out.count > 0;
    return out;
}
inline bool compound(const Document &d, uint16_t id, Span v, uint32_t &budget) {
    uint32_t cost = uint32_t(v.size) + 1 + (id < d.node_count ? d.nodes[id].attr_count * 8 : 0);
    if (cost > budget) {
        budget = 0;
        return false;
    }
    budget -= cost;
    if (!id || d.nodes[id].is_text)
        return false;
    size_t p = 0;
    if (p < v.size && v.data[p] == '*')
        ++p;
    else if (p < v.size && ident(v.data[p])) {
        size_t start = p;
        while (p < v.size && ident(v.data[p]))
            ++p;
        if (!equal_ci(v.data + start, p - start, d.str(d.nodes[id].tag)))
            return false;
    }
    while (p < v.size) {
        char type = v.data[p++];
        size_t b = p;
        while (p < v.size && ident(v.data[p]))
            ++p;
        Span name{v.data + b, p - b};
        if (type == '#') {
            const char *a = d.attr(id, "id");
            if (length(a, MaxAttrLength) != name.size)
                return false;
            for (size_t i = 0; i < name.size; ++i)
                if (a[i] != name.data[i])
                    return false;
        } else if (type == '.') {
            const char *a = d.attr(id, "class");
            size_t n = length(a, MaxAttrLength), k = 0;
            if (n > budget) {
                budget = 0;
                return false;
            }
            budget -= uint32_t(n);
            bool found = false;
            while (k < n) {
                while (k < n && space(a[k]))
                    ++k;
                size_t s = k;
                while (k < n && !space(a[k]))
                    ++k;
                if (k - s == name.size) {
                    bool same = true;
                    for (size_t j = 0; j < name.size; ++j)
                        if (a[s + j] != name.data[j])
                            same = false;
                    if (same)
                        found = true;
                }
            }
            if (!found)
                return false;
        } else if (type == ':') {
            bool first = equal_ci(name.data, name.size, "first-child");
            uint16_t sib = first ? d.nodes[id].previous : d.nodes[id].next;
            while (sib && d.nodes[sib].is_text)
                sib = first ? d.nodes[sib].previous : d.nodes[sib].next;
            if (sib)
                return false;
        } else
            return false;
    }
    return true;
}
inline bool match_part(const Document &d, uint16_t id, const Selector &s, unsigned part, uint32_t &budget) {
    if (!budget)
        return false;
    --budget;
    if (!compound(d, id, s.parts[part].compound, budget))
        return false;
    if (!part)
        return true;
    uint16_t parent = d.nodes[id].parent;
    if (s.parts[part].relation == '>')
        return parent && match_part(d, parent, s, part - 1, budget);
    for (unsigned depth = 0; parent && depth < MaxDepth; ++depth, parent = d.nodes[parent].parent)
        if (match_part(d, parent, s, part - 1, budget))
            return true;
    return false;
}
inline bool matches(const Document &d, uint16_t id, const Selector &s, uint32_t &budget) {
    return s.valid && match_part(d, id, s, s.count - 1, budget);
}
inline void css_declarations(Document &d, Span input, uint16_t &first, uint16_t &count) {
    first = uint16_t(d.declaration_count);
    count = 0;
    CssTokenizer t(input.data, input.size);
    while (t.p < t.n) {
        t.whitespace();
        if (t.p == t.n)
            break;
        size_t start = t.p;
        t.scan(":;{}");
        Span name = trimmed(t.s + start, t.p - start);
        if (t.p == t.n)
            break;
        if (t.s[t.p] != ':') {
            if (t.s[t.p] == '{') {
                unsigned depth = 1;
                ++t.p;
                while (t.p < t.n && depth) {
                    if (t.s[t.p] == '{')
                        ++depth;
                    else if (t.s[t.p] == '}')
                        --depth;
                    ++t.p;
                }
            } else
                ++t.p;
            continue;
        }
        ++t.p;
        start = t.p;
        t.scan(";{}");
        Span value = trimmed(t.s + start, t.p - start);
        if (t.p < t.n && t.s[t.p] == '{') {
            unsigned depth = 1;
            ++t.p;
            while (t.p < t.n && depth) {
                if (t.s[t.p] == '{')
                    ++depth;
                else if (t.s[t.p] == '}')
                    --depth;
                ++t.p;
            }
            continue;
        }
        bool valid = name.size > 0 && name.size <= 64 && value.size > 0 && value.size <= MaxAttrLength;
        for (size_t i = 0; i < name.size; ++i)
            if (!ident(name.data[i]))
                valid = false;
        bool important = false;
        size_t bang = value.size;
        for (size_t i = 0; i < value.size; ++i)
            if (value.data[i] == '!')
                bang = i;
        if (bang < value.size) {
            Span tail = trimmed(value.data + bang + 1, value.size - bang - 1);
            if (value_is(tail, "important")) {
                important = true;
                value = trimmed(value.data, bang);
            } else
                valid = false;
        }
        if (valid) {
            if (d.declaration_count == MaxDeclarations) {
                d.truncated = true;
                break;
            }
            auto &decl = d.declarations[d.declaration_count++];
            decl = {d.store(name.data, name.size, 64, true), d.store(value.data, value.size, MaxAttrLength),
                    important};
            ++count;
        }
        if (t.p < t.n)
            ++t.p;
    }
}
inline Span clean_css(Document &d, const char *input, size_t size) {
    uint32_t off = d.store(input, size, 65536);
    if (!off)
        return {};
    char *s = d.arena + off;
    size_t n = length(s, 65536);
    char quote = 0;
    for (size_t i = 0; i < n; ++i) {
        if (quote) {
            if (s[i] == '\\' && i + 1 < n)
                ++i;
            else if (s[i] == quote)
                quote = 0;
        } else if (s[i] == '\'' || s[i] == '"')
            quote = s[i];
        else if (i + 1 < n && s[i] == '/' && s[i + 1] == '*') {
            s[i++] = ' ';
            s[i] = ' ';
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/'))
                s[i++] = ' ';
            if (i + 1 < n) {
                s[i++] = ' ';
                s[i] = ' ';
            } else
                s[i] = ' ';
        }
    }
    return {s, n};
}
inline void parse_css(Document &d, const char *css, size_t size, uint32_t order = 0) {
    if (size > 65536) {
        size = 65536;
        d.truncated = true;
    }
    Span clean = clean_css(d, css, size);
    css = clean.data;
    size = clean.size;
    CssTokenizer t(css, size);
    uint32_t local = 0;
    while (t.p < t.n) {
        t.whitespace();
        if (t.p == t.n)
            break;
        size_t start = t.p;
        t.scan("{;");
        if (t.p == t.n)
            break;
        if (t.s[t.p] == ';') {
            ++t.p;
            continue;
        }
        Span names = trimmed(t.s + start, t.p - start);
        ++t.p;
        size_t body = t.p;
        unsigned depth = 1;
        char quote = 0;
        while (t.p < t.n && depth) {
            char c = t.s[t.p++];
            if (quote) {
                if (c == '\\' && t.p < t.n)
                    ++t.p;
                else if (c == quote)
                    quote = 0;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (c == '{')
                ++depth;
            else if (c == '}')
                --depth;
        }
        size_t end = t.p - (depth == 0 ? 1 : 0);
        if (!names.size || names.data[0] == '@')
            continue;
        uint16_t first, count;
        css_declarations(d, {css + body, end - body}, first, count);
        size_t p = 0;
        while (p < names.size) {
            size_t b = p;
            while (p < names.size && names.data[p] != ',')
                ++p;
            Span sel = trimmed(names.data + b, p - b);
            if (p < names.size)
                ++p;
            if (sel.size > 256) {
                d.truncated = true;
                continue;
            }
            Selector parsed = selector(sel);
            if (!parsed.valid)
                continue;
            if (d.rule_count == MaxRules) {
                d.truncated = true;
                break;
            }
            d.rules[d.rule_count++] = {d.store(sel.data, sel.size, 256), first, count, order + local++};
        }
    }
    d.styles_dirty = true;
    ++d.revision;
}
enum Property {
    PDisplay,
    PVisibility,
    PColor,
    PBackground,
    PWidth,
    PHeight,
    PMinWidth,
    PMaxWidth,
    PMinHeight,
    PMaxHeight,
    PMarginT,
    PMarginR,
    PMarginB,
    PMarginL,
    PPaddingT,
    PPaddingR,
    PPaddingB,
    PPaddingL,
    PBorderT,
    PBorderR,
    PBorderB,
    PBorderL,
    PBorderStyle,
    PBorderColor,
    PFamily,
    PFontSize,
    PWeight,
    PItalic,
    PAlign,
    PDecoration,
    PLine,
    PWhite,
    POverflowX,
    POverflowY,
    PVertical,
    PRadius,
    POpacity,
    PBoxSizing,
    PCount
};
struct Winner {
    Span value{};
    uint64_t order{};
    int specificity{-1};
    bool important{};
};
inline int property(const char *name) {
    static constexpr const char *names[] = {"display",
                                            "visibility",
                                            "color",
                                            "background-color",
                                            "width",
                                            "height",
                                            "min-width",
                                            "max-width",
                                            "min-height",
                                            "max-height",
                                            "margin-top",
                                            "margin-right",
                                            "margin-bottom",
                                            "margin-left",
                                            "padding-top",
                                            "padding-right",
                                            "padding-bottom",
                                            "padding-left",
                                            "border-top-width",
                                            "border-right-width",
                                            "border-bottom-width",
                                            "border-left-width",
                                            "border-style",
                                            "border-color",
                                            "font-family",
                                            "font-size",
                                            "font-weight",
                                            "font-style",
                                            "text-align",
                                            "text-decoration",
                                            "line-height",
                                            "white-space",
                                            "overflow-x",
                                            "overflow-y",
                                            "vertical-align",
                                            "border-radius",
                                            "opacity",
                                            "box-sizing"};
    for (int i = 0; i < PCount; ++i)
        if (equal(name, names[i]))
            return i;
    return -1;
}
inline void offer(Winner *w, int p, Span value, int specificity, uint64_t order, bool important) {
    if (p < 0 || p >= PCount)
        return;
    auto &old = w[p];
    if (old.specificity < 0 || important > old.important ||
        (important == old.important &&
         (specificity > old.specificity || (specificity == old.specificity && order >= old.order))))
        old = {value, order, specificity, important};
}
inline bool one_of(Span v, const char *list) {
    size_t p = 0;
    while (list[p]) {
        size_t b = p;
        while (list[p] && list[p] != '|')
            ++p;
        if (v.size == p - b) {
            bool ok = true;
            for (size_t i = 0; i < v.size; ++i)
                if (lower(v.data[i]) != list[b + i])
                    ok = false;
            if (ok)
                return true;
        }
        if (list[p])
            ++p;
    }
    return false;
}
inline bool valid_value(int p, Span v) {
    if (value_is(v, "inherit") || value_is(v, "initial"))
        return true;
    Length l;
    uint32_t c;
    bool tr;
    int n;
    size_t used;
    if (p >= PWidth && p <= PMaxHeight)
        return css_length(v, l);
    if (p >= PMarginT && p <= PMarginL)
        return css_length(v, l, true);
    if (p >= PPaddingT && p <= PBorderL)
        return css_length(v, l) && l.unit != Unit::Auto;
    switch (p) {
    case PDisplay:
        return one_of(v, "inline|block|none|table|table-row|table-cell");
    case PVisibility:
        return one_of(v, "visible|hidden|collapse");
    case PColor:
    case PBackground:
    case PBorderColor:
        return color(v, c, tr) || (p == PBorderColor && value_is(v, "currentcolor"));
    case PBorderStyle:
        return one_of(v, "none|hidden|solid|dashed|dotted");
    case PFamily:
        return v.size > 0;
    case PFontSize:
        return (css_length(v, l) && l.unit != Unit::Auto) || one_of(v, "small|medium|large");
    case PWeight:
        return one_of(v, "normal|bold|bolder|lighter") ||
               (number(v, n, used) && used == v.size && n >= 100000 && n <= 900000);
    case PItalic:
        return one_of(v, "normal|italic|oblique");
    case PAlign:
        return one_of(v, "left|right|center|start|end");
    case PDecoration:
        return one_of(v, "none|underline");
    case PLine:
        return value_is(v, "normal") || (css_length(v, l, false, true) && l.unit != Unit::Auto);
    case PWhite:
        return one_of(v, "normal|pre|pre-wrap|nowrap");
    case POverflowX:
    case POverflowY:
        return one_of(v, "visible|hidden|auto|scroll");
    case PVertical:
        return one_of(v, "baseline|top|middle|bottom");
    case PRadius:
        return css_length(v, l) && l.unit != Unit::Auto;
    case POpacity:
        return number(v, n, used) && used == v.size;
    case PBoxSizing:
        return one_of(v, "border-box|content-box");
    default:
        return false;
    }
}
inline void offer_declaration(Document &d, Winner *winners, const Declaration &decl, int specificity,
                              uint64_t order) {
    const char *name = d.str(decl.name);
    Span v{d.str(decl.value), length(d.str(decl.value), MaxAttrLength)};
    int prop = property(name);
    if (prop >= 0) {
        if (valid_value(prop, v))
            offer(winners, prop, v, specificity, order, decl.important);
        return;
    }
    Span pieces[4];
    unsigned n = words(v, pieces, 4);
    int base = -1;
    if (equal(name, "margin"))
        base = PMarginT;
    else if (equal(name, "padding"))
        base = PPaddingT;
    else if (equal(name, "border-width"))
        base = PBorderT;
    if (base >= 0 && n >= 1 && n <= 4) {
        Length test;
        for (unsigned i = 0; i < n; ++i)
            if (!css_length(pieces[i], test, base == PMarginT) && !value_is(pieces[i], "inherit") &&
                !value_is(pieces[i], "initial"))
                return;
        unsigned map[4] = {0, n > 1 ? 1U : 0, n > 2 ? 2U : 0, n > 3 ? 3U : (n > 1 ? 1U : 0)};
        for (unsigned i = 0; i < 4; ++i)
            offer(winners, base + i, pieces[map[i]], specificity, order, decl.important);
        return;
    }
    if (equal(name, "overflow") && valid_value(POverflowX, v)) {
        offer(winners, POverflowX, v, specificity, order, decl.important);
        offer(winners, POverflowY, v, specificity, order, decl.important);
    }
    if (equal(name, "background")) {
        uint32_t c;
        bool trans;
        if (color(v, c, trans))
            offer(winners, PBackground, v, specificity, order, decl.important);
    }
    if (equal(name, "border")) {
        if (value_is(v, "inherit") || value_is(v, "initial")) {
            for (int p = PBorderT; p <= PBorderColor; ++p)
                offer(winners, p, v, specificity, order, decl.important);
            return;
        }
        // CSS shorthand resets omitted border components as well.
        Span width{"3px", 3}, style{"none", 4}, col{"currentcolor", 12};
        bool valid = n > 0 && n <= 3;
        for (unsigned i = 0; i < n && valid; ++i) {
            Length l;
            uint32_t c;
            bool tr;
            if (css_length(pieces[i], l))
                width = pieces[i];
            else if (value_is(pieces[i], "solid") || value_is(pieces[i], "none") ||
                     value_is(pieces[i], "dashed") || value_is(pieces[i], "dotted"))
                style = pieces[i];
            else if (color(pieces[i], c, tr) || value_is(pieces[i], "currentcolor"))
                col = pieces[i];
            else
                valid = false;
        }
        if (valid) {
            for (int p = PBorderT; p <= PBorderL; ++p)
                offer(winners, p, width, specificity, order, decl.important);
            offer(winners, PBorderStyle, style, specificity, order, decl.important);
            offer(winners, PBorderColor, col, specificity, order, decl.important);
        }
    }
}
inline Style defaults(const Document &d, uint16_t id, const Style &parent) {
    Style s;
    s.font = parent.font;
    s.line = parent.line;
    s.family = parent.family;
    s.color = parent.color;
    s.bold = parent.bold;
    s.italic = parent.italic;
    s.underline = parent.underline;
    s.white = parent.white;
    s.align = parent.align;
    s.visible = parent.visible;
    const char *t = d.str(d.nodes[id].tag);
    if (!id || d.tag(id, "html") || d.tag(id, "body") || block_tag(t) || d.tag(id, "form") ||
        d.tag(id, "hr") || d.tag(id, "thead") || d.tag(id, "tbody") || d.tag(id, "tfoot") || d.tag(id, "li"))
        s.display = Display::Block;
    if (d.tag(id, "head") || d.tag(id, "title") || d.tag(id, "meta") || d.tag(id, "link") ||
        d.tag(id, "base") || d.tag(id, "style") || d.tag(id, "script") || d.tag(id, "svg") ||
        d.tag(id, "template") || d.tag(id, "option"))
        s.display = Display::None;
    if (d.tag(id, "body"))
        for (auto &m : s.margin)
            m = {8000, Unit::Px};
    if (d.tag(id, "p") || d.tag(id, "blockquote") || d.tag(id, "pre")) {
        s.margin[0] = s.margin[2] = {1000, Unit::Em};
    }
    if (d.tag(id, "blockquote"))
        s.margin[1] = s.margin[3] = {32000, Unit::Px};
    if (d.tag(id, "ul") || d.tag(id, "ol")) {
        s.padding[3] = {24000, Unit::Px};
        s.margin[0] = s.margin[2] = {8000, Unit::Px};
    }
    if (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]) {
        const int sizes[] = {32, 24, 21, 18, 16, 14};
        s.font = sizes[t[1] - '1'];
        s.line = s.font * 5 / 4;
        s.bold = true;
        s.margin[0] = s.margin[2] = {500, Unit::Em};
    }
    if (d.tag(id, "strong") || d.tag(id, "b") || d.tag(id, "th"))
        s.bold = true;
    if (d.tag(id, "em") || d.tag(id, "i"))
        s.italic = true;
    if (d.tag(id, "u"))
        s.underline = true;
    if (d.tag(id, "a")) {
        s.color = 0x155ab5;
        s.underline = true;
    }
    if (d.tag(id, "small")) {
        s.font = parent.font * 4 / 5;
        s.line = s.font * 5 / 4;
    }
    if (d.tag(id, "pre"))
        s.white = WhiteSpace::Pre;
    if (d.tag(id, "table")) {
        s.display = Display::Table;
        s.margin[0] = s.margin[2] = {8000, Unit::Px};
    }
    if (d.tag(id, "tr"))
        s.display = Display::Row;
    if (d.tag(id, "td") || d.tag(id, "th")) {
        s.display = Display::Cell;
        for (auto &p : s.padding)
            p = {4000, Unit::Px};
    }
    if (d.tag(id, "textarea"))
        s.white = WhiteSpace::PreWrap;
    if (d.tag(id, "input") || d.tag(id, "button") || d.tag(id, "select") || d.tag(id, "textarea")) {
        s.color = 0x202020;
        s.has_background = true;
        s.background = 0xf3f3f3;
        s.border_style = 1;
        for (auto &b : s.border)
            b = 1;
        for (auto &p : s.padding)
            p = {4000, Unit::Px};
        if (d.tag(id, "input"))
            s.width = {160000, Unit::Px};
    }
    if (d.has_attr(id, "hidden") ||
        (d.tag(id, "input") && equal_ci(d.attr(id, "type"), length(d.attr(id, "type"), 64), "hidden")))
        s.display = Display::None;
    if (d.tag(id, "hr")) {
        s.height = {1000, Unit::Px};
        s.has_background = true;
        s.background = 0x999999;
        s.margin[0] = s.margin[2] = {8000, Unit::Px};
    }
    return s;
}
inline void copy_property(Style &s, const Style &from, int p) {
    if (p >= PMarginT && p <= PMarginL) {
        s.margin[p - PMarginT] = from.margin[p - PMarginT];
        return;
    }
    if (p >= PPaddingT && p <= PPaddingL) {
        s.padding[p - PPaddingT] = from.padding[p - PPaddingT];
        return;
    }
    if (p >= PBorderT && p <= PBorderL) {
        s.border[p - PBorderT] = from.border[p - PBorderT];
        return;
    }
    switch (p) {
    case PDisplay:
        s.display = from.display;
        break;
    case PVisibility:
        s.visible = from.visible;
        break;
    case PColor:
        s.color = from.color;
        break;
    case PBackground:
        s.background = from.background;
        s.has_background = from.has_background;
        break;
    case PWidth:
        s.width = from.width;
        break;
    case PHeight:
        s.height = from.height;
        break;
    case PMinWidth:
        s.min_width = from.min_width;
        break;
    case PMaxWidth:
        s.max_width = from.max_width;
        break;
    case PMinHeight:
        s.min_height = from.min_height;
        break;
    case PMaxHeight:
        s.max_height = from.max_height;
        break;
    case PBorderStyle:
        s.border_style = from.border_style;
        break;
    case PBorderColor:
        s.border_color = from.border_color;
        break;
    case PFamily:
        s.family = from.family;
        break;
    case PFontSize:
        s.font = from.font;
        break;
    case PWeight:
        s.bold = from.bold;
        break;
    case PItalic:
        s.italic = from.italic;
        break;
    case PAlign:
        s.align = from.align;
        break;
    case PDecoration:
        s.underline = from.underline;
        break;
    case PLine:
        s.line = from.line;
        break;
    case PWhite:
        s.white = from.white;
        break;
    case POverflowX:
        s.overflow_x = from.overflow_x;
        break;
    case POverflowY:
        s.overflow_y = from.overflow_y;
        break;
    case PVertical:
        s.vertical = from.vertical;
        break;
    case PRadius:
        s.radius_length = from.radius_length;
        s.radius = from.radius;
        break;
    case POpacity:
        s.opacity = from.opacity;
        break;
    case PBoxSizing:
        s.border_box = from.border_box;
        break;
    default:
        break;
    }
}
inline void apply_property(Style &s, const Style &parent, int p, Span v, int root, uint32_t offset) {
    if (value_is(v, "inherit")) {
        copy_property(s, parent, p);
        return;
    }
    if (value_is(v, "initial")) {
        Style initial;
        copy_property(s, initial, p);
        return;
    }
    Length l;
    bool tr = false;
    uint32_t c;
    if (p >= PMarginT && p <= PMarginL) {
        if (css_length(v, l, true))
            s.margin[p - PMarginT] = l;
        return;
    }
    if (p >= PPaddingT && p <= PPaddingL) {
        if (css_length(v, l) && l.unit != Unit::Auto)
            s.padding[p - PPaddingT] = l;
        return;
    }
    if (p >= PBorderT && p <= PBorderL) {
        if (css_length(v, l) && l.unit != Unit::Auto)
            s.border[p - PBorderT] = clamp64(resolve(l, 0, s.font, root), 0, 128);
        return;
    }
    switch (p) {
    case PDisplay:
        if (value_is(v, "block"))
            s.display = Display::Block;
        else if (value_is(v, "inline"))
            s.display = Display::Inline;
        else if (value_is(v, "none"))
            s.display = Display::None;
        else if (value_is(v, "table"))
            s.display = Display::Table;
        else if (value_is(v, "table-row"))
            s.display = Display::Row;
        else if (value_is(v, "table-cell"))
            s.display = Display::Cell;
        break;
    case PVisibility:
        if (value_is(v, "hidden") || value_is(v, "collapse"))
            s.visible = false;
        else if (value_is(v, "visible"))
            s.visible = true;
        break;
    case PColor:
        if (color(v, c, tr) && !tr)
            s.color = c;
        break;
    case PBackground:
        if (color(v, c, tr)) {
            s.background = c;
            s.has_background = !tr;
        }
        break;
    case PWidth:
    case PHeight:
    case PMinWidth:
    case PMaxWidth:
    case PMinHeight:
    case PMaxHeight:
        if (css_length(v, l)) {
            Length *a[] = {&s.width, &s.height, &s.min_width, &s.max_width, &s.min_height, &s.max_height};
            *a[p - PWidth] = l;
        }
        break;
    case PBorderStyle:
        if (value_is(v, "none") || value_is(v, "hidden"))
            s.border_style = 0;
        else if (value_is(v, "solid"))
            s.border_style = 1;
        else if (value_is(v, "dashed"))
            s.border_style = 2;
        else if (value_is(v, "dotted"))
            s.border_style = 3;
        break;
    case PBorderColor:
        if (value_is(v, "currentcolor"))
            s.border_color = s.color;
        else if (color(v, c, tr))
            s.border_color = c;
        break;
    case PFamily:
        s.family = offset;
        break;
    case PFontSize:
        if (css_length(v, l) && l.unit != Unit::Auto)
            s.font = clamp64(resolve(l, parent.font, parent.font, root), 6, MaxFont);
        else if (value_is(v, "small"))
            s.font = 13;
        else if (value_is(v, "medium"))
            s.font = 16;
        else if (value_is(v, "large"))
            s.font = 20;
        s.line = s.font * 5 / 4;
        break;
    case PWeight: {
        int n;
        size_t used;
        if (value_is(v, "bold") || value_is(v, "bolder"))
            s.bold = true;
        else if (value_is(v, "normal") || value_is(v, "lighter"))
            s.bold = false;
        else if (number(v, n, used) && used == v.size)
            s.bold = n >= 600000;
        break;
    }
    case PItalic:
        if (value_is(v, "italic") || value_is(v, "oblique"))
            s.italic = true;
        else if (value_is(v, "normal"))
            s.italic = false;
        break;
    case PAlign:
        if (value_is(v, "left") || value_is(v, "start"))
            s.align = Align::Left;
        else if (value_is(v, "center"))
            s.align = Align::Center;
        else if (value_is(v, "right") || value_is(v, "end"))
            s.align = Align::Right;
        break;
    case PDecoration:
        if (value_is(v, "none"))
            s.underline = false;
        else if (value_is(v, "underline"))
            s.underline = true;
        break;
    case PLine:
        if (value_is(v, "normal"))
            s.line = s.font * 5 / 4;
        else if (css_length(v, l, false, true) && l.unit != Unit::Auto)
            s.line = clamp64(l.unit == Unit::Number ? int64_t(l.value) * s.font / 1000
                                                    : resolve(l, s.font, s.font, root),
                             1, 1024);
        break;
    case PWhite:
        if (value_is(v, "normal"))
            s.white = WhiteSpace::Normal;
        else if (value_is(v, "pre"))
            s.white = WhiteSpace::Pre;
        else if (value_is(v, "pre-wrap"))
            s.white = WhiteSpace::PreWrap;
        else if (value_is(v, "nowrap"))
            s.white = WhiteSpace::NoWrap;
        break;
    case POverflowX:
    case POverflowY: {
        Overflow o;
        if (value_is(v, "visible"))
            o = Overflow::Visible;
        else if (value_is(v, "hidden"))
            o = Overflow::Hidden;
        else if (value_is(v, "auto"))
            o = Overflow::Auto;
        else if (value_is(v, "scroll"))
            o = Overflow::Scroll;
        else
            break;
        if (p == POverflowX)
            s.overflow_x = o;
        else
            s.overflow_y = o;
        break;
    }
    case PVertical:
        if (value_is(v, "top"))
            s.vertical = Vertical::Top;
        else if (value_is(v, "middle"))
            s.vertical = Vertical::Middle;
        else if (value_is(v, "bottom"))
            s.vertical = Vertical::Bottom;
        else if (value_is(v, "baseline"))
            s.vertical = Vertical::Baseline;
        break;
    case PRadius:
        if (css_length(v, l)) {
            s.radius_length = l;
            s.radius = clamp64(resolve(l, s.font, s.font, root), 0, 4096);
        }
        break;
    case POpacity: {
        int n;
        size_t used;
        if (number(v, n, used) && used == v.size)
            s.opacity = uint8_t(clamp64(int64_t(n) * 255 / 1000, 0, 255));
        break;
    }
    case PBoxSizing:
        if (value_is(v, "border-box"))
            s.border_box = true;
        else if (value_is(v, "content-box"))
            s.border_box = false;
        break;
    default:
        break;
    }
}
inline void compute_styles(Document &d, uint32_t text_color = 0x202020) {
    uint32_t budget = 4000000;
    Style initial;
    initial.color = text_color;
    d.root_font = 16;
    for (uint16_t id = 0; id < d.node_count; ++id) {
        const Node &node = d.nodes[id];
        const Style &parent = id ? d.styles[node.parent] : initial;
        if (node.is_text) {
            d.styles[id] = parent;
            d.styles[id].display = Display::Inline;
            continue;
        }
        Style s = defaults(d, id, parent);
        Winner winners[PCount];
        for (uint32_t r = 0; r < d.rule_count && budget; ++r) {
            const Rule &rule = d.rules[r];
            Selector sel = selector({d.str(rule.selector), length(d.str(rule.selector), 256)});
            if (!matches(d, id, sel, budget))
                continue;
            for (uint32_t n = 0; n < rule.count; ++n)
                offer_declaration(d, winners, d.declarations[rule.first + n], sel.specificity,
                                  uint64_t(rule.order) * (MaxDeclarations + 1) + n);
        }
        for (unsigned n = 0; n < node.inline_count; ++n)
            offer_declaration(d, winners, d.declarations[node.inline_first + n], (1 << 24),
                              (UINT64_C(1) << 60) + n);
        for (int p = 0; p < PCount; ++p)
            if (winners[p].specificity >= 0) {
                Span v = winners[p].value;
                if (p == PFontSize || p == PFamily || p == PWeight || p == PItalic)
                    apply_property(s, parent, p, v, d.root_font,
                                   (p == PFamily ? uint32_t(v.data - d.arena) : 0));
            }
        for (int p = 0; p < PCount; ++p)
            if (winners[p].specificity >= 0 && p != PFontSize && p != PFamily && p != PWeight &&
                p != PItalic) {
                Span v = winners[p].value;
                apply_property(s, parent, p, v, d.root_font, (p == PFamily ? uint32_t(v.data - d.arena) : 0));
            }
        if (d.tag(id, "html"))
            d.root_font = s.font;
        if (!s.border_style)
            for (auto &b : s.border)
                b = 0;
        s.paint_opacity = id ? uint8_t(unsigned(s.opacity) * parent.paint_opacity / 255) : s.opacity;
        d.styles[id] = s;
    }
    if (!budget)
        d.truncated = true;
    d.styles_dirty = false;
    d.style_revision = d.revision;
}
inline void collect_styles(Document &d) {
    for (uint16_t i = 1; i < d.node_count; ++i) {
        if (d.tag(i, "style"))
            parse_css(d, d.str(d.nodes[i].text), d.nodes[i].text_length, uint32_t(i) * 4096);
        const char *in = d.attr(i, "style");
        if (*in)
            css_declarations(d, clean_css(d, in, length(in, MaxAttrLength)), d.nodes[i].inline_first,
                             d.nodes[i].inline_count);
    }
}
} // namespace Husarz
