#pragma once
#include <stddef.h>
#include <stdint.h>
// Shared browser/kernel wire validation. Certificate hostname verification is
// independent and remains mandatory in TLS.
namespace HttpTarget {
constexpr size_t MaxHost = 253, MaxTarget = 2047;
inline bool hostname(const char *s) {
    if (!s)
        return false;
    size_t n = 0, start = 0;
    for (; n <= MaxHost && s[n]; ++n) {
        unsigned char c = s[n];
        if (c == '.') {
            if (n == start || n - start > 63 || s[start] == '-' || s[n - 1] == '-')
                return false;
            start = n + 1;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return n && n <= MaxHost && n > start && n - start <= 63 && s[start] != '-' && s[n - 1] != '-';
}
inline const char *target_error(const char *s) {
    if (!s || !*s)
        return "EMPTY_TARGET";
    if (*s != '/')
        return "MISSING_LEADING_SLASH";
    for (size_t i = 0; i <= MaxTarget; ++i) {
        unsigned c = uint8_t(s[i]);
        if (!c)
            return nullptr;
        if (i == MaxTarget)
            return "TARGET_TOO_LONG";
        if (c <= 32 || c >= 127)
            return "INVALID_TARGET_BYTE";
        if (c == '#')
            return "FRAGMENT_IN_TARGET";
        if (c == '\\')
            return "BACKSLASH_IN_TARGET";
    }
    return "TARGET_TOO_LONG";
}
} // namespace HttpTarget
