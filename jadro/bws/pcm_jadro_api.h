#pragma once
#include "jadro/dzwiek/pcm_write_diag.h"
#include "jadro/dzwiek/pcm_sched_diag.h"
#include "jadro/dzwiek/pcm_aplikacji.h"
#include "sterowniki/czas/hpet.h"

namespace PcmWriteStaging {
inline bool claimed = false;
inline int16_t buffer[BWS_PCM_MAX_FRAMES * 2]{};
class Guard {
    bool owned_;
public:
    Guard() {
        bool expected = false;
        owned_ = __atomic_compare_exchange_n(&claimed, &expected, true, false,
                                             __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    }
    ~Guard() {
        if (owned_) __atomic_store_n(&claimed, false, __ATOMIC_RELEASE);
    }
    bool active() const { return owned_; }
    int16_t* data() const { return buffer; }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
} // namespace PcmWriteStaging

// Copy callbacks validate and copy user memory outside GuardHDA into bounded
// owned staging storage (avoiding a 2 KiB kernel stack frame). No pointer
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
        PcmWriteDiag::request(c);
        if (!c || c > BWS_PCM_MAX_FRAMES) return PcmWriteDiag::result(PCM_INVALID);
        BwsPcmState state{};
        const auto rc = query(owner, a, state);
        if (rc != PCM_OK) return PcmWriteDiag::result(rc);
        const uint64_t bytes = c * state.channels * sizeof(int16_t);
        if (!b || b > UINT64_MAX - bytes) return PcmWriteDiag::result(PCM_MEMORY);
        PcmWriteStaging::Guard staging;
        if (!staging.active()) return PcmWriteDiag::result(PCM_BUSY);
        const uint64_t begin = czas_monotoniczny_ns();
        if (!copy_in(staging.data(), reinterpret_cast<const void*>(b), bytes))
            return PcmWriteDiag::result(PCM_MEMORY);
        return PcmWriteDiag::result(write(owner, a, staging.data(), static_cast<uint32_t>(c),
                                          czas_monotoniczny_ns() - begin));
    }
    if (operation == PCM_QUERY_STATS) {
        if (c != BWS_PCM_STATS_V1_SIZE && c != sizeof(BwsPcmStats)) return PCM_INVALID;
        BwsPcmStats stats{};
        const auto rc = statistics(owner, a, stats);
        if (rc != PCM_OK) return rc;
        return copy_out(reinterpret_cast<void*>(b), &stats, static_cast<size_t>(c)) ? PCM_OK : PCM_MEMORY;
    }
    if (operation == PCM_QUERY) {
        if (c != sizeof(BwsPcmState)) return PcmWriteDiag::query_result(PCM_INVALID);
        BwsPcmState state{};
        const auto rc = query(owner, a, state);
        if (rc != PCM_OK) {
            if (rc == PCM_BUSY)
                PcmSchedDiag::record(PcmSchedDiag::Kind::PcmQueryAnomaly, czas_monotoniczny_ns(),
                                     owner.pid, static_cast<uint32_t>(-rc));
            return PcmWriteDiag::query_result(rc);
        }
        return PcmWriteDiag::query_result(
            copy_out(reinterpret_cast<void*>(b), &state, sizeof(state)) ? PCM_OK : PCM_MEMORY);
    }
    if (operation == PCM_GAIN) return gain(owner, a, b, c);
    if (operation == PCM_CLOSE && !b && !c) return close(owner, a);
    if (operation == PCM_FINISH && !b && !c) return finish(owner, a);
    return PCM_INVALID;
}
