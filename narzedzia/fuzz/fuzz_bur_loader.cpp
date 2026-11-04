/*
 * ============================================================================
 *  BURSZTYN OS — FUZZER LOADERA PLIKOW WYKONYWALNYCH .BUR (fuzz_bur_loader.cpp)
 * ============================================================================
 *
 *  Fuzzing sterowany pokryciem (ASan + UBSan + -fsanitize-coverage=trace-pc)
 *  bezposrednio na kodzie produkcyjnym `loader.cpp` (`BURSZTYN_LOADER_HOST_TEST`):
 *    1. `bur_blad_naglowka` oraz `bur_waliduj_naglowek`
 *    2. `pakiet_parsuj_manifest_tekst`
 *    3. Symulacja mapowania W^X i pelnego zwalniania stron przy bledzie PMM/VMM.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "fuzz_engine.h"
#include "../../loader.h"
#include "../../pamiec.h"
#include "../../pzb.h"
#include "../../stack_protector.h"

namespace {

inline constexpr uint64_t GUARD_STOSU_USER = 0x3FFFF000ULL;
inline constexpr uint64_t BAZA_STOSU_USER = 0x40000000ULL;
inline constexpr uint64_t DOMYSLNY_ROZMIAR_STOSU_USER = 0x4000ULL;

struct SymulowanyVmm {
    size_t zaalokowane_strony = 0;
    size_t limit_stron_przed_bledem = SIZE_MAX;

    bool mapuj_segment(uint64_t va, uint64_t rozmiar, bool zapis, bool wykonywalny) {
        assert(!(zapis && wykonywalny));
        if ((va & 0xFFFULL) != 0 || (rozmiar & 0xFFFULL) != 0) return false;
        const uint64_t liczba_stron = rozmiar / 4096ULL;
        if (liczba_stron > 64ULL) {
            if (zaalokowane_strony + 1 > limit_stron_przed_bledem) return false;
            zaalokowane_strony += 1;
            return true;
        }
        for (uint64_t i = 0; i < liczba_stron; ++i) {
            if (zaalokowane_strony + 1 > limit_stron_przed_bledem) {
                return false;
            }
            ++zaalokowane_strony;
        }
        return true;
    }

    void posprzataj_przestrzen() {
        zaalokowane_strony = 0;
    }
};

static void fuzz_bur_loader_target(const uint8_t* data, size_t size) {
    if (size >= sizeof(NaglowekBur)) {
        NaglowekBur nag{};
        memcpy(&nag, data, sizeof(NaglowekBur));

        uint64_t rozmiar_pliku = size;
        if (size >= sizeof(NaglowekBur) + 8) {
            memcpy(&rozmiar_pliku, data + sizeof(NaglowekBur), 8);
        }

        const char* blad = bur_blad_naglowka(nag, rozmiar_pliku);
        const bool ok = bur_waliduj_naglowek(nag, rozmiar_pliku);
        assert(ok == (blad == nullptr));

        if (ok) {
            assert(nag.magia[0] == 'B' && nag.magia[1] == 'U' &&
                   nag.magia[2] == 'R' && nag.magia[3] == '\0');
            assert(nag.tekst_rozmiar > 0 && nag.tekst_rozmiar <= BUR_MAKS_TEKST_ROZMIAR);
            assert((nag.tekst_przesuniecie & 0xFFFULL) == 0);
            assert((nag.tekst_rozmiar & 0xFFFULL) == 0);
            assert((nag.tekst_wirtualny & 0xFFFULL) == 0);
            assert(nag.punkt_wejscia >= nag.tekst_wirtualny);
            assert(nag.punkt_wejscia < nag.tekst_wirtualny + nag.tekst_rozmiar);

            for (size_t fail_at : {size_t(0), size_t(2), SIZE_MAX}) {
                SymulowanyVmm vmm{};
                vmm.limit_stron_przed_bledem = fail_at;
                bool map_ok = vmm.mapuj_segment(
                    nag.tekst_wirtualny, nag.tekst_rozmiar, false, true);
                if (map_ok && nag.dane_rozmiar > 0) {
                    map_ok = vmm.mapuj_segment(
                        nag.dane_wirtualny, nag.dane_rozmiar, true, false);
                }
                if (map_ok) {
                    map_ok = vmm.mapuj_segment(
                        BAZA_STOSU_USER, DOMYSLNY_ROZMIAR_STOSU_USER, true, false);
                }
                if (!map_ok) {
                    vmm.posprzataj_przestrzen();
                    assert(vmm.zaalokowane_strony == 0);
                }
            }
        }
    }

    if (size > 0) {
        PakietManifest manifest{};
        const size_t manifest_len = (size > 4096) ? 4096 : size;
        const bool m_ok = pakiet_parsuj_manifest_tekst(
            reinterpret_cast<const char*>(data), manifest_len, &manifest);
        assert(memchr(manifest.nazwa, '\0', sizeof(manifest.nazwa)) != nullptr);
        assert(memchr(manifest.autor, '\0', sizeof(manifest.autor)) != nullptr);
        assert(memchr(manifest.wersja, '\0', sizeof(manifest.wersja)) != nullptr);
        assert(memchr(manifest.plik_startowy, '\0', sizeof(manifest.plik_startowy)) != nullptr);
        assert(pzb_poziom_poprawny(manifest.poziom_zaufania));
        assert(pzb_maska_praw_poprawna(manifest.uprawnienia));
        if (m_ok) {
            assert(manifest.plik_startowy[0] != '\0');
        }
    }
}

static std::vector<uint8_t> make_bur_seed(const NaglowekBur& n, uint64_t fsz, const char* manifest_txt = nullptr) {
    std::vector<uint8_t> out(sizeof(NaglowekBur) + 8);
    memcpy(out.data(), &n, sizeof(NaglowekBur));
    memcpy(out.data() + sizeof(NaglowekBur), &fsz, 8);
    if (manifest_txt) {
        const size_t l = strlen(manifest_txt);
        out.insert(out.end(), manifest_txt, manifest_txt + l);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    FuzzEngine eng("fuzz_bur_loader", 0xB0520002ULL, 1024);
    eng.parse_args(argc, argv);

    NaglowekBur valid = {
        {'B', 'U', 'R', '\0'},
        0x1001000ULL,
        0x1000ULL, 0x8000ULL, 0x1001000ULL,
        0x9000ULL, 0x20000ULL, 0x1009000ULL
    };
    NaglowekBur overlap = valid;
    overlap.dane_wirtualny = 0x1005000ULL;

    NaglowekBur stack_hit = valid;
    stack_hit.dane_wirtualny = GUARD_STOSU_USER;

    const char* sample_manifest =
        "nazwa = \"TestApp\"\n"
        "autor = \"Bursztyn\"\n"
        "wersja = \"1.0\"\n"
        "plik_startowy = \"app.bur\"\n"
        "poziom_zaufania = 3\n"
        "uprawnienia = [\"okna\", \"pliki_czytaj\", \"siec\"]\n";

    eng.add_seed(make_bur_seed(valid, 0x10000ULL, sample_manifest));
    eng.add_seed(make_bur_seed(overlap, 0x20000ULL));
    eng.add_seed(make_bur_seed(stack_hit, 0x20000ULL));

    NaglowekBur ovf = {
        {'B', 'U', 'R', '\0'},
        0x1001000ULL,
        0xFFFFFFFFFFFFF000ULL, 0x2000ULL, 0x1001000ULL,
        0x9000ULL, 0x20000ULL, 0x1009000ULL
    };
    eng.add_regression(make_bur_seed(ovf, 0xFFFFFFFFFFFFFFFFULL));

    // Regresja Bug #5: wersja >= 32 znakow w opis.aplikacji nie posiadala terminatora NUL w manifest.wersja[32]
    const char* bug5_manifest =
        "nazwa = \"Test\"\n"
        "wersja = \"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"\n"
        "plik_startowy = \"app.bur\"\n";
    eng.add_regression(make_bur_seed(valid, 0x10000ULL, bug5_manifest));

    return eng.run(fuzz_bur_loader_target);
}
