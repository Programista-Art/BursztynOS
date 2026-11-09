#pragma once
#include "../../bursztyn_ui/bursztyn_ui.h"
#include "system_identity.h"
#include "content_metrics.h"
namespace SettingsUI {
using namespace BursztynUI;
class SystemPage : public WidokPrzewijany {
public:
    Panel* content;
    Etykieta* title;
    Etykieta* sections[SystemIdentity::PropertyCount]{};
    Etykieta* labels[SystemIdentity::PropertyCount]{};
    Etykieta* values[SystemIdentity::PropertyCount]{};
    SystemPage(){
        ustaw_grubosc_ramki(0);
        ustaw_tryb_paska_poziomego(TrybPaskaPrzewijania::Nigdy);
        content=&dodaj<Panel>();content->ustaw_uklad_automatyczny(false);content->ustaw_wypelnienie({0,0,0,0});
        title=&content->dodaj<Etykieta>(SystemIdentity::Name);
        title->ustaw_wypelnienie({0,0,0,0});title->ustaw_skale_czcionki(2);
        for(size_t i=0;i<SystemIdentity::PropertyCount;++i){
            const auto& p=SystemIdentity::Properties[i];
            if(SystemIdentity::starts_section(i)){sections[i]=&content->dodaj<Etykieta>(p.section);sections[i]->ustaw_wypelnienie({0,0,0,0});}
            labels[i]=&content->dodaj<Etykieta>(p.label);values[i]=&content->dodaj<Etykieta>(p.value);
            Etykieta* row[]={labels[i],values[i]};
            for(auto* label:row){label->ustaw_wypelnienie({0,0,0,0});label->ustaw_zawijanie(true);}
        }
    }
    void zaktualizuj_motyw()override{WidokPrzewijany::zaktualizuj_motyw();ustaw_grubosc_ramki(0);}
    void rozmiesc(Prostokat r)override{
        WidokPrzewijany::rozmiesc(r);auto s=skala_dpi_;
        int line=CzcionkaUI::pobierz_wysokosc(s),gap=12*s,pad=ContentInset*s,w=r.szerokosc-18*s-2*pad;
        if(w<1)w=1;
        int label_width=0;
        for(const auto& p:SystemIdentity::Properties){int width=CzcionkaUI::zmierz_tekst(p.label,s);if(width>label_width)label_width=width;}
        bool stacked=label_width+gap+180*s>w;
        int value_x=stacked?0:label_width+gap,value_w=w-value_x;
        int y=2*line;
        title->rozmiesc({0,0,w,y});
        for(size_t i=0;i<SystemIdentity::PropertyCount;++i){
            if(sections[i]){y+=gap;sections[i]->rozmiesc({0,y,w,line});y+=line+gap/2;}
            int lh=labels[i]->zmierz({stacked?w:label_width,4096*s}).wysokosc;
            int vh=values[i]->zmierz({value_w,4096*s}).wysokosc;
            labels[i]->rozmiesc({0,y,stacked?w:label_width,lh});
            values[i]->rozmiesc({value_x,y+(stacked?lh:0),value_w,vh});
            y+=(stacked?lh+vh:(lh>vh?lh:vh))+6*s;
        }
        content->rozmiesc({pad,pad,w,y+gap});ustaw_rozmiar_zawartosci({w+2*pad,y+gap+2*pad});
    }
};
}
