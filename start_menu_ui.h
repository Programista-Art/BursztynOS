#pragma once
#include "start_menu_model.h"
#include "plik_popup_ui.h"
namespace StartMenu {
using namespace BursztynUI;
class Surface : public PowierzchniaPopup {
 class Search : public PoleTekstowe {
  Surface* owner;
 public:
  explicit Search(Surface* p):PoleTekstowe("Szukaj programów..."),owner(p){ustaw_maksymalna_liczbe_znakow(QueryMax);}
  void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e)override{
   if(owner->command(e))return;
   PoleTekstowe::obsluz_wcisniecie_klawisza(e);
  }
 };
 class Results : public Kontrolka {
  Surface* owner;
 public:
  explicit Results(Surface* p):owner(p){ustaw_mozliwosc_fokusu(true);}
  void rysuj_siebie(KontekstRysowania& ctx)override{
   auto& m=owner->model;auto s=skala_dpi_;int row=owner->row_height;
   ctx.dodaj_przyciecie({0,0,obszar_.szerokosc,obszar_.wysokosc});
   if(!m.count)ctx.rysuj_tekst({8*s,(obszar_.wysokosc-CzcionkaUI::pobierz_wysokosc(s))/2},"Brak wyników",motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI),s);
   for(int visible=0;visible<owner->rows&&visible+m.first<m.count;++visible){int i=visible+m.first;bool selected=i==m.selected;
    if(selected)ctx.rysuj_prostokat({0,visible*row,obszar_.szerokosc-8*s,row},motyw_kolor(MOTYW_KOLOR_PODNIESIONY));
    const char* name=apps[m.matches[i]].name;char text[192]{};int n=0;while(name[n]&&n<180){text[n]=name[n];++n;}text[n]=0;
    int available=obszar_.szerokosc-28*s;
    if(CzcionkaUI::zmierz_tekst(text,s)>available){
     while(n){n=int(CzcionkaUI::poprzedni_znak(text,n));text[n]=char(0xe2);text[n+1]=char(0x80);text[n+2]=char(0xa6);text[n+3]=0;if(CzcionkaUI::zmierz_tekst(text,s)<=available)break;}
    }
    ctx.rysuj_tekst({8*s,visible*row+(row-CzcionkaUI::pobierz_wysokosc(s))/2},text,motyw_kolor(MOTYW_KOLOR_TEKST),s);
   }
   if(m.count>owner->rows){int h=obszar_.wysokosc;int thumb=h*owner->rows/m.count;if(thumb<8*s)thumb=8*s;
    int y=(h-thumb)*m.first/(m.count-owner->rows);ctx.rysuj_prostokat({obszar_.szerokosc-4*s,y,3*s,thumb},motyw_kolor(MOTYW_KOLOR_RAMKA));}
   ctx.zdejmij_przyciecie();
  }
  void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e)override{
   if(!e.czy_lewy_przycisk())return;
   int index=(e.y-obszar_ekranu_.y)/owner->row_height+owner->model.first;
   if(index>=0&&index<owner->model.count){owner->model.selected=index;owner->pending=owner->model.action();}e.obsluzone=true;
  }
  void obsluz_rolke(ZdarzenieRolki& e)override{
   owner->model.first+=e.delta_y>0?-3:3;owner->model.clamp(owner->rows);uniewaznij();e.obsluzone=true;
  }
  void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e)override{owner->command(e);}
 };
 Search* search;Results* list;Przycisk *restart,*shutdown;
 int pending=-1;
public:
 PlikiUI::ObslugaPopup events;
 Model model;
 int row_height=30,rows=1;
 Surface(){
  search=new Search(this);list=new Results(this);restart=new Przycisk("Uruchom ponownie");shutdown=new Przycisk("Zamknij");
  dodaj_kontrolke(search);dodaj_kontrolke(list);dodaj_kontrolke(restart);dodaj_kontrolke(shutdown);
  search->przy_zmianie([](PoleTekstowe*,const char* text,void* p){auto* s=static_cast<Surface*>(p);s->model.filter(text);s->list->uniewaznij();},this);
  restart->przy_kliknieciu([](Przycisk*,void* p){static_cast<Surface*>(p)->pending=18;},this);
  shutdown->przy_kliknieciu([](Przycisk*,void* p){static_cast<Surface*>(p)->pending=19;},this);
  model.filter("");
 }
 bool command(ZdarzenieKlawiatury& e){
  auto k=e.polecenie();if(k==0x48||k==0x50){model.move(k==0x48?-1:1,rows);list->uniewaznij();e.obsluzone=true;return true;}
  if(k==13){pending=model.action();e.obsluzone=true;return true;}return false;
 }
 int take_action(){int result=pending;pending=-1;return result;}
 const char* query()const{return search->tekst();}
 Prostokat result_bounds()const{return list->obszar_ekranu();}
 Prostokat shutdown_bounds()const{return shutdown->obszar_ekranu();}
 Prostokat search_bounds()const{return search->obszar_ekranu();}
 void rozmiesc(Prostokat r)override{
  PowierzchniaPopup::rozmiesc(r);auto s=skala_dpi_;int pad=10*s,gap=8*s;
  row_height=CzcionkaUI::pobierz_wysokosc(s)+12*s;if(row_height<30*s)row_height=30*s;
  int w=r.szerokosc-2*pad;
  search->rozmiesc({pad,pad,w,row_height});
  shutdown->rozmiesc({pad,r.wysokosc-pad-row_height,w,row_height});
  restart->rozmiesc({pad,r.wysokosc-pad-2*row_height-gap,w,row_height});
  int top=pad+row_height+gap,bottom=r.wysokosc-pad-2*row_height-3*gap;
  int h=bottom-top;if(h<0)h=0;rows=h/row_height;if(rows<1)rows=1;
  list->rozmiesc({pad,top,w,h});model.visible(rows);
 }
 void rysuj_siebie(KontekstRysowania& ctx)override{
  rysuj_zaokraglone_tlo(ctx);auto s=skala_dpi_;int y=restart->obszar().y-4*s;
  ctx.rysuj_prostokat({10*s,y,obszar_.szerokosc-20*s,s},motyw_kolor(MOTYW_KOLOR_RAMKA));
 }
 void open(Prostokat work,int x){
  events.reset_input();pending=-1;search->ustaw_tekst("");model.filter("");
  events.geometria({0,0,work.szerokosc,work.wysokosc},work);auto s=ui_skala();
  int width=340*s;for(auto& app:apps){int measured=CzcionkaUI::zmierz_tekst(app.name,s)+44*s;if(measured>width)width=measured;}
  if(width>work.szerokosc-x)width=work.szerokosc-x;
  if(width>2048-2*s)width=2048-2*s;
  int height=600*s;if(height>work.wysokosc-12*s)height=work.wysokosc-12*s;
  if(height>2048-2*s)height=2048-2*s;
  events.popups.otworz(*this,{x,work.dolny()-height,width,height},nullptr,search);
  events.popups.odrysuj_popupy(true);
 }
 void close(){events.popups.zamknij();}
 void po_zamknieciu(PowodZamknieciaPopup p)override{
  search->ustaw_tekst("");model.filter("");PowierzchniaPopup::po_zamknieciu(p);
 }
 void refresh(){events.popups.zaktualizuj_motyw();if(czy_otwarta()){rozmiesc(obszar_);uniewaznij();events.popups.odrysuj_popupy(true);}}
};
}
