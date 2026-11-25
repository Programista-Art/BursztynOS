#pragma once
/*
 * Bursztyn OS - rdzen sterownika Intel ICH AC97 (niezalezny od sprzetu).
 *
 * Cala logika sterownika (walidacja PCI, reset, kodek, BDL, uzupelnianie DMA, statusy RW1C, niedobory,
 * stop/restart, wylaczenie) jest tutaj i korzysta z wstrzyknietej platformy P:
 *
 *   uint8_t  in8(uint16_t port);   uint16_t in16(uint16_t);   uint32_t in32(uint16_t);
 *   void     out8(uint16_t, uint8_t); void out16(uint16_t, uint16_t); void out32(uint16_t, uint32_t);
 *   uint32_t pci_czytaj32(uint8_t offset);  void pci_zapisz32(uint8_t offset, uint32_t);
 *   uint64_t teraz_ns();  void czekaj_us(uint32_t);
 *   bool dma_przydziel(uint32_t rozmiar, Dma* wyj);  void dma_zwolnij(Dma*);
 *   void bariera();          // porzadek zapisow pamieci DMA wzgledem zapisow portow
 *
 * Jadro dostarcza porty I/O, konfiguracje PCI, HPET i PMM (sterowniki/dzwiek/ac97.cpp); test hosta dostarcza
 * emulator kontrolera i kodeka (testy/test_ac97_host.cpp). Ten sam kod dziala w obu.
 *
 * Model wykonania jak w sterowniku HDA: bez IRQ urzadzenia (PCI INTx wylaczone), praca odroczona wywolywana z
 * timera/planisty odczytuje CIV i SR, potwierdza statusy RW1C i uzupelnia bufory. Kazda petla oczekiwania na
 * sprzet ma limit czasu. Zadna funkcja nie alokuje pamieci poza inicjalizuj().
 *
 * Wlasnosc buforow DMA: wpisy BDL od CIV do LVI wlacznie (cyklicznie, 32 wpisy) naleza do sprzetu. Sterownik
 * zapisuje wylacznie wpis LVI+1, i to tylko wtedy, gdy sprzet posiada mniej niz WYPRZEDZENIE (< 31) wpisow,
 * wiec LVI+1 nigdy nie jest rowny CIV. Po bledzie zatrzymania (brak DCH) bufory nie sa zwalniane.
 */
#include <stdint.h>
#include <stddef.h>
#include "ac97_rejestry.h"

namespace Ac97 {

constexpr uint32_t CZESTOTLIWOSC = 48000;
constexpr uint32_t OKRES_RAMEK = 1024;                             // 21,33 ms
constexpr uint32_t OKRES_PROBEK = OKRES_RAMEK * 2;                 // stereo PCM16
constexpr uint32_t OKRES_BAJTOW = OKRES_PROBEK * 2;                // 4096 B = jedna strona
constexpr uint32_t WYPRZEDZENIE = 4;                               // okresy w posiadaniu sprzetu (~85 ms)
constexpr uint64_t OKRES_NS = uint64_t(OKRES_RAMEK) * 1000000000ULL / CZESTOTLIWOSC;
constexpr uint64_t SPOZNIENIE_NS = (WYPRZEDZENIE - 1) * OKRES_NS;  // obsluga pozniej = ryzyko niedoboru
constexpr uint64_t ZASTOJ_NS = (WYPRZEDZENIE + 4) * OKRES_NS;      // CIV/PICB bez ruchu = zawieszony DMA
constexpr uint32_t MAKS_ODZYSKAN_POD_RZAD = 3;
constexpr uint32_t ROZMIAR_BDL = WPISY_BDL * sizeof(WpisBdl);      // 256 B
constexpr uint32_t ROZMIAR_BUFOROW = WPISY_BDL * OKRES_BAJTOW;     // 128 KiB
static_assert(OKRES_PROBEK <= BDL_MAKS_PROBEK && (OKRES_PROBEK % 2) == 0, "dlugosc wpisu BDL");
static_assert(WYPRZEDZENIE >= 2 && WYPRZEDZENIE < WPISY_BDL - 1, "okno wlasnosci");

/* Limity oczekiwania (mikrosekundy) */
constexpr uint32_t LIMIT_GOTOWOSCI_KODEKA_US = 1000000;            // po zimnym resecie
constexpr uint32_t LIMIT_POWERDOWN_US = 1000000;
constexpr uint32_t LIMIT_CAS_US = 1000;
constexpr uint32_t LIMIT_KANALU_US = 10000;                        // RR / DCH
constexpr uint32_t KROK_US = 10;

enum class Stan : uint8_t { Brak, Wykryty, Gotowy, Odtwarza, Awaria, Wylaczony };
enum class Blad : uint8_t {
    Brak, ZlyVendor, NieobslugiwaneUrzadzenie, ZlaKlasa, ZlyBarNam, ZlyBarNabm, KomendaPci,
    KodekNieGotowy, DostepKodeka, Powerdown, Czestotliwosc, Dma, DmaPoza4GiB, ResetKanalu,
    BdbarOdczyt, Stop, Zastoj, Pozycja, ZlyStan, Count
};

inline const char* nazwa_bledu(Blad b) {
    switch (b) {
        case Blad::Brak: return "brak";
        case Blad::ZlyVendor: return "producent PCI nie jest Intel";
        case Blad::NieobslugiwaneUrzadzenie: return "nieobslugiwany identyfikator urzadzenia";
        case Blad::ZlaKlasa: return "klasa PCI inna niz 04:01 (audio)";
        case Blad::ZlyBarNam: return "nieprawidlowy BAR0 (NAMBAR)";
        case Blad::ZlyBarNabm: return "nieprawidlowy BAR1 (NABMBAR)";
        case Blad::KomendaPci: return "PCI I/O lub bus master nie wlaczyl sie";
        case Blad::KodekNieGotowy: return "kodek nie zglosil gotowosci po resecie";
        case Blad::DostepKodeka: return "przekroczony czas dostepu do kodeka";
        case Blad::Powerdown: return "DAC/ANL/REF kodeka niegotowe";
        case Blad::Czestotliwosc: return "kodek nie przyjal 48000 Hz";
        case Blad::Dma: return "brak pamieci DMA";
        case Blad::DmaPoza4GiB: return "bufor DMA poza 32-bitowym zakresem";
        case Blad::ResetKanalu: return "reset kanalu PCM Out nie zakonczyl sie";
        case Blad::BdbarOdczyt: return "BDBAR nie zostal zapisany";
        case Blad::Stop: return "DMA nie zatrzymalo sie (bufory zachowane)";
        case Blad::Zastoj: return "DMA bez postepu";
        case Blad::Pozycja: return "niespojny CIV";
        case Blad::ZlyStan: return "operacja w nieprawidlowym stanie";
        default: return "nieznany";
    }
}

struct ObslugiwaneId { uint16_t device; const char* nazwa; bool ich4; };
// Identyfikatory z dokumentacji Intel. QEMU "-device AC97" i VirtualBox "ICH AC97" emuluja 8086:2415.
inline const ObslugiwaneId* znajdz_id(uint16_t vendor, uint16_t device) {
    static const ObslugiwaneId tab[] = {
        {0x2415, "82801AA ICH", false},  {0x2425, "82801AB ICH0", false}, {0x2445, "82801BA ICH2", false},
        {0x2485, "82801CA ICH3", false}, {0x7195, "82440MX", false},      {0x24C5, "82801DB ICH4", true},
        {0x24D5, "82801EB ICH5", true},  {0x25A6, "6300ESB", true},       {0x266E, "82801FB ICH6", true},
        {0x27DE, "82801G ICH7", true},
    };
    if (vendor != PCI_VENDOR_INTEL) return nullptr;
    for (const auto& e : tab) if (e.device == device) return &e;
    return nullptr;
}

struct OpisPci {
    uint8_t bus, slot, func;
    uint16_t vendor, device;
    uint8_t klasa, podklasa;
    uint32_t bar0, bar1;
};

// Waliduje identyfikacje i BAR-y bez zadnego dostepu do sprzetu.
inline Blad waliduj_pci(const OpisPci& o, uint16_t* nam, uint16_t* nabm, const ObslugiwaneId** id = nullptr) {
    if (o.vendor != PCI_VENDOR_INTEL) return Blad::ZlyVendor;
    const ObslugiwaneId* e = znajdz_id(o.vendor, o.device);
    if (!e) return Blad::NieobslugiwaneUrzadzenie;
    if (o.klasa != PCI_KLASA_MULTIMEDIA || o.podklasa != PCI_PODKLASA_AUDIO) return Blad::ZlaKlasa;
    auto bar_io = [](uint32_t bar, uint32_t rozmiar, uint16_t* wyj) {
        if (!(bar & 1u)) return false;                            // BAR pamieciowy - tu wymagany I/O
        const uint32_t a = bar & ~3u;
        if (a == 0 || a > 0xFFFFu || (a & (rozmiar - 1u)) || a + rozmiar > 0x10000u) return false;
        *wyj = static_cast<uint16_t>(a);
        return true;
    };
    uint16_t n = 0, b = 0;
    if (!bar_io(o.bar0, ROZMIAR_NAM, &n)) return Blad::ZlyBarNam;
    if (!bar_io(o.bar1, ROZMIAR_NABM, &b)) return Blad::ZlyBarNabm;
    if ((n < b + ROZMIAR_NABM) && (b < n + ROZMIAR_NAM)) return Blad::ZlyBarNabm;   // nakladajace sie zakresy
    if (nam) *nam = n;
    if (nabm) *nabm = b;
    if (id) *id = e;
    return Blad::Brak;
}

struct Dma { void* wirtualny; uint64_t fizyczny; uint32_t rozmiar; };

// Bufor DMA musi lezec w calosci ponizej 4 GiB (BDBAR i adresy BDL sa 32-bitowe).
inline bool dma_32bit(const Dma& d, uint32_t wymagany, uint32_t wyrownanie) {
    if (!d.wirtualny || d.rozmiar < wymagany || d.fizyczny == 0) return false;
    if (d.fizyczny & (wyrownanie - 1u)) return false;
    return d.fizyczny <= 0x100000000ULL - d.rozmiar;
}

struct Diagnostyka {
    Stan stan;
    Blad blad, ostatni_blad;
    uint16_t vendor, device, nam, nabm;
    uint16_t kodek_vendor1, kodek_vendor2, kodek_reset_caps, kodek_ext_id;
    uint8_t vra, tlumienie_6bit, kroki_glosnosci, ich4;
    uint32_t glosnosc, wyciszony;
    uint64_t bdl_fizyczny, bufory_fizyczne;
    uint64_t starty, okresy, ramki, okresy_ciszy, niedobory, bledy_fifo, statusy_ioc, statusy_lvbci;
    uint64_t odzyskania, zastoje, bledy_pozycji, spoznienia, maks_luka_ns, bledy_cas, bledy_odczytu_kodeka;
    uint32_t w_sprzecie, civ, lvi;
};

// Zrodlo PCM: wypelnia dokladnie `ramki` ramek stereo PCM16 (przeplatane L,P) i zatwierdza je w mikserze.
// Wywolywane dokladnie raz na kazdy okres przekazywany sprzetowi. Zwraca true, gdy okres zawiera sygnal.
using Zrodlo = bool (*)(void* kontekst, int16_t* probki, uint32_t ramki);

template <class P>
class Kontroler {
public:
    explicit Kontroler(P& p) : p_(p) { d_ = {}; d_.glosnosc = 80; }
    Kontroler(const Kontroler&) = delete;
    Kontroler& operator=(const Kontroler&) = delete;

    const Diagnostyka& diag() const { return d_; }
    Stan stan() const { return d_.stan; }
    bool gotowy() const { return d_.stan == Stan::Gotowy || d_.stan == Stan::Odtwarza; }
    bool odtwarza() const { return d_.stan == Stan::Odtwarza; }
    // true: sprzet moze nadal czytac bufory DMA - nie wolno ich zwolnic ani nadpisac.
    bool sprzet_posiada_dma() const { return wlasnosc_sprzetu_; }

    // Etap 1: identyfikacja (tylko walidacja, bez zapisow do sprzetu).
    Blad sonduj(const OpisPci& o) {
        if (d_.stan != Stan::Brak && d_.stan != Stan::Wylaczony && d_.stan != Stan::Awaria) return porazka(Blad::ZlyStan);
        const ObslugiwaneId* id = nullptr;
        uint16_t nam = 0, nabm = 0;
        const Blad b = waliduj_pci(o, &nam, &nabm, &id);
        const uint32_t glosnosc = d_.glosnosc, wyciszony = d_.wyciszony;
        d_ = {};
        d_.glosnosc = glosnosc; d_.wyciszony = wyciszony;
        d_.vendor = o.vendor; d_.device = o.device;
        if (b != Blad::Brak) { d_.stan = Stan::Brak; return porazka(b); }
        nam_ = nam; nabm_ = nabm; po_ = static_cast<uint16_t>(nabm + PO_BASE);
        d_.nam = nam; d_.nabm = nabm; d_.ich4 = id->ich4;
        d_.stan = Stan::Wykryty;
        return Blad::Brak;
    }

    // Etap 2: wlaczenie PCI, zimny reset AC-link, kodek, DMA i reset kanalu PCM Out.
    Blad inicjalizuj() {
        if (d_.stan != Stan::Wykryty) return porazka(Blad::ZlyStan);
        // PCI COMMAND: I/O + bus master, INTx wylaczone (model odpytywania). Gorna polowa (STATUS) = 0,
        // bo bity statusu sa RW1C - zapis jedynek wyczyscilby cudze zdarzenia.
        const uint32_t cmd = p_.pci_czytaj32(0x04) & 0xFFFFu;
        p_.pci_zapisz32(0x04, (cmd | 0x0001u | 0x0004u | 0x0400u) & 0xFFFFu);
        if ((p_.pci_czytaj32(0x04) & 0x0005u) != 0x0005u) return awaria(Blad::KomendaPci);
        if (d_.ich4) {   // ICH4+: natywne BAR I/O dzialaja tylko z IOSE (CFG 0x41 bit 0)
            const uint32_t v = p_.pci_czytaj32(0x40);
            if (!((v >> 8) & PCI_ICH4_IOSE)) p_.pci_zapisz32(0x40, v | (uint32_t(PCI_ICH4_IOSE) << 8));
        }
        // Zimny reset: AC_RESET# aktywny >= 1 us, potem czekamy na Primary Codec Ready.
        p_.out32(nabm_ + NABM_GLOB_CNT, 0);
        p_.czekaj_us(20);
        p_.out32(nabm_ + NABM_GLOB_CNT, GC_COLD);
        if (!czekaj([&] { return (p_.in32(nabm_ + NABM_GLOB_STA) & GS_PCR) != 0; }, LIMIT_GOTOWOSCI_KODEKA_US))
            return awaria(Blad::KodekNieGotowy);
        p_.out32(nabm_ + NABM_GLOB_STA, GS_RCS | GS_GSCI);       // RW1C: stare zdarzenia
        if (!kodek_zapisz(NAM_RESET, 0)) return awaria(Blad::DostepKodeka);
        uint16_t pd = 0;
        bool odpowiedzial = false;
        if (!czekaj([&] {
                if (!kodek_czytaj(NAM_POWERDOWN, &pd)) return false;
                odpowiedzial = true;
                return (pd & PD_WYJSCIE_GOTOWE) == PD_WYJSCIE_GOTOWE;
            }, LIMIT_POWERDOWN_US))
            return awaria(odpowiedzial ? Blad::Powerdown : Blad::DostepKodeka);
        uint16_t caps = 0, v1 = 0, v2 = 0, ext = 0;
        if (!kodek_czytaj(NAM_RESET, &caps) || !kodek_czytaj(NAM_VENDOR1, &v1) || !kodek_czytaj(NAM_VENDOR2, &v2) ||
            !kodek_czytaj(NAM_EXT_ID, &ext))
            return awaria(Blad::DostepKodeka);
        d_.kodek_reset_caps = caps; d_.kodek_vendor1 = v1; d_.kodek_vendor2 = v2; d_.kodek_ext_id = ext;
        // 48 kHz: domyslna (stala) czestotliwosc AC97. VRA tylko gdy kodek ja deklaruje - wtedy ustawiamy i sprawdzamy.
        if (ext & EXT_VRA) {
            uint16_t ctl = 0, rate = 0;
            if (!kodek_czytaj(NAM_EXT_CTRL, &ctl) || !kodek_zapisz(NAM_EXT_CTRL, uint16_t(ctl | EXT_VRA)) ||
                !kodek_zapisz(NAM_PCM_FRONT_RATE, uint16_t(CZESTOTLIWOSC)) || !kodek_czytaj(NAM_PCM_FRONT_RATE, &rate))
                return awaria(Blad::DostepKodeka);
            if (rate != CZESTOTLIWOSC) return awaria(Blad::Czestotliwosc);
            d_.vra = 1;
        }
        // Rozdzielczosc tlumienia Master: zapis bitu 5; kodek 5-bitowy odczytuje 0x1F (AC97 2.3, 5.7.2).
        uint16_t m = 0;
        if (!kodek_zapisz(NAM_MASTER, uint16_t(NAM_MUTE | 0x2020)) || !kodek_czytaj(NAM_MASTER, &m))
            return awaria(Blad::DostepKodeka);
        d_.tlumienie_6bit = ((m & 0x3F) == 0x20) ? 1 : 0;
        d_.kroki_glosnosci = d_.tlumienie_6bit ? 64 : 32;
        if (!kodek_zapisz(NAM_PCM_OUT, PCM_OUT_0DB)) return awaria(Blad::DostepKodeka);
        if (!zapisz_glosnosc()) return awaria(Blad::DostepKodeka);
        // DMA: BDL (32 x 8 B) i 32 bufory po jednej stronie; caly zakres ponizej 4 GiB.
        if (!p_.dma_przydziel(4096, &bdl_dma_)) return awaria(Blad::Dma);
        if (!dma_32bit(bdl_dma_, ROZMIAR_BDL, 8)) { zwolnij_dma(); return awaria(Blad::DmaPoza4GiB); }
        if (!p_.dma_przydziel(ROZMIAR_BUFOROW, &buf_dma_)) { zwolnij_dma(); return awaria(Blad::Dma); }
        if (!dma_32bit(buf_dma_, ROZMIAR_BUFOROW, 4096)) { zwolnij_dma(); return awaria(Blad::DmaPoza4GiB); }
        bdl_ = static_cast<volatile WpisBdl*>(bdl_dma_.wirtualny);
        d_.bdl_fizyczny = bdl_dma_.fizyczny; d_.bufory_fizyczne = buf_dma_.fizyczny;
        for (uint32_t i = 0; i < WPISY_BDL; ++i) {
            bdl_[i].adres = static_cast<uint32_t>(buf_dma_.fizyczny + uint64_t(i) * OKRES_BAJTOW);
            bdl_[i].probki = 0;
            bdl_[i].flagi = 0;
        }
        if (!resetuj_kanal()) { zwolnij_dma(); return awaria(Blad::ResetKanalu); }
        d_.stan = Stan::Gotowy;
        d_.blad = Blad::Brak;
        return Blad::Brak;
    }

    // Start odtwarzania: WYPRZEDZENIE okresow ze zrodla, potem RPBM.
    bool start(Zrodlo zrodlo, void* kontekst, uint64_t teraz) {
        if (d_.stan != Stan::Gotowy || !zrodlo || wlasnosc_sprzetu_) { (void)porazka(Blad::ZlyStan); return false; }
        zrodlo_ = zrodlo; kontekst_ = kontekst;
        if (!resetuj_kanal()) { awaria_odtwarzania(Blad::ResetKanalu); return false; }
        p_.out32(po_ + BOX_BDBAR, static_cast<uint32_t>(bdl_dma_.fizyczny));
        if (p_.in32(po_ + BOX_BDBAR) != static_cast<uint32_t>(bdl_dma_.fizyczny)) { awaria_odtwarzania(Blad::BdbarOdczyt); return false; }
        for (uint32_t i = 0; i < WYPRZEDZENIE; ++i) przygotuj_wpis(i);
        civ_ = 0; lvi_ = WYPRZEDZENIE - 1; picb_ = 0;
        p_.bariera();
        p_.out8(po_ + BOX_LVI, static_cast<uint8_t>(lvi_));
        p_.out16(po_ + BOX_SR, SR_W1C);
        wlasnosc_sprzetu_ = true;
        p_.out8(po_ + BOX_CR, CR_RPBM | CR_IOCE | CR_LVBIE | CR_FEIE);
        ostatnia_obsluga_ = ostatni_ruch_ = teraz;
        ++d_.starty;
        d_.stan = Stan::Odtwarza;
        aktualizuj_okno();
        return true;
    }

    // Praca odroczona: potwierdzenie statusow, wycofanie zakonczonych okresow i uzupelnienie do WYPRZEDZENIE.
    // Wynik != Brak oznacza, ze odtwarzanie zostalo zatrzymane (lub awaria).
    Blad obsluz(uint64_t teraz) {
        if (d_.stan != Stan::Odtwarza) return Blad::Brak;
        if (ostatnia_obsluga_ && teraz > ostatnia_obsluga_) {
            const uint64_t luka = teraz - ostatnia_obsluga_;
            if (luka > d_.maks_luka_ns) d_.maks_luka_ns = luka;
            if (luka > SPOZNIENIE_NS) ++d_.spoznienia;
        }
        ostatnia_obsluga_ = teraz;
        const uint16_t sr = p_.in16(po_ + BOX_SR);
        const uint32_t civ = p_.in8(po_ + BOX_CIV) & (WPISY_BDL - 1);
        const uint16_t picb = p_.in16(po_ + BOX_PICB);
        if (sr & SR_W1C) p_.out16(po_ + BOX_SR, static_cast<uint16_t>(sr & SR_W1C));   // potwierdzenie RW1C
        if (sr & SR_BCIS) ++d_.statusy_ioc;
        if (sr & SR_LVBCI) ++d_.statusy_lvbci;
        if (sr & SR_FIFOE) ++d_.bledy_fifo;
        const uint32_t posiadane = ((lvi_ - civ_) & (WPISY_BDL - 1)) + 1;
        const uint32_t ukonczone = (civ - civ_) & (WPISY_BDL - 1);
        if (ukonczone >= posiadane) {   // CIV nie moze minac LVI
            ++d_.bledy_pozycji;
            return odzyskaj(teraz, Blad::Pozycja);
        }
        if (ukonczone || picb != picb_) ostatni_ruch_ = teraz;
        civ_ = civ; picb_ = picb;
        if (sr & SR_DCH) {
            // Kontroler zatrzymal sie po ostatnim poprawnym buforze: niedobor (sterownik nie zdazyl uzupelnic).
            ++d_.niedobory;
            return odzyskaj(teraz, Blad::Brak);
        }
        if (teraz > ostatni_ruch_ && teraz - ostatni_ruch_ > ZASTOJ_NS) {
            ++d_.zastoje;
            return odzyskaj(teraz, Blad::Zastoj);
        }
        uzupelnij();
        if (ukonczone) odzyskania_pod_rzad_ = 0;   // tylko rzeczywisty postep DMA konczy serie odzyskan
        return Blad::Brak;
    }

    // Zatrzymanie DMA. false = sprzet nie potwierdzil DCH: wlasnosc buforow pozostaje po stronie sprzetu.
    bool zatrzymaj() {
        if (!wlasnosc_sprzetu_) {
            if (d_.stan == Stan::Odtwarza) d_.stan = Stan::Gotowy;
            return true;
        }
        p_.out8(po_ + BOX_CR, static_cast<uint8_t>(p_.in8(po_ + BOX_CR) & ~(CR_RPBM | CR_IOCE | CR_LVBIE | CR_FEIE)));
        if (!czekaj([&] { return (p_.in16(po_ + BOX_SR) & SR_DCH) != 0; }, LIMIT_KANALU_US)) {
            d_.stan = Stan::Awaria;
            d_.blad = d_.ostatni_blad = Blad::Stop;
            return false;
        }
        wlasnosc_sprzetu_ = false;
        if (!resetuj_kanal()) { d_.stan = Stan::Awaria; d_.blad = d_.ostatni_blad = Blad::ResetKanalu; return true; }
        d_.stan = Stan::Gotowy;
        aktualizuj_okno();
        return true;
    }

    // Wylaczenie sterownika: zatrzymanie, wyciszenie, bus master off, zwolnienie DMA. Gdy DMA sie nie zatrzymalo,
    // bufory i bus master zostaja (zwolnienie pamieci czytanej przez urzadzenie byloby niebezpieczne).
    bool wylacz() {
        if (d_.stan == Stan::Brak || d_.stan == Stan::Wykryty || d_.stan == Stan::Wylaczony) { d_.stan = Stan::Wylaczony; return true; }
        if (!zatrzymaj() || wlasnosc_sprzetu_) return false;
        (void)kodek_zapisz(NAM_MASTER, uint16_t(NAM_MUTE | maks_tlumienie() << 8 | maks_tlumienie()));
        const uint32_t cmd = p_.pci_czytaj32(0x04) & 0xFFFFu;
        p_.pci_zapisz32(0x04, cmd & ~0x0004u & 0xFFFFu);
        zwolnij_dma();
        d_.stan = Stan::Wylaczony;
        return true;
    }

    // Glosnosc sprzetowa (tlumienie Master). 0 % = wyciszenie.
    bool ustaw_glosnosc(uint32_t procent, bool wyciszony) {
        if (procent > 100) return false;
        d_.glosnosc = procent; d_.wyciszony = wyciszony ? 1 : 0;
        if (!gotowy()) return d_.stan == Stan::Wykryty;   // zostanie zapisana przy inicjalizacji
        return zapisz_glosnosc();
    }
    uint16_t rejestr_master() const { return master_; }

private:
    template <class F> bool czekaj(F warunek, uint32_t limit_us) {
        for (uint32_t t = 0; t <= limit_us; t += KROK_US) {
            if (warunek()) return true;
            p_.czekaj_us(KROK_US);
        }
        return warunek();
    }
    // CAS: odczyt 0 = semafor przejety (sprzet ustawia go na 1 do zakonczenia cyklu AC-link).
    bool cas() {
        if (czekaj([&] { return (p_.in8(nabm_ + NABM_CAS) & 1u) == 0; }, LIMIT_CAS_US)) return true;
        ++d_.bledy_cas;
        return false;
    }
    bool kodek_czytaj(uint16_t reg, uint16_t* v) {
        if (!cas()) return false;
        *v = p_.in16(nam_ + reg);
        if (p_.in32(nabm_ + NABM_GLOB_STA) & GS_RCS) {   // kodek nie odpowiedzial: wynik nieprawidlowy
            p_.out32(nabm_ + NABM_GLOB_STA, GS_RCS);
            ++d_.bledy_odczytu_kodeka;
            return false;
        }
        return true;
    }
    bool kodek_zapisz(uint16_t reg, uint16_t v) {
        if (!cas()) return false;
        p_.out16(nam_ + reg, v);
        return true;
    }
    uint16_t maks_tlumienie() const { return d_.tlumienie_6bit ? 0x3F : 0x1F; }
    bool zapisz_glosnosc() {
        const uint16_t maks = maks_tlumienie();
        const uint32_t p = d_.glosnosc;
        uint16_t att = static_cast<uint16_t>(((100u - p) * maks + 50u) / 100u);
        if (att > maks) att = maks;
        uint16_t v = static_cast<uint16_t>(att << 8 | att);
        if (d_.wyciszony || p == 0) v |= NAM_MUTE;
        if (!kodek_zapisz(NAM_MASTER, v)) return false;
        master_ = v;
        return true;
    }
    bool resetuj_kanal() {
        p_.out8(po_ + BOX_CR, 0);
        if (!czekaj([&] { return (p_.in16(po_ + BOX_SR) & SR_DCH) != 0; }, LIMIT_KANALU_US)) return false;
        p_.out8(po_ + BOX_CR, CR_RR);
        if (!czekaj([&] { return (p_.in8(po_ + BOX_CR) & CR_RR) == 0; }, LIMIT_KANALU_US)) return false;
        p_.out16(po_ + BOX_SR, SR_W1C);
        return true;
    }
    int16_t* bufor(uint32_t i) {
        return reinterpret_cast<int16_t*>(static_cast<uint8_t*>(buf_dma_.wirtualny) + size_t(i) * OKRES_BAJTOW);
    }
    // Wpis i jest poza oknem sprzetu (wywolujacy to gwarantuje). Zrodlo zatwierdza okres w mikserze.
    void przygotuj_wpis(uint32_t i) {
        const bool sygnal = zrodlo_(kontekst_, bufor(i), OKRES_RAMEK);
        if (!sygnal) ++d_.okresy_ciszy;
        bdl_[i].adres = static_cast<uint32_t>(buf_dma_.fizyczny + uint64_t(i) * OKRES_BAJTOW);
        bdl_[i].probki = static_cast<uint16_t>(OKRES_PROBEK);
        bdl_[i].flagi = BDL_IOC | BDL_BUP;
        ++d_.okresy;
        d_.ramki += OKRES_RAMEK;
    }
    void uzupelnij() {
        uint32_t posiadane = ((lvi_ - civ_) & (WPISY_BDL - 1)) + 1;
        while (posiadane < WYPRZEDZENIE) {
            const uint32_t nastepny = (lvi_ + 1) & (WPISY_BDL - 1);
            if (nastepny == civ_) break;            // nigdy: bufor biezacy nalezy do sprzetu
            przygotuj_wpis(nastepny);
            p_.bariera();                           // dane i wpis BDL widoczne przed publikacja LVI
            lvi_ = nastepny;
            p_.out8(po_ + BOX_LVI, static_cast<uint8_t>(lvi_));
            ++posiadane;
        }
        aktualizuj_okno();
    }
    // Niedobor/zastoj: pelny restart kanalu (stop z potwierdzeniem DCH, RR, ponowne przygotowanie od wpisu 0).
    Blad odzyskaj(uint64_t teraz, Blad przyczyna) {
        ++d_.odzyskania;
        if (przyczyna != Blad::Brak) d_.ostatni_blad = przyczyna;
        if (!zatrzymaj()) return Blad::Stop;
        if (d_.stan != Stan::Gotowy) return d_.blad;
        if (++odzyskania_pod_rzad_ > MAKS_ODZYSKAN_POD_RZAD) {
            d_.stan = Stan::Awaria;
            d_.blad = przyczyna == Blad::Brak ? Blad::Zastoj : przyczyna;
            return d_.blad;
        }
        if (!start(zrodlo_, kontekst_, teraz)) return d_.blad;
        return Blad::Brak;
    }
    void aktualizuj_okno() {
        d_.civ = civ_; d_.lvi = lvi_;
        d_.w_sprzecie = wlasnosc_sprzetu_ ? ((lvi_ - civ_) & (WPISY_BDL - 1)) + 1 : 0;
    }
    void zwolnij_dma() {
        if (wlasnosc_sprzetu_) return;
        if (buf_dma_.wirtualny) p_.dma_zwolnij(&buf_dma_);
        if (bdl_dma_.wirtualny) p_.dma_zwolnij(&bdl_dma_);
        buf_dma_ = {}; bdl_dma_ = {}; bdl_ = nullptr;
    }
    Blad porazka(Blad b) { d_.ostatni_blad = b; if (d_.stan == Stan::Brak) d_.blad = b; return b; }
    Blad awaria(Blad b) {
        d_.stan = Stan::Awaria; d_.blad = d_.ostatni_blad = b;
        return b;
    }
    void awaria_odtwarzania(Blad b) { d_.stan = Stan::Awaria; d_.blad = d_.ostatni_blad = b; }

    P& p_;
    Diagnostyka d_;
    uint16_t nam_ = 0, nabm_ = 0, po_ = 0, master_ = 0;
    Dma bdl_dma_{}, buf_dma_{};
    volatile WpisBdl* bdl_ = nullptr;
    Zrodlo zrodlo_ = nullptr;
    void* kontekst_ = nullptr;
    uint32_t civ_ = 0, lvi_ = 0, odzyskania_pod_rzad_ = 0;
    uint16_t picb_ = 0;
    uint64_t ostatnia_obsluga_ = 0, ostatni_ruch_ = 0;
    bool wlasnosc_sprzetu_ = false;
};

} // namespace Ac97
