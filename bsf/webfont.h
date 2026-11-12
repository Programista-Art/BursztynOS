#pragma once
// BSF 2.2 — web font containers (WOFF 1.0, WOFF2) decoded into a plain sfnt.
// Ring 3 only; linked into the Husarz browser process (web fonts are
// browser-scoped) and into host tests. The decoded sfnt is then validated by
// Face::open exactly like a local font; there is no WOFF-specific
// rasterizer, shaper or layout path.
#include "bsf.h"
namespace BSF {
enum class WebFontStatus : uint8_t {
  Ok,
  Unsupported,      // unknown signature, WOFF2 collection, CFF transform …
  Malformed,        // header/directory/stream structure invalid
  TooLarge,         // input/output/ratio limit exceeded
  DecompressFailed, // zlib/Brotli stream invalid or size mismatch
  TransformFailed,  // WOFF2 glyf/loca/hmtx reconstruction failed
  InvalidFont,      // decoded sfnt rejected by Face validation
};
struct WebFontLimits {
  size_t max_input = 16u * 1024 * 1024; // downloaded bytes
  size_t max_output = MaxFile;          // decoded sfnt bytes
  unsigned max_ratio = 128;             // decoded / input
};
struct DecodedWebFont {
  uint8_t *bytes{}; // new[]; owned by the caller
  size_t size{};
  ContainerFormat container{ContainerFormat::SFNT};
};
// Accepts plain sfnt ('true', 0x00010000, 'OTTO'), 'wOFF' and 'wOF2'.
WebFontStatus decode_web_font(const uint8_t *in, size_t n, DecodedWebFont &out,
                              const WebFontLimits &limits = WebFontLimits{});
const char *web_font_status_name(WebFontStatus);
} // namespace BSF
