/*
 * BRB - ograniczony, powtarzalny fuzzer parsera ELF64 (Faza 3). Bez zaleznosci (libFuzzer/AFL niedostepne):
 * generator xorshift64* ze stalym ziarnem, stala liczba iteracji, ASan + UBSan.
 *
 * Uzycie: brb_fuzz_elf [iteracje=200000] [ziarno=0x42524233] [plik_jadra]
 * Strategie mutacji: losowe bajty w naglowkach, pola 64-bitowe PH ustawiane na wartosci graniczne,
 * pola 16-bitowe naglowka ELF, obcinanie pliku, zamiana typow segmentow. Kazdy zaakceptowany obraz musi
 * spelniac niezalezne niezmienniki (brb_elf_fixture.h). Wynik jest deterministyczny dla danego ziarna.
 */
#include "testy/brb_elf_fixture.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Losowe {
    uint64_t s;
    uint64_t nast() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    uint64_t do_(uint64_t n) { return n ? nast() % n : 0; }
};

const uint64_t GRANICZNE[] = {0, 1, 2, 7, 8, 0xFFF, 0x1000, 0x1001, 0xFFFFF, 0x100000, 0xFFFFFFFFull, 0x100000000ull,
                              0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, ~0ull - 0x1000, ~0ull - 1, ~0ull};

std::vector<uint8_t> wczytaj(const char* p) {
    std::vector<uint8_t> d;
    FILE* f = std::fopen(p, "rb");
    if (!f) return d;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    return d;
}

}  // namespace

int main(int argc, char** argv) {
    const uint64_t iteracje = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 200000;
    Losowe los{argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 0x42524233ull};
    if (los.s == 0) los.s = 1;
    std::vector<std::vector<uint8_t>> korpus;
    korpus.push_back(fixture::jadro_minimalne().b);
    if (argc > 3) {
        std::vector<uint8_t> j = wczytaj(argv[3]);
        if (!j.empty()) korpus.push_back(std::move(j));
    }
    const brb::PolitykaElf* polityki[] = {&brb::POLITYKA_JADRA_BURSZTYN, &brb::POLITYKA_OGOLNA};
    uint64_t przyjete = 0, naruszenia = 0;
    uint64_t kody[64] = {};
    brb::PlanLadowania plan;
    for (uint64_t it = 0; it < iteracje; ++it) {
        std::vector<uint8_t>& wzor = korpus[los.do_(korpus.size())];
        // Mutujemy tylko obszar naglowkow (pierwsze 0x1100 B) w miejscu i przywracamy po przebiegu,
        // aby nie kopiowac wielomegabajtowego jadra w kazdej iteracji.
        const uint64_t obszar = wzor.size() < 0x1100 ? wzor.size() : 0x1100;
        std::vector<uint8_t> zapas(wzor.begin(), wzor.begin() + long(obszar));
        const unsigned liczba_zmian = 1 + unsigned(los.do_(4));
        uint64_t dlugosc = wzor.size();
        for (unsigned z = 0; z < liczba_zmian; ++z) {
            switch (los.do_(5)) {
                case 0: wzor[los.do_(obszar)] = uint8_t(los.nast()); break;
                case 1: {   // pole 64-bitowe naglowka programu
                    const unsigned i = unsigned(los.do_(6));
                    const uint64_t pole = fixture::ph(i) + 8 * (1 + los.do_(6));
                    if (pole + 8 <= obszar) {
                        const uint64_t v = GRANICZNE[los.do_(sizeof(GRANICZNE) / 8)] + (los.do_(3) ? 0 : los.do_(0x2000));
                        for (int k = 0; k < 8; ++k) wzor[pole + k] = uint8_t(v >> (8 * k));
                    }
                    break;
                }
                case 2: {   // pole 16-bitowe naglowka ELF (typ, maszyna, rozmiary, liczba PH)
                    static const uint64_t pola[] = {16, 18, 52, 54, 56};
                    const uint64_t pole = pola[los.do_(5)];
                    const uint16_t v = uint16_t(los.do_(3) ? los.nast() : GRANICZNE[los.do_(sizeof(GRANICZNE) / 8)]);
                    wzor[pole] = uint8_t(v);
                    wzor[pole + 1] = uint8_t(v >> 8);
                    break;
                }
                case 3: dlugosc = los.do_(wzor.size() + 1); break;   // obciecie
                default: {   // typ segmentu
                    static const uint32_t typy[] = {0, 1, 2, 3, 4, 6, 7, 0x6474E551u};
                    const uint64_t pole = fixture::ph(unsigned(los.do_(4)));
                    if (pole + 4 <= obszar) {
                        const uint32_t t = typy[los.do_(8)];
                        for (int k = 0; k < 4; ++k) wzor[pole + k] = uint8_t(t >> (8 * k));
                    }
                }
            }
        }
        const brb::PolitykaElf& pol = *polityki[los.do_(2)];
        // Obciety obraz: osobny bufor o dokladnej dlugosci (ASan pilnuje konca).
        brb::WynikElf w;
        if (dlugosc < wzor.size()) {
            std::vector<uint8_t> krotki(wzor.begin(), wzor.begin() + long(dlugosc));
            w = brb::zbuduj_plan_ladowania(krotki.empty() ? nullptr : krotki.data(), krotki.size(), pol, &plan);
            if (w.blad == brb::BladElf::Brak && !fixture::niezmienniki(plan, krotki.size(), pol).empty()) ++naruszenia;
        } else {
            w = brb::zbuduj_plan_ladowania(wzor.data(), wzor.size(), pol, &plan);
            if (w.blad == brb::BladElf::Brak) {
                const std::string n = fixture::niezmienniki(plan, wzor.size(), pol);
                if (!n.empty()) { ++naruszenia; std::printf("NARUSZENIE it=%llu: %s\n", (unsigned long long)it, n.c_str()); }
            }
        }
        if (w.blad == brb::BladElf::Brak) ++przyjete;
        ++kody[uint8_t(w.blad) & 63];
        std::copy(zapas.begin(), zapas.end(), wzor.begin());
    }
    unsigned roznych = 0;
    for (uint64_t k : kody) roznych += k != 0;
    std::printf("[FUZZ-ELF] iteracje=%llu przyjete=%llu naruszenia=%llu roznych_kodow=%u korpus=%zu\n",
                (unsigned long long)iteracje, (unsigned long long)przyjete, (unsigned long long)naruszenia, roznych, korpus.size());
    std::printf("[FUZZ-ELF] rozklad kodow:");
    for (int k = 0; k < 64; ++k) if (kody[k]) std::printf(" %d=%llu", k, (unsigned long long)kody[k]);
    std::printf("\n");
    return naruszenia == 0 ? 0 : 1;
}
