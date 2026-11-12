#pragma once
#include "css.h"
#include "forms.h"
#include "bidi.h"
#include "../../bsf/bsf.h"
#if defined(BSF_NATIVE)
#include "../../bsf/runtime.h"
#endif
namespace Husarz {
struct Flow {
    uint16_t node{};
    int x{}, y{}, width{}, pen{}, line_height{}, line_first{};
    MarginStrut previous_margin{};
    int row_height{}, column{}, columns{1};
    bool pending_space{};
    // BSF 2.1: the collapsed space that pending_space stands for (its bidi
    // level becomes the gap level of the next fragment).
    uint16_t space_node{};
    uint32_t space_offset{};
    int pre_gap{}; // preserved white space advanced since the last fragment
};
inline int advance(Metrics m, uint32_t cp, const Style &s) {
#if defined(BSF_NATIVE)
    if(m.family_advance && (BSF::combining(cp)||BSF::default_ignorable(cp))) return 0;
#endif
    int base = m.family_advance?m.family_advance(m.user,cp,s.font,s.bold,s.italic,s.native_family):m.advance ? m.advance(m.user, cp, s.font, s.bold, s.italic) : (s.font * 9 + 8) / 16;
    return clamp64(int64_t(base) + s.letter_spacing + (cp == ' ' ? s.word_spacing : 0), 0, 2048);
}
inline uint32_t transformed(uint32_t cp, Transform t, bool word_start) {
    bool upper = t == Transform::Upper || (t == Transform::Capitalize && word_start);
    if (upper && cp >= 'a' && cp <= 'z')
        return cp - 32;
    if (t == Transform::Lower && cp >= 'A' && cp <= 'Z')
        return cp + 32;
    static constexpr uint32_t lo[] = {0x105, 0x107, 0x119, 0x142, 0x144, 0xf3, 0x15b, 0x17a, 0x17c};
    static constexpr uint32_t hi[] = {0x104, 0x106, 0x118, 0x141, 0x143, 0xd3, 0x15a, 0x179, 0x17b};
    for (unsigned i = 0; i < 9; ++i) {
        if (upper && cp == lo[i])
            return hi[i];
        if (t == Transform::Lower && cp == hi[i])
            return lo[i];
    }
    return cp;
}
// Source offsets stay in logical UTF-8 order. This is fallback, not shaping.
struct TextCluster { size_t begin{},end{}; uint32_t face{}; };
inline TextCluster text_cluster(const char* text,size_t size,size_t at,Metrics m,const Style& s,bool first=false){
    size_t used;utf8(text+at,size-at,used);TextCluster c{at,at+used,0};
#if defined(BSF_NATIVE)
    c.end=BSF::next_cluster(text,at,size);
    if(m.family_advance){
        const char* bytes=text+at;size_t length=c.end-at;char changed[128];
        if(s.transform!=Transform::None){
            size_t n=0;
            for(size_t p=at;p<c.end;){size_t z;uint32_t cp=utf8(text+p,c.end-p,z);char b[4];unsigned k=encode(transformed(cp,s.transform,first&&p==at),b);
                if(k>sizeof(changed)-n)return c;
                for(unsigned j=0;j<k;++j)changed[n++]=b[j];p+=z;
            }bytes=changed;length=n;
        }
        c.face=BSF::resolved_cluster(bytes,length,s.bold,BSF::family_name(s.native_family),s.italic);
    }
#else
    (void)m;(void)s;(void)first;
#endif
    return c;
}
inline bool text_space(const char* text,size_t size,size_t at){
    if(!space(text[at]))return false;
#if defined(BSF_NATIVE)
    return BSF::next_cluster(text,at,size)==at+1;
#else
    (void)size;return true;
#endif
}
inline int cluster_step(Metrics m,uint32_t cp,const Style& s,uint32_t face){
#if defined(BSF_NATIVE)
    if(face){
        if(BSF::combining(cp)||BSF::default_ignorable(cp))return 0;
        return clamp64(int64_t(BSF::advance_face(cp,face,s.font))+s.letter_spacing+(cp==' '?s.word_spacing:0),0,2048);
    }
#else
    (void)face;
#endif
    return advance(m,cp,s);
}
inline int cluster_width(const char* text,TextCluster c,Metrics m,const Style& s,bool first=false){
    int w=0;
    for(size_t p=c.begin;p<c.end;){size_t z;uint32_t cp=utf8(text+p,c.end-p,z);
        w=clamp64(int64_t(w)+cluster_step(m,transformed(cp,s.transform,first&&p==c.begin),s,c.face));p+=z;
    }return w;
}
#if HUSARZ_BIDI
// ---- BSF 2.1 shaped text runs ------------------------------------------
// Fragments that need complex layout (bidi level > 0, RTL/joining scripts,
// marks, controls) are measured and painted from one BSF TextLayout with an
// explicit direction equal to the fragment's (uniform) level parity, CSS
// letter/word spacing and the cursive context of adjacent fragments. Plain
// LTR fragments keep the per-cluster path and its exact geometry.
inline bool fragment_complex(const char *text, size_t n, uint8_t level) {
    return (level & 1) || BSF::needs_complex_layout(text, n);
}
// UAX #9 never gives L an odd level or R/AL an even one, so a conflict
// means the run was overridden (bidi-override / isolate-override).
inline bool fragment_overridden(const char *text, size_t n, uint8_t level) {
    for (size_t p = 0; p < n;) {
        size_t used;
        uint32_t cp = utf8(text + p, n - p, used);
        p += used ? used : 1;
        unsigned k = strong_direction(cp);
        if ((k == 1 && (level & 1)) || (k == 2 && !(level & 1)))
            return true;
    }
    return false;
}
// Logical scalars next to a byte range (for joining context).
inline uint32_t last_scalar(const char *text, size_t n) {
    if (!n)
        return 0;
    size_t p = n - 1;
    while (p && (uint8_t(text[p]) & 0xC0) == 0x80)
        --p;
    size_t used;
    return utf8(text + p, n - p, used);
}
inline uint32_t first_scalar(const char *text, size_t n) {
    if (!n)
        return 0;
    size_t used;
    return utf8(text, n, used);
}
struct ShapedKey {
    uint64_t hash{};
    bool operator==(const ShapedKey &o) const { return hash == o.hash; }
};
inline uint64_t shaped_hash(const char *text, size_t n, const Style &s, uint8_t level, uint32_t before, uint32_t after,
                            bool forced) {
    uint64_t h = UINT64_C(1469598103934665603);
    auto mix = [&](uint64_t v) { for (unsigned i = 0; i < 8; ++i) { h = (h ^ (v & 255)) * UINT64_C(1099511628211); v >>= 8; } };
    for (size_t i = 0; i < n; ++i)
        h = (h ^ uint8_t(text[i])) * UINT64_C(1099511628211);
    mix(n); mix(uint64_t(s.font) | (uint64_t(s.bold) << 16) | (uint64_t(s.italic) << 17) | (uint64_t(level & 1) << 18) |
               (uint64_t(uint8_t(s.transform)) << 20));
    mix(s.native_family); mix(uint32_t(s.letter_spacing)); mix(uint32_t(s.word_spacing)); mix(before); mix(after);
    mix(forced);
    mix(BSF::generation());
    return h ? h : 1;
}
// Transformed copy (text-transform keeps UTF-8 lengths for the mapped
// letters, so byte offsets stay valid). Returns false if it does not fit.
inline bool transformed_copy(const char *text, size_t n, Transform t, char *out, size_t cap) {
    if (n > cap)
        return false;
    for (size_t p = 0; p < n;) {
        size_t used;
        uint32_t cp = utf8(text + p, n - p, used);
        if (!used)
            used = 1;
        char b[4];
        unsigned k = encode(transformed(cp, t, p == 0), b);
        if (k != used) { // unexpected length change: keep the source bytes
            for (size_t i = 0; i < used; ++i)
                out[p + i] = text[p + i];
        } else
            for (unsigned i = 0; i < k; ++i)
                out[p + i] = b[i];
        p += used;
    }
    return true;
}
struct ShapedLayoutSlot {
    BSF::TextLayout layout;
    uint64_t hash{}, used{};
    bool valid{};
};
constexpr unsigned ShapedWidthSlots = 1024, ShapedLayoutSlots = 32;
struct ShapedWidth { uint64_t hash{}; int width{}; };
inline ShapedWidth shaped_widths[ShapedWidthSlots];
inline ShapedLayoutSlot shaped_layouts[ShapedLayoutSlots];
inline uint64_t shaped_clock{}, shaped_width_hits{}, shaped_width_misses{}, shaped_layout_hits{}, shaped_layout_misses{};
inline char shaped_buffer[MaxText];
// Bounded per-process LRU of fragment layouts (exact text compare).
inline const BSF::TextLayout *shaped_layout(const char *text, size_t n, const Style &s, uint8_t level, uint32_t before,
                                            uint32_t after, bool forced = false) {
    if (!n || n + 6 > MaxText || !BSF::ready())
        return nullptr;
    uint64_t h = shaped_hash(text, n, s, level, before, after, forced); // over the DOM bytes only
    const char *source = text;
    size_t prefix = forced ? 3 : 0;
    if (s.transform != Transform::None || forced) {
        if (!transformed_copy(text, n, s.transform, shaped_buffer + prefix, sizeof shaped_buffer - 6))
            return nullptr;
        source = shaped_buffer;
    }
    if (forced) { // LRO/RLO ... PDF: the run keeps its overridden direction
        uint32_t open = (level & 1) ? 0x202E : 0x202D;
        encode(open, shaped_buffer);
        encode(0x202C, shaped_buffer + 3 + n);
        n += 6;
    }
    ShapedLayoutSlot *victim = &shaped_layouts[0];
    for (auto &slot : shaped_layouts) {
        if (slot.valid && slot.hash == h && slot.layout.bytes() == n) {
            bool same = true;
            const char *own = slot.layout.text();
            for (size_t i = 0; i < n && same; ++i)
                same = own[i] == source[i];
            if (same) {
                slot.used = ++shaped_clock;
                ++shaped_layout_hits;
                return &slot.layout;
            }
        }
        if (!slot.valid || (victim->valid && slot.used < victim->used))
            victim = &slot;
    }
    ++shaped_layout_misses;
    victim->valid = false;
    BSF::LayoutRequest r;
    r.px = s.font;
    r.bold = s.bold;
    r.italic = s.italic;
    r.family = BSF::family_name(s.native_family);
    r.direction = (level & 1) ? BSF::BaseDirection::RTL : BSF::BaseDirection::LTR;
    r.letter_spacing = int32_t(clamp64(int64_t(s.letter_spacing) * BSF::Fixed, -INT32_MAX, INT32_MAX));
    r.word_spacing = int32_t(clamp64(int64_t(s.word_spacing) * BSF::Fixed, -INT32_MAX, INT32_MAX));
    r.context_before = before;
    r.context_after = after;
    if (!BSF::build_layout(victim->layout, source, n, r))
        return nullptr;
    victim->hash = h;
    victim->used = ++shaped_clock;
    victim->valid = true;
    return &victim->layout;
}
// Shaped advance of a fragment in px (same layout paint will use).
inline int shaped_width(const char *text, size_t n, const Style &s, uint8_t level, uint32_t before, uint32_t after,
                        bool &ok, bool forced = false) {
    ok = false;
    uint64_t h = shaped_hash(text, n, s, level, before, after, forced) ^ 0x5bd1e995u;
    auto &slot = shaped_widths[h % ShapedWidthSlots];
    if (slot.hash == h) {
        ++shaped_width_hits;
        ok = true;
        return slot.width;
    }
    ++shaped_width_misses;
    const auto *L = shaped_layout(text, n, s, level, before, after, forced);
    if (!L)
        return 0;
    int64_t w = (L->width() + BSF::Fixed / 2) / BSF::Fixed;
    slot = {h, int(clamp64(w, 0, MaxDimension))};
    ok = true;
    return slot.width;
}
#endif
inline int font_height(Metrics m, const Style &s) {
    return clamp64(m.family_height?m.family_height(m.user,s.font,s.native_family):m.height ? m.height(m.user, s.font) : s.font, 1, MaxFont * 2);
}
#if HUSARZ_BIDI
// UAX #9 L2 for one formatted line: the line's fragments (logical order)
// and the collapsed spaces between them are items with their resolved
// levels; reversing them per L2 gives the visual order, and x positions are
// reassigned inside the same horizontal extent. Fragments separated by
// pen space that is not a recorded gap (inline-block or form-control boxes,
// which are not fragments) form separate segments that are reordered
// independently, so no box can overlap text (documented limitation).
// Image fragments move with their boxes. Lines whose fragments are all
// level 0 are left untouched.
inline Work bidi_line_work(Document &d, Flow &f) {
    constexpr unsigned MaxLineItems = 512;
    uint32_t frag[MaxLineItems];
    unsigned count = 0;
    bool any = false;
    for (uint32_t i = uint32_t(f.line_first); i < d.fragment_count; ++i) {
        co_await WorkPoint{1};
        if (d.fragments[i].flow_node != f.node)
            continue;
        if (count == MaxLineItems) {
            d.limit(LimitBidi);
            co_return;
        }
        frag[count++] = i;
        any |= d.fragments[i].level != 0 || d.fragments[i].gap_level != 0;
    }
    if (!any || count < 1)
        co_return;
    uint8_t levels[MaxLineItems * 2];
    int widths[MaxLineItems * 2];
    int32_t owner[MaxLineItems * 2]; // fragment index or -1 for a gap
    uint32_t order[MaxLineItems * 2];
    for (unsigned k0 = 0; k0 < count;) {
        unsigned k1 = k0 + 1;
        while (k1 < count) {
            const auto &p = d.fragments[frag[k1 - 1]], &q = d.fragments[frag[k1]];
            if (int64_t(p.x) + p.width + q.gap != q.x)
                break; // unexplained pen space: segment barrier
            ++k1;
        }
        unsigned m = 0;
        for (unsigned k = k0; k < k1; ++k) {
            const auto &g = d.fragments[frag[k]];
            if (k > k0 && g.gap) {
                levels[m] = g.gap_level;
                widths[m] = g.gap;
                owner[m++] = -1;
            }
            levels[m] = g.level;
            widths[m] = g.width;
            owner[m++] = int32_t(frag[k]);
        }
        size_t visual = BSF::bidi_reorder(levels, m, order);
        int x = d.fragments[frag[k0]].x;
        for (size_t v = 0; v < visual; ++v) {
            co_await WorkPoint{1};
            unsigned item = order[v];
            if (owner[item] >= 0) {
                auto &g = d.fragments[uint32_t(owner[item])];
                int dx = x - g.x;
                g.x = x;
                if (dx && !g.text && !g.length && d.tag(g.node, "img")) {
                    auto &b = d.boxes[g.node];
                    b.x = clamp64(int64_t(b.x) + dx, -MaxDimension);
                    b.content_x = clamp64(int64_t(b.content_x) + dx, -MaxDimension);
                }
            }
            x = clamp64(int64_t(x) + widths[item], -MaxDimension);
        }
        k0 = k1;
    }
}
#endif
// CSS text-align start/end against the block direction (or the paragraph
// direction for unicode-bidi: plaintext).
inline Align resolved_align(const Document &d, const Flow &f) {
    const Style &s = d.styles[f.node];
    Align a = s.align;
    if (a != Align::Start && a != Align::End)
        return a;
    bool rtl = s.direction & 1;
    if (s.unicode_bidi == UnicodeBidi::Plaintext)
        for (uint32_t i = uint32_t(f.line_first); i < d.fragment_count; ++i)
            if (d.fragments[i].flow_node == f.node) {
                rtl = d.fragments[i].paragraph_level & 1;
                break;
            }
    return (a == Align::Start) == rtl ? Align::Right : Align::Left;
}
inline Work finish_line_work(Document &d, Flow &f, bool force = false) {
    if (f.pen || force) {
        int h = f.line_height ? f.line_height : d.styles[f.node].line;
        int extra = f.width - f.pen;
        if (extra < 0)
            extra = 0;
#if HUSARZ_BIDI
        if (d.has_bidi)
            co_await bidi_line_work(d, f);
#endif
        Align a = resolved_align(d, f);
        int shift = a == Align::Center ? extra / 2 : a == Align::Right ? extra : 0;
        uint16_t moved = 0;
        for (uint32_t i = uint32_t(f.line_first); i < d.fragment_count; ++i) {
            co_await WorkPoint{1};
            auto &g = d.fragments[i];
            if (g.flow_node != f.node)
                continue;
            const auto &s = d.styles[g.node];
            bool replaced = d.tag(g.node, "input") || d.tag(g.node, "textarea") || d.tag(g.node, "select") ||
                            d.tag(g.node, "img") || d.tag(g.node, "button");
            int box_height = replaced && d.boxes[g.node].height ? d.boxes[g.node].height : g.height;
            int dy = s.vertical == Vertical::Top      ? 0
                     : s.vertical == Vertical::Middle ? (h - box_height) / 2
                                                      : h - box_height;
            if (replaced && d.boxes[g.node].height && moved != g.node) {
                auto &b = d.boxes[g.node];
                b.x = clamp64(int64_t(b.x) + shift, -MaxDimension);
                b.content_x = clamp64(int64_t(b.content_x) + shift, -MaxDimension);
                b.y = clamp64(int64_t(b.y) + (dy > 0 ? dy : 0), -MaxDimension);
                b.content_y = clamp64(int64_t(b.content_y) + (dy > 0 ? dy : 0), -MaxDimension);
                moved = g.node;
            }
            if (dy < 0)
                dy = 0;
            g.x = clamp64(int64_t(g.x) + shift, -MaxDimension);
            g.y = clamp64(int64_t(g.y) + dy, -MaxDimension);
        }
        f.y = clamp64(int64_t(f.y) + h, -MaxDimension);
        f.pen = 0;
        f.line_height = 0;
        f.line_first = int(d.fragment_count);
        f.pending_space = false;
        f.pre_gap = 0;
        f.previous_margin = MarginStrut();
    }
}
inline void finish_line(Document &d, Flow &f, bool force = false) {
    finish_work(finish_line_work(d, f, force));
}

inline void emit(Document &d, Flow &f, uint16_t id, uint32_t off, uint32_t len, int width, Metrics m) {
    const auto &s = d.styles[id];
    if (d.fragment_count == MaxFragments) {
        d.truncated = true;
        return;
    }
    d.fragments[d.fragment_count++] = {
        id, off,   len, clamp64(int64_t(f.x) + f.pen, -MaxDimension), f.y, width, font_height(m, s),
        {}, f.node};
    f.pen = clamp64(int64_t(f.pen) + width);
    int h = s.line;
    if (h < font_height(m, s))
        h = font_height(m, s);
    if (h > f.line_height)
        f.line_height = h;
}
#if HUSARZ_BIDI
// One word [start, end) of text node `id` (no break opportunity inside):
// split where the bidi level changes, measure every part from its shaped
// layout (or the per-cluster path for plain LTR parts), join cursive
// context with an adjacent fragment of the previous node, then place the
// parts like an ordinary word (wrapping before it if needed).
inline Work layout_word_bidi_work(Document &d, Flow &f, uint16_t id, uint32_t offset, size_t start, size_t end,
                                  Metrics m, bool wrap, int cluster_sum) {
    const Style &s = d.styles[id];
    const char *text = d.str(offset);
    struct Part { size_t a, b; uint8_t level, para; int width; bool complex, forced; };
    constexpr unsigned MaxParts = 32;
    Part parts[MaxParts];
    unsigned np = 0;
    for (size_t a = start; a < end && np < MaxParts;) {
        co_await WorkPoint{1};
        uint8_t para = 0, level = text_level(d, id, uint32_t(a), &para);
        size_t b = text_level_end(d, id, uint32_t(a), uint32_t(end));
        if (b <= a || np + 1 == MaxParts)
            b = end;
        // Never split a grapheme: advance to the first boundary >= b
        // (linear in the part, no rescan from the node start).
        size_t g = a;
        while (g < b)
            g = BSF::next_cluster(text, g, end);
        b = g > end ? end : g;
        if (b <= a)
            b = end;
        // Plain even-level LTR text can contain no R/AL, so no override scan.
        bool complex = fragment_complex(text + a, b - a, level);
        bool forced = (complex || (level & 1)) && fragment_overridden(text + a, b - a, level);
        parts[np++] = {a, b, level, para, 0, forced || complex, forced};
        a = b;
    }
    auto measure = [&](Part &q, uint32_t before, uint32_t after) {
        if (q.complex) {
            bool ok = false;
            int w = shaped_width(text + q.a, q.b - q.a, s, q.level, before, after, ok, q.forced);
            if (ok)
                return w;
            q.complex = false; // no usable face: per-cluster fallback
        }
        int w = 0;
        for (size_t k = q.a; k < q.b;) {
            auto c = text_cluster(text, q.b, k, m, s, k == start);
            w = clamp64(int64_t(w) + cluster_width(text, c, m, s, k == start));
            k = c.end;
        }
        return w;
    };
    int total = 0;
    for (unsigned k = 0; k < np; ++k) {
        co_await WorkPoint{1};
        uint32_t before = k ? last_scalar(text + parts[k - 1].a, parts[k - 1].b - parts[k - 1].a) : 0;
        uint32_t after = k + 1 < np ? first_scalar(text + parts[k + 1].a, parts[k + 1].b - parts[k + 1].a) : 0;
        // One plain part: the per-cluster width the caller already summed.
        parts[k].width = (np == 1 && !parts[k].complex && cluster_sum >= 0) ? cluster_sum
                                                                             : measure(parts[k], before, after);
        total = clamp64(int64_t(total) + parts[k].width);
    }
    int gap = f.pending_space ? advance(m, ' ', s) : 0;
    uint8_t gap_level = (f.pending_space || f.pre_gap) ? text_level(d, f.space_node, f.space_offset) : 0;
    int recorded_gap = f.pending_space ? gap : f.pre_gap; // preserved spaces are already in pen
    f.pre_gap = 0;
    if (wrap && f.pen && int64_t(f.pen) + gap + total > f.width) {
        co_await finish_line_work(d, f);
        gap = recorded_gap = 0;
    }
    // Cursive context with the previous fragment when nothing separates them
    // (e.g. <b>مر</b>حبا): both sides are re-measured with their neighbour.
    uint32_t context0 = 0;
    if (!f.pending_space && !recorded_gap && d.fragment_count > uint32_t(f.line_first) && parts[0].complex) {
        Fragment &prev = d.fragments[d.fragment_count - 1];
        if (prev.flow_node == f.node && prev.length && prev.node != id && (prev.flags & FragmentComplex) &&
            int64_t(prev.x) + prev.width == int64_t(f.x) + f.pen) {
            const char *pt = d.str(prev.text);
            uint32_t mine = first_scalar(text + parts[0].a, parts[0].b - parts[0].a);
            bool ok = false;
            int pw = shaped_width(pt, prev.length, d.styles[prev.node], prev.level, prev.context_before, mine, ok,
                                  (prev.flags & FragmentOverride) != 0);
            if (ok) {
                f.pen = clamp64(int64_t(f.pen) + pw - prev.width);
                prev.width = pw;
                prev.context_after = mine;
                context0 = last_scalar(pt, prev.length);
                uint32_t after0 = np > 1 ? first_scalar(text + parts[1].a, parts[1].b - parts[1].a) : 0;
                parts[0].width = measure(parts[0], context0, after0);
            }
        }
    }
    f.pending_space = false;
    f.pen = clamp64(int64_t(f.pen) + gap);
    auto place = [&](const Part &q, size_t a, size_t b, int width, uint32_t before, uint32_t after, bool first) {
        if (d.fragment_count == MaxFragments) {
            d.truncated = true;
            return false;
        }
        emit(d, f, id, offset + uint32_t(a), uint32_t(b - a), width, m);
        Fragment &g = d.fragments[d.fragment_count - 1];
        g.level = q.level;
        g.paragraph_level = q.para;
        g.flags = uint8_t((q.complex ? FragmentComplex : 0) | (q.forced ? FragmentOverride : 0));
        g.gap = first ? recorded_gap : 0;
        g.gap_level = first ? gap_level : 0;
        g.context_before = before;
        g.context_after = after;
        return true;
    };
    for (unsigned k = 0; k < np; ++k) {
        co_await WorkPoint{1};
        Part &q = parts[k];
        uint32_t before = k ? last_scalar(text + parts[k - 1].a, parts[k - 1].b - parts[k - 1].a) : context0;
        uint32_t after = k + 1 < np ? first_scalar(text + parts[k + 1].a, parts[k + 1].b - parts[k + 1].a) : 0;
        // A word wider than the line breaks only between level parts...
        if (wrap && k && f.pen && int64_t(f.pen) + q.width > f.width)
            co_await finish_line_work(d, f);
        if (!wrap || q.width <= f.width || BSF::next_cluster(text, q.a, q.b) >= q.b) {
            if (!place(q, q.a, q.b, q.width, before, after, k == 0))
                co_return;
            continue;
        }
        // ...unless one part alone is wider than the line: emergency breaks
        // at grapheme boundaries (as the per-cluster path), each chunk
        // shaped with its neighbours as joining context.
        size_t c0 = q.a;
        bool first = k == 0;
        while (c0 < q.b) {
            co_await WorkPoint{1};
            size_t c1 = c0;
            int approx = 0;
            while (c1 < q.b) {
                auto c = text_cluster(text, q.b, c1, m, s, c1 == start);
                int w = cluster_width(text, c, m, s, c1 == start);
                if (c1 > c0 && int64_t(f.pen) + approx + w > f.width)
                    break;
                approx = clamp64(int64_t(approx) + w);
                c1 = c.end;
            }
            if (c1 == c0)
                c1 = BSF::next_cluster(text, c0, q.b);
            uint32_t cb = c0 == q.a ? before : last_scalar(text + q.a, c0 - q.a);
            uint32_t ca = c1 == q.b ? after : first_scalar(text + c1, q.b - c1);
            int w = approx;
            if (q.complex) {
                bool ok = false;
                int shaped = shaped_width(text + c0, c1 - c0, s, q.level, cb, ca, ok, q.forced);
                if (ok)
                    w = shaped;
            }
            if (!place(q, c0, c1, w, cb, ca, first))
                co_return;
            first = false;
            c0 = c1;
            if (c0 < q.b)
                co_await finish_line_work(d, f);
        }
    }
}
#endif
inline Work layout_text_work(Document &d, Flow &f, uint16_t id, uint32_t offset, uint32_t size, Metrics m) {
    const Style &s = d.styles[id];
    const char *text = d.str(offset);
    size_t p = 0;
    bool pre = s.white == WhiteSpace::Pre || s.white == WhiteSpace::PreWrap;
    bool wrap = s.white == WhiteSpace::Normal || s.white == WhiteSpace::PreWrap;
    while (p < size) {
        co_await WorkPoint{1};
        if (text[p] == '\r') {
            ++p;
            continue;
        }
        if (pre && text[p] == '\n') {
            co_await finish_line_work(d, f, true);
            ++p;
            continue;
        }
        if (text_space(text,size,p)) {
            if (pre) {
                int w = advance(m, ' ', s);
                if (text[p] == '\t')
                    w *= 4;
                if (wrap && f.pen && int64_t(f.pen) + w > f.width)
                    co_await finish_line_work(d, f);
                if (!f.pre_gap) {
                    f.space_node = id;
                    f.space_offset = uint32_t(p);
                }
                if (f.pen)
                    f.pre_gap = clamp64(int64_t(f.pre_gap) + w);
                f.pen = clamp64(int64_t(f.pen) + w);
                if (s.line > f.line_height)
                    f.line_height = s.line;
            } else {
                if (f.pen > 0 && !f.pending_space) {
                    f.space_node = id;
                    f.space_offset = uint32_t(p);
                }
                f.pending_space = f.pen > 0;
            }
            ++p;
            continue;
        }
        size_t start = p;
        int width = 0;
#if HUSARZ_BIDI
        if (m.family_advance && d.has_bidi) {
            // Bidi document: find the word first; words that take the shaped
            // path are not measured cluster by cluster here.
            size_t q = p;
            while (q < size && !text_space(text, size, q)) {
                co_await WorkPoint{1};
                q = BSF::next_cluster(text, q, size);
            }
            p = q;
            co_await layout_word_bidi_work(d, f, id, offset, start, p, m, wrap, -1);
            continue;
        }
#endif
        // Plain LTR fast path (unchanged BSF 2.0 measurement).
        while (p < size && !text_space(text,size,p)) {
            co_await WorkPoint{1};
            auto c=text_cluster(text,size,p,m,s,p==start);
            width=clamp64(int64_t(width)+cluster_width(text,c,m,s,p==start));p=c.end;
        }
#if HUSARZ_BIDI
        // LTR document, complex word (marks, joining letters without RTL).
        if (m.family_advance && BSF::needs_complex_layout(text + start, p - start)) {
            co_await layout_word_bidi_work(d, f, id, offset, start, p, m, wrap, width);
            continue;
        }
#endif
        int gap = f.pending_space ? advance(m, ' ', s) : 0;
        if (wrap && f.pen && int64_t(f.pen) + gap + width > f.width) {
            co_await finish_line_work(d, f);
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
            co_await WorkPoint{1};
            auto c=text_cluster(text,p,k,m,s,k==start);
            int w=cluster_width(text,c,m,s,k==start);
            if (chunk_width && int64_t(f.pen) + chunk_width + w > f.width) {
                emit(d, f, id, offset + uint32_t(chunk), uint32_t(k - chunk), chunk_width, m);
                co_await finish_line_work(d, f);
                chunk = k;
                chunk_width = 0;
            }
            chunk_width = clamp64(int64_t(chunk_width) + w);
            k = c.end;
        }
        if (chunk < p)
            emit(d, f, id, offset + uint32_t(chunk), uint32_t(p - chunk), chunk_width, m);
    }
}
inline void layout_text(Document &d, Flow &f, uint16_t id, uint32_t offset, uint32_t size, Metrics m) {
    finish_work(layout_text_work(d, f, id, offset, size, m));
}

inline unsigned positive_attribute(const Document &d, uint16_t id, const char *name, unsigned fallback,
                                   unsigned cap) {
    const char *s = d.attr(id, name);
    if (!*s)
        return fallback;
    unsigned value = 0;
    for (unsigned i = 0; s[i]; ++i) {
        if (i == 8 || s[i] < '0' || s[i] > '9')
            return fallback;
        value = value * 10 + unsigned(s[i] - '0');
        if (value > cap)
            return cap;
    }
    return value ? value : fallback;
}
inline const TablePlan *table_plan(const Document &d, uint16_t id) {
    for (uint16_t p = id; p; p = d.nodes[p].parent)
        if (d.styles[p].display == Display::Table) {
            for (unsigned i = 0; i < d.table_count; ++i)
                if (d.tables[i].node == p)
                    return &d.tables[i];
            break;
        }
    return nullptr;
}
inline unsigned table_columns(const Document &d, uint16_t id) {
    auto *plan = table_plan(d, id);
    return plan ? plan->columns : 1;
}
inline Work prepare_controls_work(Document &d) {
    for (uint16_t id = 1; id < d.node_count; ++id) {
        co_await WorkPoint{1};
        Node &n = d.nodes[id];
        if (d.tag(id, "li")) {
            char marker[16] = {};
            if (d.tag(n.parent, "ol")) {
                unsigned count = 1;
                for (uint16_t p = n.previous; p; p = d.nodes[p].previous) {
                    co_await WorkPoint{1};
                    if (d.tag(p, "li"))
                        ++count;
                }
                char digits[8];
                unsigned k = 0;
                do {
                    digits[k++] = char('0' + count % 10);
                    count /= 10;
                } while (count && k < 8);
                unsigned j = 0;
                while (k) {
                    co_await WorkPoint{1};
                    marker[j++] = digits[--k];
                }
                marker[j++] = '.';
                marker[j] = 0;
            } else {
                marker[0] = char(0xe2);
                marker[1] = char(0x80);
                marker[2] = char(0xa2);
            }
            char reserved[40]{};
            for (unsigned i = 0; i < 16; ++i) {
                co_await WorkPoint{1};
                reserved[i] = marker[i];
            }
            n.text = d.store(reserved, 39);
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
            for (uint16_t c = n.first; c; c = d.nodes[c].next) {
                co_await WorkPoint{1};
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
    co_await initialize_controls_work(d);
}
inline void prepare_controls(Document &d) { finish_work(prepare_controls_work(d)); }

inline Work intrinsic_width_work(const Document &d, uint16_t id, Metrics metrics, int &result,
                                 int *minimum = nullptr) {
    int sum = 0, longest = 0, word = 0;
    int decoration[MaxDepth + 1]{};
    bool pending_space = false;
    for (uint16_t c = id; c < d.node_count && (c == id || d.nodes[c].depth > d.nodes[id].depth); ++c) {
        co_await WorkPoint{1};
        const auto &n = d.nodes[c];
        const auto &s = d.styles[c];
        if (s.display == Display::None || (c != id && s.position == Position::Absolute)) {
            unsigned depth = n.depth;
            while (unsigned(c) + 1 < d.node_count && d.nodes[c + 1].depth > depth) {
                co_await WorkPoint{1};
                ++c;
            }
            continue;
        }
        // Descendant boxes contribute their own horizontal chrome to intrinsic
        // sizing. In particular, a padded link inside a flex item must reserve
        // space for both its text and padding before the item is shrunk.
        unsigned relative_depth = n.depth - d.nodes[id].depth;
        int own_decoration = 0;
        if (c != id && !n.is_text) {
            own_decoration = clamp64(int64_t(s.border[1]) + s.border[3] +
                resolve(s.padding[1], 0, s.font, d.root_font) +
                resolve(s.padding[3], 0, s.font, d.root_font) +
                resolve(s.margin[1], 0, s.font, d.root_font) +
                resolve(s.margin[3], 0, s.font, d.root_font), 0, 8192);
            sum = clamp64(int64_t(sum) + own_decoration, 0, 8192);
        }
        decoration[relative_depth] = clamp64(
            int64_t(relative_depth ? decoration[relative_depth - 1] : 0) + own_decoration,
            0, 8192);
        if (d.tag(c, "img")) {
            const auto *image = d.image(c);
            int natural = image ? int(image->width) : 32;
            if (s.height.unit != Unit::Auto && s.width.unit == Unit::Auto) {
                int height = resolve(s.height, 0, s.font, d.root_font);
                natural = image ? clamp64(int64_t(height) * image->width / image->height) : height;
            }
            int image_width = resolve(s.width, 0, s.font, d.root_font, natural);
            if (image_width > longest)
                longest = image_width;
            sum = clamp64(int64_t(sum) + image_width, 0, 8192);
            continue;
        }
        if (!n.is_text)
            continue;
        for (size_t p = 0; p < n.text_length;) {
            co_await WorkPoint{1};
            auto cluster=text_cluster(d.str(n.text),n.text_length,p,metrics,s);
            p=cluster.end;
            bool collapse = s.white != WhiteSpace::Pre && s.white != WhiteSpace::PreWrap;
            if (collapse && text_space(d.str(n.text),n.text_length,cluster.begin)) {
                if (word > longest)
                    longest = word;
                word = 0;
                pending_space = sum > 0;
                continue;
            }
            if (pending_space) {
                sum = clamp64(int64_t(sum) + advance(metrics, ' ', s), 0, 8192);
                pending_space = false;
            }
            int width = cluster_width(d.str(n.text),cluster,metrics,s);
            word = clamp64(int64_t(word) + width, 0, 8192);
            sum = clamp64(int64_t(sum) + width, 0, 8192);
            int decorated_word = clamp64(int64_t(word) + decoration[relative_depth], 0, 8192);
            if (decorated_word > longest)
                longest = decorated_word;
        }
    }
    if (word > longest)
        longest = word;
    if (minimum)
        *minimum = longest;
    result = sum;
    co_return;
}
inline int intrinsic_width(const Document &d, uint16_t id, Metrics metrics) {
    int result{};
    finish_work(intrinsic_width_work(d, id, metrics, result));
    return result;
}

inline Work list_markers_work(Document &d) {
    for (uint16_t id = 1; id < d.node_count; ++id) {
        co_await WorkPoint{1};
        if (d.tag(id, "li") && d.nodes[id].text) {
            auto &node = d.nodes[id];
            char *out = d.arena + node.text;
            unsigned n = 0;
            auto kind = d.styles[id].list_type;
            unsigned value = positive_attribute(d, node.parent, "start", 1, 3999);
            for (uint16_t p = node.previous; p; p = d.nodes[p].previous) {
                co_await WorkPoint{1};
                if (d.tag(p, "li"))
                    ++value;
            }
            if (kind == ListType::Disc || kind == ListType::Circle || kind == ListType::Square) {
                uint32_t cp = kind == ListType::Disc ? 0x2022 : kind == ListType::Circle ? 0x25e6 : 0x25aa;
                n = encode(cp, out);
            } else if (kind == ListType::LowerAlpha || kind == ListType::UpperAlpha) {
                char reversed[8];
                unsigned k = 0;
                do {
                    --value;
                    reversed[k++] = char((kind == ListType::LowerAlpha ? 'a' : 'A') + value % 26);
                    value /= 26;
                } while (value && k < 8);
                while (k) {
                    co_await WorkPoint{1};
                    out[n++] = reversed[--k];
                }
                out[n++] = '.';
            } else if ((kind == ListType::LowerRoman || kind == ListType::UpperRoman) && value <= 3999) {
                const char *digits[] = {"M",  "CM", "D",  "CD", "C",  "XC", "L",
                                        "XL", "X",  "IX", "V",  "IV", "I"};
                unsigned weights[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
                for (unsigned i = 0; i < 13; ++i) {
                    co_await WorkPoint{1};
                    while (value >= weights[i] && n < 35) {
                        co_await WorkPoint{1};
                        value -= weights[i];
                        for (unsigned j = 0; digits[i][j]; ++j) {
                            co_await WorkPoint{1};
                            out[n++] = kind == ListType::LowerRoman ? lower(digits[i][j]) : digits[i][j];
                        }
                    }
                }
                out[n++] = '.';
            } else if (kind != ListType::None) {
                char reversed[12];
                unsigned k = 0;
                do {
                    reversed[k++] = char('0' + value % 10);
                    value /= 10;
                } while (value && k < 12);
                while (k) {
                    co_await WorkPoint{1};
                    out[n++] = reversed[--k];
                }
                out[n++] = '.';
            }
            out[n] = 0;
            node.text_length = n;
        }
    }
}
inline void list_markers(Document &d) { finish_work(list_markers_work(d)); }

inline bool table_work(Document &d, unsigned amount = 1) {
    if (amount > MaxTableWork - d.table_work) {
        d.truncated = true;
        return false;
    }
    d.table_work += amount;
    HUSARZ_COUNT(d, table_work, amount);
    return true;
}
struct IntrinsicSize {
    int minimum{}, preferred{};
};
inline Work cell_intrinsic_work(Document &d, uint16_t cell, Metrics metrics, IntrinsicSize &result) {
    int word = 0, line = 0, minimum = 0, preferred = 0;
    for (uint16_t n = cell + 1; n < d.node_count && d.nodes[n].depth > d.nodes[cell].depth; ++n) {
        co_await WorkPoint{1};
        if (!table_work(d))
            break;
        if (d.styles[n].display == Display::None) {
            unsigned depth = d.nodes[n].depth;
            while (unsigned(n) + 1 < d.node_count && d.nodes[n + 1].depth > depth) {
                co_await WorkPoint{1};
                ++n;
            }
            continue;
        }
        const auto &node = d.nodes[n];
        const auto &style = d.styles[n];
        if (style.display == Display::Block || style.display == Display::Row || d.tag(n, "br")) {
            if (word > minimum)
                minimum = word;
            if (line > preferred)
                preferred = line;
            word = line = 0;
        }
        if (d.tag(n, "img")) {
            const auto *image = d.image(n);
            int w = resolve(style.width, 0, style.font, d.root_font, image ? int(image->width) : 32);
            if (style.width.unit == Unit::Auto && d.has_attr(n, "width"))
                w = int(positive_attribute(d, n, "width", 32, 8192));
            word = clamp64(int64_t(word) + w);
            line = clamp64(int64_t(line) + w);
        }
        if (!node.is_text)
            continue;
        for (size_t p = 0; p < node.text_length;) {
            co_await WorkPoint{1};
            if (!table_work(d))
                break;
            size_t used;
            uint32_t cp = utf8(d.str(node.text) + p, node.text_length - p, used);
            auto cluster=text_cluster(d.str(node.text),node.text_length,p,metrics,style,!word);
            int w=cluster_width(d.str(node.text),cluster,metrics,style,!word);
            if (text_space(d.str(node.text),node.text_length,cluster.begin) && style.white != WhiteSpace::NoWrap &&
                style.white != WhiteSpace::Pre) {
                if (word > minimum)
                    minimum = word;
                word = 0;
            } else
                word = clamp64(int64_t(word) + w);
            if (cp == '\n' && (style.white == WhiteSpace::Pre || style.white == WhiteSpace::PreWrap)) {
                if (line > preferred)
                    preferred = line;
                line = 0;
            } else
                line = clamp64(int64_t(line) + w);
            p=cluster.end;
        }
    }
    if (word > minimum)
        minimum = word;
    if (line > preferred)
        preferred = line;
    const auto &s = d.styles[cell];
    int extra = s.border[1] + s.border[3];
    // Percentage padding is resolved against the containing block during sizing.
    for (unsigned side = 1; side <= 3; side += 2) {
        co_await WorkPoint{1};
        if (s.padding[side].unit != Unit::Percent)
            extra = clamp64(int64_t(extra) + resolve(s.padding[side], 0, s.font, d.root_font));
    }
    minimum = clamp64(int64_t(minimum) + extra, 1);
    preferred = clamp64(int64_t(preferred) + extra, minimum);
    if (s.width.unit != Unit::Auto && s.width.unit != Unit::Percent) {
        int w = resolve(s.width, 0, s.font, d.root_font) + (s.border_box ? 0 : extra);
        if (w > minimum)
            minimum = w;
        if (w > preferred)
            preferred = w;
    }
    {
        result = {minimum, preferred};
        co_return;
    }
}
inline IntrinsicSize cell_intrinsic(Document &d, uint16_t cell, Metrics metrics) {
    IntrinsicSize result{};
    finish_work(cell_intrinsic_work(d, cell, metrics, result));
    return result;
}

inline void span_constraint(int *columns, unsigned start, unsigned count, int target, int gap) {
    int64_t sum = int64_t(gap) * (count - 1);
    for (unsigned i = start; i < start + count; ++i)
        sum += columns[i];
    if (sum >= target)
        return;
    int deficit = int(target - sum);
    for (unsigned i = 0; i < count; ++i)
        columns[start + i] = clamp64(int64_t(columns[start + i]) + deficit / int(count) +
                                     (int(i) < deficit % int(count) ? 1 : 0));
}
inline Work plan_tables_work(Document &d, Metrics metrics) {
    HUSARZ_SCOPE(d, Table);
    d.table_count = d.table_cells = 0;
    d.table_work = 0;
    for (unsigned n = 0; n < d.node_count; ++n) {
        co_await WorkPoint{1};
        d.table_cell[n] = {};
    }
    for (uint16_t id = 1; id < d.node_count; ++id) {
        HUSARZ_PROGRESS(Table, id, d.node_count);
        co_await WorkPoint{1};
        if (d.styles[id].display == Display::Table) {
            if (d.table_count == MaxTables) {
                d.truncated = true;
                break;
            }
            if (!d.tables.ensure(d.table_count)) {
                d.truncated = true;
                break;
            }
            HUSARZ_COUNT(d, table_replans, 1);
            auto &p = d.tables[d.table_count++];
            // In-place clearing avoids a large temporary on the native Ring 3 stack.
            p.node = id;
            p.rows = p.columns = 0;
            p.total = 0;
            p.gap = d.styles[id].border_collapse ? 0 : d.styles[id].border_spacing;
            for (unsigned i = 0; i < MaxTableColumns; ++i) {
                co_await WorkPoint{1};
                p.minimum[i] = p.preferred[i] = p.widths[i] = 0;
                p.explicit_columns[i] = {};
            }
            for (unsigned i = 0; i < MaxTableRows; ++i) {
                co_await WorkPoint{1};
                p.row_nodes[i] = 0;
                p.heights[i] = p.tops[i] = 0;
            }
            for (auto &entry : p.grid) {
                co_await WorkPoint{1};
                entry = 0;
            }
            // Record rows first, so spans can be clamped to existing rows and row groups.
            for (uint16_t row = id + 1; row < d.node_count && d.nodes[row].depth > d.nodes[id].depth; ++row) {
                co_await WorkPoint{1};
                if (!table_work(d))
                    break;
                if (d.styles[row].display != Display::Row)
                    continue;
                uint16_t owner = d.nodes[row].parent;
                while (owner && d.styles[owner].display != Display::Table) {
                    co_await WorkPoint{1};
                    owner = d.nodes[owner].parent;
                }
                if (owner != id)
                    continue;
                if (p.rows == MaxTableRows) {
                    d.truncated = true;
                    break;
                }
                p.row_nodes[p.rows++] = row;
            }
            for (unsigned r = 0; r < p.rows; ++r) {
                co_await WorkPoint{1};
                unsigned col = 0;
                for (uint16_t cell = d.nodes[p.row_nodes[r]].first; cell; cell = d.nodes[cell].next) {
                    co_await WorkPoint{1};
                    if (d.styles[cell].display != Display::Cell)
                        continue;
                    if (d.table_cells == MaxTableCells || !table_work(d)) {
                        d.truncated = true;
                        break;
                    }
                    while (col < MaxTableColumns && p.grid[r * MaxTableColumns + col]) {
                        co_await WorkPoint{1};
                        ++col;
                    }
                    if (col == MaxTableColumns) {
                        d.truncated = true;
                        continue;
                    }
                    unsigned cols = positive_attribute(d, cell, "colspan", 1, MaxTableSpan);
                    if (cols > MaxTableColumns - col)
                        cols = MaxTableColumns - col;
                    unsigned rows = positive_attribute(d, cell, "rowspan", 1, MaxTableSpan);
                    if (rows > p.rows - r)
                        rows = p.rows - r;
                    // A span cannot cross a row-group boundary.
                    for (unsigned j = 1; j < rows; ++j) {
                        co_await WorkPoint{1};
                        if (d.nodes[p.row_nodes[r + j]].parent != d.nodes[p.row_nodes[r]].parent) {
                            rows = j;
                            break;
                        }
                    }
                    bool overlap = false;
                    for (unsigned y = r; y < r + rows; ++y) {
                        co_await WorkPoint{1};
                        for (unsigned x = col; x < col + cols; ++x) {
                            co_await WorkPoint{1};
                            if (!table_work(d) || p.grid[y * MaxTableColumns + x])
                                overlap = true;
                        }
                    }
                    if (overlap) {
                        d.truncated = true;
                        continue;
                    } // Never overwrite an earlier reservation.
                    d.table_cell[cell] = {id, uint16_t(r), uint16_t(col), uint16_t(rows), uint16_t(cols)};
                    ++d.table_cells;
                    for (unsigned y = r; y < r + rows; ++y) {
                        co_await WorkPoint{1};
                        for (unsigned x = col; x < col + cols; ++x) {
                            co_await WorkPoint{1};
                            p.grid[y * MaxTableColumns + x] = cell;
                        }
                    }
                    col += cols;
                    if (col > p.columns)
                        p.columns = uint16_t(col);
                }
            }
            if (!p.columns)
                p.columns = 1;
            unsigned column = 0;
            for (uint16_t n = id + 1; n < d.node_count && d.nodes[n].depth > d.nodes[id].depth; ++n) {
                co_await WorkPoint{1};
                if (d.tag(n, "col")) {
                    uint16_t owner = d.nodes[n].parent;
                    while (owner && d.styles[owner].display != Display::Table) {
                        co_await WorkPoint{1};
                        owner = d.nodes[owner].parent;
                    }
                    if (owner != id)
                        continue;
                    unsigned count = positive_attribute(d, n, "span", 1, MaxTableSpan);
                    Length value = d.styles[n].width;
                    if (value.unit == Unit::Auto)
                        css_length({d.attr(n, "width"), length(d.attr(n, "width"), 32)}, value);
                    while (count-- && column < p.columns) {
                        co_await WorkPoint{1};
                        p.explicit_columns[column++] = value;
                    }
                }
            }
            // Two fixed passes: single-column minima, then spanning inequalities.
            for (unsigned pass = 0; pass < 2; ++pass) {
                co_await WorkPoint{1};
                for (uint16_t cell = id + 1; cell < d.node_count && d.nodes[cell].depth > d.nodes[id].depth;
                     ++cell) {
                    co_await WorkPoint{1};
                    const auto &c = d.table_cell[cell];
                    if (c.table != id || (c.columns == 1) != (pass == 0))
                        continue;
                    IntrinsicSize size;
                    co_await cell_intrinsic_work(d, cell, metrics, size);
                    span_constraint(p.minimum, c.column, c.columns, size.minimum, p.gap);
                    span_constraint(p.preferred, c.column, c.columns, size.preferred, p.gap);
                }
            }
            for (unsigned c = 0; c < p.columns; ++c) {
                co_await WorkPoint{1};
                if (!p.minimum[c])
                    p.minimum[c] = 1;
                if (p.preferred[c] < p.minimum[c])
                    p.preferred[c] = p.minimum[c];
                p.intrinsic_minimum[c] = p.minimum[c];
                p.intrinsic_preferred[c] = p.preferred[c];
            }
        }
    }
}
inline void plan_tables(Document &d, Metrics metrics) { finish_work(plan_tables_work(d, metrics)); }

inline Work size_table_work(Document &d, TablePlan &p, int available) {
    int64_t minimum = 0, preferred = 0;
    for (unsigned c = 0; c < p.columns; ++c) {
        co_await WorkPoint{1};
        p.minimum[c] = p.intrinsic_minimum[c];
        p.preferred[c] = p.intrinsic_preferred[c];
        const auto &s = d.styles[p.node];
        int explicit_width = resolve(p.explicit_columns[c], available, s.font, d.root_font);
        if (explicit_width > p.minimum[c])
            p.minimum[c] = explicit_width;
        if (p.preferred[c] < p.minimum[c])
            p.preferred[c] = p.minimum[c];
    }
    for (uint16_t n = p.node + 1; n < d.node_count && d.nodes[n].depth > d.nodes[p.node].depth; ++n) {
        co_await WorkPoint{1};
        const auto &cell = d.table_cell[n];
        if (cell.table != p.node)
            continue;
        const auto &s = d.styles[n];
        if (s.width.unit == Unit::Percent) {
            int target = resolve(s.width, available, s.font, d.root_font);
            if (!s.border_box)
                target = clamp64(int64_t(target) + s.border[1] + s.border[3] +
                                 resolve(s.padding[1], available, s.font, d.root_font) +
                                 resolve(s.padding[3], available, s.font, d.root_font));
            span_constraint(p.minimum, cell.column, cell.columns, target, p.gap);
            span_constraint(p.preferred, cell.column, cell.columns, target, p.gap);
        }
    }
    for (unsigned c = 0; c < p.columns; ++c) {
        co_await WorkPoint{1};
        minimum += p.minimum[c];
        preferred += p.preferred[c];
    }
    int room = clamp64(int64_t(available) - int64_t(p.gap) * (p.columns + 1));
    int target = room;
    if (d.styles[p.node].width.unit == Unit::Auto && target > preferred)
        target = int(preferred);
    if (target < minimum)
        target = clamp64(minimum);
    int64_t remaining = int64_t(target) - minimum, flexibility = preferred - minimum;
    int64_t allocated = 0;
    for (unsigned c = 0; c < p.columns; ++c) {
        co_await WorkPoint{1};
        int64_t share = flexibility > 0 && remaining < flexibility
                            ? remaining * (p.preferred[c] - p.minimum[c]) / flexibility
                            : (remaining >= flexibility
                                   ? p.preferred[c] - p.minimum[c] + (remaining - flexibility) / p.columns
                                   : 0);
        p.widths[c] = clamp64(int64_t(p.minimum[c]) + share);
        allocated += p.widths[c];
    }
    // Rounding remainder is at most a few columns; no content-controlled loop.
    for (unsigned c = 0; c < p.columns && allocated < target; ++c) {
        co_await WorkPoint{1};
        ++p.widths[c];
        ++allocated;
    }
    p.total = clamp64(allocated + int64_t(p.gap) * (p.columns + 1));
}
inline void size_table(Document &d, TablePlan &p, int available) {
    finish_work(size_table_work(d, p, available));
}

inline Work finish_table_work(Document &d, TablePlan &p, int &result) {
    auto &table = d.boxes[p.node];
    for (unsigned r = 0; r < p.rows; ++r) {
        co_await WorkPoint{1};
        p.heights[r] = d.boxes[p.row_nodes[r]].height;
    }
    // Existing non-spanning row heights first, then one bounded span-deficit pass.
    for (uint16_t n = p.node + 1; n < d.node_count && d.nodes[n].depth > d.nodes[p.node].depth; ++n) {
        co_await WorkPoint{1};
        const auto &c = d.table_cell[n];
        if (c.table != p.node)
            continue;
        span_constraint(p.heights, c.row, c.rows, d.boxes[n].height, p.gap);
    }
    int y = clamp64(int64_t(table.content_y) + p.gap, -MaxDimension);
    for (unsigned r = 0; r < p.rows; ++r) {
        co_await WorkPoint{1};
        p.tops[r] = y;
        y = clamp64(int64_t(y) + p.heights[r] + p.gap, -MaxDimension);
    }
    d.table_shift[p.node] = 0;
    for (uint16_t n = p.node + 1; n < d.node_count && d.nodes[n].depth > d.nodes[p.node].depth; ++n) {
        co_await WorkPoint{1};
        auto &box = d.boxes[n];
        const auto &c = d.table_cell[n];
        int dy = d.table_shift[d.nodes[n].parent];
        if (c.table == p.node) {
            dy = p.tops[c.row] - box.y;
            int height = 0;
            for (unsigned r = c.row; r < c.row + c.rows; ++r) {
                co_await WorkPoint{1};
                height = clamp64(int64_t(height) + p.heights[r] + (r > c.row ? p.gap : 0));
            }
            int spare = clamp64(int64_t(height) - box.height);
            box.height = height;
            // Padding/border stay in the cell; vertical alignment moves only its content.
            int alignment = d.styles[n].vertical == Vertical::Middle   ? spare / 2
                            : d.styles[n].vertical == Vertical::Bottom ? spare
                                                                       : 0;
            box.y = clamp64(int64_t(box.y) + dy, -MaxDimension);
            box.content_y = clamp64(int64_t(box.content_y) + dy + alignment, -MaxDimension);
            d.table_shift[n] = dy + alignment;
            continue;
        }
        d.table_shift[n] = dy;
        box.y = clamp64(int64_t(box.y) + dy, -MaxDimension);
        box.content_y = clamp64(int64_t(box.content_y) + dy, -MaxDimension);
    }
    for (unsigned r = 0; r < p.rows; ++r) {
        co_await WorkPoint{1};
        auto &row = d.boxes[p.row_nodes[r]];
        row.y = row.content_y = p.tops[r];
        row.height = row.content_height = p.heights[r];
    }
    // Row-group rectangles follow finalized row geometry, including rowspan growth.
    for (unsigned r = 0; r < p.rows; ++r) {
        co_await WorkPoint{1};
        uint16_t group = d.nodes[p.row_nodes[r]].parent;
        if (d.styles[group].display != Display::RowGroup)
            continue;
        auto &g = d.boxes[group];
        bool first = !r || d.nodes[p.row_nodes[r - 1]].parent != group;
        if (first) {
            g.x = g.content_x = table.content_x;
            g.y = g.content_y = p.tops[r];
            g.width = g.content_width = table.content_width;
        }
        g.height = g.content_height = clamp64(int64_t(p.tops[r]) + p.heights[r] - g.y);
    }
    uint16_t end = p.node + 1;
    while (end < d.node_count && d.nodes[end].depth > d.nodes[p.node].depth) {
        co_await WorkPoint{1};
        ++end;
    }
    for (uint32_t f = 0; f < d.fragment_count; ++f) {
        co_await WorkPoint{1};
        auto &fragment = d.fragments[f];
        // Parser indices are in tree preorder, just as in the sizing/layout walk.
        if (fragment.node > p.node && fragment.node < end)
            fragment.y = clamp64(int64_t(fragment.y) + d.table_shift[fragment.node], -MaxDimension);
    }
    {
        result = clamp64(int64_t(y) - table.content_y);
        co_return;
    }
}
inline int finish_table(Document &d, TablePlan &p) {
    int result{};
    finish_work(finish_table_work(d, p, result));
    return result;
}

inline bool collapsing_block(const Document &d, uint16_t n) {
    return !d.styles[n].flex && !d.styles[d.nodes[n].parent].flex && d.styles[n].display == Display::Block &&
           d.styles[n].position == Position::Static && !is_control(d, n);
}
inline bool flow_whitespace(const Document &d, uint16_t n) {
    if (!d.nodes[n].is_text)
        return false;
    for (unsigned i = 0; i < d.nodes[n].text_length; ++i)
        if (!space(d.str(d.nodes[n].text)[i]))
            return false;
    return d.styles[n].white == WhiteSpace::Normal || d.styles[n].white == WhiteSpace::NoWrap;
}
inline bool collapse_ignored(const Document &d, uint16_t n) {
    return d.styles[n].display == Display::None || d.styles[n].position == Position::Absolute ||
           flow_whitespace(d, n);
}
inline Work plan_margins_work(Document &d, int width) {
    d.collapse_width[0] = width;
    for (uint16_t n = 1; n < d.node_count; ++n) {
        co_await WorkPoint{1};
        auto &m = d.margins[n];
        m = {};
        const auto &s = d.styles[n];
        int basis = d.collapse_width[d.nodes[n].parent];
        m.top = MarginStrut(resolve(s.margin[0], basis, s.font, d.root_font));
        m.bottom = MarginStrut(resolve(s.margin[2], basis, s.font, d.root_font));
        int edges = s.border[1] + s.border[3] + resolve(s.padding[1], basis, s.font, d.root_font) +
                    resolve(s.padding[3], basis, s.font, d.root_font);
        int content =
            resolve(s.width, basis, s.font, d.root_font,
                    clamp64(int64_t(basis) - edges - resolve(s.margin[1], basis, s.font, d.root_font) -
                            resolve(s.margin[3], basis, s.font, d.root_font)));
        if (s.width.unit != Unit::Auto && s.border_box)
            content = clamp64(int64_t(content) - edges);
        d.collapse_width[n] = clamp64(content, 1);
    }
    // Iterative postorder over parser indices; no recursive descent on web content.
    for (uint16_t n = uint16_t(d.node_count); --n;) {
        co_await WorkPoint{1};
        if (!collapsing_block(d, n) || d.tag(n, "html"))
            continue;
        auto &m = d.margins[n];
        const auto &s = d.styles[n];
        int basis = d.collapse_width[d.nodes[n].parent];
        bool context = s.overflow_x != Overflow::Visible || s.overflow_y != Overflow::Visible;
        bool top = !context && !s.border[0] && !resolve(s.padding[0], basis, s.font, d.root_font);
        bool bottom = !context && !s.border[2] && !resolve(s.padding[2], basis, s.font, d.root_font) &&
                      s.height.unit == Unit::Auto && resolve(s.min_height, 0, s.font, d.root_font) == 0;
        bool empty = !context && top && !s.border[2] && !resolve(s.padding[2], basis, s.font, d.root_font) &&
                     (s.height.unit == Unit::Auto || (!s.height.value && s.height.unit != Unit::Percent)) &&
                     resolve(s.min_height, 0, s.font, d.root_font) == 0;
        for (uint16_t c = d.nodes[n].first; c; c = d.nodes[c].next) {
            co_await WorkPoint{1};
            if (collapse_ignored(d, c))
                continue;
            if (!collapsing_block(d, c) || !d.margins[c].through)
                empty = false;
            if (top && collapsing_block(d, c)) {
                m.top.include(d.margins[c].top);
                d.margins[c].suppress_top = true;
                if (d.margins[c].through) {
                    m.top.include(d.margins[c].bottom);
                    d.margins[c].suppress_bottom = true;
                } else
                    top = false;
            } else
                top = false;
        }
        for (uint16_t c = d.nodes[n].last; c; c = d.nodes[c].previous) {
            co_await WorkPoint{1};
            if (collapse_ignored(d, c))
                continue;
            if (bottom && collapsing_block(d, c)) {
                m.bottom.include(d.margins[c].bottom);
                d.margins[c].suppress_bottom = true;
                if (d.margins[c].through) {
                    m.bottom.include(d.margins[c].top);
                    d.margins[c].suppress_top = true;
                } else
                    bottom = false;
            } else
                bottom = false;
        }
        m.through = empty;
        if (empty) {
            m.top.include(m.bottom);
            m.bottom = m.top;
        }
    }
}
inline void plan_margins(Document &d, int width) { finish_work(plan_margins_work(d, width)); }

#include "flex_layout.inc"
inline Work reflow_work(Document &d, int width, Metrics metrics) {
    HUSARZ_SCOPE(d, Layout);
    width = clamp64(width, 1, 8192);
    if (d.style_viewport_width != width && (d.has_media || d.has_viewport_units))
        d.styles_dirty = true;
    d.style_viewport_width = width;
    if (d.styles_dirty)
        co_await compute_styles_work(d);
    co_await list_markers_work(d);
    if (d.table_plan_revision != d.revision) {
        co_await plan_tables_work(d, metrics);
        d.table_plan_revision = d.revision;
    }
    co_await plan_margins_work(d, width);
    // BSF 2.1: bidi levels depend on DOM text and styles only, not on the
    // width, so a resize reflow reuses them.
    if (d.bidi_revision != d.revision || d.bidi_style_generation != d.style_generation) {
        co_await resolve_bidi_work(d);
        d.bidi_revision = d.revision;
        d.bidi_style_generation = d.style_generation;
    }
    d.fragment_count = 0;
    d.offset_x[0] = d.offset_y[0] = 0;
    for (unsigned i = 0; i < d.node_count; ++i) {
        co_await WorkPoint{1};
        d.flex_width[i] = d.flex_dx[i] = d.flex_dy[i] = 0;
    }
    d.layout_width = width;
    ++d.reflow_count;
    HUSARZ_COUNT(d, full_reflows, 1);
    Flow flow[MaxDepth + 2]{};
    uint16_t active[MaxDepth + 2]{};
    bool block[MaxDepth + 2]{}, skip[MaxDepth + 2]{};
    int outer_margin[MaxDepth + 2]{};
    flow[0] = {0, 0, 0, width, 0, 0, 0, 0, 0, 0, 1, false};
    block[0] = true;
    d.boxes[0] = {0, 0, width, 0, 0, 0, width, 0};
    uint16_t id = 1;
    while (id < d.node_count) {
        HUSARZ_PROGRESS(Layout, id, d.node_count);
        co_await WorkPoint{1};
        HUSARZ_COUNT(d, layout_work, 1);
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
        if (s.display == Display::Cell && d.styles[parent.node].display == Display::Row &&
            table_plan(d, id) && !d.table_cell[id].table)
            skip[depth] = true;
        if (s.display == Display::Table && !table_plan(d, id))
            skip[depth] = true;
        if (s.display == Display::Row) {
            const auto *plan = table_plan(d, id);
            if (plan) {
                bool found = false;
                for (unsigned r = 0; r < plan->rows; ++r) {
                    co_await WorkPoint{1};
                    if (plan->row_nodes[r] == id) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    skip[depth] = true;
            }
        }
        bool replaced = !n.is_text && (d.tag(id, "input") || d.tag(id, "textarea") || d.tag(id, "select") ||
                                       d.tag(id, "img") || d.tag(id, "button"));
        bool is_block = s.display == Display::Block || s.display == Display::Table ||
                        s.display == Display::RowGroup || s.display == Display::Row ||
                        s.display == Display::Cell || s.display == Display::InlineBlock ||
                        s.position == Position::Absolute;
        if (replaced)
            is_block = false;
        block[depth] = is_block;
        active[depth] = uint16_t(fidx);
        outer_margin[depth] = 0;
        if (!skip[depth]) {
            if (n.is_text)
                co_await layout_text_work(d, parent, id, n.text, n.text_length, metrics);
            else if (d.tag(id, "br"))
                co_await finish_line_work(d, parent, true);
            else if (is_block) {
                bool inline_box = s.display == Display::InlineBlock && s.position != Position::Absolute;
                if (!inline_box && s.position != Position::Absolute)
                    co_await finish_line_work(d, parent);
                int mt = resolve(s.margin[0], parent.width, s.font, d.root_font),
                    mb = resolve(s.margin[2], parent.width, s.font, d.root_font);
                MarginStrut top_margin(mt), bottom_margin(mb);
                if (collapsing_block(d, id)) {
                    top_margin = d.margins[id].suppress_top ? MarginStrut() : d.margins[id].top;
                    bottom_margin = d.margins[id].suppress_bottom ? MarginStrut() : d.margins[id].bottom;
                    mt = top_margin.value();
                    mb = bottom_margin.value();
                }
                int ml = resolve(s.margin[3], parent.width, s.font, d.root_font),
                    mr = resolve(s.margin[1], parent.width, s.font, d.root_font);
                int pl = resolve(s.padding[3], parent.width, s.font, d.root_font),
                    pr = resolve(s.padding[1], parent.width, s.font, d.root_font),
                    pt = resolve(s.padding[0], parent.width, s.font, d.root_font);
                int extra = clamp64(int64_t(pl) + pr + s.border[1] + s.border[3]);
                int intrinsic = 0;
                if (inline_box && s.width.unit == Unit::Auto)
                    co_await intrinsic_width_work(d, id, metrics, intrinsic);
                int available = parent.width;
                if (inline_box && s.width.unit == Unit::Auto)
                    available = clamp64(int64_t(intrinsic) + extra, 1, parent.width);
                int inline_outer = resolve(s.width, parent.width, s.font, d.root_font, available);
                if (s.width.unit != Unit::Auto && !s.border_box)
                    inline_outer = clamp64(int64_t(inline_outer) + extra);
                if (inline_box && parent.pen && int64_t(parent.pen) + inline_outer > parent.width)
                    co_await finish_line_work(d, parent);
                if (inline_box && parent.pending_space) {
                    parent.pen = clamp64(int64_t(parent.pen) + advance(metrics, ' ', s));
                    parent.pending_space = false;
                }
                bool cell = s.display == Display::Cell && d.styles[parent.node].display == Display::Row;
                int cell_offset = 0;
                if (cell) {
                    const auto *plan = table_plan(d, id);
                    const auto &placement = d.table_cell[id];
                    if (plan && placement.table == plan->node) {
                        cell_offset = plan->gap;
                        for (unsigned c = 0; c < placement.column; ++c) {
                            co_await WorkPoint{1};
                            cell_offset += plan->widths[c] + plan->gap;
                        }
                        available = 0;
                        for (unsigned c = placement.column; c < placement.column + placement.columns; ++c) {
                            co_await WorkPoint{1};
                            available += plan->widths[c] + (c > placement.column ? plan->gap : 0);
                        }
                    } else {
                        cell_offset = parent.column * (parent.width / parent.columns);
                        available = parent.width / parent.columns;
                    }
                }
                int content = resolve(s.width, available, s.font, d.root_font,
                                      clamp64(int64_t(available) - ml - mr - extra));
                if (cell)
                    content = clamp64(int64_t(available) - extra);
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
                if (d.styles[parent.node].flex && d.flex_width[id])
                    content = clamp64(int64_t(d.flex_width[id]) - extra);
                content = clamp64(content);
                if (s.display == Display::Table) {
                    for (unsigned t = 0; t < d.table_count; ++t) {
                        co_await WorkPoint{1};
                        if (d.tables[t].node == id) {
                            co_await size_table_work(d, d.tables[t], content);
                            content = d.tables[t].total;
                            break;
                        }
                    }
                }
                int total = clamp64(int64_t(content) + extra);
                if (s.margin[3].unit == Unit::Auto || s.margin[1].unit == Unit::Auto) {
                    int remainder = clamp64(int64_t(available) - total - ml - mr);
                    if (s.margin[3].unit == Unit::Auto)
                        ml += s.margin[1].unit == Unit::Auto ? remainder / 2 : remainder;
                }
                int x =
                    clamp64(int64_t(parent.x) + (inline_box ? parent.pen : 0) + (cell ? cell_offset : 0) + ml,
                            -MaxDimension);
                MarginStrut merged = top_margin;
                bool adjoining = collapsing_block(d, id);
                if (adjoining)
                    merged.include(parent.previous_margin);
                int collapse = adjoining ? merged.value() - parent.previous_margin.value() : mt;
                int y = clamp64(int64_t(parent.y) + (cell ? mt : collapse), -MaxDimension);
                box = {x,
                       y,
                       total,
                       0,
                       clamp64(int64_t(x) + pl + s.border[3], -MaxDimension),
                       clamp64(int64_t(y) + pt + s.border[0], -MaxDimension),
                       content,
                       0};
                if (s.height.unit != Unit::Auto && s.height.unit != Unit::Percent) {
                    box.content_height = clamp64(resolve(s.height, 0, s.font, d.root_font));
                    if (s.border_box)
                        box.content_height =
                            clamp64(int64_t(box.content_height) - pt - s.border[0] - s.border[2] -
                                    resolve(s.padding[2], parent.width, s.font, d.root_font));
                } else if (s.height.unit == Unit::Percent && d.boxes[parent.node].content_height > 0)
                    box.content_height =
                        clamp64(resolve(s.height, d.boxes[parent.node].content_height, s.font, d.root_font));
                co_await plan_flex_work(d, id, metrics);
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
                    co_await layout_text_work(d, marker, id, n.text, n.text_length, metrics);
                    flow[depth].line_first = int(d.fragment_count);
                }
            } else if (replaced) {
                if (s.display == Display::Block && s.position != Position::Absolute)
                    co_await finish_line_work(d, parent);
                int pl = resolve(s.padding[3], parent.width, s.font, d.root_font),
                    pr = resolve(s.padding[1], parent.width, s.font, d.root_font),
                    pt = resolve(s.padding[0], parent.width, s.font, d.root_font),
                    pb = resolve(s.padding[2], parent.width, s.font, d.root_font);
                int intrinsic = 0;
                for (size_t p = 0; p < n.text_length;) {
                    co_await WorkPoint{1};
                    auto c=text_cluster(d.str(n.text),n.text_length,p,metrics,s,p==0);
                    intrinsic=clamp64(int64_t(intrinsic)+cluster_width(d.str(n.text),c,metrics,s,p==0));p=c.end;
                }
                const auto *image = d.image(id);
                Length image_width = s.width, image_height = s.height;
                if (d.styles[parent.node].flex && d.flex_width[id] && image_width.unit == Unit::Auto)
                    image_width = {d.flex_width[id] * 1000, Unit::Px};
                if (d.tag(id, "img")) {
                    if (image_width.unit == Unit::Auto && d.has_attr(id, "width"))
                        image_width = {int(positive_attribute(d, id, "width", 1, 8192)) * 1000, Unit::Px};
                    if (image_height.unit == Unit::Auto && d.has_attr(id, "height"))
                        image_height = {int(positive_attribute(d, id, "height", 1, 8192)) * 1000, Unit::Px};
                    if (image) {
                        intrinsic = int(image->width);
                        if (image_width.unit == Unit::Auto && image_height.unit != Unit::Auto)
                            intrinsic = clamp64(int64_t(resolve(image_height, 0, s.font, d.root_font)) *
                                                image->width / image->height);
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
                    co_await finish_line_work(d, parent);
                int gap = parent.pending_space ? advance(metrics, ' ', s) : 0;
                parent.pending_space = false;
                parent.pen = clamp64(int64_t(parent.pen) + gap);
                box = {
                    clamp64(int64_t(parent.x) + parent.pen, -MaxDimension), parent.y, outer, 0, 0, 0, cw, 0};
                box.content_x = clamp64(int64_t(box.x) + pl + s.border[3], -MaxDimension);
                box.content_y = clamp64(int64_t(box.y) + pt + s.border[0], -MaxDimension);
                Flow local{id, box.content_x, box.content_y, cw, 0, 0, int(d.fragment_count), 0, 0, 0,
                           1,  false};
                bool toggle = input_type(d, id, "checkbox") || input_type(d, id, "radio");
                if (!image && !toggle)
                    co_await layout_text_work(d, local, id, n.text, n.text_length, metrics);
                co_await finish_line_work(d, local);
                auto *field = control(d, id);
                int intrinsic_height = image ? clamp64(int64_t(cw) * image->height / image->width)
                                       : field && selectable(d, *field)
                                           ? font_height(metrics, s)
                                           : clamp64(int64_t(local.y) - box.content_y);
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
                if (image && d.fragment_count < MaxFragments)
                    d.fragments[d.fragment_count++] = {id,        0,          0,  box.x,      box.y,
                                                       box.width, box.height, {}, parent.node};
                if (s.position != Position::Absolute) {
                    parent.pen = clamp64(int64_t(parent.pen) + outer);
                    if (box.height > parent.line_height)
                        parent.line_height = box.height;
                    if (s.display == Display::Block)
                        co_await finish_line_work(d, parent);
                }
            } else {
                int left = resolve(s.margin[3], parent.width, s.font, d.root_font) +
                           resolve(s.padding[3], parent.width, s.font, d.root_font) + s.border[3];
                parent.pen = clamp64(int64_t(parent.pen) + left);
                if (n.text && !n.is_text) {
                    co_await layout_text_work(d, parent, id, n.text, n.text_length, metrics);
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
            co_await WorkPoint{1};
            Node &end = d.nodes[id];
            unsigned dep = end.depth;
            if (!skip[dep]) {
                if (block[dep]) {
                    Flow &f = flow[dep];
                    co_await finish_line_work(d, f);
                    Style &st = d.styles[id];
                    Box &b = d.boxes[id];
                    Flow &pf = flow[active[dep - 1]];
                    int content = clamp64(int64_t(f.y) - b.content_y);
                    if (st.display == Display::Row && f.row_height > content)
                        content = f.row_height;
                    if (st.display == Display::Table)
                        for (unsigned t = 0; t < d.table_count; ++t) {
                            co_await WorkPoint{1};
                            if (d.tables[t].node == id) {
                                co_await finish_table_work(d, d.tables[t], content);
                                break;
                            }
                        }
                    co_await arrange_flex_work(d, id, content);
                    int pb = resolve(st.padding[2], pf.width, st.font, d.root_font);
                    int extra = clamp64(int64_t(b.content_y) - b.y + pb + st.border[2]);
                    int containing_height = d.boxes[pf.node].content_height;
                    if (st.height.unit != Unit::Auto &&
                        (st.height.unit != Unit::Percent || containing_height > 0)) {
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
                        if (b.height > pf.line_height)
                            pf.line_height = b.height;
                    } else if (st.display == Display::Cell && d.styles[pf.node].display == Display::Row) {
                        int h = clamp64(int64_t(b.y) - pf.y + b.height + outer_margin[dep]);
                        if (d.table_cell[id].rows <= 1 && h > pf.row_height)
                            pf.row_height = h;
                        pf.column += int(positive_attribute(d, id, "colspan", 1, 32));
                    } else {
                        pf.y = clamp64(int64_t(b.y) + b.height + outer_margin[dep], -MaxDimension);
                        if (collapsing_block(d, id)) {
                            if (d.margins[id].through) {
                                MarginStrut joined =
                                    d.margins[id].suppress_top ? MarginStrut() : d.margins[id].top;
                                joined.include(pf.previous_margin);
                                pf.previous_margin = joined;
                                pf.y = b.y; // Empty blocks preserve the entire adjoining strut.
                            } else
                                pf.previous_margin =
                                    d.margins[id].suppress_bottom ? MarginStrut() : d.margins[id].bottom;
                        } else
                            pf.previous_margin = MarginStrut();
                        pf.line_first = int(d.fragment_count);
                    }
                } else if (!end.is_text && !d.tag(id, "input") && !d.tag(id, "textarea") &&
                           !d.tag(id, "select") && !d.tag(id, "img") && !d.tag(id, "button")) {
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
    co_await finish_line_work(d, flow[0]);
    d.boxes[0].height = d.boxes[0].content_height = flow[0].y;
    // Text and inline boxes are reconstructed after line alignment is final.
    for (uint32_t i = 0; i < d.fragment_count; ++i) {
        co_await WorkPoint{1};
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
        co_await WorkPoint{1};
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
        co_await WorkPoint{1};
        auto &b = d.boxes[i];
        const auto &s = d.styles[i];
        int dx = clamp64(int64_t(d.offset_x[d.nodes[i].parent]) + d.flex_dx[i], -MaxDimension),
            dy = clamp64(int64_t(d.offset_y[d.nodes[i].parent]) + d.flex_dy[i], -MaxDimension);
        if (s.position == Position::Relative) {
            const auto &cb = d.boxes[d.nodes[i].parent];
            dx += s.left.unit != Unit::Auto ? resolve(s.left, cb.content_width, s.font, d.root_font)
                                            : -resolve(s.right, cb.content_width, s.font, d.root_font);
            dy += s.top.unit != Unit::Auto ? resolve(s.top, cb.content_height, s.font, d.root_font)
                                           : -resolve(s.bottom, cb.content_height, s.font, d.root_font);
        } else if (s.position == Position::Absolute) {
            uint16_t containing = d.nodes[i].parent;
            while (containing && d.styles[containing].position == Position::Static) {
                co_await WorkPoint{1};
                containing = d.nodes[containing].parent;
            }
            const auto &cb = d.boxes[containing];
            if (s.left.unit != Unit::Auto)
                dx = cb.content_x + resolve(s.left, cb.content_width, s.font, d.root_font) - b.x;
            else if (s.right.unit != Unit::Auto)
                dx = cb.content_x + cb.content_width -
                     resolve(s.right, cb.content_width, s.font, d.root_font) - b.width - b.x;
            if (s.top.unit != Unit::Auto)
                dy = cb.content_y + resolve(s.top, cb.content_height, s.font, d.root_font) - b.y;
            else if (s.bottom.unit != Unit::Auto)
                dy = cb.content_y + cb.content_height -
                     resolve(s.bottom, cb.content_height, s.font, d.root_font) - b.height - b.y;
        }
        dx = clamp64(dx, -MaxDimension);
        dy = clamp64(dy, -MaxDimension);
        d.offset_x[i] = dx;
        d.offset_y[i] = dy;
        b.x = clamp64(int64_t(b.x) + dx, -MaxDimension);
        b.content_x = clamp64(int64_t(b.content_x) + dx, -MaxDimension);
        b.y = clamp64(int64_t(b.y) + dy, -MaxDimension);
        b.content_y = clamp64(int64_t(b.content_y) + dy, -MaxDimension);
    }
    for (uint32_t i = 0; i < d.fragment_count; ++i) {
        co_await WorkPoint{1};
        auto &f = d.fragments[i];
        f.x = clamp64(int64_t(f.x) + d.offset_x[f.node], -MaxDimension);
        f.y = clamp64(int64_t(f.y) + d.offset_y[f.node], -MaxDimension);
    }
    for (uint32_t i = 0; i < d.node_count; ++i) {
        co_await WorkPoint{1};
        auto &b = d.boxes[i];
        auto &st = d.styles[i];
        int basis = b.width < b.height ? b.width : b.height;
        st.radius = clamp64(resolve(st.radius_length, basis, st.font, d.root_font), 0, 4096);
    }
    for (unsigned i = 0; i < d.generated_count; ++i) {
        co_await WorkPoint{1};
        auto &g = d.generated[i];
        const auto &b = d.boxes[g.owner];
        const auto &s = g.style;
        int natural = 0;
        const char *text = d.str(g.content);
        for (size_t p = 0, n = length(text, MaxVariableName); p < n;) {
            co_await WorkPoint{1};
            auto c=text_cluster(text,n,p,metrics,s,p==0);
            natural=clamp64(int64_t(natural)+cluster_width(text,c,metrics,s,p==0));p=c.end;
        }
        g.width = resolve(s.width, b.content_width, s.font, d.root_font, natural);
        g.height = resolve(s.height, b.content_height, s.font, d.root_font, *text ? s.line : 0);
        g.x = b.content_x + (g.after ? b.content_width - g.width : 0);
        g.y = b.content_y;
        if (s.left.unit != Unit::Auto)
            g.x = b.content_x + resolve(s.left, b.content_width, s.font, d.root_font);
        else if (s.right.unit != Unit::Auto)
            g.x = b.content_x + b.content_width - g.width -
                  resolve(s.right, b.content_width, s.font, d.root_font);
        if (s.top.unit != Unit::Auto)
            g.y = b.content_y + resolve(s.top, b.content_height, s.font, d.root_font);
        else if (s.bottom.unit != Unit::Auto)
            g.y = b.content_y + b.content_height - g.height -
                  resolve(s.bottom, b.content_height, s.font, d.root_font);
    }
    d.height = d.boxes[0].height;
    for (uint32_t i = 0; i < d.fragment_count; ++i) {
        co_await WorkPoint{1};
        auto &f = d.fragments[i];
        Clip clip{-MaxDimension, 0, MaxDimension * 2, MaxDimension};
        for (uint16_t p = d.nodes[f.node].parent; p; p = d.nodes[p].parent) {
            co_await WorkPoint{1};
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
        if (is_control(d, f.node) || d.tag(f.node, "img")) {
            const auto &b = d.boxes[f.node];
            clip = intersect(clip, {b.content_x, b.content_y, b.content_width, b.content_height});
        }
        f.clip = clip;
        int bottom = clamp64(int64_t(f.y) + f.height), limit = clamp64(int64_t(clip.y) + clip.height);
        if (bottom > limit)
            bottom = limit;
        if (bottom > d.height)
            d.height = bottom;
    }
    for (unsigned i = 0; i < d.control_count; ++i) {
        co_await WorkPoint{1};
        d.controls[i].reveal_caret = true;
    }
    d.layout_revision = d.revision;
    ++d.paint_revision;
    d.dirty = 1;
    d.damage.all();
    for (auto &flags : d.node_dirty)
        flags = 0;
}
inline void reflow(Document &d, int width, Metrics metrics) { finish_work(reflow_work(d, width, metrics)); }

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
