// Testy hosta: modul muzyka.pak dla Grajka (wspolny kontener BZP3 z galeria): rozszerzenia audio (.wav/.mp3, bez
// rozrozniania wielkosci liter) przyjmowane tylko przez predykat muzyki, obrazy odrzucane; kopiowanie do /dane/muzyka/,
// rezerwa wolnego miejsca RAM-dysku (wpis pomijany w calosci), brak nadpisywania plikow uzytkownika, sprzatanie pliku
// tymczasowego; galeria bez zmian (domyslny predykat i katalog).
#include "../jadro/fs/galeria_kopiowanie.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <openssl/sha.h>
using namespace GaleriaPak;
static int g_ok = 0, g_fail = 0;
#define CHECK(c) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::map<std::string, std::vector<uint8_t>> g_pliki;
static uint64_t g_wolne = ~0ULL;
static Io io(const char* katalog) {
    static std::string kat; kat = katalog;
    Io i;
    i.rozmiar = [](const char* p) -> uint32_t { auto it = g_pliki.find(p); return it == g_pliki.end() ? 0 : uint32_t(it->second.size()); };
    i.utworz = [](const char* p) { if (g_pliki.count(p)) return false; g_pliki[p] = {}; return true; };
    i.usun = [](const char* p) { return g_pliki.erase(p) > 0; };
    i.zapisz = [](const char* p, uint32_t off, const uint8_t* d, uint32_t n) {
        auto it = g_pliki.find(p); if (it == g_pliki.end() || off != it->second.size() || n > g_wolne) return false;
        g_wolne -= n; it->second.insert(it->second.end(), d, d + n); return true; };
    i.zmien_nazwe = [](const char* p, const char* nowa) {
        std::string dst = kat + nowa;
        auto it = g_pliki.find(p); if (it == g_pliki.end() || g_pliki.count(dst)) return false;
        g_pliki[dst] = it->second; g_pliki.erase(it); return true; };
    i.flush = [] { return true; };
    return i;
}
static bool sha(const uint8_t* d, uint64_t n, uint8_t out[32]) { SHA256(d, size_t(n), out); return true; }

// Kontener BZP3 zgodny z narzedzia/package_galeria.py::zbuduj
static std::vector<uint8_t> kontener(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& w) {
    std::vector<uint8_t> tab;
    uint32_t tabela = 0; for (auto& e : w) tabela += 1 + uint32_t(e.first.size()) + 12;
    uint32_t poz = 48 + tabela;
    auto le = [](std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    for (auto& e : w) {
        tab.push_back(uint8_t(e.first.size())); tab.insert(tab.end(), e.first.begin(), e.first.end());
        le(tab, uint32_t(e.second.size())); le(tab, poz); le(tab, crc32(e.second.data(), e.second.size()));
        poz += uint32_t(e.second.size());
    }
    std::vector<uint8_t> b = {'B', 'Z', 'P', '3'};
    le(b, 1); le(b, uint32_t(w.size())); le(b, uint32_t(tab.size()));
    uint8_t s[32]; sha(tab.data(), tab.size(), s); b.insert(b.end(), s, s + 32);
    b.insert(b.end(), tab.begin(), tab.end());
    for (auto& e : w) b.insert(b.end(), e.second.begin(), e.second.end());
    return b;
}

int main() {
    std::vector<uint8_t> a(3000, 7), m(500000, 9), d(2000000, 3);
    const auto pak = kontener({{"Piosenka1.wav", a}, {"Bursztyn OS 2.MP3", m}, {"Duzy utwór.mp3", d}});
    Wpis wpisy[MAKS_WPISOW]; uint32_t n = 0;
    CHECK(parsuj(pak.data(), pak.size(), wpisy, &n, sha, rozszerzenie_audio) == Blad::OK && n == 3);
    CHECK(parsuj(pak.data(), pak.size(), wpisy, &n, sha) == Blad::Rozszerzenie);            // galeria: tylko obrazy
    const auto obrazy = kontener({{"a.png", a}});
    CHECK(parsuj(obrazy.data(), obrazy.size(), wpisy, &n, sha, rozszerzenie_audio) == Blad::Rozszerzenie);
    const auto zle = kontener({{"x.wav.exe", a}});
    CHECK(parsuj(zle.data(), zle.size(), wpisy, &n, sha, rozszerzenie_audio) == Blad::Rozszerzenie);
    const auto trawers = kontener({{"..", a}});
    CHECK(parsuj(trawers.data(), trawers.size(), wpisy, &n, sha, rozszerzenie_audio) != Blad::OK);
    CHECK(parsuj(pak.data(), pak.size(), wpisy, &n, sha, nullptr) == Blad::Rozszerzenie);
    CHECK(parsuj(pak.data(), pak.size(), wpisy, &n, sha, rozszerzenie_audio) == Blad::OK);

    // kopiowanie z rezerwa: wolne 2,6 MB, rezerwa 1 MB -> dwa mniejsze pliki tak, najwiekszy (2 MB) pominiety w calosci
    g_pliki.clear(); g_wolne = 2600000;
    g_pliki["/dane/muzyka/Piosenka1.wav"] = {'U', 'Z', 'Y', 'T'};                         // plik uzytkownika - bez nadpisania
    Io i = io(KATALOG_MUZYKI);
    i.wolne = [] { return g_wolne; };
    i.rezerwa = 1000000;
    static std::string g_pominiety; static uint32_t g_pominiety_rozmiar = 0; static uint64_t g_pominiety_wolne = 0;
    i.pominieto_bez_miejsca = [](const char* nazwa, uint32_t dl, uint32_t rozmiar, uint64_t wolne) {
        g_pominiety.assign(nazwa, dl); g_pominiety_rozmiar = rozmiar; g_pominiety_wolne = wolne; };
    uint32_t kol[3] = {0, 1, 2};
    Kopiarka k;
    k.start(pak.data(), wpisy, n, i, kol, KATALOG_MUZYKI, NAZWA_TMP_MUZYKI);
    while (k.krok(65536) == Krok::Dalej) {}
    CHECK(k.zainstalowane == 1 && k.pominiete == 1 && k.bez_miejsca == 1 && k.bledne == 0);
    // pominiecie z braku miejsca jest raportowane z nazwa (log jadra), nie tylko jako licznik
    CHECK(g_pominiety == "Duzy utwór.mp3" && g_pominiety_rozmiar > 1900000 && g_pominiety_wolne < g_pominiety_rozmiar + 1000000);
    CHECK(g_pliki["/dane/muzyka/Piosenka1.wav"].size() == 4);
    CHECK(g_pliki.count("/dane/muzyka/Bursztyn OS 2.MP3") && g_pliki["/dane/muzyka/Bursztyn OS 2.MP3"] == m);
    CHECK(!g_pliki.count("/dane/muzyka/Duzy utwór.mp3"));
    CHECK(!g_pliki.count(std::string(KATALOG_MUZYKI) + NAZWA_TMP_MUZYKI));
    for (auto& [s, v] : g_pliki) { (void)v; CHECK(s.rfind("/dane/muzyka/", 0) == 0); }
    // bez rezerwy (galeria): domyslny katalog i nazwa tymczasowa nie zmienione
    g_pliki.clear(); g_wolne = ~0ULL;
    const auto zd = kontener({{"a.png", a}});
    CHECK(parsuj(zd.data(), zd.size(), wpisy, &n, sha) == Blad::OK);
    Kopiarka g; g.start(zd.data(), wpisy, n, io(KATALOG_ZDJEC));
    while (g.krok(65536) == Krok::Dalej) {}
    CHECK(g.zainstalowane == 1 && g_pliki.count("/dane/zdjecia/a.png") && g.bez_miejsca == 0);
    std::printf("%s test_muzyka_pak_host: %d checks, %d failed\n", g_fail ? "FAIL" : "PASS", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
