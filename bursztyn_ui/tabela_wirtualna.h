#pragma once

#include "kontrolka.h"
#include "czcionka.h"
#include "motyw.h"

namespace BursztynUI {

class DostawcaDanychTabeli {
public:
    virtual ~DostawcaDanychTabeli() = default;
    virtual size_t liczba_wierszy() = 0;
    virtual size_t liczba_kolumn() = 0;
    virtual const char* naglowek_kolumny(size_t kolumna) = 0;
    virtual int szerokosc_kolumny(size_t kolumna) = 0;
    virtual const char* pobierz_komorke(size_t wiersz, size_t kolumna, char* bufor, size_t rozmiar_bufora) = 0;
    virtual void zazadaj_sortowania(size_t kolumna, bool rosnaco) { (void)kolumna; (void)rosnaco; }
    // Opcjonalna ikona w komorce (DIP); 0 = brak. Rysowana przed tekstem.
    virtual int szerokosc_ikony_komorki(size_t /*kolumna*/) { return 0; }
    virtual void rysuj_ikone_komorki(KontekstRysowania& /*ctx*/, size_t /*wiersz*/, size_t /*kolumna*/,
                                    Prostokat /*obszar*/) {}
};

class TabelaWirtualna : public Kontrolka {
public:
    using FunkcjaWyboruWiersza = void (*)(TabelaWirtualna* nadawca, size_t wiersz, void* dane);

    TabelaWirtualna() {
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = true;
        wyrownanie_poziome_ = WyrownaniePoziome::Rozciagnij;
        wyrownanie_pionowe_ = WyrownaniePionowe::Rozciagnij;
        preferowany_rozmiar_ = Rozmiar(320, 240);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = motyw_grubosc_ramki();
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_PANEL);
        Kontrolka::zaktualizuj_motyw();
    }

    void ustaw_dostawce(DostawcaDanychTabeli* d) {
        dostawca_ = d;
        klucz_szerokosci_ = 0;
        wybrany_wiersz_ = 0;
        przewiniecie_x_ = 0;
        przewiniecie_y_ = 0;
        liczba_zapytan_ = 0;
        uniewaznij();
    }
    DostawcaDanychTabeli* dostawca() const { return dostawca_; }

    size_t liczba_wierszy() const { return dostawca_ ? dostawca_->liczba_wierszy() : 0; }
    size_t liczba_kolumn() const { return dostawca_ ? dostawca_->liczba_kolumn() : 0; }

    // External models can retain their selection/scroll without callbacks or
    // copying thousands of rows. The owner supplies a shared scrollbar.
    void ustaw_stan_widoku(int selected, int first, int row_height, int header_height,
                           int hover = -1, int drop = -1) {
        wybrany_wiersz_ = selected < 0 ? SIZE_MAX : static_cast<size_t>(selected);
        wiersz_najechany_ = hover; wiersz_drop_ = drop;
        wysokosc_wiersza_px_ = row_height; wysokosc_naglowka_px_ = header_height;
        przewiniecie_y_ = first * wysokosc_wiersza();
        zewnetrzne_przewijanie_ = true;
    }

    size_t wybrany_wiersz() const { return wybrany_wiersz_; }
    void ustaw_wybrany_wiersz(size_t w) {
        size_t n = liczba_wierszy();
        if (n == 0) { wybrany_wiersz_ = 0; return; }
        if (w >= n) w = n - 1;
        if (wybrany_wiersz_ != w) {
            wybrany_wiersz_ = w;
            zapewnij_widocznosc_wiersza(wybrany_wiersz_);
            uniewaznij();
            if (cb_wybor_) cb_wybor_(this, wybrany_wiersz_, dane_wyboru_);
        }
    }

    // Brak zaznaczenia (np. wybrany element zniknal z modelu): zaden wiersz nie jest podswietlony.
    void wyczysc_wybor() {
        if (wybrany_wiersz_ != SIZE_MAX) { wybrany_wiersz_ = SIZE_MAX; uniewaznij(); }
    }
    bool ma_wybor() const { return wybrany_wiersz_ < liczba_wierszy(); }

    void przy_zmianie_wyboru(FunkcjaWyboruWiersza cb, void* dane = nullptr) {
        cb_wybor_ = cb;
        dane_wyboru_ = dane;
    }

    /* Przewijanie w poziomie (opcjonalne, domyslnie wylaczone - dotychczasowi uzytkownicy bez zmian): gdy kolumny
     * sa szersze niz tabela, pod wierszami pojawia sie poziomy pasek; Shift+rolka, rolka pozioma, klik paska
     * oraz strzalki w lewo/prawo przesuwaja kolumny. */
    void ustaw_przewijanie_poziome(bool w) { przewijanie_poziome_ = w; if (!w) przewiniecie_x_ = 0; uniewaznij(); }
    bool czy_przewijanie_poziome() const { return przewijanie_poziome_; }
    int przewiniecie_poziome() const { return przewiniecie_x_; }
    void ustaw_przewiniecie_poziome(int x) { const int s = przewiniecie_x_; przewiniecie_x_ = x; normalizuj_poziome(); if (s != przewiniecie_x_) uniewaznij(); }
    int maks_przewiniecie_poziome() const {
        if (!przewijanie_poziome_ || zewnetrzne_przewijanie_ || !dostawca_) return 0;
        const int m = szerokosc_kolumn() - (szerokosc_obszaru_tabeli() - 2);
        return m > 0 ? m : 0;
    }
    int wysokosc_paska_poziomego() const { return maks_przewiniecie_poziome() > 0 ? 12 * skala_dpi_ : 0; }

    /* Opcjonalnie (domyslnie wylaczone): kolumna nigdy nie jest wezsza niz jej naglowek zmierzony aktywna czcionka
     * systemowa + odstepy + miejsce na wskaznik sortowania. Szerokosci sa liczone raz i trzymane do zmiany skali,
     * czcionki (generacja BSF / rozmiar UI), dostawcy albo jawnego uniewaznij_szerokosci() - nie przy kazdym rysowaniu. */
    void ustaw_szerokosc_wg_naglowkow(bool w) { szerokosc_wg_naglowkow_ = w; uniewaznij_szerokosci(); uniewaznij(); }
    bool czy_szerokosc_wg_naglowkow() const { return szerokosc_wg_naglowkow_; }
    void uniewaznij_szerokosci() { klucz_szerokosci_ = 0; }
    /* Tabela, ktorej dostawca nie sortuje (np. "Wlasciwosc / Wartosc"): bez strzalki i bez reakcji na klik naglowka. */
    void ustaw_sortowanie_naglowkiem(bool w) { sortowanie_naglowkiem_ = w; uniewaznij(); }
    /* Szerokosc kolumny w pikselach (DIP * skala, ew. powiekszona do naglowka). Jedno zrodlo dla rysowania i klikniec. */
    int szerokosc_kolumny_px(size_t c) const {
        if (!dostawca_) return 0;
        const int bazowa = dostawca_->szerokosc_kolumny(c) * skala_dpi_;
        if (!szerokosc_wg_naglowkow_ || c >= MAKS_KOLUMN_SZEROKOSCI) return bazowa;
        odswiez_szerokosci_naglowkow();
        return bazowa > min_szerokosc_[c] ? bazowa : min_szerokosc_[c];
    }
    /* Pierwszy piksel (y, wspolrzedne kontrolki) obszaru wierszy: zawsze ponizej naglowka. */
    int poczatek_wierszy() const { return wysokosc_naglowka(); }
    int przewiniecie_pionowe() const { return przewiniecie_y_; }
    void ustaw_przewiniecie_pionowe(int y) { przewiniecie_y_ = y; normalizuj_przewiniecie(); uniewaznij(); }

    uint64_t liczba_zapytan_dostawcy() const { return liczba_zapytan_; }
    size_t pierwszy_widoczny_wiersz() const { return pierwszy_widoczny_; }
    size_t ostatni_widoczny_wiersz() const { return ostatni_widoczny_; }

    int wysokosc_naglowka() const {
        int requested=wysokosc_naglowka_px_ > 0 ? wysokosc_naglowka_px_ : 26 * skala_dpi_;
        int minimum=CzcionkaUI::pobierz_wysokosc(skala_dpi_)+4*skala_dpi_;
        return requested>minimum?requested:minimum;
    }
    int wysokosc_wiersza() const {
        int requested=wysokosc_wiersza_px_ > 0 ? wysokosc_wiersza_px_ : 22 * skala_dpi_;
        int minimum=CzcionkaUI::pobierz_wysokosc(skala_dpi_)+2*skala_dpi_;
        return requested>minimum?requested:minimum;
    }

    Rozmiar zmierz(Rozmiar dostepny) override {
        SkalaUI s = skala_dpi_;
        int w = 320 * s;
        int h = 240 * s;
        if (preferowany_rozmiar_.szerokosc >= 0) w = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) h = preferowany_rozmiar_.wysokosc * s;
        (void)dostepny;
        return Rozmiar(w, h);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);

        rysuj_tlo_i_ramke(ctx, wyznacz_biezacy_styl_stanu());

        if (!dostawca_) return;
        size_t rows = dostawca_->liczba_wierszy();
        size_t cols = dostawca_->liczba_kolumn();
        if (cols == 0) return;

        int header_h = wysokosc_naglowka();
        int row_h = wysokosc_wiersza();
        int font_h = CzcionkaUI::pobierz_wysokosc(s);
        int pad_text = 6 * s;
        int sb_w = zewnetrzne_przewijanie_ ? 0 : 12 * s;

        // Oblicz całkowitą szerokość kolumn
        int total_col_w = 0;
        for (size_t c = 0; c < cols; ++c) {
            total_col_w += szerokosc_kolumny_px(c);
        }

        normalizuj_poziome();
        normalizuj_przewiniecie();   // dane mogly sie zmienic (mniej wierszy) albo tabela zmienila wysokosc
        int table_w = obszar_.szerokosc - sb_w;
        const int hsb_h = wysokosc_paska_poziomego();
        int table_h = obszar_.wysokosc - header_h - hsb_h;
        (void)total_col_w;

        // 1. Rysuj nagłówek tabeli
        ctx.rysuj_zaokraglony_prostokat(Prostokat(1, 1, obszar_.szerokosc - 2, header_h),
                                        motyw_kolor(MOTYW_KOLOR_PANEL), 3 * s);
        ctx.rysuj_linie_pozioma(1, header_h, obszar_.szerokosc - 2, motyw_kolor(MOTYW_KOLOR_RAMKA));

        int cur_x = 1 - przewiniecie_x_;
        for (size_t c = 0; c < cols; ++c) {
            int cw = szerokosc_kolumny_px(c);
            const char* header = dostawca_->naglowek_kolumny(c);
            if (!header) header = "";

            if (cur_x + cw > 0 && cur_x < obszar_.szerokosc) {
                const int strzalka = sortowanie_naglowkiem_ && szerokosc_wg_naglowkow_ ? SZEROKOSC_STRZALKI * s : 0;
                ctx.rysuj_tekst_obciety(Punkt(cur_x + pad_text, (header_h - font_h) / 2),
                                        header, motyw_kolor(MOTYW_KOLOR_TEKST), cw - 2 * pad_text - strzalka, s);
                // Wskaźnik sortowania
                if (sortowanie_naglowkiem_ && c == kolumna_sortowana_) {
                    int arrow_x = cur_x + cw - 12 * s;
                    int arrow_y = (header_h - 6 * s) / 2;
                    ctx.rysuj_tekst(Punkt(arrow_x, arrow_y), sortowanie_rosnace_ ? "^" : "v",
                                    motyw_kolor(MOTYW_KOLOR_AKCENT), s);
                }
                ctx.rysuj_linie_pionowa(cur_x + cw, 2, header_h - 4, motyw_kolor(MOTYW_KOLOR_RAMKA));
            }
            cur_x += cw;
        }

        // 2. Rysuj widoczne wiersze danych
        if (hsb_h > 0) rysuj_pasek_poziomy(ctx, sb_w, hsb_h);
        if (rows == 0) return;

        int widocznych_wierszy = (table_h / (row_h > 0 ? row_h : 1)) + 2;
        size_t idx_start = przewiniecie_y_ / (row_h > 0 ? row_h : 1);
        if (idx_start >= rows) idx_start = rows > 0 ? rows - 1 : 0;
        size_t idx_koniec = idx_start + widocznych_wierszy;
        if (idx_koniec > rows) idx_koniec = rows;

        pierwszy_widoczny_ = idx_start;
        ostatni_widoczny_ = idx_koniec > 0 ? idx_koniec - 1 : 0;

        int start_y = header_h - (przewiniecie_y_ % (row_h > 0 ? row_h : 1));

        char bufor[128];
        // Wiersze wylacznie w obszarze danych: czesciowo przewiniety pierwszy wiersz nie moze zamazac naglowka
        // (naglowek jest rysowany wczesniej), a ostatni - paska poziomego.
        if (table_h <= 0) return;
        ctx.dodaj_przyciecie(Prostokat(0, header_h + 1, obszar_.szerokosc, table_h - 1));
        for (size_t r = idx_start; r < idx_koniec; ++r) {
            int y = start_y + static_cast<int>(r - idx_start) * row_h;
            if (y + row_h <= header_h) continue;
            if (y >= header_h + table_h) break;

            if (!ctx.przecina_klip(Prostokat(0,y,table_w,row_h))) continue;
            bool zaznaczony = (r == wybrany_wiersz_);
            Prostokat r_wiersza(1, y, table_w - 2, row_h);

            if (static_cast<int>(r) == wiersz_drop_) {
                ctx.rysuj_prostokat(r_wiersza,motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY));
            } else if (zaznaczony) {
                ctx.rysuj_prostokat(r_wiersza, motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE));
            } else if (static_cast<int>(r)==wiersz_najechany_) {
                ctx.rysuj_prostokat(r_wiersza,motyw_kolor(MOTYW_KOLOR_PRZYCISK_NAJECHANY));
            } else if ((r & 1) != 0) {
                ctx.rysuj_prostokat(r_wiersza, motyw_kolor(MOTYW_KOLOR_PODNIESIONY));
            }

            cur_x = 1 - przewiniecie_x_;
            uint32_t c_txt = zaznaczony ? motyw_kolor_tekstu_zaznaczenia()
                                        : motyw_kolor(MOTYW_KOLOR_TEKST);

            for (size_t c = 0; c < cols; ++c) {
                int cw = szerokosc_kolumny_px(c);
                if (cur_x + cw > 0 && cur_x < table_w) {
                    ++liczba_zapytan_;
                    const char* cell_txt = dostawca_->pobierz_komorke(r, c, bufor, sizeof(bufor));
                    if (!cell_txt) cell_txt = "";
                    const int icon_dip = dostawca_->szerokosc_ikony_komorki(c);
                    const int icon_px = icon_dip * s;
                    int text_pad = pad_text;
                    if (icon_px > 0) {
                        const int isz = icon_px > row_h - 4 ? row_h - 4 : icon_px;
                        const int iy = y + (row_h - isz) / 2;
                        dostawca_->rysuj_ikone_komorki(ctx, r, c,
                            Prostokat(cur_x + pad_text, iy, isz, isz));
                        text_pad = pad_text + isz + 4 * s;
                    }
                    const int text_w = cw - text_pad - pad_text;
                    if (text_w > 0)
                        ctx.rysuj_tekst_obciety(Punkt(cur_x + text_pad, y + (row_h - font_h) / 2),
                                                cell_txt, c_txt, text_w, s);
                    ctx.rysuj_linie_pionowa(cur_x + cw, y, row_h, motyw_kolor(MOTYW_KOLOR_RAMKA));
                }
                cur_x += cw;
            }
            ctx.rysuj_linie_pozioma(1, y + row_h - 1, table_w - 2, motyw_kolor(MOTYW_KOLOR_RAMKA));
        }

        ctx.zdejmij_przyciecie();
        // Pasek przewijania pionowy
        if(!zewnetrzne_przewijanie_) rysuj_pasek_pionowy(ctx, rows, row_h, sb_w);
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!czy_wlaczona_) return;
        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        SkalaUI s = skala_dpi_;
        int header_h = wysokosc_naglowka();
        int row_h = wysokosc_wiersza();
        int sb_w = zewnetrzne_przewijanie_ ? 0 : 12 * s;

        // Kliknięcie w nagłówek kolumny -> zażądaj sortowania
        if (ly < header_h) {
            if (e.czy_lewy_przycisk() && dostawca_ && sortowanie_naglowkiem_) {
                int cur_x = 1 - przewiniecie_x_;
                size_t cols = dostawca_->liczba_kolumn();
                for (size_t c = 0; c < cols; ++c) {
                    int cw = szerokosc_kolumny_px(c);
                    if (lx >= cur_x && lx < cur_x + cw) {
                        if (kolumna_sortowana_ == c) {
                            sortowanie_rosnace_ = !sortowanie_rosnace_;
                        } else {
                            kolumna_sortowana_ = c;
                            sortowanie_rosnace_ = true;
                        }
                        dostawca_->zazadaj_sortowania(kolumna_sortowana_, sortowanie_rosnace_);
                        uniewaznij();
                        e.obsluzone = true;
                        return;
                    }
                    cur_x += cw;
                }
            }
            return;
        }

        // Kliknięcie w poziomy pasek (tylko gdy kolumny sie nie mieszcza)
        const int hsb_h = wysokosc_paska_poziomego();
        if (hsb_h > 0 && ly >= obszar_.wysokosc - hsb_h) {
            if (e.czy_lewy_przycisk()) {
                const int tor = szerokosc_obszaru_tabeli() - 4 * s;
                ustaw_przewiniecie_poziome(tor > 0 ? (lx * maks_przewiniecie_poziome()) / tor : 0);
                e.obsluzone = true;
            }
            return;
        }

        // Kliknięcie w suwak pionowy
        if (lx >= obszar_.szerokosc - sb_w) {
            int total_h = static_cast<int>(liczba_wierszy()) * row_h;
            int table_h = obszar_.wysokosc - header_h - hsb_h;
            if (total_h > table_h) {
                przewiniecie_y_ = ((ly - header_h) * (total_h - table_h)) / (table_h > 0 ? table_h : 1);
                normalizuj_przewiniecie();
                uniewaznij();
                e.obsluzone = true;
                return;
            }
        }

        // Kliknięcie w wiersz
        int klik_wiersz = (przewiniecie_y_ + (ly - header_h)) / (row_h > 0 ? row_h : 1);
        if (klik_wiersz >= 0 && static_cast<size_t>(klik_wiersz) < liczba_wierszy()) {
            ustaw_wybrany_wiersz(static_cast<size_t>(klik_wiersz));
        }

        Kontrolka::obsluz_wcisniecie_myszy(e);
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_) return;
        if (maks_przewiniecie_poziome() > 0 && (e.czy_shift() || (e.delta_x != 0 && e.delta_y == 0))) {
            const int d = e.delta_x != 0 ? e.delta_x : -e.delta_y;
            ustaw_przewiniecie_poziome(przewiniecie_x_ + d * 40 * skala_dpi_);
            e.obsluzone = true;
            return;
        }
        int delta = (e.delta_y != 0) ? -e.delta_y : 3;
        przewiniecie_y_ += delta * wysokosc_wiersza();
        normalizuj_przewiniecie();
        uniewaznij();
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || liczba_wierszy() == 0) return;
        uint32_t k = e.polecenie();

        if (k == 0x48U) {
            if (wybrany_wiersz_ > 0) ustaw_wybrany_wiersz(wybrany_wiersz_ - 1);
            e.obsluzone = true;
            return;
        }
        if (k == 0x50U) {
            if (wybrany_wiersz_ + 1 < liczba_wierszy()) ustaw_wybrany_wiersz(wybrany_wiersz_ + 1);
            e.obsluzone = true;
            return;
        }
        if (k == 0x49U) {
            int page = (obszar_.wysokosc - wysokosc_naglowka()) / (wysokosc_wiersza() > 0 ? wysokosc_wiersza() : 1);
            if (wybrany_wiersz_ > static_cast<size_t>(page)) ustaw_wybrany_wiersz(wybrany_wiersz_ - page);
            else ustaw_wybrany_wiersz(0);
            e.obsluzone = true;
            return;
        }
        if (k == 0x51U) {
            int page = (obszar_.wysokosc - wysokosc_naglowka()) / (wysokosc_wiersza() > 0 ? wysokosc_wiersza() : 1);
            ustaw_wybrany_wiersz(wybrany_wiersz_ + page);
            e.obsluzone = true;
            return;
        }
        if (k == 0x47U) {
            ustaw_wybrany_wiersz(0);
            e.obsluzone = true;
            return;
        }
        if (k == 0x4FU) {
            ustaw_wybrany_wiersz(liczba_wierszy() - 1);
            e.obsluzone = true;
            return;
        }
        if ((k == 0x4BU || k == 0x4DU) && maks_przewiniecie_poziome() > 0) {   // strzalka w lewo / w prawo
            ustaw_przewiniecie_poziome(przewiniecie_x_ + (k == 0x4BU ? -40 : 40) * skala_dpi_);
            e.obsluzone = true;
            return;
        }

        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    static constexpr size_t MAKS_KOLUMN_SZEROKOSCI = 16;
    static constexpr int SZEROKOSC_STRZALKI = 14;
    void odswiez_szerokosci_naglowkow() const {
        uint32_t gen = 0; int rozmiar = 0;
#if defined(BSF_NATIVE)
        // generacja (instalacja/zmiana czcionki), rozmiar UI i postep znaku "W" (podglad innej rodziny w Ustawieniach
        // nie zmienia generacji) - jeden odczyt z pamieci podrecznej glifow zamiast mierzenia naglowkow co klatke
        gen = BSF::generation() ^ (uint32_t(CzcionkaUI::postep_znaku('W', SkalaUI::procent(100))) << 20);
        rozmiar = BSF::ui_size();
#endif
        const uint64_t klucz = (uint64_t(skala_dpi_.procent()) << 48) ^ (uint64_t(uint16_t(rozmiar)) << 32) ^ gen ^
                               (uint64_t(dostawca_ ? dostawca_->liczba_kolumn() : 0) << 40) ^ 1ULL;
        if (klucz == klucz_szerokosci_ && dostawca_ == dostawca_szerokosci_) return;
        klucz_szerokosci_ = klucz; dostawca_szerokosci_ = dostawca_;
        const SkalaUI s = skala_dpi_;
        const size_t n = dostawca_ ? dostawca_->liczba_kolumn() : 0;
        for (size_t c = 0; c < MAKS_KOLUMN_SZEROKOSCI; ++c) {
            const char* h = c < n ? dostawca_->naglowek_kolumny(c) : nullptr;
            min_szerokosc_[c] = h && h[0] ? CzcionkaUI::zmierz_tekst(h, s) + 2 * 6 * s +
                                            (sortowanie_naglowkiem_ ? SZEROKOSC_STRZALKI * s : 0) + 2 * s : 0;
        }
    }

    int szerokosc_kolumn() const {
        if (!dostawca_) return 0;
        int w = 0;
        const size_t cols = dostawca_->liczba_kolumn();
        for (size_t c = 0; c < cols; ++c) w += szerokosc_kolumny_px(c);
        return w;
    }
    int szerokosc_obszaru_tabeli() const { return obszar_.szerokosc - (zewnetrzne_przewijanie_ ? 0 : 12 * skala_dpi_); }
    void normalizuj_poziome() {
        const int m = maks_przewiniecie_poziome();
        if (przewiniecie_x_ > m) przewiniecie_x_ = m;
        if (przewiniecie_x_ < 0) przewiniecie_x_ = 0;
    }
    void rysuj_pasek_poziomy(KontekstRysowania& ctx, int sb_w, int hsb_h) {
        SkalaUI s = skala_dpi_;
        const int y = obszar_.wysokosc - hsb_h;
        const int tor = obszar_.szerokosc - sb_w - 4 * s;
        const int calosc = szerokosc_kolumn(), widok = szerokosc_obszaru_tabeli() - 2, m = maks_przewiniecie_poziome();
        if (tor <= 0 || calosc <= 0) return;
        ctx.rysuj_prostokat(Prostokat(2 * s, y, tor, hsb_h - 2 * s), motyw_kolor(MOTYW_KOLOR_PANEL));
        int kciuk = (widok * tor) / calosc;
        if (kciuk < 16 * s) kciuk = 16 * s;
        if (kciuk > tor) kciuk = tor;
        const int x = 2 * s + (m > 0 ? (przewiniecie_x_ * (tor - kciuk)) / m : 0);
        ctx.rysuj_zaokraglony_prostokat(Prostokat(x, y + 2 * s, kciuk, hsb_h - 6 * s), motyw_kolor(MOTYW_KOLOR_RAMKA), 3 * s);
    }

    void zapewnij_widocznosc_wiersza(size_t w) {
        int row_h = wysokosc_wiersza();
        int y0 = static_cast<int>(w) * row_h;
        int y1 = y0 + row_h;
        int table_h = obszar_.wysokosc - wysokosc_naglowka() - wysokosc_paska_poziomego();
        if (table_h <= 0) return;   // brak ukladu: przewiniecie policzone wzgledem 0 px wysokosci bylo bledne

        if (y0 < przewiniecie_y_) {
            przewiniecie_y_ = y0;
        } else if (y1 > przewiniecie_y_ + table_h) {
            przewiniecie_y_ = y1 - table_h;
        }
        normalizuj_przewiniecie();
    }

    void normalizuj_przewiniecie() {
        int total_h = static_cast<int>(liczba_wierszy()) * wysokosc_wiersza();
        int table_h = obszar_.wysokosc - wysokosc_naglowka() - wysokosc_paska_poziomego();
        int max_scroll = table_h > 0 ? total_h - table_h : 0;   // bez ukladu: przewiniecie 0
        if (max_scroll < 0) max_scroll = 0;
        if (przewiniecie_y_ > max_scroll) przewiniecie_y_ = max_scroll;
        if (przewiniecie_y_ < 0) przewiniecie_y_ = 0;
    }

    void rysuj_pasek_pionowy(KontekstRysowania& ctx, size_t rows, int row_h, int sb_w) {
        int total_h = static_cast<int>(rows) * row_h;
        int table_h = obszar_.wysokosc - wysokosc_naglowka() - wysokosc_paska_poziomego();
        if (total_h <= table_h || table_h <= 0) return;

        SkalaUI s = skala_dpi_;
        int sb_x = obszar_.szerokosc - sb_w - 2 * s;
        int sb_y = wysokosc_naglowka() + 2 * s;
        int sb_h = table_h - 4 * s;

        ctx.rysuj_prostokat(Prostokat(sb_x, sb_y, sb_w, sb_h), motyw_kolor(MOTYW_KOLOR_PANEL));

        int thumb_h = (table_h * sb_h) / total_h;
        if (thumb_h < 16 * s) thumb_h = 16 * s;
        int travel = sb_h - thumb_h;
        int max_scroll = total_h - table_h;
        int thumb_y = sb_y + (max_scroll > 0 ? (przewiniecie_y_ * travel) / max_scroll : 0);

        ctx.rysuj_zaokraglony_prostokat(Prostokat(sb_x + 2 * s, thumb_y, sb_w - 4 * s, thumb_h),
                                        motyw_kolor(MOTYW_KOLOR_RAMKA), 3 * s);
    }

    bool zewnetrzne_przewijanie_{false};
    bool przewijanie_poziome_{false};
    bool szerokosc_wg_naglowkow_{false};
    bool sortowanie_naglowkiem_{true};
    mutable uint64_t klucz_szerokosci_{0};
    mutable const DostawcaDanychTabeli* dostawca_szerokosci_{nullptr};
    mutable int min_szerokosc_[MAKS_KOLUMN_SZEROKOSCI]{};
    int wysokosc_wiersza_px_{0}, wysokosc_naglowka_px_{0};
    int wiersz_najechany_{-1}, wiersz_drop_{-1};
    DostawcaDanychTabeli* dostawca_{nullptr};
    size_t wybrany_wiersz_{0};
    int przewiniecie_x_{0};
    int przewiniecie_y_{0};
    uint64_t liczba_zapytan_{0};
    size_t pierwszy_widoczny_{0};
    size_t ostatni_widoczny_{0};

    size_t kolumna_sortowana_{0};
    bool sortowanie_rosnace_{true};

    FunkcjaWyboruWiersza cb_wybor_{nullptr};
    void* dane_wyboru_{nullptr};
};

} // namespace BursztynUI
