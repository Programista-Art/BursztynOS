#include "entropia.h"
#include "mbedtls/chacha20.h"
extern "C" bool bursztyn_krypto_wypelnij_losowe(uint8_t*, size_t);

extern "C" bool entropia_pobierz_bajty(uint8_t* out, size_t size) {
    if (!out || !size || size > 4096) return false;
    uint8_t seed[44]{};
    uint8_t zero[64]{};
    bool ok = bursztyn_krypto_wypelnij_losowe(seed, sizeof(seed));
    mbedtls_chacha20_context ctx;
    mbedtls_chacha20_init(&ctx);
    if (ok) ok = mbedtls_chacha20_setkey(&ctx, seed) == 0 &&
                 mbedtls_chacha20_starts(&ctx, seed + 32, 0) == 0;
    for (size_t off = 0; ok && off < size;) {
        size_t n = size - off;
        if (n > sizeof(zero)) n = sizeof(zero);
        ok = mbedtls_chacha20_update(&ctx, n, zero, out + off) == 0;
        off += n;
    }
    mbedtls_chacha20_free(&ctx);
    for (size_t i = 0; i < sizeof(seed); ++i)
        static_cast<volatile uint8_t*>(seed)[i] = 0;
    if (!ok) for (size_t i = 0; i < size; ++i) out[i] = 0;
    return ok;
}
