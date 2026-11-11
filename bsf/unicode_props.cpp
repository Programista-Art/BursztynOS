// BSF 2.0 Unicode 15.1.0 layout properties. Generated tables are bounded,
// immutable and binary searched; no allocation or global state.
#include "text.h"
#include "bsf.h"
#include "unicode_layout.h"
namespace BSF {
namespace {
namespace U = UnicodeBlob;
inline uint32_t cp3(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
}
// Range table lookup over 7-byte records; fallback when absent/unavailable.
unsigned lookup(U::Table t, uint32_t cp, unsigned fallback) {
  const uint8_t *blob = unicode_layout_blob();
  if (!blob)
    return fallback;
  const uint8_t *table = blob + U::TableOffset[t];
  size_t lo = 0, hi = U::TableBytes[t] / 7;
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    const uint8_t *p = table + m * 7;
    if (cp < cp3(p))
      hi = m;
    else if (cp > cp3(p + 3))
      lo = m + 1;
    else
      return p[6];
  }
  return fallback;
}
// Keyed scalar tuple lookup (first 3-byte field is the sorted key).
const uint8_t *find(U::Table t, uint32_t key) {
  const uint8_t *blob = unicode_layout_blob();
  if (!blob)
    return nullptr;
  const uint8_t *table = blob + U::TableOffset[t];
  size_t rec = U::TableRecord[t], lo = 0, hi = U::TableBytes[t] / rec;
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    uint32_t v = cp3(table + m * rec);
    if (key < v)
      hi = m;
    else if (key > v)
      lo = m + 1;
    else
      return table + m * rec;
  }
  return nullptr;
}
constexpr uint32_t T(char a, char b, char c, char d) {
  return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
         (uint32_t(uint8_t(c)) << 8) | uint8_t(d);
}
} // namespace
BidiClass bidi_class(uint32_t cp) {
  if (cp > 0x10ffff)
    return BidiClass::L;
  return BidiClass(lookup(U::TableBidi, cp, 0));
}
bool strong_rtl(uint32_t cp) {
  auto c = bidi_class(cp);
  return c == BidiClass::R || c == BidiClass::AL;
}
uint32_t bidi_mirror(uint32_t cp) {
  const uint8_t *r = find(U::TableMirror, cp);
  return r ? cp3(r + 3) : 0;
}
uint32_t bidi_bracket(uint32_t cp, unsigned &type) {
  const uint8_t *r = find(U::TableBrackets, cp);
  type = r ? r[6] : 0;
  return r ? cp3(r + 3) : 0;
}
Joining joining_type(uint32_t cp) { return Joining(lookup(U::TableJoining, cp, 0)); }
uint8_t script(uint32_t cp) {
  // Unlisted scalars are Unknown (Zzzz), per Scripts.txt @missing.
  return uint8_t(lookup(U::TableScript, cp, ScriptUnknown));
}
const char *script_code(uint8_t s) {
  static const char unknown[] = "Zzzz";
  const uint8_t *blob = unicode_layout_blob();
  if (!blob || s >= U::ScriptCount)
    return unknown;
  // Names are 4 bytes without terminators; copy into a small ring buffer.
  static char ring[8][5];
  static unsigned next;
  char *out = ring[next++ % 8];
  for (unsigned i = 0; i < 4; ++i)
    out[i] = char(blob[U::TableOffset[U::TableScriptNames] + s * 4 + i]);
  out[4] = 0;
  return out;
}
uint32_t script_tag(uint8_t s) {
  const char *c = script_code(s);
  struct Special { char code[5]; uint32_t tag; };
  static const Special special[] = {
      {"Zyyy", T('D', 'F', 'L', 'T')}, {"Zinh", T('D', 'F', 'L', 'T')},
      {"Zzzz", T('D', 'F', 'L', 'T')}, {"Hira", T('k', 'a', 'n', 'a')},
      {"Hrkt", T('k', 'a', 'n', 'a')}, {"Laoo", T('l', 'a', 'o', ' ')},
      {"Yiii", T('y', 'i', ' ', ' ')}, {"Nkoo", T('n', 'k', 'o', ' ')},
      {"Vaii", T('v', 'a', 'i', ' ')}, {"Deva", T('d', 'e', 'v', '2')},
      {"Beng", T('b', 'n', 'g', '2')}, {"Guru", T('g', 'u', 'r', '2')},
      {"Gujr", T('g', 'j', 'r', '2')}, {"Orya", T('o', 'r', 'y', '2')},
      {"Taml", T('t', 'm', 'l', '2')}, {"Telu", T('t', 'e', 'l', '2')},
      {"Knda", T('k', 'n', 'd', '2')}, {"Mlym", T('m', 'l', 'm', '2')},
      {"Mymr", T('m', 'y', 'm', '2')}};
  for (const auto &x : special) {
    bool eq = true;
    for (unsigned i = 0; i < 4; ++i)
      eq &= x.code[i] == c[i];
    if (eq)
      return x.tag;
  }
  auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? char(ch + 32) : ch; };
  return T(lower(c[0]), c[1], c[2], c[3]);
}
LineBreak line_break(uint32_t cp) {
  // Unlisted scalars default to AL except the documented ID/PR ranges.
  unsigned v = lookup(U::TableLinebreak, cp, 0xff);
  if (v != 0xff)
    return LineBreak(v);
  if ((cp >= 0x3400 && cp <= 0x4dbf) || (cp >= 0x4e00 && cp <= 0x9fff) ||
      (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0x20000 && cp <= 0x3fffd) ||
      (cp >= 0x1f000 && cp <= 0x1fffd))
    return LineBreak::ID;
  if (cp >= 0x20a0 && cp <= 0x20cf)
    return LineBreak::PR;
  return LineBreak::AL;
}
unsigned mark_category(uint32_t cp) {
  return lookup(U::TableMark, cp, 0);
}
bool decompose(uint32_t cp, uint32_t &a, uint32_t &b) {
  a = b = 0;
  if (cp >= 0xac00 && cp <= 0xd7a3) {
    uint32_t s = cp - 0xac00;
    if (s % 28) {
      a = 0xac00 + (s / 28) * 28;
      b = 0x11a7 + s % 28;
    } else {
      a = 0x1100 + s / 588;
      b = 0x1161 + (s % 588) / 28;
    }
    return true;
  }
  if (const uint8_t *r = find(U::TableDecompose, cp)) {
    a = cp3(r + 3);
    b = cp3(r + 6);
    return true;
  }
  if (const uint8_t *r = find(U::TableDecomposeSingle, cp)) {
    a = cp3(r + 3);
    return true;
  }
  return false;
}
uint32_t compose(uint32_t a, uint32_t b) {
  if (a >= 0x1100 && a < 0x1113 && b >= 0x1161 && b < 0x1176)
    return 0xac00 + ((a - 0x1100) * 21 + (b - 0x1161)) * 28;
  if (a >= 0xac00 && a <= 0xd7a3 && !((a - 0xac00) % 28) && b > 0x11a7 &&
      b < 0x11c3)
    return a + (b - 0x11a7);
  const uint8_t *blob = unicode_layout_blob();
  if (!blob)
    return 0;
  const uint8_t *table = blob + U::TableOffset[U::TableCompose];
  size_t lo = 0, hi = U::TableBytes[U::TableCompose] / 9;
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    const uint8_t *r = table + m * 9;
    uint32_t x = cp3(r), y = cp3(r + 3);
    if (a < x || (a == x && b < y))
      hi = m;
    else if (a > x || b > y)
      lo = m + 1;
    else
      return cp3(r + 6);
  }
  return 0;
}
bool needs_complex_layout(const char *text, size_t bytes) {
  if (!text)
    return false;
  bool tables = false; // fetched lazily, only once non-ASCII text appears
  for (size_t at = 0; at < bytes;) {
    unsigned char c = uint8_t(text[at]);
    if (c < 0x80) { // ASCII never needs the complex path.
      ++at;
      continue;
    }
    if (!tables) {
      if (!unicode_layout_blob())
        return false; // no tables: legacy LTR path stays authoritative
      tables = true;
    }
    uint32_t cp;
    size_t n = utf8(text + at, bytes - at, cp);
    at += n ? n : 1;
    BidiClass k = bidi_class(cp);
    if (k == BidiClass::R || k == BidiClass::AL || k == BidiClass::AN ||
        (k >= BidiClass::LRE && k <= BidiClass::PDI))
      return true;
    Joining j = joining_type(cp); // C (ZWJ, tatweel) alone needs no shaping
    if (j == Joining::R || j == Joining::D || j == Joining::L)
      return true;
    if (mark_category(cp))
      return true;
  }
  return false;
}
} // namespace BSF
