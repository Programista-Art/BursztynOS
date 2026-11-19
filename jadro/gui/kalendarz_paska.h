#pragma once
/*
 * Kalendarz paska zadan - kompaktowy panel otwierany kliknieciem zegara w Menedzerze Okien.
 *
 * To powierzchnia PowierzchniaPopup zaufanego Menedzera Okien, obslugiwana tym samym mechanizmem co menu Start i
 * menu przepelnienia paska (ZarzadcaPopupow + PlikiUI::ObslugaPopup): interaktywny popup WM, nie dymek BWS 56
 * i nie osobny proces. Siatka dni to istniejacy komponent BursztynUI::Kalendarz; data dzisiejsza pochodzi z BWS 9
 * (bursztyn_ui/data_systemowa.h).
 */
#include "plik_popup_ui.h"
#include "bursztyn_ui/kalendarz.h"

namespace KalendarzPaska {
using namespace BursztynUI;

/* Polozenie panelu: nad paskiem zadan, prawa krawedz przy prawej krawedzi zegara, w granicach ekranu. */
inline Prostokat polozenie(Prostokat zegar, int ekran_w, int pasek_y, int szer, int wys, int margines, int odstep) {
    if (szer > ekran_w - 2 * margines) szer = ekran_w - 2 * margines;
    if (szer < 1) szer = 1;
    if (wys > pasek_y - margines - odstep) wys = pasek_y - margines - odstep;   // niski ekran: panel krotszy, nie na pasku
    if (wys < 1) wys = 1;
    int x = zegar.prawy() - szer;
    if (x > ekran_w - margines - szer) x = ekran_w - margines - szer;
    if (x < margines) x = margines;
    int y = pasek_y - odstep - wys;            // nad paskiem: zegar pozostaje odsloniety
    if (y < margines) y = margines;
    return {x, y, szer, wys};
}

class Surface : public PowierzchniaPopup {
public:
    PlikiUI::ObslugaPopup events;

    Surface() {
        naglowek_ = new Etykieta("");
        kalendarz_ = new Kalendarz();
        dzisiaj_ = new Przycisk("Dzisiaj");
        dodaj_kontrolke(naglowek_);
        dodaj_kontrolke(kalendarz_);
        dodaj_kontrolke(dzisiaj_);
        naglowek_->ustaw_autodopasowanie(false);
        naglowek_->ustaw_wyrownanie_tekstu(WyrownaniePoziome::Srodek);
        kalendarz_->przy_zmianie_daty([](Kalendarz*, DataKalendarza, void* p) { static_cast<Surface*>(p)->aktualizuj_naglowek(); }, this);
        dzisiaj_->przy_kliknieciu([](Przycisk*, void* p) {
            auto* s = static_cast<Surface*>(p);
            s->kalendarz_->pokaz_dzisiaj();
            s->events.popups.ustaw_fokus_popupu(s->kalendarz_);
        }, this);
    }

    /* Rozmiar logiczny (DIP): siatka 6 x 7 z komfortowymi komorkami. */
    static constexpr int SZEROKOSC = 300, WYSOKOSC = 326;

    /* Otwiera panel. dzisiaj_ok = false: brak daty systemowej (siatka pokazuje ostatni widok, bez wyroznienia). */
    bool otworz(DataKalendarza dzis, bool dzisiaj_ok, Prostokat zegar, int ekran_w, int ekran_h, int pasek_y) {
        events.reset_input();
        events.geometria({0, 0, ekran_w, ekran_h}, {0, 0, ekran_w, pasek_y});
        zaktualizuj_motyw();
        dzisiaj_ok_ = dzisiaj_ok;
        kalendarz_->ustaw_dzisiaj(dzisiaj_ok ? dzis : DataKalendarza{0, 0, 0});
        if (dzisiaj_ok) {                       // kazde otwarcie: miesiac z data systemowa
            kalendarz_->ustaw_wybrana_data(dzis, false);
            kalendarz_->ustaw_widok(dzis.rok, dzis.miesiac);
        }
        dzisiaj_->ustaw_wlaczona(dzisiaj_ok);
        aktualizuj_naglowek();
        const SkalaUI s = ui_skala();
        const Prostokat r = polozenie(zegar, ekran_w, pasek_y, SZEROKOSC * s, WYSOKOSC * s, 4 * s, 6 * s);
        if (!events.popups.otworz(*this, r, nullptr, kalendarz_)) return false;
        events.popups.odrysuj_popupy(true);
        return true;
    }
    void zamknij() { if (czy_otwarta()) events.popups.zamknij(); }
    bool obsluz(const bws_zdarzenie& e) { return events.zdarzenie(e); }

    /* Zmiana daty systemowej przy otwartym panelu (np. polnoc): tylko wyroznienie dnia, widok i wybor bez zmian. */
    void ustaw_dzisiaj(DataKalendarza dzis, bool ok) {
        dzisiaj_ok_ = ok;
        kalendarz_->ustaw_dzisiaj(ok ? dzis : DataKalendarza{0, 0, 0});
        dzisiaj_->ustaw_wlaczona(ok);
        if (czy_otwarta() && events.popups.czy_otwarty()) events.popups.odrysuj_popupy(false);
    }
    void odswiez_motyw() {
        events.popups.zaktualizuj_motyw();
        if (czy_otwarta()) { rozmiesc(obszar_); uniewaznij(); events.popups.odrysuj_popupy(true); }
    }

    Kalendarz* kalendarz() { return kalendarz_; }
    Przycisk* przycisk_dzisiaj() { return dzisiaj_; }
    Etykieta* naglowek() { return naglowek_; }

    void rozmiesc(Prostokat r) override {
        PowierzchniaPopup::rozmiesc(r);
        const SkalaUI s = skala_dpi_;
        const int pad = 10 * s, odstep = 6 * s;
        const int h_nagl = CzcionkaUI::pobierz_wysokosc(s) + 8 * s;
        const int h_btn = CzcionkaUI::pobierz_wysokosc(s) + 12 * s;
        int w_btn = CzcionkaUI::zmierz_tekst("Dzisiaj", s) + 28 * s;
        if (w_btn > r.szerokosc - 2 * pad) w_btn = r.szerokosc - 2 * pad;
        naglowek_->rozmiesc({pad, pad, r.szerokosc - 2 * pad, h_nagl});
        const int y_kal = pad + h_nagl + odstep;
        int h_kal = r.wysokosc - y_kal - odstep - h_btn - pad;
        if (h_kal < 0) h_kal = 0;
        kalendarz_->rozmiesc({pad, y_kal, r.szerokosc - 2 * pad, h_kal});
        dzisiaj_->rozmiesc({r.szerokosc - pad - w_btn, r.wysokosc - pad - h_btn, w_btn, h_btn});
    }
    void rysuj_siebie(KontekstRysowania& ctx) override { rysuj_zaokraglone_tlo(ctx); }

    void zaktualizuj_motyw() override {
        PowierzchniaPopup::zaktualizuj_motyw();
        if (naglowek_) naglowek_->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));
    }

private:
    void aktualizuj_naglowek() {
        char t[64];
        if (dzisiaj_ok_ || kalendarz_->wybrana_data().czy_poprawna()) kalendarz_->wybrana_data().formatuj(t, sizeof(t));
        else t[0] = '\0';
        naglowek_->ustaw_tekst(dzisiaj_ok_ ? t : "Data systemowa niedostępna");
    }

    Etykieta* naglowek_{};
    Kalendarz* kalendarz_{};
    Przycisk* dzisiaj_{};
    bool dzisiaj_ok_ = false;
};

}  // namespace KalendarzPaska
