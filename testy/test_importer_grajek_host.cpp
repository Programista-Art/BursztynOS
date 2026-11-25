// Host E2E: plik zaimportowany przez Bursztynowy Importer Plikow -> montowanie obrazu produkcyjnym kodem PSF jadra
// (jak zainstalowany system) -> Grajek (biblioteka, metadane, dekodery WAV/MP3) czyta go strumieniowo przez
// czytaj_fragment_pliku z 64-bitowymi przesunieciami. Bez QEMU: to model sciezki odczytu, nie test dzwieku.
//
//   test_importer_grajek_host generuj <katalog>   pliki zrodlowe (WAV z INFO, MP3, WAV ~130 MiB)
//   test_importer_grajek_host graj <obraz>        montowanie tylko do odczytu + skan /dane/muzyka + dekodowanie
#include "storage_format.h"
#include "psf.h"
#include "sterowniki/czas/zegar-rtc.h"
#include "testy/grajek_fixtures.h"
#include "programy/grajek/zrodlo.h"
#include "programy/grajek/biblioteka.h"
#include "programy/grajek/dekoder.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Storage;
void* kmalloc(size_t n) { return std::malloc(n); }
void kfree(void* p) { std::free(p); }
void wypisz_log(const char*) {}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
bool blokowy_zapis_instalatora() { return false; }

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

constexpr uint32_t DLUGI_RAMKI = 130u * 1024u * 1024u / 4u;          // 130 MiB danych PCM16 stereo
static int16_t probka_dluga(uint32_t i, int k) {
    return int16_t(std::lround(16000.0 * std::sin(2.0 * M_PI * (k ? 523.25 : 440.0) * double(i) / 48000.0)) + (i % 7));
}
static GrajekFixtures::ParametryWav krotki() {
    GrajekFixtures::ParametryWav p; p.ramki = 96000; p.czestotliwosc = 48000; p.info = true; return p;
}

static void le32(FILE* f, uint32_t v) { uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); }
static void le16(FILE* f, uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); }

static int generuj(const std::string& kat) {
    auto zapisz = [](const std::string& s, const GrajekFixtures::Bajty& b) {
        FILE* f = std::fopen(s.c_str(), "wb"); CHECK(f); CHECK(std::fwrite(b.data(), 1, b.size(), f) == b.size()); std::fclose(f);
    };
    zapisz(kat + "/Żółta łódź.wav", GrajekFixtures::wav(krotki()));
    GrajekFixtures::ParametryMp3 pm; pm.ramki = 120;
    zapisz(kat + "/Bursztynowy test.mp3", GrajekFixtures::mp3(pm));
    FILE* f = std::fopen((kat + "/Długi koncert.wav").c_str(), "wb");
    CHECK(f);
    const uint32_t dane = DLUGI_RAMKI * 4;
    std::fwrite("RIFF", 1, 4, f); le32(f, 36 + dane); std::fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16); le16(f, 1); le16(f, 2); le32(f, 48000); le32(f, 48000 * 4); le16(f, 4); le16(f, 16);
    std::fwrite("data", 1, 4, f); le32(f, dane);
    std::vector<int16_t> bufor(2 * 65536);
    for (uint32_t i = 0; i < DLUGI_RAMKI; i += 65536) {
        const uint32_t n = std::min<uint32_t>(65536, DLUGI_RAMKI - i);
        for (uint32_t j = 0; j < n; ++j) { bufor[2 * j] = probka_dluga(i + j, 0); bufor[2 * j + 1] = probka_dluga(i + j, 1); }
        CHECK(std::fwrite(bufor.data(), 4, n, f) == n);
    }
    std::fclose(f);
    std::printf("wygenerowano 3 pliki w %s\n", kat.c_str());
    return 0;
}

struct Plik {
    int fd; uint64_t zapisy = 0; Device dev;
    Plik(int f, uint64_t s) : fd(f), dev{this, s, 512, false, rd, wr, fl} {}
    static bool rd(void* c, uint64_t s, uint32_t n, void* b) {
        auto& p = *static_cast<Plik*>(c);
        return range(s, n, p.dev.sectors) && pread(p.fd, b, size_t(n) * 512, off_t(s * 512)) == ssize_t(size_t(n) * 512);
    }
    static bool wr(void* c, uint64_t, uint32_t, const void*) { ++static_cast<Plik*>(c)->zapisy; return false; }
    static bool fl(void*) { return true; }
};

// Funkcje dostepu Grajka -> PSF jadra (te same wywolania co BWS 59 / 47 / 6 w zainstalowanym systemie).
static uint64_t g_odczyty = 0, g_maks_offset = 0;
static bool odczyt(void*, const char* s, uint64_t off, void* b, uint32_t n, uint32_t* got) {
    ++g_odczyty;
    if (off + n > g_maks_offset) g_maks_offset = off + n;
    return czytaj_fragment_pliku(s, off, static_cast<char*>(b), n, got);
}
static bool rozmiar(void*, const char* s, uint64_t* r) {
    psf_metadane m{};
    if (!pobierz_metadane_tworu(s, &m) || m.typ != TYP_PLIK) return false;
    *r = m.rozmiar;
    return true;
}
static bool listuj(void*, const char* s, char* b, uint32_t cap) { return wylistuj_katalog(s, b, cap); }

static int graj(const char* obraz) {
    const int fd = open(obraz, O_RDONLY);
    CHECK(fd >= 0);
    struct stat st{}; CHECK(fstat(fd, &st) == 0);
    Plik p(fd, uint64_t(st.st_size) / 512);
    Table t{};
    CHECK(gpt_read(&p.dev, &t));
    Partition bsp{};
    for (uint32_t i = 0; i < t.count; ++i)
        if (equal(t.entries[i].type, BspType, 16)) bsp = {&p.dev, t.entries[i].start, t.entries[i].count};
    CHECK(bsp.device && complete(bsp, t.uuid));
    static std::vector<uint8_t> ram(128u << 20);
    psf_ustaw_partycje(&bsp);
    inicjalizuj_psf(ram.data(), uint32_t(ram.size()));
    CHECK(psf_czy_gotowy() && psf_urzadzenie_trwale());          // trwaly montaz, nie RAM
    CHECK(psf_weryfikuj_dysk());

    Grajek::DostepDoPlikow d; d.odczyt = odczyt; d.rozmiar = rozmiar;
    Grajek::Biblioteka b; b.konfiguruj(d, listuj, nullptr);
    b.skanuj_calosc();
    CHECK(b.wynik().katalog_istnieje);
    CHECK(b.liczba() == 3 && b.wynik().odrzucone == 0);
    int wav = -1, mp3 = -1, dlugi = -1;
    for (uint32_t i = 0; i < b.liczba(); ++i) {
        const std::string s = b.utwor(i)->sciezka;
        if (s == "/dane/muzyka/Żółta łódź.wav") wav = int(i);
        if (s == "/dane/muzyka/Bursztynowy test.mp3") mp3 = int(i);
        if (s == "/dane/muzyka/Długi koncert.wav") dlugi = int(i);
    }
    CHECK(wav >= 0 && mp3 >= 0 && dlugi >= 0);
    CHECK(std::strcmp(b.utwor(uint32_t(wav))->tytul(), "Żółta łódź") == 0);            // metadane INFO z dysku

    // WAV: pelne dekodowanie i porownanie z wzorcem
    {
        std::unique_ptr<Grajek::Dekoder> dk(Grajek::utworz_dekoder(Grajek::FormatAudio::WAV));
        CHECK(dk && dk->otworz(d, "/dane/muzyka/Żółta łódź.wav"));
        const auto p = krotki();
        std::vector<int16_t> buf(2 * 4000);
        uint32_t ramka = 0;
        int maks = 0;
        while (uint32_t n = dk->dekoduj(buf.data(), 4000)) {
            for (uint32_t i = 0; i < n; ++i, ++ramka)
                for (int k = 0; k < 2; ++k)
                    maks = std::max(maks, std::abs(int(buf[2 * i + k]) - int(std::lround(GrajekFixtures::probka(p, ramka, k) * 32767.0))));
        }
        CHECK(ramka == p.ramki && maks <= 1 && !dk->ma_blad());
    }
    // MP3: pelne dekodowanie z dysku
    {
        std::unique_ptr<Grajek::Dekoder> dk(Grajek::utworz_dekoder(Grajek::FormatAudio::MP3));
        CHECK(dk && dk->otworz(d, "/dane/muzyka/Bursztynowy test.mp3"));
        CHECK(dk->parametry().czestotliwosc == 48000 && dk->parametry().kanaly == 2);
        std::vector<int16_t> buf(2 * 4096);
        uint64_t ramki = 0, energia = 0;
        while (uint32_t n = dk->dekoduj(buf.data(), 4096)) {
            ramki += n;
            for (uint32_t i = 0; i < 2 * n; ++i) energia += uint64_t(std::abs(int(buf[i])));
        }
        CHECK(ramki >= 119 * 1152 && ramki <= 120 * 1152 && energia > 0 && !dk->ma_blad());
    }
    // Dlugi WAV (> 128 MiB): przewijanie do konca i do polowy, dokladne probki, bez wczytywania calosci
    {
        std::unique_ptr<Grajek::Dekoder> dk(Grajek::utworz_dekoder(Grajek::FormatAudio::WAV));
        g_odczyty = 0;
        CHECK(dk && dk->otworz(d, "/dane/muzyka/Długi koncert.wav"));
        std::vector<int16_t> buf(2 * 4800);
        for (uint32_t start : {DLUGI_RAMKI - 48000u, DLUGI_RAMKI / 2u, 1000u}) {
            CHECK(dk->szukaj(start));
            uint32_t got = 0;
            while (got < 48000) {
                const uint32_t n = dk->dekoduj(buf.data(), 4800);
                CHECK(n > 0);
                for (uint32_t i = 0; i < n; ++i)
                    CHECK(buf[2 * i] == probka_dluga(start + got + i, 0) && buf[2 * i + 1] == probka_dluga(start + got + i, 1));
                got += n;
            }
        }
        CHECK(g_maks_offset > (128ULL << 20));                                    // odczyt za granica 128 MiB
        CHECK(g_odczyty < 64);                                                     // okno 64 KiB, nie caly plik
    }
    CHECK(p.zapisy == 0);
    std::printf("Importer -> PSF jadra -> Grajek: %u sprawdzen OK (3 utwory, WAV/MP3, plik %u MiB, odczyty do %llu MiB)\n",
                checks, unsigned((uint64_t(DLUGI_RAMKI) * 4) >> 20), (unsigned long long)(g_maks_offset >> 20));
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "generuj") return generuj(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "graj") return graj(argv[2]);
    std::fprintf(stderr, "uzycie: generuj <katalog> | graj <obraz>\n");
    return 1;
}
