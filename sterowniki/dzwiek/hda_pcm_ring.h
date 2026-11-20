#pragma once
#include <stdint.h>

namespace HdaPcm {
constexpr uint32_t PERIOD_FRAMES = 1024;
constexpr uint32_t PERIOD_BYTES = PERIOD_FRAMES * 4;
constexpr uint32_t PERIODS = 8;
constexpr uint32_t PRIME = 4;
constexpr uint32_t LEAD = PRIME + 1; // one optional prepared period, same physical BDL
constexpr uint32_t RING_BYTES = PERIOD_BYTES * PERIODS;
constexpr uint64_t BYTES_PER_SEC = 48000 * 4;
constexpr uint64_t time_for(uint32_t bytes) { return uint64_t(bytes) * 1000000000 / BYTES_PER_SEC; }
constexpr uint64_t PERIOD_NS = time_for(PERIOD_BYTES);
constexpr uint64_t LATE_NS = time_for(2 * PERIOD_BYTES);
constexpr uint64_t STALL_NS = time_for(RING_BYTES);
// Allow a bounded burst between LPIB reads. Both the ambiguity limit and
// the write guard include it. The guard also covers the checked output FIFO.
constexpr uint32_t BURST_BYTES = 2 * PERIOD_BYTES;
constexpr uint32_t GUARD_BYTES = BURST_BYTES + PERIOD_BYTES / 4;
constexpr uint64_t MAX_GAP_NS = time_for(RING_BYTES - BURST_BYTES);
constexpr uint64_t IRQ_MARGIN_NS = 8000000;

enum class Fault : uint8_t { None, Watchdog, Ambiguous, Position, Stall, Ownership, Device, Clock, Start, Count };
enum class Action { Wait, Scrub, Fill, Skip, Recover };

struct WatchdogWindow {
    uint64_t sampled_ns;
    uint32_t lpib;
    uint32_t distance_to_stale;

    // At expiry one fresh position may prove there is still time to run the
    // worker. The original ambiguity deadline always caps this extension.
    uint64_t safe_until(uint32_t pos, uint64_t now) const {
        if (pos > RING_BYTES || now < sampled_ns || now - sampled_ns >= MAX_GAP_NS)
            return 0;
        if (pos == RING_BYTES) pos = 0;
        const uint32_t moved = (pos + RING_BYTES - lpib) % RING_BYTES;
        const uint64_t plausible = (now - sampled_ns) * BYTES_PER_SEC / 1000000000ULL + BURST_BYTES;
        if (moved > plausible || distance_to_stale <= moved + GUARD_BYTES) return 0;
        const uint64_t margin = time_for(distance_to_stale - moved - GUARD_BYTES);
        if (margin <= IRQ_MARGIN_NS) return 0;
        const uint64_t candidate = now + margin - IRQ_MARGIN_NS;
        const uint64_t cap = sampled_ns + MAX_GAP_NS;
        return candidate < cap ? candidate : cap;
    }
};

struct Ring {
    uint64_t bytes = 0;
    uint64_t fill = 0;
    uint64_t retire = 0;
    uint64_t sampled_ns = 0;
    uint64_t moved_ns = 0;
    uint32_t lpib = 0;
    uint32_t delta = 0;
    bool written[PERIODS]{};

    void start(uint64_t now) {
        bytes = retire = lpib = delta = 0;
        fill = PRIME;
        sampled_ns = moved_ns = now;
        for (uint32_t i = 0; i < PERIODS; ++i) written[i] = i < PRIME;
    }

    Fault observe(uint32_t pos, uint64_t now) {
        if (now < sampled_ns || now - sampled_ns >= MAX_GAP_NS) return Fault::Ambiguous;
        if (pos > RING_BYTES) return Fault::Position;
        if (pos == RING_BYTES) pos = 0;
        delta = (pos + RING_BYTES - lpib) % RING_BYTES;
        const uint64_t plausible = (now - sampled_ns) * BYTES_PER_SEC / 1000000000ULL + BURST_BYTES;
        if (delta > plausible) return Fault::Position;
        if (!delta && now - moved_ns >= STALL_NS) return Fault::Stall;
        if (delta) moved_ns = now;
        bytes += delta;
        lpib = pos;
        sampled_ns = now;
        return Fault::None;
    }

    static uint32_t slot(uint64_t period) { return static_cast<uint32_t>(period % PERIODS); }
    uint64_t current() const { return bytes / PERIOD_BYTES; }
    bool writable(uint64_t upcoming) const {
        const uint64_t offset = upcoming * PERIOD_BYTES;
        return offset >= bytes && offset - bytes >= GUARD_BYTES;
    }
    Action next() {
        while (retire < current() && !written[slot(retire)]) ++retire;
        if (retire < current())
            return writable(retire + PERIODS) ? Action::Scrub : Action::Recover;
        if (fill >= current() + LEAD) return Action::Wait;
        // Unprepared slots have been zeroed. If too close to the hardware,
        // leave them alone and count the silence instead of stopping DMA.
        return writable(fill) ? Action::Fill : Action::Skip;
    }
    void scrubbed() { written[slot(retire++)] = false; }
    void published(bool has_signal = true) { written[slot(fill++)] = has_signal; }

    WatchdogWindow watchdog_window() const {
        for (uint64_t p = retire; p < current() + LEAD; ++p) {
            if (!written[slot(p)]) continue;
            const uint64_t next_read = (p + PERIODS) * PERIOD_BYTES;
            return {sampled_ns, lpib, next_read > bytes ?
                       static_cast<uint32_t>(next_read - bytes) : 0};
        }
        return {sampled_ns, lpib, RING_BYTES};
    }
};
}
