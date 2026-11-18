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
    void ustaw_tekst(const char* t) { kopiuj_utf8(tekst_, sizeof(tekst_), t); }

    // Wzorzec najszerszej typowej tresci (np. "Linii: 00000"): sekcja
    // DopasujDoTresci nie jest wezsza niz wzorzec zmierzony biezaca czcionka,
    // wiec szerokosc nie skacze przy kazdej cyfrze.
    const char* wzorzec() const { return wzorzec_; }
    void ustaw_wzorzec(const char* t) { kopiuj_utf8(wzorzec_, sizeof(wzorzec_), t); }

    // Wyzszy priorytet = dluzej widoczna w waskim oknie.
    int priorytet() const { return priorytet_; }
    void ustaw_priorytet(int p) { priorytet_ = p; }
    bool czy_ukryta_ukladem() const { return ukryta_ukladem_; }

    // Kopia bez rozcinania sekwencji UTF-8 na granicy bufora.
    static void kopiuj_utf8(char* cel, size_t cap, const char* t) {
        if (!cel || !cap) return;
        size_t i = 0;
        if (t) while (t[i] != '\0' && i + 1 < cap) { cel[i] = t[i]; ++i; }
        if (t && t[i] != '\0')
            while (i > 0 && (static_cast<uint8_t>(t[i]) & 0xC0U) == 0x80U) --i;
        cel[i] = '\0';
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
    char tekst_[192]{};
    char wzorzec_[48]{};
    int priorytet_{0};
    bool ukryta_ukladem_{false};
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
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = false;
        preferowany_rozmiar_ = Rozmiar(300, 24);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_PANEL);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = 1;
        przelicz_sekcje();       // motyw moze zmienic czcionke systemowa
        uniewaznij();
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
            const char* nowy = tekst ? tekst : "";
            const char* stary = sekcje_[indeks].tekst();
            size_t i = 0;
            while (stary[i] != '\0' && nowy[i] != '\0' &&
                   stary[i] == nowy[i]) ++i;
            if (stary[i] == nowy[i]) return;
            sekcje_[indeks].ustaw_tekst(tekst);
            // Przeliczenie ukladu tylko, gdy zmienia sie szerokosc sekcji
            // (np. kolejna cyfra ponad wzorzec); inaczej tylko jej obszar.
            if (sekcje_[indeks].tryb_rozmiaru() == RozmiarSekcjiStanu::DopasujDoTresci &&
                szerokosc_tresci(sekcje_[indeks]) != sekcje_[indeks].szerokosc()) {
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

    void ustaw_wzorzec(size_t indeks, const char* wzor) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_wzorzec(wzor);
            przelicz_sekcje();
            uniewaznij();
        }
    }
    void ustaw_priorytet(size_t indeks, int p) {
        if (indeks < liczba_sekcji_) {
            sekcje_[indeks].ustaw_priorytet(p);
            przelicz_sekcje();
            uniewaznij();
        }
    }
    bool czy_sekcja_widoczna_w_ukladzie(size_t indeks) const {
        return indeks < liczba_sekcji_ && sekcje_[indeks].czy_widoczna() &&
               !sekcje_[indeks].ukryta_ukladem_ && sekcje_[indeks].prostokat_.szerokosc > 0;
    }
    // Tekst sekcji po ewentualnym skroceniu z wielokropkiem (jak rysowany).
    void tekst_wyswietlany(size_t indeks, char* out, size_t cap) const {
        if (!out || !cap) return;
        out[0] = '\0';
        if (indeks >= liczba_sekcji_) return;
        const SekcjaStanu& s = sekcje_[indeks];
        int dostepne = s.prostokat_.szerokosc - 2 * margines_tekstu();
        skroc_do_szerokosci(s.tekst(), dostepne, out, cap);
    }

    void ustaw_skale_dpi(SkalaUI s) override {
        Kontrolka::ustaw_skale_dpi(s);
        przelicz_sekcje();
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

    Rozmiar zmierz(Rozmiar dostepny) override {
        // Legacy apps request a 24 px bar. Keep that request, but reserve the
        // current font's full line box so large system fonts retain descenders.
        if (preferowany_rozmiar_.wysokosc != ostatnia_wysokosc_)
            zadana_wysokosc_ = preferowany_rozmiar_.wysokosc;
        int procent = skala_dpi_.procent();
        int line = (CzcionkaUI::pobierz_wysokosc(skala_dpi_) * 100 + procent - 1) / procent;
        int height = line + 4;
        if (zadana_wysokosc_ > height) height = zadana_wysokosc_;
        preferowany_rozmiar_.wysokosc = ostatnia_wysokosc_ = height;
        return Kontrolka::zmierz(dostepny);
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        przelicz_sekcje();
    }

    // Zmiana czcionki systemowej/motywu: szerokosci z metryk nowej czcionki.
    void po_zmianie_czcionki() {
        przelicz_sekcje();
        uniewaznij();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        ctx.rysuj_prostokat(b, styl_.normalny.tlo);
        ctx.rysuj_ramke(b, styl_.normalny.ramka, 1);

        size_t ostatnia = liczba_sekcji_;
        for (size_t i = 0; i < liczba_sekcji_; ++i)
            if (czy_sekcja_widoczna_w_ukladzie(i)) ostatnia = i;
        const int m = margines_tekstu();
        for (size_t i = 0; i < liczba_sekcji_; ++i) {
            SekcjaStanu& s = sekcje_[i];
            if (!czy_sekcja_widoczna_w_ukladzie(i)) continue;

            // Separator po prawej stronie kazdej widocznej sekcji poza ostatnia.
            if (i < ostatnia) {
                int sep_x = s.prostokat_.prawy();
                ctx.rysuj_linie_pionowa(sep_x, 3, b.wysokosc - 6, motyw_kolor(MOTYW_KOLOR_RAMKA));
            }

            if (s.kontrolka()) {
                // Kontrolka rysuje się sama
            } else if (s.tekst()[0] != '\0') {
                Prostokat tekst_r(s.prostokat_.x + m, s.prostokat_.y + 2,
                                  s.prostokat_.szerokosc > 2 * m ? s.prostokat_.szerokosc - 2 * m : 0,
                                  s.prostokat_.wysokosc > 4 ? s.prostokat_.wysokosc - 4 : 0);
                if (tekst_r.szerokosc <= 0) continue;

                // Za dlugi tekst: skrocony na granicy znaku z wielokropkiem
                // (zamiast uciecia w polowie litery przez przyciecie).
                char widoczny[sizeof(s.tekst_) + 4];
                skroc_do_szerokosci(s.tekst(), tekst_r.szerokosc, widoczny, sizeof(widoczny));

                ctx.dodaj_przyciecie(tekst_r);

                int ty = tekst_r.y + (tekst_r.wysokosc - CzcionkaUI::pobierz_wysokosc(skala_dpi_)) / 2;
                int tx = tekst_r.x;
                if (s.wyrownanie() == WyrownaniePoziome::Srodek) {
                    int tw = CzcionkaUI::zmierz_tekst(widoczny, skala_dpi_);
                    tx = tekst_r.x + (tekst_r.szerokosc - tw) / 2;
                } else if (s.wyrownanie() == WyrownaniePoziome::Koniec) {
                    int tw = CzcionkaUI::zmierz_tekst(widoczny, skala_dpi_);
                    tx = tekst_r.prawy() - tw;
                }

                ctx.rysuj_tekst(Punkt(tx, ty), widoczny, motyw_kolor(MOTYW_KOLOR_TEKST), skala_dpi_);
                ctx.zdejmij_przyciecie();
            }
        }
    }

private:
    int zadana_wysokosc_{24}, ostatnia_wysokosc_{-1};

    int margines_tekstu() const { return 6 * skala_dpi_; }

    // Prefiks tekstu mieszczacy sie w szerokosci (z "…" gdy skrocony).
    void skroc_do_szerokosci(const char* t, int szer, char* out, size_t cap) const {
        out[0] = '\0';
        if (!t || szer <= 0) return;
        if (CzcionkaUI::zmierz_tekst(t, skala_dpi_) <= szer) { SekcjaStanu::kopiuj_utf8(out, cap, t); return; }
        static constexpr const char* WIELOKROPEK = "\xE2\x80\xA6";
        const int w_kropek = CzcionkaUI::zmierz_tekst(WIELOKROPEK, skala_dpi_);
        if (cap < 4) return;
        size_t dobre = 0, i = 0;
        char bufor[sizeof(SekcjaStanu::tekst_) + 4];
        while (t[i] && i + 4 < sizeof(bufor) && i + 4 < cap) {
            uint32_t cp = 0;
            size_t n = CzcionkaUI::dekoduj_utf8(t, i, &cp);
            if (!n) break;
            size_t k = 0;
            for (; k < i + n; ++k) bufor[k] = t[k];
            bufor[k] = '\0';
            if (CzcionkaUI::zmierz_tekst(bufor, skala_dpi_) + w_kropek > szer) break;
            i += n;
            dobre = i;
        }
        size_t k = 0;
        for (; k < dobre; ++k) out[k] = t[k];
        while (k > 0 && out[k - 1] == ' ') --k;
        if (w_kropek <= szer) for (size_t j = 0; WIELOKROPEK[j]; ++j) out[k++] = WIELOKROPEK[j];
        out[k] = '\0';
    }

    int szerokosc_tresci(const SekcjaStanu& s) const {
        const int pad = 2 * margines_tekstu() + 2;
        if (s.kontrolka()) return s.kontrolka()->preferowany_rozmiar().szerokosc + 8;
        int tw = CzcionkaUI::zmierz_tekst(s.tekst(), skala_dpi_);
        if (s.wzorzec()[0]) {
            const int ww = CzcionkaUI::zmierz_tekst(s.wzorzec(), skala_dpi_);
            if (ww > tw) tw = ww;
        }
        tw += pad;
        return tw < 20 ? 20 : tw;
    }

    void przelicz_sekcje() {
        int w = obszar_ekranu_.szerokosc;
        int h = obszar_ekranu_.wysokosc;
        if (w <= 2 || h <= 2) return;

        int dostepna_szerokosc = w - 4;
        int stala_szerokosc = 0;
        size_t liczba_rozciaganych = 0;

        for (size_t i = 0; i < liczba_sekcji_; ++i) {
            SekcjaStanu& s = sekcje_[i];
            s.ukryta_ukladem_ = false;
            if (!s.czy_widoczna()) continue;

            if (s.tryb_rozmiaru() == RozmiarSekcjiStanu::Stala) {
                stala_szerokosc += s.szerokosc() + 1;
            } else if (s.tryb_rozmiaru() == RozmiarSekcjiStanu::DopasujDoTresci) {
                int tw = szerokosc_tresci(s);
                s.ustaw_szerokosc(tw);
                stala_szerokosc += tw + 1;
            } else if (s.tryb_rozmiaru() == RozmiarSekcjiStanu::Rozciagnij) {
                ++liczba_rozciaganych;
            }
        }

        // Waskie okno: zamiast nachodzenia/uciecia ukrywane sa cale sekcje
        // o najnizszym priorytecie (sekcje rozciagane i tak dostaja reszte).
        while (stala_szerokosc > dostepna_szerokosc) {
            int kandydat = -1;
            for (size_t i = 0; i < liczba_sekcji_; ++i) {
                const SekcjaStanu& s = sekcje_[i];
                if (!s.czy_widoczna() || s.ukryta_ukladem_ || s.tryb_rozmiaru() == RozmiarSekcjiStanu::Rozciagnij) continue;
                if (kandydat < 0 || s.priorytet() < sekcje_[kandydat].priorytet()) kandydat = static_cast<int>(i);
            }
            if (kandydat < 0) break;
            size_t widocznych = 0;
            for (size_t i = 0; i < liczba_sekcji_; ++i)
                if (sekcje_[i].czy_widoczna() && !sekcje_[i].ukryta_ukladem_ &&
                    sekcje_[i].tryb_rozmiaru() != RozmiarSekcjiStanu::Rozciagnij) ++widocznych;
            if (widocznych <= 1) break;          // najwazniejsza zostaje (przycieta do okna)
            sekcje_[kandydat].ukryta_ukladem_ = true;
            stala_szerokosc -= sekcje_[kandydat].szerokosc() + 1;
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
            if (!s.czy_widoczna() || s.ukryta_ukladem_) {
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
