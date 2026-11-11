#pragma once
// Access to the packed Unicode 15.1.0 layout blob (bsf/unicode_layout_index.h).
// Ring 3 obtains it once per process from the kernel via BWS 51 command 2;
// host tests and the kernel link the static copy. A null result means the
// tables are unavailable: BSF then disables complex layout (legacy path).
#include <stddef.h>
#include <stdint.h>
#include "unicode_layout_index.h"
namespace BSF {
const uint8_t *unicode_layout_blob();
// Header validation shared by every provider (and the kernel self-check).
inline bool unicode_layout_valid(const uint8_t *b, size_t n) {
  auto u32 = [&](size_t o) {
    return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) |
           (uint32_t(b[o + 3]) << 24);
  };
  if (!b || n != UnicodeBlob::Size || u32(0) != UnicodeBlob::Magic ||
      u32(4) != UnicodeBlob::Version || u32(8) != UnicodeBlob::Size ||
      u32(12) != UnicodeBlob::UnicodeVersion)
    return false;
  for (unsigned t = 0; t < UnicodeBlob::TableCount; ++t)
    if (u32(16 + t * 8) != UnicodeBlob::TableOffset[t] ||
        u32(20 + t * 8) != UnicodeBlob::TableBytes[t])
      return false;
  // Whole-content check (once per process): catches a kernel/app built from
  // different Unicode tables with identical sizes, or a corrupted copy.
  uint32_t h = 0x811c9dc5u;
  for (size_t i = 0; i < n; ++i)
    h = (h ^ b[i]) * 0x01000193u;
  return h == UnicodeBlob::ContentHash;
}
} // namespace BSF
