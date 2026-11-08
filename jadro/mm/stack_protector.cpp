#include "stack_protector.h"
#include "stos_jadra.h"
#include "scheduler.h"
#include "wyjatek_uzytkownika.h"
#include "pamiec.h"

/*
 * Ten plik jest kompilowany z `-fno-stack-protector`, poniewaz zawiera:
 *   1. `stack_protector_early_init()` — inicjalizacje `__stack_chk_guard`
 *      przed wejsciem do `kernel_main()`.
 *   2. `__stack_chk_fail()` — procedure paniki po wykryciu uszkodzenia stosu,
 *      ktora nie moze rekurencyjnie sprawdzac straznika.
 */

extern "C" void wypisz_na_ekranie(const char* tekst);

namespace {

static bool g_entropia_sprzetowa_ok = false;
static volatile uint32_t g_w_trakcie_paniki_ssp = 0;
static volatile uint64_t g_licznik_procesow_ssp = 1;

static inline void port_outb_ssp(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t port_inb_ssp(uint16_t port) {
    uint8_t ret = 0;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_putc_ssp(char c) {
    for (uint32_t i = 0; i < 100000U; ++i) {
        if ((port_inb_ssp(0x3F8 + 5) & 0x20U) != 0) {
            break;
        }
    }
    port_outb_ssp(0x3F8, static_cast<uint8_t>(c));
}

static void serial_puts_ssp(const char* s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') {
            serial_putc_ssp('\r');
        }
        serial_putc_ssp(*s++);
    }
}

static void formatuj_hex64_ssp(char out[19], uint64_t v) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        const uint8_t nib = static_cast<uint8_t>((v >> ((15 - i) * 4)) & 0xFULL);
        out[2 + i] = hex[nib];
    }
    out[18] = '\0';
}

static void formatuj_u64_ssp(char out[24], uint64_t v) {
    char tmp[24];
    size_t n = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0 && n < 22) {
        tmp[n++] = static_cast<char>('0' + (v % 10ULL));
        v /= 10ULL;
    }
    for (size_t i = 0; i < n; ++i) {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
}

static inline uint64_t rdtsc_ssp() {
    uint32_t lo = 0;
    uint32_t hi = 0;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

static inline uint64_t splitmix64_ssp(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static bool cpuid_ma_rdrand() {
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    asm volatile("cpuid"
                 : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (ecx & (1U << 30)) != 0;
}

static bool cpuid_ma_rdseed() {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    asm volatile("cpuid"
                 : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 7U) return false;
    eax = 7U;
    ecx = 0U;
    asm volatile("cpuid"
                 : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (ebx & (1U << 18)) != 0;
}

static bool probuj_rdseed64(uint64_t* out) {
    if (!cpuid_ma_rdseed()) return false;
    for (int i = 0; i < 16; ++i) {
        uint64_t val = 0;
        uint8_t ok = 0;
        asm volatile("rdseed %0; setc %1" : "=r"(val), "=qm"(ok) : : "cc");
        if (ok && val != 0 && val != ~0ULL) {
            *out = val;
            return true;
        }
    }
    return false;
}

static bool probuj_rdrand64(uint64_t* out) {
    if (!cpuid_ma_rdrand()) return false;
    for (int i = 0; i < 16; ++i) {
        uint64_t val = 0;
        uint8_t ok = 0;
        asm volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok) : : "cc");
        if (ok && val != 0 && val != ~0ULL) {
            *out = val;
            return true;
        }
    }
    return false;
}

/*
 * Normalizuje 64-bitowy kanarek:
 *   - bajt 0 == 0x00 (zatrzymuje przepelnienia C-stringow przed nadpisaniem RBP/RIP),
 *   - bajty 1..7 sa rozne od 0x00,
 *   - wartosc nigdy nie jest rowna znacznikowi BUR_SSP_GUARD_PLACEHOLDER.
 */
static uintptr_t normalizuj_kanarek(uint64_t surowy) {
    uint64_t g = surowy & ~0xFFULL;
    const uint8_t zastepcze[8] = {0x00, 0x5A, 0xA5, 0x3C, 0xC3, 0x96, 0x69, 0xF1};
    for (unsigned b = 1; b < 8; ++b) {
        const uint64_t maska = 0xFFULL << (b * 8U);
        if ((g & maska) == 0) {
            g |= static_cast<uint64_t>(zastepcze[b]) << (b * 8U);
        }
    }
    if (g == BUR_SSP_GUARD_PLACEHOLDER) {
        g ^= 0x123456789ABCDEF0ULL & ~0xFFULL;
    }
    return static_cast<uintptr_t>(g);
}

} // namespace

extern "C" {

/*
 * Bezpieczna wartosc poczatkowa w obrazie ELF na wypadek uzycia przed
 * `stack_protector_early_init()` (bajt 0 = 0x00, pozostale niezerowe).
 */
uintptr_t __stack_chk_guard = 0xA55A3CC39669F100ULL;

__attribute__((no_stack_protector))
void stack_protector_early_init(void) {
    uint64_t hw1 = 0;
    uint64_t hw2 = 0;
    const bool ok_seed = probuj_rdseed64(&hw1);
    const bool ok_rand = probuj_rdrand64(&hw2);
    g_entropia_sprzetowa_ok = (ok_seed || ok_rand);

    uint64_t rsp_teraz = 0;
    asm volatile("mov %%rsp, %0" : "=r"(rsp_teraz));

    const uint64_t tsc1 = rdtsc_ssp();
    uint64_t stan = 0x6A09E667F3BCC908ULL;
    stan ^= splitmix64_ssp(hw1 ^ tsc1);
    stan ^= splitmix64_ssp(hw2 ^ rsp_teraz ^ reinterpret_cast<uintptr_t>(&__stack_chk_guard));
    stan ^= splitmix64_ssp(rdtsc_ssp() ^ (stan >> 17));

    __stack_chk_guard = normalizuj_kanarek(stan);
}

__attribute__((no_stack_protector))
bool stack_protector_czy_entropia_sprzetowa(void) {
    return g_entropia_sprzetowa_ok;
}

__attribute__((no_stack_protector))
uintptr_t stack_protector_generuj_dla_procesu(
    uint64_t pid,
    uintptr_t punkt_wejscia
) {
    uint64_t hw = 0;
    if (!probuj_rdrand64(&hw)) {
        (void)probuj_rdseed64(&hw);
    }
    const uint64_t sekwencja =
        __atomic_add_fetch(&g_licznik_procesow_ssp, 1ULL, __ATOMIC_RELAXED);
    const uint64_t tsc = rdtsc_ssp();

    uint64_t mieszanka = splitmix64_ssp(hw ^ tsc);
    mieszanka ^= splitmix64_ssp((pid << 32) ^ sekwencja ^ punkt_wejscia);
    mieszanka ^= splitmix64_ssp(__stack_chk_guard ^ 0xBB67AE8584CAA73BULL);

    uintptr_t kanarek = normalizuj_kanarek(mieszanka);
    if (kanarek == __stack_chk_guard) {
        kanarek = normalizuj_kanarek(mieszanka ^ 0x3C6EF372FE94F82AULL);
    }
    return kanarek;
}

__attribute__((no_stack_protector))
uint64_t stack_protector_losowy_offset_stron(
    uint64_t maks_stron,
    uint64_t sol
) {
    if (maks_stron <= 1) {
        return 0;
    }

    uint64_t hw = 0;
    if (!probuj_rdrand64(&hw)) {
        (void)probuj_rdseed64(&hw);
    }

    const uint64_t sekwencja =
        __atomic_add_fetch(&g_licznik_procesow_ssp, 1ULL, __ATOMIC_RELAXED);
    const uint64_t tsc = rdtsc_ssp();

    uint64_t mieszanka = splitmix64_ssp(hw ^ tsc ^ sol);
    mieszanka ^= splitmix64_ssp(sekwencja ^ (sol << 17));
    mieszanka ^= splitmix64_ssp(__stack_chk_guard ^ 0x94D049BB133111EBULL);

    /*
     * Bez modulo (bias) i bez reszty z dzielenia: mnozenie przez potege
     * dwojki zakresu daje rownomierny rozklad przy dowolnym maks_stron.
     */
    const uint64_t losowe =
        splitmix64_ssp(mieszanka);

    return maks_stron > 0
        ? (losowe % maks_stron)
        : 0;
}

__attribute__((no_stack_protector))
uint32_t stack_protector_zainicjuj_segment_danych_procesu(
    uint64_t pid,
    uintptr_t punkt_wejscia,
    uint8_t* dane_wirtualny,
    uint64_t rozmiar_danych
) {
    if (!dane_wirtualny || rozmiar_danych < sizeof(uint64_t)) {
        return 0;
    }

    const uintptr_t kanarek_procesu =
        stack_protector_generuj_dla_procesu(pid, punkt_wejscia);
    uint64_t* slowa = reinterpret_cast<uint64_t*>(dane_wirtualny);
    const uint64_t liczba_slow = rozmiar_danych / sizeof(uint64_t);
    uint32_t podmieniono = 0;

    /*
     * SMAP: dane_wirtualny to adres Ring 3 w przestrzeni nowego procesu.
     * Loader przelaczyl CR3, wiec strony sa widoczne, ale bez flagi AC
     * zapis wywola #PF w Ring 0. Okno obejmuje wylacznie petle podmiany.
     */
    OknoUzytkownika okno(dane_wirtualny, rozmiar_danych);

    for (uint64_t i = 0; i < liczba_slow; ++i) {
        if (slowa[i] == BUR_SSP_GUARD_PLACEHOLDER) {
            slowa[i] = static_cast<uint64_t>(kanarek_procesu);
            ++podmieniono;
        }
    }
    return podmieniono;
}

[[noreturn]] __attribute__((no_stack_protector))
void __stack_chk_fail(void) {
    asm volatile("cli" ::: "memory");

    /*
     * Blokada atomowa zapobiega jakiejkolwiek rekurencji, gdyby funkcja
     * wypisujaca log sama ulegla awarii.
     */
    if (!__sync_bool_compare_and_swap(&g_w_trakcie_paniki_ssp, 0U, 1U)) {
        for (;;) {
            asm volatile("cli; hlt" ::: "memory");
        }
    }

    uint64_t rsp_teraz = 0;
    asm volatile("mov %%rsp, %0" : "=r"(rsp_teraz));
    const uintptr_t caller_rip =
        reinterpret_cast<uintptr_t>(__builtin_return_address(0));

    const uint64_t pid =
        (aktualny_pid >= 0) ? static_cast<uint64_t>(aktualny_pid) : 0ULL;

    char pid_str[24];
    char rip_str[19];
    char rsp_str[19];
    formatuj_u64_ssp(pid_str, pid);
    formatuj_hex64_ssp(rip_str, static_cast<uint64_t>(caller_rip));
    formatuj_hex64_ssp(rsp_str, rsp_teraz);

    const StosRodzaj rodzaj_stosu =
        stos_klasyfikuj_rsp(rsp_teraz, nullptr, nullptr, nullptr);

    serial_puts_ssp("\n============================================================\n");
    serial_puts_ssp("[STACKPROTECTORVIOLATION] PANIKA JADRA (Ring 0): wykryto uszkodzenie stosu!\n");
    serial_puts_ssp("  pid=");
    serial_puts_ssp(pid_str);
    serial_puts_ssp(" caller_rip=");
    serial_puts_ssp(rip_str);
    serial_puts_ssp(" rsp=");
    serial_puts_ssp(rsp_str);
    serial_puts_ssp(" stack_type=");
    serial_puts_ssp(stos_nazwa_rodzaju(rodzaj_stosu));
    serial_puts_ssp("\n============================================================\n");

    wypisz_na_ekranie(
        "\n[STACKPROTECTORVIOLATION] PANIKA JADRA (Ring 0): wykryto uszkodzenie stosu!\n");

    for (;;) {
        asm volatile("cli; hlt" ::: "memory");
    }
}

[[noreturn]] __attribute__((no_stack_protector))
void __stack_chk_fail_local(void) {
    __stack_chk_fail();
}

} // extern "C"
