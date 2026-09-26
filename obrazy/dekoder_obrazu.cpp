#include "dekoder_obrazu.h"
#include "../narzedzia/tapety-btp/tapetybtp.h"

namespace {
AlokujObraz g_alokuj = nullptr;
ZwolnijObraz g_zwolnij = nullptr;
void* obraz_alloc(size_t n) { return g_alokuj ? g_alokuj(n) : nullptr; }
void obraz_free(void* p) { if (p && g_zwolnij) g_zwolnij(p); }
void* obraz_realloc(void* p, size_t old_n, size_t new_n) {
    if (!g_alokuj || !g_zwolnij) return nullptr;
    void* q = g_alokuj(new_n);
    if (!q) return nullptr;
    if (p) {
        uint8_t* dst = static_cast<uint8_t*>(q);
        const uint8_t* src = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < old_n && i < new_n; ++i) dst[i] = src[i];
        g_zwolnij(p);
    }
    return q;
}
}

#define STBI_ONLY_JPEG
#define STBI_STRICT_JPEG_ENTROPY
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS OBRAZ_MAX_BOK
#define STBI_MALLOC(sz) obraz_alloc(sz)
#define STBI_FREE(p) obraz_free(p)
#define STBI_REALLOC_SIZED(p,oldsz,newsz) obraz_realloc(p,oldsz,newsz)
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

namespace {
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
bool limit(uint32_t w, uint32_t h) {
    return w && h && w <= OBRAZ_MAX_BOK && h <= OBRAZ_MAX_BOK &&
           static_cast<uint64_t>(w) * h <= OBRAZ_MAX_PIKSELE;
}
}

FormatObrazu rozpoznaj_obraz(const uint8_t* p, size_t n) {
    if (!p || n < 2) return FormatObrazu::Nieznany;
    if (p[0] == 0xff && p[1] == 0xd8) return FormatObrazu::JPEG;
    if (n >= 4 && p[0] == 'B' && p[1] == 'T' && p[2] == 'P' && (p[3] == '1' || p[3] == '2'))
        return FormatObrazu::BTP;
    if (p[0] == 'B' && p[1] == 'M') return FormatObrazu::BMP;
    if (n >= 8 && p[0] == 137 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G')
        return FormatObrazu::PNG;
    return FormatObrazu::Nieznany;
}

BladObrazu sprawdz_jpeg(const uint8_t* p, size_t n, uint32_t* ow, uint32_t* oh) {
    if (ow) *ow = 0;
    if (oh) *oh = 0;
    if (!p || n < 4 || p[0] != 0xff || p[1] != 0xd8 || n > OBRAZ_MAX_PLIK)
        return BladObrazu::Format;
    uint8_t quant = 0, dc = 0, ac = 0, comp_ids[3]{}, qids[3]{};
    uint32_t w = 0, h = 0;
    bool sof = false, scan = false, progressive = false;
    uint8_t scanned_mask = 0;
    uint64_t coeff_seen[3]{};
    uint16_t restart = 0;
    size_t i = 2;
    while (i < n) {
        if (p[i++] != 0xff) return BladObrazu::Uszkodzony;
        while (i < n && p[i] == 0xff) ++i;
        if (i >= n) return BladObrazu::Uszkodzony;
        const uint8_t marker = p[i++];
        if (marker == 0xd9) {
            if (!sof || !scan || i != n ||
                (!progressive && scanned_mask != (comp_ids[1] ? 7U : 1U)))
                return BladObrazu::Uszkodzony;
            if (progressive) for (unsigned j = 0; j < (comp_ids[1] ? 3U : 1U); ++j)
                if (coeff_seen[j] != UINT64_MAX) return BladObrazu::Uszkodzony;
            if (ow) *ow = w;
            if (oh) *oh = h;
            return BladObrazu::OK;
        }
        if (marker == 0xd8 || marker == 0x00 || (marker >= 0xd0 && marker <= 0xd7))
            return BladObrazu::Uszkodzony;
        if (i + 2 > n) return BladObrazu::Uszkodzony;
        const uint16_t len = be16(p + i);
        if (len < 2 || static_cast<size_t>(len) > n - i) return BladObrazu::Uszkodzony;
        const size_t end = i + len;
        i += 2;
        if (marker == 0xdb) {
            while (i < end) {
                const uint8_t q = p[i++];
                const size_t bytes = (q >> 4) ? 128 : 64;
                if ((q >> 4) != 0 || (q & 15) > 3 || (quant & (1U << (q & 15))) || bytes > end - i)
                    return BladObrazu::Uszkodzony;
                for (size_t k = 0; k < 64; ++k) if (!p[i + k]) return BladObrazu::Uszkodzony;
                quant |= static_cast<uint8_t>(1U << (q & 15));
                i += bytes;
            }
        } else if (marker == 0xc4) {
            while (i < end) {
                const uint8_t t = p[i++];
                if (i + 16 > end || (t >> 4) > 1 || (t & 15) > 3) return BladObrazu::Uszkodzony;
                uint8_t& mask = (t >> 4) ? ac : dc;
                const uint8_t bit = static_cast<uint8_t>(1U << (t & 15));
                if (mask & bit) return BladObrazu::Uszkodzony;
                mask |= bit;
                unsigned count = 0, available = 1;
                for (unsigned k = 0; k < 16; ++k) {
                    available = available * 2;
                    if (p[i + k] > available) return BladObrazu::Uszkodzony;
                    available -= p[i + k];
                    count += p[i + k];
                }
                i += 16;
                if (!count || count > 256 || count > end - i) return BladObrazu::Uszkodzony;
                for (unsigned k = 0; k < count; ++k) {
                    uint8_t sym = p[i + k];
                    if (t >> 4) {
                        if ((sym & 15) > 10)
                            return BladObrazu::Uszkodzony;
                    } else if (sym > 11) return BladObrazu::Uszkodzony;
                }
                i += count;
            }
        } else if (marker == 0xc0 || marker == 0xc2) {
            if (sof || end - i < 6 || p[i] != 8) return BladObrazu::Uszkodzony;
            progressive = marker == 0xc2;
            h = be16(p + i + 1); w = be16(p + i + 3);
            const uint8_t comps = p[i + 5];
            if (!limit(w, h)) return BladObrazu::ZaDuzy;
            if ((comps != 1 && comps != 3) || end - i != static_cast<size_t>(6 + comps * 3))
                return BladObrazu::Nieobslugiwany;
            for (unsigned k = 0; k < comps; ++k) {
                const uint8_t id = p[i + 6 + k * 3];
                const uint8_t hv = p[i + 7 + k * 3];
                const uint8_t q = p[i + 8 + k * 3];
                if (!id || q > 3) return BladObrazu::Uszkodzony;
                for (unsigned j = 0; j < k; ++j) if (id == comp_ids[j]) return BladObrazu::Uszkodzony;
                if ((k == 0 && hv != 0x11 && hv != 0x21 && hv != 0x22) ||
                    (k != 0 && hv != 0x11) || (comps == 1 && hv != 0x11))
                    return BladObrazu::Nieobslugiwany;
                comp_ids[k] = id; qids[k] = q;
            }
            sof = true;
        } else if (marker == 0xdd) {
            if (end - i != 2) return BladObrazu::Uszkodzony;
            restart = be16(p + i);
        } else if (marker == 0xda) {
            if (!sof || end - i < 4) return BladObrazu::Uszkodzony;
            const unsigned ns = p[i++];
            if (!ns || ns > (comp_ids[1] ? 3U : 1U) ||
                end - i != ns * 2 + 3) return BladObrazu::Uszkodzony;
            const uint8_t ss = p[i + ns * 2], se = p[i + ns * 2 + 1];
            const uint8_t ah_al = p[i + ns * 2 + 2];
            uint8_t scan_components[3]{};
            uint8_t scan_component_mask = 0;
            for (unsigned k = 0; k < ns; ++k) {
                const uint8_t id = p[i++], sel = p[i++];
                bool found = false;
                for (unsigned j = 0; j < (comp_ids[1] ? 3U : 1U); ++j)
                    if (comp_ids[j] == id) {
                        found = true;
                        scan_components[k] = static_cast<uint8_t>(j);
                        if (scan_component_mask & (1U << j)) return BladObrazu::Uszkodzony;
                        scan_component_mask |= static_cast<uint8_t>(1U << j);
                        if (!(quant & (1U << qids[j])) ||
                            (!progressive && (scanned_mask & (1U << j))))
                            return BladObrazu::Uszkodzony;
                        if (!progressive) scanned_mask |= static_cast<uint8_t>(1U << j);
                    }
                if (!found || (sel >> 4) > 3 || (sel & 15) > 3) return BladObrazu::Uszkodzony;
                if ((!progressive || (ss == 0 && (ah_al >> 4) == 0)) &&
                    !(dc & (1U << (sel >> 4)))) return BladObrazu::Uszkodzony;
                if ((!progressive || ss != 0) && !(ac & (1U << (sel & 15))))
                    return BladObrazu::Uszkodzony;
            }
            i += 3;
            if ((!progressive && (ss != 0 || se != 63 || ah_al)) ||
                (progressive && (ss > se || se > 63 || (ss == 0 && se != 0) ||
                                 (ss != 0 && ns != 1) || (ah_al >> 4) > 13 || (ah_al & 15) > 13 ||
                                 ((ah_al >> 4) != 0 && (ah_al >> 4) != (ah_al & 15) + 1))))
                return BladObrazu::Nieobslugiwany;
            if (progressive) {
                uint64_t band = 0;
                for (unsigned bit = ss; bit <= se; ++bit) band |= UINT64_C(1) << bit;
                for (unsigned k = 0; k < ns; ++k) {
                    uint64_t& seen = coeff_seen[scan_components[k]];
                    if ((ah_al >> 4) == 0) {
                        if (seen & band) return BladObrazu::Uszkodzony;
                        seen |= band;
                    } else if ((seen & band) != band) return BladObrazu::Uszkodzony;
                }
            }
            scan = true;
            unsigned next_rst = 0;
            while (i < n) {
                if (p[i++] != 0xff) continue;
                if (i >= n) return BladObrazu::Uszkodzony;
                const uint8_t code = p[i++];
                if (code == 0x00) continue;
                if (code >= 0xd0 && code <= 0xd7) {
                    if (!restart || code != 0xd0 + next_rst) return BladObrazu::Uszkodzony;
                    next_rst = (next_rst + 1) & 7;
                    continue;
                }
                if (code == 0xff) { --i; continue; }
                i -= 2;
                break;
            }
            if (i >= n) return BladObrazu::Uszkodzony;
        } else if (marker == 0xc1 || (marker >= 0xc3 && marker <= 0xcf)) {
            return BladObrazu::Nieobslugiwany;
        }
        i = end > i ? end : i;
    }
    return BladObrazu::Uszkodzony;
}

BladObrazu dekoduj_obraz(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                         AlokujObraz alokuj, ZwolnijObraz zwolnij) {
    if (!out || !alokuj || !zwolnij) return BladObrazu::Argument;
    *out = {};
    if (!dane || !n || n > OBRAZ_MAX_PLIK) return BladObrazu::ZaDuzy;
    const FormatObrazu f = rozpoznaj_obraz(dane, n);
    uint32_t w = 0, h = 0;
    if (f == FormatObrazu::JPEG) {
        const BladObrazu valid = sprawdz_jpeg(dane, n, &w, &h);
        if (valid != BladObrazu::OK) return valid;
        if (n > 0x7fffffffU) return BladObrazu::ZaDuzy;
        g_alokuj = alokuj; g_zwolnij = zwolnij;
        int x = 0, y = 0, comps = 0;
        stbi_uc* rgba = stbi_load_from_memory(dane, static_cast<int>(n), &x, &y, &comps, 4);
        g_alokuj = nullptr; g_zwolnij = nullptr;
        if (!rgba || x != static_cast<int>(w) || y != static_cast<int>(h) ||
            (comps != 1 && comps != 3)) {
            if (rgba) zwolnij(rgba);
            return BladObrazu::Uszkodzony;
        }
        const size_t count = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < count; ++i) {
            uint8_t r = rgba[4*i], g = rgba[4*i+1], b = rgba[4*i+2];
            rgba[4*i] = b; rgba[4*i+1] = g; rgba[4*i+2] = r; rgba[4*i+3] = 255;
        }
        *out = {w, h, w * 4, OBRAZ_BGRA8888, rgba, count * 4};
        return BladObrazu::OK;
    }
    if (f == FormatObrazu::BTP) {
        BtpInfo info{};
        if (btpnaglowek(dane, n, &info) != BTPOK || !limit(info.szerokosc, info.wysokosc))
            return BladObrazu::Uszkodzony;
        w = info.szerokosc; h = info.wysokosc;
        const size_t bytes = static_cast<size_t>(w) * h * 4;
        auto* pixels = static_cast<uint32_t*>(alokuj(bytes));
        if (!pixels) return BladObrazu::Pamiec;
        for (size_t i = 0; i < bytes / 4; ++i) pixels[i] = 0xff000000U;
        if (btpwczytajirysuj(dane, n, pixels, w, h, w, bytes) != BTPOK) {
            zwolnij(pixels);
            return BladObrazu::Uszkodzony;
        }
        *out = {w, h, w * 4, OBRAZ_BGRA8888, reinterpret_cast<uint8_t*>(pixels), bytes};
        return BladObrazu::OK;
    }
    return f == FormatObrazu::Nieznany ? BladObrazu::Format : BladObrazu::Nieobslugiwany;
}

BladObrazu dekoduj_jpeg(const uint8_t* dane, size_t n, DekodowanyObraz* out,
                        AlokujObraz alokuj, ZwolnijObraz zwolnij) {
    if (!out || !alokuj || !zwolnij) return BladObrazu::Argument;
    *out = {};
    if (!dane || !n || n > OBRAZ_MAX_PLIK) return BladObrazu::ZaDuzy;
    if (rozpoznaj_obraz(dane, n) != FormatObrazu::JPEG) return BladObrazu::Format;
    return dekoduj_obraz(dane, n, out, alokuj, zwolnij);
}

const char* blad_obrazu_komunikat(BladObrazu b) {
    switch (b) {
        case BladObrazu::OK: return "Obraz poprawny.";
        case BladObrazu::Argument: return "Niepoprawne argumenty dekodera obrazu.";
        case BladObrazu::Format: return "Nie można odczytać pliku JPEG.";
        case BladObrazu::Nieobslugiwany: return "JPEG progresywny nie jest jeszcze obsługiwany.";
        case BladObrazu::Uszkodzony: return "Uszkodzony plik JPEG.";
        case BladObrazu::ZaDuzy: return "Obraz ma zbyt duże wymiary.";
        case BladObrazu::Pamiec: return "Brak pamięci do dekodowania obrazu.";
        default: return "Błąd dekodowania obrazu.";
    }
}
