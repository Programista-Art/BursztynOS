// Kernel compressed-blob decoder (jadro/rdzen/blob_skompresowany.h) vs zlib:
// every real application binary, random data, all block types, and
// corrupted / truncated / mislabelled inputs (ASan + UBSan).
#include "../jadro/rdzen/blob_skompresowany.h"
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>
#include <dirent.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#x);abort();}}while(0)
static uint32_t crc(const uint8_t* p, uint32_t n) { return uint32_t(crc32(0, p, n)); }
static std::vector<uint8_t> pack(const std::vector<uint8_t>& raw, int level, int strategy = Z_DEFAULT_STRATEGY) {
  std::vector<uint8_t> z(compressBound(raw.size()) + 64);
  z_stream s{}; deflateInit2(&s, level, Z_DEFLATED, 15, 9, strategy);
  s.next_in = const_cast<uint8_t*>(raw.data()); s.avail_in = uInt(raw.size());
  s.next_out = z.data(); s.avail_out = uInt(z.size());
  CHECK(deflate(&s, Z_FINISH) == Z_STREAM_END); z.resize(s.total_out); deflateEnd(&s);
  std::vector<uint8_t> out = {'B', 'Z', 'B', '1'};
  uint32_t n = uint32_t(raw.size()), c = crc(raw.data(), n);
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(n >> (8 * i)));
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(c >> (8 * i)));
  out.insert(out.end(), z.begin(), z.end());
  return out;
}
static bool unpack(const std::vector<uint8_t>& b, std::vector<uint8_t>& out) {
  uint32_t n = 0;
  if (!BlobZ::rozmiar_surowy(b.data(), b.data() + b.size(), &n)) return false;
  out.assign(n, 0);
  static BlobZ::Strumien z;
  return BlobZ::rozpakuj(z, b.data(), b.data() + b.size(), out.data(), n);
}
// Chunked decoding exactly like kernel_instaluj_system_step: 4 KiB targets,
// zakoncz() on the last chunk; every prefix must already be final.
static bool unpack_chunked(const std::vector<uint8_t>& b, std::vector<uint8_t>& out, const std::vector<uint8_t>* raw,
                           size_t chunk = 4096) {
  uint32_t n = 0;
  if (!BlobZ::rozmiar_surowy(b.data(), b.data() + b.size(), &n)) return false;
  out.assign(n, 0);
  static BlobZ::Strumien z;
  if (!BlobZ::otworz(z, b.data(), b.data() + b.size(), out.data(), n)) return false;
  for (size_t off = 0; off < n; off += chunk) {
    size_t m = n - off < chunk ? n - off : chunk;
    bool last = off + m == n;
    if (!(last ? BlobZ::zakoncz(z) : BlobZ::dalej(z, off + m))) return false;
    if (z.s.dl < off + m) return false;
    // Soft target: at most one match (258 B) or one stored block past it.
    if (!last && z.s.dl > off + m + 65535) return false;
    if (raw && std::memcmp(out.data() + off, raw->data() + off, m)) { fprintf(stderr, "chunk mismatch at %zu\n", off); abort(); }
  }
  return true;
}
int main() {
  unsigned apps = 0, linked_apps = 0; size_t raw_total = 0, packed_total = 0, linked_total = 0;
  if (DIR* d = opendir(".")) {
    while (dirent* e = readdir(d)) {
      std::string name = e->d_name;
      if (name.size() < 5 || name.substr(name.size() - 4) != ".bin" || name == "system_operacyjny.bin") continue;
      std::ifstream f(name, std::ios::binary);
      std::vector<uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
      if (raw.empty() || raw.size() > BlobZ::MAKS_ROZMIAR) continue;
      auto b = pack(raw, 9);
      std::vector<uint8_t> out;
      CHECK(unpack(b, out) && out == raw);
      ++apps; raw_total += raw.size(); packed_total += b.size();
      // The exact stream linked into the kernel (narzedzia/kompresuj_blob.py).
      std::ifstream fz(name + ".bz", std::ios::binary);
      std::vector<uint8_t> linked{std::istreambuf_iterator<char>(fz), {}};
      if (!linked.empty()) {
        CHECK(unpack(linked, out) && out == raw);
        CHECK(unpack_chunked(linked, out, &raw) && out == raw);
        ++linked_apps; linked_total += linked.size();
      }
    }
    closedir(d);
  }
  printf("  %u application binaries: %zu -> %zu bytes, all decoded exactly\n", apps, raw_total, packed_total);
  printf("  %u linked kernel blobs (*.bin.bz, %zu bytes) decode to their *.bin exactly\n", linked_apps, linked_total);
  CHECK(linked_apps > 0);
  std::mt19937 rng(77);
  for (int level : {0, 1, 6, 9})
    for (int strategy : {Z_DEFAULT_STRATEGY, Z_FIXED, Z_HUFFMAN_ONLY, Z_RLE})
      for (size_t size : {size_t(1), size_t(100), size_t(70000), size_t(300000)}) {
        std::vector<uint8_t> raw(size);
        for (size_t i = 0; i < size; ++i) raw[i] = uint8_t(i % 7 == 0 ? rng() : i % 13);
        auto b = pack(raw, level, strategy);
        std::vector<uint8_t> out;
        CHECK(unpack(b, out) && out == raw);
        for (size_t chunk : {size_t(1), size_t(4096), size_t(70001)})
          CHECK(unpack_chunked(b, out, &raw, chunk) && out == raw);
      }
  std::ifstream f("notatnik.bin", std::ios::binary);
  std::vector<uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
  if (raw.empty()) raw.assign(50000, 7);
  auto good = pack(raw, 9);
  std::vector<uint8_t> out;
  for (size_t cut : {size_t(0), size_t(3), size_t(13), size_t(20), good.size() / 2, good.size() - 1}) {
    std::vector<uint8_t> b(good.begin(), good.begin() + cut);
    CHECK(!unpack(b, out));
  }
  auto b = good; b[4] ^= 1; CHECK(!unpack(b, out));              // wrong size
  b = good; b[8] ^= 1; CHECK(!unpack(b, out));                   // wrong CRC
  b = good; b[0] = 'X'; CHECK(!unpack(b, out));                  // magic
  b = good; b[4] = 0; b[5] = 0; b[6] = 0; b[7] = 0x40; CHECK(!unpack(b, out)); // > 4 MiB
  unsigned rejected = 0;
  for (unsigned i = 0; i < 3000; ++i) {
    b = good;
    for (unsigned k = 0, n = 1 + rng() % 4; k < n; ++k) b[12 + rng() % (b.size() - 12)] ^= uint8_t(1 + rng() % 255);
    bool ok = unpack(b, out);
    if (ok) CHECK(out == raw); else ++rejected;
    // Chunked mode must agree: never accept what the one-shot path rejects.
    bool chunked = unpack_chunked(b, out, nullptr);
    CHECK(chunked == ok);
    if (chunked) CHECK(out == raw);
  }
  printf("  corruption: 3000 mutated streams, %u rejected, none accepted with wrong content\n", rejected);
  printf("Kernel compressed blobs: %u checks PASS\n", checks);
}
