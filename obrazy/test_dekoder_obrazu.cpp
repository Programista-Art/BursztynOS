#include "dekoder_obrazu.h"
#include "skalowanie.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>

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
    std::fseek(f, 0, SEEK_END); const long len = std::ftell(f); std::rewind(f);
    if (len <= 0) return 2;
    std::vector<uint8_t> d(static_cast<size_t>(len));
    if (std::fread(d.data(), 1, d.size(), f) != d.size()) return 2;
    std::fclose(f);
    auto start = std::chrono::steady_clock::now();
    DekodowanyObraz o{};
    auto rc = dekoduj_obraz(d.data(), d.size(), &o, alloc, freemem);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (reject) {
        if (o.piksele) freemem(o.piksele);
        if (rc == BladObrazu::OK || o.piksele || o.bajty || active_bytes) {
            std::fprintf(stderr, "malformed JPEG accepted\n"); return 1;
        }
        return 0;
    }
    const uint32_t expected_w = fixture ? static_cast<uint32_t>(std::atoi(argv[3])) : 1920;
    const uint32_t expected_h = fixture ? static_cast<uint32_t>(std::atoi(argv[4])) : 1080;
    if (rc != BladObrazu::OK || o.szerokosc != expected_w || o.wysokosc != expected_h || !o.piksele) {
        std::fprintf(stderr, "real JPEG decode FAIL: status=%d %ux%u\n", int(rc), o.szerokosc, o.wysokosc); return 1;
    }
    if (fixture) {
        const uint8_t* center = o.piksele + (static_cast<size_t>(o.wysokosc / 2) * o.szerokosc + o.szerokosc / 2) * 4;
        const int rgb[3] = {center[2], center[1], center[0]};
        for (int c = 0; c < 3; ++c) if (std::abs(rgb[c] - std::atoi(argv[5+c])) > 4) {
            std::fprintf(stderr, "fixture pixel FAIL: %d,%d,%d\n", rgb[0], rgb[1], rgb[2]); return 1;
        }
        freemem(o.piksele);
        if (active_bytes != 0) { std::fprintf(stderr, "fixture decoder leak %zu bytes\n", active_bytes); return 1; }
        std::printf("fixture PASS %ux%u %ld ms\n", expected_w, expected_h, elapsed);
        return 0;
    }
    std::printf("real JPEG PASS 1920x1080 %ld ms peak_decoder_heap=%zu bytes\n", elapsed, peak_bytes);
    const uint32_t screens[][2] = {{1280,720}, {1920,1080}, {2560,1440}, {3440,1440}, {3840,2160}};
    for (const auto& screen : screens) {
        std::vector<uint32_t> desktop(static_cast<size_t>(screen[0]) * screen[1]);
        for (unsigned mode = 0; mode < 4; ++mode) {
            if (!skaluj_obraz(reinterpret_cast<const uint32_t*>(o.piksele), o.szerokosc,
                    o.wysokosc, o.bajty, desktop.data(), screen[0], screen[1], screen[0],
                    desktop.size() * 4, static_cast<TrybSkalowaniaObrazu>(mode), 0xff1a0b00U) ||
                (desktop[desktop.size() / 2] >> 24) != 255) {
                std::fprintf(stderr, "wallpaper scaling FAIL: %ux%u mode=%u\n", screen[0], screen[1], mode);
                return 1;
            }
        }
        std::printf("wallpaper scaling PASS %ux%u four modes\n", screen[0], screen[1]);
    }
    freemem(o.piksele);
    // Verify dekoduj_jpeg API
    DekodowanyObraz dj{};
    if (dekoduj_jpeg(d.data(), d.size(), &dj, alloc, freemem) != BladObrazu::OK || !dj.piksele) {
        std::fprintf(stderr, "dekoduj_jpeg API failed on valid JPEG\n");
        return 1;
    }
    freemem(dj.piksele);
    std::printf("dekoduj_jpeg API PASS\n");

    int cases = 0;
    auto verify = [&](const char* name, const std::vector<uint8_t>& sample) {
        if (!bad(sample)) { std::fprintf(stderr, "%s accepted\n", name); std::exit(1); }
        ++cases;
    };
    // Phase 14 required security cases
    std::vector<uint8_t> empty{}; verify("empty file", empty);
    auto m = d; m.resize(100); verify("truncated", m);
    m = d; m[0] = 0; verify("missing SOI", m);
    m = d; m[m.size()-1] = 0; verify("missing EOI", m);
    m = d; m[4] = 0; m[5] = 1; verify("short marker", m);
    m = d; m.resize(m.size() - 1); m.push_back(0xff); verify("truncated marker", m);
    m = d; m[19716] = 0; m[19717] = 1; verify("invalid segment length < 2", m);
    m = d; m[19716] = 0xff; m[19717] = 0xff; verify("segment length beyond file", m);
    m = d; m[19853] = 0; m[19854] = 0; verify("zero height", m);
    m = d; m[19855] = 0; m[19856] = 0; verify("zero width", m);
    m = d; m[19853] = 0x20; m[19854] = 0x01; verify("huge height > 8192", m);
    m = d; m[19855] = 0x20; m[19856] = 0x01; verify("huge width > 8192", m);
    m = d; m[19853] = 0x10; m[19854] = 0x00; m[19855] = 0x10; m[19856] = 0x00; verify("width*height overflow", m);
    m = d; m.resize(19725); verify("truncated DQT", m);
    m = d; m.resize(19875); verify("truncated DHT", m);
    m = d; m[19718] = 0x10; verify("unsupported quantization precision", m);
    m = d; m[19720] = 0; verify("invalid quantization table", m);
    m = d; m[19870] = 0x20; verify("invalid Huffman table class", m);
    m = d; m[19888] = 12; verify("invalid Huffman symbol", m);
    m = d; m[20142] = 0; verify("invalid SOS zero components", m);
    m = d; m[20142] = 4; verify("invalid SOS too many components", m);
    m = d; m[20143] = 99; verify("invalid SOS bad component id", m);
    m = d; m[19858] = 0; verify("zero component id in SOF", m);
    m = d; m[19859] = 0x33; verify("bad MCU layout", m);
    m = d; m[19859] = 0x44; verify("malformed MCU sampling", m);
    m = d; m.insert(m.begin() + 19848, d.begin() + 19714, d.begin() + 19848); verify("duplicate table", m);
    m = d; m.insert(m.begin() + 20139, d.begin() + 19867, d.begin() + 20139); verify("duplicate Huffman table", m);
    m = d; m[19849] = 0xc9; verify("unsupported arithmetic JPEG", m);
    m = d; m[19850] = 0xc1; verify("unsupported sequential SOF1", m);
    m = d; m[19850] = 0xc3; verify("unsupported lossless SOF3", m);
    m = d; m.erase(m.begin() + 20000, m.begin() + 22000); verify("truncated entropy", m);
    m = d; m[20150] = 0xff; m[20151] = 0xd1; verify("bad restart", m);
    // Random bytes tests
    uint32_t rng = 0x12345678;
    auto next_rnd = [&]() -> uint8_t {
        rng = rng * 1103515245 + 12345;
        return static_cast<uint8_t>((rng >> 16) & 0xff);
    };
    for (size_t rsize : {16, 64, 256, 1024, 4096}) {
        std::vector<uint8_t> rnd(rsize);
        for (size_t k = 0; k < rsize; ++k) rnd[k] = next_rnd();
        verify("random bytes", rnd);
        // Random bytes with valid SOI
        if (rsize >= 4) {
            rnd[0] = 0xff; rnd[1] = 0xd8;
            verify("random bytes with SOI", rnd);
        }
    }
    uint8_t tiny = 0xff;
    if (dekoduj_obraz(&tiny, SIZE_MAX, &o, alloc, freemem) != BladObrazu::ZaDuzy)
        { std::fprintf(stderr, "size overflow accepted\n"); return 1; }
    ++cases;
    std::printf("malformed JPEG PASS %d cases\n", cases);
    if (active_bytes != 0) { std::fprintf(stderr, "decoder leak %zu bytes\n", active_bytes); return 1; }
    return 0;
}
