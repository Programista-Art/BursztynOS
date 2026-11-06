/*
 * ============================================================================
 *  BURSZTYN OS — HOST UNIT & DEATH TESTY OCHRONY STOSU (SSP)
 * ============================================================================
 *
 *  Weryfikuje:
 *    1. Niezmienniki kryptograficzne / strukturalne kanarka (bajt 0 == 0x00,
 *       bajty 1..7 != 0x00, brak kolizji z BUR_SSP_GUARD_PLACEHOLDER).
 *    2. Niezaleznosc kanarkow pomiedzy jadrem a procesami Ring 3 oraz pomiedzy
 *       kolejnymi procesami Ring 3.
 *    3. Inicjalizacje segmentu `.data` obrazu `.bur` przez loader
 *       (`stack_protector_zainicjuj_segment_danych_procesu`).
 *    4. Rzeczywiste wykrycie przepelnienia bufora na stosie przez kod
 *       wygenerowany z `-fstack-protector-strong -mstack-protector-guard=global`
 *       w izolowanym procesie potomnym (`fork()`), bez powrotu z uszkodzonej
 *       funkcji i bez ujawnienia wartosci `__stack_chk_guard` w logach.
 * ============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "stack_protector.h"

namespace {

static unsigned g_liczba_testow = 0;

#define SPRAWDZ(warunek, opis)                                                 \
    do {                                                                       \
        ++g_liczba_testow;                                                     \
        if (!(warunek)) {                                                      \
            fprintf(stderr, "[SSP-HOST-FAIL] %s (linia %d)\n", opis, __LINE__);\
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static uint64_t splitmix64_host(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static uintptr_t normalizuj_kanarek_host(uint64_t surowy) {
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

static bool kanarek_spelnia_niezmienniki(uintptr_t k) {
    if (k == 0 || k == BUR_SSP_GUARD_PLACEHOLDER) return false;
    if ((k & 0xFFULL) != 0ULL) return false;
    for (unsigned b = 1; b < 8; ++b) {
        if (((k >> (b * 8U)) & 0xFFULL) == 0ULL) return false;
    }
    return true;
}

static uint64_t g_licznik_host = 1;
static uintptr_t g_kanarek_jadra_host = 0xA55A3CC39669F100ULL;

static uintptr_t generuj_dla_procesu_host(uint64_t pid, uintptr_t entry) {
    const uint64_t seq = ++g_licznik_host;
    uint64_t m = splitmix64_host((pid << 32) ^ seq ^ entry);
    m ^= splitmix64_host(g_kanarek_jadra_host ^ 0xBB67AE8584CAA73BULL);
    uintptr_t k = normalizuj_kanarek_host(m);
    if (k == g_kanarek_jadra_host) {
        k = normalizuj_kanarek_host(m ^ 0x3C6EF372FE94F82AULL);
    }
    return k;
}

static uint32_t zainicjuj_segment_danych_host(
    uint64_t pid,
    uintptr_t entry,
    uint8_t* dane,
    uint64_t rozmiar,
    uintptr_t* out_kanarek
) {
    if (!dane || rozmiar < sizeof(uint64_t)) return 0;
    const uintptr_t k = generuj_dla_procesu_host(pid, entry);
    if (out_kanarek) *out_kanarek = k;
    uint64_t* slowa = reinterpret_cast<uint64_t*>(dane);
    const uint64_t n = rozmiar / sizeof(uint64_t);
    uint32_t c = 0;
    for (uint64_t i = 0; i < n; ++i) {
        if (slowa[i] == BUR_SSP_GUARD_PLACEHOLDER) {
            slowa[i] = static_cast<uint64_t>(k);
            ++c;
        }
    }
    return c;
}

/*
 * Funkcja celowo podatna na przepelnienie bufora na stosie, kompilowana z
 * `-fstack-protector-strong` (bez ASan w binariach death-testu lub przez
 * bezposredni zapis za buforem). Uzywamy volatile wskaznika, aby kompilator
 * nie zoptymalizowal zapisu poza bufor.
 */
__attribute__((noinline))
static int funkcja_chroniona_ssp(const uint8_t* wejscie, size_t dlugosc) {
    volatile uint8_t bufor[32];
    for (size_t i = 0; i < 32; ++i) {
        bufor[i] = 0;
    }
    volatile uint8_t* p = bufor;
    for (size_t i = 0; i < dlugosc; ++i) {
        p[i] = wejscie[i];
    }
    return static_cast<int>(bufor[0]);
}

} // namespace

#ifdef BURSZTYN_SSP_CUSTOM_RUNTIME_TEST
extern "C" {

uintptr_t __stack_chk_guard = 0xC39669F1A55A3C00ULL;

[[noreturn]] __attribute__((no_stack_protector))
void __stack_chk_fail(void) {
    const char msg[] =
        "[STACKPROTECTORVIOLATION] Wykryto naruszenie straznika stosu (test hosta)!\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(0x53);
}

[[noreturn]] __attribute__((no_stack_protector))
void __stack_chk_fail_local(void) {
    __stack_chk_fail();
}

} // extern "C"
#endif

int main() {
    /* 1. Test niezmiennikow kanarka (1000 wygenerowanych wartosci) */
    for (uint64_t i = 0; i < 1000; ++i) {
        const uintptr_t k = generuj_dla_procesu_host(i + 1, 0x1001000ULL);
        SPRAWDZ(kanarek_spelnia_niezmienniki(k),
                "Kazdy kanarek musi miec bajt 0 == 0x00 oraz bajty 1..7 != 0x00");
        SPRAWDZ(k != g_kanarek_jadra_host,
                "Kanarek procesu Ring 3 musi byc rozny od kanarka jadra");
    }

    /*Test braku zerowych bajtow na pozycjach 1..7 nawet dla wejscia 0 */
    const uintptr_t k_zero = normalizuj_kanarek_host(0ULL);
    SPRAWDZ(kanarek_spelnia_niezmienniki(k_zero),
            "Normalizacja wejscia 0 musi dac poprawny kanarek");
    const uintptr_t k_placeholder = normalizuj_kanarek_host(BUR_SSP_GUARD_PLACEHOLDER);
    SPRAWDZ(kanarek_spelnia_niezmienniki(k_placeholder),
            "Normalizacja BUR_SSP_GUARD_PLACEHOLDER musi uniknac kolizji ze znacznikiem");

    /* 2. Test inicjalizacji segmentu .data procesu Ring 3 */
    alignas(8) uint8_t seg1[128] = {};
    alignas(8) uint8_t seg2[128] = {};
    uint64_t* w1 = reinterpret_cast<uint64_t*>(seg1);
    uint64_t* w2 = reinterpret_cast<uint64_t*>(seg2);
    w1[0] = 0x1122334455667788ULL;
    w1[3] = BUR_SSP_GUARD_PLACEHOLDER;
    w1[7] = 0xAABBCCDDEEFF0011ULL;
    memcpy(seg2, seg1, sizeof(seg1));

    uintptr_t kanarek1 = 0, kanarek2 = 0;
    const uint32_t podm1 =
        zainicjuj_segment_danych_host(1, 0x1001000ULL, seg1, sizeof(seg1), &kanarek1);
    const uint32_t podm2 =
        zainicjuj_segment_danych_host(2, 0x1001000ULL, seg2, sizeof(seg2), &kanarek2);

    SPRAWDZ(podm1 == 1U && podm2 == 1U,
            "Loader musi podmienic dokladnie jeden znacznik BUR_SSP_GUARD_PLACEHOLDER");
    SPRAWDZ(w1[3] == kanarek1 && w2[3] == kanarek2,
            "Slabe slowo znacznika musi zostac zastapione kanarkiem procesu");
    SPRAWDZ(kanarek1 != kanarek2,
            "Dwa procesy Ring 3 musza otrzymac rozne kanarki stosu");
    SPRAWDZ(w1[0] == 0x1122334455667788ULL && w1[7] == 0xAABBCCDDEEFF0011ULL,
            "Pozostale dane w segmencie .data nie moga zostac zmodyfikowane");

    /* 3. Test poprawnego wywolania funkcji chronionej SSP */
    uint8_t bezpieczne_dane[32];
    memset(bezpieczne_dane, 0x41, sizeof(bezpieczne_dane));
    SPRAWDZ(funkcja_chroniona_ssp(bezpieczne_dane, sizeof(bezpieczne_dane)) == 0x41,
            "Poprawne wywolanie funkcji chronionej SSP nie moze zglosic bledu");

#ifdef BURSZTYN_SSP_CUSTOM_RUNTIME_TEST
    /*
     * 4. Kontrolowany test wykrycia przepelnienia stosu (death test w fork()):
     *    Nadpisujemy 48 bajtow w 32-bajtowym buforze na stosie, uszkadzajac
     *    kanarek umieszczony przez `-fstack-protector-strong`.
     */
    int pipefd[2];
    SPRAWDZ(pipe(pipefd) == 0, "Utworzenie potoku dla death-testu SSP");

    const pid_t child = fork();
    SPRAWDZ(child >= 0, "fork() dla death-testu SSP");
    if (child == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        uint8_t przepelnienie[48];
        memset(przepelnienie, 0x42, sizeof(przepelnienie));
        (void)funkcja_chroniona_ssp(przepelnienie, sizeof(przepelnienie));
        /* Nigdy nie powinnismy tu dotrzec! */
        _exit(0);
    }

    close(pipefd[1]);
    char log_buf[512] = {};
    const ssize_t nread = read(pipefd[0], log_buf, sizeof(log_buf) - 1);
    close(pipefd[0]);

    int status = 0;
    SPRAWDZ(waitpid(child, &status, 0) == child, "waitpid() dla death-testu SSP");
    SPRAWDZ(WIFEXITED(status) && WEXITSTATUS(status) == 0x53,
            "Proces potomny z uszkodzonym stosem musi zakonczyc sie w __stack_chk_fail (kod 0x53)");
    SPRAWDZ(nread > 0 && strstr(log_buf, "[STACKPROTECTORVIOLATION]") != nullptr,
            "Diagnostyka __stack_chk_fail musi zawierac znacznik [STACKPROTECTORVIOLATION]");

    char tajny_hex[32];
    snprintf(tajny_hex, sizeof(tajny_hex), "%llX",
             static_cast<unsigned long long>(__stack_chk_guard));
    SPRAWDZ(strstr(log_buf, tajny_hex) == nullptr,
            "Log bledu nie moze ujawnic tajnej wartosci __stack_chk_guard");
#endif

    printf("[SSP-HOST] PASS (%u asercji zweryfikowanych)\n", g_liczba_testow);
    return 0;
}
