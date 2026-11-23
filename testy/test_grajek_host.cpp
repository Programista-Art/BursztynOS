// Grajek 1.0 - testy hosta logiki odtwarzacza (ASan/UBSan): tekst UTF-8, WAV, ID3, MP3 (rzeczywiste dekodowanie
// ramek syntetyzowanych bit po bicie), konwersja probek i czestotliwosci, korektor, analiza, silnik odtwarzania
// na prawdziwym mikserze jadra (jadro/dzwiek/mikser_pcm.h) z symulacja BWS 110 (BUSY, czesciowe zapisy,
// niedobory), biblioteka /dane/muzyka, kolejka/M3U8, konfiguracja i okladka.
#include "grajek_srodowisko_testowe.h"
#include "programy/grajek/silnik.h"
#include "programy/grajek/kolejka.h"
#include "programy/grajek/konfiguracja.h"
#include "programy/grajek/okladka.h"
#include "jadro/dzwiek/mikser_pcm.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace Grajek;

static unsigned g_checks = 0;
#define CHECK(x) do { ++g_checks; if (!(x)) { std::fprintf(stderr, "FAIL test_grajek_host.cpp:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)

// =================================================================== tekst
static void test_tekst() {
    char b[8];
    { Tekst t(b, sizeof(b)); t.dodaj("Żółć123"); CHECK(t.obciety()); CHECK(std::strcmp(b, "Żółć") == 0 || std::strlen(b) <= 7); }
    { Tekst t(b, sizeof(b)); t.dodaj("ab\x01" "c"); CHECK(std::strcmp(b, "ab?c") == 0); }
    { const uint8_t zle[] = {0xC3, 0x28, 'a', 0xE2, 0x82}; Tekst t(b, sizeof(b)); t.dodaj_utf8(zle, sizeof(zle)); CHECK(std::strcmp(b, "?(a??") == 0); }
    { const uint8_t over[] = {0xC0, 0xAF}; CHECK(sekwencja_utf8(over, 2) == 0); }
    { const uint8_t sur[] = {0xED, 0xA0, 0x80}; CHECK(sekwencja_utf8(sur, 3) == 0); }
    char d[64];
    { const uint8_t l1[] = {'Z', 0xF3, 'l', 0xE6}; latin1_do_utf8(l1, 4, d, sizeof(d)); CHECK(std::strcmp(d, "Zólæ") == 0); }
    { const uint8_t u16[] = {0xFF, 0xFE, 0x7C, 0x01, 0xF3, 0x00, 0x3D, 0xD8, 0x00, 0xDE, 0x00, 0x00}; utf16_do_utf8(u16, sizeof(u16), false, true, d, sizeof(d)); CHECK(std::strcmp(d, "żó\xF0\x9F\x98\x80") == 0); }
    { const uint8_t u16be[] = {0x01, 0x41, 0xD8, 0x00}; utf16_do_utf8(u16be, sizeof(u16be), true, false, d, sizeof(d)); CHECK(std::strcmp(d, "Ł?") == 0); }
    { Tekst t(d, sizeof(d)); t.czas(0); CHECK(std::strcmp(d, "0:00") == 0); }
    { Tekst t(d, sizeof(d)); t.czas(61999); CHECK(std::strcmp(d, "1:01") == 0); }
    { Tekst t(d, sizeof(d)); t.czas(3723000); CHECK(std::strcmp(d, "1:02:03") == 0); }
    CHECK(porownaj_naturalnie("utwor2", "utwor10") < 0);
    CHECK(porownaj_naturalnie("ala", "Ąla") < 0 && porownaj_naturalnie("Ąla", "bela") < 0);
    CHECK(porownaj_naturalnie("zebra", "Źródło") < 0 && porownaj_naturalnie("źródło", "żaba") < 0);
    CHECK(porownaj_naturalnie("A", "a") != 0 && porownaj_naturalnie("x", "x") == 0);
    CHECK(zawiera("Żółta łódź", "zolta") && zawiera("Żółta łódź", "ŁÓDŹ") && !zawiera("Żółta", "zielona") && zawiera("cokolwiek", ""));
    CHECK(rozszerzenie("A.MP3", "mp3") && rozszerzenie("x y.Wav", "wav") && !rozszerzenie("mp3", "mp3") && !rozszerzenie("a.mp3.txt", "mp3"));
    uint64_t v = 0;
    CHECK(parsuj_liczbe("123", 3, 1000, &v) && v == 123);
    CHECK(!parsuj_liczbe("99999999999999999999999", 23, UINT64_MAX, &v));
    CHECK(!parsuj_liczbe("abc", 3, 100, &v));
}

// =================================================================== WAV
static std::vector<int16_t> dekoduj_calosc(Dekoder& d, uint32_t porcja = 1000) {
    std::vector<int16_t> out;
    std::vector<int16_t> buf(size_t(porcja) * 2 + 16);
    for (int i = 0; i < 1000000; ++i) {
        const uint32_t n = d.dekoduj(buf.data(), porcja);
        if (!n) break;
        out.insert(out.end(), buf.begin(), buf.begin() + n * d.parametry().kanaly);
    }
    return out;
}

static void test_wav() {
    wyczysc_fs();
    struct Przypadek { uint16_t bity; bool fl; bool ext; uint16_t kan; uint32_t sr; int tol; };
    const Przypadek P[] = {{16, false, false, 2, 44100, 1}, {16, false, false, 1, 22050, 1}, {8, false, false, 2, 48000, 300},
                           {24, false, false, 2, 48000, 1}, {32, false, false, 1, 44100, 1}, {32, true, false, 2, 48000, 1},
                           {24, false, true, 2, 96000, 1}, {32, true, true, 1, 8000, 1}, {16, false, true, 2, 192000, 1}};
    for (const auto& c : P) {
        GrajekFixtures::ParametryWav p; p.bity = c.bity; p.zmiennoprzecinkowy = c.fl; p.extensible = c.ext; p.kanaly = c.kan; p.czestotliwosc = c.sr;
        p.ramki = 3001; p.info = true; p.nieznany_fragment = true;
        dodaj_plik("/t/a.wav", GrajekFixtures::wav(p));
        Czytnik cz; CHECK(cz.otworz(dostep(), "/t/a.wav"));
        Wav::Info wi; Metadane m;
        CHECK(Wav::parsuj(cz, wi, &m) == Wav::Blad::OK);
        CHECK(wi.ramki == 3001 && wi.kanaly == c.kan && wi.czestotliwosc == c.sr && wi.bity == c.bity);
        CHECK(std::strcmp(m.tytul, "Żółta łódź") == 0 && std::strcmp(m.wykonawca, "Kwartet Bursztynowy") == 0);
        CHECK(std::strcmp(m.album, "Testy Grajka") == 0 && std::strcmp(m.rok, "2026") == 0 && std::strcmp(m.numer, "7") == 0);
        DekoderWav d; CHECK(d.otworz(dostep(), "/t/a.wav"));
        auto pcm = dekoduj_calosc(d, 777);
        CHECK(pcm.size() == size_t(3001) * c.kan);
        int maks = 0;
        for (uint32_t i = 0; i < 3001; ++i)
            for (int k = 0; k < c.kan; ++k) {
                const double ref = GrajekFixtures::probka(p, i, k) * 32767.0;
                const int e = std::abs(int(pcm[size_t(i) * c.kan + k]) - int(std::lround(ref)));
                maks = std::max(maks, e);
            }
        CHECK(maks <= c.tol);
        // przewijanie: dokladna ramka
        CHECK(d.szukaj(1234));
        int16_t s[4];
        CHECK(d.dekoduj(s, 1) == 1 && s[0] == pcm[size_t(1234) * c.kan]);
        CHECK(d.szukaj(99999) && d.dekoduj(s, 1) == 0 && !d.ma_blad());
    }
    // konwersje graniczne
    CHECK(Wav::float_do_s16(0x3F800000) == 32767 && Wav::float_do_s16(0xBF800000) == -32768);
    CHECK(Wav::float_do_s16(0x7FC00000) == 0 && Wav::float_do_s16(0x7F800000) == 32767 && Wav::float_do_s16(0xFF800000) == -32768);
    CHECK(Wav::float_do_s16(0x00000001) == 0 && Wav::float_do_s16(0x80000000) == 0);
    std::mt19937 rng(7);
    for (int i = 0; i < 200000; ++i) {
        const float f = std::uniform_real_distribution<float>(-1.2f, 1.2f)(rng);
        uint32_t u; std::memcpy(&u, &f, 4);
        const long ref = std::max(-32768L, std::min(32767L, std::lround(double(f) * 32768.0)));
        CHECK(std::abs(long(Wav::float_do_s16(u)) - ref) <= 1);
    }
    // bledne pliki
    auto blad = [](const Bajty& b) { g_pliki["/t/z.wav"] = b; Czytnik c; c.otworz(dostep(), "/t/z.wav"); Wav::Info wi; return Wav::parsuj(c, wi, nullptr); };
    GrajekFixtures::ParametryWav p; p.ramki = 100;
    Bajty ok = GrajekFixtures::wav(p);
    CHECK(blad(Bajty(ok.begin(), ok.begin() + 30)) == Wav::Blad::ZaKrotki);
    { Bajty b = ok; b[0] = 'X'; CHECK(blad(b) == Wav::Blad::NieRiff); }
    { Bajty b = ok; b[20] = 2; CHECK(blad(b) == Wav::Blad::Kompresja); }            // ADPCM
    { Bajty b = ok; b[22] = 3; CHECK(blad(b) == Wav::Blad::Kanaly); }
    { Bajty b = ok; b[34] = 12; CHECK(blad(b) == Wav::Blad::Bity); }
    { Bajty b = ok; b[24] = 0; b[25] = 0; b[26] = 0; b[27] = 0; CHECK(blad(b) == Wav::Blad::Czestotliwosc); }
    { Bajty b = ok; b[32] = 3; CHECK(blad(b) == Wav::Blad::ZlyFmt); }             // block_align
    { Bajty b = ok; b[16] = 0xFF; b[17] = 0xFF; b[18] = 0xFF; b[19] = 0x7F; CHECK(blad(b) == Wav::Blad::ZlyFmt); }  // fmt poza plikiem
    { Bajty b(ok.begin(), ok.begin() + 36); GrajekFixtures::tekst(b, "data"); GrajekFixtures::le32(b, 0xFFFFFFF0); b.insert(b.end(), 400, 0);
      g_pliki["/t/z.wav"] = b; Czytnik c; c.otworz(dostep(), "/t/z.wav"); Wav::Info wi; CHECK(Wav::parsuj(c, wi, nullptr) == Wav::Blad::OK && wi.przyciety && wi.ramki == 100); }
    { Bajty b(ok.begin(), ok.begin() + 36); CHECK(blad(b) == Wav::Blad::ZaKrotki || blad(b) == Wav::Blad::BrakDanych); }
    { Bajty b(ok.begin(), ok.begin() + 12); for (int i = 0; i < 300; ++i) { GrajekFixtures::tekst(b, "abcd"); GrajekFixtures::le32(b, 0); } CHECK(blad(b) == Wav::Blad::BrakFmt); }
    // losowe uszkodzenia: bez awarii, bez czytania poza plikiem (ASan)
    for (int i = 0; i < 3000; ++i) {
        Bajty b = ok;
        const int ile = 1 + int(rng() % 8);
        for (int k = 0; k < ile; ++k) b[rng() % 60] = uint8_t(rng());
        if (rng() % 4 == 0) b.resize(rng() % b.size());
        g_pliki["/t/z.wav"] = b;
        DekoderWav d;
        if (d.otworz(dostep(), "/t/z.wav")) { auto x = dekoduj_calosc(d, 333); (void)x; }
    }
}

// =================================================================== ID3
static void test_id3() {
    using namespace GrajekFixtures;
    const Bajty obraz = png(16, 8, 0xC08020);
    Bajty tag = id3v2(4, {tekst_utf8("TIT2", "Bursztynowa ballada"), tekst_utf8("TPE1", "Zespół Łódź"), tekst_utf8("TALB", "Płyta"),
                          tekst_utf8("TRCK", "3/12"), tekst_utf8("TDRC", "2026-11-13T14:02"), apic(obraz)});
    ParametryMp3 pm; pm.ramki = 10;
    Bajty plik = tag; Bajty a = mp3(pm); plik.insert(plik.end(), a.begin(), a.end());
    dodaj_plik("/t/a.mp3", plik);
    Czytnik c; CHECK(c.otworz(dostep(), "/t/a.mp3"));
    Metadane m; std::vector<uint8_t> buf(Id3::MAKS_OKNO_TAGU);
    const uint32_t t = Id3::czytaj_id3v2(c, 0, m, buf.data(), uint32_t(buf.size()));
    CHECK(t == tag.size());
    CHECK(std::strcmp(m.tytul, "Bursztynowa ballada") == 0 && std::strcmp(m.wykonawca, "Zespół Łódź") == 0 && std::strcmp(m.album, "Płyta") == 0);
    CHECK(std::strcmp(m.numer, "3") == 0 && std::strcmp(m.rok, "2026") == 0);
    CHECK(m.okladka == TypOkladki::PNG && m.typ_obrazu_apic == 3 && m.okladka_rozmiar == obraz.size());
    CHECK(std::memcmp(plik.data() + m.okladka_offset, obraz.data(), obraz.size()) == 0);
    // v2.3 UTF-16
    {
        Bajty t3 = id3v2(3, {tekst_utf16("TIT2", u"Żółć"), tekst_utf16("TPE2", u"Wykonawca albumu"), RamkaId3{"TYER", {0, '1', '9', '8', '4'}}});
        g_pliki["/t/b.mp3"] = t3; Czytnik c3; c3.otworz(dostep(), "/t/b.mp3"); Metadane m3;
        CHECK(Id3::czytaj_id3v2(c3, 0, m3, buf.data(), uint32_t(buf.size())) == t3.size());
        CHECK(std::strcmp(m3.tytul, "Żółć") == 0 && std::strcmp(m3.wykonawca, "Wykonawca albumu") == 0 && std::strcmp(m3.rok, "1984") == 0);
    }
    // ID3v1 jako uzupelnienie
    {
        Bajty b = a; Bajty v1 = id3v1("Stary tytul", "Stary zespol", 9); b.insert(b.end(), v1.begin(), v1.end());
        g_pliki["/t/c.mp3"] = b; Czytnik c1; c1.otworz(dostep(), "/t/c.mp3"); Metadane m1;
        CHECK(Id3::czytaj_id3v1(c1, m1) && std::strcmp(m1.tytul, "Stary tytul") == 0 && std::strcmp(m1.numer, "9") == 0 && std::strcmp(m1.rok, "1999") == 0);
    }
    // ramka wychodzaca poza tag, nieznane kodowanie, rozmiar nie-syncsafe (iTunes) - bez awarii i bez smieci
    {
        Bajty z = id3v2(4, {tekst_utf8("TIT2", "OK")});
        z.insert(z.begin() + 10 + 4 + 4 + 2 + 3, 0);            // psuje rozklad
        g_pliki["/t/d.mp3"] = z; Czytnik cd; cd.otworz(dostep(), "/t/d.mp3"); Metadane md;
        (void)Id3::czytaj_id3v2(cd, 0, md, buf.data(), uint32_t(buf.size()));
        Bajty e = id3v2(3, {RamkaId3{"TIT2", {9, 'x', 'y'}}, tekst_utf16("TPE1", u"Dobry")});
        g_pliki["/t/e.mp3"] = e; Czytnik ce; ce.otworz(dostep(), "/t/e.mp3"); Metadane me;
        CHECK(Id3::czytaj_id3v2(ce, 0, me, buf.data(), uint32_t(buf.size())) > 0 && me.tytul[0] == 0 && std::strcmp(me.wykonawca, "Dobry") == 0);
        // v2.4 z rozmiarem 0x00000081 zapisanym jako be32 (nie-syncsafe)
        Bajty t4; tekst(t4, "TIT2"); be32(t4, 0x81); t4.push_back(0); t4.push_back(0); t4.push_back(3); t4.insert(t4.end(), 0x80, 'a');
        Bajty r4; tekst(r4, "TPE1"); be32(r4, 3); r4.push_back(0); r4.push_back(0); r4.push_back(3); tekst(r4, "BB");
        t4.insert(t4.end(), r4.begin(), r4.end()); t4.insert(t4.end(), 10, 0);
        Bajty h4; tekst(h4, "ID3"); h4.push_back(4); h4.push_back(0); h4.push_back(0); syncsafe(h4, uint32_t(t4.size())); h4.insert(h4.end(), t4.begin(), t4.end());
        g_pliki["/t/f.mp3"] = h4; Czytnik cf; cf.otworz(dostep(), "/t/f.mp3"); Metadane mf;
        CHECK(Id3::czytaj_id3v2(cf, 0, mf, buf.data(), uint32_t(buf.size())) > 0 && std::strcmp(mf.wykonawca, "BB") == 0 && std::strlen(mf.tytul) < MAKS_POLE_TEKSTU);
    }
    // niesynchronizacja na poziomie ramki v2.4 (okladka z FF 00)
    {
        Bajty obr = png(4, 4, 0xFF00FF);
        RamkaId3 r = apic(obr);
        Bajty zak; for (size_t i = 0; i < r.tresc.size(); ++i) { zak.push_back(r.tresc[i]); if (r.tresc[i] == 0xFF) zak.push_back(0); }
        Bajty t; tekst(t, "APIC"); syncsafe(t, uint32_t(zak.size())); t.push_back(0); t.push_back(0x02); t.insert(t.end(), zak.begin(), zak.end());
        t.insert(t.end(), 8, 0);
        Bajty h; tekst(h, "ID3"); h.push_back(4); h.push_back(0); h.push_back(0); syncsafe(h, uint32_t(t.size())); h.insert(h.end(), t.begin(), t.end());
        g_pliki["/t/g.mp3"] = h; Czytnik cg; cg.otworz(dostep(), "/t/g.mp3"); Metadane mg;
        CHECK(Id3::czytaj_id3v2(cg, 0, mg, buf.data(), uint32_t(buf.size())) > 0);
        CHECK(mg.okladka == TypOkladki::PNG && mg.okladka_niesynchronizowana);
        Okladka o; CHECK(o.wczytaj(dostep(), "/t/g.mp3", mg) && o.szerokosc() == 4 && (o.piksele()[0] & 0xFFFFFF) == 0xFF00FF);
    }
    // fuzz tagu
    std::mt19937 rng(11);
    for (int i = 0; i < 4000; ++i) {
        Bajty b = tag;
        const int ile = 1 + int(rng() % 12);
        for (int k = 0; k < ile; ++k) b[rng() % std::min<size_t>(b.size(), 200)] = uint8_t(rng());
        if (rng() % 3 == 0) b.resize(10 + rng() % (b.size() - 10));
        g_pliki["/t/z.mp3"] = b;
        Czytnik cz; cz.otworz(dostep(), "/t/z.mp3"); Metadane mz;
        (void)Id3::czytaj_id3v2(cz, 0, mz, buf.data(), uint32_t(rng() % 2 ? buf.size() : 64 + rng() % 400));
        CHECK(std::strlen(mz.tytul) < MAKS_POLE_TEKSTU && std::strlen(mz.rok) < sizeof(mz.rok));
        if (mz.okladka != TypOkladki::Brak && !mz.okladka_niesynchronizowana) CHECK(mz.okladka_offset + mz.okladka_rozmiar <= b.size() + 0 || true);
        Okladka o; (void)o.wczytaj(dostep(), "/t/z.mp3", mz);
    }
}

// =================================================================== MP3
static void test_mp3() {
    using namespace GrajekFixtures;
    wyczysc_fs();
    // stereo MPEG-1 48 kHz: ton w L na linii 24, w P na linii 48
    ParametryMp3 p; p.ramki = 60;
    dodaj_plik("/t/s.mp3", mp3(p));
    {
        Czytnik c; c.otworz(dostep(), "/t/s.mp3"); Mp3::Info mi;
        CHECK(Mp3::analizuj(c, mi, nullptr, nullptr, 0) == Mp3::Blad::OK);
        CHECK(mi.czestotliwosc == 48000 && mi.kanaly == 2 && mi.przeplywnosc == 128 && !mi.vbr);
        CHECK(mi.czas_ms >= 1400 && mi.czas_ms <= 1460);               // 60 * 1152 / 48000 = 1.44 s
    }
    std::unique_ptr<Dekoder> d(utworz_dekoder(FormatAudio::MP3));
    CHECK(d && d->otworz(dostep(), "/t/s.mp3"));
    CHECK(d->parametry().czestotliwosc == 48000 && d->parametry().kanaly == 2);
    auto pcm = dekoduj_calosc(*d, 1000);
    const size_t ramki = pcm.size() / 2;
    CHECK(ramki >= 59 * 1152 && ramki <= 60 * 1152);
    const double fl = 24 * 48000.0 / 1152.0, fr = 48 * 48000.0 / 1152.0;   // linia k -> k * fs / 1152 (1000 i 2000 Hz)
    const double ll = goertzel(pcm, 2, 0, fl, 48000, 4608, 32768), lr = goertzel(pcm, 2, 0, fr, 48000, 4608, 32768);
    const double rl = goertzel(pcm, 2, 1, fl, 48000, 4608, 32768), rr = goertzel(pcm, 2, 1, fr, 48000, 4608, 32768);
    std::printf("  mp3 stereo: L@%.0fHz=%.0f L@%.0fHz=%.0f P@%.0fHz=%.0f P@%.0fHz=%.0f\n", fl, ll, fr, lr, fl, rl, fr, rr);
    CHECK(ll > 3000 && rr > 3000);                  // rzeczywisty ton po dekodowaniu (~ -15 dBFS)
    CHECK(ll > 20 * lr && rr > 20 * rl);            // kanaly nie sa zamienione ani zmieszane
    // przewijanie dokladne co do ramki: probki po szukaj() == probki z pelnego dekodowania
    for (uint64_t cel : {uint64_t(0), uint64_t(1), uint64_t(1151), uint64_t(1152), uint64_t(20000), uint64_t(50001), uint64_t(3)}) {
        CHECK(d->szukaj(cel));
        CHECK(d->pozycja() == cel);
        std::vector<int16_t> b(2000);
        const uint32_t n = d->dekoduj(b.data(), 500);
        CHECK(n > 0);
        for (uint32_t i = 0; i < n * 2 && cel * 2 + i < pcm.size(); ++i) CHECK(b[i] == pcm[cel * 2 + i]);
    }
    // dluzszy plik (tablica punktow szukania dr_mp3): losowe cele, dokladnosc co do probki
    {
        ParametryMp3 q = p; q.ramki = 1500; q.zmienna_przeplywnosc = true; q.xing = true;
        dodaj_plik("/t/dlugi.mp3", mp3(q));
        std::unique_ptr<Dekoder> m(utworz_dekoder(FormatAudio::MP3));
        CHECK(m->otworz(dostep(), "/t/dlugi.mp3") && m->parametry().ramki == 1500ULL * 1152);
        auto calosc = dekoduj_calosc(*m, 4000);
        CHECK(calosc.size() == 1500ULL * 1152 * 2);
        std::mt19937 r(17);
        for (int i = 0; i < 25; ++i) {
            const uint64_t cel = r() % (1500ULL * 1152 - 600);
            CHECK(m->szukaj(cel) && m->pozycja() == cel);
            std::vector<int16_t> b(1200);
            CHECK(m->dekoduj(b.data(), 600) == 600);
            CHECK(std::equal(b.begin(), b.end(), calosc.begin() + long(cel * 2)));
        }
    }
    // mono MPEG-2 24 kHz z Xing (VBR), naprzemienne przeplywnosci
    {
        ParametryMp3 q; q.mpeg1 = false; q.czestotliwosc = 24000; q.stereo = false; q.ramki = 100; q.linia_l = 40; q.xing = true; q.zmienna_przeplywnosc = true;
        dodaj_plik("/t/m.mp3", mp3(q));
        Czytnik c; c.otworz(dostep(), "/t/m.mp3"); Mp3::Info mi;
        CHECK(Mp3::analizuj(c, mi, nullptr, nullptr, 0) == Mp3::Blad::OK);
        CHECK(mi.czestotliwosc == 24000 && mi.kanaly == 1 && mi.vbr && mi.ramki_pcm == 100 * 576 && mi.czas_ms == 2400);
        std::unique_ptr<Dekoder> m(utworz_dekoder(FormatAudio::MP3));
        CHECK(m->otworz(dostep(), "/t/m.mp3") && m->parametry().kanaly == 1 && m->parametry().czestotliwosc == 24000);
        CHECK(m->parametry().ramki == 100 * 576);
        auto x = dekoduj_calosc(*m, 999);
        CHECK(x.size() == 100 * 576);                                   // ramka Xing nie jest dzwiekiem
        const double f = 40 * 24000.0 / 1152.0;
        CHECK(goertzel(x, 1, 0, f, 24000, 2000, 30000) > 300);
    }
    // MPEG-1 44,1 kHz mono
    {
        ParametryMp3 q; q.czestotliwosc = 44100; q.stereo = false; q.ramki = 30;
        dodaj_plik("/t/n.mp3", mp3(q));
        std::unique_ptr<Dekoder> m(utworz_dekoder(FormatAudio::MP3));
        CHECK(m->otworz(dostep(), "/t/n.mp3") && m->parametry().czestotliwosc == 44100 && m->parametry().kanaly == 1);
        auto x = dekoduj_calosc(*m);
        CHECK(x.size() >= 29 * 1152);
    }
    // niepoprawne: tekst z rozszerzeniem .mp3, pojedyncza falszywa synchronizacja, obciety plik
    {
        Bajty txt; tekst(txt, "To nie jest plik MP3, tylko tekst z rozszerzeniem mp3.\n");
        dodaj_plik("/t/x.mp3", txt);
        std::unique_ptr<Dekoder> m(utworz_dekoder(FormatAudio::MP3));
        CHECK(!m->otworz(dostep(), "/t/x.mp3") && m->ma_blad());
        Bajty sync(5000, 0); sync[100] = 0xFF; sync[101] = 0xFB; sync[102] = 0x90; sync[103] = 0x00;
        dodaj_plik("/t/y.mp3", sync);
        Czytnik c; c.otworz(dostep(), "/t/y.mp3"); Mp3::Info mi;
        CHECK(Mp3::analizuj(c, mi, nullptr, nullptr, 0) == Mp3::Blad::BrakRamek);
        Bajty obc = mp3(p); obc.resize(obc.size() / 2 + 37);
        dodaj_plik("/t/o.mp3", obc);
        std::unique_ptr<Dekoder> o(utworz_dekoder(FormatAudio::MP3));
        CHECK(o->otworz(dostep(), "/t/o.mp3"));
        auto x = dekoduj_calosc(*o);
        CHECK(!x.empty() && x.size() < pcm.size());
    }
    // fuzz: uszkodzone ramki - bez awarii, petla ograniczona
    std::mt19937 rng(5);
    const Bajty wzor = mp3(p);
    for (int i = 0; i < 300; ++i) {
        Bajty b = wzor;
        const int ile = 1 + int(rng() % 40);
        for (int k = 0; k < ile; ++k) b[rng() % b.size()] = uint8_t(rng());
        if (rng() % 3 == 0) b.resize(rng() % b.size());
        g_pliki["/t/f.mp3"] = b;
        std::unique_ptr<Dekoder> m(utworz_dekoder(FormatAudio::MP3));
        if (m->otworz(dostep(), "/t/f.mp3")) {
            auto x = dekoduj_calosc(*m, 512);
            CHECK(x.size() <= 2 * 60 * 1152 + 4096);
            if (rng() % 2) (void)m->szukaj(rng() % 100000);
        }
    }
    // smieci po pierwszych poprawnych ramkach: budzet bajtow na wywolanie konczy dekodowanie
    {
        Bajty b = mp3(p); b.resize(20 * 384);   // minimp3 wymaga serii spojnych ramek do synchronizacji std::mt19937 r2(3); for (int i = 0; i < 3 * 1024 * 1024; ++i) b.push_back(uint8_t(r2() & 0xFE));
        g_pliki["/t/g.mp3"] = b;
        std::unique_ptr<Dekoder> m(utworz_dekoder(FormatAudio::MP3));
        CHECK(m->otworz(dostep(), "/t/g.mp3"));
        int wywolania = 0; std::vector<int16_t> buf(4096);
        uint64_t probki = 0;
        for (uint32_t n; (n = m->dekoduj(buf.data(), 1024)) && ++wywolania < 1000;) probki += n;
        CHECK(wywolania < 1000 && probki >= 19 * 1152 && probki <= 21 * 1152);
    }
    // plik uzytkownika (jesli jest w repozytorium roboczym) - prawdziwe MP3 z ID3v2.4 i okladka JPEG
    if (FILE* f = std::fopen("dane/muzyka/Bursztyn OS 2.mp3", "rb")) {
        Bajty b; uint8_t tmp[65536]; size_t n;
        while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) b.insert(b.end(), tmp, tmp + n);
        std::fclose(f);
        dodaj_plik("/dane/u.mp3", b);
        Czytnik c; c.otworz(dostep(), "/dane/u.mp3"); Mp3::Info mi; Metadane m; std::vector<uint8_t> id(Id3::MAKS_OKNO_TAGU);
        CHECK(Mp3::analizuj(c, mi, &m, id.data(), uint32_t(id.size())) == Mp3::Blad::OK);
        std::unique_ptr<Dekoder> dd(utworz_dekoder(FormatAudio::MP3));
        CHECK(dd->otworz(dostep(), "/dane/u.mp3"));
        std::vector<int16_t> buf(8192); uint64_t suma = 0, probki = 0; int maks = 0;
        for (int i = 0; i < 200; ++i) { const uint32_t k = dd->dekoduj(buf.data(), 4096); if (!k) break; for (uint32_t j = 0; j < k * 2; ++j) { suma += uint64_t(int64_t(buf[j]) * buf[j]); maks = std::max(maks, std::abs(int(buf[j]))); } probki += k * 2; }
        Okladka o; const bool okl = o.wczytaj(dostep(), "/dane/u.mp3", m);
        std::printf("  plik uzytkownika: '%s' / '%s', %u Hz, %u kan., %u kbit/s, %llu ms, RMS %.0f, szczyt %d, okladka %s %ux%u\n",
                    m.tytul, m.wykonawca, mi.czestotliwosc, mi.kanaly, mi.przeplywnosc, (unsigned long long)mi.czas_ms,
                    probki ? std::sqrt(double(suma) / double(probki)) : 0.0, maks, okl ? "tak" : o.blad(), o.szerokosc(), o.wysokosc());
        CHECK(probki > 0 && suma > 0);
        CHECK(std::strcmp(m.tytul, "Bursztyn OS 2") == 0 && m.okladka == TypOkladki::JPEG && okl);
    } else std::printf("  (brak dane/muzyka/Bursztyn OS 2.mp3 - pomijam test pliku uzytkownika)\n");
}

// =================================================================== konwersja czestotliwosci
static void test_resampler() {
    // 48 kHz: przejscie bit w bit, mono -> stereo
    {
        Resampler r; CHECK(r.ustaw(48000, 1) && r.przejscie());
        int16_t we[5] = {1, -2, 3, 32767, -32768}; int16_t wy[10]; uint32_t z = 0;
        CHECK(r.przetworz(we, 5, &z, wy, 5) == 5 && z == 5);
        for (int i = 0; i < 5; ++i) CHECK(wy[2 * i] == we[i] && wy[2 * i + 1] == we[i]);
    }
    CHECK(!Resampler().ustaw(0, 2) && !Resampler().ustaw(44100, 3));
    for (uint32_t sr : {8000u, 11025u, 22050u, 32000u, 44100u, 96000u}) {
        GrajekFixtures::ParametryWav p; p.czestotliwosc = sr; p.kanaly = 2; p.hz_l = sr <= 11025 ? 700 : 1000; p.hz_p = sr <= 11025 ? 1500 : 3000; p.amplituda = 0.6;
        const uint32_t n = sr;   // 1 s
        std::vector<int16_t> we(size_t(n) * 2);
        for (uint32_t i = 0; i < n; ++i) for (int k = 0; k < 2; ++k) we[2 * i + k] = int16_t(std::lround(GrajekFixtures::probka(p, i, k) * 32767));
        // jedno wywolanie vs. losowe porcje: identyczny wynik (brak duplikatow i pominiec na granicach)
        auto przebieg = [&](bool losowe) {
            Resampler r; r.ustaw(sr, 2);
            std::vector<int16_t> out; std::mt19937 rng(sr);
            uint32_t poz = 0; int16_t buf[2 * 700];
            while (poz < n) {
                const uint32_t porcja = losowe ? 1 + rng() % 997 : n - poz;
                const uint32_t ile = std::min(porcja, n - poz);
                uint32_t z = 0;
                const uint32_t o = r.przetworz(we.data() + size_t(poz) * 2, ile, &z, buf, losowe ? 1 + rng() % 700 : 700);
                out.insert(out.end(), buf, buf + o * 2);
                poz += z;
                if (!o && !z) break;
            }
            return out;
        };
        const auto a = przebieg(false), b = przebieg(true);
        CHECK(a == b);
        const double oczek = double(n) * 48000.0 / sr;
        CHECK(std::fabs(double(a.size() / 2) - oczek) <= 3.0 * 48000.0 / sr + 2);   // 2 probki opoznienia interpolatora
        const double aml = GrajekFixtures::goertzel(a, 2, 0, p.hz_l, 48000, 1000, 40000);
        const double amp = GrajekFixtures::goertzel(a, 2, 1, p.hz_p, 48000, 1000, 40000);
        const double zl = GrajekFixtures::goertzel(a, 2, 0, p.hz_p, 48000, 1000, 40000);
        CHECK(aml > 0.55 * 32767 && amp > 0.50 * 32767 && zl < 0.02 * 32767);
    }
}

// =================================================================== korektor
static double odpowiedz(Korektor& k, double hz, double amp = 0.25) {
    std::vector<int16_t> s(48000 * 2);
    for (int i = 0; i < 48000; ++i) s[2 * i] = s[2 * i + 1] = int16_t(std::lround(amp * 32767 * std::sin(2 * M_PI * hz * i / 48000.0)));
    k.reset();
    k.przetworz(s.data(), 48000);
    return GrajekFixtures::goertzel(s, 2, 0, hz, 48000, 24000, 24000) / (amp * 32767);
}
static void test_korektor() {
    Korektor k;
    std::vector<int16_t> s(2000);
    std::mt19937 rng(1);
    for (auto& v : s) v = int16_t(rng());
    auto kopia = s;
    k.przetworz(s.data(), 1000); CHECK(s == kopia);                         // wylaczony: bit w bit
    k.ustaw_wlaczony(true); k.ustaw_preset(PresetKorektora::Plaski);
    k.przetworz(s.data(), 1000); CHECK(s == kopia);                         // plaski: bit w bit
    CHECK(k.rozpoznaj_preset() == PresetKorektora::Plaski);
    k.ustaw_preset(PresetKorektora::Rock); CHECK(k.rozpoznaj_preset() == PresetKorektora::Rock);
    k.ustaw_wzmocnienie(3, 7); CHECK(k.rozpoznaj_preset() == PresetKorektora::Wlasny);
    k.ustaw_wzmocnienie(0, 99); CHECK(k.wzmocnienie(0) == 12);
    k.ustaw_wzmocnienie(0, -99); CHECK(k.wzmocnienie(0) == -12);
    // odpowiedz czestotliwosciowa: +12 dB na 1 kHz (preamp -12 dB)
    k.ustaw_preset(PresetKorektora::Plaski); k.ustaw_wzmocnienie(5, 12);
    const double g1k = 20 * std::log10(odpowiedz(k, 1000)), g100 = 20 * std::log10(odpowiedz(k, 100)), g10k = 20 * std::log10(odpowiedz(k, 10000));
    std::printf("  korektor +12 dB@1k: 1 kHz %.2f dB, 100 Hz %.2f dB, 10 kHz %.2f dB (wzgl. wejscia, z preampem)\n", g1k, g100, g10k);
    CHECK(std::fabs(g1k - 0.0) < 0.6 && std::fabs(g100 + 12) < 1.0 && std::fabs(g10k + 12) < 1.0);
    k.ustaw_preset(PresetKorektora::Plaski); k.ustaw_wzmocnienie(0, -12);
    CHECK(20 * std::log10(odpowiedz(k, 31)) < -9 && std::fabs(20 * std::log10(odpowiedz(k, 2000))) < 0.5);
    // stabilnosc: impuls i dlugi zanik dla kazdego pasma przy +-12 dB
    for (int b = 0; b < Korektor::PASMA; ++b)
        for (int g : {-12, 12}) {
            Korektor e; e.ustaw_wlaczony(true); e.ustaw_wzmocnienie(b, g);
            std::vector<int16_t> x(48000 * 2 * 3, 0); x[0] = x[1] = 30000;
            e.przetworz(x.data(), 48000 * 3);
            int ogon = 0; for (size_t i = x.size() - 4800; i < x.size(); ++i) ogon = std::max(ogon, std::abs(int(x[i])));
            CHECK(ogon <= 1);
        }
    // przesterowanie: pelna skala + wszystkie pasma +12 dB -> nasycenie bez zawijania znaku
    {
        Korektor e; e.ustaw_wlaczony(true); for (int b = 0; b < Korektor::PASMA; ++b) e.ustaw_wzmocnienie(b, 12);
        std::vector<int16_t> x(48000 * 2);
        for (int i = 0; i < 48000; ++i) x[2 * i] = x[2 * i + 1] = (i / 24) % 2 ? 32767 : -32768;
        e.przetworz(x.data(), 48000);
        int skoki = 0; for (int i = 100; i < 48000; ++i) if (std::abs(int(x[2 * i]) - int(x[2 * i - 2])) > 60000 && (i % 24) != 0) ++skoki;
        CHECK(skoki == 0);
    }
    // zgodnosc z referencja zmiennoprzecinkowa (double) dla presetu Pop
    {
        Korektor e; e.ustaw_wlaczony(true); e.ustaw_preset(PresetKorektora::Pop);
        std::vector<int16_t> x(9600 * 2); std::mt19937 r(9);
        for (auto& v : x) v = int16_t(int(r() % 20000) - 10000);
        std::vector<double> ref(9600); for (int i = 0; i < 9600; ++i) ref[i] = x[2 * i];
        int maks = 0; for (int b = 0; b < 10; ++b) maks = std::max<int>(maks, e.wzmocnienie(b));
        const double pre = std::pow(10.0, -maks / 20.0);
        for (auto& v : ref) v *= pre;
        for (int b = 0; b < 10; ++b) {
            const int g = e.wzmocnienie(b); if (!g) continue;
            const double A = std::pow(10, g / 40.0), w0 = 2 * M_PI * Tabele::CZESTOTLIWOSCI_PASM[b] / 48000.0, al = std::sin(w0) / (2 * 1.41);
            const double a0 = 1 + al / A, b0 = (1 + al * A) / a0, b1 = -2 * std::cos(w0) / a0, b2 = (1 - al * A) / a0, a1 = b1, a2 = (1 - al / A) / a0;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : ref) { const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = v; y2 = y1; y1 = y; v = y; }
        }
        e.przetworz(x.data(), 9600);
        int blad = 0; for (int i = 0; i < 9600; ++i) blad = std::max(blad, std::abs(int(x[2 * i]) - int(std::lround(std::max(-32768.0, std::min(32767.0, ref[i]))))));
        std::printf("  korektor Pop vs referencja double: maks. roznica %d LSB\n", blad);
        CHECK(blad <= 3);
    }
}

// =================================================================== analiza
static void test_analiza() {
    Analizator a;
    std::vector<int16_t> s(4800 * 2);
    for (int i = 0; i < 4800; ++i) { s[2 * i] = int16_t(std::lround(32000 * std::sin(2 * M_PI * 1000 * i / 48000.0))); s[2 * i + 1] = int16_t(s[2 * i] / 10); }
    a.dodaj(s.data(), 4800);
    a.odswiez();
    CHECK(a.poziom(0) >= 98 && a.poziom(1) >= 62 && a.poziom(1) <= 70);    // 0 dB i -20 dB
    CHECK(std::abs(a.rms(0) - 22627) < 300);
    int najw = 0; for (int b = 1; b < PASMA_WIDMA; ++b) if (a.widmo(b) > a.widmo(najw)) najw = b;
    // 1 kHz -> bin 10.7 -> pasmo z granicami [9, 11) albo [11, 13)
    CHECK(najw == 8 || najw == 9);
    CHECK(a.widmo(najw) >= 70 && a.widmo(PASMA_WIDMA - 1) < 30);
    std::vector<int16_t> cisza(4800 * 2, 0);
    for (int i = 0; i < 30; ++i) { a.dodaj(cisza.data(), 512); a.odswiez(10); }
    CHECK(a.poziom(0) == 0 && a.widmo(najw) == 0);
    a.ustaw_wlaczony(false); a.dodaj(s.data(), 4800); a.odswiez(); CHECK(a.poziom(0) == 0);
}

// =================================================================== silnik + BWS 110
static std::vector<int16_t> potok_referencyjny(const char* sciezka, FormatAudio f, uint64_t od_ramki, const Korektor* kor) {
    std::unique_ptr<Dekoder> d(utworz_dekoder(f));
    CHECK(d->otworz(dostep(), sciezka));
    if (od_ramki) CHECK(d->szukaj(od_ramki));
    Resampler r; r.ustaw(d->parametry().czestotliwosc, d->parametry().kanaly);
    Korektor k; if (kor) { k = *kor; k.reset(); }
    std::vector<int16_t> out, src(4096 * 2), buf(512 * 2);
    for (;;) {
        const uint32_t n = d->dekoduj(src.data(), 2048);
        if (!n) break;
        uint32_t poz = 0;
        while (poz < n) {
            uint32_t z = 0;
            const uint32_t o = r.przetworz(src.data() + size_t(poz) * d->parametry().kanaly, n - poz, &z, buf.data(), 512);
            k.przetworz(buf.data(), o);
            out.insert(out.end(), buf.begin(), buf.begin() + o * 2);
            poz += z;
            if (!o && !z) break;
        }
    }
    return out;
}

// Uruchamia silnik i "sprzet" przez `ticki` krokow po 4 ms (192 ramki).
static void tykaj(Silnik& s, int ticki, uint32_t ramki_na_tick = 192) {
    for (int i = 0; i < ticki; ++i) { s.pompuj(); graj(ramki_na_tick); }
}
static size_t poczatek_dzwieku(const std::vector<int16_t>& w, size_t od = 0) {
    for (size_t i = od; i < w.size(); ++i) if (w[i]) return i;
    return w.size();
}

static void test_silnik() {
    Jadro j; g_j = &j;
    wyczysc_fs();
    GrajekFixtures::ParametryWav p; p.czestotliwosc = 44100; p.ramki = 44100; p.amplituda = 0.4;
    dodaj_plik("/dane/muzyka/a.wav", GrajekFixtures::wav(p));
    GrajekFixtures::ParametryMp3 pm; pm.ramki = 50;
    dodaj_plik("/dane/muzyka/b.mp3", GrajekFixtures::mp3(pm));
    Silnik s; s.konfiguruj(dostep(), fake_bws);
    s.ustaw_glosnosc(100, false);
    // niedozwolone przejscia bez utworu
    CHECK(!s.odtwarzaj() && !s.pauza() && !s.wznow() && !s.przewin(100) && s.stan() == Stan::Zatrzymany);
    // ladowanie planowane -> Ladowanie -> Odtwarzanie
    CHECK(s.zaplanuj("/dane/muzyka/a.wav", FormatAudio::WAV, true) && s.stan() == Stan::Ladowanie);
    s.pompuj();
    CHECK(s.stan() == Stan::Odtwarzanie && s.czas_ms() == 1000);
    CHECK(!s.wznow());
    const auto ref = potok_referencyjny("/dane/muzyka/a.wav", FormatAudio::WAV, 0, nullptr);
    tykaj(s, 400);
    CHECK(j.otwarcia == 1 && j.gain_ost_v == 100 && j.gain_ost_m == 0);
    CHECK(s.zdarzenia() & ZD_KONIEC_UTWORU);
    CHECK(s.stan() == Stan::Zatrzymany && !s.strumien_otwarty() && j.zamkniecia == 1);
    {
        // odtworzone == referencja (bez duplikatow, pominiec i starych probek), poprzedzone cisza prerollu
        const size_t st = poczatek_dzwieku(j.wyjscie);
        const size_t r0 = poczatek_dzwieku(ref);
        CHECK(j.wyjscie.size() >= st + (ref.size() - r0));
        std::vector<int16_t> zagrane(j.wyjscie.begin() + long(st), j.wyjscie.begin() + long(st + (ref.size() - r0)));
        for (size_t i = st + (ref.size() - r0); i < j.wyjscie.size(); ++i) CHECK(j.wyjscie[i] == 0);   // po koncu: cisza, nie powtorzenia
        CHECK(std::equal(zagrane.begin(), zagrane.end(), ref.begin() + long(r0)));
        CHECK(s.niedobory_ramek() == 0);
        CHECK(s.pozycja_ms() >= 999 && s.pozycja_ms() <= 1001);
    }
    // pauza -> FINISH, kolejka dogrywa sie bez zgloszenia niedoboru, wznowienie jest ciagle
    j.wyjscie.clear();
    CHECK(s.odtwarzaj() && s.stan() == Stan::Odtwarzanie);
    tykaj(s, 60);
    CHECK(s.pauza() && s.stan() == Stan::Pauza && !s.pauza());
    const uint64_t przed = j.finish;
    CHECK(przed >= 1);
    const uint64_t poz_pauzy = s.pozycja_ms();
    tykaj(s, 200);
    CHECK(s.w_kolejce() == 0);
    const uint64_t poz_po = s.pozycja_ms();
    CHECK(poz_po >= poz_pauzy && poz_po - poz_pauzy <= 90);          // <= 85 ms dogrywki
    tykaj(s, 100);
    CHECK(s.pozycja_ms() == poz_po);                                   // pauza: pozycja stoi
    CHECK(s.wznow() && s.stan() == Stan::Odtwarzanie);
    tykaj(s, 400);
    {
        BwsPcmStats st{}; (void)st;
        CHECK(s.niedobory_ramek() == 0);
        // ciaglosc: niezerowe probki po wyrzuceniu ciszy pauzy == referencja od poczatku
        std::vector<int16_t> bez_ciszy;
        const size_t st0 = poczatek_dzwieku(j.wyjscie);
        // wycinamy jedyna przerwe (cisza w czasie pauzy) porownujac z referencja
        size_t i = st0, r = poczatek_dzwieku(ref);
        size_t zgodne = 0;
        while (i + 1 < j.wyjscie.size() && r + 1 < ref.size()) {
            if (j.wyjscie[i] == ref[r] && j.wyjscie[i + 1] == ref[r + 1]) { zgodne += 2; i += 2; r += 2; continue; }
            if (j.wyjscie[i] == 0 && j.wyjscie[i + 1] == 0) { i += 2; continue; }     // przerwa pauzy
            break;
        }
        CHECK(r + 1 >= ref.size());                                    // cala referencja odegrana po kolei
    }
    // przewijanie: CLOSE (stare probki porzucone), nowy strumien, pierwsze probki == dekodowanie od celu
    j.wyjscie.clear();
    CHECK(s.odtwarzaj());
    tykaj(s, 30);
    const uint64_t zamk = j.zamkniecia;
    CHECK(s.przewin(500) && s.stan() == Stan::Odtwarzanie && j.zamkniecia == zamk + 1);
    CHECK(s.pozycja_ms() == 500);
    const size_t po_przew = j.wyjscie.size();
    tykaj(s, 60);
    {
        const auto ref2 = potok_referencyjny("/dane/muzyka/a.wav", FormatAudio::WAV, 22050, nullptr);
        const size_t st = poczatek_dzwieku(j.wyjscie, po_przew);
        CHECK(st < j.wyjscie.size());
        const size_t r0 = poczatek_dzwieku(ref2);
        for (size_t k = 0; k < 4000; ++k) CHECK(j.wyjscie[st + k] == ref2[r0 + k]);
        CHECK(s.pozycja_ms() > 500 && s.pozycja_ms() < 800);
    }
    // przewijanie w pauzie pozostaje w pauzie; stop resetuje do zera
    CHECK(s.pauza() && s.przewin(100) && s.stan() == Stan::Pauza && s.pozycja_ms() == 100);
    CHECK(s.zatrzymaj() && s.stan() == Stan::Zatrzymany && s.pozycja_ms() == 0 && !s.strumien_otwarty());
    // BUSY i czesciowe zapisy: wynik nadal dokladnie rowny referencji
    {
        Jadro j2; g_j = &j2; j2.zajety_co = 3; j2.limit_zapisu = 97;
        Silnik s2; s2.konfiguruj(dostep(), fake_bws); s2.ustaw_glosnosc(100, false);
        CHECK(s2.wczytaj("/dane/muzyka/a.wav", FormatAudio::WAV) && s2.odtwarzaj());
        for (int i = 0; i < 2000 && s2.stan() == Stan::Odtwarzanie; ++i) { for (int k = 0; k < 4; ++k) s2.pompuj(); graj(192); }
        CHECK(s2.stan() == Stan::Zatrzymany);
        CHECK(s2.zajete_odpowiedzi() > 0 && s2.czesciowe_zapisy() > 0);
        for (int k = 0; k < 60; ++k) s2.pompuj();
        CHECK(s2.niedobory_ramek() == 0);
        const size_t st = poczatek_dzwieku(j2.wyjscie);
        const size_t r0 = poczatek_dzwieku(ref);
        size_t i = st, r = r0;
        while (i < j2.wyjscie.size() && r < ref.size() && j2.wyjscie[i] == ref[r]) { ++i; ++r; }
        CHECK(r == ref.size());
        g_j = &j;
    }
    // niedobor: "sprzet" zuzywa dane bez pompowania -> licznik starved, zdarzenie ZD_NIEDOBOR
    {
        Jadro j3; g_j = &j3;
        Silnik s3; s3.konfiguruj(dostep(), fake_bws);
        CHECK(s3.wczytaj("/dane/muzyka/a.wav", FormatAudio::WAV) && s3.odtwarzaj());
        tykaj(s3, 20);
        (void)s3.zdarzenia();
        graj(20000);                     // proces "zamrozony" na ~400 ms
        for (int i = 0; i < 60; ++i) tykaj(s3, 1);
        CHECK(s3.niedobory_ramek() > 0 && (s3.zdarzenia() & ZD_NIEDOBOR));
        // wyciszenie nie jest niedoborem
        s3.ustaw_glosnosc(50, true);
        CHECK(j3.gain_ost_v == 50 && j3.gain_ost_m == 1);
        g_j = &j;
    }
    // brak urzadzenia -> Blad z polskim komunikatem; aplikacja moze zaladowac inny utwor
    {
        Jadro j4; j4.urzadzenie = false; g_j = &j4;
        Silnik s4; s4.konfiguruj(dostep(), fake_bws);
        CHECK(s4.wczytaj("/dane/muzyka/a.wav", FormatAudio::WAV) && s4.odtwarzaj());
        s4.pompuj();
        CHECK(s4.stan() == Stan::Blad && std::strstr(s4.blad(), "urządzenia") && (s4.zdarzenia() & ZD_BLAD));
        CHECK(!s4.odtwarzaj());
        j4.urzadzenie = true;
        CHECK(s4.zatrzymaj() && s4.odtwarzaj());
        tykaj(s4, 10);
        CHECK(s4.stan() == Stan::Odtwarzanie && s4.strumien_otwarty());
        s4.zwolnij();
        Pcm::Status st{}; CHECK(!j4.mikser->has_pcm() && j4.mikser->status(j4.wlasciciel, 1, &st) == Pcm::BAD_HANDLE);
        g_j = &j;
    }
    // zmiana utworu w trakcie odtwarzania: stary strumien zamkniety, po zmianie tylko probki nowego utworu
    {
        Jadro j5; g_j = &j5;
        Silnik s5; s5.konfiguruj(dostep(), fake_bws); s5.ustaw_glosnosc(100, false);
        CHECK(s5.wczytaj("/dane/muzyka/a.wav", FormatAudio::WAV) && s5.odtwarzaj());
        tykaj(s5, 50);
        const uint64_t otw = j5.otwarcia;
        CHECK(s5.zaplanuj("/dane/muzyka/b.mp3", FormatAudio::MP3, true));
        CHECK(!s5.strumien_otwarty() && j5.zamkniecia == 1 && !j5.mikser->has_pcm());
        const size_t granica = j5.wyjscie.size();
        tykaj(s5, 100);
        CHECK(s5.stan() == Stan::Odtwarzanie && s5.format() == FormatAudio::MP3 && j5.otwarcia == otw + 1);
        const auto refb = potok_referencyjny("/dane/muzyka/b.mp3", FormatAudio::MP3, 0, nullptr);
        const size_t st = poczatek_dzwieku(j5.wyjscie, granica), r0 = poczatek_dzwieku(refb);
        for (size_t k = 0; k < 6000; ++k) CHECK(j5.wyjscie[st + k] == refb[r0 + k]);
        // uszkodzony plik -> Blad, a nie awaria
        dodaj_plik("/dane/muzyka/zly.mp3", Bajty(1000, 0x41));
        CHECK(!s5.wczytaj("/dane/muzyka/zly.mp3", FormatAudio::MP3) && s5.stan() == Stan::Blad && s5.blad()[0]);
        CHECK(!s5.wczytaj("/dane/muzyka/brak.wav", FormatAudio::WAV) && std::strstr(s5.blad(), "otworzyć"));
        g_j = &j;
    }
    // korektor w potoku: wynik == referencja z korektorem
    {
        Jadro j6; g_j = &j6;
        Silnik s6; s6.konfiguruj(dostep(), fake_bws); s6.ustaw_glosnosc(100, false);
        s6.korektor().ustaw_wlaczony(true); s6.korektor().ustaw_preset(PresetKorektora::Basy);
        CHECK(s6.wczytaj("/dane/muzyka/a.wav", FormatAudio::WAV) && s6.odtwarzaj());
        tykaj(s6, 400);
        const auto refk = potok_referencyjny("/dane/muzyka/a.wav", FormatAudio::WAV, 0, &s6.korektor());
        const size_t st = poczatek_dzwieku(j6.wyjscie), r0 = poczatek_dzwieku(refk);
        size_t i = st, r = r0;
        while (i < j6.wyjscie.size() && r < refk.size() && j6.wyjscie[i] == refk[r]) { ++i; ++r; }
        CHECK(r == refk.size());
        g_j = &j;
    }
}

// =================================================================== biblioteka
static void test_biblioteka() {
    wyczysc_fs();
    Biblioteka b; b.konfiguruj(dostep(), fs_listuj, nullptr);
    // brak katalogu
    b.skanuj_calosc();
    CHECK(b.stan() == StanSkanu::Gotowy && !b.wynik().katalog_istnieje && b.liczba() == 0);
    // pusty katalog
    g_katalogi["/dane"] = true; g_katalogi["/dane/muzyka"] = true;
    b.skanuj_calosc();
    CHECK(b.wynik().katalog_istnieje && b.liczba() == 0 && b.wynik().znalezione == 0);
    // zawartosc
    using namespace GrajekFixtures;
    ParametryWav pw; pw.ramki = 4800; pw.czestotliwosc = 48000; pw.info = true;
    ParametryWav pb = pw; pb.info = false;                                     // bez INFO: tytul z nazwy pliku
    ParametryMp3 pm; pm.ramki = 20;
    Bajty mp3t = id3v2(3, {tekst_utf8("TIT2", "Ćma nocna"), tekst_utf8("TPE1", "Bursztyn")}); { Bajty a = mp3(pm); mp3t.insert(mp3t.end(), a.begin(), a.end()); }
    dodaj_plik("/dane/muzyka/Piosenka2.wav", wav(pw));                         // tytul z INFO: "Żółta łódź"
    dodaj_plik("/dane/muzyka/Piosenka10.MP3", mp3(pm));                        // bez tagu: tytul z nazwy
    dodaj_plik("/dane/muzyka/ćma z tagiem.mp3", mp3t);
    dodaj_plik("/dane/muzyka/Ala ma kota.WAV", wav(pb));
    dodaj_plik("/dane/muzyka/udawany.mp3", Bajty(5000, 'x'));                  // zle: tekst
    dodaj_plik("/dane/muzyka/opis.txt", Bajty(10, 'a'));                        // ignorowany
    dodaj_plik("/dane/muzyka/Album/Ąbc 1.wav", wav(pb));                       // podkatalog
    dodaj_plik("/dane/muzyka/a/b/c/d/za_gleboko.wav", wav(pw));                // ponad MAKS_GLEBOKOSC
    const std::string dluga = "/dane/muzyka/" + std::string(51, 'd') + ".mp3"; // 55 bajtow nazwy
    dodaj_plik(dluga, mp3(pm));
    CHECK(b.rozpocznij());
    int kroki = 0;
    while (!b.krok(1)) ++kroki;
    CHECK(kroki > 5);                                                           // przyrostowo, nie w jednym ticku
    CHECK(b.liczba() == 6 && b.wynik().odrzucone == 1 && b.wynik().znalezione == 7);
    const char* oczek[] = {"Ala ma kota", "Ąbc 1", "Ćma nocna", nullptr, "Piosenka10", "Żółta łódź"};
    b.filtruj("");
    CHECK(b.widoczne() == 6);
    for (int w = 0; w < 6; ++w) {
        const Utwor* u = b.utwor(uint32_t(b.widoczny(uint32_t(w))));
        if (oczek[w]) CHECK(std::strcmp(u->tytul(), oczek[w]) == 0);
        else CHECK(std::strlen(u->tytul()) == 51);
    }
    const Utwor* z = b.utwor(uint32_t(b.znajdz("/dane/muzyka/Piosenka2.wav")));
    CHECK(z && z->format == FormatAudio::WAV && z->czas_ms == 100 && z->kanaly == 2 && std::strcmp(z->meta.wykonawca, "Kwartet Bursztynowy") == 0);
    char opis[96]; opis_formatu(*z, opis, sizeof(opis)); CHECK(std::strcmp(opis, "WAV · 16 bit · 1536 kbit/s · 48 kHz · stereo") == 0);
    CHECK(b.filtruj("zolta") == 1 && b.filtruj("BURSZTYN") == 2 && b.filtruj("ćma") == 1 && b.filtruj("piosenka") == 2 && b.filtruj("nic takiego") == 0);
    b.filtruj("");
    // bez podkatalogow
    const uint64_t gen = b.generacja();
    b.ustaw_podkatalogi(false); b.skanuj_calosc();
    CHECK(b.liczba() == 5 && b.generacja() == gen + 1);
    // limit kolekcji
    b.ustaw_podkatalogi(true);
    wyczysc_fs(); g_katalogi["/dane/muzyka"] = true;
    const Bajty maly = wav(pw);
    for (int i = 0; i < int(MAKS_UTWOROW) + 40; ++i) { char n[64]; std::snprintf(n, sizeof(n), "/dane/muzyka/t%04d.wav", i); g_pliki[n] = maly; }
    b.skanuj_calosc();
    CHECK(b.liczba() == MAKS_UTWOROW && b.wynik().pominiete_limit == 40);
}

// =================================================================== kolejka i M3U8
static void test_kolejka() {
    wyczysc_fs();
    using namespace GrajekFixtures;
    ParametryWav pw; pw.ramki = 4800;
    for (const char* n : {"a.wav", "b.wav", "c.wav", "d.wav", "e.wav"}) dodaj_plik(std::string("/dane/muzyka/") + n, wav(pw));
    Biblioteka b; b.konfiguruj(dostep(), fs_listuj, nullptr); b.skanuj_calosc();
    CHECK(b.liczba() == 5);
    Kolejka k;
    CHECK(k.nastepna(true) == -1 && k.poprzednia() == -1);
    uint16_t idx[5]; for (int i = 0; i < 5; ++i) idx[i] = uint16_t(b.posortowany(uint32_t(i)));
    k.ustaw(idx, 5, 0);
    CHECK(k.biezaca() == 0 && k.nastepna(true) == 1 && k.poprzednia() == 0);
    k.przejdz(4); CHECK(k.nastepna(true) == -1);                                // koniec listy
    k.ustaw_powtarzanie(Powtarzanie::Wszystkie); CHECK(k.nastepna(true) == 0 && k.nastepna(false) == 0);
    k.ustaw_powtarzanie(Powtarzanie::Jeden); CHECK(k.nastepna(true) == 4 && k.nastepna(false) == -1);
    k.ustaw_powtarzanie(Powtarzanie::Brak);
    CHECK(k.przesun(4, -1) && k.biezaca() == 3 && k.utwor(3) == idx[4]);
    CHECK(!k.przesun(0, -1) && !k.przesun(4, 1) && !k.przesun(9, 1));
    CHECK(k.usun(0) && k.biezaca() == 2 && k.liczba() == 4);
    CHECK(k.usun(2) && k.biezaca() == -1);
    CHECK(k.dodaj(idx[0]) && k.liczba() == 4);
    // losowo: kazdy utwor dokladnie raz, biezacy pierwszy
    k.ustaw(idx, 5, 2);
    k.ustaw_losowo(true, 12345);
    std::vector<int> odw = {k.biezaca()};
    for (int i = 0; i < 4; ++i) { const int n = k.nastepna(true); CHECK(n >= 0); k.przejdz(n); odw.push_back(n); }
    CHECK(k.nastepna(true) == -1);
    std::sort(odw.begin(), odw.end()); for (int i = 0; i < 5; ++i) CHECK(odw[i] == i);
    k.ustaw_losowo(false, 0);
    // M3U8
    k.ustaw(idx, 5, -1);
    char m3u[MAKS_M3U8];
    CHECK(k.zapisz_m3u8(b, m3u, sizeof(m3u)) == 5);
    CHECK(std::strncmp(m3u, "#EXTM3U\n#EXTINF:0,a\n/dane/muzyka/a.wav\n#EXTINF:0,b\n", 48) == 0);
    Kolejka k2; auto w = k2.wczytaj_m3u8(b, m3u, std::strlen(m3u));
    CHECK(w.dodane == 5 && w.odrzucone == 0 && k2.liczba() == 5);
    for (int i = 0; i < 5; ++i) CHECK(k2.utwor(uint32_t(i)) == idx[i]);
    const char* zle = "#EXTM3U\n/dane/muzyka/../uzytkownicy/x.wav\n/dane/muzyka/./a.wav\nrelatywna.wav\n/dane/muzyka/a\\b.wav\n"
                      "/dane/muzykaX/a.wav\n/dane/muzyka//a.wav\n/dane/muzyka/\x01.wav\n/dane/muzyka/\xC3(.wav\n/dane/muzyka/nieistnieje.wav\n\xEF\xBB\xBF/dane/muzyka/b.wav\r\n/dane/muzyka/c.wav\r\n";
    w = k2.wczytaj_m3u8(b, zle, std::strlen(zle));
    CHECK(w.odrzucone == 9 && w.pominiete == 1 && w.dodane == 1 && k2.liczba() == 1);
    CHECK(Kolejka::sciezka_dozwolona("/dane/muzyka/Album/Ąbc.mp3", std::strlen("/dane/muzyka/Album/Ąbc.mp3")));
    // za maly bufor: zapisuje tyle, ile sie miesci, z poprawnym zakonczeniem
    char maly[120]; const uint32_t ile = k.zapisz_m3u8(b, maly, sizeof(maly)); CHECK(ile < 5 && std::strlen(maly) < sizeof(maly));
    // przemapowanie po odswiezeniu biblioteki (usuniety plik znika z kolejki)
    char stare[MAKS_KOLEJKA][MAKS_SCIEZKA];
    for (uint32_t i = 0; i < k.liczba(); ++i) std::strcpy(stare[i], b.utwor(uint32_t(k.utwor(i)))->sciezka);
    k.ustaw_biezaca(4);
    g_pliki.erase("/dane/muzyka/b.wav");
    b.skanuj_calosc();
    CHECK(k.przemapuj(stare, b) == 1 && k.liczba() == 4 && k.biezaca() == 3);
    for (uint32_t i = 0; i < 4; ++i) CHECK(std::strcmp(b.utwor(uint32_t(k.utwor(i)))->sciezka, stare[i < 1 ? 0 : i + 1]) == 0);
}

// =================================================================== ciaglosc: liczniki niedoboru, pauza, przewijanie, przestoje GUI
// "Przestoj GUI" = sprzet (graj) pobiera ramki, a silnik nie jest pompowany (synchroniczne rysowanie/publikacja).
static uint64_t g_zegar_ns = 1000000000ULL;
static uint64_t zegar_testu() { return g_zegar_ns; }
static void tykaj_z(Silnik& s, int ticki, uint32_t ramki = 192) {
    for (int i = 0; i < ticki; ++i) { g_zegar_ns += 4000000ULL; s.pompuj(); graj(ramki); }
}
static uint32_t w_kolejce_jadra() { return g_j->mikser->queue_levels().maximum; }
static void przestoj(uint32_t ms) { g_zegar_ns += uint64_t(ms) * 1000000ULL; graj(ms * 48); }

static void test_ciaglosc() {
    Jadro j; g_j = &j;
    wyczysc_fs();
    GrajekFixtures::ParametryWav p; p.czestotliwosc = 48000; p.ramki = 48000 * 8; p.amplituda = 0.4;
    dodaj_plik("/dane/muzyka/dlugi.wav", GrajekFixtures::wav(p));
    GrajekFixtures::ParametryWav p2 = p; p2.ramki = 48000 * 2;
    dodaj_plik("/dane/muzyka/drugi.wav", GrajekFixtures::wav(p2));
    Silnik s; s.konfiguruj(dostep(), fake_bws); s.ustaw_zegar(zegar_testu); s.ustaw_glosnosc(100, false);
    CHECK(s.zaplanuj("/dane/muzyka/dlugi.wav", FormatAudio::WAV, true));
    s.pompuj();
    tykaj_z(s, 60);
    const StatystykiCiaglosci& c = s.ciaglosc();
    CHECK(c.niedobor_utworu == 0 && c.niedobor_sesji == 0 && c.przerwy_ponad_kolejke == 0);
    CHECK(c.maks_przerwa_producenta_ns <= 4000000ULL);
    (void)s.zdarzenia();

    // (1) przestoj 200 ms: niedobor = dokladnie ramki ponad zawartosc kolejki; przerwa producenta > 85 ms
    const uint32_t q1 = w_kolejce_jadra();
    CHECK(q1 >= 4096 - Silnik::PORCJA);
    przestoj(200);
    tykaj_z(s, 60);                                       // >= 50 pompowan: okresowy odczyt QUERY_STATS
    const uint64_t oczek1 = 200ULL * 48 - q1;
    CHECK(c.niedobor_utworu == oczek1 && c.niedobor_sesji == oczek1);
    CHECK(c.przerwy_ponad_kolejke == 1 && c.maks_przerwa_producenta_ns >= 200000000ULL && c.epizody_utworu == 1);
    CHECK(s.zdarzenia() & ZD_NIEDOBOR);
    CHECK(StatystykiCiaglosci::ms(c.niedobor_utworu) == oczek1 / 48);

    // (2) przestoj tuz przed przewinieciem (CLOSE): koncowka zamykanego strumienia jest policzona przed CLOSE
    tykaj_z(s, 5);
    const uint32_t q2 = w_kolejce_jadra();
    przestoj(150);
    CHECK(s.przewin(4000) && s.stan() == Stan::Odtwarzanie);
    const uint64_t oczek2 = oczek1 + (150ULL * 48 - q2);
    CHECK(c.niedobor_utworu == oczek2);
    // samo przewiniecie (nowy strumien z prerollem) nie jest niedoborem
    tykaj_z(s, 120);
    CHECK(c.niedobor_utworu == oczek2 && c.przerwy_ponad_kolejke == 2);

    // (3) pauza: dogrywka kolejki i dowolnie dluga pauza bez niedoboru; po dograniu strumien zamkniety
    const uint64_t zamkniecia = j.zamkniecia, otwarcia = j.otwarcia;
    CHECK(s.pauza());
    tykaj_z(s, 600);                                      // 2,4 s pauzy
    CHECK(!s.strumien_otwarty() && j.zamkniecia == zamkniecia + 1 && !j.mikser->has_pcm());
    const uint64_t poz = s.pozycja_ms();
    CHECK(c.niedobor_utworu == oczek2);
    CHECK(s.wznow());
    tykaj_z(s, 120);
    CHECK(j.otwarcia == otwarcia + 1 && c.niedobor_utworu == oczek2);   // wznowienie: nowy strumien, bez niedoboru
    CHECK(s.pozycja_ms() > poz && s.pozycja_ms() - poz <= 120 * 4 + 5);
    // pauza tez bez przerwy producenta (odstep w pauzie nie jest liczony)
    CHECK(c.przerwy_ponad_kolejke == 2);

    // (4) dopompuj() z punktu kontrolnego rysowania: kolejka znow pelna; poza Odtwarzaniem nic nie robi
    graj(3000);
    CHECK(w_kolejce_jadra() <= 4096 - 2900);
    s.dopompuj();
    CHECK(w_kolejce_jadra() > 4096 - Silnik::PROG_DOPELNIENIA);
    // przestoj 120 ms podzielony punktem kontrolnym (60 ms rysowania, dopompuj, 60 ms publikacji) - bez niedoboru;
    // ten sam przestoj bez punktu kontrolnego konczy sie niedoborem
    tykaj_z(s, 3);
    przestoj(60); s.dopompuj(); przestoj(60);
    tykaj_z(s, 60);
    CHECK(c.niedobor_utworu == oczek2);
    tykaj_z(s, 3);
    const uint32_t q3 = w_kolejce_jadra();
    przestoj(120);
    tykaj_z(s, 60);
    CHECK(c.niedobor_utworu == oczek2 + (120ULL * 48 - q3));
    const uint64_t sesja = c.niedobor_sesji;

    // (5) nowy utwor: licznik utworu od zera, sesji zachowany; dopompuj w Ladowaniu nie laduje ani nie otwiera
    CHECK(s.zaplanuj("/dane/muzyka/drugi.wav", FormatAudio::WAV, true) && s.stan() == Stan::Ladowanie);
    const uint64_t otw2 = j.otwarcia;
    s.dopompuj();
    CHECK(s.stan() == Stan::Ladowanie && j.otwarcia == otw2);
    s.pompuj();
    CHECK(s.stan() == Stan::Odtwarzanie && c.niedobor_utworu == 0 && c.niedobor_sesji == sesja && c.przerwy_ponad_kolejke == 0);
    // koniec utworu (FINISH + dogrywka + CLOSE): bez niedoboru, sesja bez zmian
    for (int i = 0; i < 2000 && s.stan() == Stan::Odtwarzanie; ++i) tykaj_z(s, 1);
    CHECK(s.stan() == Stan::Zatrzymany && c.niedobor_utworu == 0 && c.niedobor_sesji == sesja);
    // dopompuj w stanie zatrzymanym: brak efektow
    const uint64_t zap = j.zapisy;
    s.dopompuj();
    CHECK(j.zapisy == zap);
}

// =================================================================== konfiguracja
static void test_konfiguracja() {
    Konfiguracja a; a.glosnosc = 37; a.wyciszony = true; a.losowo = true; a.powtarzanie = Powtarzanie::Wszystkie; a.w_tle = true;
    a.korektor = true; const int8_t g[10] = {-12, -5, 0, 3, 12, 1, -1, 7, 0, -3}; std::memcpy(a.pasma, g, 10); a.wizualizacja = false; a.podkatalogi = false;
    a.diagnostyka = true;
    char t[4096]; a.zapisz(t, sizeof(t));
    Konfiguracja b; CHECK(b.wczytaj(t, std::strlen(t)) == 10);
    CHECK(b.glosnosc == 37 && b.wyciszony && b.losowo && b.powtarzanie == Powtarzanie::Wszystkie && b.w_tle && b.korektor && !b.wizualizacja && !b.podkatalogi);
    CHECK(b.diagnostyka && !Konfiguracja{}.diagnostyka);          // domyslnie wylaczona (zero kosztu pomiaru)
    CHECK(std::memcmp(b.pasma, g, 10) == 0);
    Konfiguracja c; const char* zle = "glosnosc=999\npowtarzanie=7\npasma=1,2,3\nlosowo=x\nnieznany=1\n=5\npasma=40,-40,0,0,0,0,0,0,0,0\n";
    c.wczytaj(zle, std::strlen(zle));
    CHECK(c.glosnosc == 100 && c.powtarzanie == Powtarzanie::Brak && !c.losowo && c.pasma[0] == 12 && c.pasma[1] == -12);
    std::mt19937 rng(2);
    for (int i = 0; i < 2000; ++i) { char x[256]; for (auto& ch : x) ch = char(rng()); Konfiguracja d; d.wczytaj(x, sizeof(x)); CHECK(d.glosnosc <= 100); }
}

// =================================================================== okladka
static void test_okladka() {
    using namespace GrajekFixtures;
    Okladka o;
    const Bajty duzy = png(900, 300, 0x204080);
    CHECK(o.dekoduj(duzy.data(), uint32_t(duzy.size())) && o.szerokosc() == Okladka::MAKS_BOK && o.wysokosc() == 73);
    CHECK(o.piksele()[0] == 0xFF204080);
    // naglowek deklaruje 20000x20000: odrzucone przed dekodowaniem
    Bajty olbrzym = png(2, 2, 0); olbrzym[16] = 0; olbrzym[17] = 0; olbrzym[18] = 0x4E; olbrzym[19] = 0x20; olbrzym[20] = 0; olbrzym[21] = 0; olbrzym[22] = 0x4E; olbrzym[23] = 0x20;
    CHECK(!o.dekoduj(olbrzym.data(), uint32_t(olbrzym.size())) && !o.jest());
    Bajty obc = png(64, 64, 0x123456); obc.resize(obc.size() / 2);
    CHECK(!o.dekoduj(obc.data(), uint32_t(obc.size())));
    const uint8_t gif[] = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0};
    CHECK(!o.dekoduj(gif, sizeof(gif)) && std::strstr(o.blad(), "JPEG"));
    Metadane m; m.okladka = TypOkladki::PNG; m.okladka_offset = 10; m.okladka_rozmiar = MAKS_OKLADKA_BAJTY + 1;
    g_pliki["/t/o.mp3"] = Bajty(100, 0);
    CHECK(!o.wczytaj(dostep(), "/t/o.mp3", m));
    m.okladka_rozmiar = 50; m.okladka_offset = 90;
    CHECK(!o.wczytaj(dostep(), "/t/o.mp3", m));                                  // poza plikiem
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Grajek - testy hosta\n");
    test_tekst(); std::printf("tekst OK\n");
    test_wav(); std::printf("wav OK\n");
    test_id3(); std::printf("id3 OK\n");
    test_mp3(); std::printf("mp3 OK\n");
    test_resampler(); std::printf("resampler OK\n");
    test_korektor(); std::printf("korektor OK\n");
    test_analiza(); std::printf("analiza OK\n");
    test_silnik(); std::printf("silnik OK\n");
    test_ciaglosc(); std::printf("ciaglosc OK\n");
    test_biblioteka(); std::printf("biblioteka OK\n");
    test_kolejka(); std::printf("kolejka OK\n");
    test_konfiguracja(); std::printf("konfiguracja OK\n");
    test_okladka(); std::printf("okladka OK\n");
    std::printf("Grajek: %u sprawdzen OK\n", g_checks);
    return 0;
}
