// BSF 2.2 — bounded PNG decoder for colour-bitmap strikes (CBDT/sbix).
// Included by bsf.cpp inside namespace BSF, Ring 3 only. Supports the PNG
// subset used by colour-font strikes: bit depth 1/2/4/8/16, colour types
// 0/2/3/4/6, tRNS, non-interlaced. Interlaced or malformed images are
// rejected; dimensions and decoded bytes are capped before allocation.
namespace {
// Ring 3 has no libc: byte helpers instead of mem* (no fortified calls).
void png_copy(uint8_t *d, const uint8_t *s, size_t n) { for (size_t i = 0; i < n; ++i) d[i] = s[i]; }
uint32_t png_be32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
int paeth(int a, int b, int c) {
  int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}
} // namespace
// Decodes to straight-alpha ARGB (0xAARRGGBB). `out` is new[]; caller frees.
bool decode_png(const uint8_t *in, size_t n, uint32_t *&out, unsigned &w, unsigned &h) {
  out = nullptr;
  w = h = 0;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  bool signature = true;
  for (unsigned i = 0; in && n >= 8 && i < 8; ++i) signature &= in[i] == sig[i];
  if (!in || n < 33 || !signature)
    return false;
  size_t at = 8;
  unsigned depth = 0, type = 0, palette_n = 0, trns_n = 0;
  uint8_t palette[256 * 3]{}, trns[256]{};
  uint16_t trns_gray = 0xffff, trns_rgb[3]{0xffff, 0xffff, 0xffff};
  bool header = false, ended = false;
  size_t idat_total = 0;
  // Pass 1: validate chunks, collect sizes.
  for (unsigned chunks = 0; at + 12 <= n && chunks < 4096; ++chunks) {
    uint32_t len = png_be32(in + at), type_tag = png_be32(in + at + 4);
    if (len > n - at - 12)
      return false;
    const uint8_t *d = in + at + 8;
    if (!chunks && type_tag != 0x49484452) // IHDR first
      return false;
    if (type_tag == 0x49484452) {
      if (len != 13 || header)
        return false;
      w = png_be32(d);
      h = png_be32(d + 4);
      depth = d[8];
      type = d[9];
      if (d[10] || d[11] || d[12]) // compression, filter, interlace
        return false;
      if (!w || !h || w > MaxPngSide || h > MaxPngSide || uint64_t(w) * h > MaxPngPixels)
        return false;
      bool ok = (type == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                (type == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
                ((type == 2 || type == 4 || type == 6) && (depth == 8 || depth == 16));
      if (!ok)
        return false;
      header = true;
    } else if (type_tag == 0x504c5445) { // PLTE
      if (len % 3 || len > 768)
        return false;
      palette_n = len / 3;
      png_copy(palette, d, len);
    } else if (type_tag == 0x74524e53) { // tRNS
      if (type == 3) {
        if (len > 256)
          return false;
        trns_n = len;
        png_copy(trns, d, len);
      } else if (type == 0 && len == 2)
        trns_gray = uint16_t((d[0] << 8) | d[1]);
      else if (type == 2 && len == 6)
        for (unsigned k = 0; k < 3; ++k) trns_rgb[k] = uint16_t((d[k * 2] << 8) | d[k * 2 + 1]);
    } else if (type_tag == 0x49444154) { // IDAT
      if (idat_total > 8u * 1024 * 1024 - len)
        return false;
      idat_total += len;
    } else if (type_tag == 0x49454e44) { // IEND
      ended = true;
      break;
    }
    at += 12 + size_t(len);
  }
  if (!header || !ended || !idat_total || (type == 3 && !palette_n))
    return false;
  unsigned channels = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : 4;
  size_t bits = size_t(w) * channels * depth, stride = (bits + 7) / 8, bpp = (channels * depth + 7) / 8;
  size_t raw_size = (stride + 1) * h;
  auto *idat = BSF_NEW uint8_t[idat_total];
  auto *raw = BSF_NEW uint8_t[raw_size];
  bool ok = idat && raw;
  size_t used = 0;
  for (at = 8; ok && at + 12 <= n;) {
    uint32_t len = png_be32(in + at), type_tag = png_be32(in + at + 4);
    if (type_tag == 0x49444154) {
      png_copy(idat + used, in + at + 8, len);
      used += len;
    }
    if (type_tag == 0x49454e44)
      break;
    at += 12 + size_t(len);
  }
  size_t produced = 0;
  ok = ok && inflate_zlib(idat, used, raw, raw_size, &produced) && produced == raw_size;
  delete[] idat;
  // Unfilter in place (each scanline: filter byte + stride bytes).
  for (unsigned y = 0; ok && y < h; ++y) {
    uint8_t *line = raw + y * (stride + 1), *prev = y ? raw + (y - 1) * (stride + 1) + 1 : nullptr;
    unsigned filter = line[0];
    uint8_t *p = line + 1;
    if (filter > 4) {
      ok = false;
      break;
    }
    for (size_t i = 0; i < stride; ++i) {
      int a = i >= bpp ? p[i - bpp] : 0, b = prev ? prev[i] : 0, c = prev && i >= bpp ? prev[i - bpp] : 0;
      int v = p[i];
      if (filter == 1) v += a;
      else if (filter == 2) v += b;
      else if (filter == 3) v += (a + b) >> 1;
      else if (filter == 4) v += paeth(a, b, c);
      p[i] = uint8_t(v);
    }
  }
  if (ok) {
    out = BSF_NEW uint32_t[size_t(w) * h];
    ok = out != nullptr;
  }
  for (unsigned y = 0; ok && y < h; ++y) {
    const uint8_t *p = raw + y * (stride + 1) + 1;
    for (unsigned x = 0; x < w; ++x) {
      auto sample = [&](unsigned ch) -> unsigned {
        if (depth == 16)
          return p[(size_t(x) * channels + ch) * 2];
        if (depth == 8)
          return p[size_t(x) * channels + ch];
        size_t bit = size_t(x) * depth;
        unsigned v = (p[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
        return type == 3 ? v : v * 255 / ((1u << depth) - 1);
      };
      auto sample16 = [&](unsigned ch) -> unsigned {
        if (depth == 16)
          return (unsigned(p[(size_t(x) * channels + ch) * 2]) << 8) | p[(size_t(x) * channels + ch) * 2 + 1];
        if (depth == 8)
          return p[size_t(x) * channels + ch];
        size_t bit = size_t(x) * depth;
        return (p[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
      };
      unsigned r, g, b, a = 255;
      if (type == 3) {
        unsigned i = sample(0);
        if (i >= palette_n) {
          ok = false;
          break;
        }
        r = palette[i * 3];
        g = palette[i * 3 + 1];
        b = palette[i * 3 + 2];
        a = i < trns_n ? trns[i] : 255;
      } else if (type == 0 || type == 4) {
        r = g = b = sample(0);
        if (type == 4)
          a = sample(1);
        else if (sample16(0) == trns_gray)
          a = 0;
      } else {
        r = sample(0);
        g = sample(1);
        b = sample(2);
        if (type == 6)
          a = sample(3);
        else if (sample16(0) == trns_rgb[0] && sample16(1) == trns_rgb[1] && sample16(2) == trns_rgb[2])
          a = 0;
      }
      out[size_t(y) * w + x] = (a << 24) | (r << 16) | (g << 8) | b;
    }
  }
  delete[] raw;
  if (!ok) {
    delete[] out;
    out = nullptr;
    w = h = 0;
  }
  return ok;
}
