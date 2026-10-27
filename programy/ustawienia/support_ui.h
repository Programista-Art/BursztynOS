#pragma once
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "support_data.h"
namespace SettingsUI {
using namespace BursztynUI;
class SupportRows : public DostawcaDanychTabeli {
public:
 SupportData::Category* category{};int width=500;
 size_t liczba_wierszy()override{return category?category->count:0;}
 size_t liczba_kolumn()override{return 1;}
 const char* naglowek_kolumny(size_t)override{return "Nazwa";}
 int szerokosc_kolumny(size_t)override{return width;}
 const char* pobierz_komorke(size_t row,size_t,char*,size_t)override{return category && row<category->count?category->names[row]:"";}
};
class SupportPane : public Panel {
public:
 SupportRows rows;TabelaWirtualna* table;Etykieta* status;
 SupportPane(){ustaw_uklad_automatyczny(false);ustaw_wypelnienie({0,0,0,0});table=&dodaj<TabelaWirtualna>();table->ustaw_dostawce(&rows);status=&dodaj<Etykieta>("Brak danych");status->ustaw_wypelnienie({0,0,0,0});}
 void refresh(){status->ustaw_tekst(SupportData::message(*rows.category));table->ustaw_dostawce(&rows);table->uniewaznij();}
 void rozmiesc(Prostokat r)override{
  Panel::rozmiesc(r);int line=CzcionkaUI::pobierz_wysokosc(skala_dpi_),gap=8*skala_dpi_;
  rows.width=r.szerokosc/skala_dpi_-16;if(rows.width<1)rows.width=1;
  int height=r.wysokosc-line-gap;if(height<0)height=0;
  table->rozmiesc({0,0,r.szerokosc,height});status->rozmiesc({0,height+gap,r.szerokosc,line});
 }
};
class SupportPage : public Panel {
public:
 SupportData::Category* data;Etykieta* heading;Karty* tabs;SupportPane* panes[3]{};Przycisk* reload;
 using Reload=void(*)(SupportPage*,void*);Reload on_reload{};void* reload_context{};
 SupportPage(){
  ustaw_uklad_automatyczny(false);ustaw_wypelnienie({0,0,0,0});
  data=new SupportData::Category[3];heading=&dodaj<Etykieta>("Wsparcie projektu Bursztyn OS");heading->ustaw_wypelnienie({0,0,0,0});
  tabs=&dodaj<Karty>();tabs->ustaw_zawijanie_naglowkow(true);
  for(int i=0;i<3;++i){panes[i]=new SupportPane;panes[i]->rows.category=data?data+i:nullptr;tabs->dodaj_karte(SupportData::Titles[i],panes[i]);}
  reload=&dodaj<Przycisk>("Odśwież");reload->przy_kliknieciu([](Przycisk*,void* p){auto* self=static_cast<SupportPage*>(p);if(self->on_reload)self->on_reload(self,self->reload_context);},this);
 }
 ~SupportPage()override{delete[] data;}
 void refresh(){if(data)for(auto* pane:panes)pane->refresh();}
 void rozmiesc(Prostokat r)override{
  Panel::rozmiesc(r);auto s=skala_dpi_;int gap=12*s,line=CzcionkaUI::pobierz_wysokosc(s);
  int button=reload->zmierz({r.szerokosc,r.wysokosc}).wysokosc;if(button<line+12*s)button=line+12*s;
  heading->rozmiesc({0,0,r.szerokosc,line});
  int height=r.wysokosc-line-2*gap-button;if(height<0)height=0;
  tabs->rozmiesc({0,line+gap,r.szerokosc,height});
  int width=CzcionkaUI::zmierz_tekst("Odśwież",s)+32*s;
  reload->rozmiesc({0,line+2*gap+height,width,button});
 }
};
}
