#pragma once
#include <stdint.h>
namespace BSF {
// Fixed-size settings record (Ring 0 stores it verbatim in /system/bsf.cfg).
// Version 2 (BSF 2.2) appends the Terminal font after the v1 fields, so a v1
// record is the 24-byte prefix and is migrated by the kernel transport.
struct Config {
  uint32_t magic{0x31465342}, version{2}, family{}, size{16}, generation{1},
      checksum{};
  // ---- v2 ----
  uint32_t terminal_family{}; // catalog face id, 0 = automatic monospace
  uint32_t reserved{};        // must be 0
};
constexpr unsigned ConfigV1Bytes = 24;
inline uint32_t checksum(const Config &c) {
  uint32_t v = c.magic ^ c.version ^ c.family ^ c.size ^ 0xb5f01937U;
  if (c.version >= 2)
    v ^= (c.terminal_family * 0x9e3779b1U) ^ c.reserved ^ 0x22000000U;
  return v;
}
inline bool valid(const Config &c) {
  return c.magic == 0x31465342 && c.version == 2 && c.size >= 12 &&
         c.size <= 24 && !c.reserved && c.checksum == checksum(c);
}
// A stored BSF 2.1 record: validated with the v1 rules, then upgraded.
inline bool migrate_v1(const uint8_t *bytes, unsigned n, Config &out) {
  if (n != ConfigV1Bytes)
    return false;
  uint32_t w[6];
  for (unsigned i = 0; i < 6; ++i)
    w[i] = uint32_t(bytes[i * 4]) | (uint32_t(bytes[i * 4 + 1]) << 8) |
           (uint32_t(bytes[i * 4 + 2]) << 16) | (uint32_t(bytes[i * 4 + 3]) << 24);
  if (w[0] != 0x31465342 || w[1] != 1 || w[3] < 12 || w[3] > 24 ||
      w[5] != (w[0] ^ w[1] ^ w[2] ^ w[3] ^ 0xb5f01937U))
    return false;
  out = Config{};
  out.family = w[2];
  out.size = w[3];
  out.generation = w[4];
  out.checksum = checksum(out);
  return true;
}
} // namespace BSF
