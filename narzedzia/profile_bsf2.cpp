// BSF 2.0 host profile (optimized build): UAX #9, shaping and full layout
// throughput on representative paragraphs. Host numbers only; native
// timings come from the QEMU harness counters.
#include "../bsf/layout.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace BSF;
static std::vector<uint8_t> bytes;
static Face face;
static uint32_t run_face(void *, const char *t, size_t a, size_t b) {
  for (size_t at = a; at < b;) {
    uint32_t cp;
    at += utf8(t + at, b - at, cp);
    if (!default_ignorable(cp) && !face.glyph(cp))
      return 0;
  }
  return 1;
}
static const Face *get_face(void *, uint32_t) { return &face; }
static int32_t fallback(void *, uint32_t, int px) { return px * 32768; }
template <class F> static double ns_per(unsigned reps, F f) {
  auto t0 = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < reps; ++i)
    f();
  return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0).count()) / reps;
}
int main() {
  std::ifstream in("czcionki/ttf/DejaVuSans.ttf", std::ios::binary);
  bytes.assign(std::istreambuf_iterator<char>(in), {});
  if (!face.open(bytes.data(), bytes.size(), 1))
    return 1;
  FontResolver fr;
  fr.run_face = run_face;
  fr.cluster_face = run_face;
  fr.face = get_face;
  fr.fallback_advance = fallback;
  struct Sample { const char *name; std::string text; } samples[] = {
      {"latin", ""}, {"arabic", ""}, {"hebrew-mixed", ""}};
  for (int i = 0; i < 40; ++i) {
    samples[0].text += "The quick brown fox jumps over the lazy dog. ";
    samples[1].text += "\xd8\xa7\xd9\x84\xd9\x84\xd8\xba\xd8\xa9 \xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9 "
                       "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85. ";
    samples[2].text += "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d 2026 (abc) ";
  }
  for (auto &s : samples) {
    BidiText bt;
    double bidi = ns_per(200, [&] { bt.analyze(s.text.data(), s.text.size(), BaseDirection::Auto); });
    TextLayout t;
    LayoutOptions o;
    o.px = 16;
    double one = ns_per(100, [&] { t.build(s.text.data(), s.text.size(), o, fr); });
    o.max_width = 400 * Fixed;
    double wrapped = ns_per(100, [&] { t.build(s.text.data(), s.text.size(), o, fr); });
    size_t lines = t.line_count();
    double caret = ns_per(20000, [&] { volatile int64_t x = t.caret_x(s.text.size() / 2); (void)x; });
    double move = ns_per(20000, [&] { volatile size_t m = t.move_visual(s.text.size() / 2, 1); (void)m; });
    std::printf("%-13s bytes=%5zu glyphs=%5zu bidi=%8.0f ns  layout=%9.0f ns  wrapped=%9.0f ns (%zu lines)  "
                "caret_x=%5.0f ns  move_visual=%5.0f ns\n",
                s.name, s.text.size(), t.glyph_count(), bidi, one, wrapped, lines, caret, move);
  }
  return 0;
}
