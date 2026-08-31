#pragma once

#include "kontrolka.h"

namespace BursztynUI {

class PoleTekstowe : public Kontrolka {
public:
    static constexpr size_t MAKS_BUFOR = 256;
    using FunkcjaZmianyTekstu = void (*)(PoleTekstowe* nadawca, const char* tekst, void* dane_uzytkownika);
    using FunkcjaZatwierdzenia = void (*)(PoleTekstowe* nadawca, const char* tekst, void* dane_uzytkownika);

    PoleTekstowe(const char* tekst_zastepczy = "Wpisz tekst...") {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        wypelnienie_ = styl_.wypelnienie;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Poczatek;
        czy_moze_miec_fokus_ = true;
        ustaw_tekst_zastepczy(tekst_zastepczy);
    }

    const char* tekst() const { return bufor_; }
    void ustaw_tekst(const char* nowy_tekst) {
        if (!nowy_tekst) nowy_tekst = "";
        size_t i = 0;
        for (; i < MAKS_BUFOR - 1 && nowy_tekst[i] != '\0'; ++i) {
            bufor_[i] = nowy_tekst[i];
        }
        bufor_[i] = '\0';
        dlugosc_ = i;
        bajt_kursora_ = dlugosc_;
        uniewaznij();
        powiadom_o_zmianie();
    }

    const char* tekst_zastepczy() const { return tekst_zastepczy_; }
    void ustaw_tekst_zastepczy(const char* tz) {
        if (!tz) tz = "";
        size_t i = 0;
        for (; i < sizeof(tekst_zastepczy_) - 1 && tz[i] != '\0'; ++i) {
            tekst_zastepczy_[i] = tz[i];
        }
        tekst_zastepczy_[i] = '\0';
        uniewaznij();
    }

    void przy_zmianie(FunkcjaZmianyTekstu cb, void* dane = nullptr) {
        na_zmiane_tekstu_ = cb;
        dane_uzytkownika_ = dane;
    }

    void przy_zatwierdzeniu(FunkcjaZatwierdzenia cb, void* dane = nullptr) {
        na_zatwierdzenie_ = cb;
        dane_zatwierdzenia_ = dane;
    }

    StylKontrolki& styl() { return styl_; }
    void ustaw_styl(const StylKontrolki& s) { styl_ = s; uniewaznij(); }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        int skala = skala_dpi_;
        int font_h = gui_pobierz_wysokosc_fontu() * skala;
        int txt_w = oblicz_szerokosc_tekstu(bufor_[0] ? bufor_ : tekst_zastepczy_, skala);

        int min_w = 120 * skala;
        int zadana_szer = txt_w + wypelnienie_.poziomo() * skala + 16 * skala;
        if (zadana_szer < min_w) zadana_szer = min_w;

        int naturalna_wys = font_h + wypelnienie_.pionowo() * skala + 6 * skala;
        if (naturalna_wys < 26 * skala) naturalna_wys = 26 * skala;

        Rozmiar zadany(zadana_szer, naturalna_wys);

        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala, maksymalny_rozmiar_.szerokosc * skala);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala, maksymalny_rozmiar_.wysokosc * skala);

        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        int skala = skala_dpi_;
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        const StylStanu* biezacy = &styl_.normalny;
        if (!czy_wlaczona_) {
            biezacy = &styl_.wylaczony;
        } else if (czy_ma_fokus_) {
            biezacy = &styl_.fokus;
        } else if (czy_najechana_) {
            biezacy = &styl_.najechany;
        }

        // Tło pola
        ctx.rysuj_prostokat(r, biezacy->tlo);
        // Ramka pola
        ctx.rysuj_ramke(r, biezacy->ramka, biezacy->grubosc_ramki * skala);

        // Tekst lub Tekst Zastępczy
        int pad_l = wypelnienie_.lewy * skala;
        int pad_t = wypelnienie_.gorny * skala;
        int pad_v = wypelnienie_.pionowo() * skala;

        int klient_x = pad_l;
        int font_h = gui_pobierz_wysokosc_fontu() * skala;
        int klient_y = pad_t + (obszar_.wysokosc - pad_v - font_h) / 2;
        if (klient_y < pad_t) klient_y = pad_t;

        if (dlugosc_ == 0) {
            if (tekst_zastepczy_[0] != '\0') {
                uint32_t tz_kolor = czy_ma_fokus_ ? Kolory::TekstWyciszony : Kolory::TekstZastepczy;
                ctx.rysuj_tekst(Punkt(klient_x, klient_y), tekst_zastepczy_, tz_kolor, skala);
            }
        } else {
            uint32_t fg = czy_wlaczona_ ? biezacy->tekst : Kolory::WylaczonyTekst;
            ctx.rysuj_tekst(Punkt(klient_x, klient_y), bufor_, fg, skala);
        }

        // Rysuj karetkę kursora gdy kontrolka ma fokus
        if (czy_ma_fokus_) {
            char temp[MAKS_BUFOR];
            size_t c_len = (bajt_kursora_ < dlugosc_) ? bajt_kursora_ : dlugosc_;
            for (size_t i = 0; i < c_len; ++i) temp[i] = bufor_[i];
            temp[c_len] = '\0';

            int kursor_x = klient_x + oblicz_szerokosc_tekstu(temp, skala);
            int kursor_h = font_h;
            Prostokat kursor_r(kursor_x, klient_y, 2 * skala, kursor_h);
            ctx.rysuj_prostokat(kursor_r, Kolory::AkcentFokus);
        }
    }

    void przy_wcisnieciu_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        bajt_kursora_ = dlugosc_;
        uniewaznij();
        e.obsluzone = true;
    }

    void przy_wprowadzeniu_tekstu(ZdarzenieWprowadzaniaTekstu& e) override {
        if (!czy_wlaczona_) return;
        if (e.punkt_kodowy >= 32) {
            char utf8_bytes[5]{0};
            size_t bytes = zakoduj_utf8(e.punkt_kodowy, utf8_bytes);
            if (bytes > 0) {
                wstaw_bajty(utf8_bytes, bytes);
                e.obsluzone = true;
            }
        }
    }

    void przy_wcisnieciu_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;

        // Strzałka w lewo (kod 0x4B lub 2 / scancode E0 4B)
        if (e.kod_klawisza == 0x4B || e.kod_scancode == 0x4B || e.kod_klawisza == 2) {
            przesun_kursor_w_lewo();
            e.obsluzone = true;
            return;
        }
        // Strzałka w prawo (kod 0x4D lub 3 / scancode E0 4D)
        if (e.kod_klawisza == 0x4D || e.kod_scancode == 0x4D || e.kod_klawisza == 3) {
            przesun_kursor_w_prawo();
            e.obsluzone = true;
            return;
        }
        // Home (0x47 lub scancode 0x47)
        if (e.kod_klawisza == 0x47 || e.kod_scancode == 0x47) {
            bajt_kursora_ = 0;
            uniewaznij();
            e.obsluzone = true;
            return;
        }
        // End (0x4F lub scancode 0x4F)
        if (e.kod_klawisza == 0x4F || e.kod_scancode == 0x4F) {
            bajt_kursora_ = dlugosc_;
            uniewaznij();
            e.obsluzone = true;
            return;
        }
        // Backspace (8)
        if (e.kod_klawisza == 8) {
            usun_wstecz();
            e.obsluzone = true;
            return;
        }
        // Delete (127 lub 0x53)
        if (e.kod_klawisza == 127 || e.kod_klawisza == 0x53 || e.kod_scancode == 0x53) {
            usun_w_przod();
            e.obsluzone = true;
            return;
        }

        // Enter (13, 10 lub scancode 0x1C)
        if (e.kod_klawisza == 13 || e.kod_klawisza == 10 || e.kod_scancode == 0x1C) {
            if (na_zatwierdzenie_) {
                na_zatwierdzenie_(this, bufor_, dane_zatwierdzenia_);
            }
            e.obsluzone = true;
            return;
        }
    }

private:
    static size_t zakoduj_utf8(uint32_t cp, char* out) {
        if (cp <= 0x7F) {
            out[0] = static_cast<char>(cp);
            out[1] = '\0';
            return 1;
        } else if (cp <= 0x7FF) {
            out[0] = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
            out[1] = static_cast<char>(0x80 | (cp & 0x3F));
            out[2] = '\0';
            return 2;
        } else if (cp <= 0xFFFF) {
            out[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
            out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[2] = static_cast<char>(0x80 | (cp & 0x3F));
            out[3] = '\0';
            return 3;
        } else if (cp <= 0x10FFFF) {
            out[0] = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
            out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (cp & 0x3F));
            out[4] = '\0';
            return 4;
        }
        return 0;
    }

    void wstaw_bajty(const char* bytes, size_t count) {
        if (dlugosc_ + count >= MAKS_BUFOR) return;
        for (size_t i = dlugosc_ + count; i > bajt_kursora_ + count - 1; --i) {
            bufor_[i] = bufor_[i - count];
        }
        for (size_t i = 0; i < count; ++i) {
            bufor_[bajt_kursora_ + i] = bytes[i];
        }
        dlugosc_ += count;
        bajt_kursora_ += count;
        bufor_[dlugosc_] = '\0';
        uniewaznij();
        powiadom_o_zmianie();
    }

    void usun_wstecz() {
        if (bajt_kursora_ == 0 || dlugosc_ == 0) return;
        size_t prev_byte = bajt_kursora_ - 1;
        while (prev_byte > 0 && (static_cast<uint8_t>(bufor_[prev_byte]) & 0xC0) == 0x80) {
            prev_byte--;
        }
        size_t char_len = bajt_kursora_ - prev_byte;
        for (size_t i = prev_byte; i + char_len <= dlugosc_; ++i) {
            bufor_[i] = bufor_[i + char_len];
        }
        dlugosc_ -= char_len;
        bajt_kursora_ = prev_byte;
        bufor_[dlugosc_] = '\0';
        uniewaznij();
        powiadom_o_zmianie();
    }

    void usun_w_przod() {
        if (bajt_kursora_ >= dlugosc_) return;
        size_t next_byte = bajt_kursora_ + 1;
        while (next_byte < dlugosc_ && (static_cast<uint8_t>(bufor_[next_byte]) & 0xC0) == 0x80) {
            next_byte++;
        }
        size_t char_len = next_byte - bajt_kursora_;
        for (size_t i = bajt_kursora_; i + char_len <= dlugosc_; ++i) {
            bufor_[i] = bufor_[i + char_len];
        }
        dlugosc_ -= char_len;
        bufor_[dlugosc_] = '\0';
        uniewaznij();
        powiadom_o_zmianie();
    }

    void przesun_kursor_w_lewo() {
        if (bajt_kursora_ == 0) return;
        bajt_kursora_--;
        while (bajt_kursora_ > 0 && (static_cast<uint8_t>(bufor_[bajt_kursora_]) & 0xC0) == 0x80) {
            bajt_kursora_--;
        }
        uniewaznij();
    }

    void przesun_kursor_w_prawo() {
        if (bajt_kursora_ >= dlugosc_) return;
        bajt_kursora_++;
        while (bajt_kursora_ < dlugosc_ && (static_cast<uint8_t>(bufor_[bajt_kursora_]) & 0xC0) == 0x80) {
            bajt_kursora_++;
        }
        uniewaznij();
    }

    void powiadom_o_zmianie() {
        if (na_zmiane_tekstu_) {
            na_zmiane_tekstu_(this, bufor_, dane_uzytkownika_);
        }
    }

    char bufor_[MAKS_BUFOR]{0};
    char tekst_zastepczy_[64]{0};
    size_t dlugosc_{0};
    size_t bajt_kursora_{0};

    StylKontrolki styl_;
    FunkcjaZmianyTekstu na_zmiane_tekstu_{nullptr};
    void* dane_uzytkownika_{nullptr};
    FunkcjaZatwierdzenia na_zatwierdzenie_{nullptr};
    void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
