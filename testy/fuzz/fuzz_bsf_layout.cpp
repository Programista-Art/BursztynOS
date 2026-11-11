// BSF 2.0 unified layout fuzzer: arbitrary text through itemization, real
// DejaVu shaping, wrapping and every caret/hit-test/selection API.
// Input byte 0 selects px/direction/wrapping; the rest is the text.
#include "fuzz_engine.h"
#include "../../bsf/layout.h"
#include <fstream>
#include <iterator>
static std::vector<uint8_t> font_bytes;
static BSF::Face face;
static uint32_t run_face(void *, const char *t, size_t a, size_t b) {
  for (size_t at = a; at < b;) {
    uint32_t cp;
    at += BSF::utf8(t + at, b - at, cp);
    if (!BSF::default_ignorable(cp) && !face.glyph(cp))
      return 0;
  }
  return 1;
}
static const BSF::Face *get_face(void *, uint32_t id) { return id == 1 ? &face : nullptr; }
static int32_t fallback(void *, uint32_t, int px) { return px * 32768; }
static void target(const uint8_t *data, size_t size) {
  if (!size)
    return;
  BSF::FontResolver fr;
  fr.run_face = run_face;
  fr.cluster_face = run_face;
  fr.face = get_face;
  fr.fallback_advance = fallback;
  BSF::LayoutOptions o;
  o.px = 8 + data[0] % 40;
  o.direction = BSF::BaseDirection(data[0] % 3);
  o.max_width = (data[0] & 8) ? int64_t(30 + data[0]) * BSF::Fixed : 0;
  o.align_to_direction = (data[0] & 16) != 0;
  const char *text = reinterpret_cast<const char *>(data + 1);
  size_t n = size - 1;
  BSF::TextLayout t;
  if (!t.build(text, n, o, fr))
    abort();
  for (size_t b = 0; b <= n; b = BSF::next_cluster(text, b, n)) {
    size_t line;
    t.caret_x(b, &line);
    for (int d : {-1, 1}) {
      size_t m = t.move_visual(b, d);
      if (m > n || !BSF::cluster_boundary(text, n, m))
        abort();
    }
    if (b == n)
      break;
  }
  for (size_t l = 0; l < t.line_count(); ++l)
    for (int64_t x = -BSF::Fixed; x < t.width() + 2 * BSF::Fixed; x += 7 * BSF::Fixed) {
      size_t h = t.hit_test(x, l);
      if (h > n || !BSF::cluster_boundary(text, n, h))
        abort();
    }
  BSF::TextLayout::Span spans[16];
  t.selection(0, n, spans, 16);
}
int main(int argc, char **argv) {
  std::ifstream in("czcionki/ttf/DejaVuSans.ttf", std::ios::binary);
  font_bytes.assign(std::istreambuf_iterator<char>(in), {});
  if (!face.open(font_bytes.data(), font_bytes.size(), 1))
    return 2;
  FuzzEngine engine("fuzz_bsf_layout", 0x4c41594f, 2048);
  engine.parse_args(argc, argv);
  const char *seeds[] = {
      "\x09" "abc \xd7\x90\xd7\x91 12 (x) \xd9\x84\xd8\xa7",
      "\x1a" "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7"
      "\xd9\x84\xd9\x85\n\xe4\xbd\xa0",
      "\x02" "e\xcc\x81\xcc\xa3 \xf0\x9f\x98\x80\xe2\x80\x8d\xf0\x9f\x92\xbb\r\n\xe2\x80\xae" "x"};
  for (const char *s : seeds)
    engine.add_seed(std::vector<uint8_t>(s, s + strlen(s)));
  return engine.run(target);
}
