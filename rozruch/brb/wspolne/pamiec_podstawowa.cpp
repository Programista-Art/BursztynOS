/*
 * BRB - podstawowe operacje na pamieci.
 *
 * Bezpieczenstwo przed "rekurencja przez optymalizator": GCC potrafi rozpoznac petle kopiujaca/zerujaca
 * i zamienic ja na wywolanie memcpy/memset (-ftree-loop-distribute-patterns), co w implementacji samego
 * memcpy dawaloby nieskonczona rekurencje. Dlatego:
 *   - caly kod BRB jest kompilowany z -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns,
 *   - funkcje maja dodatkowo atrybut optimize("no-tree-loop-distribute-patterns") (obrona w glebi),
 *   - Makefile (cel brb-uefi) sprawdza w deasemblacji, ze zadna z tych funkcji nie wywoluje innej funkcji.
 * Implementacja bajtowa: Faza 2 kopiuje wylacznie male bufory; wydajnosc nie jest celem.
 */
#include "pamiec_podstawowa.h"

#include <stdint.h>

#define BRB_BEZ_ZAMIANY_PETLI __attribute__((optimize("no-tree-loop-distribute-patterns")))

extern "C" {

BRB_BEZ_ZAMIANY_PETLI void* brb_memcpy(void* cel, const void* zrodlo, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(cel);
    const unsigned char* s = static_cast<const unsigned char*>(zrodlo);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return cel;
}

BRB_BEZ_ZAMIANY_PETLI void* brb_memmove(void* cel, const void* zrodlo, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(cel);
    const unsigned char* s = static_cast<const unsigned char*>(zrodlo);
    if (n == 0 || d == s) return cel;
    // Porownanie adresow jako liczb (uintptr_t): poprawne dla dowolnych buforow w plaskiej przestrzeni x86-64.
    if (reinterpret_cast<uintptr_t>(d) < reinterpret_cast<uintptr_t>(s)) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];          // cel przed zrodlem: od poczatku
    } else {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];  // cel za zrodlem: od konca
    }
    return cel;
}

BRB_BEZ_ZAMIANY_PETLI void* brb_memset(void* cel, int wartosc, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(cel);
    const unsigned char b = static_cast<unsigned char>(wartosc);
    for (size_t i = 0; i < n; ++i) d[i] = b;
    return cel;
}

BRB_BEZ_ZAMIANY_PETLI int brb_memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = static_cast<const unsigned char*>(a);
    const unsigned char* y = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

#ifndef BRB_TEST_HOSTA
// Symbole wymagane przez kod generowany przez kompilator w obrazie UEFI.
BRB_BEZ_ZAMIANY_PETLI void* memcpy(void* cel, const void* zrodlo, size_t n) { return brb_memcpy(cel, zrodlo, n); }
BRB_BEZ_ZAMIANY_PETLI void* memmove(void* cel, const void* zrodlo, size_t n) { return brb_memmove(cel, zrodlo, n); }
BRB_BEZ_ZAMIANY_PETLI void* memset(void* cel, int wartosc, size_t n) { return brb_memset(cel, wartosc, n); }
BRB_BEZ_ZAMIANY_PETLI int memcmp(const void* a, const void* b, size_t n) { return brb_memcmp(a, b, n); }
#endif

}  // extern "C"
