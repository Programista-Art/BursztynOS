#pragma once
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "content_metrics.h"
namespace SettingsUI {
using namespace BursztynUI;
inline size_t theme_text_length(const char* text){size_t n=0;while(text[n])++n;return n;}
inline int theme_text_compare(const char* a,const char* b){while(*a&&*a==*b){++a;++b;}return int(static_cast<unsigned char>(*a))-int(static_cast<unsigned char>(*b));}
inline bool theme_file_entry(const char* text){const char prefix[]="[PLIK]";for(unsigned i=0;i<6;++i)if(!text[i]||text[i]!=prefix[i])return false;return true;}

// Owns only the kernel transaction token. The kernel owns rollback and persistence.
class ThemePreview {
 uint64_t token_=0,deadline_=0;
public:
 ~ThemePreview(){cancel();}
 bool active()const{return token_!=0;}
 bool cancel(){if(!token_)return true;bool ok=bws_wywolaj(BWS_MOTYW_POLECENIE,MOTYW_CMD_PRZYWROC,token_)==1;token_=deadline_=0;return ok;}
 bool preview(const char* id,uint64_t now){cancel();token_=bws_wywolaj(BWS_MOTYW_POLECENIE,MOTYW_CMD_PODGLAD,reinterpret_cast<uint64_t>(id));deadline_=token_?now+15000000000ULL:0;return active();}
 bool apply(const char* id){remaining(gui_czas_monotoniczny_ns());bool ok=token_?bws_wywolaj(BWS_MOTYW_POLECENIE,MOTYW_CMD_ZATWIERDZ,token_)==1:bws_wywolaj(BWS_MOTYW_POLECENIE,MOTYW_CMD_USTAW,reinterpret_cast<uint64_t>(id))==1;if(ok)token_=deadline_=0;return ok;}
 unsigned remaining(uint64_t now){if(token_&&now>=deadline_){cancel();return 0;}return token_?unsigned((deadline_-now+999999999)/1000000000):0;}
};
inline const char* theme_description(const char* id){
 struct Description{const char* id;const char* text;};
 static constexpr Description descriptions[]={
  {"bursztynnocny","Ciemny motyw bursztynowy o wysokim kontraście i ciepłych barwach."},
  {"baltyckanoc","Głęboki granat z chłodnymi, oceanicznymi akcentami."},
  {"grafit","Neutralna szarość i spokojny styl roboczy."},
  {"bursztynklasyczny","Ciepły, tradycyjny bursztynowo-brązowy styl."},
  {"bursztynjasny","Jasny motyw dzienny o czytelnym wyglądzie."}};
 for(const auto& entry:descriptions)if(!theme_text_compare(id,entry.id))return entry.text;
 return "Motyw systemowy zainstalowany w katalogu motywów.";
}
class ThemesPage : public Panel {
public:
 static constexpr unsigned MaxThemes=64;
 struct Entry{char id[32];char name[32];};
 Entry entries[MaxThemes]{};unsigned count=0;int selected=-1;
 ThemePreview transaction;
 unsigned shown_seconds=0;
 ListaWyboru* list=nullptr;Etykieta *heading=nullptr,*description=nullptr,*current=nullptr,*status=nullptr;Przycisk *preview=nullptr,*apply=nullptr,*cancel=nullptr;
 ThemesPage(){
  ustaw_uklad_automatyczny(false);ustaw_wypelnienie({0,0,0,0});
  heading=&dodaj<Etykieta>("Motywy — dostępne motywy");list=&dodaj<ListaWyboru>();description=&dodaj<Etykieta>("");
  description->ustaw_zawijanie(true);
  preview=&dodaj<Przycisk>("Podgląd (15 s)");apply=&dodaj<Przycisk>("Zastosuj");cancel=&dodaj<Przycisk>("Przywróć");
  current=&dodaj<Etykieta>("");status=&dodaj<Etykieta>("");status->ustaw_zawijanie(true);
  Etykieta* labels[]={heading,description,current,status};
  for(auto* l:labels)l->ustaw_wypelnienie({0,0,0,0});
  list->przy_zmianie_wyboru([](ListaWyboru*,int i,void* p){static_cast<ThemesPage*>(p)->select(i);},this);
  preview->przy_kliknieciu([](Przycisk*,void* p){static_cast<ThemesPage*>(p)->start_preview();},this);
  apply->przy_kliknieciu([](Przycisk*,void* p){static_cast<ThemesPage*>(p)->apply_selected();},this);
  cancel->przy_kliknieciu([](Przycisk*,void* p){auto* s=static_cast<ThemesPage*>(p);s->cancel_preview();},this);
 }
 bool valid()const{return selected>=0&&unsigned(selected)<count;}
 void refresh_current(){char text[96]="Bieżący motyw: ";size_t at=theme_text_length(text);const auto* m=gui_pobierz_motyw();if(m)for(unsigned i=0;i<31&&m->nazwa[i];++i)text[at++]=m->nazwa[i];text[at]=0;current->ustaw_tekst(text);}
 void load(){
  char* directory=new char[8192]{};count=0;selected=-1;list->wyczysc();
  if(wylistuj_katalog_uzytkownika(MOTYW_SCIEZKA_KATALOG,directory,8192)){
   for(size_t at=0;at<8191&&directory[at]&&count<MaxThemes;){
    char filename[64]{};unsigned n=0;bool file=theme_file_entry(directory+at);
    while(at<8191&&directory[at]&&directory[at]!='\n'){if(n<63)filename[n++]=directory[at];++at;}if(directory[at]=='\n')++at;
    if(!file)continue;
    char* name=filename+6;while(*name==' ')++name;size_t len=theme_text_length(name);
    if(len<7||theme_text_compare(name+len-6,".motyw"))continue;
    name[len-6]=0;if(!motyw_waliduj_id(name))continue;
    char id[32]{};__builtin_memcpy(id,name,theme_text_length(name));BursztynMotyw m{};
    if(bws_wywolaj(BWS_MOTYW_POLECENIE,MOTYW_CMD_OPIS,reinterpret_cast<uint64_t>(id),reinterpret_cast<uint64_t>(&m))!=1)continue;
    bool duplicate=false;for(unsigned i=0;i<count;++i)if(!theme_text_compare(entries[i].id,m.id))duplicate=true;if(duplicate)continue;
    __builtin_memcpy(entries[count].id,m.id,32);__builtin_memcpy(entries[count].name,m.nazwa,32);list->dodaj_element(m.nazwa,count);
    if(!theme_text_compare(gui_pobierz_motyw()->id,m.id))selected=int(count);
    ++count;
   }
  }delete[] directory;
  if(selected<0&&count)selected=0;
  list->ustaw_wybrany_indeks(selected);select(selected);refresh_current();
 }
 void select(int i){
  // Expiry can race a selection event before the next animation tick.
  // Reconcile the label as well as the kernel token in that case.
  krok_animacji(gui_czas_monotoniczny_ns());bool restart=transaction.active();selected=i;
  if(!valid()){preview->ustaw_wlaczona(false);apply->ustaw_wlaczona(false);return;}
  preview->ustaw_wlaczona(true);apply->ustaw_wlaczona(true);
  description->ustaw_tekst(theme_description(entries[i].id));
  // A new description can wrap differently. Move the action row with it;
  // countdown ticks only change the status label and never enter this path.
  if(szerokosc()>0){rozmiesc(obszar_);uniewaznij();}
  if(restart)start_preview();
 }
 void show_countdown(unsigned seconds){
  if(seconds==shown_seconds)return;
  shown_seconds=seconds;
  char text[48]="Podgląd aktywny: ";size_t at=theme_text_length(text);
  if(seconds>=10)text[at++]=char('0'+seconds/10);
  text[at++]=char('0'+seconds%10);text[at++]=' ';text[at++]='s';text[at]=0;
  // The normal label invalidation propagates this rectangle through both
  // scroll views. No page/window invalidation or layout is needed per tick.
  status->ustaw_tekst(text);
 }
 void start_preview(){
  if(!valid())return;
  shown_seconds=0;
  if(transaction.preview(entries[selected].id,gui_czas_monotoniczny_ns()))show_countdown(15);
  else status->ustaw_tekst("Nie można uruchomić podglądu.");
 }
 void apply_selected(){
  if(!valid())return;
  if(transaction.apply(entries[selected].id)){
   shown_seconds=0;status->ustaw_tekst("Zapisano motyw.");refresh_current();
  }else status->ustaw_tekst("Nie zapisano motywu.");
 }
 void cancel_preview(){
  transaction.cancel();shown_seconds=0;
  status->ustaw_tekst("Przywrócono zapisany motyw.");refresh_current();
 }
 bool czy_wymaga_animacji()const override{return transaction.active();}
 void krok_animacji(uint64_t now=0)override{
  if(!transaction.active())return;
  unsigned seconds=transaction.remaining(now?now:gui_czas_monotoniczny_ns());
  if(seconds)show_countdown(seconds);
  else {shown_seconds=0;status->ustaw_tekst("Podgląd zakończony. Przywrócono motyw.");refresh_current();}
 }
 int minimum_width()const{
  auto s=skala_dpi_;
  return CzcionkaUI::zmierz_tekst(preview->tekst(),s)+CzcionkaUI::zmierz_tekst(apply->tekst(),s)+
   CzcionkaUI::zmierz_tekst(cancel->tekst(),s)+(3*16+2*12+2*ContentInset)*s;
 }

 void zaktualizuj_motyw()override{Panel::zaktualizuj_motyw();if(current)refresh_current();}
 void rozmiesc(Prostokat r)override{
  Panel::rozmiesc(r);auto s=skala_dpi_;
  int pad=ContentInset*s,line=CzcionkaUI::pobierz_wysokosc(s),gap=12*s,h=line+16*s,w=r.szerokosc-2*pad;
  if(w<1)w=1;
  heading->rozmiesc({pad,pad,w,line});int y=pad+line+gap;
  int lh=5*(line+12*s)+12*s;list->rozmiesc({pad,y,w,lh});y+=lh+gap;
  int dh=description->zmierz({w,4096*s}).wysokosc;
  description->rozmiesc({pad,y,w,dh});y+=dh+gap;
  Przycisk* actions[]={preview,apply,cancel};int widths[3]{},minimum=0,natural=0;
  for(int i=0;i<3;++i){widths[i]=CzcionkaUI::zmierz_tekst(actions[i]->tekst(),s)+16*s;minimum+=widths[i];}
  natural=minimum+3*16*s;
  int available=w-2*gap;
  if(available>natural)available=natural;
  int spare=available>minimum?available-minimum:0;
  int x=pad;
  for(int i=0;i<3;++i){int extra=spare/(3-i);widths[i]+=extra;spare-=extra;actions[i]->rozmiesc({x,y,widths[i],h});x+=widths[i]+gap;}
  y+=h+gap;
  current->rozmiesc({pad,y,w,line});status->rozmiesc({pad,y+line+gap,w,2*line});
 }

};
class ThemesViewport : public WidokPrzewijany {
public:
 ThemesPage* page;
 ThemesViewport(){ustaw_grubosc_ramki(0);ustaw_tryb_paska_poziomego(TrybPaskaPrzewijania::Automatycznie);page=&dodaj<ThemesPage>();}
 void zaktualizuj_motyw()override{WidokPrzewijany::zaktualizuj_motyw();ustaw_grubosc_ramki(0);}
 void rozmiesc(Prostokat r)override{
  WidokPrzewijany::rozmiesc(r);auto s=skala_dpi_;int w=r.szerokosc-18*s;if(w<page->minimum_width())w=page->minimum_width();
  int h=12*CzcionkaUI::pobierz_wysokosc(s)+190*s;if(h<r.wysokosc)h=r.wysokosc;
  ustaw_rozmiar_zawartosci({w,h});page->rozmiesc({0,0,w,h});
 }
};
}
