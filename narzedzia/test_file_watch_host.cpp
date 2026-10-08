// Actual production watcher registration and publication; no duplicate parser.
#include "../bws_nowoczesne.cpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
proces_t tablica_procesow[PZB_MAKS_PROCESOW]{};
int aktualny_pid=1;
void* kmalloc(size_t n){return std::malloc(n);}
void kfree(void* p){std::free(p);}
bool skopiuj_string_z_uzytkownika(char* d,const char* s,size_t n){if(!s||strlen(s)>=n)return false;strcpy(d,s);return true;}
bool czy_bezpieczny_zakres_uzytkownika_do_odczytu(const void* p,size_t){return p!=nullptr;}
bool pobierz_metadane_tworu(const char*,psf_metadane* m){*m={};m->pzb_dostepny=true;m->poziom_pzb=PZB_UZYTKOWNIK;return true;}
bool hpet_dostepny(){return false;}
uint64_t czas_monotoniczny_ns(){return 0;}
uint64_t scheduler_pobierz_tick(){return 42;}
static std::vector<bws_zdarzenie> published;
bool scheduler_dodaj_zdarzenie(int,const bws_zdarzenie* e){published.push_back(*e);return true;}
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
int main(){
    auto& p=tablica_procesow[1];p.pid=1;p.poziom_zaufania=PZB_UZYTKOWNIK;p.uprawnienia=PRAWO_PLIKI_CZYTAJ;
    for(const char* path:{"/desktop","/desktop/tree","/desktop/tree/child","/desktop/tree/child/file","/desktop/tree2","/other"})
        CHECK(watch_dispatch(p,BWS_FILE_WATCH_DODAJ,uint64_t(path),BWS_FILE_DELETE));
    CHECK(!watch_dispatch(p,BWS_FILE_WATCH_DODAJ,uint64_t("/desktop"),BWS_POWIADOM_PODDRZEWO));
    bws_powiadom_zmiane_pliku("/desktop/tree",BWS_FILE_DELETE|BWS_POWIADOM_PODDRZEWO);
    CHECK(published.size()==4);
    for(unsigned i=0;i<4;++i){CHECK(published[i].typ==BWS_ZDARZENIE_FILE_WATCH);CHECK(published[i].kod==BWS_FILE_DELETE);CHECK(published[i].x==int(i+1));CHECK(published[i].timestamp==42);}
    published.clear();bws_powiadom_zmiane_pliku("/desktop/tree",BWS_FILE_DELETE);CHECK(published.size()==2);
    published.clear();bws_powiadom_zmiane_pliku("/desktop/tree/child/file",BWS_FILE_MODIFY);CHECK(published.empty());
    p.uprawnienia=0;bws_powiadom_zmiane_pliku("/desktop/tree",BWS_FILE_DELETE|BWS_POWIADOM_PODDRZEWO);CHECK(published.empty());
    p.uprawnienia=PRAWO_PLIKI_CZYTAJ;p.poziom_zaufania=PZB_PIASKOWNICA;
    bws_powiadom_zmiane_pliku("/desktop/tree",BWS_FILE_DELETE|BWS_POWIADOM_PODDRZEWO);CHECK(published.empty());
    for(auto& w:watchers){kfree(w.sciezka);w={};}
    printf("File watching / subtree coalescing: %u/%u PASS\n",checks,checks);
}
