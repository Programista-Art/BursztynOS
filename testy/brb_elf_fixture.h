/*
 * BRB - wspolne narzedzia testow ELF (testy hosta, mutacje, fuzzer): generator syntetycznego jadra
 * zgodnego z POLITYKA_JADRA_BURSZTYN oraz niezalezna kontrola niezmiennikow planu ladowania.
 */
#ifndef BRB_TEST_ELF_FIXTURE_H
#define BRB_TEST_ELF_FIXTURE_H

#include "rozruch/brb/wspolne/elf64.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fixture {

__extension__ typedef unsigned __int128 U128;   // tylko w testach hosta (kontrola przepelnien)

constexpr uint64_t PH0 = 64;   // poczatek tablicy naglowkow programu
inline uint64_t ph(unsigned i) { return PH0 + uint64_t(i) * 56; }
// Przesuniecia pol naglowka programu.
constexpr uint64_t P_TYPE = 0, P_FLAGS = 4, P_OFFSET = 8, P_VADDR = 16, P_PADDR = 24, P_FILESZ = 32, P_MEMSZ = 40,
                   P_ALIGN = 48;

struct Obraz {
    std::vector<uint8_t> b;
    void w16(uint64_t o, uint16_t v) { b[o] = uint8_t(v); b[o + 1] = uint8_t(v >> 8); }
    void w32(uint64_t o, uint32_t v) { w16(o, uint16_t(v)); w16(o + 2, uint16_t(v >> 16)); }
    void w64(uint64_t o, uint64_t v) { w32(o, uint32_t(v)); w32(o + 4, uint32_t(v >> 32)); }
    uint64_t r64(uint64_t o) const { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | b[o + i]; return v; }
    void segment(unsigned i, uint32_t typ, uint32_t flagi, uint64_t off, uint64_t vaddr, uint64_t paddr, uint64_t filesz,
                 uint64_t memsz, uint64_t align) {
        w32(ph(i) + P_TYPE, typ); w32(ph(i) + P_FLAGS, flagi); w64(ph(i) + P_OFFSET, off); w64(ph(i) + P_VADDR, vaddr);
        w64(ph(i) + P_PADDR, paddr); w64(ph(i) + P_FILESZ, filesz); w64(ph(i) + P_MEMSZ, memsz); w64(ph(i) + P_ALIGN, align);
    }
};

// Naglowek Multiboot2: magic, arch, dlugosc, suma, tag "framebuffer"-podobny (typ 5, 20 B -> 24), tag konca.
inline void naglowek_mb2(Obraz& o, uint64_t off) {
    const uint32_t magic = 0xE85250D6u, arch = 0, dl = 16 + 24 + 8;
    o.w32(off, magic); o.w32(off + 4, arch); o.w32(off + 8, dl); o.w32(off + 12, uint32_t(0u - (magic + arch + dl)));
    o.w16(off + 16, 5); o.w16(off + 18, 0); o.w32(off + 20, 20);
    o.w32(off + 24, 1280); o.w32(off + 28, 720); o.w32(off + 32, 32);
    o.w16(off + 40, 0); o.w16(off + 42, 0); o.w32(off + 44, 8);
}

// Syntetyczne jadro w ukladzie jadra produkcyjnego (skala zmniejszona):
//   seg0 R   .boot  plik 0x1000 -> 0x100000, 0x30 B (naglowek MB2)
//   seg1 R-X .text  plik 0x2000 -> 0x101000, 0x100 B, wejscie 0x101010
//   seg2 RW  .data  plik 0x3000 -> 0x102000, 0x80 B z pliku + BSS do 0x2000
inline Obraz jadro_minimalne() {
    Obraz o;
    o.b.assign(0x3080, 0);
    const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 2, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(o.b.data(), ident, 16);
    o.w16(16, 2); o.w16(18, 62); o.w32(20, 1); o.w64(24, 0x101010); o.w64(32, PH0); o.w64(40, 0);
    o.w32(48, 0); o.w16(52, 64); o.w16(54, 56); o.w16(56, 3); o.w16(58, 64); o.w16(60, 0); o.w16(62, 0);
    o.segment(0, 1, brb::ELF_PF_R, 0x1000, 0x100000, 0x100000, 0x30, 0x30, 0x1000);
    o.segment(1, 1, brb::ELF_PF_R | brb::ELF_PF_X, 0x2000, 0x101000, 0x101000, 0x100, 0x100, 0x1000);
    o.segment(2, 1, brb::ELF_PF_R | brb::ELF_PF_W, 0x3000, 0x102000, 0x102000, 0x80, 0x2000, 0x1000);
    naglowek_mb2(o, 0x1000);
    for (int i = 0; i < 0x100; ++i) o.b[0x2000 + i] = uint8_t(0x90);
    return o;
}

// Niezalezna kontrola planu (nie korzysta z kodu parsera): jesli parser zaakceptowal obraz, plan musi byc
// wewnetrznie spojny i zgodny z polityka. Zwraca pusty tekst albo opis naruszenia.
inline std::string niezmienniki(const brb::PlanLadowania& p, uint64_t rozmiar_pliku, const brb::PolitykaElf& pol) {
    if (p.liczba_segmentow == 0 || p.liczba_segmentow > brb::ELF_MAKS_SEGMENTOW) return "liczba segmentow";
    if (p.rozmiar_pliku != rozmiar_pliku) return "rozmiar pliku";
    U128 z_pliku = 0, zera = 0;
    bool wejscie_ok = false;
    for (uint32_t i = 0; i < p.liczba_segmentow; ++i) {
        const brb::SegmentPlanu& s = p.segmenty[i];
        if (s.bajty_z_pliku > s.rozmiar_w_pamieci || s.rozmiar_w_pamieci == 0) return "filesz/memsz";
        if ((U128)s.przesuniecie_pliku + s.bajty_z_pliku > rozmiar_pliku) return "dane poza plikiem";
        if (s.bajty_zerowane != s.rozmiar_w_pamieci - s.bajty_z_pliku) return "BSS";
        if ((s.prawa & brb::ELF_PF_W) && (s.prawa & brb::ELF_PF_X)) return "W+X";
        if (!(s.prawa & brb::ELF_PF_R)) return "brak R";
        const U128 koniec = (U128)s.adres_fizyczny + s.rozmiar_w_pamieci;
        if (koniec > ~0ull) return "przepelnienie konca";
        if (s.strona_poczatek % 4096 || s.strona_koniec % 4096 || s.strona_poczatek > s.adres_fizyczny ||
            s.strona_koniec < koniec || s.strona_koniec - koniec >= 4096 || s.adres_fizyczny - s.strona_poczatek >= 4096)
            return "zakres stron";
        if (s.liczba_stron != (s.strona_koniec - s.strona_poczatek) / 4096) return "liczba stron";
        if (s.adres_fizyczny < pol.min_adres_fizyczny || s.strona_koniec > pol.maks_koniec_fizyczny) return "polityka zakresu";
        if (pol.wymagaj_tozsamosci && s.adres_fizyczny != s.adres_wirtualny) return "tozsamosc";
        if (s.wyrownanie > 1 && ((s.wyrownanie & (s.wyrownanie - 1)) != 0 ||
                                 (s.adres_wirtualny % s.wyrownanie) != (s.przesuniecie_pliku % s.wyrownanie)))
            return "wyrownanie";
        if (i > 0 && s.adres_wirtualny < p.segmenty[i - 1].adres_wirtualny) return "kolejnosc";
        for (uint32_t j = 0; j < i; ++j) {
            const brb::SegmentPlanu& o = p.segmenty[j];
            if (s.adres_fizyczny < o.adres_fizyczny + o.rozmiar_w_pamieci && o.adres_fizyczny < koniec) return "nakladanie";
        }
        z_pliku += s.bajty_z_pliku;
        zera += s.bajty_zerowane;
        if (p.wejscie_wirtualne >= s.adres_wirtualny && p.wejscie_wirtualne - s.adres_wirtualny < s.bajty_z_pliku &&
            (s.prawa & brb::ELF_PF_X) && p.segment_wejscia == i &&
            p.wejscie_fizyczne == s.adres_fizyczny + (p.wejscie_wirtualne - s.adres_wirtualny))
            wejscie_ok = true;
    }
    if (!wejscie_ok) return "wejscie";
    if (p.wejscie_fizyczne >= pol.maks_wejscie) return "wejscie poza zakresem";
    if (z_pliku != p.bajty_z_pliku || zera != p.bajty_zerowane) return "sumy";
    if (p.pamiec_stron == 0 || p.pamiec_stron % 4096 || p.pamiec_stron > pol.maks_pamiec ||
        p.pamiec_stron > p.zakres_koniec - p.zakres_poczatek)
        return "pamiec stron";
    if (pol.wymagaj_mb2 && (!p.mb2.obecny || p.mb2.przesuniecie_pliku % 8 || p.mb2.przesuniecie_pliku + p.mb2.dlugosc > 32768))
        return "multiboot2";
    return "";
}

}  // namespace fixture

#endif
