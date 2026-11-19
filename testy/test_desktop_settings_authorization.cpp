// Actual BWS authorization/file operations and actual PSF, backed by RAM.
#include "../jadro/bws/syscalls.cpp"
#include "desktop_layout_settings.h"
#include "katalogi_uzytkownika.h"
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
extern "C" bool bws_vmm_sprawdz_zakres_uzytkownika(const void* p,uint64_t,bool){return p!=nullptr;}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*){return true;}
// syscalls.cpp liczy I/O plikow procesu (rozliczanie_io.h, BWS 106); -O0 nie usuwa wywolania.
extern "C" uint64_t scheduler_generacja_procesu(int pid){return pid>0?1:0;}
uint64_t scheduler_pobierz_tick(){return 1;}
extern "C" void bws_powiadom_zmiane_pliku(const char*,uint32_t){}
proces_t tablica_procesow[MAKS_PROCESOW]{};
int aktualny_pid=1;
int bws_gui_aktywny_pid(){return 2;}
extern "C" bool loader_pobierz_argument_startowy(int,char*,size_t){return false;}
static int test_ostatni_pid=3;
uint32_t skladacz_obrazu_snapshot_okien(GuiOknoInfo* out,uint32_t max,int){
    uint32_t count=0;for(int pid=2;pid<=test_ostatni_pid && count<max;++pid){out[count]={};out[count].pid=pid;out[count].window_id=100+pid;++count;}return count;
}
// Rejestr okien podrzednych (BWS 101): pid 4 -> wlasciciel 3, pid 5 osierocony (-1).
static int test_wlasciciel[8]={};
extern "C" int okna_podrzedne_wlasciciel(int pid){return pid>0&&pid<8?test_wlasciciel[pid]:0;}
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
// Minimal layout adapter: the serializer and storage below are production code.
struct Layout {
    int count=2,workWidth=1920,workBottom=1040,dpi=100,x0=20,y0=3,cw=120,ch=124;
    struct Item{bool placed=true;Desktop::Anchor anchor;} items[2];
    struct Bounds{int x,y;};
    Bounds cell(int i)const{return {100+i*120,200+i*100};}
};
int main(){
    proces_t wm{};wm.poziom_zaufania=PZB_ZAUFANE;wm.uprawnienia=PRAWO_SYSTEM_CONFIG;
    strcpy(tablica_procesow[2].sciezka_pliku,AppIdentity::DisplaySettings);
    strcpy(tablica_procesow[3].sciezka_pliku,AppIdentity::Settings);
    GuiOknoInfo windows[16]{};
    auto snapshot=[&](const char* path,uint64_t max=16){return bws_snapshot_okien(wm,(uint64_t)windows,max,(uint64_t)path);};
    CHECK(snapshot(nullptr)==2);CHECK(snapshot(AppIdentity::Settings)==1);CHECK(windows[0].pid==3);
    CHECK(snapshot(AppIdentity::DisplaySettings)==1);CHECK(windows[0].pid==2);
    CHECK(snapshot(AppIdentity::Settings,1)==1);CHECK(windows[0].pid==3); // filter before capacity
    CHECK(snapshot("/programy/ustawienia.cebula/ustawienia.bur.extra")==0);
    CHECK(snapshot("/programy/ustawienia")==0);CHECK(snapshot(AppIdentity::Settings,0)==0);CHECK(snapshot(AppIdentity::Settings,17)==0);
    wm.uprawnienia=0;CHECK(snapshot(AppIdentity::Settings)==0);wm.uprawnienia=PRAWO_SYSTEM_CONFIG;
    {   // Tozsamosc aplikacji dla paska zadan: okno podrzedne liczy sie jako okno wlasciciela.
        strcpy(tablica_procesow[4].sciezka_pliku,"/programy/okno-informacyjne.cebula/okno-informacyjne.bur");
        strcpy(tablica_procesow[5].sciezka_pliku,"/programy/okno-informacyjne.cebula/okno-informacyjne.bur");
        test_ostatni_pid=5;test_wlasciciel[4]=3;test_wlasciciel[5]=-1;
        auto app=[&](const char* path,uint64_t mode){return bws_snapshot_okien(wm,(uint64_t)windows,16,(uint64_t)path,mode);};
        CHECK(app(AppIdentity::Settings,BWS_OKNA_TOZSAMOSC_APLIKACJI)==2&&windows[0].pid==3&&windows[1].pid==4);
        CHECK(snapshot(AppIdentity::Settings)==1&&windows[0].pid==3);           // aktywacja/uruchamianie bez zmian
        CHECK(app("/programy/okno-informacyjne.cebula/okno-informacyjne.bur",BWS_OKNA_TOZSAMOSC_APLIKACJI)==0); // sierota: zaden program
        CHECK(app(AppIdentity::DisplaySettings,BWS_OKNA_TOZSAMOSC_APLIKACJI)==1&&windows[0].pid==2);
        // Wlasciciel zakonczony, pid ponownie uzyty przez inny program: okno juz osierocone.
        test_wlasciciel[4]=-1;strcpy(tablica_procesow[3].sciezka_pliku,AppIdentity::DisplaySettings);
        CHECK(app(AppIdentity::DisplaySettings,BWS_OKNA_TOZSAMOSC_APLIKACJI)==2&&windows[0].pid==2&&windows[1].pid==3);
        strcpy(tablica_procesow[3].sciezka_pliku,AppIdentity::Settings);
        CHECK(app(AppIdentity::Settings,2)==0&&app(nullptr,BWS_OKNA_TOZSAMOSC_APLIKACJI)==0);   // zle argumenty
        wm.uprawnienia=0;CHECK(app(AppIdentity::Settings,BWS_OKNA_TOZSAMOSC_APLIKACJI)==0);wm.uprawnienia=PRAWO_SYSTEM_CONFIG;
        test_ostatni_pid=3;
    }
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
    // GUI-only font reads stay restricted to validated public asset paths.
    CHECK(utworz_katalog_z_pzb("/czcionki",PZB_USLUGI));CHECK(utworz_katalog_z_pzb("/czcionki/ttf",PZB_USLUGI));
    CHECK(utworz_plik_z_pzb("/czcionki/ttf/Ω font.ttf",PZB_USLUGI));CHECK(zapisz_do_pliku("/czcionki/ttf/Ω font.ttf","font",4));
    proces_t gui{};gui.poziom_zaufania=PZB_UZYTKOWNIK;gui.uprawnienia=PRAWO_GUI;char font_bytes[4]{};
    CHECK(bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/ttf/Ω font.ttf",0,(uint64_t)font_bytes,4)==5);CHECK(!memcmp(font_bytes,"font",4));
    CHECK(!bws_czytaj_fragment_pliku(gui,(uint64_t)"/source/copied",0,(uint64_t)font_bytes,4));
    CHECK(!bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/ttf/../x.ttf",0,(uint64_t)font_bytes,4));
    // BSF 2.2: collections are read per table, so the GUI-only read bound is
    // MaxFontFile (64 MiB). Past EOF a read returns zero bytes (1); beyond the
    // bound it is refused.
    CHECK(bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/ttf/Ω font.ttf",BSF::MaxFile,(uint64_t)font_bytes,4)==1);
    CHECK(!bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/ttf/Ω font.ttf",BSF::MaxFontFile,(uint64_t)font_bytes,4));
    CHECK(!bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/ttf/Ω font.ttf",BSF::MaxFontFile-2,(uint64_t)font_bytes,4));
    // New font directories accept exactly their own extension.
    CHECK(utworz_katalog_z_pzb("/czcionki/otf",PZB_USLUGI));
    CHECK(utworz_plik_z_pzb("/czcionki/otf/a.otf",PZB_USLUGI));CHECK(zapisz_do_pliku("/czcionki/otf/a.otf","OTTO",4));
    CHECK(utworz_plik_z_pzb("/czcionki/otf/a.ttf",PZB_USLUGI));CHECK(zapisz_do_pliku("/czcionki/otf/a.ttf","true",4));
    CHECK(bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/otf/a.otf",0,(uint64_t)font_bytes,4)==5);CHECK(!memcmp(font_bytes,"OTTO",4));
    CHECK(!bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/otf/a.ttf",0,(uint64_t)font_bytes,4));
    CHECK(!bws_czytaj_fragment_pliku(gui,(uint64_t)"/czcionki/otc/../otf/a.otf",0,(uint64_t)font_bytes,4));
    CHECK(bws_wylistuj_katalog(gui,(uint64_t)"/czcionki/otf",(uint64_t)listing.get(),BSF::DirectoryBytes));
    CHECK(!bws_utworz_plik(gui,(uint64_t)"/czcionki/otf/new.otf"));
    CHECK(bws_wylistuj_katalog(gui,(uint64_t)"/czcionki/ttf",(uint64_t)listing.get(),BSF::DirectoryBytes));
    CHECK(!bws_utworz_plik(gui,(uint64_t)"/czcionki/ttf/new.ttf"));
    gui.uprawnienia|=PRAWO_PLIKI_ZAPISZ;CHECK(!bws_utworz_plik(gui,(uint64_t)"/czcionki/ttf/new.ttf"));
    {   // Dialog plikow Notatnika: Nowy katalog w prawdziwym PSF/PZB (poziomy jak przy starcie jadra).
        for(const char* d:{"/uzytkownicy","/system"})if(!czy_katalog_istnieje(d))CHECK(utworz_katalog_z_pzb(d,PZB_JADRO));
        for(const char* d:{KatalogiUzytkownika::Domowy,KatalogiUzytkownika::Dokumenty})
            if(!czy_katalog_istnieje(d))CHECK(utworz_katalog_z_pzb(d,PZB_UZYTKOWNIK));
        proces_t n{};n.poziom_zaufania=PZB_UZYTKOWNIK;n.uprawnienia=PRAWO_GUI|PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ;
        auto mkdir=[&](const std::string& d){return WynikPlikowy(bws_utworz_katalog(n,(uint64_t)d.c_str(),BWS_STATUS_PLIKOWY));};
        const std::string DOK=KatalogiUzytkownika::Dokumenty,ZG=DOK+"/Zażółć gęślą jaźń";
        CHECK(mkdir("/uzytkownicy/Nowy katalog")==WynikPlikowy::BrakUprawnien);      // katalog systemowy
        CHECK(mkdir("/system/Nowy katalog")==WynikPlikowy::BrakUprawnien);
        CHECK(!czy_katalog_istnieje("/uzytkownicy/Nowy katalog")&&!czy_katalog_istnieje("/system/Nowy katalog"));
        CHECK(mkdir(ZG)==WynikPlikowy::Ok&&czy_katalog_istnieje(ZG.c_str()));
        CHECK(mkdir(ZG)==WynikPlikowy::Istnieje);
        CHECK(mkdir(DOK+"/brak/x")!=WynikPlikowy::Ok&&!czy_katalog_istnieje((DOK+"/brak").c_str()));
        CHECK(mkdir(ZG+"/Nowy katalog")==WynikPlikowy::Ok);
        const std::string plik=ZG+"/Nowy katalog/Łódź.txt",dane="zażółć";
        CHECK(bws_utworz_plik(n,(uint64_t)plik.c_str())&&bws_zapisz_plik(n,(uint64_t)plik.c_str(),(uint64_t)dane.data(),dane.size()));
        CHECK(bws_rozmiar_pliku(n,(uint64_t)plik.c_str())==dane.size()+1);
        CHECK(!bws_utworz_plik(n,(uint64_t)"/system/x.txt"));                         // chroniony nadal chroniony
    }
    printf("Desktop settings/overflow production BWS/PZB: %u/%u PASS (RAM PSF)\n",checks,checks);
}
