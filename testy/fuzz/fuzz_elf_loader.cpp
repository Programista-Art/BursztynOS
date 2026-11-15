/*
 * Fuzz target loadera ELF64 (punkt 2.14 planu).
 *
 * Kontrakt, ktory fuzz ma udowodnic:
 *
 *   1. elf_waliduj() i elf_opisz() nigdy nie czytaja poza buforem wejscia
 *      (to gwarantuje ASan w tej kompilacji),
 *   2. elf_waliduj() nie ma UB na zadnym wejsciu (UBSan),
 *   3. jezeli elf_opisz() zwraca ELF_OK, to wszystkie niezmienniki
 *      bezpieczenstwa zachodza (W^X, memsz >= filesz, granice pliku,
 *      granice uzytkownika, brak nakladania, wejscie w segmencie RX).
 *
 * Fuzz nie jest "smoke testem z losowymi bajtami": mutuje poprawny obraz
 * ELF, wiec wiekszosc iteracji trafia w interesujace sciezki zamiast
 * od razu w "zla magia".
 */
#include "jadro/fs/elf_loader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

/* Poprawny obraz bazowy: RX .text + RW .data z BSS. */
std::vector<uint8_t> obraz_bazowy() {
    std::vector<uint8_t> d(0x2000, 0);
    auto p16 = [&](size_t o, uint16_t v) {
        d[o] = uint8_t(v & 0xFF); d[o + 1] = uint8_t((v >> 8) & 0xFF);
    };
    auto p32 = [&](size_t o, uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) d[o + i] = uint8_t((v >> (8 * i)) & 0xFF);
    };
    auto p64 = [&](size_t o, uint64_t v) {
        for (unsigned i = 0; i < 8; ++i) d[o + i] = uint8_t((v >> (8 * i)) & 0xFF);
    };
    d[0] = 0x7F; d[1] = 'E'; d[2] = 'L'; d[3] = 'F';
    d[4] = ELFCLASS64; d[5] = ELFDATA2LSB; d[6] = EV_CURRENT;
    p16(16, ET_EXEC);
    p16(18, EM_X86_64);
    p32(20, 1);
    p64(24, 0x400080);
    p64(32, 64);
    p16(52, 64);
    p16(54, 56);
    p16(56, 2);
    p16(58, 64);
    p32(64, PT_LOAD); p32(68, PF_R | PF_X);
    p64(72, 0x1000); p64(80, 0x400000); p64(88, 0x400000);
    p64(96, 0x200); p64(104, 0x200); p64(112, 0x1000);
    p32(120, PT_LOAD); p32(124, PF_R | PF_W);
    p64(128, 0x1200); p64(136, 0x401000); p64(144, 0x401000);
    p64(152, 0x100); p64(160, 0x400); p64(168, 0x1000);
    return d;
}

/* Wszystkie niezmienniki, ktore musza zachodzic po przyjeciu obrazu. */
bool niezmienniki_ok(const ElfObraz& obraz, size_t rozmiar_pliku) {
    if (elf_narusza_wx(obraz)) return false;
    if (!elf_wejscie_w_kodzie(obraz)) return false;
    for (uint32_t i = 0; i < obraz.liczba_segmentow; ++i) {
        const ElfSegment& s = obraz.segmenty[i];
        if (s.rozmiar_pamieci < s.rozmiar_pliku) return false;
        if (s.przesuniecie > rozmiar_pliku) return false;
        if (s.rozmiar_pliku > rozmiar_pliku - s.przesuniecie) return false;
        if (s.wirtualny < 0x10000ULL) return false;
        if (s.rozmiar_pamieci > 0x0000800000000000ULL - s.wirtualny) return false;
    }
    for (uint32_t a = 0; a < obraz.liczba_segmentow; ++a) {
        for (uint32_t b = a + 1; b < obraz.liczba_segmentow; ++b) {
            const ElfSegment& x = obraz.segmenty[a];
            const ElfSegment& y = obraz.segmenty[b];
            if (x.wirtualny < y.wirtualny + y.rozmiar_pamieci &&
                y.wirtualny < x.wirtualny + x.rozmiar_pamieci)
                return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const unsigned iteracje = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 20000;
    const uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20261111ULL;

    std::mt19937_64 rng(seed);
    const std::vector<uint8_t> bazowy = obraz_bazowy();

    unsigned przyjete = 0;
    unsigned odrzucone = 0;
    unsigned bledy = 0;

    for (unsigned it = 0; it < iteracje; ++it) {
        std::vector<uint8_t> obraz = bazowy;

        /* 1-8 mutacji: bajt, slowo albo obciecie. */
        const unsigned mutacji = 1U + static_cast<unsigned>(rng() % 8U);
        for (unsigned m = 0; m < mutacji; ++m) {
            switch (rng() % 4U) {
                case 0: {  /* pojedynczy bajt */
                    if (obraz.empty()) break;
                    const size_t off = static_cast<size_t>(rng() % obraz.size());
                    obraz[off] ^= static_cast<uint8_t>(1U << (rng() % 8U));
                    break;
                }
                case 1: {  /* losowy bajt */
                    const size_t off = static_cast<size_t>(rng() % obraz.size());
                    obraz[off] = static_cast<uint8_t>(rng() & 0xFFU);
                    break;
                }
                case 2: {  /* osmiobajtowa wartosc, tylko gdy bufor na to pozwala */
                    if (obraz.size() < 9) break;
                    const size_t off = static_cast<size_t>(rng() % (obraz.size() - 8));
                    const uint64_t v = rng();
                    std::memcpy(obraz.data() + off, &v, 8);
                    break;
                }
                default: {  /* obciecie */
                    const size_t n = static_cast<size_t>(rng() % obraz.size());
                    obraz.resize(n);
                    if (obraz.empty()) obraz.push_back(0);
                    break;
                }
            }
        }

        const ElfStatus status = elf_waliduj(obraz.data(), obraz.size());
        ElfRelokacja relokacje[ELF_MAKS_RELOKACJI]{};
        uint32_t liczba_relokacji = 0;
        /*
         * Skan sekcji RELA jest osobna granica wejscia dla ET_DYN. Nie
         * wymagamy konkretnego statusu, ale kazdy wynik musi byc bezpieczny
         * i nie moze czytac poza obcietym buforem (ASan).
         */
        (void)elf_zbierz_relokacje(
            obraz.data(),
            obraz.size(),
            relokacje,
            ELF_MAKS_RELOKACJI,
            &liczba_relokacji);
        if (status != ELF_OK) {
            ++odrzucone;
            /* Odrzucony obraz nie moze byc opisany jako poprawny. */
            ElfObraz opis{};
            const ElfStatus opis_status = elf_opisz(obraz.data(), obraz.size(), &opis);
            if (opis_status == ELF_OK) {
                std::printf("BLAD: waliduj odrzucil, a opisz przyjal (iteracja %u)\n", it);
                ++bledy;
            }
            continue;
        }

        ++przyjete;
        ElfObraz opis{};
        const ElfStatus opis_status = elf_opisz(obraz.data(), obraz.size(), &opis);
        if (opis_status != ELF_OK) {
            std::printf("BLAD: waliduj przyjal, a opisz odrzucil (iteracja %u, status %d)\n",
                        it, int(opis_status));
            ++bledy;
            continue;
        }
        if (!niezmienniki_ok(opis, obraz.size())) {
            std::printf("BLAD: przyjety obraz lamie niezmienniki (iteracja %u)\n", it);
            ++bledy;
        }
    }

    std::printf("[FUZZ-ELF-LOADER] iteracji=%u przyjete=%u odrzucone=%u bledy=%u seed=%llu\n",
                iteracje, przyjete, odrzucone, bledy,
                static_cast<unsigned long long>(seed));

    if (bledy != 0) {
        std::printf("[FUZZ-ELF-LOADER] FAIL\n");
        return 1;
    }
    /* Fuzz ma sens tylko wtedy, gdy czesc iteracji faktycznie przechodzi. */
    if (przyjete == 0) {
        std::printf("[FUZZ-ELF-LOADER] FAIL: zadna iteracja nie zostala przyjeta\n");
        return 1;
    }
    std::printf("[FUZZ-ELF-LOADER] PASS\n");
    return 0;
}
