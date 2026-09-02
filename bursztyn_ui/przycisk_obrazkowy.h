#pragma once

#include "przycisk.h"
#include "widok_ikon.h"

namespace BursztynUI {

enum class TrybPrzyciskuObrazkowego : uint8_t {
    IkonaITekst,
    TylkoIkona,
    TylkoTekst
};

enum class PozycjaIkony : uint8_t {
    Lewo,
    Gora,
    Prawo
};

class PrzyciskObrazkowy : public Przycisk {
public:
    explicit PrzyciskObrazkowy(const char* etykieta = "Przycisk",
                               TypIkonyWidoku typ_ikony = TypIkonyWidoku::Plik)
        : Przycisk(etykieta), typ_ikony_(typ_ikony) {
        preferowany_rozmiar_ = Rozmiar(120, 36);
    }

    TrybPrzyciskuObrazkowego tryb() const { return tryb_; }
    void ustaw_tryb(TrybPrzyciskuObrazkowego t) {
        if (tryb_ != t) {
            tryb_ = t;
            uniewaznij();
        }
    }

    PozycjaIkony pozycja_ikony() const { return pozycja_ikony_; }
    void ustaw_pozycja_ikony(PozycjaIkony p) {
        if (pozycja_ikony_ != p) {
            pozycja_ikony_ = p;
            uniewaznij();
        }
    }

    TypIkonyWidoku typ_ikony() const { return typ_ikony_; }
    void ustaw_typ_ikony(TypIkonyWidoku t) {
        if (typ_ikony_ != t) {
            typ_ikony_ = t;
            uniewaznij();
        }
    }

    void ustaw_piksele_ikony(const uint32_t* piksele, int w, int h) {
        piksele_ikony_ = piksele;
        ikona_w_ = w;
        ikona_h_ = h;
        uniewaznij();
    }

    Rozmiar zmierz(Rozmiar dostepny_rozmiar) override {
        (void)dostepny_rozmiar;
        int skala = skala_dpi_;
        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala);
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala);

        int ic_sz = 20 * skala;
        int naturalna_w = 0;
        int naturalna_h = 0;

        if (tryb_ == TrybPrzyciskuObrazkowego::TylkoIkona) {
            naturalna_w = ic_sz + 16 * skala;
            naturalna_h = ic_sz + 12 * skala;
        } else if (tryb_ == TrybPrzyciskuObrazkowego::TylkoTekst) {
            naturalna_w = txt_w + 24 * skala;
            naturalna_h = txt_h + 16 * skala;
        } else { // IkonaITekst
            if (pozycja_ikony_ == PozycjaIkony::Gora) {
                naturalna_w = (txt_w > ic_sz ? txt_w : ic_sz) + 20 * skala;
                naturalna_h = ic_sz + txt_h + 16 * skala;
            } else {
                naturalna_w = ic_sz + 8 * skala + txt_w + 20 * skala;
                naturalna_h = (txt_h > ic_sz ? txt_h : ic_sz) + 12 * skala;
            }
        }

        if (naturalna_h < 28 * skala) naturalna_h = 28 * skala;
        if (naturalna_w < 32 * skala) naturalna_w = 32 * skala;

        Rozmiar zadany(naturalna_w, naturalna_h);
        if (preferowany_rozmiar_.szerokosc >= 0) zadany.szerokosc = preferowany_rozmiar_.szerokosc * skala;
        if (preferowany_rozmiar_.wysokosc >= 0) zadany.wysokosc = preferowany_rozmiar_.wysokosc * skala;

        zadany.szerokosc = przytnij(zadany.szerokosc, minimalny_rozmiar_.szerokosc * skala, maksymalny_rozmiar_.szerokosc * skala);
        zadany.wysokosc = przytnij(zadany.wysokosc, minimalny_rozmiar_.wysokosc * skala, maksymalny_rozmiar_.wysokosc * skala);
        return zadany;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        int skala = skala_dpi_;
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        int ic_sz = 18 * skala;
        int txt_w = CzcionkaUI::zmierz_tekst(tekst_, skala);
        int txt_h = CzcionkaUI::pobierz_wysokosc(skala);

        if (tryb_ == TrybPrzyciskuObrazkowego::TylkoIkona) {
            int ix = (obszar_.szerokosc - ic_sz) / 2;
            int iy = (obszar_.wysokosc - ic_sz) / 2;
            rysuj_ikone(ctx, ix, iy, ic_sz, ic_sz);
        } else if (tryb_ == TrybPrzyciskuObrazkowego::TylkoTekst) {
            ctx.rysuj_tekst_wysrodkowany(Prostokat(0, 0, obszar_.szerokosc, obszar_.wysokosc),
                                         tekst_, s.tekst, skala);
        } else { // IkonaITekst
            if (pozycja_ikony_ == PozycjaIkony::Gora) {
                int total_h = ic_sz + 4 * skala + txt_h;
                int iy = (obszar_.wysokosc - total_h) / 2;
                int ix = (obszar_.szerokosc - ic_sz) / 2;
                rysuj_ikone(ctx, ix, iy, ic_sz, ic_sz);

                int ty = iy + ic_sz + 2 * skala;
                ctx.rysuj_tekst_wysrodkowany(Prostokat(0, ty, obszar_.szerokosc, txt_h),
                                             tekst_, s.tekst, skala);
            } else if (pozycja_ikony_ == PozycjaIkony::Prawo) {
                int total_w = txt_w + 6 * skala + ic_sz;
                int start_x = (obszar_.szerokosc - total_w) / 2;
                if (start_x < 4) start_x = 4;

                ctx.rysuj_tekst_obciety(
                    Punkt(start_x, (obszar_.wysokosc - txt_h) / 2),
                    tekst_, s.tekst, txt_w, skala);

                int ix = start_x + txt_w + 6 * skala;
                int iy = (obszar_.wysokosc - ic_sz) / 2;
                rysuj_ikone(ctx, ix, iy, ic_sz, ic_sz);
            } else { // Lewo
                int total_w = ic_sz + 6 * skala + txt_w;
                int start_x = (obszar_.szerokosc - total_w) / 2;
                if (start_x < 4) start_x = 4;

                int ix = start_x;
                int iy = (obszar_.wysokosc - ic_sz) / 2;
                rysuj_ikone(ctx, ix, iy, ic_sz, ic_sz);

                int tx = start_x + ic_sz + 6 * skala;
                ctx.rysuj_tekst_obciety(
                    Punkt(tx, (obszar_.wysokosc - txt_h) / 2), tekst_,
                    s.tekst, obszar_.szerokosc - tx - 4, skala);
            }
        }
    }

private:
    TrybPrzyciskuObrazkowego tryb_{TrybPrzyciskuObrazkowego::IkonaITekst};
    PozycjaIkony pozycja_ikony_{PozycjaIkony::Lewo};
    TypIkonyWidoku typ_ikony_{TypIkonyWidoku::Plik};

    const uint32_t* piksele_ikony_{nullptr};
    int ikona_w_{0};
    int ikona_h_{0};

    void rysuj_ikone(KontekstRysowania& ctx, int x, int y, int w, int h) {
        if (piksele_ikony_ && ikona_w_ > 0 && ikona_h_ > 0) {
            // Rysowanie z bufora pikseli bez alokacji w paint.
            Punkt off = ctx.pobierz_przesuniecie();
            Prostokat clip = ctx.pobierz_przyciecie();
            int sx0 = off.x + x;
            int sy0 = off.y + y;
            int rx0 = sx0 > clip.x ? sx0 : clip.x;
            int ry0 = sy0 > clip.y ? sy0 : clip.y;
            int rx1 = sx0 + w < clip.prawy() ? sx0 + w : clip.prawy();
            int ry1 = sy0 + h < clip.dolny() ? sy0 + h : clip.dolny();
            uint32_t wiersz[256];
            int rw = rx1 - rx0;
            if (rw > 256) rw = 256;
            for (int py = ry0; py < ry1 && rw > 0; ++py) {
                int sy = ((py - sy0) * ikona_h_) / h;
                for (int dx = 0; dx < rw; ++dx) {
                    int px = rx0 + dx;
                    int sx = ((px - sx0) * ikona_w_) / w;
                    uint32_t c = piksele_ikony_[sy * ikona_w_ + sx];
                    wiersz[dx] = (c & 0xFF000000U) != 0
                        ? (c & 0x00FFFFFFU)
                        : SKLADACZ_PIKSEL_PRZEZROCZYSTY;
                }
                (void)gui_blit(rx0, py, rw, 1, wiersz);
            }
        } else {
            // Rysowanie wektorowej ikony systemowej
            switch (typ_ikony_) {
                case TypIkonyWidoku::Folder:
                    ctx.rysuj_prostokat(Prostokat(x, y + 2, w, h - 2), 0x00FFBF00);
                    ctx.rysuj_prostokat(Prostokat(x + 1, y + 5, w - 2, h - 6), 0x00D59600);
                    ctx.rysuj_prostokat(Prostokat(x + 1, y + 1, w / 2, 3), 0x00FFBF00);
                    break;
                case TypIkonyWidoku::Aplikacja:
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(x, y, w, h), 0x00301500, 3);
                    ctx.rysuj_zaokraglona_ramke(Prostokat(x, y, w, h), 0x00E58A00, 3, 1);
                    ctx.rysuj_prostokat(Prostokat(x + 2, y + 2, w - 4, 3), 0x00E58A00);
                    break;
                case TypIkonyWidoku::Obraz:
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(x, y, w, h), 0x002080C0, 2);
                    ctx.rysuj_prostokat(Prostokat(x + 3, y + h - 6, 4, 4), 0x0020A040);
                    ctx.rysuj_prostokat(Prostokat(x + 7, y + h - 8, 6, 6), 0x0030C060);
                    ctx.rysuj_prostokat(Prostokat(x + w - 5, y + 3, 2, 2), 0x00FFD700);
                    break;
                case TypIkonyWidoku::Dysk:
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(x, y + 3, w, h - 6), 0x00606060, 2);
                    ctx.rysuj_prostokat(Prostokat(x + w - 4, y + h - 5, 2, 1), 0x0000FF00);
                    break;
                case TypIkonyWidoku::Gwiazda:
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(x + 2, y + 2, w - 4, h - 4), 0x00FFD700, 2);
                    ctx.rysuj_prostokat(Prostokat(x + w / 2 - 1, y, 2, h), 0x00FFD700);
                    ctx.rysuj_prostokat(Prostokat(x, y + h / 2 - 1, w, 2), 0x00FFD700);
                    break;
                case TypIkonyWidoku::Ustawienia:
                    ctx.rysuj_zaokraglony_prostokat(Prostokat(x + 2, y + 2, w - 4, h - 4), 0x00808080, 3);
                    ctx.rysuj_zaokraglona_ramke(Prostokat(x + 2, y + 2, w - 4, h - 4), 0x00C0C0C0, 3, 1);
                    ctx.rysuj_prostokat(Prostokat(x + w / 2 - 1, y + h / 2 - 1, 2, 2), 0x002B1D0E);
                    break;
                case TypIkonyWidoku::Dokument:
                case TypIkonyWidoku::Plik:
                default:
                    ctx.rysuj_prostokat(Prostokat(x + 1, y, w - 2, h), 0x00E0E0E0);
                    ctx.rysuj_ramke(Prostokat(x + 1, y, w - 2, h), 0x00707070);
                    ctx.rysuj_prostokat(Prostokat(x + 3, y + 4, w - 6, 1), 0x00505050);
                    ctx.rysuj_prostokat(Prostokat(x + 3, y + 8, w - 6, 1), 0x00505050);
                    ctx.rysuj_prostokat(Prostokat(x + 3, y + 12, w - 8, 1), 0x00505050);
                    break;
            }
        }
    }
};

} // namespace BursztynUI
