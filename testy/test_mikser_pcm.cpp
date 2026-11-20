#include "jadro/dzwiek/mikser_pcm.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstring>
using namespace Pcm;

static bool copy_ok(void* dst, const void* src, size_t n) {
    std::memcpy(dst, src, n); return true;
}
static bool copy_incomplete(void* dst, const void* src, size_t n) {
    if (n) std::memcpy(dst, src, n / 2);
    return false;
}

int main() {
    auto m = std::make_unique<Mixer>();
    const Owner a{1, 1}, b{2, 1}, reused{1, 2};
    const Format stereo{48000, 2, FORMAT_S16}, mono{48000, 1, FORMAT_S16};
    uint64_t ah=0, bh=0, h=0;
    assert(m->open(a, {44100, 2, FORMAT_S16}, &h) == UNSUPPORTED);
    assert(m->open(a, {48000, 3, FORMAT_S16}, &h) == UNSUPPORTED);
    assert(m->open(a, {48000, 2, 99}, &h) == UNSUPPORTED);
    assert(m->open({0, 1}, stereo, &h) == INVALID);
    assert(m->open(a, stereo, &ah) == OK && ah);
    assert(m->open(b, mono, &bh) == OK && bh && ah != bh);
    Status s{};
    assert(m->status(b, ah, &s) == BAD_HANDLE);
    assert(m->status(reused, ah, &s) == BAD_HANDLE);
    const int16_t as[] = {30000, -30000, 1000, -1000, 500, -500};
    const int16_t bs[] = {10000, 2000, -500};
    uint32_t accepted = 999;
    assert(m->write_user(a, ah, as, 3, copy_incomplete, &accepted) == BAD_BUFFER && accepted == 0);
    assert(m->status(a, ah, &s) == OK && s.queued == 0);
    assert(m->write_user(b, ah, as, 3, copy_ok, &accepted) == BAD_HANDLE && accepted == 0);
    assert(m->write_user(a, ah, nullptr, 3, copy_ok, &accepted) == INVALID);
    assert(m->write(a, ah, as, 3, &accepted) == OK && accepted == 3);
    assert(m->write(b, bh, bs, 3, &accepted) == OK && accepted == 3);
    Frame out[4]{};
    assert(m->mix(out, 3) == OK);
    assert(out[0].left == 32767 && out[0].right == -20000);
    assert(out[1].left == 3000 && out[1].right == 1000);
    assert(out[2].left == 0 && out[2].right == -1000);
    assert(m->gain(a, ah, 25, 0) == OK);
    assert(m->gain(b, bh, 100, 1) == OK);
    assert(m->write(a, ah, as, 1, &accepted) == OK);
    assert(m->write(b, bh, bs, 1, &accepted) == OK);
    assert(m->mix(out, 1) == OK && out[0].left == 7500 && out[0].right == -7500);
    assert(m->mix(out, 1) == OK && out[0].left == 0 && out[0].right == 0);
    assert(m->underrun_frames() == 1);
    assert(m->gain(a, ah, 101, 0) == INVALID);
    assert(m->write(a, ah, nullptr, 1, &accepted) == INVALID && accepted == 0);
    assert(m->mix(out, MAX_MIX_FRAMES + 1) == INVALID);
    assert(m->close(b, bh) == OK);
    std::vector<int16_t> samples((CAPACITY_FRAMES + 4) * 2, 1234);
    assert(m->write_user(a, ah, samples.data(), UINT32_MAX, copy_ok, &accepted) == OK && accepted == MAX_COPY_FRAMES);
    Frame chunk[MAX_MIX_FRAMES];
    assert(m->mix(chunk, MAX_COPY_FRAMES) == OK);
    assert(m->write(a, ah, samples.data(), UINT32_MAX, &accepted) == OK && accepted == CAPACITY_FRAMES);
    assert(m->write(a, ah, samples.data(), 1, &accepted) == FULL && accepted == 0);
    assert(m->gain(a, ah, 100, 0) == OK);
    assert(m->mix(chunk, MAX_MIX_FRAMES) == OK);
    for (auto f : chunk) assert(f.left == 1234 && f.right == 1234);
    for (size_t i = 0; i < 16; ++i) samples[i] = -2345;
    assert(m->write(a, ah, samples.data(), 8, &accepted) == OK && accepted == 8);
    for (int i = 0; i < 3; ++i) assert(m->mix(chunk, MAX_MIX_FRAMES) == OK);
    assert(m->mix(chunk, 8) == OK);
    for (unsigned i = 0; i < 8; ++i) assert(chunk[i].left == -2345 && chunk[i].right == -2345);
    assert(m->open(a, mono, &h) == OK);
    uint64_t extra = 0;
    assert(m->open(a, mono, &extra) == LIMIT);
    m->cleanup(reused);
    assert(m->status(a, ah, &s) == OK);
    m->cleanup(a);
    assert(m->status(a, ah, &s) == BAD_HANDLE && m->close(a, h) == BAD_HANDLE);
    assert(m->mix(out, 1) == OK && out[0].left == 0 && out[0].right == 0);
    assert(m->open(reused, stereo, &extra) == OK && extra != ah);
    assert(m->status(reused, ah, &s) == BAD_HANDLE);
    assert(m->close(reused, extra) == OK && m->close(reused, extra) == BAD_HANDLE);
    uint64_t handles[MAX_STREAMS];
    for (uint32_t i = 0; i < MAX_STREAMS; ++i) {
        Owner o{i + 10, 1};
        assert(m->open(o, stereo, &handles[i]) == OK);
        const int16_t x[] = {-32768, 32767};
        assert(m->write(o, handles[i], x, 1, &accepted) == OK);
    }
    assert(m->open({99,1}, stereo, &extra) == LIMIT);
    assert(m->mix(out, 1) == OK && out[0].left == -32768 && out[0].right == 32767);
    for (uint32_t i = 0; i < MAX_STREAMS; ++i) m->cleanup({i + 10, 1});
    // Cleanup of one process must not remove queued PCM of another.
    assert(m->open(a, stereo, &ah) == OK);
    assert(m->open(b, stereo, &bh) == OK);
    const int16_t one[] = {400, -800};
    const int16_t two[] = {1000, -2000};
    assert(m->write(a, ah, one, 1, &accepted) == OK);
    assert(m->write(b, bh, two, 1, &accepted) == OK);
    m->cleanup(a);
    assert(m->mix(out, 1) == OK && out[0].left == 1000 && out[0].right == -2000);
    assert(m->close(b, bh) == OK);
    // Closing a nonempty stream must not replay its queued frames in a new one.
    assert(m->open(a, stereo, &ah) == OK);
    assert(m->write(a, ah, one, 1, &accepted) == OK);
    assert(m->close(a, ah) == OK);
    assert(m->open(a, stereo, &h) == OK);
    assert(m->mix(out, 1) == OK && out[0].left == 0 && out[0].right == 0);
    assert(m->close(a, h) == OK);
    // Opening and finite completion do not starve a live second source.
    const auto underruns = m->underrun_frames();
    assert(m->open(a, stereo, &ah) == OK && m->open(b, stereo, &bh) == OK);
    assert(m->write(b, bh, as, 3, &accepted) == OK);
    assert(m->preview(out, 1, 1) == OK && out[0].left == 1000);
    assert(m->status(b, bh, &s) == OK && s.queued == 3 && m->underrun_frames() == underruns);
    assert(m->mix(out, 1) == OK && out[0].left == 30000);
    assert(m->write(a, ah, one, 1, &accepted) == OK && m->finish(a, ah) == OK);
    assert(m->mix(out, 2) == OK && out[0].left == 1400 && out[1].left == 500);
    assert(m->underrun_frames() == underruns);
    assert(m->write(a, ah, one, 1, &accepted) == OK);
    assert(m->mix(out, 1) == OK && out[0].left == 400 && m->underrun_frames() == underruns + 1);
    assert(m->close(b, bh) == OK && m->close(a, ah) == OK);
    for (unsigned i = 0; i < 100; ++i) {
        assert(m->open(a, mono, &h) == OK);
        assert(m->write(a, h, bs, 3, &accepted) == OK && accepted == 3);
        assert(m->close(a, h) == OK);
    }
    auto measured = std::make_unique<Mixer>();
    uint64_t measured_handle = 0;
    assert(measured->open(a, stereo, &measured_handle) == OK);
    assert(measured->write(a, measured_handle, samples.data(), 255, &accepted) == OK);
    assert(measured->mix(chunk, 256) == OK);
    BwsPcmStats metrics{};
    assert(measured->statistics(a, measured_handle, metrics) == OK);
    assert(metrics.writes == 1 && metrics.accepted == 255 && metrics.consumed == 255);
    assert(metrics.starved == 1 && metrics.min_queued == 0 && metrics.max_queued == 255);
    assert(measured->gain(a, measured_handle, 100, 1) == OK);
    assert(measured->mix(chunk, 256) == OK);
    assert(measured->statistics(a, measured_handle, metrics) == OK);
    assert(metrics.starved == 1 && metrics.muted_missing == 256);
    assert(measured->finish(a, measured_handle) == OK);
    assert(measured->mix(chunk, 1024) == OK);
    assert(measured->statistics(a, measured_handle, metrics) == OK && metrics.starved == 1);
    assert(measured->statistics(b, measured_handle, metrics) == BAD_HANDLE);

    // Per-stream pre-roll: second stream starting while first is active does not
    // interrupt first and is not prematurely consumed on its first 512-frame write.
    {
        auto pr = std::make_unique<Mixer>();
        uint64_t h1 = 0, h2 = 0;
        assert(pr->open(a, stereo, &h1, 2048) == OK);
        assert(pr->open(b, stereo, &h2, 2048) == OK);
        std::vector<int16_t> tone1(4096 * 2, 2000), tone2(4096 * 2, 1000);
        assert(pr->write(a, h1, tone1.data(), 4096, &accepted, 1000) == OK && accepted == 4096);
        // Stream B submits only 512 frames (still priming < 2048).
        assert(pr->write(b, h2, tone2.data(), 512, &accepted, 1000) == OK && accepted == 512);
        // Stream A plays cleanly without starving or consuming Stream B's 512 frames.
        assert(pr->mix(chunk, 1024) == OK);
        assert(chunk[0].left == 2000 && pr->underrun_frames() == 0);
        Status sb{};
        assert(pr->status(b, h2, &sb) == OK && sb.queued == 512);
        // Once Stream B completes pre-roll (2048 frames), both mix cleanly with 0 starvation.
        assert(pr->write(b, h2, tone2.data(), 1536, &accepted, 2000) == OK && accepted == 1536);
        assert(pr->mix(chunk, 1024) == OK);
        assert(chunk[0].left == 3000 && pr->underrun_frames() == 0);
        BwsPcmStats st1{}, st2{};
        assert(pr->statistics(a, h1, st1) == OK && st1.starved == 0 && st1.queued == 2048);
        assert(pr->statistics(b, h2, st2) == OK && st2.starved == 0 && st2.queued == 1024);
        // Full rejection increments full & rejected without inflating successful writes.
        assert(pr->write(a, h1, tone1.data(), 2048, &accepted, 3000) == OK && accepted == 2048);
        assert(pr->write(a, h1, tone1.data(), 16, &accepted, 3000) == FULL && accepted == 0);
        assert(pr->statistics(a, h1, st1) == OK && st1.writes == 2 && st1.full == 1 && st1.rejected == 1);
        // Partial write increments partial and writes, and advances queue to exact capacity.
        assert(pr->mix(chunk, 500) == OK);
        assert(pr->write(a, h1, tone1.data(), 512, &accepted, 4000) == OK && accepted == 500);
        assert(pr->statistics(a, h1, st1) == OK && st1.partial == 1 && st1.writes == 3 && st1.queued == CAPACITY_FRAMES);
        // Repeated full -> empty transitions and delayed-source timeout promotion.
        uint64_t h3 = 0;
        Owner c{3, 1};
        assert(pr->open(c, stereo, &h3, 2048) == OK);
        assert(pr->write(c, h3, tone2.data(), 256, &accepted, 10000) == OK && accepted == 256);
        assert(pr->promote_timed_out(20000, 21333333ULL) == 0);
        assert(pr->promote_timed_out(10000 + 21333333ULL, 21333333ULL) == 1);
        assert(pr->mix(chunk, 1024) == OK);
        BwsPcmStats st3{};
        assert(pr->statistics(c, h3, st3) == OK && st3.consumed == 256 && st3.starved == 768);
        // Short finite clip with finish() bypasses pre-roll with 0 starvation.
        uint64_t h4 = 0;
        Owner d{4, 1};
        assert(pr->open(d, stereo, &h4, 2048) == OK);
        assert(pr->write(d, h4, tone2.data(), 256, &accepted, 30000) == OK);
        assert(pr->finish(d, h4) == OK);
        assert(pr->has_ready_pcm());
        assert(pr->mix(chunk, 512) == OK);
        BwsPcmStats st4{};
        assert(pr->statistics(d, h4, st4) == OK && st4.consumed == 256 && st4.starved == 0);
    }
    std::puts("test_mikser_pcm: OK");
}
