/*
 * Bursztyn OS — Fuzzer #5 systemu plikow BSP2/PSF: Operacje stanowe (create/write/read/rename/move/copy/delete)
 * oraz deterministyczny test regresyjny dla Bug #4 (ewikcja tablicy posredniej blok_posredni_1
 * z 256-slotowego cache LRU podczas zwalniania/nadpisywania duzego pliku na partycji BSP2).
 */

#include "fuzz_bsp2_common.h"
#include <cassert>

static FuzzBsp2RamImage g_img(256);

struct OpsDiskBackend {
    std::vector<uint8_t> disk_bytes;
    Storage::Device dev{};
    Storage::Partition part{};

    explicit OpsDiskBackend(size_t num_blocks)
        : disk_bytes(num_blocks * PSF_ROZMIAR_BLOKU, 0) {
        static const BlockOperations ops = {
            [](BlockDevice* d, uint64_t lba, uint32_t cnt, void* buf) -> BlockError {
                auto* self = static_cast<OpsDiskBackend*>(d->driver_data);
                uint64_t off = lba * 512ULL;
                uint64_t bytes = uint64_t(cnt) * 512ULL;
                if (off + bytes > self->disk_bytes.size()) return BlockError::OutOfRange;
                std::memcpy(buf, self->disk_bytes.data() + off, bytes);
                return BlockError::Ok;
            },
            [](BlockDevice* d, uint64_t lba, uint32_t cnt, const void* buf) -> BlockError {
                auto* self = static_cast<OpsDiskBackend*>(d->driver_data);
                uint64_t off = lba * 512ULL;
                uint64_t bytes = uint64_t(cnt) * 512ULL;
                if (off + bytes > self->disk_bytes.size()) return BlockError::OutOfRange;
                std::memcpy(self->disk_bytes.data() + off, buf, bytes);
                return BlockError::Ok;
            },
            [](BlockDevice*) -> BlockError { return BlockError::Ok; },
            [](BlockDevice*) -> BlockError { return BlockError::Ok; }
        };
        dev.driver_data = this;
        dev.ops = &ops;
        dev.total_sectors = disk_bytes.size() / 512ULL;
        dev.logical_sector_size = 512;
        dev.physical_sector_size = 512;
        part = {&dev, 0, dev.total_sectors};
    }
};

static const char* kPaths[8] = {
    "/a",
    "/b",
    "/dir1",
    "/dir2",
    "/dir1/f1.txt",
    "/dir1/f2.txt",
    "/dir2/f1.txt",
    "/dir2/sub",
};

static void run_ops_case(const uint8_t* data, size_t size) {
    g_img.format_valid();
    (void)utworz_katalog("/dir1");
    (void)utworz_katalog("/dir2");

    size_t pos = 0;
    uint32_t steps = 0;
    while (pos + 4 <= size && steps < 24) {
        const uint8_t op = data[pos] % 10;
        const char* p1 = kPaths[data[pos + 1] % 8];
        const char* p2 = kPaths[data[pos + 2] % 8];
        const uint8_t param = data[pos + 3];
        pos += 4;
        ++steps;

        switch (op) {
            case 0:
                (void)utworz_plik_z_pzb(p1, param % 6);
                break;
            case 1:
                (void)utworz_katalog_z_pzb(p1, param % 6);
                break;
            case 2: {
                char payload[128];
                std::memset(payload, static_cast<int>(param), sizeof(payload));
                (void)zapisz_do_pliku(p1, payload, (param % sizeof(payload)) + 1);
                break;
            }
            case 3: {
                char payload[64];
                std::memset(payload, static_cast<int>(param), sizeof(payload));
                (void)zapisz_fragment_pliku(p1, (param % 8) * 16ULL, payload, sizeof(payload));
                break;
            }
            case 4: {
                char buf[128]{};
                uint32_t got = 0;
                (void)czytaj_fragment_pliku(p1, param, buf, sizeof(buf), &got);
                break;
            }
            case 5:
                (void)zmien_nazwe_tworu(p1, (param & 1) ? "nowa_a" : "nowa_b");
                break;
            case 6:
                (void)przenies_twor(p1, p2);
                break;
            case 7:
                (void)kopiuj_twor_z_pzb(p1, p2, param % 6);
                break;
            case 8:
                (void)usun_twor(p1);
                break;
            case 9: {
                psf_metadane m{};
                (void)pobierz_metadane_tworu(p1, &m);
                char lbuf[128]{};
                (void)wylistuj_katalog(p1, lbuf, sizeof(lbuf));
                break;
            }
        }
        assert(!psf_test_lock_held() && "PSF lock leaked after operation!");
    }
}

static void run_deterministic_bug4_regression() {
    /*
     * Regresja Bug #4:
     * Gdy system plikow BSP2 dziala na partycji dyskowej (uzywajac 256-slotowego cache
     * blokow danych w psf.cpp), plik o rozmiarze > 490 blokow (np. 520 blokow = 2 129 920 B)
     * uzywa tablicy posredniej blok_posredni_1 (30 wpisow posrednich + 490 bezposrednich).
     * Dawniej zwolnij_wszystkie_bloki_wezla() trzymalo surowy wskaznik uint64_t* t1 do slotu
     * cache zawierajacego blok_posredni_1 podczas zerowania >256 blokow danych w petli, co
     * powodowalo ewikcje slotu t1 z cache LRU, nadpisanie t1 zerami i zwolnienie bloku 0
     * (katalogu glownego "/")!
     */
    OpsDiskBackend disk(2048); // 8 MiB dysk (2048 blokow 4 KiB)
    std::vector<uint8_t> ram(16 * 1024 * 1024, 0);
    psf_ustaw_partycje(nullptr);
    inicjalizuj_psf(ram.data(), static_cast<uint32_t>(ram.size()));
    assert(psf_formatuj_instalacje(disk.part));

    psf_ustaw_partycje(&disk.part);
    inicjalizuj_psf(ram.data(), static_cast<uint32_t>(ram.size()));
    assert(psf_czy_gotowy());

    assert(utworz_katalog("/katalog_chroniony"));
    assert(utworz_plik("/katalog_chroniony/kotwica.txt"));
    assert(zapisz_do_pliku("/katalog_chroniony/kotwica.txt", "KOTWICA_OK", 10));

    // Utworz duzy plik uzywajacy blok_posredni_1 (520 blokow * 4096 B = 2 129 920 B > 490 * 4096 B)
    assert(utworz_plik("/duzy_plik.bin"));
    std::vector<char> big_payload(520 * PSF_ROZMIAR_BLOKU, 0x5A);
    assert(zapisz_do_pliku("/duzy_plik.bin", big_payload.data(),
                           static_cast<uint32_t>(big_payload.size())));
    assert(rozmiar_pliku("/duzy_plik.bin") == big_payload.size());

    // Usun duzy plik (wywoluje zwolnij_wszystkie_bloki_wezla dla >256 blokow + blok_posredni_1)
    assert(usun_twor("/duzy_plik.bin"));
    assert(psf_weryfikuj_dysk() && "Bug #4 regression: deleting >490-block file corrupted BSP2 disk image!");

    // Sprawdz ze katalog glowny oraz /katalog_chroniony/kotwica.txt sa nienaruszone
    char anchor_buf[16]{};
    assert(czytaj_z_pliku("/katalog_chroniony/kotwica.txt", anchor_buf, 10));
    assert(std::memcmp(anchor_buf, "KOTWICA_OK", 10) == 0);

    psf_ustaw_partycje(nullptr);
}

int main(int argc, char** argv) {
    run_deterministic_bug4_regression();

    FuzzEngine eng("fuzz_bsp2_ops", 0xB5920005ULL, 4096);
    eng.parse_args(argc, argv);

    std::vector<uint8_t> seed = {
        0, 4, 0, 1,
        2, 4, 0, 32,
        7, 4, 3, 2,
        6, 6, 2, 0,
        8, 4, 0, 0,
    };
    eng.add_seed(seed);

    return eng.run(run_ops_case);
}
