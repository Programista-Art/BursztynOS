#pragma once
#include "html.h"
#include "css_colors.h"
#include "user_agent.h"
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
inline bool color(Span v, uint32_t &out, bool &transparent, uint8_t *alpha_out = nullptr) {
    v = trimmed(v.data, v.size);
    uint8_t alpha = 255;
    uint32_t result = 0;
    bool found = false;
    if (value_is(v, "transparent")) { alpha = 0; found = true; }
    else if (v.size && v.data[0] == '#' &&
             (v.size == 4 || v.size == 5 || v.size == 7 || v.size == 9)) {
        bool short_hex = v.size <= 5;
        unsigned channels = (v.size == 5 || v.size == 9) ? 4 : 3;
        for (unsigned i = 0; i < channels; ++i) {
            unsigned pos = 1 + i * (short_hex ? 1 : 2);
            int hi = hex(v.data[pos]), lo = short_hex ? hi : hex(v.data[pos + 1]);
            if (hi < 0 || lo < 0) return false;
            unsigned channel = unsigned(hi * 16 + lo);
            if (i == 3) alpha = uint8_t(channel);
            else result = (result << 8) | channel;
        }
        found = true;
    } else {
        for (const auto &c : NamedColors)
            if (value_is(v, c.name)) { result = c.rgb; found = true; break; }
        bool rgba = v.size > 6 && equal_ci(v.data, 5, "rgba(");
        bool rgb = v.size > 5 && equal_ci(v.data, 4, "rgb(");
        bool hsla = v.size > 6 && equal_ci(v.data, 5, "hsla(");
        bool hsl = v.size > 5 && equal_ci(v.data, 4, "hsl(");
        if (!found && (rgba || rgb || hsla || hsl) && v.data[v.size - 1] == ')') {
            size_t prefix = rgba || hsla ? 5 : 4;
            CssTokenizer t(v.data + prefix, v.size - prefix - 1);
            int values[4]{};
            unsigned count = rgba || hsla ? 4 : 3;
            for (unsigned i = 0; i < count; ++i) {
                t.whitespace(); size_t start = t.p; t.scan(",");
                Span part = trimmed(t.s + start, t.p - start);
                int num; size_t used;
                if (!number(part, num, used)) return false;
                bool percent = used + 1 == part.size && part.data[used] == '%';
                if (used != part.size && !percent) return false;
                if (i == 3) values[i] = clamp64(int64_t(num) * 255 / (percent ? 100000 : 1000), 0, 255);
                else if (hsl || hsla) {
                    if ((i > 0) != percent) return false;
                    values[i] = i ? clamp64(num, 0, 100000) : num;
                } else values[i] = clamp64(int64_t(num) * (percent ? 255 : 1) / (percent ? 100000 : 1000), 0, 255);
                if (i + 1 < count) {
                    if (t.p == t.n || t.s[t.p] != ',') return false;
                    ++t.p;
                } else if (t.p != t.n) return false;
            }
            if (hsl || hsla) {
                int hue = values[0] % 360000; if (hue < 0) hue += 360000;
                int light = values[2], sat = values[1];
                int twice = 2 * light - 100000; if (twice < 0) twice = -twice;
                int chroma = int(int64_t(100000 - twice) * sat / 100000);
                int triangle = hue % 120000 - 60000; if (triangle < 0) triangle = -triangle;
                int x = int(int64_t(chroma) * (60000 - triangle) / 60000), m = light - chroma / 2;
                int r = 0, g = 0, b = 0;
                switch (hue / 60000) {
                case 0: r = chroma; g = x; break; case 1: r = x; g = chroma; break;
                case 2: g = chroma; b = x; break; case 3: g = x; b = chroma; break;
                case 4: r = x; b = chroma; break; default: r = chroma; b = x; break;
                }
                result = (uint32_t((r + m) * 255 / 100000) << 16) |
                         (uint32_t((g + m) * 255 / 100000) << 8) | uint32_t((b + m) * 255 / 100000);
            } else result = (uint32_t(values[0]) << 16) | (uint32_t(values[1]) << 8) | uint32_t(values[2]);
            if (count == 4) alpha = uint8_t(values[3]);
            found = true;
        }
    }
    if (!found) return false;
    out = result; transparent = !alpha;
    if (alpha_out) *alpha_out = alpha;
    return true;
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
struct SelectorPart { Span compound; char relation{' '}; };
struct Selector { SelectorPart parts[8]; unsigned count{}; int specificity{}; bool valid{}; };
inline bool span_equal(Span a, Span b) {
    if (a.size != b.size) return false;
    for (size_t i = 0; i < a.size; ++i) if (a.data[i] != b.data[i]) return false;
    return true;
}
inline bool pseudo_name(Span n) {
    return value_is(n,"first-child") || value_is(n,"last-child") || value_is(n,"only-child") ||
           value_is(n,"first-of-type") || value_is(n,"last-of-type") || value_is(n,"nth-child") ||
           value_is(n,"nth-last-child") || value_is(n,"not");
}
// Validate before matching; unsupported syntax never broadens a selector.
inline bool simple_syntax(Span v, int &specificity, bool argument = false) {
    if (!v.size || v.size > 256) return false;
    size_t p = 0;
    if (v.data[p] == '*') ++p;
    else if (ident(v.data[p])) { while (p < v.size && ident(v.data[p])) ++p; ++specificity; }
    unsigned count = 0;
    while (p < v.size && ++count <= 16) {
        char c = v.data[p++];
        if (c == '.' || c == '#' || c == ':') {
            size_t b = p; while (p < v.size && ident(v.data[p])) ++p;
            if (p == b) return false;
            Span name{v.data + b, p - b};
            if (c != ':') { specificity += c == '#' ? 65536 : 256; continue; }
            if (!pseudo_name(name) || (argument && value_is(name,"not"))) return false;
            bool function = value_is(name,"not") || value_is(name,"nth-child") || value_is(name,"nth-last-child");
            if (function) {
                if (p == v.size || v.data[p++] != '(') return false;
                b = p; while (p < v.size && v.data[p] != ')') ++p;
                if (p == v.size || p == b) return false;
                Span arg = trimmed(v.data + b, p++ - b);
                if (value_is(name,"not")) { if (!simple_syntax(arg,specificity,true)) return false; }
                else { if (arg.size > 64) return false; specificity += 256; }
            } else specificity += 256;
        } else if (c == '[') {
            size_t b = p; while (p < v.size && ident(v.data[p])) ++p;
            if (p == b) return false;
            while (p < v.size && space(v.data[p])) ++p;
            if (p == v.size) return false;
            if (v.data[p] != ']') {
                if (v.data[p] == '~' || v.data[p] == '^' || v.data[p] == '$' || v.data[p] == '*') ++p;
                if (p == v.size || v.data[p++] != '=') return false;
                while (p < v.size && space(v.data[p])) ++p;
                char quote = p < v.size && (v.data[p] == '\'' || v.data[p] == '"') ? v.data[p++] : 0;
                b = p;
                while (p < v.size && (quote ? v.data[p] != quote : !space(v.data[p]) && v.data[p] != ']')) ++p;
                if ((!quote && p == b) || p == v.size) return false;
                if (quote) ++p;
                while (p < v.size && space(v.data[p])) ++p;
            }
            if (p == v.size || v.data[p++] != ']') return false;
            specificity += 256;
        } else return false;
    }
    return p == v.size && count <= 16;
}
inline Selector selector(Span v) {
    Selector out; size_t p = 0; char relation = ' ';
    while (p < v.size) {
        while (p < v.size && space(v.data[p])) ++p;
        if (p == v.size) break;
        if (v.data[p] == '>' || v.data[p] == '+' || v.data[p] == '~') {
            if (!out.count) return out;
            relation = v.data[p++];
            while (p < v.size && space(v.data[p])) ++p;
            if (p == v.size) return out;
        }
        size_t start = p; char quote = 0; unsigned nesting = 0;
        while (p < v.size) {
            char c = v.data[p];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '[' || c == '(') { if (++nesting > 2) return out; }
            else if (c == ']' || c == ')') { if (!nesting) return out; --nesting; }
            else if (!nesting && (space(c) || c == '>' || c == '+' || c == '~')) break;
            ++p;
        }
        Span part{v.data + start,p - start};
        if (quote || nesting || out.count == 8 || !simple_syntax(part,out.specificity)) return out;
        out.parts[out.count++] = {part,relation}; relation = ' ';
    }
    out.valid = out.count != 0; return out;
}
inline bool nth(Span arg, int index) {
    char text[65]{}; unsigned n = 0;
    for (size_t i = 0; i < arg.size; ++i) if (!space(arg.data[i])) {
        if (n == 64) return false;
        text[n++] = lower(arg.data[i]);
    }
    if (equal(text,"odd")) return index % 2 == 1;
    if (equal(text,"even")) return index % 2 == 0;
    size_t pos = 0; int a = 0, b = 0; bool neg = false;
    if (pos < n && (text[pos] == '+' || text[pos] == '-')) neg = text[pos++] == '-';
    size_t begin = pos;
    while (pos < n && text[pos] >= '0' && text[pos] <= '9') {
        if (a > 100000) return false;
        a = a * 10 + text[pos++] - '0';
    }
    if (pos == n) return begin != pos && index == (neg ? -a : a);
    if (text[pos++] != 'n') return false;
    if (begin == pos - 1) a = 1;
    if (neg) a = -a;
    if (pos < n) {
        if (text[pos] != '+' && text[pos] != '-') return false;
        bool minus = text[pos++] == '-'; begin = pos;
        while (pos < n && text[pos] >= '0' && text[pos] <= '9') {
            if (b > 100000) return false;
            b = b * 10 + text[pos++] - '0';
        }
        if (pos != n || pos == begin) return false;
        if (minus) b = -b;
    }
    return a ? (index - b) % a == 0 && (index - b) / a >= 0 : index == b;
}
inline bool compound(const Document &d, uint16_t id, Span v, uint32_t &budget) {
    if (!id || id >= d.node_count || d.nodes[id].is_text || v.size + 1 > budget) return false;
    budget -= uint32_t(v.size) + 1;
    size_t p = 0;
    if (v.data[p] == '*') ++p;
    else if (ident(v.data[p])) {
        size_t b = p; while (p < v.size && ident(v.data[p])) ++p;
        if (!equal_ci(v.data + b,p - b,d.str(d.nodes[id].tag))) return false;
    }
    while (p < v.size) {
        char type = v.data[p++]; size_t b = p;
        while (p < v.size && ident(v.data[p])) ++p;
        Span name{v.data + b,p - b};
        if (type == '#' || type == '.') {
            const char *s = d.attr(id,type == '#' ? "id" : "class");
            size_t n = length(s,MaxAttrLength);
            if (n > budget) { budget = 0; return false; } budget -= uint32_t(n);
            if (type == '#') { if (!span_equal({s,n},name)) return false; }
            else {
                bool found = false; size_t k = 0;
                while (k < n) {
                    while (k < n && space(s[k])) ++k;
                    size_t start = k; while (k < n && !space(s[k])) ++k;
                    if (span_equal({s + start,k - start},name)) found = true;
                }
                if (!found) return false;
            }
        } else if (type == '[') {
            const char *value = nullptr;
            const Node &node = d.nodes[id];
            for (unsigned i = 0; i < node.attr_count; ++i) {
                if (!budget) return false;
                --budget;
                const auto &at = d.attrs[node.attr_start + i];
                if (equal_ci(name.data,name.size,d.str(at.name))) { value = d.str(at.value); break; }
            }
            if (!value) return false;
            while (p < v.size && space(v.data[p])) ++p;
            if (v.data[p] == ']') { ++p; continue; }
            char op = v.data[p++]; if (op != '=') ++p;
            while (p < v.size && space(v.data[p])) ++p;
            char quote = v.data[p] == '\'' || v.data[p] == '"' ? v.data[p++] : 0;
            b = p;
            while (p < v.size && (quote ? v.data[p] != quote : !space(v.data[p]) && v.data[p] != ']')) ++p;
            Span wanted{v.data + b,p - b};
            if (quote) ++p;
            while (p < v.size && space(v.data[p])) ++p;
            ++p;
            size_t n = length(value,MaxAttrLength);
            if (n > budget) { budget = 0; return false; } budget -= uint32_t(n);
            bool found = false;
            if (op == '=') found = span_equal({value,n},wanted);
            else if (wanted.size && wanted.size <= n) {
                if (op == '^') found = span_equal({value,wanted.size},wanted);
                else if (op == '$') found = span_equal({value + n - wanted.size,wanted.size},wanted);
                else if (op == '*') {
                    for (size_t j = 0; j + wanted.size <= n; ++j) {
                        if (wanted.size > budget) { budget = 0; return false; }
                        budget -= uint32_t(wanted.size);
                        if (span_equal({value+j,wanted.size},wanted)) { found = true; break; }
                    }
                } else if (op == '~') {
                    size_t k = 0;
                    while (k < n) { while (k < n && space(value[k])) ++k;
                        size_t start = k; while (k < n && !space(value[k])) ++k;
                        if (span_equal({value+start,k-start},wanted)) found = true;
                    }
                }
            }
            if (!found) return false;
        } else if (type == ':') {
            Span arg{};
            if (p < v.size && v.data[p] == '(') {
                b = ++p; while (p < v.size && v.data[p] != ')') ++p;
                arg = trimmed(v.data+b,p-b); ++p;
            }
            if (value_is(name,"not")) { if (compound(d,id,arg,budget) || !budget) return false; continue; }
            bool reverse = value_is(name,"last-child") || value_is(name,"last-of-type") || value_is(name,"nth-last-child");
            bool of_type = value_is(name,"first-of-type") || value_is(name,"last-of-type");
            int index = 1;
            for (uint16_t sib = reverse ? d.nodes[id].next : d.nodes[id].previous; sib;
                 sib = reverse ? d.nodes[sib].next : d.nodes[sib].previous) {
                if (!budget) return false;
                --budget;
                if (!d.nodes[sib].is_text && (!of_type || equal(d.str(d.nodes[id].tag),d.str(d.nodes[sib].tag)))) ++index;
            }
            if (value_is(name,"nth-child") || value_is(name,"nth-last-child")) { if (!nth(arg,index)) return false; }
            else if (index != 1) return false;
            if (value_is(name,"only-child"))
                for (uint16_t sib = d.nodes[id].next; sib; sib = d.nodes[sib].next) {
                    if (!budget) return false;
                --budget;
                    if (!d.nodes[sib].is_text) return false;
                }
        } else return false;
    }
    return true;
}
inline bool match_part(const Document &d, uint16_t id, const Selector &s, unsigned part, uint32_t &budget) {
    if (!budget) return false;
    --budget;
    if (!compound(d,id,s.parts[part].compound,budget)) return false;
    if (!part) return true;
    char rel = s.parts[part].relation;
    uint16_t candidate = rel == '+' || rel == '~' ? d.nodes[id].previous : d.nodes[id].parent;
    for (unsigned steps = 0; candidate && steps < MaxNodes && budget; ++steps) {
        if (!d.nodes[candidate].is_text) {
            if (match_part(d,candidate,s,part-1,budget)) return true;
            if (rel == '+' || rel == '>') return false;
        }
        candidate = rel == '+' || rel == '~' ? d.nodes[candidate].previous : d.nodes[candidate].parent;
    }
    return false;
}
inline bool matches(const Document &d, uint16_t id, const Selector &s, uint32_t &budget) {
    return s.valid && match_part(d,id,s,s.count-1,budget);
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
inline void parse_css(Document &d, const char *css, size_t size, uint32_t order = 0, uint32_t base_url = 0, bool user_agent = false) {
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
        for (unsigned i=0;i<count;++i) { d.declarations[first+i].base_url=base_url; d.declarations[first+i].user_agent=user_agent; }
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
    PLetter, PWord, PTransform, PPosition, PLeft, PTop, PRight, PBottom, PList, PBorderSpacing, PCollapse,
    PBackgroundImage, PBackgroundSize, PBackgroundPosition, PBackgroundRepeat,
    PCount
};
struct Winner {
    Span value{};
    uint64_t order{};
    int specificity{-1};
    bool important{};
    uint32_t base_url{}; bool user_agent{};
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
                                            "box-sizing", "letter-spacing", "word-spacing", "text-transform", "position",
                                            "left", "top", "right", "bottom", "list-style-type", "border-spacing", "border-collapse", "background-image", "background-size", "background-position", "background-repeat"};
    for (int i = 0; i < PCount; ++i)
        if (equal(name, names[i]))
            return i;
    return -1;
}
inline void offer(Winner *w, int p, Span value, int specificity, uint64_t order, bool important,
                  uint32_t base_url = 0, bool user_agent = false) {
    if (p < 0 || p >= PCount) return;
    auto &old=w[p];
    unsigned rank=important?(user_agent?3U:2U):(user_agent?0U:1U);
    unsigned old_rank=old.important?(old.user_agent?3U:2U):(old.user_agent?0U:1U);
    if (old.specificity<0 || rank>old_rank || (rank==old_rank &&
        (specificity>old.specificity || (specificity==old.specificity && order>=old.order))))
        old={value,order,specificity,important,base_url,user_agent};
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
    switch (p) {
    case PDisplay:
        return one_of(v, "inline|inline-block|block|none|table|table-row-group|table-header-group|table-footer-group|table-row|table-cell");
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
    case PBackgroundImage:
        return value_is(v,"none") || (v.size>5 && equal_ci(v.data,4,"url(") && v.data[v.size-1]==')');
    case PBackgroundSize: case PBackgroundPosition: {
        Span parts[2]; unsigned count=words(v,parts,2);
        if (!count || count>2) return false;
        for (unsigned i=0;i<count;++i)
            if (!css_length(parts[i],l,p==PBackgroundPosition) &&
                !one_of(parts[i],p==PBackgroundSize ? "cover|contain" : "left|right|top|bottom|center")) return false;
        return true;
    }
    case PBackgroundRepeat: return one_of(v,"repeat|repeat-x|repeat-y|no-repeat");
    case PLetter: case PWord:
        return value_is(v,"normal") || (css_length(v,l,true) && l.unit != Unit::Auto && l.unit != Unit::Percent);
    case PTransform: return one_of(v,"none|uppercase|lowercase|capitalize");
    case PPosition: return one_of(v,"static|relative|absolute");
    case PLeft: case PTop: case PRight: case PBottom: return css_length(v,l,true);
    case PList: return one_of(v,"disc|circle|square|decimal|lower-alpha|upper-alpha|lower-roman|upper-roman|none");
    case PBorderSpacing: return css_length(v,l) && l.unit != Unit::Auto && l.unit != Unit::Percent;
    case PCollapse: return one_of(v,"collapse|separate");
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
            offer(winners, base + i, pieces[map[i]], specificity, order, decl.important, decl.base_url, decl.user_agent);
        return;
    }
    if (equal(name, "overflow") && valid_value(POverflowX, v)) {
        offer(winners, POverflowX, v, specificity, order, decl.important, decl.base_url, decl.user_agent);
        offer(winners, POverflowY, v, specificity, order, decl.important, decl.base_url, decl.user_agent);
    }
    if (equal(name, "background")) {
        uint32_t c;
        bool trans;
        if (color(v, c, trans))
            offer(winners, PBackground, v, specificity, order, decl.important, decl.base_url, decl.user_agent);
        else {
            Span parts[8]; unsigned count=words(v,parts,8);
            if (count<=8) for (unsigned i=0;i<count;++i) {
                if (color(parts[i],c,trans)) offer(winners,PBackground,parts[i],specificity,order,decl.important, decl.base_url, decl.user_agent);
                else if (valid_value(PBackgroundImage,parts[i])) offer(winners,PBackgroundImage,parts[i],specificity,order,decl.important, decl.base_url, decl.user_agent);
                else if (valid_value(PBackgroundRepeat,parts[i])) offer(winners,PBackgroundRepeat,parts[i],specificity,order,decl.important, decl.base_url, decl.user_agent);
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
            offer(winners, PBorderStyle, style, specificity, order, decl.important, decl.base_url, decl.user_agent);
            offer(winners, PBorderColor, col, specificity, order, decl.important, decl.base_url, decl.user_agent);
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
    s.letter_spacing = parent.letter_spacing; s.word_spacing = parent.word_spacing;
    s.transform = parent.transform; s.list_type = parent.list_type;
    s.border_spacing = parent.border_spacing; s.border_collapse = parent.border_collapse;
    if (d.tag(id,"ol")) s.list_type = ListType::Decimal;
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
        if (d.tag(id, "input")) {
            s.width = {160000, Unit::Px};
            const char *type=d.attr(id,"type"); size_t size=length(type,32);
            if (equal_ci(type,size,"checkbox") || equal_ci(type,size,"radio")) {
                s.width=s.height={13000,Unit::Px}; for (auto &p:s.padding) p={0,Unit::Px};
                if(equal_ci(type,size,"radio")) s.radius_length={50000,Unit::Percent};
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
    switch (p) {
    case PDisplay:
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
    case PBackgroundImage: s.background_base=from.background_base; s.background_image=from.background_image; break;
    case PBackgroundSize: s.background_size=from.background_size; break;
    case PBackgroundPosition: s.background_position=from.background_position; break;
    case PBackgroundRepeat: s.background_repeat=from.background_repeat; break;
    case PLetter: s.letter_spacing = from.letter_spacing; break;
    case PWord: s.word_spacing = from.word_spacing; break;
    case PTransform: s.transform = from.transform; break;
    case PPosition: s.position = from.position; break;
    case PLeft: s.left = from.left; break; case PTop: s.top = from.top; break;
    case PRight: s.right = from.right; break; case PBottom: s.bottom = from.bottom; break;
    case PList: s.list_type = from.list_type; break;
    case PBorderSpacing: s.border_spacing = from.border_spacing; break;
    case PCollapse: s.border_collapse = from.border_collapse; break;
    case PBoxSizing:
        s.border_box = from.border_box;
        break;
    default:
        break;
    }
}
inline bool inherited_property(int p) {
    return p == PColor || p == PVisibility || (p >= PFamily && p <= PWhite) ||
           p == PLetter || p == PWord || p == PTransform || p == PList || p == PBorderSpacing || p == PCollapse;
}
inline void apply_property(Style &s, const Style &parent, int p, Span v, int root, uint32_t offset) {
    if (value_is(v, "inherit") || (value_is(v,"unset") && inherited_property(p))) {
        copy_property(s, parent, p);
        return;
    }
    if (value_is(v, "initial") || value_is(v,"unset")) {
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
        else if (value_is(v, "inline-block"))
            s.display = Display::InlineBlock;
        else if (value_is(v, "inline"))
            s.display = Display::Inline;
        else if (value_is(v, "none"))
            s.display = Display::None;
        else if (value_is(v, "table"))
            s.display = Display::Table;
        else if (value_is(v, "table-row-group") || value_is(v, "table-header-group") || value_is(v, "table-footer-group"))
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
        if (color(v, c, tr, &s.color_alpha))
            s.color = c;
        break;
    case PBackground:
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
        else if (color(v, c, tr, &s.border_alpha))
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
    case PBackgroundImage: s.background_image=value_is(v,"none") ? 0 : offset; break;
    case PBackgroundSize: s.background_size=offset; break;
    case PBackgroundPosition: s.background_position=offset; break;
    case PBackgroundRepeat:
        s.background_repeat=value_is(v,"repeat-x") ? 1 : value_is(v,"repeat-y") ? 2 : value_is(v,"no-repeat") ? 3 : 0; break;
    case PLetter: case PWord: {
        int spacing = 0;
        if (css_length(v,l,true)) spacing = clamp64(resolve(l,0,s.font,root),-256,256);
        if (p == PLetter) s.letter_spacing = spacing; else s.word_spacing = spacing;
        break;
    }
    case PTransform:
        s.transform = value_is(v,"uppercase") ? Transform::Upper : value_is(v,"lowercase") ? Transform::Lower :
                      value_is(v,"capitalize") ? Transform::Capitalize : Transform::None; break;
    case PPosition:
        s.position = value_is(v,"relative") ? Position::Relative : value_is(v,"absolute") ? Position::Absolute : Position::Static; break;
    case PLeft: case PTop: case PRight: case PBottom:
        if (css_length(v,l,true)) {
            Length* offsets[] = {&s.left,&s.top,&s.right,&s.bottom}; *offsets[p-PLeft] = l;
        } break;
    case PList: {
        const char* names[] = {"disc","circle","square","decimal","lower-alpha","upper-alpha","lower-roman","upper-roman","none"};
        for (unsigned i=0;i<9;++i) if (value_is(v,names[i])) s.list_type = ListType(i);
        break;
    }
    case PBorderSpacing:
        if (css_length(v,l)) s.border_spacing = clamp64(resolve(l,0,s.font,root),0,128);
        break;
    case PCollapse: s.border_collapse = value_is(v,"collapse"); break;
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
inline void compute_styles(Document &d) {
    uint32_t budget = 4000000;
    Style initial;
    initial.color = d.user_agent.text;
    d.root_font = 16;
    for (uint16_t id = 0; id < d.node_count; ++id) {
        const Node &node = d.nodes[id];
        const Style &parent = id ? d.styles[node.parent] : initial;
        if (node.is_text) {
            d.styles[id] = defaults(d,id,parent);
            d.styles[id].display = Display::Inline;
            d.styles[id].paint_opacity = parent.paint_opacity;
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
                                   ((p == PFamily || p == PBackgroundImage || p == PBackgroundSize || p == PBackgroundPosition) ? uint32_t(v.data - d.arena) : 0));
            }
        for (int p = 0; p < PCount; ++p)
            if (winners[p].specificity >= 0 && p != PFontSize && p != PFamily && p != PWeight &&
                p != PItalic) {
                Span v = winners[p].value;
                apply_property(s, parent, p, v, d.root_font, ((p == PFamily || p == PBackgroundImage || p == PBackgroundSize || p == PBackgroundPosition) ? uint32_t(v.data - d.arena) : 0));
            }
        if (winners[PBackgroundImage].specificity>=0 && !value_is(winners[PBackgroundImage].value,"inherit"))
            s.background_base=winners[PBackgroundImage].base_url;
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
    if (!d.user_agent_loaded) {
        parse_css(d,UserAgentStyleSheet,sizeof(UserAgentStyleSheet)-1,0,0,true);
        d.user_agent_loaded=true;
    }
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
