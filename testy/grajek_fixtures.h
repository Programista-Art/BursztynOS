#pragma once
/*
 * Grajek - generator ORYGINALNYCH plikow testowych (WAV i MP3) dla testow hosta. Zadnej muzyki z zewnatrz.
 *
 * WAV: ton sinusoidalny w dowolnym formacie (8/16/24/32-bit PCM, float32, EXTENSIBLE, mono/stereo, rozne
 *      czestotliwosci) + opcjonalny fragment LIST/INFO i nieznane fragmenty.
 * MP3: prawdziwe ramki MPEG-1 albo MPEG-2 Layer III zbudowane bit po bicie: brak wspolczynnikow skali, big_values = 0,
 *      a w obszarze count1 (tablica B) jedna niezerowa linia widma na kanal. Dekoder zgodny z ISO 11172-3 odtwarza
 *      z niej ton o czestotliwosci linia * fs / 1152 (zmierzone: linia 24 przy 48 kHz = 1000 Hz) - test sprawdza wiec rzeczywiste dekodowanie
 *      (Huffman, dekwantyzacja, IMDCT, bank filtrow), a rozne linie w L i P sprawdzaja kolejnosc kanalow.
 *      Opcjonalnie: ramka Xing (VBR), tag ID3v2.3/2.4 (UTF-8/UTF-16, APIC PNG) i ID3v1.
 */
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace GrajekFixtures {

using Bajty = std::vector<uint8_t>;

inline void le16(Bajty& b, uint32_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
inline void le32(Bajty& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
inline void be32(Bajty& b, uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back(uint8_t(v >> (8 * i))); }
inline void tekst(Bajty& b, const char* s) { while (*s) b.push_back(uint8_t(*s++)); }

struct ParametryWav {
    uint32_t czestotliwosc = 44100;
    uint16_t kanaly = 2;
    uint16_t bity = 16;
    bool zmiennoprzecinkowy = false;
    bool extensible = false;
    uint32_t ramki = 4410;
    double hz_l = 1000.0, hz_p = 2000.0, amplituda = 0.5;
    bool info = false;
    bool nieznany_fragment = false;
};

inline double probka(const ParametryWav& p, uint32_t i, int k) {
    const double hz = k == 0 ? p.hz_l : p.hz_p;
    return p.amplituda * std::sin(2.0 * M_PI * hz * double(i) / double(p.czestotliwosc));
}

inline Bajty wav(const ParametryWav& p) {
    Bajty fmt, dane, b;
    const uint16_t blok = uint16_t(p.kanaly * (p.bity / 8));
    le16(fmt, p.extensible ? 0xFFFE : (p.zmiennoprzecinkowy ? 3 : 1));
    le16(fmt, p.kanaly); le32(fmt, p.czestotliwosc); le32(fmt, p.czestotliwosc * blok); le16(fmt, blok); le16(fmt, p.bity);
    if (p.extensible) {
        le16(fmt, 22); le16(fmt, p.bity); le32(fmt, p.kanaly == 2 ? 3 : 4);
        le16(fmt, p.zmiennoprzecinkowy ? 3 : 1);
        const uint8_t ogon[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        b.clear(); fmt.insert(fmt.end(), ogon, ogon + 14);
    }
    for (uint32_t i = 0; i < p.ramki; ++i)
        for (int k = 0; k < p.kanaly; ++k) {
            const double v = probka(p, i, k);
            if (p.zmiennoprzecinkowy) { float f = float(v); uint32_t u; std::memcpy(&u, &f, 4); le32(dane, u); }
            else if (p.bity == 8) dane.push_back(uint8_t(int(std::lround(v * 127.0)) + 128));
            else if (p.bity == 16) le16(dane, uint16_t(int16_t(std::lround(v * 32767.0))));
            else if (p.bity == 24) { const int32_t s = int32_t(std::lround(v * 8388607.0)); dane.push_back(uint8_t(s)); dane.push_back(uint8_t(s >> 8)); dane.push_back(uint8_t(s >> 16)); }
            else if (p.bity == 32) le32(dane, uint32_t(int32_t(std::llround(v * 2147483647.0))));
        }
    b.clear();
    tekst(b, "RIFF"); le32(b, 0); tekst(b, "WAVE");
    if (p.nieznany_fragment) { tekst(b, "junk"); le32(b, 3); b.push_back(1); b.push_back(2); b.push_back(3); b.push_back(0); }
    tekst(b, "fmt "); le32(b, uint32_t(fmt.size())); b.insert(b.end(), fmt.begin(), fmt.end());
    if (p.info) {
        Bajty l; tekst(l, "INFO");
        auto pole = [&](const char* id, const char* v) {
            tekst(l, id); const uint32_t n = uint32_t(std::strlen(v) + 1); le32(l, n); tekst(l, v); l.push_back(0); if (n & 1) l.push_back(0);
        };
        pole("INAM", "Żółta łódź"); pole("IART", "Kwartet Bursztynowy"); pole("IPRD", "Testy Grajka"); pole("ICRD", "2026-11-21"); pole("ITRK", "7");
        tekst(b, "LIST"); le32(b, uint32_t(l.size())); b.insert(b.end(), l.begin(), l.end());
    }
    tekst(b, "data"); le32(b, uint32_t(dane.size())); b.insert(b.end(), dane.begin(), dane.end());
    if (dane.size() & 1) b.push_back(0);
    const uint32_t riff = uint32_t(b.size() - 8);
    b[4] = uint8_t(riff); b[5] = uint8_t(riff >> 8); b[6] = uint8_t(riff >> 16); b[7] = uint8_t(riff >> 24);
    return b;
}

// ---------------------------------------------------------------- MP3
class Bity {
public:
    void zapisz(uint32_t v, int n) { for (int i = n - 1; i >= 0; --i) bit((v >> i) & 1); }
    void bit(uint32_t b) { if (n_ % 8 == 0) d_.push_back(0); if (b) d_.back() |= uint8_t(0x80 >> (n_ % 8)); ++n_; }
    size_t liczba() const { return n_; }
    Bajty& dane() { return d_; }
private:
    Bajty d_;
    size_t n_ = 0;
};

struct ParametryMp3 {
    bool mpeg1 = true;            // false = MPEG-2 (LSF)
    uint32_t czestotliwosc = 48000;   // MPEG-1: 32000/44100/48000; MPEG-2: 16000/22050/24000
    bool stereo = true;
    uint32_t ramki = 40;
    int linia_l = 24, linia_p = 48;   // linie widma (0..575) niosace ton
    int wzmocnienie = 200;            // global_gain (200 ~ -15 dBFS)
    bool xing = false;
    bool zmienna_przeplywnosc = false;  // naprzemienne przeplywnosci (VBR)
};

inline int indeks_czestotliwosci(const ParametryMp3& p) {
    if (p.mpeg1) return p.czestotliwosc == 44100 ? 0 : p.czestotliwosc == 48000 ? 1 : 2;
    return p.czestotliwosc == 22050 ? 0 : p.czestotliwosc == 24000 ? 1 : 2;
}

// Zwraca bajty jednej ramki. przeplywnosc_idx: indeks tablicy przeplywnosci.
inline Bajty ramka_mp3(const ParametryMp3& p, int przeplywnosc_idx, bool cisza, const Bajty* xing_tresc = nullptr) {
    static const int BR1[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const int BR2[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    const int br = p.mpeg1 ? BR1[przeplywnosc_idx] : BR2[przeplywnosc_idx];
    const int dl = (p.mpeg1 ? 144000 : 72000) * br / int(p.czestotliwosc);
    const int kan = p.stereo ? 2 : 1;
    const int granule = p.mpeg1 ? 2 : 1;
    Bity s;
    // naglowek
    s.zapisz(0x7FF, 11); s.zapisz(p.mpeg1 ? 3 : 2, 2); s.zapisz(1, 2); s.zapisz(1, 1);
    s.zapisz(uint32_t(przeplywnosc_idx), 4); s.zapisz(uint32_t(indeks_czestotliwosci(p)), 2); s.zapisz(0, 1); s.zapisz(0, 1);
    s.zapisz(p.stereo ? 0 : 3, 2); s.zapisz(0, 2); s.zapisz(0, 1); s.zapisz(0, 1); s.zapisz(0, 2);
    // dlugosci czesci 3 (count1, tablica B): (linia/4) zerowych czworek po 4 bity + czworka z jedynka (4 bity) + znak
    auto dlugosc_czesci3 = [&](int linia) { return cisza ? 0 : (linia / 4) * 4 + 4 + 1; };
    // informacje boczne
    if (p.mpeg1) { s.zapisz(0, 9); s.zapisz(0, p.stereo ? 3 : 5); for (int k = 0; k < kan; ++k) s.zapisz(0, 4); }
    else { s.zapisz(0, 8); s.zapisz(0, p.stereo ? 2 : 1); }
    for (int g = 0; g < granule; ++g)
        for (int k = 0; k < kan; ++k) {
            const int linia = k == 0 ? p.linia_l : p.linia_p;
            s.zapisz(uint32_t(dlugosc_czesci3(linia)), 12);   // part2_3_length
            s.zapisz(0, 9);                                   // big_values
            s.zapisz(uint32_t(p.wzmocnienie), 8);             // global_gain
            s.zapisz(0, p.mpeg1 ? 4 : 9);                     // scalefac_compress
            s.zapisz(0, 1);                                   // window_switching_flag
            s.zapisz(0, 5); s.zapisz(0, 5); s.zapisz(0, 5);  // table_select
            s.zapisz(0, 4); s.zapisz(0, 3);                   // region0/1_count
            if (p.mpeg1) s.zapisz(0, 1);                      // preflag
            s.zapisz(0, 1);                                   // scalefac_scale
            s.zapisz(1, 1);                                   // count1table_select = B
        }
    // dane glowne
    if (xing_tresc) {
        for (uint8_t c : *xing_tresc) s.zapisz(c, 8);
    } else {
        for (int g = 0; g < granule; ++g)
            for (int k = 0; k < kan; ++k) {
                if (cisza) continue;
                const int linia = k == 0 ? p.linia_l : p.linia_p;
                for (int q = 0; q < linia / 4; ++q) s.zapisz(0xF, 4);           // (0,0,0,0)
                const int poz = linia % 4;                                       // v,w,x,y
                s.zapisz(uint32_t(~(8 >> poz)) & 0xF, 4);
                s.zapisz(0, 1);                                                  // znak +
            }
    }
    while (s.liczba() < size_t(dl) * 8) s.zapisz(0, 1);
    Bajty b = s.dane();
    b.resize(size_t(dl));
    return b;
}

inline Bajty mp3(const ParametryMp3& p) {
    Bajty b;
    const int idx_a = p.mpeg1 ? 9 : 8;    // 128 kbit/s (MPEG-1) / 64 kbit/s (MPEG-2)
    const int idx_b = p.mpeg1 ? 11 : 10;  // 192 / 96
    if (p.xing) {
        Bajty x; tekst(x, "Xing"); be32(x, 1); be32(x, p.ramki);
        // ramka Xing: informacje boczne zerowe, dane glowne zaczynaja sie od "Xing"
        Bajty r = ramka_mp3(p, idx_a, true, &x);
        b.insert(b.end(), r.begin(), r.end());
    }
    for (uint32_t i = 0; i < p.ramki; ++i) {
        const int idx = p.zmienna_przeplywnosc && (i & 1) ? idx_b : idx_a;
        Bajty r = ramka_mp3(p, idx, false);
        b.insert(b.end(), r.begin(), r.end());
    }
    return b;
}

// ---------------------------------------------------------------- PNG / ID3
inline Bajty png(uint32_t w, uint32_t h, uint32_t rgb) {
    auto crc_chunk = [](Bajty& out, const char* typ, const Bajty& d) {
        Bajty c; tekst(c, typ); c.insert(c.end(), d.begin(), d.end());
        be32(out, uint32_t(d.size())); out.insert(out.end(), c.begin(), c.end());
        be32(out, uint32_t(crc32(0, c.data(), uInt(c.size()))));
    };
    Bajty b = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bajty ihdr; be32(ihdr, w); be32(ihdr, h); ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    crc_chunk(b, "IHDR", ihdr);
    Bajty raw;
    for (uint32_t y = 0; y < h; ++y) { raw.push_back(0); for (uint32_t x = 0; x < w; ++x) { raw.push_back(uint8_t(rgb >> 16)); raw.push_back(uint8_t(rgb >> 8)); raw.push_back(uint8_t(rgb)); } }
    uLongf zn = compressBound(uLong(raw.size()));
    Bajty z(zn);
    compress(z.data(), &zn, raw.data(), uLong(raw.size()));
    z.resize(zn);
    crc_chunk(b, "IDAT", z);
    crc_chunk(b, "IEND", {});
    return b;
}

inline void syncsafe(Bajty& b, uint32_t v) { b.push_back(uint8_t((v >> 21) & 0x7F)); b.push_back(uint8_t((v >> 14) & 0x7F)); b.push_back(uint8_t((v >> 7) & 0x7F)); b.push_back(uint8_t(v & 0x7F)); }

struct RamkaId3 { std::string id; Bajty tresc; };

inline RamkaId3 tekst_utf8(const char* id, const char* s) { RamkaId3 r{id, {3}}; tekst(r.tresc, s); return r; }
inline RamkaId3 tekst_utf16(const char* id, const char16_t* s) {
    RamkaId3 r{id, {1, 0xFF, 0xFE}};
    for (; *s; ++s) { r.tresc.push_back(uint8_t(*s)); r.tresc.push_back(uint8_t(*s >> 8)); }
    return r;
}
inline RamkaId3 apic(const Bajty& obraz, const char* mime = "image/png", uint8_t typ = 3) {
    RamkaId3 r{"APIC", {0}}; tekst(r.tresc, mime); r.tresc.push_back(0); r.tresc.push_back(typ);
    tekst(r.tresc, "Okladka"); r.tresc.push_back(0);
    r.tresc.insert(r.tresc.end(), obraz.begin(), obraz.end());
    return r;
}

inline Bajty id3v2(uint8_t wersja, const std::vector<RamkaId3>& ramki, uint32_t dopelnienie = 64) {
    Bajty t;
    for (const auto& r : ramki) {
        tekst(t, r.id.c_str());
        if (wersja == 4) syncsafe(t, uint32_t(r.tresc.size())); else be32(t, uint32_t(r.tresc.size()));
        t.push_back(0); t.push_back(0);
        t.insert(t.end(), r.tresc.begin(), r.tresc.end());
    }
    t.insert(t.end(), dopelnienie, 0);
    Bajty b; tekst(b, "ID3"); b.push_back(wersja); b.push_back(0); b.push_back(0);
    syncsafe(b, uint32_t(t.size()));
    b.insert(b.end(), t.begin(), t.end());
    return b;
}

inline Bajty id3v1(const char* tytul, const char* wykonawca, uint8_t numer) {
    Bajty b(128, 0);
    std::memcpy(b.data(), "TAG", 3);
    std::strncpy(reinterpret_cast<char*>(b.data() + 3), tytul, 30);
    std::strncpy(reinterpret_cast<char*>(b.data() + 33), wykonawca, 30);
    std::memcpy(b.data() + 93, "1999", 4);
    b[126] = numer;
    b[127] = 255;
    return b;
}

// Pomocnik analizy: amplituda skladowej o czestotliwosci hz (Goertzel) w kanale k przeplatanego stereo.
inline double goertzel(const std::vector<int16_t>& pcm, int kanaly, int k, double hz, double fs, size_t start = 0, size_t n = 0) {
    const size_t ramki = pcm.size() / size_t(kanaly);
    if (!n || start + n > ramki) n = ramki > start ? ramki - start : 0;
    const double w = 2.0 * M_PI * hz / fs, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) { const double s0 = pcm[(start + i) * size_t(kanaly) + size_t(k)] + c * s1 - s2; s2 = s1; s1 = s0; }
    const double moc = s1 * s1 + s2 * s2 - c * s1 * s2;
    return n ? 2.0 * std::sqrt(moc > 0 ? moc : 0) / double(n) : 0.0;
}

} // namespace GrajekFixtures
