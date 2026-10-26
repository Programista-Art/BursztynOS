// Actual BWS authorization/file operations and actual PSF, backed by RAM.
#include "../syscalls.cpp"
#include "../desktop_layout_settings.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
void* kmalloc(size_t n){return std::malloc(n);}
void kfree(void* p){std::free(p);}
void wypisz_log(const char*){}
bool instalator_tryb(){return false;}
bool blokowy_zapis_instalatora(){return false;}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*){return false;}
bool skopiuj_string_z_uzytkownika(char* dst,const char* src,size_t n){if(!src||strlen(src)>=n)return false;strcpy(dst,src);return true;}
bool skopiuj_do_przestrzeni_uzytkownika(void* d,const void* s,size_t n){memcpy(d,s,n);return true;}
bool skopiuj_z_przestrzeni_uzytkownika(void* d,const void* s,size_t n){memcpy(d,s,n);return true;}
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* p,size_t){return p!=nullptr;}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*){return true;}
uint64_t scheduler_pobierz_tick(){return 1;}
extern "C" void bws_powiadom_zmiane_pliku(const char*,uint32_t){}
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
// Minimal layout adapter: the serializer and storage below are production code.
struct Layout {
    int count=2,workWidth=1920,workBottom=1040,dpi=100;
    struct Item{bool placed=true;} items[2];
    struct Bounds{int x,y;};
    Bounds cell(int i)const{return {100+i*120,200+i*100};}
};
int main(){
    std::vector<uint8_t> ram(16*1024*1024);inicjalizuj_psf(ram.data(),ram.size());
    proces_t p{};p.poziom_zaufania=PZB_USLUGI;p.uprawnienia=PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ;
    auto io=[&](uint64_t n,uint64_t a,uint64_t b,uint64_t c)->uint64_t {
        switch(n){case 2:return bws_utworz_plik(p,a);case 3:return bws_zapisz_plik(p,a,b,c);case 5:return bws_czytaj_plik(p,a,b,c);case 44:return bws_rozmiar_pliku(p,a);case 46:return bws_utworz_katalog(p,a);default:return 0;}
    };
    auto state=std::make_unique<DesktopSettings::State>();auto reload=std::make_unique<DesktopSettings::State>();Layout layout;
    auto key=[](int i)->DesktopSettings::Key{return i?DesktopSettings::Key{'F',"zażółć.txt"}:DesktopSettings::Key{'L',"notatnik"};};
    CHECK(!state->load(io));CHECK(!state->dirty);
    state->dirty=true;CHECK(state->save(layout,key,io));CHECK(!psf_test_lock_held());
    CHECK(reload->load(io));CHECK(reload->count==2);CHECK(reload->records[0].x==100);CHECK(!strcmp(reload->records[1].id,"zażółć.txt"));
    psf_metadane metadata{};CHECK(pobierz_metadane_tworu(DesktopSettings::Path,&metadata));CHECK(metadata.poziom_pzb==PZB_USLUGI);
    uint32_t original=rozmiar_pliku(DesktopSettings::Path);
    p.uprawnienia=PRAWO_PLIKI_CZYTAJ;state->dirty=true;CHECK(!state->save(layout,key,io));CHECK(!state->dirty);CHECK(rozmiar_pliku(DesktopSettings::Path)==original);CHECK(!psf_test_lock_held());
    p.uprawnienia=PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ;p.poziom_zaufania=PZB_UZYTKOWNIK;
    state->dirty=true;CHECK(!state->save(layout,key,io));CHECK(rozmiar_pliku(DesktopSettings::Path)==original);CHECK(!psf_test_lock_held());
    p.uprawnienia=0;CHECK(!reload->load(io));CHECK(reload->count==0);
    p.poziom_zaufania=PZB_USLUGI;p.uprawnienia=PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ;
    state->dirty=true;CHECK(state->save(layout,key,io));CHECK(reload->load(io));CHECK(reload->count==2);CHECK(!psf_test_lock_held());
    // Boot-created settings directories use the existing default PZB label.
    // Creation inherits that label; it must not invent a stronger policy.
    CHECK(usun_twor(DesktopSettings::Path));CHECK(usun_twor("/ustawienia"));CHECK(utworz_katalog("/ustawienia"));
    state->dirty=true;CHECK(state->save(layout,key,io));CHECK(pobierz_metadane_tworu(DesktopSettings::Path,&metadata));CHECK(metadata.poziom_pzb==PZB_UZYTKOWNIK);
    p.poziom_zaufania=PZB_PIASKOWNICA;state->dirty=true;CHECK(!state->save(layout,key,io));CHECK(!psf_test_lock_held());
    p.poziom_zaufania=PZB_USLUGI;CHECK(reload->load(io));CHECK(reload->count==2);
    for(const char* metadata:{"width=-1\n","height=99999999999999999\n","dpi=0\n","dpi=oops\n"}) {
        std::string text=std::string("BursztynDesktopLayout=1\n")+metadata;
        char end[32];snprintf(end,sizeof(end),"end=%08X\n",DesktopSettings::checksum(text.data(),text.size()));text+=end;
        memcpy(state->buffer,text.data(),text.size());CHECK(!state->parse(text.size()));CHECK(state->count==0);
    }
    // Actual filesystem success does not depend on the visual or logical grid.
    (void)bws_utworz_katalog(p,(uint64_t)"/uzytkownicy");
    CHECK(bws_utworz_katalog(p,(uint64_t)DesktopEntries::Directory));
    std::string base=std::string(DesktopEntries::Directory)+"/";
    for(int i=0;i<130;++i){auto path=base+"f"+std::to_string(i);CHECK(bws_utworz_plik(p,(uint64_t)path.c_str()));}
    CHECK(bws_utworz_katalog(p,(uint64_t)(base+"folder").c_str()));
    CHECK(utworz_katalog("/source"));CHECK(utworz_plik("/source/copied"));CHECK(utworz_plik("/source/moved"));
    CHECK(bws_kopiuj_twor(p,(uint64_t)"/source/copied",(uint64_t)DesktopEntries::Directory));
    CHECK(bws_przenies(p,(uint64_t)"/source/moved",(uint64_t)DesktopEntries::Directory));
    CHECK(pobierz_metadane_tworu("/source/copied",&metadata));CHECK(!pobierz_metadane_tworu("/source/moved",&metadata));
    CHECK(pobierz_metadane_tworu((base+"copied").c_str(),&metadata));CHECK(pobierz_metadane_tworu((base+"moved").c_str(),&metadata));
    auto listing=std::make_unique<char[]>(65536);CHECK(bws_wylistuj_katalog(p,(uint64_t)DesktopEntries::Directory,(uint64_t)listing.get(),65536));
    unsigned lines=0;for(char* c=listing.get();*c;++c)lines+=*c=='\n';CHECK(lines==133);
    // Overflow halfway through tag/name, at newline and exactly after a complete
    // entry all report failure, never a misleading successful prefix.
    const char* newline=strchr(listing.get(),'\n');CHECK(newline);
    size_t first=size_t(newline-listing.get())+1;
    for(size_t cap:{size_t(1),size_t(4),size_t(8),first,first+1,first+4}) {
        CHECK(!bws_wylistuj_katalog(p,(uint64_t)DesktopEntries::Directory,(uint64_t)listing.get(),cap));CHECK(!psf_test_lock_held());
    }
    printf("Desktop settings/overflow production BWS/PZB: %u/%u PASS (RAM PSF)\n",checks,checks);
}
