#pragma once
#include "plik_popup_ui.h"

namespace BursztynUI {
// Reusable adapter for a PoleTekstowe owned by an application without Okno.
// Editing and shortcuts remain in the control; popup rendering stays native.
class MenuTekstowe {
public:
    PlikiUI::ObslugaPopup events;
    MenuKontekstowe menu;
    bool otworz(PoleTekstowe& field,int x,int y,Prostokat owner,Prostokat work) {
        menu.ukryj();events.reset_input();events.geometria(owner,work);
        field_=&field;changed_=false;field.obsluz_uzyskanie_skupienia();
        menu.menu().wyczysc();
        uint64_t size=0;
        bool paste=clipboard_size("text/plain",&size) && size>0;
        bool selection=field.czy_ma_zaznaczenie(),editable=!field.czy_tylko_do_odczytu();
        const char* labels[]={"Kopiuj","Wytnij","Wklej","Usuń zaznaczone","Zaznacz wszystko"};
        bool enabled[]={selection && !field.czy_tryb_hasla(),selection && editable && !field.czy_tryb_hasla(),paste && editable,selection && editable,field.tekst()[0]!=0};
        for(unsigned i=0;i<5;++i)menu.menu().dodaj_element(labels[i],i,activate,this).ustaw_wlaczony(enabled[i]);
        bool opened=menu.pokaz(events.popups,x,y,ui_skala(),&field);
        events.popups.odrysuj_popupy(true);return opened;
    }
    bool zdarzenie(const bws_zdarzenie& e) {return events.zdarzenie(e);}
    bool pobierz_zmiane() {bool c=changed_;changed_=false;return c;}
    void zamknij() {menu.ukryj();}
private:
    PoleTekstowe* field_{nullptr};bool changed_{false};
    static void activate(ElementMenu& item,void* data) {
        auto& self=*static_cast<MenuTekstowe*>(data);if(!self.field_ || !item.czy_wlaczony())return;
        switch(item.identyfikator()) {
        case 0:self.field_->kopiuj_do_schowka();break;
        case 1:self.field_->wytnij_do_schowka();break;
        case 2:self.field_->wklej_ze_schowka();break;
        case 3:self.field_->usun_zaznaczony_tekst();break;
        case 4:self.field_->zaznacz_wszystko();break;
        }
        self.changed_=true;
    }
};
}
