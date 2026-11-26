#pragma once
/*
 * Grajek - parser RIFF/WAVE i konwersja probek do PCM S16 (bez liczb zmiennoprzecinkowych).
 *
 * Obslugiwane: WAVE_FORMAT_PCM (1) 8/16/24/32 bit, WAVE_FORMAT_IEEE_FLOAT (3) 32 bit, WAVE_FORMAT_EXTENSIBLE
 * (0xFFFE) z podformatem PCM albo IEEE float; 1 albo 2 kanaly; 8000..192000 Hz. Inne formaty (ADPCM, A-law,
 * mu-law, MPEG w WAV...) sa odrzucane z czytelnym komunikatem.
 *
 * Walidacja: RIFF/WAVE, kazdy fragment (chunk) miesci sie w pliku (rozmiar 64-bit, wyrownanie do 2 bajtow),
 * fmt przed data albo po (dowolna kolejnosc), nieznane fragmenty sa pomijane, limit liczby fragmentow
 * (zlosliwy plik nie zapetli parsera), block_align == kanaly * bajty_probki, fragment data przyciety do
 * rozmiaru pliku i do pelnych ramek. Metadane z LIST/INFO (INAM, IART, IPRD, ICRD, ITRK/IPRT).
 */
#include "metadane.h"
#include "zrodlo.h"

namespace Grajek::Wav {

enum class Blad : uint8_t { OK, ZaKrotki, NieRiff, BrakFmt, BrakDanych, ZlyFmt, Kompresja, Kanaly, Bity, Czestotliwosc, Odczyt };

inline const char* opis(Blad b) {
    switch (b) {
        case Blad::OK: return "OK";
        case Blad::ZaKrotki: return "Plik WAV jest za krótki.";
        case Blad::NieRiff: return "To nie jest plik RIFF/WAVE.";
        case Blad::BrakFmt: return "Brak opisu formatu (fmt) w pliku WAV.";
        case Blad::BrakDanych: return "Brak danych dźwięku w pliku WAV.";
        case Blad::ZlyFmt: return "Uszkodzony opis formatu WAV.";
        case Blad::Kompresja: return "Nieobsługiwana kompresja WAV (tylko PCM i float).";
        case Blad::Kanaly: return "Nieobsługiwana liczba kanałów (tylko mono i stereo).";
        case Blad::Bity: return "Nieobsługiwana głębia bitowa WAV.";
        case Blad::Czestotliwosc: return "Nieobsługiwana częstotliwość próbkowania.";
        case Blad::Odczyt: return "Błąd odczytu pliku.";
    }
    return "Nieznany błąd WAV.";
}

constexpr uint32_t MIN_CZESTOTLIWOSC = 8000;
constexpr uint32_t MAKS_CZESTOTLIWOSC = 192000;
constexpr uint32_t MAKS_FRAGMENTOW = 128;
constexpr uint32_t MAKS_LIST = 8192;

struct Info {
    uint16_t format = 0;          // 1 = PCM, 3 = float (po rozwinieciu EXTENSIBLE)
    uint16_t kanaly = 0;
    uint32_t czestotliwosc = 0;
    uint16_t bity = 0;
    uint16_t blok = 0;            // bajty na ramke
    uint64_t dane_offset = 0;
    uint64_t dane_rozmiar = 0;    // przyciete do pelnych ramek i do rozmiaru pliku
    uint64_t ramki = 0;
    bool przyciety = false;       // fragment data deklarowal wiecej niz jest w pliku
};

// Podformat KSDATAFORMAT_SUBTYPE_xxx: xxxxxxxx-0000-0010-8000-00aa00389b71
inline bool guid_standardowy(const uint8_t* g, uint16_t* kod) {
    static constexpr uint8_t ogon[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    for (int i = 0; i < 14; ++i) if (g[2 + i] != ogon[i]) return false;
    *kod = le16(g);
    return true;
}

// Porownanie 4-znakowego identyfikatora fragmentu RIFF.
inline bool id4(const uint8_t* p, const char* id) { return p[0] == id[0] && p[1] == id[1] && p[2] == id[2] && p[3] == id[3]; }

inline void czytaj_info(Czytnik& c, uint64_t start, uint32_t rozm, Metadane& m) {
    if (rozm < 4) return;
    if (rozm > MAKS_LIST) rozm = MAKS_LIST;
    uint8_t buf[MAKS_LIST];
    if (!c.czytaj_w(start, buf, rozm)) return;
    if (buf[0] != 'I' || buf[1] != 'N' || buf[2] != 'F' || buf[3] != 'O') return;
    uint32_t p = 4;
    for (uint32_t licznik = 0; licznik < 64 && p + 8 <= rozm; ++licznik) {
        const uint8_t* id = buf + p;
        const uint32_t dl = le32(buf + p + 4);
        if (dl > rozm - p - 8) break;
        const uint8_t* v = buf + p + 8;
        char* cel = nullptr; size_t cap = 0; bool cyfry = false;
        if (id4(id, "INAM")) { cel = m.tytul; cap = sizeof(m.tytul); }
        else if (id4(id, "IART")) { cel = m.wykonawca; cap = sizeof(m.wykonawca); }
        else if (id4(id, "IPRD")) { cel = m.album; cap = sizeof(m.album); }
        else if (id4(id, "ICRD")) { cel = m.rok; cap = sizeof(m.rok); cyfry = true; }
        else if (id4(id, "ITRK") || id4(id, "IPRT")) { cel = m.numer; cap = sizeof(m.numer); cyfry = true; }
        if (cel && !cel[0] && dl > 0) {
            // RIFF INFO nie deklaruje kodowania: poprawny UTF-8 przyjmujemy wprost, inaczej Latin-1.
            bool utf8 = true;
            for (uint32_t i = 0; i < dl && v[i];) {
                const size_t k = sekwencja_utf8(v + i, dl - i);
                if (!k) { utf8 = false; break; }
                i += uint32_t(k);
            }
            char tmp[MAKS_POLE_TEKSTU];
            if (utf8) { Tekst t(tmp, sizeof(tmp)); t.dodaj_utf8(v, dl); }
            else latin1_do_utf8(v, dl, tmp, sizeof(tmp));
            przytnij(tmp);
            if (cyfry) {
                size_t i = 0, k = 0;
                while (tmp[i] && !cyfra(uint8_t(tmp[i]))) ++i;
                while (tmp[i] && cyfra(uint8_t(tmp[i])) && k < 4 && k + 1 < cap) cel[k++] = tmp[i++];
                cel[k] = '\0';
            } else kopiuj_tekst(cel, cap, tmp);
        }
        p += 8 + dl + (dl & 1);
    }
}

inline Blad parsuj(Czytnik& c, Info& out, Metadane* meta) {
    out = Info{};
    if (c.rozmiar() < 12 + 8 + 16) return Blad::ZaKrotki;
    uint8_t h[12];
    if (!c.czytaj_w(0, h, 12)) return Blad::Odczyt;
    if (!id4(h, "RIFF") || !id4(h + 8, "WAVE")) return Blad::NieRiff;
    bool ma_fmt = false, ma_dane = false;
    uint64_t pos = 12;
    const uint64_t koniec = c.rozmiar();
    for (uint32_t licznik = 0; licznik < MAKS_FRAGMENTOW && pos + 8 <= koniec; ++licznik) {
        uint8_t f[8];
        if (!c.czytaj_w(pos, f, 8)) return Blad::Odczyt;
        const uint64_t rozm = le32(f + 4);
        const uint64_t dane = pos + 8;
        if (id4(f, "fmt ")) {
            if (rozm < 16 || rozm > koniec - dane) return Blad::ZlyFmt;
            uint8_t b[40] = {};
            const uint32_t n = rozm < 40 ? uint32_t(rozm) : 40;
            if (!c.czytaj_w(dane, b, n)) return Blad::Odczyt;
            uint16_t tag = le16(b);
            out.kanaly = le16(b + 2);
            out.czestotliwosc = le32(b + 4);
            out.blok = le16(b + 12);
            out.bity = le16(b + 14);
            if (tag == 0xFFFE) {
                if (n < 40 || le16(b + 16) < 22) return Blad::ZlyFmt;
                const uint16_t poprawne = le16(b + 18);
                uint16_t pod = 0;
                if (!guid_standardowy(b + 24, &pod)) return Blad::Kompresja;
                if (poprawne && poprawne > out.bity) return Blad::ZlyFmt;
                tag = pod;
            }
            if (tag != 1 && tag != 3) return Blad::Kompresja;
            out.format = tag;
            ma_fmt = true;
        } else if (id4(f, "data")) {
            uint64_t dost = koniec - dane;
            uint64_t d = rozm;
            if (d > dost) { d = dost; out.przyciety = true; }
            out.dane_offset = dane;
            out.dane_rozmiar = d;
            ma_dane = true;
            if (ma_fmt && !meta) break;
        } else if (id4(f, "LIST") && meta && rozm <= koniec - dane) {
            czytaj_info(c, dane, uint32_t(rozm > MAKS_LIST ? MAKS_LIST : rozm), *meta);
        }
        // nastepny fragment (wyrownanie do 2 bajtow); fragment wychodzacy poza plik konczy przeglad
        if (rozm > koniec - dane) break;
        pos = dane + rozm + (rozm & 1);
    }
    if (!ma_fmt) return Blad::BrakFmt;
    if (!ma_dane) return Blad::BrakDanych;
    if (out.kanaly < 1 || out.kanaly > 2) return Blad::Kanaly;
    if (out.czestotliwosc < MIN_CZESTOTLIWOSC || out.czestotliwosc > MAKS_CZESTOTLIWOSC) return Blad::Czestotliwosc;
    if (out.format == 1 && out.bity != 8 && out.bity != 16 && out.bity != 24 && out.bity != 32) return Blad::Bity;
    if (out.format == 3 && out.bity != 32) return Blad::Bity;
    if (out.blok != out.kanaly * (out.bity / 8)) return Blad::ZlyFmt;
    out.ramki = out.dane_rozmiar / out.blok;
    out.dane_rozmiar = out.ramki * out.blok;
    if (!out.ramki) return Blad::BrakDanych;
    return Blad::OK;
}

inline int16_t nasyc16(int32_t v) { return v > 32767 ? int16_t(32767) : v < -32768 ? int16_t(-32768) : int16_t(v); }

// IEEE 754 binary32 -> S16 wylacznie arytmetyka calkowita (Ring 3 domyslnie bez SSE). Zaokraglenie do najblizszej,
// NaN -> 0, nieskonczonosci i |x| >= 1 -> nasycenie.
inline int16_t float_do_s16(uint32_t bity) {
    const bool ujemna = bity >> 31;
    const int32_t wyk = int32_t((bity >> 23) & 0xFF);
    const uint32_t mant = bity & 0x7FFFFF;
    if (wyk == 0xFF) return mant ? int16_t(0) : (ujemna ? int16_t(-32768) : int16_t(32767));
    if (wyk == 0) return 0;                                  // subnormalne: ponizej 1 LSB
    // wartosc = 1.mant * 2^(wyk-127); wynik = wartosc * 32768
    const uint64_t m = uint64_t(mant | 0x800000);            // Q23
    const int32_t przes = (wyk - 127) + 15 - 23;             // * 2^15, / 2^23
    uint64_t v;
    if (przes >= 0) { if (przes > 16) v = 1ULL << 40; else v = m << przes; }
    else { const int32_t s = -przes; v = s >= 63 ? 0 : (m + (1ULL << (s - 1))) >> s; }
    if (ujemna) return v >= 32768 ? int16_t(-32768) : int16_t(-int32_t(v));
    return v >= 32767 ? int16_t(32767) : int16_t(v);
}

// Konwersja `ramki` ramek z formatu pliku do przeplatanych S16 (kanaly bez zmian).
inline void konwertuj(const Info& f, const uint8_t* we, uint32_t ramki, int16_t* wy) {
    const uint32_t n = ramki * f.kanaly;
    if (f.format == 3) {
        for (uint32_t i = 0; i < n; ++i) wy[i] = float_do_s16(le32(we + 4 * i));
        return;
    }
    switch (f.bity) {
        case 8: for (uint32_t i = 0; i < n; ++i) wy[i] = int16_t((int32_t(we[i]) - 128) * 256); break;
        case 16: for (uint32_t i = 0; i < n; ++i) wy[i] = int16_t(le16(we + 2 * i)); break;
        case 24:
            for (uint32_t i = 0; i < n; ++i) {
                const uint8_t* p = we + 3 * i;
                int32_t v = int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24)) >> 8;
                wy[i] = nasyc16((v + 128) >> 8);
            }
            break;
        case 32:
            for (uint32_t i = 0; i < n; ++i) {
                const int64_t v = int32_t(le32(we + 4 * i));
                wy[i] = nasyc16(int32_t((v + 32768) >> 16));
            }
            break;
        default: for (uint32_t i = 0; i < n; ++i) wy[i] = 0; break;
    }
}

} // namespace Grajek::Wav
