#include "../bsf/bsf.h"
#include "../bsf/runtime.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstring>
#include <filesystem>
#include "bursztyn_gui.h"

// Host filesystem adapter; all measurement and caching below use the native
// implementation. Filesystem loading is outside the timed warm operations.
bool wylistuj_katalog_uzytkownika(const char* path, char* output, uint32_t cap) {
  if (std::strcmp(path, "/czcionki/ttf")) return false;
  std::string list;
  for (const auto& entry : std::filesystem::directory_iterator("czcionki/ttf"))
    if (entry.is_regular_file()) list += "[F] " + entry.path().filename().string() + "\n";
  if (list.size() >= cap) return false;
  std::memcpy(output, list.c_str(), list.size()+1);
  return true;
}
bool pobierz_rozmiar_pliku(const char* path, uint32_t* size) {
  std::error_code error;
  auto bytes=std::filesystem::file_size(path+1,error);
  if (error || bytes>BSF::MaxFile) return false;
  *size=uint32_t(bytes); return true;
}
bool czytaj_plik(const char* path, char* output, uint32_t cap) {
  std::ifstream input(path+1,std::ios::binary);
  return bool(input.read(output,cap));
}
void wypisz(const char* text) { std::fputs(text,stderr); }
extern "C" uint64_t bws_wywolaj(uint64_t number,uint64_t command,uint64_t pointer,uint64_t size,uint64_t) {
  if (number!=92 || command || size!=sizeof(BSF::Config)) return 0;
  BSF::Config config; config.checksum=BSF::checksum(config);
  *reinterpret_cast<BSF::Config*>(pointer)=config; return 1;
}
int main() {
  std::ifstream file("czcionki/ttf/DejaVuSans.ttf", std::ios::binary);
  std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
  BSF::Face face;
  if (!face.open(bytes.data(), bytes.size(), 1))
    return 1;
  BSF::Cache cache;
  using C = std::chrono::steady_clock;
  auto t = C::now();
  auto *first = cache.get(face, 'B', 16);
  auto first_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(C::now() - t)
          .count();
  if (!first)
    return 2;
  t = C::now();
  for (unsigned i = 0; i < 200000; ++i)
    if (!cache.get(face, 'B', 16))
      return 3;
  auto hit_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(C::now() - t)
          .count() /
      200000;
  printf("Face=%zu Glyph=%zu Cache=%zu FontInfo=%zu catalog=%zu file=%zu "
         "cache_capacity=%u used=%zu first_ns=%lld hit_ns=%lld hits=%llu "
         "misses=%llu\n",
         sizeof(face), sizeof(BSF::Glyph), sizeof(cache), sizeof(BSF::FontInfo),
         sizeof(BSF::FontInfo) * BSF::MaxFiles, bytes.size(), BSF::CacheBytes,
         cache.stats.bitmap_bytes, (long long)first_ns, (long long)hit_ns,
         (unsigned long long)cache.stats.hits,
         (unsigned long long)cache.stats.misses);
  if (!BSF::ready()) return 4;
  const char* sample="Bursztyn OS — Zażółć gęślą jaźń AV WWW iii 0123456789";
  volatile int measured=0;
  t=C::now();
  for(unsigned i=0;i<20000;++i) measured=BSF::measure_text(sample,16);
  auto measure_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(C::now()-t).count()/20000;
  std::printf("runtime_measure_ns=%lld sample_width=%d sample_utf8_bytes=%zu\n",
              (long long)measure_ns,int(measured),std::strlen(sample));
  BSF::shutdown();
}
