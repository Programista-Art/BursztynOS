#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class PoleTekstowe : public Kontrolka {
public:
    static constexpr size_t MAKS_BUFOR = 256;
    static constexpr size_t MAKS_HISTORIA = 12;

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

    void zaktualizuj_motyw() override {
        bool wlasne_tlo = styl_.normalny.wlasne_tlo;
        bool wlasny_tekst = styl_.normalny.wlasny_tekst;
        bool wlasna_ramka = styl_.normalny.wlasna_ramka;
        StylKontrolki s = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        if (wlasne_tlo) { s.normalny.tlo = styl_.normalny.tlo; s.normalny.wlasne_tlo = true; }
        if (wlasny_tekst) { s.normalny.tekst = styl_.normalny.tekst; s.normalny.wlasny_tekst = true; }
        if (wlasna_ramka) { s.normalny.ramka = styl_.normalny.ramka; s.normalny.wlasna_ramka = true; }
        styl_ = s;
        Kontrolka::zaktualizuj_motyw();
    }

    const char* tekst() const override { return bufor_; }
    void ustaw_tekst(const char* nowy_tekst) override {
        zapisz_stan_undo();
        if (!nowy_tekst) nowy_tekst = "";
        size_t i = 0;
        for (; i < MAKS_BUFOR - 1 && nowy_tekst[i] != '\0'; ++i) {
            bufor_[i] = nowy_tekst[i];
        }
#if defined(BSF_NATIVE)
        size_t size=i;while(nowy_tekst[size])++size;i=BSF::cluster_prefix(nowy_tekst,size,i);
#else
        while(i>0 && (static_cast<uint8_t>(nowy_tekst[i])&0xC0U)==0x80U)--i;
#endif
        bufor_[i] = '\0';
        dlugosc_ = i;
        bajt_kursora_ = dlugosc_;
        bajt_kotwicy_ = bajt_kursora_;
        dopasuj_przewiniecie();
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

    // Hasło i maskowanie
    bool czy_tryb_hasla() const { return czy_tryb_hasla_; }
    void ustaw_tryb_hasla(bool h) {
        if (czy_tryb_hasla_ != h) {
            czy_tryb_hasla_ = h;
            dopasuj_przewiniecie();
            uniewaznij();
        }
    }

    char znak_maskowania() const { return znak_maskowania_; }
    void ustaw_znak_maskowania(char z) {
        znak_maskowania_ = z;
        if (czy_tryb_hasla_) uniewaznij();
    }

    // BSF 2.1: base direction of this field (never global state).
    // Automatyczny: first strong character decides; an RTL value is
    // right-aligned while it fits and moves visually with Left/Right.
    void ustaw_kierunek_tekstu(KierunekTekstu k) {
        if (kierunek_ == k) return;
        kierunek_ = k; dopasuj_przewiniecie(); uniewaznij();
    }
    KierunekTekstu kierunek_tekstu() const { return kierunek_; }
    // Diagnostics (tests, observers): caret x relative to the text origin
    // including alignment, base level, horizontal scroll and the visual
    // selection spans (several for a mixed-direction selection).
    int x_kursora() const { return szerokosc_tekstu_do(bajt_kursora_); }
    uint8_t poziom_bazowy() const {
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if (const auto* L = uklad()) return L->base_level();
#endif
        return 0;
    }
    bool czy_uklad_zlozony() const {
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        return uklad() != nullptr;
#else
        return false;
#endif
    }
    int przewiniecie() const { return przewiniecie_x_; }
    size_t spany_zaznaczenia(int* x0, int* x1, size_t pojemnosc) const {
        if (!czy_ma_zaznaczenie() || !pojemnosc) return 0;
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if (const auto* L = uklad()) {
            BSF::TextLayout::Span spans[64];
            size_t ns = L->selection(poczatek_zaznaczenia(), koniec_zaznaczenia(), spans, 64), out = 0;
            int shift = przesuniecie_wyrownania();
            for (size_t k = 0; k < ns && out < pojemnosc; ++k, ++out) {
                x0[out] = shift + int(spans[k].x0 / BSF::Fixed);
                x1[out] = shift + int((spans[k].x1 + BSF::Fixed - 1) / BSF::Fixed);
            }
            return out;
        }
#endif
        x0[0] = szerokosc_tekstu_do(poczatek_zaznaczenia());
        x1[0] = szerokosc_tekstu_do(koniec_zaznaczenia());
        if (x1[0] < x0[0]) { int t = x0[0]; x0[0] = x1[0]; x1[0] = t; }
        return 1;
    }

    // Limit znaków
    size_t maksymalna_liczba_znakow() const { return maksymalna_liczba_znakow_; }
    void ustaw_maksymalna_liczbe_znakow(size_t n) { maksymalna_liczba_znakow_ = n; }

    // Pozycja kursora
    size_t pozycja_kursora() const { return bajt_kursora_; }
    void ustaw_pozycje_kursora(size_t bajt) {
        if (bajt > dlugosc_) bajt = dlugosc_;
        bajt_kursora_ = dopasuj_do_granicy_utf8(bajt);
        bajt_kotwicy_ = bajt_kursora_;
        dopasuj_przewiniecie();
        uniewaznij();
    }

    // Zaznaczanie tekstu
    bool czy_ma_zaznaczenie() const { return bajt_kursora_ != bajt_kotwicy_; }
    size_t poczatek_zaznaczenia() const { return (bajt_kotwicy_ < bajt_kursora_) ? bajt_kotwicy_ : bajt_kursora_; }
    size_t koniec_zaznaczenia() const { return (bajt_kotwicy_ < bajt_kursora_) ? bajt_kursora_ : bajt_kotwicy_; }
    size_t dlugosc_zaznaczenia() const { return koniec_zaznaczenia() - poczatek_zaznaczenia(); }

    void zaznacz_wszystko() {
        bajt_kotwicy_ = 0;
        bajt_kursora_ = dlugosc_;
        dopasuj_przewiniecie();
        uniewaznij();
    }

    void wyczysc_zaznaczenie() {
        bajt_kotwicy_ = bajt_kursora_;
        uniewaznij();
    }

    void ustaw_zaznaczenie(size_t poczatek, size_t koniec) {
        if (poczatek > dlugosc_) poczatek = dlugosc_;
        if (koniec > dlugosc_) koniec = dlugosc_;
        bajt_kotwicy_ = dopasuj_do_granicy_utf8(poczatek);
        bajt_kursora_ = dopasuj_do_granicy_utf8(koniec);
        dopasuj_przewiniecie();
        uniewaznij();
    }

    size_t pobierz_zaznaczony_tekst(char* out_bufor, size_t pojemnosc) const {
        if (!out_bufor || pojemnosc == 0) return 0;
        out_bufor[0] = '\0';
        if (!czy_ma_zaznaczenie()) return 0;

        size_t od = poczatek_zaznaczenia();
        size_t ile = dlugosc_zaznaczenia();
        if (ile >= pojemnosc) ile = pojemnosc - 1;
#if defined(BSF_NATIVE)
        ile=BSF::cluster_prefix(bufor_+od,dlugosc_zaznaczenia(),ile);
#else
        while(ile>0 && (static_cast<uint8_t>(bufor_[od+ile])&0xC0U)==0x80U)--ile;
#endif

        for (size_t i = 0; i < ile; ++i) {
            out_bufor[i] = bufor_[od + i];
        }
        out_bufor[ile] = '\0';
        return ile;
    }

    void zastap_zaznaczenie(const char* nowy_fragment) {
        if (czy_tylko_do_odczytu_) return;
        zapisz_stan_undo();

        size_t od = poczatek_zaznaczenia();
        size_t do_poz = koniec_zaznaczenia();
        size_t stary_rozmiar = do_poz - od;

        size_t nowy_rozmiar = 0;
        if (nowy_fragment) {
            while (nowy_fragment[nowy_rozmiar] != '\0') ++nowy_rozmiar;
        }

        size_t source_size=nowy_rozmiar;
        if (dlugosc_ - stary_rozmiar + nowy_rozmiar >= MAKS_BUFOR) {
            nowy_rozmiar = MAKS_BUFOR - 1 - (dlugosc_ - stary_rozmiar);
        }

#if defined(BSF_NATIVE)
        nowy_rozmiar=BSF::cluster_prefix(nowy_fragment,source_size,nowy_rozmiar);
#else
        (void)source_size;
        while(nowy_fragment && nowy_rozmiar>0 && (static_cast<uint8_t>(nowy_fragment[nowy_rozmiar])&0xC0U)==0x80U)--nowy_rozmiar;
#endif

        if (nowy_rozmiar > stary_rozmiar) {
            size_t roznica = nowy_rozmiar - stary_rozmiar;
            for (size_t i = dlugosc_ + roznica; i >= do_poz + roznica; --i) {
                bufor_[i] = bufor_[i - roznica];
                if (i == 0) break;
            }
        } else if (nowy_rozmiar < stary_rozmiar) {
            size_t roznica = stary_rozmiar - nowy_rozmiar;
            for (size_t i = od + nowy_rozmiar; i + roznica <= dlugosc_; ++i) {
                bufor_[i] = bufor_[i + roznica];
            }
        }

        for (size_t i = 0; i < nowy_rozmiar; ++i) {
            bufor_[od + i] = nowy_fragment[i];
        }

        dlugosc_ = dlugosc_ - stary_rozmiar + nowy_rozmiar;
        bufor_[dlugosc_] = '\0';
        bajt_kursora_ = od + nowy_rozmiar;
#if defined(BSF_NATIVE)
        bajt_kursora_=BSF::cluster_ceil(bufor_,dlugosc_,bajt_kursora_);
#endif
        bajt_kotwicy_ = bajt_kursora_;

        dopasuj_przewiniecie();
        uniewaznij();
        powiadom_o_zmianie();
    }

    void usun_zaznaczenie() {
        if (czy_ma_zaznaczenie()) {
            zastap_zaznaczenie("");
        }
    }

    // Operacje edycyjne gotowe do uzycia jako callbacki PolecenieUI.
    bool kopiuj_do_schowka() {
        if (!czy_ma_zaznaczenie()) return false;
        char temp[MAKS_BUFOR];
        size_t len = pobierz_zaznaczony_tekst(temp, sizeof(temp));
        return len > 0 && clipboard_set("text/plain", temp, len + 1);
    }
    bool wytnij_do_schowka() {
        if (czy_tylko_do_odczytu_ || !kopiuj_do_schowka()) return false;
        usun_zaznaczenie();
        return true;
    }
    bool wklej_ze_schowka() {
        if (czy_tylko_do_odczytu_) return false;
        char temp[MAKS_BUFOR]{};
        uint64_t odczytano = 0;
        if (!clipboard_read("text/plain", temp, sizeof(temp) - 1, &odczytano) || odczytano == 0)
            return false;
        if (odczytano >= sizeof(temp)) odczytano = sizeof(temp) - 1;
        while(odczytano>0 && (static_cast<uint8_t>(temp[odczytano])&0xC0U)==0x80U)--odczytano;
        // A short read can end inside a codepoint; omit that final character.
        if(odczytano>0) {size_t start=odczytano-1;while(start>0 && (static_cast<uint8_t>(temp[start])&0xC0U)==0x80U)--start;
            unsigned b=static_cast<uint8_t>(temp[start]);size_t expected=b<128?1:b<224?2:b<240?3:4;
            if(start+expected>odczytano)odczytano=start;}
        temp[odczytano] = '\0';
        zastap_zaznaczenie(temp);
        return true;
    }
    void usun_zaznaczony_tekst() {
        if (!czy_tylko_do_odczytu_) usun_zaznaczenie();
    }

    // Funkcje zwrotne
    void przy_zmianie(FunkcjaZmianyTekstu cb, void* dane = nullptr) {
        cb_zmiana_tekstu_ = cb;
        dane_zmiany_tekstu_ = dane;
    }

    void przy_zatwierdzeniu(FunkcjaZatwierdzenia cb, void* dane = nullptr) {
        cb_zatwierdzenie_ = cb;
        dane_zatwierdzenia_ = dane;
    }

    // Historia Cofnij / Ponów (Undo / Redo)
    bool czy_mozna_cofnac() const { return pozycja_historii_ > 0; }
    bool czy_mozna_ponowic() const { return pozycja_historii_ < liczba_historii_; }

    bool cofnij() {
        if (!czy_mozna_cofnac()) return false;
        if (pozycja_historii_ == liczba_historii_) {
            zapisz_migawke(historia_[pozycja_historii_]);
        }
        --pozycja_historii_;
        przywroc_migawke(historia_[pozycja_historii_]);
        return true;
    }

    bool ponow() {
        if (!czy_mozna_ponowic()) return false;
        ++pozycja_historii_;
        przywroc_migawke(historia_[pozycja_historii_]);
        return true;
    }

    // Wymiary
    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        SkalaUI skala = skala_dpi_;
        int font_h = CzcionkaUI::pobierz_wysokosc(skala);

        char wyswietlany[MAKS_BUFOR];
        pobierz_tekst_do_wyswietlenia(wyswietlany, sizeof(wyswietlany));

        int txt_w = CzcionkaUI::zmierz_tekst(wyswietlany[0] ? wyswietlany : tekst_zastepczy_, skala);

        int min_w = 120 * skala;
        int zadana_szer = txt_w + wypelnienie_.poziomo() * skala + 16 * skala;
        if (zadana_szer < min_w) zadana_szer = min_w;

        int naturalna_wys = font_h + wypelnienie_.pionowo() * skala + 8 * skala;
        if (naturalna_wys < 30 * skala) naturalna_wys = 30 * skala;

        Rozmiar zadany(zadana_szer, naturalna_wys);

        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala, maksymalny_rozmiar_.szerokosc * skala);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala, maksymalny_rozmiar_.wysokosc * skala);

        return zadany;
    }

    // Rysowanie
    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI skala = skala_dpi_;
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        int pad_l = wypelnienie_.lewy * skala;
        int pad_t = wypelnienie_.gorny * skala;
        int pad_r = wypelnienie_.prawy * skala;
        int pad_b = wypelnienie_.dolny * skala;

        int klient_x = pad_l;
        int klient_w = obszar_.szerokosc - (pad_l + pad_r);
        int font_h = CzcionkaUI::pobierz_wysokosc(skala);
        int klient_y = pad_t + (obszar_.wysokosc - pad_t - pad_b - font_h) / 2;
        if (klient_y < pad_t) klient_y = pad_t;

        if (klient_w <= 0) return;

        ctx.dodaj_przyciecie(Prostokat(klient_x, pad_t, klient_w, obszar_.wysokosc - (pad_t + pad_b)));

        if (dlugosc_ == 0) {
            if (tekst_zastepczy_[0] != '\0') {
                uint32_t tz_kolor = czy_ma_fokus_ ? motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI) : motyw_kolor(MOTYW_KOLOR_WYLACZONY);
                ctx.rysuj_tekst(Punkt(klient_x - przewiniecie_x_, klient_y), tekst_zastepczy_, tz_kolor, skala);
            }
        } else {
            char wyswietlany[MAKS_BUFOR];
            pobierz_tekst_do_wyswietlenia(wyswietlany, sizeof(wyswietlany));

#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
            if (const auto* L = uklad()) {
                // BSF 2.1: drawn from the same owned layout as caret, hit
                // testing and the visual selection spans.
                int px = ui_skaluj(BSF::ui_size(), skala.procent());
                Punkt pt(klient_x - przewiniecie_x_ + przesuniecie_wyrownania(), klient_y);
                int x0[64], x1[64]; size_t ns = spany_zaznaczenia(x0, x1, 64);
                for (size_t k = 0; k < ns; ++k)
                    ctx.rysuj_zaznaczenie(Prostokat(klient_x - przewiniecie_x_ + x0[k], klient_y - 2 * skala, x1[k] - x0[k], font_h + 4 * skala), motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
                ctx.rysuj_linie_ukladu(pt, *L, 0, s.tekst, px);
                for (size_t k = 0; k < ns; ++k) {
                    ctx.dodaj_przyciecie(Prostokat(klient_x - przewiniecie_x_ + x0[k], klient_y, x1[k] - x0[k], font_h));
                    ctx.rysuj_linie_ukladu(pt, *L, 0, motyw_kolor_tekstu_zaznaczenia(), px);
                    ctx.zdejmij_przyciecie();
                }
            } else
#endif
            if (czy_ma_zaznaczenie()) {
                size_t sel_od = poczatek_zaznaczenia();
                size_t sel_do = koniec_zaznaczenia();

                int x_start = klient_x - przewiniecie_x_ + szerokosc_tekstu_do(sel_od);
                int x_end = klient_x - przewiniecie_x_ + szerokosc_tekstu_do(sel_do);

                // Zaznaczenie tła
                Prostokat sel_rect(x_start, klient_y - 2 * skala, x_end - x_start, font_h + 4 * skala);
                ctx.rysuj_zaznaczenie(sel_rect, motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));

                ctx.rysuj_tekst(Punkt(klient_x-przewiniecie_x_,klient_y),wyswietlany,s.tekst,skala);
                ctx.dodaj_przyciecie(Prostokat(x_start,klient_y,x_end-x_start,font_h));
                ctx.rysuj_tekst(Punkt(klient_x-przewiniecie_x_,klient_y),wyswietlany,motyw_kolor_tekstu_zaznaczenia(),skala);
                ctx.zdejmij_przyciecie();
            } else {
                ctx.rysuj_tekst(Punkt(klient_x - przewiniecie_x_, klient_y), wyswietlany, s.tekst, skala);
            }
        }

        // Kursor
        if (czy_ma_fokus_ && !czy_tylko_do_odczytu_) {
            int kx = klient_x - przewiniecie_x_ + szerokosc_tekstu_do(bajt_kursora_);
            Prostokat kursor_r(kx, klient_y - 1, 2 * skala, font_h + 2);
            ctx.rysuj_prostokat(kursor_r, motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY));
        }

        ctx.zdejmij_przyciecie();
    }

    // Obsługa Myszy
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        if (e.czy_lewy_przycisk()) {
            int mx_rel = e.x - obszar_ekranu_.x;
            size_t klik_bajt = pozycja_z_x(mx_rel);
            if (e.czy_shift()) {
                bajt_kursora_ = klik_bajt;
            } else {
                bajt_kursora_ = klik_bajt;
                bajt_kotwicy_ = klik_bajt;
            }
            czy_zaznaczanie_mysza_ = true;
            dopasuj_przewiniecie();
            uniewaznij();
            e.obsluzone = true;
        }
        if (cb_wcisniecie_myszy_) cb_wcisniecie_myszy_(this, e, dane_wcisniecia_myszy_);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (czy_zaznaczanie_mysza_) {
            int mx_rel = e.x - obszar_ekranu_.x;
            bajt_kursora_ = pozycja_z_x(mx_rel);
            dopasuj_przewiniecie();
            uniewaznij();
            e.obsluzone = true;
        }
        if (cb_ruch_myszy_) cb_ruch_myszy_(this, e, dane_ruchu_myszy_);
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (czy_zaznaczanie_mysza_) {
            czy_zaznaczanie_mysza_ = false;
            e.obsluzone = true;
        }
        if (cb_zwolnienie_myszy_) cb_zwolnienie_myszy_(this, e, dane_zwolnienia_myszy_);
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        zaznacz_wszystko();
        e.obsluzone = true;
        if (cb_podwojne_klikniecie_) cb_podwojne_klikniecie_(this, dane_podwojnego_klikniecia_);
    }

    // Obsługa Klawiatury
    void obsluz_wprowadzenie_tekstu(ZdarzenieWprowadzaniaTekstu& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ ||
            (e.czy_ctrl() && !e.czy_alt()) || e.czy_super()) return;
        if (e.punkt_kodowy >= 32 && e.punkt_kodowy <= 0x10ffff &&
            !(e.punkt_kodowy >= 127 && e.punkt_kodowy <= 159) &&
            !(e.punkt_kodowy >= 0xd800 && e.punkt_kodowy <= 0xdfff)) {
            char utf8_bytes[5]{0};
            size_t bytes = CzcionkaUI::zakoduj_utf8(e.punkt_kodowy, utf8_bytes);
            if (bytes > 0) {
                if (maksymalna_liczba_znakow_ > 0 && utf8_liczba_znakow() >= maksymalna_liczba_znakow_ && !czy_ma_zaznaczenie()) {
                    return;
                }
                zastap_zaznaczenie(utf8_bytes);
                e.obsluzone = true;
            }
        }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;

        bool shift = e.czy_shift();
        bool ctrl = e.czy_ctrl();

        // Ctrl + A (Select All)
        if (ctrl && klawisz_litery(e, 'a', 0x1E)) {
            zaznacz_wszystko();
            e.obsluzone = true;
            return;
        }

        // Ctrl + C (Copy)
        if (ctrl && klawisz_litery(e, 'c', 0x2E)) {
            kopiuj_do_schowka();
            e.obsluzone = true;
            return;
        }

        // Ctrl + X (Cut)
        if (ctrl && klawisz_litery(e, 'x', 0x2D)) {
            wytnij_do_schowka();
            e.obsluzone = true;
            return;
        }

        // Ctrl + V (Paste)
        if (ctrl && klawisz_litery(e, 'v', 0x2F)) {
            wklej_ze_schowka();
            e.obsluzone = true;
            return;
        }

        // Ctrl + Z (Undo)
        if (ctrl && !shift && klawisz_litery(e, 'z', 0x2C)) {
            if (!czy_tylko_do_odczytu_) cofnij();
            e.obsluzone = true;
            return;
        }

        // Ctrl + Y lub Ctrl + Shift + Z (Redo)
        if ((ctrl && klawisz_litery(e, 'y', 0x15)) ||
            (ctrl && shift && klawisz_litery(e, 'z', 0x2C))) {
            if (!czy_tylko_do_odczytu_) ponow();
            e.obsluzone = true;
            return;
        }

        // BSF 2.1: Left/Right move visually on a bidi/shaped value; with a
        // selection and no Shift they collapse to its visually left/right
        // side (logical start/end swapped in an RTL field).
        if ((e.kod_klawisza == 0x4B || e.kod_klawisza == 0x4D) && czy_uklad_zlozony()) {
            int dir = e.kod_klawisza == 0x4D ? 1 : -1;
            bool rtl = (poziom_bazowy() & 1) != 0;
            if (!shift && czy_ma_zaznaczenie()) {
                bajt_kursora_ = ((dir > 0) != rtl) ? koniec_zaznaczenia() : poczatek_zaznaczenia();
            } else {
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
                bool moved = false;
                size_t m = uklad()->move_visual_on(bajt_kursora_, 0, dir, &moved);
                if (moved) bajt_kursora_ = m; // visual edge: stay
#endif
            }
            if (!shift) bajt_kotwicy_ = bajt_kursora_;
            dopasuj_przewiniecie();
            uniewaznij();
            e.obsluzone = true;
            return;
        }

        // Strzałka w lewo
        if (e.kod_klawisza == 0x4B) {
            if (shift) {
                przesun_kursor_w_lewo();
            } else {
                if (czy_ma_zaznaczenie()) {
                    bajt_kursora_ = poczatek_zaznaczenia();
                    bajt_kotwicy_ = bajt_kursora_;
                    dopasuj_przewiniecie();
                    uniewaznij();
                } else {
                    przesun_kursor_w_lewo();
                    bajt_kotwicy_ = bajt_kursora_;
                }
            }
            e.obsluzone = true;
            return;
        }

        // Strzałka w prawo
        if (e.kod_klawisza == 0x4D) {
            if (shift) {
                przesun_kursor_w_prawo();
            } else {
                if (czy_ma_zaznaczenie()) {
                    bajt_kursora_ = koniec_zaznaczenia();
                    bajt_kotwicy_ = bajt_kursora_;
                    dopasuj_przewiniecie();
                    uniewaznij();
                } else {
                    przesun_kursor_w_prawo();
                    bajt_kotwicy_ = bajt_kursora_;
                }
            }
            e.obsluzone = true;
            return;
        }

        // Home
        if (e.kod_klawisza == 0x47) {
            bajt_kursora_ = 0;
            if (!shift) bajt_kotwicy_ = 0;
            dopasuj_przewiniecie();
            uniewaznij();
            e.obsluzone = true;
            return;
        }

        // End
        if (e.kod_klawisza == 0x4F) {
            bajt_kursora_ = dlugosc_;
            if (!shift) bajt_kotwicy_ = dlugosc_;
            dopasuj_przewiniecie();
            uniewaznij();
            e.obsluzone = true;
            return;
        }

        // Backspace
        if (e.kod_klawisza == 8) {
            if (!czy_tylko_do_odczytu_) {
                if (czy_ma_zaznaczenie()) {
                    usun_zaznaczenie();
                } else {
                    usun_wstecz();
                }
            }
            e.obsluzone = true;
            return;
        }

        // Delete
        if (e.kod_klawisza == 0x53) {
            if (!czy_tylko_do_odczytu_) {
                if (czy_ma_zaznaczenie()) {
                    usun_zaznaczenie();
                } else {
                    usun_w_przod();
                }
            }
            e.obsluzone = true;
            return;
        }

        // Enter
        if (e.kod_klawisza == 13) {
            if (cb_zatwierdzenie_) {
                cb_zatwierdzenie_(this, bufor_, dane_zatwierdzenia_);
            }
            e.obsluzone = true;
            return;
        }

        if (czy_klawisz_tekstu_do_skonsumowania(e)) {
            e.obsluzone = true;
            return;
        }

        if (cb_klawisz_wcisniety_) cb_klawisz_wcisniety_(this, e, dane_klawisza_wcisnietego_);
    }

private:
    static bool czy_klawisz_tekstu_do_skonsumowania(const ZdarzenieKlawiatury& e) {
        if (e.czy_ctrl() && !e.czy_alt()) return false;
        if ((e.modyfikatory & BWS_MOD_SUPER) != 0) return false;

        const uint32_t kod = e.kod_klawisza;
        const bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        const uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if (usb) {
            return (baza >= 4U && baza <= 39U) ||
                   baza == 44U ||
                   (baza >= 45U && baza <= 56U) ||
                   (baza >= 84U && baza <= 103U);
        }

        return (kod & KLAWISZ_FIZYCZNY_GUI) || (kod >= 32U && kod < 127U);
    }

    static bool klawisz_litery(const ZdarzenieKlawiatury& e,
                               char litera, uint32_t skan_ps2) {
        const uint32_t kod = e.kod_klawisza;
        if ((kod & BWS_KEY_SOURCE_USB) != 0) {
            return (kod & 0xFFU) ==
                   static_cast<uint32_t>(4 + (litera - 'a'));
        }
        return kod == static_cast<uint32_t>(litera) ||
               kod == static_cast<uint32_t>(litera - 'a' + 'A') ||
               (kod & 0x7FU) == skan_ps2 ||
               (e.kod_skanu & 0x7FU) == skan_ps2;
    }

    struct MigawkaStanu {
        char bufor[MAKS_BUFOR]{0};
        size_t dlugosc{0};
        size_t bajt_kursora{0};
        size_t bajt_kotwicy{0};
    };

    void zapisz_migawke(MigawkaStanu& m) const {
        for (size_t i = 0; i < dlugosc_; ++i) m.bufor[i] = bufor_[i];
        m.bufor[dlugosc_] = '\0';
        m.dlugosc = dlugosc_;
        m.bajt_kursora = bajt_kursora_;
        m.bajt_kotwicy = bajt_kotwicy_;
    }

    void przywroc_migawke(const MigawkaStanu& m) {
        for (size_t i = 0; i < m.dlugosc; ++i) bufor_[i] = m.bufor[i];
        bufor_[m.dlugosc] = '\0';
        dlugosc_ = m.dlugosc;
        bajt_kursora_ = m.bajt_kursora;
        bajt_kotwicy_ = m.bajt_kotwicy;
        dopasuj_przewiniecie();
        uniewaznij();
        powiadom_o_zmianie();
    }

    void zapisz_stan_undo() {
        if (pozycja_historii_ < MAKS_HISTORIA - 1) {
            zapisz_migawke(historia_[pozycja_historii_]);
            ++pozycja_historii_;
            liczba_historii_ = pozycja_historii_;
        } else {
            for (size_t i = 0; i < MAKS_HISTORIA - 1; ++i) {
                historia_[i] = historia_[i + 1];
            }
            zapisz_migawke(historia_[MAKS_HISTORIA - 1]);
            pozycja_historii_ = MAKS_HISTORIA - 1;
            liczba_historii_ = pozycja_historii_;
        }
    }

    size_t dopasuj_do_granicy_utf8(size_t bajt) const {
        if (bajt >= dlugosc_) return dlugosc_;
#if defined(BSF_NATIVE)
        return BSF::cluster_floor(bufor_,dlugosc_,bajt);
#else
        while (bajt > 0 && (static_cast<uint8_t>(bufor_[bajt]) & 0xC0U) == 0x80U) {
            bajt--;
        }
        return bajt;
#endif
    }

    size_t utf8_liczba_znakow() const {
#if defined(BSF_NATIVE)
        return BSF::cluster_count(bufor_,dlugosc_);
#else
        return CzcionkaUI::liczba_znakow(bufor_);
#endif
    }

    void usun_wstecz() {
        if (bajt_kursora_ == 0 || dlugosc_ == 0) return;
        zapisz_stan_undo();
        size_t prev_byte = CzcionkaUI::poprzedni_znak(bufor_, bajt_kursora_);
        size_t char_len = bajt_kursora_ - prev_byte;
        for (size_t i = prev_byte; i + char_len <= dlugosc_; ++i) {
            bufor_[i] = bufor_[i + char_len];
        }
        dlugosc_ -= char_len;
        bajt_kursora_ = prev_byte;
        bajt_kotwicy_ = bajt_kursora_;
        bufor_[dlugosc_] = '\0';
#if defined(BSF_NATIVE)
        bajt_kursora_=BSF::cluster_floor(bufor_,dlugosc_,bajt_kursora_);bajt_kotwicy_=bajt_kursora_;
#endif
        dopasuj_przewiniecie();
        uniewaznij();
        powiadom_o_zmianie();
    }

    void usun_w_przod() {
        if (bajt_kursora_ >= dlugosc_) return;
        zapisz_stan_undo();
        size_t next_byte = CzcionkaUI::nastepny_znak(bufor_, bajt_kursora_, dlugosc_);
        size_t char_len = next_byte - bajt_kursora_;
        for (size_t i = bajt_kursora_; i + char_len <= dlugosc_; ++i) {
            bufor_[i] = bufor_[i + char_len];
        }
        dlugosc_ -= char_len;
        bajt_kotwicy_ = bajt_kursora_;
        bufor_[dlugosc_] = '\0';
#if defined(BSF_NATIVE)
        bajt_kursora_=BSF::cluster_floor(bufor_,dlugosc_,bajt_kursora_);bajt_kotwicy_=bajt_kursora_;
#endif
        dopasuj_przewiniecie();
        uniewaznij();
        powiadom_o_zmianie();
    }

    void przesun_kursor_w_lewo() {
        if (bajt_kursora_ == 0) return;
        bajt_kursora_ = CzcionkaUI::poprzedni_znak(bufor_, bajt_kursora_);
        dopasuj_przewiniecie();
        uniewaznij();
    }

    void przesun_kursor_w_prawo() {
        if (bajt_kursora_ >= dlugosc_) return;
        bajt_kursora_ = CzcionkaUI::nastepny_znak(bufor_, bajt_kursora_, dlugosc_);
        dopasuj_przewiniecie();
        uniewaznij();
    }

    void pobierz_tekst_do_wyswietlenia(char* out_bufor, size_t pojemnosc) const {
        if (!out_bufor || pojemnosc == 0) return;
        if (!czy_tryb_hasla_) {
            size_t i = 0;
            for (; i < dlugosc_ && i < pojemnosc - 1; ++i) {
                out_bufor[i] = bufor_[i];
            }
            out_bufor[i] = '\0';
        } else {
            size_t znakow = utf8_liczba_znakow();
            if (znakow >= pojemnosc) znakow = pojemnosc - 1;
            for (size_t i = 0; i < znakow; ++i) {
                out_bufor[i] = znak_maskowania_;
            }
            out_bufor[znakow] = '\0';
        }
    }

#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
    // Owned layout of the value when it needs BSF 2.1 (bidi/shaping or an
    // explicit RTL direction), else null. Password fields never lay out the
    // real value (only mask characters are measured), so no plaintext enters
    // a layout cache.
    const BSF::TextLayout* uklad() const {
        if (czy_tryb_hasla_ || !dlugosc_ || !BSF::ready()) return nullptr;
        if (kierunek_ != KierunekTekstu::OdPrawej && !BSF::needs_complex_layout(bufor_, dlugosc_)) return nullptr;
        int px = ui_skaluj(BSF::ui_size(), skala_dpi_.procent());
        uint64_t h = UINT64_C(1469598103934665603);
        for (size_t i = 0; i < dlugosc_; ++i) h = (h ^ uint8_t(bufor_[i])) * UINT64_C(1099511628211);
        uint32_t gen = BSF::generation();
        if (uklad_wazny_ && uklad_skrot_ == h && uklad_.bytes() == dlugosc_ && uklad_px_ == px &&
            uklad_kierunek_ == uint8_t(kierunek_) && uklad_generacja_ == gen) {
            bool rowne = true; const char* own = uklad_.text();
            for (size_t i = 0; i < dlugosc_ && rowne; ++i) rowne = own[i] == bufor_[i];
            if (rowne) return &uklad_;
        }
        uklad_wazny_ = false;
        BSF::LayoutRequest r; r.px = px; r.direction = CzcionkaUI::kierunek_bsf(kierunek_);
        if (!BSF::build_layout(uklad_, bufor_, dlugosc_, r)) return nullptr;
        uklad_skrot_ = h; uklad_px_ = px; uklad_kierunek_ = uint8_t(kierunek_); uklad_generacja_ = gen; uklad_wazny_ = true;
        return &uklad_;
    }
#endif
    int szerokosc_klienta() const {
        SkalaUI skala = skala_dpi_;
        return obszar_.szerokosc - (wypelnienie_.lewy * skala + wypelnienie_.prawy * skala);
    }
    // Direction-aware alignment: an RTL value that fits is right-aligned.
    int przesuniecie_wyrownania() const {
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if (const auto* L = uklad()) {
            if (!(L->base_level() & 1)) return 0;
            int w = int((L->width() + BSF::Fixed / 2) / BSF::Fixed), kw = szerokosc_klienta() - 2 * skala_dpi_;
            return kw > w ? kw - w : 0;
        }
#endif
        return 0;
    }
    int szerokosc_tekstu_do(size_t bajt_limit) const {
        SkalaUI skala = skala_dpi_;
        if (bajt_limit > dlugosc_) bajt_limit = dlugosc_;
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if (const auto* L = uklad())
            return przesuniecie_wyrownania() + int((L->caret_x_on(bajt_limit, 0) + BSF::Fixed / 2) / BSF::Fixed);
#endif
        if (!czy_tryb_hasla_) {
            return CzcionkaUI::pozycja_x_z_bajtu(bufor_, bajt_limit, skala);
        } else {
            size_t znakow = 0;
            for(size_t p=0;p<bajt_limit;){size_t next=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_);if(next>bajt_limit)break;++znakow;p=next;}
            char temp[MAKS_BUFOR];
            if (znakow >= sizeof(temp)) znakow = sizeof(temp) - 1;
            for (size_t i = 0; i < znakow; ++i) temp[i] = znak_maskowania_;
            temp[znakow] = '\0';
            return CzcionkaUI::zmierz_tekst(temp, skala);
        }
    }

    size_t pozycja_z_x(int target_x) const {
        SkalaUI skala = skala_dpi_;
        int pad_l = wypelnienie_.lewy * skala;
        int txt_rel_x = target_x - pad_l + przewiniecie_x_;
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if (const auto* L = uklad())
            return L->hit_test(int64_t(txt_rel_x - przesuniecie_wyrownania()) * BSF::Fixed, 0);
#endif
        if (txt_rel_x <= 0) return 0;

        if (!czy_tryb_hasla_) {
            return CzcionkaUI::znak_z_pozycji_x(bufor_, txt_rel_x, skala);
        } else {
            size_t znakow = utf8_liczba_znakow();
            char temp[MAKS_BUFOR];
            if (znakow >= sizeof(temp)) znakow = sizeof(temp) - 1;
            for (size_t i = 0; i < znakow; ++i) temp[i] = znak_maskowania_;
            temp[znakow] = '\0';
            size_t znak_idx = CzcionkaUI::znak_z_pozycji_x(temp, txt_rel_x, skala);
            // Map character index to byte index in bufor_
            size_t c_idx = 0;
            size_t b_idx = 0;
            while (b_idx < dlugosc_ && c_idx < znak_idx) {
                b_idx = CzcionkaUI::nastepny_znak(bufor_, b_idx, dlugosc_);
                c_idx++;
            }
            return b_idx;
        }
    }

    void dopasuj_przewiniecie() {
        SkalaUI skala = skala_dpi_;
        int pad_l = wypelnienie_.lewy * skala;
        int pad_r = wypelnienie_.prawy * skala;
        int klient_w = obszar_.szerokosc - (pad_l + pad_r);
        if (klient_w <= 0) return;

        int kx = szerokosc_tekstu_do(bajt_kursora_);
        if (kx - przewiniecie_x_ < 0) {
            przewiniecie_x_ = kx;
        } else if (kx - przewiniecie_x_ > klient_w - 4 * skala) {
            przewiniecie_x_ = kx - klient_w + 4 * skala;
        }
        if (przewiniecie_x_ < 0) przewiniecie_x_ = 0;
    }

    void powiadom_o_zmianie() {
        if (cb_zmiana_tekstu_) {
            cb_zmiana_tekstu_(this, bufor_, dane_zmiany_tekstu_);
        }
        if (cb_zmiana_wartosci_) {
            cb_zmiana_wartosci_(this, dane_zmiany_wartosci_);
        }
    }

    char bufor_[MAKS_BUFOR]{0};
    char tekst_zastepczy_[64]{0};
    size_t dlugosc_{0};
    size_t bajt_kursora_{0};
    size_t bajt_kotwicy_{0};

    int przewiniecie_x_{0};
    bool czy_zaznaczanie_mysza_{false};
    KierunekTekstu kierunek_{KierunekTekstu::Automatyczny};
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
    mutable BSF::TextLayout uklad_;
    mutable uint64_t uklad_skrot_{0};
    mutable int uklad_px_{0};
    mutable uint8_t uklad_kierunek_{0};
    mutable uint32_t uklad_generacja_{0};
    mutable bool uklad_wazny_{false};
#endif

    bool czy_tryb_hasla_{false};
    char znak_maskowania_{'*'};
    size_t maksymalna_liczba_znakow_{0};

    MigawkaStanu historia_[MAKS_HISTORIA];
    size_t liczba_historii_{0};
    size_t pozycja_historii_{0};

    FunkcjaZmianyTekstu cb_zmiana_tekstu_{nullptr};
    void* dane_zmiany_tekstu_{nullptr};
    FunkcjaZatwierdzenia cb_zatwierdzenie_{nullptr};
    void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
