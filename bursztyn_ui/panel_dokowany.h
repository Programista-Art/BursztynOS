#pragma once

#include "kontrolka.h"

namespace BursztynUI {

enum class Dokowanie : uint8_t {
    Gora,
    Dol,
    Lewo,
    Prawo,
    Wypelnij
};

class PanelDokowany : public Kontrolka {
public:
    static constexpr size_t MAKS_DOKOWANYCH = Kontrolka::MAKS_DZIECI;

    struct WpisDokowania {
        Kontrolka* kontrolka{nullptr};
        Dokowanie tryb{Dokowanie::Wypelnij};
    };

    PanelDokowany() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        wypelnienie_ = Marginesy(4, 4, 4, 4);
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        Kontrolka::zaktualizuj_motyw();
    }

    int odstep() const { return odstep_; }
    void ustaw_odstep(int o) {
        odstep_ = o >= 0 ? o : 0;
        uniewaznij();
    }

    bool dodaj_dokowany(Kontrolka* k, Dokowanie tryb = Dokowanie::Wypelnij) {
        if (!k || liczba_wpisow_ >= MAKS_DOKOWANYCH) return false;
        if (!dodaj_kontrolke(k)) return false;

        wpisy_[liczba_wpisow_++] = WpisDokowania{k, tryb};
        uniewaznij();
        return true;
    }

    template <typename T, typename... Args>
    T& dodaj(Dokowanie tryb, Args&&... args) {
        T* k = new T(args...);
        dodaj_dokowany(k, tryb);
        return *k;
    }

    void ustaw_dokowanie(Kontrolka* k, Dokowanie tryb) {
        for (size_t i = 0; i < liczba_wpisow_; ++i) {
            if (wpisy_[i].kontrolka == k) {
                if (wpisy_[i].tryb != tryb) {
                    wpisy_[i].tryb = tryb;
                    uniewaznij();
                }
                return;
            }
        }
    }

    Dokowanie pobierz_dokowanie(const Kontrolka* k) const {
        for (size_t i = 0; i < liczba_wpisow_; ++i) {
            if (wpisy_[i].kontrolka == k) return wpisy_[i].tryb;
        }
        return Dokowanie::Wypelnij;
    }

    Rozmiar zmierz(Rozmiar dostepny) override {
        SkalaUI s = skala_dpi_;
        int pad_h = wypelnienie_.poziomo() * s;
        int pad_v = wypelnienie_.pionowo() * s;

        int req_w = pad_h;
        int req_h = pad_v;

        for (size_t i = 0; i < liczba_wpisow_; ++i) {
            Kontrolka* child = wpisy_[i].kontrolka;
            if (!child || !child->czy_widoczna()) continue;

            Rozmiar sz = child->zmierz(dostepny);
            Marginesy cm = child->margines();
            int mw = sz.szerokosc + cm.poziomo() * s;
            int mh = sz.wysokosc + cm.pionowo() * s;

            Dokowanie d = wpisy_[i].tryb;
            if (d == Dokowanie::Gora || d == Dokowanie::Dol) {
                req_h += mh + odstep_ * s;
                if (mw + pad_h > req_w) req_w = mw + pad_h;
            } else if (d == Dokowanie::Lewo || d == Dokowanie::Prawo) {
                req_w += mw + odstep_ * s;
                if (mh + pad_v > req_h) req_h = mh + pad_v;
            } else { // Wypelnij
                if (mw + pad_h > req_w) req_w = mw + pad_h;
                if (mh + pad_v > req_h) req_h = mh + pad_v;
            }
        }

        Rozmiar zadany(req_w, req_h);
        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * s;
        return zadany;
    }

    void rozmiesc(Prostokat docelowy) override {
        bool rozmiar_zmieniony = (obszar_.szerokosc != docelowy.szerokosc || obszar_.wysokosc != docelowy.wysokosc);
        obszar_ = docelowy;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        SkalaUI s = skala_dpi_;
        int pad_l = wypelnienie_.lewy * s;
        int pad_t = wypelnienie_.gorny * s;
        int pad_r = wypelnienie_.prawy * s;
        int pad_b = wypelnienie_.dolny * s;

        Prostokat wolny(pad_l, pad_t,
                         obszar_.szerokosc - (pad_l + pad_r),
                         obszar_.wysokosc - (pad_t + pad_b));
        if (wolny.szerokosc < 0) wolny.szerokosc = 0;
        if (wolny.wysokosc < 0) wolny.wysokosc = 0;

        for (size_t i = 0; i < liczba_wpisow_; ++i) {
            Kontrolka* child = wpisy_[i].kontrolka;
            if (!child || !child->czy_widoczna()) continue;

            Marginesy cm = child->margines();
            int cml = cm.lewy * s;
            int cmt = cm.gorny * s;
            int cmr = cm.prawy * s;
            int cmb = cm.dolny * s;

            Rozmiar sz = child->zmierz(wolny.rozmiar());
            Dokowanie d = wpisy_[i].tryb;

            switch (d) {
                case Dokowanie::Gora: {
                    int h = (child->preferowany_rozmiar().czy_okreslona_wysokosc())
                            ? child->preferowany_rozmiar().wysokosc * s : sz.wysokosc;
                    int r_h = h + cmt + cmb;
                    if (r_h > wolny.wysokosc) r_h = wolny.wysokosc;

                    Prostokat child_r(wolny.x + cml, wolny.y + cmt,
                                      wolny.szerokosc - (cml + cmr) > 0 ? wolny.szerokosc - (cml + cmr) : 0,
                                      r_h - (cmt + cmb) > 0 ? r_h - (cmt + cmb) : 0);
                    child->rozmiesc(child_r);

                    int step = r_h + odstep_ * s;
                    wolny.y += step;
                    wolny.wysokosc = (wolny.wysokosc > step) ? wolny.wysokosc - step : 0;
                    break;
                }
                case Dokowanie::Dol: {
                    int h = (child->preferowany_rozmiar().czy_okreslona_wysokosc())
                            ? child->preferowany_rozmiar().wysokosc * s : sz.wysokosc;
                    int r_h = h + cmt + cmb;
                    if (r_h > wolny.wysokosc) r_h = wolny.wysokosc;

                    Prostokat child_r(wolny.x + cml, wolny.dolny() - r_h + cmt,
                                      wolny.szerokosc - (cml + cmr) > 0 ? wolny.szerokosc - (cml + cmr) : 0,
                                      r_h - (cmt + cmb) > 0 ? r_h - (cmt + cmb) : 0);
                    child->rozmiesc(child_r);

                    int step = r_h + odstep_ * s;
                    wolny.wysokosc = (wolny.wysokosc > step) ? wolny.wysokosc - step : 0;
                    break;
                }
                case Dokowanie::Lewo: {
                    int w = (child->preferowany_rozmiar().czy_okreslona_szerokosc())
                            ? child->preferowany_rozmiar().szerokosc * s : sz.szerokosc;
                    int r_w = w + cml + cmr;
                    if (r_w > wolny.szerokosc) r_w = wolny.szerokosc;

                    Prostokat child_r(wolny.x + cml, wolny.y + cmt,
                                      r_w - (cml + cmr) > 0 ? r_w - (cml + cmr) : 0,
                                      wolny.wysokosc - (cmt + cmb) > 0 ? wolny.wysokosc - (cmt + cmb) : 0);
                    child->rozmiesc(child_r);

                    int step = r_w + odstep_ * s;
                    wolny.x += step;
                    wolny.szerokosc = (wolny.szerokosc > step) ? wolny.szerokosc - step : 0;
                    break;
                }
                case Dokowanie::Prawo: {
                    int w = (child->preferowany_rozmiar().czy_okreslona_szerokosc())
                            ? child->preferowany_rozmiar().szerokosc * s : sz.szerokosc;
                    int r_w = w + cml + cmr;
                    if (r_w > wolny.szerokosc) r_w = wolny.szerokosc;

                    Prostokat child_r(wolny.prawy() - r_w + cml, wolny.y + cmt,
                                      r_w - (cml + cmr) > 0 ? r_w - (cml + cmr) : 0,
                                      wolny.wysokosc - (cmt + cmb) > 0 ? wolny.wysokosc - (cmt + cmb) : 0);
                    child->rozmiesc(child_r);

                    int step = r_w + odstep_ * s;
                    wolny.szerokosc = (wolny.szerokosc > step) ? wolny.szerokosc - step : 0;
                    break;
                }
                case Dokowanie::Wypelnij: {
                    Prostokat child_r(wolny.x + cml, wolny.y + cmt,
                                      wolny.szerokosc - (cml + cmr) > 0 ? wolny.szerokosc - (cml + cmr) : 0,
                                      wolny.wysokosc - (cmt + cmb) > 0 ? wolny.wysokosc - (cmt + cmb) : 0);
                    child->rozmiesc(child_r);
                    break;
                }
            }
        }

        if (rozmiar_zmieniony) {
            obsluz_zmiane_rozmiaru();
        }
    }

private:
    WpisDokowania wpisy_[MAKS_DOKOWANYCH]{};
    size_t liczba_wpisow_{0};
    int odstep_{4};
};

} // namespace BursztynUI
