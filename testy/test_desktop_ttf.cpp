#define BSF_NATIVE 1
#include "../bursztyn_ui/bursztyn_ui.h"

#define BURSZTYN_HOST_CUSTOM_ALPHA 1
#include "bursztyn_ui_host_fixture.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
using namespace BursztynUI;
static unsigned checks;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d %s\n", __LINE__, #x);                           \
      abort();                                                                 \
    }                                                                          \
  } while (0)
static std::map<std::string, std::string> files;
static BSF::Config saved;
static std::string directory;
static unsigned directory_reads;
static uint64_t hook(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t) {
  if (n == 92) {
    auto *config = reinterpret_cast<BSF::Config *>(b);
    if (c != sizeof(*config))
      return 0;
    if (!a) {
      *config = saved;
      return 1;
    }
    if (!BSF::valid(*config))
      return 0;
    uint32_t generation=saved.generation+1;
    saved = *config;
    saved.generation=generation?generation:1;
    return 1;
  }
  if (n == 6) {
    // BSF 2.2 also scans the optional /czcionki/{otf,ttc,otc} directories;
    // this fixture provides /czcionki/ttf only.
    if (strcmp(reinterpret_cast<const char *>(a), "/czcionki/ttf"))
      return 0;
    CHECK(!strcmp(reinterpret_cast<const char *>(a), "/czcionki/ttf"));
    ++directory_reads;
    if (directory.size() + 1 > c)
      return 0;
    memcpy(reinterpret_cast<void *>(b), directory.c_str(),
           directory.size() + 1);
    return 1;
  }
  if (n == 4) {
    auto it = files.find(reinterpret_cast<const char *>(a));
    if (it == files.end())
      return 0;
    *reinterpret_cast<uint32_t *>(b) = uint32_t(it->second.size());
    return 1;
  }
  return 0;
}
bool czytaj_plik(const char *path, char *out, uint32_t capacity) {
  CHECK(!strncmp(path, "/czcionki/ttf/", 14));
  auto i = files.find(path);
  if (i == files.end() || i->second.size() > capacity)
    return false;
  memcpy(out, i->second.data(), i->second.size());
  return true;
}
void wypisz(const char *) {}
bool gui_rejestruj_cele_drop(const BwsCelDrop*,uint32_t){return true;}
bool przenies_twor_uzytkownika(const char*,const char*){return false;}
bool ipc_send(uint64_t,const BwsIpcWyslij*){return false;}
bool ipc_receive(uint64_t,BwsIpcOdbierz*){return false;}

#define _start desktop_test_entry
#ifndef BURSZTYN_DESKTOP_SOURCE
#define BURSZTYN_DESKTOP_SOURCE "../programy/menedzer_okien/menedzer_okien.cpp"
#endif
#include BURSZTYN_DESKTOP_SOURCE
#undef _start
#include "../programy/ustawienia/layout.h"
int oblicz_szerokosc_tekstu(const char* text,int scale){return CzcionkaUI::zmierz_tekst(text,SkalaUI::procent(scale));}
void gui_ustaw_przejecie_myszy(bool) {}
void gui_ustaw_system_overlay(bool,int,int,int,int) {}
void rysuj_tekst_wysrodkowany(int x,int y,int w,int h,int scale,uint32_t color,const char* text) {
    auto* ctx=new KontekstRysowania({0,0,8192,8192});
    ctx->rysuj_tekst_wysrodkowany({x,y,w,h},text,color,SkalaUI::procent(scale));delete ctx;
}
void gui_odswiez_pulpit() {}
static unsigned icon_reads=0;
bool czytaj_fragment_pliku(const char* path,uint64_t offset,char* out,uint32_t capacity,uint32_t* got){
 auto i=files.find(path);if(i==files.end()||offset>i->second.size())return false;
 size_t n=i->second.size()-offset;if(n>capacity)n=capacity;
 memcpy(out,i->second.data()+offset,n);*got=n;++icon_reads;return true;
}
int bws_utworz_warstwe(int,int,int,int,int) {return 1;}
bool gui_blit_alpha(int x,int y,int w,int h,const uint32_t* pixels) {
    if(!test_capture_pixels && !test_surface_width)return true;
  for(int j=0;j<h;++j)for(int i=0;i<w;++i) {
        uint32_t old=0;
        if(test_capture_pixels)old=test_pixels[{x+i,y+j}];
        if(x+i>=0 && y+j>=0 && x+i<test_surface_width && y+j<test_surface_height)
            old=test_surface[size_t(y+j)*test_surface_width+x+i];
        test_set_pixel(x+i,y+j,kolor_nad(old,pixels[j*w+i]));
    }
    return true;
}
static bool inside(Prostokat outer,Prostokat r){return r.x>=0&&r.y>=0&&r.prawy()<=outer.szerokosc&&r.dolny()<=outer.wysokosc;}
#include "clock_support_cases.h"
#include "desktop_display_cases.h"
#include "desktop_rubber_pixels_cases.h"
#include "slider_pixel_cases.h"
#include "power_png_cases.h"
#include "power_theme_pixel_cases.h"
int main(){
 saved.checksum=BSF::checksum(saved);g_test_bws_hook=hook;
 for(const char* name:{"DejaVuSans.ttf","DejaVuSans-Bold.ttf","DejaVuSansMono.ttf","DejaVuSerifCondensed-Italic.ttf"}){
  std::ifstream f(std::string("czcionki/ttf/")+name,std::ios::binary);files[std::string("/czcionki/ttf/")+name]={std::istreambuf_iterator<char>(f),{}};directory+=std::string("[PLIK] ")+name+"\n";
 }
 for(const char* n:{"zasilania","zablokuj","uspij","zamknij","uruchomponownie"}){
  std::string path=std::string("/ikonki/przycisk")+n+".png";
  std::ifstream f(path.substr(1),std::ios::binary);files[path]={std::istreambuf_iterator<char>(f),{}};CHECK(!files[path].empty());
 }
 CHECK(BSF::ready());
 power_theme_pixel_cases();
 power_png_cases();
 if(getenv("BURSZTYN_POWER_PNG_ONLY"))return 0;
 wpisy_pulpitu=new WpisPulpitu[PULPIT_MAX_WPISOW]{};
#ifdef BURSZTYN_RUBBER_ONLY
 desktop_rubber_pixels_cases();printf("Desktop rubber interleaving: %u checks PASS\n",checks);
 return 0;
#else
 desktop_display_cases();
 desktop_rubber_pixels_cases();
 if(getenv("BURSZTYN_RUBBER_PIXELS")){printf("Desktop rubber pixels: %u PASS\n",checks);return 0;}
 std::vector<uint64_t> slider_signature;
 for(int px:{12,16,20,24}) {
  auto config=BSF::configuration();config.size=px;CHECK(BSF::apply(config));
  auto signature=slider_pixel_cases();
  if(!slider_signature.empty())CHECK(signature==slider_signature);
  slider_signature=signature;
 }
 StartMenu::Model model;
 for(auto pair:{std::pair<const char*,int>{"hus",4},{"usta",20},{"diag",16},{"kalk",3},{"xyz",-1},{"PRZEGLĄDARKA OBRAZÓW",5}}){model.filter(pair.first);CHECK(model.action()==pair.second);}
 uint32_t query[64]{};int qn=StartMenu::normalize("UST",query,64);
 CHECK(StartMenu::score("Ustawienia",query,qn)<StartMenu::score("Moje ustawienia",query,qn));
 CHECK(StartMenu::lower(0x141)==0x142);
 for(int px:{12,16,20,24})for(unsigned scale:{100U,125U,150U,200U}){
  auto config=BSF::configuration();config.family=BSF::default_id();config.size=px;CHECK(BSF::apply(config));ui_skala_procent=scale;
  SettingsUI::Body body;body.ustaw_skale_dpi(SkalaUI::procent(scale));
  body.fonts->families->dodaj_element("Bardzo długa nazwa rodziny czcionki DejaVu Sans");body.fonts->size->dodaj_element("24 px");
  for(auto dimensions:{Rozmiar(950,640),Rozmiar(820,600),Rozmiar(1100,720),Rozmiar(950,512)}){
   Prostokat bounds{0,0,ui_skaluj(dimensions.szerokosc,scale),ui_skaluj(dimensions.wysokosc,scale)};body.zmierz(bounds.rozmiar());body.rozmiesc(bounds);
   auto* page=body.fonts;CHECK(page->families->obszar().y==page->size->obszar().y);CHECK(page->families->wysokosc()==page->size->wysokosc());
   CHECK(page->family_label->obszar().y==page->size_label->obszar().y);
   CHECK(body.title->obszar().y==page->obszar().y);
   CHECK(body.nav[0]->szerokosc()==body.nav[1]->szerokosc());
   auto* themes=body.themes;themes->description->ustaw_tekst(SettingsUI::theme_description("bursztynnocny"));body.themes_view->rozmiesc(body.themes_view->obszar());
   CHECK(themes->preview->obszar().y==themes->apply->obszar().y);CHECK(themes->apply->obszar().y==themes->cancel->obszar().y);
   CHECK(themes->preview->obszar().prawy()<themes->apply->obszar().x);CHECK(themes->apply->obszar().prawy()<themes->cancel->obszar().x);
   CHECK(themes->cancel->obszar().prawy()<=themes->szerokosc()-SettingsUI::ContentInset*SkalaUI::procent(scale));
   CHECK(themes->heading->obszar().x==SettingsUI::ContentInset*SkalaUI::procent(scale));CHECK(themes->list->obszar().x==themes->heading->obszar().x);
   CHECK(page->heading->obszar().x==themes->heading->obszar().x);CHECK(body.support->heading->obszar().x==themes->heading->obszar().x);CHECK(body.system->content->obszar().x==themes->heading->obszar().x);
   CHECK(themes->cancel->obszar().dolny()<=themes->current->obszar().y);
   CHECK(themes->status->obszar().dolny()<=themes->wysokosc());

   for(size_t i=0;i<body.liczba_dzieci();++i)CHECK(inside(bounds,body.dziecko(i)->obszar()));
   for(size_t i=0;i<page->liczba_dzieci();++i)CHECK(inside(page->obszar(),page->dziecko(i)->obszar()));
   CHECK(page->preview_view->obszar().dolny()<=page->apply->obszar().y);CHECK(page->apply->obszar().dolny()<=page->status->obszar().y);
   CHECK(page->families->obszar().prawy()<page->size->obszar().x);
   ZarzadcaPopupow popups;popups.ustaw_skale_dpi(SkalaUI::procent(scale));popups.ustaw_geometrie_wlasciciela(bounds,{0,0,8192,8192});
   page->families->ustaw_zarzadce_popupow(&popups);CHECK(page->families->rozwin());
   CHECK(page->families->prostokat_popupu().szerokosc==page->families->szerokosc());
   CHECK(!page->families->lista_popupu().pasek_pionowy().czy_widoczna());
   CHECK(!page->families->lista_popupu().pasek_poziomy().czy_widoczna());
   page->families->zwin();page->families->ustaw_zarzadce_popupow(nullptr);
  }
  SettingsUI::Viewport small;small.ustaw_skale_dpi(SkalaUI::procent(scale));small.rozmiesc({0,0,ui_skaluj(640,scale),ui_skaluj(360,scale)});
  CHECK(small.body->szerokosc()>=ui_skaluj(800,scale));CHECK(small.body->wysokosc()>=ui_skaluj(480,scale));
  CHECK(small.pasek_pionowy().czy_widoczna());CHECK(small.pasek_poziomy().czy_widoczna());
  StartMenu::Surface start;start.ustaw_skale_dpi(SkalaUI::procent(scale));
  start.rozmiesc({10,10,ui_skaluj(430,scale),ui_skaluj(600,scale)});auto original=start.obszar();auto bottom=start.shutdown_bounds();
  for(const char* q:{"hus","usta","diag","kalk","xyz",""}){start.model.filter(q);start.rozmiesc(original);CHECK(start.obszar()==original);CHECK(start.shutdown_bounds()==bottom);}
  CHECK(start.shutdown_bounds().dolny()>start.result_bounds().dolny());CHECK(start.search_bounds().dolny()<start.result_bounds().y);
  start.model.filter("");start.model.move(100,start.rows);CHECK(start.model.first>0);start.model.move(-100,start.rows);CHECK(start.model.first==0);
  if(scale==100 && (px==12 || px==16 || px==24)){
   StartMenu::Surface probe;probe.rozmiesc({10,80,340,600});probe.model.filter("zzzzzz");probe.rozmiesc(probe.obszar());
   std::ofstream f(".bsf-test/start-polish-layout-"+std::to_string(px)+".json");
   auto rect=[&](Prostokat r){f<<"["<<r.x<<","<<r.y<<","<<r.szerokosc<<","<<r.wysokosc<<"]";};
   f<<"{\"power\":";rect(probe.shutdown_bounds());f<<",\"results\":";rect(probe.result_bounds());f<<",\"empty\":";rect(probe.empty_message_bounds());f<<",\"search\":";rect(probe.search_bounds());f<<",\"row\":"<<probe.row_height<<"}";
  }
  screen_w=1280;screen_h=1000;metryki_ekranu();desktop_layout.set_count(1);desktop_layout.items[0]={};desktop_layout.items[0].placed=true;
  wpisy_pulpitu[0].launcher=-1;wpisy_pulpitu[0].folder=false;strcpy(wpisy_pulpitu[0].nazwa,"Przeglądarka Obrazów");
  aktualizuj_layout_pulpitu();auto region=wizualny_wpis_pulpitu(0);CHECK(desktop_layout.items[0].label.count<=3);
  CHECK(desktop_layout.items[0].label.count>=1);
  std::vector<uint32_t> normal(size_t(region.szerokosc)*region.wysokosc),selected(normal.size()),again(normal.size());
  KontekstRysowania ctx(region);pulpit_canvas=&ctx;ctx.ustaw_cel(normal.data(),region);RysujWpisPulpitu(0);
  auto misses=BSF::statistics().misses;desktop_layout.items[0].selected=true;ctx.ustaw_cel(selected.data(),region);RysujWpisPulpitu(0);CHECK(BSF::statistics().misses==misses);
  desktop_layout.items[0].selected=false;ctx.ustaw_cel(again.data(),region);RysujWpisPulpitu(0);CHECK(normal==again);CHECK(normal!=selected);
  int partial=0;auto cell=desktop_layout.cell(0);auto g=pulpit_geometria_komorki(cell.x,cell.y,scale);
  for(int y=g.etykieta_y;y<region.dolny();++y)for(int x=region.x;x<region.prawy();++x){auto index=size_t(y-region.y)*region.szerokosc+x-region.x;auto c=normal[index];if((c>>24)>1&&(c>>24)<255)++partial;
   if(!c)continue;for(uint32_t wallpaper:{0xff101010U,0xffeeeeeeU}){auto composed=motyw_pulpit_skladaj(wallpaper,c,true);CHECK(composed==(kolor_nad(wallpaper,kolor_z_alfa(c))&0xffffffU));}
  }
  CHECK(partial>0);pulpit_canvas=nullptr;ctx.ustaw_cel(nullptr,{});
  auto label=Desktop::wrap("Testy systemu",CzcionkaUI::zmierz_tekst("Testy",ui_skala()),CzcionkaUI::pobierz_wysokosc(ui_skala()),2,3,int(scale),oblicz_szerokosc_tekstu);
  CHECK(label.ends[0]==5);CHECK(label.starts[1]==6);
 }

 // Exercise the actual live font-event path, including metryki_ekranu().
 const char* names[]={"Notatnik","Kalkulator","Husarz","Testy systemu",
  "Przeglądarka Obrazów","Demo BursztynUI","Diagnostyka systemu",
  "Aktualizacje","gg","gggggg","Bardzo długa polska nazwa folderu żółw"};
 for(int px:{12,16,20,24})for(unsigned scale:{100U,125U,150U,200U}){
  auto config=BSF::configuration();config.family=BSF::default_id();config.size=px;CHECK(BSF::apply(config));
  ui_skala_procent=scale;screen_w=ui_skaluj(1280,scale);screen_h=ui_skaluj(1000,scale);
  desktop_layout={};desktop_layout.set_count(11);metryki_ekranu();
  for(int i=0;i<11;++i){strcpy(wpisy_pulpitu[i].nazwa,names[i]);wpisy_pulpitu[i].launcher=-1;}
  rozmiesc_ikony_ekranu();
  // A manually placed file with a non-grid offset must also retain its anchor.
  auto manualMetrics=pulpit_metryki(scale);
  int inset=(desktop_layout.cw-manualMetrics.etykieta_szer)/2-ui_skaluj(3,scale);
  desktop_layout.anchor_position(9,-inset,desktop_layout.y0+3*desktop_layout.ch);
  Prostokat anchors[11];for(int i=0;i<11;++i){CHECK(desktop_layout.items[i].placed);anchors[i]=desktop_layout.cell(i);}
  const int pitch=desktop_layout.ch,columns=desktop_layout.cw;
  auto state=std::make_unique<DesktopSettings::State>();
  auto keys=[&](int i){return DesktopSettings::Key{'F',names[i]};};
  CHECK(state->serialize(desktop_layout,keys)>0);std::string original=state->buffer;
  desktop_settings=state.get();state->applied=true;state->dirty=false;
  for(int repeat=0;repeat<4;++repeat)for(unsigned f=0;f<BSF::enumerate();++f){
   config.family=BSF::info(f)->id;CHECK(BSF::apply(config));
   metryki_ekranu();aktualizuj_layout_pulpitu();
   CHECK(desktop_layout.y0==ui_skaluj(3,scale));CHECK(desktop_layout.ch==pitch);CHECK(desktop_layout.cw==columns);CHECK(!state->dirty);
   auto m=pulpit_metryki(scale);
   for(int i=0;i<11;++i){CHECK(desktop_layout.cell(i)==anchors[i]);CHECK(desktop_layout.items[i].label.height<=m.etykieta_wys);CHECK(desktop_layout.items[i].label.count<=3);}
   CHECK(state->serialize(desktop_layout,keys)>0);CHECK(original==state->buffer);
   // Restart reconstruction uses serialized anchors with the active face.
   auto restored=desktop_layout;CHECK(state->parse(original.size()));CHECK(state->anchorCoordinates);
   CHECK(state->apply(restored,keys)==0);
   for(int i=0;i<11;++i)CHECK(restored.cell(i)==anchors[i]);
  }
  desktop_settings=nullptr;
 }
 // Extreme face height cannot affect the placement footprint or row pitch.
 Desktop::Layout<2> extreme;extreme.set_count(2);extreme.geometry(1280,1000,3,100);
 auto footprint=extreme.placement_bounds(0);extreme.items[0].label.height=8000;
 CHECK(extreme.placement_bounds(0)==footprint);
 // A legacy text-bound save is migrated at load, before any live Apply.
 Desktop::Layout<1> migration;migration.set_count(1);migration.geometry(1280,1000,3,100);
 auto migrationMetrics=pulpit_metryki(100);
 migration.items[0].label=Desktop::wrap("gg",112,migrationMetrics.wysokosc_linii,2,3,100,oblicz_szerokosc_tekstu);
 migration.anchor_position(0,300,200);auto migrationAnchor=migration.cell(0);auto legacyVisual=migration.visual_bounds(0);
 auto legacy=std::make_unique<DesktopSettings::State>();legacy->count=1;
 legacy->records[0].type='F';strcpy(legacy->records[0].id,"gg");legacy->records[0].x=legacyVisual.x;legacy->records[0].y=legacyVisual.y;
 auto migrationKey=[](int){return DesktopSettings::Key{'F',"gg"};};
 CHECK(legacy->apply(migration,migrationKey)==0);CHECK(legacy->dirty);CHECK(migration.cell(0)==migrationAnchor);
 auto migratedBytes=legacy->serialize(migration,migrationKey);CHECK(migratedBytes>0);CHECK(legacy->parse(migratedBytes));CHECK(legacy->anchorCoordinates);
 CHECK(legacy->apply(migration,migrationKey)==0);CHECK(!legacy->dirty);CHECK(migration.cell(0)==migrationAnchor);
 auto wide=Desktop::wrap("W",112,20,2,3,100,[](const char* t,int){return !strcmp(t,"…")?10:1000;});
 CHECK(wide.count==1);CHECK(!wide.complete);CHECK(wide.width==10);
 for(int px:{12,16,20,24})for(unsigned dpi:{100U,125U,150U,200U}){
  auto config=BSF::configuration();config.size=px;CHECK(BSF::apply(config));
  SettingsUI::Body body;body.ustaw_skale_dpi(SkalaUI::procent(dpi));
  CHECK(!strcmp(body.nav[0]->tekst(),"System"));CHECK(!strcmp(body.nav[1]->tekst(),"Energia"));CHECK(!strcmp(body.nav[2]->tekst(),"Wygląd"));CHECK(!strcmp(body.nav[3]->tekst(),"Motywy"));CHECK(!strcmp(body.nav[4]->tekst(),"Czcionki"));CHECK(!strcmp(body.nav[5]->tekst(),"Wsparcie"));CHECK(!body.power->czy_widoczna());
  CHECK(body.system->czy_widoczna());CHECK(!body.fonts->czy_widoczna());CHECK(!body.support->czy_widoczna());
  for(int width:{800,950,1100}){
   body.rozmiesc({0,0,ui_skaluj(width,dpi),ui_skaluj(512,dpi)});auto* page=body.system;
   int bottom=0;
   for(size_t i=0;i<SystemIdentity::PropertyCount;++i){
    CHECK(!strcmp(page->values[i]->tekst(),SystemIdentity::Properties[i].value));
    auto label=page->labels[i]->obszar(),value=page->values[i]->obszar();
    CHECK(label.y>=bottom);CHECK(value.y>=bottom);CHECK(!Desktop::intersects(label,value));
    CHECK(value.prawy()<=page->content->szerokosc());
    auto size=page->values[i]->zmierz({value.szerokosc,10000});CHECK(size.wysokosc<=value.wysokosc);
    bottom=value.dolny()>label.dolny()?value.dolny():label.dolny();
   }
   CHECK(page->content->wysokosc()>=bottom);
  }
 }
 CHECK(!strcmp(SystemIdentity::Version,BURSZTYN_NAZWA_WERSJI));
 CHECK(!strcmp(SystemIdentity::Author,"Dymitr Wygowski"));CHECK(!strcmp(SystemIdentity::Email,"programista.art@gmail.com"));
 CHECK(BURSZTYN_WERSJA_REWIZJA==9);
 std::printf("System UI object sizes: page=%zu label=%zu panel=%zu; 31 labels, two panels, two scrollbars plus viewport\n",sizeof(SettingsUI::SystemPage),sizeof(Etykieta),sizeof(Panel));
 clock_support_cases();
 std::printf("Desktop TTF / Settings / Start: %u checks PASS\n",checks);
#endif
}
