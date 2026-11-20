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
struct BwsPcmFormat {
    uint32_t version, size, rate, channels, encoding, reserved;
};
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
