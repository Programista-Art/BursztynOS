#pragma once

#include <stdint.h>
#include <stddef.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "fuzz_engine.h"
#include "../../storage_format.h"
#include "../../psf.h"
#include "../../zegar-rtc.h"

/* Host runtime symbols required by psf.cpp and storage_format.cpp under -DBURSZTYN_PSF_HOST_TEST */
void* kmalloc(size_t n) { return std::malloc(n); }
void kfree(void* p) { std::free(p); }
void wypisz_log(const char*) {}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) { return false; }
bool blokowy_zapis_instalatora() { return true; }

struct FuzzBsp2RamImage {
    std::vector<uint8_t> bytes;

    explicit FuzzBsp2RamImage(size_t num_blocks = 256)
        : bytes(num_blocks * PSF_ROZMIAR_BLOKU, 0) {}

    void format_valid() {
        psf_ustaw_partycje(nullptr);
        psf_wlacz_translacje_slotow(false);
        std::memset(bytes.data(), 0, bytes.size());
        inicjalizuj_psf(bytes.data(), static_cast<uint32_t>(bytes.size()));
    }

    void remount() {
        psf_ustaw_partycje(nullptr);
        inicjalizuj_psf(bytes.data(), static_cast<uint32_t>(bytes.size()));
    }

    superblok* sb() {
        return reinterpret_cast<superblok*>(bytes.data());
    }

    uint8_t* bitmap_block(size_t idx = 0) {
        return bytes.data() + (1 + idx) * PSF_ROZMIAR_BLOKU;
    }

    wezel_indeksowy* inode(uint64_t id) {
        if (id == 0) return nullptr;
        const uint64_t start_w = sb()->start_wezlow;
        const uint64_t blk = start_w + (id - 1ULL);
        if (blk * PSF_ROZMIAR_BLOKU + sizeof(wezel_indeksowy) > bytes.size()) {
            return nullptr;
        }
        return reinterpret_cast<wezel_indeksowy*>(
            bytes.data() + blk * PSF_ROZMIAR_BLOKU
        );
    }

    uint8_t* data_block(uint64_t rel_blk) {
        const uint64_t start_d = sb()->start_danych;
        const uint64_t blk = start_d + rel_blk;
        if ((blk + 1) * PSF_ROZMIAR_BLOKU > bytes.size()) {
            return nullptr;
        }
        return bytes.data() + blk * PSF_ROZMIAR_BLOKU;
    }
};
