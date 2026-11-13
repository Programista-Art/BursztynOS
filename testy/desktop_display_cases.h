#pragma once
#include "../programy/ekran/ekran_okno.h"
static void display_layout_probe(){
 {
  struct SliderProbe:Suwak{size_t rect_offset(){return reinterpret_cast<char*>(&obszar_ekranu_)-reinterpret_cast<char*>(this);}};
  SliderProbe probe;probe.ustaw_wartosc(65);
  std::vector<unsigned char> before(sizeof(probe));memcpy(before.data(),&probe,sizeof(probe));
  probe.ustaw_wartosc(66);size_t value_offset=0;
  for(size_t i=0;i+4<=sizeof(probe);i+=4){int a,b;memcpy(&a,before.data()+i,4);memcpy(&b,reinterpret_cast<char*>(&probe)+i,4);if(a==65 && b==66)value_offset=i;}
  CHECK(value_offset>0);
  std::ofstream file(".bsf-test/gui-observer-layout.json");
  file<<"{\"control_rect\":"<<probe.rect_offset()<<",\"slider_value\":"<<value_offset
      <<",\"selected\":"<<offsetof(Desktop::Position,selected)
      <<",\"gesture_dragging\":"<<offsetof(GestPulpitu,dragging)
      <<",\"gesture_band\":"<<offsetof(GestPulpitu,band)<<"}\n";
 }
 {
  using L=decltype(desktop_layout);
  std::ofstream file(".bsf-test/display-observer-layout.json");
  file<<"{\"position_size\":"<<sizeof(Desktop::Position)<<",\"anchor_offset\":"<<offsetof(Desktop::Position,anchor)
      <<",\"count_offset\":"<<offsetof(L,count)<<",\"origin_offset\":"<<offsetof(L,x0)<<",\"dpi_offset\":"<<offsetof(L,dpi)<<"}\n";
 }

 for(unsigned scale:{100U,125U,150U,200U})for(bool pending:{false,true}){
  ui_skala_procent=scale;EkranOdpowiedz state{};state.skala=scale;state.liczba_trybow=10;state.transakcja=pending?1:0;state.aktywny={1280,720,32,5120,0,0};state.mozliwosci.mozna_zmienic_runtime=true;
  const int widths[]={800,1024,1280,1280,1366,1600,1920,2560,3440,3840};
  for(int i=0;i<10;++i)state.tryby[i]={uint32_t(widths[i]),720,32,uint32_t(widths[i]*4),0,0};
  OknoEkranu window;window.inicjalizuj(state);window.ustaw_geometrie(0,0,ui_skaluj(640,scale),ui_skaluj(290,scale),false);
  std::ofstream file(".bsf-test/display-layout-"+std::to_string(scale)+(pending?"-pending.json":".json"));
  file<<"{";bool first=true;
  auto rect=[&](const char* name,Kontrolka& c){auto r=c.obszar_ekranu();if(!first)file<<",";first=false;file<<"\""<<name<<"\":["<<r.x<<","<<r.y<<","<<r.szerokosc<<","<<r.wysokosc<<"]";};
  rect("mode",window.lista_trybow());rect("scale",window.lista_skal());rect("apply",window.przycisk_zastosuj());rect("keep",window.przycisk_zachowaj());
  window.lista_trybow().rozwin();auto popup=window.lista_trybow().prostokat_popupu();
  file<<",\"popup\":["<<popup.x<<","<<popup.y<<","<<popup.szerokosc<<","<<popup.wysokosc<<"],\"row\":"<<window.lista_trybow().lista_popupu().wysokosc_wiersza_piksele()<<",\"first\":"<<window.lista_trybow().lista_popupu().pierwszy_widoczny_indeks()<<"}\n";
 }
}

static void desktop_display_cases() {
 auto config=BSF::configuration();config.family=BSF::default_id();config.size=16;CHECK(BSF::apply(config));
 display_layout_probe();
 const char* names[]={"Notatnik","Kalkulator","Husarz","Testy systemu","Snake","Eksplorator","Przeglądarka Obrazów","Demo BursztynUI","Kontrolki","Diagnostyka systemu","test.bmp","test1.bmp","Mój folder","plik.txt"};
 ui_skala_procent=100;screen_w=1920;screen_h=1080;desktop_layout={};desktop_layout.set_count(14);metryki_ekranu();
 for(int i=0;i<14;++i){strcpy(wpisy_pulpitu[i].nazwa,names[i]);wpisy_pulpitu[i].launcher=i<10?i:-1;}
 rozmiesc_ikony_ekranu();
 // File icons auto-place after the whole launcher catalog (DesktopEntries::Count),
 // so pick an empty column. The intentional sub-cell offset must be a valid,
 // non-overlapping position, otherwise reflow rightly relocates it.
 desktop_layout.anchor_position(13,desktop_layout.x0+5*desktop_layout.cw+7,desktop_layout.y0+2*desktop_layout.ch+5);
 CHECK(desktop_layout.visible(13));
 for(int j=0;j<13;++j)CHECK(!Desktop::intersects(desktop_layout.placement_bounds(j),desktop_layout.placement_bounds(13)));
 auto original=desktop_layout;
 auto state=std::make_unique<DesktopSettings::State>();desktop_settings=state.get();state->applied=true;state->dirty=false;
 auto keys=[&](int i){return DesktopSettings::Key{i<10?'L':'F',names[i]};};
 CHECK(state->serialize(desktop_layout,keys)>0);std::string persisted=state->buffer;
 for(int repeat=0;repeat<3;++repeat){
  for(unsigned scale:{125U,150U,200U,150U,125U,100U}){
   ui_skala_procent=scale;metryki_ekranu();rozmiesc_ikony_ekranu();
   CHECK(desktop_layout.y0==ui_skaluj(3,scale));CHECK(!state->dirty);
   for(unsigned family=0;family<BSF::enumerate();++family){
    config.family=BSF::info(family)->id;CHECK(BSF::apply(config));
    metryki_ekranu();rozmiesc_ikony_ekranu();CHECK(!state->dirty);
    for(int i=0;i<14;++i){
     const auto& a=desktop_layout.items[i].anchor;const auto& b=original.items[i].anchor;
     CHECK(a.col==b.col && a.row==b.row && a.offsetX==b.offsetX && a.offsetY==b.offsetY && a.dpi==b.dpi);
    }
   }
  }
  for(int i=0;i<14;++i){CHECK(desktop_layout.items[i].placed);CHECK(desktop_layout.cell(i)==original.cell(i));}
  CHECK(state->serialize(desktop_layout,keys)>0);CHECK(persisted==state->buffer);
  for(auto dimensions:{Rozmiar(1280,720),Rozmiar(1920,1080),Rozmiar(800,600),Rozmiar(1920,1080)}){
   screen_w=dimensions.szerokosc;screen_h=dimensions.wysokosc;metryki_ekranu();rozmiesc_ikony_ekranu();CHECK(!state->dirty);
   if(screen_w==1920)for(int i=0;i<14;++i)CHECK(desktop_layout.cell(i)==original.cell(i));
  }
 }
 desktop_settings=nullptr;
}
