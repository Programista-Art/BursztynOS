#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * Bursztyn OS - Dekoder PNG (obrazy/png_dekoder.h)
 *
 * Lekki, w pelni samodzielny dekoder formatu PNG (RFC 2083) z obsluga
 * dekompresji DEFLATE (RFC 1951) w trybie freestanding.
 *
 * - Brak zaleznosci od biblioteki standardowej C/C++ (brak STL, brak libc).
 * - Obsluga PNG 8-bit RGBA (typ koloru 6) oraz RGB (typ koloru 2).
 * - Dokladna obsluga wszystkich 5 filtrow PNG (None, Sub, Up, Average, Paeth).
 * - Przeksztalcanie do natywnego 32-bitowego formatu pikseli ARGB Bursztyn OS.
 * - Zachowanie kanalu alfa (0 = przezroczysty, 255 = pelny).
 */

namespace BursztynPNG {

struct TinfTree {
    uint16_t counts[16];
    uint16_t symbols[288];
};

struct TinfBitStream {
    const uint8_t* source;
    size_t sourceLen;
    size_t sourcePos;
    uint32_t tag;
    int bitcount;
};

inline void tinf_init_bitstream(TinfBitStream* stream, const uint8_t* source, size_t sourceLen) {
    stream->source = source;
    stream->sourceLen = sourceLen;
    stream->sourcePos = 0;
    stream->tag = 0;
    stream->bitcount = 0;
}

inline uint32_t tinf_getbit(TinfBitStream* stream) {
    if (stream->bitcount == 0) {
        if (stream->sourcePos < stream->sourceLen) {
            stream->tag = stream->source[stream->sourcePos++];
        } else {
            stream->tag = 0;
        }
        stream->bitcount = 8;
    }
    uint32_t bit = stream->tag & 1U;
    stream->tag >>= 1U;
    stream->bitcount--;
    return bit;
}

inline uint32_t tinf_getbits(TinfBitStream* stream, int num) {
    uint32_t res = 0;
    for (int i = 0; i < num; ++i) {
        res |= (tinf_getbit(stream) << i);
    }
    return res;
}

inline void tinf_build_tree(TinfTree* tree, const uint8_t* lengths, unsigned int num) {
    uint16_t offsets[16];
    for (unsigned int i = 0; i < 16; ++i) tree->counts[i] = 0;
    for (unsigned int i = 0; i < num; ++i) tree->counts[lengths[i]]++;
    tree->counts[0] = 0;
    uint16_t sum = 0;
    for (unsigned int i = 0; i < 16; ++i) {
        offsets[i] = sum;
        sum += tree->counts[i];
    }
    for (unsigned int i = 0; i < num; ++i) {
        if (lengths[i]) {
            tree->symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
        }
    }
}

inline int tinf_decode_symbol(TinfBitStream* stream, const TinfTree* tree) {
    int cur = 0;
    int sum = 0;
    for (int len = 1; len <= 15; ++len) {
        cur |= static_cast<int>(tinf_getbit(stream));
        int count = tree->counts[len];
        if (cur < count) {
            return tree->symbols[sum + cur];
        }
        cur -= count;
        sum += count;
        cur <<= 1;
    }
    return -1;
}

inline void tinf_build_fixed_trees(TinfTree* ltree, TinfTree* dtree) {
    uint8_t lengths[288];
    for (int i = 0; i <= 143; ++i) lengths[i] = 8;
    for (int i = 144; i <= 255; ++i) lengths[i] = 9;
    for (int i = 256; i <= 279; ++i) lengths[i] = 7;
    for (int i = 280; i <= 287; ++i) lengths[i] = 8;
    tinf_build_tree(ltree, lengths, 288);
    for (int i = 0; i < 30; ++i) lengths[i] = 5;
    tinf_build_tree(dtree, lengths, 30);
}

inline int tinf_uncompress(uint8_t* dest, size_t* destLen, const uint8_t* source, size_t sourceLen) {
    static const uint16_t length_base[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
        15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
        67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const uint8_t length_extra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
        1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
        4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static const uint16_t dist_base[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25,
        33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    static const uint8_t dist_extra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
        4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static const uint8_t clc_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    TinfBitStream stream;
    tinf_init_bitstream(&stream, source, sourceLen);
    size_t outPos = 0;
    size_t outCap = *destLen;
    int bfinal = 0;

    TinfTree fixed_ltree, fixed_dtree;
    int fixed_built = 0;

    while (!bfinal) {
        bfinal = static_cast<int>(tinf_getbit(&stream));
        int btype = static_cast<int>(tinf_getbits(&stream, 2));

        if (btype == 0) {
            stream.bitcount = 0;
            stream.tag = 0;
            if (stream.sourcePos + 4 > stream.sourceLen) return -1;
            uint16_t len = static_cast<uint16_t>(stream.source[stream.sourcePos] | (stream.source[stream.sourcePos + 1] << 8));
            uint16_t nlen = static_cast<uint16_t>(stream.source[stream.sourcePos + 2] | (stream.source[stream.sourcePos + 3] << 8));
            stream.sourcePos += 4;
            if (static_cast<uint16_t>(~len) != nlen) return -1;
            if (stream.sourcePos + len > stream.sourceLen || outPos + len > outCap) return -1;
            for (size_t i = 0; i < len; ++i) {
                dest[outPos + i] = stream.source[stream.sourcePos + i];
            }
            stream.sourcePos += len;
            outPos += len;
        } else if (btype == 1 || btype == 2) {
            TinfTree ltree, dtree;
            const TinfTree* p_ltree = nullptr;
            const TinfTree* p_dtree = nullptr;

            if (btype == 1) {
                if (!fixed_built) {
                    tinf_build_fixed_trees(&fixed_ltree, &fixed_dtree);
                    fixed_built = 1;
                }
                p_ltree = &fixed_ltree;
                p_dtree = &fixed_dtree;
            } else {
                int hlit = static_cast<int>(tinf_getbits(&stream, 5)) + 257;
                int hdist = static_cast<int>(tinf_getbits(&stream, 5)) + 1;
                int hclen = static_cast<int>(tinf_getbits(&stream, 4)) + 4;

                uint8_t cl_lengths[19] = {0};
                for (int i = 0; i < hclen; ++i) {
                    cl_lengths[clc_order[i]] = static_cast<uint8_t>(tinf_getbits(&stream, 3));
                }
                TinfTree cltree;
                tinf_build_tree(&cltree, cl_lengths, 19);

                uint8_t lengths[288 + 32] = {0};
                int total_codes = hlit + hdist;
                int num = 0;
                while (num < total_codes) {
                    int sym = tinf_decode_symbol(&stream, &cltree);
                    if (sym < 0) return -1;
                    if (sym < 16) {
                        lengths[num++] = static_cast<uint8_t>(sym);
                    } else if (sym == 16) {
                        if (num == 0) return -1;
                        uint8_t prev = lengths[num - 1];
                        int rep = static_cast<int>(tinf_getbits(&stream, 2)) + 3;
                        if (num + rep > total_codes) return -1;
                        while (rep--) lengths[num++] = prev;
                    } else if (sym == 17) {
                        int rep = static_cast<int>(tinf_getbits(&stream, 3)) + 3;
                        if (num + rep > total_codes) return -1;
                        while (rep--) lengths[num++] = 0;
                    } else if (sym == 18) {
                        int rep = static_cast<int>(tinf_getbits(&stream, 7)) + 11;
                        if (num + rep > total_codes) return -1;
                        while (rep--) lengths[num++] = 0;
                    }
                }
                tinf_build_tree(&ltree, lengths, static_cast<unsigned int>(hlit));
                tinf_build_tree(&dtree, lengths + hlit, static_cast<unsigned int>(hdist));
                p_ltree = &ltree;
                p_dtree = &dtree;
            }

            while (true) {
                int sym = tinf_decode_symbol(&stream, p_ltree);
                if (sym < 0) return -1;
                if (sym < 256) {
                    if (outPos >= outCap) return -1;
                    dest[outPos++] = static_cast<uint8_t>(sym);
                } else if (sym == 256) {
                    break;
                } else {
                    sym -= 257;
                    if (sym >= 29) return -1;
                    uint32_t length = length_base[sym] + tinf_getbits(&stream, length_extra[sym]);
                    int dsym = tinf_decode_symbol(&stream, p_dtree);
                    if (dsym < 0 || dsym >= 30) return -1;
                    uint32_t dist = dist_base[dsym] + tinf_getbits(&stream, dist_extra[dsym]);
                    if (dist > outPos || outPos + length > outCap) return -1;
                    for (uint32_t i = 0; i < length; ++i) {
                        dest[outPos] = dest[outPos - dist];
                        outPos++;
                    }
                }
            }
        } else {
            return -1;
        }
    }

    *destLen = outPos;
    return 0;
}

inline uint8_t paeth_predictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return static_cast<uint8_t>(a);
    if (pb <= pc) return static_cast<uint8_t>(b);
    return static_cast<uint8_t>(c);
}

/*
 * Dekoduje obraz PNG z bufora pamieci do tablicy 32-bitowych pikseli ARGB.
 * Zwraca true w przypadku sukcesu, false przy bledzie.
 * Bufor wyjsciowy *out_pixels nalezy zwolnic za pomoca delete[] *out_pixels.
 */
inline bool dekoduj_png(const uint8_t* dane_pliku, size_t rozmiar_pliku,
                        uint32_t** out_pixels, int* out_w, int* out_h) {
    if (!dane_pliku || rozmiar_pliku < 33 || !out_pixels || !out_w || !out_h) return false;
    *out_pixels = nullptr;
    *out_w = 0;
    *out_h = 0;

    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    for (size_t i = 0; i < 8; ++i) {
        if (dane_pliku[i] != png_sig[i]) return false;
    }

    /* Czytanie naglowka IHDR */
    uint32_t ihdr_len = (static_cast<uint32_t>(dane_pliku[8]) << 24) |
                        (static_cast<uint32_t>(dane_pliku[9]) << 16) |
                        (static_cast<uint32_t>(dane_pliku[10]) << 8) |
                        static_cast<uint32_t>(dane_pliku[11]);
    if (ihdr_len != 13) return false;
    if (dane_pliku[12] != 'I' || dane_pliku[13] != 'H' ||
        dane_pliku[14] != 'D' || dane_pliku[15] != 'R') return false;

    uint32_t w = (static_cast<uint32_t>(dane_pliku[16]) << 24) |
                 (static_cast<uint32_t>(dane_pliku[17]) << 16) |
                 (static_cast<uint32_t>(dane_pliku[18]) << 8) |
                 static_cast<uint32_t>(dane_pliku[19]);
    uint32_t h = (static_cast<uint32_t>(dane_pliku[20]) << 24) |
                 (static_cast<uint32_t>(dane_pliku[21]) << 16) |
                 (static_cast<uint32_t>(dane_pliku[22]) << 8) |
                 static_cast<uint32_t>(dane_pliku[23]);
    uint8_t bit_depth = dane_pliku[24];
    uint8_t color_type = dane_pliku[25];
    uint8_t comp_method = dane_pliku[26];
    uint8_t filter_method = dane_pliku[27];
    uint8_t interlace_method = dane_pliku[28];

    if (bit_depth != 8 || (color_type != 6 && color_type != 2) ||
        comp_method != 0 || filter_method != 0 || interlace_method != 0) {
        return false;
    }
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    int bpp = (color_type == 6) ? 4 : 3;

    /* Zbierz sumaryczny rozmiar i sprawdz czy jest pojedynczy IDAT */
    size_t idat_total_len = 0;
    size_t idat_count = 0;
    const uint8_t* single_idat_ptr = nullptr;
    size_t pos = 8;
    while (pos + 8 <= rozmiar_pliku) {
        uint32_t chunk_len = (static_cast<uint32_t>(dane_pliku[pos]) << 24) |
                             (static_cast<uint32_t>(dane_pliku[pos + 1]) << 16) |
                             (static_cast<uint32_t>(dane_pliku[pos + 2]) << 8) |
                             static_cast<uint32_t>(dane_pliku[pos + 3]);
        if (pos + 12 + chunk_len > rozmiar_pliku) break;
        if (dane_pliku[pos + 4] == 'I' && dane_pliku[pos + 5] == 'D' &&
            dane_pliku[pos + 6] == 'A' && dane_pliku[pos + 7] == 'T') {
            idat_total_len += chunk_len;
            if (idat_count == 0) single_idat_ptr = dane_pliku + pos + 8;
            idat_count++;
        }
        pos += 12 + chunk_len;
    }
    if (idat_total_len < 6) return false;

    const uint8_t* deflate_src = nullptr;
    size_t deflate_len = idat_total_len - 6;
    uint8_t* idat_buf = nullptr;

    if (idat_count == 1 && single_idat_ptr != nullptr) {
        deflate_src = single_idat_ptr + 2;
    } else {
        idat_buf = new uint8_t[idat_total_len];
        if (!idat_buf) return false;
        size_t idat_pos = 0;
        pos = 8;
        while (pos + 8 <= rozmiar_pliku) {
            uint32_t chunk_len = (static_cast<uint32_t>(dane_pliku[pos]) << 24) |
                                 (static_cast<uint32_t>(dane_pliku[pos + 1]) << 16) |
                                 (static_cast<uint32_t>(dane_pliku[pos + 2]) << 8) |
                                 static_cast<uint32_t>(dane_pliku[pos + 3]);
            if (pos + 12 + chunk_len > rozmiar_pliku) break;
            if (dane_pliku[pos + 4] == 'I' && dane_pliku[pos + 5] == 'D' &&
                dane_pliku[pos + 6] == 'A' && dane_pliku[pos + 7] == 'T') {
                for (size_t i = 0; i < chunk_len; ++i) {
                    idat_buf[idat_pos + i] = dane_pliku[pos + 8 + i];
                }
                idat_pos += chunk_len;
            }
            pos += 12 + chunk_len;
        }
        deflate_src = idat_buf + 2;
    }

    size_t row_bytes = static_cast<size_t>(w) * static_cast<size_t>(bpp);
    size_t expected_decompressed = static_cast<size_t>(h) * (row_bytes + 1);
    uint8_t* raw_rows = new uint8_t[expected_decompressed];
    if (!raw_rows) {
        if (idat_buf) delete[] idat_buf;
        return false;
    }

    size_t out_len = expected_decompressed;
    int res = tinf_uncompress(raw_rows, &out_len, deflate_src, deflate_len);
    if (idat_buf) delete[] idat_buf;
    if (res != 0 || out_len != expected_decompressed) {
        delete[] raw_rows;
        return false;
    }

    /* Odfakturowanie (un-filter) wierszy do pikseli ARGB */
    uint32_t* pixels = new uint32_t[static_cast<size_t>(w) * static_cast<size_t>(h)];
    if (!pixels) {
        delete[] raw_rows;
        return false;
    }

    uint8_t* recon_row = new uint8_t[row_bytes];
    uint8_t* prior_row = new uint8_t[row_bytes];
    if (!recon_row || !prior_row) {
        if (recon_row) delete[] recon_row;
        if (prior_row) delete[] prior_row;
        delete[] raw_rows;
        delete[] pixels;
        return false;
    }
    for (size_t i = 0; i < row_bytes; ++i) prior_row[i] = 0;

    size_t raw_pos = 0;
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t filter_type = raw_rows[raw_pos++];
        const uint8_t* filt_line = raw_rows + raw_pos;
        raw_pos += row_bytes;

        for (size_t x = 0; x < row_bytes; ++x) {
            uint8_t x_filt = filt_line[x];
            uint8_t a = (x >= static_cast<size_t>(bpp)) ? recon_row[x - bpp] : 0;
            uint8_t b = prior_row[x];
            uint8_t c = (x >= static_cast<size_t>(bpp)) ? prior_row[x - bpp] : 0;

            uint8_t val;
            switch (filter_type) {
                case 0: val = x_filt; break;
                case 1: val = static_cast<uint8_t>(x_filt + a); break;
                case 2: val = static_cast<uint8_t>(x_filt + b); break;
                case 3: val = static_cast<uint8_t>(x_filt + ((a + b) >> 1)); break;
                case 4: val = static_cast<uint8_t>(x_filt + paeth_predictor(a, b, c)); break;
                default: val = x_filt; break;
            }
            recon_row[x] = val;
        }

        /* Konwersja do natywnego 32-bit ARGB */
        uint32_t* out_row = pixels + (static_cast<size_t>(y) * w);
        for (uint32_t x = 0; x < w; ++x) {
            size_t off = static_cast<size_t>(x) * static_cast<size_t>(bpp);
            uint8_t r = recon_row[off];
            uint8_t g = recon_row[off + 1];
            uint8_t b = recon_row[off + 2];
            uint8_t a = (color_type == 6) ? recon_row[off + 3] : 255;
            out_row[x] = (static_cast<uint32_t>(a) << 24) |
                         (static_cast<uint32_t>(r) << 16) |
                         (static_cast<uint32_t>(g) << 8)  |
                         static_cast<uint32_t>(b);
        }

        for (size_t i = 0; i < row_bytes; ++i) prior_row[i] = recon_row[i];
    }

    delete[] prior_row;
    delete[] recon_row;
    delete[] raw_rows;

    *out_pixels = pixels;
    *out_w = static_cast<int>(w);
    *out_h = static_cast<int>(h);
    return true;
}

} // namespace BursztynPNG
