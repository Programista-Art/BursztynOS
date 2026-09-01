#pragma once

#include "kontrolka.h"
#include "model_przewijania.h"

namespace BursztynUI {

class PasekPrzewijania : public Kontrolka {
public:
    using FunkcjaPrzewijania = void (*)(PasekPrzewijania* nadawca,
                                        int64_t wartosc,
                                        void* dane_uzytkownika);

    PasekPrzewijania() {
        styl_ = Motyw::pobierz_domyslny().styl_suwaka;
        wypelnienie_ = Marginesy(1, 1, 1, 1);
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        model_.przy_zmianie(obsluz_zmiane_modelu, this);
        model_.przy_zmianie_geometrii(obsluz_zmiane_geometrii_modelu, this);
    }

    ModelPrzewijania& model() { return model_; }
    const ModelPrzewijania& model() const { return model_; }

    void ustaw_orientacje(Orientacja orientacja) {
        if (orientacja_ == orientacja) return;
        orientacja_ = orientacja;
        uniewaznij();
    }
    Orientacja orientacja() const { return orientacja_; }

    void ustaw_zakres(int64_t minimum, int64_t maksimum) {
        Geometria stara = oblicz_geometrie();
        model_.ustaw_zakres(minimum, maksimum);
        uniewaznij_zmiane_geometrii(stara, oblicz_geometrie());
    }
    int64_t minimum() const { return model_.minimum(); }
    int64_t maksimum() const { return model_.maksimum(); }

    void ustaw_wartosc(int64_t wartosc) { model_.ustaw_wartosc(wartosc); }
    int64_t wartosc() const { return model_.wartosc(); }

    void ustaw_rozmiar_strony(int64_t rozmiar) {
        Geometria stara = oblicz_geometrie();
        model_.ustaw_rozmiar_obszaru(rozmiar);
        uniewaznij_zmiane_geometrii(stara, oblicz_geometrie());
    }
    int64_t rozmiar_strony() const { return model_.rozmiar_obszaru(); }

    void ustaw_maly_krok(int64_t krok) { model_.ustaw_maly_krok(krok); }
    int64_t maly_krok() const { return model_.maly_krok(); }

    void ustaw_duzy_krok(int64_t krok) { model_.ustaw_duzy_krok(krok); }
    int64_t duzy_krok() const { return model_.duzy_krok(); }

    bool przewin_o(int64_t delta) { return model_.przewin_o(delta); }
    bool przewin_strone(int kierunek) { return model_.przewin_strone(kierunek); }

    void pokaz_strzalki(bool pokaz) {
        if (czy_pokazuje_strzalki_ == pokaz) return;
        czy_pokazuje_strzalki_ = pokaz;
        uniewaznij();
    }
    bool czy_pokazuje_strzalki() const { return czy_pokazuje_strzalki_; }

    void ustaw_podswietlanie_toru(bool wlaczone) {
        if (czy_podswietla_tor_ == wlaczone) return;
        Prostokat stary = prostokat_czesci(czesc_najechana_, oblicz_geometrie());
        czy_podswietla_tor_ = wlaczone;
        if (!stary.czy_pusty()) uniewaznij_obszar(stary);
    }
    bool czy_podswietla_tor() const { return czy_podswietla_tor_; }

    void przy_przewijaniu(FunkcjaPrzewijania cb, void* dane = nullptr) {
        cb_przewijanie_ = cb;
        dane_przewijania_ = dane;
    }
    void przy_zatwierdzeniu(FunkcjaPrzewijania cb, void* dane = nullptr) {
        cb_zatwierdzenie_ = cb;
        dane_zatwierdzenia_ = dane;
    }

    // Ten sam prostokat jest zwracany testom i uzywany w rysowaniu/hit-test.
    Prostokat prostokat_uchwytu() const { return oblicz_geometrie().uchwyt; }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        int skala = skala_dpi_ < 1 ? 1 : skala_dpi_;
        return orientacja_ == Orientacja::Pozioma
             ? Rozmiar(160 * skala, 18 * skala)
             : Rozmiar(18 * skala, 160 * skala);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Geometria g = oblicz_geometrie();
        ostatni_uchwyt_ = g.uchwyt;
        int skala = skala_dpi_ < 1 ? 1 : skala_dpi_;
        Prostokat zewnetrzny(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        ctx.rysuj_prostokat(zewnetrzny,
                            czy_wlaczona_ ? Kolory::PowierzchniaTlo
                                          : Kolory::WylaczonyTlo);
        ctx.rysuj_ramke(zewnetrzny,
                        czy_wlaczona_ ? Kolory::RamkaDomyslna
                                      : Kolory::WylaczonyRamka,
                        skala);
        uint32_t tor = czy_wlaczona_ ? Kolory::PowierzchniaAlternatywna
                                     : Kolory::WylaczonyTlo;
        if (!g.tor.czy_pusty()) ctx.rysuj_prostokat(g.tor, tor);

        if (czy_pokazuje_strzalki_) {
            rysuj_przycisk_strzalki(ctx, g.zmniejsz, true,
                                    czesc_najechana_ == Czesc::Zmniejsz,
                                    czesc_wcisnieta_ == Czesc::Zmniejsz);
            rysuj_przycisk_strzalki(ctx, g.zwieksz, false,
                                    czesc_najechana_ == Czesc::Zwieksz,
                                    czesc_wcisnieta_ == Czesc::Zwieksz);
        }

        uint32_t kolor_uchwytu = czy_wlaczona_ ? Kolory::AkcentZloty
                                               : Kolory::WylaczonyTekst;
        if (czy_wlaczona_ && czesc_wcisnieta_ == Czesc::Uchwyt)
            kolor_uchwytu = Kolory::AkcentWcisniety;
        else if (czy_wlaczona_ && czesc_najechana_ == Czesc::Uchwyt)
            kolor_uchwytu = Kolory::AkcentNajechany;
        ctx.rysuj_zaokraglony_prostokat(g.uchwyt, kolor_uchwytu, 3 * skala_dpi_);
        ctx.rysuj_zaokraglona_ramke(g.uchwyt,
                                   czy_wlaczona_ ? Kolory::AkcentRamka
                                                 : Kolory::WylaczonyRamka,
                                   3 * skala_dpi_, skala_dpi_);

        if (czy_podswietla_tor_ && czy_wlaczona_ &&
            (czesc_najechana_ == Czesc::TorPrzed ||
             czesc_najechana_ == Czesc::TorZa)) {
            Prostokat r = prostokat_czesci(czesc_najechana_, g);
            ctx.rysuj_ramke(r, Kolory::RamkaNajechana, skala_dpi_);
        }

        if (czy_ma_fokus_ && czy_wlaczona_) {
            ctx.rysuj_ramke(zewnetrzny.zmniejszony(Marginesy(skala)),
                            Kolory::AkcentFokus, skala);
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Geometria g = oblicz_geometrie();
        Punkt p(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        if (czy_przeciagany_) {
            ustaw_z_piksela(wspolrzedna_osi(p) - przesuniecie_przeciagania_, g);
            e.obsluzone = true;
        }
        ustaw_czesc_najechana(czesc_w_punkcie(p, g), g);
        Kontrolka::obsluz_ruch_myszy(e);
    }

    void obsluz_opuszczenie_myszy() override {
        Geometria g = oblicz_geometrie();
        ustaw_czesc_najechana(Czesc::Brak, g);
        Kontrolka::obsluz_opuszczenie_myszy();
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        Geometria g = oblicz_geometrie();
        Punkt p(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        Czesc trafiona = czesc_w_punkcie(p, g);
        if (trafiona == Czesc::Brak) return;
        ustaw_czesc_wcisnieta(trafiona, g);
        czy_interakcja_myszy_ = true;
        if (trafiona == Czesc::Uchwyt) {
            czy_przeciagany_ = true;
            int poczatek = orientacja_ == Orientacja::Pozioma ? g.uchwyt.x : g.uchwyt.y;
            przesuniecie_przeciagania_ = wspolrzedna_osi(p) - poczatek;
        } else if (trafiona == Czesc::Zmniejsz) {
            model_.przewin_maly_krok(-1);
        } else if (trafiona == Czesc::Zwieksz) {
            model_.przewin_maly_krok(1);
        } else if (trafiona == Czesc::TorPrzed) {
            model_.przewin_strone(-1);
        } else if (trafiona == Czesc::TorZa) {
            model_.przewin_strone(1);
        }
        e.obsluzone = true;
        if (cb_wcisniecie_myszy_) cb_wcisniecie_myszy_(this, e, dane_wcisniecia_myszy_);
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (!e.czy_lewy_przycisk()) return;
        bool zatwierdz = czy_interakcja_myszy_;
        Geometria g = oblicz_geometrie();
        czy_przeciagany_ = false;
        czy_interakcja_myszy_ = false;
        ustaw_czesc_wcisnieta(Czesc::Brak, g);
        Punkt p(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        ustaw_czesc_najechana(czesc_w_punkcie(p, g), g);
        if (zatwierdz) {
            e.obsluzone = true;
            powiadom_o_zatwierdzeniu();
        }
        if (cb_zwolnienie_myszy_) cb_zwolnienie_myszy_(this, e, dane_zwolnienia_myszy_);
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || (!czy_ma_fokus_ && !czy_najechana_)) return;
        int delta = 0;
        if (orientacja_ == Orientacja::Pionowa) delta = e.delta_y;
        else delta = e.delta_x != 0 ? -e.delta_x : e.delta_y;
        if (delta == 0) return;
        model_.przewin_maly_krok(delta > 0 ? -1 : 1);
        powiadom_o_zatwierdzeniu();
        e.obsluzone = true;
        Kontrolka::obsluz_rolke(e);
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        AkcjaKlawisza akcja = rozpoznaj_klawisz(e.kod_klawisza);
        bool obsluzone = true;
        switch (akcja) {
            case AkcjaKlawisza::Mniej: model_.przewin_maly_krok(-1); break;
            case AkcjaKlawisza::Wiecej: model_.przewin_maly_krok(1); break;
            case AkcjaKlawisza::StronaMniej: model_.przewin_strone(-1); break;
            case AkcjaKlawisza::StronaWiecej: model_.przewin_strone(1); break;
            case AkcjaKlawisza::Minimum: model_.ustaw_wartosc(model_.minimum()); break;
            case AkcjaKlawisza::Maksimum: model_.ustaw_wartosc(model_.maksimum()); break;
            default: obsluzone = false; break;
        }
        if (obsluzone) {
            e.obsluzone = true;
            powiadom_o_zatwierdzeniu();
            return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

    void obsluz_utrate_skupienia() override {
        Geometria g = oblicz_geometrie();
        czy_przeciagany_ = false;
        czy_interakcja_myszy_ = false;
        ustaw_czesc_wcisnieta(Czesc::Brak, g);
        Kontrolka::obsluz_utrate_skupienia();
    }

private:
    enum class Czesc { Brak, Zmniejsz, TorPrzed, Uchwyt, TorZa, Zwieksz };
    enum class AkcjaKlawisza {
        Brak, Mniej, Wiecej, StronaMniej, StronaWiecej, Minimum, Maksimum
    };
    struct Geometria {
        Prostokat zmniejsz;
        Prostokat tor;
        Prostokat tor_przed;
        Prostokat uchwyt;
        Prostokat tor_za;
        Prostokat zwieksz;
        int poczatek_toru{0};
        int dlugosc_toru{0};
        int droga_uchwytu{0};
    };

    static void obsluz_zmiane_modelu(ModelPrzewijania*, int64_t,
                                     void* dane) {
        PasekPrzewijania* pasek = static_cast<PasekPrzewijania*>(dane);
        if (!pasek) return;
        Geometria nowa = pasek->oblicz_geometrie();
        Prostokat brudny = pasek->ostatni_uchwyt_.czy_pusty()
                         ? nowa.uchwyt : pasek->ostatni_uchwyt_.suma(nowa.uchwyt);
        pasek->ostatni_uchwyt_ = nowa.uchwyt;
        pasek->uniewaznij_obszar(brudny.powiekszony(Marginesy(2 * pasek->skala_dpi_)));
        if (pasek->cb_przewijanie_)
            pasek->cb_przewijanie_(pasek, pasek->wartosc(), pasek->dane_przewijania_);
    }

    static void obsluz_zmiane_geometrii_modelu(ModelPrzewijania*, int64_t,
                                                void* dane) {
        PasekPrzewijania* pasek = static_cast<PasekPrzewijania*>(dane);
        if (!pasek) return;
        Geometria nowa = pasek->oblicz_geometrie();
        Prostokat r = pasek->ostatni_uchwyt_.suma(nowa.uchwyt);
        pasek->ostatni_uchwyt_ = nowa.uchwyt;
        if (!r.czy_pusty())
            pasek->uniewaznij_obszar(r.powiekszony(Marginesy(2 * pasek->skala_dpi_)));
    }

    static SzerokaLiczbaBezZnaku szerokosc_zakresu(int64_t minimum, int64_t maksimum) {
        return static_cast<SzerokaLiczbaBezZnaku>(
            static_cast<SzerokaLiczba>(maksimum) - static_cast<SzerokaLiczba>(minimum));
    }

    // Freestanding Ring3 nie linkuje libgcc (__udivti3). Dzielenie szerokiego
    // licznika wykonujemy lokalnie, bez alokacji i bez zaleznosci runtime.
    static SzerokaLiczbaBezZnaku podziel_szeroko(
        SzerokaLiczbaBezZnaku licznik,
        SzerokaLiczbaBezZnaku mianownik) {
        if (mianownik == 0) return 0;
        SzerokaLiczbaBezZnaku wynik = 0;
        SzerokaLiczbaBezZnaku reszta = 0;
        for (int bit = 127; bit >= 0; --bit) {
            reszta = (reszta << 1) | ((licznik >> bit) & 1);
            if (reszta >= mianownik) {
                reszta -= mianownik;
                wynik |= static_cast<SzerokaLiczbaBezZnaku>(1) << bit;
            }
        }
        return wynik;
    }

    static int proporcja_pikseli(int dlugosc,
                                 SzerokaLiczbaBezZnaku czesc,
                                 SzerokaLiczbaBezZnaku calosc) {
        if (dlugosc <= 0 || czesc == 0 || calosc == 0) return 0;
        SzerokaLiczbaBezZnaku licznik =
            static_cast<SzerokaLiczbaBezZnaku>(dlugosc) * czesc + calosc / 2;
        SzerokaLiczbaBezZnaku wynik = podziel_szeroko(licznik, calosc);
        if (wynik > static_cast<SzerokaLiczbaBezZnaku>(dlugosc)) return dlugosc;
        return static_cast<int>(wynik);
    }

    int wspolrzedna_osi(Punkt p) const {
        return orientacja_ == Orientacja::Pozioma ? p.x : p.y;
    }

    Geometria oblicz_geometrie() const {
        Geometria g;
        int skala = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int pl = wypelnienie_.lewy * skala;
        int pg = wypelnienie_.gorny * skala;
        int pp = wypelnienie_.prawy * skala;
        int pd = wypelnienie_.dolny * skala;
        int glowna = orientacja_ == Orientacja::Pozioma
                   ? obszar_.szerokosc - pl - pp : obszar_.wysokosc - pg - pd;
        int poprzeczna = orientacja_ == Orientacja::Pozioma
                       ? obszar_.wysokosc - pg - pd : obszar_.szerokosc - pl - pp;
        if (glowna < 0) glowna = 0;
        if (poprzeczna < 0) poprzeczna = 0;
        int strzalka = czy_pokazuje_strzalki_ ? poprzeczna : 0;
        if (strzalka * 2 > glowna) strzalka = glowna / 2;
        int poczatek = orientacja_ == Orientacja::Pozioma ? pl : pg;
        int tor_start = poczatek + strzalka;
        int tor_len = glowna - 2 * strzalka;
        if (tor_len < 0) tor_len = 0;
        int min_uchwyt = 12 * skala;
        if (min_uchwyt > tor_len) min_uchwyt = tor_len;

        SzerokaLiczbaBezZnaku zakres = szerokosc_zakresu(model_.minimum(), model_.maksimum());
        SzerokaLiczbaBezZnaku strona = static_cast<SzerokaLiczbaBezZnaku>(model_.rozmiar_obszaru());
        int uchwyt_len = tor_len;
        if (zakres != 0) {
            SzerokaLiczbaBezZnaku calosc = zakres + strona;
            if (strona == 0) uchwyt_len = min_uchwyt;
            else uchwyt_len = proporcja_pikseli(tor_len, strona, calosc);
            if (uchwyt_len < min_uchwyt) uchwyt_len = min_uchwyt;
            if (uchwyt_len > tor_len) uchwyt_len = tor_len;
        }
        int droga = tor_len - uchwyt_len;
        int od = 0;
        if (droga > 0 && zakres != 0) {
            SzerokaLiczbaBezZnaku pozycja = static_cast<SzerokaLiczbaBezZnaku>(
                static_cast<SzerokaLiczba>(model_.wartosc()) - static_cast<SzerokaLiczba>(model_.minimum()));
            od = proporcja_pikseli(droga, pozycja, zakres);
        }

        if (orientacja_ == Orientacja::Pozioma) {
            g.zmniejsz = Prostokat(pl, pg, strzalka, poprzeczna);
            g.tor = Prostokat(tor_start, pg, tor_len, poprzeczna);
            g.uchwyt = Prostokat(tor_start + od, pg, uchwyt_len, poprzeczna);
            g.zwieksz = Prostokat(tor_start + tor_len, pg, strzalka, poprzeczna);
            g.tor_przed = Prostokat(tor_start, pg, od, poprzeczna);
            int za_x = g.uchwyt.prawy();
            g.tor_za = Prostokat(za_x, pg, tor_start + tor_len - za_x, poprzeczna);
        } else {
            g.zmniejsz = Prostokat(pl, pg, poprzeczna, strzalka);
            g.tor = Prostokat(pl, tor_start, poprzeczna, tor_len);
            g.uchwyt = Prostokat(pl, tor_start + od, poprzeczna, uchwyt_len);
            g.zwieksz = Prostokat(pl, tor_start + tor_len, poprzeczna, strzalka);
            g.tor_przed = Prostokat(pl, tor_start, poprzeczna, od);
            int za_y = g.uchwyt.dolny();
            g.tor_za = Prostokat(pl, za_y, poprzeczna, tor_start + tor_len - za_y);
        }
        g.poczatek_toru = tor_start;
        g.dlugosc_toru = tor_len;
        g.droga_uchwytu = droga;
        return g;
    }

    Czesc czesc_w_punkcie(Punkt p, const Geometria& g) const {
        if (!czy_wlaczona_) return Czesc::Brak;
        if (czy_pokazuje_strzalki_ && g.zmniejsz.zawiera(p)) return Czesc::Zmniejsz;
        if (czy_pokazuje_strzalki_ && g.zwieksz.zawiera(p)) return Czesc::Zwieksz;
        if (g.uchwyt.zawiera(p)) return Czesc::Uchwyt;
        if (g.tor_przed.zawiera(p)) return Czesc::TorPrzed;
        if (g.tor_za.zawiera(p)) return Czesc::TorZa;
        return Czesc::Brak;
    }

    Prostokat prostokat_czesci(Czesc czesc, const Geometria& g) const {
        switch (czesc) {
            case Czesc::Zmniejsz: return g.zmniejsz;
            case Czesc::TorPrzed: return g.tor_przed;
            case Czesc::Uchwyt: return g.uchwyt;
            case Czesc::TorZa: return g.tor_za;
            case Czesc::Zwieksz: return g.zwieksz;
            default: return Prostokat();
        }
    }

    void ustaw_czesc_najechana(Czesc nowa, const Geometria& g) {
        if (czesc_najechana_ == nowa) return;
        Prostokat stary_r = prostokat_czesci(czesc_najechana_, g);
        Prostokat nowy_r = prostokat_czesci(nowa, g);
        czesc_najechana_ = nowa;
        Prostokat brudny = stary_r.suma(nowy_r);
        if (!brudny.czy_pusty()) uniewaznij_obszar(brudny);
    }

    void ustaw_czesc_wcisnieta(Czesc nowa, const Geometria& g) {
        if (czesc_wcisnieta_ == nowa) return;
        Prostokat stary_r = prostokat_czesci(czesc_wcisnieta_, g);
        Prostokat nowy_r = prostokat_czesci(nowa, g);
        czesc_wcisnieta_ = nowa;
        Prostokat brudny = stary_r.suma(nowy_r);
        if (!brudny.czy_pusty()) uniewaznij_obszar(brudny);
    }

    void ustaw_z_piksela(int piksel, const Geometria& g) {
        if (g.droga_uchwytu <= 0 || model_.minimum() == model_.maksimum()) {
            model_.ustaw_wartosc(model_.minimum());
            return;
        }
        int od = piksel - g.poczatek_toru;
        od = przytnij(od, 0, g.droga_uchwytu);
        SzerokaLiczbaBezZnaku zakres = szerokosc_zakresu(model_.minimum(), model_.maksimum());
        SzerokaLiczbaBezZnaku licznik = zakres * static_cast<SzerokaLiczbaBezZnaku>(od);
        SzerokaLiczbaBezZnaku przesuniecie = podziel_szeroko(
            licznik + static_cast<unsigned>(g.droga_uchwytu / 2),
            static_cast<unsigned>(g.droga_uchwytu));
        SzerokaLiczba wynik = static_cast<SzerokaLiczba>(model_.minimum()) +
                              static_cast<SzerokaLiczba>(przesuniecie);
        if (wynik > static_cast<SzerokaLiczba>(INT64_MAX)) wynik = INT64_MAX;
        model_.ustaw_wartosc(static_cast<int64_t>(wynik));
    }

    void uniewaznij_zmiane_geometrii(const Geometria& stara, const Geometria& nowa) {
        Prostokat r = stara.uchwyt.suma(nowa.uchwyt);
        if (!r.czy_pusty()) uniewaznij_obszar(r.powiekszony(Marginesy(2 * skala_dpi_)));
        ostatni_uchwyt_ = nowa.uchwyt;
    }

    void rysuj_przycisk_strzalki(KontekstRysowania& ctx, Prostokat r,
                                  bool zmniejsz, bool hover, bool pressed) const {
        if (r.czy_pusty()) return;
        uint32_t tlo = czy_wlaczona_ ? Kolory::PowierzchniaTlo : Kolory::WylaczonyTlo;
        if (czy_wlaczona_ && pressed) tlo = Kolory::PowierzchniaAktywna;
        else if (czy_wlaczona_ && hover) tlo = Kolory::PowierzchniaNajechana;
        ctx.rysuj_prostokat(r, tlo);
        ctx.rysuj_ramke(r, czy_wlaczona_ ? Kolory::AkcentRamka : Kolory::WylaczonyRamka,
                        skala_dpi_);
        uint32_t kolor = czy_wlaczona_ ? Kolory::AkcentZloty : Kolory::WylaczonyTekst;
        int roz = (orientacja_ == Orientacja::Pozioma ? r.wysokosc : r.szerokosc) / 4;
        if (roz < 2) roz = 2;
        int cx = r.x + r.szerokosc / 2;
        int cy = r.y + r.wysokosc / 2;
        for (int i = 0; i < roz; ++i) {
            if (orientacja_ == Orientacja::Pozioma) {
                int x = zmniejsz ? cx - roz / 2 + i : cx + roz / 2 - i;
                ctx.rysuj_linie_pionowa(x, cy - i, i * 2 + 1, kolor);
            } else {
                int y = zmniejsz ? cy - roz / 2 + i : cy + roz / 2 - i;
                ctx.rysuj_linie_pozioma(cx - i, y, i * 2 + 1, kolor);
            }
        }
    }

    AkcjaKlawisza rozpoznaj_klawisz(uint32_t kod) const {
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);
        if (usb) {
            if (baza == 74U) return AkcjaKlawisza::Minimum;
            if (baza == 77U) return AkcjaKlawisza::Maksimum;
            if (baza == 75U) return AkcjaKlawisza::StronaMniej;
            if (baza == 78U) return AkcjaKlawisza::StronaWiecej;
            if (orientacja_ == Orientacja::Pozioma) {
                if (baza == 80U) return AkcjaKlawisza::Mniej;
                if (baza == 79U) return AkcjaKlawisza::Wiecej;
            } else {
                if (baza == 82U) return AkcjaKlawisza::Mniej;
                if (baza == 81U) return AkcjaKlawisza::Wiecej;
            }
            return AkcjaKlawisza::Brak;
        }
        if (e0 || kod <= 0x7FU) {
            if (baza == 0x47U) return AkcjaKlawisza::Minimum;
            if (baza == 0x4FU) return AkcjaKlawisza::Maksimum;
            if (baza == 0x49U) return AkcjaKlawisza::StronaMniej;
            if (baza == 0x51U) return AkcjaKlawisza::StronaWiecej;
            if (orientacja_ == Orientacja::Pozioma) {
                if (baza == 0x4BU) return AkcjaKlawisza::Mniej;
                if (baza == 0x4DU) return AkcjaKlawisza::Wiecej;
            } else {
                if (baza == 0x48U) return AkcjaKlawisza::Mniej;
                if (baza == 0x50U) return AkcjaKlawisza::Wiecej;
            }
        }
        return AkcjaKlawisza::Brak;
    }

    void powiadom_o_zatwierdzeniu() {
        if (cb_zatwierdzenie_) cb_zatwierdzenie_(this, wartosc(), dane_zatwierdzenia_);
    }

    ModelPrzewijania model_;
    Orientacja orientacja_{Orientacja::Pozioma};
    bool czy_pokazuje_strzalki_{true};
    bool czy_podswietla_tor_{false};
    bool czy_przeciagany_{false};
    bool czy_interakcja_myszy_{false};
    int przesuniecie_przeciagania_{0};
    Czesc czesc_najechana_{Czesc::Brak};
    Czesc czesc_wcisnieta_{Czesc::Brak};
    mutable Prostokat ostatni_uchwyt_;
    FunkcjaPrzewijania cb_przewijanie_{nullptr};
    void* dane_przewijania_{nullptr};
    FunkcjaPrzewijania cb_zatwierdzenie_{nullptr};
    void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
