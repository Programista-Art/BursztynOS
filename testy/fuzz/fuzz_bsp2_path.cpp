/*
 * Bursztyn OS — Fuzzer #4 systemu plikow BSP2/PSF: Parser sciezek, translacja slotow A/B i kodowanie UTF-8.
 * Zawiera deterministyczne testy regresyjne dla Bug #2 (psf_przetlumacz_sciezke_slotu:
 * ciche obcinanie sciezek >= 160 B i brak wywolania bezpieczna_sciezka przed translacja).
 */

#include "fuzz_bsp2_common.h"
#include <cassert>
#include <string>

static FuzzBsp2RamImage g_img(192);

static void run_path_case(const uint8_t* data, size_t size) {
    // 1. Test raw buffer (potentially non-NUL-terminated up to 600 bytes)
    char raw_buf[600];
    std::memset(raw_buf, 'A', sizeof(raw_buf));
    const size_t copy_len = size < sizeof(raw_buf) ? size : sizeof(raw_buf);
    if (copy_len > 0) {
        std::memcpy(raw_buf, data, copy_len);
    }
    if (copy_len < sizeof(raw_buf) && (size == 0 || (data[0] & 1U))) {
        raw_buf[copy_len] = '\0';
    } else {
        raw_buf[sizeof(raw_buf) - 1] = '\0';
    }

    psf_wlacz_translacje_slotow((size & 1U) != 0);
    psf_ustaw_aktywny_slot((size & 2U) ? 1 : 0);

    (void)utworz_katalog(raw_buf);
    (void)utworz_plik(raw_buf);
    (void)rozmiar_pliku(raw_buf);
    psf_metadane meta{};
    (void)pobierz_metadane_tworu(raw_buf, &meta);
    char list_buf[128]{};
    (void)wylistuj_katalog(raw_buf, list_buf, sizeof(list_buf));
    (void)usun_twor(raw_buf);

    psf_wlacz_translacje_slotow(false);
}

static void run_deterministic_bug2_regression() {
    g_img.format_valid();
    assert(utworz_katalog("/sloty"));
    assert(utworz_katalog("/sloty/a"));
    assert(utworz_katalog("/sloty/a/programy"));
    assert(utworz_katalog("/sloty/a/programy/eksplorator.cebula"));

    psf_ustaw_aktywny_slot(0);
    psf_wlacz_translacje_slotow(true);

    /* Regresja Bug #2a: Sciezka wirtualna /programy/... o dlugosci ~175 bajtow
       (> 160 B i po translacji "/sloty/a" < MAKS_DLUGOSC_SCIEZKI=256 B) dawniej
       byla cicho obcinana na granicy 159 bajtow przez psf_przetlumacz_sciezke_slotu! */
    constexpr size_t kMaksDlugoscSciezki = 512;
    const char* d1 = "/programy/eksplorator.cebula/podkatalog_dlugi_nazwa_testowa_1234567890";
    const char* d2 = "/programy/eksplorator.cebula/podkatalog_dlugi_nazwa_testowa_1234567890/kolejny_podkatalog_dlugi_nazwa_testowa_12345";
    const char* f3 = "/programy/eksplorator.cebula/podkatalog_dlugi_nazwa_testowa_1234567890/kolejny_podkatalog_dlugi_nazwa_testowa_12345/plik_o_dlugiej_nazwie_ponad_160_bajtow_w_sciezce.txt";
    assert(std::strlen(f3) > 165 && std::strlen(f3) + 8 < kMaksDlugoscSciezki);

    assert(utworz_katalog(d1));
    assert(utworz_katalog(d2));
    assert(utworz_plik(f3));
    assert(zapisz_do_pliku(f3, "OK180", 5));
    assert(rozmiar_pliku(f3) == 5 && "Bug #2a regression: >160B slot-translated path must not be truncated!");

    /* Regresja Bug #2b: Sciezka przekraczajaca MAKS_DLUGOSC_SCIEZKI (512 B) po
       dopisaniu prefiksu "/sloty/a" (8 B) musi zamknac sie bezpiecznie (fail-closed),
       a nie zostac obcieta do istniejacego pliku! */
    std::string long_path = "/programy/eksplorator.cebula";
    while (long_path.size() < 505) {
        long_path += "/segment_123456789012345";
    }
    if (long_path.size() >= kMaksDlugoscSciezki) {
        long_path.resize(kMaksDlugoscSciezki - 3);
    }
    assert(long_path.size() >= 505 && long_path.size() < kMaksDlugoscSciezki);
    // Po dodaniu "/sloty/a" (8 B) przekracza 511 B -> musi zwrocic false / 0!
    assert(!utworz_katalog(long_path.c_str()) && "Bug #2b regression: slot path overflow must fail closed!");

    /* Regresja Bug #2c: Niezakonczony NUL-em bufor >= 512 B przy wlaczonej translacji slotow */
    char non_nul[520];
    std::memset(non_nul, 'a', sizeof(non_nul));
    std::memcpy(non_nul, "/programy/", 10);
    assert(!utworz_plik(non_nul) && "Bug #2c regression: non-NUL path must be rejected before slot translation!");

    psf_wlacz_translacje_slotow(false);
}

int main(int argc, char** argv) {
    g_img.format_valid();
    run_deterministic_bug2_regression();

    FuzzEngine eng("fuzz_bsp2_path", 0xB5920004ULL, 4096);
    eng.parse_args(argc, argv);

    const char* seeds[] = {
        "/",
        "/programy/test.cebula/test.bur",
        "/../../etc/passwd",
        "/.system/slot_a/../../tajne",
        "/żółć/ąęćłńóśźż.txt",
        "//podwojny///ukosnik//plik",
        "/katalog/./../katalog/plik",
    };
    for (const char* s : seeds) {
        eng.add_seed(std::vector<uint8_t>(s, s + std::strlen(s) + 1));
    }

    return eng.run(run_path_case);
}
