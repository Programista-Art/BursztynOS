#include "../tapetybtp.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v); b[at + 1] = static_cast<uint8_t>(v >> 8);
}
static void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (i * 8));
}
static uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t c = ~0U;
    for (size_t i = 0; i < size; ++i) {
        c ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
    }
    return ~c;
}
static std::vector<uint8_t> tiny(const std::vector<uint8_t>& tokens) {
    std::vector<uint8_t> b(92 + tokens.size(), 0);
    b[0] = 'B'; b[1] = 'T'; b[2] = 'P'; b[3] = '2';
    put16(b, 4, 2); put16(b, 6, 72);
    put32(b, 8, 1); put32(b, 12, 1); b[16] = 1; b[17] = 1;
    put32(b, 20, 4); put32(b, 28, static_cast<uint32_t>(tokens.size()));
    put32(b, 36, 1); put32(b, 40, 4); put32(b, 44, 72); put32(b, 52, 92);
    put32(b, 72, 92); put32(b, 80, static_cast<uint32_t>(tokens.size()));
    put32(b, 84, 4); b[88] = 1;
    for (size_t i = 0; i < tokens.size(); ++i) b[92 + i] = tokens[i];
    put32(b, 64, crc32(b.data() + 92, tokens.size()));
    return b;
}

static std::vector<uint8_t> read(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    auto encoded = read(argv[1]);
    auto raw = read(argv[2]);
    BtpInfo info{};
    if (btpsprawdzplik(encoded.data(), encoded.size(), &info) != BTPOK || info.kompresja != 2)
        return 3;
    if (raw.size() != static_cast<size_t>(info.szerokosc) * info.wysokosc * 4) return 4;
    std::vector<uint32_t> dst(raw.size() / 4 + 2, 0x12345678U);
    if (btpwczytajirysuj(encoded.data(), encoded.size(), dst.data() + 1,
                         info.szerokosc, info.wysokosc, info.szerokosc, raw.size()) != BTPOK)
        return 5;
    if (dst.front() != 0x12345678U || dst.back() != 0x12345678U) return 6;
    auto* bytes = reinterpret_cast<const uint8_t*>(dst.data() + 1);
    for (size_t i = 0; i < raw.size(); ++i) if (bytes[i] != raw[i]) return 7;
    for (size_t offset : {size_t(4), size_t(6), size_t(16), size_t(18),
                          size_t(20), size_t(36), size_t(40), size_t(44),
                          size_t(52), size_t(64), size_t(68), size_t(72),
                          size_t(72 + 12), size_t(72 + 16), size_t(72 + 17)}) {
        auto broken = encoded;
        broken[offset] ^= 0xff;
        if (btpsprawdzplik(broken.data(), broken.size(), &info) == BTPOK)
            return 9;
    }
    auto wrong_decoded_crc = encoded;
    wrong_decoded_crc[60] ^= 1;
    if (btpsprawdzplik(wrong_decoded_crc.data(), wrong_decoded_crc.size(), &info) != BTPOK ||
        btpwczytajirysuj(wrong_decoded_crc.data(), wrong_decoded_crc.size(), dst.data() + 1,
                         info.szerokosc, info.wysokosc, info.szerokosc, raw.size()) != BTPBLADV2)
        return 10;
    for (const auto& tokens : {std::vector<uint8_t>{3, 'A', 'B', 'C'},
                               std::vector<uint8_t>{0x40, 'A'},
                               std::vector<uint8_t>{0x80, 'A', 'B', 'C', 'D'},
                               std::vector<uint8_t>{0x7f, 0, 0, 'A'},
                               std::vector<uint8_t>{0xbf, 0, 0, 'A', 'B', 'C', 'D'},
                               std::vector<uint8_t>{0xff, 0, 0, 1, 0},
                               std::vector<uint8_t>{0xc0, 0, 0},
                               std::vector<uint8_t>{0xc0, 5, 0},
                               std::vector<uint8_t>{0, 'A', 0xc1, 1, 0}}) {
        auto malformed = tiny(tokens);
        if (btpsprawdzplik(malformed.data(), malformed.size(), &info) == BTPOK)
            return 11;
    }
    auto overlap = tiny({0, 'A', 0xc0, 1, 0});
    put32(overlap, 60, crc32(reinterpret_cast<const uint8_t*>("AAAA"), 4));
    uint32_t overlap_out = 0;
    if (btpwczytajirysuj(overlap.data(), overlap.size(), &overlap_out, 1, 1, 1, 4) != BTPOK ||
        overlap_out != 0x41414141U) return 12;
    encoded.back() ^= 1;
    if (btpsprawdzplik(encoded.data(), encoded.size(), &info) == BTPOK) return 8;
    std::puts("BTP2 host decoder PASS");
    return 0;
}
