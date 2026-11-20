#pragma once
/*
 * Ograniczony rdzen PCM. Nie wykonuje alokacji ani dostepow do Ring 3.
 * Wywolujacy serializuje WSZYSTKIE operacje, wlacznie z mix() i cleanup().
 * Wskazniki write() i mix() musza wskazywac pamiec jadra przez caly czas
 * wywolania. To nie jest jeszcze publiczny interfejs BWS ani sterownik DMA.
 */
#include <stdint.h>
#include <stddef.h>
#include "jadro/bws/pcm_api.h"

namespace Pcm {
constexpr uint32_t RATE = 48000;
constexpr uint32_t FORMAT_S16 = 1;
constexpr uint32_t MAX_STREAMS = 8;
constexpr uint32_t MAX_PER_PROCESS = 2;
constexpr uint32_t CAPACITY_FRAMES = 4096;
constexpr uint32_t MAX_MIX_FRAMES = 1024;
constexpr uint32_t MAX_COPY_FRAMES = 256;

struct Owner { uint32_t pid; uint64_t generation; };
struct Format { uint32_t rate, channels, encoding; };
struct Frame { int16_t left, right; };
static_assert(sizeof(Frame) == 4, "PCM frame layout");
struct Status { uint32_t queued, capacity, volume, muted, channels; };
struct Starvation { Owner owner; uint64_t ns, last_write_ns; uint32_t missing, queued; };
struct QueueLevels { uint32_t minimum, maximum, expecting; };

enum Result : uint32_t { OK, INVALID, UNSUPPORTED, LIMIT, BAD_HANDLE, FULL, BAD_BUFFER };
// The callback validates the entire range and copies it to kernel memory.
using CopyFromUser = bool (*)(void* dst, const void* src, size_t bytes);

class Mixer {
    struct Stream {
        Frame data[CAPACITY_FRAMES];
        BwsPcmStats stats;
        Owner owner;
        uint64_t handle;
        uint64_t first_write_ns;
        uint32_t read, count, channels, volume, preroll;
        bool mute, active, expecting, primed;
    };
    Stream streams_[MAX_STREAMS]{};
    uint64_t last_handle_ = 0;
    uint64_t underrun_frames_ = 0;
    Starvation starvation_[32]{};
    uint32_t starvation_count_ = 0;

    static bool valid_owner(Owner o) { return o.pid != 0 && o.generation != 0; }
    static bool same(Owner a, Owner b) { return a.pid == b.pid && a.generation == b.generation; }
    Stream* find(Owner owner, uint64_t handle) {
        if (!valid_owner(owner) || !handle) return nullptr;
        for (uint32_t i = 0; i < MAX_STREAMS; ++i)
            if (streams_[i].active && streams_[i].handle == handle && same(streams_[i].owner, owner))
                return &streams_[i];
        return nullptr;
    }
    static int16_t saturate(int64_t value) {
        if (value > 32767) return 32767;
        if (value < -32768) return -32768;
        return static_cast<int16_t>(value);
    }
    static void remove(Stream& s) {
        // No pointers into a stream escape; stale samples are never addressed.
        s.active = false; s.count = 0; s.read = 0; s.handle = 0; s.owner = {};
        s.expecting = false; s.primed = false; s.preroll = 0; s.first_write_ns = 0;
    }
public:
    Mixer() = default;
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    Result open(Owner owner, Format fmt, uint64_t* out, uint32_t preroll_frames = 0) {
        if (!out || !valid_owner(owner)) return INVALID;
        *out = 0;
        if (fmt.rate != RATE || fmt.encoding != FORMAT_S16 || (fmt.channels != 1 && fmt.channels != 2)) return UNSUPPORTED;
        uint32_t owned = 0, slot = MAX_STREAMS;
        for (uint32_t i = 0; i < MAX_STREAMS; ++i) {
            if (streams_[i].active && same(streams_[i].owner, owner)) ++owned;
            if (!streams_[i].active && slot == MAX_STREAMS) slot = i;
        }
        // A wrapped handle counter is retired permanently, never reused.
        if (owned >= MAX_PER_PROCESS || slot == MAX_STREAMS || last_handle_ == INT64_MAX) return LIMIT;
        Stream& s = streams_[slot];
        s.owner = owner; s.handle = ++last_handle_; s.read = 0; s.count = 0;
        s.channels = fmt.channels; s.volume = 100; s.mute = false; s.active = true;
        s.preroll = preroll_frames > CAPACITY_FRAMES ? CAPACITY_FRAMES : preroll_frames;
        s.primed = s.preroll == 0;
        s.first_write_ns = 0;
        s.expecting = false;
        s.stats = {};
        s.stats.min_queued = CAPACITY_FRAMES;
        *out = s.handle;
        return OK;
    }
    Result close(Owner owner, uint64_t handle) {
        Stream* s = find(owner, handle); if (!s) return BAD_HANDLE;
        remove(*s); return OK;
    }
    void archive_owner(Owner owner, void (*visit)(Owner, uint64_t)) const {
        for (const auto& s : streams_)
            if (s.active && same(s.owner, owner)) visit(owner, s.handle);
    }
    uint32_t cleanup(Owner owner) {
        if (!valid_owner(owner)) return 0;
        uint32_t removed = 0;
        for (uint32_t i = 0; i < MAX_STREAMS; ++i)
            if (streams_[i].active && same(streams_[i].owner, owner)) { remove(streams_[i]); ++removed; }
        return removed;
    }
    Result status(Owner owner, uint64_t handle, Status* out) {
        if (!out) return INVALID;
        Stream* s = find(owner, handle); if (!s) return BAD_HANDLE;
        *out = Status{s->count, CAPACITY_FRAMES, s->volume, s->mute ? 1U : 0U, s->channels};
        return OK;
    }
    Result statistics(Owner owner, uint64_t handle, BwsPcmStats& out) {
        Stream* s = find(owner, handle);
        if (!s) return BAD_HANDLE;
        out = s->stats;
        out.queued = s->count;
        return OK;
    }
    uint64_t write_timing(Owner owner, uint64_t handle, uint64_t now, uint64_t copy_ns) {
        Stream* s = find(owner, handle);
        if (!s) return 0;
        uint64_t gap = 0;
        if (s->stats.last_write_ns && now >= s->stats.last_write_ns) {
            gap = now - s->stats.last_write_ns;
            if (gap > s->stats.max_write_gap_ns) s->stats.max_write_gap_ns = gap;
        }
        s->stats.last_write_ns = now;
        if (copy_ns > s->stats.max_copy_ns) s->stats.max_copy_ns = copy_ns;
        return gap;
    }
    Result gain(Owner owner, uint64_t handle, uint32_t volume, uint32_t mute) {
        if (volume > 100 || mute > 1) return INVALID;
        Stream* s = find(owner, handle); if (!s) return BAD_HANDLE;
        s->volume = volume; s->mute = mute != 0; return OK;
    }
    // Drain a finite source normally. A subsequent accepted write resumes it.
    Result finish(Owner owner, uint64_t handle) {
        Stream* s = find(owner, handle); if (!s) return BAD_HANDLE;
        if (s->count > 0) s->primed = true;
        s->expecting = false;
        return OK;
    }
    // Samples are interleaved native little-endian S16; return accepted frames.
    // Only the accepted prefix is read. A zero-frame write is invalid.
    Result write(Owner owner, uint64_t handle, const int16_t* samples, uint32_t frames,
                 uint32_t* accepted, uint64_t now = 0) {
        if (!accepted) return INVALID;
        *accepted = 0;
        if (!samples || !frames) {
            if (Stream* s = find(owner, handle)) ++s->stats.rejected;
            return INVALID;
        }
        Stream* s = find(owner, handle); if (!s) return BAD_HANDLE;
        uint32_t n = CAPACITY_FRAMES - s->count;
        if (!n) { ++s->stats.full; ++s->stats.rejected; return FULL; }
        ++s->stats.writes;
        s->stats.requested += frames;
        if (n > frames) n = frames;
        uint32_t pos = (s->read + s->count) % CAPACITY_FRAMES;
        for (uint32_t i = 0; i < n; ++i) {
            const int16_t l = samples[size_t(i) * s->channels];
            const int16_t r = s->channels == 2 ? samples[size_t(i) * 2 + 1] : l;
            s->data[pos] = Frame{l, r};
            pos = (pos + 1U) % CAPACITY_FRAMES;
        }
        s->stats.accepted += n;
        if (n < frames) ++s->stats.partial;
        s->count += n;
        if (s->count > s->stats.max_queued) s->stats.max_queued = s->count;
        if (!s->primed) {
            if (!s->first_write_ns && now) s->first_write_ns = now;
            if (s->count >= s->preroll) {
                s->primed = true;
                s->expecting = true;
            }
        } else {
            s->expecting = true;
        }
        *accepted = n;
        return OK;
    }
    // A bounded staged copy means a failed or partially completed copy cannot
    // publish any PCM. Serialize this operation just like write() and mix().
    Result write_user(Owner owner, uint64_t handle, const void* user, uint32_t frames,
                      CopyFromUser copy, uint32_t* accepted) {
        if (!accepted) return INVALID;
        *accepted = 0;
        if (!user || !frames || !copy) return INVALID;
        Stream* s = find(owner, handle); if (!s) return BAD_HANDLE;
        uint32_t n = CAPACITY_FRAMES - s->count;
        if (!n) return FULL;
        if (n > frames) n = frames;
        if (n > MAX_COPY_FRAMES) n = MAX_COPY_FRAMES;
        int16_t staged[MAX_COPY_FRAMES * 2];
        const size_t bytes = size_t(n) * s->channels * sizeof(int16_t);
        if (!copy(staged, user, bytes)) return BAD_BUFFER;
        return write(owner, handle, staged, n, accepted);
    }
    // A transport may check hardware ownership again before committing PCM.
    // Render and consume under the same caller lock. Offset is for DMA priming.
    Result preview(Frame* output, uint32_t frames, uint32_t offset = 0) const {
        if (!output || !frames || frames > MAX_MIX_FRAMES || offset > CAPACITY_FRAMES - frames)
            return INVALID;
        for (uint32_t f = 0; f < frames; ++f) {
            int64_t left = 0, right = 0;
            for (const auto& s : streams_) {
                if (!s.active || !s.primed || s.mute || offset + f >= s.count) continue;
                const Frame v = s.data[(s.read + offset + f) % CAPACITY_FRAMES];
                left += int64_t(v.left) * s.volume / 100;
                right += int64_t(v.right) * s.volume / 100;
            }
            output[f] = Frame{saturate(left), saturate(right)};
        }
        return OK;
    }
    // Counts committed output frames with at least one expected source absent.
    // Muting does not stop source time. Unstarted, priming, drained and closed
    // sources do not count. Shortages overlap at the tail, so use the longest one.
    uint32_t consume(uint32_t frames, uint64_t now = 0) {
        uint32_t missing = 0;
        for (auto& s : streams_) {
            if (!s.active || !s.primed) continue;
            const uint32_t taken = frames < s.count ? frames : s.count;
            if (s.expecting) {
                if (s.mute || !s.volume) s.stats.muted_missing += frames - taken;
                else {
                    s.stats.starved += frames - taken;
                    if (frames > taken && starvation_count_ < 32)
                        starvation_[starvation_count_++] = {s.owner, now, s.stats.last_write_ns, frames - taken, s.count};
                }
                if (frames - taken > missing) missing = frames - taken;
            }
            s.stats.consumed += taken;
            s.read = (s.read + taken) % CAPACITY_FRAMES;
            s.count -= taken;
            if (s.expecting && s.count < s.stats.min_queued) s.stats.min_queued = s.count;
        }
        underrun_frames_ += missing <= UINT64_MAX - underrun_frames_ ? missing : UINT64_MAX - underrun_frames_;
        return missing;
    }
    Result mix(Frame* output, uint32_t frames) {
        const auto result = preview(output, frames);
        if (result == OK) consume(frames);
        return result;
    }
    // Promote any unprimed non-empty stream whose bounded startup grace elapsed.
    uint32_t promote_timed_out(uint64_t now, uint64_t grace_ns) {
        uint32_t promoted = 0;
        for (auto& s : streams_) {
            if (!s.active || s.primed || !s.count) continue;
            if (!s.first_write_ns) { s.first_write_ns = now; continue; }
            if (now >= s.first_write_ns && now - s.first_write_ns >= grace_ns) {
                s.primed = true;
                s.expecting = true;
                ++promoted;
            }
        }
        return promoted;
    }
    uint32_t promote_all_nonempty() {
        uint32_t promoted = 0;
        for (auto& s : streams_) {
            if (!s.active || s.primed || !s.count) continue;
            s.primed = true;
            s.expecting = true;
            ++promoted;
        }
        return promoted;
    }
    QueueLevels queue_levels() const {
        QueueLevels levels{CAPACITY_FRAMES, 0, 0};
        for (const auto& s : streams_) {
            if (!s.active || !s.primed || !s.expecting) continue;
            if (s.count < levels.minimum) levels.minimum = s.count;
            if (s.count > levels.maximum) levels.maximum = s.count;
            ++levels.expecting;
        }
        if (!levels.expecting) levels.minimum = 0;
        return levels;
    }
    bool has_ready_pcm() const {
        for (const auto& s : streams_)
            if (s.active && s.primed && s.count) return true;
        return false;
    }
    bool has_pcm() const {
        for (const auto& s : streams_)
            if (s.active && s.count) return true;
        return false;
    }
    bool starvation(uint32_t index, Starvation& out) const {
        if (index >= starvation_count_) return false;
        out = starvation_[index];
        return true;
    }
    uint64_t underrun_frames() const { return underrun_frames_; }
};
} // namespace Pcm
