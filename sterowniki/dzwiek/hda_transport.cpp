#include "hda_transport.h"
#include "hda_quirks.h"
// Called before a link reset invalidates a running playback stream.
void hda_powiadom_reset_kontrolera(uint32_t);
#ifndef BURSZTYN_HDA_HOST_TEST
#include "pamiec.h"
#include "pci.h"
#include "../czas/hpet.h"
void wypisz_log(const char*);
#endif

namespace {
constexpr uint32_t MMIO_LEN = 0x4000;
constexpr uint32_t CORBL = 0x40, CORBU = 0x44, CORBWP = 0x48, CORBRP = 0x4a;
constexpr uint32_t CORBCTL = 0x4c, CORBSTS = 0x4d, CORBSIZE = 0x4e;
constexpr uint32_t RIRBL = 0x50, RIRBU = 0x54, RIRBWP = 0x58, RINTCNT = 0x5a;
constexpr uint32_t RIRBCTL = 0x5c, RIRBSTS = 0x5d, RIRBSIZE = 0x5e;
constexpr uint32_t ICOI = 0x60, ICII = 0x64, ICIS = 0x68;
struct Kontroler {
    HdaDiagKontroler info{};
    HdaControllerQuirks quirks{};
    uint64_t mmio = 0, deadline = 0;
    DmaBuffer corb{}, rirb{};
    uint16_t corb_wp = 0, rirb_rp = 0, discarded = 0;
    uint32_t owner = 0, command = 0, response = 0;
    uint64_t tx_deadline = 0;
    HdaFazaTransakcji phase = HDA_TX_BEZCZYNNY;
    HdaTransportStatus status = HDA_TRANSPORT_NOT_READY;
    bool lock = false, recovering = false, cancelled = false;
    uint8_t recoveries = 0;
};
Kontroler kontrolery[HDA_DIAG_MAKS_KONTROLEROW];
HdaPlatforma platforma{};
uint32_t liczba = 0;
bool wykryto = false, obciety = false;
struct Guard {
    Kontroler& c; bool ok;
    explicit Guard(Kontroler& k): c(k), ok(!__atomic_test_and_set(&c.lock, __ATOMIC_ACQUIRE)) {}
    ~Guard() { if (ok) __atomic_clear(&c.lock, __ATOMIC_RELEASE); }
};
uint32_t rd(uint32_t k, uint32_t o, uint8_t n) { return platforma.odczyt(platforma.kontekst,k,o,n); }
void wr(uint32_t k, uint32_t o, uint8_t n, uint32_t v) { platforma.zapis(platforma.kontekst,k,o,n,v); }
uint64_t now() { return platforma.czas_ns(platforma.kontekst); }
uint64_t deadline(uint64_t duration) { const uint64_t n = now(); return n > UINT64_MAX-duration ? UINT64_MAX : n+duration; }
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
bool expired(uint64_t d) { return now() >= d; }
void fail(Kontroler& c, HdaEtapBledu stage) {
    c.info.etap_bledu = stage; c.info.inicjalizacja = HDA_INIT_BLAD; c.info.gotowy = 0;
    c.info.blad = stage == HDA_BLAD_MAPOWANIA ? HDA_DIAG_BLAD_MMIO : HDA_DIAG_BLAD_RESET;
    c.status = stage == HDA_BLAD_RESET_W_TRAKCIE ? HDA_TRANSPORT_RESET_REQUIRED : HDA_TRANSPORT_NOT_READY;
}
uint16_t ring_size(uint8_t caps) { return caps & 0x40 ? 256 : caps & 0x20 ? 16 : caps & 0x10 ? 2 : 0; }
uint8_t ring_code(uint16_t n) { return n == 256 ? 2 : n == 16 ? 1 : 0; }
bool dma_valid(const DmaBuffer& b, bool dma64) {
    return b.virtual_address && b.size >= 2048 && b.page_count && b.physical_address &&
        !(b.physical_address & 127) && b.physical_address <= UINT64_MAX-4095 &&
        (dma64 || b.physical_address <= UINT32_MAX-4095ULL);
}
void begin_recovery(uint32_t k, Kontroler& c) {
    c.info.gotowy = 0;
    c.recoveries = 1; // A single bounded recovery attempt for this failed transaction.
    ++c.info.odzyskiwania;
    c.recovering = true;
    // An unfinished IC command or loss of CRST needs a complete link reset,
    // not a transition back to READY while ICB is still hardware-owned.
    if (c.status == HDA_TRANSPORT_RESET_REQUIRED || c.info.transport == 1 || !(rd(k,8,4)&1)) {
        c.info.transport = 2;
        c.info.inicjalizacja = HDA_INIT_STOP;
    } else c.info.inicjalizacja = HDA_INIT_RING_STOP;
}
HdaTransportStatus tx_error(uint32_t k, Kontroler& c, HdaTransportStatus s) {
    c.status = s; c.phase = s == HDA_TRANSPORT_TIMEOUT || s == HDA_TRANSPORT_RIRB_NO_RESPONSE ? HDA_TX_TIMEOUT : HDA_TX_BLAD;
    begin_recovery(k,c);
    return s;
}
void sample(uint32_t k, Kontroler& c) {
    if (!c.mmio) return;
    c.info.gctl = rd(k,8,4); c.info.statests = uint16_t(rd(k,0xe,2)) & 0x7fff;
    c.info.discovered_codec_mask |= c.info.statests;
    c.info.corb_run = (rd(k,CORBCTL,1) & 2) != 0;
    c.info.rirb_run = (rd(k,RIRBCTL,1) & 2) != 0;
}
void log_run_failure(uint32_t k, const Kontroler& c) {
#ifndef BURSZTYN_HDA_HOST_TEST
    char message[192];uint32_t length=0;
    auto add=[&](const char* s){while(*s&&length+1<sizeof(message))message[length++]=*s++;};
    auto hex=[&](uint32_t v){for(int shift=28;shift>=0;shift-=4)message[length++]="0123456789ABCDEF"[(v>>shift)&15];};
    add("[HDA] RUN start failed controller=");hex(k);
    add(" CORBCTL before=");hex(c.info.corb_ctl_before_start);
    add(" after=");hex(c.info.corb_ctl_after_start);add(" current=");hex(rd(k,CORBCTL,1));
    add(" RIRBCTL before=");hex(c.info.rirb_ctl_before_start);
    add(" after=");hex(c.info.rirb_ctl_after_start);add(" current=");hex(rd(k,RIRBCTL,1));
    message[length]=0;wypisz_log(message);
#else
    (void)k;(void)c;
#endif
}
}

bool hda_transport_wykryj_z(const HdaPlatforma& p) {
    if (wykryto) return true;
    if (!p.pci_odczyt || !p.pci_zapis || !p.mapuj || !p.odczyt || !p.zapis ||
        !p.dma || !p.zwolnij_dma || !p.czas_ns || !p.zegar_gotowy) return false;
    platforma = p;
    for (uint16_t bus=0; bus<256; ++bus) for (uint8_t slot=0; slot<32; ++slot) {
        // Read all functions. No ID allowlist and no assumption about function zero.
        for (uint8_t fn=0; fn<8; ++fn) {
            const uint32_t vd=p.pci_odczyt(p.kontekst,uint8_t(bus),slot,fn,0);
            if (uint16_t(vd)==65535 || uint16_t(vd)==0) continue;
            const uint32_t cl=p.pci_odczyt(p.kontekst,uint8_t(bus),slot,fn,8);
            if ((cl >> 16) != 0x0403) continue;
            if (liczba == HDA_DIAG_MAKS_KONTROLEROW) { obciety=true; continue; }
            auto& c=kontrolery[liczba++]; c = Kontroler{};
            auto& i=c.info; i.wersja=HDA_DIAG_WERSJA_ABI; i.rozmiar=sizeof(i);
            i.bus=uint8_t(bus); i.slot=slot; i.func=fn; i.vendor_id=uint16_t(vd);
            i.device_id=uint16_t(vd>>16); i.revision=uint8_t(cl); i.inicjalizacja=HDA_INIT_PCI;
            c.quirks=hda_pobierz_quirki_kontrolera(i.vendor_id, i.device_id);
            i.subsystem_id=p.pci_odczyt(p.kontekst,i.bus,slot,fn,0x2c);
            const uint32_t lo=p.pci_odczyt(p.kontekst,i.bus,slot,fn,0x10);
            const uint32_t type=lo&6;
            i.bar_typ = type==4 ? 64 : 32;
            if (!lo || lo==UINT32_MAX || (lo&1) || (type!=0 && type!=4) || (lo&8)) {
                fail(c,HDA_BLAD_BAR); continue;
            }
            const uint32_t hi=type==4 ? p.pci_odczyt(p.kontekst,i.bus,slot,fn,0x14) : 0;
            i.bar0=(uint64_t(hi)<<32)|(lo&0xfffffff0U); i.bar0_dlugosc=MMIO_LEN;
            // HDA register aperture is 16 KiB aligned. Also reject physical-address overflow.
            if (!i.bar0 || (i.bar0 & (MMIO_LEN-1)) || i.bar0 > 0x000fffffffffffffULL-(MMIO_LEN-1)) {
                fail(c,HDA_BLAD_BAR); continue;
            }
            const uint16_t cmd=uint16_t(p.pci_odczyt(p.kontekst,i.bus,slot,fn,4));
            p.pci_zapis(p.kontekst,i.bus,slot,fn,4,uint32_t(cmd|6U)); // status W1C half = zero
            i.command=uint16_t(p.pci_odczyt(p.kontekst,i.bus,slot,fn,4));
            if (!(i.command&2)) { fail(c,HDA_BLAD_PCI_MEMORY); continue; }
            if (!(i.command&4)) { fail(c,HDA_BLAD_PCI_MASTER); continue; }
            i.inicjalizacja=HDA_INIT_MAPOWANIE;
        }
    }
    wykryto=true;
    return true;
}
uint32_t hda_transport_liczba() { return liczba; }
bool hda_transport_rejestr_obciety() { return obciety; }
uint64_t hda_transport_mmio(uint32_t k) { return k<liczba ? kontrolery[k].mmio : 0; }
uint32_t hda_transport_odczyt_rejestru(uint32_t k,uint32_t o,uint8_t n) {
    if (k>=liczba || !kontrolery[k].mmio || (n!=1 && n!=2 && n!=4) || o>MMIO_LEN-n || (o&(n-1))) return UINT32_MAX;
    return rd(k,o,n);
}
bool hda_transport_stan(uint32_t k,HdaDiagKontroler* out) {
    if (k>=liczba || !out) return false;
    auto& c=kontrolery[k]; Guard g(c); if (!g.ok) return false;
    sample(k,c); c.info.stan_transakcji=c.phase; c.info.status_transportu=c.status;
    *out=c.info; return true;
}
void hda_transport_zapisz_maski(uint32_t k,uint16_t d,uint16_t r) {
    if (k>=liczba) return;
    auto& c=kontrolery[k]; Guard g(c); if (!g.ok) return;
    c.info.discovered_codec_mask |= d&0x7fff; c.info.responsive_codec_mask=r&0x7fff;
}
void hda_transport_test_dostepny(uint32_t k,bool v) { if(k<liczba){auto& c=kontrolery[k];Guard g(c);if(g.ok)c.info.test_dostepny=v;} }
void hda_transport_generacja(uint32_t k,uint32_t v) { if(k<liczba){auto& c=kontrolery[k];Guard g(c);if(g.ok)c.info.generacja=v;} }

bool hda_transport_init_krok(uint32_t k) {
    if (k>=liczba) return true;
    auto& c=kontrolery[k]; Guard g(c); if (!g.ok) return false;
    auto& i=c.info;
    if (i.inicjalizacja==HDA_INIT_BLAD) return true;
    if (!platforma.zegar_gotowy(platforma.kontekst)) { fail(c,HDA_BLAD_ZEGARA); return true; }
    if (i.inicjalizacja>=HDA_INIT_RING_BUFOR && i.inicjalizacja!=HDA_INIT_GOTOWY && !(rd(k,8,4)&1)) {
        fail(c,HDA_BLAD_RESET_W_TRAKCIE); return true;
    }
    switch (i.inicjalizacja) {
    case HDA_INIT_MAPOWANIE:
        if (!platforma.mapuj(platforma.kontekst,k,i.bar0,MMIO_LEN,&c.mmio)) { fail(c,HDA_BLAD_MAPOWANIA); break; }
        i.mmio_zmapowane=1; i.hda_vmin=uint8_t(rd(k,2,1)); i.hda_vmaj=uint8_t(rd(k,3,1));
        i.gcap=uint16_t(rd(k,0,2)); i.oss=(i.gcap>>12)&15; i.iss=(i.gcap>>8)&15; i.bss=(i.gcap>>3)&31;
        i.dma64=i.gcap&1; i.outpay=uint16_t(rd(k,4,2));
        if (i.hda_vmaj!=1 || i.hda_vmin!=0) { fail(c,HDA_BLAD_WERSJI); break; }
        if (!i.gcap || i.gcap==65535 || uint32_t(i.oss+i.iss+i.bss)>30 || !(i.oss+i.iss+i.bss)) {
            fail(c,HDA_BLAD_GCAP); break;
        }
        i.initial_statests=uint16_t(rd(k,0xe,2))&0x7fff;
        i.discovered_codec_mask=i.initial_statests;
        i.inicjalizacja=HDA_INIT_STOP; break;
    case HDA_INIT_STOP:
        wr(k,0x20,4,0); wr(k,0x0c,2,0); wr(k,0x70,4,0); wr(k,0x74,4,0);
        wr(k,CORBCTL,1,0); wr(k,RIRBCTL,1,0);
        for(uint32_t s=0;s<uint32_t(i.oss+i.iss+i.bss);++s) {
            const uint32_t off=0x80+s*0x20; wr(k,off,2,rd(k,off,2)&~0x1eU);
        }
        c.deadline=deadline(HDA_CZAS_RESETU_NS); i.inicjalizacja=HDA_INIT_CZEKA_STOP; break;
    case HDA_INIT_CZEKA_STOP: {
        bool stopped=!(rd(k,CORBCTL,1)&2) && !(rd(k,RIRBCTL,1)&2);
        for(uint32_t s=0;s<uint32_t(i.oss+i.iss+i.bss);++s) if(rd(k,0x80+s*0x20,2)&2) stopped=false;
        if(stopped) i.inicjalizacja=HDA_INIT_RESET_ZERO;
        else if(expired(c.deadline)) fail(c,HDA_BLAD_STOP_DMA);
        break;
    }
    case HDA_INIT_RESET_ZERO:
        hda_powiadom_reset_kontrolera(k);
        wr(k,8,4,rd(k,8,4)&~1U); c.deadline=deadline(HDA_CZAS_RESETU_NS);
        i.inicjalizacja=HDA_INIT_CZEKA_RESET_ZERO; break;
    case HDA_INIT_CZEKA_RESET_ZERO:
        if(!(rd(k,8,4)&1)) { c.deadline=deadline(100000); i.inicjalizacja=HDA_INIT_TRZYMA_RESET; }
        else if(expired(c.deadline)) fail(c,HDA_BLAD_RESET_ZERO);
        break;
    case HDA_INIT_TRZYMA_RESET:
        if(expired(c.deadline)) i.inicjalizacja=HDA_INIT_RESET_JEDEN;
        break;
    case HDA_INIT_RESET_JEDEN:
        wr(k,8,4,rd(k,8,4)|1); c.deadline=deadline(HDA_CZAS_RESETU_NS);
        i.inicjalizacja=HDA_INIT_CZEKA_RESET_JEDEN; break;
    case HDA_INIT_CZEKA_RESET_JEDEN:
        if(rd(k,8,4)&1) { c.deadline=deadline(HDA_CZAS_STABILIZACJI_NS); i.inicjalizacja=HDA_INIT_KODEKI; }
        else if(expired(c.deadline)) fail(c,HDA_BLAD_RESET_JEDEN);
        break;
    case HDA_INIT_KODEKI:
        if(!expired(c.deadline)) break;
        i.initial_statests |= uint16_t(rd(k,0xe,2))&0x7fff;
        i.discovered_codec_mask |= i.initial_statests;
        wr(k,0xe,2,i.initial_statests); // preserve BEFORE W1C
        i.inicjalizacja=HDA_INIT_RING_BUFOR; break;
    case HDA_INIT_RING_BUFOR:
        i.corb_caps=uint8_t(rd(k,CORBSIZE,1))&0x70; i.rirb_caps=uint8_t(rd(k,RIRBSIZE,1))&0x70;
        i.corb_rozmiar=ring_size(i.corb_caps); i.rirb_rozmiar=ring_size(i.rirb_caps);
        if(!i.corb_rozmiar) { fail(c,HDA_BLAD_CORB_ROZMIAR); break; }
        if(!i.rirb_rozmiar) { fail(c,HDA_BLAD_RIRB_ROZMIAR); break; }
        if((!c.corb.virtual_address && !platforma.dma(platforma.kontekst,i.dma64,&c.corb)) || !dma_valid(c.corb,i.dma64)) {
            platforma.zwolnij_dma(platforma.kontekst,&c.corb); fail(c,HDA_BLAD_CORB_DMA); break;
        }
        if((!c.rirb.virtual_address && !platforma.dma(platforma.kontekst,i.dma64,&c.rirb)) || !dma_valid(c.rirb,i.dma64)) {
            platforma.zwolnij_dma(platforma.kontekst,&c.rirb);
            platforma.zwolnij_dma(platforma.kontekst,&c.corb); fail(c,HDA_BLAD_RIRB_DMA); break;
        }
        // QEMU 8086:2668 supports the standard rings. Never bypass their RUN
        // handshake or enter IC simply because this is an emulated controller.
        i.transport=2;
        i.inicjalizacja=HDA_INIT_RING_STOP; break;
    case HDA_INIT_RING_STOP:
        wr(k,CORBCTL,1,0); wr(k,RIRBCTL,1,0); c.deadline=deadline(HDA_CZAS_RESETU_NS);
        i.inicjalizacja=HDA_INIT_RING_CZEKA_STOP; break;
    case HDA_INIT_RING_CZEKA_STOP:
        if(!(rd(k,CORBCTL,1)&2) && !(rd(k,RIRBCTL,1)&2)) {
            if(c.recovering){c.deadline=deadline(HDA_CZAS_VERBA_NS);i.inicjalizacja=HDA_INIT_RING_WYCISZENIE;}
            else i.inicjalizacja=HDA_INIT_RING_BAZY;
        }
        else if(expired(c.deadline)) fail(c,HDA_BLAD_STOP_DMA);
        break;
    case HDA_INIT_RING_WYCISZENIE:
        // Let an already consumed link command finish before discarding old replies.
        if(expired(c.deadline))i.inicjalizacja=HDA_INIT_RING_BAZY;
        break;
    case HDA_INIT_RING_BAZY:
        wr(k,CORBL,4,uint32_t(c.corb.physical_address)); wr(k,CORBU,4,uint32_t(c.corb.physical_address>>32));
        wr(k,RIRBL,4,uint32_t(c.rirb.physical_address)); wr(k,RIRBU,4,uint32_t(c.rirb.physical_address>>32));
        wr(k,CORBSIZE,1,ring_code(i.corb_rozmiar)); wr(k,RIRBSIZE,1,ring_code(i.rirb_rozmiar));
        if((rd(k,CORBSIZE,1)&3)!=ring_code(i.corb_rozmiar)){fail(c,HDA_BLAD_CORB_ROZMIAR);break;}
        if((rd(k,RIRBSIZE,1)&3)!=ring_code(i.rirb_rozmiar)){fail(c,HDA_BLAD_RIRB_ROZMIAR);break;}
        c.corb_wp=0; c.rirb_rp=0; wr(k,CORBWP,2,0);
        i.inicjalizacja=HDA_INIT_CORB_RESET; break;
    case HDA_INIT_CORB_RESET:
        wr(k,CORBRP,2,0x8000); c.deadline=deadline(HDA_CZAS_RESETU_NS);
        i.corb_stan=1;
        if(c.quirks.corbrp_self_clear) {
            i.inicjalizacja=HDA_INIT_CORB_ZERUJ_RESET;
        } else {
            i.inicjalizacja=HDA_INIT_CORB_CZEKA_RESET;
        }
        break;
    case HDA_INIT_CORB_CZEKA_RESET:
        if(rd(k,CORBRP,2)&0x8000) i.inicjalizacja=HDA_INIT_CORB_ZERUJ_RESET;
        else if(expired(c.deadline)) fail(c,HDA_BLAD_CORB_RESET);
        break;
    case HDA_INIT_CORB_ZERUJ_RESET:
        wr(k,CORBRP,2,0); c.deadline=deadline(HDA_CZAS_RESETU_NS);
        i.inicjalizacja=HDA_INIT_CORB_CZEKA_ZERO; break;
    case HDA_INIT_CORB_CZEKA_ZERO:
        if(!(rd(k,CORBRP,2)&0x80ff)) { i.corb_stan=2; i.inicjalizacja=HDA_INIT_RIRB_RESET; }
        else if(expired(c.deadline)) fail(c,HDA_BLAD_CORB_RESET);
        break;
    case HDA_INIT_RIRB_RESET:
        wr(k,RIRBWP,2,0x8000); c.deadline=deadline(HDA_CZAS_RESETU_NS);
        i.rirb_stan=1; i.inicjalizacja=HDA_INIT_RIRB_CZEKA_ZERO; break;
    case HDA_INIT_RIRB_CZEKA_ZERO:
        if(!(rd(k,RIRBWP,2)&0xff)) { i.rirb_stan=2; i.inicjalizacja=HDA_INIT_RING_START; }
        else if(expired(c.deadline)) fail(c,HDA_BLAD_RIRB_RESET);
        break;
    case HDA_INIT_RING_START:
        wr(k,CORBSTS,1,1); wr(k,RIRBSTS,1,5); wr(k,RINTCNT,2,1);
        fence();
        i.rirb_ctl_before_start=rd(k,RIRBCTL,1); i.corb_ctl_before_start=rd(k,CORBCTL,1);
        // RINTCNT=1 needs response-status generation, also in polling mode:
        // QEMU advances the response counter only after RIRBSTS.RINTFL ack.
        // INTCTL remains zero, so this does not enable a hardware IRQ.
        wr(k,RIRBCTL,1,3); wr(k,CORBCTL,1,2);
        i.rirb_ctl_after_start=rd(k,RIRBCTL,1); i.corb_ctl_after_start=rd(k,CORBCTL,1);
        c.deadline=deadline(HDA_CZAS_RESETU_NS); i.inicjalizacja=HDA_INIT_RING_CZEKA_START; break;
    case HDA_INIT_RING_CZEKA_START:
        if((rd(k,RIRBCTL,1)&2) && (rd(k,CORBCTL,1)&2)) {
            i.inicjalizacja=HDA_INIT_GOTOWY; i.gotowy=1; i.corb_stan=3; i.rirb_stan=3;
            i.etap_bledu=HDA_BRAK_BLEDU; i.blad=HDA_DIAG_OK;
            c.recovering=false; c.phase=HDA_TX_BEZCZYNNY; c.owner=0; c.status=HDA_TRANSPORT_SUCCESS;
        } else if(expired(c.deadline)) {log_run_failure(k,c);fail(c,HDA_BLAD_RING_START);}
        break;
    case HDA_INIT_GOTOWY: return true;
    default: fail(c,HDA_BLAD_MAPOWANIA); break;
    }
    return i.inicjalizacja==HDA_INIT_GOTOWY || i.inicjalizacja==HDA_INIT_BLAD;
}

HdaTransportStatus hda_transport_zglos(uint32_t k,uint32_t owner,uint32_t cmd) {
    if(k>=liczba || !owner || (cmd>>28)>14 || (cmd&(1U<<27))) return HDA_TRANSPORT_INVALID_ARGUMENT;
    auto& c=kontrolery[k]; Guard g(c); if(!g.ok) return HDA_TRANSPORT_BUSY;
    if(c.owner || c.phase!=HDA_TX_BEZCZYNNY) return HDA_TRANSPORT_BUSY;
    if(!c.info.gotowy || c.recovering) return HDA_TRANSPORT_NOT_READY;
    if(!platforma.zegar_gotowy(platforma.kontekst)) return HDA_TRANSPORT_NOT_READY;
    c.owner=owner; c.command=cmd; c.response=0; c.cancelled=false;c.discarded=0;
    c.phase=HDA_TX_PRZYGOTUJ; c.status=HDA_TRANSPORT_PENDING;
    c.tx_deadline=deadline(HDA_CZAS_VERBA_NS);
    return HDA_TRANSPORT_PENDING;
}
HdaTransportStatus hda_transport_krok(uint32_t k,uint32_t owner) {
    if(k>=liczba) return HDA_TRANSPORT_NOT_READY;
    auto& c=kontrolery[k]; Guard g(c); if(!g.ok || c.owner!=owner) return HDA_TRANSPORT_BUSY;
    const uint64_t begin=now();
    struct Timing { Kontroler& c; uint64_t start; ~Timing() {
        const uint64_t end=now(), elapsed=end>=start?end-start:0;
        if(elapsed>c.info.maks_krok_ns) c.info.maks_krok_ns=elapsed;
    } } timing{c,begin};
    if(c.phase==HDA_TX_GOTOWA || c.phase==HDA_TX_BLAD || c.phase==HDA_TX_TIMEOUT) return c.status;
    if(!c.info.gotowy || c.recovering || !(rd(k,8,4)&1)) {
        c.info.etap_bledu=HDA_BLAD_RESET_W_TRAKCIE;
        return tx_error(k,c,HDA_TRANSPORT_RESET_REQUIRED);
    }
    if(c.info.transport==1) {
        // ONE short phase. Never poll hardware in a loop, even when already ready.
        switch(c.phase) {
        case HDA_TX_PRZYGOTUJ: c.phase=HDA_TX_CZEKA_INTERFEJS; break;
        case HDA_TX_CZEKA_INTERFEJS: {
            const uint32_t s=rd(k,ICIS,2);
            if(s==65535) return tx_error(k,c,HDA_TRANSPORT_NOT_READY);
            if(!(s&1)) c.phase=HDA_TX_USUN_STARA;
            else if(expired(c.tx_deadline)) return tx_error(k,c,HDA_TRANSPORT_BUSY_STUCK);
            break;
        }
        case HDA_TX_USUN_STARA:
            wr(k,ICIS,2,2); fence(); c.phase=HDA_TX_WYSLIJ; break;
        case HDA_TX_WYSLIJ:
            if(rd(k,ICIS,2)&3) return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
            wr(k,ICOI,4,c.command); fence(); wr(k,ICIS,2,1);
            c.tx_deadline=deadline(HDA_CZAS_VERBA_NS); c.phase=HDA_TX_CZEKA_KOMENDA; break;
        case HDA_TX_CZEKA_KOMENDA: {
            const auto s=rd(k,ICIS,2);
            if(s==65535) return tx_error(k,c,HDA_TRANSPORT_NOT_READY);
            if(!(s&1)) { c.phase=HDA_TX_CZEKA_ODPOWIEDZ; }
            else if(expired(c.tx_deadline)) return tx_error(k,c,HDA_TRANSPORT_TIMEOUT);
            break;
        }
        case HDA_TX_CZEKA_ODPOWIEDZ:
            if(rd(k,ICIS,2)&2) c.phase=HDA_TX_ODCZYTAJ;
            else if(expired(c.tx_deadline)) return tx_error(k,c,HDA_TRANSPORT_CODEC_NO_RESPONSE);
            break;
        case HDA_TX_ODCZYTAJ:
            c.response=rd(k,ICII,4); wr(k,ICIS,2,2); fence();
            c.phase=HDA_TX_GOTOWA; c.status=HDA_TRANSPORT_SUCCESS; c.recoveries=0; break;
        default: return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
        }
    } else {
        switch(c.phase) {
        case HDA_TX_PRZYGOTUJ:
            // Drain preexisting responses BEFORE publishing the next command.
            c.phase=HDA_TX_USUN_STARA; break;
        case HDA_TX_USUN_STARA:
        case HDA_TX_CZEKA_ODPOWIEDZ: {
            if(rd(k,RIRBSTS,1)&4) return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
            const uint32_t wp=rd(k,RIRBWP,2)&0xff;
            if(wp>=c.info.rirb_rozmiar) return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
            if(c.rirb_rp!=wp) {
                c.rirb_rp=uint16_t((c.rirb_rp+1)%c.info.rirb_rozmiar); fence();
                const auto* entries=static_cast<volatile uint32_t*>(c.rirb.virtual_address);
                const uint32_t response=entries[c.rirb_rp*2], extended=entries[c.rirb_rp*2+1];
                if(extended & ~0x1fU) return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
                if((extended&0x10) || c.phase==HDA_TX_USUN_STARA || (extended&15)!=(c.command>>28)) {
                    if(extended&0x10)++c.info.odpowiedzi_niezamowione;else ++c.info.odpowiedzi_obce;
                    // A delayed service turn may find the valid reply already queued behind an event.
                    // Drain at most one ring's worth, one entry per turn, before applying timeout.
                    if(++c.discarded>=c.info.rirb_rozmiar)return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
                    if(c.rirb_rp==wp)wr(k,RIRBSTS,1,1);
                    if(c.phase==HDA_TX_CZEKA_ODPOWIEDZ && c.rirb_rp==wp && expired(c.tx_deadline))
                        return tx_error(k,c,HDA_TRANSPORT_RIRB_NO_RESPONSE);
                    break;
                }
                c.response=response; c.phase=HDA_TX_GOTOWA; c.status=HDA_TRANSPORT_SUCCESS; c.recoveries=0;
                wr(k,RIRBSTS,1,1); break;
            }
            if(c.phase==HDA_TX_USUN_STARA){c.phase=HDA_TX_WYSLIJ;c.tx_deadline=deadline(HDA_CZAS_VERBA_NS);}
            else if(expired(c.tx_deadline)) return tx_error(k,c,HDA_TRANSPORT_RIRB_NO_RESPONSE);
            break;
        }
        case HDA_TX_WYSLIJ: {
            // CORB memory error requires CRST before commands may resume.
            if(rd(k,CORBSTS,1)&1) return tx_error(k,c,HDA_TRANSPORT_RESET_REQUIRED);
            if(!(rd(k,CORBCTL,1)&2) || !(rd(k,RIRBCTL,1)&2)) return tx_error(k,c,HDA_TRANSPORT_RESET_REQUIRED);
            const uint32_t rp=rd(k,CORBRP,2);
            if((rp&0x8000) || (rp&0xff)>=c.info.corb_rozmiar) return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
            const uint16_t next=uint16_t((c.corb_wp+1)%c.info.corb_rozmiar);
            if(next==(rp&0xff)) {
                if(expired(c.tx_deadline)) return tx_error(k,c,HDA_TRANSPORT_CORB_FULL);
                return HDA_TRANSPORT_PENDING;
            }
            static_cast<volatile uint32_t*>(c.corb.virtual_address)[next]=c.command;
            fence(); c.corb_wp=next; wr(k,CORBWP,2,next);
            c.tx_deadline=deadline(HDA_CZAS_VERBA_NS); c.phase=HDA_TX_CZEKA_ODPOWIEDZ; break;
        }
        default: return tx_error(k,c,HDA_TRANSPORT_MALFORMED);
        }
    }
    return c.status;
}
HdaTransportStatus hda_transport_odbierz(uint32_t k,uint32_t owner,uint32_t* out) {
    if(k>=liczba || !out) return HDA_TRANSPORT_INVALID_ARGUMENT;
    auto& c=kontrolery[k]; Guard g(c); if(!g.ok || c.owner!=owner) return HDA_TRANSPORT_BUSY;
    if(c.phase!=HDA_TX_GOTOWA && c.phase!=HDA_TX_BLAD && c.phase!=HDA_TX_TIMEOUT) return HDA_TRANSPORT_PENDING;
    const auto status=c.status; *out=status==HDA_TRANSPORT_SUCCESS ? c.response : 0;
    c.owner=0; c.phase=HDA_TX_BEZCZYNNY; return status;
}
void hda_transport_anuluj(uint32_t k,uint32_t owner) {
    if(k>=liczba) return;
    auto& c=kontrolery[k]; Guard g(c); if(!g.ok || c.owner!=owner) return;
    // Late replies cannot be mistaken for a new generation's reply.
    if(c.phase>=HDA_TX_WYSLIJ && c.phase<HDA_TX_GOTOWA) begin_recovery(k,c);
    c.owner=0; c.phase=HDA_TX_BEZCZYNNY; c.status=HDA_TRANSPORT_CANCELLED; c.cancelled=true;
}
bool hda_transport_odnow(uint32_t k) {
    if(k>=liczba) return false;
    auto& c=kontrolery[k];
    { Guard g(c); if(!g.ok || c.owner) return false;
      if(!c.recovering) return c.info.gotowy;
    }
    return hda_transport_init_krok(k);
}

#ifndef BURSZTYN_HDA_HOST_TEST
namespace {
uint64_t va[HDA_DIAG_MAKS_KONTROLEROW]{};
uint32_t pci_read(void*,uint8_t b,uint8_t s,uint8_t f,uint8_t o) { return pci_odczytaj_dword(b,s,f,o); }
void pci_write(void*,uint8_t b,uint8_t s,uint8_t f,uint8_t o,uint32_t v) { pci_zapisz_dword(b,s,f,o,v); }
extern "C" bool VmmDiagnostykaWpisu(uint64_t,uint64_t*);
bool map(void*,uint32_t k,uint64_t physical,uint32_t len,uint64_t* result) {
    // Dedicated supervisor mapping, also for >4GiB 64-bit BARs. Never use HHDM for MMIO.
    const uint64_t base=0xffff920000000000ULL + uint64_t(k)*0x10000;
    for(uint32_t o=0;o<len;o+=4096) {
        ZmapujStrone(reinterpret_cast<void*>(base+o),reinterpret_cast<void*>(physical+o),
            VMM_FLAGA_OBECNA|VMM_FLAGA_ZAPIS|VMM_FLAGA_PCD|VMM_FLAGA_PWT);
        uint64_t pte=0;
        if(!VmmDiagnostykaWpisu(base+o,&pte) || (pte&0x1f)!=0x1b ||
            (pte&0x000ffffffffff000ULL)!=(physical+o)) return false;
    }
    va[k]=base; *result=base; return true;
}
uint32_t read(void*,uint32_t k,uint32_t o,uint8_t n) {
    const auto a=va[k]+o; asm volatile("":::"memory");
    if(n==1) return *reinterpret_cast<volatile uint8_t*>(a);
    if(n==2) return *reinterpret_cast<volatile uint16_t*>(a);
    return *reinterpret_cast<volatile uint32_t*>(a);
}
void write(void*,uint32_t k,uint32_t o,uint8_t n,uint32_t v) {
    const auto a=va[k]+o;
    if(n==1) *reinterpret_cast<volatile uint8_t*>(a)=uint8_t(v);
    else if(n==2) *reinterpret_cast<volatile uint16_t*>(a)=uint16_t(v);
    else *reinterpret_cast<volatile uint32_t*>(a)=v;
    asm volatile("":::"memory");
}
bool dma(void*,bool dma64,DmaBuffer* b) {
    // Existing PMM/DMA allocator allocates lowest contiguous HHDM RAM first.
    // Verify addressability explicitly; never truncate an address.
    if(!dma_allocate(4096,128,b)) return false;
    if(!dma_valid(*b,dma64)) { dma_release(b); return false; }
    return true;
}
void release(void*,DmaBuffer* b) { dma_release(b); }
uint64_t clock(void*) { return czas_monotoniczny_ns(); }
bool clock_ready(void*) { return hpet_dostepny(); }
}
bool hda_transport_wykryj() {
    return hda_transport_wykryj_z({nullptr,pci_read,pci_write,map,read,write,dma,release,clock,clock_ready});
}
#else
bool hda_transport_wykryj() { return wykryto; }
#endif

#ifdef BURSZTYN_HDA_HOST_TEST
bool hda_transport_host_immediate(uint32_t k){
    if(k>=liczba)return false;
    auto& c=kontrolery[k];Guard g(c);
    if(!g.ok || !c.info.gotowy || c.recovering || c.owner || c.phase!=HDA_TX_BEZCZYNNY ||
        !(rd(k,8,4)&1) || c.status==HDA_TRANSPORT_RESET_REQUIRED)return false;
    wr(k,CORBCTL,1,0);wr(k,RIRBCTL,1,0);
    if((rd(k,CORBCTL,1)&2)||(rd(k,RIRBCTL,1)&2))return false;
    c.info.transport=1;return true;
}
void hda_transport_host_reset(){
    for(uint32_t k=0;k<liczba;++k){
        if(platforma.zwolnij_dma){platforma.zwolnij_dma(platforma.kontekst,&kontrolery[k].corb);platforma.zwolnij_dma(platforma.kontekst,&kontrolery[k].rirb);}
        kontrolery[k]=Kontroler{};
    }
    liczba=0;wykryto=false;obciety=false;platforma={};
}
#endif
