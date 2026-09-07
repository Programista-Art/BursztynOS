#pragma once

#include "kontrolka.h"
#include "czcionka.h"

namespace BursztynUI {

class PasekSciezki : public Kontrolka {
public:
    static constexpr size_t MAKS_SEGMENTOW = 32;

    struct Segment {
        char nazwa[64]{};
        uint64_t znacznik{0};
        int x{0};
        int szerokosc{0};
    };

    using FunkcjaSegmentu = void (*)(PasekSciezki* nadawca, int indeks, const char* nazwa, void* dane_uzytkownika);

    PasekSciezki() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = Kolory::WejscieTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        preferowany_rozmiar_ = Rozmiar(260, 28);
        wypelnienie_ = Marginesy(4, 2, 4, 2);
    }

    ~PasekSciezki() override = default;

    bool dodaj_segment(const char* nazwa, uint64_t znacznik = 0) {
        if (liczba_segmentow_ >= MAKS_SEGMENTOW) return false;
        Segment& s = segmenty_[liczba_segmentow_++];
        if (!nazwa) nazwa = "";
        size_t i = 0;
        for (; i < sizeof(s.nazwa) - 1 && nazwa[i] != '\0'; ++i) {
            s.nazwa[i] = nazwa[i];
        }
        s.nazwa[i] = '\0';
        s.znacznik = znacznik;
        uniewaznij();
        return true;
    }

    bool ustaw_sciezke(const char* pelna_sciezka, char separator = '/') {
        wyczysc();
        if (!pelna_sciezka || pelna_sciezka[0] == '\0') return true;

        const char* p = pelna_sciezka;
        char buf[64];
        size_t bi = 0;

        while (*p != '\0') {
            if (*p == separator) {
                if (bi > 0) {
                    buf[bi] = '\0';
                    dodaj_segment(buf);
                    bi = 0;
                }
            } else {
                if (bi < sizeof(buf) - 1) {
                    buf[bi++] = *p;
                }
            }
            ++p;
        }
        if (bi > 0) {
            buf[bi] = '\0';
            dodaj_segment(buf);
        }
        return true;
    }

    void wyczysc() {
        liczba_segmentow_ = 0;
        najechany_segment_ = -1;
        najechany_skrot_ = false;
        uniewaznij();
    }

    size_t liczba_segmentow() const { return liczba_segmentow_; }

    const char* segment(size_t indeks) const {
        return indeks < liczba_segmentow_ ? segmenty_[indeks].nazwa : "";
    }

    uint64_t znacznik_segmentu(size_t indeks) const {
        return indeks < liczba_segmentow_ ? segmenty_[indeks].znacznik : 0;
    }

    Prostokat obszar_segmentu(size_t indeks) const {
        if (indeks >= liczba_segmentow_ || segmenty_[indeks].szerokosc <= 0)
            return Prostokat();
        return Prostokat(segmenty_[indeks].x, 2 * skala_dpi_,
                         segmenty_[indeks].szerokosc,
                         obszar_.wysokosc - 4 * skala_dpi_);
    }

    Prostokat obszar_skrotu() const { return skrot_rect_; }

    const char* separator_tekst() const { return separator_tekst_; }
    void ustaw_separator_tekst(const char* sep) {
        if (!sep) sep = ">";
        size_t i = 0;
        for (; i < sizeof(separator_tekst_) - 1 && sep[i] != '\0'; ++i) {
            separator_tekst_[i] = sep[i];
        }
        separator_tekst_[i] = '\0';
        uniewaznij();
    }

    void przy_wyborze_segmentu(FunkcjaSegmentu cb, void* dane = nullptr) {
        cb_wybor_ = cb;
        dane_wyboru_ = dane;
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        SkalaUI skala = skala_dpi_;
        int sep_w = CzcionkaUI::zmierz_tekst(separator_tekst_, skala) + 8 * skala;
        int total_w = wypelnienie_.poziomo() * skala;

        for (size_t i = 0; i < liczba_segmentow_; ++i) {
            int tw = CzcionkaUI::zmierz_tekst(segmenty_[i].nazwa, skala) + 8 * skala;
            total_w += tw;
            if (i + 1 < liczba_segmentow_) total_w += sep_w;
        }

        int h = 26 * skala;
        return Rozmiar(total_w, h);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        if (liczba_segmentow_ == 0) return;

        SkalaUI skala = skala_dpi_;
        int pad_l = wypelnienie_.lewy * skala;
        int sep_tw = CzcionkaUI::zmierz_tekst(separator_tekst_, skala);
        int sep_spacing = 6 * skala;
        int sep_total = sep_tw + sep_spacing * 2;

        int dostepne_w = obszar_.szerokosc - wypelnienie_.poziomo() * skala;
        if (dostepne_w <= 0) return;

        int segment_widths[MAKS_SEGMENTOW];
        for (size_t i = 0; i < liczba_segmentow_; ++i) {
            segmenty_[i].x = 0;
            segmenty_[i].szerokosc = 0;
        }
        int total_needed = 0;
        for (size_t i = 0; i < liczba_segmentow_; ++i) {
            segment_widths[i] = CzcionkaUI::zmierz_tekst(segmenty_[i].nazwa, skala) + 8 * skala;
            total_needed += segment_widths[i];
            if (i + 1 < liczba_segmentow_) total_needed += sep_total;
        }

        size_t start_seg = 0;
        bool ma_skrot = false;
        int skrot_w = CzcionkaUI::zmierz_tekst("...", skala) + 8 * skala;

        if (total_needed > dostepne_w && liczba_segmentow_ > 1) {
            ma_skrot = true;
            int current_needed = segment_widths[liczba_segmentow_ - 1] + skrot_w + sep_total;
            start_seg = liczba_segmentow_ - 1;
            while (start_seg > 1) {
                int next_needed = current_needed + segment_widths[start_seg - 1] + sep_total;
                if (next_needed <= dostepne_w) {
                    current_needed = next_needed;
                    --start_seg;
                } else {
                    break;
                }
            }
        }

        int cur_x = pad_l;
        int cy = (obszar_.wysokosc - 16) / 2;

        skrot_rect_ = Prostokat(0, 0, 0, 0);
        if (ma_skrot) {
            skrot_rect_ = Prostokat(cur_x, 2 * skala, skrot_w, obszar_.wysokosc - 4 * skala);
            if (najechany_skrot_) {
                ctx.rysuj_zaokraglony_prostokat(skrot_rect_, 0x003A2814, 3);
                ctx.rysuj_zaokraglona_ramke(skrot_rect_, Kolory::RamkaDomyslna, 3, 1);
            }
            ctx.rysuj_tekst_wysrodkowany(skrot_rect_, "...",
                                         najechany_skrot_ ? Kolory::AkcentNajechany : Kolory::TekstWyciszony, skala);
            cur_x += skrot_w + sep_spacing;

            ctx.rysuj_tekst_obciety(Punkt(cur_x, cy), separator_tekst_,
                                    Kolory::TekstWyciszony, sep_tw, skala);
            cur_x += sep_tw + sep_spacing;
        }

        for (size_t i = start_seg; i < liczba_segmentow_; ++i) {
            int sw = segment_widths[i];
            segmenty_[i].x = cur_x;
            segmenty_[i].szerokosc = sw;

            Prostokat seg_rect(cur_x, 2 * skala, sw, obszar_.wysokosc - 4 * skala);

            bool ostatni = (i + 1 == liczba_segmentow_);
            bool hov = (static_cast<int>(i) == najechany_segment_);

            if (hov) {
                ctx.rysuj_zaokraglony_prostokat(seg_rect, 0x003A2814, 3);
                ctx.rysuj_zaokraglona_ramke(seg_rect, Kolory::RamkaDomyslna, 3, 1);
            }

            uint32_t col_txt = ostatni ? Kolory::AkcentZloty
                                       : (hov ? Kolory::AkcentNajechany
                                              : Kolory::TekstBialy);
            ctx.rysuj_tekst_wysrodkowany(seg_rect, segmenty_[i].nazwa, col_txt, skala);

            cur_x += sw + sep_spacing;

            if (!ostatni) {
                ctx.rysuj_tekst_obciety(Punkt(cur_x, cy), separator_tekst_,
                                        Kolory::TekstWyciszony, sep_tw, skala);
                cur_x += sep_tw + sep_spacing;
            }
        }
    }

    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_wcisniecie_myszy(e);
        if (!e.czy_lewy_przycisk() || !czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        if (skrot_rect_.szerokosc > 0 && skrot_rect_.zawiera(lx, ly)) {
            if (cb_wybor_) cb_wybor_(this, 0, segmenty_[0].nazwa, dane_wyboru_);
            e.obsluzone = true;
            return;
        }

        int seg = segment_pod_punktem(lx, ly);
        if (seg >= 0 && seg < static_cast<int>(liczba_segmentow_)) {
            if (cb_wybor_) {
                cb_wybor_(this, seg, segmenty_[seg].nazwa, dane_wyboru_);
            }
            e.obsluzone = true;
        }
    }

    void obsluz_ruch_myszy(ZdarzenieMyszki& e) override {
        Kontrolka::obsluz_ruch_myszy(e);
        if (!czy_wlaczona_) return;

        int lx = e.x - obszar_ekranu_.x;
        int ly = e.y - obszar_ekranu_.y;

        bool skrot_hov = skrot_rect_.szerokosc > 0 && skrot_rect_.zawiera(lx, ly);
        int seg = segment_pod_punktem(lx, ly);

        if (seg != najechany_segment_ || skrot_hov != najechany_skrot_) {
            najechany_segment_ = seg;
            najechany_skrot_ = skrot_hov;
            uniewaznij();
        }
    }

    void obsluz_opuszczenie_myszy() override {
        Kontrolka::obsluz_opuszczenie_myszy();
        if (najechany_segment_ != -1 || najechany_skrot_) {
            najechany_segment_ = -1;
            najechany_skrot_ = false;
            uniewaznij();
        }
    }

private:
    Segment segmenty_[MAKS_SEGMENTOW]{};
    size_t liczba_segmentow_{0};

    char separator_tekst_[16]{'>', '\0'};

    int najechany_segment_{-1};
    bool najechany_skrot_{false};
    Prostokat skrot_rect_{};

    FunkcjaSegmentu cb_wybor_{nullptr};
    void* dane_wyboru_{nullptr};

    int segment_pod_punktem(int lx, int ly) const {
        const SkalaUI skala = skala_dpi_;
        if (ly < 2 * skala || ly >= obszar_.wysokosc - 2 * skala) return -1;
        for (size_t i = 0; i < liczba_segmentow_; ++i) {
            if (segmenty_[i].szerokosc > 0) {
                if (lx >= segmenty_[i].x && lx < segmenty_[i].x + segmenty_[i].szerokosc) {
                    return static_cast<int>(i);
                }
            }
        }
        return -1;
    }
};

} // namespace BursztynUI
