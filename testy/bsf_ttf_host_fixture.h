// Testy hosta z prawdziwymi czcionkami TTF (BSF, DejaVu Sans z czcionki/ttf) - te same metryki co natywny
// BursztynUI. Wymaga kompilacji z -DBSF_NATIVE=1 oraz bsf/bsf.cpp, bsf/runtime.cpp, BSF2_SOURCES, bsf/bitmap.cpp.
#pragma once
#include <fstream>
#include <iterator>
#include <map>
#include <string>
static std::map<std::string, std::string> g_ttf_pliki;
static BSF::Config g_ttf_cfg;
static std::string g_ttf_katalog;
static uint64_t ttf_hook(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t) {
    if (n == 92) {
        auto* cfg = reinterpret_cast<BSF::Config*>(b);
        if (c != sizeof(*cfg)) return 0;
        if (!a) { *cfg = g_ttf_cfg; return 1; }
        if (!BSF::valid(*cfg)) return 0;
        uint32_t g = g_ttf_cfg.generation + 1; g_ttf_cfg = *cfg; g_ttf_cfg.generation = g ? g : 1; return 1;
    }
    if (n == 6) {
        if (strcmp(reinterpret_cast<const char*>(a), "/czcionki/ttf")) return 0;
        if (g_ttf_katalog.size() + 1 > c) return 0;
        memcpy(reinterpret_cast<void*>(b), g_ttf_katalog.c_str(), g_ttf_katalog.size() + 1); return 1;
    }
    if (n == 4) {
        auto it = g_ttf_pliki.find(reinterpret_cast<const char*>(a));
        if (it == g_ttf_pliki.end()) return 0;
        *reinterpret_cast<uint32_t*>(b) = uint32_t(it->second.size()); return 1;
    }
    return 0;
}
bool czytaj_plik(const char* path, char* out, uint32_t cap) {
    auto i = g_ttf_pliki.find(path);
    if (i == g_ttf_pliki.end() || i->second.size() > cap) return false;
    memcpy(out, i->second.data(), i->second.size()); return true;
}
void wypisz(const char*) {}
static bool zaladuj_ttf(const char* katalog_repo = "czcionki/ttf/") {
    g_ttf_cfg.checksum = BSF::checksum(g_ttf_cfg);
    g_test_bws_hook = ttf_hook;
    for (const char* n : {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVuSansMono.ttf"}) {
        std::ifstream f(std::string(katalog_repo) + n, std::ios::binary);
        g_ttf_pliki[std::string("/czcionki/ttf/") + n] = {std::istreambuf_iterator<char>(f), {}};
        g_ttf_katalog += std::string("[PLIK] ") + n + "\n";
    }
    return BSF::enumerate() >= 1 && BSF::ready();
}
