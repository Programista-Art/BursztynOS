#include "entropia.h"
#include "mbedtls/chacha20.h"

extern "C" bool bursztyn_krypto_wypelnij_losowe(uint8_t*, size_t);

namespace {

static uint32_t g_entropia_lock = 0;
static uint8_t g_chacha_stan[44]{};
static bool g_entropia_gotowa = false;
static uint64_t g_licznik_reseedow = 0;

struct EntropiaLockGuard {
    EntropiaLockGuard() {
        while (__atomic_exchange_n(&g_entropia_lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
            asm volatile("pause" ::: "memory");
        }
    }
    ~EntropiaLockGuard() {
        __atomic_store_n(&g_entropia_lock, 0U, __ATOMIC_RELEASE);
    }
    EntropiaLockGuard(const EntropiaLockGuard&) = delete;
    EntropiaLockGuard& operator=(const EntropiaLockGuard&) = delete;
};

static void wyzeruj_ulotnie(void* p, size_t n) {
    volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        v[i] = 0;
    }
    asm volatile("" : : "r"(p) : "memory");
}

} // namespace

extern "C" bool entropia_czy_gotowa(void) {
    return __atomic_load_n(&g_entropia_gotowa, __ATOMIC_ACQUIRE);
}

extern "C" uint64_t entropia_liczba_reseedow(void) {
    return __atomic_load_n(&g_licznik_reseedow, __ATOMIC_ACQUIRE);
}

extern "C" bool entropia_pobierz_bajty(uint8_t* out, size_t size) {
    if (!out || !size || size > 4096) return false;

    EntropiaLockGuard guard;

    uint8_t seed[44]{};
    uint8_t zero[64]{};
    bool ok = bursztyn_krypto_wypelnij_losowe(seed, sizeof(seed));
    if (!ok) {
        __atomic_store_n(&g_entropia_gotowa, false, __ATOMIC_RELEASE);
        wyzeruj_ulotnie(seed, sizeof(seed));
        wyzeruj_ulotnie(out, size);
        return false;
    }

    const uint64_t nr = g_licznik_reseedow + 1ULL;
    for (size_t i = 0; i < sizeof(seed); ++i) {
        seed[i] ^= g_chacha_stan[i];
    }
    for (unsigned i = 0; i < 8; ++i) {
        seed[32 + i] ^= static_cast<uint8_t>(nr >> (i * 8));
    }

    mbedtls_chacha20_context ctx;
    mbedtls_chacha20_init(&ctx);
    ok = (mbedtls_chacha20_setkey(&ctx, seed) == 0) &&
         (mbedtls_chacha20_starts(&ctx, seed + 32, 0U) == 0);

    // Fast-key-erasure: wygeneruj nowy 44-bajtowy stan lancuchowy przed wyjsciem dla wywolujacego
    if (ok) {
        ok = (mbedtls_chacha20_update(&ctx, sizeof(g_chacha_stan), zero, g_chacha_stan) == 0);
    }

    for (size_t off = 0; ok && off < size;) {
        size_t n = size - off;
        if (n > sizeof(zero)) n = sizeof(zero);
        ok = (mbedtls_chacha20_update(&ctx, n, zero, out + off) == 0);
        off += n;
    }

    mbedtls_chacha20_free(&ctx);
    wyzeruj_ulotnie(seed, sizeof(seed));

    if (!ok) {
        __atomic_store_n(&g_entropia_gotowa, false, __ATOMIC_RELEASE);
        wyzeruj_ulotnie(g_chacha_stan, sizeof(g_chacha_stan));
        wyzeruj_ulotnie(out, size);
        return false;
    }

    __atomic_store_n(&g_licznik_reseedow, nr, __ATOMIC_RELEASE);
    __atomic_store_n(&g_entropia_gotowa, true, __ATOMIC_RELEASE);
    return true;
}
