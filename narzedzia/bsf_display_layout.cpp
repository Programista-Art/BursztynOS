#define BSF_NATIVE 1
#include "testy/bursztyn_ui_host_fixture.h"
#include "programy/ekran/ekran_okno.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace BursztynUI;
void rect(const char* name,Prostokat r){printf("\"%s\":[%d,%d,%d,%d],",name,r.x,r.y,r.szerokosc,r.wysokosc);}
static std::map<std::string,std::string> fonts;
void wypisz(const char*){}
bool czytaj_plik(const char* path,char* out,uint32_t n){auto& bytes=fonts[path];if(bytes.empty()||bytes.size()>n)return false;memcpy(out,bytes.data(),bytes.size());return true;}
static uint64_t hook(uint64_t fn,uint64_t a,uint64_t b,uint64_t c,uint64_t){
 if(fn==92){BSF::Config cfg;cfg.checksum=BSF::checksum(cfg);*(BSF::Config*)b=cfg;return 1;}
 if(fn==4){*reinterpret_cast<uint32_t*>(b)=fonts[(const char*)a].size();return *reinterpret_cast<uint32_t*>(b)>0;}
 if(fn==6){std::string d;for(auto& f:fonts)d+="[F] "+f.first.substr(14)+"\n";if(d.size()+1>c)return 0;memcpy((void*)b,d.c_str(),d.size()+1);return 1;}
 return 0;}
int main(){g_test_bws_hook=hook;
for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf","DejaVuSerifCondensed-Italic.ttf"}){std::ifstream f(std::string("czcionki/ttf/")+name,std::ios::binary);fonts[std::string("/czcionki/ttf/")+name]=std::string(std::istreambuf_iterator<char>(f),{});}
for(unsigned scale:{100u,125u,150u,175u,200u})for(bool pending:{false,true}){
 ui_skala_procent=scale;OknoEkranu w;EkranOdpowiedz st{};st.mozliwosci.mozna_zmienic_runtime=true;st.liczba_trybow=10;st.transakcja=pending;
 w.inicjalizuj(st);w.ustaw_skale_dpi(SkalaUI::procent(scale));w.ustaw_rozmiar(ui_skaluj(640,scale),ui_skaluj(EKRAN_OKNO_NORMALNE_H,scale));w.przelicz_uklad();
 char path[128];snprintf(path,sizeof(path),".bsf-test/display-layout-%u%s.json",scale,pending?"-pending":"");freopen(path,"w",stdout);printf("{");rect("mode",w.lista_trybow().obszar_ekranu());rect("scale",w.lista_skal().obszar_ekranu());rect("apply",w.przycisk_zastosuj().obszar_ekranu());rect("keep",w.przycisk_zachowaj().obszar_ekranu());printf("\"row\":%d,\"first\":2}\n",BSF::height(ui_skaluj(16,scale))+6*SkalaUI::procent(scale));
}}
bool ipc_send(uint64_t,const BwsIpcWyslij*){return false;}
bool ipc_receive(uint64_t,BwsIpcOdbierz*){return false;}
