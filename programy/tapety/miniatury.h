#pragma once
#include "../../bursztyn_gui.h"
#include "../../tapety_format.h"
#include "../../narzedzia/tapety-btp/tapetybtp.h"

// Dziesiec malych buforow; jeden plik i jeden obraz zrodlowy naraz.
class MiniaturyTapet {
    uint32_t* piksele_ = nullptr;
public:
    static constexpr size_t bajty = TAPETY_LICZBA * TAPETY_MINI_W * TAPETY_MINI_H * sizeof(uint32_t);
    MiniaturyTapet() = default;
    MiniaturyTapet(const MiniaturyTapet&) = delete;
    MiniaturyTapet& operator=(const MiniaturyTapet&) = delete;
    ~MiniaturyTapet() { if (piksele_) gui_free(piksele_); }
    bool przygotuj() {
        if (piksele_) return true;
        piksele_ = static_cast<uint32_t*>(gui_malloc(bajty));
        if (!piksele_) return false;
        for (size_t i = 0; i < bajty / 4; ++i) piksele_[i] = TAPETY_TLO;
        return true;
    }
    uint32_t* obraz(unsigned i) const {
        return piksele_ && i < TAPETY_LICZBA ? piksele_ + i * TAPETY_MINI_W * TAPETY_MINI_H : nullptr;
    }
    bool wczytaj(unsigned i) {
        if (!obraz(i)) return false;
        uint32_t n = 0, read = 0;
        if (!pobierz_rozmiar_pliku(TAPETY_SCIEZKI[i], &n) || n < 14 || n > TAPETY_PLIK_MAX) return false;
        auto* plik = static_cast<uint8_t*>(gui_malloc(n));
        if (!plik) return false;
        BtpInfo info{};
        bool ok = czytaj_fragment_pliku(TAPETY_SCIEZKI[i], 0, reinterpret_cast<char*>(plik), n, &read) &&
            read == n && btpnaglowek(plik, n, &info) == BTPOK;
        size_t count = ok ? static_cast<size_t>(info.szerokosc) * info.wysokosc : 0;
        if (count > TAPETY_PIKSELE_MAX) ok = false;
        auto* src = ok ? static_cast<uint32_t*>(gui_malloc(count * 4)) : nullptr;
        if (!src) ok = false;
        if (ok) {
            for (size_t p = 0; p < count; ++p) src[p] = TAPETY_TLO;
            ok = btpwczytajirysuj(plik, n, src, info.szerokosc, info.wysokosc, info.szerokosc, count * 4) == BTPOK &&
                skaluj_obraz(src, info.szerokosc, info.wysokosc, count * 4,
                    obraz(i), TAPETY_MINI_W, TAPETY_MINI_H, TAPETY_MINI_W,
                    TAPETY_MINI_W * TAPETY_MINI_H * 4, TrybSkalowaniaObrazu::Dopasuj, TAPETY_TLO);
        }
        if (src) gui_free(src);
        gui_free(plik);
        return ok;
    }
};
