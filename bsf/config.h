#pragma once
#include <stdint.h>
namespace BSF {
struct Config {
  uint32_t magic{0x31465342}, version{1}, family{}, size{16}, generation{1},
      checksum{};
};
inline uint32_t checksum(const Config &c) {
  return c.magic ^ c.version ^ c.family ^ c.size ^ 0xb5f01937U;
}
inline bool valid(const Config &c) {
  return c.magic == 0x31465342 && c.version == 1 && c.size >= 12 &&
         c.size <= 24 && c.checksum == checksum(c);
}
} // namespace BSF
