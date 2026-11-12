// BSF 2.1 editing fuzzer: arbitrary mixed-direction UTF-8 (malformed
// included) through paragraph layout with wrapping, explicit direction,
// letter/word spacing and joining context; every line-aware caret query
// (caret_x_on, move_visual_on walks, line_edge), hit testing per line,
// selection spans and a sequence of random logical edits re-laid out.
// Byte 0: px/direction/wrap; byte 1: spacing/context; rest: text.
#include "fuzz_engine.h"
#include "../../bsf/layout.h"
#include <fstream>
#include <iterator>
#include <string>
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

static void check_layout(const BSF::TextLayout &t, const char *text, size_t n) {
  for (size_t l = 0; l < t.line_count(); ++l) {
    const BSF::LayoutLine &L = t.lines()[l];
    if (L.byte_begin > L.byte_end || L.byte_end > n)
      abort();
    for (int dir : {-1, 1}) {
      size_t b = t.line_edge(l, -dir), steps = 0;
      int64_t x = t.caret_x_on(b, l);
      for (;;) {
        bool moved = false;
        size_t m = t.move_visual_on(b, l, dir, &moved);
        if (!moved)
          break;
        int64_t nx = t.caret_x_on(m, l);
        if (m > n || !BSF::cluster_boundary(text, n, m) || (dir > 0 ? nx <= x : nx >= x) || ++steps > n + 2)
          abort();
        b = m;
        x = nx;
      }
    }
    for (int64_t x = -BSF::Fixed; x < t.width() + 2 * BSF::Fixed; x += 5 * BSF::Fixed) {
      size_t h = t.hit_test(x, l);
      if (h > n || !BSF::cluster_boundary(text, n, h))
        abort();
    }
  }
  BSF::TextLayout::Span spans[64];
  size_t a = n / 3, z = n - n / 4;
  size_t ns = t.selection(a, z, spans, 64);
  for (size_t k = 0; k < ns; ++k)
    if (spans[k].line >= t.line_count() || spans[k].x0 > spans[k].x1)
      abort();
}
static void target(const uint8_t *data, size_t size) {
  if (size < 2)
    return;
  BSF::FontResolver fr;
  fr.run_face = run_face;
  fr.cluster_face = run_face;
  fr.face = get_face;
  fr.fallback_advance = fallback;
  BSF::LayoutOptions o;
  o.px = 8 + data[0] % 40;
  o.direction = BSF::BaseDirection(data[0] % 3);
  o.max_width = (data[0] & 8) ? int64_t(20 + data[0]) * BSF::Fixed : 0;
  o.letter_spacing = int32_t(int8_t(data[1]) % 5) * BSF::Fixed;
  o.word_spacing = int32_t((data[1] >> 3) % 4) * BSF::Fixed;
  o.context_before = (data[1] & 64) ? 0x0628 : 0;
  o.context_after = (data[1] & 128) ? 0x0644 : 0;
  std::string text(reinterpret_cast<const char *>(data + 2), size - 2);
  BSF::TextLayout t;
  // A few logical edits (insert/delete at grapheme boundaries), each
  // followed by a fresh layout of the logical text (never reordered).
  for (unsigned round = 0; round < 3; ++round) {
    if (!t.build(text.data(), text.size(), o, fr))
      abort();
    if (std::string(t.text(), t.bytes()) != text)
      abort();
    check_layout(t, text.data(), text.size());
    size_t at = BSF::cluster_floor(text.data(), text.size(), (size_t(data[0]) * 31 + round * 7) % (text.size() + 1));
    if (round == 0)
      text.insert(at, "\xd7\xa9 ab");
    else if (round == 1 && at < text.size())
      text.erase(at, BSF::next_cluster(text.data(), at, text.size()) - at);
  }
}
int main(int argc, char **argv) {
  std::ifstream in("czcionki/ttf/DejaVuSans.ttf", std::ios::binary);
  font_bytes.assign(std::istreambuf_iterator<char>(in), {});
  if (!face.open(font_bytes.data(), font_bytes.size(), 1))
    return 2;
  FuzzEngine engine("fuzz_bsf21_editing", 0x42534632, 4096);
  engine.parse_args(argc, argv);
  const char *seeds[] = {
      "\x09\x05" "abc \xd7\x90\xd7\x91 12 (x) \xd9\x84\xd8\xa7 Hello world \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d",
      "\x1a\xc1" "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85 123 abc\n\xd7\x90",
      "\x0a\x00" "\xe2\x81\xa7\xd7\x90 abc\xe2\x81\xa9 \xe2\x80\xae" "xyz\xe2\x80\xac e\xcc\x81 \xff\xc0"};
  for (const char *s : seeds)
    engine.add_seed(std::vector<uint8_t>(s, s + 2 + strlen(s + 2)));
  return engine.run(target);
}
