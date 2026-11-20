#pragma once
#include <stdint.h>

// Lightweight, bounded scheduler & PCM diagnostic timeline.
// Per-PID wait/execution stats run only for tracked audio PIDs (zero HPET overhead
// when idle); timeline capture is opt-in during regression runs. No serial I/O.
namespace PcmSchedDiag {
constexpr uint32_t MAX_PIDS = 16;
constexpr uint32_t TIMELINE_CAP = 128;
constexpr uint64_t LONG_SCHED_NS = 12000000ULL;
constexpr uint64_t LONG_WRITE_NS = 30000000ULL;

enum class Kind : uint8_t {
    None = 0,
    TimerGap = 1,
    SchedLongWait = 2,
    VoluntaryLongWait = 3,
    PcmWriteGap = 4,
    PcmQueryAnomaly = 5,
    HdaWorkerLate = 6,
    HdaShortfall = 7,
    HdaRecovery = 8,
};

struct PidStats {
    uint64_t ready_since_ns;
    uint64_t wait_since_ns;
    uint64_t last_scheduled_ns;
    uint64_t max_runnable_wait_ns;
    uint64_t max_voluntary_wait_ns;
    uint64_t max_exec_gap_ns;
    uint64_t total_runnable_wait_ns;
    uint64_t total_voluntary_wait_ns;
    uint64_t runnable_waits;
    uint64_t voluntary_waits;
    uint64_t scheduled_count;
};

struct Event {
    uint64_t ns;
    uint32_t pid;
    uint32_t arg0;
    uint32_t arg1;
    uint8_t kind;
    uint8_t flags;
};

inline PidStats pids[MAX_PIDS]{};
inline uint32_t tracked_mask = 0;
inline bool timeline_enabled = false;
inline Event timeline[TIMELINE_CAP]{};
inline uint32_t timeline_count = 0;

inline void enable_timeline() {
    timeline_count = 0;
    __atomic_store_n(&timeline_enabled, true, __ATOMIC_RELEASE);
}

inline void disable_timeline() {
    __atomic_store_n(&timeline_enabled, false, __ATOMIC_RELEASE);
}

inline bool timeline_active() {
    return __atomic_load_n(&timeline_enabled, __ATOMIC_ACQUIRE);
}

inline void record(Kind kind, uint64_t ns, uint32_t pid, uint32_t arg0, uint32_t arg1 = 0, uint8_t flags = 0) {
    if (!timeline_active()) return;
    const uint32_t idx = __atomic_load_n(&timeline_count, __ATOMIC_RELAXED);
    if (idx >= TIMELINE_CAP) return;
    const uint32_t slot = __atomic_fetch_add(&timeline_count, 1U, __ATOMIC_ACQ_REL);
    if (slot < TIMELINE_CAP)
        timeline[slot] = Event{ns, pid, arg0, arg1, static_cast<uint8_t>(kind), flags};
}

inline bool is_tracked(uint32_t pid) {
    return pid > 0 && pid < MAX_PIDS &&
           ((__atomic_load_n(&tracked_mask, __ATOMIC_ACQUIRE) & (1U << pid)) != 0);
}

inline void track_pid(uint32_t pid) {
    if (pid == 0 || pid >= MAX_PIDS) return;
    __atomic_fetch_or(&tracked_mask, 1U << pid, __ATOMIC_RELEASE);
}

inline void reset_pid(uint32_t pid) {
    if (pid == 0 || pid >= MAX_PIDS) return;
    __atomic_fetch_and(&tracked_mask, ~(1U << pid), __ATOMIC_RELEASE);
    pids[pid] = {};
}

// Called with interrupts disabled inside scheduler transitions.
inline void on_wait_enter(int pid, uint64_t now) {
    if (pid <= 0 || pid >= static_cast<int>(MAX_PIDS) || !now || !is_tracked(static_cast<uint32_t>(pid))) return;
    PidStats& s = pids[pid];
    s.ready_since_ns = 0;
    if (!s.wait_since_ns) {
        s.wait_since_ns = now;
        ++s.voluntary_waits;
    }
}

inline void on_ready(int pid, uint64_t now, uint32_t reason = 0) {
    if (pid <= 0 || pid >= static_cast<int>(MAX_PIDS) || !now || !is_tracked(static_cast<uint32_t>(pid))) return;
    PidStats& s = pids[pid];
    if (s.wait_since_ns && now >= s.wait_since_ns) {
        const uint64_t slept = now - s.wait_since_ns;
        s.total_voluntary_wait_ns += slept;
        if (slept > s.max_voluntary_wait_ns) s.max_voluntary_wait_ns = slept;
        if (slept >= LONG_SCHED_NS)
            record(Kind::VoluntaryLongWait, now, static_cast<uint32_t>(pid),
                   static_cast<uint32_t>(slept / 1000ULL), reason);
    }
    s.wait_since_ns = 0;
    if (!s.ready_since_ns) s.ready_since_ns = now;
}

inline void on_preempt(int pid, uint64_t now, bool still_runnable) {
    if (pid <= 0 || pid >= static_cast<int>(MAX_PIDS) || !now || !is_tracked(static_cast<uint32_t>(pid))) return;
    PidStats& s = pids[pid];
    if (still_runnable && !s.ready_since_ns) s.ready_since_ns = now;
}

inline void on_schedule(int pid, uint64_t now) {
    if (pid <= 0 || pid >= static_cast<int>(MAX_PIDS) || !now || !is_tracked(static_cast<uint32_t>(pid))) return;
    PidStats& s = pids[pid];
    uint64_t exec_gap = 0;
    if (s.last_scheduled_ns && now >= s.last_scheduled_ns) {
        exec_gap = now - s.last_scheduled_ns;
        if (exec_gap > s.max_exec_gap_ns) s.max_exec_gap_ns = exec_gap;
    }
    s.last_scheduled_ns = now;
    ++s.scheduled_count;
    if (s.ready_since_ns && now >= s.ready_since_ns) {
        const uint64_t wait = now - s.ready_since_ns;
        s.total_runnable_wait_ns += wait;
        ++s.runnable_waits;
        if (wait > s.max_runnable_wait_ns) s.max_runnable_wait_ns = wait;
        if (wait >= LONG_SCHED_NS)
            record(Kind::SchedLongWait, now, static_cast<uint32_t>(pid),
                   static_cast<uint32_t>(wait / 1000ULL),
                   static_cast<uint32_t>(exec_gap / 1000ULL));
    }
    s.ready_since_ns = 0;
}

inline bool snapshot_pid(uint32_t pid, PidStats& out) {
    if (pid == 0 || pid >= MAX_PIDS) return false;
    out = pids[pid];
    return true;
}

inline uint32_t event_count() {
    const uint32_t n = __atomic_load_n(&timeline_count, __ATOMIC_ACQUIRE);
    return n < TIMELINE_CAP ? n : TIMELINE_CAP;
}

inline bool event(uint32_t index, Event& out) {
    if (index >= event_count()) return false;
    out = timeline[index];
    return true;
}
} // namespace PcmSchedDiag
