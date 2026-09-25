#pragma once
#include "../../bursztyn_gui.h"
#include "../../tapety_format.h"
#include "../../narzedzia/tapety-btp/tapetybtp.h"

// Dziesiec malych buforow; jeden plik i jeden obraz zrodlowy naraz.
class MiniaturyTapet {
    uint32_t* piksele_ = nullptr;
    uint32_t pojemnosc_ = 0;
public:
    static constexpr size_t bajty = TAPETY_LICZBA * TAPETY_MINI_W * TAPETY_MINI_H * sizeof(uint32_t);
    MiniaturyTapet() = default;
    MiniaturyTapet(const MiniaturyTapet&) = delete;
    MiniaturyTapet& operator=(const MiniaturyTapet&) = delete;
    ~MiniaturyTapet() { if (piksele_) gui_free(piksele_); }
    bool przygotuj(uint32_t liczba = TAPETY_LICZBA) {
        if (liczba == 0) liczba = 1;
        if (piksele_) { gui_free(piksele_); piksele_ = nullptr; }
        const size_t sz = static_cast<size_t>(liczba) * TAPETY_MINI_W * TAPETY_MINI_H * sizeof(uint32_t);
        piksele_ = static_cast<uint32_t*>(gui_malloc(sz));
        if (!piksele_) return false;
        for (size_t i = 0; i < sz / 4; ++i) piksele_[i] = TAPETY_TLO;
        pojemnosc_ = liczba;
        return true;
    }
    uint32_t* obraz(unsigned i) const {
        return piksele_ && i < pojemnosc_ ? piksele_ + i * TAPETY_MINI_W * TAPETY_MINI_H : nullptr;
    }
    void ustaw_placeholder(unsigned i) {
        uint32_t* dst = obraz(i);
        if (!dst) return;
        for (uint32_t y = 0; y < TAPETY_MINI_H; ++y) {
            for (uint32_t x = 0; x < TAPETY_MINI_W; ++x) {
                const bool ramka = (x == 0 || x == TAPETY_MINI_W - 1 || y == 0 || y == TAPETY_MINI_H - 1);
                const bool krzyz = (x * TAPETY_MINI_H == y * TAPETY_MINI_W) ||
                                   ((TAPETY_MINI_W - 1 - x) * TAPETY_MINI_H == y * TAPETY_MINI_W);
                dst[y * TAPETY_MINI_W + x] = ramka ? 0xFF804020U : (krzyz ? 0xFF4A2010U : 0xFF2A1508U);
            }
        }
    }
    static void zglos_blad(const char* path) {
        bws_wywolaj(1, reinterpret_cast<uint64_t>("[TAPETY APP] BLAD miniatury "), 0, 0, 0);
        bws_wywolaj(1, reinterpret_cast<uint64_t>(path ? path : "(nieznany)"), 0, 0, 0);
        bws_wywolaj(1, reinterpret_cast<uint64_t>("\n"), 0, 0, 0);
    }
    bool wczytaj(unsigned i, const char* sciezka = nullptr) {
        if (!obraz(i)) return false;
        const char* path = sciezka ? sciezka : (i < TAPETY_LICZBA ? TAPETY_SCIEZKI[i] : nullptr);
        if (!path) return false;
        uint32_t n = 0, read = 0;
        if (!pobierz_rozmiar_pliku(path, &n) || n < 14 || n > TAPETY_PLIK_MAX) {
            zglos_blad(path);
            return false;
        }
        auto* plik = static_cast<uint8_t*>(gui_malloc(n));
        if (!plik) {
            zglos_blad(path);
            return false;
        }
        BtpInfo info{};
        bool ok = czytaj_fragment_pliku(path, 0, reinterpret_cast<char*>(plik), n, &read) &&
            read == n && btpnaglowek(plik, n, &info) == BTPOK;
        size_t count = ok ? static_cast<size_t>(info.szerokosc) * info.wysokosc : 0;
        if (count > TAPETY_PIKSELE_MAX) ok = false;
        auto* src = ok ? static_cast<uint32_t*>(gui_malloc(count * 4)) : nullptr;
        if (!src) ok = false;
        bool dec_ok = false, scale_ok = false;
        if (ok) {
            for (size_t p = 0; p < count; ++p) src[p] = TAPETY_TLO;
            dec_ok = (btpwczytajirysuj(plik, n, src, info.szerokosc, info.wysokosc, info.szerokosc, count * 4) == BTPOK);
            if (dec_ok) {
                scale_ok = skaluj_obraz(src, info.szerokosc, info.wysokosc, count * 4,
                    obraz(i), TAPETY_MINI_W, TAPETY_MINI_H, TAPETY_MINI_W,
                    TAPETY_MINI_W * TAPETY_MINI_H * 4, TrybSkalowaniaObrazu::Dopasuj, TAPETY_TLO);
            }
            ok = dec_ok && scale_ok;
        }
        if (src) gui_free(src);
        gui_free(plik);

#if BURSZTYN_DIAG_TAPETY
        bws_wywolaj(1, reinterpret_cast<uint64_t>("[TAPETY GUI] Miniatura plik="), 0, 0, 0);
        bws_wywolaj(1, reinterpret_cast<uint64_t>(path ? path : "(nieznany)"), 0, 0, 0);
        bws_wywolaj(1, reinterpret_cast<uint64_t>(info.kompresja == 2 ? " BTP mode=V2" :
            info.kompresja == 0 ? " BTP mode=RAW" : " BTP mode=RLE"), 0, 0, 0);
        bws_wywolaj(1, reinterpret_cast<uint64_t>(dec_ok ? " decode=OK" : " decode=FAIL"), 0, 0, 0);
        bws_wywolaj(1, reinterpret_cast<uint64_t>(scale_ok ? " scale=OK\n" : " scale=FAIL\n"), 0, 0, 0);
#endif

        if (!ok) {
            zglos_blad(path);
        }

        return ok;
    }
};
