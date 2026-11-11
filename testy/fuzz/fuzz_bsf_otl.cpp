// BSF 2.0 OpenType layout fuzzer: arbitrary GSUB/GPOS/GDEF bytes attached to
// a valid DejaVu Sans face, shaped over Arabic/Hebrew/Latin/mark samples with
// a small work budget. Input: u32 gsub_len, u32 gpos_len, gsub, gpos, gdef.
#include "fuzz_engine.h"
#include "../../bsf/shape.h"
#include <fstream>
#include <iterator>
static std::vector<uint8_t> font_bytes;
static BSF::Face base_face;
static BSF::OT::Buffer buffer;
static uint8_t script_of(const char *code) {
  for (unsigned k = 0; k < 256; ++k)
    if (!strcmp(BSF::script_code(uint8_t(k)), code))
      return uint8_t(k);
  return 0;
}
static uint32_t rd(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
static void target(const uint8_t *data, size_t size) {
  if (size < 8)
    return;
  size_t a = rd(data), b = rd(data + 4), rest = size - 8;
  if (a > rest)
    a = rest;
  if (b > rest - a)
    b = rest - a;
  BSF::Face f = base_face;
  f.gsub = {data + 8, a};
  f.gpos = {data + 8 + a, b};
  f.gdef = {data + 8 + a + b, rest - a - b};
  BSF::OT::Layout L;
  L.open(f);
  static const uint32_t samples[4][12] = {
      {0x645, 0x631, 0x62d, 0x628, 0x627, ' ', 0x628, 0x627, 0x644, 0x639, 0x627, 0x644},
      {0x644, 0x627, 0x644, 0x650, 0x644, 0x651, 0x670, 0x647, 0x650, 0x200d, 0x628, 0x200c},
      {0x5e9, 0x5b8, 0x5c1, 0x5dc, 0x5d5, 0x5b9, 0x5dd, ' ', '(', '1', ')', 0x5d0},
      {'o', 'f', 'f', 'i', 'c', 'e', ' ', 'A', 'V', 0x301, 0x323, 0x308}};
  static const char *scripts[4] = {"Arab", "Arab", "Hebr", "Latn"};
  uint32_t clusters[12];
  for (unsigned i = 0; i < 12; ++i)
    clusters[i] = i * 2;
  for (unsigned s = 0; s < 4; ++s) {
    BSF::ShapeOptions o;
    o.script = script_of(scripts[s]);
    o.rtl = s < 3;
    o.budget = 200000;
    o.features = BSF::FeatureDefault | BSF::FeatureDlig;
    BSF::shape(f, L, samples[s], clusters, 12, o, buffer);
    // Invariants: bounded output, valid glyph ids, cluster provenance.
    if (buffer.len > buffer.cap)
      abort();
    for (unsigned i = 0; i < buffer.len; ++i)
      if (buffer.info[i].glyph >= f.glyphs || buffer.info[i].cluster > 22)
        abort();
  }
}
static std::vector<uint8_t> pack(const BSF::Face &f) {
  std::vector<uint8_t> v(8);
  auto put = [&](size_t o, uint32_t x) {
    v[o] = uint8_t(x >> 24);
    v[o + 1] = uint8_t(x >> 16);
    v[o + 2] = uint8_t(x >> 8);
    v[o + 3] = uint8_t(x);
  };
  put(0, uint32_t(f.gsub.n));
  put(4, uint32_t(f.gpos.n));
  v.insert(v.end(), f.gsub.p, f.gsub.p + f.gsub.n);
  v.insert(v.end(), f.gpos.p, f.gpos.p + f.gpos.n);
  v.insert(v.end(), f.gdef.p, f.gdef.p + f.gdef.n);
  return v;
}
int main(int argc, char **argv) {
  std::ifstream in("czcionki/ttf/DejaVuSans.ttf", std::ios::binary);
  font_bytes.assign(std::istreambuf_iterator<char>(in), {});
  if (!base_face.open(font_bytes.data(), font_bytes.size(), 1))
    return 2;
  buffer.reserve(64);
  FuzzEngine engine("fuzz_bsf_otl", 0x4f544c32, 1 << 20);
  engine.parse_args(argc, argv);
  engine.add_seed(pack(base_face));
  // Regressions: empty tables, truncated headers and all-0xff tables
  // (huge counts, self-referencing offsets) must stay bounded.
  engine.add_regression(std::vector<uint8_t>(8, 0));
  std::vector<uint8_t> trunc = pack(base_face);
  trunc.resize(64);
  engine.add_regression(trunc);
  std::vector<uint8_t> loops(8 + 64, 0xff);
  loops[0] = loops[1] = loops[2] = 0;
  loops[4] = loops[5] = loops[6] = 0;
  loops[3] = 32;
  loops[7] = 32;
  engine.add_regression(loops);
  return engine.run(target);
}
