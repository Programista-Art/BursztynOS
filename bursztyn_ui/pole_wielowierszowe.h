#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {


class PoleWielowierszowe : public Kontrolka {
public:
    static constexpr size_t DOMYSLNA_POJEMNOSC = 8192;
    // Poczatkowa pojemnosc tablicy linii wizualnych. Tablica rosnie do
    // pojemnosc_+1 (kazda linia poza ostatnia zajmuje co najmniej bajt), wiec
    // dlugi dokument nigdy nie jest ucinany w ukladzie ani w nawigacji.
    static constexpr size_t MAKS_LINII_WIZUALNYCH = 2048;
    static constexpr size_t MAKS_HISTORIA = 8;
    using FunkcjaTekstu = void (*)(PoleWielowierszowe*, const char*, void*);
    using FunkcjaMenuKontekstowego = void (*)(PoleWielowierszowe*, ZdarzenieMyszki&, void*);

    explicit PoleWielowierszowe(const char* placeholder = "", size_t zadana_pojemnosc = DOMYSLNA_POJEMNOSC) {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        wypelnienie_ = Marginesy(6, 5, 6, 5);
        czy_moze_miec_fokus_ = true;
        pojemnosc_ = (zadana_pojemnosc > 0) ? zadana_pojemnosc : DOMYSLNA_POJEMNOSC;
        bufor_ = new char[pojemnosc_];
        roboczy_ = new char[pojemnosc_];
        schowek_ = new char[pojemnosc_];
        for (size_t i = 0; i < MAKS_HISTORIA; ++i) historia_[i] = new Migawka(pojemnosc_);
        linie_ = new Linia[MAKS_LINII_WIZUALNYCH];
        pojemnosc_linii_ = linie_ ? MAKS_LINII_WIZUALNYCH : 0;
        bufor_[0] = roboczy_[0] = schowek_[0] = '\0';
        pionowy_ = new PasekPrzewijania();
        poziomy_ = new PasekPrzewijania();
        pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        pionowy_->ustaw_maly_krok(1);
        poziomy_->ustaw_maly_krok(16);
        dodaj_kontrolke(pionowy_);
        dodaj_kontrolke(poziomy_);
        pionowy_->przy_przewijaniu(przewinieto, this);
        poziomy_->przy_przewijaniu(przewinieto, this);
        ustaw_placeholder(placeholder);
        przelicz_linie();
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
        if (pionowy_) pionowy_->zaktualizuj_motyw();
        if (poziomy_) poziomy_->zaktualizuj_motyw();
        uniewaznij();
        Kontrolka::zaktualizuj_motyw();
    }

    ~PoleWielowierszowe() override {
        for (size_t i = 0; i < MAKS_HISTORIA; ++i) delete historia_[i];
        delete[] linie_;
        delete[] bufor_;
        delete[] roboczy_;
        delete[] schowek_;
    }

    const char* tekst() const override { return bufor_; }
    void ustaw_tekst(const char* tekst) override {
        zapisz_historie();
        ustaw_tekst_bez_historii(tekst ? tekst : "");
        powiadom_zmiane();
    }

    void dopisz_tekst(const char* tekst) {
        if (!tekst || czy_tylko_do_odczytu_) return;
        ustaw_zaznaczenie(dlugosc_, dlugosc_);
        wstaw_fragment(tekst);
    }

    void wyczysc() { ustaw_tekst(""); }

    void ustaw_tylko_do_odczytu(bool ro) { Kontrolka::ustaw_tylko_do_odczytu(ro); }
    bool czy_tylko_do_odczytu() const { return Kontrolka::czy_tylko_do_odczytu(); }

    // BSF 2.1: base direction of every paragraph of this control.
    // Automatyczny detects it once per logical paragraph (UAX #9 P2/P3);
    // OdLewej/OdPrawej override detection. Alignment follows the paragraph
    // direction (LTR left, RTL right) within the text view width.
    void ustaw_kierunek_tekstu(KierunekTekstu k) {
        if (kierunek_ == k) return;
        kierunek_ = k;
        przelicz_linie(); aktualizuj_przewijanie(); przewin_do_kursora(); uniewaznij();
    }
    KierunekTekstu kierunek_tekstu() const { return kierunek_; }
    // Paragraph embedding level of the visual line holding the caret (0 LTR, 1 RTL).
    uint8_t poziom_akapitu_kursora() const { return liczba_wizualnych_ ? linie_[linia_dla_bajtu(kursor_)].poziom : 0; }
    size_t linia_wizualna_kursora() const { return linia_dla_bajtu(kursor_); }
    bool afinicja_kursora_wstecz() const { return afinicja_wstecz_; }
    // Diagnostics for tests: caret x in view coordinates, visual line ranges.
    int x_kursora() const { return x_w_linii(linia_dla_bajtu(kursor_), kursor_); }
    size_t poczatek_linii_wizualnej(size_t li) const { return li < liczba_wizualnych_ ? linie_[li].start : dlugosc_; }
    size_t koniec_linii_wizualnej(size_t li) const { return li < liczba_wizualnych_ ? linie_[li].koniec : dlugosc_; }
    uint8_t poziom_linii(size_t li) const { return li < liczba_wizualnych_ ? linie_[li].poziom : 0; }
    int przesuniecie_linii(size_t li) const { return li < liczba_wizualnych_ ? linie_[li].przesuniecie : 0; }
    bool linia_zlozona(size_t li) const { return li < liczba_wizualnych_ && linie_[li].zlozona; }
    int wysokosc_jednej_linii() const { return wysokosc_linii(); }
    // Visual selection rectangles of line li in view x (without scrolling):
    // a mixed-direction selection yields several disjoint spans.
    size_t spany_zaznaczenia(size_t li, int* x0, int* x1, size_t pojemnosc) const {
        if (li >= liczba_wizualnych_ || !czy_ma_zaznaczenie()) return 0;
        const Linia& ln = linie_[li];
        size_t a = poczatek_zaznaczenia() > ln.start ? poczatek_zaznaczenia() : ln.start;
        size_t b = koniec_zaznaczenia() < ln.koniec ? koniec_zaznaczenia() : ln.koniec;
        if (a >= b) return 0;
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if (const auto* L = uklad_linii(li)) {
            BSF::TextLayout::Span spans[128];
            size_t ns = L->selection(a - ln.akapit, b - ln.akapit, spans, 128), out = 0;
            for (size_t k = 0; k < ns && out < pojemnosc; ++k) {
                if (spans[k].line != ln.linia_ukladu) continue;
                x0[out] = ln.przesuniecie + int(spans[k].x0 / BSF::Fixed);
                x1[out] = ln.przesuniecie + int((spans[k].x1 + BSF::Fixed - 1) / BSF::Fixed);
                ++out;
            }
            return out;
        }
#endif
        if (!pojemnosc) return 0;
        x0[0] = ln.przesuniecie + szerokosc_fragmentu(ln.start, a);
        x1[0] = ln.przesuniecie + szerokosc_fragmentu(ln.start, b);
        return 1;
    }
    struct StatystykiUkladu { uint64_t trafienia{}, chybienia{}; };
    StatystykiUkladu statystyki_ukladu() const {
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        return {trafienia_ukladu_, chybienia_ukladu_};
#else
        return {};
#endif
    }

    void ustaw_zawijanie_wierszy(bool zawijaj) {
        if (zawijanie_ == zawijaj) return;
        zawijanie_ = zawijaj;
        przelicz_linie();
        aktualizuj_przewijanie();
        uniewaznij();
    }
    bool czy_zawija_wiersze() const { return zawijanie_; }

    void ustaw_pokazywanie_paska_poziomego(bool pokazuj) {
        pozwol_poziomy_ = pokazuj;
        aktualizuj_przewijanie(); uniewaznij();
    }
    void ustaw_pokazywanie_paska_pionowego(bool pokazuj) {
        pozwol_pionowy_ = pokazuj;
        aktualizuj_przewijanie(); uniewaznij();
    }

    bool przejdz_do_wiersza(size_t wiersz) {
        if (liczba_logicznych_ == 0) return false;
        if (wiersz >= liczba_logicznych_) wiersz = liczba_logicznych_ - 1;
        for (size_t i = 0; i < liczba_wizualnych_; ++i) {
            if (linie_[i].logiczna == wiersz) {
                ustaw_zaznaczenie(linie_[i].start, linie_[i].start);
                przewin_do_kursora();
                return true;
            }
        }
        return false;
    }

    void przewin_do_kursora() {
        size_t linia = linia_dla_bajtu(kursor_);
        int wid = liczba_widocznych_linii();
        int pierwszy = static_cast<int>(pionowy_->wartosc());
        if (static_cast<int>(linia) < pierwszy) pionowy_->ustaw_wartosc(linia);
        else if (static_cast<int>(linia) >= pierwszy + wid)
            pionowy_->ustaw_wartosc(static_cast<int64_t>(linia) - wid + 1);
        if (!zawijanie_) {
            int x = x_w_linii(linia, kursor_);
            int szer = geometria().widok.szerokosc;
            if (x < poziomy_->wartosc()) poziomy_->ustaw_wartosc(x);
            else if (x > poziomy_->wartosc() + szer - 4 * skala_dpi_)
                poziomy_->ustaw_wartosc(x - szer + 4 * skala_dpi_);
        }
    }

    void pozycja_logiczna(size_t& row,size_t& column)const{
        size_t li=linia_dla_bajtu(kursor_);row=linie_[li].logiczna+1;
        while(li&&linie_[li-1].logiczna==linie_[li].logiczna)--li;
#if defined(BSF_NATIVE)
        column=granice_.rank(bufor_,kursor_)-granice_.rank(bufor_,linie_[li].start)+1;
#else
        column=1;for(size_t p=linie_[li].start;p<kursor_;p=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_))++column;
#endif
    }
    size_t liczba_wierszy() const { return liczba_logicznych_; }
    size_t liczba_linii_wizualnych() const { return liczba_wizualnych_; }
    size_t liczba_ostatnio_narysowanych_linii() const { return ostatnio_narysowane_; }

    void ustaw_maksymalna_liczbe_znakow(size_t maks) { maks_znakow_ = maks; }
    size_t maksymalna_liczba_znakow() const { return maks_znakow_; }

    void ustaw_placeholder(const char* tekst) {
        kopiuj_staly(placeholder_, sizeof(placeholder_), tekst ? tekst : "");
        if (dlugosc_ == 0) uniewaznij();
    }
    const char* placeholder() const { return placeholder_; }

    void ustaw_odstep_linii(int piksele) {
        odstep_linii_ = piksele < 0 ? 0 : piksele;
        przelicz_linie(); aktualizuj_przewijanie(); uniewaznij();
    }
    int odstep_linii() const { return odstep_linii_; }

    void przy_zmianie(FunkcjaTekstu cb, void* dane = nullptr) {
        cb_zmiana_ = cb; dane_zmiany_ = dane;
    }
    void przy_zatwierdzeniu(FunkcjaTekstu cb, void* dane = nullptr) {
        cb_zatwierdzenie_ = cb; dane_zatwierdzenia_ = dane;
    }
    void przy_menu_kontekstowym(FunkcjaMenuKontekstowego cb, void* dane = nullptr) {
        cb_menu_kontekstowe_ = cb; dane_menu_kontekstowego_ = dane;
    }

    bool czy_ma_zaznaczenie() const { return kursor_ != kotwica_; }
    size_t pozycja_kursora() const { return kursor_; }
    size_t poczatek_zaznaczenia() const { return kursor_ < kotwica_ ? kursor_ : kotwica_; }
    size_t koniec_zaznaczenia() const { return kursor_ > kotwica_ ? kursor_ : kotwica_; }
    void ustaw_zaznaczenie(size_t poczatek, size_t koniec) {
        if (poczatek > dlugosc_) poczatek = dlugosc_;
        if (koniec > dlugosc_) koniec = dlugosc_;
        kotwica_ = granica_utf8(poczatek);
        kursor_ = granica_utf8(koniec);
        afinicja_wstecz_ = false;
        przewin_do_kursora();
        uniewaznij_obszar(geometria().widok);
    }
    void zaznacz_wszystko() { ustaw_zaznaczenie(0, dlugosc_); }
    void kopiuj_do_schowka() { kopiuj(); }
    void wytnij_do_schowka() {
        kopiuj();
        if (!czy_tylko_do_odczytu_) usun_zaznaczenie();
    }
    void wklej_ze_schowka() { if (!czy_tylko_do_odczytu_) wklej(); }
    size_t dlugosc() const { return dlugosc_; }
    void usun_zaznaczony_tekst() { if (!czy_tylko_do_odczytu_) usun_zaznaczenie(); }

    bool cofnij() {
        if (hist_pozycja_ == 0) return false;
        if (hist_pozycja_ == hist_liczba_) zapisz_migawke(*historia_[hist_pozycja_]);
        --hist_pozycja_;
        przywroc_migawke(*historia_[hist_pozycja_]);
        return true;
    }
    bool czy_mozna_cofnac() const { return !czy_tylko_do_odczytu_ && hist_pozycja_ > 0; }
    bool czy_mozna_ponowic() const { return !czy_tylko_do_odczytu_ && hist_pozycja_ < hist_liczba_; }
    // Nowy dokument (np. wczytany plik) nie moze cofnac sie do tresci
    // poprzedniego dokumentu.
    void wyczysc_historie() { hist_pozycja_ = hist_liczba_ = 0; }
    bool ponow() {
        if (hist_pozycja_ >= hist_liczba_) return false;
        ++hist_pozycja_;
        przywroc_migawke(*historia_[hist_pozycja_]);
        return true;
    }

    PasekPrzewijania& pasek_pionowy() { return *pionowy_; }
    PasekPrzewijania& pasek_poziomy() { return *poziomy_; }

    Prostokat prostokat_zewnetrzny() const { return geometria().zewnetrzny; }
    Prostokat prostokat_wewnatrz_ramki() const { return geometria().wewnatrz_ramki; }
    Prostokat prostokat_widoku_tekstu() const { return geometria().widok; }
    Prostokat prostokat_paska_pionowego() const { return geometria().pionowy; }
    Prostokat prostokat_paska_poziomego() const { return geometria().poziomy; }
    Prostokat prostokat_rogu_paskow() const { return geometria().rog; }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI s = skala_dpi_;
        return Rozmiar(320 * s, 180 * s);
    }

    void rozmiesc(Prostokat r) override {
        Kontrolka::rozmiesc(r);
        aktualizuj_przewijanie();
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu styl = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, styl);
        Geometria g = geometria();
        ostatnio_narysowane_ = 0;
        if (!g.rog.czy_pusty()) {
            ctx.rysuj_prostokat(g.rog, motyw_kolor(MOTYW_KOLOR_OKNOTLO));
        }
        if (g.widok.czy_pusty()) return;
        ctx.dodaj_przyciecie(g.widok);
        SkalaUI s = skala_dpi_;
        int wys = wysokosc_linii();
        size_t pierwszy = static_cast<size_t>(pionowy_->wartosc());
        size_t koniec = pierwszy + static_cast<size_t>(g.widok.wysokosc / wys + 1);
        if (koniec > liczba_wizualnych_) koniec = liczba_wizualnych_;
        if (dlugosc_ == 0 && placeholder_[0]) {
            ctx.rysuj_tekst(Punkt(g.widok.x, g.widok.y), placeholder_,
                            motyw_kolor(MOTYW_KOLOR_WYLACZONY), s);
        }
        for (size_t i = pierwszy; i < koniec && dlugosc_ > 0; ++i) {
            int y = g.widok.y + static_cast<int>(i - pierwszy) * wys;
            rysuj_linie(ctx, i, y, g, styl);
            ++ostatnio_narysowane_;
        }
        if (czy_ma_fokus_ && !czy_tylko_do_odczytu_) {
            size_t li = linia_dla_bajtu(kursor_);
            if (li >= pierwszy && li < koniec) {
                int x = g.widok.x - static_cast<int>(poziomy_->wartosc()) + x_w_linii(li, kursor_);
                int y = g.widok.y + static_cast<int>(li - pierwszy) * wys;
                ctx.rysuj_prostokat(Prostokat(x, y, 2 * s, CzcionkaUI::pobierz_wysokosc(s)),
                                    motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY));
            }
        }
        ctx.zdejmij_przyciecie();
    }

    void obsluz_wprowadzenie_tekstu(ZdarzenieWprowadzaniaTekstu& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ || e.punkt_kodowy < 32 || e.punkt_kodowy > 0x10ffff ||
            (e.punkt_kodowy >= 127 && e.punkt_kodowy <= 159) ||
            (e.punkt_kodowy >= 0xd800 && e.punkt_kodowy <= 0xdfff) ||
            (e.czy_ctrl() && !e.czy_alt()) || e.czy_super()) return;
        char znak[5];
        if (CzcionkaUI::zakoduj_utf8(e.punkt_kodowy, znak)) {
            wstaw_fragment(znak); e.obsluzone = true;
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (czy_wlaczona_ && e.czy_prawy_przycisk() && cb_menu_kontekstowe_) {
            cb_menu_kontekstowe_(this, e, dane_menu_kontekstowego_);
            e.obsluzone = true;
            return;
        }
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;

        bool wstecz = false;
        size_t p = bajt_z_punktu(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y, &wstecz);
        if (!e.czy_shift()) kotwica_ = p;
        kursor_ = p;
        afinicja_wstecz_ = wstecz;
        preferowane_x_ = x_w_linii(linia_dla_bajtu(kursor_), kursor_);
        zaznaczanie_ = true;
        przewin_do_kursora();
        uniewaznij_obszar(geometria().widok);
        e.obsluzone = true;
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (zaznaczanie_) {
            Geometria g = geometria();
            int local_x = e.x - obszar_ekranu_.x;
            int local_y = e.y - obszar_ekranu_.y;

            if (local_y < g.widok.y) {
                pionowy_->przewin_o(-1);
            } else if (local_y > g.widok.dolny()) {
                pionowy_->przewin_o(1);
            }

            if (!zawijanie_) {
                if (local_x < g.widok.x) {
                    poziomy_->przewin_o(-16);
                } else if (local_x > g.widok.prawy()) {
                    poziomy_->przewin_o(16);
                }
            }

            bool wstecz = false;
            size_t p = bajt_z_punktu(local_x, local_y, &wstecz);
            if (p != kursor_ || wstecz != afinicja_wstecz_) {
                kursor_ = p;
                afinicja_wstecz_ = wstecz;
                przewin_do_kursora();
                uniewaznij_obszar(g.widok);
            }
            e.obsluzone = true;
        }
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (e.czy_lewy_przycisk() && zaznaczanie_) {
            zaznaczanie_ = false;
            e.obsluzone = true;
        }
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_ || !e.czy_lewy_przycisk()) return;
        size_t p = bajt_z_punktu(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        if (p > dlugosc_) p = dlugosc_;

        size_t start = p;
        while (start > 0 && bufor_[start - 1] != ' ' && bufor_[start - 1] != '\n' && bufor_[start - 1] != '\t') {
            start = CzcionkaUI::poprzedni_znak(bufor_, start);
        }
        size_t end = p;
        while (end < dlugosc_ && bufor_[end] != ' ' && bufor_[end] != '\n' && bufor_[end] != '\t') {
            end = CzcionkaUI::nastepny_znak(bufor_, end, dlugosc_);
        }

        kotwica_ = start;
        kursor_ = end;
        przewin_do_kursora();
        uniewaznij_obszar(geometria().widok);
        e.obsluzone = true;
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0) return;
        pionowy_->przewin_o(e.delta_y > 0 ? -3 : 3);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        bool ctrl = e.czy_ctrl(), shift = e.czy_shift();
        uint32_t k = e.polecenie();
        if (k == 27) { // Escape: unieważnij zaznaczenie
            if (czy_ma_zaznaczenie()) {
                kotwica_ = kursor_;
                uniewaznij_obszar(geometria().widok);
                e.obsluzone = true;
                return;
            }
        }
        if (ctrl && klawisz_litery(e, 'a', 0x1E)) { zaznacz_wszystko(); e.obsluzone = true; return; }
        if (ctrl && klawisz_litery(e, 'c', 0x2E)) { kopiuj(); e.obsluzone = true; return; }
        if (ctrl && klawisz_litery(e, 'x', 0x2D)) { kopiuj(); if (!czy_tylko_do_odczytu_) usun_zaznaczenie(); e.obsluzone = true; return; }
        if (ctrl && klawisz_litery(e, 'v', 0x2F)) { if (!czy_tylko_do_odczytu_) wklej(); e.obsluzone = true; return; }
        if (ctrl && !shift && klawisz_litery(e, 'z', 0x2C)) { if (!czy_tylko_do_odczytu_) cofnij(); e.obsluzone = true; return; }
        if (ctrl && (klawisz_litery(e, 'y', 0x15) || (shift && klawisz_litery(e, 'z', 0x2C)))) { if (!czy_tylko_do_odczytu_) ponow(); e.obsluzone = true; return; }
        if (k == 13) {
            if (!czy_tylko_do_odczytu_) wstaw_fragment("\n");
            if (cb_zatwierdzenie_) cb_zatwierdzenie_(this, bufor_, dane_zatwierdzenia_);
            e.obsluzone = true; return;
        }
        if (k == 8) { if (!czy_tylko_do_odczytu_) usun_wstecz(); e.obsluzone = true; return; }
        if (k == 0x53U) { if (!czy_tylko_do_odczytu_) usun_w_przod(); e.obsluzone = true; return; }
        size_t stary = kursor_;
        bool ruch = true, pionowo = false, wstecz = false;
        // Paragraph direction of the caret line decides which logical side
        // Left/Right and Ctrl+Left/Right mean (RTL: Left goes forward).
        bool rtl = (linie_[linia_dla_bajtu(kursor_)].poziom & 1) != 0;
        if (ctrl && (k == 0x47U)) kursor_ = 0;
        else if (ctrl && (k == 0x4FU)) kursor_ = dlugosc_;
        else if (k == 0x4BU || k == 0x4DU) {
            int dir = k == 0x4DU ? 1 : -1;
            bool naprzod = (dir > 0) != rtl;
            if (!shift && czy_ma_zaznaczenie()) {
                kursor_ = naprzod ? koniec_zaznaczenia() : poczatek_zaznaczenia();
            } else if (ctrl) {
                kursor_ = naprzod ? nastepny_bajt(kursor_, true) : poprzedni_bajt(kursor_, true);
            } else if (ruch_wizualny(dir, wstecz)) {
            } else {
                kursor_ = naprzod ? nastepny_bajt(kursor_, false) : poprzedni_bajt(kursor_, false);
            }
        }
        else if (k == 0x48U) { przesun_pionowo(-1, wstecz); pionowo = true; }
        else if (k == 0x50U) { przesun_pionowo(1, wstecz); pionowo = true; }
        else if (k == 0x49U) { przesun_pionowo(-liczba_widocznych_linii(), wstecz); pionowo = true; }
        else if (k == 0x51U) { przesun_pionowo(liczba_widocznych_linii(), wstecz); pionowo = true; }
        // Home/End: logical start/end of the caret's visual line (shown at the
        // right edge for RTL lines); End keeps the caret on that line.
        else if (k == 0x47U) kursor_ = linie_[linia_dla_bajtu(kursor_)].start;
        else if (k == 0x4FU) { size_t li = linia_dla_bajtu(kursor_); kursor_ = linie_[li].koniec; wstecz = miekka_granica(li); }
        else ruch = false;
        if (ruch) {
            afinicja_wstecz_ = wstecz && miekka_granica_bajtu(kursor_);
            if (!shift) kotwica_ = kursor_;
            if (!pionowo) preferowane_x_ = x_w_linii(linia_dla_bajtu(kursor_), kursor_);
            przewin_do_kursora();
            uniewaznij_zaznaczenie(stary, kursor_);
            e.obsluzone = true; return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    // zlozona: the visual line needs BSF 2.0 layout (bidi/shaping); computed
    // once per rebuild so caret keys never rescan long lines.
    // A visual line. zlozona: drawn/edited through the paragraph layout
    // (bidi/shaping); akapit/akapit_dl: its logical paragraph; linia_ukladu:
    // the line index inside that paragraph's layout; poziom: paragraph
    // embedding level (resolved once per paragraph); przesuniecie: alignment.
    struct Linia { size_t start{0}, koniec{0}, logiczna{0}; int szerokosc{0}; bool zlozona{false};
                   size_t akapit{0}, akapit_dl{0}; uint32_t linia_ukladu{0}; uint8_t poziom{0}; int przesuniecie{0}; };
    struct Geometria {
        Prostokat zewnetrzny;
        Prostokat wewnatrz_ramki;
        Prostokat widok;
        Prostokat pionowy;
        Prostokat poziomy;
        Prostokat rog;
    };
    struct Migawka {
        explicit Migawka(size_t n) { tekst = new char[n]; tekst[0] = '\0'; }
        ~Migawka() { delete[] tekst; }
        char* tekst{nullptr}; size_t dlugosc{0}, kursor{0}, kotwica{0};
    };

    static void przewinieto(PasekPrzewijania*, int64_t, void* dane) {
        PoleWielowierszowe* p = static_cast<PoleWielowierszowe*>(dane);
        if (p) p->uniewaznij_obszar(p->geometria().widok);
    }
    static void kopiuj_staly(char* dst, size_t cap, const char* src) {
        size_t i = 0; if (!src) src = "";
        while (i + 1 < cap && src[i]) { dst[i] = src[i]; ++i; }
        dst[i] = '\0';
    }
    static bool klawisz_litery(const ZdarzenieKlawiatury& e, char l, uint32_t scan) {
        uint32_t kod = e.polecenie();
        if ((kod & BWS_KEY_SOURCE_USB) != 0) {
            return (kod & 0xFFU) == static_cast<uint32_t>(4 + (l - 'a'));
        }
        uint32_t baza = kod & 0x7FU;
        return kod == static_cast<uint32_t>(l) ||
               kod == static_cast<uint32_t>(l - 'a' + 'A') ||
               baza == scan || (e.kod_skanu & 0x7FU) == scan;
    }
    int wysokosc_linii() const { return CzcionkaUI::pobierz_wysokosc(skala_dpi_) + odstep_linii_ * skala_dpi_; }
    int grubosc_paska() const { return 18 * skala_dpi_; }

    Prostokat obszar_po_ramce_i_wypelnieniu() const {
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        int gr = grubosc_ramki_geometrii();
        r = r.zmniejszony(Marginesy(gr));
        SkalaUI s = skala_dpi_;
        return r.zmniejszony(Marginesy(wypelnienie_.lewy * s,
                                      wypelnienie_.gorny * s,
                                      wypelnienie_.prawy * s,
                                      wypelnienie_.dolny * s));
    }

    Geometria geometria() const {
        Geometria g;
        g.zewnetrzny = Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        int gr = grubosc_ramki_geometrii();
        g.wewnatrz_ramki = g.zewnetrzny.zmniejszony(Marginesy(gr));
        Prostokat baza = obszar_po_ramce_i_wypelnieniu();
        int gp = grubosc_paska(); bool pv = pionowy_->czy_widoczna(), ph = poziomy_->czy_widoczna();
        int vw = baza.szerokosc - (pv ? gp : 0);
        int vh = baza.wysokosc - (ph ? gp : 0);
        if (vw < 0) vw = 0;
        if (vh < 0) vh = 0;
        g.widok = Prostokat(baza.x, baza.y, vw, vh);
        g.pionowy = Prostokat(baza.x + vw, baza.y, pv ? gp : 0, vh);
        g.poziomy = Prostokat(baza.x, baza.y + vh, vw, ph ? gp : 0);
        g.rog = (pv && ph)
              ? Prostokat(baza.x + vw, baza.y + vh, gp, gp)
              : Prostokat();
        return g;
    }

    void aktualizuj_przewijanie() {
        Prostokat stara = geometria().widok;
        Prostokat baza = obszar_po_ramce_i_wypelnieniu();
        int gp = grubosc_paska();
        bool pv = false;
        bool ph = false;

        // Monotoniczny punkt staly: w ramach jednego przeliczenia pasek moze
        // tylko dojsc. Dwa paski daja najwyzej dwa zalezne przejscia, wiec
        // trzy iteracje wystarczaja i nie ma oscylacji widocznosci.
        for (int iteracja = 0; iteracja < 3; ++iteracja) {
            int vw = baza.szerokosc - (pv ? gp : 0); if (vw < 0) vw = 0;
            int vh = baza.wysokosc - (ph ? gp : 0); if (vh < 0) vh = 0;
            przelicz_linie_dla_limitu(vw);
            int64_t wysokosc_tresci = static_cast<int64_t>(liczba_wizualnych_) * wysokosc_linii();
            bool potrzebny_p = pozwol_pionowy_ && wysokosc_tresci > vh;
            bool potrzebny_h = pozwol_poziomy_ && !zawijanie_ && maks_szerokosc_ > vw;
            bool nowy_p = pv || potrzebny_p;
            bool nowy_h = ph || potrzebny_h;
            if (nowy_p == pv && nowy_h == ph) break;
            pv = nowy_p;
            ph = nowy_h;
        }

        int koncowa_szerokosc = baza.szerokosc - (pv ? gp : 0);
        if (koncowa_szerokosc < 0) koncowa_szerokosc = 0;
        przelicz_linie_dla_limitu(koncowa_szerokosc);
        pionowy_->ustaw_widoczna(pv);
        poziomy_->ustaw_widoczna(ph);
        int wid = liczba_widocznych_linii();
        int64_t max_y = static_cast<int64_t>(liczba_wizualnych_) - wid; if (max_y < 0) max_y = 0;
        pionowy_->ustaw_zakres(0,max_y); pionowy_->ustaw_rozmiar_strony(wid); pionowy_->ustaw_duzy_krok(wid);
        int vw = geometria().widok.szerokosc; int64_t max_x = maks_szerokosc_ - vw; if (max_x < 0) max_x = 0;
        poziomy_->ustaw_zakres(0,max_x); poziomy_->ustaw_rozmiar_strony(vw); poziomy_->ustaw_duzy_krok(vw > 16 ? vw-16 : 16);
        Geometria g = geometria();
        pionowy_->rozmiesc(g.pionowy);
        poziomy_->rozmiesc(g.poziomy);
        if (stara != g.widok) uniewaznij_obszar(stara.suma(g.widok).suma(g.rog));
    }
    int liczba_widocznych_linii() const { int n = geometria().widok.wysokosc / wysokosc_linii(); return n > 0 ? n : 1; }

    void przelicz_linie() {
        przelicz_linie_dla_limitu(geometria().widok.szerokosc);
    }

    void przelicz_linie_dla_limitu(int limit) {
#if defined(BSF_NATIVE)
        granice_.rebuild(bufor_,dlugosc_);
        for(auto& point:pomiary_)point.offset=size_t(-1);
#endif
        liczba_wizualnych_ = 0; liczba_logicznych_ = 1; maks_szerokosc_ = 0;
        size_t start = 0, logiczna = 0;
        if (limit < 1) limit = 1;
        limit_ukladu_ = limit;
        while (start <= dlugosc_ && liczba_wizualnych_ < maks_linii()) {
            size_t koniec_log = start;
            while (koniec_log < dlugosc_ && bufor_[koniec_log] != '\n') ++koniec_log;
            size_t nastepna_log=koniec_log;
            if(koniec_log>start && bufor_[koniec_log-1]=='\r')--koniec_log;
            size_t p = start;
            if (p == koniec_log) dodaj_linie(p, p, logiczna, 0);
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
            // BSF 2.1: one layout per logical paragraph. UAX #9 resolves the
            // paragraph direction once, wraps with the shaped advances that
            // draw it, then applies L1/L2 per visual line. Explicit direction
            // overrides detection (an explicit RTL paragraph of plain ASCII
            // also needs the bidi layout for punctuation and alignment).
            if (p < koniec_log && BSF::ready() &&
                (kierunek_ == KierunekTekstu::OdPrawej || BSF::needs_complex_layout(bufor_+start,koniec_log-start))) {
                const auto* L=uklad_akapitu(start,koniec_log-start);
                if (L) {
                    for(size_t k=0;k<L->line_count()&&liczba_wizualnych_<maks_linii();++k){
                        const auto& vl=L->lines()[k];
                        dodaj_linie(start+vl.byte_begin,start+vl.byte_end,logiczna,int((vl.width+BSF::Fixed/2)/BSF::Fixed),
                                    true,start,koniec_log-start,uint32_t(k),vl.paragraph_level);
                    }
                    p = koniec_log;
                }
            }
#endif
            while (p < koniec_log && liczba_wizualnych_ < maks_linii()) {
                size_t q = p; int szer = 0, ostatnia_spacja = -1, szer_spacji = 0;
#if defined(BSF_NATIVE)
                BSF::TextMetrics pen;
#endif
                while (q < koniec_log) {
                    uint32_t cp=0; size_t z=CzcionkaUI::dekoduj_utf8(bufor_,q,&cp); if (!z) break;
                    int krok=CzcionkaUI::postep_znaku(cp,skala_dpi_);
#if defined(BSF_NATIVE)
                    if(q<65536){auto& point=pomiary_[q/64];if(point.offset==size_t(-1)||point.line!=p){point.offset=q;point.line=p;point.pen=pen;point.pen.width=szer;}}
                    z=BSF::next_cluster(bufor_,q,koniec_log)-q;
                    if(BSF::ready()){
                        auto next=pen;next.cluster(bufor_,q,q+z,ui_skaluj(BSF::ui_size(),skala_dpi_.procent()));
                        krok=int(next.width-pen.width);pen=next;
                    }
#endif
                    if (zawijanie_ && szer+krok>limit && q>p) break;
                    szer+=krok; q+=z;
                    if (cp==' ' || cp=='\t') { ostatnia_spacja=static_cast<int>(q); szer_spacji=szer; }
                }
                if (zawijanie_ && q < koniec_log && ostatnia_spacja > static_cast<int>(p)) { q=static_cast<size_t>(ostatnia_spacja); szer=szer_spacji; }
                if (q==p) q=CzcionkaUI::nastepny_znak(bufor_,p,koniec_log);
                dodaj_linie(p,q,logiczna,szer); p=q;
            }
            if (nastepna_log >= dlugosc_) break;
            start=nastepna_log+1; ++logiczna; ++liczba_logicznych_;
        }
        if (liczba_wizualnych_==0) dodaj_linie(0,0,0,0);
        // Direction-aware alignment within the text view (wrapping) or the
        // widest line (horizontal scrolling); LTR lines stay at x = 0.
        int dostepna = zawijanie_ ? limit : (maks_szerokosc_ > limit ? maks_szerokosc_ : limit);
        for (size_t i = 0; i < liczba_wizualnych_; ++i) {
            Linia& ln = linie_[i];
            ln.przesuniecie = (ln.poziom & 1) && dostepna > ln.szerokosc ? dostepna - ln.szerokosc : 0;
        }
    }
    void dodaj_linie(size_t a,size_t b,size_t log,int szer,bool zlozona=false,size_t akapit=0,size_t akapit_dl=0,uint32_t linia_ukladu=0,uint8_t poziom=0) {
        if (liczba_wizualnych_>=maks_linii()) return;
        if (liczba_wizualnych_>=pojemnosc_linii_ && !powieksz_linie()) return;
        Linia ln; ln.start=a; ln.koniec=b; ln.logiczna=log; ln.szerokosc=szer; ln.zlozona=zlozona;
        ln.akapit=akapit; ln.akapit_dl=akapit_dl; ln.linia_ukladu=linia_ukladu; ln.poziom=poziom;
        linie_[liczba_wizualnych_++]=ln; if(szer>maks_szerokosc_) maks_szerokosc_=szer;
    }
    // A soft-wrap boundary: line li ends exactly where line li+1 starts.
    bool miekka_granica(size_t li) const {
        return li + 1 < liczba_wizualnych_ && linie_[li].koniec == linie_[li + 1].start && linie_[li].koniec > linie_[li].start;
    }
    bool miekka_granica_bajtu(size_t b) const {
        for (size_t i = 0; i + 1 < liczba_wizualnych_; ++i) if (linie_[i].koniec == b) return miekka_granica(i);
        return false;
    }
    // Caret line with affinity: a byte at a soft-wrap boundary belongs to the
    // following line (downstream) unless the caret was placed at the end of
    // the previous line (End, click past the line end, vertical move).
    size_t linia_dla_bajtu(size_t b) const {
        for(size_t i=0;i<liczba_wizualnych_;++i) if(b>=linie_[i].start && b<=linie_[i].koniec) {
            if (b == linie_[i].koniec && miekka_granica(i) && !(b == kursor_ && afinicja_wstecz_)) return i + 1;
            return i;
        }
        return liczba_wizualnych_?liczba_wizualnych_-1:0;
    }
#if defined(BSF_NATIVE)
    int piksele_bsf() const { return ui_skaluj(BSF::ui_size(),skala_dpi_.procent()); }
#if BSF_COMPLEX_TEXT
    // Owned, bounded cache of paragraph layouts (LRU). Keyed by content, so
    // an edit only rebuilds the edited paragraph; font configuration and
    // catalog changes invalidate through BSF::generation().
    const BSF::TextLayout* uklad_akapitu(size_t a,size_t n) const {
        if(!n||n>BSF::MaxLayoutBytes||a+n>dlugosc_)return nullptr;
        uint64_t h=UINT64_C(1469598103934665603);
        for(size_t i=0;i<n;++i)h=(h^uint8_t(bufor_[a+i]))*UINT64_C(1099511628211);
        int px=piksele_bsf();int64_t szer=zawijanie_?int64_t(limit_ukladu_)*BSF::Fixed:0;uint32_t gen=BSF::generation();
        for(auto& u:uklady_){
            if(!u.wazny||u.skrot!=h||u.dlugosc!=n||u.px!=px||u.szerokosc!=szer||u.kierunek!=uint8_t(kierunek_)||u.generacja!=gen)continue;
            const char* own=u.uklad.text();bool rowne=true;for(size_t i=0;i<n&&rowne;++i)rowne=own[i]==bufor_[a+i];
            if(!rowne)continue;
            u.uzycie=++zegar_ukladu_;++trafienia_ukladu_;return &u.uklad;
        }
        ++chybienia_ukladu_;
        UkladAkapitu* ofiara=&uklady_[0];
        for(auto& u:uklady_)if(!u.wazny||(ofiara->wazny&&u.uzycie<ofiara->uzycie))ofiara=&u;
        ofiara->wazny=false;
        BSF::LayoutRequest r;r.px=px;r.max_width=szer;r.direction=CzcionkaUI::kierunek_bsf(kierunek_);
        if(!BSF::build_layout(ofiara->uklad,bufor_+a,n,r))return nullptr;
        ofiara->skrot=h;ofiara->dlugosc=n;ofiara->px=px;ofiara->szerokosc=szer;ofiara->kierunek=uint8_t(kierunek_);ofiara->generacja=gen;
        ofiara->uzycie=++zegar_ukladu_;ofiara->wazny=true;return &ofiara->uklad;
    }
#endif
    // Paragraph layout of a visual line when it is drawn through BSF 2.1,
    // else null (legacy LTR path). Validated against the line's bytes.
    const BSF::TextLayout* uklad_linii(size_t li) const {
#if !BSF_COMPLEX_TEXT
        (void)li;return nullptr;
#else
        if(li>=liczba_wizualnych_||!linie_[li].zlozona||!BSF::ready())return nullptr;
        const Linia& ln=linie_[li];
        const auto* L=uklad_akapitu(ln.akapit,ln.akapit_dl);
        if(!L||ln.linia_ukladu>=L->line_count())return nullptr;
        const auto& vl=L->lines()[ln.linia_ukladu];
        if(ln.akapit+vl.byte_begin!=ln.start||ln.akapit+vl.byte_end!=ln.koniec)return nullptr;
        return L;
#endif
    }
#endif
    int x_w_linii(size_t li,size_t bajt) const {
        if(li>=liczba_wizualnych_) return 0;
        if(bajt<linie_[li].start) bajt=linie_[li].start;
        if(bajt>linie_[li].koniec) bajt=linie_[li].koniec;
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if(const auto* L=uklad_linii(li))
            return linie_[li].przesuniecie+int((L->caret_x_on(bajt-linie_[li].akapit,linie_[li].linia_ukladu)+BSF::Fixed/2)/BSF::Fixed);
#endif
        return linie_[li].przesuniecie+szerokosc_fragmentu(linie_[li].start,bajt);
    }
    int szerokosc_fragmentu(size_t a,size_t b) const {
#if defined(BSF_NATIVE)
        if(b<=65536){
            size_t at=a;BSF::TextMetrics pen;
            size_t page=b/64;if(page>=1025)page=1024;
            while(true){const auto& point=pomiary_[page];if(point.offset!=size_t(-1)&&point.line==a&&point.offset<=b){at=point.offset;pen=point.pen;break;}if(!page||page*64<=a)break;--page;}
            while(at<b){size_t end=BSF::next_cluster(bufor_,at,b);
                if(BSF::ready())pen.cluster(bufor_,at,end,ui_skaluj(BSF::ui_size(),skala_dpi_.procent()));
                else for(size_t p=at;p<end;){uint32_t cp;size_t z=CzcionkaUI::dekoduj_utf8(bufor_,p,&cp);pen.width+=CzcionkaUI::postep_znaku(cp,skala_dpi_);p+=z;}
                at=end;
            }return int(pen.width);
        }
#endif
        int x=0; while(a<b && a<dlugosc_){uint32_t cp=0;size_t z=CzcionkaUI::dekoduj_utf8(bufor_,a,&cp);if(!z)break;x+=CzcionkaUI::postep_znaku(cp,skala_dpi_);a+=z;} return x;
    }
    // Hit test on visual line li at view x; *wstecz reports that the result
    // is the end of li at a soft-wrap boundary (caret stays on li).
    size_t bajt_z_x(size_t li,int x,bool* wstecz=nullptr) const {
        if(wstecz)*wstecz=false;
        if(li>=liczba_wizualnych_) return dlugosc_;
        size_t wynik=bajt_z_x_bez_afinicji(li,x-linie_[li].przesuniecie);
        if(wstecz)*wstecz=wynik==linie_[li].koniec&&miekka_granica(li);
        return wynik;
    }
    size_t bajt_z_x_bez_afinicji(size_t li,int x) const {
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if(const auto* L=uklad_linii(li))
            return linie_[li].akapit+L->hit_test(int64_t(x)*BSF::Fixed,linie_[li].linia_ukladu);
#endif
#if defined(BSF_NATIVE)
        if(BSF::ready())return linie_[li].start+BSF::hit_test(bufor_+linie_[li].start,x,ui_skaluj(BSF::ui_size(),skala_dpi_.procent()),false,nullptr,linie_[li].koniec-linie_[li].start);
#endif
        size_t p=linie_[li].start; int px=0;
        while(p<linie_[li].koniec){uint32_t cp=0;size_t z=CzcionkaUI::dekoduj_utf8(bufor_,p,&cp);int k=CzcionkaUI::postep_znaku(cp,skala_dpi_);if(x<px+k/2)return p;px+=k;p+=z;}return linie_[li].koniec;
    }
    size_t bajt_z_punktu(int x, int y, bool* wstecz = nullptr) const {
        if (wstecz) *wstecz = false;
        if (dlugosc_ == 0 || liczba_wizualnych_ == 0) return 0;
        Geometria g = geometria();
        if (y < g.widok.y) {
            size_t li = static_cast<size_t>(pionowy_->wartosc());
            if (li >= liczba_wizualnych_) li = liczba_wizualnych_ - 1;
            return bajt_z_x(li, x - g.widok.x + static_cast<int>(poziomy_->wartosc()), wstecz);
        }
        int rel = (y - g.widok.y) / wysokosc_linii();
        if (rel < 0) rel = 0;
        size_t li = static_cast<size_t>(pionowy_->wartosc()) + static_cast<size_t>(rel);
        if (li >= liczba_wizualnych_) {
            return dlugosc_;
        }
        return bajt_z_x(li, x - g.widok.x + static_cast<int>(poziomy_->wartosc()), wstecz);
    }
    size_t granica_utf8(size_t b) const {
#if defined(BSF_NATIVE)
        return granice_.floor(bufor_,b);
#else
        if(b>=dlugosc_)return dlugosc_;while(b>0&&(static_cast<uint8_t>(bufor_[b])&0xC0U)==0x80U)--b;return b;
#endif
    }
    size_t poprzedni_klaster(size_t p)const{
#if defined(BSF_NATIVE)
        return granice_.previous(bufor_,p);
#else
        return CzcionkaUI::poprzedni_znak(bufor_,p);
#endif
    }
    size_t poprzedni_bajt(size_t p,bool slowo) const { if(!p)return 0;p=poprzedni_klaster(p);if(slowo){while(p>0&&bufor_[p]==' ')p=poprzedni_klaster(p);while(p>0&&bufor_[poprzedni_klaster(p)]!=' '&&bufor_[poprzedni_klaster(p)]!='\n')p=poprzedni_klaster(p);}return p; }
    size_t nastepny_bajt(size_t p,bool slowo) const { if(p>=dlugosc_)return dlugosc_;p=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_);if(slowo){while(p<dlugosc_&&bufor_[p]!=' '&&bufor_[p]!='\n')p=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_);while(p<dlugosc_&&bufor_[p]==' ')p=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_);}return p; }
    // Left/Right on a line drawn through the paragraph layout move visually.
    // At the visual edge the caret continues on the adjacent visual line in
    // the reading order of the current paragraph (Right is forward in LTR,
    // backward in RTL) and lands on the edge from which the same key keeps
    // progressing (Right: leftmost caret, Left: rightmost). A backward
    // crossing into a line of the opposite direction is refused: otherwise
    // [LTR][RTL] paragraphs would form a cycle (LTR end -> RTL start -> LTR
    // end ...). With this rule a held key moves monotonically through each
    // run of same-direction lines and only enters a new run forward, so it
    // always terminates and never revisits a position. false: legacy LTR
    // line, logical move.
    bool ruch_wizualny(int dir,bool& wstecz){
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        wstecz=false;
        size_t li=linia_dla_bajtu(kursor_);const auto* L=uklad_linii(li);if(!L)return false;
        const Linia& ln=linie_[li];
        bool moved=false;size_t m=L->move_visual_on(kursor_-ln.akapit,ln.linia_ukladu,dir,&moved);
        if(moved){kursor_=ln.akapit+m;wstecz=kursor_==ln.koniec&&miekka_granica(li);return true;}
        bool naprzod=(dir>0)!=((ln.poziom&1)!=0);
        if(naprzod?li+1>=liczba_wizualnych_:li==0)return true; // document edge: stay
        size_t nl=naprzod?li+1:li-1;
        if(!naprzod&&((linie_[nl].poziom^ln.poziom)&1))return true;
        if(const auto* N=uklad_linii(nl))kursor_=linie_[nl].akapit+N->line_edge(linie_[nl].linia_ukladu,-dir);
        else kursor_=dir>0?linie_[nl].start:linie_[nl].koniec;
        wstecz=kursor_==linie_[nl].koniec&&miekka_granica(nl);
        return true;
#else
        (void)dir;wstecz=false;return false;
#endif
    }
    void przesun_pionowo(int delta,bool& wstecz){size_t li=linia_dla_bajtu(kursor_);int n=static_cast<int>(li)+delta;if(n<0)n=0;if(n>=static_cast<int>(liczba_wizualnych_))n=static_cast<int>(liczba_wizualnych_)-1;kursor_=bajt_z_x(static_cast<size_t>(n),preferowane_x_,&wstecz);}

    void rysuj_linie(KontekstRysowania& ctx,size_t li,int y,const Geometria& g,const StylStanu& styl) {
        Linia ln=linie_[li]; int offx=static_cast<int>(poziomy_->wartosc());
        int x0=g.widok.x-offx;int h=CzcionkaUI::pobierz_wysokosc(skala_dpi_);
        uint32_t kolor=czy_wlaczona_?styl.tekst:motyw_kolor(MOTYW_KOLOR_WYLACZONY);
        // Visual selection spans (several for mixed-direction selections).
        int sx0[64],sx1[64];size_t ns=spany_zaznaczenia(li,sx0,sx1,64);
        for(size_t k=0;k<ns;++k)ctx.rysuj_prostokat(Prostokat(x0+sx0[k],y,sx1[k]-sx0[k],h),motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
        if(const auto* L=uklad_linii(li)){
            int px=piksele_bsf();
            ctx.rysuj_linie_ukladu(Punkt(x0+ln.przesuniecie,y),*L,ln.linia_ukladu,kolor,px);
            for(size_t k=0;k<ns;++k){
                // The paint path may evict nothing here: the layout is owned.
                ctx.dodaj_przyciecie(Prostokat(x0+sx0[k],y,sx1[k]-sx0[k],h));
                ctx.rysuj_linie_ukladu(Punkt(x0+ln.przesuniecie,y),*L,ln.linia_ukladu,motyw_kolor_tekstu_zaznaczenia(),px);
                ctx.zdejmij_przyciecie();
            }
            return;
        }
#endif
        size_t n=ln.koniec-ln.start;if(n>=pojemnosc_)n=pojemnosc_-1;for(size_t i=0;i<n;++i)roboczy_[i]=bufor_[ln.start+i];roboczy_[n]='\0';
        ctx.rysuj_tekst(Punkt(x0+ln.przesuniecie,y),roboczy_,kolor,skala_dpi_);
        for(size_t k=0;k<ns;++k){
            ctx.dodaj_przyciecie(Prostokat(x0+sx0[k],y,sx1[k]-sx0[k],h));
            ctx.rysuj_tekst(Punkt(x0+ln.przesuniecie,y),roboczy_,motyw_kolor_tekstu_zaznaczenia(),skala_dpi_);ctx.zdejmij_przyciecie();
        }
    }
    void ustaw_tekst_bez_historii(const char* src){
        size_t n=0;while(src[n])++n;size_t j=n<pojemnosc_?n:pojemnosc_-1;
#if defined(BSF_NATIVE)
        j=BSF::cluster_prefix(src,n,j);
#else
        while(j&&(static_cast<uint8_t>(src[j])&0xc0U)==0x80U)--j;
#endif
        for(size_t i=0;i<j;++i)bufor_[i]=src[i];
        bufor_[j]=0;dlugosc_=j;kursor_=kotwica_=j;przelicz_linie();aktualizuj_przewijanie();przewin_do_kursora();uniewaznij_obszar(geometria().widok);
    }
    void wstaw_fragment(const char* src) {
        if (!src) return;
        size_t znaki = CzcionkaUI::liczba_znakow(bufor_);
        size_t sel = poczatek_zaznaczenia();
        size_t end = koniec_zaznaczenia();
        size_t usun = end - sel;
        size_t n = 0;
        while (src[n]) ++n;
        size_t src_zn = CzcionkaUI::liczba_znakow(src);
#if defined(BSF_NATIVE)
        znaki=BSF::cluster_count(bufor_,dlugosc_);src_zn=BSF::cluster_count(src,n);
#endif
        size_t source_size=n;
        size_t sel_zn = 0;
        for (size_t p = sel; p < end;
             p = CzcionkaUI::nastepny_znak(bufor_, p, end)) ++sel_zn;
        if (maks_znakow_ && znaki - sel_zn + src_zn > maks_znakow_) {
            while (src_zn && znaki - sel_zn + src_zn > maks_znakow_) {
                n = CzcionkaUI::poprzedni_znak(src, n);
                --src_zn;
            }
        }
        if (dlugosc_ - usun + n >= pojemnosc_)
            n = pojemnosc_ - 1 - (dlugosc_ - usun);
#if defined(BSF_NATIVE)
        n=BSF::cluster_prefix(src,source_size,n);
#else
        (void)source_size;
        while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0U) == 0x80U) --n;
#endif

        zapisz_historie();
        if (n <= usun) {
            // Przesuniecie w lewo ma nakladajace sie zrodlo i cel, dlatego
            // kopiujemy od poczatku ogona (semantyka memmove).
            for (size_t i = end; i <= dlugosc_; ++i)
                bufor_[sel + n + (i - end)] = bufor_[i];
        } else {
            // Przy rozszerzaniu kopiujemy od NUL-a wstecz, aby nie
            // nadpisac jeszcze nieprzeniesionego ogona.
            for (size_t i = dlugosc_ + 1; i > end; --i)
                bufor_[i - usun + n - 1] = bufor_[i - 1];
        }
        for (size_t i = 0; i < n; ++i) bufor_[sel + i] = src[i];
        dlugosc_ = dlugosc_ - usun + n;
        bufor_[dlugosc_] = '\0';
        kursor_ = kotwica_ = sel + n;
        afinicja_wstecz_ = false;
#if defined(BSF_NATIVE)
        kursor_=kotwica_=BSF::cluster_ceil(bufor_,dlugosc_,kursor_);
#endif
        po_edycji(sel);
    }
    void usun_zaznaczenie(){if(!czy_ma_zaznaczenie())return;wstaw_fragment("");}
    void usun_wstecz(){if(czy_ma_zaznaczenie()){usun_zaznaczenie();return;}if(!kursor_)return;kotwica_=poprzedni_klaster(kursor_);wstaw_fragment("");}
    void usun_w_przod(){if(czy_ma_zaznaczenie()){usun_zaznaczenie();return;}if(kursor_>=dlugosc_)return;kotwica_=CzcionkaUI::nastepny_znak(bufor_,kursor_,dlugosc_);wstaw_fragment("");}
    void po_edycji(size_t od){przelicz_linie();aktualizuj_przewijanie();przewin_do_kursora();size_t li=linia_dla_bajtu(od);Geometria g=geometria();int pierwszy=static_cast<int>(pionowy_->wartosc());int y=g.widok.y+(static_cast<int>(li)-pierwszy)*wysokosc_linii();if(y<g.widok.y)y=g.widok.y;uniewaznij_obszar(Prostokat(g.widok.x,y,g.widok.szerokosc,g.widok.dolny()-y));powiadom_zmiane();}
    void kopiuj(){if(!czy_ma_zaznaczenie())return;size_t a=poczatek_zaznaczenia(),n=koniec_zaznaczenia()-a;if(n>=pojemnosc_)n=pojemnosc_-1;for(size_t i=0;i<n;++i)schowek_[i]=bufor_[a+i];schowek_[n]='\0';clipboard_set("text/plain",schowek_,n+1);}
    void wklej(){uint64_t n=0;if(clipboard_read("text/plain",schowek_,pojemnosc_-1,&n)&&n){if(n>=pojemnosc_)n=pojemnosc_-1;schowek_[n]='\0';wstaw_fragment(schowek_);}}
    void zapisz_migawke(Migawka& m)const{for(size_t i=0;i<=dlugosc_;++i)m.tekst[i]=bufor_[i];m.dlugosc=dlugosc_;m.kursor=kursor_;m.kotwica=kotwica_;}
    void zapisz_historie(){if(hist_pozycja_>=MAKS_HISTORIA-1){Migawka* p=historia_[0];for(size_t i=0;i+1<MAKS_HISTORIA;++i)historia_[i]=historia_[i+1];historia_[MAKS_HISTORIA-1]=p;hist_pozycja_=MAKS_HISTORIA-2;}zapisz_migawke(*historia_[hist_pozycja_]);++hist_pozycja_;hist_liczba_=hist_pozycja_;}
    void przywroc_migawke(const Migawka&m){for(size_t i=0;i<=m.dlugosc;++i)bufor_[i]=m.tekst[i];dlugosc_=m.dlugosc;kursor_=m.kursor;kotwica_=m.kotwica;przelicz_linie();aktualizuj_przewijanie();przewin_do_kursora();uniewaznij_obszar(geometria().widok);powiadom_zmiane();}
    void powiadom_zmiane(){if(cb_zmiana_)cb_zmiana_(this,bufor_,dane_zmiany_);if(cb_zmiana_wartosci_)cb_zmiana_wartosci_(this,dane_zmiany_wartosci_);}
    void uniewaznij_zaznaczenie(size_t,size_t){uniewaznij_obszar(geometria().widok);}

    KierunekTekstu kierunek_{KierunekTekstu::Automatyczny};
    bool afinicja_wstecz_{false};
    int limit_ukladu_{1};
#if defined(BSF_NATIVE) && BSF_COMPLEX_TEXT
    struct UkladAkapitu { BSF::TextLayout uklad; uint64_t skrot{},uzycie{}; size_t dlugosc{}; int px{}; int64_t szerokosc{}; uint8_t kierunek{}; uint32_t generacja{}; bool wazny{}; };
    static constexpr unsigned SLOTY_UKLADU = 4;
    mutable UkladAkapitu uklady_[SLOTY_UKLADU];
    mutable uint64_t zegar_ukladu_{0},trafienia_ukladu_{0},chybienia_ukladu_{0};
#endif
#if defined(BSF_NATIVE)
    BSF::BoundaryIndex<> granice_;
    struct PunktPomiaru{size_t offset{size_t(-1)},line{};BSF::TextMetrics pen;};
    PunktPomiaru pomiary_[1025];
#endif
    char* bufor_{nullptr}; char* roboczy_{nullptr}; char* schowek_{nullptr}; size_t pojemnosc_{0},dlugosc_{0},kursor_{0},kotwica_{0};
    char placeholder_[128]{}; size_t maks_znakow_{0}; bool zawijanie_{true},pozwol_poziomy_{true},pozwol_pionowy_{true},zaznaczanie_{false}; int odstep_linii_{2},preferowane_x_{0},maks_szerokosc_{0};
    size_t maks_linii() const { return pojemnosc_ + 1; }
    bool powieksz_linie() {
        size_t n = pojemnosc_linii_ ? pojemnosc_linii_ * 2 : MAKS_LINII_WIZUALNYCH;
        if (n > maks_linii()) n = maks_linii();
        if (n <= pojemnosc_linii_) return false;
        Linia* nowe = new Linia[n];
        if (!nowe) return false;
        for (size_t i = 0; i < liczba_wizualnych_; ++i) nowe[i] = linie_[i];
        delete[] linie_; linie_ = nowe; pojemnosc_linii_ = n;
        return true;
    }
    Linia* linie_{nullptr}; size_t pojemnosc_linii_{0}; size_t liczba_wizualnych_{0},liczba_logicznych_{1}; mutable size_t ostatnio_narysowane_{0};
    Migawka* historia_[MAKS_HISTORIA]{}; size_t hist_liczba_{0},hist_pozycja_{0};
    PasekPrzewijania* pionowy_{nullptr}; PasekPrzewijania* poziomy_{nullptr};
    FunkcjaMenuKontekstowego cb_menu_kontekstowe_{nullptr};
    void* dane_menu_kontekstowego_{nullptr};
    FunkcjaTekstu cb_zmiana_{nullptr},cb_zatwierdzenie_{nullptr}; void* dane_zmiany_{nullptr}; void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
