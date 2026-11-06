#pragma once
#include "http_target.h"
#include "document.h"
namespace Husarz {
constexpr size_t MaxUrl = 2048;
struct Url {
    char host[254]{};
    char target[2048]{};
    uint16_t port{};
    bool secure{};
};
inline bool parse_absolute_url(const char *s, Url &u) {
    size_t n = length(s, MaxUrl), p = 0;
    if (!n || n == MaxUrl)
        return false;
    u = {};
    if (n >= 8 && equal_ci(s, 8, "https://")) {
        p = 8;
        u.secure = true;
        u.port = 443;
    } else if (n >= 7 && equal_ci(s, 7, "http://")) {
        p = 7;
        u.port = 80;
    } else
        return false;
    size_t h = 0;
    while (p < n && s[p] != '/' && s[p] != '?' && s[p] != '#' && s[p] != ':') {
        if (h == 253)
            return false;
        u.host[h++] = lower(s[p++]);
    }
    if (!HttpTarget::hostname(u.host))
        return false;
    if (p < n && s[p] == ':') {
        unsigned port = 0;
        size_t start = ++p;
        while (p < n && s[p] >= '0' && s[p] <= '9') {
            unsigned digit = unsigned(s[p++] - '0');
            if (port > (65535 - digit) / 10)
                return false;
            port = port * 10 + digit;
        }
        if (p == start || !port)
            return false;
        u.port = uint16_t(port);
    }
    if (p < n && s[p] != '/' && s[p] != '?' && s[p] != '#')
        return false;
    size_t t = 0;
    if (p == n || s[p] != '/')
        u.target[t++] = '/';
    constexpr char digits[] = "0123456789ABCDEF";
    while (p < n && s[p] != '#') {
        unsigned c = uint8_t(s[p++]);
        if (c < 32 || c == 127 || c == '\\')
            return false;
        if (c == ' ' || c >= 128) {
            if (t + 3 >= sizeof(u.target))
                return false;
            u.target[t++] = '%';
            u.target[t++] = digits[c >> 4];
            u.target[t++] = digits[c & 15];
        } else {
            if (t + 1 >= sizeof(u.target))
                return false;
            u.target[t++] = char(c);
        }
    }
    u.target[t] = 0;
    return !HttpTarget::target_error(u.target);
}
// RFC-style dot segment removal touches only the path. Query bytes, repeated
// slashes and existing percent escapes remain intact.
inline bool normalize_resource_url(const char *input, char *out, size_t cap) {
    if (!input || !out || !cap)
        return false;
    struct Scratch {
        Url u{};
        char path[2048]{};
    };
    auto *w = new Scratch;
    if (!w)
        return false;
    bool ok = parse_absolute_url(input, w->u);
    size_t used = 0;
    auto append = [&](const char *s, size_t n) {
        if (n >= cap || used >= cap - n)
            return false;
        for (size_t i = 0; i < n; ++i)
            out[used++] = s[i];
        out[used] = 0;
        return true;
    };
    if (ok) {
        const char *target = w->u.target;
        size_t p = 0, k = 0;
        while (target[p] && target[p] != '?') {
            if (target[p] == '/' && target[p + 1] == '.' &&
                (target[p + 2] == '/' || !target[p + 2] || target[p + 2] == '?')) {
                p += 2;
                if (target[p] != '/')
                    w->path[k++] = '/';
                continue;
            }
            if (target[p] == '/' && target[p + 1] == '.' && target[p + 2] == '.' &&
                (target[p + 3] == '/' || !target[p + 3] || target[p + 3] == '?')) {
                p += 3;
                while (k && w->path[k - 1] != '/')
                    --k;
                if (k)
                    --k;
                if (target[p] != '/')
                    w->path[k++] = '/';
                continue;
            }
            w->path[k++] = target[p++];
        }
        if (!k)
            w->path[k++] = '/';
        while (target[p])
            w->path[k++] = target[p++];
        w->path[k] = 0;
        ok = append(w->u.secure ? "https://" : "http://", w->u.secure ? 8 : 7) &&
             append(w->u.host, length(w->u.host, 254));
        if (ok && w->u.port != (w->u.secure ? 443 : 80)) {
            char port[8];
            unsigned t = w->u.port, c = 0;
            char rev[5];
            do {
                rev[c++] = char('0' + t % 10);
                t /= 10;
            } while (t);
            size_t q = 0;
            port[q++] = ':';
            while (c)
                port[q++] = rev[--c];
            ok = append(port, q);
        }
        if (ok)
            ok = append(w->path, k);
    }
    delete w;
    if (!ok)
        out[0] = 0;
    return ok;
}
inline bool resolve_resource_url(const char *base, const char *reference, char *out, size_t cap) {
    if (!base || !reference || !out)
        return false;
    size_t n = length(reference, MaxUrl);
    if (n == MaxUrl)
        return false;
    while (n && space(*reference)) {
        ++reference;
        --n;
    }
    while (n && space(reference[n - 1]))
        --n;
    struct Scratch {
        char combined[2048]{};
        Url u{};
    };
    auto *w = new Scratch;
    if (!w)
        return false;
    size_t used = 0;
    bool ok = true;
    auto add = [&](const char *s, size_t k) {
        if (k >= sizeof(w->combined) || used >= sizeof(w->combined) - k)
            return false;
        for (size_t i = 0; i < k; ++i)
            w->combined[used++] = s[i];
        w->combined[used] = 0;
        return true;
    };
    if ((n >= 8 && equal_ci(reference, 8, "https://")) || (n >= 7 && equal_ci(reference, 7, "http://")))
        ok = add(reference, n);
    else if (!parse_absolute_url(base, w->u))
        ok = false;
    else {
        for (size_t i = 0; i < n && reference[i] != '/' && reference[i] != '?' && reference[i] != '#'; ++i)
            if (reference[i] == ':')
                ok = false;
        if (ok && n >= 2 && reference[0] == '/' && reference[1] == '/')
            ok = add(w->u.secure ? "https:" : "http:", w->u.secure ? 6 : 5) && add(reference, n);
        else if (ok) {
            size_t authority = w->u.secure ? 8 : 7;
            while (base[authority] && base[authority] != '/' && base[authority] != '?' &&
                   base[authority] != '#')
                ++authority;
            ok = add(base, authority);
            if (!n || reference[0] == '#')
                ok = ok && add(w->u.target, length(w->u.target, 2048));
            else if (reference[0] != '/') {
                size_t end = 0, last = 0;
                while (w->u.target[end] && w->u.target[end] != '?') {
                    if (w->u.target[end] == '/')
                        last = end + 1;
                    ++end;
                }
                ok = ok && add(w->u.target, reference[0] == '?' ? end : last);
            }
            ok = ok && add(reference, n);
        }
    }
    if (ok)
        ok = normalize_resource_url(w->combined, out, cap);
    delete w;
    return ok;
}
inline bool mime_is(const char *value, const char *expected) {
    if (!value)
        return false;
    while (space(*value))
        ++value;
    size_t n = 0;
    while (value[n] && value[n] != ';' && !space(value[n]))
        ++n;
    return equal_ci(value, n, expected);
}
} // namespace Husarz
