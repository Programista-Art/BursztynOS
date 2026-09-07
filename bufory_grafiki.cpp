#include "bufory_grafiki.h"
#include "pamiec.h"
namespace {
size_t rozmiary[BUFORY_GRAFIKI_SLOTOW]{};
uint64_t zajete=0, alokacje=0;
bool gotowe=false;
constexpr uint64_t LIMIT=512ULL*1024*1024;
}
bool bufory_grafiki_inicjalizuj() {
    if(gotowe) return true;
    // Pierwsza strona tworzy wspolna galaz przed klonowaniem CR3 procesow.
    void* pa=ZaalokujRamke();
    if(!pa) return false;
    if(!VmmMapujNowaStroneJadra(reinterpret_cast<void*>(BUFORY_GRAFIKI_BAZA),pa)) {
        ZwolnijRamke(pa); return false;
    }
    rozmiary[0]=4096; zajete=4096; gotowe=true; return true;
}
void* bufor_grafiki_alokuj(size_t n) {
    if(!gotowe||!n||n>BUFORY_GRAFIKI_SLOT||n>SIZE_MAX-4095) return nullptr;
    n=(n+4095)&~size_t(4095);
    if(n>LIMIT-zajete) return nullptr;
    unsigned i=1;
    for(;i<BUFORY_GRAFIKI_SLOTOW&&rozmiary[i];++i) {}
    if(i==BUFORY_GRAFIKI_SLOTOW) return nullptr;
    const uint64_t va=BUFORY_GRAFIKI_BAZA+uint64_t(i)*BUFORY_GRAFIKI_SLOT;
    rozmiary[i]=n; zajete+=n;
    size_t off=0;
    for(;off<n;off+=4096) {
        void* pa=ZaalokujRamke();
        if(!pa) break;
        if(!VmmMapujNowaStroneJadra(reinterpret_cast<void*>(va+off),pa)) { ZwolnijRamke(pa); break; }
    }
    if(off!=n) {
        rozmiary[i]=off; zajete-=n-off;
        bufor_grafiki_zwolnij(reinterpret_cast<void*>(va));
        return nullptr;
    }
    ++alokacje;
    return reinterpret_cast<void*>(va);
}
void bufor_grafiki_zwolnij(void* p) {
    const uint64_t va=reinterpret_cast<uint64_t>(p);
    if(va<=BUFORY_GRAFIKI_BAZA||va>=BUFORY_GRAFIKI_BAZA+uint64_t(BUFORY_GRAFIKI_SLOTOW)*BUFORY_GRAFIKI_SLOT||
        (va-BUFORY_GRAFIKI_BAZA)%BUFORY_GRAFIKI_SLOT) return;
    const unsigned i=(va-BUFORY_GRAFIKI_BAZA)/BUFORY_GRAFIKI_SLOT;
    const size_t n=rozmiary[i];
    for(size_t off=0;off<n;off+=4096) {
        void* pa=VmmOdmapujBuforGrafiki(reinterpret_cast<void*>(va+off));
        if(pa) ZwolnijRamke(pa);
    }
    zajete-=n; rozmiary[i]=0;
}
uint64_t bufory_grafiki_bajty(){return zajete;}
uint64_t bufory_grafiki_alokacje(){return alokacje;}
