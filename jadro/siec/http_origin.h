#pragma once
/*
 * BSF 2.2 / Husarz: naglowek zadania "Origin" (RFC 6454) dla zasobow
 * pobieranych w trybie CORS (@font-face). Jadro przyjmuje wylacznie
 * zserializowany origin: "null" albo scheme "://" host [":" port], gdzie
 * scheme to http/https, host to male litery ASCII, cyfry, '.', '-' (forma
 * po normalizacji URL; IDN jako punycode), port 1..65535 bez zer wiodacych.
 * Nic innego - zwlaszcza CR/LF, spacje i sciezki - nie trafia do zadania.
 */
#include <stddef.h>

constexpr size_t HTTP_ORIGIN_MAX = 255;

inline bool http_origin_poprawny(const char* o) {
    if (!o) return false;
    size_t n = 0;
    while (n <= HTTP_ORIGIN_MAX && o[n]) ++n;
    if (n == 0 || n > HTTP_ORIGIN_MAX) return false;
    if (n == 4 && o[0] == 'n' && o[1] == 'u' && o[2] == 'l' && o[3] == 'l') return true;
    size_t p = 0;
    if (n > 7 && o[0] == 'h' && o[1] == 't' && o[2] == 't' && o[3] == 'p' && o[4] == ':') p = 5;
    else if (n > 8 && o[0] == 'h' && o[1] == 't' && o[2] == 't' && o[3] == 'p' && o[4] == 's' && o[5] == ':') p = 6;
    else return false;
    if (o[p] != '/' || o[p + 1] != '/') return false;
    p += 2;
    size_t host = p;
    while (p < n && o[p] != ':') {
        char c = o[p];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
        ++p;
    }
    if (p == host || o[host] == '.' || o[host] == '-' || o[p - 1] == '.' || o[p - 1] == '-') return false;
    if (p == n) return true;
    ++p; // ':'
    if (p == n || o[p] == '0' || n - p > 5) return false;
    unsigned port = 0;
    for (; p < n; ++p) {
        if (o[p] < '0' || o[p] > '9') return false;
        port = port * 10u + unsigned(o[p] - '0');
    }
    return port >= 1 && port <= 65535;
}
