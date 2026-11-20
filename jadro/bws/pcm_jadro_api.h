#pragma once
#include "jadro/dzwiek/pcm_aplikacji.h"

// Copy callbacks validate and copy user memory outside GuardHDA. No pointer
// supplied by a process reaches the asynchronous transport.
inline int64_t bws_pcm_obsluz(Pcm::Owner owner, uint64_t operation, uint64_t a,
                              uint64_t b, uint64_t c,
                              Pcm::CopyFromUser copy_in,
                              bool (*copy_out)(void*, const void*, size_t)) {
    using namespace PcmAplikacji;
    if (!owner.pid || !owner.generation) return PCM_INVALID;
    if (operation == PCM_OPEN) {
        if (b != sizeof(BwsPcmFormat) || c != BWS_PCM_V1) return PCM_INVALID;
        BwsPcmFormat format{};
        if (!copy_in(&format, reinterpret_cast<const void*>(a), sizeof(format))) return PCM_MEMORY;
        return open(owner, format);
    }
    if (operation == PCM_WRITE) {
        if (!c || c > BWS_PCM_MAX_FRAMES) return PCM_INVALID;
        BwsPcmState state{};
        const auto rc = query(owner, a, state);
        if (rc != PCM_OK) return rc;
        const uint64_t bytes = c * state.channels * sizeof(int16_t);
        if (!b || b > UINT64_MAX - bytes) return PCM_MEMORY;
        int16_t samples[BWS_PCM_MAX_FRAMES * 2];
        if (!copy_in(samples, reinterpret_cast<const void*>(b), bytes)) return PCM_MEMORY;
        return write(owner, a, samples, static_cast<uint32_t>(c));
    }
    if (operation == PCM_QUERY) {
        if (c != sizeof(BwsPcmState)) return PCM_INVALID;
        BwsPcmState state{};
        const auto rc = query(owner, a, state);
        if (rc != PCM_OK) return rc;
        return copy_out(reinterpret_cast<void*>(b), &state, sizeof(state)) ? PCM_OK : PCM_MEMORY;
    }
    if (operation == PCM_GAIN) return gain(owner, a, b, c);
    if (operation == PCM_CLOSE && !b && !c) return close(owner, a);
    if (operation == PCM_FINISH && !b && !c) return finish(owner, a);
    return PCM_INVALID;
}
