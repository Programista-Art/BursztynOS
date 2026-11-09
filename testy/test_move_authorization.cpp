// Production BWS authorization + native PSF; guest pointers copied by a host stub.
#include "../jadro/bws/syscalls.cpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
void* kmalloc(size_t n) {return std::malloc(n);}
void wypisz_log(const char*) {}
bool instalator_tryb() {return false;}
bool pobierz_czas_rtc_bezpiecznie(czas_rtc*) {return false;}
bool blokowy_zapis_instalatora() {return false;}
bool skopiuj_string_z_uzytkownika(char* dst,const char* src,size_t n) {if(std::strlen(src)>=n)return false;std::strcpy(dst,src);return true;}
bool skopiuj_do_przestrzeni_uzytkownika(void* dst,const void* src,size_t size) {memcpy(dst,src,size);return true;}
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* p,size_t) {return p!=nullptr;}
static unsigned notifications=0;
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie*) {++notifications;return true;}
uint64_t scheduler_pobierz_tick() {return 1;}
extern "C" void bws_powiadom_zmiane_pliku(const char*,uint32_t) {++notifications;}
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
static bool exists(const char* p) {psf_metadane m{};return pobierz_metadane_tworu(p,&m);}
static uint64_t move(proces_t& p,const char* a,const char* b) {return bws_przenies(p,(uint64_t)a,(uint64_t)b);}
int main() {
    std::vector<uint8_t> ram(16*1024*1024);inicjalizuj_psf(ram.data(),ram.size());
    proces_t p{};p.poziom_zaufania=PZB_UZYTKOWNIK;p.uprawnienia=PRAWO_PLIKI_ZAPISZ;
    CHECK(utworz_katalog_z_pzb("/desktop",4));CHECK(utworz_katalog_z_pzb("/dest",4));
    CHECK(utworz_katalog_z_pzb("/desktop/denied",3));CHECK(utworz_katalog_z_pzb("/desktop/allowed",4));CHECK(utworz_katalog_z_pzb("/denied",3));
    CHECK(!move(p,"/desktop/denied","/dest"));CHECK(exists("/desktop/denied"));CHECK(!exists("/dest/denied"));CHECK(!psf_test_lock_held());CHECK(!notifications);
    CHECK(!move(p,"/desktop/allowed","/denied"));CHECK(exists("/desktop/allowed"));CHECK(!exists("/denied/allowed"));CHECK(!psf_test_lock_held());CHECK(!notifications);
    p.uprawnienia=0;CHECK(!move(p,"/desktop/allowed","/dest"));CHECK(exists("/desktop/allowed"));CHECK(!exists("/dest/allowed"));CHECK(!notifications);
    p.uprawnienia=PRAWO_PLIKI_ZAPISZ;CHECK(move(p,"/desktop/allowed","/dest"));CHECK(!exists("/desktop/allowed"));CHECK(exists("/dest/allowed"));CHECK(notifications>0);
    CHECK(utworz_plik_z_pzb("/desktop/file",4));CHECK(move(p,"/desktop/file","/dest"));CHECK(exists("/dest/file"));CHECK(!psf_test_lock_held());
    static char paths[2][512]={"/dest/file","/desktop/denied"};
    p.uprawnienia=PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ;
    CHECK(bws_ustaw_schowek_plikow(p,(uint64_t)paths,BWS_SCHOWEK_COPY,2));
    BwsSchowekPlikow record{};CHECK(bws_pobierz_schowek_plikow(p,(uint64_t)&record,0,0));CHECK(record.zarezerwowane==2);
    auto generation=record.generacja;
    CHECK(bws_pobierz_schowek_plikow(p,(uint64_t)&record,1,generation));CHECK(!strcmp(record.sciezka,paths[1]));
    CHECK(!bws_pobierz_schowek_plikow(p,(uint64_t)&record,2,generation));CHECK(!bws_pobierz_schowek_plikow(p,(uint64_t)&record,0,generation+1));
    CHECK(!bws_ustaw_schowek_plikow(p,(uint64_t)paths,BWS_SCHOWEK_CUT,2));CHECK(schowek_plikow.generacja==generation);CHECK(!schowek_blokada);
    CHECK(!bws_wyczysc_schowek_plikow(p,generation,1)); // COPY cannot lose a prefix
    snprintf(paths[1],512,"/dest/allowed");
    CHECK(bws_ustaw_schowek_plikow(p,(uint64_t)paths,BWS_SCHOWEK_CUT,2));generation=schowek_plikow.generacja;
    CHECK(bws_wyczysc_schowek_plikow(p,generation,1));CHECK(schowek_plikow.zarezerwowane==1);CHECK(!strcmp(schowek_plikow.sciezka,paths[1]));
    CHECK(!bws_wyczysc_schowek_plikow(p,generation,0));CHECK(!schowek_blokada);
    CHECK(bws_wyczysc_schowek_plikow(p,schowek_plikow.generacja,0));CHECK(schowek_plikow.operacja==BWS_SCHOWEK_PUSTY);
    snprintf(paths[1],512,"%s",paths[0]);CHECK(!bws_ustaw_schowek_plikow(p,(uint64_t)paths,BWS_SCHOWEK_COPY,2));
    CHECK(!bws_ustaw_schowek_plikow(p,(uint64_t)paths,BWS_SCHOWEK_COPY,BWS_SCHOWEK_MAX_PLIKOW+1));
    p.uprawnienia=0;CHECK(!bws_ustaw_schowek_plikow(p,(uint64_t)paths,BWS_SCHOWEK_COPY,1));CHECK(!schowek_blokada);
    static char many[BWS_SCHOWEK_MAX_PLIKOW][512]{};
    p.uprawnienia=PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ;
    for(unsigned i=0;i<BWS_SCHOWEK_MAX_PLIKOW;++i){snprintf(many[i],512,"/desktop/item_%u",i);CHECK(utworz_plik_z_pzb(many[i],4));}
    CHECK(bws_ustaw_schowek_plikow(p,(uint64_t)many,BWS_SCHOWEK_CUT,BWS_SCHOWEK_MAX_PLIKOW));
    generation=schowek_plikow.generacja;CHECK(schowek_plikow.zarezerwowane==BWS_SCHOWEK_MAX_PLIKOW);
    for(unsigned i=0;i<BWS_SCHOWEK_MAX_PLIKOW;++i){CHECK(bws_pobierz_schowek_plikow(p,(uint64_t)&record,i,generation));CHECK(!strcmp(record.sciezka,many[i]));}
    CHECK(bws_wyczysc_schowek_plikow(p,generation,0));CHECK(!schowek_blokada);
    printf("Production BWS move authorization: %u assertions PASS (RAM)\n",checks);
}
