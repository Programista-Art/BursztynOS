// BSF 2.2 — bounded zlib/DEFLATE decoder (RFC 1950/1951), Ring 3 only.
// Included by bsf.cpp inside namespace BSF. Used by WOFF 1.0 tables and by
// PNG colour-bitmap strikes (CBDT/sbix).
//
// The output size is fixed by the caller (WOFF origLength / PNG scanlines):
// any stream that would write past it, reference data before the output
// start, or end early is rejected. Work is bounded by the output size plus
// the input size (every decoded symbol produces at least one byte or
// consumes input bits), and Huffman tables are validated (no over-subscribed
// codes; incomplete codes only where RFC 1951 permits a single code).
namespace {
struct InflateState {
  const uint8_t *in{};
  size_t n{}, pos{};
  uint32_t bitbuf{};
  unsigned bitcnt{};
  uint8_t *out{};
  size_t cap{}, len{};
  bool bad{};
  uint32_t bits(unsigned need) {
    uint32_t v = bitbuf;
    while (bitcnt < need) {
      if (pos >= n) {
        bad = true;
        return 0;
      }
      v |= uint32_t(in[pos++]) << bitcnt;
      bitcnt += 8;
    }
    bitbuf = v >> need;
    bitcnt -= need;
    return v & ((1u << need) - 1);
  }
};
struct Huffman {
  uint16_t count[16]{};
  uint16_t symbol[320]{};
};
// Canonical code construction. Returns 0 complete, >0 incomplete, <0 bad.
int huffman_build(Huffman &h, const uint8_t *lengths, unsigned n) {
  for (auto &c : h.count)
    c = 0;
  for (unsigned s = 0; s < n; ++s)
    ++h.count[lengths[s]];
  if (h.count[0] == n)
    return 0; // no codes (allowed for an unused distance code)
  int left = 1;
  for (unsigned len = 1; len < 16; ++len) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0)
      return -1;
  }
  uint16_t offs[16]{};
  for (unsigned len = 1; len < 15; ++len)
    offs[len + 1] = uint16_t(offs[len] + h.count[len]);
  for (unsigned s = 0; s < n; ++s)
    if (lengths[s])
      h.symbol[offs[lengths[s]]++] = uint16_t(s);
  return left;
}
int huffman_decode(InflateState &s, const Huffman &h) {
  int code = 0, first = 0, index = 0;
  for (unsigned len = 1; len < 16; ++len) {
    code |= int(s.bits(1));
    if (s.bad)
      return -1;
    int count = h.count[len];
    if (code - count < first)
      return h.symbol[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}
bool inflate_codes(InflateState &s, const Huffman &lit, const Huffman &dist) {
  static const uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                     15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                     67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const uint16_t dbase[30] = {
      1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,   97,   129,
      193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  for (;;) {
    int sym = huffman_decode(s, lit);
    if (sym < 0)
      return false;
    if (sym < 256) {
      if (s.len >= s.cap)
        return false;
      s.out[s.len++] = uint8_t(sym);
    } else if (sym == 256)
      return true;
    else {
      sym -= 257;
      if (sym >= 29)
        return false;
      size_t length = lbase[sym] + s.bits(lext[sym]);
      int d = huffman_decode(s, dist);
      if (d < 0 || d >= 30 || s.bad)
        return false;
      size_t distance = dbase[d] + s.bits(dext[d]);
      if (s.bad || distance > s.len || length > s.cap - s.len)
        return false;
      for (size_t i = 0; i < length; ++i, ++s.len)
        s.out[s.len] = s.out[s.len - distance];
    }
  }
}
bool inflate_raw(InflateState &s) {
  auto *lit = BSF_NEW Huffman, *dist = BSF_NEW Huffman;
  if (!lit || !dist) {
    delete lit;
    delete dist;
    return false;
  }
  bool ok = true, last = false;
  while (ok && !last) {
    last = s.bits(1);
    unsigned type = s.bits(2);
    if (s.bad) {
      ok = false;
      break;
    }
    if (type == 0) { // stored
      s.bitbuf = 0;
      s.bitcnt = 0;
      if (s.pos + 4 > s.n) {
        ok = false;
        break;
      }
      unsigned len = s.in[s.pos] | (unsigned(s.in[s.pos + 1]) << 8),
               nlen = s.in[s.pos + 2] | (unsigned(s.in[s.pos + 3]) << 8);
      s.pos += 4;
      if (len != (~nlen & 0xffff) || len > s.n - s.pos || len > s.cap - s.len) {
        ok = false;
        break;
      }
      for (unsigned k = 0; k < len; ++k)
        s.out[s.len + k] = s.in[s.pos + k];
      s.len += len;
      s.pos += len;
    } else if (type == 1) { // fixed codes
      uint8_t lengths[288 + 30];
      unsigned i = 0;
      for (; i < 144; ++i) lengths[i] = 8;
      for (; i < 256; ++i) lengths[i] = 9;
      for (; i < 280; ++i) lengths[i] = 7;
      for (; i < 288; ++i) lengths[i] = 8;
      for (i = 0; i < 30; ++i) lengths[288 + i] = 5;
      ok = huffman_build(*lit, lengths, 288) >= 0 &&
           huffman_build(*dist, lengths + 288, 30) >= 0 &&
           inflate_codes(s, *lit, *dist);
    } else if (type == 2) { // dynamic codes
      unsigned nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
      if (s.bad || nlen > 286 || ndist > 30) {
        ok = false;
        break;
      }
      static const uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                        11, 4,  12, 3, 13, 2, 14, 1, 15};
      uint8_t lengths[320]{};
      for (unsigned i = 0; i < ncode; ++i)
        lengths[order[i]] = uint8_t(s.bits(3));
      if (s.bad || huffman_build(*lit, lengths, 19) != 0) {
        ok = false;
        break;
      }
      unsigned index = 0;
      while (index < nlen + ndist && ok) {
        int sym = huffman_decode(s, *lit);
        if (sym < 0) {
          ok = false;
          break;
        }
        if (sym < 16)
          lengths[index++] = uint8_t(sym);
        else {
          unsigned value = 0, repeat;
          if (sym == 16) {
            if (!index) {
              ok = false;
              break;
            }
            value = lengths[index - 1];
            repeat = 3 + s.bits(2);
          } else if (sym == 17)
            repeat = 3 + s.bits(3);
          else
            repeat = 11 + s.bits(7);
          if (s.bad || index + repeat > nlen + ndist) {
            ok = false;
            break;
          }
          while (repeat--)
            lengths[index++] = uint8_t(value);
        }
      }
      if (!ok || !lengths[256])
        ok = false;
      // Incomplete literal/length codes are only valid with a single code.
      int e = ok ? huffman_build(*lit, lengths, nlen) : -1;
      if (e < 0 || (e > 0 && nlen - lit->count[0] != 1))
        ok = false;
      e = ok ? huffman_build(*dist, lengths + nlen, ndist) : -1;
      if (e < 0 || (e > 0 && ndist - dist->count[0] != 1))
        ok = false;
      if (ok)
        ok = inflate_codes(s, *lit, *dist);
    } else
      ok = false;
  }
  delete lit;
  delete dist;
  return ok && !s.bad;
}
} // namespace
// zlib stream into exactly `cap` bytes (an output shorter than `cap` is an
// error unless `allow_short`). Adler-32 is verified.
bool inflate_zlib(const uint8_t *in, size_t n, uint8_t *out, size_t cap,
                  size_t *produced) {
  if (produced)
    *produced = 0;
  if (!in || !out || n < 6)
    return false;
  unsigned cmf = in[0], flg = in[1];
  if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 || (flg & 32))
    return false;
  InflateState s;
  s.in = in + 2;
  s.n = n - 2;
  s.out = out;
  s.cap = cap;
  if (!inflate_raw(s))
    return false;
  // Adler-32 trailer, byte aligned.
  if (s.n - s.pos < 4)
    return false;
  uint32_t expect = (uint32_t(s.in[s.pos]) << 24) | (uint32_t(s.in[s.pos + 1]) << 16) |
                    (uint32_t(s.in[s.pos + 2]) << 8) | s.in[s.pos + 3];
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < s.len; ++i) {
    a = (a + out[i]) % 65521;
    b = (b + a) % 65521;
  }
  if (((b << 16) | a) != expect)
    return false;
  if (produced)
    *produced = s.len;
  return true;
}
