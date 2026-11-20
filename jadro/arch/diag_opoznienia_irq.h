#pragma once
#include <stdint.h>
#include "sterowniki/czas/hpet.h"
#include "jadro/dzwiek/pcm_sched_diag.h"

// BSP-only, opt-in during the PCM regression. Instrumented scopes already
// have IF=0. Events are copied/logged after tracing has been disabled.
namespace DiagOpoznieniaIrq {
enum class Site : uint32_t { Timer, OtherIrq, Yield, Vmm, VmmClone, VmmCount, Psf, Heap, Pmm, Count };
constexpr uint32_t SITE_COUNT = static_cast<uint32_t>(Site::Count);
constexpr uint32_t CAPACITY = 32;
constexpr uint64_t LONG_NS = 12000000;
struct Span { uint64_t begin, end; uint32_t site; };
struct Gap { uint64_t previous, delivered; uint32_t pid; bool ring0; };
inline bool enabled = false;
inline Span longest[SITE_COUNT]{};
inline Span spans[CAPACITY]{};
inline Gap gaps[CAPACITY]{};
inline Gap longest_gap{};
inline uint32_t span_count, gap_count;
inline uint64_t last, interval_sum, intervals, late_gaps, eois, missing_eois, max_eoi_ns;

inline bool active() { return __atomic_load_n(&enabled, __ATOMIC_RELAXED); }
inline void enable() { __atomic_store_n(&enabled, true, __ATOMIC_RELEASE); }
inline void disable() { __atomic_store_n(&enabled, false, __ATOMIC_RELEASE); }

inline void record(Site site, uint64_t begin, uint64_t end) {
    const uint32_t id = static_cast<uint32_t>(site);
    if (end - begin > longest[id].end - longest[id].begin) longest[id] = {begin, end, id};
    if (end - begin >= LONG_NS && span_count < CAPACITY) spans[span_count++] = {begin, end, id};
}

class Scope {
    Site site_;
    uint64_t begin_;
public:
    explicit Scope(Site site, bool outer = true)
        : site_(site), begin_(outer && active() ? czas_monotoniczny_ns() : 0) {}
    ~Scope() { finish(); }
    void finish() {
        if (!begin_) return;
        record(site_, begin_, czas_monotoniczny_ns());
        begin_ = 0;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

inline uint64_t timer(unsigned pid, bool ring0) {
    if (!active()) return 0;
    const uint64_t now = czas_monotoniczny_ns();
    if (last) {
        const uint64_t ns = now - last;
        ++intervals;
        interval_sum += ns;
        if (ns > longest_gap.delivered - longest_gap.previous)
            longest_gap = {last, now, pid, ring0};
        if (ns >= LONG_NS) {
            ++late_gaps;
            if (gap_count < CAPACITY) gaps[gap_count++] = {last, now, pid, ring0};
            PcmSchedDiag::record(PcmSchedDiag::Kind::TimerGap, now, pid,
                                 static_cast<uint32_t>(ns / 1000ULL), ring0 ? 1U : 0U);
        }
    }
    last = now;
    return now;
}

inline void eoi(uint64_t entry, bool acknowledged) {
    if (!entry) return;
    if (acknowledged) ++eois;
    else ++missing_eois;
    const uint64_t ns = czas_monotoniczny_ns() - entry;
    if (ns > max_eoi_ns) max_eoi_ns = ns;
}
}
