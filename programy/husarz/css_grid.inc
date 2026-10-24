constexpr unsigned MaxGridColumns = 16;
inline bool grid_columns(Span input, int available, int gap, int font, int root, int *widths,
                         unsigned &count) {
    Span tracks[MaxGridColumns];
    unsigned n = words(input, tracks, MaxGridColumns);
    count = 0;
    if (n == 1 && input.size > 8 && equal_ci(input.data, 7, "repeat(") && input.data[input.size - 1] == ')') {
        CssTokenizer t(input.data + 7, input.size - 8);
        size_t begin = t.p;
        t.scan(",");
        Span repetitions = trimmed(t.s + begin, t.p - begin);
        if (t.p == t.n)
            return false;
        ++t.p;
        Span track = trimmed(t.s + t.p, t.n - t.p);
        unsigned columns = 0;
        if (value_is(repetitions, "auto-fit") || value_is(repetitions, "auto-fill")) {
            if (track.size < 12 || !equal_ci(track.data, 7, "minmax(") || track.data[track.size - 1] != ')')
                return false;
            CssTokenizer bounds(track.data + 7, track.size - 8);
            bounds.scan(",");
            Length minimum;
            if (!css_length(trimmed(bounds.s, bounds.p), minimum) || bounds.p == bounds.n)
                return false;
            Span maximum = trimmed(bounds.s + bounds.p + 1, bounds.n - bounds.p - 1);
            if (!value_is(maximum, "1fr"))
                return false;
            int px = resolve(minimum, available, font, root);
            if (px < 1)
                return false;
            columns = unsigned((int64_t(available) + gap) / (int64_t(px) + gap));
            if (!columns)
                columns = 1;
            if (columns > MaxGridColumns)
                columns = MaxGridColumns;
            track = {"1fr", 3};
        } else {
            int fixed;
            size_t used;
            if (!number(repetitions, fixed, used) || used != repetitions.size || fixed % 1000 ||
                fixed < 1000 || fixed > int(MaxGridColumns) * 1000)
                return false;
            columns = unsigned(fixed / 1000);
        }
        n = columns;
        for (unsigned i = 0; i < n; ++i)
            tracks[i] = track;
    }
    if (!n || n > MaxGridColumns)
        return false;
    int64_t fixed = 0, fractions = 0;
    int fraction[MaxGridColumns]{};
    for (unsigned i = 0; i < n; ++i) {
        Length l;
        int num;
        size_t used;
        if (number(tracks[i], num, used) && used + 2 == tracks[i].size &&
            equal_ci(tracks[i].data + used, 2, "fr") && num > 0) {
            fraction[i] = num;
            fractions += num;
            widths[i] = 0;
        } else if (css_length(tracks[i], l) && l.unit != Unit::Auto) {
            widths[i] = resolve(l, available, font, root);
            fixed += widths[i];
        } else
            return false;
    }
    int remaining = clamp64(int64_t(available) - fixed - int64_t(gap) * (n - 1));
    for (unsigned i = 0; i < n; ++i)
        if (fraction[i])
            widths[i] = clamp64(int64_t(remaining) * fraction[i] / fractions);
    count = n;
    return true;
}
