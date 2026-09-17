#pragma once

#include "popup.h"
#include "motyw.h"
#include "czcionka.h"
#include "ikona_ui.h"

namespace BursztynUI {

enum class TypPowiadomienia : uint8_t {
    Informacja,
    Sukces,
    Ostrzezenie,
    Blad
};

class Powiadomienie : public PowierzchniaPopup {
public:
    using FunkcjaAkcji = void (*)(Powiadomienie*, void*);
    static constexpr size_t MAKS_POWIADOMIEN = 4;

    Powiadomienie() {
        ustaw_modalna(false);
        ustaw_zamykanie_na_klik_poza(false);
        zaktualizuj_motyw();
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_panelu;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_PODNIESIONY);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = 1;
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        Kontrolka::zaktualizuj_motyw();
    }

    TypPowiadomienia typ() const { return typ_; }
    const char* tytul() const { return tytul_; }
    const char* wiadomosc() const { return wiadomosc_; }
    int pozostaly_czas_ms() const { return czas_ms_; }
    uint64_t kolejnosc() const { return kolejnosc_; }
    void ustaw_kolejnosc(uint64_t k) { kolejnosc_ = k; }
    Prostokat obszar_tekstu() const { return obszar_tekstu_; }
    Prostokat przycisk_zamknij() const { return przycisk_zamknij_r_; }
    Prostokat przycisk_akcji() const { return przycisk_akcji_r_; }
    size_t liczba_linii() const { return liczba_linii_; }

    void ustaw_dane(TypPowiadomienia t, const char* tytul_txt, const char* msg,
                    uint32_t timeout_ms = 4000, const char* akcja = nullptr,
                    FunkcjaAkcji cb = nullptr, void* dane = nullptr) {
        typ_ = t;
        czas_ms_ = static_cast<int>(timeout_ms);
        ostatni_krok_ns_ = 0;
        najechany_zamknij_ = false;
        najechany_akcja_ = false;
        przycisk_zamknij_r_ = Prostokat();
        przycisk_akcji_r_ = Prostokat();

        size_t i = 0;
        if (!tytul_txt) tytul_txt = "";
        while (tytul_txt[i] && i < sizeof(tytul_) - 1) { tytul_[i] = tytul_txt[i]; ++i; }
        tytul_[i] = '\0';

        i = 0;
        if (!msg) msg = "";
        while (msg[i] && i < sizeof(wiadomosc_) - 1) { wiadomosc_[i] = msg[i]; ++i; }
        wiadomosc_[i] = '\0';

        i = 0;
        if (akcja) {
            while (akcja[i] && i < sizeof(akcja_tekst_) - 1) { akcja_tekst_[i] = akcja[i]; ++i; }
        }
        akcja_tekst_[i] = '\0';

        cb_akcja_ = cb;
        dane_akcji_ = dane;
        zawin_wiadomosc(220 * skala_dpi_);
        uniewaznij();
    }

    Rozmiar wymagany_rozmiar(SkalaUI s) {
        int w = 300 * s;
        zawin_wiadomosc(w - 58 * s);
        int fh = CzcionkaUI::pobierz_wysokosc(s);
        int h = 20 * s + fh + 4 * s + static_cast<int>(liczba_linii_) * (fh + 2 * s) + 12 * s;
        if (akcja_tekst_[0]) h += 28 * s;
        if (h < 76 * s) h = 76 * s;
        return Rozmiar(w, h);
    }

    bool czy_wymaga_animacji() const override {
        return czy_otwarta() && czas_ms_ > 0;
    }

    void krok_animacji(uint64_t teraz_ns = 0) override {
        if (!czy_otwarta()) return;
        if (teraz_ns != 0) {
            if (ostatni_krok_ns_ != 0) {
                uint64_t delta_ms = (teraz_ns - ostatni_krok_ns_) / 1000000ULL;
                if (delta_ms > 0) {
                    czas_ms_ -= static_cast<int>(delta_ms);
                    if (czas_ms_ <= 0) {
                        czas_ms_ = 0;
                        zamknij();
                        return;
                    }
                }
            }
            ostatni_krok_ns_ = teraz_ns;
        } else {
            czas_ms_ -= 50;
            if (czas_ms_ <= 0) {
                czas_ms_ = 0;
                zamknij();
            }
        }
    }

    void zamknij() {
        if (zarzadca()) {
            zarzadca()->zamknij_powierzchnie(this, PowodZamknieciaPopup::Zatwierdzenie);
        }
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        int promien = motyw_promien(MOTYW_ROG_PANEL) * s;
        ctx.rysuj_zaokraglony_prostokat(b, styl_.normalny.tlo, promien);
        ctx.rysuj_zaokraglona_ramke(b, styl_.normalny.ramka, promien, 1);

        uint32_t kolor_stanu = pobierz_kolor_stanu();

        // Pasek stanu po lewej
        ctx.rysuj_zaokraglony_prostokat(Prostokat(2 * s, 3 * s, 4 * s, b.wysokosc - 6 * s),
                                        kolor_stanu, 2 * s);

        // Kropka / ikona stanu
        int ic_sz = 14 * s;
        int ic_x = 12 * s;
        int ic_y = 12 * s;
        ctx.rysuj_zaokraglony_prostokat(Prostokat(ic_x, ic_y, ic_sz, ic_sz), kolor_stanu, ic_sz / 2);

        // Tytuł
        int font_h = CzcionkaUI::pobierz_wysokosc(s);
        int tx = ic_x + ic_sz + 8 * s;
        int ty = 10 * s;
        ctx.rysuj_tekst(Punkt(tx, ty), tytul_, motyw_kolor(MOTYW_KOLOR_TEKST), s);

        // Treść wiadomości, zawinięta tą samą czcionką, którą mierzymy.
        int my = ty + font_h + 4 * s;
        int text_w = b.szerokosc - tx - 12 * s;
        obszar_tekstu_ = Prostokat(tx, my, text_w,
                                   static_cast<int>(liczba_linii_) * (font_h + 2 * s));
        for (size_t i = 0; i < liczba_linii_; ++i)
            ctx.rysuj_tekst(Punkt(tx, my + static_cast<int>(i) * (font_h + 2 * s)),
                            linie_[i], motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);

        // Przycisk zamknięcia "X"
        przycisk_zamknij_r_ = Prostokat(b.prawy() - 22 * s, 8 * s, 16 * s, 16 * s);
        if (najechany_zamknij_) {
            ctx.rysuj_zaokraglony_prostokat(przycisk_zamknij_r_,
                                            motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY), 3 * s);
        }
        ctx.rysuj_tekst_wysrodkowany(przycisk_zamknij_r_, "x",
                                     motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);

        // Przycisk akcji (jeśli określony)
        if (akcja_tekst_[0]) {
            int bw = CzcionkaUI::zmierz_tekst(akcja_tekst_, s) + 16 * s;
            int bh = 22 * s;
            przycisk_akcji_r_ = Prostokat(tx, obszar_tekstu_.dolny() + 6 * s, bw, bh);

            uint32_t btn_tlo = najechany_akcja_ ? motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY)
                                                : motyw_kolor(MOTYW_KOLOR_PRZYCISK);
            ctx.rysuj_zaokraglony_prostokat(przycisk_akcji_r_, btn_tlo, 4 * s);
            ctx.rysuj_zaokraglona_ramke(przycisk_akcji_r_, motyw_kolor(MOTYW_KOLOR_RAMKA), 4 * s, 1);
            ctx.rysuj_tekst_wysrodkowany(przycisk_akcji_r_, akcja_tekst_, kolor_stanu, s);
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        bool na_z = przycisk_zamknij_r_.zawiera(lx, ly);
        bool na_a = akcja_tekst_[0] && przycisk_akcji_r_.zawiera(lx, ly);

        if (na_z != najechany_zamknij_ || na_a != najechany_akcja_) {
            najechany_zamknij_ = na_z;
            najechany_akcja_ = na_a;
            uniewaznij();
        }
        e.obsluzone = true;
    }

    void obsluz_opuszczenie_myszy() override {
        if (najechany_zamknij_ || najechany_akcja_) {
            najechany_zamknij_ = false;
            najechany_akcja_ = false;
            uniewaznij();
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!e.czy_lewy_przycisk()) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        if (przycisk_zamknij_r_.zawiera(lx, ly)) {
            zamknij();
            e.obsluzone = true;
            return;
        }

        if (akcja_tekst_[0] && przycisk_akcji_r_.zawiera(lx, ly)) {
            if (cb_akcja_) cb_akcja_(this, dane_akcji_);
            zamknij();
            e.obsluzone = true;
            return;
        }

        e.obsluzone = true;
    }

private:
    void zawin_wiadomosc(int maks_w) {
        liczba_linii_ = 0;
        const char* p = wiadomosc_;
        while (*p && liczba_linii_ < MAKS_LINII) {
            char* out = linie_[liczba_linii_];
            size_t n = 0, ostatnia_spacja = 0;
            while (*p && *p != '\n' && n + 1 < DL_LINII) {
                out[n++] = *p++;
                out[n] = 0;
                if (out[n - 1] == ' ') ostatnia_spacja = n;
                if (CzcionkaUI::zmierz_tekst(out, skala_dpi_) > maks_w) {
                    if (ostatnia_spacja > 0) {
                        size_t cof = n - ostatnia_spacja;
                        p -= cof;
                        n = ostatnia_spacja - 1;
                    }
                    break;
                }
            }
            while (n && out[n - 1] == ' ') --n;
            out[n] = 0;
            if (*p == '\n') ++p;
            while (*p == ' ') ++p;
            ++liczba_linii_;
        }
        if (liczba_linii_ == 0) { linie_[0][0] = 0; liczba_linii_ = 1; }
    }

    uint32_t pobierz_kolor_stanu() const {
        switch (typ_) {
            case TypPowiadomienia::Sukces: return motyw_kolor(MOTYW_KOLOR_SUKCES);
            case TypPowiadomienia::Ostrzezenie: return motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE);
            case TypPowiadomienia::Blad: return motyw_kolor(MOTYW_KOLOR_BLAD);
            case TypPowiadomienia::Informacja:
            default: return motyw_kolor(MOTYW_KOLOR_INFORMACJA);
        }
    }

    TypPowiadomienia typ_{TypPowiadomienia::Informacja};
    char tytul_[48]{};
    char wiadomosc_[128]{};
    char akcja_tekst_[32]{};
    int czas_ms_{4000};
    uint64_t ostatni_krok_ns_{0};
    uint64_t kolejnosc_{0};
    static constexpr size_t MAKS_LINII = 6;
    static constexpr size_t DL_LINII = 96;
    char linie_[MAKS_LINII][DL_LINII]{};
    size_t liczba_linii_{1};

    FunkcjaAkcji cb_akcja_{nullptr};
    void* dane_akcji_{nullptr};

    bool najechany_zamknij_{false};
    bool najechany_akcja_{false};
    Prostokat przycisk_zamknij_r_{};
    Prostokat przycisk_akcji_r_{};
    Prostokat obszar_tekstu_{};
};

class MenedzerPowiadomien {
public:
    static constexpr size_t MAKS_TOASTOW = Powiadomienie::MAKS_POWIADOMIEN;

    MenedzerPowiadomien() = default;
    ~MenedzerPowiadomien() {
        niszczony_ = true;
        for (size_t i = 0; i < MAKS_TOASTOW; ++i) {
            toast_[i].przy_zamknieciu(nullptr, nullptr);
            toast_[i].zamknij();
        }
    }

    bool pokaz(ZarzadcaPopupow& z, TypPowiadomienia typ, const char* tytul,
               const char* tresc, uint32_t timeout_ms = 4000,
               const char* akcja = nullptr, Powiadomienie::FunkcjaAkcji cb = nullptr,
               void* dane = nullptr, SkalaUI skala = 1) {
        zarzadca_ = &z;
        skala_ = skala;
        // Znajdź wolny slot lub deterministycznie zastąp najstarszy.
        size_t slot = znajdz_wolny_slot();
        if (slot >= MAKS_TOASTOW) {
            slot = najstarszy_slot();
            toast_[slot].zamknij();
        }

        Powiadomienie& p = toast_[slot];
        p.przy_zamknieciu(&MenedzerPowiadomien::toast_zamkniety, this);
        p.zaktualizuj_motyw();
        p.ustaw_dane(typ, tytul, tresc, timeout_ms, akcja, cb, dane);
        p.ustaw_kolejnosc(++kolejnosc_);
        Rozmiar roz = p.wymagany_rozmiar(skala);
        Prostokat gdzie = z.umiesc_w_obszarze_roboczym_prawy_dol(roz, 16 * skala, 16 * skala);
        bool ok = z.dodaj_powierzchnie(p, gdzie, nullptr);
        if (ok) rozmiesc_aktywne();
        return ok;
    }

    size_t liczba_aktywnych() const {
        size_t c = 0;
        for (size_t i = 0; i < MAKS_TOASTOW; ++i) {
            if (toast_[i].czy_otwarta()) ++c;
        }
        return c;
    }

    Powiadomienie& powiadomienie(size_t i) {
        return toast_[i < MAKS_TOASTOW ? i : 0];
    }

private:
    static void toast_zamkniety(PowierzchniaPopup*, PowodZamknieciaPopup, void* dane) {
        MenedzerPowiadomien* self = static_cast<MenedzerPowiadomien*>(dane);
        if (self && !self->niszczony_) self->rozmiesc_aktywne();
    }

    void rozmiesc_aktywne() {
        if (!zarzadca_) return;
        size_t indeksy[MAKS_TOASTOW]{};
        size_t n = 0;
        for (size_t i = 0; i < MAKS_TOASTOW; ++i) if (toast_[i].czy_otwarta()) indeksy[n++] = i;
        for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j)
            if (toast_[indeksy[j]].kolejnosc() > toast_[indeksy[i]].kolejnosc()) {
                size_t t = indeksy[i]; indeksy[i] = indeksy[j]; indeksy[j] = t;
            }
        int od_dolu = 16 * skala_;
        for (size_t i = 0; i < n; ++i) {
            Powiadomienie& p = toast_[indeksy[i]];
            Rozmiar roz = p.wymagany_rozmiar(skala_);
            Prostokat r = zarzadca_->umiesc_w_obszarze_roboczym_prawy_dol(
                roz, 16 * skala_, od_dolu);
            zarzadca_->przemiesc_powierzchnie(p, r, false);
            od_dolu += roz.wysokosc + 8 * skala_;
        }
        zarzadca_->odswiez_rejestracje_popupow();
        // Rejestracja globalnej powierzchni wymienia jej bufor. Wszystkie
        // aktywne toasty musza zostac odmalowane, nie tylko najnowszy.
        for (size_t i = 0; i < n; ++i) toast_[indeksy[i]].uniewaznij();
    }

    size_t znajdz_wolny_slot() const {
        for (size_t i = 0; i < MAKS_TOASTOW; ++i) {
            if (!toast_[i].czy_otwarta()) return i;
        }
        return MAKS_TOASTOW;
    }

    size_t najstarszy_slot() const {
        int min_t = 0x7FFFFFFF;
        size_t best = 0;
        for (size_t i = 0; i < MAKS_TOASTOW; ++i) {
            if (toast_[i].czy_otwarta() && toast_[i].pozostaly_czas_ms() < min_t) {
                min_t = toast_[i].pozostaly_czas_ms();
                best = i;
            }
        }
        return best;
    }

    Powiadomienie toast_[MAKS_TOASTOW];
    ZarzadcaPopupow* zarzadca_{nullptr};
    SkalaUI skala_{1};
    uint64_t kolejnosc_{0};
    bool niszczony_{false};
};

} // namespace BursztynUI
