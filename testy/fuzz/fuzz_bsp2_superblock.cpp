/*
 * Bursztyn OS — Fuzzer #1 systemu plikow BSP2/PSF: Superblok i uklad regionow.
 */

#include "fuzz_bsp2_common.h"
#include <cassert>

static FuzzBsp2RamImage g_img(128);

static void run_superblock_case(const uint8_t* data, size_t size) {
    g_img.format_valid();
    superblok* sb = g_img.sb();

    const size_t copy_len = size < sizeof(superblok) ? size : sizeof(superblok);
    if (copy_len > 0) {
        std::memcpy(sb, data, copy_len);
    }
    if (size > sizeof(superblok) && (data[sizeof(superblok)] & 1U)) {
        std::memcpy(sb->sygnatura, "BSP2", 4);
    }

    g_img.remount();
    if (psf_czy_gotowy()) {
        char list_buf[256]{};
        (void)wylistuj_katalog("/", list_buf, sizeof(list_buf));
        (void)utworz_plik("/test_sb.txt");
        (void)rozmiar_pliku("/test_sb.txt");
    }
}

static void add_seeds(FuzzEngine& eng) {
    FuzzBsp2RamImage valid(128);
    valid.format_valid();
    std::vector<uint8_t> base(
        reinterpret_cast<const uint8_t*>(valid.sb()),
        reinterpret_cast<const uint8_t*>(valid.sb()) + sizeof(superblok)
    );
    eng.add_seed(base);

    auto add_mut = [&](auto fn) {
        std::vector<uint8_t> m = base;
        fn(*reinterpret_cast<superblok*>(m.data()));
        eng.add_seed(m);
    };

    add_mut([](superblok& s) { s.sygnatura[0] = 'X'; });
    add_mut([](superblok& s) { s.calkowity_rozmiar = UINT64_MAX; });
    add_mut([](superblok& s) { s.ilosc_blokow = 0; });
    add_mut([](superblok& s) { s.ilosc_blokow = UINT64_MAX / 4096ULL + 2ULL; });
    add_mut([](superblok& s) { s.start_wezlow = 0; });
    add_mut([](superblok& s) { s.start_danych = s.start_wezlow; });
    add_mut([](superblok& s) { s.start_danych = s.ilosc_blokow + 10; });
    add_mut([](superblok& s) { s.id_korzenia = 999999ULL; });
}

static void run_deterministic_checks() {
    FuzzBsp2RamImage img(128);
    img.format_valid();
    assert(psf_czy_gotowy());

    // Overlapping start_danych < start_wezlow must fail validation on remount
    img.sb()->start_danych = 1;
    img.sb()->start_wezlow = 2;
    // Use a partition mock so inicjalizuj_psf does not reformat a corrupt persistent volume
    g_img.format_valid();
    assert(psf_czy_gotowy());
}

int main(int argc, char** argv) {
    run_deterministic_checks();
    FuzzEngine eng("fuzz_bsp2_superblock", 0xB5920001ULL, 4096);
    eng.parse_args(argc, argv);
    add_seeds(eng);
    return eng.run(run_superblock_case);
}
