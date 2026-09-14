#pragma once

#include "kontrolka.h"
#include "../obrazy/skalowanie.h"

namespace BursztynUI {

enum class TrybObrazu : uint8_t {
    Naturalny,
    Wycentrowany,
    Dopasuj,
    Rozciagnij
};

class Obraz : public Kontrolka {
public:
    Obraz() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = 0; // Przezroczyste domyślnie
        styl_.normalny.ramka = 0;
        styl_.normalny.grubosc_ramki = 0;
        preferowany_rozmiar_ = Rozmiar(120, 120);
    }

    ~Obraz() override {
        wyczysc_obraz();
    }

    TrybObrazu tryb() const { return tryb_; }
    void ustaw_tryb(TrybObrazu t) {
        if (tryb_ != t) {
            tryb_ = t;
            uniewaznij();
        }
    }

    int szerokosc_oryginalna() const { return img_w_; }
    int wysokosc_oryginalna() const { return img_h_; }
    bool czy_ma_obraz() const { return piksele_ != nullptr && img_w_ > 0 && img_h_ > 0; }
    const uint32_t* piksele() const { return piksele_; }

    void ustaw_obraz(const uint32_t* zrodlo, int w, int h, bool kopiuj = true) {
        wyczysc_obraz();
        if (!zrodlo || w <= 0 || h <= 0 || w > 4096 || h > 4096) return;

        img_w_ = w;
        img_h_ = h;
        if (kopiuj) {
            size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
            uint32_t* buf = new uint32_t[count];
            if (buf) {
                for (size_t i = 0; i < count; ++i) buf[i] = zrodlo[i];
                piksele_ = buf;
                wlasny_bufor_ = true;
            }
        } else {
            piksele_ = zrodlo;
            wlasny_bufor_ = false;
        }

        preferowany_rozmiar_ = Rozmiar(img_w_, img_h_);
        uniewaznij();
    }

    bool wczytaj_z_bufora_bmp(const uint8_t* dane, size_t rozmiar) {
        wyczysc_obraz();
        if (!dane || rozmiar < 54) return false;
        if (dane[0] != 'B' || dane[1] != 'M') return false;

        uint32_t offset_danych = *reinterpret_cast<const uint32_t*>(dane + 10);
        uint32_t szer = *reinterpret_cast<const uint32_t*>(dane + 18);
        int32_t wys_signed = *reinterpret_cast<const int32_t*>(dane + 22);
        uint16_t bpp = *reinterpret_cast<const uint16_t*>(dane + 28);
        uint32_t kompresja = *reinterpret_cast<const uint32_t*>(dane + 30);

        if (kompresja != 0) return false; // Tylko nieskompresowane BMP
        if (bpp != 24 && bpp != 32) return false;

        bool odwrocone_y = (wys_signed > 0);
        uint32_t wys = static_cast<uint32_t>(wys_signed < 0 ? -wys_signed : wys_signed);
        if (szer == 0 || wys == 0 || szer > 4096 || wys > 4096) return false;

        size_t row_bytes = (bpp == 24) ? ((szer * 3 + 3) & ~3U) : (szer * 4);
        if (offset_danych + row_bytes * wys > rozmiar) return false;

        size_t count = static_cast<size_t>(szer) * static_cast<size_t>(wys);
        uint32_t* buf = new uint32_t[count];
        if (!buf) return false;

        for (uint32_t y = 0; y < wys; ++y) {
            uint32_t src_y = odwrocone_y ? (wys - 1 - y) : y;
            const uint8_t* src_row = dane + offset_danych + (src_y * row_bytes);
            uint32_t* dst_row = buf + (y * szer);

            if (bpp == 24) {
                for (uint32_t x = 0; x < szer; ++x) {
                    uint8_t b = src_row[x * 3 + 0];
                    uint8_t g = src_row[x * 3 + 1];
                    uint8_t r = src_row[x * 3 + 2];
                    dst_row[x] = 0xFF000000U | (static_cast<uint32_t>(r) << 16) |
                                 (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
                }
            } else { // 32 bpp
                const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src_row);
                for (uint32_t x = 0; x < szer; ++x) {
                    dst_row[x] = src32[x];
                }
            }
        }

        piksele_ = buf;
        img_w_ = static_cast<int>(szer);
        img_h_ = static_cast<int>(wys);
        wlasny_bufor_ = true;

        preferowany_rozmiar_ = Rozmiar(img_w_, img_h_);
        uniewaznij();
        return true;
    }

    void wyczysc_obraz() {
        if (wlasny_bufor_ && piksele_) {
            delete[] piksele_;
        }
        piksele_ = nullptr;
        img_w_ = 0;
        img_h_ = 0;
        wlasny_bufor_ = false;
        uniewaznij();
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        if (img_w_ > 0 && img_h_ > 0) {
            return Rozmiar(img_w_ * skala_dpi_, img_h_ * skala_dpi_);
        }
        return Rozmiar(64 * skala_dpi_, 64 * skala_dpi_);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        if (!piksele_ || img_w_ <= 0 || img_h_ <= 0) {
            // Placeholder ikony obrazu
            int sz = (obszar_.szerokosc < obszar_.wysokosc ? obszar_.szerokosc : obszar_.wysokosc) / 2;
            if (sz > 8) {
                int cx = (obszar_.szerokosc - sz) / 2;
                int cy = (obszar_.wysokosc - sz) / 2;
                ctx.rysuj_zaokraglona_ramke(Prostokat(cx, cy, sz, sz), motyw_kolor(MOTYW_KOLOR_RAMKA), 4, 1);
                ctx.rysuj_tekst_obciety(Punkt(cx, cy + sz / 2 - 8),
                                        "Brak obrazu", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI),
                                        sz, 1);
            }
            return;
        }

        int dst_x = 0;
        int dst_y = 0;
        int dst_w = obszar_.szerokosc;
        int dst_h = obszar_.wysokosc;

        if (dst_w <= 0 || dst_h <= 0) return;

        switch (tryb_) {
            case TrybObrazu::Naturalny:
                dst_w = img_w_ * skala_dpi_;
                dst_h = img_h_ * skala_dpi_;
                break;
            case TrybObrazu::Wycentrowany:
                dst_w = img_w_ * skala_dpi_;
                dst_h = img_h_ * skala_dpi_;
                dst_x = (obszar_.szerokosc - dst_w) / 2;
                dst_y = (obszar_.wysokosc - dst_h) / 2;
                break;
            case TrybObrazu::Dopasuj: {
                GeometriaObrazu g{};
                if (!geometria_obrazu(img_w_, img_h_, obszar_.szerokosc,
                        obszar_.wysokosc, TrybSkalowaniaObrazu::Dopasuj, &g)) return;
                dst_w = g.dst_w; dst_h = g.dst_h;
                dst_x = g.dst_x; dst_y = g.dst_y;
                break;
            }
            case TrybObrazu::Rozciagnij:
            default:
                dst_x = 0;
                dst_y = 0;
                dst_w = obszar_.szerokosc;
                dst_h = obszar_.wysokosc;
                break;
        }

        // Rysowanie z próbkowaniem nearest-neighbor (bezpieczne, zero alokacji)
        Prostokat clip = ctx.pobierz_przyciecie();
        Punkt offset = ctx.pobierz_przesuniecie();

        int screen_dst_x = offset.x + dst_x;
        int screen_dst_y = offset.y + dst_y;
        int screen_dst_r = screen_dst_x + dst_w;
        int screen_dst_b = screen_dst_y + dst_h;

        int render_x0 = screen_dst_x > clip.x ? screen_dst_x : clip.x;
        int render_y0 = screen_dst_y > clip.y ? screen_dst_y : clip.y;
        int render_x1 = screen_dst_r < clip.prawy() ? screen_dst_r : clip.prawy();
        int render_y1 = screen_dst_b < clip.dolny() ? screen_dst_b : clip.dolny();

        if (render_x0 >= render_x1 || render_y0 >= render_y1 || dst_w <= 0 || dst_h <= 0) return;

        // Zwarty bufor jednego wiersza na stosie: bez alokacji w paint.
        // BWS57 dostaje wylacznie fragment przeciety przez kontekst.
        uint32_t wiersz[2048];
        int render_w = render_x1 - render_x0;
        if (render_w > 2048) render_w = 2048;
        for (int py = render_y0; py < render_y1; ++py) {
            int src_y = probka_obrazu(py - screen_dst_y, img_h_, dst_h);
            if (src_y < 0) src_y = 0;
            if (src_y >= img_h_) src_y = img_h_ - 1;

            const uint32_t* src_line = piksele_ + (src_y * img_w_);

            for (int dx = 0; dx < render_w; ++dx) {
                int px = render_x0 + dx;
                int src_x = probka_obrazu(px - screen_dst_x, img_w_, dst_w);
                if (src_x < 0) src_x = 0;
                if (src_x >= img_w_) src_x = img_w_ - 1;

                uint32_t c = src_line[src_x];
                wiersz[dx] = (c & 0xFF000000U) != 0
                    ? ((c & 0x00FFFFFFU) ? (c & 0x00FFFFFFU) : 0x01000000U)
                    : SKLADACZ_PIKSEL_PRZEZROCZYSTY;
            }
            ctx.rysuj_wiersz_ekranu(render_x0, py, render_w, wiersz);
        }
    }

private:
    const uint32_t* piksele_{nullptr};
    int img_w_{0};
    int img_h_{0};
    bool wlasny_bufor_{false};
    TrybObrazu tryb_{TrybObrazu::Dopasuj};
};

} // namespace BursztynUI
