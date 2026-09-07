#pragma once

#include "pole_wyszukiwania.h"
#include "popup.h"

namespace BursztynUI {

class PoleSugestii : public PoleWyszukiwania {
public:
    static constexpr size_t MAKS_SUGESTII = 64;
    static constexpr size_t MAKS_TEKST_SUGESTII = 96;
    using FunkcjaWyboru = void (*)(PoleSugestii*, int, const char*, void*);
    using FunkcjaZmiany = void (*)(PoleSugestii*, const char*, void*);

private:
    class PopupSugestii : public PowierzchniaPopup {
    public:
        explicit PopupSugestii(PoleSugestii* w) : wlasciciel_(w) {
            czy_reaguje_wizualnie_na_najechanie_ = false;
            przy_zamknieciu(zamknieto, this);
        }
        void rysuj_siebie(KontekstRysowania& ctx) override {
            PowierzchniaPopup::rysuj_siebie(ctx);
            if (!wlasciciel_) return;
            wlasciciel_->ostatnio_narysowane_ = 0;
            SkalaUI s = skala_dpi_;
            int rh = wlasciciel_->wysokosc_wiersza_ * s;
            int ile = static_cast<int>(wlasciciel_->liczba_dopasowanych_);
            if (ile > wlasciciel_->maks_widocznych_) ile = wlasciciel_->maks_widocznych_;
            for (int i = 0; i < ile; ++i) {
                ++wlasciciel_->ostatnio_narysowane_;
                int indeks_wyniku = wlasciciel_->pierwsza_widoczna_ + i;
                if (indeks_wyniku < 0 || indeks_wyniku >= static_cast<int>(wlasciciel_->liczba_dopasowanych_)) break;
                int indeks = wlasciciel_->dopasowane_[indeks_wyniku];
                Prostokat r(s, s + i * rh, obszar_.szerokosc - 2 * s, rh);
                if (indeks_wyniku == wlasciciel_->podswietlona_)
                    ctx.rysuj_prostokat(r, Kolory::ZaznaczenieTlo);
                ctx.rysuj_tekst_obciety(Punkt(r.x + 6 * s,
                    r.y + (rh - CzcionkaUI::pobierz_wysokosc(s)) / 2),
                    wlasciciel_->sugestie_[indeks],
                    indeks_wyniku == wlasciciel_->podswietlona_
                        ? Kolory::ZaznaczenieTekst : Kolory::TekstBialy,
                    r.szerokosc - 12 * s, s);
            }
        }
        void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
            if (!wlasciciel_ || !e.czy_lewy_przycisk()) return;
            SkalaUI s = skala_dpi_;
            int rh = wlasciciel_->wysokosc_wiersza_ * s;
            int lokalny_y = e.y - obszar_ekranu_.y - s;
            int wiersz = lokalny_y >= 0 ? lokalny_y / rh : -1;
            int wynik = wlasciciel_->pierwsza_widoczna_ + wiersz;
            if (wiersz >= 0 && wynik >= 0 && wynik < static_cast<int>(wlasciciel_->liczba_dopasowanych_)) {
                wlasciciel_->podswietlona_ = wynik;
                wlasciciel_->zaakceptuj_podswietlona();
                e.obsluzone = true;
            }
        }
        void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
            if (!wlasciciel_) return;
            SkalaUI s = skala_dpi_;
            int rh = wlasciciel_->wysokosc_wiersza_ * s;
            int wiersz = (e.y - obszar_ekranu_.y - s) / rh;
            int wynik = wlasciciel_->pierwsza_widoczna_ + wiersz;
            if (wiersz < 0 || wynik < 0 || wynik >= static_cast<int>(wlasciciel_->liczba_dopasowanych_)) wynik = -1;
            if (wynik != wlasciciel_->podswietlona_) {
                int stary = wlasciciel_->podswietlona_;
                wlasciciel_->podswietlona_ = wynik;
                wlasciciel_->uniewaznij_wiersz_popupu(stary);
                wlasciciel_->uniewaznij_wiersz_popupu(wynik);
            }
        }
        void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
            if (wlasciciel_) wlasciciel_->obsluz_klawisz_popupu(e);
        }
        void obsluz_wprowadzenie_tekstu(ZdarzenieWprowadzaniaTekstu& e) override {
            if (wlasciciel_) wlasciciel_->pole_tekstowe().obsluz_wprowadzenie_tekstu(e);
        }
    private:
        static void zamknieto(PowierzchniaPopup*, PowodZamknieciaPopup, void* dane) {
            PopupSugestii* p = static_cast<PopupSugestii*>(dane);
            if (p && p->wlasciciel_) p->wlasciciel_->popup_zamkniety();
        }
        PoleSugestii* wlasciciel_{nullptr};
    };

public:
    explicit PoleSugestii(const char* placeholder = "Wpisz, aby wybrać…")
        : PoleWyszukiwania(placeholder), popup_(this) {
        PoleWyszukiwania::przy_zmianie(zmieniono_wewnetrzne, this);
    }
    ~PoleSugestii() override {
        if (popup_.zarzadca()) popup_.zarzadca()->zamknij(PowodZamknieciaPopup::WlascicielZamkniety);
    }

    bool ustaw_sugestie(const char* const* teksty, size_t liczba) {
        bool komplet = liczba <= MAKS_SUGESTII;
        if (liczba > MAKS_SUGESTII) liczba = MAKS_SUGESTII;
        liczba_sugestii_ = 0;
        for (size_t i = 0; i < liczba; ++i) {
            kopiuj(sugestie_[liczba_sugestii_], MAKS_TEKST_SUGESTII,
                   teksty && teksty[i] ? teksty[i] : "");
            ++liczba_sugestii_;
        }
        przefiltruj();
        return komplet;
    }
    bool ustawsugestie(const char* const* t, size_t n) { return ustaw_sugestie(t, n); }
    bool dodaj_sugestie(const char* tekst) {
        if (liczba_sugestii_ >= MAKS_SUGESTII) return false;
        kopiuj(sugestie_[liczba_sugestii_++], MAKS_TEKST_SUGESTII, tekst ? tekst : "");
        przefiltruj(); return true;
    }
    void wyczysc_sugestie() {
        liczba_sugestii_ = liczba_dopasowanych_ = 0;
        zamknij_popup();
    }
    void wyczyscsugestie() { wyczysc_sugestie(); }
    void ustaw_maksymalna_liczbe_widocznych(int n) {
        if (n < 1) n = 1;
        if (n > 16) n = 16;
        if (maks_widocznych_ == n) return;
        maks_widocznych_ = n;
        if (popup_.czy_otwarta()) { zamknij_popup(); otworz_popup(); }
    }
    void ustawmaksymalnaliczbewidocznych(int n) { ustaw_maksymalna_liczbe_widocznych(n); }

    size_t liczba_sugestii() const { return liczba_sugestii_; }
    size_t liczba_dopasowanych() const { return liczba_dopasowanych_; }
    size_t liczba_ostatnio_narysowanych() const { return ostatnio_narysowane_; }
    int podswietlony_indeks() const { return podswietlona_; }
    bool czy_popup_otwarty() const { return popup_.czy_otwarta(); }

    void przy_wybraniu(FunkcjaWyboru cb, void* dane = nullptr) {
        cb_wybor_ = cb; dane_wyboru_ = dane;
    }
    void przywybraniu(FunkcjaWyboru cb, void* dane = nullptr) { przy_wybraniu(cb, dane); }
    void przy_zmianie(FunkcjaZmiany cb, void* dane = nullptr) {
        cb_zmiana_ = cb; dane_zmiany_ = dane;
    }
    void przyzmianie(FunkcjaZmiany cb, void* dane = nullptr) { przy_zmianie(cb, dane); }

protected:
    bool obsluz_klawisz_specjalny(ZdarzenieKlawiatury& e) override {
        if (obsluz_klawisz_popupu(e)) return true;
        return PoleWyszukiwania::obsluz_klawisz_specjalny(e);
    }

private:
    static void kopiuj(char* dst, size_t cap, const char* src) {
        if (!dst || cap == 0) return;
        size_t i = 0;
        if (src) while (src[i] && i + 1 < cap) { dst[i] = src[i]; ++i; }
        dst[i] = '\0';
    }
    static bool zawiera(const char* tekst, const char* wzorzec) {
        if (!tekst || !wzorzec || !wzorzec[0]) return true;
        for (size_t i = 0; tekst[i]; ++i) {
            size_t j = 0;
            while (wzorzec[j] && tekst[i + j] == wzorzec[j]) ++j;
            if (!wzorzec[j]) return true;
        }
        return false;
    }
    static void zmieniono_wewnetrzne(PoleWyszukiwania*, const char* tekst, void* dane) {
        PoleSugestii* p = static_cast<PoleSugestii*>(dane);
        if (!p) return;
        p->przefiltruj();
        if (p->cb_zmiana_) p->cb_zmiana_(p, tekst, p->dane_zmiany_);
    }
    void przefiltruj() {
        liczba_dopasowanych_ = 0;
        for (size_t i = 0; i < liczba_sugestii_; ++i)
            if (zawiera(sugestie_[i], pobierz_tekst()))
                dopasowane_[liczba_dopasowanych_++] = static_cast<int>(i);
        podswietlona_ = liczba_dopasowanych_ ? 0 : -1;
        pierwsza_widoczna_ = 0;
        if (!akceptowanie_ && liczba_dopasowanych_ && pobierz_tekst()[0]) otworz_popup();
        else zamknij_popup();
        if (popup_.czy_otwarta()) popup_.uniewaznij();
    }
    void otworz_popup() {
        if (popup_.czy_otwarta() || !zarzadca_popupow_ || liczba_dopasowanych_ == 0) return;
        SkalaUI s = skala_dpi_;
        int ile = static_cast<int>(liczba_dopasowanych_);
        if (ile > maks_widocznych_) ile = maks_widocznych_;
        Rozmiar rozmiar(obszar_.szerokosc, ile * wysokosc_wiersza_ * s + 2 * s);
        Prostokat gdzie = zarzadca_popupow_->umiesc_pod_kotwica(obszar_ekranu_, rozmiar);
        (void)zarzadca_popupow_->otworz(popup_, gdzie, &pole_tekstowe(), &popup_);
    }
    void zamknij_popup() {
        if (popup_.zarzadca()) popup_.zarzadca()->zamknij(PowodZamknieciaPopup::Anulowanie);
    }
    void popup_zamkniety() { podswietlona_ = -1; pierwsza_widoczna_ = 0; }
    bool obsluz_klawisz_popupu(ZdarzenieKlawiatury& e) {
        uint32_t k = e.kod_klawisza;
        bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (k & BWS_KEY_SOURCE_E0) != 0;
        uint32_t b = usb ? (k & 0xFFU) : (k & 0x7FU);
        int delta = 0;
        if ((usb && b == 82U) || (e0 && b == 0x48U)) delta = -1;
        if ((usb && b == 81U) || (e0 && b == 0x50U)) delta = 1;
        if (delta && liczba_dopasowanych_) {
            if (!popup_.czy_otwarta()) otworz_popup();
            int n = podswietlona_ < 0 ? 0 : podswietlona_ + delta;
            if (n < 0) n = static_cast<int>(liczba_dopasowanych_) - 1;
            if (n >= static_cast<int>(liczba_dopasowanych_)) n = 0;
            int stary = podswietlona_; podswietlona_ = n;
            if (n < pierwsza_widoczna_) pierwsza_widoczna_ = n;
            if (n >= pierwsza_widoczna_ + maks_widocznych_) pierwsza_widoczna_ = n - maks_widocznych_ + 1;
            uniewaznij_wiersz_popupu(stary); uniewaznij_wiersz_popupu(n);
            e.obsluzone = true; return true;
        }
        if (k == 13U || k == 10U || (usb && b == 40U)) {
            if (popup_.czy_otwarta() && podswietlona_ >= 0) zaakceptuj_podswietlona();
            e.obsluzone = true; return true;
        }
        if (k == 27U || (usb && b == 41U) || (!usb && b == 1U)) {
            if (popup_.czy_otwarta()) zamknij_popup();
            else wyczysc();
            e.obsluzone = true; return true;
        }
        // Pozostale klawisze edycyjne musza dotrzec do wspolnego silnika tekstu.
        if (popup_.czy_otwarta()) {
            pole_tekstowe().PoleTekstowe::obsluz_wcisniecie_klawisza(e);
            return e.obsluzone;
        }
        return false;
    }
    void zaakceptuj_podswietlona() {
        if (podswietlona_ < 0 || podswietlona_ >= static_cast<int>(liczba_dopasowanych_)) return;
        int indeks = dopasowane_[podswietlona_];
        char kopia[MAKS_TEKST_SUGESTII]; kopiuj(kopia, sizeof(kopia), sugestie_[indeks]);
        zamknij_popup();
        akceptowanie_ = true;
        ustaw_tekst(kopia);
        akceptowanie_ = false;
        if (cb_wybor_) cb_wybor_(this, indeks, sugestie_[indeks], dane_wyboru_);
    }
    void uniewaznij_wiersz_popupu(int wynik) {
        if (!popup_.czy_otwarta() || wynik < pierwsza_widoczna_ ||
            wynik >= pierwsza_widoczna_ + maks_widocznych_) return;
        SkalaUI s = skala_dpi_;
        popup_.uniewaznij_obszar(Prostokat(s, s + (wynik - pierwsza_widoczna_) * wysokosc_wiersza_ * s,
                                           popup_.szerokosc() - 2 * s, wysokosc_wiersza_ * s));
    }

    char sugestie_[MAKS_SUGESTII][MAKS_TEKST_SUGESTII]{};
    size_t liczba_sugestii_{0};
    int dopasowane_[MAKS_SUGESTII]{};
    size_t liczba_dopasowanych_{0};
    int podswietlona_{-1};
    int pierwsza_widoczna_{0};
    int maks_widocznych_{6};
    int wysokosc_wiersza_{24};
    size_t ostatnio_narysowane_{0};
    bool akceptowanie_{false};
    PopupSugestii popup_;
    FunkcjaWyboru cb_wybor_{nullptr};
    void* dane_wyboru_{nullptr};
    FunkcjaZmiany cb_zmiana_{nullptr};
    void* dane_zmiany_{nullptr};
};

} // namespace BursztynUI
