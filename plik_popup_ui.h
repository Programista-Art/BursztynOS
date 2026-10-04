#pragma once
#include "plik_operacje_ui.h"
#include "bursztyn_ui/menu_kontekstowe.h"
#include "bursztyn_ui/pole_tekstowe.h"
#include "bursztyn_ui/etykieta.h"
#include "bursztyn_ui/dialog_pliku.h"
#include "bursztyn_ui/przycisk.h"

namespace PlikiUI {
using namespace BursztynUI;
enum class Akcja : uint8_t {
    NONE, OPEN, RUN, COPY, CUT, RENAME, REMOVE, PROPERTIES,
    PASTE, NEW_FILE, NEW_FOLDER, REFRESH, OPEN_WITH, NOTEPAD, MOVE
};
struct CelMenu { bool selected{false}, directory{false}, text{false}, program{false}, paste{false}; unsigned count{1}; };
inline bool dostepna(Akcja a, CelMenu t) {
    switch (a) {
    case Akcja::COPY: case Akcja::CUT: case Akcja::REMOVE: return t.selected;
    case Akcja::OPEN: case Akcja::RENAME: case Akcja::MOVE: case Akcja::PROPERTIES: return t.selected && t.count==1;
    case Akcja::OPEN_WITH: case Akcja::NOTEPAD: return t.selected && t.count==1 && !t.directory && t.text;
    case Akcja::RUN: return t.selected && t.count==1 && t.program;
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
    void limit_tekstu(PoleTekstowe* field,size_t bytes) {limit_field_=field;limit_bytes_=bytes;}
    void reset_input() { skip_count_=skip_pos_=ansi_=byte_count_=0; }
    static uint32_t klawisz(const bws_zdarzenie& e) {
        unsigned code=e.kod&255;
        if(e.kod&BWS_KEY_SOURCE_USB) {
            switch(code) {
            case 40:return 13; case 41:return 27; case 42:return 8; case 43:return 9;
            case 76:return 0x53; case 79:return 0x4d; case 80:return 0x4b;
            case 81:return 0x50; case 82:return 0x48; case 74:return 0x47;
            case 77:return 0x4f; case 75:return 0x49; case 78:return 0x51;
            default:return e.kod;
            }
        }
        switch(code&127) {
        case 14:return 8; case 28:return 13; case 1:return 27; case 15:return 9;
        default:return (e.kod&BWS_KEY_SOURCE_E0)?code:e.kod;
        }
    }
    bool wymaga_odswiezenia() const {return needs_refresh_;}
    bool zdarzenie(const bws_zdarzenie& e) {
        needs_refresh_=e.typ!=BWS_ZDARZENIE_TIMER;
        if(e.typ==BWS_ZDARZENIE_MYSZ_PRAWY_UP && consume_right_up_) {consume_right_up_=false;return true;}
        if(e.typ==BWS_ZDARZENIE_MYSZ_UP && consume_up_) {consume_up_=false;return true;}
        if(e.typ==BWS_ZDARZENIE_KLAWISZ && skip_pos_<skip_count_) {
            if(e.kod==static_cast<unsigned char>(skip_[skip_pos_])) {++skip_pos_;return true;}
            skip_count_=skip_pos_=0;
        }
        if(!popups.czy_otwarty()) return false;
        ZdarzenieMyszki m{};m.x=e.x;m.y=e.y;m.przycisk=1;
        switch(e.typ) {
        case BWS_ZDARZENIE_MYSZ_RUCH:popups.obsluz_ruch(m);break;
        case BWS_ZDARZENIE_MYSZ_DOWN:popups.obsluz_wcisniecie(m);consume_up_=!popups.czy_otwarty();break;
        case BWS_ZDARZENIE_MYSZ_UP:popups.obsluz_zwolnienie(m);break;
        case BWS_ZDARZENIE_MYSZ_PRAWY_DOWN:
            // Modal right clicks are consumed without changing focus, caret or
            // selection. Context menus keep their existing dismissal policy.
            if(!popups.czy_ma_modalna())popups.zamknij();
            consume_right_up_=true;break;
        case BWS_ZDARZENIE_MYSZ_KOLO: {
            ZdarzenieRolki r{};r.x=e.x;r.y=e.y;r.delta_y=e.dy;r.delta_x=e.dx;popups.obsluz_rolke(r);break;
        }
        case BWS_ZDARZENIE_TEXT_INPUT: {
            skip_count_=CzcionkaUI::zakoduj_utf8(e.kod,skip_);skip_pos_=0;
            ZdarzenieWprowadzaniaTekstu t{};t.punkt_kodowy=e.kod;t.modyfikatory=e.przyciski;send_text(t);break;
        }
        case BWS_ZDARZENIE_KLAWISZ_FIZYCZNY:
            if(e.x) {
                uint32_t key=klawisz(e);send_key(key,e.przyciski,e.kod);
                const char* legacy=key==8?"\b":key==13?"\n":key==27?"\x1b":key==9?"\t":
                    key==0x4b?"\x1b[D":key==0x4d?"\x1b[C":key==0x48?"\x1b[A":key==0x50?"\x1b[B":
                    key==0x53?"\x1b[3~":key==0x47?"\x1b[H":key==0x4f?"\x1b[F":
                    key==0x49?"\x1b[5~":key==0x51?"\x1b[6~":nullptr;
                if(legacy) {skip_count_=0;skip_pos_=0;while(legacy[skip_count_] && skip_count_<4) {skip_[skip_count_]=legacy[skip_count_];++skip_count_;}}
            }
            break;
        case BWS_ZDARZENIE_KLAWISZ: {
            unsigned char c=e.kod;
            if(ansi_==1) {ansi_=c=='['?2:0;break;}
            if(ansi_==2) {
                ansi_=0;
                if(c>='1' && c<='6') {ansi_=10+c-'0';break;}
                send_key(c=='A'?0x48:c=='B'?0x50:c=='C'?0x4d:c=='D'?0x4b:c=='H'?0x47:c=='F'?0x4f:0);break;
            }
            if(ansi_>=10) {int n=ansi_-10;ansi_=0;if(c=='~')send_key(n==3?0x53:n==1?0x47:n==4?0x4f:n==5?0x49:n==6?0x51:0);break;}
            if(c==27) {ansi_=1;break;}
            if(c<32 || c==127) {send_key(c==127?0x53:c==10?13:c);break;}
            if(byte_count_ && (c&192)!=128)byte_count_=0;
            if(!byte_count_ && c>=128 && (c<194 || c>244))break;
            bytes_[byte_count_++]=char(c);bytes_[byte_count_]=0;
            unsigned char first=bytes_[0];unsigned expected=first<128?1:first<224?2:first<240?3:4;
            if(byte_count_==expected) {
                uint32_t cp=0;if(CzcionkaUI::dekoduj_utf8(bytes_,0,&cp)==expected) {ZdarzenieWprowadzaniaTekstu t{};t.punkt_kodowy=cp;send_text(t);}
                byte_count_=0;
            }
            break;
        }
        case BWS_ZDARZENIE_TIMER:if(ansi_==1) {ansi_=0;send_key(27);needs_refresh_=true;}break;
        default:break;
        }
        if(popups.czy_otwarty())needs_refresh_ |= popups.odrysuj_popupy();
        else needs_refresh_=true;
        return true;
    }
private:
    bool needs_refresh_{false};
    bool consume_up_{false},consume_right_up_{false};
    char skip_[5]{},bytes_[5]{};
    unsigned skip_count_{0},skip_pos_{0},byte_count_{0};int ansi_{0};
    PoleTekstowe* limit_field_{nullptr};size_t limit_bytes_{0};
    void send_text(ZdarzenieWprowadzaniaTekstu& t) {
        if(limit_field_ && popups.fokus_popupu()==limit_field_) {
            char bytes[5]{};size_t add=CzcionkaUI::zakoduj_utf8(t.punkt_kodowy,bytes),len=0;
            while(limit_field_->tekst()[len])++len;
            if(len-limit_field_->dlugosc_zaznaczenia()+add>limit_bytes_)return;
        }
        popups.obsluz_tekst(t);
    }
    void send_key(uint32_t key,uint32_t mods=0,uint32_t scan=0) {
        if(!key)return;
        ZdarzenieKlawiatury k{};k.kod_klawisza=key;k.kod_skanu=scan;k.modyfikatory=mods;popups.obsluz_klawisz(k);
    }

};

class MenuPlikowe {
public:
    ObslugaPopup events;
    MenuKontekstowe menu;
    void otworz(int x,int y,CelMenu target,bool desktop=false) {
        menu.ukryj(); events.reset_input();menu.menu().wyczysc(); pending_=Akcja::NONE;
        const Akcja actions[]={Akcja::OPEN,Akcja::OPEN_WITH,Akcja::NOTEPAD,Akcja::RENAME,
            Akcja::CUT,Akcja::COPY,Akcja::PASTE,Akcja::MOVE,Akcja::REMOVE,
            Akcja::NEW_FOLDER,Akcja::NEW_FILE,Akcja::REFRESH};
        const char* labels[]={"Otwórz","Otwórz za pomocą","Notatnik","Zmień nazwę",
            "Wytnij","Kopiuj","Wklej","Przenieś","Usuń",
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
        field_->przy_zatwierdzeniu([](PoleTekstowe*,const char*,void* data) {static_cast<DialogPlikowy*>(data)->result_=1;},this);
    }
    bool otworz(const char* title,const char* initial,const char* question,
                Prostokat owner,Prostokat work,size_t limit=55,const char* accept_label="OK") {
        events.popups.zamknij(); events.reset_input(); result_=0;
        confirmation_=initial==nullptr; events.limit_tekstu(field_,limit);
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
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if(e.kod_klawisza==9) {
            Kontrolka* current=events.popups.fokus_popupu();
            Kontrolka* next=e.czy_shift()?(current==cancel_?static_cast<Kontrolka*>(ok_):current==ok_&&!confirmation_?static_cast<Kontrolka*>(field_):cancel_):
                (current==field_?static_cast<Kontrolka*>(ok_):current==ok_?cancel_:confirmation_?static_cast<Kontrolka*>(ok_):field_);
            events.popups.ustaw_fokus_popupu(next);e.obsluzone=true;
        }
    }
    bool zdarzenie(const bws_zdarzenie& e) {
        bool open=czy_otwarta();bool handled=events.zdarzenie(e);
        if(open && !czy_otwarta() && !result_)result_=2;
        return handled;
    }
    void zamknij() { events.popups.zamknij(); result_=0; }
private:
    Etykieta *title_,*message_;
    PoleTekstowe* field_;
    Przycisk *ok_,*cancel_;
    bool confirmation_{false}; int result_{0};
    static void accept(Przycisk*,void* data) {static_cast<DialogPlikowy*>(data)->result_=1;}
    static void cancel(Przycisk*,void* data) {static_cast<DialogPlikowy*>(data)->result_=2;}
};
class WyborKatalogu : public DialogPliku {
public:
    ObslugaPopup events;
    WyborKatalogu() : DialogPliku(TrybDialoguPliku::WybierzKatalog,"Przenieś — folder docelowy") {}
    bool otworz(const char* source,const char* directory,Prostokat owner,Prostokat work) {
        events.popups.zamknij();events.reset_input();events.geometria(owner,work);
        ustaw_tekst(source);ustaw_nazwe_poczatkowa("");ustaw_katalog_poczatkowy(directory);ustaw_skale_dpi(ui_skala());zaktualizuj_motyw();
        bool ok=pokaz(events.popups);events.popups.odrysuj_popupy(true);return ok;
    }
    void zamknij() {events.popups.zamknij();}
protected:
    void oblicz_geometrie_dialogu(int& w,int& h) override {w=620*skala_dpi_;h=420*skala_dpi_;}
};
} // namespace PlikiUI
