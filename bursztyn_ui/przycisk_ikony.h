#pragma once
/*
 * BursztynUI - przycisk z ikona rastrowa (PNG z kanalem alfa) i jej pamiec podreczna.
 *
 * ObrazIkony: plik PNG (<= 128 x 128, <= 256 KiB - limit dekodera ikon Ring 3, dekoduj_ikone_png) jest czytany i
 * dekodowany RAZ (takze nieudana proba jest zapamietywana - brak ponownych odczytow), a wersja dla danego boku jest
 * liczona raz na zmiane rozmiaru (uklad / skala DPI), nigdy przy rysowaniu. Skalowanie: srednia obszarowa na
 * pikselach z premnozona alfa (bez ciemnych obwodek i bez aliasingu przy zmniejszaniu 128 -> 20 px), proporcje
 * zachowane (dopasowanie do kwadratu, przezroczyste marginesy).
 *
 * PrzyciskIkony (Przycisk): ta sama obsluga myszy, klawiatury (Spacja/Enter), fokusu, polecen i podpowiedzi.
 *  - ikona lezy na "kaflu" w kolorze jasnej powierzchni motywu: grafiki moga byc ciemne (grafitowe) i bez kafla
 *    bylyby niewidoczne na ciemnych przyciskach; kafel nie zmienia samej grafiki,
 *  - stany: normalny, najechany (jasniejszy kafel + obwodka akcentu), wcisniety (ciemniejszy kafel), wylaczony
 *    (przygaszony kafel, ikona z alfa 40 %), fokus klawiatury (pierscien akcentu), zaznaczony/przelaczony
 *    (np. wyciszenie: kafel z odcieniem akcentu + pierscien),
 *  - Rodzaj::Glowny: wiekszy, okragly kafel ze stalym pierscieniem akcentu (np. Odtworz/Pauza),
 *  - bez ikony (brak lub uszkodzony plik) przycisk jest zwyklym przyciskiem tekstowym z tym samym tekstem - akcja
 *    pozostaje dostepna i opisana; tekst jest tez nazwa dostepnosci, a podpowiedz (ustaw_podpowiedz) opisuje akcje.
 */
#include "przycisk.h"
#include "../obrazy/dekoder_obrazu.h"

namespace BursztynUI {

class ObrazIkony {
public:
    static constexpr int MAKS_BOK = 128;
    ObrazIkony() = default;
    ObrazIkony(const ObrazIkony&) = delete;
    ObrazIkony& operator=(const ObrazIkony&) = delete;
    ~ObrazIkony() { delete[] zrodlo_; delete[] skal_; }

    // Odczyt i dekodowanie pliku - tylko przy pierwszym wywolaniu (wynik, takze porazka, jest zapamietany).
    bool wczytaj_plik(const char* sciezka) {
        if (proba_) return jest();
        proba_ = true;
        uint32_t rozmiar = 0, odczytano = 0;
        if (!sciezka || !pobierz_rozmiar_pliku(sciezka, &rozmiar) || !rozmiar) { blad_ = BladObrazu::Format; return false; }
        if (rozmiar > 256U * 1024U) { blad_ = BladObrazu::ZaDuzy; return false; }
        auto* dane = new uint8_t[rozmiar];
        if (!dane) { blad_ = BladObrazu::Pamiec; return false; }
        const bool ok = czytaj_fragment_pliku(sciezka, 0, reinterpret_cast<char*>(dane), rozmiar, &odczytano) && odczytano == rozmiar;
        if (ok) dekoduj(dane, rozmiar); else blad_ = BladObrazu::Uszkodzony;
        delete[] dane;
        return jest();
    }
    // Dekodowanie z pamieci (testy, zasoby wbudowane).
    bool wczytaj_dane(const uint8_t* dane, size_t n) {
        if (proba_) return jest();
        proba_ = true;
        dekoduj(dane, n);
        return jest();
    }
    bool jest() const { return zrodlo_ != nullptr; }
    bool proba() const { return proba_; }
    BladObrazu blad() const { return blad_; }
    int szerokosc_zrodla() const { return zw_; }
    int wysokosc_zrodla() const { return zh_; }
    uint32_t liczba_dekodowan() const { return dekodowania_; }
    uint32_t liczba_skalowan() const { return skalowania_; }

    // Wersja bok x bok (proste ARGB, alfa nieprzemnozona). Liczona tylko przy zmianie boku (uklad, nie rysowanie).
    bool przygotuj(int bok) {
        if (!zrodlo_ || bok < 1 || bok > MAKS_BOK) return false;
        if (bok == skal_bok_ && skal_) return true;
        auto* p = new uint32_t[size_t(bok) * size_t(bok)];
        if (!p) return false;
        skaluj(p, bok);
        delete[] skal_;
        skal_ = p; skal_bok_ = bok; ++skalowania_;
        return true;
    }
    const uint32_t* piksele(int bok) const { return (skal_ && bok == skal_bok_) ? skal_ : nullptr; }
    int przygotowany_bok() const { return skal_ ? skal_bok_ : 0; }

private:
    static void* al(size_t n) { return gui_malloc(static_cast<unsigned long>(n)); }
    static void zw(void* p) { gui_free(p); }
    void dekoduj(const uint8_t* dane, size_t n) {
        DekodowanyObraz o{};
        blad_ = dane && n ? dekoduj_ikone_png(dane, n, &o, &ObrazIkony::al, &ObrazIkony::zw) : BladObrazu::Format;
        ++dekodowania_;
        if (blad_ != BladObrazu::OK || !o.piksele || !o.szerokosc || !o.wysokosc || o.szerokosc > uint32_t(MAKS_BOK) ||
            o.wysokosc > uint32_t(MAKS_BOK) || o.stride < o.szerokosc * 4) {
            if (o.piksele) zw(o.piksele);
            if (blad_ == BladObrazu::OK) blad_ = BladObrazu::Uszkodzony;
            return;
        }
        zw_ = int(o.szerokosc); zh_ = int(o.wysokosc);
        zrodlo_ = new uint32_t[size_t(zw_) * size_t(zh_)];
        if (zrodlo_) {
            for (int y = 0; y < zh_; ++y) {
                const auto* w = reinterpret_cast<const uint32_t*>(o.piksele + size_t(y) * o.stride);
                for (int x = 0; x < zw_; ++x) zrodlo_[size_t(y) * size_t(zw_) + size_t(x)] = w[x];
            }
        } else blad_ = BladObrazu::Pamiec;
        zw(o.piksele);
    }
    // Srednia obszarowa (premnozona alfa) do kwadratu bok x bok z zachowaniem proporcji. Powiekszanie (bok >
    // zrodlo) sprowadza sie do najblizszego sasiada - ikony sa przygotowane jako 128 x 128, wiec zwykle maleja.
    void skaluj(uint32_t* dst, int bok) const {
        for (int i = 0; i < bok * bok; ++i) dst[i] = 0;
        int dw = bok, dh = bok;
        if (zw_ >= zh_) dh = zh_ * bok / zw_; else dw = zw_ * bok / zh_;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        const int ox = (bok - dw) / 2, oy = (bok - dh) / 2;
        for (int y = 0; y < dh; ++y) {
            const int y0 = y * zh_ / dh; int y1 = (y + 1) * zh_ / dh; if (y1 <= y0) y1 = y0 + 1;
            for (int x = 0; x < dw; ++x) {
                const int x0 = x * zw_ / dw; int x1 = (x + 1) * zw_ / dw; if (x1 <= x0) x1 = x0 + 1;
                uint32_t a = 0, r = 0, g = 0, b = 0, k = 0;
                for (int yy = y0; yy < y1 && yy < zh_; ++yy)
                    for (int xx = x0; xx < x1 && xx < zw_; ++xx) {
                        const uint32_t c = zrodlo_[size_t(yy) * size_t(zw_) + size_t(xx)];
                        const uint32_t al = c >> 24;
                        a += al; r += ((c >> 16) & 0xFF) * al; g += ((c >> 8) & 0xFF) * al; b += (c & 0xFF) * al; ++k;
                    }
                uint32_t out = 0;
                if (a && k) {
                    const uint32_t aa = (a + k / 2) / k;
                    out = (aa << 24) | (((r + a / 2) / a) << 16) | (((g + a / 2) / a) << 8) | ((b + a / 2) / a);
                }
                dst[size_t(oy + y) * size_t(bok) + size_t(ox + x)] = out;
            }
        }
    }
    uint32_t* zrodlo_ = nullptr;
    int zw_ = 0, zh_ = 0;
    uint32_t* skal_ = nullptr;
    int skal_bok_ = 0;
    bool proba_ = false;
    BladObrazu blad_ = BladObrazu::OK;
    uint32_t dekodowania_ = 0, skalowania_ = 0;
};

enum class RodzajPrzyciskuIkony : uint8_t { Zwykly, Glowny };

class PrzyciskIkony : public Przycisk {
public:
    // tekst: nazwa akcji (dostepnosc) i etykieta zastepcza, gdy ikony brak; bok_dip: rozmiar ikony przy 100 %.
    PrzyciskIkony(const char* tekst, ObrazIkony* ikona, int bok_dip = 22,
                  RodzajPrzyciskuIkony rodzaj = RodzajPrzyciskuIkony::Zwykly)
        : Przycisk(tekst), ikona_(ikona), bok_dip_(bok_dip < 8 ? 8 : bok_dip), rodzaj_(rodzaj) {
        // w komorce siatki wysrodkowany (rzad z przyciskiem glownym jest wyzszy niz zwykle kafle)
        wyrownanie_poziome_ = WyrownaniePoziome::Srodek;
        wyrownanie_pionowe_ = WyrownaniePionowe::Srodek;
    }

    void ustaw_obraz_ikony(ObrazIkony* i) { if (i != ikona_) { ikona_ = i; przygotuj(); uniewaznij(); } }
    const ObrazIkony* obraz_ikony() const { return ikona_; }
    bool uzywa_ikony() const { return ikona_ && ikona_->jest(); }
    void ustaw_zaznaczony(bool z) { if (z != zaznaczony_) { zaznaczony_ = z; uniewaznij(); } }
    bool czy_zaznaczony() const { return zaznaczony_; }
    RodzajPrzyciskuIkony rodzaj() const { return rodzaj_; }
    // Bok ikony i kafla w pikselach (biezaca skala) - uklad i testy.
    int bok_ikony_px() const { int b = bok_dip_ * skala_dpi_; return b > ObrazIkony::MAKS_BOK ? ObrazIkony::MAKS_BOK : b; }
    int bok_kafla_px() const { return bok_ikony_px() + (rodzaj_ == RodzajPrzyciskuIkony::Glowny ? 14 : 10) * skala_dpi_; }
    Prostokat obszar_kafla() const {
        const int k = bok_kafla_px();
        return Prostokat((obszar_.szerokosc - k) / 2, (obszar_.wysokosc - k) / 2, k, k);
    }

    Rozmiar zmierz(Rozmiar dostepny) override {
        if (!uzywa_ikony()) return Przycisk::zmierz(dostepny);
        przygotuj();                                   // uklad: jedyne miejsce skalowania ikony
        const SkalaUI s = skala_dpi_;
        int bok = bok_kafla_px() + 8 * s;              // miejsce na pierscien fokusu (4 DIP z kazdej strony)
        if (bok < 34 * s) bok = 34 * s;                // cel klikniecia
        Rozmiar r(bok, bok);
        if (preferowany_rozmiar_.szerokosc >= 0) r.szerokosc = preferowany_rozmiar_.szerokosc * s;
        if (preferowany_rozmiar_.wysokosc >= 0) r.wysokosc = preferowany_rozmiar_.wysokosc * s;
        r.szerokosc = przytnij(r.szerokosc, minimalny_rozmiar_.szerokosc * s, maksymalny_rozmiar_.szerokosc * s);
        r.wysokosc = przytnij(r.wysokosc, minimalny_rozmiar_.wysokosc * s, maksymalny_rozmiar_.wysokosc * s);
        return r;
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        const int bok = bok_ikony_px();
        const uint32_t* px = ikona_ ? ikona_->piksele(bok) : nullptr;
        if (!uzywa_ikony() || !px) { Przycisk::rysuj_siebie(ctx); return; }
        const SkalaUI s = skala_dpi_;
        const Prostokat k = obszar_kafla();
        const bool glowny = rodzaj_ == RodzajPrzyciskuIkony::Glowny;
        const int promien = glowny ? k.szerokosc / 2 : 7 * s;
        const uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
        const uint32_t tekst = motyw_kolor(MOTYW_KOLOR_TEKST), panel = motyw_kolor(MOTYW_KOLOR_PANEL), ramka = motyw_kolor(MOTYW_KOLOR_RAMKA);
        const uint32_t jasny = jasnosc(tekst) > jasnosc(panel) ? tekst : panel;   // jasna powierzchnia w kazdym motywie
        uint32_t kafel = mieszaj(jasny, ramka, 30);
        if (!czy_wlaczona_) kafel = mieszaj(jasny, panel, 140);
        else if (czy_wcisnieta_) kafel = mieszaj(jasny, ramka, 80);
        else if (czy_najechana_) kafel = jasny;
        if (zaznaczony_ && czy_wlaczona_) kafel = mieszaj(kafel, akcent, 70);
        // pierscien: fokus (gruby), zaznaczenie / przycisk glowny / najechanie (cienki)
        if (czy_ma_fokus_ && czy_wlaczona_) {
            const int g = 2 * s;
            ctx.rysuj_zaokraglona_ramke(Prostokat(k.x - 2 * g, k.y - 2 * g, k.szerokosc + 4 * g, k.wysokosc + 4 * g), akcent,
                                        glowny ? (k.szerokosc + 4 * g) / 2 : promien + 2 * g, g);
        }
        ctx.rysuj_zaokraglony_prostokat(k, kafel, promien);
        if (czy_wlaczona_ && (zaznaczony_ || glowny || czy_najechana_))
            ctx.rysuj_zaokraglona_ramke(k, akcent, promien, (zaznaczony_ || glowny) ? 2 * s : 1 * s);
        else
            ctx.rysuj_zaokraglona_ramke(k, ramka, promien, 1);
        // ikona: proste ARGB nad kaflem; wylaczony przycisk - alfa 40 %
        const int ix = k.x + (k.szerokosc - bok) / 2, iy = k.y + (k.wysokosc - bok) / 2 + (czy_wcisnieta_ ? 1 * s : 0);
        const Punkt off = ctx.pobierz_przesuniecie();
        uint32_t wiersz[ObrazIkony::MAKS_BOK];
        for (int y = 0; y < bok; ++y) {
            const uint32_t* zr = px + size_t(y) * size_t(bok);
            for (int x = 0; x < bok; ++x) {
                uint32_t c = zr[x];
                if (!czy_wlaczona_) c = (c & 0x00FFFFFFu) | ((((c >> 24) * 2 + 2) / 5) << 24);
                wiersz[x] = c;
            }
            ctx.rysuj_wiersz_alfa_ekranu(off.x + ix, off.y + iy + y, bok, wiersz);
        }
    }

private:
    void przygotuj() { if (ikona_ && ikona_->jest()) ikona_->przygotuj(bok_ikony_px()); }
    static unsigned jasnosc(uint32_t c) { return (((c >> 16) & 255) * 54 + ((c >> 8) & 255) * 183 + (c & 255) * 19) >> 8; }
    // t: 0..256 (udzial b)
    static uint32_t mieszaj(uint32_t a, uint32_t b, unsigned t) {
        uint32_t o = 0;
        for (unsigned sh = 0; sh < 24; sh += 8)
            o |= ((((a >> sh) & 255) * (256 - t) + ((b >> sh) & 255) * t) >> 8) << sh;
        return o;
    }
    ObrazIkony* ikona_;        // wspolna pamiec podreczna aplikacji (jeden bok na obraz)
    int bok_dip_;
    RodzajPrzyciskuIkony rodzaj_;
    bool zaznaczony_ = false;
};

} // namespace BursztynUI
