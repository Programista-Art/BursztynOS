// Opcjonalny hostowy test i386: bez libc, zwraca status przez Linux int 0x80.
#include "../tapetybtp.cpp"
static_assert(sizeof(size_t) == 4, "Test wymaga 32-bitowego size_t");

static void ustaw32(uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}

extern "C" [[noreturn]] void _start() {
    uint8_t plik[19] = {'B','T','P','1',1,0,0,0,1,0,0,0,32,1,1,0,0,0,255};
    int wynik = 0;
    for (uint8_t tryb = 0; tryb < 2; ++tryb) {
        uint32_t piksel = 0x12345678;
        plik[13] = tryb;
        plik[14] = tryb;
        plik[17] = tryb == 0 ? 255 : 0;
        const size_t rozmiar = tryb == 0 ? 18 : 19;
        ustaw32(plik + 4, 65536); ustaw32(plik + 8, 65536);
        if (btpwczytajirysuj(plik, rozmiar, &piksel, 1, 1, 1, 4) != BTPBLADPRZEPELNIENIA) wynik = 1;
        ustaw32(plik + 4, 65535);
        if (btpwczytajirysuj(plik, rozmiar, &piksel, 1, 1, 1, 4) != BTPBLADPRZEPELNIENIA) wynik = 2;
        ustaw32(plik + 4, 1); ustaw32(plik + 8, 1);
        if (btpwczytajirysuj(plik, rozmiar, &piksel, 1, 2, 0x80000000U, SIZE_MAX) != BTPBLADPRZEPELNIENIA) wynik = 3;
        if (btpwczytajirysuj(plik, rozmiar, &piksel, 1, 1, 0x40000000U, SIZE_MAX) != BTPBLADPRZEPELNIENIA) wynik = 4;
        if (piksel != 0x12345678) wynik = 5;
        if (btpwczytajirysuj(plik, rozmiar, &piksel, 1, 1, 1, 4) != BTPOK || piksel != 0xff000000U) wynik = 6;
    }
    asm volatile("int $0x80" : : "a"(1), "b"(wynik) : "memory");
    __builtin_unreachable();
}
