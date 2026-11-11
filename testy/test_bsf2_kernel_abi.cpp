// Boundary and negative tests for the kernel-side pieces of BSF 2.0
// stabilization:
//   * BWS 51 (bws_czcionka_kopiuj): privilege, exact sizes, null/overflowing
//     pointers, unwritable ranges, partial-copy faults, header-last publish,
//     ABI of commands 0/1 unchanged, unknown commands rejected.
//   * Ring 3 blob validation (BSF::unicode_layout_valid), incl. content hash.
//   * Desktop BMP Multiboot2 module validation (obrazy_startowe::sprawdz_bmp).
// The production handler source is compiled directly; only the two Ring 0
// user-copy primitives are replaced by a simulated user address space.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

// ---- Simulated Ring 3 address space -------------------------------------
namespace sim {
uint8_t *base = nullptr;   // writable user range [base, base + size)
size_t size = 0;
long fail_on_call = -1;    // 0-based index of the copy call that faults
size_t fail_after = 0;     // bytes written before the injected fault
long calls = 0;
bool range_ok(const void *p, size_t n) {
  auto a = reinterpret_cast<uintptr_t>(p), b = reinterpret_cast<uintptr_t>(base);
  return p && a >= b && n <= size && a - b <= size - n;
}
} // namespace sim
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void *p, size_t n) {
  return sim::range_ok(p, n);
}
bool skopiuj_do_przestrzeni_uzytkownika(void *dst, const void *src, size_t n) {
  long call = sim::calls++;
  if (!sim::range_ok(dst, n))
    return false;
  if (call == sim::fail_on_call) { // partial copy, then fault
    std::memcpy(dst, src, sim::fail_after < n ? sim::fail_after : n);
    return false;
  }
  std::memcpy(dst, src, n);
  return true;
}

#include "../jadro/gui/czcionka_systemowa.cpp"
#include "../bsf/unicode_layout.h"
#include "../jadro/rdzen/obrazy_startowe.h"

static unsigned checks;
#define CHECK(x)                                                              \
  do {                                                                        \
    ++checks;                                                                 \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);       \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

static uint64_t addr(const void *p) { return reinterpret_cast<uint64_t>(p); }

static void reset(std::vector<uint8_t> &mem, uint8_t fill) {
  std::memset(mem.data(), fill, mem.size());
  sim::base = mem.data();
  sim::size = mem.size();
  sim::fail_on_call = -1;
  sim::calls = 0;
}

static void test_bws51() {
  using namespace BSF;
  constexpr uint64_t N = UnicodeBlob::Size;
  constexpr uint64_t F = sizeof(BwsCzcionkaSystemowa);
  std::vector<uint8_t> mem(N + F + 8192);
  uint8_t *out = mem.data() + 4096;
  const uint64_t all = ~uint64_t(0);

  // Command 0: legacy height, requires PRAWO_GUI, ignores buffer args.
  reset(mem, 0);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 0, 0, 0) == 16);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 0, 0xdead, 7) == 16);
  CHECK(bws_czcionka_kopiuj(0, 0, 0, 0) == 0);
  CHECK(bws_czcionka_kopiuj(all & ~PRAWO_GUI, 0, 0, 0) == 0);

  // Unknown commands are rejected without touching memory.
  for (uint64_t cmd : {uint64_t(3), uint64_t(4), uint64_t(255), uint64_t(1) << 32, all}) {
    reset(mem, 0xAA);
    CHECK(bws_czcionka_kopiuj(PRAWO_GUI, cmd, addr(out), N) == 0);
    CHECK(sim::calls == 0 && out[0] == 0xAA);
  }

  // Command 2 success: exact bytes, header written last.
  reset(mem, 0xAA);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(out), N) == 1);
  CHECK(std::memcmp(out, bsf_unicode_layout_blob, N) == 0);
  CHECK(out[-1] == 0xAA && out[N] == 0xAA); // no write outside the buffer
  CHECK(unicode_layout_valid(out, N));
  CHECK(sim::calls == 2);

  // Without PRAWO_GUI nothing is copied, even with every other right.
  reset(mem, 0xAA);
  CHECK(bws_czcionka_kopiuj(all & ~PRAWO_GUI, 2, addr(out), N) == 0);
  CHECK(sim::calls == 0 && out[0] == 0xAA);

  // Exact size only (no short/long/zero/huge sizes).
  for (uint64_t n : {uint64_t(0), uint64_t(1), uint64_t(16), N - 1, N + 1, N * 2,
                     uint64_t(UINT32_MAX), all}) {
    reset(mem, 0xAA);
    CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(out), n) == 0);
    CHECK(sim::calls == 0 && out[0] == 0xAA);
  }
  // Null and wrapping pointers.
  reset(mem, 0xAA);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, 0, N) == 0);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, all - N + 2, N) == 0);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, all, N) == 0);
  CHECK(sim::calls == 0);
  // Buffer straddling the end of user memory / starting before it.
  reset(mem, 0xAA);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(mem.data() + mem.size() - N + 1), N) == 0);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(mem.data()) - 1, N) == 0);
  CHECK(sim::calls == 0);
  // Exactly at the end boundary is accepted.
  reset(mem, 0xAA);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(mem.data() + mem.size() - N), N) == 1);

  // Partial-copy faults: the call fails and the header is never published,
  // so Ring 3 validation rejects the buffer.
  for (size_t after : {size_t(0), size_t(1), size_t(4095), size_t(N / 2), size_t(N - 17)}) {
    reset(mem, 0);
    sim::fail_on_call = 0; // body copy faults
    sim::fail_after = after;
    CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(out), N) == 0);
    CHECK(sim::calls == 1);
    CHECK(out[0] == 0 && out[15] == 0); // header untouched
    CHECK(!unicode_layout_valid(out, N));
  }
  reset(mem, 0);
  sim::fail_on_call = 1; // header copy faults after 8 bytes
  sim::fail_after = 8;
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(out), N) == 0);
  CHECK(!unicode_layout_valid(out, N));

  // Command 1 (system font) ABI unchanged.
  reset(mem, 0xAA);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 1, addr(out), F) == 1);
  BwsCzcionkaSystemowa font{};
  std::memcpy(&font, out, F);
  CHECK(font.wersja == BWS_CZCIONKA_WERSJA && font.rozmiar == F &&
        font.liczba_glifow == BWS_CZCIONKA_GLIFY && font.wysokosc == 16);
  CHECK(std::memcmp(font.szerokosci, nowa_czcionka_szerokosci, sizeof font.szerokosci) == 0);
  CHECK(out[F] == 0xAA);
  for (uint64_t n : {F - 1, F + 1, N, uint64_t(0)}) {
    reset(mem, 0xAA);
    CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 1, addr(out), n) == 0);
    CHECK(sim::calls == 0);
  }
  reset(mem, 0);
  sim::fail_on_call = 1;
  sim::fail_after = 100;
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 1, addr(out), F) == 0);
  CHECK(reinterpret_cast<uint32_t *>(out)[0] == 0); // header not published
  // A blob-sized request to command 1 and a font-sized one to command 2 fail.
  reset(mem, 0xAA);
  CHECK(bws_czcionka_kopiuj(PRAWO_GUI, 2, addr(out), F) == 0);
}

static void test_blob_validation() {
  using namespace BSF;
  constexpr size_t N = UnicodeBlob::Size;
  std::vector<uint8_t> b(bsf_unicode_layout_blob, bsf_unicode_layout_blob + N);
  CHECK(unicode_layout_valid(b.data(), N));
  CHECK(!unicode_layout_valid(nullptr, N));
  CHECK(!unicode_layout_valid(b.data(), N - 1));
  // Any single-byte change anywhere (header, directory, tables) is rejected.
  for (size_t i = 0; i < N; i += (i < 256 ? 1 : 97)) {
    b[i] ^= 0x01;
    CHECK(!unicode_layout_valid(b.data(), N));
    b[i] ^= 0x01;
  }
  b[N - 1] ^= 0x80;
  CHECK(!unicode_layout_valid(b.data(), N));
  b[N - 1] ^= 0x80;
  CHECK(unicode_layout_valid(b.data(), N));
}

static std::vector<uint8_t> read_file(const char *p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
static void put32(std::vector<uint8_t> &d, size_t o, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    d[o + i] = uint8_t(v >> (8 * i));
}
static void put16(std::vector<uint8_t> &d, size_t o, uint16_t v) {
  d[o] = uint8_t(v);
  d[o + 1] = uint8_t(v >> 8);
}
static std::vector<uint8_t> make_bmp(int32_t w, int32_t h, uint16_t bpp) {
  uint64_t row = ((uint64_t(w < 0 ? 0 : w) * bpp + 31) / 32) * 4;
  uint64_t rows = h < 0 ? uint64_t(-int64_t(h)) : uint64_t(h);
  std::vector<uint8_t> d(54 + row * rows, 0);
  d[0] = 'B';
  d[1] = 'M';
  put32(d, 2, uint32_t(d.size()));
  put32(d, 10, 54);
  put32(d, 14, 40);
  put32(d, 18, uint32_t(w));
  put32(d, 22, uint32_t(h));
  put16(d, 26, 1);
  put16(d, 28, bpp);
  return d;
}

static void test_bmp_modules() {
  using namespace obrazy_startowe;
  // GRUB places modules at physical addresses; emulate one at 32 MiB.
  const uint64_t at = 32ULL << 20;
  auto check = [&](const std::vector<uint8_t> &d) {
    return sprawdz_bmp(d.data(), at, at + d.size());
  };
  for (const char *p : {"obrazy/test.bmp", "obrazy/test1.bmp"}) {
    auto d = read_file(p);
    CHECK(!d.empty());
    CHECK(check(d) == Wynik::Ok);
    // Truncated module (GRUB end shorter than the file): bfSize mismatch.
    CHECK(sprawdz_bmp(d.data(), at, at + d.size() - 1) == Wynik::NiezgodnyRozmiar);
    auto e = d;
    e[0] = 'X';
    CHECK(check(e) == Wynik::BrakSygnatury);
  }
  auto ok = make_bmp(3, 2, 24);
  CHECK(check(ok) == Wynik::Ok);
  CHECK(check(make_bmp(7, -5, 24)) == Wynik::Ok); // top-down
  for (uint16_t bpp : {1, 4, 8, 16, 32})
    CHECK(check(make_bmp(9, 3, bpp)) == Wynik::Ok);
  // Address range checks.
  CHECK(sprawdz_bmp(ok.data(), 0, ok.size()) == Wynik::ZlyZakres);
  CHECK(sprawdz_bmp(ok.data(), at, at) == Wynik::ZlyZakres);
  CHECK(sprawdz_bmp(ok.data(), at + 10, at) == Wynik::ZlyZakres);
  CHECK(sprawdz_bmp(nullptr, at, at + ok.size()) == Wynik::ZlyZakres);
  CHECK(sprawdz_bmp(ok.data(), 0xFFFFFFFFULL, 0x100000000ULL + ok.size()) == Wynik::ZlyZakres);
  // Size limits (checked before any byte is read beyond the header).
  CHECK(sprawdz_bmp(ok.data(), at, at + 53) == Wynik::ZlyRozmiar);
  CHECK(sprawdz_bmp(ok.data(), at, at + MaksRozmiar + 1) == Wynik::ZlyRozmiar);
  {
    // Exactly MaksRozmiar is accepted when self-consistent.
    auto big = make_bmp(1024, 2048, 32); // 8 MiB + 54
    big.resize(MaksRozmiar);
    put32(big, 2, uint32_t(big.size()));
    put32(big, 22, uint32_t((MaksRozmiar - 54) / 4096));
    CHECK(check(big) == Wynik::Ok);
  }
  auto mut = [&](auto fn) {
    auto d = ok;
    fn(d);
    return check(d);
  };
  CHECK(mut([](auto &d) { put32(d, 2, uint32_t(d.size() + 1)); }) == Wynik::NiezgodnyRozmiar);
  CHECK(mut([](auto &d) { put32(d, 2, 0); }) == Wynik::NiezgodnyRozmiar);
  CHECK(mut([](auto &d) { put32(d, 14, 12); }) == Wynik::ZlyNaglowek);   // OS/2 core header
  CHECK(mut([](auto &d) { put32(d, 14, 0xFFFFFFF0u); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put32(d, 10, uint32_t(d.size())); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put32(d, 10, 20); }) == Wynik::ZlyNaglowek);  // inside header
  CHECK(mut([](auto &d) { put32(d, 18, 0); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put32(d, 18, 0x80000000u); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put32(d, 22, 0); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put32(d, 22, 0x80000000u); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put16(d, 26, 2); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put16(d, 28, 7); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put16(d, 28, 0); }) == Wynik::ZlyNaglowek);
  CHECK(mut([](auto &d) { put32(d, 30, 4); }) == Wynik::ZlyNaglowek);  // JPEG
  CHECK(mut([](auto &d) { put32(d, 30, 1); }) == Wynik::Ok);           // RLE8
  // Dimensions claiming more pixels than the file holds.
  CHECK(mut([](auto &d) { put32(d, 22, 3); }) == Wynik::ZaMaloDanych);
  CHECK(mut([](auto &d) { put32(d, 18, 0x7FFFFFFF); }) == Wynik::ZaMaloDanych);
  CHECK(mut([](auto &d) { put32(d, 22, 0x7FFFFFFF); put32(d, 18, 0x7FFFFFFF);
                          put16(d, 28, 32); }) == Wynik::ZaMaloDanych);
  // Deterministic mutation sweep: every result is a defined verdict and
  // no read leaves the buffer (ASan).
  uint32_t seed = 12345;
  for (int round = 0; round < 20000; ++round) {
    auto d = ok;
    unsigned edits = 1 + (seed % 4);
    for (unsigned k = 0; k < edits; ++k) {
      seed = seed * 1103515245u + 12345u;
      d[(seed >> 8) % 54] = uint8_t(seed >> 24);
    }
    seed = seed * 1103515245u + 12345u;
    size_t len = (seed >> 9) % (d.size() + 1);
    std::vector<uint8_t> cut(d.begin(), d.begin() + long(len));
    Wynik w = cut.empty() ? sprawdz_bmp(nullptr, at, at + 1)
                          : sprawdz_bmp(cut.data(), at, at + cut.size());
    CHECK(unsigned(w) <= unsigned(Wynik::ZaMaloDanych));
    CHECK(opis(w)[0] != '?');
  }
}

int main() {
  test_bws51();
  test_blob_validation();
  test_bmp_modules();
  std::printf("BSF2 kernel ABI/boundary: %u checks PASS (BWS 51 cmds 0/1/2, "
              "blob content hash, desktop BMP module validation)\n", checks);
  return 0;
}
