#pragma once
#include <stdint.h>
#include "../../jadro/bws/pcm_api.h"

// Bounded Ring 3 producer refill policy shared between production apps and host tests.
// Maintains target occupancy near full queue capacity (4096 frames = 85.333 ms at 48 kHz)
// while avoiding needless small writes when the queue is already sufficiently filled
// (< REFILL_MIN_FREE free frames). Always refreshes QUERY after completing a batch or
// receiving a partial write so concurrent mixer consumption / DMA priming cannot leave
// the queue drained. Preserves exact phase across partial writes, FULL, and BUSY.
namespace PcmDemo {
constexpr uint32_t WRITE_BUDGET = 16;
constexpr uint32_t REFILL_CHUNK = BWS_PCM_MAX_FRAMES;
constexpr uint32_t REFILL_MIN_FREE = REFILL_CHUNK;
constexpr uint32_t MAX_BUSY_RETRIES = 2;

inline uint32_t refill_frames(uint32_t queued, uint32_t capacity) {
    if (!capacity || queued >= capacity) return 0;
    const uint32_t free_frames = capacity - queued;
    const uint32_t threshold = capacity < REFILL_MIN_FREE ? 1U : REFILL_MIN_FREE;
    return free_frames >= threshold ? free_frames : 0;
}

inline int16_t triangle_sample(uint64_t phase) {
    const int a = static_cast<int>(phase * 4);
    const int triangle = a < 48000 ? a : a < 144000 ? 96000 - a : a - 192000;
    return static_cast<int16_t>(triangle / 6);
}

struct RefillResult {
    uint32_t queries = 0;
    uint32_t writes = 0;
    uint32_t frames_accepted = 0;
    uint32_t partials = 0;
    uint32_t fulls = 0;
    uint32_t busies = 0;
    bool hit_budget = false;
    bool needs_retry = false;
};

template <class QueryFn, class WriteFn>
inline RefillResult refill_stream(uint64_t& phase, uint32_t hz, QueryFn&& query_fn, WriteFn&& write_fn) {
    RefillResult res{};
    uint32_t remaining = 0;
    uint32_t busy_retries = 0;
    int16_t samples[BWS_PCM_MAX_FRAMES * 2];

    for (uint32_t batch = 0; batch < WRITE_BUDGET; ++batch) {
        if (!remaining) {
            BwsPcmState queue{};
            ++res.queries;
            const int64_t qrc = query_fn(queue);
            if (qrc == PCM_BUSY) {
                ++res.busies;
                res.needs_retry = true;
                if (++busy_retries > MAX_BUSY_RETRIES) return res;
                continue;
            }
            if (qrc != PCM_OK) {
                res.needs_retry = true;
                return res;
            }
            remaining = refill_frames(queue.queued, queue.capacity);
            if (!remaining) return res;
        }
        const uint32_t requested = remaining < BWS_PCM_MAX_FRAMES ? remaining : BWS_PCM_MAX_FRAMES;
        uint64_t next = phase;
        for (uint32_t i = 0; i < requested; ++i) {
            const int16_t s = triangle_sample(next);
            samples[2 * i] = samples[2 * i + 1] = s;
            next = (next + hz) % 48000;
        }
        const int64_t accepted = write_fn(samples, requested);
        if (accepted == PCM_BUSY) {
            ++res.busies;
            res.needs_retry = true;
            remaining = 0;
            if (++busy_retries > MAX_BUSY_RETRIES) return res;
            continue;
        }
        if (accepted == PCM_FULL) {
            ++res.fulls;
            return res;
        }
        if (accepted <= 0) {
            res.needs_retry = true;
            return res;
        }
        busy_retries = 0;
        ++res.writes;
        res.frames_accepted += static_cast<uint32_t>(accepted);
        phase = (phase + static_cast<uint64_t>(accepted) * hz) % 48000;
        if (static_cast<uint32_t>(accepted) < requested) {
            ++res.partials;
            remaining = 0;
            continue;
        }
        remaining -= static_cast<uint32_t>(accepted);
    }
    res.hit_budget = true;
    return res;
}
} // namespace PcmDemo
