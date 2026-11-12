#pragma once
// BSF 2.1: bidirectional paragraphs of the Husarz DOM.
//
// A bidi paragraph is the inline content of one block container (CSS
// Writing Modes 3 §2.4): text of its inline descendants in tree order,
// atomic inlines (inline-block, img, input, button, select, textarea) as
// U+FFFC, and <br> / preserved newlines as paragraph separators (HTML
// renders br like U+000A). Inline elements with unicode-bidi != normal
// contribute the UAX #9 controls CSS prescribes:
//   embed            LRE/RLE ... PDF
//   isolate          LRI/RLI ... PDI
//   bidi-override    LRO/RLO ... PDF
//   isolate-override LRI/RLI LRO/RLO ... PDF PDI
//   plaintext        FSI ... PDI
// On a block: direction sets the paragraph level, plaintext makes every
// paragraph auto (P2/P3), bidi-override/isolate-override override all its
// inline content. Open inline controls are re-emitted after a separator or
// an interrupting block so nesting survives.
//
// The synthetic paragraph text exists only in a bounded scratch buffer; the
// DOM text is never modified or reordered. Levels are resolved by the one
// BSF UAX #9 implementation (BSF::BidiText) and stored as per-node level
// runs (Document::bidi_runs). Layout splits words at level changes and
// applies L2 per line (layout.h). Paragraphs that are LTR, contain no
// RTL/control characters and no RTL inline bidi elements are skipped
// (all level 0), so ordinary pages cost one scan.
//
// Allocation: plain new (Ring 3 is built with -fcheck-new, so failure yields
// nullptr and records LimitBidi; no exceptions, no std::nothrow).
// Budgets: MaxBidiParagraph bytes and MaxBidiSegments text pieces per
// paragraph (a longer paragraph is analysed in consecutive chunks: a chunk
// boundary acts as a paragraph boundary), MaxBidiRuns runs per document,
// MaxBidiNesting nested block contexts; exceeding one records LimitBidi and
// the remaining text keeps level 0 (never unbounded work, never a crash).
#include "css.h"
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
#define HUSARZ_BIDI 1
#else
#define HUSARZ_BIDI 0
#endif
namespace Husarz {
constexpr unsigned MaxBidiSegments = 2048, MaxBidiNesting = 8, MaxBidiOpen = 32;
// Cost of one synchronous step, capped to a single scheduler slice (a
// WorkPoint above MaxSliceWork would fail the job).
inline unsigned work_units(size_t n) { return n == 0 ? 1 : n > MaxSliceWork ? MaxSliceWork : unsigned(n); }
inline bool atomic_inline(const Document &d, uint16_t id) {
    return d.tag(id, "img") || d.tag(id, "input") || d.tag(id, "button") || d.tag(id, "select") ||
           d.tag(id, "textarea");
}
inline bool bidi_block(const Style &s) {
    return s.display == Display::Block || s.display == Display::Table || s.display == Display::RowGroup ||
           s.display == Display::Row || s.display == Display::Cell || s.display == Display::InlineBlock ||
           s.position == Position::Absolute;
}
// Level of byte `offset` of `node`; *paragraph receives the paragraph level.
inline uint8_t text_level(const Document &d, uint16_t node, uint32_t offset, uint8_t *paragraph = nullptr) {
    if (paragraph)
        *paragraph = 0;
    if (node >= MaxNodes || !d.bidi_first[node])
        return 0;
    uint32_t lo = d.bidi_first[node] - 1, hi = lo + d.bidi_count[node];
    if (hi > d.bidi_run_count)
        return 0;
    uint32_t first = lo;
    while (lo + 1 < hi) { // last run with run.offset <= offset
        uint32_t mid = lo + (hi - lo) / 2;
        if (d.bidi_runs[mid].offset <= offset)
            lo = mid;
        else
            hi = mid;
    }
    if (d.bidi_runs[lo].offset > offset)
        lo = first;
    if (paragraph)
        *paragraph = d.bidi_runs[lo].paragraph;
    return d.bidi_runs[lo].level;
}
// End (exclusive, node-relative) of the level run containing `offset`.
inline uint32_t text_level_end(const Document &d, uint16_t node, uint32_t offset, uint32_t limit) {
    if (node >= MaxNodes || !d.bidi_first[node])
        return limit;
    uint32_t first = d.bidi_first[node] - 1, end = first + d.bidi_count[node];
    for (uint32_t k = first; k < end && k < d.bidi_run_count; ++k)
        if (d.bidi_runs[k].offset > offset)
            return d.bidi_runs[k].offset < limit ? d.bidi_runs[k].offset : limit;
    return limit;
}
#if HUSARZ_BIDI
struct BidiScratch {
    struct Segment { uint32_t at, length, node_offset; uint16_t node; };
    struct Open { uint16_t depth; uint32_t open[2], close[2]; uint8_t opens, closes; };
    struct Context {
        char text[MaxBidiParagraph];
        Segment segments[MaxBidiSegments];
        Open open[MaxBidiOpen];
        uint32_t used{}, segment_count{}, open_count{};
        uint16_t depth{};
        BSF::BaseDirection base{BSF::BaseDirection::LTR};
        bool needs{}, plaintext{};
    };
    Context *contexts[MaxBidiNesting]{};
    unsigned count{};
    BSF::BidiText bt;
    ~BidiScratch() {
        for (auto *c : contexts)
            delete c;
    }
};
inline bool bidi_append(Document &d, BidiScratch::Context &c, const char *text, uint32_t n) {
    if (n > MaxBidiParagraph - c.used) {
        d.limit(LimitBidi);
        ++d.bidi_limited;
        return false;
    }
    for (uint32_t i = 0; i < n; ++i)
        c.text[c.used + i] = text[i];
    c.used += n;
    return true;
}
inline void bidi_append_cp(Document &d, BidiScratch::Context &c, uint32_t cp) {
    char b[4];
    unsigned k = encode(cp, b);
    if (bidi_append(d, c, b, k) && cp >= 0x202A && cp <= 0x2069)
        c.needs = true;
}
inline void bidi_reopen(Document &d, BidiScratch::Context &c) {
    for (unsigned i = 0; i < c.open_count; ++i)
        for (unsigned k = 0; k < c.open[i].opens; ++k)
            bidi_append_cp(d, c, c.open[i].open[k]);
}
// Resolve the context's paragraph text and store level runs per segment.
inline Work bidi_flush_work(Document &d, BidiScratch &scratch, BidiScratch::Context &c) {
    if (c.used && c.needs && c.segment_count) {
        co_await WorkPoint{work_units(c.used / 16 + 1)};
        ++d.bidi_paragraphs;
        BSF::BidiText &bt = scratch.bt;
        if (bt.analyze(c.text, c.used, c.plaintext ? BSF::BaseDirection::Auto : c.base)) {
            d.has_bidi = true;
            // Paragraph level of the B-separated sub-paragraph holding scalar
            // s, maintained incrementally (s only grows): O(n) in total.
            size_t s = 0, para_end = 0;
            uint8_t para_level = uint8_t(c.base == BSF::BaseDirection::RTL);
            auto paragraph_at = [&](size_t at) {
                while (at >= para_end && para_end < bt.n) {
                    size_t begin = para_end, e = begin;
                    while (e < bt.n && bt.cls[e] != uint8_t(BSF::BidiClass::B))
                        ++e;
                    para_end = e < bt.n ? e + 1 : bt.n;
                    para_level = c.plaintext ? BSF::resolve_paragraph_level(bt.cls + begin, e - begin, 0)
                                             : uint8_t(c.base == BSF::BaseDirection::RTL);
                }
                return para_level;
            };
            for (uint32_t g = 0; g < c.segment_count; ++g) {
                co_await WorkPoint{1};
                const auto &seg = c.segments[g];
                uint16_t node = seg.node;
                if (!d.bidi_first[node] && d.bidi_run_count < MaxBidiRuns) {
                    d.bidi_first[node] = d.bidi_run_count + 1;
                    d.bidi_count[node] = 0;
                }
                uint8_t last = 0xfe;
                while (s < bt.n && bt.offset[s] < seg.at)
                    ++s;
                for (; s < bt.n && bt.offset[s] < seg.at + seg.length; ++s) {
                    // Paragraph level: the last B before s decides (P1).
                    uint8_t level = bt.level[s];
                    if (level == BSF::BidiRemoved)
                        level = last == 0xfe ? 0 : last;
                    if (level != last) {
                        if (d.bidi_run_count >= MaxBidiRuns ||
                            d.bidi_first[node] + d.bidi_count[node] - 1 != d.bidi_run_count) {
                            d.limit(LimitBidi);
                            ++d.bidi_limited;
                            break;
                        }
                        uint8_t para = paragraph_at(s);
                        d.bidi_runs[d.bidi_run_count++] = {seg.node_offset + (bt.offset[s] - seg.at), node, level, para};
                        ++d.bidi_count[node];
                        last = level;
                    }
                }
            }
        }
    }
    c.used = c.segment_count = 0;
    c.needs = c.base == BSF::BaseDirection::RTL || c.plaintext;
}
inline BidiScratch::Context *bidi_push(Document &d, BidiScratch &scratch, uint16_t depth, const Style &s) {
    if (scratch.count >= MaxBidiNesting) {
        d.limit(LimitBidi);
        ++d.bidi_limited;
        return nullptr;
    }
    auto *&c = scratch.contexts[scratch.count];
    if (!c)
        c = new BidiScratch::Context;
    if (!c) {
        d.limit(LimitBidi);
        return nullptr;
    }
    ++scratch.count;
    c->used = c->segment_count = c->open_count = 0;
    c->depth = depth;
    c->plaintext = s.unicode_bidi == UnicodeBidi::Plaintext;
    c->base = s.direction ? BSF::BaseDirection::RTL : BSF::BaseDirection::LTR;
    c->needs = s.direction || c->plaintext;
    if (s.unicode_bidi == UnicodeBidi::BidiOverride || s.unicode_bidi == UnicodeBidi::IsolateOverride) {
        auto &o = c->open[c->open_count++];
        o = {depth, {s.direction ? 0x202Eu : 0x202Du, 0}, {0x202Cu, 0}, 1, 1};
        bidi_reopen(d, *c);
    }
    return c;
}
#endif
// Resolve all bidi paragraphs of the document (cooperative, bounded).
inline Work resolve_bidi_work(Document &d) {
    d.bidi_run_count = 0;
    d.has_bidi = false;
    d.bidi_paragraphs = d.bidi_limited = 0;
    for (uint32_t i = 0; i < d.node_count && i < MaxNodes; ++i) {
        if (!(i & 255))
            co_await WorkPoint{1};
        d.bidi_first[i] = 0;
        d.bidi_count[i] = 0;
    }
#if HUSARZ_BIDI
    BidiScratch *scratch = new BidiScratch;
    if (!scratch) {
        d.limit(LimitBidi);
        co_return;
    }
    // Element stack by depth: 0 none, 1 inline controls, 2 block context,
    // 3 inline-block context.
    uint8_t kind[MaxDepth + 2]{};
    uint16_t skip_depth = 0xffff;
    if (!bidi_push(d, *scratch, 0, d.styles[0])) {
        delete scratch;
        co_return;
    }
    auto close_to = [&](unsigned depth) -> Work {
        // Close elements at depth >= `depth` (LIFO).
        for (unsigned k = MaxDepth + 1; k-- > depth;) {
            if (!kind[k])
                continue;
            if (kind[k] == 3 && scratch->count > 1) {
                // Inline-block: its own paragraphs; the outer one continues.
                co_await bidi_flush_work(d, *scratch, *scratch->contexts[scratch->count - 1]);
                --scratch->count;
            } else if (kind[k] == 2 && scratch->count > 1) {
                co_await bidi_flush_work(d, *scratch, *scratch->contexts[scratch->count - 1]);
                --scratch->count;
                // The parent paragraph continues after the block.
                auto &parent = *scratch->contexts[scratch->count - 1];
                co_await bidi_flush_work(d, *scratch, parent);
                bidi_reopen(d, parent);
            } else if (kind[k] == 1) {
                auto &c = *scratch->contexts[scratch->count - 1];
                if (c.open_count && c.open[c.open_count - 1].depth == k) {
                    auto &o = c.open[c.open_count - 1];
                    for (unsigned j = 0; j < o.closes; ++j)
                        bidi_append_cp(d, c, o.close[j]);
                    --c.open_count;
                }
            }
            kind[k] = 0;
        }
    };
    for (uint16_t id = 1; id < d.node_count; ++id) {
        co_await WorkPoint{1};
        const Node &n = d.nodes[id];
        unsigned depth = n.depth;
        if (depth > MaxDepth)
            continue;
        if (depth <= skip_depth)
            skip_depth = 0xffff;
        co_await close_to(depth);
        if (skip_depth != 0xffff && depth > skip_depth)
            continue;
        const Style &s = d.styles[id];
        auto &c = *scratch->contexts[scratch->count - 1];
        if (n.is_text) {
            if (n.text_length > MaxBidiParagraph - c.used || c.segment_count == MaxBidiSegments) {
                // Chunk boundary: analyse what we have, continue afresh.
                co_await bidi_flush_work(d, *scratch, c);
                bidi_reopen(d, c);
            }
            uint32_t len = n.text_length < MaxBidiParagraph - c.used ? n.text_length : MaxBidiParagraph - c.used;
            if (len < n.text_length) {
                d.limit(LimitBidi);
                ++d.bidi_limited;
            }
            const char *t = d.str(n.text);
            const Style &ps = d.styles[n.parent];
            bool preserve = ps.white == WhiteSpace::Pre || ps.white == WhiteSpace::PreWrap;
            uint32_t at = c.used;
            for (uint32_t i = 0; i < len; ++i) {
                if (!(i & 255))
                    co_await WorkPoint{1};
                char ch = t[i];
                if (!preserve && (ch == '\n' || ch == '\r' || ch == '\t' || ch == '\f'))
                    ch = ' ';
                c.text[c.used++] = ch;
            }
            if (!c.needs && BSF::needs_complex_layout(t, len))
                c.needs = true;
            c.segments[c.segment_count++] = {at, len, 0, id};
            continue;
        }
        if (s.display == Display::None) {
            skip_depth = uint16_t(depth);
            continue;
        }
        if (d.tag(id, "br")) {
            bidi_append_cp(d, c, '\n');
            bidi_reopen(d, c);
            skip_depth = uint16_t(depth);
            continue;
        }
        if (atomic_inline(d, id) && s.display != Display::Block && s.position != Position::Absolute) {
            // Atomic inline: one neutral U+FFFC mapped to the element itself.
            if (c.segment_count < MaxBidiSegments) {
                uint32_t at = c.used;
                bidi_append_cp(d, c, 0xFFFC);
                c.segments[c.segment_count++] = {at, c.used - at, 0, id};
            }
            skip_depth = uint16_t(depth);
            continue;
        }
        if (bidi_block(s)) {
            bool inline_box = s.display == Display::InlineBlock && s.position != Position::Absolute;
            if (inline_box) {
                if (c.segment_count < MaxBidiSegments) {
                    uint32_t at = c.used;
                    bidi_append_cp(d, c, 0xFFFC);
                    c.segments[c.segment_count++] = {at, c.used - at, 0, id};
                }
            } else
                co_await bidi_flush_work(d, *scratch, c); // block ends the paragraph
            if (bidi_push(d, *scratch, uint16_t(depth), s))
                kind[depth] = inline_box ? 3 : 2;
            else
                skip_depth = uint16_t(depth);
            continue;
        }
        if (s.unicode_bidi != UnicodeBidi::Normal && c.open_count < MaxBidiOpen) {
            BidiScratch::Open o{uint16_t(depth), {0, 0}, {0, 0}, 0, 0};
            bool rtl = s.direction;
            switch (s.unicode_bidi) {
            case UnicodeBidi::Embed: o.open[o.opens++] = rtl ? 0x202B : 0x202A; o.close[o.closes++] = 0x202C; break;
            case UnicodeBidi::Isolate: o.open[o.opens++] = rtl ? 0x2067 : 0x2066; o.close[o.closes++] = 0x2069; break;
            case UnicodeBidi::BidiOverride: o.open[o.opens++] = rtl ? 0x202E : 0x202D; o.close[o.closes++] = 0x202C; break;
            case UnicodeBidi::IsolateOverride:
                o.open[o.opens++] = rtl ? 0x2067 : 0x2066; o.open[o.opens++] = rtl ? 0x202E : 0x202D;
                o.close[o.closes++] = 0x202C; o.close[o.closes++] = 0x2069; break;
            case UnicodeBidi::Plaintext: o.open[o.opens++] = 0x2068; o.close[o.closes++] = 0x2069; break;
            default: break;
            }
            for (unsigned k = 0; k < o.opens; ++k)
                bidi_append_cp(d, c, o.open[k]);
            c.open[c.open_count++] = o;
            kind[depth] = 1;
        }
    }
    co_await close_to(1);
    while (scratch->count) {
        co_await bidi_flush_work(d, *scratch, *scratch->contexts[scratch->count - 1]);
        --scratch->count;
    }
    delete scratch;
#endif
}
inline void resolve_bidi(Document &d) { finish_work(resolve_bidi_work(d)); }
} // namespace Husarz
