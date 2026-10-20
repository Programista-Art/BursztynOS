#pragma once
#include <stddef.h>
#include <stdint.h>
// No OS, networking or GUI dependency. All indices refer to this document.
namespace Husarz {
constexpr uint32_t MaxSource = 256 * 1024, MaxNodes = 2048, MaxDepth = 64, MaxAttrs = 8192;
constexpr uint32_t MaxAttrsPerNode = 24, MaxAttrLength = 2048, MaxText = 16384;
constexpr uint32_t ArenaSize = 768 * 1024, MaxRules = 384, MaxDeclarations = 4096;
constexpr uint32_t MaxFragments = 16384, MaxScripts = 32, MaxScriptSource = 32768;
constexpr int MaxDimension = 8000000, MaxFont = 128;
inline int clamp64(int64_t n, int lo = 0, int hi = MaxDimension) {
    return n < lo ? lo : n > hi ? hi : int(n);
}
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
inline bool equal(const char *a, const char *b) {
    if (!a || !b)
        return false;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
inline bool equal_ci(const char *a, size_t n, const char *b) {
    size_t i = 0;
    for (; i < n && b[i]; ++i)
        if (lower(a[i]) != b[i])
            return false;
    return i == n && !b[i];
}
inline size_t length(const char *s, size_t cap) {
    size_t n = 0;
    if (s)
        while (n < cap && s[n])
            ++n;
    return n;
}
inline bool ident(char c) {
    return (lower(c) >= 'a' && lower(c) <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           uint8_t(c) >= 128;
}
inline uint32_t utf8(const char *s, size_t n, size_t &used) {
    used = 1;
    if (!n)
        return 0xfffd;
    uint8_t c = uint8_t(s[0]);
    if (c < 128)
        return c ? c : 0xfffd;
    unsigned k = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!k || k > n)
        return 0xfffd;
    uint32_t cp = c & ((1U << (7 - k)) - 1);
    for (unsigned i = 1; i < k; ++i) {
        uint8_t b = uint8_t(s[i]);
        if ((b & 0xc0) != 0x80)
            return 0xfffd;
        cp = (cp << 6) | (b & 63);
    }
    if ((k == 2 && cp < 128) || (k == 3 && cp < 2048) || (k == 4 && cp < 65536) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff))
        return 0xfffd;
    used = k;
    return cp;
}
inline unsigned encode(uint32_t cp, char *out) {
    if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        cp = 0xfffd;
    if (cp < 128) {
        out[0] = char(cp);
        return 1;
    }
    if (cp < 2048) {
        out[0] = char(0xc0 | (cp >> 6));
        out[1] = char(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 65536) {
        out[0] = char(0xe0 | (cp >> 12));
        out[1] = char(0x80 | ((cp >> 6) & 63));
        out[2] = char(0x80 | (cp & 63));
        return 3;
    }
    out[0] = char(0xf0 | (cp >> 18));
    out[1] = char(0x80 | ((cp >> 12) & 63));
    out[2] = char(0x80 | ((cp >> 6) & 63));
    out[3] = char(0x80 | (cp & 63));
    return 4;
}
enum class Unit : uint8_t { Auto, Px, Percent, Em, Rem, Number };
struct Length {
    int value{};
    Unit unit{Unit::Auto};
};
// Fixed point: value / 1000.
inline int resolve(Length v, int basis, int font, int root, int automatic = 0) {
    int b = v.unit == Unit::Percent ? basis : v.unit == Unit::Em ? font : v.unit == Unit::Rem ? root : 1;
    return v.unit == Unit::Auto
               ? automatic
               : clamp64(int64_t(v.value) * b / (v.unit == Unit::Percent ? 100000 : 1000), -MaxDimension);
}
enum class Display : uint8_t { Inline, Block, None, Table, Row, Cell };
enum class WhiteSpace : uint8_t { Normal, Pre, PreWrap, NoWrap };
enum class Align : uint8_t { Left, Center, Right };
enum class Vertical : uint8_t { Baseline, Top, Middle, Bottom };
enum class Overflow : uint8_t { Visible, Hidden, Auto, Scroll };
struct Style {
    Display display{Display::Inline};
    WhiteSpace white{WhiteSpace::Normal};
    Align align{Align::Left};
    Vertical vertical{Vertical::Baseline};
    Overflow overflow_x{Overflow::Visible}, overflow_y{Overflow::Visible};
    bool visible{true}, bold{}, italic{}, underline{}, border_box{};
    uint8_t paint_opacity{255};
    uint8_t opacity{255}, border_style{};
    uint32_t color{0x202020}, background{0x00ffffff}, border_color{0x202020};
    bool has_background{};
    int font{16}, line{20};
    uint32_t family{};
    Length width{}, height{}, min_width{0, Unit::Px}, max_width{}, min_height{0, Unit::Px}, max_height{};
    Length margin[4]{{0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}},
        padding[4]{{0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}};
    int border[4]{};
    Length radius_length{0, Unit::Px};
    int radius{};
};
struct Attribute {
    uint32_t name{}, value{};
};
struct Node {
    uint16_t parent{}, first{}, last{}, next{}, previous{}, depth{}, attr_start{}, attr_count{};
    uint32_t tag{}, text{}, text_length{};
    uint16_t inline_first{}, inline_count{};
    bool is_text{}, raw{};
};
struct Declaration {
    uint32_t name{}, value{};
    bool important{};
};
struct Rule {
    uint32_t selector{};
    uint16_t first{}, count{};
    uint32_t order{};
};
struct Script {
    uint16_t node{};
    uint32_t source{}, url{}, type{};
    bool truncated{};
};
struct Box {
    int x{}, y{}, width{}, height{}, content_x{}, content_y{}, content_width{}, content_height{};
};
struct Clip {
    int x{}, y{}, width{MaxDimension}, height{MaxDimension};
};
inline Clip intersect(Clip a, Clip b) {
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int r = clamp64(int64_t(a.x) + a.width), s = clamp64(int64_t(b.x) + b.width);
    if (s < r)
        r = s;
    int d = clamp64(int64_t(a.y) + a.height), e = clamp64(int64_t(b.y) + b.height);
    if (e < d)
        d = e;
    return {x, y, r > x ? r - x : 0, d > y ? d - y : 0};
}
struct Fragment {
    uint16_t node{};
    uint32_t text{}, length{};
    int x{}, y{}, width{}, height{};
    Clip clip{};
};
struct Metrics {
    void *user{};
    int (*advance)(void *, uint32_t, int, bool, bool){};
    int (*height)(void *, int){};
};
inline uint32_t document_generation = 0;
struct Document {
    Node nodes[MaxNodes];
    Attribute attrs[MaxAttrs];
    Style styles[MaxNodes];
    Box boxes[MaxNodes];
    Rule rules[MaxRules];
    Declaration declarations[MaxDeclarations];
    Fragment fragments[MaxFragments];
    Script scripts[MaxScripts];
    char arena[ArenaSize];
    uint32_t node_count{}, attr_count{}, arena_used{}, rule_count{}, declaration_count{}, fragment_count{},
        script_count{};
    uint32_t title{}, generation{}, revision{}, layout_revision{}, style_revision{}, base_url{};
    bool truncated{}, styles_dirty{true};
    int layout_width{}, height{}, root_font{16};
    uint32_t parse_count{}, reflow_count{};
    Document() { reset(); }
    void reset() {
        node_count = 1;
        attr_count = rule_count = declaration_count = fragment_count = script_count = 0;
        arena_used = 1;
        arena[0] = 0;
        nodes[0] = {};
        title = base_url = 0;
        truncated = false;
        styles_dirty = true;
        layout_width = 0;
        height = 0;
        generation = ++document_generation;
        if (!generation)
            generation = ++document_generation;
        ++revision;
    }
    const char *str(uint32_t off) const { return off < arena_used ? arena + off : arena; }
    uint32_t store(const char *s, size_t n, size_t limit = MaxText, bool lowercase = false) {
        if (!s)
            return 0;
        if (n > limit) {
            n = limit;
            truncated = true;
        }
        if (n + 1 > ArenaSize - arena_used) {
            truncated = true;
            return 0;
        }
        uint32_t off = arena_used;
        for (size_t i = 0; i < n; ++i)
            arena[arena_used++] = lowercase ? lower(s[i]) : s[i];
        arena[arena_used++] = 0;
        return off;
    }
    uint16_t add(uint16_t parent, bool text = false) {
        if (node_count == MaxNodes) {
            truncated = true;
            return 0;
        }
        uint16_t id = uint16_t(node_count++);
        Node &n = nodes[id];
        n = {};
        n.parent = parent;
        n.is_text = text;
        n.depth = nodes[parent].depth + 1;
        Node &p = nodes[parent];
        n.previous = p.last;
        if (p.last)
            nodes[p.last].next = id;
        else
            p.first = id;
        p.last = id;
        return id;
    }
    const char *attr(uint16_t id, const char *name) const {
        if (id >= node_count)
            return "";
        const Node &n = nodes[id];
        for (unsigned i = 0; i < n.attr_count; ++i) {
            const auto &a = attrs[n.attr_start + i];
            if (equal(str(a.name), name))
                return str(a.value);
        }
        return "";
    }
    bool has_attr(uint16_t id, const char *name) const {
        const Node &n = nodes[id];
        for (unsigned i = 0; i < n.attr_count; ++i)
            if (equal(str(attrs[n.attr_start + i].name), name))
                return true;
        return false;
    }
    bool tag(uint16_t id, const char *name) const {
        return id < node_count && equal(str(nodes[id].tag), name);
    }
};
} // namespace Husarz
