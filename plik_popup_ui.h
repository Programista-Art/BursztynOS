#pragma once
#include "plik_operacje_ui.h"
#include "bursztyn_ui/menu_kontekstowe.h"
#include "bursztyn_ui/pole_tekstowe.h"
#include "bursztyn_ui/etykieta.h"
#include "bursztyn_ui/przycisk.h"

namespace PlikiUI {
using namespace BursztynUI;
enum class Akcja : uint8_t {
    NONE, OPEN, RUN, COPY, CUT, RENAME, REMOVE, PROPERTIES,
    PASTE, NEW_FILE, NEW_FOLDER, REFRESH, OPEN_WITH, NOTEPAD, MOVE
};
struct CelMenu { bool selected{false}, directory{false}, text{false}, program{false}, paste{false}; };
inline bool dostepna(Akcja a, CelMenu t) {
    switch (a) {
    case Akcja::OPEN: case Akcja::COPY: case Akcja::CUT: case Akcja::RENAME:
    case Akcja::REMOVE: case Akcja::MOVE: case Akcja::PROPERTIES: return t.selected;
    case Akcja::OPEN_WITH: case Akcja::NOTEPAD: return t.selected && !t.directory && t.text;
    case Akcja::RUN: return t.selected && t.program;
    case Akcja::PASTE: return t.paste;
    default: return true;
    }
}

// Translates the existing BWS event stream to shared popup event consumption.
// The release that follows an outside closing click is consumed as well.
class ObslugaPopup {
public:
    ZarzadcaPopupow popups;
    void geometria(Prostokat owner, Prostokat work) {
        popups.ustaw_skale_dpi(ui_skala());
        popups.ustaw_geometrie_wlasciciela(owner, work);
    }
    bool zdarzenie(const bws_zdarzenie& e) {
        if(e.typ==BWS_ZDARZENIE_MYSZ_PRAWY_UP && consume_right_up_) {consume_right_up_=false;return true;}
        if (e.typ == BWS_ZDARZENIE_MYSZ_UP && consume_up_) { consume_up_=false; return true; }
        if (e.typ == BWS_ZDARZENIE_KLAWISZ && skip_byte_ >= 0) {
            bool skip = int(e.kod) == skip_byte_; skip_byte_=-1; if (skip) return true;
        }
        if (!popups.czy_otwarty()) return false;
        ZdarzenieMyszki m{}; m.x=e.x; m.y=e.y; m.przycisk=1;
        switch (e.typ) {
        case BWS_ZDARZENIE_MYSZ_RUCH: popups.obsluz_ruch(m); break;
        case BWS_ZDARZENIE_MYSZ_DOWN: popups.obsluz_wcisniecie(m); consume_up_=!popups.czy_otwarty(); break;
        case BWS_ZDARZENIE_MYSZ_UP: popups.obsluz_zwolnienie(m); break;
        case BWS_ZDARZENIE_MYSZ_PRAWY_DOWN: popups.zamknij(); consume_right_up_=true;break;
        case BWS_ZDARZENIE_KLAWISZ:
            if(ansi_==1) {ansi_=e.kod=='['?2:0;break;}
            if(ansi_==2) {
                ansi_=0;
                if(!physical_arrow_) {
                    ZdarzenieKlawiatury k{};k.kod_klawisza=e.kod=='A'?0x48:e.kod=='B'?0x50:e.kod=='C'?0x4d:e.kod=='D'?0x4b:0;
                    if(k.kod_klawisza)popups.obsluz_klawisz(k);
                }
                physical_arrow_=false;break;
            }
            if(e.kod==27)ansi_=1;
            else if(e.kod==10 || e.kod==13) {ZdarzenieKlawiatury k{};k.kod_klawisza=13;popups.obsluz_klawisz(k);}
            break;
        case BWS_ZDARZENIE_TIMER:
            if(ansi_==1) {ansi_=0;popups.zamknij(PowodZamknieciaPopup::Escape);}
            break;
        case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY:
            if (e.x) {
                ZdarzenieKlawiatury k{}; k.kod_klawisza=e.kod; k.modyfikatory=e.przyciski;
                if ((e.kod & BWS_KEY_SOURCE_USB) && (e.kod & 255)==40) k.kod_klawisza=13;
                popups.obsluz_klawisz(k);
                const unsigned code=e.kod & 255;
                if(code==82 || code==81 || code==79 || code==80 || code==0x48 || code==0x50 || code==0x4b || code==0x4d)physical_arrow_=true;
                if (code==40 || code==0x1c) skip_byte_='\n';
                else if (code==41 || code==1) skip_byte_=27;
            }
            break;
        default: break;
        }
        if (popups.czy_otwarty()) popups.odrysuj_popupy();
        return true;
    }
private:
    bool consume_up_{false},consume_right_up_{false};
    int skip_byte_{-1},ansi_{0};
    bool physical_arrow_{false};
};

class MenuPlikowe {
public:
    ObslugaPopup events;
    MenuKontekstowe menu;
    void otworz(int x,int y,CelMenu target,bool desktop=false) {
        menu.ukryj(); menu.menu().wyczysc(); pending_=Akcja::NONE;
        const Akcja actions[]={Akcja::OPEN,Akcja::OPEN_WITH,Akcja::NOTEPAD,Akcja::RENAME,
            Akcja::CUT,Akcja::COPY,Akcja::PASTE,Akcja::MOVE,Akcja::REMOVE,
            Akcja::NEW_FOLDER,Akcja::NEW_FILE,Akcja::REFRESH};
        const char* labels[]={"Otwórz","Otwórz za pomocą","Notatnik","Zmień nazwę",
            "Wytnij","Kopiuj","Wklej","Przenieś","Usuń plik",
            "Utwórz folder","Utwórz plik tekstowy","Odśwież"};
        for (unsigned i=0;i<12;++i) {
            // Same safe policy on desktop and Explorer; empty targets remain disabled.
            (void)desktop;
            menu.menu().dodaj_element(labels[i],static_cast<uint32_t>(actions[i]),activate,this)
                .ustaw_wlaczony(dostepna(actions[i],target));
        }
        menu.pokaz(events.popups,x,y,ui_skala()); events.popups.odrysuj_popupy(true);
    }
    Akcja pobierz_akcje() { Akcja a=pending_; pending_=Akcja::NONE; return a; }
    void zamknij() { menu.ukryj(); }
private:
    Akcja pending_{Akcja::NONE};
    static void activate(ElementMenu& item,void* data) {
        if (item.czy_wlaczony()) static_cast<MenuPlikowe*>(data)->pending_=static_cast<Akcja>(item.identyfikator());
    }
};

// A compact file-operation popup composed entirely from current controls.
class DialogPlikowy : public PowierzchniaPopup {
public:
    ObslugaPopup events;
    DialogPlikowy() {
        ustaw_modalna(true); ustaw_zamykanie_na_klik_poza(false);
        title_=new Etykieta(); message_=new Etykieta(); field_=new PoleTekstowe();
        ok_=new Przycisk("OK"); cancel_=new Przycisk("Anuluj");
        dodaj_kontrolke(title_); dodaj_kontrolke(message_); dodaj_kontrolke(field_);
        dodaj_kontrolke(ok_); dodaj_kontrolke(cancel_);
        ok_->przy_kliknieciu(accept,this); cancel_->przy_kliknieciu(cancel,this);
    }
    bool otworz(const char* title,const char* initial,const char* question,
                Prostokat owner,Prostokat work,size_t limit=55,const char* accept_label="OK") {
        events.popups.zamknij(); result_=0; byte_count_=0; ansi_=0;
        confirmation_=initial==nullptr; limit_=limit;
        title_->ustaw_tekst(title); message_->ustaw_tekst(question ? question : "Nazwa elementu:");
        field_->ustaw_tekst(initial ? initial : ""); field_->ustaw_widoczna(!confirmation_);
        ok_->ustaw_tekst(accept_label); ustaw_skale_dpi(ui_skala()); zaktualizuj_motyw();
        events.geometria(owner,work);
        int natural=CzcionkaUI::zmierz_tekst(title,ui_skala())+24*ui_skala();
        int message_width=CzcionkaUI::zmierz_tekst(question?question:"",ui_skala())+24*ui_skala();
        if(message_width>natural)natural=message_width;
        int w=przytnij(natural,300*ui_skala(),420*ui_skala()),h=confirmation_?148*ui_skala():164*ui_skala();
        Prostokat where=events.popups.umiesc_przy_punkcie_ekranu(owner.x+(owner.szerokosc-w)/2,
            owner.y+(owner.wysokosc-h)/2,Rozmiar(w,h));
        bool opened=events.popups.otworz(*this,where,nullptr,confirmation_?static_cast<Kontrolka*>(cancel_):field_);
        events.popups.odrysuj_popupy(true); return opened;
    }
    void rozmiesc(Prostokat r) override {
        PowierzchniaPopup::rozmiesc(r);
        SkalaUI s=skala_dpi_; int pad=12*s, bh=28*s;
        title_->rozmiesc(Prostokat(pad,10*s,r.szerokosc-2*pad,24*s));
        message_->rozmiesc(Prostokat(pad,40*s,r.szerokosc-2*pad,24*s));
        field_->rozmiesc(Prostokat(pad,70*s,r.szerokosc-2*pad,28*s));
        int bw=90*s; if (bw>(r.szerokosc-3*pad)/2) bw=(r.szerokosc-3*pad)/2;
        ok_->rozmiesc(Prostokat(r.szerokosc-2*bw-2*pad,r.wysokosc-bh-pad,bw,bh));
        cancel_->rozmiesc(Prostokat(r.szerokosc-bw-pad,r.wysokosc-bh-pad,bw,bh));
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        ctx.rysuj_zaokraglony_prostokat(Prostokat(0,0,szerokosc(),wysokosc()),motyw_kolor(MOTYW_KOLOR_PANEL),motyw_promien(MOTYW_ROG_OKNO));
        ctx.rysuj_ramke(Prostokat(0,0,szerokosc(),wysokosc()),motyw_kolor(MOTYW_KOLOR_RAMKA));
    }
    const char* tekst() const override { return field_->tekst(); }
    int wynik() const { return result_; }
    bool zdarzenie(const bws_zdarzenie& e) {
        if (czy_otwarta() && e.typ==BWS_ZDARZENIE_KLAWISZ) {
            unsigned char c=static_cast<unsigned char>(e.kod);
            if (ansi_==1) { ansi_=c=='['?2:0; if (!ansi_) result_=2; return true; }
            if (ansi_==2) {
                ansi_=0; ZdarzenieKlawiatury k{};
                if(c=='D') k.kod_klawisza=0x4b; else if(c=='C') k.kod_klawisza=0x4d;
                else if(c=='3') {ansi_=3;return true;}
                field_->obsluz_wcisniecie_klawisza(k); events.popups.odrysuj_popupy(); return true;
            }
            if(ansi_==3) {ansi_=0; if(c=='~') {ZdarzenieKlawiatury k{};k.kod_klawisza=0x53;field_->obsluz_wcisniecie_klawisza(k);} return true;}
            if (c==27) {ansi_=1;return true;}
            if (c=='\n'||c=='\r') {result_=events.popups.fokus_popupu()==cancel_?2:1;return true;}
            if (!confirmation_) {
                if(c=='\b'||c==127) {ZdarzenieKlawiatury k{};k.kod_klawisza=0x0e;field_->obsluz_wcisniecie_klawisza(k);}
                else if(c>=32) {
                    if(byte_count_==0 && c>=128 && (c<194 || c>244))return true;
                    if(byte_count_ && (c&192)!=128) {byte_count_=0;return true;}
                    if(byte_count_<4) bytes_[byte_count_++]=char(c);
                    bytes_[byte_count_]=0;
                    size_t expected=(static_cast<unsigned char>(bytes_[0])<128)?1:(static_cast<unsigned char>(bytes_[0])<224?2:(static_cast<unsigned char>(bytes_[0])<240?3:4));
                    if(byte_count_==expected) {
                        uint32_t cp=0; size_t used=CzcionkaUI::dekoduj_utf8(bytes_,0,&cp);
                        size_t len=0;while(field_->tekst()[len])++len;
                        if(used==expected && len+expected<=limit_) {ZdarzenieWprowadzaniaTekstu t{};t.punkt_kodowy=cp;field_->obsluz_wprowadzenie_tekstu(t);}
                        byte_count_=0;
                    }
                }
                events.popups.odrysuj_popupy();
            }
            return true;
        }
        if(czy_otwarta() && e.typ==BWS_ZDARZENIE_TIMER && ansi_==1) {ansi_=0;result_=2;return true;}
        // BWS emits text bytes in addition to physical keys. Avoid duplicate edits.
        if(czy_otwarta() && e.typ==BWS_ZDARZENIE_KLAWISZ_FIZYCZNY) {
            unsigned code=e.kod&255;
            if((e.kod&BWS_KEY_SOURCE_USB)?code==43:code==15)return events.zdarzenie(e);
            return true;
        }
        return events.zdarzenie(e);
    }
    void zamknij() { events.popups.zamknij(); result_=0; }
private:
    Etykieta *title_,*message_;
    PoleTekstowe* field_;
    Przycisk *ok_,*cancel_;
    bool confirmation_{false}; size_t limit_{55}; int result_{0},ansi_{0};
    char bytes_[5]{}; size_t byte_count_{0};
    static void accept(Przycisk*,void* data) {static_cast<DialogPlikowy*>(data)->result_=1;}
    static void cancel(Przycisk*,void* data) {static_cast<DialogPlikowy*>(data)->result_=2;}
};
} // namespace PlikiUI
