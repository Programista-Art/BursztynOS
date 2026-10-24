inline bool shadow(Span value, Style &style) {
    if (value_is(value, "none")) {
        style.has_shadow = false;
        return true;
    }
    Span parts[6];
    unsigned n = words(value, parts, 6), lengths = 0;
    int values[4]{};
    uint32_t c = style.color;
    uint8_t alpha = 255;
    bool transparent = false;
    if (n < 2 || n > 6)
        return false;
    for (unsigned i = 0; i < n; ++i) {
        Length l;
        if (css_length(parts[i], l, true) && l.unit != Unit::Auto && l.unit != Unit::Percent) {
            if (lengths == 4)
                return false;
            values[lengths++] = resolve(l, 0, style.font, 16);
        } else if (!color(parts[i], c, transparent, &alpha))
            return false;
    }
    if (lengths < 2 || values[2] < 0)
        return false;
    style.has_shadow = true;
    style.shadow_x = clamp64(values[0], -256, 256);
    style.shadow_y = clamp64(values[1], -256, 256);
    style.shadow_blur = clamp64(values[2], 0, 16);
    style.shadow_spread = clamp64(values[3], -64, 64);
    style.shadow_color = c;
    style.shadow_alpha = alpha;
    return true;
}
