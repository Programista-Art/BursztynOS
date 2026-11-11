// BSF 2.0 UAX #9 conformance: official Unicode 15.1.0 BidiTest.txt and
// BidiCharacterTest.txt, plus API/robustness checks for BidiText maps.
#include "../bsf/text.h"
#include "../bsf/bsf.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
using namespace BSF;
static unsigned checks, failures;
#define CHECK(x)                                                              \
  do {                                                                        \
    ++checks;                                                                 \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                    \
      std::abort();                                                           \
    }                                                                         \
  } while (0)
static const char *names[] = {"L",   "R",   "AL",  "EN",  "ES",  "ET",
                              "AN",  "CS",  "NSM", "BN",  "B",   "S",
                              "WS",  "ON",  "LRE", "LRO", "RLE", "RLO",
                              "PDF", "LRI", "RLI", "FSI", "PDI"};
static int class_index(const std::string &s) {
  for (int i = 0; i < 23; ++i)
    if (s == names[i])
      return i;
  return -1;
}
static std::vector<std::string> split(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string w;
  while (in >> w)
    out.push_back(w);
  return out;
}
static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
struct Outcome {
  std::vector<int> levels; // -1 == x
  std::vector<uint32_t> order;
  int para;
};
static bool run(const uint32_t *text, const std::vector<uint8_t> &cls,
                BaseDirection dir, Outcome &out) {
  std::vector<uint8_t> lv(cls.size() + 1), line(cls.size() + 1);
  uint8_t para = 0;
  if (!resolve_bidi_levels(text, cls.data(), cls.size(), dir, lv.data(), para))
    return false;
  bidi_line_levels(cls.data(), lv.data(), 0, cls.size(), para, line.data());
  out.levels.clear();
  for (size_t i = 0; i < cls.size(); ++i)
    out.levels.push_back(line[i] == BidiRemoved ? -1 : line[i]);
  std::vector<uint32_t> order(cls.size() + 1);
  size_t m = bidi_reorder(line.data(), cls.size(), order.data());
  out.order.assign(order.begin(), order.begin() + m);
  out.para = para;
  return true;
}
static unsigned bidi_test(const char *path) {
  std::ifstream f(path);
  CHECK(f.good());
  std::string s;
  std::vector<int> levels;
  std::vector<uint32_t> order;
  unsigned cases = 0;
  while (std::getline(f, s)) {
    s = trim(s);
    if (s.empty() || s[0] == '#')
      continue;
    if (!s.compare(0, 8, "@Levels:")) {
      levels.clear();
      for (auto &w : split(s.substr(8)))
        levels.push_back(w == "x" ? -1 : std::atoi(w.c_str()));
      continue;
    }
    if (!s.compare(0, 9, "@Reorder:")) {
      order.clear();
      for (auto &w : split(s.substr(9)))
        order.push_back(uint32_t(std::atoi(w.c_str())));
      continue;
    }
    size_t semi = s.find(';');
    std::vector<uint8_t> cls;
    for (auto &w : split(s.substr(0, semi))) {
      int c = class_index(w);
      CHECK(c >= 0);
      cls.push_back(uint8_t(c));
    }
    int bits = std::atoi(s.c_str() + semi + 1);
    const BaseDirection dirs[] = {BaseDirection::Auto, BaseDirection::LTR,
                                  BaseDirection::RTL};
    for (int b = 0; b < 3; ++b) {
      if (!(bits & (1 << b)))
        continue;
      Outcome o;
      CHECK(run(nullptr, cls, dirs[b], o));
      ++cases;
      ++checks;
      if (o.levels != levels || o.order != order) {
        if (++failures < 20)
          std::fprintf(stderr, "BidiTest mismatch: %s (dir %d)\n", s.c_str(), b);
      }
    }
  }
  return cases;
}
static unsigned character_test(const char *path) {
  std::ifstream f(path);
  CHECK(f.good());
  std::string s;
  unsigned cases = 0;
  while (std::getline(f, s)) {
    s = trim(s);
    if (s.empty() || s[0] == '#')
      continue;
    std::vector<std::string> field;
    size_t at = 0;
    for (;;) {
      size_t e = s.find(';', at);
      field.push_back(s.substr(at, e == std::string::npos ? e : e - at));
      if (e == std::string::npos)
        break;
      at = e + 1;
    }
    CHECK(field.size() == 5);
    std::vector<uint32_t> text;
    std::vector<uint8_t> cls;
    for (auto &w : split(field[0])) {
      text.push_back(uint32_t(std::strtoul(w.c_str(), nullptr, 16)));
      cls.push_back(uint8_t(bidi_class(text.back())));
    }
    int d = std::atoi(field[1].c_str());
    BaseDirection dir = d == 0 ? BaseDirection::LTR
                        : d == 1 ? BaseDirection::RTL
                                 : BaseDirection::Auto;
    std::vector<int> levels;
    for (auto &w : split(field[3]))
      levels.push_back(w == "x" ? -1 : std::atoi(w.c_str()));
    std::vector<uint32_t> order;
    for (auto &w : split(field[4]))
      order.push_back(uint32_t(std::atoi(w.c_str())));
    Outcome o;
    CHECK(run(text.data(), cls, dir, o));
    ++cases;
    ++checks;
    if (o.levels != levels || o.order != order ||
        o.para != std::atoi(field[2].c_str())) {
      if (++failures < 40)
        std::fprintf(stderr, "BidiCharacterTest mismatch: %s\n", s.c_str());
    }
  }
  return cases;
}
static std::string u8(std::initializer_list<uint32_t> cps) {
  std::string s;
  for (uint32_t c : cps) {
    if (c < 0x80)
      s += char(c);
    else if (c < 0x800) {
      s += char(0xc0 | (c >> 6));
      s += char(0x80 | (c & 63));
    } else if (c < 0x10000) {
      s += char(0xe0 | (c >> 12));
      s += char(0x80 | ((c >> 6) & 63));
      s += char(0x80 | (c & 63));
    } else {
      s += char(0xf0 | (c >> 18));
      s += char(0x80 | ((c >> 12) & 63));
      s += char(0x80 | ((c >> 6) & 63));
      s += char(0x80 | (c & 63));
    }
  }
  return s;
}
static void api_tests() {
  // Properties.
  CHECK(bidi_class('A') == BidiClass::L);
  CHECK(bidi_class(0x5d0) == BidiClass::R);
  CHECK(bidi_class(0x627) == BidiClass::AL);
  CHECK(bidi_class(0x661) == BidiClass::AN);
  CHECK(bidi_class('1') == BidiClass::EN);
  CHECK(bidi_class(0x5ff) == BidiClass::R);   // unassigned, @missing R
  CHECK(bidi_class(0x20c1) == BidiClass::ET); // unassigned, @missing ET
  CHECK(bidi_class(0x2067) == BidiClass::RLI);
  CHECK(bidi_mirror('(') == ')' && bidi_mirror('<') == '>' && !bidi_mirror('a'));
  unsigned kind;
  CHECK(bidi_bracket('[', kind) == ']' && kind == 1);
  CHECK(bidi_bracket(')', kind) == '(' && kind == 2);
  CHECK(joining_type(0x628) == Joining::D && joining_type(0x627) == Joining::R);
  CHECK(joining_type(0x64e) == Joining::T && joining_type(0x200d) == Joining::C);
  CHECK(std::strcmp(script_code(script(0x627)), "Arab") == 0);
  CHECK(std::strcmp(script_code(script(0x5d0)), "Hebr") == 0);
  CHECK(std::strcmp(script_code(script('a')), "Latn") == 0);
  CHECK(std::strcmp(script_code(script(0x301)), "Zinh") == 0);
  CHECK(script_tag(script(0x627)) == 0x61726162u); // 'arab'
  CHECK(script_tag(script(0x3042)) == 0x6b616e61u); // 'kana'
  uint32_t a, b;
  CHECK(decompose(0xe9, a, b) && a == 'e' && b == 0x301);
  CHECK(compose('e', 0x301) == 0xe9 && !compose('q', 0x301));
  CHECK(decompose(0xd55c, a, b) && compose(a, b) == 0xd55c);
  CHECK(!needs_complex_layout("Hello, world!", 13));
  std::string pl = "Zażółć gęślą jaźń";
  CHECK(!needs_complex_layout(pl.data(), pl.size()));
  std::string he = "שלום עולם";
  CHECK(needs_complex_layout(he.data(), he.size()));

  // Mixed paragraph: logical text is never reordered in storage.
  std::string mixed = "abc " + u8({0x5d0, 0x5d1, 0x5d2}) + " 123 " +
                      u8({0x5d3}) + " def";
  std::string copy = mixed;
  BidiText t;
  CHECK(t.analyze(mixed.data(), mixed.size(), BaseDirection::Auto));
  CHECK(mixed == copy && t.base_level == 0);
  std::vector<uint32_t> v2l(t.n), l2v(t.n);
  CHECK(bidi_line_map(t, 0, t.n, v2l.data(), l2v.data()));
  for (size_t i = 0; i < t.n; ++i)
    CHECK(v2l[l2v[i]] == i);
  // "abc " stays, then the RTL span "אבג 123 ד" is reversed as a unit with
  // the number kept LTR inside it.
  std::vector<uint32_t> visual;
  for (auto i : v2l)
    visual.push_back(t.cp[i]);
  std::vector<uint32_t> expect = {'a', 'b', 'c', ' ', 0x5d3, ' ', '1', '2', '3',
                                  ' ', 0x5d2, 0x5d1, 0x5d0, ' ', 'd', 'e', 'f'};
  CHECK(visual == expect);
  // RTL paragraph with explicit isolates and Arabic-Indic digits.
  std::string ar = u8({0x645, 0x631, 0x62d, 0x628, 0x627, ' ', 0x2066, 'O',
                       'S', 0x2069, ' ', 0x661, 0x662});
  CHECK(t.analyze(ar.data(), ar.size(), BaseDirection::Auto) && t.base_level == 1);
  CHECK(t.level[0] == 1 && t.level[7] == 2 && t.level[11] == 2);
  CHECK(t.scalar_at(0) == 0 && t.scalar_at(1) == 0 && t.scalar_at(2) == 1);
  // Malformed UTF-8 decodes to U+FFFD per byte, never over-reads.
  const char bad[] = {char(0xd7), char(0x90), char(0xff), char(0xe2), char(0x82)};
  CHECK(t.analyze(bad, sizeof bad, BaseDirection::Auto));
  CHECK(t.n == 4 && t.cp[0] == 0x5d0 && t.cp[1] == 0xfffd && t.offset[4] == 5);
  // Overflow: 200 nested RLE must clamp at depth 125 without recursion.
  std::vector<uint8_t> deep(400, uint8_t(BidiClass::RLE));
  deep.push_back(uint8_t(BidiClass::L));
  std::vector<uint8_t> lv(deep.size());
  uint8_t para;
  CHECK(resolve_bidi_levels(nullptr, deep.data(), deep.size(),
                            BaseDirection::LTR, lv.data(), para));
  CHECK(lv.back() == 126 || lv.back() == 125); // L at odd level 125 -> 126
  // Bounds: oversize input is rejected, not truncated.
  std::vector<uint8_t> huge(MaxBidiLength + 1, 0), hl(MaxBidiLength + 1);
  CHECK(!resolve_bidi_levels(nullptr, huge.data(), huge.size(),
                             BaseDirection::LTR, hl.data(), para));
  // Deterministic random fuzz: arbitrary classes and scalars.
  std::mt19937 rng(9);
  for (unsigned round = 0; round < 20000; ++round) {
    size_t n = rng() % 64;
    std::vector<uint32_t> cps(n);
    std::vector<uint8_t> c(n), l(n + 1), line(n + 1);
    for (size_t i = 0; i < n; ++i) {
      static const uint32_t pool[] = {'a', 0x5d0, 0x627, '1', 0x661, '(', ')',
                                      '[', ']', ' ', 0x202a, 0x202b, 0x202c,
                                      0x202d, 0x202e, 0x2066, 0x2067, 0x2068,
                                      0x2069, 0x300, 0x200b, ',', '+', '$', '\t'};
      cps[i] = pool[rng() % (sizeof pool / sizeof pool[0])];
      c[i] = uint8_t(bidi_class(cps[i]));
    }
    uint8_t p;
    CHECK(resolve_bidi_levels(cps.data(), c.data(), n, BaseDirection(rng() % 3),
                              l.data(), p));
    bidi_line_levels(c.data(), l.data(), 0, n, p, line.data());
    std::vector<uint32_t> order(n + 1);
    size_t m = bidi_reorder(line.data(), n, order.data());
    std::vector<bool> seen(n);
    for (size_t k = 0; k < m; ++k) {
      CHECK(order[k] < n && !seen[order[k]]);
      seen[order[k]] = true;
    }
    for (size_t i = 0; i < n; ++i)
      CHECK(l[i] == BidiRemoved || l[i] <= BidiMaxDepth + 1);
  }
}
int main() {
  api_tests();
  unsigned a = bidi_test("narzedzia/unicode/15.1.0/BidiTest.txt");
  unsigned b = character_test("narzedzia/unicode/15.1.0/BidiCharacterTest.txt");
  std::printf("UAX #9 Unicode 15.1.0: BidiTest %u cases, BidiCharacterTest %u "
              "cases, failures %u; %u checks\n",
              a, b, failures, checks);
  return failures ? 1 : 0;
}
