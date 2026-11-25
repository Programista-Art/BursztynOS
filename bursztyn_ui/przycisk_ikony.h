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
 *  - ikona (prosta alfa) lezy BEZPOSREDNIO na tle motywu: w stanie normalnym przycisk nie rysuje zadnego tla ani
 *    obwodki, wiec przezroczyste piksele pokazuja tlo okna, a wygladzone krawedzie mieszaja sie z nim,
 *  - stany (kolory motywu, ciemny i jasny; styl_stanu): co najwyzej JEDNA ramka o grubosci 1 DIP, zawsze na
 *    obwodzie kafla (wewnatrz przycisku, bez wychodzenia na sasiadow) i co najwyzej jedna powierzchnia:
 *      najechany    - bardzo subtelna powierzchnia (~8 % w strone koloru tekstu), bez ramki,
 *      wcisniety    - nieco mocniejsza (~16 %) + ikona 1 px nizej, bez ramki,
 *      zaznaczony   - (przelacznik, np. wyciszenie) miekki odcien akcentu (~19 %) + stonowana ramka akcentu,
 *      fokus        - ramka w pelnym kolorze akcentu (wyraznie widoczna, ale 1 DIP),
 *      fokus + zazn.- jedna ramka w pelnym akcencie na odcieniu zaznaczenia (bez podwojnych pierscieni),
 *      wylaczony    - bez tla i ramki, ikona z alfa 40 %,
 *  - Rodzaj::Glowny (Odtworz/Pauza): wiekszy, okragly, ze stala cienka (1 DIP) stonowana ramka akcentu; fokus
 *    zmienia tylko jej kolor na pelny akcent. Stan odtwarzania pokazuje ikona (Odtworz/Pauza), nie ramka,
 *  - bez ikony (brak lub uszkodzony plik) przycisk jest zwyklym przyciskiem tekstowym z tym samym tekstem - akcja
 *    pozostaje dostepna i opisana; tekst jest tez nazwa dostepnosci, a podpowiedz (ustaw_podpowiedz) opisuje akcje.
 */
#include "przycisk.h"
#include "../obrazy/dekoder_obrazu.h"
#include "../obrazy/skalowanie.h"

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
    // Wspolne skalowanie ikon z alfa (obrazy/skalowanie.h): srednia obszarowa na premnozonej alfie przy
    // pomniejszaniu, proporcje zachowane, przezroczyste marginesy; kopia 1:1 przy tym samym rozmiarze.
    void skaluj(uint32_t* dst, int bok) const {
        const size_t n = size_t(bok) * size_t(bok);
        if (!skaluj_ikone_alfa(zrodlo_, uint32_t(zw_), uint32_t(zh_), size_t(zw_) * size_t(zh_) * 4, dst, uint32_t(bok),
                               uint32_t(bok), uint32_t(bok), n * 4))
            for (size_t i = 0; i < n; ++i) dst[i] = 0;
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
        const StylStanuIkony st = styl_stanu();
        if (st.tlo) ctx.rysuj_zaokraglony_prostokat(k, st.kolor_tla, promien);
        if (st.ramka) ctx.rysuj_zaokraglona_ramke(k, st.kolor_ramki, promien, st.grubosc);
        // ikona: proste ARGB nad tlem okna (albo powierzchnia stanu); wylaczony przycisk - alfa 40 %
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

    // Wyglad stanu (uklad i testy): jedna powierzchnia i jedna ramka 1 DIP na obwodzie kafla.
    struct StylStanuIkony { bool tlo; uint32_t kolor_tla; bool ramka; uint32_t kolor_ramki; int grubosc; };
    StylStanuIkony styl_stanu() const {
        const uint32_t akcent = motyw_kolor(MOTYW_KOLOR_AKCENT);
        const uint32_t tekst = motyw_kolor(MOTYW_KOLOR_TEKST), panel = motyw_kolor(MOTYW_KOLOR_PANEL);
        StylStanuIkony st{false, panel, false, akcent, 1 * skala_dpi_};
        if (!czy_wlaczona_) return st;                            // wylaczony: tylko przygaszona ikona
        const bool glowny = rodzaj_ == RodzajPrzyciskuIkony::Glowny;
        if (czy_wcisnieta_) { st.kolor_tla = mieszaj(panel, tekst, 40); st.tlo = true; }
        else if (czy_najechana_) { st.kolor_tla = mieszaj(panel, tekst, 20); st.tlo = true; }
        if (zaznaczony_) { st.kolor_tla = mieszaj(st.kolor_tla, akcent, 48); st.tlo = true; }
        const uint32_t stonowany = mieszaj(panel, akcent, 150);  // ~59 % akcentu: zaznaczenie / przycisk glowny
        if (czy_ma_fokus_) { st.ramka = true; st.kolor_ramki = akcent; }
        else if (zaznaczony_ || glowny) { st.ramka = true; st.kolor_ramki = stonowany; }
        return st;
    }

private:
    void przygotuj() { if (ikona_ && ikona_->jest()) ikona_->przygotuj(bok_ikony_px()); }
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
