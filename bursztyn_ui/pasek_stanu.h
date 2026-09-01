#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

enum class RozmiarSekcjiStanu {
    Stala,
    DopasujDoTresci,
    Rozciagnij
};

class PasekStanu;

class SekcjaStanu {
public:
    SekcjaStanu() = default;

    const char* tekst() const { return tekst_; }
    void ustaw_tekst(const char* t) {
        if (!t) { tekst_[0] = '\0'; return; }
        size_t i = 0;
        while (t[i] != '\0' && i < sizeof(tekst_) - 1) {
            tekst_[i] = t[i];
            ++i;
        }
        tekst_[i] = '\0';
    }

    int szerokosc() const { return szerokosc_; }
    void ustaw_szerokosc(int sz) { szerokosc_ = sz > 0 ? sz : 0; }

    RozmiarSekcjiStanu tryb_rozmiaru() const { return tryb_; }
    void ustaw_tryb_rozmiaru(RozmiarSekcjiStanu t) { tryb_ = t; }

    WyrownaniePoziome wyrownanie() const { return wyrownanie_; }
    void ustaw_wyrownanie(WyrownaniePoziome w) { wyrownanie_ = w; }

    bool czy_widoczna() const { return widoczna_; }
    void ustaw_widoczna(bool w) { widoczna_ = w; }

    Kontrolka* kontrolka() const { return kontrolka_; }
    void ustaw_kontrolke(Kontrolka* k) { kontrolka_ = k; }

    Prostokat prostokat() const { return prostokat_; }

private:
    friend class PasekStanu;
    char tekst_[64]{};
    int szerokosc_{100};
    RozmiarSekcjiStanu tryb_{RozmiarSekcjiStanu::DopasujDoTresci};
    WyrownaniePoziome wyrownanie_{WyrownaniePoziome::Poczatek};
    bool widoczna_{true};
    Kontrolka* kontrolka_{nullptr};
    Prostokat prostokat_{};
};

class PasekStanu : public Kontrolka {
public:
    static constexpr size_t MAKS_SEKCJI = 16;

    PasekStanu() {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = Kolory::PowierzchniaAlternatywna;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = false;
        preferowany_rozmiar_ = Rozmiar(300, 24);
    }

    size_t dodaj_sekcje(const char* tekst = "", int szerokosc = 100,
                        RozmiarSekcjiStanu tryb = RozmiarSekcjiStanu::DopasujDoTresci,
                        WyrownaniePoziome wyr = WyrownaniePoziome::Poczatek) {
        if (liczba_sekcji_ >= MAKS_SEKCJI) return MAKS_SEKCJI - 1;
        size_t idx = liczba_sekcji_++;
        sekcje_[idx] = SekcjaStanu();
        sekcje_[idx].ustaw_tekst(tekst);
        sekcje_[idx].ustaw_szerokosc(szerokosc);
        sekcje_[idx].ustaw_tryb_rozmiaru(tryb);
        sekcje_[idx].ustaw_wyrownanie(wyr);
        przelicz_sekcje();
        uniewaznij();
        return idx;
    }

    bool usun_sekcje(size_t indeks) {
        if (indeks >= liczba_sekcji_) return false;
        if (sekcje_[indeks].kontrolka()) {
            sekcje_[indeks].kontrolka()->ustaw_widoczna(false);
        }
        for (size_t i = indeks; i < liczba_sekcji_ - 1; ++i) {
            sekcje_[i] = sekcje_[i + 1];
        }
        --liczba_sekcji_;
        sekcje_[liczba_sekcji_] = SekcjaStanu();
        przelicz_sekcje();
        uniewaznij();
        return true;
    }

    void wyczysc() {
        for (size_t i = 0; i < liczba_sekcji_; ++i) {
            if (sekcje_[i].kontrolka()) sekcje_[i].kontrolka()->ustaw_widoczna(false);
            sekcje_[i] = SekcjaStanu();
        }
        liczba_sekcji_ = 0;
        uniewaznij();
    }

    size_t liczba_sekcji() const { return liczba_sekcji_; }

    void ustaw_tekst(size_t indeks, const char* tekst) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_tekst(tekst);
            if (sekcje_[indeks].tryb_rozmiaru() == RozmiarSekcjiStanu::DopasujDoTresci) {
                przelicz_sekcje();
                uniewaznij();
            } else {
                uniewaznij_obszar(sekcje_[indeks].prostokat_);
            }
        }
    }
    const char* tekst(size_t indeks) const {
        return (indeks < liczba_sekcji_) ? sekcje_[indeks].tekst() : "";
    }

    void ustaw_szerokosc(size_t indeks, int szerokosc) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_szerokosc(szerokosc);
            przelicz_sekcje();
            uniewaznij();
        }
    }
    int szerokosc(size_t indeks) const {
        return (indeks < liczba_sekcji_) ? sekcje_[indeks].szerokosc() : 0;
    }

    void ustaw_tryb_rozmiaru(size_t indeks, RozmiarSekcjiStanu tryb) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_tryb_rozmiaru(tryb);
            przelicz_sekcje();
            uniewaznij();
        }
    }

    void ustaw_wyrownanie(size_t indeks, WyrownaniePoziome wyr) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_wyrownanie(wyr);
            uniewaznij_obszar(sekcje_[indeks].prostokat_);
        }
    }

    void ustaw_widoczna_sekcje(size_t indeks, bool widoczna) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_widoczna(widoczna);
            przelicz_sekcje();
            uniewaznij();
        }
    }

    void ustaw_kontrolke(size_t indeks, Kontrolka* k) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_kontrolke(k);
            if (k) dodaj_kontrolke(k);
            przelicz_sekcje();
            uniewaznij();
        }
    }

    Prostokat prostokat_sekcji(size_t indeks) const {
        return (indeks < liczba_sekcji_) ? sekcje_[indeks].prostokat_ : Prostokat();
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        przelicz_sekcje();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        ctx.rysuj_prostokat(b, styl_.normalny.tlo);
        ctx.rysuj_ramke(b, styl_.normalny.ramka, 1);

        for (size_t i = 0; i < liczba_sekcji_; ++i) {
            SekcjaStanu& s = sekcje_[i];
            if (!s.czy_widoczna() || s.prostokat_.szerokosc <= 0) continue;

            // Rysuj separator po prawej stronie (dla wszystkich poza ostatnią widoczną)
            if (i + 1 < liczba_sekcji_) {
                int sep_x = s.prostokat_.prawy();
                ctx.rysuj_linie_pionowa(sep_x, 3, b.wysokosc - 6, Kolory::RamkaDomyslna);
            }

            if (s.kontrolka()) {
                // Kontrolka rysuje się sama
            } else if (s.tekst()[0] != '\0') {
                Prostokat tekst_r(s.prostokat_.x + 4, s.prostokat_.y + 2,
                                  s.prostokat_.szerokosc > 8 ? s.prostokat_.szerokosc - 8 : 0,
                                  s.prostokat_.wysokosc > 4 ? s.prostokat_.wysokosc - 4 : 0);

                ctx.dodaj_przyciecie(tekst_r);

                int ty = tekst_r.y + (tekst_r.wysokosc - 16) / 2;
                int tx = tekst_r.x;
                if (s.wyrownanie() == WyrownaniePoziome::Srodek) {
                    int tw = CzcionkaUI::zmierz_tekst(s.tekst(), skala_dpi_);
                    tx = tekst_r.x + (tekst_r.szerokosc - tw) / 2;
                } else if (s.wyrownanie() == WyrownaniePoziome::Koniec) {
                    int tw = CzcionkaUI::zmierz_tekst(s.tekst(), skala_dpi_);
                    tx = tekst_r.prawy() - tw;
                }

                ctx.rysuj_tekst(Punkt(tx, ty), s.tekst(), Kolory::TekstBialy, skala_dpi_);
                ctx.zdejmij_przyciecie();
            }
        }
    }

private:
    void przelicz_sekcje() {
        int w = obszar_ekranu_.szerokosc;
        int h = obszar_ekranu_.wysokosc;
        if (w <= 2 || h <= 2) return;

        int dostepna_szerokosc = w - 4;
        int stala_szerokosc = 0;
        size_t liczba_rozciaganych = 0;

        for (size_t i = 0; i < liczba_sekcji_; ++i) {
            SekcjaStanu& s = sekcje_[i];
            if (!s.czy_widoczna()) continue;

            if (s.tryb_rozmiaru() == RozmiarSekcjiStanu::Stala) {
                stala_szerokosc += s.szerokosc();
            } else if (s.tryb_rozmiaru() == RozmiarSekcjiStanu::DopasujDoTresci) {
                int tw = CzcionkaUI::zmierz_tekst(s.tekst(), skala_dpi_) + 16;
                if (s.kontrolka()) {
                    tw = s.kontrolka()->preferowany_rozmiar().szerokosc + 8;
                }
                if (tw < 20) tw = 20;
                s.ustaw_szerokosc(tw);
                stala_szerokosc += tw;
            } else if (s.tryb_rozmiaru() == RozmiarSekcjiStanu::Rozciagnij) {
                ++liczba_rozciaganych;
            }
        }

        int pozostala = dostepna_szerokosc - stala_szerokosc;
        int bazowa_szerokosc_rozciagana = 0;
        int reszta_rozciagana = 0;
        if (liczba_rozciaganych > 0) {
            if (pozostala > 0) {
                bazowa_szerokosc_rozciagana = pozostala / static_cast<int>(liczba_rozciaganych);
                reszta_rozciagana = pozostala % static_cast<int>(liczba_rozciaganych);
            } else {
                bazowa_szerokosc_rozciagana = 0;
                reszta_rozciagana = 0;
            }
        }

        int cur_x = 2;
        int rozciagana_idx = 0;
        for (size_t i = 0; i < liczba_sekcji_; ++i) {
            SekcjaStanu& s = sekcje_[i];
            if (!s.czy_widoczna()) {
                s.prostokat_ = Prostokat();
                if (s.kontrolka()) s.kontrolka()->ustaw_widoczna(false);
                continue;
            }

            int sw = 0;
            if (s.tryb_rozmiaru() == RozmiarSekcjiStanu::Rozciagnij) {
                sw = bazowa_szerokosc_rozciagana + (rozciagana_idx < reszta_rozciagana ? 1 : 0);
                ++rozciagana_idx;
            } else {
                sw = s.szerokosc();
            }

            if (sw < 0) sw = 0;
            if (cur_x + sw > w - 2) {
                sw = (w - 2 > cur_x) ? (w - 2 - cur_x) : 0;
            }

            s.prostokat_ = Prostokat(cur_x, 1, sw, h - 2);
            if (s.kontrolka()) {
                s.kontrolka()->rozmiesc(Prostokat(cur_x + 2,
                                                 2,
                                                 sw > 4 ? sw - 4 : sw,
                                                 h > 4 ? h - 4 : h));
                s.kontrolka()->ustaw_widoczna(sw > 4);
            }
            cur_x += sw + 1;
        }
    }

    SekcjaStanu sekcje_[MAKS_SEKCJI];
    size_t liczba_sekcji_{0};
};

} // namespace BursztynUI
