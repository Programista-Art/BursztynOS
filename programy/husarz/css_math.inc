// Small bounded length grammar. Percentages stay relative; mixed percentage and
// absolute expressions are rejected rather than guessed without a containing box.
struct LengthMath {
    Span input;
    size_t p{};
    unsigned tokens{}, depth{};
    int font, root, vw, vh;
    bool ok{true};
    void ws() {
        while (p < input.size && space(input.data[p]))
            ++p;
    }
    Length normalize(Length a) {
        int basis = a.unit == Unit::Em    ? font
                    : a.unit == Unit::Rem ? root
                    : a.unit == Unit::Vw  ? vw
                    : a.unit == Unit::Vh  ? vh
                                          : 0;
        if (basis) {
            a.value =
                clamp64(int64_t(a.value) * basis / ((a.unit == Unit::Vw || a.unit == Unit::Vh) ? 100 : 1),
                        -INT32_MAX, INT32_MAX);
            a.unit = Unit::Px;
        }
        return a;
    }
    Length atom() {
        ws();
        if (++tokens > 64 || ++depth > 8) {
            ok = false;
            --depth;
            return {};
        }
        Length out;
        size_t b = p;
        if (p < input.size && (lower(input.data[p]) >= 'a' && lower(input.data[p]) <= 'z')) {
            while (p < input.size && ident(input.data[p]))
                ++p;
            Span name{input.data + b, p - b};
            ws();
            if (p == input.size || input.data[p++] != '(') {
                ok = false;
                --depth;
                return {};
            }
            out = sum();
            ws();
            if (value_is(name, "min") || value_is(name, "max") || value_is(name, "clamp")) {
                Length args[3];
                args[0] = out;
                unsigned count = 1;
                while (p < input.size && input.data[p] == ',' && count < 3) {
                    ++p;
                    args[count++] = sum();
                    ws();
                }
                if ((value_is(name, "clamp") && count != 3) || (!value_is(name, "clamp") && count < 2))
                    ok = false;
                for (unsigned i = 1; i < count; ++i)
                    if (args[i].unit != args[0].unit)
                        ok = false;
                if (value_is(name, "min")) {
                    for (unsigned i = 1; i < count; ++i)
                        if (args[i].value < out.value)
                            out = args[i];
                } else if (value_is(name, "max")) {
                    for (unsigned i = 1; i < count; ++i)
                        if (args[i].value > out.value)
                            out = args[i];
                } else {
                    out = args[1];
                    if (out.value > args[2].value)
                        out = args[2];
                    if (out.value < args[0].value)
                        out = args[0];
                }
            } else if (!value_is(name, "calc"))
                ok = false;
            if (p == input.size || input.data[p++] != ')')
                ok = false;
        } else if (p < input.size && input.data[p] == '(') {
            ++p;
            out = sum();
            ws();
            if (p == input.size || input.data[p++] != ')')
                ok = false;
        } else {
            if (p < input.size && (input.data[p] == '+' || input.data[p] == '-'))
                ++p;
            while (p < input.size && ((input.data[p] >= '0' && input.data[p] <= '9') || input.data[p] == '.'))
                ++p;
            while (p < input.size &&
                   ((lower(input.data[p]) >= 'a' && lower(input.data[p]) <= 'z') || input.data[p] == '%'))
                ++p;
            if (p == b || !css_simple_length({input.data + b, p - b}, out, true, true))
                ok = false;
            out = normalize(out);
        }
        --depth;
        return out;
    }
    Length product() {
        Length a = atom();
        ws();
        while (ok && p < input.size && (input.data[p] == '*' || input.data[p] == '/')) {
            char op = input.data[p++];
            Length b = atom();
            if (++tokens > 64) {
                ok = false;
                break;
            }
            if (op == '*') {
                if (a.unit == Unit::Number && b.unit != Unit::Number) {
                    auto t = a;
                    a = b;
                    b = t;
                }
                if (b.unit != Unit::Number)
                    ok = false;
                a.value = clamp64(int64_t(a.value) * b.value / 1000, -INT32_MAX, INT32_MAX);
            } else {
                if (b.unit != Unit::Number || !b.value) {
                    ok = false;
                    break;
                }
                a.value = clamp64(int64_t(a.value) * 1000 / b.value, -INT32_MAX, INT32_MAX);
            }
            ws();
        }
        return a;
    }
    Length sum() {
        Length a = product();
        ws();
        while (ok && p < input.size && (input.data[p] == '+' || input.data[p] == '-')) {
            char op = input.data[p++];
            Length b = product();
            if (++tokens > 64) {
                ok = false;
                break;
            }
            if (a.unit != b.unit) {
                if (!a.value && a.unit == Unit::Number)
                    a.unit = b.unit;
                else if (!b.value && b.unit == Unit::Number)
                    b.unit = a.unit;
                else
                    ok = false;
            }
            a.value = clamp64(int64_t(a.value) + (op == '+' ? int64_t(b.value) : -int64_t(b.value)),
                              -INT32_MAX, INT32_MAX);
            ws();
        }
        return a;
    }
};
inline bool math_length(Span v, Length &l, bool negative = false, bool scalar = false, int font = 16,
                        int root = 16, int vw = 800, int vh = 600) {
    v = trimmed(v.data, v.size);
    if (css_simple_length(v, l, negative, scalar))
        return true;
    if (v.size > 512 || v.size < 6)
        return false;
    LengthMath parser{v, 0, 0, 0, font, root, vw, vh};
    Length result = parser.sum();
    parser.ws();
    if (!parser.ok || parser.p != v.size || (!negative && result.value < 0))
        return false;
    if (result.unit == Unit::Number && !scalar) {
        if (result.value)
            return false;
        result.unit = Unit::Px;
    }
    if (result.unit == Unit::Auto)
        return false;
    l = result;
    return true;
}
inline bool css_length(Span v, Length &l, bool negative = false, bool scalar = false) {
    return math_length(v, l, negative, scalar);
}
