#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

struct DataKalendarza {
    int rok{2026};
    int miesiac{9}; // 1..12
    int dzien{2};   // 1..31

    static bool czy_przestepny(int r) {
        return (r % 4 == 0 && r % 100 != 0) || (r % 400 == 0);
    }

    static int dni_w_miesiacu(int r, int m) {
        if (m < 1 || m > 12) return 30;
        static const int DNI[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m == 2 && czy_przestepny(r)) return 29;
        return DNI[m - 1];
    }

    static int dzien_tygodnia(int r, int m, int d) {
        if (m < 1 || m > 12 || d < 1 || d > dni_w_miesiacu(r, m))
            return -1;
        // Sakamoto's algorithm (0 = Poniedziałek ... 6 = Niedziela)
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        int y = r - (m < 3);
        int dow = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
        return (dow + 6) % 7; // Przesunięcie: 0 = Pn, 6 = Nd
    }

    bool czy_poprawna() const {
        if (rok < 1900 || rok > 2100) return false;
        if (miesiac < 1 || miesiac > 12) return false;
        if (dzien < 1 || dzien > dni_w_miesiacu(rok, miesiac)) return false;
        return true;
    }

    bool operator==(const DataKalendarza& o) const {
        return rok == o.rok && miesiac == o.miesiac && dzien == o.dzien;
    }
    bool operator!=(const DataKalendarza& o) const { return !(*this == o); }
};

class Kalendarz : public Kontrolka {
public:
    using FunkcjaKalendarza = void (*)(Kalendarz* nadawca, DataKalendarza data, void* dane_uzytkownika);

    explicit Kalendarz(DataKalendarza data_poczatkowa = DataKalendarza{}) {
        if (!data_poczatkowa.czy_poprawna()) data_poczatkowa = DataKalendarza{};
        wybrana_data_ = data_poczatkowa;
        widok_rok_ = data_poczatkowa.rok;
        widok_miesiac_ = data_poczatkowa.miesiac;
        aktualizuj_model_miesiaca();
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = Kolory::WejscieTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        preferowany_rozmiar_ = Rozmiar(230, 200);
        wypelnienie_ = Marginesy(4, 4, 4, 4);
    }

    ~Kalendarz() override = default;

    DataKalendarza wybrana_data() const { return wybrana_data_; }
    int najechana_komorka() const { return najechana_komorka_; }
    uint64_t liczba_przeliczen_miesiaca() const {
        return liczba_przeliczen_miesiaca_;
    }
    uint32_t kolor_nieaktywnego_dnia() const {
        return Kolory::TekstWyciszony;
    }
    Prostokat prostokat_komorki(int indeks) const {
        if (indeks < 0 || indeks >= 42) return Prostokat();
        Geometria g = geometria();
        return Prostokat(g.grid_x + (indeks % 7) * g.cell_w,
                         g.grid_y + (indeks / 7) * g.cell_h,
                         g.cell_w, g.cell_h);
    }
    Prostokat prostokat_przycisku_nawigacji(int indeks) const {
        return indeks >= 0 && indeks < 4 ? geometria().przyciski[indeks]
                                         : Prostokat();
    }

    void ustaw_wybrana_data(DataKalendarza d, bool powiadom = true) {
        if (!d.czy_poprawna() || d == wybrana_data_) return;
        const int stary_rok = widok_rok_;
        const int stary_miesiac = widok_miesiac_;
        const int stara_komorka = komorka_dnia(wybrana_data_.dzien);
        wybrana_data_ = d;
        widok_rok_ = d.rok;
        widok_miesiac_ = d.miesiac;
        if (stary_rok == widok_rok_ && stary_miesiac == widok_miesiac_) {
            uniewaznij_komorke(stara_komorka);
            uniewaznij_komorke(komorka_dnia(d.dzien));
        } else {
            aktualizuj_model_miesiaca();
            najechana_komorka_ = -1;
            uniewaznij();
        }
        if (powiadom && cb_zmiana_daty_) {
            cb_zmiana_daty_(this, wybrana_data_, dane_zmiany_daty_);
        }
    }

    void przy_zmianie_daty(FunkcjaKalendarza cb, void* dane = nullptr) {
        cb_zmiana_daty_ = cb;
        dane_zmiany_daty_ = dane;
    }

    void poprzedni_miesiac() {
        if (widok_rok_ <= 1900 && widok_miesiac_ <= 1) return;
        if (--widok_miesiac_ < 1) {
            widok_miesiac_ = 12;
            --widok_rok_;
        }
        aktualizuj_model_miesiaca();
        uniewaznij();
    }

    void nastepny_miesiac() {
        if (widok_rok_ >= 2100 && widok_miesiac_ >= 12) return;
        if (++widok_miesiac_ > 12) {
            widok_miesiac_ = 1;
            ++widok_rok_;
        }
        aktualizuj_model_miesiaca();
        uniewaznij();
    }

    void poprzedni_rok() {
        if (widok_rok_ <= 1900) return;
        --widok_rok_;
        aktualizuj_model_miesiaca();
        uniewaznij();
    }

    void nastepny_rok() {
        if (widok_rok_ >= 2100) return;
        ++widok_rok_;
        aktualizuj_model_miesiaca();
        uniewaznij();
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI skala = skala_dpi_;
        return Rozmiar(230 * skala, 200 * skala);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        SkalaUI skala = skala_dpi_;
        Geometria g = geometria();
        int nav_h = g.nav_h;
        int dows_h = g.dows_h;
        int grid_y = g.grid_y;

        // 1. Pasek nawigacji (przyciski << < Tytuł > >>)
        btn_prev_year_ = g.przyciski[0];
        btn_prev_month_ = g.przyciski[1];
        btn_next_month_ = g.przyciski[2];
        btn_next_year_ = g.przyciski[3];

        ctx.rysuj_zaokraglony_prostokat(btn_prev_year_, kolor_przycisku(0), 2);
        ctx.rysuj_tekst_wysrodkowany(btn_prev_year_, "<<", motyw_kolor(MOTYW_KOLOR_TEKST), skala);

        ctx.rysuj_zaokraglony_prostokat(btn_prev_month_, kolor_przycisku(1), 2);
        ctx.rysuj_tekst_wysrodkowany(btn_prev_month_, "<", motyw_kolor(MOTYW_KOLOR_TEKST), skala);

        ctx.rysuj_zaokraglony_prostokat(btn_next_month_, kolor_przycisku(2), 2);
        ctx.rysuj_tekst_wysrodkowany(btn_next_month_, ">", motyw_kolor(MOTYW_KOLOR_TEKST), skala);

        ctx.rysuj_zaokraglony_prostokat(btn_next_year_, kolor_przycisku(3), 2);
        ctx.rysuj_tekst_wysrodkowany(btn_next_year_, ">>", motyw_kolor(MOTYW_KOLOR_TEKST), skala);

        // Tytuł miesiąca i roku
        static const char* const NAZWY_MIESIECY[] = {
            "Styczeń", "Luty", "Marzec", "Kwiecień", "Maj", "Czerwiec",
            "Lipiec", "Sierpień", "Wrzesień", "Październik", "Listopad", "Grudzień"
        };
        char tytul[64];
        sformatuj_tytul(tytul, sizeof(tytul), NAZWY_MIESIECY[widok_miesiac_ - 1], widok_rok_);

        Prostokat rect_tytul(46 * skala, 4 * skala, obszar_.szerokosc - 92 * skala, 18 * skala);
        ctx.rysuj_tekst_wysrodkowany(rect_tytul, tytul, motyw_kolor(MOTYW_KOLOR_AKCENT), skala);

        // 2. Dni tygodnia (Pn .. Nd)
        static const char* const DNI_TYG[] = {"Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd"};
        int cell_w = g.cell_w;
        int cell_h = g.cell_h;

        for (int c = 0; c < 7; ++c) {
            int x = g.grid_x + c * cell_w;
            Prostokat rect_dow(x, nav_h + 2 * skala, cell_w, dows_h);
            uint32_t col_dow = (c >= 5) ? motyw_kolor(MOTYW_KOLOR_AKCENT) : motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI);
            ctx.rysuj_tekst_wysrodkowany(rect_dow, DNI_TYG[c], col_dow, skala);
        }

        ctx.rysuj_prostokat(Prostokat(4 * skala, grid_y - 2 * skala, obszar_.szerokosc - 8 * skala, 1),
                            motyw_kolor(MOTYW_KOLOR_RAMKA));

        // 3. Siatka dni
        const int pierwszy_dow = pierwszy_dow_;
        const int ile_dni = dni_w_widoku_;
        const int prev_dni = dni_w_poprzednim_miesiacu_;

        Punkt off = ctx.pobierz_przesuniecie();
        Prostokat clip_swiat = ctx.pobierz_przyciecie();
        Prostokat clip_lokalny(clip_swiat.x - off.x, clip_swiat.y - off.y,
                               clip_swiat.szerokosc, clip_swiat.wysokosc);
        Prostokat grid_rect(g.grid_x, g.grid_y, 7 * cell_w, 6 * cell_h);
        Prostokat grid_clip = grid_rect.przeciecie(clip_lokalny);
        int start_r = 0;
        int end_r = 0;
        if (!grid_clip.czy_pusty() && cell_h > 0) {
            start_r = (grid_clip.y - g.grid_y) / cell_h;
            end_r = (grid_clip.dolny() - g.grid_y - 1) / cell_h + 1;
            if (start_r < 0) start_r = 0;
            if (end_r > 6) end_r = 6;
        }

        for (int r = start_r; r < end_r; ++r) {
            for (int c = 0; c < 7; ++c) {
                int cell_idx = r * 7 + c;
                int x = g.grid_x + c * cell_w;
                int y = grid_y + r * cell_h;
                Prostokat rect_cell(x, y, cell_w, cell_h);
                if (!rect_cell.przecina(clip_lokalny)) continue;

                char buf_dzien[8];
                uint32_t col_txt = motyw_kolor(MOTYW_KOLOR_TEKST);
                const int day_num = cell_idx - pierwszy_dow + 1;

                if (day_num < 1) {
                    int d = prev_dni + day_num;
                    formatuj_liczbe(buf_dzien, sizeof(buf_dzien), d);
                    col_txt = kolor_nieaktywnego_dnia();
                } else if (day_num <= ile_dni) {
                    formatuj_liczbe(buf_dzien, sizeof(buf_dzien), day_num);

                    bool sel = (widok_rok_ == wybrana_data_.rok &&
                                widok_miesiac_ == wybrana_data_.miesiac &&
                                day_num == wybrana_data_.dzien);
                    bool hov = (cell_idx == najechana_komorka_ && czy_wlaczona_);

                    if (sel) {
                        ctx.rysuj_zaokraglony_prostokat(rect_cell, motyw_kolor(MOTYW_KOLOR_ZAZNACZENIE), 3);
                        ctx.rysuj_zaokraglona_ramke(rect_cell, motyw_kolor(MOTYW_KOLOR_AKCENT_NAJECHANY), 3, 1);
                        col_txt = motyw_kolor(MOTYW_KOLOR_TEKST_AKCENTU);
                    } else if (hov) {
                        ctx.rysuj_zaokraglony_prostokat(rect_cell, motyw_kolor(MOTYW_KOLOR_PODNIESIONY), 3);
                        ctx.rysuj_zaokraglona_ramke(rect_cell, motyw_kolor(MOTYW_KOLOR_RAMKA), 3, 1);
                    } else if (c >= 5) {
                        col_txt = motyw_kolor(MOTYW_KOLOR_AKCENT);
                    }
                } else {
                    formatuj_liczbe(buf_dzien, sizeof(buf_dzien), day_num - ile_dni);
                    col_txt = kolor_nieaktywnego_dnia();
                }

                if (!czy_wlaczona_ && !sel_dla_komorki(cell_idx))
                    col_txt = kolor_nieaktywnego_dnia();

                ctx.rysuj_tekst_wysrodkowany(rect_cell, buf_dzien, col_txt, skala);
            }
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        const int przycisk = przycisk_pod_punktem(lx, ly);
        if (przycisk >= 0) {
            ustaw_wcisniety_przycisk(przycisk);
        }
        if (przycisk == 0) {
            poprzedni_rok();
            e.obsluzone = true;
            return;
        }
        if (przycisk == 1) {
            poprzedni_miesiac();
            e.obsluzone = true;
            return;
        }
        if (przycisk == 2) {
            nastepny_miesiac();
            e.obsluzone = true;
            return;
        }
        if (przycisk == 3) {
            nastepny_rok();
            e.obsluzone = true;
            return;
        }

        int d = dzien_pod_punktem(lx, ly);
        if (d >= 1) {
            ustaw_wybrana_data({widok_rok_, widok_miesiac_, d});
            e.obsluzone = true;
        }
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_zwolnienie_myszy(e);
        ustaw_wcisniety_przycisk(-1);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_ruch_myszy(e);
        if (!czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        int nowy_przycisk = przycisk_pod_punktem(lx, ly);
        int nowa_komorka = nowy_przycisk < 0
                         ? komorka_pod_punktem(lx, ly, true) : -1;
        ustaw_najechany_przycisk(nowy_przycisk);
        ustaw_najechana_komorke(nowa_komorka);
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        ustaw_najechany_przycisk(-1);
        ustaw_najechana_komorke(-1);
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        Kontrolka::obsluz_wcisniecie_klawisza(e);
        if (!czy_wlaczona_) return;

        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if ((usb && baza == 79U) || (e0 && baza == 0x4DU)) { // Right
            przesun_wybrana_date(1);
            e.obsluzone = true;
        } else if ((usb && baza == 80U) || (e0 && baza == 0x4BU)) { // Left
            przesun_wybrana_date(-1);
            e.obsluzone = true;
        } else if ((usb && baza == 81U) || (e0 && baza == 0x50U)) { // Down
            przesun_wybrana_date(7);
            e.obsluzone = true;
        } else if ((usb && baza == 82U) || (e0 && baza == 0x48U)) { // Up
            przesun_wybrana_date(-7);
            e.obsluzone = true;
        } else if ((usb && baza == 78U) || (e0 && baza == 0x51U)) { // Page Down
            przesun_miesiac_wybranej(1);
            e.obsluzone = true;
        } else if ((usb && baza == 75U) || (e0 && baza == 0x49U)) { // Page Up
            przesun_miesiac_wybranej(-1);
            e.obsluzone = true;
        }
    }

private:
    DataKalendarza wybrana_data_{};
    int widok_rok_{2026};
    int widok_miesiac_{9};

    int najechana_komorka_{-1};
    int najechany_przycisk_{-1};
    int wcisniety_przycisk_{-1};
    int pierwszy_dow_{0};
    int dni_w_widoku_{30};
    int dni_w_poprzednim_miesiacu_{31};
    uint64_t liczba_przeliczen_miesiaca_{0};

    Prostokat btn_prev_year_{};
    Prostokat btn_prev_month_{};
    Prostokat btn_next_month_{};
    Prostokat btn_next_year_{};

    FunkcjaKalendarza cb_zmiana_daty_{nullptr};
    void* dane_zmiany_daty_{nullptr};

    void przesun_wybrana_date(int dni) {
        DataKalendarza d = wybrana_data_;
        const int kierunek = dni < 0 ? -1 : 1;
        int pozostalo = dni < 0 ? -dni : dni;
        while (pozostalo-- > 0) {
            if (kierunek > 0) {
                const int maksimum = DataKalendarza::dni_w_miesiacu(d.rok, d.miesiac);
                if (d.dzien < maksimum) ++d.dzien;
                else if (d.miesiac < 12) { ++d.miesiac; d.dzien = 1; }
                else if (d.rok < 2100) { ++d.rok; d.miesiac = 1; d.dzien = 1; }
                else return;
            } else {
                if (d.dzien > 1) --d.dzien;
                else if (d.miesiac > 1) {
                    --d.miesiac;
                    d.dzien = DataKalendarza::dni_w_miesiacu(d.rok, d.miesiac);
                } else if (d.rok > 1900) {
                    --d.rok; d.miesiac = 12; d.dzien = 31;
                } else return;
            }
        }
        ustaw_wybrana_data(d);
    }

    void przesun_miesiac_wybranej(int kierunek) {
        DataKalendarza d = wybrana_data_;
        if (kierunek > 0) {
            if (d.miesiac < 12) ++d.miesiac;
            else if (d.rok < 2100) { ++d.rok; d.miesiac = 1; }
            else return;
        } else {
            if (d.miesiac > 1) --d.miesiac;
            else if (d.rok > 1900) { --d.rok; d.miesiac = 12; }
            else return;
        }
        const int maksimum = DataKalendarza::dni_w_miesiacu(d.rok, d.miesiac);
        if (d.dzien > maksimum) d.dzien = maksimum;
        ustaw_wybrana_data(d);
    }

    struct Geometria {
        int nav_h{0};
        int dows_h{0};
        int grid_x{0};
        int grid_y{0};
        int cell_w{0};
        int cell_h{0};
        Prostokat przyciski[4];
    };

    Geometria geometria() const {
        const SkalaUI s = skala_dpi_;
        Geometria g;
        g.nav_h = 24 * s;
        g.dows_h = 18 * s;
        g.grid_x = 4 * s;
        g.grid_y = g.nav_h + g.dows_h + 4 * s;
        g.cell_w = (obszar_.szerokosc - 8 * s) / 7;
        g.cell_h = (obszar_.wysokosc - g.grid_y - 6 * s) / 6;
        g.przyciski[0] = Prostokat(4 * s, 4 * s, 20 * s, 18 * s);
        g.przyciski[1] = Prostokat(26 * s, 4 * s, 18 * s, 18 * s);
        g.przyciski[2] = Prostokat(obszar_.szerokosc - 44 * s, 4 * s,
                                   18 * s, 18 * s);
        g.przyciski[3] = Prostokat(obszar_.szerokosc - 24 * s, 4 * s,
                                   20 * s, 18 * s);
        return g;
    }

    int komorka_pod_punktem(int lx, int ly, bool tylko_biezacy_miesiac) const {
        Geometria g = geometria();
        if (g.cell_w <= 0 || g.cell_h <= 0 ||
            lx < g.grid_x || lx >= g.grid_x + 7 * g.cell_w ||
            ly < g.grid_y || ly >= g.grid_y + 6 * g.cell_h) return -1;
        int col = (lx - g.grid_x) / g.cell_w;
        int row = (ly - g.grid_y) / g.cell_h;
        int cell_idx = row * 7 + col;
        if (!tylko_biezacy_miesiac) return cell_idx;
        int day_num = cell_idx - pierwszy_dow_ + 1;
        return day_num >= 1 && day_num <= dni_w_widoku_ ? cell_idx : -1;
    }

    int dzien_pod_punktem(int lx, int ly) const {
        int komorka = komorka_pod_punktem(lx, ly, true);
        return komorka < 0 ? -1 : komorka - pierwszy_dow_ + 1;
    }

    int komorka_dnia(int dzien) const {
        if (dzien < 1 || dzien > dni_w_widoku_) return -1;
        return pierwszy_dow_ + dzien - 1;
    }

    bool sel_dla_komorki(int komorka) const {
        return widok_rok_ == wybrana_data_.rok &&
               widok_miesiac_ == wybrana_data_.miesiac &&
               komorka == komorka_dnia(wybrana_data_.dzien);
    }

    int przycisk_pod_punktem(int lx, int ly) const {
        Geometria g = geometria();
        for (int i = 0; i < 4; ++i)
            if (g.przyciski[i].zawiera(lx, ly)) return i;
        return -1;
    }

    uint32_t kolor_przycisku(int indeks) const {
        if (!czy_wlaczona_) return Kolory::WylaczonyTlo;
        if (indeks == wcisniety_przycisk_) return Kolory::PowierzchniaAktywna;
        if (indeks == najechany_przycisk_) return Kolory::PowierzchniaNajechana;
        return Kolory::PowierzchniaAlternatywna;
    }

    void uniewaznij_komorke(int indeks) {
        Prostokat r = prostokat_komorki(indeks);
        if (!r.czy_pusty()) uniewaznij_obszar(r);
    }

    void uniewaznij_przycisk(int indeks) {
        if (indeks < 0 || indeks >= 4) return;
        uniewaznij_obszar(geometria().przyciski[indeks]);
    }

    void ustaw_najechana_komorke(int nowa) {
        if (nowa == najechana_komorka_) return;
        int stara = najechana_komorka_;
        najechana_komorka_ = nowa;
        uniewaznij_komorke(stara);
        uniewaznij_komorke(nowa);
    }

    void ustaw_najechany_przycisk(int nowy) {
        if (nowy == najechany_przycisk_) return;
        int stary = najechany_przycisk_;
        najechany_przycisk_ = nowy;
        uniewaznij_przycisk(stary);
        uniewaznij_przycisk(nowy);
    }

    void ustaw_wcisniety_przycisk(int nowy) {
        if (nowy == wcisniety_przycisk_) return;
        int stary = wcisniety_przycisk_;
        wcisniety_przycisk_ = nowy;
        uniewaznij_przycisk(stary);
        uniewaznij_przycisk(nowy);
    }

    void aktualizuj_model_miesiaca() {
        pierwszy_dow_ = DataKalendarza::dzien_tygodnia(
            widok_rok_, widok_miesiac_, 1);
        dni_w_widoku_ = DataKalendarza::dni_w_miesiacu(
            widok_rok_, widok_miesiac_);
        const int poprzedni_miesiac = widok_miesiac_ == 1
                                    ? 12 : widok_miesiac_ - 1;
        const int poprzedni_rok = widok_miesiac_ == 1
                                ? widok_rok_ - 1 : widok_rok_;
        dni_w_poprzednim_miesiacu_ = DataKalendarza::dni_w_miesiacu(
            poprzedni_rok, poprzedni_miesiac);
        ++liczba_przeliczen_miesiaca_;
    }

    static void formatuj_liczbe(char* buf, size_t cap, int n) {
        if (cap < 4) return;
        if (n >= 10) {
            buf[0] = static_cast<char>('0' + (n / 10));
            buf[1] = static_cast<char>('0' + (n % 10));
            buf[2] = '\0';
        } else {
            buf[0] = static_cast<char>('0' + n);
            buf[1] = '\0';
        }
    }

    static void sformatuj_tytul(char* buf, size_t cap, const char* miesiac, int rok) {
        size_t p = 0;
        while (miesiac && miesiac[p] != '\0' && p + 10 < cap) {
            buf[p] = miesiac[p];
            ++p;
        }
        if (p + 7 < cap) {
            buf[p++] = ' ';
            buf[p++] = static_cast<char>('0' + (rok / 1000) % 10);
            buf[p++] = static_cast<char>('0' + (rok / 100) % 10);
            buf[p++] = static_cast<char>('0' + (rok / 10) % 10);
            buf[p++] = static_cast<char>('0' + (rok % 10));
            buf[p] = '\0';
        }
    }
};

} // namespace BursztynUI
