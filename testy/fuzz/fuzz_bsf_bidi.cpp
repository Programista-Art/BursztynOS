// BSF 2.0 UAX #9 fuzzer: arbitrary UTF-8 (incl. malformed) and arbitrary
// class sequences. Invariants: analysis never fails for valid sizes, levels
// stay bounded, line maps are permutations and inverses of each other.
#include "fuzz_engine.h"
#include "../../bsf/text.h"
static void target(const uint8_t *data, size_t size) {
  BSF::BidiText t;
  for (int dir = 0; dir < 3; ++dir) {
    if (!t.analyze(reinterpret_cast<const char *>(data), size, BSF::BaseDirection(dir)))
      abort();
    std::vector<uint32_t> v2l(t.n + 1), l2v(t.n + 1);
    for (size_t b = 0; b < t.n;) {
      size_t e = b;
      while (e < t.n && t.cls[e] != uint8_t(BSF::BidiClass::B))
        ++e;
      if (e < t.n)
        ++e;
      if (!BSF::bidi_line_map(t, b, e, v2l.data(), l2v.data()))
        abort();
      for (size_t k = 0; k < e - b; ++k)
        if (v2l[l2v[k]] != b + k)
          abort();
      b = e;
    }
    for (size_t i = 0; i < t.n; ++i)
      if (t.level[i] != BSF::BidiRemoved && t.level[i] > BSF::BidiMaxDepth + 1)
        abort();
  }
  // Raw class sequence (BidiTest style: B only as the final class).
  std::vector<uint8_t> cls, lv(size + 1), line(size + 1);
  for (size_t i = 0; i < size; ++i) {
    uint8_t c = data[i] % 23;
    if (c == uint8_t(BSF::BidiClass::B) && i + 1 != size)
      c = uint8_t(BSF::BidiClass::WS);
    cls.push_back(c);
  }
  uint8_t para;
  if (!BSF::resolve_bidi_levels(nullptr, cls.data(), cls.size(), BSF::BaseDirection::Auto,
                                lv.data(), para))
    abort();
  BSF::bidi_line_levels(cls.data(), lv.data(), 0, cls.size(), para, line.data());
  std::vector<uint32_t> order(size + 1);
  BSF::bidi_reorder(line.data(), cls.size(), order.data());
}
int main(int argc, char **argv) {
  FuzzEngine engine("fuzz_bsf_bidi", 0x42494449, 4096);
  engine.parse_args(argc, argv);
  const char *seeds[] = {
      "abc \xd7\x90\xd7\x91\xd7\x92 123",
      "\xd9\x85\xd8\xb1 (\xe2\x81\xa6x\xe2\x81\xa9) \xd9\xa1",
      "\xe2\x80\xab\xe2\x80\xaa\xe2\x80\xae\xe2\x80\xac[a]\n\xd7\x90",
      "\xff\xfe\xe2\x82"};
  for (const char *s : seeds)
    engine.add_seed(std::vector<uint8_t>(s, s + strlen(s)));
  std::vector<uint8_t> deep;
  for (int i = 0; i < 200; ++i) { // 200 nested RLI: overflow handling
    deep.push_back(0xe2);
    deep.push_back(0x81);
    deep.push_back(0xa7);
  }
  engine.add_regression(deep);
  return engine.run(target);
}
