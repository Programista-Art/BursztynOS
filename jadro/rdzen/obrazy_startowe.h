#pragma once
// Desktop sample images (test.bmp, test1.bmp) shipped as Multiboot2 modules
// instead of being linked into the kernel image (15 MiB image limit).
//
// The kernel never decodes the pixels; it only checks that a module is a
// self-consistent, bounded, uncompressed or standard BMP before copying the
// bytes into PSF, exactly like the previous embedded-blob path. This header
// is freestanding so the same validator runs in host tests.
#include <stdint.h>

namespace obrazy_startowe {

// Exact module labels (GRUB `module2 <path> <label>`); compared exactly.
constexpr const char* EtykietaTest = "pulpit/test.bmp";
constexpr const char* EtykietaTest1 = "pulpit/test1.bmp";
constexpr const char* SciezkaTest = "/uzytkownicy/Pulpit/test.bmp";
constexpr const char* SciezkaTest1 = "/uzytkownicy/Pulpit/test1.bmp";

constexpr uint64_t MinRozmiar = 54;               // file + BITMAPINFOHEADER
constexpr uint64_t MaksRozmiar = 8ULL * 1024 * 1024; // per sample image
constexpr uint64_t MaksAdres = 0x100000000ULL;      // MB2 module fields are u32

enum class Wynik : uint8_t {
    Ok,
    ZlyZakres,      // null/inverted/out-of-range module addresses
    ZlyRozmiar,     // below header size or above MaksRozmiar
    BrakSygnatury,  // not "BM"
    NiezgodnyRozmiar, // bfSize != module size
    ZlyNaglowek,    // offsets, DIB header, planes, bpp or dimensions
    ZaMaloDanych,   // pixel array does not fit inside the file
};

inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
inline uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0] | (p[1] << 8));
}

// `start`/`koniec` are the module's physical bounds as reported by GRUB;
// `dane` points at the (identity-mapped) bytes and is only dereferenced
// after the range and minimum size are proven.
inline Wynik sprawdz_bmp(const uint8_t* dane, uint64_t start, uint64_t koniec) {
    if (!dane || start == 0 || koniec <= start || koniec > MaksAdres)
        return Wynik::ZlyZakres;
    const uint64_t rozmiar = koniec - start;
    if (rozmiar < MinRozmiar || rozmiar > MaksRozmiar)
        return Wynik::ZlyRozmiar;
    if (dane[0] != 'B' || dane[1] != 'M')
        return Wynik::BrakSygnatury;
    if (le32(dane + 2) != rozmiar)
        return Wynik::NiezgodnyRozmiar;
    const uint32_t offset = le32(dane + 10);
    const uint32_t dib = le32(dane + 14);
    if (dib < 40 || dib > rozmiar - 14 || offset < 14 + uint64_t(dib) ||
        offset >= rozmiar)
        return Wynik::ZlyNaglowek;
    const int32_t szer = int32_t(le32(dane + 18));
    const int32_t wys = int32_t(le32(dane + 22));
    const uint16_t plany = le16(dane + 26);
    const uint16_t bpp = le16(dane + 28);
    const uint32_t kompresja = le32(dane + 30);
    if (szer <= 0 || wys == 0 || wys == INT32_MIN || plany != 1)
        return Wynik::ZlyNaglowek;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return Wynik::ZlyNaglowek;
    // BI_RGB (0) and BI_BITFIELDS (3) store a plain pixel array whose size
    // is fully determined by the header; check it fits (64-bit, no overflow:
    // width < 2^31, bpp <= 32 -> row bits < 2^37).
    if (kompresja == 0 || kompresja == 3) {
        const uint64_t wiersz = ((uint64_t(szer) * bpp + 31) / 32) * 4;
        const uint64_t wiersze = wys < 0 ? uint64_t(-int64_t(wys)) : uint64_t(wys);
        if (wiersze > (rozmiar - offset) / wiersz)
            return Wynik::ZaMaloDanych;
    } else if (kompresja != 1 && kompresja != 2) {
        return Wynik::ZlyNaglowek; // RLE8/RLE4 are bounded by the file size
    }
    return Wynik::Ok;
}

inline const char* opis(Wynik w) {
    switch (w) {
    case Wynik::Ok: return "OK";
    case Wynik::ZlyZakres: return "nieprawidlowy zakres modulu";
    case Wynik::ZlyRozmiar: return "nieprawidlowy rozmiar";
    case Wynik::BrakSygnatury: return "brak sygnatury BM";
    case Wynik::NiezgodnyRozmiar: return "rozmiar BMP niezgodny z modulem";
    case Wynik::ZlyNaglowek: return "nieprawidlowy naglowek BMP";
    case Wynik::ZaMaloDanych: return "dane pikseli poza plikiem";
    }
    return "?";
}

} // namespace obrazy_startowe
