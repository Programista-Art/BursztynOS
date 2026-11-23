#pragma once
// Grajek - wspolne srodowisko testow hosta: wirtualny system plikow (odczyt fragmentu, rozmiar, listing BWS 6)
// oraz symulacja BWS 110 na PRAWDZIWYM mikserze jadra (jadro/dzwiek/mikser_pcm.h) z wstrzykiwaniem BUSY,
// czesciowych zapisow i brakiem urzadzenia. graj() odgrywa role DMA HDA: pobiera ramki z miksera.
#include "grajek_fixtures.h"
#include "programy/grajek/zrodlo.h"
#include "programy/grajek/biblioteka.h"
#include "jadro/bws/pcm_api.h"
#include "jadro/dzwiek/mikser_pcm.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using GrajekFixtures::Bajty;
using Grajek::DostepDoPlikow;

// =================================================================== wirtualny system plikow
static std::map<std::string, Bajty> g_pliki;
static std::map<std::string, bool> g_katalogi;
static uint64_t g_odczyty = 0;

static bool fs_odczyt(void*, const char* s, uint64_t off, void* buf, uint32_t n, uint32_t* got) {
    ++g_odczyty;
    *got = 0;
    auto it = g_pliki.find(s);
    if (it == g_pliki.end() || n == 0 || n > 1024 * 1024) return false;
    if (off >= it->second.size()) return true;
    const size_t k = std::min<size_t>(n, it->second.size() - off);
    std::memcpy(buf, it->second.data() + off, k);
    *got = uint32_t(k);
    return true;
}
static bool fs_rozmiar(void*, const char* s, uint64_t* r) {
    auto it = g_pliki.find(s);
    if (it == g_pliki.end()) return false;
    *r = it->second.size();
    return true;
}
static bool fs_listuj(void*, const char* s, char* buf, uint32_t cap) {
    if (!g_katalogi.count(s)) return false;
    std::string prefiks = std::string(s) + "/";
    std::string out;
    for (auto& [k, v] : g_katalogi) {
        (void)v;
        if (k.size() > prefiks.size() && k.compare(0, prefiks.size(), prefiks) == 0 && k.find('/', prefiks.size()) == std::string::npos)
            out += "[KAT]  " + k.substr(prefiks.size()) + "\n";
    }
    for (auto& [k, v] : g_pliki) {
        (void)v;
        if (k.size() > prefiks.size() && k.compare(0, prefiks.size(), prefiks) == 0 && k.find('/', prefiks.size()) == std::string::npos)
            out += "[PLIK] " + k.substr(prefiks.size()) + "\n";
    }
    if (out.size() + 1 > cap) out.resize(cap - 1);
    std::memcpy(buf, out.c_str(), out.size() + 1);
    return true;
}
static DostepDoPlikow dostep() { DostepDoPlikow d; d.odczyt = fs_odczyt; d.rozmiar = fs_rozmiar; return d; }
static void dodaj_plik(const std::string& s, Bajty b) {
    g_pliki[s] = std::move(b);
    for (size_t p = s.find('/', 1); p != std::string::npos; p = s.find('/', p + 1)) g_katalogi[s.substr(0, p)] = true;
}
static void wyczysc_fs() { g_pliki.clear(); g_katalogi.clear(); }

// =================================================================== symulowany BWS 110 na mikserze jadra
struct Jadro {
    std::unique_ptr<Pcm::Mixer> mikser = std::make_unique<Pcm::Mixer>();
    Pcm::Owner wlasciciel{77, 3};
    bool urzadzenie = true;
    uint32_t zajety_co = 0;        // co n-te wywolanie zwraca PCM_BUSY (0 = nigdy)
    uint32_t limit_zapisu = 0;     // maks. ramek akceptowanych w jednym zapisie (0 = bez limitu)
    uint64_t wywolania = 0, otwarcia = 0, zamkniecia = 0, finish = 0, gain_ost_v = 999, gain_ost_m = 9, zapisy = 0;
    std::vector<int16_t> wyjscie;  // to, co "zagral" mikser (stereo)
    void reset() { *this = Jadro{}; }
};
static Jadro* g_j = nullptr;

static uint64_t fake_bws(uint64_t nr, uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    Jadro& j = *g_j;
    if (nr != BWS_PCM) return 0;
    ++j.wywolania;
    auto r = [](Pcm::Result x) -> int64_t {
        switch (x) { case Pcm::OK: return PCM_OK; case Pcm::UNSUPPORTED: return PCM_UNSUPPORTED; case Pcm::LIMIT: return PCM_LIMIT;
                     case Pcm::BAD_HANDLE: return PCM_HANDLE; case Pcm::FULL: return PCM_FULL; default: return PCM_INVALID; }
    };
    if (j.zajety_co && op != PCM_CLOSE && j.wywolania % j.zajety_co == 0) return uint64_t(int64_t(PCM_BUSY));
    if (op == PCM_OPEN) {
        if (b != sizeof(BwsPcmFormat) || c != BWS_PCM_V1) return uint64_t(int64_t(PCM_INVALID));
        if (!j.urzadzenie) return uint64_t(int64_t(PCM_DEVICE));
        BwsPcmFormat f; std::memcpy(&f, reinterpret_cast<void*>(a), sizeof(f));
        if (f.version != BWS_PCM_V1 || f.size != sizeof(f) || f.reserved) return uint64_t(int64_t(PCM_INVALID));
        uint64_t h = 0;
        const auto rc = j.mikser->open(j.wlasciciel, {f.rate, f.channels, f.encoding}, &h, 2048);
        if (rc != Pcm::OK) return uint64_t(r(rc));
        ++j.otwarcia;
        return h;
    }
    if (op == PCM_WRITE) {
        if (!c || c > BWS_PCM_MAX_FRAMES) return uint64_t(int64_t(PCM_INVALID));
        Pcm::Status s{};
        if (j.mikser->status(j.wlasciciel, a, &s) != Pcm::OK) return uint64_t(int64_t(PCM_HANDLE));
        uint32_t ramki = uint32_t(c);
        if (j.limit_zapisu && ramki > j.limit_zapisu) ramki = j.limit_zapisu;
        uint32_t acc = 0;
        const auto rc = j.mikser->write(j.wlasciciel, a, reinterpret_cast<const int16_t*>(b), ramki, &acc, 1);
        ++j.zapisy;
        return rc == Pcm::OK ? acc : uint64_t(r(rc));
    }
    if (op == PCM_QUERY) {
        if (c != sizeof(BwsPcmState)) return uint64_t(int64_t(PCM_INVALID));
        Pcm::Status s{};
        const auto rc = j.mikser->status(j.wlasciciel, a, &s);
        if (rc != Pcm::OK) return uint64_t(r(rc));
        BwsPcmState st{BWS_PCM_V1, sizeof(st), s.queued, s.capacity, s.volume, s.muted, s.channels, 48000};
        std::memcpy(reinterpret_cast<void*>(b), &st, sizeof(st));
        return PCM_OK;
    }
    if (op == PCM_QUERY_STATS) {
        BwsPcmStats st{};
        const auto rc = j.mikser->statistics(j.wlasciciel, a, st);
        if (rc != Pcm::OK) return uint64_t(r(rc));
        std::memcpy(reinterpret_cast<void*>(b), &st, size_t(c));
        return PCM_OK;
    }
    if (op == PCM_GAIN) { j.gain_ost_v = b; j.gain_ost_m = c; return uint64_t(r(j.mikser->gain(j.wlasciciel, a, uint32_t(b), uint32_t(c)))); }
    if (op == PCM_CLOSE) { const auto rc = j.mikser->close(j.wlasciciel, a); if (rc == Pcm::OK) ++j.zamkniecia; return uint64_t(r(rc)); }
    if (op == PCM_FINISH) { ++j.finish; return uint64_t(r(j.mikser->finish(j.wlasciciel, a))); }
    return uint64_t(int64_t(PCM_INVALID));
}

// "Sprzet": pobiera z miksera `ramki` ramek (jak DMA HDA) i zapisuje je jako odtworzone.
static uint32_t graj(uint32_t ramki) {
    Pcm::Frame f[1024];
    uint32_t zrobione = 0;
    while (zrobione < ramki) {
        const uint32_t n = std::min<uint32_t>(1024, ramki - zrobione);
        if (g_j->mikser->preview(f, n) != Pcm::OK) break;
        // zapisz tylko probki strumieni gotowych (preview miksuje 0 dla brakow)
        for (uint32_t i = 0; i < n; ++i) { g_j->wyjscie.push_back(f[i].left); g_j->wyjscie.push_back(f[i].right); }
        g_j->mikser->consume(n, 1);
        zrobione += n;
    }
    return zrobione;
}

