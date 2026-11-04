/*
 * Bursztyn OS — Fuzzer #3 systemu plikow BSP2/PSF: Wpisy katalogowe i walidacja katalogow.
 * Zawiera deterministyczny test regresyjny dla Bug #3 (wpis katalogowy
 * wskazujacy na id_korzenia tworzacy cykl w drzewie katalogow).
 */

#include "fuzz_bsp2_common.h"
#include <cassert>

static FuzzBsp2RamImage g_img(192);

struct DirDiskBackend {
    uint8_t* backing = nullptr;
    size_t backing_bytes = 0;
    Storage::Device dev{};
    Storage::Partition part{};

    void bind(FuzzBsp2RamImage& img) {
        backing = img.bytes.data();
        backing_bytes = img.bytes.size();
        static const BlockOperations ops = {
            [](BlockDevice* d, uint64_t lba, uint32_t cnt, void* buf) -> BlockError {
                auto* self = static_cast<DirDiskBackend*>(d->driver_data);
                uint64_t off = lba * 512ULL;
                uint64_t bytes = uint64_t(cnt) * 512ULL;
                if (off + bytes > self->backing_bytes) return BlockError::OutOfRange;
                std::memcpy(buf, self->backing + off, bytes);
                return BlockError::Ok;
            },
            [](BlockDevice* d, uint64_t lba, uint32_t cnt, const void* buf) -> BlockError {
                auto* self = static_cast<DirDiskBackend*>(d->driver_data);
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

static DirDiskBackend g_dir_backend;

static void prepare_dir_image(FuzzBsp2RamImage& img) {
    img.format_valid();
    (void)utworz_katalog("/katalog");
    (void)utworz_plik("/katalog/plik_a.txt");
    (void)utworz_plik("/katalog/plik_b.txt");
}

static void run_dir_case(const uint8_t* data, size_t size) {
    prepare_dir_image(g_img);
    wezel_indeksowy* root = g_img.inode(g_img.sb()->id_korzenia);
    if (!root || root->wskazniki_blokow[0] == BARK_BLOKU) return;

    uint8_t* dir_blk = g_img.data_block(root->wskazniki_blokow[0]);
    if (!dir_blk) return;

    const size_t copy_len = size < 256 ? size : 256;
    if (copy_len > 0) {
        std::memcpy(dir_blk, data, copy_len);
    }

    g_dir_backend.bind(g_img);
    psf_ustaw_partycje(&g_dir_backend.part);
    std::vector<uint8_t> scratch(g_img.bytes.size(), 0);
    inicjalizuj_psf(scratch.data(), static_cast<uint32_t>(scratch.size()));
    if (psf_czy_gotowy()) {
        char list_buf[512]{};
        (void)wylistuj_katalog("/", list_buf, sizeof(list_buf));
        (void)wylistuj_katalog("/katalog", list_buf, sizeof(list_buf));
    }
    psf_ustaw_partycje(nullptr);
}

static void run_deterministic_root_cycle_regression() {
    /* Regresja Bug #3c: Wpis katalogowy wskazujacy na id_korzenia (1) lub na samego
       siebie (2) wewnatrz podkatalogu musi zostac odrzucony przez waliduj_katalog! */
    FuzzBsp2RamImage img(192);
    prepare_dir_image(img);
    wezel_indeksowy* subdir = img.inode(2);
    assert(subdir && subdir->typ == TYP_KATALOG);
    uint8_t* blk = img.data_block(subdir->wskazniki_blokow[0]);
    assert(blk);
    auto* entries = reinterpret_cast<wpis_katalogowy*>(blk);
    // Przekieruj wpis w podkatalogu na id_wezla = 2 (sam podkatalog -> cykl!)
    entries[0].id_wezla = 2;
    std::strcpy(entries[0].nazwa, "cykl_do_siebie");

    g_dir_backend.bind(img);
    psf_ustaw_partycje(&g_dir_backend.part);
    std::vector<uint8_t> scratch(img.bytes.size(), 0);
    inicjalizuj_psf(scratch.data(), static_cast<uint32_t>(scratch.size()));
    assert(!czy_katalog_istnieje("/katalog") && "Self-referencing directory entry must be rejected on mount!");
    psf_ustaw_partycje(nullptr);
}

int main(int argc, char** argv) {
    run_deterministic_root_cycle_regression();
    FuzzEngine eng("fuzz_bsp2_dir", 0xB5920003ULL, 4096);
    eng.parse_args(argc, argv);

    wpis_katalogowy e{};
    e.id_wezla = 1;
    std::strcpy(e.nazwa, "poprawny");
    eng.add_seed(std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(&e),
        reinterpret_cast<const uint8_t*>(&e) + sizeof(e)
    ));

    // Non-NUL terminated name seed
    std::memset(e.nazwa, 'A', PSF_MAX_NAZWA);
    eng.add_seed(std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(&e),
        reinterpret_cast<const uint8_t*>(&e) + sizeof(e)
    ));

    return eng.run(run_dir_case);
}
