#pragma once
// BSF 2.2 — CSS @font-face support for Husarz (pure parsing / policy helpers;
// no network, no BSF dependency, host-testable).
//
// Policy summary (docs/bsf2-2-husarz-webfonts.md):
// - src is tried in order; url() sources with an unsupported format() hint
//   are skipped without a request; local() matches installed families.
// - Supported formats: truetype, opentype, woff, woff2 (and their
//   "-variations" spellings). embedded-opentype, svg and collection are not.
// - Fonts are browser-scoped resources; they never reach /czcionki or the
//   system catalog.
// - Cross-origin font responses must carry Access-Control-Allow-Origin "*" or
//   the document origin (the kernel HTTP client cannot send an Origin header,
//   so servers that only answer to a request Origin are treated as denying).
#include "origin.h"
#include <stdint.h>
#include <stddef.h>
namespace Husarz {
enum class FontFormat : uint8_t { Any, TrueType, OpenType, Woff, Woff2, Unsupported };
enum class FontDisplay : uint8_t { Auto, Block, Swap, Fallback, Optional };
constexpr unsigned MaxFontSources = 8, MaxUnicodeRanges = 16;
struct FontSource {
    bool local{};
    FontFormat format{FontFormat::Any};
    char value[512]{}; // URL (unresolved) or local family name
};
struct FontFaceDescriptor {
    uint16_t weight_min{400}, weight_max{400}, stretch{100};
    bool italic{};
    FontDisplay display{FontDisplay::Auto};
    uint8_t range_count{};
    uint32_t ranges[MaxUnicodeRanges][2]{};
};
inline char font_lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
inline bool font_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
inline bool font_ci(const char *a, size_t n, const char *b) {
    size_t i = 0;
    for (; i < n && b[i]; ++i)
        if (font_lower(a[i]) != font_lower(b[i]))
            return false;
    return i == n && !b[i];
}
inline FontFormat font_format_name(const char *s, size_t n) {
    while (n && font_space(*s)) { ++s; --n; }
    while (n && font_space(s[n - 1])) --n;
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) { ++s; n -= 2; }
    if (font_ci(s, n, "truetype") || font_ci(s, n, "truetype-variations")) return FontFormat::TrueType;
    if (font_ci(s, n, "opentype") || font_ci(s, n, "opentype-variations")) return FontFormat::OpenType;
    if (font_ci(s, n, "woff") || font_ci(s, n, "woff-variations")) return FontFormat::Woff;
    if (font_ci(s, n, "woff2") || font_ci(s, n, "woff2-variations")) return FontFormat::Woff2;
    return FontFormat::Unsupported; // embedded-opentype, svg, collection, unknown
}
// Copies an url()/local() argument, unquoting and unescaping simple escapes.
inline bool font_argument(const char *s, size_t n, char *out, size_t cap) {
    while (n && font_space(*s)) { ++s; --n; }
    while (n && font_space(s[n - 1])) --n;
    char quote = 0;
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) { quote = s[0]; ++s; n -= 2; }
    (void)quote;
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) c = s[++i];
        if (uint8_t(c) < 32 || k + 1 >= cap) return false;
        out[k++] = c;
    }
    out[k] = 0;
    return k > 0;
}
// Parses a CSS src descriptor into at most MaxFontSources entries (in order).
inline unsigned parse_font_sources(const char *s, size_t n, FontSource *out) {
    unsigned count = 0;
    size_t i = 0;
    while (i < n && count < MaxFontSources) {
        while (i < n && (font_space(s[i]) || s[i] == ',')) ++i;
        if (i >= n) break;
        FontSource src{};
        bool ok = false;
        auto function = [&](const char *name, size_t len) {
            if (n - i < len + 1) return false;
            for (size_t k = 0; k < len; ++k) if (font_lower(s[i + k]) != name[k]) return false;
            return s[i + len] == '(';
        };
        bool is_url = function("url", 3), is_local = !is_url && function("local", 5);
        if (is_url || is_local) {
            size_t start = i + (is_url ? 4 : 6), end = start;
            char quote = 0;
            while (end < n) {
                char c = s[end];
                if (quote) { if (c == '\\') ++end; else if (c == quote) quote = 0; }
                else if (c == '"' || c == '\'') quote = c;
                else if (c == ')') break;
                ++end;
            }
            if (end >= n) break;
            ok = font_argument(s + start, end - start, src.value, sizeof(src.value));
            src.local = is_local;
            i = end + 1;
            while (i < n && font_space(s[i])) ++i;
            if (!is_local && n - i >= 7 && font_lower(s[i]) == 'f' && font_ci(s + i, 6, "format") && s[i + 6] == '(') {
                size_t a = i + 7, b = a;
                while (b < n && s[b] != ')') ++b;
                src.format = font_format_name(s + a, b - a);
                i = b < n ? b + 1 : n;
                while (i < n && font_space(s[i])) ++i;
            }
            // tech(...) hints: colour/variations technologies are accepted,
            // anything else makes the source unsupported.
            if (!is_local && n - i >= 5 && font_ci(s + i, 4, "tech") && s[i + 4] == '(') {
                size_t b = i + 5;
                while (b < n && s[b] != ')') ++b;
                i = b < n ? b + 1 : n;
            }
        }
        // Skip to the next comma at depth 0.
        int depth = 0;
        while (i < n && !(depth == 0 && s[i] == ',')) {
            if (s[i] == '(') ++depth;
            else if (s[i] == ')' && depth) --depth;
            ++i;
        }
        if (ok) out[count++] = src;
    }
    return count;
}
inline bool font_hex(char c, unsigned &v) {
    if (c >= '0' && c <= '9') { v = unsigned(c - '0'); return true; }
    c = font_lower(c);
    if (c >= 'a' && c <= 'f') { v = unsigned(c - 'a' + 10); return true; }
    return false;
}
// unicode-range: U+0-7F, U+0400-04FF, U+4??, comma separated.
inline unsigned parse_unicode_ranges(const char *s, size_t n, uint32_t (*out)[2], unsigned cap) {
    unsigned count = 0;
    size_t i = 0;
    while (i < n && count < cap) {
        while (i < n && (font_space(s[i]) || s[i] == ',')) ++i;
        if (i + 2 > n || font_lower(s[i]) != 'u' || s[i + 1] != '+') return 0;
        i += 2;
        uint32_t lo = 0, hi = 0;
        unsigned digits = 0, wild = 0, v;
        while (i < n && digits < 6 && (font_hex(s[i], v) || s[i] == '?')) {
            if (s[i] == '?') { ++wild; lo <<= 4; hi = (hi << 4) | 15; }
            else { if (wild) return 0; lo = (lo << 4) | v; hi = (hi << 4) | v; }
            ++digits; ++i;
        }
        if (!digits) return 0;
        if (!wild && i < n && s[i] == '-') {
            ++i; hi = 0; unsigned d2 = 0;
            while (i < n && d2 < 6 && font_hex(s[i], v)) { hi = (hi << 4) | v; ++d2; ++i; }
            if (!d2) return 0;
        }
        if (lo > hi || hi > 0x10ffff) return 0;
        out[count][0] = lo; out[count][1] = hi; ++count;
        while (i < n && font_space(s[i])) ++i;
        if (i < n && s[i] != ',') return 0;
    }
    return count;
}
inline bool font_number(const char *&s, const char *end, unsigned &v) {
    while (s < end && font_space(*s)) ++s;
    v = 0; unsigned d = 0;
    while (s < end && *s >= '0' && *s <= '9' && d < 5) { v = v * 10 + unsigned(*s++ - '0'); ++d; }
    if (s < end && *s == '.') { ++s; while (s < end && *s >= '0' && *s <= '9') ++s; }
    return d > 0;
}
inline void parse_font_weight(const char *s, size_t n, uint16_t &lo, uint16_t &hi) {
    lo = hi = 400;
    const char *end = s + n, *p = s;
    while (p < end && font_space(*p)) ++p;
    size_t rest = size_t(end - p);
    if (rest >= 4 && font_ci(p, 4, "bold")) { lo = hi = 700; return; }
    unsigned a, b;
    if (!font_number(p, end, a)) return;
    if (!font_number(p, end, b)) b = a;
    if (a < 1) a = 1;
    if (b > 1000) b = 1000;
    if (a > b) { unsigned t = a; a = b; b = t; }
    lo = uint16_t(a); hi = uint16_t(b);
}
inline bool parse_font_italic(const char *s, size_t n) {
    while (n && font_space(*s)) { ++s; --n; }
    return (n >= 6 && font_ci(s, 6, "italic")) || (n >= 7 && font_ci(s, 7, "oblique"));
}
inline uint16_t parse_font_stretch(const char *s, size_t n) {
    struct K { const char *name; uint16_t v; } keys[] = {
        {"ultra-condensed", 50}, {"extra-condensed", 62}, {"semi-condensed", 87}, {"condensed", 75},
        {"normal", 100}, {"semi-expanded", 112}, {"extra-expanded", 150}, {"ultra-expanded", 200},
        {"expanded", 125}};
    while (n && font_space(*s)) { ++s; --n; }
    while (n && font_space(s[n - 1])) --n;
    for (auto &k : keys) if (font_ci(s, n, k.name)) return k.v;
    const char *p = s; unsigned v;
    if (font_number(p, s + n, v) && v >= 50 && v <= 200) return uint16_t(v);
    return 100;
}
inline FontDisplay parse_font_display(const char *s, size_t n) {
    while (n && font_space(*s)) { ++s; --n; }
    while (n && font_space(s[n - 1])) --n;
    if (font_ci(s, n, "block")) return FontDisplay::Block;
    if (font_ci(s, n, "swap")) return FontDisplay::Swap;
    if (font_ci(s, n, "fallback")) return FontDisplay::Fallback;
    if (font_ci(s, n, "optional")) return FontDisplay::Optional;
    return FontDisplay::Auto;
}
// MIME types accepted for font responses. Browsers do not require a font
// MIME type; the signature decides the format. Script/HTML/CSS/image types
// are refused so a mis-served document is never parsed as a font.
inline bool font_mime_allowed(const char *mime) {
    if (!mime || !*mime) return true;
    size_t n = 0;
    while (mime[n] && mime[n] != ';' && n < 95) ++n;
    while (n && font_space(mime[n - 1])) --n;
    auto prefix = [&](const char *p) { size_t k = 0; while (p[k]) { if (k >= n || font_lower(mime[k]) != p[k]) return false; ++k; } return true; };
    if (prefix("font/") || prefix("application/font-") || prefix("application/x-font-")) return true;
    return font_ci(mime, n, "application/vnd.ms-opentype") || font_ci(mime, n, "application/octet-stream") ||
           font_ci(mime, n, "binary/octet-stream");
}
// CORS for fonts (response side). `allow_origin` is the response's
// Access-Control-Allow-Origin header value ("" when absent).
inline bool font_cors_allowed(const char *document_url, const char *font_url, const char *allow_origin) {
    Origin doc = Origin::parse(document_url), font = Origin::parse(font_url);
    if (!font.valid) return false;
    if (doc.valid && doc.same(font)) return true;
    if (!allow_origin) return false;
    size_t n = 0;
    while (allow_origin[n] && n < 255) ++n;
    while (n && font_space(allow_origin[n - 1])) --n;
    const char *a = allow_origin;
    while (n && font_space(*a)) { ++a; --n; }
    if (n == 1 && a[0] == '*') return true;
    if (!doc.valid) return false;
    char buffer[256]{};
    for (size_t i = 0; i < n && i < 255; ++i) buffer[i] = a[i];
    Origin allowed = Origin::parse(buffer);
    return allowed.valid && allowed.same(doc);
}
// Rewrites a CSS font-family list: each family for which `lookup(name, n)`
// returns a replacement gets that replacement inserted before it (scoped
// web-font family or local() family). Tokens starting with \x01 (the BSF
// scoped-name prefix) are removed, so page CSS can never address a scope.
// Returns false when the list needs no change (out is then unspecified).
template <class Lookup>
bool rewrite_font_families(const char *list, char *out, size_t cap, Lookup &&lookup) {
    if (!list || !out || cap < 2) return false;
    size_t used = 0, n = 0;
    while (list[n] && n < cap - 1) ++n;
    bool changed = false;
    for (size_t at = 0; at < n;) {
        size_t b = at;
        while (at < n && list[at] != ',') ++at;
        size_t e = at;
        if (at < n) ++at;
        size_t tb = b, te = e;
        while (tb < te && (font_space(list[tb]) || list[tb] == '"' || list[tb] == '\'')) ++tb;
        while (te > tb && (font_space(list[te - 1]) || list[te - 1] == '"' || list[te - 1] == '\'')) --te;
        if (te > tb && uint8_t(list[tb]) == 1) { changed = true; continue; }
        if (te > tb) {
            const char *use = lookup(list + tb, te - tb);
            if (use) {
                size_t k = 0;
                while (use[k] && k < 256) ++k;
                if (used + k + 2 < cap) {
                    if (used) out[used++] = ',';
                    for (size_t j = 0; j < k; ++j) out[used++] = use[j];
                    changed = true;
                }
            }
        }
        if (used + (e - b) + 2 < cap) {
            if (used) out[used++] = ',';
            for (size_t j = b; j < e; ++j) out[used++] = list[j];
        }
    }
    out[used] = 0;
    return changed;
}
} // namespace Husarz
