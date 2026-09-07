#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class Suwak : public Kontrolka {
public:
    using FunkcjaWartosci = void (*)(Suwak* nadawca, int wartosc, void* dane_uzytkownika);

    Suwak() {
        styl_ = Motyw::pobierz_domyslny().styl_suwaka;
        wypelnienie_ = styl_.wypelnienie;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = true;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
    }

    void ustaw_zakres(int min, int max) {
        if (min > max) {
            int tmp = min;
            min = max;
            max = tmp;
        }
        if (minimum_ == min && maksimum_ == max) return;

        int stara = wartosc_;
        minimum_ = min;
        maksimum_ = max;
        wartosc_ = dopasuj_wartosc(wartosc_);
        uniewaznij();
        if (wartosc_ != stara) powiadom_o_zmianie();
    }

    int minimum() const { return minimum_; }
    int maksimum() const { return maksimum_; }

    void ustaw_wartosc(int wartosc) {
        ustaw_wartosc_wewnetrznie(wartosc);
    }

    int wartosc() const { return wartosc_; }

    void ustaw_krok(int krok) {
        int nowy = krok;
        if (nowy < 0) {
            int64_t dodatni = -static_cast<int64_t>(nowy);
            nowy = (dodatni > 2147483647LL) ? 2147483647 : static_cast<int>(dodatni);
        }
        if (nowy == 0) nowy = 1;
        if (krok_ == nowy) return;

        int stara = wartosc_;
        krok_ = nowy;
        wartosc_ = dopasuj_wartosc(wartosc_);
        uniewaznij();
        if (wartosc_ != stara) powiadom_o_zmianie();
    }

    int krok() const { return krok_; }

    void ustaw_orientacje(Orientacja orientacja) {
        if (orientacja_ == orientacja) return;
        orientacja_ = orientacja;
        if (orientacja_ == Orientacja::Pozioma) {
            wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
            wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        } else {
            wyrownanie_poziome_ = WyrownaniePoziome::Poczatek;
            wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        }
        uniewaznij();
    }

    Orientacja orientacja() const { return orientacja_; }

    void ustaw_pokazywanie_wartosci(bool pokazuj) {
        if (czy_pokazuje_wartosc_ == pokazuj) return;
        czy_pokazuje_wartosc_ = pokazuj;
        uniewaznij();
    }

    bool czy_pokazuje_wartosc() const { return czy_pokazuje_wartosc_; }

    void ustaw_kolor_toru(uint32_t kolor) {
        kolor_toru_ = kolor;
        uniewaznij();
    }

    void ustaw_kolor_wypelnienia(uint32_t kolor) {
        kolor_wypelnienia_ = kolor;
        uniewaznij();
    }

    void ustaw_kolor_uchwytu(uint32_t kolor) {
        kolor_uchwytu_ = kolor;
        uniewaznij();
    }

    void ustaw_rozmiar_uchwytu(int rozmiar) {
        int nowy = rozmiar < 4 ? 4 : rozmiar;
        if (rozmiar_uchwytu_ == nowy) return;
        rozmiar_uchwytu_ = nowy;
        uniewaznij();
    }

    using Kontrolka::przy_zmianie_wartosci;
    void przy_zmianie_wartosci(FunkcjaWartosci cb, void* dane = nullptr) {
        cb_wartosc_ = cb;
        dane_wartosci_ = dane;
    }

    void przy_zatwierdzeniu_wartosci(FunkcjaWartosci cb, void* dane = nullptr) {
        cb_zatwierdzenie_ = cb;
        dane_zatwierdzenia_ = dane;
    }

    void ustaw_wlaczona(bool wlaczona) {
        if (czy_wlaczona_ == wlaczona) return;
        czy_wlaczona_ = wlaczona;
        if (!wlaczona) {
            czy_wcisnieta_ = false;
            czy_przeciagany_ = false;
            czy_interakcja_myszy_ = false;
        }
        uniewaznij();
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI skala = skala_dpi_;
        int uchwyt = rozmiar_uchwytu_ * skala;
        int tekst_h = czy_pokazuje_wartosc_ ? CzcionkaUI::pobierz_wysokosc(skala) : 0;
        int pad_h = wypelnienie_.poziomo() * skala;
        int pad_v = wypelnienie_.pionowo() * skala;

        if (orientacja_ == Orientacja::Pozioma) {
            int h = uchwyt > tekst_h ? uchwyt : tekst_h;
            return Rozmiar(180 * skala, h + pad_v);
        }

        char min_buf[16];
        char max_buf[16];
        formatuj_liczbe(minimum_, min_buf, sizeof(min_buf));
        formatuj_liczbe(maksimum_, max_buf, sizeof(max_buf));
        int tekst_w = 0;
        if (czy_pokazuje_wartosc_) {
            int min_w = CzcionkaUI::zmierz_tekst(min_buf, skala);
            int max_w = CzcionkaUI::zmierz_tekst(max_buf, skala);
            tekst_w = min_w > max_w ? min_w : max_w;
        }
        int w = uchwyt > tekst_w ? uchwyt : tekst_w;
        return Rozmiar(w + pad_h, 160 * skala);
    }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        Rozmiar zadany = oblicz_naturalny_rozmiar();
        SkalaUI skala = skala_dpi_;
        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala;
        zadany.szerokosc = przytnij(zadany.szerokosc,
                                    minimalny_rozmiar_.szerokosc * skala,
                                    maksymalny_rozmiar_.szerokosc * skala);
        zadany.wysokosc = przytnij(zadany.wysokosc,
                                   minimalny_rozmiar_.wysokosc * skala,
                                   maksymalny_rozmiar_.wysokosc * skala);
        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);
        int gr = skala_dpi_;
        Prostokat zewnetrzny(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        ctx.rysuj_ramke(zewnetrzny,
                        czy_wlaczona_ ? Kolory::RamkaDomyslna
                                      : Kolory::WylaczonyRamka,
                        gr);

        Geometria g = oblicz_geometrie(wartosc_);
        if (!g.tor.czy_pusty()) {
            uint32_t tor = czy_wlaczona_ ? kolor_toru_ : Kolory::WylaczonyTlo;
            uint32_t wypelnienie = czy_wlaczona_ ? kolor_wypelnienia_ : Kolory::WylaczonyRamka;
            ctx.rysuj_zaokraglony_prostokat(g.tor, tor, g.tor_grubosc / 2);
            if (!g.wypelnienie.czy_pusty()) {
                ctx.rysuj_zaokraglony_prostokat(g.wypelnienie, wypelnienie,
                                                g.tor_grubosc / 2);
            }
        }

        uint32_t uchwyt = kolor_uchwytu_;
        if (!czy_wlaczona_) {
            uchwyt = Kolory::WylaczonyTekst;
        } else if (czy_wcisnieta_) {
            uchwyt = Kolory::AkcentFokus;
        } else if (czy_najechana_) {
            uchwyt = Kolory::AkcentNajechany;
        }
        ctx.rysuj_zaokraglony_prostokat(g.uchwyt, uchwyt, 3 * skala_dpi_);
        ctx.rysuj_zaokraglona_ramke(g.uchwyt, s.ramka, 3 * skala_dpi_,
                                   skala_dpi_);

        if (czy_ma_fokus_ && czy_wlaczona_) {
            ctx.rysuj_ramke(zewnetrzny.zmniejszony(Marginesy(gr)),
                            Kolory::AkcentFokus, gr);
        }

        if (czy_pokazuje_wartosc_ && !g.wartosc.czy_pusty()) {
            char bufor[16];
            formatuj_liczbe(wartosc_, bufor, sizeof(bufor));
            uint32_t tekst = czy_wlaczona_ ? s.tekst : Kolory::WylaczonyTekst;
            ctx.rysuj_tekst_wysrodkowany(g.wartosc, bufor, tekst, skala_dpi_);
        }
    }

    void obsluz_wejscie_myszy() override {
        Kontrolka::obsluz_wejscie_myszy();
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;

        Geometria g = oblicz_geometrie(wartosc_);
        int lokalny_x = e.x - obszar_ekranu_.x;
        int lokalny_y = e.y - obszar_ekranu_.y;
        bool uchwyt_trafiony = g.uchwyt.zawiera(lokalny_x, lokalny_y);

        czy_wcisnieta_ = true;
        czy_przeciagany_ = true;
        czy_interakcja_myszy_ = true;
        int os = orientacja_ == Orientacja::Pozioma ? lokalny_x : lokalny_y;
        int srodek = orientacja_ == Orientacja::Pozioma
                   ? g.uchwyt.x + g.uchwyt.szerokosc / 2
                   : g.uchwyt.y + g.uchwyt.wysokosc / 2;
        przesuniecie_chwytu_ = uchwyt_trafiony ? os - srodek : 0;

        if (!uchwyt_trafiony) ustaw_z_piksela(os, g, 0);
        uniewaznij_obszar(g.uchwyt);
        e.obsluzone = true;
        if (cb_wcisniecie_myszy_) cb_wcisniecie_myszy_(this, e, dane_wcisniecia_myszy_);
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        obsluz_wcisniecie_myszy(e);
        if (cb_podwojne_klikniecie_) {
            cb_podwojne_klikniecie_(this, dane_podwojnego_klikniecia_);
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (czy_przeciagany_) {
            Geometria g = oblicz_geometrie(wartosc_);
            int os = orientacja_ == Orientacja::Pozioma
                   ? e.x - obszar_ekranu_.x
                   : e.y - obszar_ekranu_.y;
            ustaw_z_piksela(os, g, przesuniecie_chwytu_);
            e.obsluzone = true;
        }
        if (cb_ruch_myszy_) cb_ruch_myszy_(this, e, dane_ruchu_myszy_);
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        bool koniec_interakcji = czy_interakcja_myszy_ && e.czy_lewy_przycisk();
        czy_przeciagany_ = false;
        czy_interakcja_myszy_ = false;
        czy_wcisnieta_ = false;
        czy_najechana_ = obszar_ekranu_.zawiera(e.x, e.y) && czy_wlaczona_;
        uniewaznij();
        if (koniec_interakcji) {
            e.obsluzone = true;
            powiadom_o_zatwierdzeniu();
        }
        if (cb_zwolnienie_myszy_) cb_zwolnienie_myszy_(this, e, dane_zwolnienia_myszy_);
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        KierunekKlawisza kierunek = rozpoznaj_klawisz(e.kod_klawisza);
        bool obslugiwany = true;
        int64_t docelowa = wartosc_;
        int64_t duzy_krok = static_cast<int64_t>(krok_) * 10;

        switch (kierunek) {
            case KierunekKlawisza::Minimum: docelowa = minimum_; break;
            case KierunekKlawisza::Maksimum: docelowa = maksimum_; break;
            case KierunekKlawisza::Mniejsza:
                docelowa = static_cast<int64_t>(wartosc_) - krok_;
                break;
            case KierunekKlawisza::Wieksza:
                docelowa = static_cast<int64_t>(wartosc_) + krok_;
                break;
            case KierunekKlawisza::StronaMniejsza:
                docelowa = static_cast<int64_t>(wartosc_) - duzy_krok;
                break;
            case KierunekKlawisza::StronaWieksza:
                docelowa = static_cast<int64_t>(wartosc_) + duzy_krok;
                break;
            default: obslugiwany = false; break;
        }

        if (obslugiwany) {
            ustaw_wartosc_wewnetrznie(ogranicz_do_int(docelowa));
            powiadom_o_zatwierdzeniu();
            e.obsluzone = true;
            return;
        }
        if (cb_klawisz_wcisniety_) cb_klawisz_wcisniety_(this, e, dane_klawisza_wcisnietego_);
    }

    void obsluz_zwolnienie_klawisza(ZdarzenieKlawiatury& e) override {
        if (cb_klawisz_zwolniony_) cb_klawisz_zwolniony_(this, e, dane_klawisza_zwolnionego_);
    }

    void obsluz_utrate_skupienia() override {
        czy_przeciagany_ = false;
        czy_interakcja_myszy_ = false;
        czy_wcisnieta_ = false;
        Kontrolka::obsluz_utrate_skupienia();
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || (!czy_ma_fokus_ && !czy_najechana_)) return;
        int delta = e.delta_y != 0 ? e.delta_y : -e.delta_x;
        if (delta == 0) return;

        int64_t docelowa = static_cast<int64_t>(wartosc_) + (delta > 0 ? krok_ : -static_cast<int64_t>(krok_));
        ustaw_wartosc_wewnetrznie(ogranicz_do_int(docelowa));
        powiadom_o_zatwierdzeniu();
        e.obsluzone = true;
        if (cb_rolka_) cb_rolka_(this, e, dane_rolki_);
    }

private:
    struct Geometria {
        Prostokat tor;
        Prostokat wypelnienie;
        Prostokat uchwyt;
        Prostokat wartosc;
        int poczatek_osi{0};
        int koniec_osi{0};
        int tor_grubosc{1};
    };

    enum class KierunekKlawisza {
        Brak,
        Mniejsza,
        Wieksza,
        StronaMniejsza,
        StronaWieksza,
        Minimum,
        Maksimum
    };

    static int ogranicz_do_int(int64_t wartosc) {
        if (wartosc < -2147483648LL) return static_cast<int>(-2147483647 - 1);
        if (wartosc > 2147483647LL) return 2147483647;
        return static_cast<int>(wartosc);
    }

    int dopasuj_wartosc(int wartosc) const {
        int64_t min = minimum_;
        int64_t max = maksimum_;
        int64_t kandydat = wartosc;
        if (kandydat <= min) return minimum_;
        if (kandydat >= max) return maksimum_;
        if (min == max) return minimum_;

        int64_t step = krok_ > 0 ? krok_ : 1;
        int64_t od_min = kandydat - min;
        int64_t liczba_krokow = od_min / step;
        int64_t reszta = od_min % step;
        if (reszta * 2 >= step) ++liczba_krokow;
        int64_t wynik = min + liczba_krokow * step;
        if (wynik < min) wynik = min;
        if (wynik > max) wynik = max;
        return static_cast<int>(wynik);
    }

    bool ustaw_wartosc_wewnetrznie(int wartosc) {
        int nowa = dopasuj_wartosc(wartosc);
        if (nowa == wartosc_) return false;
        int stara = wartosc_;
        wartosc_ = nowa;
        uniewaznij_zmiane(stara, nowa);
        powiadom_o_zmianie();
        return true;
    }

    void powiadom_o_zmianie() {
        if (cb_wartosc_) cb_wartosc_(this, wartosc_, dane_wartosci_);
        if (cb_zmiana_wartosci_) cb_zmiana_wartosci_(this, dane_zmiany_wartosci_);
    }

    void powiadom_o_zatwierdzeniu() {
        if (cb_zatwierdzenie_) cb_zatwierdzenie_(this, wartosc_, dane_zatwierdzenia_);
    }

    static void formatuj_liczbe(int wartosc, char* bufor, size_t pojemnosc) {
        if (!bufor || pojemnosc == 0) return;
        char odwrotnie[16];
        size_t ile = 0;
        int64_t n = wartosc;
        bool ujemna = n < 0;
        uint64_t liczba = ujemna ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
        do {
            odwrotnie[ile++] = static_cast<char>('0' + (liczba % 10));
            liczba /= 10;
        } while (liczba != 0 && ile < sizeof(odwrotnie));

        size_t pos = 0;
        if (ujemna && pos + 1 < pojemnosc) bufor[pos++] = '-';
        while (ile > 0 && pos + 1 < pojemnosc) bufor[pos++] = odwrotnie[--ile];
        bufor[pos] = '\0';
    }

    int szerokosc_pola_wartosci(SkalaUI skala) const {
        if (!czy_pokazuje_wartosc_) return 0;
        char min_buf[16];
        char max_buf[16];
        formatuj_liczbe(minimum_, min_buf, sizeof(min_buf));
        formatuj_liczbe(maksimum_, max_buf, sizeof(max_buf));
        int a = CzcionkaUI::zmierz_tekst(min_buf, skala);
        int b = CzcionkaUI::zmierz_tekst(max_buf, skala);
        return (a > b ? a : b) + 4 * skala;
    }

    int piksel_dla_wartosci(int wartosc, int dlugosc) const {
        if (dlugosc <= 0 || minimum_ == maksimum_) return 0;
        int64_t zakres = static_cast<int64_t>(maksimum_) - minimum_;
        int64_t od_min = static_cast<int64_t>(wartosc) - minimum_;
        return static_cast<int>((od_min * dlugosc + zakres / 2) / zakres);
    }

    Geometria oblicz_geometrie(int wartosc) const {
        Geometria g;
        SkalaUI skala = skala_dpi_;
        int pad_l = wypelnienie_.lewy * skala;
        int pad_t = wypelnienie_.gorny * skala;
        int pad_r = wypelnienie_.prawy * skala;
        int pad_b = wypelnienie_.dolny * skala;
        int uchwyt = rozmiar_uchwytu_ * skala;
        int grubosc_toru = 6 * skala;
        g.tor_grubosc = grubosc_toru;

        if (orientacja_ == Orientacja::Pozioma) {
            int pole_w = szerokosc_pola_wartosci(skala);
            int przerwa = czy_pokazuje_wartosc_ ? 6 * skala : 0;
            int klient_prawy = obszar_.szerokosc - pad_r - pole_w - przerwa;
            g.poczatek_osi = pad_l + uchwyt / 2;
            g.koniec_osi = klient_prawy - (uchwyt - uchwyt / 2);
            if (g.koniec_osi < g.poczatek_osi) g.koniec_osi = g.poczatek_osi;
            int srodek_y = pad_t + (obszar_.wysokosc - pad_t - pad_b) / 2;
            int dlugosc = g.koniec_osi - g.poczatek_osi;
            int srodek_x = g.poczatek_osi + piksel_dla_wartosci(wartosc, dlugosc);
            g.tor = Prostokat(g.poczatek_osi, srodek_y - grubosc_toru / 2,
                              dlugosc + 1, grubosc_toru);
            g.wypelnienie = Prostokat(g.poczatek_osi, g.tor.y,
                                      srodek_x - g.poczatek_osi + 1, grubosc_toru);
            g.uchwyt = Prostokat(srodek_x - uchwyt / 2, srodek_y - uchwyt / 2,
                                 uchwyt, uchwyt);
            if (czy_pokazuje_wartosc_) {
                g.wartosc = Prostokat(obszar_.szerokosc - pad_r - pole_w, pad_t,
                                      pole_w, obszar_.wysokosc - pad_t - pad_b);
            }
        } else {
            int tekst_h = czy_pokazuje_wartosc_ ? CzcionkaUI::pobierz_wysokosc(skala) : 0;
            int przerwa = czy_pokazuje_wartosc_ ? 4 * skala : 0;
            int srodek_x = pad_l + (obszar_.szerokosc - pad_l - pad_r) / 2;
            g.poczatek_osi = pad_t + tekst_h + przerwa + uchwyt / 2;
            g.koniec_osi = obszar_.wysokosc - pad_b - (uchwyt - uchwyt / 2);
            if (g.koniec_osi < g.poczatek_osi) g.koniec_osi = g.poczatek_osi;
            int dlugosc = g.koniec_osi - g.poczatek_osi;
            int srodek_y = g.koniec_osi - piksel_dla_wartosci(wartosc, dlugosc);
            g.tor = Prostokat(srodek_x - grubosc_toru / 2, g.poczatek_osi,
                              grubosc_toru, dlugosc + 1);
            g.wypelnienie = Prostokat(g.tor.x, srodek_y, grubosc_toru,
                                      g.koniec_osi - srodek_y + 1);
            g.uchwyt = Prostokat(srodek_x - uchwyt / 2, srodek_y - uchwyt / 2,
                                 uchwyt, uchwyt);
            if (czy_pokazuje_wartosc_) {
                g.wartosc = Prostokat(pad_l, pad_t,
                                      obszar_.szerokosc - pad_l - pad_r, tekst_h);
            }
        }
        return g;
    }

    void ustaw_z_piksela(int piksel, const Geometria& g, int przesuniecie) {
        int pozycja = piksel - przesuniecie;
        int dlugosc = g.koniec_osi - g.poczatek_osi;
        if (dlugosc <= 0 || minimum_ == maksimum_) {
            ustaw_wartosc_wewnetrznie(minimum_);
            return;
        }
        int od_poczatku;
        if (orientacja_ == Orientacja::Pozioma) {
            od_poczatku = pozycja - g.poczatek_osi;
        } else {
            od_poczatku = g.koniec_osi - pozycja;
        }
        od_poczatku = przytnij(od_poczatku, 0, dlugosc);
        int64_t zakres = static_cast<int64_t>(maksimum_) - minimum_;
        int64_t surowa = static_cast<int64_t>(minimum_) +
                         (zakres * od_poczatku + dlugosc / 2) / dlugosc;
        ustaw_wartosc_wewnetrznie(ogranicz_do_int(surowa));
    }

    void uniewaznij_zmiane(int stara, int nowa) {
        if (obszar_.szerokosc <= 0 || obszar_.wysokosc <= 0) {
            uniewaznij();
            return;
        }
        Geometria a = oblicz_geometrie(stara);
        Geometria b = oblicz_geometrie(nowa);
        Prostokat brudny = a.uchwyt.suma(b.uchwyt).suma(a.wypelnienie).suma(b.wypelnienie);
        if (czy_pokazuje_wartosc_) brudny = brudny.suma(a.wartosc).suma(b.wartosc);
        int obwodka = (skala_dpi_) * 2;
        uniewaznij_obszar(brudny.powiekszony(Marginesy(obwodka)));
    }

    KierunekKlawisza rozpoznaj_klawisz(uint32_t kod) const {
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if (usb) {
            if (baza == 74U) return KierunekKlawisza::Minimum;       // Home
            if (baza == 77U) return KierunekKlawisza::Maksimum;      // End
            if (baza == 75U) return KierunekKlawisza::StronaWieksza; // PageUp
            if (baza == 78U) return KierunekKlawisza::StronaMniejsza;// PageDown
            if (orientacja_ == Orientacja::Pozioma) {
                if (baza == 80U) return KierunekKlawisza::Mniejsza;  // Left
                if (baza == 79U) return KierunekKlawisza::Wieksza;   // Right
            } else {
                if (baza == 82U) return KierunekKlawisza::Wieksza;   // Up
                if (baza == 81U) return KierunekKlawisza::Mniejsza;  // Down
            }
            return KierunekKlawisza::Brak;
        }

        if (e0 || kod <= 0x7FU) {
            if (baza == 0x47U) return KierunekKlawisza::Minimum;
            if (baza == 0x4FU) return KierunekKlawisza::Maksimum;
            if (baza == 0x49U) return KierunekKlawisza::StronaWieksza;
            if (baza == 0x51U) return KierunekKlawisza::StronaMniejsza;
            if (orientacja_ == Orientacja::Pozioma) {
                if (baza == 0x4BU) return KierunekKlawisza::Mniejsza;
                if (baza == 0x4DU) return KierunekKlawisza::Wieksza;
            } else {
                if (baza == 0x48U) return KierunekKlawisza::Wieksza;
                if (baza == 0x50U) return KierunekKlawisza::Mniejsza;
            }
        }
        return KierunekKlawisza::Brak;
    }

    int minimum_{0};
    int maksimum_{100};
    int wartosc_{0};
    int krok_{1};
    Orientacja orientacja_{Orientacja::Pozioma};
    bool czy_pokazuje_wartosc_{false};
    uint32_t kolor_toru_{Kolory::PowierzchniaAlternatywna};
    uint32_t kolor_wypelnienia_{Kolory::AkcentWcisniety};
    uint32_t kolor_uchwytu_{Kolory::AkcentZloty};
    int rozmiar_uchwytu_{18};
    bool czy_przeciagany_{false};
    bool czy_interakcja_myszy_{false};
    int przesuniecie_chwytu_{0};
    FunkcjaWartosci cb_wartosc_{nullptr};
    void* dane_wartosci_{nullptr};
    FunkcjaWartosci cb_zatwierdzenie_{nullptr};
    void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
