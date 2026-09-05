#pragma once
#include "pole_tekstowe.h"
#include "przycisk.h"
#include "popup.h"

namespace BursztynUI {

class SelektorKoloru : public Kontrolka {
public:
    static constexpr uint32_t WIDOCZNA_CZERN = 0x01000000U;
    using FunkcjaZmiany = void (*)(SelektorKoloru*, uint32_t, void*);
private:
    class Picker : public PowierzchniaPopup {
    public:
        explicit Picker(SelektorKoloru* owner) : owner_(owner) {
            czy_reaguje_wizualnie_na_najechanie_ = false;
            for (int i = 0; i < 4; ++i) {
                pola_[i] = new PoleTekstowe(i == 3 ? "#RRGGBB" : "0–255");
                pola_[i]->ustaw_maksymalna_liczbe_znakow(i == 3 ? 7 : 3);
                dodaj_kontrolke(pola_[i]);
                pola_[i]->przy_zmianie([](PoleTekstowe* pole, const char*, void* d) {
                    auto* p = static_cast<Picker*>(d);
                    if (p->synchronizacja_) return;
                    int index = 0; while (index < 4 && p->pola_[index] != pole) ++index;
                    p->wczytaj_pole(index);
                }, this);
            }
            ok_ = new Przycisk("OK"); anuluj_ = new Przycisk("Anuluj");
            dodaj_kontrolke(ok_); dodaj_kontrolke(anuluj_);
            ok_->przy_kliknieciu([](Przycisk*, void* d) { static_cast<Picker*>(d)->zatwierdz(); }, this);
            anuluj_->przy_kliknieciu([](Przycisk*, void* d) {
                auto* p = static_cast<Picker*>(d); if (p->zarzadca()) p->zarzadca()->zamknij(PowodZamknieciaPopup::Anulowanie);
            }, this);
        }
        void po_otwarciu() override { poprzedni_ = owner_->kolor_; ustaw_rgb(poprzedni_); synchronizuj(); }
        void po_zamknieciu(PowodZamknieciaPopup powod) override {
            ciagniecie_ = 0; czy_wcisnieta_ = false;
            PowierzchniaPopup::po_zamknieciu(powod);
        }
        void rozmiesc(Prostokat r) override {
            PowierzchniaPopup::rozmiesc(r);
            int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
            int fieldw = (r.szerokosc - 32*s) / 3;
            int fieldh = r.wysokosc - 150*s; if (fieldh < 32) fieldh = 32;
            sv_ = Prostokat(12*s, 30*s, r.szerokosc - 112*s, fieldh);
            hue_ = Prostokat(sv_.prawy()+8*s, sv_.y, 18*s, sv_.wysokosc);
            int y = sv_.dolny()+25*s;
            for (int i = 0; i < 3; ++i) pola_[i]->rozmiesc(Prostokat(12*s+i*(fieldw+4*s), y, fieldw, 26*s));
            pola_[3]->rozmiesc(Prostokat(12*s, y+34*s, r.szerokosc/2-18*s, 26*s));
            int bw = (r.szerokosc/2-18*s)/2;
            ok_->rozmiesc(Prostokat(r.szerokosc/2, y+34*s, bw, 26*s));
            anuluj_->rozmiesc(Prostokat(r.szerokosc/2+bw+6*s, y+34*s, bw, 26*s));
        }
        void rysuj_siebie(KontekstRysowania& ctx) override {
            rysuj_tlo_i_ramke(ctx, styl_.normalny);
            int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
            ctx.rysuj_tekst(Punkt(12*s, 8*s), "Wybierz kolor", Kolory::TekstBialy, s);
            Prostokat clip = sv_.przeciecie(ctx.lokalny_klip());
            for (int y = clip.y; !clip.czy_pusty() && y < clip.dolny(); ++y)
                for (int x = clip.x; x < clip.prawy(); ++x)
                    ctx.rysuj_prostokat(Prostokat(x,y,1,1), hsv(h_, (x-sv_.x)*255/(sv_.szerokosc-1), 255-(y-sv_.y)*255/(sv_.wysokosc-1)));
            for (int y = 0; y < hue_.wysokosc; ++y)
                ctx.rysuj_prostokat(Prostokat(hue_.x,hue_.y+y,hue_.szerokosc,1), hsv(y*359/(hue_.wysokosc-1),255,255));
            int px = sv_.x + sat_*(sv_.szerokosc-1)/255, py = sv_.y + (255-val_)*(sv_.wysokosc-1)/255;
            ctx.dodaj_przyciecie(sv_);
            ctx.rysuj_ramke(Prostokat(px-3*s,py-3*s,7*s,7*s),WIDOCZNA_CZERN,s);
            ctx.rysuj_ramke(Prostokat(px-2*s,py-2*s,5*s,5*s),0x00FFFFFF,s);
            ctx.zdejmij_przyciecie();
            int hy = hue_.y+h_*(hue_.wysokosc-1)/359;
            ctx.rysuj_ramke(Prostokat(hue_.x-2*s,hy-2*s,hue_.szerokosc+4*s,5*s),0x00FFFFFF,s);
            int x = hue_.prawy()+8*s, w = obszar_.szerokosc-x-10*s;
            ctx.rysuj_tekst(Punkt(x,sv_.y),"Nowy",Kolory::TekstBialy,s);
            ctx.rysuj_prostokat(Prostokat(x,sv_.y+22*s,w,38*s),roboczy_);
            ctx.rysuj_tekst(Punkt(x,sv_.y+70*s),"Stary",Kolory::TekstBialy,s);
            ctx.rysuj_prostokat(Prostokat(x,sv_.y+92*s,w,38*s),poprzedni_ ? poprzedni_ : WIDOCZNA_CZERN);
            const char* labels[] = {"R", "G", "B"};
            for (int i=0;i<3;++i) ctx.rysuj_tekst(Punkt(pola_[i]->obszar().x,sv_.dolny()+6*s),labels[i],Kolory::TekstBialy,s);
            if (!poprawne()) ctx.rysuj_tekst(Punkt(12*s,obszar_.wysokosc-19*s),"Nieprawidłowa wartość",0x00FF8080,s);
        }
        void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
            if (!e.czy_lewy_przycisk()) return;
            Punkt p(e.x-obszar_ekranu_.x,e.y-obszar_ekranu_.y);
            ciagniecie_ = sv_.zawiera(p) ? 1 : (hue_.zawiera(p) ? 2 : 0);
            if (ciagniecie_) obsluz_ruch_myszy(e);
            e.obsluzone = ciagniecie_ != 0;
        }
        void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
            if (!ciagniecie_) return;
            int x=e.x-obszar_ekranu_.x,y=e.y-obszar_ekranu_.y;
            if (ciagniecie_==1) ustaw_hsv(h_,przytnij((x-sv_.x)*255/(sv_.szerokosc-1),0,255),przytnij(255-(y-sv_.y)*255/(sv_.wysokosc-1),0,255));
            else ustaw_hsv(przytnij((y-hue_.y)*359/(hue_.wysokosc-1),0,359),sat_,val_);
            e.obsluzone=true;
        }
        void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override { ciagniecie_=0; e.obsluzone=true; }
        bool ustaw_hsv(int h,int s,int v) {
            if (h<0||h>359||s<0||s>255||v<0||v>255) return false;
            if (h==h_&&s==sat_&&v==val_) return true;
            h_=h;sat_=s;val_=v;roboczy_=hsv(h,s,v); synchronizuj(); uniewaznij(); return true;
        }
        void ustaw_rgb(uint32_t c) {
            roboczy_=(c&0xFFFFFFU) ? c&0xFFFFFFU : WIDOCZNA_CZERN;
            int r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            int max=r>g?r:g; if(b>max)max=b; int min=r<g?r:g;if(b<min)min=b;
            int d=max-min; val_=max;sat_=max?d*255/max:0;
            if(d) { h_= max==r ? 60*(g-b)/d : (max==g ? 120+60*(b-r)/d : 240+60*(r-g)/d); if(h_<0)h_+=360; }
            synchronizuj(); uniewaznij();
        }
        bool poprawne() const {
            uint32_t c; if(!owner_->parsuj_hex(pola_[3]->tekst(),&c))return false;
            for(int i=0;i<3;++i) if(numer(pola_[i]->tekst())<0)return false;
            return true;
        }
        bool zatwierdz() {
            if(!czy_otwarta()||!poprawne())return false;
            uint32_t c=roboczy_; auto* owner=owner_;
            zarzadca()->zamknij(PowodZamknieciaPopup::Zatwierdzenie);
            owner->ustaw_kolor(c); return true;
        }
        static int numer(const char* p) {
            if(!p||!p[0])return -1;
            int n=0;
            for(int i=0;p[i];++i) { if(i>=3||p[i]<'0'||p[i]>'9')return -1;n=n*10+p[i]-'0'; }
            return n<=255?n:-1;
        }
        void wczytaj_pole(int i) {
            uint32_t c=0;
            if(i==3) { if(!owner_->parsuj_hex(pola_[3]->tekst(),&c)) { uniewaznij();return; } }
            else { int r=numer(pola_[0]->tekst()),g=numer(pola_[1]->tekst()),b=numer(pola_[2]->tekst());
                if(r<0||g<0||b<0) { uniewaznij();return; } c=(r<<16)|(g<<8)|b; }
            // Keep the active field/caret intact while synchronizing other representations.
            aktywne_pole_=i; ustaw_rgb(c); aktywne_pole_=-1;
        }
        void synchronizuj() {
            synchronizacja_=true;
            char b[8]; formatuj(roboczy_,b);
            if(aktywne_pole_!=3&&!rowne(pola_[3]->tekst(),b))pola_[3]->ustaw_tekst(b);
            for(int i=0;i<3;++i) { int v=(roboczy_>>((2-i)*8))&255;char n[4];int k=0;
                if(v>=100)n[k++]='0'+v/100;
                if(v>=10)n[k++]='0'+(v/10)%10;
                n[k++]='0'+v%10;n[k]=0;
                if(i!=aktywne_pole_&&!rowne(pola_[i]->tekst(),n))pola_[i]->ustaw_tekst(n); }
            synchronizacja_=false;
        }
        PoleTekstowe* pola_[4]{};
        uint32_t roboczy_{WIDOCZNA_CZERN};
        Prostokat sv_{},hue_{};
    private:
        SelektorKoloru* owner_; Przycisk* ok_{nullptr}; Przycisk* anuluj_{nullptr};
        uint32_t poprzedni_{0}; int h_{0},sat_{0},val_{0},ciagniecie_{0},aktywne_pole_{-1}; bool synchronizacja_{false};
    };
public:
    explicit SelektorKoloru(uint32_t kolor=0x00FFBF00U) : kolor_(kolor),picker_(this) {
        styl_=Motyw::pobierz_domyslny().styl_pola_tekstowego;czy_moze_miec_fokus_=true;
        czy_reaguje_wizualnie_na_najechanie_=false;czy_reaguje_wizualnie_na_wcisniecie_=false;
        preferowany_rozmiar_=Rozmiar(230,36);picker_.ustaw_rgb(kolor);
    }
    ~SelektorKoloru() override { if(picker_.zarzadca())picker_.zarzadca()->zamknij(PowodZamknieciaPopup::WlascicielZamkniety); }
    uint32_t pobierz_kolor() const { return kolor_; }
    uint32_t pobierzkolor() const { return kolor_; }
    void ustaw_kolor(uint32_t c,bool powiadom=true) {
        if(kolor_==c)return;
        kolor_=c;uniewaznij();
        if(!czy_popup_otwarty())picker_.ustaw_rgb(c);
        if(powiadom&&cb_)cb_(this,c,dane_);
    }
    void ustawkolor(uint32_t c) { ustaw_kolor(c); }
    void przy_zmianie(FunkcjaZmiany cb,void* d=nullptr) { cb_=cb;dane_=d; }
    void przyzmianie(FunkcjaZmiany cb,void* d=nullptr) { przy_zmianie(cb,d); }
    bool parsuj_hex(const char* t,uint32_t* out) const {
        if(!t||!out)return false;
        int i=t[0]=='#'?1:0,start=i;uint32_t v=0;
        for(;t[i]&&i-start<6;++i) { int d=cyfra(t[i]);if(d<0)return false;v=(v<<4)|d; }
        if(i-start!=6||t[i])return false;
        *out=v?v:WIDOCZNA_CZERN;return true;
    }
    bool ustaw_hex(const char* t,bool powiadom=true) { uint32_t c;if(!parsuj_hex(t,&c))return false;ustaw_kolor(c,powiadom);return true; }
    bool czy_hex_poprawny() const { uint32_t c;return parsuj_hex(tekst_hex(),&c); }
    const char* tekst_hex() const { return picker_.pola_[3]->tekst(); }
    PoleTekstowe& pole_hex() { return *picker_.pola_[3]; }
    PoleTekstowe& pole_rgb(int i) { return *picker_.pola_[przytnij(i,0,2)]; }
    bool czy_popup_otwarty() const { return picker_.czy_otwarta(); }
    bool otworz_popup() {
        if(!zarzadca_popupow_||!czy_wlaczona_||czy_tylko_do_odczytu_)return false;
        if(czy_popup_otwarty())return true;
        int s=skala_dpi_<1?1:skala_dpi_;
        return zarzadca_popupow_->otworz(picker_,zarzadca_popupow_->umiesc_pod_kotwica(obszar_ekranu_,Rozmiar(400*s,320*s)),this,&picker_);
    }
    bool ustaw_hsv(int h,int s,int v) { return picker_.ustaw_hsv(h,s,v); }
    bool ustaw_rgb(int r,int g,int b) { if(r<0||r>255||g<0||g>255||b<0||b>255)return false;picker_.ustaw_rgb((r<<16)|(g<<8)|b);return true; }
    uint32_t kolor_podgladu() const { return picker_.roboczy_; }
    bool zatwierdz_popup() { return picker_.zatwierdz(); }
    void anuluj_popup() { if(picker_.zarzadca())picker_.zarzadca()->zamknij(PowodZamknieciaPopup::Anulowanie); }
    Rozmiar oblicz_naturalny_rozmiar() const override { return Rozmiar(230,36); }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        rysuj_tlo_i_ramke(ctx,wyznacz_biezacy_styl_stanu());int s=skala_dpi_<1?1:skala_dpi_;
        Prostokat preview(7*s,6*s,32*s,obszar_.wysokosc-12*s);
        ctx.rysuj_prostokat(preview,kolor_?kolor_:WIDOCZNA_CZERN);ctx.rysuj_ramke(preview,Kolory::RamkaDomyslna,s);
        char hex[8];formatuj(kolor_,hex);ctx.rysuj_tekst(Punkt(48*s,(obszar_.wysokosc-16*s)/2),hex,Kolory::TekstBialy,s);
        int x=obszar_.szerokosc-18*s,y=obszar_.wysokosc/2;
        for(int i=0;i<4;++i)ctx.rysuj_prostokat(Prostokat(x+i*s,y+i*s,(7-2*i)*s,s),Kolory::TekstBialy);
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override { if(czy_wlaczona_&&!czy_tylko_do_odczytu_&&e.czy_lewy_przycisk()){czy_wcisnieta_=true;e.obsluzone=true;} }
    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if(!e.czy_lewy_przycisk())return;
        bool open=czy_wcisnieta_&&obszar_ekranu_.zawiera(e.pozycja());czy_wcisnieta_=false;if(open)otworz_popup();e.obsluzone=open;
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        uint32_t k=e.kod_klawisza;bool usb=(k&BWS_KEY_SOURCE_USB)!=0;uint32_t b=k&255;
        if(k==13||k==32||(usb&&(b==40||b==44))){otworz_popup();e.obsluzone=true;}
    }
    static uint32_t hsv(int h,int s,int v) {
        int c=v*s/255, x=c*(60-((h%120>60)?h%120-60:60-h%120))/60,m=v-c;
        int r=0,g=0,b=0;
        switch(h/60){case 0:r=c;g=x;break;case 1:r=x;g=c;break;case 2:g=c;b=x;break;case 3:g=x;b=c;break;case 4:r=x;b=c;break;default:r=c;b=x;break;}
        uint32_t rgb=((r+m)<<16)|((g+m)<<8)|(b+m);return rgb?rgb:WIDOCZNA_CZERN;
    }
private:
    static int cyfra(char c) { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1; }
    static bool rowne(const char* a,const char* b) { int i=0;while(a[i]&&a[i]==b[i])++i;return a[i]==b[i]; }
    static void formatuj(uint32_t c,char* b) { static const char h[]="0123456789ABCDEF";b[0]='#';for(int i=0;i<6;++i)b[i+1]=h[(c>>((5-i)*4))&15];b[7]=0; }
    uint32_t kolor_; Picker picker_; FunkcjaZmiany cb_{nullptr};void* dane_{nullptr};
};
}
