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

    void przy_zmianie_wyboru(FunkcjaWyboruWiersza cb, void* dane = nullptr) {
        cb_wybor_ = cb;
        dane_wyboru_ = dane;
    }

    uint64_t liczba_zapytan_dostawcy() const { return liczba_zapytan_; }
    size_t pierwszy_widoczny_wiersz() const { return pierwszy_widoczny_; }
    size_t ostatni_widoczny_wiersz() const { return ostatni_widoczny_; }

    int wysokosc_naglowka() const { return wysokosc_naglowka_px_ > 0 ? wysokosc_naglowka_px_ : 26 * skala_dpi_; }
    int wysokosc_wiersza() const { return wysokosc_wiersza_px_ > 0 ? wysokosc_wiersza_px_ : 22 * skala_dpi_; }

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
            total_col_w += dostawca_->szerokosc_kolumny(c) * s;
        }

        int table_w = obszar_.szerokosc - sb_w;
        int table_h = obszar_.wysokosc - header_h;

        // 1. Rysuj nagłówek tabeli
        ctx.rysuj_zaokraglony_prostokat(Prostokat(1, 1, obszar_.szerokosc - 2, header_h),
                                        motyw_kolor(MOTYW_KOLOR_PANEL), 3 * s);
        ctx.rysuj_linie_pozioma(1, header_h, obszar_.szerokosc - 2, motyw_kolor(MOTYW_KOLOR_RAMKA));

        int cur_x = 1 - przewiniecie_x_;
        for (size_t c = 0; c < cols; ++c) {
            int cw = dostawca_->szerokosc_kolumny(c) * s;
            const char* header = dostawca_->naglowek_kolumny(c);
            if (!header) header = "";

            if (cur_x + cw > 0 && cur_x < obszar_.szerokosc) {
                ctx.rysuj_tekst_obciety(Punkt(cur_x + pad_text, (header_h - font_h) / 2),
                                        header, motyw_kolor(MOTYW_KOLOR_TEKST), cw - 2 * pad_text, s);
                // Wskaźnik sortowania
                if (c == kolumna_sortowana_) {
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
        for (size_t r = idx_start; r < idx_koniec; ++r) {
            int y = start_y + static_cast<int>(r - idx_start) * row_h;
            if (y + row_h <= header_h) continue;

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
                int cw = dostawca_->szerokosc_kolumny(c) * s;
                if (cur_x + cw > 0 && cur_x < table_w) {
                    ++liczba_zapytan_;
                    const char* cell_txt = dostawca_->pobierz_komorke(r, c, bufor, sizeof(bufor));
                    if (!cell_txt) cell_txt = "";

                    ctx.rysuj_tekst_obciety(Punkt(cur_x + pad_text, y + (row_h - font_h) / 2),
                                            cell_txt, c_txt, cw - 2 * pad_text, s);
                    ctx.rysuj_linie_pionowa(cur_x + cw, y, row_h, motyw_kolor(MOTYW_KOLOR_RAMKA));
                }
                cur_x += cw;
            }
            ctx.rysuj_linie_pozioma(1, y + row_h - 1, table_w - 2, motyw_kolor(MOTYW_KOLOR_RAMKA));
        }

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
            if (e.czy_lewy_przycisk() && dostawca_) {
                int cur_x = 1 - przewiniecie_x_;
                size_t cols = dostawca_->liczba_kolumn();
                for (size_t c = 0; c < cols; ++c) {
                    int cw = dostawca_->szerokosc_kolumny(c) * s;
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

        // Kliknięcie w suwak pionowy
        if (lx >= obszar_.szerokosc - sb_w) {
            int total_h = static_cast<int>(liczba_wierszy()) * row_h;
            int table_h = obszar_.wysokosc - header_h;
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
        int delta = (e.delta_y != 0) ? -e.delta_y : 3;
        przewiniecie_y_ += delta * wysokosc_wiersza();
        normalizuj_przewiniecie();
        uniewaznij();
        e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        if (!czy_wlaczona_ || liczba_wierszy() == 0) return;
        uint32_t k = e.kod_klawisza;
        bool usb = (k & BWS_KEY_SOURCE_USB) != 0;
        uint32_t b = usb ? (k & 0xFFU) : (k & 0x7FU);

        if ((usb && b == 82U) || (!usb && b == 0x48U)) {
            if (wybrany_wiersz_ > 0) ustaw_wybrany_wiersz(wybrany_wiersz_ - 1);
            e.obsluzone = true;
            return;
        }
        if ((usb && b == 81U) || (!usb && b == 0x50U)) {
            if (wybrany_wiersz_ + 1 < liczba_wierszy()) ustaw_wybrany_wiersz(wybrany_wiersz_ + 1);
            e.obsluzone = true;
            return;
        }
        if ((usb && b == 75U) || (!usb && b == 0x49U)) {
            int page = (obszar_.wysokosc - wysokosc_naglowka()) / (wysokosc_wiersza() > 0 ? wysokosc_wiersza() : 1);
            if (wybrany_wiersz_ > static_cast<size_t>(page)) ustaw_wybrany_wiersz(wybrany_wiersz_ - page);
            else ustaw_wybrany_wiersz(0);
            e.obsluzone = true;
            return;
        }
        if ((usb && b == 78U) || (!usb && b == 0x51U)) {
            int page = (obszar_.wysokosc - wysokosc_naglowka()) / (wysokosc_wiersza() > 0 ? wysokosc_wiersza() : 1);
            ustaw_wybrany_wiersz(wybrany_wiersz_ + page);
            e.obsluzone = true;
            return;
        }
        if ((usb && b == 74U) || (!usb && b == 0x47U)) {
            ustaw_wybrany_wiersz(0);
            e.obsluzone = true;
            return;
        }
        if ((usb && b == 77U) || (!usb && b == 0x4FU)) {
            ustaw_wybrany_wiersz(liczba_wierszy() - 1);
            e.obsluzone = true;
            return;
        }

        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

private:
    void zapewnij_widocznosc_wiersza(size_t w) {
        int row_h = wysokosc_wiersza();
        int y0 = static_cast<int>(w) * row_h;
        int y1 = y0 + row_h;
        int table_h = obszar_.wysokosc - wysokosc_naglowka();

        if (y0 < przewiniecie_y_) {
            przewiniecie_y_ = y0;
        } else if (y1 > przewiniecie_y_ + table_h) {
            przewiniecie_y_ = y1 - table_h;
        }
        normalizuj_przewiniecie();
    }

    void normalizuj_przewiniecie() {
        int total_h = static_cast<int>(liczba_wierszy()) * wysokosc_wiersza();
        int table_h = obszar_.wysokosc - wysokosc_naglowka();
        int max_scroll = total_h - table_h;
        if (max_scroll < 0) max_scroll = 0;
        if (przewiniecie_y_ > max_scroll) przewiniecie_y_ = max_scroll;
        if (przewiniecie_y_ < 0) przewiniecie_y_ = 0;
    }

    void rysuj_pasek_pionowy(KontekstRysowania& ctx, size_t rows, int row_h, int sb_w) {
        int total_h = static_cast<int>(rows) * row_h;
        int table_h = obszar_.wysokosc - wysokosc_naglowka();
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
