// Bounded flex sizing and placement. Nested contents use the existing resumable
// layout; translations are composed in the final parent-first transform pass.
constexpr unsigned MaxFlexItems = 128;
inline Work plan_flex_work(Document &d, uint16_t id, Metrics metrics) {
    const auto &s = d.styles[id];
    if (!s.flex)
        co_return;
    const auto &box = d.boxes[id];
    int available = box.content_width;
    if (s.grid) {
        int widths[MaxGridColumns]{};
        unsigned columns = 1, index = 0;
        widths[0] = available;
        int gap = resolve(s.column_gap, available, s.font, d.root_font);
        if (s.grid_template)
            grid_columns({d.str(s.grid_template), length(d.str(s.grid_template), 512)}, available, gap,
                         s.font, d.root_font, widths, columns);
        for (uint16_t c = d.nodes[id].first; c; c = d.nodes[c].next) {
            co_await WorkPoint{1};
            if (d.nodes[c].is_text || d.styles[c].display == Display::None ||
                d.styles[c].position == Position::Absolute)
                continue;
            d.flex_width[c] = widths[index++ % columns];
        }
        co_return;
    }
    uint16_t items[MaxFlexItems]{};
    int minimums[MaxFlexItems]{};
    unsigned count = 0;
    int64_t total = 0, grow = 0, shrink = 0;
    int gap = resolve(s.column_gap, available, s.font, d.root_font);
    for (uint16_t c = d.nodes[id].first; c; c = d.nodes[c].next) {
        co_await WorkPoint{1};
        const auto &cs = d.styles[c];
        if (d.nodes[c].is_text || cs.display == Display::None || cs.position == Position::Absolute)
            continue;
        if (count == MaxFlexItems) {
            d.limit(LimitSelectorWork);
            break;
        }
        items[count++] = c;
        int intrinsic = 0, min_content = 0;
        co_await intrinsic_width_work(d, c, metrics, intrinsic, &min_content);
        int extra = cs.border[1] + cs.border[3] + resolve(cs.padding[1], available, cs.font, d.root_font) +
                    resolve(cs.padding[3], available, cs.font, d.root_font);
        Length basis = !s.flex_column && cs.flex_basis.unit != Unit::Auto ? cs.flex_basis : cs.width;
        int width = resolve(basis, available, cs.font, d.root_font, intrinsic);
        if (basis.unit == Unit::Auto || !cs.border_box)
            width = clamp64(int64_t(width) + extra);
        if (s.flex_column && cs.width.unit == Unit::Auto && s.align_items == 0)
            width = available;
        if (s.flex_wrap && width > available)
            width = available;
        if (width < extra)
            width = extra;
        int minimum = resolve(cs.min_width, available, cs.font, d.root_font);
        if (cs.flex_min_auto && cs.overflow_x == Overflow::Visible) {
            minimum = clamp64(int64_t(min_content) + extra);
            if (cs.width.unit != Unit::Auto) {
                int specified = resolve(cs.width, available, cs.font, d.root_font);
                if (!cs.border_box)
                    specified = clamp64(int64_t(specified) + extra);
                if (specified < minimum)
                    minimum = specified;
            }
        }
        minimums[count - 1] = minimum;
        d.flex_width[c] = width;
        total += width;
        grow += cs.flex_grow;
        shrink += int64_t(cs.flex_shrink) * width;
    }
    if (count > 1)
        total += int64_t(gap) * (count - 1);
    int64_t free = int64_t(available) - total;
    if (!s.flex_column && !s.flex_wrap)
        for (unsigned i = 0; i < count; ++i) {
            co_await WorkPoint{1};
            auto c = items[i];
            int64_t width = d.flex_width[c];
            if (free > 0 && grow)
                width += free * d.styles[c].flex_grow / grow;
            else if (free < 0 && shrink)
                width += free * d.styles[c].flex_shrink * width / shrink;
            d.flex_width[c] = clamp64(width, minimums[i] > 1 ? minimums[i] : 1);
        }
}
inline Work arrange_flex_work(Document &d, uint16_t id, int &content_height) {
    const auto &s = d.styles[id];
    if (!s.flex)
        co_return;
    auto &parent = d.boxes[id];
    uint16_t items[MaxFlexItems]{};
    unsigned count = 0;
    for (uint16_t c = d.nodes[id].first; c && count < MaxFlexItems; c = d.nodes[c].next) {
        co_await WorkPoint{1};
        if (!d.nodes[c].is_text && d.styles[c].display != Display::None &&
            d.styles[c].position != Position::Absolute)
            items[count++] = c;
    }
    int rowgap = resolve(s.row_gap, parent.content_width, s.font, d.root_font),
        colgap = resolve(s.column_gap, parent.content_width, s.font, d.root_font);
    int cross = 0;
    unsigned start = 0;
    while (start < count) {
        co_await WorkPoint{1};
        unsigned end = start;
        int main = 0, linecross = 0;
        int gap = s.flex_column ? rowgap : colgap;
        while (end < count) {
            co_await WorkPoint{1};
            auto &b = d.boxes[items[end]];
            int extent = s.flex_column ? b.height : b.width;
            if (!s.flex_column && s.flex_wrap && end > start &&
                int64_t(main) + gap + extent > parent.content_width)
                break;
            main = clamp64(int64_t(main) + (end > start ? gap : 0) + extent);
            int c = s.flex_column ? b.width : b.height;
            if (c > linecross)
                linecross = c;
            ++end;
        }
        unsigned n = end - start;
        int available = s.flex_column ? parent.content_height : parent.content_width;
        if (!s.flex_column && !s.flex_wrap && parent.content_height > linecross)
            linecross = parent.content_height;
        if (!available)
            available = main;
        int free = clamp64(int64_t(available) - main), cursor = 0, between = gap;
        if (s.justify == 1)
            cursor = free;
        else if (s.justify == 2)
            cursor = free / 2;
        else if (s.justify == 3 && n > 1)
            between += free / int(n - 1);
        else if (s.justify == 4) {
            between += free / int(n);
            cursor = free / int(n * 2);
        } else if (s.justify == 5) {
            between += free / int(n + 1);
            cursor = free / int(n + 1);
        }
        for (unsigned j = start; j < end; ++j) {
            co_await WorkPoint{1};
            uint16_t c = items[s.flex_reverse ? end - 1 - (j - start) : j];
            auto &b = d.boxes[c];
            const auto &cs = d.styles[c];
            int remaining = linecross - (s.flex_column ? b.width : b.height);
            uint8_t align = cs.align_self == 255 ? s.align_items : cs.align_self;
            int offset = align == 2 ? remaining : align == 3 ? remaining / 2 : 0;
            if (align == 0 && !s.flex_column && cs.height.unit == Unit::Auto && remaining > 0) {
                b.height += remaining;
                b.content_height += remaining;
            }
            int x = parent.content_x + (s.flex_column ? offset : cursor),
                y = parent.content_y + (s.flex_column ? cursor : cross + offset);
            d.flex_dx[c] = clamp64(int64_t(x) - b.x, -MaxDimension);
            d.flex_dy[c] = clamp64(int64_t(y) - b.y, -MaxDimension);
            cursor = clamp64(int64_t(cursor) + (s.flex_column ? b.height : b.width) + between);
        }
        cross = clamp64(int64_t(cross) + linecross + (end < count ? rowgap : 0));
        if (s.flex_column) {
            content_height = available;
            co_return;
        }
        start = end;
    }
    content_height = cross;
}
