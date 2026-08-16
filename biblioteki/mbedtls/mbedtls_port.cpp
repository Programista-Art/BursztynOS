/*
 * Bursztyn OS - port mbedTLS 2.28.x
 *
 * Nie ma tutaj rand(), LCG ani stalego seeda.
 * Brak kryptograficznego zrodla losowosci powoduje fail-closed.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#include "mbedtls/entropy.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/platform.h"

namespace {

constexpr size_t MBEDTLS_HEAP_SIZE =
    256U * 1024U;

alignas(64)
unsigned char mbedtls_heap[
    MBEDTLS_HEAP_SIZE
];

bool mbedtls_port_gotowy =
    false;

}

/*
 * Docelowy hook kernela.
 *
 * Implementacja MUSI byc CSPRNG/bezpiecznym zrodlem entropii.
 * Nie uzywaj stalego seeda, LCG, samego RDTSC ani samego RTC.
 */
extern "C" bool bursztyn_krypto_wypelnij_losowe(
    uint8_t* bufor,
    size_t dlugosc
) __attribute__((weak));

extern "C" int mbedtls_hardware_poll(
    void* data,
    unsigned char* output,
    size_t len,
    size_t* olen
) {
    (void)data;

    if (!olen) {
        return
            MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    *olen =
        0;

    if (len == 0) {
        return 0;
    }

    if (!output ||
        !bursztyn_krypto_wypelnij_losowe) {

        return
            MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    if (!bursztyn_krypto_wypelnij_losowe(
            output,
            len)) {

        return
            MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    *olen =
        len;

    return 0;
}

/* -------------------------------------------------------------------------
 * Minimalne funkcje libc wymagane przez kod mbedTLS
 * ------------------------------------------------------------------------- */

extern "C" void* memset(
    void* dest,
    int value,
    size_t n
) {
    if (!dest) {
        return dest;
    }

    unsigned char* p =
        static_cast<unsigned char*>(
            dest
        );

    const unsigned char v =
        static_cast<unsigned char>(
            value
        );

    while (n-- != 0) {
        *p++ =
            v;
    }

    return dest;
}

extern "C" void* memcpy(
    void* dest,
    const void* src,
    size_t n
) {
    if (!dest ||
        !src) {

        return dest;
    }

    unsigned char* d =
        static_cast<unsigned char*>(
            dest
        );

    const unsigned char* s =
        static_cast<const unsigned char*>(
            src
        );

    while (n-- != 0) {
        *d++ =
            *s++;
    }

    return dest;
}

extern "C" void* memmove(
    void* dest,
    const void* src,
    size_t n
) {
    if (!dest ||
        !src ||
        dest == src ||
        n == 0) {

        return dest;
    }

    unsigned char* d =
        static_cast<unsigned char*>(
            dest
        );

    const unsigned char* s =
        static_cast<const unsigned char*>(
            src
        );

    if (d < s) {
        while (n-- != 0) {
            *d++ =
                *s++;
        }
    } else {
        d +=
            n;

        s +=
            n;

        while (n-- != 0) {
            *--d =
                *--s;
        }
    }

    return dest;
}

extern "C" int memcmp(
    const void* lhs,
    const void* rhs,
    size_t n
) {
    if (n == 0) {
        return 0;
    }

    if (!lhs) {
        return rhs ? -1 : 0;
    }

    if (!rhs) {
        return 1;
    }

    const unsigned char* a =
        static_cast<const unsigned char*>(
            lhs
        );

    const unsigned char* b =
        static_cast<const unsigned char*>(
            rhs
        );

    for (size_t i = 0;
         i < n;
         ++i) {

        if (a[i] !=
            b[i]) {

            return
                a[i] <
                        b[i]
                    ? -1
                    : 1;
        }
    }

    return 0;
}

extern "C" size_t strlen(
    const char* s
) {
    if (!s) {
        return 0;
    }

    size_t n =
        0;

    while (s[n] != '\0') {
        ++n;
    }

    return n;
}

extern "C" int strcmp(
    const char* lhs,
    const char* rhs
) {
    if (!lhs) {
        return rhs ? -1 : 0;
    }

    if (!rhs) {
        return 1;
    }

    while (*lhs != '\0' &&
           *lhs == *rhs) {

        ++lhs;
        ++rhs;
    }

    const unsigned char a =
        static_cast<unsigned char>(
            *lhs
        );

    const unsigned char b =
        static_cast<unsigned char>(
            *rhs
        );

    return
        a == b
            ? 0
            : (a < b ? -1 : 1);
}

extern "C" int strncmp(
    const char* lhs,
    const char* rhs,
    size_t n
) {
    if (n == 0) {
        return 0;
    }

    if (!lhs) {
        return rhs ? -1 : 0;
    }

    if (!rhs) {
        return 1;
    }

    for (size_t i = 0;
         i < n;
         ++i) {

        const unsigned char a =
            static_cast<unsigned char>(
                lhs[i]
            );

        const unsigned char b =
            static_cast<unsigned char>(
                rhs[i]
            );

        if (a != b) {
            return
                a < b
                    ? -1
                    : 1;
        }

        if (a == 0) {
            return 0;
        }
    }

    return 0;
}

extern "C" char* strncpy(
    char* dest,
    const char* src,
    size_t n
) {
    if (!dest) {
        return dest;
    }

    size_t i =
        0;

    if (src) {
        while (i < n &&
               src[i] != '\0') {

            dest[i] =
                src[i];

            ++i;
        }
    }

    while (i < n) {
        dest[i++] =
            '\0';
    }

    return dest;
}

extern "C" char* strchr(
    const char* s,
    int c
) {
    if (!s) {
        return nullptr;
    }

    const char needle =
        static_cast<char>(
            c
        );

    for (;;) {
        if (*s ==
            needle) {

            return
                const_cast<char*>(
                    s
                );
        }

        if (*s ==
            '\0') {

            return nullptr;
        }

        ++s;
    }
}

extern "C" char* strstr(
    const char* haystack,
    const char* needle
) {
    if (!haystack ||
        !needle) {

        return nullptr;
    }

    if (*needle ==
        '\0') {

        return
            const_cast<char*>(
                haystack
            );
    }

    for (const char* h =
             haystack;
         *h != '\0';
         ++h) {

        const char* a =
            h;

        const char* b =
            needle;

        while (*a != '\0' &&
               *b != '\0' &&
               *a == *b) {

            ++a;
            ++b;
        }

        if (*b ==
            '\0') {

            return
                const_cast<char*>(
                    h
                );
        }
    }

    return nullptr;
}

extern "C" void* __memcpy_chk(
    void* dest,
    const void* src,
    size_t len,
    size_t destlen
) {
    if (len >
        destlen) {

        return nullptr;
    }

    return
        memcpy(
            dest,
            src,
            len
        );
}

extern "C" void* __memset_chk(
    void* dest,
    int value,
    size_t len,
    size_t destlen
) {
    if (len >
        destlen) {

        return nullptr;
    }

    return
        memset(
            dest,
            value,
            len
        );
}

/* -------------------------------------------------------------------------
 * Platform printf/snprintf.
 *
 * To jest tylko diagnostyka. Funkcja zachowuje NUL i nie interpretuje
 * format stringa, wiec nie jest powierzchnia format-string dla kernela.
 * ------------------------------------------------------------------------- */

static int bursztyn_vsnprintf(
    char* out,
    size_t out_size,
    const char* format,
    va_list args
) {
    (void)args;

    if (!format) {
        if (out &&
            out_size > 0) {

            out[0] =
                '\0';
        }

        return -1;
    }

    size_t len =
        0;

    while (format[len] != '\0') {
        ++len;
    }

    if (out &&
        out_size > 0) {

        size_t copy =
            len;

        if (copy >=
            out_size) {

            copy =
                out_size -
                1U;
        }

        for (size_t i = 0;
             i < copy;
             ++i) {

            out[i] =
                format[i];
        }

        out[copy] =
            '\0';
    }

    if (len >
        0x7FFFFFFFU) {

        return -1;
    }

    return
        static_cast<int>(
            len
        );
}

static int bursztyn_snprintf(
    char* out,
    size_t out_size,
    const char* format,
    ...
) {
    va_list args;

    va_start(
        args,
        format
    );

    const int ret =
        bursztyn_vsnprintf(
            out,
            out_size,
            format,
            args
        );

    va_end(
        args
    );

    return ret;
}

static int bursztyn_printf(
    const char* format,
    ...
) {
    (void)format;
    return 0;
}

[[noreturn]]
static void bursztyn_exit(
    int status
) {
    (void)status;

    asm volatile(
        "cli"
        :
        :
        : "memory"
    );

    for (;;) {
        asm volatile(
            "hlt"
            :
            :
            : "memory"
        );
    }
}

/* -------------------------------------------------------------------------
 * Inicjalizacja
 * ------------------------------------------------------------------------- */

extern "C" void inicjalizuj_mbedtls() {
    if (__atomic_load_n(
            &mbedtls_port_gotowy,
            __ATOMIC_ACQUIRE)) {

        return;
    }

    mbedtls_memory_buffer_alloc_init(
        mbedtls_heap,
        sizeof(
            mbedtls_heap
        )
    );

    (void)mbedtls_platform_set_printf(
        bursztyn_printf
    );

    (void)mbedtls_platform_set_snprintf(
        bursztyn_snprintf
    );

    (void)mbedtls_platform_set_vsnprintf(
        bursztyn_vsnprintf
    );

    (void)mbedtls_platform_set_exit(
        bursztyn_exit
    );

    __atomic_store_n(
        &mbedtls_port_gotowy,
        true,
        __ATOMIC_RELEASE
    );
}
