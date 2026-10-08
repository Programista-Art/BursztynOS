#pragma once

#include "popup.h"
#include "przycisk.h"
#include "panel.h"
#include "panel_dokowany.h"
#include "motyw.h"

namespace BursztynUI {

enum class TypDialogu : uint8_t {
    Zwykly,
    Informacja,
    Ostrzezenie,
    Blad,
    Pytanie,
    Sukces
};

enum class WynikDialogu : uint8_t {
    Brak = 0,
    OK = 1,
    Anuluj = 2,
    Tak = 3,
    Nie = 4,
    Ponow = 5,
    Przerwij = 6,
    Ignoruj = 7,
    Wlasny = 8
};

enum class PrzyciskiDialogu : uint8_t {
    OkAnuluj,
    TakNie,
    TylkoOk,
    TakNieAnuluj,
    PonowAnuluj,
    PrzerwijPonowIgnoruj,
    Wlasne
};

namespace SzczegolyDialogu {

inline void kopiuj_napis(char* cel, size_t cap, const char* zr) {
    if (!cel || cap == 0) return;
    if (!zr) { cel[0] = '\0'; return; }
    size_t i = 0;
    while (zr[i] && i + 1 < cap) {
        cel[i] = zr[i];
        ++i;
    }
    cel[i] = '\0';
}

inline void rysuj_linie_wektorowa(KontekstRysowania& ctx, int x0, int y0, int x1, int y1,
                                  int grubosc, uint32_t kolor) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int pol = grubosc / 2;
    while (true) {
        ctx.rysuj_prostokat(Prostokat(x0 - pol, y0 - pol, grubosc, grubosc), kolor);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

} // namespace SzczegolyDialogu

class OknoDialogowe : public PowierzchniaPopup {
public:
    using FunkcjaWyniku = void (*)(OknoDialogowe* dialog, WynikDialogu wynik, void* dane);

    explicit OknoDialogowe(const char* tytul = "Komunikat",
                           const char* wiadomosc = "",
                           PrzyciskiDialogu przyciski = PrzyciskiDialogu::OkAnuluj,
                           TypDialogu typ = TypDialogu::Informacja)
        : przyciski_typ_(przyciski), typ_(typ) {
        ustaw_modalna(true);
        ustaw_zamykanie_na_klik_poza(false);
        ustaw_tekst(tytul);
        zaktualizuj_motyw();
        ustaw_wiadomosc(wiadomosc);
        inicjalizuj_przyciski(przyciski);
    }

    ~OknoDialogowe() override = default;

    uint16_t promien_zaokraglenia() const override {
        return motyw_promien(MOTYW_ROG_OKNO);
    }

    Kontrolka* sprawdz_trafienie(Punkt pt) override {
        if (!czy_widoczna_ || !obszar_.zawiera(pt)) return nullptr;

        int r = motyw_promien(MOTYW_ROG_OKNO) * skala_dpi_;
        if (r > 0) {
            int lx = pt.x - obszar_.x;
            int ly = pt.y - obszar_.y;
            // Lewy gorny
            if (ly >= 0 && ly < r && lx >= 0 && lx < r) {
                int inset = motyw_promien_inset(r, ly);
                if (lx < inset) return nullptr;
            }
            // Prawy gorny
            if (ly >= 0 && ly < r && lx >= obszar_.szerokosc - r && lx < obszar_.szerokosc) {
                int inset = motyw_promien_inset(r, ly);
                if (lx >= obszar_.szerokosc - inset) return nullptr;
            }
            // Lewy dolny
            if (ly >= obszar_.wysokosc - r && ly < obszar_.wysokosc && lx >= 0 && lx < r) {
                int dy = obszar_.wysokosc - 1 - ly;
                int inset = motyw_promien_inset(r, dy);
                if (lx < inset) return nullptr;
            }
            // Prawy dolny
            if (ly >= obszar_.wysokosc - r && ly < obszar_.wysokosc &&
                lx >= obszar_.szerokosc - r && lx < obszar_.szerokosc) {
                int dy = obszar_.wysokosc - 1 - ly;
                int inset = motyw_promien_inset(r, dy);
                if (lx >= obszar_.szerokosc - inset) return nullptr;
            }
        }

        return PowierzchniaPopup::sprawdz_trafienie(pt);
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_okna;
        styl_.normalny.tlo = motyw_kolor(MOTYW_KOLOR_OKNOTLO);
        styl_.normalny.ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        styl_.normalny.grubosc_ramki = 2;
        styl_.normalny.promien_zaokraglenia = motyw_promien(MOTYW_ROG_OKNO);
        Kontrolka::zaktualizuj_motyw();
        if (uklad_) wyzeruj_tlo(*uklad_);
        if (panel_przyciskow_) wyzeruj_tlo(*panel_przyciskow_);
    }

    const char* wiadomosc() const { return wiadomosc_; }
    void ustaw_wiadomosc(const char* w) {
        SzczegolyDialogu::kopiuj_napis(wiadomosc_, sizeof(wiadomosc_), w);
        podziel_tekst_na_linie(400 * skala_dpi_);
        uniewaznij();
    }

    TypDialogu typ_dialogu() const { return typ_; }
    void ustaw_typ_dialogu(TypDialogu t) {
        if (typ_ != t) {
            typ_ = t;
            uniewaznij();
        }
    }

    PrzyciskiDialogu przyciski_typ() const { return przyciski_typ_; }
    void ustaw_przyciski_typ(PrzyciskiDialogu p) {
        inicjalizuj_przyciski(p);
        uniewaznij();
    }

    WynikDialogu wynik() const { return wynik_; }
    WynikDialogu wynik_domyslny() const { return wynik_domyslny_; }
    WynikDialogu wynik_anuluj() const { return wynik_anuluj_; }
    size_t liczba_przyciskow() const { return liczba_przyciskow_; }
    Przycisk* przycisk(size_t i) const { return i < liczba_przyciskow_ ? sloty_[i].przycisk : nullptr; }
    WynikDialogu wynik_przycisku(size_t i) const { return i < liczba_przyciskow_ ? sloty_[i].wynik : WynikDialogu::Brak; }
    Prostokat obszar_tresci() const { return obszar_tresci_; }
    Prostokat obszar_przyciskow() const { return obszar_przyciskow_; }
    PanelDokowany* uklad() const { return uklad_; }
    Kontrolka* panel_tresci() const { return panel_tresci_; }
    Wiersz* panel_przyciskow() const { return panel_przyciskow_; }
    void wyczysc_sloty_przyciskow() { liczba_przyciskow_ = 0; indeks_fokusu_przycisku_ = 0; }

    void rozmiesc(Prostokat docelowy) override {
        bool zmiana = obszar_.szerokosc != docelowy.szerokosc || obszar_.wysokosc != docelowy.wysokosc;
        obszar_ = docelowy;
        Punkt rp = rodzic_ ? rodzic_->obszar_ekranu().pozycja() : Punkt(0, 0);
        obszar_ekranu_ = Prostokat(rp + obszar_.pozycja(), obszar_.rozmiar());
        if (uklad_) {
            int belka = 32 * skala_dpi_;
            uklad_->rozmiesc(Prostokat(0, belka, obszar_.szerokosc,
                                      obszar_.wysokosc > belka ? obszar_.wysokosc - belka : 0));
            aktualizuj_obszary_ukladu();
        }
        zaktualizuj_pozycje_ekranowa();
        if (zmiana) obsluz_zmiane_rozmiaru();
    }

    Prostokat prostokat_przycisku_zamknij() const {
        SkalaUI s = skala_dpi_;
        int h_belki = 32 * s;
        int btn_size = 20 * s;
        int btn_x = obszar_.szerokosc - 26 * s;
        int btn_y = (h_belki - btn_size) / 2;
        return Prostokat(btn_x, btn_y, btn_size, btn_size);
    }

    bool czy_przeciagane() const { return przeciaganie_; }

    void ustaw_przycisk_domyslny(WynikDialogu w) {
        wynik_domyslny_ = w;
        for (size_t i = 0; i < liczba_przyciskow_; ++i) {
            sloty_[i].domyslny = (sloty_[i].wynik == w);
            if (sloty_[i].domyslny) {
                indeks_fokusu_przycisku_ = static_cast<int>(i);
                if (sloty_[i].przycisk) {
                    sloty_[i].przycisk->ustaw_kolor_ramki_stanu(StanWizualny::Normalny, motyw_kolor(MOTYW_KOLOR_AKCENT));
                    sloty_[i].przycisk->ustaw_grubosc_ramki(2);
                }
            } else if (sloty_[i].przycisk) {
                sloty_[i].przycisk->ustaw_kolor_ramki_stanu(StanWizualny::Normalny, motyw_kolor(MOTYW_KOLOR_RAMKA));
                sloty_[i].przycisk->ustaw_grubosc_ramki(1);
            }
        }
    }

    void ustaw_przycisk_anuluj(WynikDialogu w) {
        wynik_anuluj_ = w;
        for (size_t i = 0; i < liczba_przyciskow_; ++i) {
            sloty_[i].anuluj = (sloty_[i].wynik == w);
        }
    }

    bool zarejestruj_przycisk_akcji(Przycisk* btn, WynikDialogu w, bool domyslny = false, bool anuluj = false) {
        if (!btn || liczba_przyciskow_ >= MAKS_PRZYCISKOW) return false;
        size_t idx = liczba_przyciskow_++;
        sloty_[idx].dialog = this;
        sloty_[idx].wynik = w;
        sloty_[idx].domyslny = domyslny;
        sloty_[idx].anuluj = anuluj;
        sloty_[idx].przycisk = btn;
        SzczegolyDialogu::kopiuj_napis(sloty_[idx].etykieta, sizeof(sloty_[idx].etykieta), btn->tekst());
        if (domyslny) {
            wynik_domyslny_ = w;
            indeks_fokusu_przycisku_ = static_cast<int>(idx);
            btn->ustaw_kolor_ramki_stanu(StanWizualny::Normalny, motyw_kolor(MOTYW_KOLOR_AKCENT));
            btn->ustaw_grubosc_ramki(2);
        }
        if (anuluj) {
            wynik_anuluj_ = w;
        }
        btn->przy_kliknieciu([](Przycisk*, void* d) {
            PrzyciskSlot* slot = static_cast<PrzyciskSlot*>(d);
            slot->dialog->zamknij_z_wynikiem(slot->wynik);
        }, &sloty_[idx]);
        return true;
    }

    void dodaj_przycisk(const char* etykieta, WynikDialogu w, bool domyslny = false, bool anuluj = false) {
        if (liczba_przyciskow_ >= MAKS_PRZYCISKOW) return;
        przyciski_typ_ = PrzyciskiDialogu::Wlasne;
        dodaj_slot_przycisku(etykieta, w, domyslny, anuluj);
        int dw = 0, dh = 0;
        oblicz_geometrie_dialogu(dw, dh);
        zbuduj_interfejs(dw, dh);
    }

    void przy_wyniku(FunkcjaWyniku cb, void* dane = nullptr) {
        cb_wynik_ = cb;
        dane_wyniku_ = dane;
    }

    bool pokaz(ZarzadcaPopupow& z, Kontrolka* owner = nullptr) {
        wlasciciel_ = owner;
        if (z.fokus_popupu()) {
            fokus_poprzedni_ = z.fokus_popupu();
        } else if (owner) {
            fokus_poprzedni_ = owner;
        }

        int dialog_w = 0;
        int dialog_h = 0;
        oblicz_geometrie_dialogu(dialog_w, dialog_h);

        int cx = 0;
        int cy = 0;
        if (owner) {
            Prostokat oe = owner->prostokat_do_ekranu(Prostokat(0, 0, owner->szerokosc(), owner->wysokosc()));
            cx = oe.x + (oe.szerokosc - dialog_w) / 2;
            cy = oe.y + (oe.wysokosc - dialog_h) / 2;
        } else {
            Prostokat work = z.obszar_roboczy_ekranu();
            cx = work.x + (work.szerokosc - dialog_w) / 2;
            cy = work.y + (work.wysokosc - dialog_h) / 2;
        }

        Prostokat gdzie = z.umiesc_przy_punkcie_ekranu(cx, cy, Rozmiar(dialog_w, dialog_h));
        wynik_ = WynikDialogu::Brak;

        zbuduj_interfejs(dialog_w, dialog_h);

        Przycisk* fokus_startowy = nullptr;
        if (indeks_fokusu_przycisku_ >= 0 && indeks_fokusu_przycisku_ < static_cast<int>(liczba_przyciskow_)) {
            fokus_startowy = sloty_[indeks_fokusu_przycisku_].przycisk;
        }

        int p_level = 0;
        if (owner) {
            int ow_p = z.powierzchnia_dla(owner);
            if (ow_p >= 0) p_level = ow_p + 1;
        }
        z.zamknij_od_poziomu(p_level);
        return z.dodaj_powierzchnie(*this, gdzie, fokus_startowy ? static_cast<Kontrolka*>(fokus_startowy) : this);
    }

    virtual void zamknij_z_wynikiem(WynikDialogu w) {
        wynik_ = w;
        if (cb_wynik_) {
            cb_wynik_(this, wynik_, dane_wyniku_);
        }
        if (zarzadca()) {
            zarzadca()->zamknij_powierzchnie(this,
                (w == WynikDialogu::Anuluj || w == WynikDialogu::Nie || w == WynikDialogu::Ignoruj)
                    ? PowodZamknieciaPopup::Anulowanie
                    : PowodZamknieciaPopup::Zatwierdzenie);
        }
        if (fokus_poprzedni_) {
            fokus_poprzedni_->przy_fokusie();
            fokus_poprzedni_ = nullptr;
        }
    }

    void ustaw_fokus_przycisku(int idx) {
        if (idx < 0 || idx >= static_cast<int>(liczba_przyciskow_)) return;
        indeks_fokusu_przycisku_ = idx;
        Przycisk* btn = sloty_[idx].przycisk;
        if (btn) {
            if (zarzadca()) {
                zarzadca()->ustaw_fokus_popupu(btn);
            } else {
                for (size_t i = 0; i < liczba_przyciskow_; ++i) {
                    if (sloty_[i].przycisk) sloty_[i].przycisk->przy_utracie_fokusu();
                }
                btn->przy_fokusie();
            }
            btn->uniewaznij();
        }
    }

    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        uint32_t kod = e.polecenie();

        // Escape: anuluj
        if (kod == 27U) {
            zamknij_z_wynikiem(wynik_anuluj_);
            e.obsluzone = true;
            return;
        }

        // Enter: wykonaj skupiony lub domyslny przycisk
        if (kod == 13U) {
            if (indeks_fokusu_przycisku_ >= 0 && indeks_fokusu_przycisku_ < static_cast<int>(liczba_przyciskow_)) {
                zamknij_z_wynikiem(sloty_[indeks_fokusu_przycisku_].wynik);
            } else {
                zamknij_z_wynikiem(wynik_domyslny_);
            }
            e.obsluzone = true;
            return;
        }

        // Tab / Shift+Tab oraz strzalki przechodza po rzeczywistych przyciskach.
        const bool tab = kod == 9U;
        if (tab && e.czy_shift()) {
            if (liczba_przyciskow_ > 1) {
                int poprz = (indeks_fokusu_przycisku_ + static_cast<int>(liczba_przyciskow_) - 1) % static_cast<int>(liczba_przyciskow_);
                ustaw_fokus_przycisku(poprz);
                e.obsluzone = true;
                return;
            }
        }
        if (tab || kod == 0x4DU) {
            if (liczba_przyciskow_ > 1) {
                int nast = (indeks_fokusu_przycisku_ + 1) % static_cast<int>(liczba_przyciskow_);
                ustaw_fokus_przycisku(nast);
                e.obsluzone = true;
                return;
            }
        }

        // Strzalka w lewo: poprzedni przycisk
        if (kod == 0x4BU) {
            if (liczba_przyciskow_ > 1) {
                int poprz = (indeks_fokusu_przycisku_ + static_cast<int>(liczba_przyciskow_) - 1) % static_cast<int>(liczba_przyciskow_);
                ustaw_fokus_przycisku(poprz);
                e.obsluzone = true;
                return;
            }
        }

        // Spacja: jesli przycisk ma indeks fokusu, kliknij
        if (kod == 32U) {
            if (indeks_fokusu_przycisku_ >= 0 && indeks_fokusu_przycisku_ < static_cast<int>(liczba_przyciskow_)) {
                zamknij_z_wynikiem(sloty_[indeks_fokusu_przycisku_].wynik);
                e.obsluzone = true;
                return;
            }
        }

        Kontrolka::obsluz_wcisniecie_klawisza(e);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        SkalaUI s = skala_dpi_;
        int promien = motyw_promien(MOTYW_ROG_OKNO) * s;
        rysuj_zaokraglone_tlo(ctx,styl_.normalny.tlo,styl_.normalny.ramka,0);

        // Belka naglowkowa
        int h_belki = 32 * s;
        Prostokat belka(0, 0, obszar_.szerokosc, h_belki);
        ctx.rysuj_zaokraglony_prostokat(belka, motyw_kolor(MOTYW_KOLOR_PANEL), promien);
        ctx.rysuj_prostokat(Prostokat(0, h_belki - 6 * s, obszar_.szerokosc, 6 * s), motyw_kolor(MOTYW_KOLOR_PANEL));
        ctx.rysuj_linie_pozioma(0, h_belki, obszar_.szerokosc, motyw_kolor(MOTYW_KOLOR_RAMKA));

        // Tytul w belce
        ctx.rysuj_tekst(Punkt(14 * s, (h_belki - CzcionkaUI::pobierz_wysokosc(s)) / 2),
                        tekst_, motyw_kolor(MOTYW_KOLOR_TEKST), s);

        // Przycisk zamkniecia [X] w belce
        Prostokat r_close = prostokat_przycisku_zamknij();
        if (najechany_zamknij_) {
            uint32_t close_bg = wcisniety_zamknij_ ? 0x00D32F2F : 0x00E53935;
            ctx.rysuj_zaokraglony_prostokat(r_close, close_bg, 3 * s);
        }
        uint32_t close_kolor = najechany_zamknij_ ? 0x00FFFFFF : motyw_kolor(MOTYW_KOLOR_TEKST);
        int cx = r_close.x + r_close.szerokosc / 2;
        int cy = r_close.y + r_close.wysokosc / 2;
        int d = 4 * s;
        SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - d, cy - d, cx + d, cy + d, 2 * s, close_kolor);
        SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - d, cy + d, cx + d, cy - d, 2 * s, close_kolor);

        // Renderowanie ikony semantycznej
        int start_x = obszar_tresci_.x + 16 * s;
        int txt_start_x = start_x;
        int txt_start_y = obszar_tresci_.y + 12 * s;

        if (typ_ != TypDialogu::Zwykly) {
            int ikona_rozm = 36 * s;
            Prostokat r_ikona(start_x, txt_start_y, ikona_rozm, ikona_rozm);
            rysuj_ikone_dialogu(ctx, r_ikona, typ_, s);
            txt_start_x += ikona_rozm + 16 * s;
        }

        // Renderowanie zawinietego tekstu z unifikacja krojow
        int font_h = CzcionkaUI::pobierz_wysokosc(s);
        int line_gap = 4 * s;
        uint32_t kolor_tekstu = motyw_kolor(MOTYW_KOLOR_TEKST);

        for (size_t i = 0; i < liczba_linii_; ++i) {
            int ty = txt_start_y + static_cast<int>(i) * (font_h + line_gap);
            ctx.rysuj_tekst(Punkt(txt_start_x, ty), linie_[i].tekst, kolor_tekstu, s);
        }

        // Subtelna linia oddzielajaca pasek przyciskow
        int sep_y = obszar_przyciskow_.y;
        ctx.rysuj_linie_pozioma(12 * s, sep_y, obszar_.szerokosc - 24 * s, motyw_kolor(MOTYW_KOLOR_RAMKA));
        rysuj_ramke_popupu(ctx,styl_.normalny.ramka,2);
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (!e.czy_lewy_przycisk()) {
            PowierzchniaPopup::obsluz_wcisniecie_myszy(e);
            return;
        }
        int lx = e.x - obszar_.x;
        int ly = e.y - obszar_.y;
        if (prostokat_przycisku_zamknij().zawiera(lx, ly)) {
            wcisniety_zamknij_ = true;
            najechany_zamknij_ = true;
            uniewaznij();
            e.obsluzone = true;
            return;
        }
        if (ly >= 0 && ly < 32 * static_cast<int>(skala_dpi_)) {
            przeciaganie_ = true;
            offset_ciagniecia_x_ = lx;
            offset_ciagniecia_y_ = ly;
            gui_ustaw_capture_myszy(true);
            e.obsluzone = true;
            return;
        }
        PowierzchniaPopup::obsluz_wcisniecie_myszy(e);
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        int lx = e.x - obszar_.x;
        int ly = e.y - obszar_.y;
        if (przeciaganie_ && zarzadca()) {
            int nowy_x = e.x - offset_ciagniecia_x_;
            int nowy_y = e.y - offset_ciagniecia_y_;
            Prostokat work = zarzadca()->obszar_roboczy_ekranu();
            int min_x = work.x;
            int min_y = work.y;
            int max_x = work.prawy() - 60 * static_cast<int>(skala_dpi_);
            int max_y = work.dolny() - 32 * static_cast<int>(skala_dpi_);
            if (nowy_x < min_x) nowy_x = min_x;
            if (nowy_x > max_x) nowy_x = max_x;
            if (nowy_y < min_y) nowy_y = min_y;
            if (nowy_y > max_y) nowy_y = max_y;
            zarzadca()->przemiesc_powierzchnie(*this, Prostokat(nowy_x, nowy_y, obszar_.szerokosc, obszar_.wysokosc));
            e.obsluzone = true;
            return;
        }
        bool hover = prostokat_przycisku_zamknij().zawiera(lx, ly);
        if (hover != najechany_zamknij_) {
            najechany_zamknij_ = hover;
            uniewaznij();
        }
        PowierzchniaPopup::obsluz_ruch_myszy(e);
    }

    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override {
        int lx = e.x - obszar_.x;
        int ly = e.y - obszar_.y;
        if (przeciaganie_) {
            przeciaganie_ = false;
            gui_ustaw_capture_myszy(false);
            e.obsluzone = true;
            return;
        }
        if (wcisniety_zamknij_) {
            wcisniety_zamknij_ = false;
            uniewaznij();
            if (prostokat_przycisku_zamknij().zawiera(lx, ly)) {
                zamknij_z_wynikiem(wynik_anuluj_);
            }
            e.obsluzone = true;
            return;
        }
        PowierzchniaPopup::obsluz_zwolnienie_myszy(e);
    }

    void obsluz_opuszczenie_myszy() override {
        if (najechany_zamknij_ || wcisniety_zamknij_) {
            najechany_zamknij_ = false;
            wcisniety_zamknij_ = false;
            uniewaznij();
        }
        PowierzchniaPopup::obsluz_opuszczenie_myszy();
    }

protected:
    static constexpr size_t MAKS_PRZYCISKOW = 8;
    static constexpr size_t MAKS_LINII = 16;

    struct LiniaTekstu {
        char tekst[128]{};
        int szerokosc{0};
    };

    struct PrzyciskSlot {
        OknoDialogowe* dialog{nullptr};
        WynikDialogu wynik{WynikDialogu::Brak};
        Przycisk* przycisk{nullptr};
        bool domyslny{false};
        bool anuluj{false};
        char etykieta[48]{};
    };

    static void rysuj_ikone_dialogu(KontekstRysowania& ctx, Prostokat r, TypDialogu typ, SkalaUI s) {
        int cx = r.x + r.szerokosc / 2;
        int cy = r.y + r.wysokosc / 2;
        int promien_kola = r.szerokosc / 2;

        switch (typ) {
            case TypDialogu::Informacja: {
                uint32_t bg = motyw_kolor(MOTYW_KOLOR_INFORMACJA);
                ctx.rysuj_zaokraglony_prostokat(r, bg, promien_kola);
                // Wektorowy glif informacyjny 'i'
                ctx.rysuj_zaokraglony_prostokat(Prostokat(cx - 1 * s, cy - 9 * s, 3 * s, 3 * s), 0x00FFFFFF, 1 * s);
                ctx.rysuj_prostokat(Prostokat(cx - 1 * s, cy - 3 * s, 3 * s, 9 * s), 0x00FFFFFF);
                ctx.rysuj_prostokat(Prostokat(cx - 3 * s, cy - 3 * s, 2 * s, 2 * s), 0x00FFFFFF);
                ctx.rysuj_prostokat(Prostokat(cx - 4 * s, cy + 6 * s, 9 * s, 2 * s), 0x00FFFFFF);
                break;
            }
            case TypDialogu::Sukces: {
                uint32_t bg = motyw_kolor(MOTYW_KOLOR_SUKCES);
                ctx.rysuj_zaokraglony_prostokat(r, bg, promien_kola);
                // Wektorowy znak powodzenia
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - 8 * s, cy + 1 * s, cx - 2 * s, cy + 7 * s, 3 * s, 0x00FFFFFF);
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - 2 * s, cy + 7 * s, cx + 8 * s, cy - 4 * s, 3 * s, 0x00FFFFFF);
                break;
            }
            case TypDialogu::Ostrzezenie: {
                uint32_t bg = motyw_kolor(MOTYW_KOLOR_OSTRZEZENIE);
                // Wektorowy trojkat ostrzegawczy
                int top_y = r.y + 2 * s;
                int bot_y = r.dolny() - 2 * s;
                int h = bot_y - top_y;
                if (h > 0) {
                    for (int dy = 0; dy < h; ++dy) {
                        int half_w = (dy * (r.szerokosc - 6 * s)) / (2 * h);
                        ctx.rysuj_prostokat(Prostokat(cx - half_w, top_y + dy, 2 * half_w + 1, 1), bg);
                    }
                }
                // Wykrzyknik
                ctx.rysuj_zaokraglony_prostokat(Prostokat(cx - 1 * s, cy - 6 * s, 3 * s, 8 * s), 0x00FFFFFF, 1 * s);
                ctx.rysuj_zaokraglony_prostokat(Prostokat(cx - 1 * s, cy + 5 * s, 3 * s, 3 * s), 0x00FFFFFF, 1 * s);
                break;
            }
            case TypDialogu::Blad: {
                uint32_t bg = motyw_kolor(MOTYW_KOLOR_BLAD);
                ctx.rysuj_zaokraglony_prostokat(r, bg, promien_kola);
                // Wektorowy krzyzyk 'X'
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - 6 * s, cy - 6 * s, cx + 6 * s, cy + 6 * s, 3 * s, 0x00FFFFFF);
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - 6 * s, cy + 6 * s, cx + 6 * s, cy - 6 * s, 3 * s, 0x00FFFFFF);
                break;
            }
            case TypDialogu::Pytanie: {
                uint32_t bg = motyw_kolor(MOTYW_KOLOR_INFORMACJA);
                ctx.rysuj_zaokraglony_prostokat(r, bg, promien_kola);
                // Wektorowy znak zapytania '?'
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - 5 * s, cy - 5 * s, cx - 2 * s, cy - 8 * s, 3 * s, 0x00FFFFFF);
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx - 2 * s, cy - 8 * s, cx + 2 * s, cy - 8 * s, 3 * s, 0x00FFFFFF);
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx + 2 * s, cy - 8 * s, cx + 5 * s, cy - 5 * s, 3 * s, 0x00FFFFFF);
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx + 5 * s, cy - 5 * s, cx + 1 * s, cy - 2 * s, 3 * s, 0x00FFFFFF);
                SzczegolyDialogu::rysuj_linie_wektorowa(ctx, cx + 1 * s, cy - 2 * s, cx, cy + 1 * s, 3 * s, 0x00FFFFFF);
                ctx.rysuj_zaokraglony_prostokat(Prostokat(cx - 1 * s, cy + 5 * s, 3 * s, 3 * s), 0x00FFFFFF, 1 * s);
                break;
            }
            default:
                break;
        }
    }

    void podziel_tekst_na_linie(int max_szerokosc) {
        liczba_linii_ = 0;
        max_szerokosc_linii_ = 0;
        if (!wiadomosc_[0]) return;
        SkalaUI s = skala_dpi_;
        if (max_szerokosc <= 40 * s) max_szerokosc = 320 * s;

        const char* p = wiadomosc_;
        char bufor_linii[128]{};
        size_t len = 0;

        while (*p && liczba_linii_ < MAKS_LINII) {
            if (*p == '\r') { ++p; continue; }
            if (*p == '\n') {
                bufor_linii[len] = '\0';
                SzczegolyDialogu::kopiuj_napis(linie_[liczba_linii_].tekst,
                                               sizeof(linie_[liczba_linii_].tekst), bufor_linii);
                linie_[liczba_linii_].szerokosc = CzcionkaUI::zmierz_tekst(bufor_linii, s);
                if (linie_[liczba_linii_].szerokosc > max_szerokosc_linii_)
                    max_szerokosc_linii_ = linie_[liczba_linii_].szerokosc;
                ++liczba_linii_;
                len = 0;
                bufor_linii[0] = '\0';
                ++p;
                continue;
            }

            const char* start_slowa = p;
            while (*p && *p != ' ' && *p != '\n' && *p != '\r') ++p;
            size_t len_slowa = p - start_slowa;

            char test_linia[128]{};
            size_t test_len = 0;
            for (size_t k = 0; k < len; ++k) test_linia[test_len++] = bufor_linii[k];
            if (len > 0 && test_len + 1 < sizeof(test_linia)) test_linia[test_len++] = ' ';
            for (size_t k = 0; k < len_slowa && test_len + 1 < sizeof(test_linia); ++k)
                test_linia[test_len++] = start_slowa[k];
            test_linia[test_len] = '\0';

            int szer_test = CzcionkaUI::zmierz_tekst(test_linia, s);
            if (szer_test <= max_szerokosc || len == 0) {
                for (size_t k = 0; k < test_len; ++k) bufor_linii[k] = test_linia[k];
                len = test_len;
                bufor_linii[len] = '\0';
            } else {
                bufor_linii[len] = '\0';
                SzczegolyDialogu::kopiuj_napis(linie_[liczba_linii_].tekst,
                                               sizeof(linie_[liczba_linii_].tekst), bufor_linii);
                linie_[liczba_linii_].szerokosc = CzcionkaUI::zmierz_tekst(bufor_linii, s);
                if (linie_[liczba_linii_].szerokosc > max_szerokosc_linii_)
                    max_szerokosc_linii_ = linie_[liczba_linii_].szerokosc;
                ++liczba_linii_;

                len = 0;
                for (size_t k = 0; k < len_slowa && len + 1 < sizeof(bufor_linii); ++k)
                    bufor_linii[len++] = start_slowa[k];
                bufor_linii[len] = '\0';
            }

            while (*p == ' ') ++p;
        }

        if (len > 0 && liczba_linii_ < MAKS_LINII) {
            bufor_linii[len] = '\0';
            SzczegolyDialogu::kopiuj_napis(linie_[liczba_linii_].tekst,
                                           sizeof(linie_[liczba_linii_].tekst), bufor_linii);
            linie_[liczba_linii_].szerokosc = CzcionkaUI::zmierz_tekst(bufor_linii, s);
            if (linie_[liczba_linii_].szerokosc > max_szerokosc_linii_)
                max_szerokosc_linii_ = linie_[liczba_linii_].szerokosc;
            ++liczba_linii_;
        }
    }

    virtual void oblicz_geometrie_dialogu(int& szer, int& wys) {
        SkalaUI s = skala_dpi_;
        int max_text_width = 460 * s;
        if (typ_ != TypDialogu::Zwykly) max_text_width -= (36 * s + 16 * s);

        podziel_tekst_na_linie(max_text_width);

        int total_btn_w = 0;
        for (size_t i = 0; i < liczba_przyciskow_; ++i) {
            int tw = CzcionkaUI::zmierz_tekst(sloty_[i].etykieta, s);
            int bw = tw + 28 * s;
            if (bw < 75 * s) bw = 75 * s;
            total_btn_w += bw;
            if (i + 1 < liczba_przyciskow_) total_btn_w += 10 * s;
        }

        int content_w = max_szerokosc_linii_;
        if (typ_ != TypDialogu::Zwykly) content_w += (36 * s + 16 * s);
        content_w += 48 * s;

        int w = content_w > (total_btn_w + 40 * s) ? content_w : (total_btn_w + 40 * s);
        if (w < 360 * s) w = 360 * s;
        if (w > 580 * s) w = 580 * s;

        int font_h = CzcionkaUI::pobierz_wysokosc(s);
        int line_gap = 4 * s;
        int text_h = static_cast<int>(liczba_linii_) * font_h +
                     (liczba_linii_ > 1 ? static_cast<int>(liczba_linii_ - 1) * line_gap : 0);
        int icon_h = (typ_ != TypDialogu::Zwykly) ? 36 * s : 0;
        int content_h = (text_h > icon_h) ? text_h : icon_h;
        if (content_h < 36 * s) content_h = 36 * s;

        int h = 32 * s + 20 * s + content_h + 16 * s + 48 * s;
        if (h < 170 * s) h = 170 * s;
        if (h > 420 * s) h = 420 * s;

        szer = w;
        wys = h;
    }

    virtual void zbuduj_interfejs(int w, int h) {
        wyczysc_kontrolki();
        SkalaUI s = skala_dpi_;

        uklad_ = new PanelDokowany();
        uklad_->ustaw_wypelnienie(Marginesy(0, 0, 0, 0));
        uklad_->ustaw_odstep(0);
        wyzeruj_tlo(*uklad_);
        dodaj_kontrolke(uklad_);

        panel_przyciskow_ = new Wiersz();
        panel_przyciskow_->ustaw_preferowany_rozmiar(Rozmiar(-1, 48));
        panel_przyciskow_->ustaw_wypelnienie(Marginesy(18, 8, 18, 10));
        panel_przyciskow_->ustaw_odstep(10);
        panel_przyciskow_->ustaw_wyrownanie_poziome(WyrownaniePoziome::Koniec);
        panel_przyciskow_->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
        wyzeruj_tlo(*panel_przyciskow_);
        uklad_->dodaj_dokowany(panel_przyciskow_, Dokowanie::Dol);

        Kontrolka* wypychacz = new Kontrolka();
        wypychacz->ustaw_wyrownanie_poziome(WyrownaniePoziome::Rozciagnij);
        wypychacz->ustaw_mozliwosc_fokusu(false);
        panel_przyciskow_->dodaj_kontrolke(wypychacz);

        panel_tresci_ = new Kontrolka();
        uklad_->dodaj_dokowany(panel_tresci_, Dokowanie::Wypelnij);

        int total_btn_w = 0;
        int btn_widths[MAKS_PRZYCISKOW]{};
        for (size_t i = 0; i < liczba_przyciskow_; ++i) {
            int tw = CzcionkaUI::zmierz_tekst(sloty_[i].etykieta, s);
            int bw = tw + 28 * s;
            if (bw < 75 * s) bw = 75 * s;
            btn_widths[i] = bw;
            total_btn_w += bw;
            if (i + 1 < liczba_przyciskow_) total_btn_w += 10 * s;
        }

        int btn_h = 30 * s;

        for (size_t i = 0; i < liczba_przyciskow_; ++i) {
            Przycisk* btn = new Przycisk(sloty_[i].etykieta);
            btn->ustaw_preferowany_rozmiar(Rozmiar(btn_widths[i] / s, btn_h / s));
            sloty_[i].przycisk = btn;

            if (sloty_[i].domyslny) {
                btn->ustaw_kolor_ramki_stanu(StanWizualny::Normalny, motyw_kolor(MOTYW_KOLOR_AKCENT));
                btn->ustaw_grubosc_ramki(2);
            }

            btn->przy_kliknieciu([](Przycisk*, void* d) {
                PrzyciskSlot* slot = static_cast<PrzyciskSlot*>(d);
                slot->dialog->zamknij_z_wynikiem(slot->wynik);
            }, &sloty_[i]);

            btn->ustaw_wyrownanie_poziome(WyrownaniePoziome::Poczatek);
            btn->ustaw_wyrownanie_pionowe(WyrownaniePionowe::Srodek);
            panel_przyciskow_->dodaj_kontrolke(btn);
        }
        (void)total_btn_w;
        uklad_->rozmiesc(Prostokat(0, 32 * s, w, h > 32 * s ? h - 32 * s : 0));
        aktualizuj_obszary_ukladu();
    }

    static void wyzeruj_tlo(Kontrolka& k) {
        for (int i = 0; i <= static_cast<int>(StanWizualny::TylkoDoOdczytu); ++i) {
            StylStanu& st = k.styl_stanu(static_cast<StanWizualny>(i));
            st.tlo = 0; st.ramka = 0; st.grubosc_ramki = 0;
        }
    }

    void aktualizuj_obszary_ukladu() {
        if (!uklad_ || !panel_tresci_ || !panel_przyciskow_) return;
        Prostokat ur = uklad_->obszar();
        Prostokat tr = panel_tresci_->obszar();
        Prostokat pr = panel_przyciskow_->obszar();
        obszar_tresci_ = Prostokat(ur.x + tr.x, ur.y + tr.y, tr.szerokosc, tr.wysokosc);
        obszar_przyciskow_ = Prostokat(ur.x + pr.x, ur.y + pr.y, pr.szerokosc, pr.wysokosc);
    }

    void dodaj_slot_przycisku(const char* etykieta, WynikDialogu w, bool domyslny = false, bool anuluj = false) {
        if (liczba_przyciskow_ >= MAKS_PRZYCISKOW) return;
        size_t idx = liczba_przyciskow_++;
        sloty_[idx].dialog = this;
        sloty_[idx].wynik = w;
        sloty_[idx].domyslny = domyslny;
        sloty_[idx].anuluj = anuluj;
        SzczegolyDialogu::kopiuj_napis(sloty_[idx].etykieta, sizeof(sloty_[idx].etykieta), etykieta);
        if (domyslny) {
            wynik_domyslny_ = w;
            indeks_fokusu_przycisku_ = static_cast<int>(idx);
        }
        if (anuluj) {
            wynik_anuluj_ = w;
        }
    }

    void inicjalizuj_przyciski(PrzyciskiDialogu p) {
        przyciski_typ_ = p;
        liczba_przyciskow_ = 0;
        indeks_fokusu_przycisku_ = 0;

        switch (p) {
            case PrzyciskiDialogu::TylkoOk:
                dodaj_slot_przycisku("OK", WynikDialogu::OK, true, true);
                break;
            case PrzyciskiDialogu::OkAnuluj:
                dodaj_slot_przycisku("OK", WynikDialogu::OK, true, false);
                dodaj_slot_przycisku("Anuluj", WynikDialogu::Anuluj, false, true);
                break;
            case PrzyciskiDialogu::TakNie:
                dodaj_slot_przycisku("Tak", WynikDialogu::Tak, true, false);
                dodaj_slot_przycisku("Nie", WynikDialogu::Nie, false, true);
                break;
            case PrzyciskiDialogu::TakNieAnuluj:
                dodaj_slot_przycisku("Tak", WynikDialogu::Tak, true, false);
                dodaj_slot_przycisku("Nie", WynikDialogu::Nie, false, false);
                dodaj_slot_przycisku("Anuluj", WynikDialogu::Anuluj, false, true);
                break;
            case PrzyciskiDialogu::PonowAnuluj:
                dodaj_slot_przycisku("Ponów", WynikDialogu::Ponow, true, false);
                dodaj_slot_przycisku("Anuluj", WynikDialogu::Anuluj, false, true);
                break;
            case PrzyciskiDialogu::PrzerwijPonowIgnoruj:
                dodaj_slot_przycisku("Przerwij", WynikDialogu::Przerwij, false, false);
                dodaj_slot_przycisku("Ponów", WynikDialogu::Ponow, true, false);
                dodaj_slot_przycisku("Ignoruj", WynikDialogu::Ignoruj, false, true);
                break;
            case PrzyciskiDialogu::Wlasne:
            default:
                break;
        }

        int w = 0, h = 0;
        oblicz_geometrie_dialogu(w, h);
        zbuduj_interfejs(w, h);
    }

    PrzyciskiDialogu przyciski_typ_{PrzyciskiDialogu::OkAnuluj};
    TypDialogu typ_{TypDialogu::Informacja};
    char wiadomosc_[1024]{};
    WynikDialogu wynik_{WynikDialogu::Brak};
    WynikDialogu wynik_domyslny_{WynikDialogu::OK};
    WynikDialogu wynik_anuluj_{WynikDialogu::Anuluj};
    FunkcjaWyniku cb_wynik_{nullptr};
    void* dane_wyniku_{nullptr};

    Kontrolka* wlasciciel_{nullptr};
    Kontrolka* fokus_poprzedni_{nullptr};
    int indeks_fokusu_przycisku_{0};

    PrzyciskSlot sloty_[MAKS_PRZYCISKOW]{};
    size_t liczba_przyciskow_{0};

    LiniaTekstu linie_[MAKS_LINII]{};
    size_t liczba_linii_{0};
    int max_szerokosc_linii_{0};
    PanelDokowany* uklad_{nullptr};
    Kontrolka* panel_tresci_{nullptr};
    Wiersz* panel_przyciskow_{nullptr};
    Prostokat obszar_tresci_{};
    Prostokat obszar_przyciskow_{};

    bool przeciaganie_{false};
    int offset_ciagniecia_x_{0};
    int offset_ciagniecia_y_{0};
    bool najechany_zamknij_{false};
    bool wcisniety_zamknij_{false};
};

class Komunikat {
public:
    static OknoDialogowe* informacja(ZarzadcaPopupow& z, const char* tytul, const char* wiadomosc,
                                     OknoDialogowe::FunkcjaWyniku cb = nullptr, void* dane = nullptr,
                                     Kontrolka* owner = nullptr) {
        OknoDialogowe* dlg = new OknoDialogowe(tytul, wiadomosc, PrzyciskiDialogu::TylkoOk, TypDialogu::Informacja);
        dlg->przy_wyniku(cb, dane);
        dlg->pokaz(z, owner);
        return dlg;
    }

    static OknoDialogowe* ostrzezenie(ZarzadcaPopupow& z, const char* tytul, const char* wiadomosc,
                                      OknoDialogowe::FunkcjaWyniku cb = nullptr, void* dane = nullptr,
                                      Kontrolka* owner = nullptr) {
        OknoDialogowe* dlg = new OknoDialogowe(tytul, wiadomosc, PrzyciskiDialogu::TylkoOk, TypDialogu::Ostrzezenie);
        dlg->przy_wyniku(cb, dane);
        dlg->pokaz(z, owner);
        return dlg;
    }

    static OknoDialogowe* blad(ZarzadcaPopupow& z, const char* tytul, const char* wiadomosc,
                               OknoDialogowe::FunkcjaWyniku cb = nullptr, void* dane = nullptr,
                               Kontrolka* owner = nullptr) {
        OknoDialogowe* dlg = new OknoDialogowe(tytul, wiadomosc, PrzyciskiDialogu::TylkoOk, TypDialogu::Blad);
        dlg->przy_wyniku(cb, dane);
        dlg->pokaz(z, owner);
        return dlg;
    }

    static OknoDialogowe* pytanie(ZarzadcaPopupow& z, const char* tytul, const char* wiadomosc,
                                  PrzyciskiDialogu przyciski = PrzyciskiDialogu::TakNie,
                                  OknoDialogowe::FunkcjaWyniku cb = nullptr, void* dane = nullptr,
                                  Kontrolka* owner = nullptr) {
        OknoDialogowe* dlg = new OknoDialogowe(tytul, wiadomosc, przyciski, TypDialogu::Pytanie);
        dlg->przy_wyniku(cb, dane);
        dlg->pokaz(z, owner);
        return dlg;
    }

    static OknoDialogowe* sukces(ZarzadcaPopupow& z, const char* tytul, const char* wiadomosc,
                                 OknoDialogowe::FunkcjaWyniku cb = nullptr, void* dane = nullptr,
                                 Kontrolka* owner = nullptr) {
        OknoDialogowe* dlg = new OknoDialogowe(tytul, wiadomosc, PrzyciskiDialogu::TylkoOk, TypDialogu::Sukces);
        dlg->przy_wyniku(cb, dane);
        dlg->pokaz(z, owner);
        return dlg;
    }

    static OknoDialogowe* pokaz(ZarzadcaPopupow& z, TypDialogu typ, const char* tytul, const char* wiadomosc,
                                PrzyciskiDialogu przyciski = PrzyciskiDialogu::TylkoOk,
                                OknoDialogowe::FunkcjaWyniku cb = nullptr, void* dane = nullptr,
                                WynikDialogu domyslny = WynikDialogu::Brak,
                                Kontrolka* owner = nullptr) {
        OknoDialogowe* dlg = new OknoDialogowe(tytul, wiadomosc, przyciski, typ);
        if (domyslny != WynikDialogu::Brak) {
            dlg->ustaw_przycisk_domyslny(domyslny);
        }
        dlg->przy_wyniku(cb, dane);
        dlg->pokaz(z, owner);
        return dlg;
    }
};

} // namespace BursztynUI
