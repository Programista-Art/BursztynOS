#include "../jadro/bws/bws_nowoczesne.cpp"
#include <cassert>
#include <cstring>
#include <cstdio>
volatile bool ekran_zajety=false;
static DisplayGeometry active=display_geometry(1920,1080,125,7);
DisplayGeometry grafika_geometria_ekranu(){return active;}
int grafika_pobierz_szerokosc(){return active.width;}
int grafika_pobierz_wysokosc(){return active.height;}
void grafika_ustaw_pasek_zadan_gotowy(bool){}
bool czy_bezpieczny_zakres_uzytkownika_do_zapisu(void* p,size_t){return p!=nullptr;}
bool skopiuj_do_przestrzeni_uzytkownika(void* d,const void* s,size_t n){memcpy(d,s,n);return true;}
int main(){
 proces_t process{};process.poziom_zaufania=PZB_ZAUFANE;process.uprawnienia=PRAWO_GUI|PRAWO_SYSTEM_CONFIG;
 bws_aktualizuj_ekran(active.scale);
 GuiMonitorInfo legacy{};DisplayGeometry snapshot{};
 assert(monitor_info(process,0,uint64_t(&legacy))==1);
 assert(legacy.szer==1920 && legacy.wys==1080 && legacy.work_wys==1030 && legacy.scale_x1000==1250);
 assert(monitor_info(process,0,uint64_t(&snapshot),sizeof(snapshot))==1);
 assert(snapshot.generation==7 && snapshot.cursor_max_y==1079 && snapshot.work_height==1030);
 assert(!monitor_info(process,0,uint64_t(&snapshot),sizeof(snapshot)-1));
 assert(!monitor_info(process,1,uint64_t(&snapshot),sizeof(snapshot)));
 assert(!ustaw_work_area(process,0,0,1920,1030,6)); // delayed old WM acknowledgement
 assert(!ustaw_work_area(process,0,0,1920,1040,7)); // wrong scale/work area
 assert(ustaw_work_area(process,0,0,1920,1030,7));
 assert(monitor_info(process,0,uint64_t(&snapshot),sizeof(snapshot))==1 && snapshot.desktop_generation==7);
 active=display_geometry(1280,720,100,8);bws_aktualizuj_ekran(100);
 assert(!ustaw_work_area(process,0,0,1280,680,7));
 assert(monitor_info(process,0,uint64_t(&snapshot),sizeof(snapshot))==1 && snapshot.generation==8 && snapshot.desktop_generation==7);
 assert(snapshot.cursor_max_y==719 && snapshot.taskbar_y==680);
 process.uprawnienia=0;
 assert(!monitor_info(process,0,uint64_t(&snapshot),sizeof(snapshot)));
 assert(!ustaw_work_area(process,0,0,1280,680,8));
 puts("Display snapshot ABI / stale generation / authorization: PASS");
}
