#pragma once
#include "perf.h"
#include <stddef.h>
#include <stdint.h>
// No OS, networking or GUI dependency. All indices refer to this document.
namespace Husarz {
constexpr uint32_t MaxSource = 256 * 1024, MaxNodes = 2048, MaxDepth = 64, MaxAttrs = 8192;
constexpr uint32_t MaxAttrsPerNode = 24, MaxAttrLength = 2048, MaxText = 16384;
constexpr uint32_t ArenaSize = 2 * 1024 * 1024, MaxRules = 4096, MaxDeclarations = 12288;
constexpr uint32_t MaxFragments = 16384, MaxScripts = 32, MaxScriptSource = 32768;
constexpr uint32_t MaxStylesheetBytes = 128 * 1024, MaxStylesheets = 16, MaxSelectorLength = 512;
constexpr unsigned MaxCssNesting = 8, MaxCustomProperties = 2048, MaxCustomPerElement = 256;
constexpr unsigned MaxVariableName = 96, MaxResolvedValue = 2048, MaxVariableDepth = 16;
enum Limit : uint32_t {
    LimitArena = 1,
    LimitNodes = 2,
    LimitRules = 4,
    LimitDeclarations = 8,
    LimitCssBytes = 16,
    LimitSelector = 32,
    LimitCssDepth = 64,
    LimitVariables = 128,
    LimitStylesheets = 256,
    LimitSelectorWork = 512,
    LimitAttrs = 1024,
    LimitDepth = 2048,
    LimitText = 4096,
    LimitFragments = 8192,
    LimitGenerated = 16384,
    LimitCssValue = 32768
};
inline void (*document_limit_log)(const char *){};
struct CustomProperty {
    uint32_t hash{};
    uint32_t name{}, value{};
    uint16_t next{}, owner{};
    uint64_t order{};
    int specificity{};
    bool important{};
};
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
inline bool token_ci(const char *text, const char *token) {
    size_t n = length(text, MaxAttrLength), p = 0;
    while (p < n) {
        while (p < n && space(text[p]))
            ++p;
        size_t start = p;
        while (p < n && !space(text[p]))
            ++p;
        if (equal_ci(text + start, p - start, token))
            return true;
    }
    return false;
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
enum class Unit : uint8_t { Auto, Px, Percent, Em, Rem, Number, Vw, Vh };
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
enum class Display : uint8_t { Inline, InlineBlock, Block, None, Table, RowGroup, Row, Cell };
enum class WhiteSpace : uint8_t { Normal, Pre, PreWrap, NoWrap };
enum class Align : uint8_t { Left, Center, Right };
enum class Vertical : uint8_t { Baseline, Top, Middle, Bottom };
enum class Transform : uint8_t { None, Upper, Lower, Capitalize };
enum class Position : uint8_t { Static, Relative, Absolute };
enum class ListType : uint8_t {
    Disc,
    Circle,
    Square,
    Decimal,
    LowerAlpha,
    UpperAlpha,
    LowerRoman,
    UpperRoman,
    None
};
enum class Overflow : uint8_t { Visible, Hidden, Auto, Scroll };
struct Style {
    Display display{Display::Inline};
    bool flex{}, flex_column{}, flex_reverse{}, flex_wrap{}, grid{}, flex_min_auto{true};
    uint32_t grid_template{};
    uint8_t justify{}, align_items{}, align_self{255};
    int flex_grow{}, flex_shrink{1000};
    Length flex_basis{}, row_gap{0, Unit::Px}, column_gap{0, Unit::Px};
    WhiteSpace white{WhiteSpace::Normal};
    Align align{Align::Left};
    Vertical vertical{Vertical::Baseline};
    Overflow overflow_x{Overflow::Visible}, overflow_y{Overflow::Visible};
    Transform transform{Transform::None};
    Position position{Position::Static};
    ListType list_type{ListType::Disc};
    Length left{}, top{}, right{}, bottom{};
    int letter_spacing{}, word_spacing{};
    int border_spacing{2};
    bool border_collapse{};
    bool visible{true}, bold{}, italic{}, underline{}, border_box{};
    uint8_t paint_opacity{255};
    uint8_t color_alpha{255}, background_alpha{255}, border_alpha{255};
    uint8_t opacity{255}, border_style{};
    uint32_t color{0x000000}, background{0x00ffffff}, border_color{0x202020};
    bool has_background{}, has_gradient{}, gradient_horizontal{};
    uint32_t gradient_color[2]{};
    uint8_t gradient_alpha[2]{};
    uint8_t object_fit{};
    bool has_shadow{};
    int shadow_x{}, shadow_y{}, shadow_blur{}, shadow_spread{};
    uint32_t shadow_color{};
    uint8_t shadow_alpha{255};
    int font{16}, line{20};
    uint32_t family{},native_family{};
    uint32_t background_base{}, background_image{}, background_size{}, background_position{};
    uint8_t background_repeat{}; // 0 repeat, 1 repeat-x, 2 repeat-y, 3 no-repeat
    Length width{}, height{}, min_width{0, Unit::Px}, max_width{}, min_height{0, Unit::Px}, max_height{};
    Length margin[4]{{0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}},
        padding[4]{{0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}, {0, Unit::Px}};
    int border[4]{};
    uint32_t border_colors[4]{0x202020, 0x202020, 0x202020, 0x202020};
    uint8_t border_alphas[4]{255, 255, 255, 255}, border_styles[4]{};
    Length radius_length{0, Unit::Px};
    int radius{};
};
constexpr unsigned MaxGeneratedBoxes = 256, MaxFontFaces = 64;
struct GeneratedBox {
    uint16_t owner{};
    bool after{};
    uint32_t content{};
    Style style{};
    int x{}, y{}, width{}, height{};
};
struct FontFace {
    uint32_t family{}, sources{}, base_url{};
};
struct Attribute {
    uint32_t name{}, value{};
};
struct Node {
    uint16_t parent{}, first{}, last{}, next{}, previous{}, depth{}, attr_start{}, attr_count{};
    uint32_t tag{}, text{}, text_length{};
    uint16_t inline_first{}, inline_count{};
    bool is_text{}, raw{}, detached{};
    uint16_t identity{};
};
struct Declaration {
    uint32_t name{}, value{};
    bool important{};
    uint32_t base_url{};
    bool user_agent{};
};
struct Rule {
    uint32_t selector{};
    uint16_t first{}, count{};
    uint32_t order{};
    int media_min{}, media_max{MaxDimension};
    uint8_t pseudo{};
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
    uint16_t flow_node{};
};
struct Metrics {
    void *user{};
    int (*advance)(void *, uint32_t, int, bool, bool){};
    int (*height)(void *, int){};
    int (*family_advance)(void*,uint32_t,int,bool,bool,uint32_t){};
    int (*family_height)(void*,int,uint32_t){};
};
inline uint32_t (*font_family_resolver)(const char*){};
inline uint32_t document_generation = 0;
// Web defaults are document-owned and have no BursztynUI dependency.
struct UserAgentContext {
    uint32_t canvas{0xffffff}, text{0x000000}, link{0x0000ee};
};
struct FormControl {
    uint16_t node{}, cursor{}, length{}, option{};
    bool checked{};
    uint32_t display{};
    char value[2049]{};
    uint16_t anchor{};
    int scroll_x{}, scroll_y{}, preferred_x{-1};
    bool reveal_caret{true};
};
struct DocumentImage {
    uint16_t node{};
    unsigned width{}, height{};
    const uint32_t *pixels{};
    void *owner{};
    void (*release)(void *){};
    bool background{};
};
// All table storage is document-owned, never allocated from content dimensions.
constexpr unsigned MaxTables = 32, MaxTableRows = 256, MaxTableColumns = 32;
constexpr unsigned MaxTableCells = 1024, MaxTableSpan = 32, MaxTableWork = 2000000;
struct MarginStrut {
    int positive{}, negative{};
    MarginStrut(int value = 0) : positive(value > 0 ? value : 0), negative(value < 0 ? value : 0) {}
    int value() const { return positive + negative; }
    void include(MarginStrut other) {
        if (other.positive > positive)
            positive = other.positive;
        if (other.negative < negative)
            negative = other.negative;
    }
};
struct CollapsedMargins {
    MarginStrut top{}, bottom{};
    bool through{}, suppress_top{}, suppress_bottom{};
};
struct TableCell {
    uint16_t table{}, row{}, column{}, rows{}, columns{};
};
struct TablePlan {
    uint16_t node{}, columns{}, rows{};
    uint16_t row_nodes[MaxTableRows]{};
    uint16_t grid[MaxTableRows * MaxTableColumns]{};
    int minimum[MaxTableColumns]{}, preferred[MaxTableColumns]{}, widths[MaxTableColumns]{};
    int intrinsic_minimum[MaxTableColumns]{}, intrinsic_preferred[MaxTableColumns]{};
    Length explicit_columns[MaxTableColumns]{};
    int heights[MaxTableRows]{}, tops[MaxTableRows]{};
    int total{}, gap{};
};
struct SelectorIndex;
void release_selector_index(SelectorIndex *);
struct TableStorage {
    TablePlan *slabs[MaxTables / 4]{};
    ~TableStorage() { clear(); }
    void clear() {
        for (auto &p : slabs) {
            delete[] p;
            p = nullptr;
        }
    }
    bool ensure(unsigned index) {
        auto &p = slabs[index / 4];
        if (!p)
            p = new TablePlan[4];
        return p != nullptr;
    }
    TablePlan &operator[](unsigned i) { return slabs[i / 4][i % 4]; }
    const TablePlan &operator[](unsigned i) const { return slabs[i / 4][i % 4]; }
    size_t bytes() const {
        size_t n = 0;
        for (auto *p : slabs)
            if (p)
                n += 4 * sizeof(TablePlan);
        return n;
    }
};
struct DirtyRectangles {
    Clip rectangles[8]{};
    unsigned count{};
    bool full{true};
    void clear() {
        count = 0;
        full = false;
    }
    void all() {
        count = 0;
        full = true;
    }
    void add(Clip r) {
        if (full || r.width <= 0 || r.height <= 0)
            return;
        for (unsigned i = 0; i < count;) {
            auto a = rectangles[i];
            if (int64_t(r.x) <= int64_t(a.x) + a.width && int64_t(a.x) <= int64_t(r.x) + r.width &&
                int64_t(r.y) <= int64_t(a.y) + a.height && int64_t(a.y) <= int64_t(r.y) + r.height) {
                int x = r.x < a.x ? r.x : a.x, y = r.y < a.y ? r.y : a.y;
                int right = clamp64(int64_t(r.x) + r.width), bottom = clamp64(int64_t(r.y) + r.height);
                if (int64_t(a.x) + a.width > right)
                    right = clamp64(int64_t(a.x) + a.width);
                if (int64_t(a.y) + a.height > bottom)
                    bottom = clamp64(int64_t(a.y) + a.height);
                r = {x, y, right - x, bottom - y};
                rectangles[i] = rectangles[--count];
                i = 0;
            } else
                ++i;
        }
        if (count == 8) {
            all();
            return;
        }
        rectangles[count++] = r;
    }
};
struct Document {
#if HUSARZ_PERF
    mutable Performance perf{};
#endif
    UserAgentContext user_agent{};
    FormControl controls[64]{};
    unsigned control_count{};
    uint16_t focused_control{}, open_select{};
    DocumentImage images[32]{};
    unsigned image_count{};
    size_t image_bytes{};
    ~Document() {
        release_images();
        release_selector_index(selector_index);
    }
    void release_images() {
        for (unsigned i = 0; i < image_count; ++i)
            if (images[i].release)
                images[i].release(images[i].owner);
        image_count = 0;
        image_bytes = 0;
    }
    const DocumentImage *image(uint16_t node, bool background = false) const {
        for (unsigned i = 0; i < image_count; ++i)
            if (images[i].node == node && images[i].background == background)
                return &images[i];
        return nullptr;
    }
    Node nodes[MaxNodes];
    uint16_t identity_index[MaxNodes]{};
    Attribute attrs[MaxAttrs];
    Style styles[MaxNodes];
    Box boxes[MaxNodes];
    int offset_x[MaxNodes]{}, offset_y[MaxNodes]{};
    int flex_width[MaxNodes]{}, flex_dx[MaxNodes]{}, flex_dy[MaxNodes]{};
    TableStorage tables{};
    uint16_t table_count{}, table_cells{};
    TableCell table_cell[MaxNodes]{};
    int table_shift[MaxNodes]{};
    CollapsedMargins margins[MaxNodes]{};
    int collapse_width[MaxNodes]{};
    uint32_t table_work{};
    SelectorIndex *selector_index{};
    CustomProperty custom[MaxCustomProperties]{};
    uint16_t custom_heads[MaxNodes]{};
    unsigned custom_count{};
    GeneratedBox generated[MaxGeneratedBoxes]{};
    unsigned generated_count{};
    FontFace fonts[MaxFontFaces]{};
    unsigned font_count{};
    bool has_media{}, has_viewport_units{}, has_pseudo{};
    uint32_t limits{}, unsupported_selectors{}, unknown_at_rules{};
    int style_viewport_width{800}, style_viewport_height{600};
    void limit(uint32_t flag) {
        bool first = !(limits & flag);
        limits |= flag;
        truncated = true;
        if (!first || !document_limit_log)
            return;
        const char *name = "UNKNOWN";
        unsigned usage = 0, maximum = 0;
        switch (flag) {
        case LimitArena:
            name = "TEXT_ARENA";
            usage = arena_used;
            maximum = ArenaSize;
            break;
        case LimitNodes:
            name = "DOM_NODES";
            usage = node_count;
            maximum = MaxNodes;
            break;
        case LimitAttrs:
            name = "DOM_ATTRIBUTES";
            usage = attr_count;
            maximum = MaxAttrs;
            break;
        case LimitDepth:
            name = "DOM_DEPTH";
            maximum = MaxDepth;
            break;
        case LimitRules:
            name = "CSS_RULES";
            usage = rule_count;
            maximum = MaxRules;
            break;
        case LimitDeclarations:
            name = "CSS_DECLARATIONS";
            usage = declaration_count;
            maximum = MaxDeclarations;
            break;
        case LimitCssBytes:
            name = "STYLESHEET_BYTES";
            maximum = MaxStylesheetBytes;
            break;
        case LimitSelector:
            name = "SELECTOR_BYTES";
            maximum = MaxSelectorLength;
            break;
        case LimitCssDepth:
            name = "CSS_CONDITIONAL_DEPTH";
            maximum = MaxCssNesting;
            break;
        case LimitVariables:
            name = "CUSTOM_PROPERTIES";
            usage = custom_count;
            maximum = MaxCustomProperties;
            break;
        case LimitStylesheets:
            name = "STYLESHEETS_OR_RESOURCE_QUEUE";
            maximum = MaxStylesheets;
            break;
        case LimitSelectorWork:
            name = "SELECTOR_OR_FLEX_WORK";
            break;
        case LimitFragments:
            name = "LAYOUT_FRAGMENTS";
            usage = fragment_count;
            maximum = MaxFragments;
            break;
        case LimitGenerated:
            name = "GENERATED_BOXES";
            usage = generated_count;
            maximum = MaxGeneratedBoxes;
            break;
        case LimitCssValue:
            name = "CSS_VALUE_BYTES";
            maximum = MaxAttrLength;
            break;
        case LimitText:
            name = "TEXT_BYTES";
            maximum = MaxText;
            break;
        }
        char line[160];
        size_t n = 0;
        auto text = [&](const char *s) {
            while (*s && n < 130)
                line[n++] = *s++;
        };
        auto number = [&](unsigned v) {
            char b[16];
            unsigned k = 0;
            do {
                b[k++] = char('0' + v % 10);
                v /= 10;
            } while (v);
            while (k)
                line[n++] = b[--k];
        };
        text("[HUSARZ LIMIT] ");
        text(name);
        text(" used=");
        number(usage);
        text(" max=");
        number(maximum);
        line[n++] = '\n';
        line[n] = 0;
        document_limit_log(line);
    }
    Rule rules[MaxRules];
    Declaration declarations[MaxDeclarations];
    Fragment fragments[MaxFragments];
    Script scripts[MaxScripts];
    struct ComputedText {
        uint64_t hash{};
        uint32_t offset{};
        uint16_t bytes{};
    };
    ComputedText computed_text[1024]{};
    char arena[ArenaSize];
    uint32_t node_count{}, attr_count{}, arena_used{}, rule_count{}, declaration_count{}, fragment_count{},
        script_count{};
    uint16_t next_node_identity{};
    uint32_t title{}, generation{}, revision{}, layout_revision{}, style_revision{}, base_url{};
    bool truncated{}, styles_dirty{true}, user_agent_loaded{};
    int layout_width{}, height{}, root_font{16};
    uint32_t parse_count{}, reflow_count{}, paint_revision{}, table_plan_revision{};
    uint8_t dirty{15};
    uint8_t node_dirty[MaxNodes]{};
    DirtyRectangles damage{};
    void invalidate(uint16_t node, uint8_t flags) {
        if (flags & 8) {
            flags |= 4;
            styles_dirty = true;
        }
        if (flags & 4)
            flags |= 3;
        if (flags & 4)
            ++revision;
        dirty |= flags;
        if (node < node_count)
            node_dirty[node] |= flags;
        ++paint_revision;
        if (flags & 4)
            damage.all();
        else if (node < node_count) {
            auto &b = boxes[node];
            damage.add({b.x, b.y, b.width, b.height});
        }
    }
    Document() { reset(); }
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;
    void reset() {
        release_images();
        tables.clear();
        release_selector_index(selector_index);
        selector_index = nullptr;
        control_count = 0;
        focused_control = open_select = 0;
        table_count = table_cells = 0;
        table_work = 0;
        node_count = 1;
        next_node_identity = 0;
        for (auto &index : identity_index)
            index = 0;
        attr_count = rule_count = declaration_count = fragment_count = script_count = 0;
        for (auto &text : computed_text)
            text = {};
        arena_used = 1;
        arena[0] = 0;
        nodes[0] = {};
        title = base_url = 0;
        has_media = has_viewport_units = has_pseudo = false;
        generated_count = font_count = 0;
        limits = custom_count = unsupported_selectors = unknown_at_rules = 0;
        for (auto &h : custom_heads)
            h = 0;
        truncated = false;
        dirty = 15;
        damage.all();
        ++paint_revision;
        for (auto &f : node_dirty)
            f = 0;
        styles_dirty = true;
        user_agent_loaded = false;
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
            this->limit(LimitText);
        }
        if (n + 1 > ArenaSize - arena_used) {
            this->limit(LimitArena);
            return 0;
        }
        uint32_t off = arena_used;
        for (size_t i = 0; i < n; ++i)
            arena[arena_used++] = lowercase ? lower(s[i]) : s[i];
        arena[arena_used++] = 0;
        return off;
    }
    uint32_t store_computed(const char *text, size_t bytes, size_t cap = MaxAttrLength) {
        if (!text || bytes > cap)
            return 0;
        uint64_t hash = UINT64_C(1469598103934665603);
        for (size_t i = 0; i < bytes; ++i)
            hash = (hash ^ uint8_t(text[i])) * UINT64_C(1099511628211);
        ComputedText *empty = nullptr;
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            auto &slot = computed_text[(hash + attempt) & 1023];
            if (!slot.offset) {
                empty = &slot;
                break;
            }
            if (slot.hash != hash || slot.bytes != bytes)
                continue;
            bool same = true;
            for (size_t i = 0; i < bytes; ++i)
                if (arena[slot.offset + i] != text[i]) {
                    same = false;
                    break;
                }
            if (same)
                return slot.offset;
        }
        uint32_t offset = store(text, bytes, cap);
        if (offset && empty)
            *empty = {hash, offset, uint16_t(bytes)};
        return offset;
    }
    uint16_t add(uint16_t parent, bool text = false) {
        if (node_count == MaxNodes) {
            limit(LimitNodes);
            return 0;
        }
        uint16_t id = uint16_t(node_count++);
        Node &n = nodes[id];
        n = {};
        n.identity = ++next_node_identity;
        identity_index[n.identity] = id;
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
        if (id >= node_count)
            return false;
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
