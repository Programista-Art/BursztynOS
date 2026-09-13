#pragma once

#include "kontrolka.h"
#include "pole_tekstowe.h"

namespace BursztynUI {

class PoleNumeryczne : public Kontrolka {
public:
    using FunkcjaLiczby = void (*)(PoleNumeryczne*, int64_t, void*);

    explicit PoleNumeryczne(int64_t min = 0, int64_t max = 100, int64_t start = 0)
        : minimum_(min), maksimum_(max), wartosc_(start), ostatnia_zatwierdzona_(start) {
        normalizuj_zakres();
        if (wartosc_ < minimum_) wartosc_ = minimum_;
        if (wartosc_ > maksimum_) wartosc_ = maksimum_;
        ostatnia_zatwierdzona_ = wartosc_;

        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        preferowany_rozmiar_ = Rozmiar(120, 26);
        czy_moze_miec_fokus_ = true;

        pole_edycji_ = new PoleWewnetrzne(this);
        dodaj_kontrolke(pole_edycji_);
        pole_edycji_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        aktualizuj_tekst_edycji();

        pole_edycji_->przy_zmianie([](PoleTekstowe*, const char* t, void* dane) {
            PoleNumeryczne* pn = static_cast<PoleNumeryczne*>(dane);
            if (pn && !pn->blokada_tekstu_) pn->obsluz_zmiane_tekstu(t);
        }, this);

        pole_edycji_->przy_zatwierdzeniu([](PoleTekstowe*, const char*, void* dane) {
            PoleNumeryczne* pn = static_cast<PoleNumeryczne*>(dane);
            if (pn) pn->zatwierdz();
        }, this);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        if (pole_edycji_) pole_edycji_->zaktualizuj_motyw();
        uniewaznij();
    }

    void ustaw_zakres(int64_t minimum, int64_t maksimum) {
        minimum_ = minimum;
        maksimum_ = maksimum;
        normalizuj_zakres();
        if (wartosc_ < minimum_) wartosc_ = minimum_;
        if (wartosc_ > maksimum_) wartosc_ = maksimum_;
        ostatnia_zatwierdzona_ = wartosc_;
        aktualizuj_tekst_edycji();
        uniewaznij();
    }

    int64_t minimum() const { return minimum_; }
    int64_t maksimum() const { return maksimum_; }

    void ustaw_wartosc(int64_t w) {
        if (w < minimum_) w = minimum_;
        if (w > maksimum_) w = maksimum_;
        if (wartosc_ != w) {
            wartosc_ = w;
            ostatnia_zatwierdzona_ = w;
            aktualizuj_tekst_edycji();
            uniewaznij();
            if (cb_zmiana_) cb_zmiana_(this, wartosc_, dane_zmiany_);
        }
    }

    int64_t wartosc() const { return wartosc_; }

    void ustaw_krok(int64_t k) { krok_ = (k < 1) ? 1 : k; }
    int64_t krok() const { return krok_; }

    void ustaw_duzy_krok(int64_t dk) { duzy_krok_ = (dk < 1) ? 1 : dk; }
    int64_t duzy_krok() const { return duzy_krok_; }

    void ustaw_tylko_do_odczytu(bool ro) {
        czy_tylko_do_odczytu_ = ro;
        if (pole_edycji_) pole_edycji_->ustaw_tylko_do_odczytu(ro);
        uniewaznij();
    }
    bool czy_tylko_do_odczytu() const { return czy_tylko_do_odczytu_; }

    void ustaw_prefiks(const char* p) {
        kopiuj_str(prefiks_, sizeof(prefiks_), p);
        aktualizuj_tekst_edycji();
    }
    const char* prefiks() const { return prefiks_; }

    void ustaw_sufiks(const char* s) {
        kopiuj_str(sufiks_, sizeof(sufiks_), s);
        aktualizuj_tekst_edycji();
    }
    const char* sufiks() const { return sufiks_; }

    void ustaw_pokazywanie_przyciskow(bool p) {
        if (pokazuj_przyciski_ != p) {
            pokazuj_przyciski_ = p;
            rozmiesc_elementy();
            uniewaznij();
        }
    }
    bool czy_pokazuje_przyciski() const { return pokazuj_przyciski_; }

    void ustaw_zawijanie_wartosci(bool z) { zawijaj_ = z; }
    bool czy_zawija_wartosc() const { return zawijaj_; }

    void zwieksz() { zmien_wartosc_o(krok_); }
    void zmniejsz() { zmien_wartosc_o(-krok_); }

    void przy_zmianie_wartosci(FunkcjaLiczby cb, void* dane = nullptr) {
        cb_zmiana_ = cb;
        dane_zmiany_ = dane;
    }

    void przy_zatwierdzeniu_wartosci(FunkcjaLiczby cb, void* dane = nullptr) {
        cb_zatwierdzenie_ = cb;
        dane_zatwierdzenia_ = dane;
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        rozmiesc_elementy();
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ || e.delta_y == 0) return;
        if (e.delta_y > 0) zwieksz();
        else zmniejsz();
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk() || !pokazuj_przyciski_ || czy_tylko_do_odczytu_) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        if (przycisk_gora_.zawiera(lx, ly)) {
            wcisniety_gora_ = true;
            zwieksz();
            uniewaznij();
            e.obsluzone = true;
        } else if (przycisk_dol_.zawiera(lx, ly)) {
            wcisniety_dol_ = true;
            zmniejsz();
            uniewaznij();
            e.obsluzone = true;
        }
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (wcisniety_gora_ || wcisniety_dol_) {
            wcisniety_gora_ = false;
            wcisniety_dol_ = false;
            uniewaznij();
            e.obsluzone = true;
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (!pokazuj_przyciski_) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        bool ng = przycisk_gora_.zawiera(lx, ly);
        bool nd = przycisk_dol_.zawiera(lx, ly);
        if (ng != najechany_gora_ || nd != najechany_dol_) {
            najechany_gora_ = ng;
            najechany_dol_ = nd;
            uniewaznij();
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_gora_ || najechany_dol_ || wcisniety_gora_ || wcisniety_dol_) {
            najechany_gora_ = false;
            najechany_dol_ = false;
            wcisniety_gora_ = false;
            wcisniety_dol_ = false;
            uniewaznij();
        }
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        uint32_t ramka = czy_wlaczona_ ? (czy_ma_fokus_ ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_RAMKA))
                                       : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);
        ctx.rysuj_ramke(b, ramka, 1);

        if (pokazuj_przyciski_) {
            uint32_t tlo_przyciskow = czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_PANEL) : motyw_kolor(MOTYW_KOLOR_PANEL);
            uint32_t strzalka_kolor = czy_wlaczona_ ? motyw_kolor(MOTYW_KOLOR_TEKST) : motyw_kolor(MOTYW_KOLOR_TEKST_WYLACZONY);

            // Przycisk w górę
            uint32_t bg_g = wcisniety_gora_ ? motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY) : (najechany_gora_ ? motyw_kolor(MOTYW_KOLOR_PODNIESIONY) : tlo_przyciskow);
            ctx.rysuj_prostokat(przycisk_gora_, bg_g);
            ctx.rysuj_ramke(przycisk_gora_, ramka, 1);
            rysuj_trojkat_gora(ctx, przycisk_gora_, strzalka_kolor);

            // Przycisk w dół
            uint32_t bg_d = wcisniety_dol_ ? motyw_kolor(MOTYW_KOLOR_AKCENT_AKTYWNY) : (najechany_dol_ ? motyw_kolor(MOTYW_KOLOR_PODNIESIONY) : tlo_przyciskow);
            ctx.rysuj_prostokat(przycisk_dol_, bg_d);
            ctx.rysuj_ramke(przycisk_dol_, ramka, 1);
            rysuj_trojkat_dol(ctx, przycisk_dol_, strzalka_kolor);
        }
    }

    void zatwierdz() {
        if (!pole_edycji_) return;
        int64_t w = wartosc_;
        if (parsuj_tekst(pole_edycji_->tekst(), w)) {
            if (w < minimum_) w = minimum_;
            if (w > maksimum_) w = maksimum_;
            bool zmieniono = (wartosc_ != w);
            wartosc_ = w;
            ostatnia_zatwierdzona_ = w;
            aktualizuj_tekst_edycji();
            if (zmieniono && cb_zmiana_) cb_zmiana_(this, wartosc_, dane_zmiany_);
            if (cb_zatwierdzenie_) cb_zatwierdzenie_(this, wartosc_, dane_zatwierdzenia_);
        } else {
            wartosc_ = ostatnia_zatwierdzona_;
            aktualizuj_tekst_edycji();
        }
    }

    void anuluj_edycje() {
        wartosc_ = ostatnia_zatwierdzona_;
        aktualizuj_tekst_edycji();
    }

private:
    class PoleWewnetrzne : public PoleTekstowe {
    public:
        explicit PoleWewnetrzne(PoleNumeryczne* rod) : rod_(rod) {}
        void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
            if (!rod_) { PoleTekstowe::obsluz_wcisniecie_klawisza(e); return; }
            uint32_t kod = e.kod_klawisza;
            bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
            bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
            uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

            if (kod == 27U || (usb && baza == 41U) || (!usb && baza == 0x01U)) {
                rod_->anuluj_edycje();
                e.obsluzone = true;
                return;
            }
            if ((!usb && e0 && baza == 0x48U) || (usb && baza == 82U)) { // Up
                rod_->zwieksz();
                e.obsluzone = true;
                return;
            }
            if ((!usb && e0 && baza == 0x50U) || (usb && baza == 81U)) { // Down
                rod_->zmniejsz();
                e.obsluzone = true;
                return;
            }
            if ((!usb && e0 && baza == 0x49U) || (usb && baza == 75U)) { // PageUp
                rod_->zmien_wartosc_o(rod_->duzy_krok());
                e.obsluzone = true;
                return;
            }
            if ((!usb && e0 && baza == 0x51U) || (usb && baza == 78U)) { // PageDown
                rod_->zmien_wartosc_o(-rod_->duzy_krok());
                e.obsluzone = true;
                return;
            }
            PoleTekstowe::obsluz_wcisniecie_klawisza(e);
        }
        void przy_utracie_fokusu() override {
            PoleTekstowe::przy_utracie_fokusu();
            if (rod_) rod_->zatwierdz();
        }
    private:
        PoleNumeryczne* rod_{nullptr};
    };

    void normalizuj_zakres() {
        if (maksimum_ < minimum_) {
            int64_t tmp = minimum_;
            minimum_ = maksimum_;
            maksimum_ = tmp;
        }
    }

    void rozmiesc_elementy() {
        int w = obszar_ekranu_.szerokosc;
        int h = obszar_ekranu_.wysokosc;
        int spinner_w = pokazuj_przyciski_ ? 18 : 0;
        int edit_w = (w > spinner_w + 2) ? w - spinner_w - 2 : w;
        if (pole_edycji_) {
            pole_edycji_->rozmiesc(Prostokat(1, 1, edit_w, h > 2 ? h - 2 : h));
        }
        if (pokazuj_przyciski_) {
            int btn_x = w - spinner_w - 1;
            int btn_h1 = (h - 2) / 2;
            int btn_h2 = (h - 2) - btn_h1;
            przycisk_gora_ = Prostokat(btn_x, 1, spinner_w, btn_h1);
            przycisk_dol_ = Prostokat(btn_x, 1 + btn_h1, spinner_w, btn_h2);
        } else {
            przycisk_gora_ = Prostokat();
            przycisk_dol_ = Prostokat();
        }
    }

    void zmien_wartosc_o(int64_t delta) {
        if (czy_tylko_do_odczytu_ || !czy_wlaczona_) return;
        int64_t nowa = wartosc_ + delta;
        if (delta > 0 && nowa < wartosc_) nowa = maksimum_; // overflow guard
        if (delta < 0 && nowa > wartosc_) nowa = minimum_; // underflow guard

        if (zawijaj_) {
            if (nowa > maksimum_) nowa = minimum_;
            if (nowa < minimum_) nowa = maksimum_;
        } else {
            if (nowa > maksimum_) nowa = maksimum_;
            if (nowa < minimum_) nowa = minimum_;
        }
        if (nowa != wartosc_) {
            wartosc_ = nowa;
            ostatnia_zatwierdzona_ = nowa;
            aktualizuj_tekst_edycji();
            uniewaznij();
            if (cb_zmiana_) cb_zmiana_(this, wartosc_, dane_zmiany_);
            if (cb_zatwierdzenie_) cb_zatwierdzenie_(this, wartosc_, dane_zatwierdzenia_);
        }
    }

    void obsluz_zmiane_tekstu(const char* t) {
        int64_t w = 0;
        if (parsuj_tekst(t, w)) {
            if (w < minimum_) w = minimum_;
            if (w > maksimum_) w = maksimum_;
            if (wartosc_ != w) {
                wartosc_ = w;
                if (cb_zmiana_) cb_zmiana_(this, wartosc_, dane_zmiany_);
            }
        }
    }

    void aktualizuj_tekst_edycji() {
        if (!pole_edycji_) return;
        char buf[128];
        formatuj_wartosc(buf, sizeof(buf), wartosc_);
        blokada_tekstu_ = true;
        pole_edycji_->ustaw_tekst(buf);
        blokada_tekstu_ = false;
    }

    void formatuj_wartosc(char* buf, size_t max_len, int64_t v) {
        size_t p = 0;
        // Prefiks
        for (size_t i = 0; prefiks_[i] != '\0' && p + 1 < max_len; ++i) buf[p++] = prefiks_[i];

        // Liczba int64 z podziałem sprzętowym uint64_t (brak __divmodti4)
        char num[32];
        size_t np = 0;
        bool ujemna = false;
        uint64_t uv = 0;
        if (v < 0) {
            ujemna = true;
            if (v == (-9223372036854775807LL - 1LL)) {
                uv = 0x8000000000000000ULL;
            } else {
                uv = static_cast<uint64_t>(-v);
            }
        } else {
            uv = static_cast<uint64_t>(v);
        }

        if (uv == 0) {
            num[np++] = '0';
        } else {
            while (uv > 0 && np < sizeof(num)) {
                num[np++] = static_cast<char>('0' + (uv % 10ULL));
                uv /= 10ULL;
            }
        }
        if (ujemna && p + 1 < max_len) buf[p++] = '-';
        while (np > 0 && p + 1 < max_len) {
            buf[p++] = num[--np];
        }

        // Sufiks
        for (size_t i = 0; sufiks_[i] != '\0' && p + 1 < max_len; ++i) buf[p++] = sufiks_[i];
        buf[p] = '\0';
    }

    bool parsuj_tekst(const char* txt, int64_t& out) {
        if (!txt) return false;
        size_t i = 0;
        while (txt[i] == ' ' || txt[i] == '\t') ++i;

        // Sprawdź i pomiń prefiks
        size_t pi = 0;
        while (prefiks_[pi] != '\0' && txt[i] == prefiks_[pi]) { ++i; ++pi; }

        while (txt[i] == ' ' || txt[i] == '\t') ++i;

        bool ujemna = false;
        if (txt[i] == '-') { ujemna = true; ++i; }
        else if (txt[i] == '+') { ++i; }

        if (txt[i] < '0' || txt[i] > '9') return false;

        uint64_t val = 0;
        bool overflow = false;
        while (txt[i] >= '0' && txt[i] <= '9') {
            uint64_t d = static_cast<uint64_t>(txt[i] - '0');
            if (val > (0xFFFFFFFFFFFFFFFFULL - d) / 10ULL) {
                overflow = true;
            } else {
                val = val * 10ULL + d;
            }
            ++i;
        }

        // Pomiń białe znaki i ewentualny sufiks
        while (txt[i] == ' ' || txt[i] == '\t') ++i;
        size_t si = 0;
        while (sufiks_[si] != '\0' && txt[i] == sufiks_[si]) { ++i; ++si; }
        while (txt[i] == ' ' || txt[i] == '\t') ++i;

        // Jeśli zostały nieobsłużone znaki (np. "12abc" bez takiego sufiksu), odrzuć
        if (txt[i] != '\0') return false;

        if (ujemna) {
            if (overflow || val >= 0x8000000000000000ULL) {
                out = (-9223372036854775807LL - 1LL);
            } else {
                out = -static_cast<int64_t>(val);
            }
        } else {
            if (overflow || val >= 0x7FFFFFFFFFFFFFFFULL) {
                out = 0x7FFFFFFFFFFFFFFFLL;
            } else {
                out = static_cast<int64_t>(val);
            }
        }
        return true;
    }

    static void kopiuj_str(char* cel, size_t max_cel, const char* zr) {
        if (!cel || max_cel == 0) return;
        if (!zr) { cel[0] = '\0'; return; }
        size_t i = 0;
        while (zr[i] != '\0' && i + 1 < max_cel) {
            cel[i] = zr[i];
            ++i;
        }
        cel[i] = '\0';
    }

    void rysuj_trojkat_gora(KontekstRysowania& ctx, Prostokat r, uint32_t kolor) {
        int cx = r.x + r.szerokosc / 2;
        int cy = r.y + r.wysokosc / 2 - 1;
        ctx.rysuj_prostokat(Prostokat(cx, cy - 2, 1, 1), kolor);
        ctx.rysuj_linie_pozioma(cx - 1, cy - 1, 3, kolor);
        ctx.rysuj_linie_pozioma(cx - 2, cy, 5, kolor);
        ctx.rysuj_linie_pozioma(cx - 3, cy + 1, 7, kolor);
    }

    void rysuj_trojkat_dol(KontekstRysowania& ctx, Prostokat r, uint32_t kolor) {
        int cx = r.x + r.szerokosc / 2;
        int cy = r.y + r.wysokosc / 2 - 1;
        ctx.rysuj_linie_pozioma(cx - 3, cy - 1, 7, kolor);
        ctx.rysuj_linie_pozioma(cx - 2, cy, 5, kolor);
        ctx.rysuj_linie_pozioma(cx - 1, cy + 1, 3, kolor);
        ctx.rysuj_prostokat(Prostokat(cx, cy + 2, 1, 1), kolor);
    }

    int64_t minimum_{0};
    int64_t maksimum_{100};
    int64_t wartosc_{0};
    int64_t ostatnia_zatwierdzona_{0};
    int64_t krok_{1};
    int64_t duzy_krok_{10};
    bool czy_tylko_do_odczytu_{false};
    bool pokazuj_przyciski_{true};
    bool zawijaj_{false};
    bool blokada_tekstu_{false};
    bool najechany_gora_{false};
    bool najechany_dol_{false};
    bool wcisniety_gora_{false};
    bool wcisniety_dol_{false};

    char prefiks_[16]{};
    char sufiks_[16]{};

    Prostokat przycisk_gora_;
    Prostokat przycisk_dol_;
    PoleWewnetrzne* pole_edycji_{nullptr};

    FunkcjaLiczby cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
    FunkcjaLiczby cb_zatwierdzenie_{nullptr};
    void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
