/*
 * BRB - arytmetyka z kontrola przepelnienia i bezpieczny odczyt liczb little-endian z niezaufanych buforow.
 *
 * Kazda operacja zwraca false przy przepelnieniu (wynik nie jest wtedy zapisywany). Wbudowane funkcje GCC
 * __builtin_*_overflow kompiluja sie do instrukcji (bez wywolan libgcc). Odczyty le*() skladaja liczbe
 * bajt po bajcie: nie zakladaja wyrownania bufora i nie lamia regul aliasingu (zadnych rzutowan na struktury).
 */
#ifndef BRB_ARYTMETYKA_H
#define BRB_ARYTMETYKA_H

#include <stddef.h>
#include <stdint.h>

namespace brb {

inline bool dodaj_u64(uint64_t a, uint64_t b, uint64_t* wynik) {
    uint64_t w;
    if (__builtin_add_overflow(a, b, &w)) return false;
    *wynik = w;
    return true;
}

inline bool mnoz_u64(uint64_t a, uint64_t b, uint64_t* wynik) {
    uint64_t w;
    if (__builtin_mul_overflow(a, b, &w)) return false;
    *wynik = w;
    return true;
}

inline bool potega_dwojki(uint64_t x) { return x != 0 && (x & (x - 1)) == 0; }

// Zaokraglenie w gore do wielokrotnosci wyrownania (potega dwojki); false przy przepelnieniu.
inline bool wyrownaj_w_gore(uint64_t x, uint64_t wyrownanie, uint64_t* wynik) {
    if (!potega_dwojki(wyrownanie)) return false;
    uint64_t suma;
    if (!dodaj_u64(x, wyrownanie - 1, &suma)) return false;
    *wynik = suma & ~(wyrownanie - 1);
    return true;
}

inline uint64_t wyrownaj_w_dol(uint64_t x, uint64_t wyrownanie) { return x & ~(wyrownanie - 1); }

// Czy [poczatek, poczatek + dlugosc) miesci sie w [0, calosc)? Bez przepelnienia.
inline bool zakres_w_buforze(uint64_t poczatek, uint64_t dlugosc, uint64_t calosc) {
    return poczatek <= calosc && dlugosc <= calosc - poczatek;
}

inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
inline uint32_t le32(const uint8_t* p) { return uint32_t(le16(p)) | (uint32_t(le16(p + 2)) << 16); }
inline uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32); }

}  // namespace brb

#endif
