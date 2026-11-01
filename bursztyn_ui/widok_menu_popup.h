#pragma once

#include "popup.h"
#include "menu.h"
#include "czcionka.h"
#include "ikona_ui.h"
#include "ikona_png.h"

namespace BursztynUI {

class WidokMenuPopup;

class ObslugaWidokuMenu {
public:
    virtual ~ObslugaWidokuMenu() = default;
    virtual void menu_aktywuj(WidokMenuPopup&, ElementMenu&) = 0;
    virtual void menu_otworz_podmenu(WidokMenuPopup&, ElementMenu&, Prostokat element) = 0;
    virtual void menu_kierunek_poziomy(WidokMenuPopup&, int kierunek) = 0;
};

class WidokMenuPopup : public PowierzchniaPopup {
public:
    WidokMenuPopup() {
        zaktualizuj_motyw();
    }
    ~WidokMenuPopup() override {
        if (menu_) menu_->przy_zmianie(nullptr, nullptr);
    }
    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_menu;
        uniewaznij();
    }
    void ustaw(Menu* menu, ObslugaWidokuMenu* host) {
        if (menu_ && menu_ != menu) menu_->przy_zmianie(nullptr, nullptr);
        menu_ = menu; host_ = host; aktywny_ = -1; wcisniety_ = -1;
        indeks_otwartego_podmenu_ = -1;
        if (menu_) menu_->przy_zmianie(&WidokMenuPopup::menu_zmienione, this);
        uniewaznij();
    }
    Menu* menu() const { return menu_; }
    int aktywny_indeks() const { return aktywny_; }
    void ustaw_aktywny_indeks(int i) {
        if (i == aktywny_) return;
        if (zarzadca()) zarzadca()->zamknij_od_poziomu(poziom_popupu() + 1);
        indeks_otwartego_podmenu_ = -1;
        uniewaznij_element(aktywny_); aktywny_ = i; uniewaznij_element(aktywny_);
    }
    // Shared columns keep every label left aligned; absent features cost no space.
    struct Kolumny { int znak=0, ikona=0, tekst=0, skrot=0, strzalka=0; };
    Kolumny kolumny(SkalaUI s) const {
        Kolumny c;
        if(!menu_) return c;
        for(size_t i=0;i<menu_->liczba_elementow();++i) {
            const ElementMenu* e=menu_->element(i);
            if(!e || !e->czy_widoczny() || e->czy_separator()) continue;
            if(e->typ_zaznaczenia()!=TypZaznaczeniaMenu::Brak || e->czy_zaznaczony()) c.znak=18*s;
            if(e->obraz_ikony()) c.ikona=24*s;
            else if(e->ikona() && c.ikona<20*s) c.ikona=20*s;
            if(e->podmenu()) c.strzalka=14*s;
            int width=CzcionkaUI::zmierz_tekst(e->tekst(),s);if(width>c.tekst) c.tekst=width;
            width=CzcionkaUI::zmierz_tekst(e->skrot(),s);if(width>c.skrot) c.skrot=width;
        }
        return c;
    }
    int tekst_x(SkalaUI s) const { Kolumny c=kolumny(s);return 1*s+8*s+c.znak+c.ikona; }
    int wysokosc_wiersza(SkalaUI s) const {
        int h=CzcionkaUI::pobierz_wysokosc(s)+8*s;
        int minimum=24*s;
        if(menu_)for(size_t i=0;i<menu_->liczba_elementow();++i){
            auto* e=menu_->element(i);if(e&&e->czy_widoczny()&&e->obraz_ikony())minimum=26*s;
        }
        return h>minimum?h:minimum;
    }
    Rozmiar wymagany_rozmiar(SkalaUI skala = 1) const {
        Kolumny c=kolumny(skala);
        int w=2*skala+16*skala+c.znak+c.ikona+c.tekst+c.strzalka;
        if(c.skrot) w+=12*skala+c.skrot;
        int h=2*skala;
        if(!menu_) return Rozmiar(w,28*skala);
        for(size_t i=0;i<menu_->liczba_elementow();++i) {
            const ElementMenu* e=menu_->element(i);
            if(e && e->czy_widoczny()) h+=e->czy_separator()?9*skala:wysokosc_wiersza(skala);
        }
        return Rozmiar(w,h);
    }
    Prostokat prostokat_elementu(int indeks) const {
        if (!menu_ || indeks < 0) return Prostokat();
        SkalaUI s = skala_dpi_; int y = 1 * s;
        for (int i = 0; i < static_cast<int>(menu_->liczba_elementow()); ++i) {
            const ElementMenu* e = menu_->element(i); if (!e || !e->czy_widoczny()) continue;
            int h = e->czy_separator() ? 9 * s : wysokosc_wiersza(s);
            if (i == indeks) return Prostokat(s, y, obszar_.szerokosc - 2 * s, h);
            y += h;
        }
        return Prostokat();
    }
    Prostokat prostokat_elementu_klienta(int indeks) const {
        Prostokat r = prostokat_elementu(indeks);
        return Prostokat(obszar_.x + r.x, obszar_.y + r.y,
                         r.szerokosc, r.wysokosc);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        // Stan fokusu dotyczy aktualnego wiersza, nie calej powierzchni popupu.
        // Uzycie wyznacz_biezacy_styl_stanu() barwilo caly popup kolorem zaznaczenia.
        rysuj_tlo_i_ramke(ctx, styl_.normalny);
        if (!menu_) return;
        SkalaUI s = skala_dpi_;
        const Kolumny columns=kolumny(s);
        const int label_x=tekst_x(s);
        for (int i = 0; i < static_cast<int>(menu_->liczba_elementow()); ++i) {
            ElementMenu* e = menu_->element(i); if (!e || !e->czy_widoczny()) continue;
            Prostokat r = prostokat_elementu(i);
            if (e->czy_separator()) {
                ctx.rysuj_linie_pozioma(r.x + 8 * s, r.y + r.wysokosc / 2,
                                        r.szerokosc - 16 * s, motyw_kolor(MOTYW_KOLOR_RAMKA));
                continue;
            }
            const bool jest_aktywny = (i == aktywny_ && e->czy_wlaczony());
            const bool jest_wcisniety = (i == wcisniety_ && e->czy_wlaczony());
            const StylStanu& stan_wiersza = jest_wcisniety ? styl_.wcisniety : styl_.najechany;
            if (jest_aktywny || jest_wcisniety) {
                int promien = styl_.normalny.promien_zaokraglenia * s / 2;
                if (promien < 2 * s) promien = 2 * s;
                ctx.rysuj_zaokraglony_prostokat(r, stan_wiersza.tlo, promien);
            }
            uint32_t kolor = e->czy_wlaczony() ? ((jest_aktywny || jest_wcisniety) ? stan_wiersza.tekst : styl_.normalny.tekst)
                                               : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);
            int ty = r.y + (r.wysokosc - CzcionkaUI::pobierz_wysokosc(s)) / 2;
            if (e->czy_zaznaczony()) {
                uint32_t mark_col = (jest_aktywny || jest_wcisniety) ? stan_wiersza.tekst : motyw_kolor(MOTYW_KOLOR_AKCENT);
                if (e->typ_zaznaczenia() == TypZaznaczeniaMenu::Radio)
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(r.x + 8 * s + 3 * s, ty + 5 * s, 6 * s, 6 * s),
                                                    mark_col, 3 * s);
                else {
                    ctx.rysuj_linie_pozioma(r.x + 8 * s, ty + 8 * s, 5 * s, mark_col);
                    ctx.rysuj_linie_pozioma(r.x + 11 * s, ty + 11 * s, 8 * s, mark_col);
                }
            }
            const auto* png=e->obraz_ikony();
            if(png && png->piksele()) {
                int side=png->bok();
                rysuj_png(ctx,{r.x+8*s+columns.znak,r.y+(r.wysokosc-side)/2,side,side},png->piksele(),side,e->czy_wlaczony());
            }else if(e->ikona()){int side=e->obraz_ikony()?18*s:14*s;rysuj_ikone_ui(ctx,{r.x+8*s+columns.znak,r.y+(r.wysokosc-side)/2,side,side},e->ikona(),e->czy_wlaczony(),s);}
            int available=r.prawy()-8*s-columns.strzalka-label_x;
            if(columns.skrot)available-=columns.skrot+12*s;
            char label[100]{};size_t n=0;while(e->tekst()[n]&&n<96){label[n]=e->tekst()[n];++n;}
            if(CzcionkaUI::zmierz_tekst(label,s)>available){
                do{n=CzcionkaUI::poprzedni_znak(label,n);label[n]=char(0xe2);label[n+1]=char(0x80);label[n+2]=char(0xa6);label[n+3]=0;}
                while(n&&CzcionkaUI::zmierz_tekst(label,s)>available);
            }
            ctx.dodaj_przyciecie({label_x,r.y,available>0?available:0,r.wysokosc});
            ctx.rysuj_tekst(Punkt(label_x, ty), label, kolor, s);
            ctx.zdejmij_przyciecie();
            if (e->skrot()[0]) {
                int sw = CzcionkaUI::zmierz_tekst(e->skrot(), s);
                uint32_t skrot_col = (jest_aktywny || jest_wcisniety) ? stan_wiersza.tekst
                    : (e->czy_wlaczony() ? motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY));
                ctx.rysuj_tekst(Punkt(r.prawy() - sw - 8 * s - columns.strzalka, ty), e->skrot(), skrot_col, s);
            }
            if (e->podmenu()) ctx.rysuj_tekst(Punkt(r.prawy() - 8 * s - 6 * s, ty), ">", kolor, s);
        }
    }
    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        int i = indeks_z_punktu(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        if (i != aktywny_) ustaw_aktywny_indeks(i);
        ElementMenu* el = menu_ && i >= 0 ? menu_->element(static_cast<size_t>(i)) : nullptr;
        int nowe_podmenu = el && el->czy_wlaczony() && el->podmenu() ? i : -1;
        if (nowe_podmenu != indeks_otwartego_podmenu_) {
            indeks_otwartego_podmenu_ = nowe_podmenu;
            if (nowe_podmenu >= 0 && host_)
                host_->menu_otworz_podmenu(*this, *el, prostokat_elementu_klienta(i));
            else if (zarzadca())
                zarzadca()->zamknij_od_poziomu(poziom_popupu() + 1);
        }
        e.obsluzone = i >= 0;
    }
    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (indeks_otwartego_podmenu_ < 0 && aktywny_ >= 0) {
            int stary = aktywny_;
            aktywny_ = -1;
            uniewaznij_element(stary);
        }
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!e.czy_lewy_przycisk()) return;
        wcisniety_ = indeks_z_punktu(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        uniewaznij_element(wcisniety_);
        e.obsluzone = wcisniety_ >= 0;
    }
    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        int i = indeks_z_punktu(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        bool wykonaj = i >= 0 && i == wcisniety_;
        int stary_wcisniety = wcisniety_;
        wcisniety_ = -1;
        uniewaznij_element(stary_wcisniety);
        e.obsluzone = i >= 0;
        if (wykonaj) aktywuj(i);
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        uint32_t k = e.polecenie();
        if (k == 0x48U) { // Strzalka w gore
            ustaw_aktywny_indeks(nastepny_aktywny(aktywny_, -1));
        } else if (k == 0x50U) { // Strzalka w dol
            ustaw_aktywny_indeks(nastepny_aktywny(aktywny_, 1));
        } else if (k == 0x47U) { // Home
            ustaw_aktywny_indeks(pierwszy_aktywny(1));
        } else if (k == 0x4FU) { // End
            ustaw_aktywny_indeks(nastepny_aktywny(static_cast<int>(menu_ ? menu_->liczba_elementow() : 0), -1));
        } else if (k == 0x4DU) { // Strzalka w prawo
            int sel = (aktywny_ >= 0) ? aktywny_ : pierwszy_aktywny(1);
            ElementMenu* el = (sel >= 0 && menu_) ? menu_->element(sel) : nullptr;
            if (el && el->czy_wlaczony() && el->podmenu() && host_) {
                ustaw_aktywny_indeks(sel);
                indeks_otwartego_podmenu_ = sel;
                host_->menu_otworz_podmenu(*this, *el, prostokat_elementu_klienta(sel));
            }
            else if (host_) host_->menu_kierunek_poziomy(*this, 1);
        } else if (k == 0x4BU) { // Strzalka w lewo
            if (host_) host_->menu_kierunek_poziomy(*this, -1);
        } else if (k == 13U) { // Enter
            aktywuj(aktywny_);
        } else return;
        e.obsluzone = true;
    }
    void po_zamknieciu(PowodZamknieciaPopup powod) override {
        aktywny_ = -1;
        wcisniety_ = -1;
        indeks_otwartego_podmenu_ = -1;
        PowierzchniaPopup::po_zamknieciu(powod);
    }

private:
    static void menu_zmienione(Menu&, void* dane) {
        WidokMenuPopup* self = static_cast<WidokMenuPopup*>(dane);
        if (!self) return;
        self->aktywny_ = self->pierwszy_aktywny(1);
        self->uniewaznij();
    }
    int indeks_z_punktu(int x, int y) const {
        (void)x;
        if (!menu_) return -1;
        for (int i = 0; i < static_cast<int>(menu_->liczba_elementow()); ++i) {
            ElementMenu* e = menu_->element(i); if (!e || !e->czy_widoczny()) continue;
            if (prostokat_elementu(i).zawiera(x, y)) return e->czy_separator() ? -1 : i;
        }
        return -1;
    }
    int pierwszy_aktywny(int kierunek) const { return nastepny_aktywny(kierunek > 0 ? -1 : 0, kierunek); }
    int nastepny_aktywny(int od, int kierunek) const {
        if (!menu_ || menu_->liczba_elementow() == 0) return -1;
        int n = static_cast<int>(menu_->liczba_elementow());
        for (int krok = 0; krok < n; ++krok) {
            od = (od + kierunek + n) % n;
            ElementMenu* e = menu_->element(od);
            if (e && e->czy_widoczny() && !e->czy_separator() && e->czy_wlaczony()) return od;
        }
        return -1;
    }
    void aktywuj(int i) {
        ElementMenu* e = menu_ && i >= 0 ? menu_->element(i) : nullptr;
        if (!e || !e->czy_wlaczony() || e->czy_separator()) return;
        if (e->podmenu()) {
            indeks_otwartego_podmenu_ = i;
            if (host_) host_->menu_otworz_podmenu(*this, *e, prostokat_elementu_klienta(i));
        }
        else if (host_) host_->menu_aktywuj(*this, *e);
    }
    void uniewaznij_element(int i) { Prostokat r = prostokat_elementu(i); if (!r.czy_pusty()) uniewaznij_obszar(r); }

    Menu* menu_{nullptr};
    ObslugaWidokuMenu* host_{nullptr};
    int aktywny_{-1}, wcisniety_{-1}, indeks_otwartego_podmenu_{-1};
};

} // namespace BursztynUI
