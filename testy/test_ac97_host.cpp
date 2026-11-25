// Host: sterownik Intel ICH AC97 (sterowniki/dzwiek/ac97_rdzen.h + ac97_pcm.h) na emulatorze kontrolera i kodeka.
// Emulator modeluje konfiguracje PCI, NAMBAR/NABMBAR, CAS, zimny reset, kanal PCM Out (BDBAR/CIV/LVI/SR/PICB/CR),
// silnik DMA czytajacy bufory z "pamieci fizycznej" w czasie wirtualnym oraz wstrzykiwanie bledow. Odtworzone
// probki sa porownywane z wejsciem miksera (licznik w probkach), wiec duplikaty, pominiecia i nadpisanie bufora
// nalezacego do sprzetu sa wykrywane. To NIE jest dowod dzialania na prawdziwym sprzecie ani w QEMU.
#include "sterowniki/dzwiek/ac97_pcm.h"
#include "jadro/dzwiek/wyjscie_audio_polityka.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

using namespace Ac97;

constexpr uint16_t NAM = 0x1000, NABM = 0x1400;

struct Emulator {
    // --- konfiguracja i wstrzykiwanie ---
    uint16_t vendor = 0x8086, device = 0x2415;
    uint8_t klasa = 0x04, podklasa = 0x01;
    bool vra = false, vra_odrzuca = false, master_6bit = true;
    bool pcr_nigdy = false, powerdown_nigdy = false, cas_zablokowany = false, rcs_przy_odczycie = false;
    bool bus_master_zablokowany = false, rr_nie_konczy = false, dch_nigdy = false, zamrozony = false;
    bool fifo_raz = false;
    uint32_t pcr_po_odczytach = 3, pd_po_odczytach = 2;
    // --- stan ---
    uint32_t cfg[64]{};
    uint16_t kodek[64]{};
    uint32_t glob_cnt = 0, glob_sta = 0;
    bool cas = false, zimny_reset_zakonczony = false;
    uint32_t odczyty_sta = 0, odczyty_pd = 0;
    uint32_t bdbar = 0; uint8_t civ = 0, lvi = 0, piv = 0, cr = 0; uint16_t sr = SR_DCH, picb = 0;
    uint32_t pozycja_w_buforze = 0;   // probki z biezacego bufora juz odtworzone
    std::map<uint64_t, uint8_t*> pamiec;  // fizyczny -> host
    std::vector<int16_t> wyjscie;
    uint64_t probki_ciszy_po_zatrzymaniu = 0;
    uint64_t naruszenia_wlasnosci = 0, zapisy_lvi = 0;
    uint64_t sumy[WPISY_BDL]{};
    bool w_oknie[WPISY_BDL]{};
    uint64_t zapisy_rate = 0;
    double reszta = 0;

    Emulator() { reset_pci(); reset_kodeka(); }
    void reset_pci() {
        std::memset(cfg, 0, sizeof(cfg));
        cfg[0] = uint32_t(device) << 16 | vendor;
        cfg[2] = uint32_t(klasa) << 24 | uint32_t(podklasa) << 16 | 0x01;
        cfg[4] = NAM | 1; cfg[5] = NABM | 1;
    }
    OpisPci opis() const {
        return OpisPci{0, 5, 0, uint16_t(cfg[0]), uint16_t(cfg[0] >> 16), uint8_t(cfg[2] >> 24), uint8_t(cfg[2] >> 16), cfg[4], cfg[5]};
    }
    void reset_kodeka() {
        std::memset(kodek, 0, sizeof(kodek));
        kodek[NAM_MASTER / 2] = 0x8000; kodek[NAM_PCM_OUT / 2] = 0x8808;
        kodek[NAM_EXT_ID / 2] = vra ? EXT_VRA : 0;
        kodek[NAM_PCM_FRONT_RATE / 2] = 48000;
        kodek[NAM_VENDOR1 / 2] = 0x8384; kodek[NAM_VENDOR2 / 2] = 0x7600;
        odczyty_pd = 0;
    }
    void reset_kanalu() { bdbar = 0; civ = lvi = piv = 0; sr = SR_DCH; picb = 0; cr = 0; pozycja_w_buforze = 0;
        for (auto& w : w_oknie) w = false; }

    WpisBdl wpis(uint32_t i) {
        WpisBdl w{};
        uint8_t* b = host(bdbar + i * 8, 8);
        if (b) std::memcpy(&w, b, 8);
        return w;
    }
    uint8_t* host(uint64_t fiz, uint32_t n) {
        for (auto& [baza, ptr] : pamiec) {
            if (fiz >= baza && fiz + n <= baza + 0x40000) return ptr + (fiz - baza);
        }
        return nullptr;
    }
    uint64_t suma(uint32_t i) {
        WpisBdl w = wpis(i);
        uint8_t* b = host(w.adres, uint32_t(w.probki) * 2);
        uint64_t s = 1469598103934665603ULL;
        for (uint32_t k = 0; b && k < uint32_t(w.probki) * 2; ++k) s = (s ^ b[k]) * 1099511628211ULL;
        return s ^ w.probki ^ (uint64_t(w.adres) << 20);
    }
    void zaladuj_biezacy() { picb = wpis(civ).probki; pozycja_w_buforze = 0; }

    // --- porty ---
    uint8_t in8(uint16_t p) {
        if (p == NABM + NABM_CAS) { if (cas_zablokowany) return 1; const bool b = cas; cas = true; return b ? 1 : 0; }
        if (p == NABM + PO_BASE + BOX_CIV) return civ;
        if (p == NABM + PO_BASE + BOX_LVI) return lvi;
        if (p == NABM + PO_BASE + BOX_PIV) return piv;
        if (p == NABM + PO_BASE + BOX_CR) { if ((cr & CR_RR) && !rr_nie_konczy) { reset_kanalu(); } return cr; }
        return 0xFF;
    }
    uint16_t in16(uint16_t p) {
        if (p >= NAM && p < NAM + 256) {
            cas = false;
            const uint16_t r = p - NAM;
            if (rcs_przy_odczycie) glob_sta |= GS_RCS;
            if (r == NAM_POWERDOWN) return (powerdown_nigdy || ++odczyty_pd < pd_po_odczytach) ? 0 : 0x000F;
            if (r == NAM_RESET) return 0x0D50;
            return kodek[r / 2];
        }
        if (p == NABM + PO_BASE + BOX_SR) return sr;
        if (p == NABM + PO_BASE + BOX_PICB) return picb;
        return 0xFFFF;
    }
    uint32_t in32(uint16_t p) {
        if (p == NABM + NABM_GLOB_STA) {
            if (zimny_reset_zakonczony && !pcr_nigdy && ++odczyty_sta >= pcr_po_odczytach) glob_sta |= GS_PCR;
            return glob_sta;
        }
        if (p == NABM + NABM_GLOB_CNT) return glob_cnt;
        if (p == NABM + PO_BASE + BOX_BDBAR) return bdbar;
        return 0xFFFFFFFF;
    }
    void out8(uint16_t p, uint8_t v) {
        if (p == NABM + PO_BASE + BOX_LVI) {
            ++zapisy_lvi;
            const uint8_t nowy = v & 31;
            // Kazdy wpis w oknie sprzetu musi byc niezmieniony od jego publikacji.
            for (uint32_t i = 0; i < WPISY_BDL; ++i) if (w_oknie[i] && suma(i) != sumy[i]) ++naruszenia_wlasnosci;
            if (w_oknie[nowy] && nowy != lvi) ++naruszenia_wlasnosci;   // publikacja wpisu juz nalezacego do sprzetu
            if (nowy != lvi)
                for (uint32_t i = (lvi + 1) & 31; ; i = (i + 1) & 31) { sumy[i] = suma(i); w_oknie[i] = true; if (i == nowy) break; }
            lvi = nowy;
            return;
        }
        if (p == NABM + PO_BASE + BOX_CR) {
            if (v & CR_RR) { cr = CR_RR; if (!rr_nie_konczy) reset_kanalu(); return; }
            const bool start = (v & CR_RPBM) && !(cr & CR_RPBM);
            const bool stop = !(v & CR_RPBM) && (cr & CR_RPBM);
            cr = v;
            if (start) { sr &= uint16_t(~SR_DCH); sumy[civ] = suma(civ); w_oknie[civ] = true; zaladuj_biezacy(); }
            if (stop && !dch_nigdy) sr |= SR_DCH;
            return;
        }
    }
    void out16(uint16_t p, uint16_t v) {
        if (p >= NAM && p < NAM + 256) {
            cas = false;
            const uint16_t r = p - NAM;
            if (r == NAM_RESET) { reset_kodeka(); return; }
            if (r == NAM_MASTER) { uint16_t l = (v >> 8) & 0x3F, pr = v & 0x3F;
                if (!master_6bit) { if (l & 0x20) l = 0x1F; if (pr & 0x20) pr = 0x1F; }
                kodek[r / 2] = uint16_t((v & 0x8000) | l << 8 | pr); return; }
            if (r == NAM_PCM_FRONT_RATE) { ++zapisy_rate; if ((kodek[NAM_EXT_CTRL / 2] & EXT_VRA) && !vra_odrzuca) kodek[r / 2] = v; else if (vra_odrzuca) kodek[r / 2] = 44100; return; }
            kodek[r / 2] = v;
            return;
        }
        if (p == NABM + PO_BASE + BOX_SR) { sr &= uint16_t(~(v & SR_W1C)); return; }
    }
    void out32(uint16_t p, uint32_t v) {
        if (p == NABM + NABM_GLOB_CNT) {
            if (!(v & GC_COLD)) { glob_sta = 0; zimny_reset_zakonczony = false; reset_kodeka(); reset_kanalu(); }
            else if (!(glob_cnt & GC_COLD)) { zimny_reset_zakonczony = true; odczyty_sta = 0; }
            glob_cnt = v; return;
        }
        if (p == NABM + NABM_GLOB_STA) { glob_sta &= ~(v & (GS_RCS | GS_GSCI)); return; }
        if (p == NABM + PO_BASE + BOX_BDBAR) { bdbar = v & ~7u; return; }
    }
    uint32_t pci_czytaj32(uint8_t off) { return cfg[off / 4]; }
    void pci_zapisz32(uint8_t off, uint32_t v) {
        if (off == 0x04) { uint32_t c = v & 0x0507; if (bus_master_zablokowany) c &= ~0x4u; cfg[1] = (cfg[1] & 0xFFFF0000u) | c; return; }
        if (off == 0x40) { cfg[0x10] = v; return; }
    }

    // --- DMA w czasie wirtualnym ---
    void postep(uint64_t ns) {
        if (zamrozony || !(cr & CR_RPBM) || (sr & SR_DCH)) {
            if ((cr & CR_RPBM) && (sr & SR_DCH)) probki_ciszy_po_zatrzymaniu += ns * 96000ULL / 1000000000ULL;
            return;
        }
        reszta += double(ns) * 96000.0 / 1e9;           // 48 kHz x 2 probki
        uint64_t n = uint64_t(reszta); reszta -= double(n);
        while (n) {
            if (sr & SR_DCH) { probki_ciszy_po_zatrzymaniu += n; break; }
            WpisBdl w = wpis(civ);
            if (fifo_raz) { sr |= SR_FIFOE; fifo_raz = false; }
            const uint32_t zostalo = w.probki - pozycja_w_buforze;
            const uint32_t krok = uint32_t(n < zostalo ? n : zostalo);
            int16_t* b = reinterpret_cast<int16_t*>(host(w.adres, uint32_t(w.probki) * 2));
            for (uint32_t k = 0; k < krok; ++k) wyjscie.push_back(b ? b[pozycja_w_buforze + k] : 0);
            pozycja_w_buforze += krok; n -= krok;
            picb = uint16_t(w.probki - pozycja_w_buforze);
            if (pozycja_w_buforze < w.probki) continue;
            if (suma(civ) != sumy[civ]) ++naruszenia_wlasnosci;   // bufor zmieniony w trakcie odtwarzania
            w_oknie[civ] = false;
            if (w.flagi & BDL_IOC) sr |= SR_BCIS;
            if (civ == lvi) { sr |= SR_LVBCI | SR_CELV | SR_DCH; pozycja_w_buforze = 0; picb = 0; continue; }
            civ = (civ + 1) & 31; piv = (civ + 1) & 31; sr &= uint16_t(~SR_CELV);
            zaladuj_biezacy();
        }
    }
};

struct Platforma {
    Emulator& e;
    uint64_t czas = 1000000;
    uint64_t nastepny_fiz = 0x00200000;
    int alokacje = 0;
    bool dma_awaria = false;
    std::vector<void*> bloki;
    explicit Platforma(Emulator& em) : e(em) {}
    ~Platforma() { for (void* b : bloki) std::free(b); }
    uint8_t in8(uint16_t p) { return e.in8(p); }
    uint16_t in16(uint16_t p) { return e.in16(p); }
    uint32_t in32(uint16_t p) { return e.in32(p); }
    void out8(uint16_t p, uint8_t v) { e.out8(p, v); }
    void out16(uint16_t p, uint16_t v) { e.out16(p, v); }
    void out32(uint16_t p, uint32_t v) { e.out32(p, v); }
    uint32_t pci_czytaj32(uint8_t o) { return e.pci_czytaj32(o); }
    void pci_zapisz32(uint8_t o, uint32_t v) { e.pci_zapisz32(o, v); }
    uint64_t teraz_ns() { return czas; }
    void czekaj_us(uint32_t us) { czas += uint64_t(us) * 1000; }
    void bariera() {}
    bool dma_przydziel(uint32_t rozmiar, Dma* out) {
        if (dma_awaria) return false;
        void* p = std::aligned_alloc(4096, (rozmiar + 4095) & ~4095u);
        if (!p) return false;
        std::memset(p, 0, rozmiar);
        bloki.push_back(p);
        *out = {p, nastepny_fiz, rozmiar};
        e.pamiec[nastepny_fiz] = static_cast<uint8_t*>(p);
        nastepny_fiz += 0x40000;
        ++alokacje;
        return true;
    }
    void dma_zwolnij(Dma* d) {
        e.pamiec.erase(d->fizyczny);
        for (auto& b : bloki) if (b == d->wirtualny) { std::free(b); b = nullptr; }
        --alokacje;
        *d = {};
    }
};

// Uruchamia sterownik do stanu Gotowy.
struct Stanowisko {
    Emulator e;
    Platforma p{e};
    Kontroler<Platforma> k{p};
    SilnikPcm<Platforma> s{k};
    Blad init() {
        e.reset_pci();
        const Blad b = k.sonduj(e.opis());
        if (b != Blad::Brak) return b;
        return k.inicjalizuj();
    }
    // Wirtualny czas: DMA postepuje, praca odroczona co `co_ns`.
    void biegnij(uint64_t ns, uint64_t co_ns, bool obsluga = true) {
        for (uint64_t t = 0; t < ns; t += co_ns) {
            e.postep(co_ns); p.czas += co_ns;
            if (obsluga) s.obsluz(p.czas);
        }
    }
};

static void test_pci() {
    Emulator e;
    uint16_t nam = 0, nabm = 0;
    CHECK(waliduj_pci(e.opis(), &nam, &nabm) == Blad::Brak && nam == NAM && nabm == NABM);
    OpisPci o = e.opis();
    o.vendor = 0x1022; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyVendor);
    o = e.opis(); o.device = 0x2668; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::NieobslugiwaneUrzadzenie);
    o = e.opis(); o.podklasa = 0x03; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlaKlasa);   // HDA 04:03
    o = e.opis(); o.klasa = 0x02; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlaKlasa);
    o = e.opis(); o.bar0 = 0xFEB00000; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyBarNam);   // BAR pamieciowy
    o = e.opis(); o.bar0 = 1; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyBarNam);            // adres 0
    o = e.opis(); o.bar0 = 0x1081; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyBarNam);       // brak wyrownania 256
    o = e.opis(); o.bar0 = 0x1FF01; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyBarNam);      // poza 64 KiB
    o = e.opis(); o.bar1 = 0x1411; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyBarNabm);
    o = e.opis(); o.bar1 = 0x1041; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyBarNabm);      // naklada sie na NAM
    o = e.opis(); o.bar1 = 0; CHECK(waliduj_pci(o, &nam, &nabm) == Blad::ZlyBarNabm);
    for (uint16_t id : {0x2415, 0x2425, 0x2445, 0x2485, 0x24C5, 0x24D5, 0x266E, 0x27DE}) CHECK(znajdz_id(0x8086, id));
    CHECK(!znajdz_id(0x8086, 0x293E));
    CHECK(znajdz_id(0x8086, 0x24C5)->ich4 && !znajdz_id(0x8086, 0x2415)->ich4);
}

static void test_inicjalizacja() {
    {
        Stanowisko t;
        CHECK(t.init() == Blad::Brak);
        CHECK(t.k.stan() == Stan::Gotowy);
        CHECK((t.e.cfg[1] & 0x0405) == 0x0405);          // I/O + bus master + INTx disable
        CHECK(t.e.glob_cnt & GC_COLD);
        CHECK(t.e.kodek[NAM_PCM_OUT / 2] == PCM_OUT_0DB);
        CHECK(t.k.diag().kodek_vendor1 == 0x8384 && t.k.diag().tlumienie_6bit == 1 && t.k.diag().kroki_glosnosci == 64);
        CHECK(t.e.zapisy_rate == 0);                     // bez VRA: brak zapisu czestotliwosci
        CHECK(t.p.alokacje == 2);
        CHECK(t.k.diag().bdl_fizyczny < 0x100000000ULL && t.k.diag().bufory_fizyczne < 0x100000000ULL);
        CHECK(t.k.inicjalizuj() == Blad::ZlyStan);       // ponowna inicjalizacja bez sondowania odrzucona
    }
    { Stanowisko t; t.e.master_6bit = false; CHECK(t.init() == Blad::Brak); CHECK(t.k.diag().kroki_glosnosci == 32);
      CHECK(t.k.ustaw_glosnosc(100, false) && (t.e.kodek[NAM_MASTER / 2] & 0x9F1F) == 0x0000);
      CHECK(t.k.ustaw_glosnosc(0, false) && (t.e.kodek[NAM_MASTER / 2] & 0x8000));
      CHECK(t.k.ustaw_glosnosc(50, false) && t.e.kodek[NAM_MASTER / 2] == 0x1010);
      CHECK(t.k.ustaw_glosnosc(80, true) && (t.e.kodek[NAM_MASTER / 2] & 0x8000));
      CHECK(!t.k.ustaw_glosnosc(101, false)); }
    { Stanowisko t; CHECK(t.init() == Blad::Brak);
      CHECK(t.k.ustaw_glosnosc(50, false) && t.e.kodek[NAM_MASTER / 2] == 0x2020); }
    { Stanowisko t; t.e.vra = true; CHECK(t.init() == Blad::Brak); CHECK(t.k.diag().vra == 1 && t.e.kodek[NAM_PCM_FRONT_RATE / 2] == 48000 && t.e.zapisy_rate == 1); }
    { Stanowisko t; t.e.vra = true; t.e.vra_odrzuca = true; CHECK(t.init() == Blad::Czestotliwosc); CHECK(t.k.stan() == Stan::Awaria && t.p.alokacje == 0); }
    { Stanowisko t; t.e.pcr_nigdy = true; CHECK(t.init() == Blad::KodekNieGotowy); CHECK(t.p.alokacje == 0); }
    { Stanowisko t; t.e.powerdown_nigdy = true; CHECK(t.init() == Blad::Powerdown); }
    { Stanowisko t; t.e.cas_zablokowany = true; CHECK(t.init() == Blad::DostepKodeka); CHECK(t.k.diag().bledy_cas > 0); }
    { Stanowisko t; t.e.rcs_przy_odczycie = true; CHECK(t.init() == Blad::DostepKodeka); CHECK(t.k.diag().bledy_odczytu_kodeka > 0); }
    { Stanowisko t; t.e.bus_master_zablokowany = true; CHECK(t.init() == Blad::KomendaPci); }
    { Stanowisko t; t.p.dma_awaria = true; CHECK(t.init() == Blad::Dma); CHECK(t.p.alokacje == 0); }
    { Stanowisko t; t.p.nastepny_fiz = 0xFFFC0000ULL; CHECK(t.init() == Blad::DmaPoza4GiB); CHECK(t.p.alokacje == 0); }
    { Stanowisko t; t.p.nastepny_fiz = 0x100000000ULL; CHECK(t.init() == Blad::DmaPoza4GiB); CHECK(t.p.alokacje == 0); }
    { Stanowisko t; t.e.rr_nie_konczy = true; CHECK(t.init() == Blad::ResetKanalu); CHECK(t.p.alokacje == 0); }
    { Stanowisko t; t.e.device = 0x24C5; t.e.reset_pci(); CHECK(t.init() == Blad::Brak); CHECK(((t.e.cfg[0x10] >> 8) & 1) == 1); }  // ICH4 IOSE
    { Stanowisko t; t.e.klasa = 0x04; t.e.podklasa = 0x03; t.e.reset_pci(); CHECK(t.init() == Blad::ZlaKlasa); CHECK(t.e.cfg[1] == 0); }  // bez zapisow PCI
}

// Licznik w probkach: lewy = n, prawy = -n (pomijamy 0, by odroznic cisze).
static std::vector<int16_t> sygnal(uint32_t od, uint32_t ramki) {
    std::vector<int16_t> v;
    for (uint32_t i = 0; i < ramki; ++i) { int16_t x = int16_t(((od + i) % 30000) + 1); v.push_back(x); v.push_back(int16_t(-x)); }
    return v;
}

// Sprawdza, ze niezerowe ramki wyjscia tworza ciagla sekwencje 1..N bez duplikatow i pominiec.
static bool ciagle(const std::vector<int16_t>& w, uint32_t oczekiwane, uint32_t* znalezione = nullptr) {
    uint32_t n = 0;
    for (size_t i = 0; i + 1 < w.size(); i += 2) {
        if (w[i] == 0 && w[i + 1] == 0) continue;
        const int16_t x = int16_t((n % 30000) + 1);
        if (w[i] != x || w[i + 1] != -x) { std::fprintf(stderr, "  ramka %u: %d/%d zamiast %d\n", n, w[i], w[i + 1], x); return false; }
        ++n;
    }
    if (znalezione) *znalezione = n;
    return n == oczekiwane;
}

static void podaj(Stanowisko& t, Pcm::Owner o, uint64_t h, uint32_t& wyslane, uint32_t cel) {
    while (wyslane < cel) {
        const uint32_t porcja = cel - wyslane < 512 ? cel - wyslane : 512;
        auto v = sygnal(wyslane, porcja);
        const int64_t r = t.s.write(o, h, v.data(), porcja, t.p.czas);
        if (r == PCM_FULL) break;
        CHECK(r > 0);
        wyslane += uint32_t(r);
    }
}

static void test_odtwarzanie() {
    Stanowisko t;
    CHECK(t.init() == Blad::Brak);
    const Pcm::Owner o{7, 1};
    const BwsPcmFormat f{BWS_PCM_V1, sizeof(BwsPcmFormat), 48000, 2, Pcm::FORMAT_S16, 0};
    const int64_t h = t.s.open(o, f);
    CHECK(h > 0);
    const uint32_t razem = 48000 * 3;   // 3 s, ok. 140 okresow (ponad 4 obiegi 32-wpisowego BDL)
    uint32_t wyslane = 0;
    for (uint64_t t_ns = 0; t_ns < 4000000000ULL; t_ns += 5000000) {
        podaj(t, o, uint64_t(h), wyslane, razem);
        t.biegnij(5000000, 5000000);
    }
    CHECK(wyslane == razem);
    CHECK(t.k.diag().okresy > 3 * WPISY_BDL);            // zawijanie pierscienia
    CHECK(t.e.naruszenia_wlasnosci == 0);
    CHECK(t.k.diag().niedobory == 0 && t.k.diag().odzyskania == 0);
    CHECK(t.k.diag().statusy_ioc > 0);
    CHECK((t.e.sr & SR_BCIS) == 0 || t.k.odtwarza());     // RW1C potwierdzane
    CHECK(t.k.diag().w_sprzecie <= WYPRZEDZENIE);
    uint32_t n = 0;
    CHECK(ciagle(t.e.wyjscie, razem, &n));
    // Po wyczerpaniu danych silnik nadal dziala (strumien otwarty), generujac cisze i liczac braki.
    CHECK(t.k.odtwarza());
    // Zamkniecie -> stop DMA po 256 ms bezczynnosci (DCH potwierdzone), bufory nadal przydzielone.
    CHECK(t.s.close(o, uint64_t(h), t.p.czas) == PCM_OK);
    t.biegnij(400000000, 5000000);
    CHECK(!t.k.odtwarza() && t.k.stan() == Stan::Gotowy && !t.k.sprzet_posiada_dma());
    CHECK((t.e.cr & CR_RPBM) == 0 && (t.e.sr & SR_DCH));
    CHECK(t.s.statystyki_silnika().stopy_bezczynnosci == 1);
    BwsPcmStats st{};
    CHECK(t.s.statystyki_koncowe(7, 1, st) && st.consumed == razem);
    // Restart: nowy strumien gra od nowa.
    t.e.wyjscie.clear();
    const int64_t h2 = t.s.open(o, f);
    CHECK(h2 > h);
    wyslane = 0;
    for (int i = 0; i < 300; ++i) { podaj(t, o, uint64_t(h2), wyslane, 48000); t.biegnij(5000000, 5000000); }
    CHECK(ciagle(t.e.wyjscie, 48000));
    CHECK(t.k.diag().starty == 2);
    CHECK(t.e.naruszenia_wlasnosci == 0);
}

static void test_niedobor_i_bledy() {
    // Brak obslugi przez 200 ms (> 85 ms w sprzecie): kontroler zatrzymuje sie (DCH), sterownik restartuje
    // kanal; dane miksera nie sa tracone ani powtarzane.
    Stanowisko t;
    CHECK(t.init() == Blad::Brak);
    const Pcm::Owner o{9, 3};
    const BwsPcmFormat f{BWS_PCM_V1, sizeof(BwsPcmFormat), 48000, 2, Pcm::FORMAT_S16, 0};
    const int64_t h = t.s.open(o, f);
    uint32_t wyslane = 0;
    for (int i = 0; i < 100; ++i) { podaj(t, o, uint64_t(h), wyslane, 96000); t.biegnij(5000000, 5000000); }
    t.biegnij(200000000, 5000000, false);
    CHECK(t.e.sr & SR_DCH);
    for (int i = 0; i < 600; ++i) { podaj(t, o, uint64_t(h), wyslane, 96000); t.biegnij(5000000, 5000000); }
    CHECK(t.k.diag().niedobory >= 1 && t.k.diag().odzyskania >= 1);
    CHECK(t.k.diag().spoznienia >= 1 && t.k.diag().maks_luka_ns >= 200000000ULL);
    CHECK(t.e.probki_ciszy_po_zatrzymaniu > 0);
    CHECK(ciagle(t.e.wyjscie, 96000));
    CHECK(t.e.naruszenia_wlasnosci == 0);
    // FIFOE: liczony i potwierdzany (RW1C).
    t.e.fifo_raz = true;
    t.biegnij(30000000, 5000000);
    CHECK(t.k.diag().bledy_fifo == 1 && (t.e.sr & SR_FIFOE) == 0);
    // Zastoj: CIV/PICB bez ruchu -> restart; po MAKS_ODZYSKAN_POD_RZAD restartach bez postepu - awaria.
    t.e.zamrozony = true;
    for (int i = 0; i < 400 && t.k.stan() != Stan::Awaria; ++i) { podaj(t, o, uint64_t(h), wyslane, wyslane + 1024); t.biegnij(5000000, 5000000); }
    CHECK(t.k.diag().zastoje >= 1);
    CHECK(t.k.stan() == Stan::Awaria);
    CHECK(t.s.write(o, uint64_t(h), sygnal(0, 4).data(), 4, t.p.czas) == PCM_DEVICE);
    CHECK(t.s.open({10, 1}, f) == PCM_DEVICE);
}

static void test_pozycja_i_stop() {
    {   // CIV poza oknem sprzetu (CIV > LVI) -> odzyskanie przez restart kanalu
        Stanowisko t; CHECK(t.init() == Blad::Brak);
        const Pcm::Owner o{4, 4};
        const BwsPcmFormat f{BWS_PCM_V1, sizeof(BwsPcmFormat), 48000, 2, Pcm::FORMAT_S16, 0};
        const int64_t h = t.s.open(o, f); uint32_t w = 0;
        for (int i = 0; i < 40; ++i) { podaj(t, o, uint64_t(h), w, 48000); t.biegnij(5000000, 5000000); }
        CHECK(t.k.odtwarza());
        t.e.civ = uint8_t((t.e.lvi + 3) & 31);
        t.biegnij(5000000, 5000000);
        CHECK(t.k.diag().bledy_pozycji == 1 && t.k.odtwarza());
    }
    {   // Stop bez DCH: bufory zostaja (sprzet moze je czytac), wylaczenie odmawia zwolnienia pamieci.
        Stanowisko t; CHECK(t.init() == Blad::Brak);
        const Pcm::Owner o{5, 5};
        const BwsPcmFormat f{BWS_PCM_V1, sizeof(BwsPcmFormat), 48000, 2, Pcm::FORMAT_S16, 0};
        const int64_t h = t.s.open(o, f); uint32_t w = 0;
        for (int i = 0; i < 40; ++i) { podaj(t, o, uint64_t(h), w, 48000); t.biegnij(5000000, 5000000); }
        CHECK(t.k.odtwarza());
        t.e.dch_nigdy = true;
        CHECK(!t.k.zatrzymaj());
        CHECK(t.k.sprzet_posiada_dma() && t.k.stan() == Stan::Awaria && t.k.diag().blad == Blad::Stop);
        CHECK(!t.k.wylacz());
        CHECK(t.p.alokacje == 2);                        // nic nie zwolniono
        CHECK(t.e.cfg[1] & 0x4);                          // bus master zostaje
    }
    {   // Poprawne wylaczenie i ponowna inicjalizacja (bez wyciekow DMA).
        Stanowisko t;
        for (int r = 0; r < 3; ++r) {
            CHECK(t.init() == Blad::Brak);
            const Pcm::Owner o{6, uint64_t(r + 1)};
            const BwsPcmFormat f{BWS_PCM_V1, sizeof(BwsPcmFormat), 48000, 2, Pcm::FORMAT_S16, 0};
            const int64_t h = t.s.open(o, f); uint32_t w = 0;
            for (int i = 0; i < 20; ++i) { podaj(t, o, uint64_t(h), w, 24000); t.biegnij(5000000, 5000000); }
            t.s.posprzataj(o, t.p.czas);
            CHECK(t.s.klienci() == 0);
            CHECK(t.k.wylacz());
            CHECK(t.k.stan() == Stan::Wylaczony && t.p.alokacje == 0 && (t.e.cfg[1] & 0x4) == 0);
            CHECK(t.e.kodek[NAM_MASTER / 2] & 0x8000);
        }
    }
}

static void test_api_pcm() {
    Stanowisko t; CHECK(t.init() == Blad::Brak);
    const Pcm::Owner o{11, 2};
    BwsPcmFormat f{BWS_PCM_V1, sizeof(BwsPcmFormat), 48000, 2, Pcm::FORMAT_S16, 0};
    BwsPcmFormat zly = f; zly.version = 2; CHECK(t.s.open(o, zly) == PCM_INVALID);
    zly = f; zly.reserved = 1; CHECK(t.s.open(o, zly) == PCM_INVALID);
    zly = f; zly.rate = 44100; CHECK(t.s.open(o, zly) == PCM_UNSUPPORTED);
    zly = f; zly.channels = 6; CHECK(t.s.open(o, zly) == PCM_UNSUPPORTED);
    CHECK(t.s.write(o, 99, sygnal(0, 4).data(), 4, 1) == PCM_HANDLE);
    const int64_t a = t.s.open(o, f), b = t.s.open(o, f);
    CHECK(a > 0 && b > 0 && t.s.open(o, f) == PCM_LIMIT);  // maks. 2 strumienie na proces
    CHECK(t.s.write({12, 1}, uint64_t(a), sygnal(0, 4).data(), 4, 1) == PCM_HANDLE);   // cudzy uchwyt
    CHECK(t.s.gain(o, uint64_t(a), 101, 0) == PCM_INVALID && t.s.gain(o, uint64_t(a), 50, 2) == PCM_INVALID);
    CHECK(t.s.gain(o, uint64_t(a), 50, 1) == PCM_OK);
    BwsPcmState st{};
    CHECK(t.s.query(o, uint64_t(a), st) == PCM_OK && st.volume == 50 && st.muted == 1 && st.rate == 48000 && st.capacity == Pcm::CAPACITY_FRAMES);
    CHECK(t.s.write(o, uint64_t(a), sygnal(0, 100).data(), 100, 1) == 100);
    BwsPcmStats ps{};
    CHECK(t.s.statistics(o, uint64_t(a), ps) == PCM_OK && ps.accepted == 100);
    CHECK(t.s.finish(o, uint64_t(a)) == PCM_OK);
    t.s.posprzataj(o, t.p.czas);
    CHECK(t.s.klienci() == 0 && t.s.statystyki_silnika().usuniete_przy_zakonczeniu == 2);
    CHECK(t.s.query(o, uint64_t(a), st) == PCM_HANDLE);
}

static void test_polityka() {
    using namespace WyjscieAudio;
    CHECK(wybierz({true, true, true, true}) == Urzadzenie::Hda);       // HDA ma pierwszenstwo
    CHECK(wybierz({true, true, false, false}) == Urzadzenie::Hda);
    CHECK(wybierz({true, false, true, true}) == Urzadzenie::Ac97);     // HDA nie dziala -> AC97
    CHECK(wybierz({false, false, true, true}) == Urzadzenie::Ac97);
    CHECK(wybierz({false, false, true, false}) == Urzadzenie::Brak);   // wykryte, ale bez udanej inicjalizacji
    CHECK(wybierz({false, false, false, false}) == Urzadzenie::Brak);
    CHECK(!inicjalizowac_ac97(true, true) && inicjalizowac_ac97(false, true) && !inicjalizowac_ac97(false, false));
    CHECK(std::strstr(opis_braku({false, false, false, false}), "Nie wykryto"));
    CHECK(std::strstr(opis_braku({false, false, true, false}), "AC97"));
    Wybor w;
    CHECK(w.ustaw({false, false, true, true}) && w.aktywne() == Urzadzenie::Ac97);
    CHECK(!w.ustaw({true, true, false, false}) && w.aktywne() == Urzadzenie::Ac97);   // brak przelaczenia w trakcie
}

int main() {
    test_pci();
    test_inicjalizacja();
    test_odtwarzanie();
    test_niedobor_i_bledy();
    test_pozycja_i_stop();
    test_api_pcm();
    test_polityka();
    std::printf("AC97 host: %u sprawdzen OK (emulator ICH AC97 + kodek, bez QEMU i bez sprzetu)\n", checks);
    return 0;
}
