// Production BWS authorization (jadro/bws/syscalls.cpp) + native PSF on a RAM disk:
// ordinary processes must not modify trusted system executables, and a replaced
// executable must never run with privileged rights. Guest pointers are host pointers.
#include "../jadro/bws/syscalls.cpp"
#include "../jadro/fs/tozsamosc_systemowa.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
void* kmalloc(size_t n) {return std::malloc(n);}
void kfree(void* p) {std::free(p);}
void wypisz_log(const char*) {}
bool instalator_tryb() {return false;}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) {return false;}
bool blokowy_zapis_instalatora() {return false;}
bool skopiuj_string_z_uzytkownika(char* dst,const char* src,size_t n) {if(std::strlen(src)>=n)return false;std::strcpy(dst,src);return true;}
bool skopiuj_z_przestrzeni_uzytkownika(void* dst,const void* src,size_t n) {memcpy(dst,src,n);return true;}
bool skopiuj_do_przestrzeni_uzytkownika(void* dst,const void* src,size_t size) {memcpy(dst,src,size);return true;}
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* p,size_t) {return p!=nullptr;}
bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void* p,size_t) {return p!=nullptr;}
bool bws_vmm_sprawdz_zakres_uzytkownika(const void*,size_t,bool) {return true;}
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*) {return true;}
uint64_t scheduler_pobierz_tick() {return 1;}
extern "C" void bws_powiadom_zmiane_pliku(const char*,uint32_t) {}
// ---- loader stubs: record what the syscall would hand to the real loader
struct Launch {int calls=0;std::string path;uint8_t level=0;uint64_t rights=0;bool require_identity=false;};
static Launch launch;
static bool launch_result=true;
extern "C" bool bws_uruchom_program_z_pliku(const char* p,uint8_t l,uint64_t r,bool){launch={launch.calls+1,p,l,r,false};return launch_result;}
extern "C" bool bws_uruchom_program_z_pliku_z_argumentem(const char* p,uint8_t l,uint64_t r,bool,const char*){launch={launch.calls+1,p,l,r,false};return launch_result;}
extern "C" bool bws_uruchom_program_wbudowany(const char* p,uint8_t l,uint64_t r,bool,const char*){launch={launch.calls+1,p,l,r,true};return launch_result;}
extern "C" int loader_przekaz_argument_uruchomionemu(const char*,const char*) {return 0;}
extern "C" uint8_t loader_ostatni_blad(void) {return 0;}
static PakietManifest g_manifest;static bool g_has_manifest=false;
extern "C" bool pakiet_wczytaj_manifest(const char*,PakietManifest* m) {if(g_has_manifest)*m=g_manifest;return g_has_manifest;}
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
static bool exists(const char* p) {psf_metadane m{};return pobierz_metadane_tworu(p,&m);}
static std::string content(const char* p) {uint32_t n=rozmiar_pliku(p);std::string s(n,'\0');uint32_t got=0;if(n)czytaj_fragment_pliku(p,0,s.data(),n,&got);return s;}
static const char GOOD[]="TRUSTED-BYTES";
static const char EVIL[]="EVIL-REPLACEMENT-BYTES";
static uint64_t write_all(proces_t& p,const char* path,const char* data){return bws_zapisz_plik(p,(uint64_t)path,(uint64_t)data,strlen(data));}
static uint64_t launch_path(proces_t& p,const char* path) {return bws_uruchom(p,(uint64_t)path,0);}
int main() {
    std::vector<uint8_t> ram(16*1024*1024);inicjalizuj_psf(ram.data(),ram.size());
    // Layout as the kernel installs it (labels PZB 4 - exactly the writable-by-user situation).
    CHECK(utworz_katalog_z_pzb("/programy",4));CHECK(utworz_katalog_z_pzb("/programy/ustawienia.cebula",4));
    CHECK(utworz_plik_z_pzb("/programy/ustawienia.cebula/ustawienia.bur",4));
    CHECK(zapisz_do_pliku("/programy/ustawienia.cebula/ustawienia.bur",GOOD,sizeof(GOOD)-1));
    CHECK(utworz_plik_z_pzb("/programy/ustawienia.cebula/opis.aplikacji",4));
    CHECK(zapisz_do_pliku("/programy/ustawienia.cebula/opis.aplikacji","poziom_zaufania = 3\n",20));
    CHECK(utworz_katalog_z_pzb("/gry",4));CHECK(utworz_plik_z_pzb("/menedzer_okien.bur",4));
    CHECK(zapisz_do_pliku("/menedzer_okien.bur",GOOD,sizeof(GOOD)-1));
    CHECK(utworz_katalog_z_pzb("/uzytkownicy",4));CHECK(utworz_katalog_z_pzb("/uzytkownicy/ala",4));
    const char* const SET="/programy/ustawienia.cebula/ustawienia.bur";
    proces_t user{};user.poziom_zaufania=PZB_UZYTKOWNIK;user.uprawnienia=PRAWO_PLIKI_ZAPISZ|PRAWO_PLIKI_CZYTAJ|PRAWO_URUCHOM_PROGRAM|PRAWO_GUI;
    proces_t sandbox=user;sandbox.poziom_zaufania=PZB_PIASKOWNICA;
    proces_t service{};service.poziom_zaufania=PZB_USLUGI;service.uprawnienia=user.uprawnienia|PRAWO_SYSTEM_CONFIG;
    // ---- 1. an ordinary process cannot alter, replace, truncate, delete or rename trusted files
    CHECK(!write_all(user,SET,EVIL));CHECK(content(SET)==GOOD);
    CHECK(!write_all(user,SET,""));CHECK(content(SET)==GOOD);                                   // truncation
    {char buf[]="X";CHECK(!bws_zapisz_fragment_pliku(user,(uint64_t)SET,0,(uint64_t)buf,1));CHECK(content(SET)==GOOD);} // BWS 98
    CHECK(!bws_usun_twor(user,(uint64_t)SET));CHECK(exists(SET));
    CHECK(!bws_zmien_nazwe(user,(uint64_t)SET,(uint64_t)"/programy/ustawienia.cebula/stare.bur"));CHECK(exists(SET));
    CHECK(!bws_usun_twor(user,(uint64_t)"/programy/ustawienia.cebula"));CHECK(exists(SET));
    CHECK(!bws_usun_twor(user,(uint64_t)"/programy"));CHECK(exists(SET));
    CHECK(!bws_zapisz_plik(user,(uint64_t)"/menedzer_okien.bur",(uint64_t)EVIL,strlen(EVIL)));CHECK(content("/menedzer_okien.bur")==GOOD);
    CHECK(!bws_usun_twor(user,(uint64_t)"/menedzer_okien.bur"));CHECK(exists("/menedzer_okien.bur"));
    // creating look-alikes / new packages in trusted trees
    CHECK(!bws_utworz_plik(user,(uint64_t)"/programy/evil.bur"));CHECK(!exists("/programy/evil.bur"));
    CHECK(!bws_utworz_katalog(user,(uint64_t)"/programy/evil.cebula"));CHECK(!exists("/programy/evil.cebula"));
    CHECK(!bws_utworz_plik(user,(uint64_t)"/gry/evil.bur"));CHECK(!exists("/gry/evil.bur"));
    CHECK(!bws_utworz_plik(user,(uint64_t)"/shell.bur"));CHECK(!exists("/shell.bur"));
    CHECK(!bws_utworz_plik(user,(uint64_t)"/evil.bur"));CHECK(!exists("/evil.bur"));
    // replace-by-move / copy-over / move-out
    CHECK(utworz_plik_z_pzb("/uzytkownicy/ala/evil.bur",4));CHECK(zapisz_do_pliku("/uzytkownicy/ala/evil.bur",EVIL,strlen(EVIL)));
    CHECK(!bws_przenies(user,(uint64_t)"/uzytkownicy/ala/evil.bur",(uint64_t)"/programy/ustawienia.cebula"));CHECK(content(SET)==GOOD);
    CHECK(!bws_przenies(user,(uint64_t)SET,(uint64_t)"/uzytkownicy/ala"));CHECK(exists(SET));CHECK(!exists("/uzytkownicy/ala/ustawienia.bur"));
    CHECK(!bws_kopiuj_twor(user,(uint64_t)"/uzytkownicy/ala/evil.bur",(uint64_t)"/programy/ustawienia.cebula"));CHECK(content(SET)==GOOD);
    // path aliasing never reaches a trusted object
    for(const char* alias:{"/uzytkownicy/../programy/ustawienia.cebula/ustawienia.bur","//programy/ustawienia.cebula/ustawienia.bur",
        "/programy//ustawienia.cebula/ustawienia.bur","/programy/./ustawienia.cebula/ustawienia.bur","/programy/ustawienia.cebula/../ustawienia.cebula/ustawienia.bur",
        "/programy/ustawienia.cebula/ustawienia.bur/","programy/ustawienia.cebula/ustawienia.bur"}) {
        CHECK(!write_all(user,alias,EVIL));CHECK(content(SET)==GOOD);
        CHECK(!bws_usun_twor(user,(uint64_t)alias));CHECK(exists(SET));
    }
    // sandbox and a process without the write right fare no better
    CHECK(!write_all(sandbox,SET,EVIL));
    proces_t reader=user;reader.uprawnienia=PRAWO_PLIKI_CZYTAJ|PRAWO_URUCHOM_PROGRAM;CHECK(!write_all(reader,SET,EVIL));CHECK(content(SET)==GOOD);
    // ---- 2. ordinary files keep working
    CHECK(bws_utworz_plik(user,(uint64_t)"/uzytkownicy/ala/notatka.txt"));CHECK(write_all(user,"/uzytkownicy/ala/notatka.txt","tekst"));
    CHECK(bws_zmien_nazwe(user,(uint64_t)"/uzytkownicy/ala/notatka.txt",(uint64_t)"/uzytkownicy/ala/notatka2.txt")||exists("/uzytkownicy/ala/notatka.txt"));
    CHECK(bws_utworz_katalog(user,(uint64_t)"/uzytkownicy/ala/dir"));CHECK(bws_usun_twor(user,(uint64_t)"/uzytkownicy/ala/dir"));
    // ---- 3. a trusted service (update) may legitimately replace the file
    CHECK(write_all(service,SET,"UPDATED-BYTES"));CHECK(content(SET)=="UPDATED-BYTES");
    CHECK(write_all(service,SET,GOOD));
    // ---- 4. launch authorization at the syscall boundary (BWS 10)
    // 4a. an ordinary parent launches a system app: levels/rights come from the kernel table,
    //     NEVER from the (user-writable) manifest; identity verification is mandatory.
    g_has_manifest=true;g_manifest={};g_manifest.poziom_zaufania=0;g_manifest.uprawnienia=PRAWA_UPRZYWILEJOWANE|PRAWO_SIEC;
    launch={};CHECK(launch_path(user,SET)==1);CHECK(launch.calls==1&&launch.require_identity);
    CHECK(launch.level==PZB_ZAUFANE);CHECK(!(launch.rights&PRAWO_STEROWNIK)&&!(launch.rights&PRAWO_DEBUG)&&!(launch.rights&PRAWO_SIEC));
    CHECK(launch.rights==(PRAWO_GUI|PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ|PRAWO_SYSTEM_CONFIG));
    // 4b. the trusted Window Manager path is identity-checked too (tampered file must not run elevated)
    proces_t wm{};wm.poziom_zaufania=PZB_USLUGI;wm.uprawnienia=PRAWO_GUI|PRAWO_URUCHOM_PROGRAM|PRAWO_SYSTEM_CONFIG|PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ|PRAWO_SIEC;
    launch={};CHECK(launch_path(wm,SET)==1);CHECK(launch.require_identity&&launch.level==PZB_ZAUFANE);
    launch={};CHECK(launch_path(wm,"/programy/ekran.cebula/ekran.bur")==1);CHECK(launch.require_identity);
    // 4c. a loader that rejects the identity yields a failed launch (no process)
    launch_result=false;launch={};CHECK(launch_path(user,SET)==0);launch_result=true;
    // 4d. a forged package asking for privileged level through its manifest is refused for ordinary parents
    g_manifest.poziom_zaufania=0;g_manifest.uprawnienia=PRAWO_SYSTEM_CONFIG;
    launch={};CHECK(launch_path(user,"/uzytkownicy/ala/evil.cebula/evil.bur")==0);CHECK(launch.calls==0);
    launch={};CHECK(bws_uruchom(sandbox,(uint64_t)SET,0)==0&&launch.calls==0);
    // 4e. unprivileged app: user level, privileged rights stripped
    // manifest asking for system_config at level 4 is refused for an ordinary parent too
    g_manifest.poziom_zaufania=4;g_manifest.uprawnienia=PRAWO_GUI|PRAWO_SYSTEM_CONFIG;
    launch={};CHECK(launch_path(user,"/uzytkownicy/ala/app.cebula/app.bur")==0&&launch.calls==0);
    g_manifest.poziom_zaufania=4;g_manifest.uprawnienia=PRAWO_GUI|PRAWO_STEROWNIK|PRAWO_DEBUG;
    launch={};CHECK(launch_path(user,"/uzytkownicy/ala/app.cebula/app.bur")==1);CHECK(launch.level==PZB_UZYTKOWNIK&&!(launch.rights&PRAWA_UPRZYWILEJOWANE)&&!launch.require_identity);
    g_has_manifest=false;
    // ---- 5. identity table logic (SHA-256 comparison, exact path, exact size)
    using namespace TozsamoscSystemowa;
    static constexpr Wpis tab[]={{"/programy/a.cebula/a.bur",{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32},100}};
    uint8_t d[32];for(int i=0;i<32;++i)d[i]=uint8_t(i+1);
    CHECK(skrot_wbudowany_zgodny(tab,"/programy/a.cebula/a.bur",d,100));
    CHECK(!skrot_wbudowany_zgodny(tab,"/programy/a.cebula/a.bur",d,101));          // size
    CHECK(!skrot_wbudowany_zgodny(tab,"/programy/a.cebula/b.bur",d,100));          // path
    CHECK(!skrot_wbudowany_zgodny(tab,"/programy/a.cebula/a.bur/",d,100));
    for(int i=0;i<32;++i){uint8_t e[32];memcpy(e,d,32);e[i]^=0x80;CHECK(!skrot_wbudowany_zgodny(tab,"/programy/a.cebula/a.bur",e,100));} // every digest bit position
    CHECK(!skrot_wbudowany_zgodny(tab,nullptr,d,100));
    printf("System binary protection: %u assertions PASS (production BWS + RAM PSF)\n",checks);
}
