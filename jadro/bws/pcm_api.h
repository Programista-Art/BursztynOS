#pragma once
#include <stdint.h>
#include <stddef.h>

#define BWS_PCM 110
#define BWS_PCM_V1 1
#define BWS_PCM_MAX_FRAMES 512
enum BwsPcmOperation { PCM_OPEN = 1, PCM_WRITE, PCM_QUERY, PCM_GAIN, PCM_CLOSE, PCM_FINISH };
enum BwsPcmError {
    PCM_OK = 0, PCM_INVALID = -1, PCM_UNSUPPORTED = -2, PCM_LIMIT = -3,
    PCM_HANDLE = -4, PCM_FULL = -5, PCM_BUSY = -6, PCM_DEVICE = -7, PCM_MEMORY = -8
};
// reserved: zadana pojemnosc kolejki strumienia w ramkach. 0 = domyslna (4096 ramek = 85 ms). Wartosc z zakresu
// [BWS_PCM_DEFAULT_FRAMES, BWS_PCM_DEEP_FRAMES] prosi o gleboka kolejke (512 ms) - przydzielana, gdy wolny jest slot
// puli jadra; inaczej strumien dostaje pojemnosc domyslna. Faktyczna pojemnosc zwraca PCM_QUERY (capacity).
// Kazda inna niezerowa wartosc = PCM_INVALID (zachowanie sprzed rozszerzenia dla starszych wartosci, np. 1).
struct BwsPcmFormat {
    uint32_t version, size, rate, channels, encoding, reserved;
};
#define BWS_PCM_DEFAULT_FRAMES 4096
#define BWS_PCM_DEEP_FRAMES 24576
// Pauza bez porzucania kolejki (rozszerzenie addytywne; starsze jadro zwraca PCM_INVALID). PAUSE: strumien nie
// jest miksowany ani oczekiwany (brak niedoboru), probki zostaja. RESUME: odtwarzanie od tej samej ramki.
#define PCM_PAUSE 8
#define PCM_RESUME 9
inline bool bws_pcm_pojemnosc_poprawna(uint32_t r) {
    return r == 0 || (r >= BWS_PCM_DEFAULT_FRAMES && r <= BWS_PCM_DEEP_FRAMES);
}
struct BwsPcmState {
    uint32_t version, size, queued, capacity, volume, muted, channels, rate;
};
#ifdef __cplusplus
static_assert(sizeof(BwsPcmFormat) == 24 && offsetof(BwsPcmFormat, rate) == 8);
static_assert(sizeof(BwsPcmState) == 32 && offsetof(BwsPcmState, queued) == 8);
#endif

// Owner-scoped cumulative counters since OPEN. QUERY_STATS is additive to V1.
// Callers may pass either BWS_PCM_STATS_V1_SIZE (104) or sizeof(BwsPcmStats) (144).
#define PCM_QUERY_STATS 7
#define BWS_PCM_STATS_V1_SIZE 104
struct BwsPcmStats {
    uint64_t writes, requested, accepted, partial, full;
    uint64_t starved, muted_missing, last_write_ns, max_write_gap_ns;
    uint64_t min_queued, max_queued, consumed, max_copy_ns;
    uint64_t rejected, queued, max_runnable_wait_ns, max_voluntary_wait_ns, max_exec_gap_ns;
};
#ifdef __cplusplus
static_assert(offsetof(BwsPcmStats, rejected) == BWS_PCM_STATS_V1_SIZE);
static_assert(sizeof(BwsPcmStats) == 144);
#endif
