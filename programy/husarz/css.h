#pragma once
#include "css_colors.h"
#include "html.h"
#include "user_agent.h"
namespace Husarz {
// Bounded lexical scans share quote/comment/bracket handling. No regex engine.
class CssTokenizer {
  public:
    const char *s;
    size_t n, p{};
    CssTokenizer(const char *text, size_t size) : s(text), n(size) {}
    Work whitespace_work() {
        for (;;) {
            co_await WorkPoint{1};
            while (p < n && space(s[p])) {
                co_await WorkPoint{1};
                ++p;
            }
            if (p + 1 < n && s[p] == '/' && s[p + 1] == '*') {
                p += 2;
                while (p + 1 < n && !(s[p] == '*' && s[p + 1] == '/')) {
                    co_await WorkPoint{1};
                    ++p;
                }
                p = p + 1 < n ? p + 2 : n;
            } else
                break;
        }
    }
    inline void whitespace() { finish_work(whitespace_work()); }

    Work scan_work(const char *delimiters) {
        char quote = 0;
        unsigned brackets = 0;
        while (p < n) {
            co_await WorkPoint{1};
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
                for (const char *d = delimiters; *d; ++d) {
                    co_await WorkPoint{1};
                    if (c == *d)
                        end = true;
                }
                if (end)
                    break;
            }
            ++p;
        }
    }
    inline void scan(const char *delimiters) { finish_work(scan_work(delimiters)); }
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
inline bool css_simple_length(Span v, Length &l, bool allow_negative = false, bool allow_number = false) {
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
    else if (value_is(suffix, "vw"))
        unit = Unit::Vw;
    else if (value_is(suffix, "vh"))
        unit = Unit::Vh;
    else
        return false;
    l = {num, unit};
    return true;
}
#include "css_math.inc"
inline int hex(char c) {
    c = lower(c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
inline bool color(Span v, uint32_t &out, bool &transparent, uint8_t *alpha_out = nullptr) {
    v = trimmed(v.data, v.size);
    uint8_t alpha = 255;
    uint32_t result = 0;
    bool found = false;
    if (value_is(v, "transparent")) {
        alpha = 0;
        found = true;
    } else if (v.size && v.data[0] == '#' && (v.size == 4 || v.size == 5 || v.size == 7 || v.size == 9)) {
        bool short_hex = v.size <= 5;
        unsigned channels = (v.size == 5 || v.size == 9) ? 4 : 3;
        for (unsigned i = 0; i < channels; ++i) {
            unsigned pos = 1 + i * (short_hex ? 1 : 2);
            int hi = hex(v.data[pos]), lo = short_hex ? hi : hex(v.data[pos + 1]);
            if (hi < 0 || lo < 0)
                return false;
            unsigned channel = unsigned(hi * 16 + lo);
            if (i == 3)
                alpha = uint8_t(channel);
            else
                result = (result << 8) | channel;
        }
        found = true;
    } else {
        for (const auto &c : NamedColors)
            if (value_is(v, c.name)) {
                result = c.rgb;
                found = true;
                break;
            }
        bool rgba = v.size > 6 && equal_ci(v.data, 5, "rgba(");
        bool rgb = v.size > 5 && equal_ci(v.data, 4, "rgb(");
        bool hsla = v.size > 6 && equal_ci(v.data, 5, "hsla(");
        bool hsl = v.size > 5 && equal_ci(v.data, 4, "hsl(");
        if (!found && (rgba || rgb || hsla || hsl) && v.data[v.size - 1] == ')') {
            size_t prefix = rgba || hsla ? 5 : 4;
            CssTokenizer t(v.data + prefix, v.size - prefix - 1);
            int values[4]{};
            unsigned count = 0;
            bool slash = false, commas = false;
            while (t.p < t.n && count < 4) {
                t.whitespace();
                if (t.p == t.n)
                    break;
                size_t begin = t.p;
                t.scan(", /\t\r\n");
                Span part = trimmed(t.s + begin, t.p - begin);
                int num;
                size_t used;
                if (!number(part, num, used))
                    return false;
                bool percent = used + 1 == part.size && part.data[used] == '%';
                bool degrees = count == 0 && (hsl || hsla) && used + 3 == part.size &&
                               equal_ci(part.data + used, 3, "deg");
                if (used != part.size && !percent && !degrees)
                    return false;
                if (count == 3)
                    values[count] = clamp64(int64_t(num) * 255 / (percent ? 100000 : 1000), 0, 255);
                else if (hsl || hsla) {
                    if ((count > 0) != percent)
                        return false;
                    values[count] = count ? clamp64(num, 0, 100000) : num;
                } else
                    values[count] =
                        clamp64(int64_t(num) * (percent ? 255 : 1) / (percent ? 100000 : 1000), 0, 255);
                ++count;
                t.whitespace();
                if (t.p < t.n && (t.s[t.p] == ',' || t.s[t.p] == '/')) {
                    if (t.s[t.p] == '/')
                        slash = true;
                    else
                        commas = true;
                    ++t.p;
                } else if (t.p == begin)
                    return false;
            }
            t.whitespace();
            if (t.p != t.n || (count != 3 && count != 4) || (count == 4 && !slash && !commas))
                return false;
            if (hsl || hsla) {
                int hue = values[0] % 360000;
                if (hue < 0)
                    hue += 360000;
                int light = values[2], sat = values[1];
                int twice = 2 * light - 100000;
                if (twice < 0)
                    twice = -twice;
                int chroma = int(int64_t(100000 - twice) * sat / 100000);
                int triangle = hue % 120000 - 60000;
                if (triangle < 0)
                    triangle = -triangle;
                int x = int(int64_t(chroma) * (60000 - triangle) / 60000), m = light - chroma / 2;
                int r = 0, g = 0, b = 0;
                switch (hue / 60000) {
                case 0:
                    r = chroma;
                    g = x;
                    break;
                case 1:
                    r = x;
                    g = chroma;
                    break;
                case 2:
                    g = chroma;
                    b = x;
                    break;
                case 3:
                    g = x;
                    b = chroma;
                    break;
                case 4:
                    r = x;
                    b = chroma;
                    break;
                default:
                    r = chroma;
                    b = x;
                    break;
                }
                result = (uint32_t((r + m) * 255 / 100000) << 16) | (uint32_t((g + m) * 255 / 100000) << 8) |
                         uint32_t((b + m) * 255 / 100000);
            } else
                result = (uint32_t(values[0]) << 16) | (uint32_t(values[1]) << 8) | uint32_t(values[2]);
            if (count == 4)
                alpha = uint8_t(values[3]);
            found = true;
        }
    }
    if (!found)
        return false;
    out = result;
    transparent = !alpha;
    if (alpha_out)
        *alpha_out = alpha;
    return true;
}
inline bool gradient(Span v, uint32_t *colors, uint8_t *alpha, bool &horizontal) {
    v = trimmed(v.data, v.size);
    if (v.size && v.data[v.size - 1] == ',')
        --v.size;
    if (v.size < 18 || !equal_ci(v.data, 16, "linear-gradient(") || v.data[v.size - 1] != ')')
        return false;
    CssTokenizer t(v.data + 16, v.size - 17);
    unsigned count = 0;
    horizontal = false;
    while (t.p < t.n) {
        size_t begin = t.p;
        t.scan(",");
        Span part = trimmed(t.s + begin, t.p - begin);
        if (t.p < t.n)
            ++t.p;
        if (!count && (value_is(part, "to right") || value_is(part, "90deg"))) {
            horizontal = true;
            continue;
        }
        if (!count && (value_is(part, "to bottom") || value_is(part, "180deg")))
            continue;
        bool transparent = false;
        uint32_t c;
        uint8_t a;
        if (!color(part, c, transparent, &a)) {
            // A trailing stop position does not invalidate its color.
            size_t end = part.size;
            while (end && !space(part.data[end - 1]))
                --end;
            if (!end || !color(trimmed(part.data, end), c, transparent, &a))
                return false;
        }
        if (count == 2)
            return false;
        colors[count] = c;
        alpha[count++] = a;
    }
    return count == 2;
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
#include "css_grid.inc"
#include "css_shadow.inc"
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
inline bool span_equal(Span a, Span b) {
    if (a.size != b.size)
        return false;
    for (size_t i = 0; i < a.size; ++i)
        if (a.data[i] != b.data[i])
            return false;
    return true;
}
inline bool pseudo_name(Span n) {
    return value_is(n, "first-child") || value_is(n, "last-child") || value_is(n, "only-child") ||
           value_is(n, "first-of-type") || value_is(n, "last-of-type") || value_is(n, "nth-child") ||
           value_is(n, "nth-last-child") || value_is(n, "root") || value_is(n, "is") ||
           value_is(n, "where") || value_is(n, "not");
}
// Validate before matching; unsupported syntax never broadens a selector.
inline bool simple_syntax(Span v, int &specificity, unsigned argument = 0) {
    if (!v.size || v.size > MaxSelectorLength || argument > 4)
        return false;
    size_t p = 0;
    if (v.data[p] == '*')
        ++p;
    else if (ident(v.data[p])) {
        while (p < v.size && ident(v.data[p]))
            ++p;
        ++specificity;
    }
    unsigned count = 0;
    while (p < v.size && ++count <= 16) {
        char c = v.data[p++];
        if (c == '.' || c == '#' || c == ':') {
            size_t b = p;
            while (p < v.size && ident(v.data[p]))
                ++p;
            if (p == b)
                return false;
            Span name{v.data + b, p - b};
            if (c != ':') {
                specificity += c == '#' ? 65536 : 256;
                continue;
            }
            if (!pseudo_name(name))
                return false;
            bool functional_list = value_is(name, "not") || value_is(name, "is") || value_is(name, "where");
            bool function =
                functional_list || value_is(name, "nth-child") || value_is(name, "nth-last-child");
            if (function) {
                if (p == v.size || v.data[p++] != '(')
                    return false;
                b = p;
                unsigned level = 1;
                while (p < v.size) {
                    if (v.data[p] == '(')
                        ++level;
                    else if (v.data[p] == ')' && !--level)
                        break;
                    ++p;
                }
                if (p == v.size || p == b)
                    return false;
                Span arg = trimmed(v.data + b, p++ - b);
                if (functional_list) {
                    CssTokenizer list(arg.data, arg.size);
                    int maximum = 0;
                    bool any = false;
                    while (list.p < list.n) {
                        size_t begin = list.p;
                        list.scan(",");
                        Span item = trimmed(arg.data + begin, list.p - begin);
                        if (list.p < list.n)
                            ++list.p;
                        int score = 0;
                        if (simple_syntax(item, score, argument + 1)) {
                            any = true;
                            if (score > maximum)
                                maximum = score;
                        } else if (value_is(name, "not"))
                            return false;
                    }
                    if (!any)
                        return false;
                    if (!value_is(name, "where"))
                        specificity += maximum;
                } else {
                    if (arg.size > 64)
                        return false;
                    specificity += 256;
                }
            } else
                specificity += 256;
        } else if (c == '[') {
            size_t b = p;
            while (p < v.size && ident(v.data[p]))
                ++p;
            if (p == b)
                return false;
            while (p < v.size && space(v.data[p]))
                ++p;
            if (p == v.size)
                return false;
            if (v.data[p] != ']') {
                if (v.data[p] == '~' || v.data[p] == '^' || v.data[p] == '$' || v.data[p] == '*')
                    ++p;
                if (p == v.size || v.data[p++] != '=')
                    return false;
                while (p < v.size && space(v.data[p]))
                    ++p;
                char quote = p < v.size && (v.data[p] == '\'' || v.data[p] == '"') ? v.data[p++] : 0;
                b = p;
                while (p < v.size && (quote ? v.data[p] != quote : !space(v.data[p]) && v.data[p] != ']'))
                    ++p;
                if ((!quote && p == b) || p == v.size)
                    return false;
                if (quote)
                    ++p;
                while (p < v.size && space(v.data[p]))
                    ++p;
            }
            if (p == v.size || v.data[p++] != ']')
                return false;
            specificity += 256;
        } else
            return false;
    }
    return p == v.size && count <= 16;
}
inline Selector selector(Span v) {
    Selector out;
    size_t p = 0;
    char relation = ' ';
    while (p < v.size) {
        while (p < v.size && space(v.data[p]))
            ++p;
        if (p == v.size)
            break;
        if (v.data[p] == '>' || v.data[p] == '+' || v.data[p] == '~') {
            if (!out.count)
                return out;
            relation = v.data[p++];
            while (p < v.size && space(v.data[p]))
                ++p;
            if (p == v.size)
                return out;
        }
        size_t start = p;
        char quote = 0;
        unsigned nesting = 0;
        while (p < v.size) {
            char c = v.data[p];
            if (quote) {
                if (c == quote)
                    quote = 0;
            } else if (c == '\'' || c == '"')
                quote = c;
            else if (c == '[' || c == '(') {
                if (++nesting > 5)
                    return out;
            } else if (c == ']' || c == ')') {
                if (!nesting)
                    return out;
                --nesting;
            } else if (!nesting && (space(c) || c == '>' || c == '+' || c == '~'))
                break;
            ++p;
        }
        Span part{v.data + start, p - start};
        if (quote || nesting || out.count == 8 || !simple_syntax(part, out.specificity))
            return out;
        out.parts[out.count++] = {part, relation};
        relation = ' ';
    }
    out.valid = out.count != 0;
    return out;
}
inline bool nth(Span arg, int index) {
    char text[65]{};
    unsigned n = 0;
    for (size_t i = 0; i < arg.size; ++i)
        if (!space(arg.data[i])) {
            if (n == 64)
                return false;
            text[n++] = lower(arg.data[i]);
        }
    if (equal(text, "odd"))
        return index % 2 == 1;
    if (equal(text, "even"))
        return index % 2 == 0;
    size_t pos = 0;
    int a = 0, b = 0;
    bool neg = false;
    if (pos < n && (text[pos] == '+' || text[pos] == '-'))
        neg = text[pos++] == '-';
    size_t begin = pos;
    while (pos < n && text[pos] >= '0' && text[pos] <= '9') {
        if (a > 100000)
            return false;
        a = a * 10 + text[pos++] - '0';
    }
    if (pos == n)
        return begin != pos && index == (neg ? -a : a);
    if (text[pos++] != 'n')
        return false;
    if (begin == pos - 1)
        a = 1;
    if (neg)
        a = -a;
    if (pos < n) {
        if (text[pos] != '+' && text[pos] != '-')
            return false;
        bool minus = text[pos++] == '-';
        begin = pos;
        while (pos < n && text[pos] >= '0' && text[pos] <= '9') {
            if (b > 100000)
                return false;
            b = b * 10 + text[pos++] - '0';
        }
        if (pos != n || pos == begin)
            return false;
        if (minus)
            b = -b;
    }
    return a ? (index - b) % a == 0 && (index - b) / a >= 0 : index == b;
}
inline Work compound_work(const Document &d, uint16_t id, Span v, uint32_t &budget, bool &result) {
    if (!id || id >= d.node_count || d.nodes[id].is_text || v.size + 1 > budget) {
        result = false;
        co_return;
    }
    budget -= uint32_t(v.size) + 1;
    size_t p = 0;
    if (v.data[p] == '*')
        ++p;
    else if (ident(v.data[p])) {
        size_t b = p;
        while (p < v.size && ident(v.data[p])) {
            co_await WorkPoint{1};
            ++p;
        }
        if (!equal_ci(v.data + b, p - b, d.str(d.nodes[id].tag))) {
            result = false;
            co_return;
        }
    }
    while (p < v.size) {
        co_await WorkPoint{1};
        char type = v.data[p++];
        size_t b = p;
        while (p < v.size && ident(v.data[p])) {
            co_await WorkPoint{1};
            ++p;
        }
        Span name{v.data + b, p - b};
        if (type == '#' || type == '.') {
            const char *s = d.attr(id, type == '#' ? "id" : "class");
            size_t n = length(s, MaxAttrLength);
            if (n > budget) {
                budget = 0;
                {
                    result = false;
                    co_return;
                }
            }
            budget -= uint32_t(n);
            if (type == '#') {
                if (!span_equal({s, n}, name)) {
                    result = false;
                    co_return;
                }
            } else {
                bool found = false;
                size_t k = 0;
                while (k < n) {
                    co_await WorkPoint{1};
                    while (k < n && space(s[k])) {
                        co_await WorkPoint{1};
                        ++k;
                    }
                    size_t start = k;
                    while (k < n && !space(s[k])) {
                        co_await WorkPoint{1};
                        ++k;
                    }
                    if (span_equal({s + start, k - start}, name))
                        found = true;
                }
                if (!found) {
                    result = false;
                    co_return;
                }
            }
        } else if (type == '[') {
            const char *value = nullptr;
            const Node &node = d.nodes[id];

            for (unsigned i = 0; i < node.attr_count; ++i) {
                co_await WorkPoint{1};
                if (!budget) {
                    result = false;
                    co_return;
                }
                --budget;
                const auto &at = d.attrs[node.attr_start + i];
                if (equal_ci(name.data, name.size, d.str(at.name))) {
                    value = d.str(at.value);
                    break;
                }
            }
            if (!value) {
                result = false;
                co_return;
            }
            while (p < v.size && space(v.data[p])) {
                co_await WorkPoint{1};
                ++p;
            }
            if (v.data[p] == ']') {
                ++p;
                continue;
            }
            char op = v.data[p++];
            if (op != '=')
                ++p;
            while (p < v.size && space(v.data[p])) {
                co_await WorkPoint{1};
                ++p;
            }
            char quote = v.data[p] == '\'' || v.data[p] == '"' ? v.data[p++] : 0;
            b = p;
            while (p < v.size && (quote ? v.data[p] != quote : !space(v.data[p]) && v.data[p] != ']')) {
                co_await WorkPoint{1};
                ++p;
            }
            Span wanted{v.data + b, p - b};
            if (quote)
                ++p;
            while (p < v.size && space(v.data[p])) {
                co_await WorkPoint{1};
                ++p;
            }
            ++p;
            size_t n = length(value, MaxAttrLength);
            if (n > budget) {
                budget = 0;
                {
                    result = false;
                    co_return;
                }
            }
            budget -= uint32_t(n);
            bool found = false;
            if (op == '=')
                found = span_equal({value, n}, wanted);
            else if (wanted.size && wanted.size <= n) {
                if (op == '^')
                    found = span_equal({value, wanted.size}, wanted);
                else if (op == '$')
                    found = span_equal({value + n - wanted.size, wanted.size}, wanted);
                else if (op == '*') {
                    for (size_t j = 0; j + wanted.size <= n; ++j) {
                        co_await WorkPoint{1};
                        if (wanted.size > budget) {
                            budget = 0;
                            {
                                result = false;
                                co_return;
                            }
                        }
                        budget -= uint32_t(wanted.size);
                        if (span_equal({value + j, wanted.size}, wanted)) {
                            found = true;
                            break;
                        }
                    }
                } else if (op == '~') {
                    size_t k = 0;
                    while (k < n) {
                        co_await WorkPoint{1};
                        while (k < n && space(value[k])) {
                            co_await WorkPoint{1};
                            ++k;
                        }
                        size_t start = k;
                        while (k < n && !space(value[k])) {
                            co_await WorkPoint{1};
                            ++k;
                        }
                        if (span_equal({value + start, k - start}, wanted))
                            found = true;
                    }
                }
            }
            if (!found) {
                result = false;
                co_return;
            }
        } else if (type == ':') {
            Span arg{};
            if (p < v.size && v.data[p] == '(') {
                b = ++p;
                unsigned level = 1;
                while (p < v.size) {
                    co_await WorkPoint{1};
                    if (v.data[p] == '(')
                        ++level;
                    else if (v.data[p] == ')' && !--level)
                        break;
                    ++p;
                }
                arg = trimmed(v.data + b, p - b);
                ++p;
            }
            if (value_is(name, "root")) {
                if (!d.tag(id, "html")) {
                    result = false;
                    co_return;
                }
                continue;
            }
            if (value_is(name, "not") || value_is(name, "is") || value_is(name, "where")) {
                bool any = false;
                CssTokenizer list(arg.data, arg.size);
                while (list.p < list.n) {
                    co_await WorkPoint{1};
                    size_t begin = list.p;
                    co_await list.scan_work(",");
                    Span item = trimmed(arg.data + begin, list.p - begin);
                    if (list.p < list.n)
                        ++list.p;
                    int specificity = 0;
                    if (!simple_syntax(item, specificity, 1))
                        continue;
                    bool found = false;
                    co_await compound_work(d, id, item, budget, found);
                    if (found)
                        any = true;
                }
                if (!budget || (value_is(name, "not") ? any : !any)) {
                    result = false;
                    co_return;
                }
                continue;
            }
            bool reverse = value_is(name, "last-child") || value_is(name, "last-of-type") ||
                           value_is(name, "nth-last-child");
            bool of_type = value_is(name, "first-of-type") || value_is(name, "last-of-type");
            int index = 1;
            for (uint16_t sib = reverse ? d.nodes[id].next : d.nodes[id].previous; sib;
                 sib = reverse ? d.nodes[sib].next : d.nodes[sib].previous) {
                co_await WorkPoint{1};
                if (!budget) {
                    result = false;
                    co_return;
                }
                --budget;
                if (!d.nodes[sib].is_text &&
                    (!of_type || equal(d.str(d.nodes[id].tag), d.str(d.nodes[sib].tag))))
                    ++index;
            }
            if (value_is(name, "nth-child") || value_is(name, "nth-last-child")) {
                if (!nth(arg, index)) {
                    result = false;
                    co_return;
                }
            } else if (index != 1) {
                result = false;
                co_return;
            }
            if (value_is(name, "only-child"))
                for (uint16_t sib = d.nodes[id].next; sib; sib = d.nodes[sib].next) {
                    co_await WorkPoint{1};
                    if (!budget) {
                        result = false;
                        co_return;
                    }
                    --budget;
                    if (!d.nodes[sib].is_text) {
                        result = false;
                        co_return;
                    }
                }
        } else {
            result = false;
            co_return;
        }
    }
    {
        result = true;
        co_return;
    }
}
inline bool compound(const Document &d, uint16_t id, Span v, uint32_t &budget) {
    bool result{};
    finish_work(compound_work(d, id, v, budget, result));
    return result;
}

// Fast complete grammar for positive tag/id/class compounds. Attribute and
// pseudo selectors retain the resumable reference path below.
inline bool simple_compound_match(const Document &d, uint16_t id, Span v, uint32_t &budget, bool &result) {
    for (size_t i = 0; i < v.size; ++i)
        if (!ident(v.data[i]) && v.data[i] != '*' && v.data[i] != '.' && v.data[i] != '#')
            return false;
    result = false;
    if (!id || id >= d.node_count || d.nodes[id].is_text || v.size + 1 > budget)
        return true;
    budget -= uint32_t(v.size) + 1;
    size_t p = 0;
    if (v.data[p] == '*')
        ++p;
    else if (ident(v.data[p])) {
        size_t b = p;
        while (p < v.size && ident(v.data[p]))
            ++p;
        if (!equal_ci(v.data + b, p - b, d.str(d.nodes[id].tag)))
            return true;
    }
    while (p < v.size) {
        char type = v.data[p++];
        size_t b = p;
        while (p < v.size && ident(v.data[p]))
            ++p;
        Span name{v.data + b, p - b};
        const char *value = d.attr(id, type == '#' ? "id" : "class");
        size_t n = length(value, MaxAttrLength);
        if (n > budget) {
            budget = 0;
            return true;
        }
        budget -= uint32_t(n);
        if (type == '#') {
            if (!span_equal({value, n}, name))
                return true;
        } else {
            bool found = false;
            size_t k = 0;
            while (k < n) {
                while (k < n && space(value[k]))
                    ++k;
                size_t start = k;
                while (k < n && !space(value[k]))
                    ++k;
                if (span_equal({value + start, k - start}, name))
                    found = true;
            }
            if (!found)
                return true;
        }
    }
    result = true;
    return true;
}
inline Work match_part_work(const Document &d, uint16_t id, const Selector &s, unsigned part,
                            uint32_t &budget, bool &result) {
    if (!budget) {
        result = false;
        co_return;
    }
    --budget;
    bool accepted = false;
    auto v = s.parts[part].compound;
    unsigned classes = unsigned(length(d.attr(id, "class"), MaxAttrLength)),
             name = unsigned(length(d.attr(id, "id"), MaxAttrLength));
    unsigned bytes = (classes > name ? classes : name) + 64;
    unsigned estimate = unsigned(v.size) * bytes / 64 + 16;
    bool simple = true;
    for (size_t i = 0; i < v.size; ++i)
        if (!ident(v.data[i]) && v.data[i] != '*' && v.data[i] != '.' && v.data[i] != '#') {
            simple = false;
            break;
        }
    if (simple && estimate <= MaxSliceWork) {
        co_await WorkPoint{estimate};
        simple_compound_match(d, id, v, budget, accepted);
    } else
        co_await compound_work(d, id, v, budget, accepted);
    if (!accepted) {
        result = false;
        co_return;
    }
    if (!part) {
        result = true;
        co_return;
    }
    char rel = s.parts[part].relation;
    uint16_t candidate = rel == '+' || rel == '~' ? d.nodes[id].previous : d.nodes[id].parent;
    for (unsigned steps = 0; candidate && steps < MaxNodes && budget; ++steps) {
        co_await WorkPoint{1};
        if (!d.nodes[candidate].is_text) {
            bool found = false;
            co_await match_part_work(d, candidate, s, part - 1, budget, found);
            if (found) {
                result = true;
                co_return;
            }
            if (rel == '+' || rel == '>') {
                result = false;
                co_return;
            }
        }
        candidate = rel == '+' || rel == '~' ? d.nodes[candidate].previous : d.nodes[candidate].parent;
    }
    {
        result = false;
        co_return;
    }
}
inline bool match_part(const Document &d, uint16_t id, const Selector &s, unsigned part, uint32_t &budget) {
    bool result{};
    finish_work(match_part_work(d, id, s, part, budget, result));
    return result;
}

inline Work matches_work(const Document &d, uint16_t id, const Selector &s, uint32_t &budget, bool &result) {
    result = false;
    if (s.valid)
        co_await match_part_work(d, id, s, s.count - 1, budget, result);
}
inline bool matches(const Document &d, uint16_t id, const Selector &s, uint32_t &budget) {
    bool result{};
    finish_work(matches_work(d, id, s, budget, result));
    return result;
}

inline Work css_declarations_work(Document &d, Span input, uint16_t &first, uint16_t &count) {
    first = uint16_t(d.declaration_count);
    count = 0;
    CssTokenizer t(input.data, input.size);
    while (t.p < t.n) {
        HUSARZ_PROGRESS(Css, t.p, t.n);
        co_await WorkPoint{1};
        co_await t.whitespace_work();
        if (t.p == t.n)
            break;
        size_t start = t.p;
        co_await t.scan_work(":;{}");
        Span name = trimmed(t.s + start, t.p - start);
        if (t.p == t.n)
            break;
        if (t.s[t.p] != ':') {
            if (t.s[t.p] == '{') {
                unsigned depth = 1;
                ++t.p;
                while (t.p < t.n && depth) {
                    co_await WorkPoint{1};
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
        co_await t.scan_work(";{}");
        Span value = trimmed(t.s + start, t.p - start);
        if (t.p < t.n && t.s[t.p] == '{') {
            unsigned depth = 1;
            ++t.p;
            while (t.p < t.n && depth) {
                co_await WorkPoint{1};
                if (t.s[t.p] == '{')
                    ++depth;
                else if (t.s[t.p] == '}')
                    --depth;
                ++t.p;
            }
            continue;
        }
        bool valid =
            name.size > 0 && name.size <= MaxVariableName && value.size > 0 && value.size <= MaxAttrLength;
        for (size_t i = 0; i < name.size; ++i) {
            co_await WorkPoint{1};
            if (!ident(name.data[i]))
                valid = false;
        }
        bool important = false;
        size_t bang = value.size;
        for (size_t i = 0; i < value.size; ++i) {
            co_await WorkPoint{1};
            if (value.data[i] == '!')
                bang = i;
        }
        if (bang < value.size) {
            Span tail = trimmed(value.data + bang + 1, value.size - bang - 1);
            if (value_is(tail, "important")) {
                important = true;
                value = trimmed(value.data, bang);
            } else
                valid = false;
        }
        if (valid)
            for (size_t i = 0; i + 2 <= value.size; ++i) {
                co_await WorkPoint{1};
                if (equal_ci(value.data + i, 2, "vw") || equal_ci(value.data + i, 2, "vh"))
                    d.has_viewport_units = true;
            }
        if (valid) {
            if (d.declaration_count == MaxDeclarations) {
                d.limit(LimitDeclarations);
                break;
            }
            auto &decl = d.declarations[d.declaration_count++];
            decl = {d.store(name.data, name.size, MaxVariableName,
                            !(name.size > 1 && name.data[0] == '-' && name.data[1] == '-')),
                    d.store(value.data, value.size, MaxAttrLength), important};
            ++count;
        }
        if (t.p < t.n)
            ++t.p;
    }
}
inline void css_declarations(Document &d, Span input, uint16_t &first, uint16_t &count) {
    finish_work(css_declarations_work(d, input, first, count));
}

inline Work clean_css_work(Document &d, const char *input, size_t size, Span &result) {
    uint32_t off = 0;
    co_await store_string_work(d, input, size, MaxStylesheetBytes, off);
    if (!off) {
        result = {};
        co_return;
    }
    char *s = d.arena + off;
    size_t n = length(s, MaxStylesheetBytes);
    if (n >= 3 && uint8_t(s[0]) == 0xef && uint8_t(s[1]) == 0xbb && uint8_t(s[2]) == 0xbf)
        s[0] = s[1] = s[2] = ' ';
    char quote = 0;
    for (size_t i = 0; i < n; ++i) {
        co_await WorkPoint{1};
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
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) {
                co_await WorkPoint{1};
                s[i++] = ' ';
            }
            if (i + 1 < n) {
                s[i++] = ' ';
                s[i] = ' ';
            } else
                s[i] = ' ';
        }
    }
    {
        result = {s, n};
        co_return;
    }
}
inline Span clean_css(Document &d, const char *input, size_t size) {
    Span result{};
    finish_work(clean_css_work(d, input, size, result));
    return result;
}

inline bool one_of_media_type(Span v) { return value_is(v, "screen") || value_is(v, "all"); }
inline bool media_condition(Span v, int &minimum, int &maximum) {
    if (v.size > 512)
        return false;
    size_t p = 0;
    auto ws = [&]() {
        while (p < v.size && space(v.data[p]))
            ++p;
    };
    ws();
    if (p < v.size && v.data[p] != '(') {
        size_t b = p;
        while (p < v.size && ident(v.data[p]))
            ++p;
        if (!one_of_media_type({v.data + b, p - b}))
            return false;
        ws();
        if (p == v.size)
            return true;
        if (p + 3 > v.size || !equal_ci(v.data + p, 3, "and"))
            return false;
        p += 3;
    }
    while (p < v.size) {
        ws();
        if (p == v.size || v.data[p++] != '(')
            return false;
        size_t b = p;
        while (p < v.size && v.data[p] != ':')
            ++p;
        if (p == v.size)
            return false;
        Span key = trimmed(v.data + b, p - b);
        b = ++p;
        while (p < v.size && v.data[p] != ')')
            ++p;
        if (p == v.size)
            return false;
        Span value = trimmed(v.data + b, p - b);
        ++p;
        Length l;
        if (!css_length(value, l) || l.unit != Unit::Px)
            return false;
        int px = resolve(l, 0, 16, 16);
        if (value_is(key, "min-width")) {
            if (px > minimum)
                minimum = px;
        } else if (value_is(key, "max-width")) {
            if (px < maximum)
                maximum = px;
        } else
            return false;
        ws();
        if (p == v.size)
            return true;
        if (p + 3 > v.size || !equal_ci(v.data + p, 3, "and"))
            return false;
        p += 3;
    }
    return false;
}
inline bool supports_condition(Span);
inline Work parse_css_rules_work(Document &d, Span clean, uint32_t order, uint32_t base_url, bool user_agent,
                                 uint32_t &local, unsigned nesting = 0, int media_min = 0,
                                 int media_max = MaxDimension) {
    const char *css = clean.data;
    size_t size = clean.size;
    CssTokenizer t(css, size);
    while (t.p < t.n) {
        HUSARZ_PROGRESS(Css, t.p, t.n);
        co_await WorkPoint{1};
        co_await t.whitespace_work();
        if (t.p == t.n)
            break;
        size_t start = t.p;
        co_await t.scan_work("{;");
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
            co_await WorkPoint{1};
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
        if (!names.size)
            continue;
        if (names.data[0] == '@') {
            bool enable = false;
            int minimum = media_min, maximum = media_max;
            if (names.size >= 6 && equal_ci(names.data, 6, "@media")) {
                d.has_media = true;
                enable = media_condition(trimmed(names.data + 6, names.size - 6), minimum, maximum);
            } else if (names.size >= 9 && equal_ci(names.data, 9, "@supports"))
                enable = supports_condition(trimmed(names.data + 9, names.size - 9));
            else if (value_is(names, "@font-face")) {
                uint16_t first = 0, count = 0;
                co_await css_declarations_work(d, {css + body, end - body}, first, count);
                if (d.font_count < MaxFontFaces) {
                    auto &face = d.fonts[d.font_count++];
                    face.base_url = base_url;
                    for (unsigned i = 0; i < count; ++i) {
                        co_await WorkPoint{1};
                        const auto &decl = d.declarations[first + i];
                        if (equal(d.str(decl.name), "font-family"))
                            face.family = decl.value;
                        else if (equal(d.str(decl.name), "src"))
                            face.sources = decl.value;
                    }
                } else
                    d.limit(LimitStylesheets);
            } else
                ++d.unknown_at_rules;
            if (enable) {
                if (nesting >= MaxCssNesting)
                    d.limit(LimitCssDepth);
                else
                    co_await parse_css_rules_work(d, {css + body, end - body}, order, base_url, user_agent,
                                                  local, nesting + 1, minimum, maximum);
            }
            continue;
        }
        uint16_t first, count;
        co_await css_declarations_work(d, {css + body, end - body}, first, count);
        for (unsigned i = 0; i < count; ++i) {
            co_await WorkPoint{1};
            d.declarations[first + i].base_url = base_url;
            d.declarations[first + i].user_agent = user_agent;
        }
        size_t p = 0;
        while (p < names.size) {
            co_await WorkPoint{1};
            size_t b = p;
            CssTokenizer list(names.data, names.size);
            list.p = p;
            co_await list.scan_work(",");
            p = list.p;
            Span sel = trimmed(names.data + b, p - b);
            if (p < names.size)
                ++p;
            if (sel.size > MaxSelectorLength) {
                d.limit(LimitSelector);
                continue;
            }
            uint8_t pseudo = 0;
            for (unsigned kind = 1; kind <= 2; ++kind) {
                const char *suffix = kind == 1 ? "before" : "after";
                size_t n = kind == 1 ? 6 : 5;
                if (sel.size > n && equal_ci(sel.data + sel.size - n, n, suffix) &&
                    sel.data[sel.size - n - 1] == ':') {
                    pseudo = uint8_t(kind);
                    sel.size -= n + 1;
                    if (sel.size && sel.data[sel.size - 1] == ':')
                        --sel.size;
                    sel = trimmed(sel.data, sel.size);
                    break;
                }
            }
            if (pseudo)
                d.has_pseudo = true;
            Selector parsed = selector(sel);
            if (!parsed.valid) {
                ++d.unsupported_selectors;
                continue;
            }
            if (d.rule_count == MaxRules) {
                d.limit(LimitRules);
                break;
            }
            d.rules[d.rule_count++] = {d.store(sel.data, sel.size, MaxSelectorLength),
                                       first,
                                       count,
                                       order + local++,
                                       media_min,
                                       media_max,
                                       pseudo};
        }
    }
}
inline Work parse_css_work(Document &d, const char *css, size_t size, uint32_t order = 0,
                           uint32_t base_url = 0, bool user_agent = false) {
    HUSARZ_SCOPE(d, Css);
    if (size > MaxStylesheetBytes) {
        size = MaxStylesheetBytes;
        d.limit(LimitCssBytes);
    }
    Span clean;
    co_await clean_css_work(d, css, size, clean);
    uint32_t local = 0;
    co_await parse_css_rules_work(d, clean, order, base_url, user_agent, local);
    d.styles_dirty = true;
    ++d.revision;
}
inline void parse_css(Document &d, const char *css, size_t size, uint32_t order = 0, uint32_t base_url = 0,
                      bool user_agent = false) {
    finish_work(parse_css_work(d, css, size, order, base_url, user_agent));
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
    PLetter,
    PWord,
    PTransform,
    PPosition,
    PLeft,
    PTop,
    PRight,
    PBottom,
    PList,
    PBorderSpacing,
    PCollapse,
    PBackgroundImage,
    PBackgroundSize,
    PBackgroundPosition,
    PBackgroundRepeat,
    PBorderCT,
    PBorderCR,
    PBorderCB,
    PBorderCL,
    PBorderST,
    PBorderSR,
    PBorderSB,
    PBorderSL,
    PShadow,
    PGridColumns,
    PGradient,
    PObjectFit,
    PContent,
    PFlexDirection,
    PJustify,
    PAlignItems,
    PAlignSelf,
    PFlexWrap,
    PFlexGrow,
    PFlexShrink,
    PFlexBasis,
    PRowGap,
    PColumnGap,
    PCount
};
struct Winner {
    Span value{};
    uint64_t order{};
    int specificity{-1};
    bool important{};
    uint32_t base_url{};
    bool user_agent{};
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
                                            "box-sizing",
                                            "letter-spacing",
                                            "word-spacing",
                                            "text-transform",
                                            "position",
                                            "left",
                                            "top",
                                            "right",
                                            "bottom",
                                            "list-style-type",
                                            "border-spacing",
                                            "border-collapse",
                                            "background-image",
                                            "background-size",
                                            "background-position",
                                            "background-repeat",
                                            "border-top-color",
                                            "border-right-color",
                                            "border-bottom-color",
                                            "border-left-color",
                                            "border-top-style",
                                            "border-right-style",
                                            "border-bottom-style",
                                            "border-left-style",
                                            "box-shadow",
                                            "grid-template-columns",
                                            "-husarz-gradient",
                                            "object-fit",
                                            "content",
                                            "flex-direction",
                                            "justify-content",
                                            "align-items",
                                            "align-self",
                                            "flex-wrap",
                                            "flex-grow",
                                            "flex-shrink",
                                            "flex-basis",
                                            "row-gap",
                                            "column-gap"};
    for (int i = 0; i < PCount; ++i)
        if (equal(name, names[i]))
            return i;
    return -1;
}
inline void offer(Winner *w, int p, Span value, int specificity, uint64_t order, bool important,
                  uint32_t base_url = 0, bool user_agent = false) {
    if (p < 0 || p >= PCount)
        return;
    auto &old = w[p];
    unsigned rank = important ? (user_agent ? 3U : 2U) : (user_agent ? 0U : 1U);
    unsigned old_rank = old.important ? (old.user_agent ? 3U : 2U) : (old.user_agent ? 0U : 1U);
    if (old.specificity < 0 || rank > old_rank ||
        (rank == old_rank &&
         (specificity > old.specificity || (specificity == old.specificity && order >= old.order))))
        old = {value, order, specificity, important, base_url, user_agent};
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
    if (value_is(v, "inherit") || value_is(v, "initial") || value_is(v, "unset"))
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
    if (p >= PBorderCT && p <= PBorderCL)
        return color(v, c, tr) || value_is(v, "currentcolor");
    if (p >= PBorderST && p <= PBorderSL)
        return one_of(v, "none|hidden|solid|dashed|dotted");
    switch (p) {
    case PShadow: {
        Style s;
        return shadow(v, s);
    }
    case PGridColumns: {
        int widths[MaxGridColumns];
        unsigned count;
        return grid_columns(v, 1000, 0, 16, 16, widths, count);
    }
    case PContent:
        return value_is(v, "none") || value_is(v, "normal") ||
               (v.size >= 2 && v.size <= MaxVariableName &&
                ((v.data[0] == '\'' && v.data[v.size - 1] == '\'') ||
                 (v.data[0] == '"' && v.data[v.size - 1] == '"')));
    case PObjectFit:
        return one_of(v, "fill|contain|cover");
    case PGradient: {
        uint32_t colors[2];
        uint8_t a[2];
        bool h;
        return gradient(v, colors, a, h);
    }
    case PFlexDirection:
        return one_of(v, "row|row-reverse|column|column-reverse");
    case PJustify:
        return one_of(v, "flex-start|start|flex-end|end|center|space-between|space-"
                         "around|space-evenly");
    case PAlignItems:
        return one_of(v, "stretch|flex-start|start|flex-end|end|center");
    case PAlignSelf:
        return one_of(v, "auto|stretch|flex-start|start|flex-end|end|center");
    case PFlexWrap:
        return one_of(v, "nowrap|wrap");
    case PFlexGrow:
    case PFlexShrink:
        return number(v, n, used) && used == v.size && n >= 0 && n <= 1000000;
    case PFlexBasis:
    case PRowGap:
    case PColumnGap:
        return css_length(v, l);
    case PDisplay:
        return one_of(v, "grid|inline-grid|flex|inline-flex|inline|inline-block|block|none|table|table-row-"
                         "group|table-header-group|table-footer-group|table-row|table-cell");
    case PVisibility:
        return one_of(v, "visible|hidden|collapse");
    case PColor:
    case PBackground:
    case PBorderColor:
        return color(v, c, tr) || value_is(v, "currentcolor");
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
    case PBackgroundImage:
        return valid_value(PGradient, v) || value_is(v, "none") ||
               (v.size > 5 && equal_ci(v.data, 4, "url(") && v.data[v.size - 1] == ')');
    case PBackgroundSize:
    case PBackgroundPosition: {
        Span parts[2];
        unsigned count = words(v, parts, 2);
        if (!count || count > 2)
            return false;
        for (unsigned i = 0; i < count; ++i)
            if (!css_length(parts[i], l, p == PBackgroundPosition) &&
                !one_of(parts[i], p == PBackgroundSize ? "cover|contain" : "left|right|top|bottom|center"))
                return false;
        return true;
    }
    case PBackgroundRepeat:
        return one_of(v, "repeat|repeat-x|repeat-y|no-repeat");
    case PLetter:
    case PWord:
        return value_is(v, "normal") ||
               (css_length(v, l, true) && l.unit != Unit::Auto && l.unit != Unit::Percent);
    case PTransform:
        return one_of(v, "none|uppercase|lowercase|capitalize");
    case PPosition:
        return one_of(v, "static|relative|absolute");
    case PLeft:
    case PTop:
    case PRight:
    case PBottom:
        return css_length(v, l, true);
    case PList:
        return one_of(v, "disc|circle|square|decimal|lower-alpha|upper-alpha|lower-"
                         "roman|upper-roman|none");
    case PBorderSpacing:
        return css_length(v, l) && l.unit != Unit::Auto && l.unit != Unit::Percent;
    case PCollapse:
        return one_of(v, "collapse|separate");
    case PBoxSizing:
        return one_of(v, "border-box|content-box");
    default:
        return false;
    }
}
inline bool supports_condition(Span v) {
    v = trimmed(v.data, v.size);
    bool negate = false;
    if (v.size > 4 && equal_ci(v.data, 3, "not")) {
        negate = true;
        v = trimmed(v.data + 3, v.size - 3);
    }
    if (v.size < 5 || v.size > 256 || v.data[0] != '(' || v.data[v.size - 1] != ')')
        return false;
    size_t p = 1;
    while (p < v.size && v.data[p] != ':')
        ++p;
    if (p == v.size)
        return false;
    Span name = trimmed(v.data + 1, p - 1), value = trimmed(v.data + p + 1, v.size - p - 2);
    char text[97]{};
    if (name.size > 96)
        return false;
    for (size_t i = 0; i < name.size; ++i)
        text[i] = lower(name.data[i]);
    int prop = property(text);
    bool accepted = prop >= 0 && valid_value(prop, value);
    return negate ? !accepted : accepted;
}
inline void offer_declaration(Document &d, Winner *winners, const Declaration &decl, int specificity,
                              uint64_t order) {
    const char *name = d.str(decl.name);
    Span v{d.str(decl.value), length(d.str(decl.value), MaxAttrLength)};
    if (equal(name, "gap")) {
        Span parts[2];
        unsigned n = words(v, parts, 2);
        if (n && n <= 2 && valid_value(PRowGap, parts[0]) && valid_value(PColumnGap, parts[n - 1])) {
            offer(winners, PRowGap, parts[0], specificity, order, decl.important, decl.base_url,
                  decl.user_agent);
            offer(winners, PColumnGap, parts[n - 1], specificity, order, decl.important, decl.base_url,
                  decl.user_agent);
        }
        return;
    }
    if (equal(name, "flex")) {
        Span parts[3];
        unsigned n = words(v, parts, 3);
        if (value_is(v, "none")) {
            offer(winners, PFlexGrow, {"0", 1}, specificity, order, decl.important);
            offer(winners, PFlexShrink, {"0", 1}, specificity, order, decl.important);
            return;
        }
        if (value_is(v, "auto")) {
            offer(winners, PFlexGrow, {"1", 1}, specificity, order, decl.important);
            offer(winners, PFlexShrink, {"1", 1}, specificity, order, decl.important);
            return;
        }
        if (n && n <= 3 && valid_value(PFlexGrow, parts[0])) {
            offer(winners, PFlexGrow, parts[0], specificity, order, decl.important);
            offer(winners, PFlexShrink, n > 1 ? parts[1] : Span{"1", 1}, specificity, order, decl.important);
            offer(winners, PFlexBasis, n > 2 ? parts[2] : Span{"0px", 3}, specificity, order, decl.important);
        }
        return;
    }
    if (equal(name, "border-color") || equal(name, "border-style")) {
        Span pieces[4];
        unsigned n = words(v, pieces, 4);
        int base = equal(name, "border-color") ? PBorderCT : PBorderST;
        if (!n || n > 4)
            return;
        for (unsigned i = 0; i < n; ++i)
            if (!valid_value(base, pieces[i]))
                return;
        unsigned map[4] = {0, n > 1 ? 1U : 0, n > 2 ? 2U : 0, n > 3 ? 3U : (n > 1 ? 1U : 0)};
        for (unsigned side = 0; side < 4; ++side)
            offer(winners, base + int(side), pieces[map[side]], specificity, order, decl.important,
                  decl.base_url, decl.user_agent);
        return;
    }
    int only_side = equal(name, "border-top")      ? 0
                    : equal(name, "border-right")  ? 1
                    : equal(name, "border-bottom") ? 2
                    : equal(name, "border-left")   ? 3
                                                   : -1;
    if (only_side >= 0) {
        Span parts[4];
        unsigned n = words(v, parts, 4);
        if (!n || n > 4)
            return;
        for (unsigned i = 0; i < n; ++i) {
            Length l;
            uint32_t c;
            bool tr;
            int prop = css_length(parts[i], l)                                        ? PBorderT + only_side
                       : valid_value(PBorderST, parts[i])                             ? PBorderST + only_side
                       : color(parts[i], c, tr) || value_is(parts[i], "currentcolor") ? PBorderCT + only_side
                                                                                      : -1;
            if (prop >= 0)
                offer(winners, prop, parts[i], specificity, order, decl.important, decl.base_url,
                      decl.user_agent);
        }
        return;
    }
    if (equal(name, "font")) {
        CssTokenizer t(v.data, v.size);
        bool size_found = false;
        while (t.p < t.n) {
            t.whitespace();
            size_t b = t.p;
            t.scan(" \t\r\n");
            Span token{v.data + b, t.p - b};
            if (!token.size)
                break;
            size_t slash = 0;
            while (slash < token.size && token.data[slash] != '/')
                ++slash;
            Span size{token.data, slash};
            if (valid_value(PFontSize, size)) {
                offer(winners, PFontSize, size, specificity, order, decl.important, decl.base_url,
                      decl.user_agent);
                if (slash < token.size)
                    offer(winners, PLine, {token.data + slash + 1, token.size - slash - 1}, specificity,
                          order, decl.important, decl.base_url, decl.user_agent);
                size_found = true;
                t.whitespace();
                if (t.p < t.n)
                    offer(winners, PFamily, {v.data + t.p, t.n - t.p}, specificity, order, decl.important,
                          decl.base_url, decl.user_agent);
                break;
            }
            if (valid_value(PItalic, token))
                offer(winners, PItalic, token, specificity, order, decl.important, decl.base_url,
                      decl.user_agent);
            if (valid_value(PWeight, token))
                offer(winners, PWeight, token, specificity, order, decl.important, decl.base_url,
                      decl.user_agent);
        }
        (void)size_found;
        return;
    }
    int prop = property(name);
    if (prop >= 0) {
        if (valid_value(prop, v))
            offer(winners, prop, v, specificity, order, decl.important, decl.base_url, decl.user_agent);
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
            offer(winners, base + i, pieces[map[i]], specificity, order, decl.important, decl.base_url,
                  decl.user_agent);
        return;
    }
    if (equal(name, "overflow") && valid_value(POverflowX, v)) {
        offer(winners, POverflowX, v, specificity, order, decl.important, decl.base_url, decl.user_agent);
        offer(winners, POverflowY, v, specificity, order, decl.important, decl.base_url, decl.user_agent);
    }
    if (equal(name, "background")) {
        uint32_t c;
        bool trans;
        if (color(v, c, trans) || value_is(v, "currentcolor"))
            offer(winners, PBackground, v, specificity, order, decl.important, decl.base_url,
                  decl.user_agent);
        else {
            Span parts[8];
            unsigned count = words(v, parts, 8);
            if (count <= 8)
                for (unsigned i = 0; i < count; ++i) {
                    if (color(parts[i], c, trans) || value_is(parts[i], "currentcolor"))
                        offer(winners, PBackground, parts[i], specificity, order, decl.important,
                              decl.base_url, decl.user_agent);
                    else if (valid_value(PGradient, parts[i]))
                        offer(winners, PGradient, parts[i], specificity, order, decl.important, decl.base_url,
                              decl.user_agent);
                    else if (valid_value(PBackgroundImage, parts[i]))
                        offer(winners, PBackgroundImage, parts[i], specificity, order, decl.important,
                              decl.base_url, decl.user_agent);
                    else if (valid_value(PBackgroundRepeat, parts[i]))
                        offer(winners, PBackgroundRepeat, parts[i], specificity, order, decl.important,
                              decl.base_url, decl.user_agent);
                }
        }
    }
    if (equal(name, "border")) {
        if (value_is(v, "inherit") || value_is(v, "initial") || value_is(v, "unset")) {
            for (int p = PBorderT; p <= PBorderColor; ++p)
                offer(winners, p, v, specificity, order, decl.important, decl.base_url, decl.user_agent);
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
                offer(winners, p, width, specificity, order, decl.important, decl.base_url, decl.user_agent);
            for (int side = 0; side < 4; ++side) {
                offer(winners, PBorderST + side, style, specificity, order, decl.important, decl.base_url,
                      decl.user_agent);
                offer(winners, PBorderCT + side, col, specificity, order, decl.important, decl.base_url,
                      decl.user_agent);
            }
        }
    }
}
inline Style defaults(const Document &d, uint16_t id, const Style &parent) {
    Style s;
    s.font = parent.font;
    s.line = parent.line;
    s.family = parent.family;
    s.color = parent.color;
    s.color_alpha = parent.color_alpha;
    s.bold = parent.bold;
    s.italic = parent.italic;
    s.underline = parent.underline;
    s.white = parent.white;
    s.align = parent.align;
    s.visible = parent.visible;
    s.letter_spacing = parent.letter_spacing;
    s.word_spacing = parent.word_spacing;
    s.transform = parent.transform;
    s.list_type = parent.list_type;
    s.border_spacing = parent.border_spacing;
    s.border_collapse = parent.border_collapse;
    if (d.tag(id, "ol"))
        s.list_type = ListType::Decimal;
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
        s.color = d.user_agent.link;
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
    if (d.tag(id, "thead") || d.tag(id, "tbody") || d.tag(id, "tfoot"))
        s.display = Display::RowGroup;
    if (d.tag(id, "tr"))
        s.display = Display::Row;
    if (d.tag(id, "td") || d.tag(id, "th")) {
        s.display = Display::Cell;
        for (auto &p : s.padding)
            p = {4000, Unit::Px};
    }
    if (d.tag(id, "textarea")) {
        s.white = WhiteSpace::PreWrap;
        s.width = {160000, Unit::Px};
        s.height = {64000, Unit::Px};
    }
    if (d.tag(id, "input") || d.tag(id, "button") || d.tag(id, "select") || d.tag(id, "textarea")) {
        s.color = 0x202020;
        s.has_background = true;
        s.background = 0xf3f3f3;
        s.border_style = 1;
        for (auto &b : s.border)
            b = 1;
        for (auto &p : s.padding)
            p = {4000, Unit::Px};
        if (d.tag(id, "input")) {
            s.width = {160000, Unit::Px};
            const char *type = d.attr(id, "type");
            size_t size = length(type, 32);
            if (equal_ci(type, size, "checkbox") || equal_ci(type, size, "radio")) {
                s.width = s.height = {13000, Unit::Px};
                for (auto &p : s.padding)
                    p = {0, Unit::Px};
                if (equal_ci(type, size, "radio"))
                    s.radius_length = {50000, Unit::Percent};
            }
        }
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
    if (p >= PBorderCT && p <= PBorderCL) {
        unsigned side = unsigned(p - PBorderCT);
        s.border_colors[side] = from.border_colors[side];
        s.border_alphas[side] = from.border_alphas[side];
        return;
    }
    if (p >= PBorderST && p <= PBorderSL) {
        s.border_styles[p - PBorderST] = from.border_styles[p - PBorderST];
        return;
    }
    if (p == PGridColumns) {
        s.grid_template = from.grid_template;
        return;
    }
    if (p == PFlexDirection) {
        s.flex_column = from.flex_column;
        s.flex_reverse = from.flex_reverse;
        return;
    }
    if (p == PJustify) {
        s.justify = from.justify;
        return;
    }
    if (p == PAlignItems) {
        s.align_items = from.align_items;
        return;
    }
    if (p == PAlignSelf) {
        s.align_self = from.align_self;
        return;
    }
    if (p == PFlexWrap) {
        s.flex_wrap = from.flex_wrap;
        return;
    }
    if (p == PFlexGrow) {
        s.flex_grow = from.flex_grow;
        return;
    }
    if (p == PFlexShrink) {
        s.flex_shrink = from.flex_shrink;
        return;
    }
    if (p == PFlexBasis) {
        s.flex_basis = from.flex_basis;
        return;
    }
    if (p == PRowGap) {
        s.row_gap = from.row_gap;
        return;
    }
    if (p == PColumnGap) {
        s.column_gap = from.column_gap;
        return;
    }
    if (p == PObjectFit) {
        s.object_fit = from.object_fit;
        return;
    }
    switch (p) {
    case PDisplay:
        s.flex = from.flex;
        s.grid = from.grid;
        s.display = from.display;
        break;
    case PVisibility:
        s.visible = from.visible;
        break;
    case PColor:
        s.color = from.color;
        s.color_alpha = from.color_alpha;
        break;
    case PBackground:
        s.background = from.background;
        s.background_alpha = from.background_alpha;
        s.has_background = from.has_background;
        break;
    case PWidth:
        s.width = from.width;
        break;
    case PHeight:
        s.height = from.height;
        break;
    case PMinWidth:
        s.flex_min_auto=from.flex_min_auto;
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
        s.border_alpha = from.border_alpha;
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
    case PBackgroundImage:
        s.background_base = from.background_base;
        s.background_image = from.background_image;
        break;
    case PBackgroundSize:
        s.background_size = from.background_size;
        break;
    case PBackgroundPosition:
        s.background_position = from.background_position;
        break;
    case PBackgroundRepeat:
        s.background_repeat = from.background_repeat;
        break;
    case PLetter:
        s.letter_spacing = from.letter_spacing;
        break;
    case PWord:
        s.word_spacing = from.word_spacing;
        break;
    case PTransform:
        s.transform = from.transform;
        break;
    case PPosition:
        s.position = from.position;
        break;
    case PLeft:
        s.left = from.left;
        break;
    case PTop:
        s.top = from.top;
        break;
    case PRight:
        s.right = from.right;
        break;
    case PBottom:
        s.bottom = from.bottom;
        break;
    case PList:
        s.list_type = from.list_type;
        break;
    case PBorderSpacing:
        s.border_spacing = from.border_spacing;
        break;
    case PCollapse:
        s.border_collapse = from.border_collapse;
        break;
    case PBoxSizing:
        s.border_box = from.border_box;
        break;
    default:
        break;
    }
}
inline bool inherited_property(int p) {
    return p == PColor || p == PVisibility || (p >= PFamily && p <= PWhite) || p == PLetter || p == PWord ||
           p == PTransform || p == PList || p == PBorderSpacing || p == PCollapse;
}
inline void apply_property(Style &s, const Style &parent, int p, Span v, int root, uint32_t offset,
                           int viewport_width = 800, int viewport_height = 600) {
    auto contextual_length = [&](Span input, Length &l, bool negative = false, bool scalar = false) {
        if (!math_length(input, l, negative, scalar, s.font, root, viewport_width, viewport_height))
            return false;
        if (l.unit == Unit::Vw || l.unit == Unit::Vh) {
            l.value =
                clamp64(int64_t(l.value) * (l.unit == Unit::Vw ? viewport_width : viewport_height) / 100,
                        -INT32_MAX, INT32_MAX);
            l.unit = Unit::Px;
        }
        return true;
    };
    if (value_is(v, "inherit") || (value_is(v, "unset") && inherited_property(p))) {
        copy_property(s, parent, p);
        return;
    }
    if (value_is(v, "initial") || value_is(v, "unset")) {
        Style initial;
        copy_property(s, initial, p);
        return;
    }
    Length l;
    bool tr = false;
    uint32_t c;
    if (p >= PMarginT && p <= PMarginL) {
        if (contextual_length(v, l, true))
            s.margin[p - PMarginT] = l;
        return;
    }
    if (p >= PPaddingT && p <= PPaddingL) {
        if (contextual_length(v, l) && l.unit != Unit::Auto)
            s.padding[p - PPaddingT] = l;
        return;
    }
    if (p >= PBorderT && p <= PBorderL) {
        if (contextual_length(v, l) && l.unit != Unit::Auto)
            s.border[p - PBorderT] = clamp64(resolve(l, 0, s.font, root), 0, 128);
        return;
    }
    if (p >= PBorderCT && p <= PBorderCL) {
        unsigned side = unsigned(p - PBorderCT);
        if (value_is(v, "currentcolor")) {
            s.border_colors[side] = s.color;
            s.border_alphas[side] = s.color_alpha;
        } else
            color(v, s.border_colors[side], tr, &s.border_alphas[side]);
        return;
    }
    if (p >= PBorderST && p <= PBorderSL) {
        s.border_styles[p - PBorderST] = value_is(v, "solid")    ? 1
                                         : value_is(v, "dashed") ? 2
                                         : value_is(v, "dotted") ? 3
                                                                 : 0;
        return;
    }
    switch (p) {
    case PShadow:
        shadow(v, s);
        break;
    case PGridColumns:
        s.grid_template = offset;
        break;
    case PFlexDirection:
        s.flex_column = value_is(v, "column") || value_is(v, "column-reverse");
        s.flex_reverse = value_is(v, "row-reverse") || value_is(v, "column-reverse");
        break;
    case PJustify:
        s.justify = value_is(v, "center")          ? 2
                    : one_of(v, "end|flex-end")    ? 1
                    : value_is(v, "space-between") ? 3
                    : value_is(v, "space-around")  ? 4
                    : value_is(v, "space-evenly")  ? 5
                                                   : 0;
        break;
    case PAlignItems:
    case PAlignSelf: {
        uint8_t a = value_is(v, "stretch")          ? 0
                    : one_of(v, "start|flex-start") ? 1
                    : one_of(v, "end|flex-end")     ? 2
                    : value_is(v, "center")         ? 3
                                                    : 255;
        if (p == PAlignItems)
            s.align_items = a;
        else
            s.align_self = a;
        break;
    }
    case PFlexWrap:
        s.flex_wrap = value_is(v, "wrap");
        break;
    case PFlexGrow:
    case PFlexShrink: {
        int n;
        size_t used;
        if (number(v, n, used)) {
            if (p == PFlexGrow)
                s.flex_grow = n;
            else
                s.flex_shrink = n;
        }
        break;
    }
    case PFlexBasis:
    case PRowGap:
    case PColumnGap:
        if (contextual_length(v, l)) {
            if (p == PFlexBasis)
                s.flex_basis = l;
            else if (p == PRowGap)
                s.row_gap = l;
            else
                s.column_gap = l;
        }
        break;
    case PDisplay:
        s.grid = value_is(v, "grid") || value_is(v, "inline-grid");
        s.flex = s.grid || value_is(v, "flex") || value_is(v, "inline-flex");
        if (s.grid)
            s.flex_wrap = true;
        if (s.flex)
            s.display = (value_is(v, "inline-flex") || value_is(v, "inline-grid")) ? Display::InlineBlock
                                                                                   : Display::Block;
        else if (value_is(v, "block"))
            s.display = Display::Block;
        else if (value_is(v, "inline-block"))
            s.display = Display::InlineBlock;
        else if (value_is(v, "inline"))
            s.display = Display::Inline;
        else if (value_is(v, "none"))
            s.display = Display::None;
        else if (value_is(v, "table"))
            s.display = Display::Table;
        else if (value_is(v, "table-row-group") || value_is(v, "table-header-group") ||
                 value_is(v, "table-footer-group"))
            s.display = Display::RowGroup;
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
        if (value_is(v, "currentcolor")) {
            s.color = parent.color;
            s.color_alpha = parent.color_alpha;
            break;
        }
        if (color(v, c, tr, &s.color_alpha))
            s.color = c;
        break;
    case PBackground:
        if (value_is(v, "currentcolor")) {
            s.background = s.color;
            s.background_alpha = s.color_alpha;
            s.has_background = true;
            break;
        }
        if (color(v, c, tr, &s.background_alpha)) {
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
        if (contextual_length(v, l)) {
            Length *a[] = {&s.width, &s.height, &s.min_width, &s.max_width, &s.min_height, &s.max_height};
            *a[p - PWidth] = l;if(p==PMinWidth)s.flex_min_auto=l.unit==Unit::Auto;
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
        else if (color(v, c, tr, &s.border_alpha))
            s.border_color = c;
        break;
    case PFamily:
        s.family = offset;
        break;
    case PFontSize:
        if (contextual_length(v, l) && l.unit != Unit::Auto)
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
        else if (contextual_length(v, l, false, true) && l.unit != Unit::Auto)
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
        if (contextual_length(v, l)) {
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
    case PGradient:
        s.has_gradient = gradient(v, s.gradient_color, s.gradient_alpha, s.gradient_horizontal);
        break;
    case PObjectFit:
        s.object_fit = value_is(v, "contain") ? 1 : value_is(v, "cover") ? 2 : 0;
        break;
    case PBackgroundImage:
        if (valid_value(PGradient, v))
            s.has_gradient = gradient(v, s.gradient_color, s.gradient_alpha, s.gradient_horizontal);
        else
            s.background_image = value_is(v, "none") ? 0 : offset;
        break;
    case PBackgroundSize:
        s.background_size = offset;
        break;
    case PBackgroundPosition:
        s.background_position = offset;
        break;
    case PBackgroundRepeat:
        s.background_repeat = value_is(v, "repeat-x")    ? 1
                              : value_is(v, "repeat-y")  ? 2
                              : value_is(v, "no-repeat") ? 3
                                                         : 0;
        break;
    case PLetter:
    case PWord: {
        int spacing = 0;
        if (contextual_length(v, l, true))
            spacing = clamp64(resolve(l, 0, s.font, root), -256, 256);
        if (p == PLetter)
            s.letter_spacing = spacing;
        else
            s.word_spacing = spacing;
        break;
    }
    case PTransform:
        s.transform = value_is(v, "uppercase")    ? Transform::Upper
                      : value_is(v, "lowercase")  ? Transform::Lower
                      : value_is(v, "capitalize") ? Transform::Capitalize
                                                  : Transform::None;
        break;
    case PPosition:
        s.position = value_is(v, "relative")   ? Position::Relative
                     : value_is(v, "absolute") ? Position::Absolute
                                               : Position::Static;
        break;
    case PLeft:
    case PTop:
    case PRight:
    case PBottom:
        if (contextual_length(v, l, true)) {
            Length *offsets[] = {&s.left, &s.top, &s.right, &s.bottom};
            *offsets[p - PLeft] = l;
        }
        break;
    case PList: {
        const char *names[] = {"disc",        "circle",      "square",      "decimal", "lower-alpha",
                               "upper-alpha", "lower-roman", "upper-roman", "none"};
        for (unsigned i = 0; i < 9; ++i)
            if (value_is(v, names[i]))
                s.list_type = ListType(i);
        break;
    }
    case PBorderSpacing:
        if (contextual_length(v, l))
            s.border_spacing = clamp64(resolve(l, 0, s.font, root), 0, 128);
        break;
    case PCollapse:
        s.border_collapse = value_is(v, "collapse");
        break;
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
} // namespace Husarz
#include "css_variables.h"
namespace Husarz {
// One necessary key from the rightmost compound. Hash collisions only add
// candidates; the complete matcher remains authoritative. Rule order is
// retained.
struct SelectorIndex {
    static constexpr unsigned Buckets = 128, Words = (MaxRules + 63) / 64;
    Selector parsed[MaxRules]{};
    uint64_t buckets[Buckets][Words]{}, general[Words]{};
    uint16_t fingerprints[MaxRules]{};
    unsigned count{};
    static unsigned hash(Span s, unsigned kind) {
        uint32_t h = 2166136261U ^ kind;
        for (size_t i = 0; i < s.size; ++i)
            h = (h ^ uint8_t(kind == 1 ? lower(s.data[i]) : s.data[i])) * 16777619U;
        return h;
    }
    void add(const Document &d, unsigned r) {
        auto &sel = parsed[r];
        sel = selector({d.str(d.rules[r].selector), length(d.str(d.rules[r].selector), MaxSelectorLength)});
        Span key{};
        unsigned kind = 0;
        if (sel.valid) {
            Span v = sel.parts[sel.count - 1].compound;
            size_t p = 0;
            if (p < v.size && ident(v.data[p])) {
                size_t b = p;
                while (p < v.size && ident(v.data[p]))
                    ++p;
                key = {v.data + b, p - b};
                kind = 1;
            } else if (p < v.size && v.data[p] == '*')
                ++p;
            // Only scan leading positive class/id tests, never :not arguments.
            while (p < v.size && (v.data[p] == '.' || v.data[p] == '#')) {
                char type = v.data[p++];
                size_t b = p;
                while (p < v.size && ident(v.data[p]))
                    ++p;
                if (type == '#' || kind != 3) {
                    key = {v.data + b, p - b};
                    kind = type == '#' ? 3 : 2;
                }
            }
        }
        unsigned full = kind ? hash(key, kind) : 0;
        fingerprints[r] = kind ? uint16_t((full >> 7) & 1023) : UINT16_MAX;
        auto *bits = kind ? buckets[full % Buckets] : general;
        bits[r / 64] |= UINT64_C(1) << (r % 64);
    }
    void candidates(const Document &d, uint16_t id, uint64_t *bits) const {
        uint64_t present[16]{};
        for (unsigned w = 0; w < Words; ++w)
            bits[w] = general[w];
        auto include = [&](Span key, unsigned kind) {
            unsigned full = hash(key, kind), fingerprint = (full >> 7) & 1023;
            present[fingerprint / 64] |= UINT64_C(1) << (fingerprint % 64);
            auto *b = buckets[full % Buckets];
            for (unsigned w = 0; w < Words; ++w)
                bits[w] |= b[w];
        };
        const char *tag = d.str(d.nodes[id].tag);
        include({tag, length(tag, 64)}, 1);
        const char *name = d.attr(id, "id");
        if (*name)
            include({name, length(name, MaxAttrLength)}, 3);
        const char *classes = d.attr(id, "class");
        size_t n = length(classes, MaxAttrLength), p = 0;
        while (p < n) {
            while (p < n && space(classes[p]))
                ++p;
            size_t b = p;
            while (p < n && !space(classes[p]))
                ++p;
            if (p > b)
                include({classes + b, p - b}, 2);
        }
        for (unsigned w = 0; w < Words; ++w) {
            uint64_t pending = bits[w];
            while (pending) {
                unsigned bit = unsigned(__builtin_ctzll(pending));
                pending &= pending - 1;
                unsigned r = w * 64 + bit;
                if (r >= d.rule_count) {
                    bits[w] &= ~(UINT64_C(1) << bit);
                    continue;
                }
                auto fp = fingerprints[r];
                if (fp != UINT16_MAX && !(present[fp / 64] & (UINT64_C(1) << (fp % 64))))
                    bits[w] &= ~(UINT64_C(1) << bit);
            }
        }
    }
};

inline void release_selector_index(SelectorIndex *p) { delete p; }
inline Work compute_generated_work(Document &d);
inline Work compute_styles_work(Document &d, bool indexed = true) {
    HUSARZ_SCOPE(d, Style);
    uint32_t budget = 16000000;
    Style initial;
    initial.color = d.user_agent.text;
    d.root_font = 16;
    d.custom_count = 0;
    for (auto &h : d.custom_heads)
        h = 0;
    if (indexed && !d.selector_index)
        d.selector_index = new SelectorIndex;
    auto *index = indexed ? d.selector_index : nullptr;
    if (index)
        while (index->count < d.rule_count) {
            co_await WorkPoint{1};
            index->add(d, index->count++);
        }
    for (uint16_t id = 0; id < d.node_count; ++id) {
        HUSARZ_PROGRESS(Style, id, d.node_count);
        co_await WorkPoint{1};
        const Node &node = d.nodes[id];
        if (node.detached) {
            d.styles[id] = {};
            d.styles[id].display = Display::None;
            continue;
        }
        const Style &parent = id ? d.styles[node.parent] : initial;
        if (node.is_text) {
            d.styles[id] = defaults(d, id, parent);
            d.styles[id].display = Display::Inline;
            d.styles[id].paint_opacity = parent.paint_opacity;
            continue;
        }
        Style s = defaults(d, id, parent);
        Winner winners[PCount];
        uint16_t matched_rules[MaxRules]{};
        unsigned matched_count = 0;
        uint64_t candidates[SelectorIndex::Words]{};
        if (index)
            index->candidates(d, id, candidates);
        for (unsigned word = 0; word < SelectorIndex::Words && budget; ++word) {
            uint64_t pending = index ? candidates[word] : UINT64_MAX;
            while (pending && budget) {
                co_await WorkPoint{1};
                unsigned r = word * 64 + unsigned(__builtin_ctzll(pending));
                pending &= pending - 1;
                if (r >= d.rule_count)
                    break;
                const Rule &rule = d.rules[r];
                if (rule.pseudo)
                    continue;
                if (d.style_viewport_width < rule.media_min || d.style_viewport_width > rule.media_max)
                    continue;
                Selector sel =
                    index ? index->parsed[r]
                          : selector({d.str(rule.selector), length(d.str(rule.selector), MaxSelectorLength)});
                HUSARZ_COUNT(d, selector_checks, 1);
                bool matched = false;
                co_await matches_work(d, id, sel, budget, matched);
                if (!matched)
                    continue;
                HUSARZ_COUNT(d, selector_matches, 1);
                matched_rules[matched_count++] = uint16_t(r);
                for (uint32_t n = 0; n < rule.count; ++n) {
                    co_await WorkPoint{1};
                    offer_custom(d, id, d.declarations[rule.first + n], sel.specificity,
                                 uint64_t(rule.order) * (MaxDeclarations + 1) + n);
                }
            }
        }
        for (unsigned n = 0; n < node.inline_count; ++n) {
            co_await WorkPoint{1};
            offer_custom(d, id, d.declarations[node.inline_first + n], 1 << 24, (UINT64_C(1) << 60) + n);
        }
        for (unsigned r = 0; r <= matched_count; ++r) {
            const Rule *rule = r < matched_count ? &d.rules[matched_rules[r]] : nullptr;
            unsigned count = rule ? rule->count : node.inline_count;
            int specificity = rule ? (index ? index->parsed[matched_rules[r]].specificity
                                            : selector({d.str(rule->selector),
                                                        length(d.str(rule->selector), MaxSelectorLength)})
                                                  .specificity)
                                   : (1 << 24);
            for (unsigned n = 0; n < count; ++n) {
                co_await WorkPoint{1};
                auto decl = d.declarations[(rule ? rule->first : node.inline_first) + n];
                if (d.str(decl.name)[0] == '-' && d.str(decl.name)[1] == '-')
                    continue;
                Span value{d.str(decl.value), length(d.str(decl.value), MaxAttrLength)};
                const char *name = d.str(decl.name);
                if (property(name) < 0 && !equal(name, "margin") && !equal(name, "padding") &&
                    !equal(name, "border") && !equal(name, "border-width") && !equal(name, "border-top") &&
                    !equal(name, "border-right") && !equal(name, "border-bottom") &&
                    !equal(name, "border-left") && !equal(name, "background") && !equal(name, "font") &&
                    !equal(name, "overflow") && !equal(name, "gap") && !equal(name, "flex"))
                    continue;
                if (has_variable(value)) {
                    uint32_t resolved = 0;
                    bool valid = false;
                    co_await resolve_variable_work(d, id, value, resolved, valid);
                    decl.value = valid ? resolved : d.store_computed("unset", 5);
                }

                offer_declaration(d, winners, decl, specificity,
                                  rule ? uint64_t(rule->order) * (MaxDeclarations + 1) + n
                                       : (UINT64_C(1) << 60) + n);
            }
        }
        for (int p = 0; p < PCount; ++p) {
            co_await WorkPoint{1};
            if (winners[p].specificity >= 0) {
                Span v = winners[p].value;
                if (p == PFontSize || p == PFamily || p == PWeight || p == PItalic)
                    apply_property(s, parent, p, v, d.root_font,
                                   ((p == PGridColumns || p == PFamily || p == PBackgroundImage ||
                                     p == PBackgroundSize || p == PBackgroundPosition)
                                        ? d.store_computed(v.data, v.size, MaxAttrLength)
                                        : 0),
                                   d.style_viewport_width, d.style_viewport_height);
            }
        }
        for (int p = 0; p < PCount; ++p) {
            co_await WorkPoint{1};
            if (winners[p].specificity >= 0 && p != PFontSize && p != PFamily && p != PWeight &&
                p != PItalic) {
                Span v = winners[p].value;
                apply_property(s, parent, p, v, d.root_font,
                               ((p == PGridColumns || p == PFamily || p == PBackgroundImage ||
                                 p == PBackgroundSize || p == PBackgroundPosition)
                                    ? d.store_computed(v.data, v.size, MaxAttrLength)
                                    : 0),
                               d.style_viewport_width, d.style_viewport_height);
            }
        }
        if (winners[PBackgroundImage].specificity >= 0 &&
            !value_is(winners[PBackgroundImage].value, "inherit"))
            s.background_base = winners[PBackgroundImage].base_url;
        if (d.tag(id, "html"))
            d.root_font = s.font;
        for (unsigned side = 0; side < 4; ++side) {
            co_await WorkPoint{1};
            if (winners[PBorderST + int(side)].specificity < 0)
                s.border_styles[side] = s.border_style;
            if (winners[PBorderCT + int(side)].specificity < 0)
                s.border_colors[side] = s.border_color;
            if (!s.border_styles[side])
                s.border[side] = 0;
        }
        s.border_color = s.border_colors[0];
        s.border_alpha = s.border_alphas[0];
        s.border_style = 0;
        for (unsigned side = 0; side < 4; ++side)
            if (s.border_styles[side] > s.border_style)
                s.border_style = s.border_styles[side];
        s.paint_opacity = id ? uint8_t(unsigned(s.opacity) * parent.paint_opacity / 255) : s.opacity;
        if (parent.flex && s.display == Display::Inline)
            s.display = Display::Block;
        d.styles[id] = s;
    }
    if (!budget)
        d.limit(LimitSelectorWork);
    co_await compute_generated_work(d);
    d.styles_dirty = false;
    d.style_revision = d.revision;
}
inline Work compute_generated_work(Document &d) {
    d.generated_count = 0;
    if (!d.has_pseudo)
        co_return;
    uint32_t budget = 8000000;
    for (uint16_t id = 1; id < d.node_count; ++id) {
        co_await WorkPoint{1};
        if (d.nodes[id].is_text || d.styles[id].display == Display::None)
            continue;
        uint64_t candidates[SelectorIndex::Words]{};
        if (d.selector_index)
            d.selector_index->candidates(d, id, candidates);
        for (unsigned kind = 1; kind <= 2; ++kind) {
            Winner winners[PCount];
            bool any = false;
            for (unsigned word = 0; word < SelectorIndex::Words; ++word) {
                uint64_t bits = d.selector_index ? candidates[word] : UINT64_MAX;
                while (bits) {
                    co_await WorkPoint{1};
                    unsigned r = word * 64 + unsigned(__builtin_ctzll(bits));
                    bits &= bits - 1;
                    if (r >= d.rule_count)
                        break;
                    const auto &rule = d.rules[r];
                    if (rule.pseudo != kind || d.style_viewport_width < rule.media_min ||
                        d.style_viewport_width > rule.media_max)
                        continue;
                    auto sel = d.selector_index ? d.selector_index->parsed[r]
                                                : selector({d.str(rule.selector),
                                                            length(d.str(rule.selector), MaxSelectorLength)});
                    bool matched = false;
                    co_await matches_work(d, id, sel, budget, matched);
                    if (!matched)
                        continue;
                    for (unsigned j = 0; j < rule.count; ++j) {
                        co_await WorkPoint{1};
                        auto decl = d.declarations[rule.first + j];
                        Span v{d.str(decl.value), length(d.str(decl.value), MaxAttrLength)};
                        if (has_variable(v)) {
                            uint32_t resolved = 0;
                            bool valid = false;
                            co_await resolve_variable_work(d, id, v, resolved, valid);
                            if (!valid)
                                continue;
                            decl.value = resolved;
                        }

                        offer_declaration(d, winners, decl, sel.specificity,
                                          uint64_t(rule.order) * (MaxDeclarations + 1) + j);
                        any = true;
                    }
                }
            }
            if (!any || winners[PContent].specificity < 0)
                continue;
            Span content = winners[PContent].value;
            if (value_is(content, "none") || value_is(content, "normal") || content.size < 2)
                continue;
            if (d.generated_count == MaxGeneratedBoxes) {
                d.limit(LimitGenerated);
                co_return;
            }
            auto &box = d.generated[d.generated_count++];
            box = {};
            box.owner = id;
            box.after = kind == 2;
            char generated_text[MaxVariableName + 1]{};
            size_t written = 0;
            for (size_t i = 1; i + 1 < content.size && written < MaxVariableName; ++i) {
                uint32_t cp = uint8_t(content.data[i]);
                if (cp == '\\' && i + 2 < content.size) {
                    ++i;
                    cp = uint8_t(content.data[i]);
                    if (hex(content.data[i]) >= 0) {
                        cp = 0;
                        unsigned digits = 0;
                        while (i + 1 < content.size && digits < 6 && hex(content.data[i]) >= 0) {
                            cp = cp * 16 + unsigned(hex(content.data[i++]));
                            ++digits;
                        }
                        if (i + 1 < content.size && space(content.data[i])) {
                        } else
                            --i;
                    }
                }
                char encoded[4];
                unsigned bytes = encode(cp, encoded);
                if (written + bytes > MaxVariableName)
                    break;
                for (unsigned b = 0; b < bytes; ++b)
                    generated_text[written++] = encoded[b];
            }
            box.content = d.store_computed(generated_text, written, MaxVariableName);
            Style parent = d.styles[id];
            Style style;
            style.font = parent.font;
            style.line = parent.line;
            style.color = parent.color;
            style.color_alpha = parent.color_alpha;
            style.family = parent.family;
            style.bold = parent.bold;
            style.italic = parent.italic;
            for (int p = 0; p < PCount; ++p) {
                co_await WorkPoint{1};
                if (winners[p].specificity < 0)
                    continue;
                Span value = winners[p].value;
                apply_property(
                    style, parent, p, value, d.root_font,
                    (p == PBackgroundImage || p == PFamily) ? d.store_computed(value.data, value.size) : 0,
                    d.style_viewport_width, d.style_viewport_height);
            }
            style.paint_opacity = uint8_t(unsigned(style.opacity) * parent.paint_opacity / 255);
            box.style = style;
        }
    }
}
inline void compute_styles(Document &d, bool indexed = true) { finish_work(compute_styles_work(d, indexed)); }

inline Work collect_styles_work(Document &d) {
    if (!d.user_agent_loaded) {
        co_await parse_css_work(d, UserAgentStyleSheet, sizeof(UserAgentStyleSheet) - 1, 0, 0, true);
        d.user_agent_loaded = true;
    }
    for (uint16_t i = 1; i < d.node_count; ++i) {
        co_await WorkPoint{1};
        if (d.tag(i, "style"))
            co_await parse_css_work(d, d.str(d.nodes[i].text), d.nodes[i].text_length, uint32_t(i) * 4096);
        const char *in = d.attr(i, "style");
        if (*in) {
            Span clean;
            co_await clean_css_work(d, in, length(in, MaxAttrLength), clean);
            co_await css_declarations_work(d, clean, d.nodes[i].inline_first, d.nodes[i].inline_count);
        }
    }
}
inline void collect_styles(Document &d) { finish_work(collect_styles_work(d)); }

} // namespace Husarz
