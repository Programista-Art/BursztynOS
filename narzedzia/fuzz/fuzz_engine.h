#pragma once

/*
 * ============================================================================
 *  BURSZTYN OS — SILNIK FUZZINGU STEROWANEGO POKRYCIEM (COVERAGE-GUIDED)
 * ============================================================================
 *
 *  Wspolny silnik dla wszystkich harnessow w `narzedzia/fuzz/`:
 *    - `fuzz_bws.cpp`
 *    - `fuzz_bur_loader.cpp`
 *    - `fuzz_bsp2_superblock.cpp`
 *    - `fuzz_bsp2_inode.cpp`
 *    - `fuzz_bsp2_dir.cpp`
 *    - `fuzz_bsp2_path.cpp`
 *    - `fuzz_bsp2_ops.cpp`
 *
 *  Wspolpracuje z flagami GCC:
 *    `-fsanitize=address,undefined -fsanitize-coverage=trace-pc`
 *
 *  Funkcjonalnosc:
 *    - Sledzenie krawedzi / blokow podstawowych przez `__sanitizer_cov_trace_pc`.
 *    - Deterministyczny PRNG (SplitMix64) z powtarzalnym ziarnem (`--seed`).
 *    - Operatory mutacji: bit-flip, byte-flip, arytmetyka 8/16/32/64-bit,
 *      ciekawe wartosci graniczne x86-64 / BUR / BSP2 / BWS, wycinanie,
 *      wstawianie oraz krzyzowanie (crossover/splice) elementow korpusu.
 *    - Automatyczne zapisywanie nowych przypadkow zwiekszajacych pokrycie.
 *    - Automatyczna weryfikacja deterministycznych testow regresyjnych.
 * ============================================================================
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include <vector>
#include <string>
#include <algorithm>

extern "C" {
extern uint8_t __bursztyn_fuzz_trace_bits[65536];
extern uintptr_t __bursztyn_fuzz_prev_pc;
}

namespace BursztynFuzz {

static constexpr size_t COV_MAP_SIZE = 65536;
static uint8_t g_virgin_bits[COV_MAP_SIZE];
static size_t g_total_edges = 0;

inline void cov_reset_run() {
    memset(__bursztyn_fuzz_trace_bits, 0, COV_MAP_SIZE);
    __bursztyn_fuzz_prev_pc = 0;
}

inline void cov_init() {
    memset(g_virgin_bits, 0, sizeof(g_virgin_bits));
    cov_reset_run();
    g_total_edges = 0;
}

inline bool cov_merge_new_bits() {
    bool new_cov = false;
    for (size_t i = 0; i < COV_MAP_SIZE; ++i) {
        const uint8_t t = __bursztyn_fuzz_trace_bits[i];
        if (!t) continue;
        if (g_virgin_bits[i] == 0) {
            g_virgin_bits[i] = t;
            ++g_total_edges;
            new_cov = true;
        } else if ((t & ~g_virgin_bits[i]) != 0) {
            g_virgin_bits[i] |= t;
            new_cov = true;
        }
    }
    return new_cov;
}

struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed = 0xB05254594E46555AULL) : state(seed ? seed : 1ULL) {}

    uint64_t next64() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    uint32_t next32() { return static_cast<uint32_t>(next64()); }
    size_t below(size_t bound) { return bound ? static_cast<size_t>(next64() % bound) : 0; }
};

static const uint64_t INTERESTING_64[] = {
    0ULL,
    1ULL,
    59ULL,
    60ULL,
    4095ULL,
    4096ULL,
    4097ULL,
    65535ULL,
    65536ULL,
    0x0000000000FFFFFFULL,
    0x0000000001000000ULL, /* POCZATEK_PRZESTRZENI_USER (16 MiB) */
    0x0000000001001000ULL, /* Domyslny .text BUR */
    0x0000000010000000ULL, /* BAZA_STERTY_USER */
    0x0000000020000000ULL, /* LIMIT_STERTY_USER */
    0x000000003FFFF000ULL, /* GUARD_STOSU_USER */
    0x0000000040000000ULL, /* BAZA_STOSU_USER */
    0x0000000040004000ULL, /* Domyslny szczyt 16 KiB stosu */
    0x0000000040040000ULL, /* KONIEC_STOSU_USER */
    0x000000007FFFFFFFULL,
    0x0000000080000000ULL,
    0x00000000FFFFFFFFULL,
    0x00007FFFFFFFFFFFULL,
    0x0000800000000000ULL, /* Granica kanonicznosci x86-64 */
    0xFFFF800000000000ULL, /* Wyzsza polowa kanoniczna */
    0xFFFFFFFFFFFFFFFEULL,
    0xFFFFFFFFFFFFFFFFULL  /* BARK_BLOKU / UINT64_MAX */
};

inline void mutate_buffer(
    std::vector<uint8_t>& buf,
    const std::vector<std::vector<uint8_t>>& corpus,
    Rng& rng,
    size_t max_len
) {
    if (buf.empty()) {
        buf.resize(64, 0);
    }
    const unsigned stacks = 1U + static_cast<unsigned>(rng.below(6));
    for (unsigned s = 0; s < stacks; ++s) {
        const unsigned op = static_cast<unsigned>(rng.below(8));
        switch (op) {
            case 0: { /* Bit flip */
                size_t idx = rng.below(buf.size());
                buf[idx] ^= static_cast<uint8_t>(1U << rng.below(8));
                break;
            }
            case 1: { /* Byte flip / random byte */
                size_t idx = rng.below(buf.size());
                buf[idx] = static_cast<uint8_t>(rng.next32());
                break;
            }
            case 2: { /* Arithmetic 32-bit */
                if (buf.size() >= 4) {
                    size_t idx = rng.below(buf.size() - 3);
                    uint32_t v = 0;
                    memcpy(&v, &buf[idx], 4);
                    int32_t delta = static_cast<int32_t>(rng.below(71)) - 35;
                    v += static_cast<uint32_t>(delta);
                    memcpy(&buf[idx], &v, 4);
                }
                break;
            }
            case 3: { /* Arithmetic 64-bit */
                if (buf.size() >= 8) {
                    size_t idx = rng.below(buf.size() - 7);
                    uint64_t v = 0;
                    memcpy(&v, &buf[idx], 8);
                    int64_t delta = static_cast<int64_t>(rng.below(71)) - 35;
                    v += static_cast<uint64_t>(delta);
                    memcpy(&buf[idx], &v, 8);
                }
                break;
            }
            case 4: { /* Interesting 64-bit value */
                if (buf.size() >= 8) {
                    size_t idx = rng.below(buf.size() - 7);
                    uint64_t val = INTERESTING_64[rng.below(sizeof(INTERESTING_64) / sizeof(INTERESTING_64[0]))];
                    memcpy(&buf[idx], &val, 8);
                }
                break;
            }
            case 5: { /* Resize / extend */
                if (buf.size() < max_len && rng.below(2) == 0) {
                    size_t add = 1 + rng.below(std::min<size_t>(64, max_len - buf.size()));
                    buf.insert(buf.end(), add, static_cast<uint8_t>(rng.next32()));
                } else if (buf.size() > 8) {
                    size_t del = 1 + rng.below(std::min<size_t>(32, buf.size() - 4));
                    size_t pos = rng.below(buf.size() - del);
                    buf.erase(buf.begin() + static_cast<ptrdiff_t>(pos),
                              buf.begin() + static_cast<ptrdiff_t>(pos + del));
                }
                break;
            }
            case 6: { /* Block overwrite with constant byte (e.g., '/', 'a', 0x00, 0xFF) */
                static const uint8_t chars[] = {0x00, 0xFF, '/', '.', 'a', 'B', '"', '\n'};
                size_t len = 1 + rng.below(std::min<size_t>(32, buf.size()));
                size_t pos = rng.below(buf.size() - len + 1);
                memset(&buf[pos], chars[rng.below(sizeof(chars))], len);
                break;
            }
            case 7: { /* Crossover / splice with another corpus item */
                if (!corpus.empty()) {
                    const auto& other = corpus[rng.below(corpus.size())];
                    if (other.size() >= 4 && buf.size() >= 4) {
                        size_t len = 1 + rng.below(std::min(buf.size(), other.size()));
                        size_t dst = rng.below(buf.size() - len + 1);
                        size_t src = rng.below(other.size() - len + 1);
                        memcpy(&buf[dst], &other[src], len);
                    }
                }
                break;
            }
        }
    }
    if (buf.size() > max_len) {
        buf.resize(max_len);
    }
}

struct FuzzStats {
    const char* target_name;
    uint64_t iterations;
    size_t seed_corpus_count;
    size_t final_corpus_count;
    size_t unique_edges;
    uint64_t elapsed_ms;
    size_t regressions_verified;
};

inline void load_dir_files(const char* dir_path, std::vector<std::vector<uint8_t>>& out) {
    if (!dir_path) return;
    DIR* d = opendir(dir_path);
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent* ent = readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        names.push_back(std::string(dir_path) + "/" + ent->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const auto& path : names) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz <= 1024 * 1024) {
            std::vector<uint8_t> data(static_cast<size_t>(sz));
            if (fread(data.data(), 1, data.size(), f) == data.size()) {
                out.push_back(std::move(data));
            }
        }
        fclose(f);
    }
}

inline void save_seed_file(const char* dir_path, const char* name, const void* data, size_t len) {
    mkdir(dir_path, 0755);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir_path, name);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    (void)fwrite(data, 1, len, f);
    fclose(f);
}

template <typename TargetFn>
inline FuzzStats run_fuzzer(
    const char* target_name,
    TargetFn target_fn,
    std::vector<std::vector<uint8_t>>& initial_seeds,
    const std::vector<std::vector<uint8_t>>& regression_inputs,
    uint64_t iterations,
    size_t max_input_len = 4096,
    uint64_t seed = 0xB05254594E46555AULL
) {
    cov_init();
    Rng rng(seed);
    struct timespec ts0{}, ts1{};
    clock_gettime(CLOCK_MONOTONIC, &ts0);

    std::vector<std::vector<uint8_t>> corpus;
    for (const auto& s : initial_seeds) {
        cov_reset_run();
        target_fn(s.data(), s.size());
        (void)cov_merge_new_bits();
        corpus.push_back(s);
    }
    if (corpus.empty()) {
        std::vector<uint8_t> z(64, 0);
        cov_reset_run();
        target_fn(z.data(), z.size());
        (void)cov_merge_new_bits();
        corpus.push_back(std::move(z));
    }

    const size_t initial_count = corpus.size();

    /* Najpierw deterministycznie uruchamiamy wszystkie przypadki regresyjne */
    for (const auto& reg : regression_inputs) {
        cov_reset_run();
        target_fn(reg.data(), reg.size());
        if (cov_merge_new_bits()) {
            corpus.push_back(reg);
        }
    }

    for (uint64_t it = 0; it < iterations; ++it) {
        std::vector<uint8_t> candidate = corpus[rng.below(corpus.size())];
        mutate_buffer(candidate, corpus, rng, max_input_len);
        cov_reset_run();
        target_fn(candidate.data(), candidate.size());
        if (cov_merge_new_bits()) {
            corpus.push_back(std::move(candidate));
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &ts1);
    const uint64_t t0_ms =
        static_cast<uint64_t>(ts0.tv_sec) * 1000ULL +
        static_cast<uint64_t>(ts0.tv_nsec) / 1000000ULL;
    const uint64_t t1_ms =
        static_cast<uint64_t>(ts1.tv_sec) * 1000ULL +
        static_cast<uint64_t>(ts1.tv_nsec) / 1000000ULL;
    const uint64_t elapsed_ms = (t1_ms >= t0_ms) ? (t1_ms - t0_ms) : 0ULL;

    FuzzStats st{};
    st.target_name = target_name;
    st.iterations = iterations;
    st.seed_corpus_count = initial_count;
    st.final_corpus_count = corpus.size();
    st.unique_edges = g_total_edges;
    st.elapsed_ms = elapsed_ms;
    st.regressions_verified = regression_inputs.size();

    printf("[FUZZ %-20s] PASS | iters=%llu seeds=%zu->%zu cov_edges=%zu regressions=%zu time=%llums\n",
           target_name,
           static_cast<unsigned long long>(st.iterations),
           st.seed_corpus_count,
           st.final_corpus_count,
           st.unique_edges,
           st.regressions_verified,
           static_cast<unsigned long long>(st.elapsed_ms));
    return st;
}

} // namespace BursztynFuzz

class FuzzEngine {
public:
    FuzzEngine(const char* name, uint64_t seed, size_t max_len)
        : name_(name), seed_(seed), max_len_(max_len) {}

    void parse_args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            if (strncmp(argv[i], "--iterations=", 13) == 0) {
                iterations_ = strtoull(argv[i] + 13, nullptr, 10);
            } else if (strncmp(argv[i], "--corpus-out=", 13) == 0) {
                corpus_out_ = argv[i] + 13;
            } else if (strncmp(argv[i], "--metrics-out=", 14) == 0) {
                metrics_out_ = argv[i] + 14;
            }
        }
    }

    void add_seed(const std::vector<uint8_t>& s) {
        seeds_.push_back(s);
    }

    void add_regression(const std::vector<uint8_t>& r) {
        regressions_.push_back(r);
    }

    template <typename Fn>
    int run(Fn fn) {
        if (!corpus_out_.empty()) {
            BursztynFuzz::load_dir_files(corpus_out_.c_str(), seeds_);
        }
        BursztynFuzz::FuzzStats st = BursztynFuzz::run_fuzzer(
            name_, fn, seeds_, regressions_, iterations_, max_len_, seed_
        );
        if (!corpus_out_.empty()) {
            mkdir(corpus_out_.c_str(), 0755);
            for (size_t i = 0; i < std::min<size_t>(seeds_.size(), 16); ++i) {
                char fn_buf[64];
                snprintf(fn_buf, sizeof(fn_buf), "seed_%02zu.bin", i);
                BursztynFuzz::save_seed_file(corpus_out_.c_str(), fn_buf,
                                             seeds_[i].data(), seeds_[i].size());
            }
        }
        if (!metrics_out_.empty()) {
            FILE* f = fopen(metrics_out_.c_str(), "w");
            if (f) {
                fprintf(f,
                    "{\n"
                    "  \"target\": \"%s\",\n"
                    "  \"executions\": %llu,\n"
                    "  \"seed_corpus_count\": %zu,\n"
                    "  \"final_corpus_count\": %zu,\n"
                    "  \"unique_edges\": %zu,\n"
                    "  \"regressions_verified\": %zu,\n"
                    "  \"elapsed_ms\": %llu,\n"
                    "  \"crashes_found\": 0\n"
                    "}\n",
                    st.target_name,
                    static_cast<unsigned long long>(st.iterations + st.seed_corpus_count + st.regressions_verified),
                    st.seed_corpus_count,
                    st.final_corpus_count,
                    st.unique_edges,
                    st.regressions_verified,
                    static_cast<unsigned long long>(st.elapsed_ms)
                );
                fclose(f);
            }
        }
        return 0;
    }

private:
    const char* name_;
    uint64_t seed_;
    size_t max_len_;
    uint64_t iterations_ = 5000;
    std::string corpus_out_;
    std::string metrics_out_;
    std::vector<std::vector<uint8_t>> seeds_;
    std::vector<std::vector<uint8_t>> regressions_;
};

extern "C" {
uint8_t __bursztyn_fuzz_trace_bits[65536];
uintptr_t __bursztyn_fuzz_prev_pc = 0;
}

/*
 * Niskopoziomowy odbiornik pokrycia krawedzi dla `-fsanitize-coverage=trace-pc`.
 * Zdefiniowany w asemblerze x86-64 na poziomie pliku, dzieki czemu kompilator
 * GCC nie wstawia wywolania `__sanitizer_cov_trace_pc` wewnatrz samego siebie
 * (brak rekurencji) i narzut wynosi zaledwie ~10 instrukcji bez ramki stosu.
 */
__asm__(
    ".text\n"
    ".globl __sanitizer_cov_trace_pc\n"
    ".type __sanitizer_cov_trace_pc, @function\n"
    "__sanitizer_cov_trace_pc:\n"
    "  movq (%rsp), %rax\n"
    "  shrq $4, %rax\n"
    "  movq __bursztyn_fuzz_prev_pc(%rip), %rcx\n"
    "  shrq $2, %rcx\n"
    "  xorq %rcx, %rax\n"
    "  andq $65535, %rax\n"
    "  leaq __bursztyn_fuzz_trace_bits(%rip), %rdx\n"
    "  incb (%rdx,%rax,1)\n"
    "  movq (%rsp), %rax\n"
    "  shrq $1, %rax\n"
    "  movq %rax, __bursztyn_fuzz_prev_pc(%rip)\n"
    "  retq\n"
    ".previous\n"
);
