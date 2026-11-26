#pragma once

#include "popup.h"
#include "czcionka.h"

namespace BursztynUI {

class Podpowiedz : public PowierzchniaPopup {
public:
    static constexpr size_t MAKS_TEKSTU = 256;
    static constexpr size_t MAKS_LINII = 16;

    Podpowiedz() {
        zaktualizuj_motyw();
        czy_moze_miec_fokus_ = false;
    }

    void zaktualizuj_motyw() override {
        styl_ = Motyw::pobierz_domyslny().styl_podpowiedzi;
        uniewaznij();
    }

    void ustaw_tekst(const char* t) {
        kopiuj_str(tekst_, sizeof(tekst_), t);
        przelicz_rozmiar();
        uniewaznij();
    }
    const char* tekst() const { return tekst_; }

    void ustaw_tytul(const char* t) {
        kopiuj_str(tytul_, sizeof(tytul_), t);
        przelicz_rozmiar();
        uniewaznij();
    }
    const char* tytul() const { return tytul_; }

    void ustaw_maksymalna_szerokosc(int max_w) {
        max_szerokosc_ = max_w > 60 ? max_w : 60;
        przelicz_rozmiar();
        uniewaznij();
    }
    int maksymalna_szerokosc() const { return max_szerokosc_; }

    Rozmiar obliczony_rozmiar() const { return obliczony_rozmiar_; }
    // Wynik zawijania (tylko odczyt; testy i diagnostyka ukladu).
    size_t liczba_linii() const { return liczba_linii_; }
    void linia(size_t i, char* cel, size_t cap) const {
        if (!cel || !cap) return;
        cel[0] = '\0';
        if (i >= liczba_linii_) return;
        size_t n = linie_[i].dlugosc < cap - 1 ? linie_[i].dlugosc : cap - 1;
        for (size_t j = 0; j < n; ++j) cel[j] = tekst_[linie_[i].start + j];
        cel[n] = '\0';
    }

    void rysuj_siebie(KontekstRysowania& ctx) override {
        Prostokat b(0, 0, obszar_ekranu_.szerokosc, obszar_ekranu_.wysokosc);
        ctx.rysuj_prostokat(b, styl_.normalny.tlo);
        ctx.rysuj_ramke(b, styl_.normalny.ramka, 1);

        SkalaUI s = skala_dpi_;
        const int lh = wysokosc_linii(s);
        int y = 4 * s;

        // Tytuł jeśli obecny
        if (tytul_[0] != '\0') {
            ctx.rysuj_tekst(Punkt(6 * s, y), tytul_, motyw_kolor(MOTYW_KOLOR_AKCENT), s);
            y += lh + 2 * s;
            ctx.rysuj_linie_pozioma(4 * s, y - 1 * s, b.szerokosc - 8 * s, motyw_kolor(MOTYW_KOLOR_RAMKA));
            y += 4 * s;
        }

        // Linie tekstu (cala linia: zawijanie mierzy ten sam tekst, ktory jest rysowany)
        for (size_t i = 0; i < liczba_linii_; ++i) {
            char linia_buf[MAKS_TEKSTU];
            kopiuj_zakres(linia_buf, linie_[i]);
            ctx.rysuj_tekst(Punkt(6 * s, y), linia_buf, motyw_kolor(MOTYW_KOLOR_TEKST), s);
            y += lh;
        }
    }

private:
    struct ZakresLinii {
        size_t start{0};
        size_t dlugosc{0};
    };

    // Wysokosc linii i szerokosc z metryk biezacej czcionki (TTF przez BSF albo bitmapowa) - nie ze stalych
    // 8 px na bajt i 16 px na linie: przy TTF i polskich znakach ramka byla wezsza i nizsza niz tekst (przyciecie).
    static int wysokosc_linii(SkalaUI s) { return CzcionkaUI::pobierz_wysokosc(s) + 2 * s; }
    void kopiuj_zakres(char* cel, const ZakresLinii& z) const {
        size_t len = z.dlugosc < MAKS_TEKSTU - 1 ? z.dlugosc : MAKS_TEKSTU - 1;
        for (size_t j = 0; j < len; ++j) cel[j] = tekst_[z.start + j];
        cel[len] = '\0';
    }
    int szerokosc_zakresu(size_t start, size_t dlugosc, SkalaUI s) const {
        char b[MAKS_TEKSTU];
        kopiuj_zakres(b, ZakresLinii{start, dlugosc});
        return CzcionkaUI::zmierz_tekst(b, s);
    }
    // Dlugosc (bajty) najdluzszego prefiksu od start, ktory miesci sie w limicie; granica znaku UTF-8, min. 1 znak.
    size_t prefiks_mieszczacy(size_t start, size_t koniec, int limit, SkalaUI s) const {
        size_t lo = 0, hi = koniec - start;
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            while (mid > lo && (uint8_t(tekst_[start + mid]) & 0xC0) == 0x80) --mid;
            if (mid == lo) break;
            if (szerokosc_zakresu(start, mid, s) <= limit) lo = mid; else hi = mid - 1;
        }
        if (lo == 0) {   // nawet jeden znak sie nie miesci: caly pierwszy znak (moze zostac przyciety)
            lo = 1;
            while (start + lo < koniec && (uint8_t(tekst_[start + lo]) & 0xC0) == 0x80) ++lo;
        }
        return lo;
    }

    void przelicz_rozmiar() {
        SkalaUI s = skala_dpi_;
        liczba_linii_ = 0;
        int max_w = 0;

        if (tytul_[0] != '\0') {
            max_w = CzcionkaUI::zmierz_tekst(tytul_, s);
        }

        size_t len = 0;
        while (tekst_[len] != '\0') ++len;
        const int limit_px = max_szerokosc_ * s - 12 * s;

        // Zawijanie po slowach (pomiar calej kandydackiej linii, O(slowa) pomiarow na linie); slowo dluzsze niz
        // limit jest dzielone na granicy znaku.
        size_t p = 0;
        while (p < len && liczba_linii_ < MAKS_LINII) {
            while (p < len && tekst_[p] == ' ') ++p;
            if (p >= len) break;
            size_t koniec_akapitu = p;
            while (koniec_akapitu < len && tekst_[koniec_akapitu] != '\n') ++koniec_akapitu;
            size_t dl = 0;                      // zaakceptowana dlugosc linii
            size_t q = p;
            while (q < koniec_akapitu) {
                size_t k = q;
                while (k < koniec_akapitu && tekst_[k] != ' ') ++k;   // koniec slowa
                if (szerokosc_zakresu(p, k - p, s) <= limit_px) { dl = k - p; q = k; while (q < koniec_akapitu && tekst_[q] == ' ') ++q; continue; }
                if (dl == 0) dl = prefiks_mieszczacy(p, k, limit_px, s);
                break;
            }
            if (dl == 0) dl = koniec_akapitu - p;
            while (dl > 0 && tekst_[p + dl - 1] == ' ') --dl;
            linie_[liczba_linii_].start = p;
            linie_[liczba_linii_].dlugosc = dl;
            const int lw = szerokosc_zakresu(p, dl, s);
            if (lw > max_w) max_w = lw;
            ++liczba_linii_;
            p += dl;
            while (p < len && tekst_[p] == ' ') ++p;
            if (p < len && tekst_[p] == '\n') ++p;
        }

        const int lh = wysokosc_linii(s);
        int h = (tytul_[0] != '\0' ? lh + 6 * s : 0) + static_cast<int>(liczba_linii_) * lh + 8 * s;
        int w = max_w + 12 * s;
        if (w < 40 * s) w = 40 * s;
        obliczony_rozmiar_ = Rozmiar(w, h);
    }

    static void kopiuj_str(char* cel, size_t max_cel, const char* zr) {
        if (!cel || max_cel == 0) return;
        if (!zr) { cel[0] = '\0'; return; }
        size_t i = 0;
        while (zr[i] != '\0' && i + 1 < max_cel) {
            cel[i] = zr[i];
            ++i;
        }
        cel[i] = '\0';
    }

    char tekst_[MAKS_TEKSTU]{};
    char tytul_[64]{};
    int max_szerokosc_{240};
    ZakresLinii linie_[MAKS_LINII];
    size_t liczba_linii_{0};
    Rozmiar obliczony_rozmiar_{60, 24};
};

class ZarzadcaPodpowiedzi {
public:
    ZarzadcaPodpowiedzi() = default;
    ~ZarzadcaPodpowiedzi() {
        ukryj();
    }

    void ustaw_opoznienie_pokazania(uint64_t ms) { opoznienie_ms_ = ms; }
    uint64_t opoznienie_pokazania() const { return opoznienie_ms_; }

    void ustaw_czas_wyswietlania(uint64_t ms) { czas_wyswietlania_ms_ = ms; }
    uint64_t czas_wyswietlania() const { return czas_wyswietlania_ms_; }

    bool czy_widoczna() const { return podpowiedz_.czy_otwarta(); }
    Podpowiedz& podpowiedz() { return podpowiedz_; }

    void obsluz_ruch(ZarzadcaPopupow& popupy, Kontrolka* najechana, int kursor_ekran_x, int kursor_ekran_y, uint64_t timestamp = 0) {
        if (najechana != ostatnia_kontrolka_) {
            ukryj(&popupy);
            ostatnia_kontrolka_ = najechana;
            czas_wejscia_ = timestamp;
            kursor_x_ = kursor_ekran_x;
            kursor_y_ = kursor_ekran_y;
            if (opoznienie_ms_ != 0) return;
        }

        kursor_x_ = kursor_ekran_x;
        kursor_y_ = kursor_ekran_y;

        if (najechana && !podpowiedz_.czy_otwarta()) {
            const char* tip = najechana->podpowiedz();
            if (tip && tip[0] != '\0') {
                if (timestamp >= czas_wejscia_ + opoznienie_ms_ || opoznienie_ms_ == 0) {
                    pokaz(popupy, tip, kursor_ekran_x, kursor_ekran_y);
                }
            }
        }
    }

    // Czy w zarzadcy jest otwarta INNA powierzchnia (lista rozwijana, menu, dialog). ZarzadcaPopupow::otworz zamyka
    // wszystkie otwarte powierzchnie, wiec podpowiedz pokazana w tym czasie zamknelaby np. rozwinieta liste zaraz po
    // otwarciu (Grajek: lista presetow korektora). Podpowiedz czeka wtedy, az popup sie zamknie.
    bool inny_popup_otwarty(const ZarzadcaPopupow& popupy) const {
        for (int i = 0; i < popupy.liczba_powierzchni(); ++i)
            if (popupy.powierzchnia(i) && popupy.powierzchnia(i) != &podpowiedz_) return true;
        return false;
    }

    void pokaz(ZarzadcaPopupow& popupy, const char* tekst, int kursor_ekran_x, int kursor_ekran_y, const char* tytul = nullptr) {
        if (!tekst || tekst[0] == '\0') return;
        if (inny_popup_otwarty(popupy)) return;
        ukryj(&popupy);
        podpowiedz_.ustaw_tytul(tytul);
        podpowiedz_.ustaw_tekst(tekst);
        Rozmiar sz = podpowiedz_.obliczony_rozmiar();

        Prostokat rect = popupy.umiesc_przy_punkcie_ekranu(kursor_ekran_x + 12, kursor_ekran_y + 18, sz);
        popupy.otworz(podpowiedz_, rect);
    }

    // Zamyka TYLKO powierzchnie podpowiedzi (nie wszystkie popupy zarzadcy - inaczej ukrycie podpowiedzi przy
    // kliknieciu zamykaloby takze liste rozwijana otwarta nad nia).
    void ukryj(ZarzadcaPopupow* popupy = nullptr) {
        if (podpowiedz_.czy_otwarta()) {
            ZarzadcaPopupow* z = podpowiedz_.zarzadca() ? podpowiedz_.zarzadca() : popupy;
            if (z && !z->zamknij_powierzchnie(&podpowiedz_, PowodZamknieciaPopup::Anulowanie))
                z->zamknij(PowodZamknieciaPopup::Anulowanie);
        }
    }

    void odrejestruj_kontrolke(Kontrolka* k) {
        if (ostatnia_kontrolka_ == k) {
            ukryj();
            reset();
        }
    }

    void odrejestruj_zarzadce(ZarzadcaPopupow& popupy) {
        if (podpowiedz_.zarzadca() == &popupy) {
            ukryj(&popupy);
        }
        reset();
    }

    void reset() {
        ostatnia_kontrolka_ = nullptr;
        czas_wejscia_ = 0;
    }

private:
    Podpowiedz podpowiedz_;
    Kontrolka* ostatnia_kontrolka_{nullptr};
    uint64_t czas_wejscia_{0};
    uint64_t opoznienie_ms_{500};
    uint64_t czas_wyswietlania_ms_{5000};
    int kursor_x_{0};
    int kursor_y_{0};
};

} // namespace BursztynUI
