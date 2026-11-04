/*
 * Bursztyn OS — Fuzzer #2 systemu plikow BSP2/PSF: Wezly indeksowe (inode) i wskazniki blokow.
 * Zawiera deterministyczne testy regresyjne dla Bug #3 (duplikaty wskaznikow
 * posrednich blok_posredni_1/blok_posredni_2 oraz duplikaty blokow danych).
 */

#include "fuzz_bsp2_common.h"
#include <cassert>

static FuzzBsp2RamImage g_img(192);

/* Mock persistent partition so inicjalizuj_psf validates the on-disk image
   via obraz_poprawny() instead of reformatting it when corrupt! */
struct InodeDiskBackend {
    uint8_t* backing = nullptr;
    size_t backing_bytes = 0;
    Storage::Device dev{};
    Storage::Partition part{};

    void bind(FuzzBsp2RamImage& img) {
        backing = img.bytes.data();
        backing_bytes = img.bytes.size();
        static const BlockOperations ops = {
            [](BlockDevice* d, uint64_t lba, uint32_t cnt, void* buf) -> BlockError {
                auto* self = static_cast<InodeDiskBackend*>(d->driver_data);
                uint64_t off = lba * 512ULL;
                uint64_t bytes = uint64_t(cnt) * 512ULL;
                if (off + bytes > self->backing_bytes) return BlockError::OutOfRange;
                std::memcpy(buf, self->backing + off, bytes);
                return BlockError::Ok;
            },
            [](BlockDevice* d, uint64_t lba, uint32_t cnt, const void* buf) -> BlockError {
                auto* self = static_cast<InodeDiskBackend*>(d->driver_data);
                uint64_t off = lba * 512ULL;
                uint64_t bytes = uint64_t(cnt) * 512ULL;
                if (off + bytes > self->backing_bytes) return BlockError::OutOfRange;
                std::memcpy(self->backing + off, buf, bytes);
                return BlockError::Ok;
            },
            [](BlockDevice*) -> BlockError { return BlockError::Ok; },
            [](BlockDevice*) -> BlockError { return BlockError::Ok; }
        };
        dev = {};
        dev.driver_data = this;
        dev.ops = &ops;
        dev.total_sectors = backing_bytes / 512ULL;
        dev.logical_sector_size = 512;
        dev.physical_sector_size = 512;
        part = {&dev, 0, dev.total_sectors};
    }
};

static InodeDiskBackend g_backend;

static void prepare_base_image(FuzzBsp2RamImage& img) {
    img.format_valid();
    (void)utworz_plik("/plik.bin");
    const char payload[64] = "BursztynOS BSP2 Inode Fuzz Seed Payload";
    (void)zapisz_do_pliku("/plik.bin", payload, sizeof(payload));
}

static void run_inode_case(const uint8_t* data, size_t size) {
    prepare_base_image(g_img);
    wezel_indeksowy* w = g_img.inode(2);
    if (!w) return;

    if (size >= 32) {
        w->typ = data[0] % 4;
        std::memcpy(&w->rozmiar_w_bajtach, data + 1, sizeof(uint64_t));
        std::memcpy(&w->czas_utworzenia, data + 9, sizeof(uint64_t));
        std::memcpy(&w->flagi_zabezpieczen, data + 17, sizeof(uint64_t));
        size_t pos = 25;
        for (size_t i = 0; i < 8 && pos + 8 <= size; ++i, pos += 8) {
            std::memcpy(&w->wskazniki_blokow[i], data + pos, sizeof(uint64_t));
        }
        if (pos + 8 <= size) {
            std::memcpy(&w->blok_posredni_1, data + pos, sizeof(uint64_t));
            pos += 8;
        }
        if (pos + 8 <= size) {
            std::memcpy(&w->blok_posredni_2, data + pos, sizeof(uint64_t));
        }
    }

    g_backend.bind(g_img);
    psf_ustaw_partycje(&g_backend.part);
    std::vector<uint8_t> scratch(g_img.bytes.size(), 0);
    inicjalizuj_psf(scratch.data(), static_cast<uint32_t>(scratch.size()));
    if (psf_czy_gotowy()) {
        char buf[128]{};
        (void)czytaj_z_pliku("/plik.bin", buf, sizeof(buf));
        psf_metadane meta{};
        (void)pobierz_metadane_tworu("/plik.bin", &meta);
        (void)usun_twor("/plik.bin");
    }
    psf_ustaw_partycje(nullptr);
}

static void run_deterministic_bug3_regression() {
    /* Regresja Bug #3a: Dwa wskazniki bezposrednie wskazujace na ten sam blok danych
       musza zostac odrzucone przez waliduj_metadane() / wskazniki_pliku_poprawne()! */
    FuzzBsp2RamImage img(192);
    prepare_base_image(img);
    wezel_indeksowy* w = img.inode(2);
    assert(w && w->typ == TYP_PLIK);
    w->rozmiar_w_bajtach = 2 * PSF_ROZMIAR_BLOKU;
    w->wskazniki_blokow[0] = 1;
    w->wskazniki_blokow[1] = 1; // duplikat tego samego bloku danych!
    assert(!zapisz_do_pliku("/plik.bin", "test", 4) && "In-RAM duplicate direct block pointer must be rejected!");

    g_backend.bind(img);
    psf_ustaw_partycje(&g_backend.part);
    std::vector<uint8_t> scratch(img.bytes.size(), 0);
    inicjalizuj_psf(scratch.data(), static_cast<uint32_t>(scratch.size()));
    assert(rozmiar_pliku("/plik.bin") == 0 && "Bug #3a regression: corrupt on-disk image must not be mounted!");

    /* Regresja Bug #3b: blok_posredni_1 kolidujacy ze wskaznikiem bezposrednim */
    prepare_base_image(img);
    w = img.inode(2);
    assert(w && w->typ == TYP_PLIK);
    w->blok_posredni_1 = w->wskazniki_blokow[0];
    assert(!zapisz_do_pliku("/plik.bin", "test", 4) && "In-RAM blok_posredni_1 aliasing direct block must be rejected!");

    g_backend.bind(img);
    psf_ustaw_partycje(&g_backend.part);
    inicjalizuj_psf(scratch.data(), static_cast<uint32_t>(scratch.size()));
    assert(rozmiar_pliku("/plik.bin") == 0 && "Bug #3b regression: corrupt on-disk image must not be mounted!");

    psf_ustaw_partycje(nullptr);
}

int main(int argc, char** argv) {
    run_deterministic_bug3_regression();
    FuzzEngine eng("fuzz_bsp2_inode", 0xB5920002ULL, 4096);
    eng.parse_args(argc, argv);

    std::vector<uint8_t> seed(128, 0);
    seed[0] = TYP_PLIK;
    eng.add_seed(seed);
    seed[0] = TYP_KATALOG;
    eng.add_seed(seed);

    return eng.run(run_inode_case);
}
