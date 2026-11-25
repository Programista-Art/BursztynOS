/*
 * Bursztyn OS - Intel ICH AC97: integracja z jadrem.
 *
 * Tu sa wylacznie elementy zalezne od jadra: porty I/O, konfiguracja PCI (centralne pci.cpp), HPET,
 * pamiec DMA z PMM (sterowniki/dma.cpp), blokada, praca odroczona i API BWS 110. Cala logika sprzetowa
 * (reset, kodek, BDL, statusy RW1C, wlasnosc buforow, odzyskiwanie) jest w ac97_rdzen.h i ac97_pcm.h,
 * testowana na hoscie z emulatorem (testy/test_ac97_host.cpp).
 *
 * Blokada: try-lock jak w HDA. Wywolania BWS przy zajetej blokadzie zwracaja PCM_BUSY; praca odroczona
 * pomija przebieg. Zadna sekcja pod blokada nie usypia; oczekiwania sprzetowe sa ograniczone (<= 10 ms
 * przy zatrzymaniu kanalu, inicjalizacja przy starcie <= ok. 2 s przy braku kodeka).
 */
#include "ac97.h"
#include "sterowniki/pci/pci.h"
#include "sterowniki/dma.h"
#include "sterowniki/czas/hpet.h"
#include "jadro/dzwiek/pcm_sched_diag.h"

void wypisz_log(const char* tekst);

namespace {

inline uint8_t in8(uint16_t p) { uint8_t v; asm volatile("inb %1, %0" : "=a"(v) : "Nd"(p) : "memory"); return v; }
inline uint16_t in16(uint16_t p) { uint16_t v; asm volatile("inw %1, %0" : "=a"(v) : "Nd"(p) : "memory"); return v; }
inline uint32_t in32(uint16_t p) { uint32_t v; asm volatile("inl %1, %0" : "=a"(v) : "Nd"(p) : "memory"); return v; }
inline void out8(uint16_t p, uint8_t v) { asm volatile("outb %0, %1" :: "a"(v), "Nd"(p) : "memory"); }
inline void out16(uint16_t p, uint16_t v) { asm volatile("outw %0, %1" :: "a"(v), "Nd"(p) : "memory"); }
inline void out32(uint16_t p, uint32_t v) { asm volatile("outl %0, %1" :: "a"(v), "Nd"(p) : "memory"); }

struct PlatformaJadra {
    uint8_t bus = 0, slot = 0, func = 0;
    uint8_t in8(uint16_t p) { return ::in8(p); }
    uint16_t in16(uint16_t p) { return ::in16(p); }
    uint32_t in32(uint16_t p) { return ::in32(p); }
    void out8(uint16_t p, uint8_t v) { ::out8(p, v); }
    void out16(uint16_t p, uint16_t v) { ::out16(p, v); }
    void out32(uint16_t p, uint32_t v) { ::out32(p, v); }
    uint32_t pci_czytaj32(uint8_t o) { return pci_odczytaj_dword(bus, slot, func, o); }
    void pci_zapisz32(uint8_t o, uint32_t v) { pci_zapisz_dword(bus, slot, func, o, v); }
    uint64_t teraz_ns() { return czas_monotoniczny_ns(); }
    void czekaj_us(uint32_t us) {
        const uint64_t koniec = czas_monotoniczny_ns() + uint64_t(us) * 1000ULL;
        while (czas_monotoniczny_ns() < koniec) asm volatile("pause" ::: "memory");
    }
    void bariera() { asm volatile("sfence" ::: "memory"); }
    bool dma_przydziel(uint32_t rozmiar, Ac97::Dma* out) {
        DmaBuffer b{};
        if (!dma_allocate(rozmiar, 4096, &b)) return false;
        *out = {b.virtual_address, b.physical_address, static_cast<uint32_t>(b.page_count * 4096U)};
        return true;
    }
    void dma_zwolnij(Ac97::Dma* d) {
        DmaBuffer b{d->wirtualny, d->fizyczny, d->rozmiar, (d->rozmiar + 4095U) / 4096U};
        dma_release(&b);
        *d = {};
    }
};

PlatformaJadra g_platforma;
Ac97::Kontroler<PlatformaJadra> g_kontroler(g_platforma);
Ac97::SilnikPcm<PlatformaJadra> g_silnik(g_kontroler);
bool g_blokada = false;
bool g_wykryty = false;
bool g_aktywny = false;
uint64_t g_oczekuje_ns = 0;
uint32_t g_generacja_glosnosci = 0;
Ac97::OpisPci g_opis{};

class Blokada {
public:
    Blokada() {
        bool oczekiwane = false;
        ok_ = __atomic_compare_exchange_n(&g_blokada, &oczekiwane, true, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    }
    ~Blokada() { if (ok_) __atomic_store_n(&g_blokada, false, __ATOMIC_RELEASE); }
    bool ok() const { return ok_; }
    Blokada(const Blokada&) = delete;
    Blokada& operator=(const Blokada&) = delete;
private:
    bool ok_;
};

void hak_planisty(Pcm::Owner o, BwsPcmStats& st) {
    PcmSchedDiag::PidStats s{};
    if (PcmSchedDiag::snapshot_pid(o.pid, s)) {
        st.max_runnable_wait_ns = s.max_runnable_wait_ns;
        st.max_voluntary_wait_ns = s.max_voluntary_wait_ns;
        st.max_exec_gap_ns = s.max_exec_gap_ns;
    }
}
void hak_otwarcia(uint32_t pid) { PcmSchedDiag::track_pid(pid); }

Ac97::OpisPci opis(uint8_t bus, uint8_t slot, uint8_t func) {
    const uint32_t id = pci_odczytaj_dword(bus, slot, func, 0x00);
    const uint32_t klasa = pci_odczytaj_dword(bus, slot, func, 0x08);
    return Ac97::OpisPci{bus, slot, func, uint16_t(id), uint16_t(id >> 16), uint8_t(klasa >> 24), uint8_t(klasa >> 16),
                         pci_odczytaj_dword(bus, slot, func, PCI_OFFSET_BAR0), pci_odczytaj_dword(bus, slot, func, PCI_OFFSET_BAR1)};
}

// Pierwszy kontroler 04:01 o obslugiwanym ID; tylko odczyty konfiguracji.
bool znajdz(Ac97::OpisPci* out) {
    for (uint32_t bus = 0; bus < PCI_LICZBA_MAGISTRAL; ++bus) {
        for (uint8_t slot = 0; slot < PCI_LICZBA_SLOTOW; ++slot) {
            const uint32_t id0 = pci_odczytaj_dword(uint8_t(bus), slot, 0, 0x00);
            if ((id0 & 0xFFFFu) == PCI_VENDOR_BRAK) continue;
            const bool wielofunkcyjne = (pci_odczytaj_dword(uint8_t(bus), slot, 0, PCI_OFFSET_HEADER_INFO) >> 16) & 0x80u;
            for (uint8_t func = 0; func < (wielofunkcyjne ? PCI_LICZBA_FUNKCJI : 1); ++func) {
                const Ac97::OpisPci o = opis(uint8_t(bus), slot, func);
                if (o.vendor == PCI_VENDOR_BRAK) continue;
                if (o.klasa != Ac97::PCI_KLASA_MULTIMEDIA || o.podklasa != Ac97::PCI_PODKLASA_AUDIO) continue;
                if (!Ac97::znajdz_id(o.vendor, o.device)) continue;
                *out = o;
                return true;
            }
        }
    }
    return false;
}

void log_blad(const char* prefiks, Ac97::Blad b) {
    char t[128] = {};
    size_t n = 0;
    for (const char* s = prefiks; *s && n + 1 < sizeof(t); ++s) t[n++] = *s;
    for (const char* s = Ac97::nazwa_bledu(b); *s && n + 1 < sizeof(t); ++s) t[n++] = *s;
    wypisz_log(t);
}

} // namespace

bool ac97_wykryj() {
    Ac97::OpisPci o{};
    g_wykryty = znajdz(&o);
    if (g_wykryty) g_opis = o;
    return g_wykryty;
}

bool inicjalizuj_ac97() {
    Blokada b;
    if (!b.ok()) return false;
    if (!hpet_dostepny()) { wypisz_log("[AC97] Brak HPET - sterownik wymaga zegara monotonicznego."); return false; }
    if (!g_wykryty && !ac97_wykryj()) { wypisz_log("[AC97] Brak kontrolera Intel ICH AC97 na PCI."); return false; }
    g_platforma.bus = g_opis.bus; g_platforma.slot = g_opis.slot; g_platforma.func = g_opis.func;
    Ac97::Blad e = g_kontroler.sonduj(g_opis);
    if (e == Ac97::Blad::Brak) e = g_kontroler.inicjalizuj();
    if (e != Ac97::Blad::Brak) {
        log_blad("[AC97] Inicjalizacja nieudana: ", e);
        // Po awarii inicjalizacji DMA nie bylo uruchomione - mozna bezpiecznie zwolnic zasoby.
        (void)g_kontroler.wylacz();
        return false;
    }
    g_silnik.ustaw_haki(hak_planisty, hak_otwarcia);
    g_aktywny = true;
    wypisz_log("[AC97] Kontroler Intel ICH AC97 gotowy: PCM Out 48000 Hz, stereo PCM16, 32 wpisy BDL.");
    return true;
}

bool ac97_wylacz() {
    Blokada b;
    if (!b.ok()) return false;
    if (!g_kontroler.wylacz()) { wypisz_log("[AC97] DMA nie zatrzymalo sie - bufory pozostaja przydzielone."); return false; }
    g_aktywny = false;
    return true;
}

void ac97_zaplanuj_prace() {
    if (!__atomic_load_n(&g_aktywny, __ATOMIC_ACQUIRE) || !hpet_dostepny()) return;
    uint64_t pusty = 0;
    const uint64_t teraz = czas_monotoniczny_ns();
    (void)__atomic_compare_exchange_n(&g_oczekuje_ns, &pusty, teraz ? teraz : 1, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

bool ac97_praca_oczekuje() { return __atomic_load_n(&g_oczekuje_ns, __ATOMIC_ACQUIRE) != 0; }

void ac97_obsluz_okresowo() {
    if (!__atomic_load_n(&g_aktywny, __ATOMIC_ACQUIRE)) return;
    uint64_t flagi;
    asm volatile("pushfq; popq %0" : "=r"(flagi) :: "memory");
    if (!(flagi & (1ULL << 9))) return;                     // tylko z IF=1 (jak HDA)
    Blokada b;
    if (!b.ok()) return;
    __atomic_store_n(&g_oczekuje_ns, 0, __ATOMIC_RELEASE);
    g_silnik.obsluz(czas_monotoniczny_ns());
}

bool ac97_migawka(Ac97Migawka* out) {
    if (!out) return false;
    Blokada b;
    if (!b.ok()) return false;
    *out = {};
    out->wykryty = g_wykryty;
    out->aktywny = g_aktywny;
    out->kontroler = g_kontroler.diag();
    out->pcm = g_silnik.statystyki_silnika();
    out->bus = g_opis.bus; out->slot = g_opis.slot; out->func = g_opis.func;
    return true;
}

void ac97_glosnosc_info(HdaGlosnoscInfo* out) {
    if (!out) return;
    *out = {};
    Blokada b;
    const Ac97::Diagnostyka& d = g_kontroler.diag();
    out->urzadzenie = g_aktywny && g_kontroler.gotowy();
    out->wyciszanie = out->urzadzenie;
    out->wyciszony = d.wyciszony != 0;
    out->odtwarza = g_kontroler.odtwarza();
    out->backend = out->urzadzenie ? HDA_GLOSNOSC_WZMACNIACZ : HDA_GLOSNOSC_BRAK;
    out->procent = d.glosnosc;
    out->generacja = g_generacja_glosnosci;
    out->pci_vendor = d.vendor; out->pci_device = d.device;
    out->kroki = d.kroki_glosnosci;
}

uint32_t ac97_glosnosc_ustaw(uint32_t procent) {
    if (procent > 100) return HDA_GLOSNOSC_ZLE_ARGUMENTY;
    Blokada b;
    if (!b.ok()) return HDA_GLOSNOSC_ZAJETE;
    if (!g_aktywny) return HDA_GLOSNOSC_NIEOBSLUGIWANE;
    if (!g_kontroler.ustaw_glosnosc(procent, g_kontroler.diag().wyciszony != 0)) return HDA_GLOSNOSC_ZAJETE;
    ++g_generacja_glosnosci;
    return HDA_GLOSNOSC_OK;
}

uint32_t ac97_wyciszenie_ustaw(bool wyciszony) {
    Blokada b;
    if (!b.ok()) return HDA_GLOSNOSC_ZAJETE;
    if (!g_aktywny) return HDA_GLOSNOSC_NIEOBSLUGIWANE;
    if (!g_kontroler.ustaw_glosnosc(g_kontroler.diag().glosnosc, wyciszony)) return HDA_GLOSNOSC_ZAJETE;
    ++g_generacja_glosnosci;
    return HDA_GLOSNOSC_OK;
}

namespace Ac97Pcm {
int64_t open(Pcm::Owner o, const BwsPcmFormat& f) {
    Blokada b; if (!b.ok()) return PCM_BUSY;
    return g_silnik.open(o, f);
}
int64_t write(Pcm::Owner o, uint64_t h, const int16_t* p, uint32_t n) {
    Blokada b; if (!b.ok()) return PCM_BUSY;
    return g_silnik.write(o, h, p, n, czas_monotoniczny_ns());
}
int64_t query(Pcm::Owner o, uint64_t h, BwsPcmState& s) {
    Blokada b; if (!b.ok()) return PCM_BUSY;
    return g_silnik.query(o, h, s);
}
int64_t statistics(Pcm::Owner o, uint64_t h, BwsPcmStats& s) {
    Blokada b; if (!b.ok()) return PCM_BUSY;
    return g_silnik.statistics(o, h, s);
}
int64_t gain(Pcm::Owner o, uint64_t h, uint64_t v, uint64_t m) {
    Blokada b; if (!b.ok()) return PCM_BUSY;
    return g_silnik.gain(o, h, v, m);
}
int64_t close(Pcm::Owner o, uint64_t h) {
    Blokada b; if (!b.ok()) return PCM_BUSY;
    return g_silnik.close(o, h, czas_monotoniczny_ns());
}
int64_t finish(Pcm::Owner o, uint64_t h) {
    Blokada b; if (!b.ok()) return PCM_BUSY;
    return g_silnik.finish(o, h);
}
bool posprzataj(uint32_t pid, uint64_t gen) {
    Blokada b; if (!b.ok()) return false;
    g_silnik.posprzataj({pid, gen}, czas_monotoniczny_ns());
    return true;
}
bool diag_stream(uint32_t pid, uint64_t gen, BwsPcmStats& out) {
    Blokada b; if (!b.ok()) return false;
    return g_silnik.statystyki_koncowe(pid, gen, out);
}
bool diag_starvation(uint32_t i, Pcm::Starvation& out) {
    Blokada b; if (!b.ok()) return false;
    return g_silnik.glod(i, out);
}
} // namespace Ac97Pcm
