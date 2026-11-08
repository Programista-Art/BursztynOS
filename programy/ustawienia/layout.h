#pragma once
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "content_metrics.h"
#include "support_ui.h"
#include "system_ui.h"
#include "power_ui.h"
#include "themes_ui.h"
namespace SettingsUI {
using namespace BursztynUI;
constexpr int Padding=14,Gap=12,SmallGap=4;
class FontPage : public Panel {
public:
 Etykieta *heading,*family_label,*size_label,*status,*help;
 ListaRozwijana *families,*size;
 PowierzchniaRysunkowa* preview;
 WidokPrzewijany* preview_view;
 Przycisk *apply,*reset;
 FontPage(){
  ustaw_uklad_automatyczny(false);ustaw_wypelnienie({0,0,0,0});
  heading=&dodaj<Etykieta>("Ustawienia czcionki");family_label=&dodaj<Etykieta>("Systemowa czcionka");size_label=&dodaj<Etykieta>("Rozmiar");
  families=&dodaj<ListaRozwijana>();size=&dodaj<ListaRozwijana>();preview_view=&dodaj<WidokPrzewijany>();preview_view->ustaw_tryb_paska_poziomego(TrybPaskaPrzewijania::Nigdy);preview=&preview_view->dodaj<PowierzchniaRysunkowa>();
  apply=&dodaj<Przycisk>("Zastosuj");reset=&dodaj<Przycisk>("Przywróć domyślne");
  status=&dodaj<Etykieta>("Zastosuj zapisuje zmianę dla aplikacji.");help=&dodaj<Etykieta>("Terminal zachowuje czcionkę stałej szerokości.");
  Etykieta* labels[]={heading,family_label,size_label,status,help};
  for(auto* label:labels)label->ustaw_wypelnienie({0,0,0,0});
 }
 void rozmiesc(Prostokat r)override{
  Panel::rozmiesc(r);auto s=skala_dpi_;int gap=Gap*s,small=SmallGap*s;
  int inset=ContentInset*s,line=CzcionkaUI::pobierz_wysokosc(s),w=r.szerokosc-2*inset;
  int control=families->oblicz_naturalny_rozmiar().wysokosc;
  int button=apply->zmierz({w,r.wysokosc}).wysokosc;if(button<line+12*s)button=line+12*s;
  int size_width=CzcionkaUI::zmierz_tekst("24 px",s)+56*s;if(size_width<100*s)size_width=100*s;
  if(size_width>w/3)size_width=w/3;
  int family_width=w-gap-size_width;
  heading->rozmiesc({inset,inset,w,line});int y=inset+line+gap;
  family_label->rozmiesc({inset,y,family_width,line});size_label->rozmiesc({inset+family_width+gap,y,size_width,line});y+=line+small;
  families->rozmiesc({inset,y,family_width,control});size->rozmiesc({inset+family_width+gap,y,size_width,control});y+=control+gap;
  int preview_line=BSF::height(ui_skaluj(24,s.procent()));
  if(preview_line<1)preview_line=28*s;
  int preview_h=7*(preview_line+2*s)+28*s;
  int available=r.wysokosc-inset-y-button-3*gap-2*line-small;
  if(preview_h>available)preview_h=available;
  if(preview_h<0)preview_h=0;
  preview_view->rozmiesc({inset,y,w,preview_h});
  const int sizes[]={12,14,16,18,20,24};int index=size->wybrany_indeks();if(index<0||index>5)index=2;
  int sample_line=BSF::height(ui_skaluj(sizes[index],s.procent()),families->wybrany_tekst());
  if(sample_line<1)sample_line=line;
  int sample_height=8*(sample_line+2*s)+28*s;
  preview_view->ustaw_rozmiar_zawartosci({w-18*s,sample_height});
  preview->rozmiesc({0,0,w-18*s,sample_height});y+=preview_h+gap;
  int first=CzcionkaUI::zmierz_tekst("Zastosuj",s)+32*s;
  int second=CzcionkaUI::zmierz_tekst("Przywróć domyślne",s)+32*s;
  if(first+second+gap>w){first=(w-gap)/2;second=w-gap-first;}
  apply->rozmiesc({inset,y,first,button});reset->rozmiesc({inset+first+gap,y,second,button});y+=button+gap;
  status->rozmiesc({inset,y,w,line});help->rozmiesc({inset,y+line+small,w,line});
 }
};
class Body : public Panel {
public:
 Etykieta* title;Przycisk* nav[6]{};ThemesPage* themes;ThemesViewport* themes_view;FontPage* fonts;SupportPage* support;SystemPage* system;PowerPage* power;Panel* placeholder;Etykieta* placeholder_text;
 Body(){
  ustaw_uklad_automatyczny(false);ustaw_wypelnienie({0,0,0,0});
  title=&dodaj<Etykieta>("Ustawienia");title->ustaw_wypelnienie({0,0,0,0});
  const char* names[]={"System","Energia","Wygląd","Motywy","Czcionki","Wsparcie"};for(int i=0;i<6;++i)nav[i]=&dodaj<Przycisk>(names[i]);
  themes_view=&dodaj<ThemesViewport>();themes=themes_view->page;themes_view->ustaw_widoczna(false);system=&dodaj<SystemPage>();power=&dodaj<PowerPage>();power->ustaw_widoczna(false);fonts=&dodaj<FontPage>();fonts->ustaw_widoczna(false);support=&dodaj<SupportPage>();support->ustaw_widoczna(false);placeholder=&dodaj<Panel>();placeholder->ustaw_widoczna(false);placeholder->ustaw_wypelnienie({ContentInset,ContentInset,ContentInset,ContentInset});placeholder_text=&placeholder->dodaj<Etykieta>("");placeholder_text->ustaw_wypelnienie({0,0,0,0});
  ustaw_minimalny_rozmiar({800,480});
 }
 void rozmiesc(Prostokat r)override{
  Panel::rozmiesc(r);auto s=skala_dpi_;int pad=Padding*s,gap=Gap*s,line=CzcionkaUI::pobierz_wysokosc(s);
  int nav_width=CzcionkaUI::zmierz_tekst("Ustawienia",s)+32*s;if(nav_width<150*s)nav_width=150*s;
  int button=nav[0]->zmierz({nav_width,r.wysokosc}).wysokosc;if(button<line+12*s)button=line+12*s;
  title->rozmiesc({pad,pad,nav_width,line});
  for(int i=0;i<6;++i)nav[i]->rozmiesc({pad,pad+line+gap+i*(button+gap),nav_width,button});
  int x=pad+nav_width+gap,w=r.szerokosc-x-pad,h=r.wysokosc-2*pad;if(w<0)w=0;if(h<0)h=0;
  themes_view->rozmiesc({x,pad,w,h});system->rozmiesc({x,pad,w,h});power->rozmiesc({x,pad,w,h});fonts->rozmiesc({x,pad,w,h});support->rozmiesc({x,pad,w,h});placeholder->rozmiesc({x,pad,w,h});
 }
};
// Below the measured minimum, preserve usable controls in a scrollable client.
class Viewport : public WidokPrzewijany {
public:
 Body* body;
 Viewport(){ustaw_grubosc_ramki(0);body=&dodaj<Body>();}
 void zaktualizuj_motyw()override{WidokPrzewijany::zaktualizuj_motyw();ustaw_grubosc_ramki(0);}
 void rozmiesc(Prostokat r)override{
  WidokPrzewijany::rozmiesc(r);auto s=skala_dpi_;
  int h=r.wysokosc>480*s?r.wysokosc:480*s;
  int available=r.szerokosc-(h>r.wysokosc?14*s:0);
  int w=available>800*s?available:800*s;
  if(w>r.szerokosc && h<r.wysokosc+14*s)h=r.wysokosc-14*s>480*s?r.wysokosc-14*s:480*s;
  ustaw_rozmiar_zawartosci({w,h});body->rozmiesc({0,0,w,h});
 }
};

}
