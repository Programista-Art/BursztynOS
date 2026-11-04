#pragma once
#include "start_menu_model.h"
#include "plik_popup_ui.h"
#include "bursztyn_ui/przycisk_obrazkowy.h"
namespace StartMenu {
using namespace BursztynUI;
class Surface : public PowierzchniaPopup, private ObslugaWidokuMenu {
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
  Surface* owner;int hovered=-1,pressed=-1;Etykieta* empty=nullptr;
 public:
  explicit Results(Surface* p):owner(p){
   ustaw_mozliwosc_fokusu(true);
   empty=&dodaj<Etykieta>("Brak aplikacji o takiej nazwie");
   empty->ustaw_wypelnienie({0,0,0,0});empty->ustaw_zawijanie(true);empty->ustaw_widoczna(false);
   empty->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));
  }
  void layout_empty(){
   auto s=skala_dpi_;int width=obszar_.szerokosc-16*s;if(width<1)width=1;
   int height=empty->zmierz({width,obszar_.wysokosc}).wysokosc;
   empty->rozmiesc({8*s,(owner->row_height-CzcionkaUI::pobierz_wysokosc(s))/2,width,height});
   empty->ustaw_widoczna(owner->model.count==0);
  }
  void rozmiesc(Prostokat r)override{Kontrolka::rozmiesc(r);layout_empty();}
  void zaktualizuj_motyw()override{Kontrolka::zaktualizuj_motyw();if(empty)empty->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI));}
  Prostokat empty_bounds()const{return empty->obszar_ekranu();}
  bool empty_visible()const{return empty->czy_widoczna();}
  void reset(){hovered=pressed=-1;layout_empty();uniewaznij();}
  void rysuj_siebie(KontekstRysowania& ctx)override{
   auto& m=owner->model;auto s=skala_dpi_;int row=owner->row_height;
   ctx.dodaj_przyciecie({0,0,obszar_.szerokosc,obszar_.wysokosc});
   for(int visible=0;visible<owner->rows&&visible+m.first<m.count;++visible){int i=visible+m.first;bool selected=i==m.selected;
    auto style=Motyw::pobierz_domyslny().styl_menu;
    auto state=i==pressed?style.wcisniety:i==hovered?style.najechany:style.normalny;
    if(selected&&i!=pressed&&i!=hovered){state.tlo=motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE);state.tekst=motyw_kolor_tekstu_zaznaczenia();}
    ctx.rysuj_prostokat({0,visible*row,obszar_.szerokosc-8*s,row},state.tlo);
    if(selected&&czy_ma_fokus())ctx.rysuj_ramke({0,visible*row,obszar_.szerokosc-8*s,row},motyw_kolor(MOTYW_KOLOR_AKCENT),s);
    const char* name=apps[m.matches[i]].name;char text[192]{};int n=0;while(name[n]&&n<180){text[n]=name[n];++n;}text[n]=0;
    int available=obszar_.szerokosc-28*s;
    if(CzcionkaUI::zmierz_tekst(text,s)>available){
     while(n){n=int(CzcionkaUI::poprzedni_znak(text,n));text[n]=char(0xe2);text[n+1]=char(0x80);text[n+2]=char(0xa6);text[n+3]=0;if(CzcionkaUI::zmierz_tekst(text,s)<=available)break;}
    }
    ctx.rysuj_tekst({8*s,visible*row+(row-CzcionkaUI::pobierz_wysokosc(s))/2},text,state.tekst,s);
   }
   if(m.count>owner->rows){int h=obszar_.wysokosc;int thumb=h*owner->rows/m.count;if(thumb<8*s)thumb=8*s;
    int y=(h-thumb)*m.first/(m.count-owner->rows);ctx.rysuj_prostokat({obszar_.szerokosc-4*s,y,3*s,thumb},motyw_kolor(MOTYW_KOLOR_RAMKA));}
   ctx.zdejmij_przyciecie();
  }
  void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e)override{
   if(!e.czy_lewy_przycisk())return;
   int index=(e.y-obszar_ekranu_.y)/owner->row_height+owner->model.first;
   if(index>=0&&index<owner->model.count){owner->model.selected=index;pressed=index;uniewaznij();}e.obsluzone=true;
  }
  void obsluz_ruch_myszy(ZdarzenieMyszki& e)override{int i=(e.y-obszar_ekranu_.y)/owner->row_height+owner->model.first;if(i<0||i>=owner->model.count)i=-1;if(i!=hovered){hovered=i;uniewaznij();}}
  void obsluz_opuszczenie_myszy()override{hovered=-1;uniewaznij();Kontrolka::obsluz_opuszczenie_myszy();}
  void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e)override{int i=(e.y-obszar_ekranu_.y)/owner->row_height+owner->model.first;if(obszar_ekranu_.zawiera(e.x,e.y)&&i==pressed&&i>=0&&i<owner->model.count)owner->pending=owner->model.action();pressed=-1;uniewaznij();e.obsluzone=true;}
  void obsluz_rolke(ZdarzenieRolki& e)override{
   owner->model.first+=e.delta_y>0?-3:3;owner->model.clamp(owner->rows);uniewaznij();e.obsluzone=true;
  }
  void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e)override{owner->command(e);}
 };
 class Power : public PrzyciskObrazkowy {
  Surface* owner;
 public:
  explicit Power(Surface* p):PrzyciskObrazkowy(""),owner(p){
   ustaw_tryb(TrybPrzyciskuObrazkowego::TylkoIkona);ustaw_ikone(102);
   ustaw_preferowany_rozmiar({36,36});ustaw_podpowiedz("Zasilanie");
  }
  void rysuj_tlo_i_ramke(KontekstRysowania& ctx,const StylStanu& state)override{
   // Open is an active button state, independent of keyboard focus ownership.
   auto s=state;
   if(czy_wlaczona() && owner->power_open()){
    s.tlo=styl().wcisniety.tlo;s.tekst=styl().wcisniety.tekst;
    s.ramka=czy_ma_fokus()?styl().fokus.ramka:styl().wcisniety.ramka;
   }
   PrzyciskObrazkowy::rysuj_tlo_i_ramke(ctx,s);
  }
  void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e)override{
   if(!czy_wlaczona())return;
   if(e.polecenie()==13 || e.polecenie()==32){owner->open_power();e.obsluzone=true;return;}
   if(owner->command(e))return;
   PrzyciskObrazkowy::obsluz_wcisniecie_klawisza(e);
  }
 };
 Search* search;Results* list;Power* power;
 IkonaPNG power_icons[5];
 Menu power_items;WidokMenuPopup power_popup;
 void prepare_icons(){
  power_icons[0].przygotuj("/ikonki/przyciskzasilania.png",22*skala_dpi_);
  power->ustaw_obraz_ikony(&power_icons[0]);
  struct ActionIcon {uint32_t id;const char* path;};
  static constexpr ActionIcon mapping[]={
   {22,"/ikonki/przyciskzablokuj.png"},{23,"/ikonki/przyciskuspij.png"},
   {19,"/ikonki/przyciskzamknij.png"},{18,"/ikonki/przyciskuruchomponownie.png"}};
  for(unsigned i=0;i<4;++i){power_icons[i+1].przygotuj(mapping[i].path,18*skala_dpi_);
   power_items.znajdz(mapping[i].id)->ustaw_obraz_ikony(&power_icons[i+1]);}
 }
 Prostokat place_power()const{
  auto work=events.popups.obszar_roboczy_ekranu();auto button=power->obszar_ekranu();auto s=skala_dpi_;
  int margin=4*s,gap=8*s,shadow=2*s;
  auto size=power_popup.wymagany_rozmiar(s);
  if(size.szerokosc<150*s)size.szerokosc=150*s;
  int maxw=work.szerokosc-2*margin;
  if(size.szerokosc>maxw)size.szerokosc=maxw;
  if(size.wysokosc>work.wysokosc-2*margin)size.wysokosc=work.wysokosc-2*margin;
  int x=button.prawy()-size.szerokosc;
  if(x<work.x+margin)x=work.x+margin;
  if(x+size.szerokosc>work.prawy()-margin)x=work.prawy()-margin-size.szerokosc;
  int y=button.y-gap-shadow-size.wysokosc;
  if(y<work.y+margin){
   y=button.dolny()+gap;
   if(y+size.wysokosc>work.dolny()-margin){
    // A very short work area: use the nearest non-overlapping side.
    y=work.y+margin;
    int left=button.x-gap-size.szerokosc,right=button.prawy()+gap;
    if(left>=work.x+margin)x=left;
    else if(right+size.szerokosc<=work.prawy()-margin)x=right;
    else { // No side fits; retain separation by clipping available height above.
     y=work.y+margin;size.wysokosc=button.y-gap-shadow-y;
     if(size.wysokosc<0)size.wysokosc=0;
    }
   }
  }
  return {x,y,size.szerokosc,size.wysokosc};
 }
 uint64_t hover_deadline=0,leave_deadline=0;
 bool pointer_power=false,pointer_popup=false;
 void menu_aktywuj(WidokMenuPopup&,ElementMenu& item)override{if(item.czy_wlaczony()){pending=int(item.identyfikator());events.popups.zamknij(PowodZamknieciaPopup::Zatwierdzenie);}}
 void menu_otworz_podmenu(WidokMenuPopup&,ElementMenu&,Prostokat)override{}
 void menu_kierunek_poziomy(WidokMenuPopup&,int direction)override{if(direction<0)close_power();}
 void close_power(){events.popups.zamknij_od_poziomu(1);if(czy_otwarta())events.popups.ustaw_fokus_popupu(power);hover_deadline=leave_deadline=0;}

 int pending=-1;
public:
 PlikiUI::ObslugaPopup events;
 Model model;
 int row_height=30,rows=1;
 Surface(){
  search=new Search(this);list=new Results(this);power=new Power(this);
  dodaj_kontrolke(search);dodaj_kontrolke(list);dodaj_kontrolke(power);
  search->przy_zmianie([](PoleTekstowe*,const char* text,void* p){auto* s=static_cast<Surface*>(p);s->model.filter(text);s->list->reset();},this);
  power->przy_kliknieciu([](Przycisk*,void* p){static_cast<Surface*>(p)->open_power();},this);
  power_items.dodaj_element("Zablokuj",22).ustaw_wlaczony(false).ustaw_ikone(100);
  power_items.dodaj_element("Uśpij",23).ustaw_wlaczony(false).ustaw_ikone(101);
  power_items.dodaj_element("Zamknij",19).ustaw_ikone(102);
  power_items.dodaj_element("Uruchom ponownie",18).ustaw_ikone(103);
  power_popup.ustaw(&power_items,this);
  power_popup.przy_zamknieciu([](PowierzchniaPopup*,PowodZamknieciaPopup,void* p){auto* s=static_cast<Surface*>(p);if(s->czy_otwarta())s->events.popups.ustaw_fokus_popupu(s->power);},this);
  model.filter("");
 }
 bool command(ZdarzenieKlawiatury& e){
  auto k=e.polecenie();if(k==9){events.popups.ustaw_fokus_popupu(events.popups.fokus_popupu()==power?static_cast<Kontrolka*>(search):power);e.obsluzone=true;return true;}if(k==0x48||k==0x50){model.move(k==0x48?-1:1,rows);list->uniewaznij();e.obsluzone=true;return true;}
  if(k==13){pending=model.action();e.obsluzone=true;return true;}return false;
 }
 int take_action(){int result=pending;pending=-1;return result;}
 const char* query()const{return search->tekst();}
 Prostokat result_bounds()const{return list->obszar_ekranu();}
 Prostokat shutdown_bounds()const{return power->obszar_ekranu();}
 Prostokat empty_message_bounds()const{return list->empty_bounds();}
 bool empty_message_visible()const{return list->empty_visible();}
 Prostokat power_anchor()const{return power->obszar_ekranu();}
 Prostokat search_bounds()const{return search->obszar_ekranu();}
 void rozmiesc(Prostokat r)override{
  PowierzchniaPopup::rozmiesc(r);auto s=skala_dpi_;int pad=10*s,gap=8*s;
  row_height=CzcionkaUI::pobierz_wysokosc(s)+12*s;if(row_height<30*s)row_height=30*s;
  int w=r.szerokosc-2*pad;
  search->rozmiesc({pad,pad,w,row_height});
  prepare_icons();
  int side=36*s,inset=8*s;power->rozmiesc({r.szerokosc-inset-side,r.wysokosc-inset-side,side,side});
  int top=pad+row_height+gap,bottom=r.wysokosc-pad-side-2*gap;
  int h=bottom-top;if(h<0)h=0;rows=h/row_height;if(rows<1)rows=1;
  list->rozmiesc({pad,top,w,h});model.visible(rows);
 }
 void rysuj_siebie(KontekstRysowania& ctx)override{
  rysuj_zaokraglone_tlo(ctx);auto s=skala_dpi_;int y=power->obszar().y-4*s;
  ctx.rysuj_prostokat({10*s,y,obszar_.szerokosc-20*s,s},motyw_kolor(MOTYW_KOLOR_RAMKA));
 }
 void open(Prostokat work,int x){
  hover_deadline=leave_deadline=0;pointer_power=pointer_popup=false;
  events.reset_input();pending=-1;list->reset();search->ustaw_tekst("");model.filter("");
  events.geometria({0,0,work.szerokosc,work.wysokosc},work);auto s=ui_skala();
  if(x<work.x)x=work.x;
  int width=340*s;for(auto& app:apps){int measured=CzcionkaUI::zmierz_tekst(app.name,s)+44*s;if(measured>width)width=measured;}
  if(width>work.prawy()-x)width=work.prawy()-x;
  if(width>2048-2*s)width=2048-2*s;
  int height=600*s;if(height>work.wysokosc-12*s)height=work.wysokosc-12*s;
  if(height>2048-2*s)height=2048-2*s;
  events.popups.otworz(*this,{x,work.dolny()-height,width,height},nullptr,search);
  events.popups.odrysuj_popupy(true);
 }
 void open_power(){
  if(power_popup.czy_otwarta())return;
  hover_deadline=leave_deadline=0;power_popup.zaktualizuj_motyw();
  auto where=place_power();
  events.popups.otworz_podmenu(power_popup,where,&power_popup);power_popup.ustaw_aktywny_indeks(2);
  events.popups.odrysuj_popupy(true);
 }
 bool power_open()const{return power_popup.czy_otwarta();}
 Prostokat power_popup_bounds()const{return power_popup.obszar();}
 const Menu& power_menu()const{return power_items;}
 bool handle(const bws_zdarzenie& e){
  if(!czy_otwarta())return events.zdarzenie(e);
  uint64_t now=gui_czas_monotoniczny_ns();
  if(e.typ==BWS_ZDARZENIE_MYSZ_RUCH){
   pointer_popup=power_popup.czy_otwarta()&&power_popup.obszar().zawiera(e.x,e.y);
   pointer_power=!pointer_popup&&power->obszar_ekranu().zawiera(e.x,e.y);
   if(pointer_power&&!power_popup.czy_otwarta()){if(!hover_deadline)hover_deadline=now+300000000ULL;}else hover_deadline=0;
   if(power_popup.czy_otwarta()&&!pointer_popup&&!pointer_power){if(!leave_deadline)leave_deadline=now+350000000ULL;}else leave_deadline=0;
  }
  if(e.typ==BWS_ZDARZENIE_MYSZ_DOWN&&power_popup.czy_otwarta()&&!power_popup.obszar().zawiera(e.x,e.y))close_power();
  if(e.typ==BWS_ZDARZENIE_TIMER){
   if(hover_deadline&&now>=hover_deadline)open_power();
   if(leave_deadline&&now>=leave_deadline)close_power();
  }
  return events.zdarzenie(e);
 }
 void close(){hover_deadline=leave_deadline=0;events.popups.zamknij();}
 void po_zamknieciu(PowodZamknieciaPopup p)override{
  search->ustaw_tekst("");model.filter("");PowierzchniaPopup::po_zamknieciu(p);
 }
 void refresh(){
  events.popups.zaktualizuj_motyw();
  if(czy_otwarta()){
   rozmiesc(obszar_);
   if(power_popup.czy_otwarta())events.popups.przemiesc_powierzchnie(power_popup,
    place_power());
   uniewaznij();events.popups.odrysuj_popupy(true);
  }
 }
};
}
