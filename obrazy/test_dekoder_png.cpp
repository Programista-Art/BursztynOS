#include "dekoder_obrazu.h"
#include "skalowanie.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <cmath>

static size_t active_bytes = 0, peak_bytes = 0;
static void* alloc(size_t n) {
    if (n > SIZE_MAX - sizeof(size_t)) return nullptr;
    auto* p = static_cast<size_t*>(std::malloc(n + sizeof(size_t)));
    if (!p) return nullptr;
    *p = n;
    active_bytes += n;
    if (active_bytes > peak_bytes) peak_bytes = active_bytes;
    return p + 1;
}
static void freemem(void* p) {
    if (!p) return;
    auto* base = static_cast<size_t*>(p) - 1;
    active_bytes -= *base;
    std::free(base);
}
static bool bad(const std::vector<uint8_t>& d) {
    DekodowanyObraz o{};
    BladObrazu rc = dekoduj_obraz(d.data(), d.size(), &o, alloc, freemem);
    if (o.piksele) freemem(o.piksele);
    return rc != BladObrazu::OK && !o.piksele && !o.bajty;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const bool fixture = argc == 8 && std::strcmp(argv[1], "--fixture") == 0;
    const bool reject = argc == 3 && std::strcmp(argv[1], "--reject") == 0;
    const char* filename = (fixture || reject) ? argv[2] : argv[1];

    FILE* f = std::fopen(filename, "rb");
    if (!f) return 2;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::rewind(f);
    if (len <= 0) { std::fclose(f); return 2; }
    std::vector<uint8_t> d(static_cast<size_t>(len));
    if (std::fread(d.data(), 1, d.size(), f) != d.size()) { std::fclose(f); return 2; }
    std::fclose(f);

    auto start = std::chrono::steady_clock::now();
    DekodowanyObraz o{};
    auto rc = dekoduj_obraz(d.data(), d.size(), &o, alloc, freemem);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    if (reject) {
        if (o.piksele) freemem(o.piksele);
        if (rc == BladObrazu::OK || o.piksele || o.bajty || active_bytes) {
            std::fprintf(stderr, "malformed PNG accepted\n");
            return 1;
        }
        return 0;
    }

    const uint32_t expected_w = fixture ? static_cast<uint32_t>(std::atoi(argv[3])) : 1920;
    const uint32_t expected_h = fixture ? static_cast<uint32_t>(std::atoi(argv[4])) : 1080;
    if (rc != BladObrazu::OK || o.szerokosc != expected_w || o.wysokosc != expected_h || !o.piksele) {
        std::fprintf(stderr, "real PNG decode FAIL: status=%d %ux%u\n", int(rc), o.szerokosc, o.wysokosc);
        if (o.piksele) freemem(o.piksele);
        return 1;
    }

    // OPAQUE ALPHA ASSERTION: Every pixel must have A == 255
    const size_t total_px = static_cast<size_t>(o.szerokosc) * o.wysokosc;
    for (size_t i = 0; i < total_px; ++i) {
        if (o.piksele[4 * i + 3] != 255) {
            std::fprintf(stderr, "alpha opacity FAIL at pixel %zu: %u\n", i, o.piksele[4 * i + 3]);
            freemem(o.piksele);
            return 1;
        }
    }

    if (fixture) {
        const uint8_t* center = o.piksele + (static_cast<size_t>(o.wysokosc / 2) * o.szerokosc + o.szerokosc / 2) * 4;
        const int rgb[3] = {center[2], center[1], center[0]};
        for (int c = 0; c < 3; ++c) {
            if (std::abs(rgb[c] - std::atoi(argv[5 + c])) > 4) {
                std::fprintf(stderr, "fixture pixel FAIL: %d,%d,%d expected %s,%s,%s\n",
                             rgb[0], rgb[1], rgb[2], argv[5], argv[6], argv[7]);
                freemem(o.piksele);
                return 1;
            }
        }
        freemem(o.piksele);
        if (active_bytes != 0) {
            std::fprintf(stderr, "fixture decoder leak %zu bytes\n", active_bytes);
            return 1;
        }
        std::printf("fixture PASS %ux%u %ld ms\n", expected_w, expected_h, elapsed);
        return 0;
    }

    std::printf("real PNG PASS 1920x1080 %ld ms peak_decoder_heap=%zu bytes\n", elapsed, peak_bytes);

    // Multi-resolution scaling verification
    const uint32_t screens[][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
    for (const auto& screen : screens) {
        std::vector<uint32_t> desktop(static_cast<size_t>(screen[0]) * screen[1]);
        for (unsigned mode = 0; mode < 4; ++mode) {
            if (!skaluj_obraz(reinterpret_cast<const uint32_t*>(o.piksele), o.szerokosc,
                    o.wysokosc, o.bajty, desktop.data(), screen[0], screen[1], screen[0],
                    desktop.size() * 4, static_cast<TrybSkalowaniaObrazu>(mode), 0xff1a0b00U) ||
                (desktop[desktop.size() / 2] >> 24) != 255) {
                std::fprintf(stderr, "wallpaper scaling FAIL: %ux%u mode=%u\n", screen[0], screen[1], mode);
                freemem(o.piksele);
                return 1;
            }
        }
        std::printf("wallpaper scaling PASS %ux%u four modes\n", screen[0], screen[1]);
    }
    freemem(o.piksele);

    // Verify dekoduj_png API
    DekodowanyObraz dp{};
    if (dekoduj_png(d.data(), d.size(), &dp, alloc, freemem) != BladObrazu::OK || !dp.piksele) {
        std::fprintf(stderr, "dekoduj_png API failed on valid PNG\n");
        return 1;
    }
    freemem(dp.piksele);
    std::printf("dekoduj_png API PASS\n");

    // Security & malformed input tests
    int cases = 0;
    auto verify = [&](const char* name, const std::vector<uint8_t>& sample) {
        if (!bad(sample)) {
            std::fprintf(stderr, "Security case '%s' erroneously accepted!\n", name);
            std::exit(1);
        }
        ++cases;
    };

    std::vector<uint8_t> empty{}; verify("empty file", empty);
    auto m = d; m.resize(7); verify("signature truncated", m);
    m = d; m[0] = 0; verify("bad signature byte 0", m);
    m = d; m[1] = 'J'; verify("bad signature byte 1", m);
    m = d; m.resize(20); verify("truncated after IHDR head", m);
    m = d; m[12] = 'X'; verify("IHDR type corrupted", m);
    m = d; m[8] = 0; m[9] = 0; m[10] = 0; m[11] = 12; verify("IHDR length != 13", m);
    m = d; m[16] = 0; m[17] = 0; m[18] = 0; m[19] = 0; verify("zero width", m);
    m = d; m[20] = 0; m[21] = 0; m[22] = 0; m[23] = 0; verify("zero height", m);
    m = d; m[16] = 0; m[17] = 0; m[18] = 0x20; m[19] = 0x01; verify("width > 8192", m);
    m = d; m[20] = 0; m[21] = 0; m[22] = 0x20; m[23] = 0x01; verify("height > 8192", m);
    m = d; m[26] = 1; verify("invalid compression method != 0", m);
    m = d; m[27] = 1; verify("invalid filter method != 0", m);
    m = d; m[28] = 2; verify("invalid interlace method > 1", m);

    // Random fuzz tests
    uint32_t rng = 0x87654321;
    auto next_rnd = [&]() -> uint8_t {
        rng = rng * 1103515245 + 12345;
        return static_cast<uint8_t>((rng >> 16) & 0xff);
    };
    for (size_t rsize : {16, 64, 256, 1024, 4096}) {
        std::vector<uint8_t> rnd(rsize);
        for (size_t k = 0; k < rsize; ++k) rnd[k] = next_rnd();
        verify("random bytes", rnd);
        if (rsize >= 8) {
            rnd[0] = 0x89; rnd[1] = 'P'; rnd[2] = 'N'; rnd[3] = 'G';
            rnd[4] = 0x0D; rnd[5] = 0x0A; rnd[6] = 0x1A; rnd[7] = 0x0A;
            verify("random bytes with PNG header", rnd);
        }
    }

    uint8_t tiny = 0x89;
    if (dekoduj_obraz(&tiny, SIZE_MAX, &o, alloc, freemem) != BladObrazu::ZaDuzy) {
        std::fprintf(stderr, "size overflow accepted\n");
        return 1;
    }
    ++cases;

    std::printf("malformed PNG PASS %d cases\n", cases);
    if (active_bytes != 0) {
        std::fprintf(stderr, "decoder leak %zu bytes\n", active_bytes);
        return 1;
    }
    return 0;
}
