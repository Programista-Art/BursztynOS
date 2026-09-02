#pragma once

#include "kontrolka.h"
#include "pasek_przewijania.h"
#include "czcionka.h"

namespace BursztynUI {

enum class WyrownanieAkapitu : uint8_t {
    Lewo,
    Srodek,
    Prawo
};

struct FormatTekstu {
    bool pogrubienie{false};
    bool kursywa{false};
    bool podkreslenie{false};
    uint32_t kolor{0x00FFFFFF};
    uint32_t tlo{0};
};

class EdytorFormatowany : public Kontrolka {
public:
    static constexpr size_t MAKS_AKAPITOW = 128;
    static constexpr size_t MAKS_FRAGMENTOW = 16;
    static constexpr size_t MAKS_DLUGOSC_FRAGMENTU = 128;

    struct Fragment {
        char tekst[MAKS_DLUGOSC_FRAGMENTU]{};
        FormatTekstu format{};
    };

    struct Akapit {
        Fragment fragmenty[MAKS_FRAGMENTOW]{};
        size_t liczba_fragmentow{0};
        WyrownanieAkapitu wyrownanie{WyrownanieAkapitu::Lewo};
    };

    EdytorFormatowany() {
        styl_ = Motyw::pobierz_domyslny().styl_pola_tekstowego;
        styl_.normalny.tlo = Kolory::WejscieTlo;
        styl_.normalny.ramka = Kolory::RamkaDomyslna;
        styl_.normalny.grubosc_ramki = 1;
        czy_moze_miec_fokus_ = true;
        wypelnienie_ = Marginesy(6, 6, 6, 6);
        preferowany_rozmiar_ = Rozmiar(320, 220);

        pionowy_ = new PasekPrzewijania();
        pionowy_->ustaw_orientacje(Orientacja::Pionowa);
        pionowy_->pokaz_strzalki(true);
        pionowy_->ustaw_maly_krok(wysokosc_linii_);
        dodaj_kontrolke(pionowy_);

        pionowy_->model().przy_zmianie([](ModelPrzewijania*, int64_t, void* dane) {
            EdytorFormatowany* e = static_cast<EdytorFormatowany*>(dane);
            if (e) e->uniewaznij();
        }, this);
    }

    ~EdytorFormatowany() override = default;

    void wyczysc() {
        liczba_akapitow_ = 0;
        zaznaczony_akapit_ = -1;
        aktualizuj_przewijanie();
        uniewaznij();
    }

    size_t liczba_akapitow() const { return liczba_akapitow_; }

    bool dodaj_akapit(WyrownanieAkapitu wyrownanie = WyrownanieAkapitu::Lewo) {
        if (liczba_akapitow_ >= MAKS_AKAPITOW) return false;
        Akapit& a = akapity_[liczba_akapitow_++];
        a.liczba_fragmentow = 0;
        a.wyrownanie = wyrownanie;
        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    bool dodaj_fragment(size_t akapit_idx, const char* tekst, FormatTekstu format = FormatTekstu{}) {
        if (akapit_idx >= liczba_akapitow_) return false;
        Akapit& a = akapity_[akapit_idx];
        if (a.liczba_fragmentow >= MAKS_FRAGMENTOW) return false;

        Fragment& f = a.fragmenty[a.liczba_fragmentow++];
        if (!tekst) tekst = "";
        size_t i = 0;
        for (; i < sizeof(f.tekst) - 1 && tekst[i] != '\0'; ++i) {
            f.tekst[i] = tekst[i];
        }
        f.tekst[i] = '\0';
        f.format = format;

        aktualizuj_przewijanie();
        uniewaznij();
        return true;
    }

    bool dodaj_tekst(const char* tekst, FormatTekstu format = FormatTekstu{}) {
        if (liczba_akapitow_ == 0) {
            if (!dodaj_akapit()) return false;
        }
        return dodaj_fragment(liczba_akapitow_ - 1, tekst, format);
    }

    void ustaw_tekst_prosty(const char* tekst) {
        wyczysc();
        if (!tekst) return;

        const char* p = tekst;
        char buf[MAKS_DLUGOSC_FRAGMENTU];
        size_t bi = 0;

        dodaj_akapit();
        while (*p != '\0') {
            if (*p == '\n') {
                buf[bi] = '\0';
                if (bi > 0) dodaj_fragment(liczba_akapitow_ - 1, buf);
                dodaj_akapit();
                bi = 0;
            } else {
                if (bi < sizeof(buf) - 1) {
                    buf[bi++] = *p;
                }
            }
            ++p;
        }
        if (bi > 0) {
            buf[bi] = '\0';
            dodaj_fragment(liczba_akapitow_ - 1, buf);
        }
        aktualizuj_przewijanie();
        uniewaznij();
    }

    void ustaw_wyrownanie_akapitu(size_t akapit_idx, WyrownanieAkapitu w) {
        if (akapit_idx < liczba_akapitow_) {
            akapity_[akapit_idx].wyrownanie = w;
            uniewaznij();
        }
    }

    void rozmiesc(Prostokat docelowy_obszar) override {
        Kontrolka::rozmiesc(docelowy_obszar);
        aktualizuj_geometrie_paskow();
        aktualizuj_przewijanie();
    }

    Rozmiar oblicz_naturalny_rozmiar() const override {
        int skala = skala_dpi_;
        int h = static_cast<int>(liczba_akapitow_) * wysokosc_linii_ * skala + wypelnienie_.pionowo() * skala;
        return Rozmiar(280 * skala, h > 100 * skala ? h : 100 * skala);
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        StylStanu s = wyznacz_biezacy_styl_stanu();
        rysuj_tlo_i_ramke(ctx, s);

        int vp_w = viewport_w();
        int vp_h = viewport_h();
        if (vp_w <= 0 || vp_h <= 0) return;

        int skala = skala_dpi_;
        int h_linii = wysokosc_linii_ * skala;
        int scroll_y = pionowy_ && pionowy_->czy_widoczna() ? static_cast<int>(pionowy_->wartosc()) : 0;
        int pad_l = wypelnienie_.lewy * skala;
        int pad_t = wypelnienie_.gorny * skala;

        Prostokat rect_klienta(pad_l, pad_t, vp_w - pad_l, vp_h - pad_t);
        ctx.dodaj_przyciecie(rect_klienta);

        int start_akapit = scroll_y / h_linii;
        if (start_akapit < 0) start_akapit = 0;
        int end_akapit = (scroll_y + vp_h) / h_linii + 1;
        if (end_akapit > static_cast<int>(liczba_akapitow_)) end_akapit = static_cast<int>(liczba_akapitow_);

        for (int a = start_akapit; a < end_akapit; ++a) {
            const Akapit& ak = akapity_[a];
            int line_y = pad_t + (a * h_linii) - scroll_y;

            int total_line_w = 0;
            int frag_widths[MAKS_FRAGMENTOW];
            for (size_t f = 0; f < ak.liczba_fragmentow; ++f) {
                frag_widths[f] = CzcionkaUI::zmierz_tekst(ak.fragmenty[f].tekst, skala);
                total_line_w += frag_widths[f];
            }

            int cur_x = pad_l;
            if (ak.wyrownanie == WyrownanieAkapitu::Srodek) {
                int avail = vp_w - pad_l;
                if (total_line_w < avail) cur_x += (avail - total_line_w) / 2;
            } else if (ak.wyrownanie == WyrownanieAkapitu::Prawo) {
                int avail = vp_w - pad_l;
                if (total_line_w < avail) cur_x += (avail - total_line_w);
            }

            for (size_t f = 0; f < ak.liczba_fragmentow; ++f) {
                const Fragment& fr = ak.fragmenty[f];
                int fw = frag_widths[f];
                if (fw <= 0) continue;

                if (fr.format.tlo != 0) {
                    ctx.rysuj_prostokat(Prostokat(cur_x, line_y, fw, h_linii), fr.format.tlo);
                }

                if (fr.format.pogrubienie) {
                    ctx.rysuj_tekst_obciety(Punkt(cur_x, line_y + 1), fr.tekst,
                                            fr.format.kolor, fw, skala);
                    ctx.rysuj_tekst_obciety(Punkt(cur_x + 1, line_y + 1), fr.tekst,
                                            fr.format.kolor, fw, skala);
                } else if (fr.format.kursywa) {
                    ctx.rysuj_tekst_obciety(Punkt(cur_x + 1, line_y + 1), fr.tekst,
                                            fr.format.kolor, fw, skala);
                } else {
                    ctx.rysuj_tekst_obciety(Punkt(cur_x, line_y + 1), fr.tekst,
                                            fr.format.kolor, fw, skala);
                }

                if (fr.format.podkreslenie) {
                    int under_y = line_y + h_linii - 2 * skala;
                    ctx.rysuj_prostokat(Prostokat(cur_x, under_y, fw, 1 * skala), fr.format.kolor);
                }

                cur_x += fw;
            }
        }

        ctx.zdejmij_przyciecie();
    }

    void obsluz_rolke(ZdarzenieRolki& e) override {
        if (pionowy_ && pionowy_->czy_widoczna()) {
            pionowy_->obsluz_rolke(e);
        }
    }

private:
    Akapit akapity_[MAKS_AKAPITOW]{};
    size_t liczba_akapitow_{0};

    int zaznaczony_akapit_{-1};
    int wysokosc_linii_{20};
    int szerokosc_paska_{14};

    PasekPrzewijania* pionowy_{nullptr};

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

    void aktualizuj_geometrie_paskow() {
        int h_linii = wysokosc_linii_ * skala_dpi_;
        int total_h = static_cast<int>(liczba_akapitow_) * h_linii + wypelnienie_.pionowo() * skala_dpi_;
        bool potr = total_h > (obszar_.wysokosc - 2);

        int sb_sz = szerokosc_paska_ * skala_dpi_;
        if (pionowy_) {
            pionowy_->ustaw_widoczna(potr);
            if (potr) {
                pionowy_->ustaw_geometrie(obszar_.szerokosc - sb_sz - 1, 1, sb_sz, obszar_.wysokosc - 2);
            }
        }
    }

    void aktualizuj_przewijanie() {
        aktualizuj_geometrie_paskow();
        if (pionowy_) {
            int h_linii = wysokosc_linii_ * skala_dpi_;
            int total_h = static_cast<int>(liczba_akapitow_) * h_linii + wypelnienie_.pionowo() * skala_dpi_;
            pionowy_->ustaw_zakres(0, total_h);
            pionowy_->ustaw_rozmiar_strony(viewport_h() > 0 ? viewport_h() : 1);
        }
    }
};

} // namespace BursztynUI
