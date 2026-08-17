/* ACPI RSDP -> RSDT/XSDT -> HPET, bez dynamicznej alokacji. */
#include "acpi.h"
#include "pamiec.h"

#include <stdint.h>
#include <stddef.h>

extern void wypisz_log(const char* tekst);

namespace {

constexpr uint32_t TAG_ACPI_OLD = 14;
constexpr uint32_t TAG_ACPI_NEW = 15;
constexpr uint64_t STRONA = 4096;
constexpr uint64_t LIMIT_IDENTITY = 4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OKNO_ACPI = 0x140000000ULL;
constexpr uint64_t ROZMIAR_OKNA = 1024ULL * 1024ULL;
constexpr uint32_t FLAGI_ACPI = VMM_FLAGA_PRESENT | VMM_FLAGA_ZAPIS;
constexpr uint32_t MAKS_WPISOW = 256;

struct Rsdp1 {
    char podpis[8];
    uint8_t checksum;
    char oem[6];
    uint8_t rewizja;
    uint32_t rsdt;
} __attribute__((packed));

struct Rsdp2 {
    Rsdp1 v1;
    uint32_t dlugosc;
    uint64_t xsdt;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct SdtHeader {
    char podpis[4];
    uint32_t dlugosc;
    uint8_t rewizja;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct Gas {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

struct TabelaHpet {
    SdtHeader naglowek;
    uint32_t event_timer_block_id;
    Gas adres;
    uint8_t numer_hpet;
    uint16_t min_tick;
    uint8_t page_protection;
} __attribute__((packed));

uint64_t znaleziony_hpet = 0;
bool parser_gotowy = false;
bool parser_xsdt = false;

Gas reset_reg{};
uint8_t reset_value = 0;
bool reset_valid = false;
Gas pm1a_cnt{};
Gas pm1b_cnt{};
bool s5_valid = false;
uint16_t slp_typa = 0;
uint16_t slp_typb = 0;

void* mapuj_tabele(uint64_t fiz, uint32_t rozmiar);
bool pobierz_naglowek(uint64_t fiz, SdtHeader* wynik);
bool podpis(const char* a, const char* b, size_t n);
bool checksum_ok(const void* p, uint32_t n);

uint32_t le32(const uint8_t* p){return p?static_cast<uint32_t>(p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24)):0;}
uint64_t le64(const uint8_t* p){uint64_t v=0;if(p)for(unsigned i=0;i<8;++i)v|=static_cast<uint64_t>(p[i])<<(i*8);return v;}

bool gas_poprawny(const Gas& g) {
    return (g.address_space==0||g.address_space==1) && g.address!=0 &&
           g.bit_offset==0 && (g.bit_width==8||g.bit_width==16||g.bit_width==32);
}

bool aml_pkg_len(const uint8_t* p,const uint8_t* end,uint32_t* len,size_t* used){
    if(!p||p>=end||!len||!used)return false;
    uint8_t lead=*p;unsigned follow=lead>>6;
    if(static_cast<size_t>(end-p)<1U+follow)return false;
    uint32_t v=follow?lead&0x0FU:lead&0x3FU;
    for(unsigned i=0;i<follow;++i)v|=static_cast<uint32_t>(p[1+i])<<(4+8*i);
    *len=v;*used=1U+follow;return v>=*used;
}
bool aml_integer(const uint8_t*&p,const uint8_t*end,uint64_t*out){
    if(!p||p>=end||!out)return false;
    uint8_t op=*p++;
    if(op==0x00){*out=0;return true;}if(op==0x01){*out=1;return true;}
    unsigned n=op==0x0A?1U:op==0x0B?2U:op==0x0C?4U:op==0x0E?8U:0U;
    if(!n||static_cast<size_t>(end-p)<n)return false;
    *out=0;for(unsigned i=0;i<n;++i)*out|=static_cast<uint64_t>(p[i])<<(8*i);
    p+=n;return true;
}
bool parsuj_s5(uint64_t dsdt_fiz) {
    SdtHeader h{};if(!pobierz_naglowek(dsdt_fiz,&h)||!podpis(h.podpis,"DSDT",4))return false;
    const uint8_t* t=static_cast<const uint8_t*>(mapuj_tabele(dsdt_fiz,h.dlugosc));
    if(!t||!checksum_ok(t,h.dlugosc))return false;
    const uint8_t* p=t+sizeof(SdtHeader),*end=t+h.dlugosc;
    while(static_cast<size_t>(end-p)>=6){
        const uint8_t* n=p;if(*n==0x08)++n;else{++p;continue;}if(n<end&&*n==0x5C)++n;
        if(static_cast<size_t>(end-n)<5||n[0]!='_'||n[1]!='S'||n[2]!='5'||n[3]!='_'){++p;continue;}n+=4;
        if(n>=end||*n++!=0x12)return false;
        uint32_t pkg=0;size_t used=0;if(!aml_pkg_len(n,end,&pkg,&used))return false;
        const uint8_t* pkg_start=n;n+=used;if(pkg>static_cast<uint32_t>(end-pkg_start)||n>=end)return false;
        const uint8_t* pkg_end=pkg_start+pkg;uint8_t count=*n++;if(count<2)return false;uint64_t a=0,b=0;
        if(!aml_integer(n,pkg_end,&a)||!aml_integer(n,pkg_end,&b)||a>7||b>7)return false;
        slp_typa=static_cast<uint16_t>(a<<10);slp_typb=static_cast<uint16_t>(b<<10);return true;
    }return false;
}

void parsuj_fadt(uint64_t fiz,const SdtHeader& h){
    const uint8_t* f=static_cast<const uint8_t*>(mapuj_tabele(fiz,h.dlugosc));
    if(!f||!checksum_ok(f,h.dlugosc)||h.dlugosc<116)return;
    uint64_t dsdt=h.dlugosc>=148?le64(f+140):0;if(!dsdt)dsdt=le32(f+40);
    const uint8_t cnt_len=h.dlugosc>89?f[89]:0;
    if(h.dlugosc>=196){Gas a{},b{};for(size_t i=0;i<sizeof(Gas);++i){reinterpret_cast<uint8_t*>(&a)[i]=f[172+i];reinterpret_cast<uint8_t*>(&b)[i]=f[184+i];}if(gas_poprawny(a))pm1a_cnt=a;if(gas_poprawny(b))pm1b_cnt=b;}
    if(!gas_poprawny(pm1a_cnt)&&le32(f+64)!=0){pm1a_cnt={1,static_cast<uint8_t>(cnt_len*8),0,0,le32(f+64)};}
    if(!gas_poprawny(pm1b_cnt)&&le32(f+68)!=0){pm1b_cnt={1,static_cast<uint8_t>(cnt_len*8),0,0,le32(f+68)};}
    if(h.dlugosc>=129&&(le32(f+112)&(1U<<10))){for(size_t i=0;i<sizeof(Gas);++i)reinterpret_cast<uint8_t*>(&reset_reg)[i]=f[116+i];reset_value=f[128];reset_valid=gas_poprawny(reset_reg);}
    s5_valid=gas_poprawny(pm1a_cnt)&&dsdt&&parsuj_s5(dsdt);
    wypisz_log(reset_valid ? "[ACPI] FADT RESET_REG gotowy." :
                              "[ACPI] FADT bez obslugi RESET_REG.");
    wypisz_log(s5_valid ? "[ACPI] DSDT _S5 gotowy." :
                          "[ACPI] Nie znaleziono poprawnego _S5.");
}

bool podpis(const char* a, const char* b, size_t n) {
    if (!a || !b) return false;
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}

bool checksum_ok(const void* p, uint32_t n) {
    if (!p || n == 0) return false;
    const volatile uint8_t* b = static_cast<const volatile uint8_t*>(p);
    uint8_t suma = 0;
    for (uint32_t i = 0; i < n; ++i) suma = static_cast<uint8_t>(suma + b[i]);
    return suma == 0;
}

void log_sygnature(const char* prefiks,const char sig[4]) {
    char b[64]={};size_t n=0;while(prefiks&&*prefiks&&n+1<sizeof(b))b[n++]=*prefiks++;
    for(size_t i=0;i<4&&n+1<sizeof(b);++i) b[n++]=sig[i];
    b[n]='\0';
    wypisz_log(b);
}

void* mapuj_tabele(uint64_t fiz, uint32_t rozmiar) {
    if (fiz == 0 || rozmiar == 0 || rozmiar > ROZMIAR_OKNA ||
        fiz > UINT64_MAX - rozmiar) return nullptr;
    if (fiz < LIMIT_IDENTITY && fiz + rozmiar <= LIMIT_IDENTITY)
        return reinterpret_cast<void*>(fiz);

    const uint64_t baza = fiz & ~(STRONA - 1ULL);
    const uint64_t przesuniecie = fiz - baza;
    const uint64_t koniec = (przesuniecie + rozmiar + STRONA - 1ULL) &
                            ~(STRONA - 1ULL);
    if (koniec > ROZMIAR_OKNA) return nullptr;
    for (uint64_t off = 0; off < koniec; off += STRONA) {
        ZmapujStrone(reinterpret_cast<void*>(OKNO_ACPI + off),
                     reinterpret_cast<void*>(baza + off), FLAGI_ACPI);
        asm volatile("invlpg (%0)" : : "r"(OKNO_ACPI + off) : "memory");
    }
    return reinterpret_cast<void*>(OKNO_ACPI + przesuniecie);
}

bool pobierz_naglowek(uint64_t fiz, SdtHeader* wynik) {
    if (!wynik) return false;
    const SdtHeader* h = static_cast<const SdtHeader*>(mapuj_tabele(fiz, sizeof(SdtHeader)));
    if (!h) return false;
    *wynik = *h;
    return wynik->dlugosc >= sizeof(SdtHeader) && wynik->dlugosc <= ROZMIAR_OKNA;
}

bool znajdz_w_root(uint64_t root_fiz, bool xsdt) {
    SdtHeader kopia{};
    if (!pobierz_naglowek(root_fiz, &kopia)) { wypisz_log("[ACPI] Nie mozna odczytac naglowka root SDT."); return false; }
    if (!podpis(kopia.podpis, xsdt ? "XSDT" : "RSDT", 4)) { log_sygnature("[ACPI] Bledna root signature=",kopia.podpis); return false; }
    const SdtHeader* root = static_cast<const SdtHeader*>(mapuj_tabele(root_fiz, kopia.dlugosc));
    if (!root || !checksum_ok(root, kopia.dlugosc)) { wypisz_log("[ACPI] Bledny checksum root SDT."); return false; }
    const uint32_t szer = xsdt ? 8U : 4U;
    const uint32_t liczba = (kopia.dlugosc - sizeof(SdtHeader)) / szer;
    if (liczba > MAKS_WPISOW) return false;
    uint64_t wpisy[MAKS_WPISOW] = {};
    const volatile uint8_t* dane = reinterpret_cast<const volatile uint8_t*>(root) + sizeof(SdtHeader);
    for (uint32_t i = 0; i < liczba; ++i) {
        uint64_t adres = 0;
        for (uint32_t j = 0; j < szer; ++j)
            adres |= static_cast<uint64_t>(dane[i * szer + j]) << (j * 8U);
        wpisy[i] = adres;
    }
    for (uint32_t i = 0; i < liczba; ++i) {
        SdtHeader h{};
        if (!pobierz_naglowek(wpisy[i], &h)) continue;
        log_sygnature("[ACPI] SDT ",h.podpis);
        if (podpis(h.podpis,"FACP",4)) { parsuj_fadt(wpisy[i],h); continue; }
        if (!podpis(h.podpis, "HPET", 4)) continue;
        if (h.dlugosc < sizeof(TabelaHpet)) return false;
        const TabelaHpet* t = static_cast<const TabelaHpet*>(mapuj_tabele(wpisy[i], h.dlugosc));
        if (!t || !checksum_ok(t, h.dlugosc)) return false;
        /* ACPI GAS: 0 oznacza System Memory. */
        if (t->adres.address_space != 0 || t->adres.address == 0 ||
            t->adres.bit_offset != 0) return false;
        znaleziony_hpet = t->adres.address;
    }
    return znaleziony_hpet != 0;
}

bool gas_zapisz(const Gas& g,uint32_t value) {
    if(!gas_poprawny(g))return false;
    if(g.address_space==1){
        if(g.address>0xFFFFU)return false;
        const uint16_t port=static_cast<uint16_t>(g.address);
        if(g.bit_width==8)asm volatile("outb %0,%1"::"a"(static_cast<uint8_t>(value)),"Nd"(port):"memory");
        else if(g.bit_width==16)asm volatile("outw %0,%1"::"a"(static_cast<uint16_t>(value)),"Nd"(port):"memory");
        else asm volatile("outl %0,%1"::"a"(value),"Nd"(port):"memory");
        return true;
    }
    volatile void* p=mapuj_tabele(g.address,g.bit_width/8U);if(!p)return false;
    if(g.bit_width==8)*static_cast<volatile uint8_t*>(p)=static_cast<uint8_t>(value);
    else if(g.bit_width==16)*static_cast<volatile uint16_t*>(p)=static_cast<uint16_t>(value);
    else *static_cast<volatile uint32_t*>(p)=value;
    return true;
}

} // namespace

bool acpi_inicjalizuj(uint64_t mbi) {
    parser_gotowy = true;
    znaleziony_hpet = 0;
    parser_xsdt = false;
    reset_reg={};reset_value=0;reset_valid=false;
    pm1a_cnt={};pm1b_cnt={};s5_valid=false;slp_typa=slp_typb=0;
    if (mbi == 0) return false;
    const volatile uint32_t* info = reinterpret_cast<const volatile uint32_t*>(mbi);
    const uint32_t calosc = info[0];
    if (calosc < 16 || mbi > UINT64_MAX - calosc) return false;

    const Rsdp1* old_rsdp = nullptr;
    const Rsdp2* new_rsdp = nullptr;
    uint64_t off = 8;
    while (off + sizeof(WpisTaguMB2) <= calosc) {
        const WpisTaguMB2* tag = reinterpret_cast<const WpisTaguMB2*>(mbi + off);
        if (tag->rozmiar < sizeof(WpisTaguMB2) || off + tag->rozmiar > calosc) return false;
        if (tag->typ == TAG_ACPI_NEW && tag->rozmiar >= 8 + sizeof(Rsdp2))
            new_rsdp = reinterpret_cast<const Rsdp2*>(mbi + off + 8);
        else if (tag->typ == TAG_ACPI_OLD && tag->rozmiar >= 8 + sizeof(Rsdp1))
            old_rsdp = reinterpret_cast<const Rsdp1*>(mbi + off + 8);
        if (tag->typ == MULTIBOOT_TAG_TYPE_END) break;
        off = (off + tag->rozmiar + 7ULL) & ~7ULL;
    }

    if (new_rsdp) wypisz_log("[ACPI] Multiboot2 ACPI new RSDP znaleziony.");
    else if (old_rsdp) wypisz_log("[ACPI] Multiboot2 ACPI old RSDP znaleziony.");
    else wypisz_log("[ACPI] Brak tagu RSDP w Multiboot2.");

    if (new_rsdp && podpis(new_rsdp->v1.podpis, "RSD PTR ", 8) &&
        new_rsdp->v1.rewizja >= 2 && new_rsdp->dlugosc >= sizeof(Rsdp2) &&
        new_rsdp->dlugosc <= 4096 && checksum_ok(new_rsdp, new_rsdp->dlugosc) &&
        new_rsdp->xsdt != 0 && znajdz_w_root(new_rsdp->xsdt, true)) {
        parser_xsdt = true;
        return true;
    }
    if (new_rsdp) wypisz_log("[ACPI] XSDT nie zawiera poprawnej tabeli HPET; probuje RSDT.");
    const Rsdp1* r = old_rsdp ? old_rsdp : (new_rsdp ? &new_rsdp->v1 : nullptr);
    if (r && podpis(r->podpis, "RSD PTR ", 8) && checksum_ok(r, sizeof(Rsdp1)) &&
        r->rsdt != 0 && znajdz_w_root(r->rsdt, false)) return true;
    return false;
}

bool acpi_pobierz_adres_hpet(uint64_t* adres_mmio) {
    if (!adres_mmio || !parser_gotowy || znaleziony_hpet == 0) return false;
    *adres_mmio = znaleziony_hpet;
    return true;
}

bool acpi_uzyto_xsdt() { return parser_xsdt; }

bool acpi_restart_dostepny() { return reset_valid; }
bool acpi_wykonaj_restart() {
    if(!reset_valid)return false;
    wypisz_log("[POWER] ACPI Reset Register");
    return gas_zapisz(reset_reg,reset_value);
}
bool acpi_shutdown_dostepny() { return s5_valid; }
bool acpi_wykonaj_shutdown() {
    if(!s5_valid)return false;
    wypisz_log("[POWER] Shutdown ACPI S5.");
    bool ok=gas_zapisz(pm1a_cnt,static_cast<uint32_t>(slp_typa|0x2000U));
    if(gas_poprawny(pm1b_cnt))ok=gas_zapisz(pm1b_cnt,static_cast<uint32_t>(slp_typb|0x2000U))&&ok;
    return ok;
}
