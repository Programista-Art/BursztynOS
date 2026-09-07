#include "bochs_vbe.h"
#include "../../pci.h"
namespace {
bool wykryty=false;
uint64_t pamiec=0;
uint16_t max_x=0,max_y=0,max_bpp=0;
void out16(uint16_t p,uint16_t v){asm volatile("outw %0,%1"::"a"(v),"Nd"(p));}
uint16_t in16(uint16_t p){uint16_t v;asm volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
uint16_t odczyt(uint16_t i){out16(0x1ce,i);return in16(0x1cf);}
void zapis(uint16_t i,uint16_t v){out16(0x1ce,i);out16(0x1cf,v);}
}
bool bochs_wykryj(uint64_t lfb,const TrybEkranu& boot,uint64_t* vram) {
    // PCI identification and exact boot BAR binding BEFORE touching DISPI.
    bool pci=false;
    for(unsigned bus=0;bus<256&&!pci;++bus) for(unsigned slot=0;slot<32&&!pci;++slot) {
        if(pci_odczytaj_dword(bus,slot,0,0)!=0x11111234U) continue;
        const uint32_t cls=pci_odczytaj_dword(bus,slot,0,8);
        const uint32_t bar=pci_odczytaj_dword(bus,slot,0,0x10);
        if((cls>>16)!=0x0300U || (bar&7)!=0 || uint64_t(bar&~15U)!=lfb) continue;
        uint32_t cmd = pci_odczytaj_dword(bus,slot,0,4);
        if(!(cmd & 1)) {
            cmd |= 0x0003;
            pci_zapisz_dword(bus,slot,0,4,cmd);
        }
        if(!(pci_odczytaj_word(bus,slot,0,4)&1)) continue;
        pci=true;
    }
    if(!pci) return false;
    const uint16_t id=odczyt(0);
    if(id<0xb0c4 || id>0xb0c5) return false;
    // Require the currently active linear mode to agree with the boot tag.
    const uint16_t enable=odczyt(4);
    if(!(enable&1) || odczyt(1)!=boot.szerokosc || odczyt(2)!=boot.wysokosc ||
        odczyt(3)!=32 || uint32_t(odczyt(6))*4!=boot.pitch || odczyt(8)||odczyt(9)) return false;
    pamiec=uint64_t(odczyt(10))*65536;
    size_t n=0;
    if(!ekran_rozmiar(boot,&n)||pamiec<n||pamiec>256ULL*1024*1024) return false;
    // GETCAPS preserves ENABLED; no SetMode or firmware call.
    zapis(4,enable|2|0x80);
    max_x=odczyt(1);max_y=odczyt(2);max_bpp=odczyt(3);
    zapis(4,enable);
    if(max_x<boot.szerokosc||max_y<boot.wysokosc||max_bpp<32) return false;
    wykryty=true;
    if(vram)*vram=pamiec;
    extern void klog_serial(const char*);
    char buf[128]{};size_t p=0;
    auto str=[&](const char* s){while(*s&&p+1<sizeof(buf))buf[p++]=*s++;};
    auto num=[&](uint64_t v){char r[24];unsigned k=0;do{r[k++]=char('0'+v%10);v/=10;}while(v);while(k&&p+1<sizeof(buf))buf[p++]=r[--k];};
    str("[BOCHS VBE] VRAM: ");num(pamiec);str(" B, max: ");num(max_x);str("x");num(max_y);str("@");num(max_bpp);buf[p]=0;
    klog_serial(buf);
    return true;
}
bool bochs_sprawdz_tryb(const TrybEkranu&t) {
    size_t n=0;
    // QEMU DISPI rounds XRES down to a multiple of eight, including 1366.
    return wykryty && t.szerokosc>=320 && t.wysokosc>=200 && !(t.szerokosc&7) &&
        t.szerokosc<=max_x && t.wysokosc<=max_y && t.bpp==32 &&
        t.pitch==uint64_t(t.szerokosc)*4 && !t.odswiezanie && !t.flagi &&
        ekran_rozmiar(t,&n) && n<=pamiec;
}
bool bochs_ustaw_tryb(const TrybEkranu&t) {
    if(!bochs_sprawdz_tryb(t))return false;
    zapis(4,0);zapis(1,t.szerokosc);zapis(2,t.wysokosc);zapis(3,32);
    zapis(4,0xc1); // ENABLED | LFB | NOCLEARMEM, cleared by prepared full redraw.
    zapis(6,t.szerokosc);zapis(8,0);zapis(9,0);
    return (odczyt(4)&0x41)==0x41 && odczyt(1)==t.szerokosc && odczyt(2)==t.wysokosc &&
        odczyt(3)==32 && uint32_t(odczyt(6))*4==t.pitch && odczyt(7)>=t.wysokosc &&
        !odczyt(8)&&!odczyt(9);
}
