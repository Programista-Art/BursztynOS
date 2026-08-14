#!/bin/bash

echo "=== Narzedzie Naprawcze mbedTLS dla Bursztyn OS ==="

# Upewniamy się, że główny folder biblioteki istnieje
mkdir -p biblioteki/mbedtls

echo "[1/6] Usuwanie niekompatybilnej wersji mbedTLS 3.x..."
rm -rf biblioteki/mbedtls/library
rm -rf biblioteki/mbedtls/include

echo "[2/6] Pobieranie stabilnej wersji mbedTLS 2.28.8 LTS..."
wget -q --show-progress https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.8.zip

echo "[3/6] Rozpakowywanie archiwum..."
unzip -q v2.28.8.zip

echo "[4/6] Kopiowanie wlasciwych folderow library i include..."
cp -r mbedtls-2.28.8/library biblioteki/mbedtls/
cp -r mbedtls-2.28.8/include biblioteki/mbedtls/

echo "[5/6] Automatyczne generowanie plikow konfiguracyjnych i portu dla Bursztyn OS..."

# Automatyczne tworzenie pliku konfiguracyjnego
cat << 'EOF' > biblioteki/mbedtls/bursztyn_mbedtls_config.h
/*
 * Minimalna konfiguracja mbedTLS dla Bursztyn OS (Bare-Metal).
 */
#pragma once
#include <stddef.h>

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS

#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE

#undef MBEDTLS_PSA_CRYPTO_C
#undef MBEDTLS_USE_PSA_CRYPTO
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C

// KRYTYCZNA POPRAWKA: Wyłączenie sprzętowej akceleracji
#undef MBEDTLS_AESNI_C
#undef MBEDTLS_PADLOCK_C

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_DEFAULT_ENTROPY_SOURCES

#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_RSA_C
#define MBEDTLS_AES_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_PK_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_X509_CRT_PARSE_C

// POPRAWKA BŁĘDÓW "implicit declaration"
#ifdef __cplusplus
extern "C" {
#endif
int snprintf_dummy(char * s, size_t n, const char * format, ...);
int printf_dummy(const char * format, ...);
void exit_dummy(int status);
#ifdef __cplusplus
}
#endif

#define MBEDTLS_PLATFORM_SNPRINTF_MACRO  snprintf_dummy
#define MBEDTLS_PLATFORM_PRINTF_MACRO    printf_dummy
#define MBEDTLS_PLATFORM_EXIT_MACRO      exit_dummy
EOF

# Automatyczne tworzenie portu integracyjnego (C++)
cat << 'EOF' > biblioteki/mbedtls/mbedtls_port.cpp
/*
 * Port integracyjny mbedTLS -> Bursztyn OS
 */
#include <stdint.h>
#include <stddef.h>

extern "C" {
    void mbedtls_memory_buffer_alloc_init(unsigned char *buf, size_t len);
    int snprintf_dummy(char * s, size_t n, const char * format, ...) { (void)s; (void)n; (void)format; return 0; }
    int printf_dummy(const char * format, ...) { (void)format; return 0; }

    void *memset(void *s, int c, size_t n) {
        unsigned char *p = (unsigned char *)s;
        while (n--) *p++ = (unsigned char)c;
        return s;
    }

    void *memcpy(void *dest, const void *src, size_t n) {
        unsigned char *d = (unsigned char *)dest;
        const unsigned char *s = (const unsigned char *)src;
        while (n--) *d++ = *s++;
        return dest;
    }

    void *memmove(void *dest, const void *src, size_t n) {
        unsigned char *d = (unsigned char *)dest;
        const unsigned char *s = (const unsigned char *)src;
        if (d == s) return d;
        if (d < s) {
            while (n--) *d++ = *s++;
        } else {
            d += n;
            s += n;
            while (n--) *(--d) = *(--s);
        }
        return dest;
    }

    int memcmp(const void *s1, const void *s2, size_t n) {
        const unsigned char *p1 = (const unsigned char *)s1;
        const unsigned char *p2 = (const unsigned char *)s2;
        while (n--) {
            if (*p1 != *p2) return *p1 - *p2;
            p1++; p2++;
        }
        return 0;
    }

    size_t strlen(const char *s) {
        size_t len = 0;
        while (s[len]) len++;
        return len;
    }

    char *strncpy(char *dest, const char *src, size_t n) {
        size_t i;
        for (i = 0; i < n && src[i] != '\0'; i++) {
            dest[i] = src[i];
        }
        for ( ; i < n; i++) {
            dest[i] = '\0';
        }
        return dest;
    }

    void *__memcpy_chk(void *dest, const void *src, size_t len, size_t destlen) {
        (void)destlen;
        return memcpy(dest, src, len);
    }

    void *__memset_chk(void *dest, int c, size_t len, size_t destlen) {
        (void)destlen;
        return memset(dest, c, len);
    }

    int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t os, const char *format, __builtin_va_list ap) {
        (void)s; (void)maxlen; (void)flag; (void)os; (void)format; (void)ap;
        return 0;
    }

    int strcmp(const char *s1, const char *s2) {
        while (*s1 && (*s1 == *s2)) { s1++; s2++; }
        return *(const unsigned char*)s1 - *(const unsigned char*)s2;
    }

    int strncmp(const char *s1, const char *s2, size_t n) {
        while (n && *s1 && (*s1 == *s2)) { ++s1; ++s2; --n; }
        if (n == 0) return 0;
        return *(const unsigned char*)s1 - *(const unsigned char*)s2;
    }

    char *strchr(const char *s, int c) {
        while (*s != (char)c) {
            if (!*s++) return NULL;
        }
        return (char *)s;
    }

    char *strstr(const char *haystack, const char *needle) {
        size_t n = 0;
        while (needle[n]) n++; 
        if (n == 0) return (char *)haystack;
        while (*haystack) {
            size_t i = 0;
            while (haystack[i] == needle[i]) {
                i++;
                if (i == n) return (char *)haystack;
            }
            haystack++;
        }
        return NULL;
    }

    void exit_dummy(int status) {
        (void)status;
        while(1) { asm volatile("cli; hlt"); }
    }

    // BEZPIECZNY GENERATOR: Omijamy instrukcję 'RDRAND', ponieważ wirtualizacja VirtualBox'a może ją blokować i rzucać BSOD!
    int rand(void) {
        static uint32_t ziarno = 0x12345678;
        ziarno = (1103515245 * ziarno + 12345) % 2147483648;
        return (int)ziarno;
    }
}

// Ograniczenie wielkości bufora do 64KB, aby oszczędzać pamięć Jądra!
unsigned char mbedtls_heap[64 * 1024] __attribute__((aligned(16)));

extern "C" void inicjalizuj_mbedtls() {
    mbedtls_memory_buffer_alloc_init(mbedtls_heap, sizeof(mbedtls_heap));
}

extern "C" int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    for (size_t i = 0; i < len; i++) {
        output[i] = (unsigned char)(rand() & 0xFF);
    }
    *olen = len;
    return 0;
}
EOF

echo "[6/6] Sprzatanie resztek..."
rm -rf mbedtls-2.28.8 v2.28.8.zip

echo "==================================================="
echo "Gotowe! Wersja 2.28 LTS zintegrowana i skonfigurowana."
echo "Wpisz teraz w terminalu: make clear && make run"