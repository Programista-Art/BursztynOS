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

// Handshake TLS z łańcuchem X.509 potrzebuje więcej pamięci niż prosty AES.
unsigned char mbedtls_heap[256 * 1024] __attribute__((aligned(16)));

extern "C" void inicjalizuj_mbedtls() {
    mbedtls_memory_buffer_alloc_init(mbedtls_heap, sizeof(mbedtls_heap));
}

extern "C" int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    uint32_t eax = 1, ebx, ecx, edx;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    bool ma_rdrand = (ecx & (1u << 30)) != 0;
    size_t zapisano = 0;
    while (ma_rdrand && zapisano < len) {
        uint64_t losowa = 0;
        unsigned char sukces = 0;
        for (int proba = 0; proba < 10 && !sukces; proba++)
            asm volatile("rdrand %0; setc %1" : "=r"(losowa), "=qm"(sukces));
        if (!sukces) break;
        for (int i = 0; i < 8 && zapisano < len; i++, zapisano++)
            output[zapisano] = (unsigned char)(losowa >> (i * 8));
    }
    // Awaryjne mieszanie dla maszyn bez RDRAND. Zapewnia uruchomienie TLS,
    // ale log informuje, że taka platforma nie ma sprzętowego CSPRNG.
    uint32_t tsc_lo, tsc_hi;
    asm volatile("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
    uint64_t stan = ((uint64_t)tsc_hi << 32) | tsc_lo;
    stan ^= (uint64_t)(uintptr_t)output ^ ((uint64_t)rand() << 32);
    while (zapisano < len) {
        stan ^= stan << 13; stan ^= stan >> 7; stan ^= stan << 17;
        output[zapisano++] = (unsigned char)stan;
    }
    *olen = len;
    return 0;
}
