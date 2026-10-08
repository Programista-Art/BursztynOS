#pragma once

#include "okno_dialogowe.h"
#include "selektor_koloru.h"
#include "pole_numeryczne.h"
#include "pole_tekstowe.h"
#include "etykieta.h"
#include "panel.h"
#include "przycisk.h"

namespace BursztynUI {

enum class WynikDialoguKoloru : uint8_t {
    Brak = 0,
    OK = 1,
    Anuluj = 2,
    Wybrano = 1,
    Anulowano = 2
};

class PodgladKoloruDialogu : public Kontrolka {
public:
    PodgladKoloruDialogu() {
        czy_moze_miec_fokus_ = false;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        preferowany_rozmiar_ = Rozmiar(240, 56);
    }

    uint32_t kolor_poczatkowy() const { return stary_; }
    uint32_t kolor_biezacy() const { return nowy_; }

    void ustaw_kolory(uint32_t stary, uint32_t nowy) {
        stary_ = stary & 0x00FFFFFFU;
        nowy_ = nowy & 0x00FFFFFFU;
        uniewaznij();
    }

    void ustaw_nowy(uint32_t nowy) {
        nowy_ = nowy & 0x00FFFFFFU;
        uniewaznij();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        int promien = motyw_promien(MOTYW_ROG_PANEL) * s;
        ctx.rysuj_zaokraglony_prostokat(b, motyw_kolor(MOTYW_KOLOR_PANEL), promien);
        ctx.rysuj_zaokraglona_ramke(b, motyw_kolor(MOTYW_KOLOR_RAMKA), promien, 1 * s);

        int pol_w = (b.szerokosc - 24 * s) / 2;
        if (pol_w < 20 * s) pol_w = 20 * s;
        int h = b.wysokosc - 22 * s;
        if (h < 12 * s) h = 12 * s;

        // Podglad Nowy
        Prostokat r_nowy(8 * s, 16 * s, pol_w, h);
        ctx.rysuj_tekst(Punkt(8 * s, 2 * s), "Nowy", motyw_kolor(MOTYW_KOLOR_TEKST), s);
        ctx.rysuj_zaokraglony_prostokat(r_nowy, nowy_ ? nowy_ : SelektorKoloru::WIDOCZNA_CZERN, motyw_promien(MOTYW_ROG_POLE) * s);
        ctx.rysuj_zaokraglona_ramke(r_nowy, motyw_kolor(MOTYW_KOLOR_RAMKA), motyw_promien(MOTYW_ROG_POLE) * s, 1 * s);

        // Podglad Stary
        Prostokat r_stary(r_nowy.prawy() + 8 * s, 16 * s, pol_w, h);
        ctx.rysuj_tekst(Punkt(r_stary.x, 2 * s), "Poprzedni", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);
        ctx.rysuj_zaokraglony_prostokat(r_stary, stary_ ? stary_ : SelektorKoloru::WIDOCZNA_CZERN, motyw_promien(MOTYW_ROG_POLE) * s);
        ctx.rysuj_zaokraglona_ramke(r_stary, motyw_kolor(MOTYW_KOLOR_RAMKA), motyw_promien(MOTYW_ROG_POLE) * s, 1 * s);
    }

private:
    uint32_t stary_{0};
    uint32_t nowy_{0};
};

class DialogKoloru : public OknoDialogowe {
public:
    using FunkcjaWynikuKoloru = void (*)(DialogKoloru* dialog, WynikDialoguKoloru wynik,
                                         uint32_t kolor, void* dane);

    explicit DialogKoloru(uint32_t kolor_poczatkowy = 0x00F2A900U,
                          const char* tytul = "Wybór koloru")
        : OknoDialogowe(tytul, "", PrzyciskiDialogu::Wlasne, TypDialogu::Zwykly),
          kolor_poczatkowy_(kolor_poczatkowy & 0x00FFFFFFU),
          kolor_biezacy_(kolor_poczatkowy & 0x00FFFFFFU) {
        if (!tytul || !tytul[0]) ustaw_tekst("Wybór koloru");
    }

    ~DialogKoloru() override = default;

    uint32_t kolor_poczatkowy() const { return kolor_poczatkowy_; }
    uint32_t kolor() const { return kolor_biezacy_; }
    uint32_t pobierz_kolor() const { return kolor_biezacy_; }

    void ustaw_kolor(uint32_t c) {
        c &= 0x00FFFFFFU;
        kolor_biezacy_ = c;
        if (!blokada_sync_) {
            blokada_sync_ = true;
            if (selektor_) selektor_->ustaw_kolor(c, false);
            aktualizuj_kontrolki_z_koloru(c);
            blokada_sync_ = false;
        }
    }

    WynikDialoguKoloru wynik_koloru() const { return wynik_koloru_; }

    void przy_wyniku_koloru(FunkcjaWynikuKoloru cb, void* dane = nullptr) {
        cb_wynik_koloru_ = cb;
        dane_wyniku_koloru_ = dane;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        uint32_t kod = e.polecenie();

        // Escape: anuluj
        if (kod == 27U) {
            anuluj();
            e.obsluzone = true;
            return;
        }

        // Enter: zatwierdz
        if (kod == 13U) {
            zatwierdz();
            e.obsluzone = true;
            return;
        }

        // Tab / Shift+Tab: przelaczanie skupienia pomiedzy kontrolkami
        const bool tab = kod == 9U;
        if (tab) {
            nawiguj_fokus(!e.czy_shift());
            e.obsluzone = true;
            return;
        }

        OknoDialogowe::obsluz_wcisniecie_klawisza(e);
    }

    void zatwierdz() {
        if (pole_hex_) {
            uint32_t parsed = 0;
            if (parsuj_hex(pole_hex_->tekst(), &parsed)) {
                kolor_biezacy_ = parsed;
            }
        }
        wynik_koloru_ = WynikDialoguKoloru::OK;
        uint32_t wygrany = kolor_biezacy_;
        if (cb_wynik_koloru_) {
            cb_wynik_koloru_(this, WynikDialoguKoloru::OK, wygrany, dane_wyniku_koloru_);
        }
        zamknij_z_wynikiem(WynikDialogu::OK);
    }

    void anuluj() {
        kolor_biezacy_ = kolor_poczatkowy_;
        wynik_koloru_ = WynikDialoguKoloru::Anuluj;
        if (cb_wynik_koloru_) {
            cb_wynik_koloru_(this, WynikDialoguKoloru::Anuluj, kolor_poczatkowy_, dane_wyniku_koloru_);
        }
        zamknij_z_wynikiem(WynikDialogu::Anuluj);
    }

    void zamknij_z_wynikiem(WynikDialogu w) override {
        if (w == WynikDialogu::Anuluj || w == WynikDialogu::Nie) {
            kolor_biezacy_ = kolor_poczatkowy_;
            wynik_koloru_ = WynikDialoguKoloru::Anuluj;
            if (cb_wynik_koloru_) {
                cb_wynik_koloru_(this, WynikDialoguKoloru::Anuluj, kolor_poczatkowy_, dane_wyniku_koloru_);
            }
        }
        OknoDialogowe::zamknij_z_wynikiem(w);
    }

    void po_zamknieciu(PowodZamknieciaPopup powod) override {
        if (powod == PowodZamknieciaPopup::Anulowanie || powod == PowodZamknieciaPopup::Escape) {
            if (wynik_koloru_ == WynikDialoguKoloru::Brak) {
                kolor_biezacy_ = kolor_poczatkowy_;
                wynik_koloru_ = WynikDialoguKoloru::Anuluj;
                if (cb_wynik_koloru_) {
                    cb_wynik_koloru_(this, WynikDialoguKoloru::Anuluj, kolor_poczatkowy_, dane_wyniku_koloru_);
                }
            }
        }
        OknoDialogowe::po_zamknieciu(powod);
    }

    static bool parsuj_hex(const char* t, uint32_t* out) {
        if (!t || !out) return false;
        while (*t == ' ') ++t;
        int start = (t[0] == '#') ? 1 : 0;
        uint32_t v = 0;
        int count = 0;
        for (int i = start; t[i] != '\0' && t[i] != ' '; ++i) {
            char c = t[i];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;

            v = (v << 4) | static_cast<uint32_t>(d);
            ++count;
            if (count > 6) return false;
        }
        if (count != 6) return false;
        *out = v & 0x00FFFFFFU;
        return true;
    }

    static void formatuj_hex(uint32_t c, char* b, bool z_kratka = true) {
        if (!b) return;
        static const char h[] = "0123456789ABCDEF";
        int idx = 0;
        if (z_kratka) b[idx++] = '#';
        for (int i = 0; i < 6; ++i) {
            b[idx++] = h[(c >> ((5 - i) * 4)) & 15];
        }
        b[idx] = '\0';
    }

    using OknoDialogowe::pokaz;

    static DialogKoloru* wybierz(ZarzadcaPopupow& z, uint32_t startowy = 0x00F2A900U,
                                 FunkcjaWynikuKoloru cb = nullptr, void* dane = nullptr,
                                 Kontrolka* owner = nullptr, const char* tytul = "Wybór koloru") {
        DialogKoloru* dlg = new DialogKoloru(startowy, tytul);
        dlg->przy_wyniku_koloru(cb, dane);
        dlg->OknoDialogowe::pokaz(z, owner);
        return dlg;
    }

    static DialogKoloru* wybierz_kolor(ZarzadcaPopupow& z, uint32_t startowy = 0x00F2A900U,
                                       FunkcjaWynikuKoloru cb = nullptr, void* dane = nullptr,
                                       Kontrolka* owner = nullptr, const char* tytul = "Wybór koloru") {
        return wybierz(z, startowy, cb, dane, owner, tytul);
    }

    // Bezposredni dostep do kontrolek w testach i zaawansowanym uzyciu
    SelektorKoloru* selektor() const { return selektor_; }
    PoleNumeryczne* pole_r() const { return pole_r_; }
    PoleNumeryczne* pole_g() const { return pole_g_; }
    PoleNumeryczne* pole_b() const { return pole_b_; }
    PoleTekstowe* pole_hex() const { return pole_hex_; }
    PodgladKoloruDialogu* podglad() const { return podglad_; }
    Przycisk* przycisk_ok() const { return btn_ok_; }
    Przycisk* przycisk_anuluj() const { return btn_anuluj_; }

protected:
    void oblicz_geometrie_dialogu(int& szer, int& wys) override {
        SkalaUI s = skala_dpi_;
        szer = 500 * s;
        wys = 350 * s;
    }

    void zbuduj_interfejs(int w, int h) override {
        wyczysc_kontrolki();
        wyczysc_sloty_przyciskow();
        SkalaUI s = skala_dpi_;

        uklad_ = new PanelDokowany();
        uklad_->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        uklad_->ustaw_odstep(0);
        wyzeruj_tlo(*uklad_);
        dodaj_kontrolke(uklad_);

        // 1. Dolny pasek przyciskow akcji
        panel_przyciskow_ = new Wiersz();
        panel_przyciskow_->ustaw_preferowany_rozmiar(Rozmiar(-1, 48));
        panel_przyciskow_->ustaw_wypelnienie(Marginesy(16, 8, 16, 10));
        panel_przyciskow_->ustaw_odstep(10);
        panel_przyciskow_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Koniec);
        panel_przyciskow_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        wyzeruj_tlo(*panel_przyciskow_);
        uklad_->dodaj_dokowany(panel_przyciskow_, Dokowanie::Dol);

        Kontrolka* wypychacz = new Kontrolka();
        wypychacz->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        wypychacz->ustaw_mozliwosc_fokusu(false);
        panel_przyciskow_->dodaj_kontrolke(wypychacz);

        btn_ok_ = new Przycisk("OK");
        btn_ok_->ustaw_preferowany_rozmiar(Rozmiar(95, 32));
        panel_przyciskow_->dodaj_kontrolke(btn_ok_);
        zarejestruj_przycisk_akcji(btn_ok_, WynikDialogu::OK, true, false);

        btn_anuluj_ = new Przycisk("Anuluj");
        btn_anuluj_->ustaw_preferowany_rozmiar(Rozmiar(95, 32));
        panel_przyciskow_->dodaj_kontrolke(btn_anuluj_);
        zarejestruj_przycisk_akcji(btn_anuluj_, WynikDialogu::Anuluj, false, true);

        // Nadpisz callbacki przyciskow, by prawidlowo wolaly zatwierdz() i anuluj()
        btn_ok_->przy_kliknieciu([](Przycisk*, void* d) {
            static_cast<DialogKoloru*>(d)->zatwierdz();
        }, this);
        btn_anuluj_->przy_kliknieciu([](Przycisk*, void* d) {
            static_cast<DialogKoloru*>(d)->anuluj();
        }, this);

        // 2. Glowny panel zawartosci: lewa kolumna (Selektor 2D + Podglad), prawa kolumna (RGB + HEX)
        Panel* p_tresci = new Panel(Orientacja::Pozioma);
        p_tresci->ustaw_wypelnienie(Marginesy(16, 10, 16, 8));
        p_tresci->ustaw_odstep(16);
        p_tresci->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        p_tresci->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Rozciagnij);
        wyzeruj_tlo(*p_tresci);
        panel_tresci_ = p_tresci;
        uklad_->dodaj_dokowany(p_tresci, Dokowanie::Wypelnij);

        // Lewa kolumna: Selektor 2D + Podglad koloru
        Kolumna* kol_lewa = new Kolumna();
        kol_lewa->ustaw_preferowany_rozmiar(Rozmiar(250, -1));
        kol_lewa->ustaw_odstep(8);
        kol_lewa->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        p_tresci->dodaj_kontrolke(kol_lewa);

        Etykieta* ety_sel = new Etykieta("Selektor barwy:");
        ety_sel->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));
        kol_lewa->dodaj_kontrolke(ety_sel);

        selektor_ = new SelektorKoloru(TrybSelektoraKoloru::Osadzony, kolor_biezacy_);
        selektor_->ustaw_preferowany_rozmiar(Rozmiar(250, 160));
        selektor_->przy_zmianie([](SelektorKoloru*, uint32_t c, void* d) {
            DialogKoloru* dlg = static_cast<DialogKoloru*>(d);
            if (dlg && !dlg->blokada_sync_) {
                dlg->blokada_sync_ = true;
                dlg->kolor_biezacy_ = c & 0x00FFFFFFU;
                dlg->aktualizuj_kontrolki_z_koloru(dlg->kolor_biezacy_);
                dlg->blokada_sync_ = false;
            }
        }, this);
        kol_lewa->dodaj_kontrolke(selektor_);

        podglad_ = new PodgladKoloruDialogu();
        podglad_->ustaw_kolory(kolor_poczatkowy_, kolor_biezacy_);
        kol_lewa->dodaj_kontrolke(podglad_);

        // Prawa kolumna: Pola R, G, B i HEX
        Kolumna* kol_prawa = new Kolumna();
        kol_prawa->ustaw_preferowany_rozmiar(Rozmiar(190, -1));
        kol_prawa->ustaw_odstep(8);
        kol_prawa->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        p_tresci->dodaj_kontrolke(kol_prawa);

        Etykieta* ety_kanaly = new Etykieta("Wartości RGB i HEX:");
        ety_kanaly->ustaw_kolor_tekstu(motyw_kolor(MOTYW_KOLOR_AKCENT));
        kol_prawa->dodaj_kontrolke(ety_kanaly);

        uint8_t init_r = (kolor_biezacy_ >> 16) & 0xFF;
        uint8_t init_g = (kolor_biezacy_ >> 8) & 0xFF;
        uint8_t init_b = kolor_biezacy_ & 0xFF;

        // R
        Wiersz* w_r = new Wiersz();
        w_r->ustaw_odstep(8);
        w_r->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        Etykieta* ety_r = new Etykieta("R:");
        ety_r->ustaw_preferowany_rozmiar(Rozmiar(24, 28));
        w_r->dodaj_kontrolke(ety_r);
        pole_r_ = new PoleNumeryczne(0, 255, init_r);
        pole_r_->ustaw_preferowany_rozmiar(Rozmiar(130, 28));
        pole_r_->przy_zmianie_wartosci([](PoleNumeryczne*, int64_t, void* d) {
            static_cast<DialogKoloru*>(d)->obsluz_zmiane_rgb();
        }, this);
        w_r->dodaj_kontrolke(pole_r_);
        kol_prawa->dodaj_kontrolke(w_r);

        // G
        Wiersz* w_g = new Wiersz();
        w_g->ustaw_odstep(8);
        w_g->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        Etykieta* ety_g = new Etykieta("G:");
        ety_g->ustaw_preferowany_rozmiar(Rozmiar(24, 28));
        w_g->dodaj_kontrolke(ety_g);
        pole_g_ = new PoleNumeryczne(0, 255, init_g);
        pole_g_->ustaw_preferowany_rozmiar(Rozmiar(130, 28));
        pole_g_->przy_zmianie_wartosci([](PoleNumeryczne*, int64_t, void* d) {
            static_cast<DialogKoloru*>(d)->obsluz_zmiane_rgb();
        }, this);
        w_g->dodaj_kontrolke(pole_g_);
        kol_prawa->dodaj_kontrolke(w_g);

        // B
        Wiersz* w_b = new Wiersz();
        w_b->ustaw_odstep(8);
        w_b->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        Etykieta* ety_b = new Etykieta("B:");
        ety_b->ustaw_preferowany_rozmiar(Rozmiar(24, 28));
        w_b->dodaj_kontrolke(ety_b);
        pole_b_ = new PoleNumeryczne(0, 255, init_b);
        pole_b_->ustaw_preferowany_rozmiar(Rozmiar(130, 28));
        pole_b_->przy_zmianie_wartosci([](PoleNumeryczne*, int64_t, void* d) {
            static_cast<DialogKoloru*>(d)->obsluz_zmiane_rgb();
        }, this);
        w_b->dodaj_kontrolke(pole_b_);
        kol_prawa->dodaj_kontrolke(w_b);

        // HEX
        Wiersz* w_hex = new Wiersz();
        w_hex->ustaw_odstep(8);
        w_hex->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        Etykieta* ety_hex = new Etykieta("HEX:");
        ety_hex->ustaw_preferowany_rozmiar(Rozmiar(36, 28));
        w_hex->dodaj_kontrolke(ety_hex);
        pole_hex_ = new PoleTekstowe("#RRGGBB");
        pole_hex_->ustaw_maksymalna_liczbe_znakow(7);
        pole_hex_->ustaw_preferowany_rozmiar(Rozmiar(118, 28));
        char hbuf[10];
        formatuj_hex(kolor_biezacy_, hbuf, true);
        pole_hex_->ustaw_tekst(hbuf);
        pole_hex_->przy_zmianie([](PoleTekstowe*, const char* txt, void* d) {
            DialogKoloru* dlg = static_cast<DialogKoloru*>(d);
            if (dlg && !dlg->blokada_sync_) {
                uint32_t parsed = 0;
                if (dlg->parsuj_hex(txt, &parsed)) {
                    dlg->blokada_sync_ = true;
                    dlg->kolor_biezacy_ = parsed;
                    if (dlg->selektor_) dlg->selektor_->ustaw_kolor(parsed, false);
                    if (dlg->pole_r_) dlg->pole_r_->ustaw_wartosc((parsed >> 16) & 0xFF);
                    if (dlg->pole_g_) dlg->pole_g_->ustaw_wartosc((parsed >> 8) & 0xFF);
                    if (dlg->pole_b_) dlg->pole_b_->ustaw_wartosc(parsed & 0xFF);
                    if (dlg->podglad_) dlg->podglad_->ustaw_nowy(parsed);
                    dlg->blokada_sync_ = false;
                }
            }
        }, this);
        w_hex->dodaj_kontrolke(pole_hex_);
        kol_prawa->dodaj_kontrolke(w_hex);

        // Tab order
        kontrolki_fokusu_[0] = selektor_;
        kontrolki_fokusu_[1] = pole_r_;
        kontrolki_fokusu_[2] = pole_g_;
        kontrolki_fokusu_[3] = pole_b_;
        kontrolki_fokusu_[4] = pole_hex_;
        kontrolki_fokusu_[5] = btn_ok_;
        kontrolki_fokusu_[6] = btn_anuluj_;
        indeks_fokusu_ = 0;

        uklad_->rozmiesc(Prostokat(0, 32 * s, w, h > 32 * s ? h - 32 * s : 0));
        aktualizuj_obszary_ukladu();
    }

private:
    void obsluz_zmiane_rgb() {
        if (blokada_sync_) return;
        blokada_sync_ = true;
        uint8_t r = static_cast<uint8_t>(przytnij<int64_t>(pole_r_ ? pole_r_->wartosc() : 0, 0, 255));
        uint8_t g = static_cast<uint8_t>(przytnij<int64_t>(pole_g_ ? pole_g_->wartosc() : 0, 0, 255));
        uint8_t b = static_cast<uint8_t>(przytnij<int64_t>(pole_b_ ? pole_b_->wartosc() : 0, 0, 255));
        kolor_biezacy_ = (static_cast<uint32_t>(r) << 16) |
                         (static_cast<uint32_t>(g) << 8) |
                         static_cast<uint32_t>(b);

        if (selektor_) selektor_->ustaw_kolor(kolor_biezacy_, false);
        if (pole_hex_) {
            char buf[10];
            formatuj_hex(kolor_biezacy_, buf, true);
            pole_hex_->ustaw_tekst(buf);
        }
        if (podglad_) podglad_->ustaw_nowy(kolor_biezacy_);
        blokada_sync_ = false;
    }

    void aktualizuj_kontrolki_z_koloru(uint32_t c) {
        uint8_t r = (c >> 16) & 0xFF;
        uint8_t g = (c >> 8) & 0xFF;
        uint8_t b = c & 0xFF;

        if (pole_r_) pole_r_->ustaw_wartosc(r);
        if (pole_g_) pole_g_->ustaw_wartosc(g);
        if (pole_b_) pole_b_->ustaw_wartosc(b);
        if (pole_hex_) {
            char buf[10];
            formatuj_hex(c, buf, true);
            pole_hex_->ustaw_tekst(buf);
        }
        if (podglad_) podglad_->ustaw_nowy(c);
    }

    void nawiguj_fokus(bool nastepny) {
        constexpr int N = 7;
        if (nastepny) {
            indeks_fokusu_ = (indeks_fokusu_ + 1) % N;
        } else {
            indeks_fokusu_ = (indeks_fokusu_ + N - 1) % N;
        }
        Kontrolka* target = kontrolki_fokusu_[indeks_fokusu_];
        if (target) {
            if (zarzadca()) {
                zarzadca()->ustaw_fokus_popupu(target);
            } else {
                target->przy_fokusie();
            }
            target->uniewaznij();
        }
    }

    uint32_t kolor_poczatkowy_{0x00F2A900U};
    uint32_t kolor_biezacy_{0x00F2A900U};
    bool blokada_sync_{false};
    WynikDialoguKoloru wynik_koloru_{WynikDialoguKoloru::Brak};

    FunkcjaWynikuKoloru cb_wynik_koloru_{nullptr};
    void* dane_wyniku_koloru_{nullptr};

    SelektorKoloru* selektor_{nullptr};
    PoleNumeryczne* pole_r_{nullptr};
    PoleNumeryczne* pole_g_{nullptr};
    PoleNumeryczne* pole_b_{nullptr};
    PoleTekstowe* pole_hex_{nullptr};
    PodgladKoloruDialogu* podglad_{nullptr};
    Przycisk* btn_ok_{nullptr};
    Przycisk* btn_anuluj_{nullptr};

    Kontrolka* kontrolki_fokusu_[7]{};
    int indeks_fokusu_{0};
};

} // namespace BursztynUI
