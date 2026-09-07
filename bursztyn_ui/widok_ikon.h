#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

enum class TypIkonyWidoku : uint8_t {
    Plik,
    Folder,
    Aplikacja,
    Obraz,
    Dysk,
    Ustawienia,
    Dokument,
    Gwiazda,
    Wlasna
};

class WidokIkon : public Kontrolka {
public:
    struct Element {
        char* tekst{nullptr};
        TypIkonyWidoku typ{TypIkonyWidoku::Plik};
        uint64_t znacznik{0};
        bool zaznaczony{false};
    };

    using FunkcjaWidokuIkon = void (*)(WidokIkon* nadawca, int indeks, void* dane_uzytkownika);

    WidokIkon() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = Kolory::WejscieTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        wypelnienie_ = Marginesy(4, 4, 4, 4);
        czy_moze_miec_fokus_ = true;
        czy_reaguje_wizualnie_na_najechanie_ = false;
        czy_reaguje_wizualnie_na_wcisniecie_ = false;
        preferowany_rozmiar_ = Rozmiar(300, 220);

        pionowy_ = new PasekPrzewijania();
        pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        pionowy_->pokaz_strzalki(true);
        pionowy_->ustaw_maly_krok(wysokosc_komorki_);
        dodaj_kontrolke(pionowy_);

        pionowy_->przy_przewijaniu([](PasekPrzewijania*, int64_t, void* dane) {
            WidokIkon* w = static_cast<WidokIkon*>(dane);
            if (w) w->uniewaznij_obszar(w->prostokat_widoku());
        }, this);
    }

    ~WidokIkon() override {
        wyczysc();
        if (elementy_) {
            delete[] elementy_;
            elementy_ = nullptr;
        }
    }

    bool dodaj_element(const char* tekst, TypIkonyWidoku typ = TypIkonyWidoku::Plik, uint64_t znacznik = 0) {
        if (!zapewnij_pojemnosc(liczba_ + 1)) return false;
        char* kopia = kopiuj_tekst(tekst ? tekst : "");
        if (!kopia) return false;

        Element& el = elementy_[liczba_++];
        el.tekst = kopia;
        el.typ = typ;
        el.znacznik = znacznik;
        el.zaznaczony = false;

        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    bool usun_element(size_t indeks) {
        if (indeks >= liczba_) return false;
        if (elementy_[indeks].tekst) {
            delete[] elementy_[indeks].tekst;
            elementy_[indeks].tekst = nullptr;
        }
        for (size_t i = indeks; i + 1 < liczba_; ++i) {
            elementy_[i] = elementy_[i + 1];
        }
        --liczba_;
        elementy_[liczba_] = Element{};

        if (wybrany_indeks_ == static_cast<int>(indeks)) {
            wybrany_indeks_ = -1;
        } else if (wybrany_indeks_ > static_cast<int>(indeks)) {
            --wybrany_indeks_;
        }

        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    void wyczysc() {
        for (size_t i = 0; i < liczba_; ++i) {
            if (elementy_[i].tekst) {
                delete[] elementy_[i].tekst;
                elementy_[i].tekst = nullptr;
            }
        }
        liczba_ = 0;
        wybrany_indeks_ = -1;
        najechany_indeks_ = -1;
        aktualizuj_przewijanie();
        uniewaznij();
    }

    size_t liczba_elementow() const { return liczba_; }

    PasekPrzewijania& pasek_pionowy() { return *pionowy_; }
    const PasekPrzewijania& pasek_pionowy() const { return *pionowy_; }
    size_t liczba_ostatnio_narysowanych_elementow() const {
        return ostatnio_narysowane_elementy_;
    }
    int najechany_indeks() const { return najechany_indeks_; }
    Prostokat prostokat_widoku() const {
        return Prostokat(1, 1, viewport_w(), viewport_h());
    }

    const char* element(size_t indeks) const {
        return indeks < liczba_ ? elementy_[indeks].tekst : "";
    }

    TypIkonyWidoku typ_elementu(size_t indeks) const {
        return indeks < liczba_ ? elementy_[indeks].typ : TypIkonyWidoku::Plik;
    }

    uint64_t znacznik_elementu(size_t indeks) const {
        return indeks < liczba_ ? elementy_[indeks].znacznik : 0;
    }

    void ustaw_znacznik_elementu(size_t indeks, uint64_t tag) {
        if (indeks < liczba_) elementy_[indeks].znacznik = tag;
    }

    int wybrany_indeks() const { return wybrany_indeks_; }

    bool ustaw_wybrany_indeks(int indeks) {
        if (indeks < -1 || indeks >= static_cast<int>(liczba_)) return false;
        if (wybrany_indeks_ == indeks) return true;

        const int poprzedni = wybrany_indeks_;
        if (wybrany_indeks_ >= 0 && static_cast<size_t>(wybrany_indeks_) < liczba_) {
            elementy_[wybrany_indeks_].zaznaczony = false;
        }
        wybrany_indeks_ = indeks;
        if (wybrany_indeks_ >= 0) {
            elementy_[wybrany_indeks_].zaznaczony = true;
            przewin_do_indeksu(wybrany_indeks_);
        }

        uniewaznij_element(poprzedni);
        uniewaznij_element(wybrany_indeks_);
        if (cb_zmiana_zaznaczenia_) {
            cb_zmiana_zaznaczenia_(this, wybrany_indeks_, dane_zmiany_zaznaczenia_);
        }
        return true;
    }

    void przy_zmianie_zaznaczenia(FunkcjaWidokuIkon cb, void* dane = nullptr) {
        cb_zmiana_zaznaczenia_ = cb;
        dane_zmiany_zaznaczenia_ = dane;
    }

    void przy_aktywacji(FunkcjaWidokuIkon cb, void* dane = nullptr) {
        cb_aktywacja_ = cb;
        dane_aktywacji_ = dane;
    }

    int szerokosc_komorki() const { return szerokosc_komorki_; }
    int wysokosc_komorki() const { return wysokosc_komorki_; }

    void ustaw_rozmiar_komorki(int w, int h) {
        szerokosc_komorki_ = w < 40 ? 40 : w;
        wysokosc_komorki_ = h < 40 ? 40 : h;
        aktualizuj_przewijanie();
        uniewaznij();
    }

    void przewin_do_indeksu(int indeks) {
        if (indeks < 0 || indeks >= static_cast<int>(liczba_) || !pionowy_) return;
        int cols = liczba_kolumn_siatki();
        if (cols <= 0) return;
        int row = indeks / cols;
        int cell_h = wysokosc_komorki_ * skala_dpi_;
        int item_y = row * cell_h;
        int vp_h = viewport_h();
        int cur_scroll = static_cast<int>(pionowy_->wartosc());

        if (item_y < cur_scroll) {
            pionowy_->ustaw_wartosc(item_y);
        } else if (item_y + cell_h > cur_scroll + vp_h) {
            pionowy_->ustaw_wartosc(item_y + cell_h - vp_h);
        }
    }

    void ustaw_skale_dpi(SkalaUI s) override {
        Kontrolka::ustaw_skale_dpi(s);
        if (pionowy_) pionowy_->ustaw_maly_krok(wysokosc_komorki_ * skala_dpi_);
        aktualizuj_przewijanie();
    }

    void rozmiesc(Prostokat docelowy_obszar) override {
        Kontrolka::rozmiesc(docelowy_obszar);
        aktualizuj_geometrie_paskow();
        aktualizuj_przewijanie();
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        return Rozmiar(240, 180);
    }

    // =========================================================================
    // Rysowanie
    // =========================================================================

    void rysuj_siebie(KontekstRysowania& ctx) override {
        ostatnio_narysowane_elementy_ = 0;
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        int vp_w = viewport_w();
        int vp_h = viewport_h();
        if (vp_w <= 0 || vp_h <= 0 || liczba_ == 0) return;

        int cols = liczba_kolumn_siatki();
        if (cols <= 0) return;

        int cell_w = szerokosc_komorki_ * skala_dpi_;
        int cell_h = wysokosc_komorki_ * skala_dpi_;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;

        Prostokat rect_klienta(1, 1, vp_w, vp_h);
        Punkt off = ctx.pobierz_przesuniecie();
        Prostokat clip_swiat = ctx.pobierz_przyciecie();
        Prostokat clip_lokalny(clip_swiat.x - off.x, clip_swiat.y - off.y,
                               clip_swiat.szerokosc, clip_swiat.wysokosc);
        Prostokat clip_elementow = rect_klienta.przeciecie(clip_lokalny);
        if (clip_elementow.czy_pusty()) return;
        ctx.dodaj_przyciecie(rect_klienta);

        int start_row = (scroll_y + clip_elementow.y - rect_klienta.y) / cell_h;
        if (start_row < 0) start_row = 0;
        int end_row = (scroll_y + clip_elementow.dolny() - rect_klienta.y - 1) /
                      cell_h + 1;
        size_t start_idx = static_cast<size_t>(start_row * cols);
        size_t end_idx = static_cast<size_t>(end_row * cols);
        if (end_idx > liczba_) end_idx = liczba_;

        for (size_t i = start_idx; i < end_idx; ++i) {
            ++ostatnio_narysowane_elementy_;
            int row = static_cast<int>(i) / cols;
            int col = static_cast<int>(i) % cols;

            int item_x = 1 + col * cell_w;
            int item_y = 1 + row * cell_h - scroll_y;
            Prostokat item_rect(item_x + 2, item_y + 2, cell_w - 4, cell_h - 4);

            bool sel = (static_cast<int>(i) == wybrany_indeks_);
            bool hov = (static_cast<int>(i) == najechany_indeks_ && czy_wlaczona_);

            if (sel) {
                ctx.rysuj_zaokraglony_prostokat(item_rect, Kolory::ZaznaczenieTlo, 4 * skala_dpi_);
                ctx.rysuj_zaokraglona_ramke(item_rect, Kolory::AkcentFokus,
                                            4 * skala_dpi_, 1);
            } else if (hov) {
                ctx.rysuj_zaokraglony_prostokat(item_rect, 0x003A2814, 4 * skala_dpi_);
                ctx.rysuj_zaokraglona_ramke(item_rect, Kolory::RamkaDomyslna, 4 * skala_dpi_, 1);
            }

            // Ikona
            int icon_w = 32 * skala_dpi_;
            int icon_h = 32 * skala_dpi_;
            int icon_x = item_rect.x + (item_rect.szerokosc - icon_w) / 2;
            int icon_y = item_rect.y + 4 * skala_dpi_;
            rysuj_ikone_elementu(ctx, elementy_[i].typ, icon_x, icon_y, icon_w, icon_h);

            // Etykieta
            int txt_y = icon_y + icon_h + 2 * skala_dpi_;
            int txt_w = item_rect.szerokosc - 4;
            uint32_t col_txt = sel ? Kolory::ZaznaczenieTekst : s.tekst;

            ctx.rysuj_tekst_obciety(Punkt(item_rect.x + 2, txt_y),
                                    elementy_[i].tekst, col_txt, txt_w, 1);
        }

        ctx.zdejmij_przyciecie();
    }

    // =========================================================================
    // Obsługa wejścia
    // =========================================================================

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        int idx = indeks_pod_punktem(lx, ly);
        if (idx >= 0 && idx < static_cast<int>(liczba_)) {
            ustaw_wybrany_indeks(idx);
            e.obsluzone = true;
        }
    }

    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_podwojne_klikniecie(e);
        if (wybrany_indeks_ >= 0 && cb_aktywacja_) {
            cb_aktywacja_(this, wybrany_indeks_, dane_aktywacji_);
            e.obsluzone = true;
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_ruch_myszy(e);
        if (!czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;
        int idx = indeks_pod_punktem(lx, ly);
        if (idx != najechany_indeks_) {
            const int poprzedni = najechany_indeks_;
            najechany_indeks_ = idx;
            uniewaznij_element(poprzedni);
            uniewaznij_element(najechany_indeks_);
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_indeks_ != -1) {
            const int poprzedni = najechany_indeks_;
            najechany_indeks_ = -1;
            uniewaznij_element(poprzedni);
        }
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (!czy_wlaczona_ || e.delta_y == 0 || !pionowy_ ||
            !pionowy_->czy_widoczna()) return;
        const int64_t stara = pionowy_->wartosc();
        const int64_t krok = static_cast<int64_t>(wysokosc_komorki_) *
                             skala_dpi_;
        pionowy_->przewin_o(e.delta_y > 0 ? -krok : krok);
        if (pionowy_->wartosc() != stara) e.obsluzone = true;
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        Kontrolka::obsluz_wcisniecie_klawisza(e);
        if (!czy_wlaczona_ || liczba_ == 0) return;

        int cols = liczba_kolumn_siatki();
        if (cols <= 0) return;

        uint32_t kod = e.kod_klawisza;
        bool usb = (kod & BWS_KEY_SOURCE_USB) != 0;
        bool e0 = (kod & BWS_KEY_SOURCE_E0) != 0;
        uint32_t baza = usb ? (kod & 0xFFU) : (kod & 0x7FU);

        if ((usb && baza == 79U) || (e0 && baza == 0x4DU)) { // Right
            int next = wybrany_indeks_ + 1;
            if (next < static_cast<int>(liczba_)) ustaw_wybrany_indeks(next);
            e.obsluzone = true;
        } else if ((usb && baza == 80U) || (e0 && baza == 0x4BU)) { // Left
            int prev = wybrany_indeks_ - 1;
            if (prev >= 0) ustaw_wybrany_indeks(prev);
            e.obsluzone = true;
        } else if ((usb && baza == 81U) || (e0 && baza == 0x50U)) { // Down
            int target = wybrany_indeks_ + cols;
            if (target < static_cast<int>(liczba_)) ustaw_wybrany_indeks(target);
            e.obsluzone = true;
        } else if ((usb && baza == 82U) || (e0 && baza == 0x48U)) { // Up
            int target = wybrany_indeks_ - cols;
            if (target >= 0) ustaw_wybrany_indeks(target);
            e.obsluzone = true;
        } else if ((usb && baza == 78U) || (e0 && baza == 0x51U)) { // Page Down
            int cell_h = wysokosc_komorki_ * skala_dpi_;
            int rows_per_page = viewport_h() / (cell_h > 0 ? cell_h : 1);
            int target = wybrany_indeks_ + rows_per_page * cols;
            if (target >= static_cast<int>(liczba_)) target = static_cast<int>(liczba_ - 1);
            ustaw_wybrany_indeks(target);
            e.obsluzone = true;
        } else if ((usb && baza == 75U) || (e0 && baza == 0x49U)) { // Page Up
            int cell_h = wysokosc_komorki_ * skala_dpi_;
            int rows_per_page = viewport_h() / (cell_h > 0 ? cell_h : 1);
            int target = wybrany_indeks_ - rows_per_page * cols;
            if (target < 0) target = 0;
            ustaw_wybrany_indeks(target);
            e.obsluzone = true;
        } else if ((usb && baza == 74U) || (e0 && baza == 0x47U)) { // Home
            ustaw_wybrany_indeks(0);
            e.obsluzone = true;
        } else if ((usb && baza == 77U) || (e0 && baza == 0x4FU)) { // End
            ustaw_wybrany_indeks(static_cast<int>(liczba_ - 1));
            e.obsluzone = true;
        } else if (kod == 13 || (usb && baza == 40U)) { // Enter
            if (wybrany_indeks_ >= 0 && cb_aktywacja_) {
                cb_aktywacja_(this, wybrany_indeks_, dane_aktywacji_);
                e.obsluzone = true;
            }
        }
    }

private:
    Element* elementy_{nullptr};
    size_t liczba_{0};
    size_t pojemnosc_{0};

    int wybrany_indeks_{-1};
    int najechany_indeks_{-1};
    size_t ostatnio_narysowane_elementy_{0};

    int szerokosc_komorki_{72};
    int wysokosc_komorki_{72};
    int szerokosc_paska_{14};

    PasekPrzewijania* pionowy_{nullptr};

    FunkcjaWidokuIkon cb_zmiana_zaznaczenia_{nullptr};
    void* dane_zmiany_zaznaczenia_{nullptr};

    FunkcjaWidokuIkon cb_aktywacja_{nullptr};
    void* dane_aktywacji_{nullptr};

    static char* kopiuj_tekst(const char* s) {
        if (!s) s = "";
        size_t len = 0;
        while (s[len] != '\0') ++len;
        char* b = new char[len + 1];
        for (size_t i = 0; i <= len; ++i) b[i] = s[i];
        return b;
    }

    bool zapewnij_pojemnosc(size_t req) {
        if (req <= pojemnosc_) return true;
        size_t nowa = pojemnosc_ == 0 ? 32 : pojemnosc_ * 2;
        if (nowa < req) nowa = req;
        Element* n = new Element[nowa];
        if (!n) return false;
        for (size_t i = 0; i < liczba_; ++i) n[i] = elementy_[i];
        for (size_t i = liczba_; i < nowa; ++i) n[i] = Element{};
        if (elementy_) delete[] elementy_;
        elementy_ = n;
        pojemnosc_ = nowa;
        return true;
    }

    int viewport_w() const {
        int w = obszar_.szerokosc - 2;
        if (pionowy_ && pionowy_->czy_widoczna()) {
            w -= pionowy_->obszar().szerokosc;
        }
        return w > 0 ? w : 0;
    }

    int viewport_h() const {
        int h = obszar_.wysokosc - 2;
        return h > 0 ? h : 0;
    }

    int liczba_kolumn_siatki() const {
        int cell_w = szerokosc_komorki_ * skala_dpi_;
        if (cell_w <= 0) return 1;
        int cols = viewport_w() / cell_w;
        return cols > 0 ? cols : 1;
    }

    int indeks_pod_punktem(int lx, int ly) const {
        int vp_w = viewport_w();
        int vp_h = viewport_h();
        if (lx < 1 || lx >= 1 + vp_w || ly < 1 || ly >= 1 + vp_h) return -1;

        int cols = liczba_kolumn_siatki();
        if (cols <= 0) return -1;

        int cell_w = szerokosc_komorki_ * skala_dpi_;
        int cell_h = wysokosc_komorki_ * skala_dpi_;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;

        int col = (lx - 1) / cell_w;
        int row = (ly - 1 + scroll_y) / cell_h;
        if (col < 0 || col >= cols || row < 0) return -1;

        int idx = row * cols + col;
        return (idx >= 0 && idx < static_cast<int>(liczba_)) ? idx : -1;
    }

    void aktualizuj_geometrie_paskow() {
        int cols = liczba_kolumn_siatki();
        int rows = cols > 0 ? static_cast<int>((liczba_ + cols - 1) / cols) : 0;
        int total_h = rows * (wysokosc_komorki_ * skala_dpi_);
        bool potr = total_h > (obszar_.wysokosc - 2);

        int sb_sz = szerokosc_paska_ * skala_dpi_;
        if (pionowy_) {
            pionowy_->ustaw_widoczna(potr);
            if (potr) {
                pionowy_->rozmiesc(Prostokat(
                    obszar_.szerokosc - sb_sz - 1, 1, sb_sz,
                    obszar_.wysokosc - 2));
            }
        }
    }

    void aktualizuj_przewijanie() {
        aktualizuj_geometrie_paskow();
        if (pionowy_) {
            int cols = liczba_kolumn_siatki();
            int rows = cols > 0 ? static_cast<int>((liczba_ + cols - 1) / cols) : 0;
            int total_h = rows * (wysokosc_komorki_ * skala_dpi_);
            int strona = viewport_h() > 0 ? viewport_h() : 1;
            int maks = total_h - strona;
            if (maks < 0) maks = 0;
            pionowy_->ustaw_zakres(0, maks);
            pionowy_->ustaw_rozmiar_strony(strona);
            pionowy_->ustaw_duzy_krok(strona);
        }
    }

    void uniewaznij_element(int indeks) {
        if (indeks < 0 || indeks >= static_cast<int>(liczba_)) return;
        const int kolumny = liczba_kolumn_siatki();
        if (kolumny <= 0) return;
        const int w = szerokosc_komorki_ * skala_dpi_;
        const int h = wysokosc_komorki_ * skala_dpi_;
        const int scroll = pionowy_ && pionowy_->czy_widoczna()
            ? static_cast<int>(pionowy_->wartosc()) : 0;
        Prostokat r(1 + (indeks % kolumny) * w,
                    1 + (indeks / kolumny) * h - scroll, w, h);
        r = r.przeciecie(Prostokat(1, 1, viewport_w(), viewport_h()));
        if (!r.czy_pusty()) uniewaznij_obszar(r);
    }

    void rysuj_ikone_elementu(KontekstRysowania& ctx, TypIkonyWidoku typ, int x, int y, int w, int h) {
        switch (typ) {
            case TypIkonyWidoku::Folder:
                ctx.rysuj_prostokat(Prostokat(x, y + 4, w, h - 4), 0x00FFBF00);
                ctx.rysuj_prostokat(Prostokat(x + 2, y + 8, w - 4, h - 10), 0x00D59600);
                ctx.rysuj_prostokat(Prostokat(x + 2, y + 2, w / 2, 6), 0x00FFBF00);
                break;
            case TypIkonyWidoku::Aplikacja:
                ctx.rysuj_zaokraglony_prostokat(Prostokat(x, y, w, h), 0x00301500, 4);
                ctx.rysuj_zaokraglona_ramke(Prostokat(x, y, w, h), 0x00E58A00, 4, 1);
                ctx.rysuj_prostokat(Prostokat(x + 4, y + 4, w - 8, 6), 0x00E58A00);
                ctx.rysuj_prostokat(Prostokat(x + 4, y + 14, (w - 12) / 2, h - 18), 0x00FFBF00);
                ctx.rysuj_prostokat(Prostokat(x + w / 2 + 2, y + 14, (w - 12) / 2, h - 18), 0x00FFBF00);
                break;
            case TypIkonyWidoku::Obraz:
                ctx.rysuj_zaokraglony_prostokat(Prostokat(x, y, w, h), 0x002080C0, 3);
                ctx.rysuj_zaokraglona_ramke(Prostokat(x, y, w, h), 0x00FFFFFF, 3, 1);
                ctx.rysuj_prostokat(Prostokat(x + 6, y + h - 12, 10, 8), 0x0020A040);
                ctx.rysuj_prostokat(Prostokat(x + 14, y + h - 16, 12, 12), 0x0030C060);
                ctx.rysuj_prostokat(Prostokat(x + w - 10, y + 6, 4, 4), 0x00FFD700);
                break;
            case TypIkonyWidoku::Dysk:
                ctx.rysuj_zaokraglony_prostokat(Prostokat(x, y + 6, w, h - 12), 0x00505050, 2);
                ctx.rysuj_zaokraglona_ramke(Prostokat(x, y + 6, w, h - 12), 0x00A0A0A0, 2, 1);
                ctx.rysuj_prostokat(Prostokat(x + w - 8, y + h - 10, 4, 2), 0x0000FF00);
                break;
            case TypIkonyWidoku::Ustawienia:
                ctx.rysuj_zaokraglony_prostokat(Prostokat(x + 4, y + 4, w - 8, h - 8), 0x00808080, 4);
                ctx.rysuj_zaokraglona_ramke(Prostokat(x + 4, y + 4, w - 8, h - 8), 0x00C0C0C0, 4, 2);
                ctx.rysuj_prostokat(Prostokat(x + (w - 6) / 2, y + (h - 6) / 2, 6, 6), 0x002B1D0E);
                break;
            case TypIkonyWidoku::Dokument:
            case TypIkonyWidoku::Plik:
            default:
                ctx.rysuj_prostokat(Prostokat(x + 2, y, w - 4, h), 0x00E0E0E0);
                ctx.rysuj_ramke(Prostokat(x + 2, y, w - 4, h), 0x00707070);
                ctx.rysuj_prostokat(Prostokat(x + 6, y + 8, w - 12, 2), 0x00505050);
                ctx.rysuj_prostokat(Prostokat(x + 6, y + 14, w - 12, 2), 0x00505050);
                ctx.rysuj_prostokat(Prostokat(x + 6, y + 20, w - 16, 2), 0x00505050);
                break;
        }
    }
};

} // namespace BursztynUI
