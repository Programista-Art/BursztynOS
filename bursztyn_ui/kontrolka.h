#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "zdarzenia.h"
#include "motyw.h"
#include "rysowanie.h"

namespace BursztynUI {

class ZarzadcaPopupow;

class Kontrolka {
public:
    static constexpr size_t MAKS_DZIECI = 64;
    static constexpr size_t MAKS_DLUGOSC_TEKSTU = 256;

    // Typy funkcji zwrotnych dla ujednoliconego modelu zdarzeń
    using FunkcjaZdarzenia = void (*)(Kontrolka* nadawca, void* dane_uzytkownika);
    using FunkcjaMyszy = void (*)(Kontrolka* nadawca, const ZdarzenieMyszki& e, void* dane_uzytkownika);
    using FunkcjaRolki = void (*)(Kontrolka* nadawca, const ZdarzenieRolki& e, void* dane_uzytkownika);
    using FunkcjaKlawiatury = void (*)(Kontrolka* nadawca, const ZdarzenieKlawiatury& e, void* dane_uzytkownika);
    using FunkcjaTekstu = void (*)(Kontrolka* nadawca, const char* tekst, void* dane_uzytkownika);
    using FunkcjaZmianyWartosci = void (*)(Kontrolka* nadawca, void* dane_uzytkownika);
    using FunkcjaRysowania = void (*)(Kontrolka* nadawca, KontekstRysowania& ctx, void* dane_uzytkownika);

    Kontrolka() {
        preferowany_rozmiar_ = Rozmiar(-1, -1);
    }
    virtual ~Kontrolka() {
        wyczysc_kontrolki();
    }

    // =========================================================================
    // 1. Tożsamość i dane użytkownika
    // =========================================================================

    int id() const { return id_; }
    void ustaw_id(int id) { id_ = id; }

    const char* nazwa() const { return nazwa_; }
    void ustaw_nazwe(const char* n) {
        if (!n) n = "";
        size_t i = 0;
        for (; i < sizeof(nazwa_) - 1 && n[i] != '\0'; ++i) {
            nazwa_[i] = n[i];
        }
        nazwa_[i] = '\0';
    }

    const char* podpowiedz() const { return podpowiedz_; }
    void ustaw_podpowiedz(const char* p) {
        if (!p) p = "";
        size_t i = 0;
        for (; i < sizeof(podpowiedz_) - 1 && p[i] != '\0'; ++i) {
            podpowiedz_[i] = p[i];
        }
        podpowiedz_[i] = '\0';
    }

    void* dane_uzytkownika() const { return dane_uzytkownika_; }
    void ustaw_dane_uzytkownika(void* dane) { dane_uzytkownika_ = dane; }

    uint64_t znacznik() const { return znacznik_; }
    void ustaw_znacznik(uint64_t z) { znacznik_ = z; }

    virtual const char* tekst() const { return tekst_; }
    virtual void ustaw_tekst(const char* nowy_tekst) {
        if (!nowy_tekst) nowy_tekst = "";
        size_t i = 0;
        for (; i < MAKS_DLUGOSC_TEKSTU - 1 && nowy_tekst[i] != '\0'; ++i) {
            tekst_[i] = nowy_tekst[i];
        }
        tekst_[i] = '\0';
        uniewaznij();
    }

    // =========================================================================
    // 2. Drzewo kontrolek i kolejność Z-order
    // =========================================================================

    Kontrolka* rodzic() const { return rodzic_; }
    ZarzadcaPopupow* zarzadca_popupow() const { return zarzadca_popupow_; }
    virtual void ustaw_zarzadce_popupow(ZarzadcaPopupow* zarzadca) {
        zarzadca_popupow_ = zarzadca;
        for (size_t i = 0; i < liczba_dzieci_; ++i)
            if (dzieci_[i]) dzieci_[i]->ustaw_zarzadce_popupow(zarzadca);
    }
    size_t liczba_dzieci() const { return liczba_dzieci_; }
    Kontrolka* dziecko(size_t indeks) const {
        return (indeks < liczba_dzieci_) ? dzieci_[indeks] : nullptr;
    }

    int indeks_w_rodzicu() const {
        if (!rodzic_) return -1;
        for (size_t i = 0; i < rodzic_->liczba_dzieci_; ++i) {
            if (rodzic_->dzieci_[i] == this) return static_cast<int>(i);
        }
        return -1;
    }

    bool dodaj_kontrolke(Kontrolka* kontrolka) {
        if (!kontrolka || kontrolka == this || liczba_dzieci_ >= MAKS_DZIECI) return false;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] == kontrolka) return true;
        }
        kontrolka->rodzic_ = this;
        kontrolka->ustaw_skale_dpi(skala_dpi_);
        kontrolka->ustaw_zarzadce_popupow(zarzadca_popupow_);
        dzieci_[liczba_dzieci_++] = kontrolka;
        uniewaznij();
        return true;
    }

    template <typename T, typename... Args>
    T& dodaj(Args&&... args) {
        T* k = new T(args...);
        dodaj_kontrolke(k);
        return *k;
    }

    bool usun_kontrolke(Kontrolka* kontrolka) {
        if (!kontrolka) return false;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] == kontrolka) {
                kontrolka->rodzic_ = nullptr;
                for (size_t j = i; j < liczba_dzieci_ - 1; ++j) {
                    dzieci_[j] = dzieci_[j + 1];
                }
                dzieci_[--liczba_dzieci_] = nullptr;
                uniewaznij();
                return true;
            }
        }
        return false;
    }

    void wyczysc_kontrolki() {
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i]) {
                dzieci_[i]->rodzic_ = nullptr;
                delete dzieci_[i];
                dzieci_[i] = nullptr;
            }
        }
        liczba_dzieci_ = 0;
        uniewaznij();
    }

    void przenies_na_wierzch() {
        if (!rodzic_ || rodzic_->liczba_dzieci_ <= 1) return;
        int idx = indeks_w_rodzicu();
        if (idx < 0 || static_cast<size_t>(idx) == rodzic_->liczba_dzieci_ - 1) return;
        for (size_t i = idx; i < rodzic_->liczba_dzieci_ - 1; ++i) {
            rodzic_->dzieci_[i] = rodzic_->dzieci_[i + 1];
        }
        rodzic_->dzieci_[rodzic_->liczba_dzieci_ - 1] = this;
        rodzic_->uniewaznij();
    }

    void przenies_na_spod() {
        if (!rodzic_ || rodzic_->liczba_dzieci_ <= 1) return;
        int idx = indeks_w_rodzicu();
        if (idx <= 0) return;
        for (size_t i = idx; i > 0; --i) {
            rodzic_->dzieci_[i] = rodzic_->dzieci_[i - 1];
        }
        rodzic_->dzieci_[0] = this;
        rodzic_->uniewaznij();
    }

    // =========================================================================
    // 3. Geometria, układ i dopasowanie
    // =========================================================================

    Prostokat obszar() const { return obszar_; }
    void ustaw_obszar(Prostokat r) {
        if (obszar_ != r) {
            obszar_ = r;
            uniewaznij();
        }
    }

    Prostokat obszar_ekranu() const { return obszar_ekranu_; }

    int x() const { return obszar_.x; }
    int y() const { return obszar_.y; }
    int szerokosc() const { return obszar_.szerokosc; }
    int wysokosc() const { return obszar_.wysokosc; }

    void ustaw_pozycje(int x, int y) {
        if (obszar_.x != x || obszar_.y != y) {
            obszar_.x = x;
            obszar_.y = y;
            uniewaznij();
        }
    }

    void ustaw_rozmiar(int w, int h) {
        int sz = (w < 0) ? 0 : w;
        int wys = (h < 0) ? 0 : h;
        if (obszar_.szerokosc != sz || obszar_.wysokosc != wys) {
            obszar_.szerokosc = sz;
            obszar_.wysokosc = wys;
            uniewaznij();
        }
    }

    void ustaw_geometrie(int x, int y, int w, int h) {
        ustaw_pozycje(x, y);
        ustaw_rozmiar(w, h);
    }

    Rozmiar preferowany_rozmiar() const { return preferowany_rozmiar_; }
    void ustaw_preferowany_rozmiar(Rozmiar r) {
        preferowany_rozmiar_ = r;
        uniewaznij();
    }

    Rozmiar minimalny_rozmiar() const { return minimalny_rozmiar_; }
    void ustaw_minimalny_rozmiar(Rozmiar r) {
        minimalny_rozmiar_ = r;
        uniewaznij();
    }

    Rozmiar maksymalny_rozmiar() const { return maksymalny_rozmiar_; }
    void ustaw_maksymalny_rozmiar(Rozmiar r) {
        maksymalny_rozmiar_ = r;
        uniewaznij();
    }

    Marginesy margines() const { return margines_; }
    void ustaw_margines(Marginesy m) {
        margines_ = m;
        uniewaznij();
    }

    Marginesy wypelnienie() const { return wypelnienie_; }
    void ustaw_wypelnienie(Marginesy w) {
        wypelnienie_ = w;
        uniewaznij();
    }

    WyrownaniePoziome wyrownanie_poziome() const { return wyrownanie_poziome_; }
    void ustaw_wyrownanie_poziome(WyrownaniePoziome w) {
        wyrownanie_poziome_ = w;
        uniewaznij();
    }

    WyrownaniePionowe wyrownanie_pionowe() const { return wyrownanie_pionowe_; }
    void ustaw_wyrownanie_pionowe(WyrownaniePionowe w) {
        wyrownanie_pionowe_ = w;
        uniewaznij();
    }

    int skala_dpi() const { return skala_dpi_; }
    virtual void ustaw_skale_dpi(int s) {
        skala_dpi_ = (s < 1) ? 1 : s;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i]) dzieci_[i]->ustaw_skale_dpi(skala_dpi_);
        }
        uniewaznij();
    }

    // =========================================================================
    // 4. Stany, widoczność, skupienie i kolejność Tab
    // =========================================================================

    bool czy_widoczna() const { return czy_widoczna_; }
    bool czy_efektywnie_widoczna() const {
        const Kontrolka* k = this;
        while (k) {
            if (!k->czy_widoczna_) return false;
            k = k->rodzic_;
        }
        return true;
    }
    void ustaw_widoczna(bool v) {
        if (czy_widoczna_ != v) {
            czy_widoczna_ = v;
            uniewaznij();
        }
    }
    void pokaz() { ustaw_widoczna(true); }
    void ukryj() { ustaw_widoczna(false); }

    bool czy_wlaczona() const { return czy_wlaczona_; }
    void ustaw_wlaczona(bool e) {
        if (czy_wlaczona_ != e) {
            czy_wlaczona_ = e;
            uniewaznij();
        }
    }

    bool czy_tylko_do_odczytu() const { return czy_tylko_do_odczytu_; }
    void ustaw_tylko_do_odczytu(bool ro) {
        if (czy_tylko_do_odczytu_ != ro) {
            czy_tylko_do_odczytu_ = ro;
            uniewaznij();
        }
    }

    bool czy_najechana() const { return czy_najechana_; }
    virtual bool czy_reaguje_wizualnie_na_najechanie() const {
        return czy_reaguje_wizualnie_na_najechanie_;
    }
    void ustaw_reagowanie_wizualne_na_najechanie(bool reaguje) {
        if (czy_reaguje_wizualnie_na_najechanie_ == reaguje) return;
        bool trzeba_odrysowac = czy_najechana_ && czy_wlaczona_;
        czy_reaguje_wizualnie_na_najechanie_ = reaguje;
        if (trzeba_odrysowac) uniewaznij();
    }
    bool czy_wcisnieta() const { return czy_wcisnieta_; }
    virtual bool czy_reaguje_wizualnie_na_wcisniecie() const {
        return czy_reaguje_wizualnie_na_wcisniecie_;
    }
    void ustaw_reagowanie_wizualne_na_wcisniecie(bool reaguje) {
        if (czy_reaguje_wizualnie_na_wcisniecie_ == reaguje) return;
        bool trzeba_odrysowac = czy_wcisnieta_ && czy_wlaczona_;
        czy_reaguje_wizualnie_na_wcisniecie_ = reaguje;
        if (trzeba_odrysowac) uniewaznij();
    }
    bool czy_ma_fokus() const { return czy_ma_fokus_; }
    bool czy_ma_skupienie() const { return czy_ma_fokus_; }

    bool czy_moze_miec_fokus() const { return czy_moze_miec_fokus_ && czy_widoczna_ && czy_wlaczona_; }
    void ustaw_mozliwosc_fokusu(bool f) { czy_moze_miec_fokus_ = f; }

    int kolejnosc_tab() const { return kolejnosc_tab_; }
    void ustaw_kolejnosc_tab(int order) { kolejnosc_tab_ = order; }

    bool czy_widoczna_w_drzewie() const {
        if (!czy_widoczna_) return false;
        if (rodzic_) return rodzic_->czy_widoczna_w_drzewie();
        return true;
    }

    bool czy_wlaczona_w_drzewie() const {
        if (!czy_wlaczona_) return false;
        if (rodzic_) return rodzic_->czy_wlaczona_w_drzewie();
        return true;
    }

    // =========================================================================
    // 5. Stylizacja, nadpisywanie kolorów i przezroczystość
    // =========================================================================

    StylKontrolki& styl() { return styl_; }
    const StylKontrolki& styl() const { return styl_; }
    void ustaw_styl(const StylKontrolki& s) {
        styl_ = s;
        uniewaznij();
    }

    StylStanu& styl_stanu(StanWizualny stan) {
        return wybierz_styl_stanu(stan);
    }
    const StylStanu& styl_stanu(StanWizualny stan) const {
        return wybierz_styl_stanu(stan);
    }
    void ustaw_styl_stanu(StanWizualny stan, const StylStanu& s) {
        wybierz_styl_stanu(stan) = s;
        uniewaznij();
    }

    // Proste settery ustawiają wyłącznie bazowy (normalny) stan kontrolki.
    // Pozostałe stany mają jawne API i nie są wyprowadzane z nazwy koloru.
    uint32_t kolor_tla() const { return styl_.normalny.tlo; }
    void ustaw_kolor_tla(uint32_t c) {
        styl_.normalny.tlo = c;
        uniewaznij();
    }

    uint32_t kolor_tekstu() const { return styl_.normalny.tekst; }
    void ustaw_kolor_tekstu(uint32_t c) {
        styl_.normalny.tekst = c;
        uniewaznij();
    }

    uint32_t kolor_ramki() const { return styl_.normalny.ramka; }
    void ustaw_kolor_ramki(uint32_t c) {
        styl_.normalny.ramka = c;
        uniewaznij();
    }

    void ustaw_kolor_tla_stanu(StanWizualny stan, uint32_t c) {
        wybierz_styl_stanu(stan).tlo = c;
        uniewaznij();
    }

    void ustaw_kolor_tekstu_stanu(StanWizualny stan, uint32_t c) {
        wybierz_styl_stanu(stan).tekst = c;
        uniewaznij();
    }

    void ustaw_kolor_ramki_stanu(StanWizualny stan, uint32_t c) {
        wybierz_styl_stanu(stan).ramka = c;
        uniewaznij();
    }

    int grubosc_ramki() const { return wlasna_grubosc_ramki_; }
    void ustaw_grubosc_ramki(int g) {
        wlasna_grubosc_ramki_ = g;
        uniewaznij();
    }

    int promien_zaokraglenia() const { return wlasny_promien_zaokraglenia_; }
    void ustaw_promien_zaokraglenia(int p) {
        wlasny_promien_zaokraglenia_ = p;
        uniewaznij();
    }

    uint8_t przezroczystosc() const { return przezroczystosc_; }
    void ustaw_przezroczystosc(uint8_t p) {
        przezroczystosc_ = p;
        uniewaznij();
    }

    StylStanu wyznacz_biezacy_styl_stanu() const {
        StylStanu s = styl_.normalny;
        if (!czy_wlaczona_) {
            s = styl_.wylaczony;
        } else if (czy_tylko_do_odczytu_) {
            s = styl_.tylko_do_odczytu;
        } else if (czy_wcisnieta_ && czy_reaguje_wizualnie_na_wcisniecie()) {
            s = styl_.wcisniety;
        } else if (czy_ma_fokus_) {
            s = styl_.fokus;
        } else if (czy_najechana_ && czy_reaguje_wizualnie_na_najechanie()) {
            s = styl_.najechany;
        }

        if (wlasna_grubosc_ramki_ >= 0) s.grubosc_ramki = wlasna_grubosc_ramki_;
        if (wlasny_promien_zaokraglenia_ >= 0) s.promien_zaokraglenia = wlasny_promien_zaokraglenia_;
        s.przezroczystosc = przezroczystosc_;
        return s;
    }

    // =========================================================================
    // 6. Zarządzanie odrysowywaniem i grupowanie (begin_update / end_update)
    // =========================================================================

    bool czy_brudna() const { return czy_brudna_; }
    Prostokat pobierz_brudny_obszar() const { return brudny_obszar_; }
    void wyczysc_brudny_obszar() {
        czy_brudna_ = false;
        brudny_obszar_ = Prostokat(0, 0, 0, 0);
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i]) dzieci_[i]->wyczysc_brudny_obszar();
        }
    }

    void rozpocznij_aktualizacje() {
        ++blokada_aktualizacji_;
    }

    void zakoncz_aktualizacje() {
        if (blokada_aktualizacji_ > 0) {
            --blokada_aktualizacji_;
            if (blokada_aktualizacji_ == 0) {
                uniewaznij();
            }
        }
    }

    bool czy_w_trakcie_aktualizacji() const { return blokada_aktualizacji_ > 0; }

    void uniewaznij() {
        if (blokada_aktualizacji_ > 0) return;
        czy_brudna_ = true;
        brudny_obszar_ = obszar_ekranu_;
        if (rodzic_) {
            rodzic_->uniewaznij_dziecko(this, obszar_ekranu_);
        }
    }

    void uniewaznij_obszar(Prostokat r) {
        if (blokada_aktualizacji_ > 0) return;
        czy_brudna_ = true;
        Prostokat r_ekranu(obszar_ekranu_.x + r.x, obszar_ekranu_.y + r.y, r.szerokosc, r.wysokosc);
        brudny_obszar_ = brudny_obszar_.suma(r_ekranu);
        if (rodzic_) {
            rodzic_->uniewaznij_dziecko(this, r_ekranu);
        }
    }

    virtual void uniewaznij_dziecko(Kontrolka* child, Prostokat r_ekranu) {
        (void)child;
        if (blokada_aktualizacji_ > 0) return;
        czy_brudna_ = true;
        brudny_obszar_ = brudny_obszar_.suma(r_ekranu);
        if (rodzic_) {
            rodzic_->uniewaznij_dziecko(this, r_ekranu);
        }
    }

    // =========================================================================
    // 7. Rejestracja funkcji zwrotnych (Event API)
    // =========================================================================

    void przy_kliknieciu(FunkcjaZdarzenia cb, void* dane = nullptr) {
        cb_klikniecie_ = cb;
        dane_klikniecia_ = dane;
    }

    void przy_podwojnym_kliknieciu(FunkcjaZdarzenia cb, void* dane = nullptr) {
        cb_podwojne_klikniecie_ = cb;
        dane_podwojnego_klikniecia_ = dane;
    }

    void przy_wcisnieciu_myszy(FunkcjaMyszy cb, void* dane = nullptr) {
        cb_wcisniecie_myszy_ = cb;
        dane_wcisniecia_myszy_ = dane;
    }

    void przy_zwolnieniu_myszy(FunkcjaMyszy cb, void* dane = nullptr) {
        cb_zwolnienie_myszy_ = cb;
        dane_zwolnienia_myszy_ = dane;
    }

    void przy_ruchu_myszy(FunkcjaMyszy cb, void* dane = nullptr) {
        cb_ruch_myszy_ = cb;
        dane_ruchu_myszy_ = dane;
    }

    void przy_wejsciu_myszy(FunkcjaZdarzenia cb, void* dane = nullptr) {
        cb_wejscie_myszy_ = cb;
        dane_wejscia_myszy_ = dane;
    }

    void przy_opuszczeniu_myszy(FunkcjaZdarzenia cb, void* dane = nullptr) {
        cb_opuszczenie_myszy_ = cb;
        dane_opuszczenia_myszy_ = dane;
    }

    void przy_rolce(FunkcjaRolki cb, void* dane = nullptr) {
        cb_rolka_ = cb;
        dane_rolki_ = dane;
    }

    void przy_klawiszu_wcisnietym(FunkcjaKlawiatury cb, void* dane = nullptr) {
        cb_klawisz_wcisniety_ = cb;
        dane_klawisza_wcisnietego_ = dane;
    }

    void przy_klawiszu_zwolnionym(FunkcjaKlawiatury cb, void* dane = nullptr) {
        cb_klawisz_zwolniony_ = cb;
        dane_klawisza_zwolnionego_ = dane;
    }

    void przy_uzyskaniu_skupienia(FunkcjaZdarzenia cb, void* dane = nullptr) {
        cb_uzyskanie_skupienia_ = cb;
        dane_uzyskania_skupienia_ = dane;
    }

    void przy_utracie_skupienia(FunkcjaZdarzenia cb, void* dane = nullptr) {
        cb_utrata_skupienia_ = cb;
        dane_utraty_skupienia_ = dane;
    }

    void przy_zmianie_wartosci(FunkcjaZmianyWartosci cb, void* dane = nullptr) {
        cb_zmiana_wartosci_ = cb;
        dane_zmiany_wartosci_ = dane;
    }

    void przy_zmianie_rozmiaru(FunkcjaZdarzenia cb, void* dane = nullptr) {
        cb_zmiana_rozmiaru_ = cb;
        dane_zmiany_rozmiaru_ = dane;
    }

    void przy_wlasnym_rysowaniu(FunkcjaRysowania cb, void* dane = nullptr) {
        cb_wlasne_rysowanie_ = cb;
        dane_wlasnego_rysowania_ = dane;
    }

    virtual bool czy_wymaga_animacji() const {
        if (!czy_widoczna_) return false;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] && dzieci_[i]->czy_widoczna() && dzieci_[i]->czy_wymaga_animacji()) return true;
        }
        return false;
    }

    virtual void krok_animacji(uint64_t teraz_ns = 0) {
        if (!czy_widoczna_) return;
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] && dzieci_[i]->czy_widoczna()) dzieci_[i]->krok_animacji(teraz_ns);
        }
    }

    virtual Rozmiar oblicz_naturalny_rozmiar() const {
        return Rozmiar(0, 0);
    }

    virtual Rozmiar zmierz(Rozmiar dostepny_rozmiar) {
        (void)dostepny_rozmiar;
        Rozmiar naturalny = oblicz_naturalny_rozmiar();
        int w = preferowany_rozmiar_.czy_okreslona_szerokosc() ? (preferowany_rozmiar_.szerokosc * skala_dpi_) : naturalny.szerokosc;
        int h = preferowany_rozmiar_.czy_okreslona_wysokosc() ? (preferowany_rozmiar_.wysokosc * skala_dpi_) : naturalny.wysokosc;

        int min_w = minimalny_rozmiar_.szerokosc > 0 ? (minimalny_rozmiar_.szerokosc * skala_dpi_) : 0;
        int min_h = minimalny_rozmiar_.wysokosc > 0 ? (minimalny_rozmiar_.wysokosc * skala_dpi_) : 0;
        int max_w = maksymalny_rozmiar_.szerokosc > 0 ? (maksymalny_rozmiar_.szerokosc * skala_dpi_) : 100000;
        int max_h = maksymalny_rozmiar_.wysokosc > 0 ? (maksymalny_rozmiar_.wysokosc * skala_dpi_) : 100000;

        w = przytnij(w, min_w, max_w);
        h = przytnij(h, min_h, max_h);
        return Rozmiar(w < 0 ? 0 : w, h < 0 ? 0 : h);
    }

    virtual void rozmiesc(Prostokat docelowy_obszar) {
        bool rozmiar_zmieniony = (obszar_.szerokosc != docelowy_obszar.szerokosc || obszar_.wysokosc != docelowy_obszar.wysokosc);
        obszar_ = docelowy_obszar;
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());

        Prostokat klient = Prostokat(wypelnienie_.lewy * skala_dpi_, wypelnienie_.gorny * skala_dpi_,
                                     obszar_.szerokosc - wypelnienie_.poziomo() * skala_dpi_ < 0 ? 0 : obszar_.szerokosc - wypelnienie_.poziomo() * skala_dpi_,
                                     obszar_.wysokosc - wypelnienie_.pionowo() * skala_dpi_ < 0 ? 0 : obszar_.wysokosc - wypelnienie_.pionowo() * skala_dpi_);

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] && dzieci_[i]->czy_widoczna()) {
                dzieci_[i]->rozmiesc(klient);
            }
        }

        if (rozmiar_zmieniony) {
            obsluz_zmiane_rozmiaru();
        }
    }

    virtual void zaktualizuj_pozycje_ekranowa() {
        Punkt rodzic_poz = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rodzic_poz + obszar_.pozycja(), obszar_.rozmiar());
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i]) {
                dzieci_[i]->zaktualizuj_pozycje_ekranowa();
            }
        }
    }

    virtual void rysuj(KontekstRysowania& ctx, bool wymus = false) {
        if (!czy_widoczna_) return;

        // obszar_ekranu_ jest w przestrzeni klienta okna. Stos przyciecia
        // KontekstRysowania jest natomiast w przestrzeni ekranu (Okno dodaje
        // swoje x/y przed rozpoczeciem przejscia drzewa). Wyznaczamy wiec
        // rzeczywisty prostokat renderowania z aktualnego przesuniecia
        // rodzica i lokalnej geometrii kontrolki.
        Punkt przesuniecie_rodzica = ctx.pobierz_przesuniecie();
        Prostokat obszar_renderowania(przesuniecie_rodzica.x + obszar_.x,
                                      przesuniecie_rodzica.y + obszar_.y,
                                      obszar_.szerokosc, obszar_.wysokosc);
        Prostokat clip = ctx.pobierz_przyciecie();
        if (!obszar_renderowania.przecina(clip) && !wymus) {
            return;
        }

        bool nalezy_rysowac = czy_brudna_ || wymus || obszar_renderowania.przecina(clip);

        // Przyciecie kontrolki obejmuje caly jej poddrzewo. Bez tego dzieci
        // panelu mogly rysowac poza kontenerem podczas dirty-redraw.
        ctx.dodaj_przesuniecie(Punkt(obszar_.x, obszar_.y));
        ctx.dodaj_przyciecie(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc));

        if (nalezy_rysowac) {
            rysuj_siebie(ctx);

            if (cb_wlasne_rysowanie_) {
                cb_wlasne_rysowanie_(this, ctx, dane_wlasnego_rysowania_);
            }
        }

        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (dzieci_[i] && dzieci_[i]->czy_widoczna()) {
                dzieci_[i]->rysuj(ctx, wymus);
            }
        }

        ctx.zdejmij_przyciecie();
        ctx.zdejmij_przesuniecie();

        czy_brudna_ = false;
    }

    virtual void rysuj_siebie(KontekstRysowania& ctx) {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);
    }

    virtual void rysuj_tlo_i_ramke(KontekstRysowania& ctx, const StylStanu& s) {
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        if (r.czy_pusty()) return;

        // Tło
        if (s.tlo != 0) {
            if (s.promien_zaokraglenia > 0) {
                ctx.rysuj_zaokraglony_prostokat(r, s.tlo, s.promien_zaokraglenia * skala_dpi_);
            } else {
                ctx.rysuj_prostokat(r, s.tlo);
            }
        }

        // Ramka
        if (s.ramka != 0 && s.grubosc_ramki > 0) {
            if (s.promien_zaokraglenia > 0) {
                ctx.rysuj_zaokraglona_ramke(r, s.ramka, s.promien_zaokraglenia * skala_dpi_, s.grubosc_ramki * skala_dpi_);
            } else {
                ctx.rysuj_ramke(r, s.ramka, s.grubosc_ramki * skala_dpi_);
            }
        }
    }

    virtual Kontrolka* sprawdz_trafienie(Punkt pt) {
        if (!czy_widoczna_ || !obszar_.zawiera(pt)) return nullptr;

        Punkt lokalny(pt.x - obszar_.x, pt.y - obszar_.y);
        for (size_t i = liczba_dzieci_; i > 0; --i) {
            Kontrolka* child = dzieci_[i - 1];
            if (child && child->czy_widoczna()) {
                Kontrolka* hit = child->sprawdz_trafienie(lokalny);
                if (hit) return hit;
            }
        }
        return this;
    }

    virtual Kontrolka* kontrolka_aktywacji_alt() {
        for (size_t i = 0; i < liczba_dzieci_; ++i) {
            if (!dzieci_[i] || !dzieci_[i]->czy_widoczna()) continue;
            Kontrolka* wynik = dzieci_[i]->kontrolka_aktywacji_alt();
            if (wynik) return wynik;
        }
        return nullptr;
    }

    // =========================================================================
    // 9. Wirtualne metody obsługi zdarzeń
    // =========================================================================

    virtual void obsluz_wejscie_myszy() {
        bool zmiana_wizualna = !czy_najechana_ && czy_wlaczona_ &&
                               czy_reaguje_wizualnie_na_najechanie();
        czy_najechana_ = true;
        if (zmiana_wizualna) uniewaznij();
        if (cb_wejscie_myszy_) cb_wejscie_myszy_(this, dane_wejscia_myszy_);
    }

    virtual void obsluz_opuszczenie_myszy() {
        bool zmiana_wizualna = czy_wlaczona_ &&
                               ((czy_wcisnieta_ && czy_reaguje_wizualnie_na_wcisniecie()) ||
                                (czy_najechana_ && czy_reaguje_wizualnie_na_najechanie()));
        czy_najechana_ = false;
        czy_wcisnieta_ = false;
        if (zmiana_wizualna) uniewaznij();
        if (cb_opuszczenie_myszy_) cb_opuszczenie_myszy_(this, dane_opuszczenia_myszy_);
    }

    virtual void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) {
        if (!czy_wlaczona_) return;
        czy_wcisnieta_ = true;
        if (czy_reaguje_wizualnie_na_wcisniecie()) uniewaznij();
        if (cb_wcisniecie_myszy_) cb_wcisniecie_myszy_(this, e, dane_wcisniecia_myszy_);
    }

    virtual void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) {
        if (!czy_wlaczona_) return;
        bool zmiana_wizualna = czy_wcisnieta_ &&
                               czy_reaguje_wizualnie_na_wcisniecie();
        czy_wcisnieta_ = false;
        if (zmiana_wizualna) uniewaznij();
        if (cb_zwolnienie_myszy_) cb_zwolnienie_myszy_(this, e, dane_zwolnienia_myszy_);
    }

    virtual void obsluz_klikniecie(ZdarzenieMyszki& e) {
        (void)e;
        if (cb_klikniecie_) cb_klikniecie_(this, dane_klikniecia_);
    }

    virtual void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) {
        (void)e;
        if (cb_podwojne_klikniecie_) cb_podwojne_klikniecie_(this, dane_podwojnego_klikniecia_);
    }

    virtual void obsluz_ruch_myszy(ZdarzenieMyszki& e) {
        if (cb_ruch_myszy_) cb_ruch_myszy_(this, e, dane_ruchu_myszy_);
    }

    virtual void obsluz_rolke(ZdarzenieRolki& e) {
        if (cb_rolka_) cb_rolka_(this, e, dane_rolki_);
    }

    virtual void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) {
        if (cb_klawisz_wcisniety_) cb_klawisz_wcisniety_(this, e, dane_klawisza_wcisnietego_);
    }

    virtual void obsluz_zwolnienie_klawisza(ZdarzenieKlawiatury& e) {
        if (cb_klawisz_zwolniony_) cb_klawisz_zwolniony_(this, e, dane_klawisza_zwolnionego_);
    }

    virtual void obsluz_wprowadzenie_tekstu(ZdarzenieWprowadzaniaTekstu& e) {
        (void)e;
    }

    virtual void obsluz_uzyskanie_skupienia() {
        czy_ma_fokus_ = true;
        uniewaznij();
        if (cb_uzyskanie_skupienia_) cb_uzyskanie_skupienia_(this, dane_uzyskania_skupienia_);
    }

    virtual void obsluz_utrate_skupienia() {
        czy_ma_fokus_ = false;
        uniewaznij();
        if (cb_utrata_skupienia_) cb_utrata_skupienia_(this, dane_utraty_skupienia_);
    }

    virtual void obsluz_zmiane_rozmiaru() {
        if (cb_zmiana_rozmiaru_) cb_zmiana_rozmiaru_(this, dane_zmiany_rozmiaru_);
    }

    // Aliasy dla zachowania wstecznej zgodności
    virtual void przy_najechaniu() { obsluz_wejscie_myszy(); }
    virtual void przy_opuszczeniu() { obsluz_opuszczenie_myszy(); }
    virtual void przy_wcisnieciu_myszy(ZdarzenieMyszki& e) { obsluz_wcisniecie_myszy(e); }
    virtual void przy_zwolnieniu_myszy(ZdarzenieMyszki& e) { obsluz_zwolnienie_myszy(e); }
    virtual void przy_ruchu_myszy(ZdarzenieMyszki& e) { obsluz_ruch_myszy(e); }
    virtual void przy_wcisnieciu_klawisza(ZdarzenieKlawiatury& e) { obsluz_wcisniecie_klawisza(e); }
    virtual void przy_zwolnieniu_klawisza(ZdarzenieKlawiatury& e) { obsluz_zwolnienie_klawisza(e); }
    virtual void przy_wprowadzeniu_tekstu(ZdarzenieWprowadzaniaTekstu& e) { obsluz_wprowadzenie_tekstu(e); }
    virtual void przy_fokusie() { obsluz_uzyskanie_skupienia(); }
    virtual void przy_utracie_fokusu() { obsluz_utrate_skupienia(); }

protected:
    // Uklad nie moze zmieniac sie przy przejsciu normalny/fokus/hover.
    // Rezerwujemy najwieksza zadana grubosc sposrod stanow (albo jawne
    // nadpisanie kontrolki), a biezacy stan zmienia jedynie sposob rysowania.
    int grubosc_ramki_geometrii() const {
        if (wlasna_grubosc_ramki_ >= 0)
            return wlasna_grubosc_ramki_ * skala_dpi_;
        int g = styl_.normalny.grubosc_ramki;
        const StylStanu* stany[] = {
            &styl_.najechany, &styl_.wcisniety, &styl_.fokus,
            &styl_.wylaczony, &styl_.tylko_do_odczytu
        };
        for (size_t i = 0; i < sizeof(stany) / sizeof(stany[0]); ++i)
            if (stany[i]->grubosc_ramki > g) g = stany[i]->grubosc_ramki;
        return (g > 0 ? g : 0) * skala_dpi_;
    }

    StylStanu& wybierz_styl_stanu(StanWizualny stan) {
        switch (stan) {
            case StanWizualny::Najechany: return styl_.najechany;
            case StanWizualny::Wcisniety: return styl_.wcisniety;
            case StanWizualny::Fokus: return styl_.fokus;
            case StanWizualny::Wylaczony: return styl_.wylaczony;
            case StanWizualny::TylkoDoOdczytu: return styl_.tylko_do_odczytu;
            case StanWizualny::Normalny:
            default: return styl_.normalny;
        }
    }

    const StylStanu& wybierz_styl_stanu(StanWizualny stan) const {
        switch (stan) {
            case StanWizualny::Najechany: return styl_.najechany;
            case StanWizualny::Wcisniety: return styl_.wcisniety;
            case StanWizualny::Fokus: return styl_.fokus;
            case StanWizualny::Wylaczony: return styl_.wylaczony;
            case StanWizualny::TylkoDoOdczytu: return styl_.tylko_do_odczytu;
            case StanWizualny::Normalny:
            default: return styl_.normalny;
        }
    }

    Kontrolka* rodzic_{nullptr};
    ZarzadcaPopupow* zarzadca_popupow_{nullptr};
    Kontrolka* dzieci_[MAKS_DZIECI]{nullptr};
    size_t liczba_dzieci_{0};

    int id_{0};
    char nazwa_[32]{0};
    char podpowiedz_[64]{0};
    void* dane_uzytkownika_{nullptr};
    uint64_t znacznik_{0};
    char tekst_[MAKS_DLUGOSC_TEKSTU]{0};

    Prostokat obszar_{0, 0, 0, 0};
    Prostokat obszar_ekranu_{0, 0, 0, 0};

    Rozmiar minimalny_rozmiar_{0, 0};
    Rozmiar maksymalny_rozmiar_{10000, 10000};
    Rozmiar preferowany_rozmiar_{-1, -1};

    Marginesy margines_{0, 0, 0, 0};
    Marginesy wypelnienie_{0, 0, 0, 0};

    WyrownaniePoziome wyrownanie_poziome_{WyrownaniePoziome::Rozciagnij};
    WyrownaniePionowe wyrownanie_pionowe_{WyrownaniePionowe::Poczatek};

    bool czy_widoczna_{true};
    bool czy_wlaczona_{true};
    bool czy_tylko_do_odczytu_{false};
    bool czy_najechana_{false};
    bool czy_reaguje_wizualnie_na_najechanie_{false};
    bool czy_reaguje_wizualnie_na_wcisniecie_{true};
    bool czy_wcisnieta_{false};
    bool czy_ma_fokus_{false};
    bool czy_moze_miec_fokus_{false};
    int kolejnosc_tab_{0};
    int skala_dpi_{1};

    int blokada_aktualizacji_{0};
    bool czy_brudna_{true};
    Prostokat brudny_obszar_{0, 0, 0, 0};

    StylKontrolki styl_;
    int wlasna_grubosc_ramki_{-1};
    int wlasny_promien_zaokraglenia_{-1};
    uint8_t przezroczystosc_{255};

    // Wskaźniki funkcji zwrotnych
    FunkcjaZdarzenia cb_klikniecie_{nullptr};
    void* dane_klikniecia_{nullptr};

    FunkcjaZdarzenia cb_podwojne_klikniecie_{nullptr};
    void* dane_podwojnego_klikniecia_{nullptr};

    FunkcjaMyszy cb_wcisniecie_myszy_{nullptr};
    void* dane_wcisniecia_myszy_{nullptr};

    FunkcjaMyszy cb_zwolnienie_myszy_{nullptr};
    void* dane_zwolnienia_myszy_{nullptr};

    FunkcjaMyszy cb_ruch_myszy_{nullptr};
    void* dane_ruchu_myszy_{nullptr};

    FunkcjaZdarzenia cb_wejscie_myszy_{nullptr};
    void* dane_wejscia_myszy_{nullptr};

    FunkcjaZdarzenia cb_opuszczenie_myszy_{nullptr};
    void* dane_opuszczenia_myszy_{nullptr};

    FunkcjaRolki cb_rolka_{nullptr};
    void* dane_rolki_{nullptr};

    FunkcjaKlawiatury cb_klawisz_wcisniety_{nullptr};
    void* dane_klawisza_wcisnietego_{nullptr};

    FunkcjaKlawiatury cb_klawisz_zwolniony_{nullptr};
    void* dane_klawisza_zwolnionego_{nullptr};

    FunkcjaZdarzenia cb_uzyskanie_skupienia_{nullptr};
    void* dane_uzyskania_skupienia_{nullptr};

    FunkcjaZdarzenia cb_utrata_skupienia_{nullptr};
    void* dane_utraty_skupienia_{nullptr};

    FunkcjaZmianyWartosci cb_zmiana_wartosci_{nullptr};
    void* dane_zmiany_wartosci_{nullptr};

    FunkcjaZdarzenia cb_zmiana_rozmiaru_{nullptr};
    void* dane_zmiany_rozmiaru_{nullptr};

    FunkcjaRysowania cb_wlasne_rysowanie_{nullptr};
    void* dane_wlasnego_rysowania_{nullptr};
};

} // namespace BursztynUI
