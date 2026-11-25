#pragma once
// Grajek - wspolne srodowisko testow hosta: wirtualny system plikow (odczyt fragmentu, rozmiar, listing BWS 6)
// oraz symulacja BWS 110 na PRAWDZIWYM mikserze jadra (jadro/dzwiek/mikser_pcm.h) z wstrzykiwaniem BUSY,
// czesciowych zapisow i brakiem urzadzenia. graj() odgrywa role DMA HDA: pobiera ramki z miksera.
#include "../jadro/bws/bws_magazyn.h"
#include "grajek_fixtures.h"
#include "programy/grajek/zrodlo.h"
#include "programy/grajek/biblioteka.h"
#include "jadro/bws/pcm_api.h"
#include "jadro/dzwiek/mikser_pcm.h"
#include <algorithm>
#include <cstdio>
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
// Grafiki Grajka jak w systemie: prawdziwe pliki z repozytorium (ikony po konwersji przy budowaniu - Makefile:
// $(GRAJEK_ZASOBY_DIR)/.gotowe - i oryginalna plyta winylowa) pod /ikonki/. wyczysc_fs() dodaje je ponownie, chyba
// ze test wylaczy je (g_zasoby_grajka = false), np. aby sprawdzic brak lub uszkodzenie grafiki.
static bool g_zasoby_grajka = true;
static const char* const AKCJE_IKON_GRAJKA[] = {"poprzedni", "odtworz", "pauza", "stop", "nastepny", "wycisz", "odswiez",
                                               "dodaj", "w_gore", "w_dol", "usun", "wyczysc", "zapisz", "wczytaj"};
static Bajty plik_repozytorium(const std::string& sciezka) {
    Bajty b;
    if (FILE* f = std::fopen(sciezka.c_str(), "rb")) {
        unsigned char buf[65536]; size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) b.insert(b.end(), buf, buf + n);
        std::fclose(f);
    }
    return b;
}
static void dodaj_plik(const std::string& s, Bajty b);
static size_t dodaj_zasoby_grajka() {
    size_t n = 0;
    for (const char* a : AKCJE_IKON_GRAJKA) {
        Bajty b = plik_repozytorium(std::string("grajek_zasoby/grajek_") + a + ".png");
        if (!b.empty()) { dodaj_plik(std::string("/ikonki/grajek_") + a + ".png", std::move(b)); ++n; }
    }
    Bajty p = plik_repozytorium("ikonki/plyta_winylowa.jpg");
    if (!p.empty()) { dodaj_plik("/ikonki/grajek_plyta_winylowa.jpg", std::move(p)); ++n; }
    return n;
}
static void wyczysc_fs() { g_pliki.clear(); g_katalogi.clear(); if (g_zasoby_grajka) dodaj_zasoby_grajka(); }

// =================================================================== symulowany BWS 110 na mikserze jadra
struct Jadro {
    std::unique_ptr<Pcm::Mixer> mikser = std::make_unique<Pcm::Mixer>();
    Pcm::Owner wlasciciel{77, 3};
    bool urzadzenie = true;
    uint32_t zajety_co = 0;        // co n-te wywolanie zwraca PCM_BUSY (0 = nigdy)
    uint32_t limit_zapisu = 0;     // maks. ramek akceptowanych w jednym zapisie (0 = bez limitu)
    uint64_t wywolania = 0, otwarcia = 0, zamkniecia = 0, finish = 0, gain_ost_v = 999, gain_ost_m = 9, zapisy = 0;
    std::vector<int16_t> wyjscie;  // to, co "zagral" mikser (stereo)
    // planista (PcmSchedDiag w jadrze): wartosci zwracane w BwsPcmStats V2
    uint64_t czekanie_cpu_ns = 0, uspienie_ns = 0;
    // BWS 111: 0 = brak wywolania (starsze jadro), 1 = trwaly dysk BSP2, 2 = RAM (Live)
    int magazyn = 0;
    void reset() { *this = Jadro{}; }
};
static Jadro* g_j = nullptr;

static uint64_t fake_bws(uint64_t nr, uint64_t op, uint64_t a, uint64_t b, uint64_t c) {
    Jadro& j = *g_j;
    if (nr == 111) {
        if (!j.magazyn) return 1;   // nieznane wywolanie: blad wersji
        if (b != BWS_MAGAZYN_V1 || a < sizeof(BwsMagazynV1)) return 1;   // (bufor, rozmiar, wersja)
        BwsMagazynV1 m{};
        m.wersja = BWS_MAGAZYN_V1; m.rozmiar = sizeof(m);
        m.flagi = BWS_MAG_GOTOWY | (j.magazyn == 1 ? BWS_MAG_TRWALY | BWS_MAG_ZAINSTALOWANY : 0u);
        m.pojemnosc_bajty = 1ULL << 31; m.wolne_bajty = 1ULL << 30;
        std::memcpy(reinterpret_cast<void*>(op), &m, sizeof(m));
        return BWS_MAG_OK;
    }
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
        st.max_runnable_wait_ns = j.czekanie_cpu_ns; st.max_voluntary_wait_ns = j.uspienie_ns;
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

