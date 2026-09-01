#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

class PoleWielowierszowe : public Kontrolka {
public:
    static constexpr size_t DOMYSLNA_POJEMNOSC = 8192;
    static constexpr size_t MAKS_LINII_WIZUALNYCH = 2048;
    static constexpr size_t MAKS_HISTORIA = 8;
    using FunkcjaTekstu = void (*)(PoleWielowierszowe*, const char*, void*);
    using FunkcjaMenuKontekstowego = void (*)(PoleWielowierszowe*, ZdarzenieMyszki&, void*);

    explicit PoleWielowierszowe(const char* placeholder = "") {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        wypelnienie_ = Marginesy(6, 5, 6, 5);
        czy_moze_miec_fokus_ = true;
        pojemnosc_ = DOMYSLNA_POJEMNOSC;
        bufor_ = new char[pojemnosc_];
        roboczy_ = new char[pojemnosc_];
        schowek_ = new char[pojemnosc_];
        for (size_t i = 0; i < MAKS_HISTORIA; ++i) historia_[i] = new Migawka(pojemnosc_);
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

    ~PoleWielowierszowe() override {
        for (size_t i = 0; i < MAKS_HISTORIA; ++i) delete historia_[i];
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
        przewin_do_kursora();
        uniewaznij_obszar(geometria().widok);
    }
    void zaznacz_wszystko() { ustaw_zaznaczenie(0, dlugosc_); }
    void kopiuj_do_schowka() { kopiuj(); }
    void wytnij_do_schowka() {
        if (!czy_tylko_do_odczytu_) { kopiuj(); usun_zaznaczenie(); }
    }
    void wklej_ze_schowka() { if (!czy_tylko_do_odczytu_) wklej(); }
    void usun_zaznaczony_tekst() { if (!czy_tylko_do_odczytu_) usun_zaznaczenie(); }

    bool cofnij() {
        if (hist_pozycja_ == 0) return false;
        if (hist_pozycja_ == hist_liczba_) zapisz_migawke(*historia_[hist_pozycja_]);
        --hist_pozycja_;
        przywroc_migawke(*historia_[hist_pozycja_]);
        return true;
    }
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
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
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
        if (g.widok.czy_pusty()) return;
        ctx.dodaj_przyciecie(g.widok);
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
        int wys = wysokosc_linii();
        size_t pierwszy = static_cast<size_t>(pionowy_->wartosc());
        size_t koniec = pierwszy + static_cast<size_t>(g.widok.wysokosc / wys + 1);
        if (koniec > liczba_wizualnych_) koniec = liczba_wizualnych_;
        if (dlugosc_ == 0 && placeholder_[0]) {
            ctx.rysuj_tekst(Punkt(g.widok.x, g.widok.y), placeholder_,
                            Kolory::TekstZastepczy, s);
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
                                    Kolory::AkcentFokus);
            }
        }
        ctx.zdejmij_przyciecie();
    }

    void obsluz_wprowadzenie_tekstu(ZdarzenieWprowadzaniaTekstu& e) override {
        if (!czy_wlaczona_ || czy_tylko_do_odczytu_ || e.punkt_kodowy < 32) return;
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
        size_t p = bajt_z_punktu(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
        if (!e.czy_shift()) kotwica_ = p;
        kursor_ = p;
        zaznaczanie_ = true;
        przewin_do_kursora();
        uniewaznij_obszar(geometria().widok);
        e.obsluzone = true;
    }
    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        if (zaznaczanie_) {
            kursor_ = bajt_z_punktu(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y);
            przewin_do_kursora();
            uniewaznij_obszar(geometria().widok);
            e.obsluzone = true;
        }
    }
    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        if (e.czy_lewy_przycisk() && zaznaczanie_) {
            zaznaczanie_ = false; e.obsluzone = true;
        }
    }
    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0) return;
        pionowy_->przewin_o(e.delta_y > 0 ? -3 : 3);
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_) return;
        bool ctrl = e.czy_ctrl(), shift = e.czy_shift();
        uint32_t k = e.kod_klawisza;
        uint32_t b = (k & BWS_KEY_SOURCE_USB) ? k & 0xFFU : k & 0x7FU;
        if (ctrl && klawisz_litery(e, 'a', 0x1E)) { zaznacz_wszystko(); e.obsluzone = true; return; }
        if (ctrl && klawisz_litery(e, 'c', 0x2E)) { kopiuj(); e.obsluzone = true; return; }
        if (ctrl && klawisz_litery(e, 'x', 0x2D)) { if (!czy_tylko_do_odczytu_) { kopiuj(); usun_zaznaczenie(); } e.obsluzone = true; return; }
        if (ctrl && klawisz_litery(e, 'v', 0x2F)) { if (!czy_tylko_do_odczytu_) wklej(); e.obsluzone = true; return; }
        if (ctrl && !shift && klawisz_litery(e, 'z', 0x2C)) { if (!czy_tylko_do_odczytu_) cofnij(); e.obsluzone = true; return; }
        if (ctrl && (klawisz_litery(e, 'y', 0x15) || (shift && klawisz_litery(e, 'z', 0x2C)))) { if (!czy_tylko_do_odczytu_) ponow(); e.obsluzone = true; return; }
        if (k == 13 || k == 10 || b == 0x1CU) {
            if (!czy_tylko_do_odczytu_) wstaw_fragment("\n");
            if (cb_zatwierdzenie_) cb_zatwierdzenie_(this, bufor_, dane_zatwierdzenia_);
            e.obsluzone = true; return;
        }
        if (k == 8) { if (!czy_tylko_do_odczytu_) usun_wstecz(); e.obsluzone = true; return; }
        if (k == 127 || b == 0x53U) { if (!czy_tylko_do_odczytu_) usun_w_przod(); e.obsluzone = true; return; }
        size_t stary = kursor_;
        bool ruch = true;
        if (ctrl && ((k & BWS_KEY_SOURCE_USB && b == 74U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x47U))) kursor_ = 0;
        else if (ctrl && ((k & BWS_KEY_SOURCE_USB && b == 77U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x4FU))) kursor_ = dlugosc_;
        else if ((k & BWS_KEY_SOURCE_USB && b == 80U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x4BU)) kursor_ = poprzedni_bajt(kursor_, ctrl);
        else if ((k & BWS_KEY_SOURCE_USB && b == 79U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x4DU)) kursor_ = nastepny_bajt(kursor_, ctrl);
        else if ((k & BWS_KEY_SOURCE_USB && b == 82U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x48U)) przesun_pionowo(-1);
        else if ((k & BWS_KEY_SOURCE_USB && b == 81U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x50U)) przesun_pionowo(1);
        else if ((k & BWS_KEY_SOURCE_USB && b == 75U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x49U)) przesun_pionowo(-liczba_widocznych_linii());
        else if ((k & BWS_KEY_SOURCE_USB && b == 78U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x51U)) przesun_pionowo(liczba_widocznych_linii());
        else if ((k & BWS_KEY_SOURCE_USB && b == 74U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x47U)) kursor_ = linie_[linia_dla_bajtu(kursor_)].start;
        else if ((k & BWS_KEY_SOURCE_USB && b == 77U) || (!(k & BWS_KEY_SOURCE_USB) && b == 0x4FU)) kursor_ = linie_[linia_dla_bajtu(kursor_)].koniec;
        else ruch = false;
        if (ruch) {
            if (!shift) kotwica_ = kursor_;
            preferowane_x_ = x_w_linii(linia_dla_bajtu(kursor_), kursor_);
            przewin_do_kursora();
            uniewaznij_zaznaczenie(stary, kursor_);
            e.obsluzone = true; return;
        }
        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    struct Linia { size_t start{0}, koniec{0}, logiczna{0}; int szerokosc{0}; };
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
        return e.kod_klawisza == static_cast<uint32_t>(l) ||
               e.kod_klawisza == static_cast<uint32_t>(l - 'a' + 'A') || e.kod_skanu == scan;
    }
    int wysokosc_linii() const { return CzcionkaUI::pobierz_wysokosc(skala_dpi_) + odstep_linii_ * skala_dpi_; }
    int grubosc_paska() const { return 18 * skala_dpi_; }

    Prostokat obszar_po_ramce_i_wypelnieniu() const {
        Prostokat r(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        int gr = grubosc_ramki_geometrii();
        r = r.zmniejszony(Marginesy(gr));
        int s = skala_dpi_ < 1 ? 1 : skala_dpi_;
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
        liczba_wizualnych_ = 0; liczba_logicznych_ = 1; maks_szerokosc_ = 0;
        size_t start = 0, logiczna = 0;
        if (limit < 1) limit = 1;
        while (start <= dlugosc_ && liczba_wizualnych_ < MAKS_LINII_WIZUALNYCH) {
            size_t koniec_log = start;
            while (koniec_log < dlugosc_ && bufor_[koniec_log] != '\n') ++koniec_log;
            size_t p = start;
            if (p == koniec_log) dodaj_linie(p, p, logiczna, 0);
            while (p < koniec_log && liczba_wizualnych_ < MAKS_LINII_WIZUALNYCH) {
                size_t q = p; int szer = 0, ostatnia_spacja = -1, szer_spacji = 0;
                while (q < koniec_log) {
                    uint32_t cp=0; size_t z=CzcionkaUI::dekoduj_utf8(bufor_,q,&cp); if (!z) break;
                    int krok=CzcionkaUI::postep_znaku(cp,skala_dpi_);
                    if (zawijanie_ && szer+krok>limit && q>p) break;
                    szer+=krok; q+=z;
                    if (cp==' ' || cp=='\t') { ostatnia_spacja=static_cast<int>(q); szer_spacji=szer; }
                }
                if (zawijanie_ && q < koniec_log && ostatnia_spacja > static_cast<int>(p)) { q=static_cast<size_t>(ostatnia_spacja); szer=szer_spacji; }
                if (q==p) q=CzcionkaUI::nastepny_znak(bufor_,p,koniec_log);
                dodaj_linie(p,q,logiczna,szer); p=q;
            }
            if (koniec_log >= dlugosc_) break;
            start=koniec_log+1; ++logiczna; ++liczba_logicznych_;
        }
        if (liczba_wizualnych_==0) dodaj_linie(0,0,0,0);
    }
    void dodaj_linie(size_t a,size_t b,size_t log,int szer) {
        if (liczba_wizualnych_>=MAKS_LINII_WIZUALNYCH) return;
        linie_[liczba_wizualnych_++]={a,b,log,szer}; if(szer>maks_szerokosc_) maks_szerokosc_=szer;
    }
    size_t linia_dla_bajtu(size_t b) const {
        for(size_t i=0;i<liczba_wizualnych_;++i) if(b>=linie_[i].start && b<=linie_[i].koniec) return i;
        return liczba_wizualnych_?liczba_wizualnych_-1:0;
    }
    int x_w_linii(size_t li,size_t bajt) const {
        if(li>=liczba_wizualnych_) return 0;
        if(bajt<linie_[li].start) bajt=linie_[li].start;
        return szerokosc_fragmentu(linie_[li].start,bajt);
    }
    int szerokosc_fragmentu(size_t a,size_t b) const {
        int x=0; while(a<b && a<dlugosc_){uint32_t cp=0;size_t z=CzcionkaUI::dekoduj_utf8(bufor_,a,&cp);if(!z)break;x+=CzcionkaUI::postep_znaku(cp,skala_dpi_);a+=z;} return x;
    }
    size_t bajt_z_x(size_t li,int x) const {
        if(li>=liczba_wizualnych_) return dlugosc_;
        size_t p=linie_[li].start; int px=0;
        while(p<linie_[li].koniec){uint32_t cp=0;size_t z=CzcionkaUI::dekoduj_utf8(bufor_,p,&cp);int k=CzcionkaUI::postep_znaku(cp,skala_dpi_);if(x<px+k/2)return p;px+=k;p+=z;}return linie_[li].koniec;
    }
    size_t bajt_z_punktu(int x,int y) const {
        Geometria g=geometria(); int rel=(y-g.widok.y)/wysokosc_linii(); if(rel<0)rel=0;
        size_t li=static_cast<size_t>(pionowy_->wartosc()+rel);if(li>=liczba_wizualnych_)li=liczba_wizualnych_-1;
        return bajt_z_x(li,x-g.widok.x+static_cast<int>(poziomy_->wartosc()));
    }
    size_t granica_utf8(size_t b) const { if(b>=dlugosc_)return dlugosc_;while(b>0&&(static_cast<uint8_t>(bufor_[b])&0xC0U)==0x80U)--b;return b; }
    size_t poprzedni_bajt(size_t p,bool slowo) const { if(!p)return 0;p=CzcionkaUI::poprzedni_znak(bufor_,p);if(slowo){while(p>0&&bufor_[p]==' ')p=CzcionkaUI::poprzedni_znak(bufor_,p);while(p>0&&bufor_[CzcionkaUI::poprzedni_znak(bufor_,p)]!=' '&&bufor_[CzcionkaUI::poprzedni_znak(bufor_,p)]!='\n')p=CzcionkaUI::poprzedni_znak(bufor_,p);}return p; }
    size_t nastepny_bajt(size_t p,bool slowo) const { if(p>=dlugosc_)return dlugosc_;p=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_);if(slowo){while(p<dlugosc_&&bufor_[p]!=' '&&bufor_[p]!='\n')p=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_);while(p<dlugosc_&&bufor_[p]==' ')p=CzcionkaUI::nastepny_znak(bufor_,p,dlugosc_);}return p; }
    void przesun_pionowo(int delta){size_t li=linia_dla_bajtu(kursor_);int n=static_cast<int>(li)+delta;if(n<0)n=0;if(n>=static_cast<int>(liczba_wizualnych_))n=static_cast<int>(liczba_wizualnych_)-1;kursor_=bajt_z_x(static_cast<size_t>(n),preferowane_x_);}

    void rysuj_linie(KontekstRysowania& ctx,size_t li,int y,const Geometria& g,const StylStanu& styl) {
        Linia ln=linie_[li]; int offx=static_cast<int>(poziomy_->wartosc());
        size_t so=poczatek_zaznaczenia(), sd=koniec_zaznaczenia();
        size_t a=so>ln.start?so:ln.start,b=sd<ln.koniec?sd:ln.koniec;
        if(czy_ma_zaznaczenie()&&a<b){int x1=g.widok.x-offx+szerokosc_fragmentu(ln.start,a);int x2=g.widok.x-offx+szerokosc_fragmentu(ln.start,b);ctx.rysuj_prostokat(Prostokat(x1,y,x2-x1,CzcionkaUI::pobierz_wysokosc(skala_dpi_)),Kolory::ZaznaczenieTlo);}
        size_t n=ln.koniec-ln.start;if(n>=pojemnosc_)n=pojemnosc_-1;for(size_t i=0;i<n;++i)roboczy_[i]=bufor_[ln.start+i];roboczy_[n]='\0';
        ctx.rysuj_tekst(Punkt(g.widok.x-offx,y),roboczy_,czy_wlaczona_?styl.tekst:Kolory::WylaczonyTekst,skala_dpi_);
        if(czy_ma_zaznaczenie()&&a<b){size_t m=b-a;if(m>=pojemnosc_)m=pojemnosc_-1;for(size_t i=0;i<m;++i)roboczy_[i]=bufor_[a+i];roboczy_[m]='\0';int x=g.widok.x-offx+szerokosc_fragmentu(ln.start,a);ctx.rysuj_tekst(Punkt(x,y),roboczy_,Kolory::ZaznaczenieTekst,skala_dpi_);}
    }
    void ustaw_tekst_bez_historii(const char* src){size_t j=0;for(size_t i=0;src[i]&&j+1<pojemnosc_;++i){if(src[i]=='\r'){if(src[i+1]=='\n')++i;bufor_[j++]='\n';}else bufor_[j++]=src[i];}bufor_[j]='\0';dlugosc_=j;kursor_=kotwica_=dlugosc_;przelicz_linie();aktualizuj_przewijanie();przewin_do_kursora();uniewaznij_obszar(geometria().widok);}
    void wstaw_fragment(const char* src){if(!src)return;size_t znaki=CzcionkaUI::liczba_znakow(bufor_);size_t sel=poczatek_zaznaczenia(),end=koniec_zaznaczenia();size_t usun=end-sel,n=0;while(src[n])++n;
        size_t src_zn=CzcionkaUI::liczba_znakow(src);size_t sel_zn=0;for(size_t p=sel;p<end;p=CzcionkaUI::nastepny_znak(bufor_,p,end))++sel_zn;if(maks_znakow_&&znaki-sel_zn+src_zn>maks_znakow_){while(src_zn&&znaki-sel_zn+src_zn>maks_znakow_){n=CzcionkaUI::poprzedni_znak(src,n);--src_zn;}}if(dlugosc_-usun+n>=pojemnosc_)n=pojemnosc_-1-(dlugosc_-usun);while(n>0&&(static_cast<uint8_t>(src[n])&0xC0U)==0x80U)--n;zapisz_historie();for(size_t i=dlugosc_+1;i>end;--i)bufor_[i-usun+n-1]=bufor_[i-1];for(size_t i=0;i<n;++i)bufor_[sel+i]=src[i];dlugosc_=dlugosc_-usun+n;bufor_[dlugosc_]='\0';kursor_=kotwica_=sel+n;po_edycji(sel);}
    void usun_zaznaczenie(){if(!czy_ma_zaznaczenie())return;wstaw_fragment("");}
    void usun_wstecz(){if(czy_ma_zaznaczenie()){usun_zaznaczenie();return;}if(!kursor_)return;kotwica_=CzcionkaUI::poprzedni_znak(bufor_,kursor_);wstaw_fragment("");}
    void usun_w_przod(){if(czy_ma_zaznaczenie()){usun_zaznaczenie();return;}if(kursor_>=dlugosc_)return;kotwica_=CzcionkaUI::nastepny_znak(bufor_,kursor_,dlugosc_);wstaw_fragment("");}
    void po_edycji(size_t od){przelicz_linie();aktualizuj_przewijanie();przewin_do_kursora();size_t li=linia_dla_bajtu(od);Geometria g=geometria();int pierwszy=static_cast<int>(pionowy_->wartosc());int y=g.widok.y+(static_cast<int>(li)-pierwszy)*wysokosc_linii();if(y<g.widok.y)y=g.widok.y;uniewaznij_obszar(Prostokat(g.widok.x,y,g.widok.szerokosc,g.widok.dolny()-y));powiadom_zmiane();}
    void kopiuj(){if(!czy_ma_zaznaczenie())return;size_t a=poczatek_zaznaczenia(),n=koniec_zaznaczenia()-a;if(n>=pojemnosc_)n=pojemnosc_-1;for(size_t i=0;i<n;++i)schowek_[i]=bufor_[a+i];schowek_[n]='\0';clipboard_set("text/plain",schowek_,n+1);}
    void wklej(){uint64_t n=0;if(clipboard_read("text/plain",schowek_,pojemnosc_-1,&n)&&n){if(n>=pojemnosc_)n=pojemnosc_-1;schowek_[n]='\0';wstaw_fragment(schowek_);}}
    void zapisz_migawke(Migawka& m)const{for(size_t i=0;i<=dlugosc_;++i)m.tekst[i]=bufor_[i];m.dlugosc=dlugosc_;m.kursor=kursor_;m.kotwica=kotwica_;}
    void zapisz_historie(){if(hist_pozycja_>=MAKS_HISTORIA-1){Migawka* p=historia_[0];for(size_t i=0;i+1<MAKS_HISTORIA;++i)historia_[i]=historia_[i+1];historia_[MAKS_HISTORIA-1]=p;hist_pozycja_=MAKS_HISTORIA-2;}zapisz_migawke(*historia_[hist_pozycja_]);++hist_pozycja_;hist_liczba_=hist_pozycja_;}
    void przywroc_migawke(const Migawka&m){for(size_t i=0;i<=m.dlugosc;++i)bufor_[i]=m.tekst[i];dlugosc_=m.dlugosc;kursor_=m.kursor;kotwica_=m.kotwica;przelicz_linie();aktualizuj_przewijanie();przewin_do_kursora();uniewaznij_obszar(geometria().widok);powiadom_zmiane();}
    void powiadom_zmiane(){if(cb_zmiana_)cb_zmiana_(this,bufor_,dane_zmiany_);if(cb_zmiana_wartosci_)cb_zmiana_wartosci_(this,dane_zmiany_wartosci_);}
    void uniewaznij_zaznaczenie(size_t,size_t){uniewaznij_obszar(geometria().widok);}

    char* bufor_{nullptr}; char* roboczy_{nullptr}; char* schowek_{nullptr}; size_t pojemnosc_{0},dlugosc_{0},kursor_{0},kotwica_{0};
    char placeholder_[128]{}; size_t maks_znakow_{0}; bool zawijanie_{true},pozwol_poziomy_{true},pozwol_pionowy_{true},zaznaczanie_{false}; int odstep_linii_{2},preferowane_x_{0},maks_szerokosc_{0};
    Linia linie_[MAKS_LINII_WIZUALNYCH]; size_t liczba_wizualnych_{0},liczba_logicznych_{1}; mutable size_t ostatnio_narysowane_{0};
    Migawka* historia_[MAKS_HISTORIA]{}; size_t hist_liczba_{0},hist_pozycja_{0};
    PasekPrzewijania* pionowy_{nullptr}; PasekPrzewijania* poziomy_{nullptr};
    FunkcjaMenuKontekstowego cb_menu_kontekstowe_{nullptr};
    void* dane_menu_kontekstowego_{nullptr};
    FunkcjaTekstu cb_zmiana_{nullptr},cb_zatwierdzenie_{nullptr}; void* dane_zmiany_{nullptr}; void* dane_zatwierdzenia_{nullptr};
};

} // namespace BursztynUI
