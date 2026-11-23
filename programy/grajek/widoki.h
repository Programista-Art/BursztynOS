#pragma once
/*
 * Grajek - kontrolki BursztynUI specyficzne dla odtwarzacza.
 *
 *  - TabelaGrajka: TabelaWirtualna z aktywacja (dwuklik, Enter) i wskaznikiem biezacego utworu,
 *  - DaneBiblioteki / DaneKolejki: dostawcy wierszy (Tytul, Wykonawca, Czas, Format; kolejka z numerem),
 *  - WidokWizualizacji: wskazniki poziomu L/P i widmo 24 pasm z rzeczywistych probek (Analizator),
 *  - SuwakPostepu: Suwak, ktory wie, ze uzytkownik go przeciaga (pozycja odtwarzania nie "wyrywa" uchwytu),
 *  - EtykietaJednowierszowa: tekst w jednym wierszu z wielokropkiem; dopasowanie (skala, obciecie) liczone tylko
 *    przy zmianie tekstu lub szerokosci, nie przy kazdym rysowaniu; pelny tekst w podpowiedzi,
 *  - PanelElastyczny: panel dokowany, ktorego szerokosc jest ulamkiem dostepnej (z min/maks) - uklad podaza za
 *    rozmiarem okna bez dodatkowego przeliczania w aplikacji,
 *  - ObrazOkladki: kwadratowa okladka zmniejszana w waskim oknie,
 *  - PasekStanuGrajka: staly, jednowierszowy pasek komunikatow (+ opcjonalny wiersz diagnostyki); zmiana tekstu
 *    uniewaznia tylko pasek - bez przeliczania ukladu i pelnego odrysowania okna,
 *  - grafika_domyslna(): domyslna okladka Grajka (generowana raz, bez zasobow zewnetrznych).
 * Kolory pochodza z globalnego motywu Bursztyn OS (motyw_kolor), wiec okno podaza za motywem systemu.
 */
#include "kontroler.h"

namespace Grajek {

using namespace BursztynUI;

constexpr size_t MAKS_DLUGOSC_TEKSTU_GRAJKA = Kontrolka::MAKS_DLUGOSC_TEKSTU;

// Dopelnienie kolejki PCM tuz przed kosztownym rysowaniem (tabela, wizualizacja) i przed publikacja regionu
// (hak Okno::hak_rysowania). Petla BursztynUI rysuje synchronicznie, a kolejka BWS 110 to tylko 85 ms.
// Wolany jest Silnik::dopompuj(): bez ladowania plikow i otwierania strumienia (bezpieczne w trakcie rysowania).
struct PompaPcm { void (*f)(void*) = nullptr; void* kontekst = nullptr; void operator()() const { if (f) f(kontekst); } };
inline PompaPcm g_pompa_pcm;

// Tekst dopasowany do szerokosci: pelny albo obciety na granicy znaku UTF-8 z "…" (wyszukiwanie binarne,
// O(log n) pomiarow). Zwraca szerokosc wyniku w pikselach.
inline int dopasuj_tekst(const char* pelny, int szerokosc, SkalaUI skala, char* wynik, size_t cap) {
    if (!wynik || cap < 8) return 0;
    wynik[0] = '\0';
    if (!pelny || !pelny[0] || szerokosc <= 0) return 0;
    const int w = CzcionkaUI::zmierz_tekst(pelny, skala);
    if (w <= szerokosc) { kopiuj_tekst(wynik, cap, pelny); return w; }
    static const char WIELOKROPEK[] = "\xE2\x80\xA6";   // U+2026
    // granice znakow UTF-8 (najwyzej cap - 4 bajtow prefiksu)
    const size_t n = dlugosc(pelny, MAKS_DLUGOSC_TEKSTU_GRAJKA);
    size_t lo = 0, hi = n < cap - 4 ? n : cap - 4;
    while (hi > 0 && (uint8_t(pelny[hi]) & 0xC0) == 0x80) --hi;
    int najlepsza = 0;
    char proba[MAKS_DLUGOSC_TEKSTU_GRAJKA + 4];
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        while (mid > lo && (uint8_t(pelny[mid]) & 0xC0) == 0x80) --mid;
        if (mid == lo) break;
        size_t k = mid;
        while (k > 0 && pelny[k - 1] == ' ') --k;     // bez spacji przed wielokropkiem
        for (size_t i = 0; i < k; ++i) proba[i] = pelny[i];
        for (size_t i = 0; i < 3; ++i) proba[k + i] = WIELOKROPEK[i];
        proba[k + 3] = '\0';
        const int pw = CzcionkaUI::zmierz_tekst(proba, skala);
        if (pw <= szerokosc) { lo = mid; najlepsza = pw; for (size_t i = 0; i <= k + 3; ++i) wynik[i] = proba[i]; }
        else hi = mid - 1;
    }
    if (!wynik[0]) {   // nawet jeden znak sie nie miesci: sam wielokropek (moze zostac przyciety)
        for (size_t i = 0; i < 4; ++i) wynik[i] = WIELOKROPEK[i < 3 ? i : 0];
        wynik[3] = '\0';
        najlepsza = CzcionkaUI::zmierz_tekst(wynik, skala);
    }
    return najlepsza;
}

// Szerokosc tekstu w DIP (zaokraglona w gore) dla biezacej skali: kolumny i minima paneli liczone z metryk czcionki
// (natywnie TTF przez BSF), nie ze stalych pikseli dobranych pod czcionke bitmapowa hosta.
inline int dip_tekstu(const char* t, SkalaUI s, int zapas_dip = 0) {
    const int p = int(s.procent() ? s.procent() : 100);
    return (CzcionkaUI::zmierz_tekst(t, s) * 100 + p - 1) / p + zapas_dip;
}

// Jednowierszowa etykieta z wielokropkiem. Tekst pelny = Kontrolka::tekst() (podpowiedz, gdy obciety).
// Skala maksymalna (np. 2 dla tytulu) maleje do 1, potem wielokropek (SkalaUI nie schodzi ponizej 100 %).
class EtykietaJednowierszowa : public Kontrolka {
public:
    explicit EtykietaJednowierszowa(const char* t = "", int skala_maks = 1, uint32_t rola_koloru = MOTYW_KOLOR_TEKST)
        : skala_maks_(skala_maks < 1 ? 1 : skala_maks), rola_(rola_koloru) { ustaw_tekst(t); }
    void ustaw_tekst(const char* t) override {
        const bool rozne = !rowne(tekst(), t ? t : "");
        Kontrolka::ustaw_tekst(t);
        if (rozne) szer_cache_ = -1;
    }
    void ustaw_role_koloru(uint32_t r) { if (r != rola_) { rola_ = r; uniewaznij(); } }
    void ustaw_wyrownanie(WyrownaniePoziome w) { wyrownanie_ = w; uniewaznij(); }
    // Szerokosc preferowana = tekst w skali maksymalnej (np. naglowek "Grajek" dokowany w lewo).
    void ustaw_szerokosc_wg_tekstu(bool w) { wg_tekstu_ = w; }
    const char* tekst_widoczny() const { return widoczny_; }
    int skala_widoczna() const { return skala_; }
    SkalaUI skala_tekstu() const { return SkalaUI::procent(skala_dpi_.procent() * uint32_t(skala_)); }
    bool obciety() const { return obciety_; }
    Rozmiar zmierz(Rozmiar) override {
        const SkalaUI s = skala_dpi_;
        const SkalaUI duza = SkalaUI::procent(s.procent() * uint32_t(skala_maks_));
        const int h = CzcionkaUI::pobierz_wysokosc(duza) + 4 * s;
        const int w = wg_tekstu_ ? CzcionkaUI::zmierz_tekst(tekst(), duza) + 4 * s : 64 * s;
        return Rozmiar(preferowany_rozmiar_.szerokosc >= 0 ? preferowany_rozmiar_.szerokosc * s : w,
                       preferowany_rozmiar_.wysokosc >= 0 ? preferowany_rozmiar_.wysokosc * s : h);
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        if (obszar_.szerokosc != szer_cache_ || skala_dpi_.procent() != dpi_cache_) dopasuj();
        if (!widoczny_[0]) return;
        const SkalaUI s = skala_tekstu();
        const int h = CzcionkaUI::pobierz_wysokosc(s);
        int x = 0;
        if (wyrownanie_ != WyrownaniePoziome::Poczatek) {
            const int w = CzcionkaUI::zmierz_tekst(widoczny_, s);
            x = wyrownanie_ == WyrownaniePoziome::Koniec ? obszar_.szerokosc - w : (obszar_.szerokosc - w) / 2;
            if (x < 0) x = 0;
        }
        ctx.rysuj_tekst(Punkt(x, (obszar_.wysokosc - h) / 2), widoczny_, motyw_kolor(czy_wlaczona() ? rola_ : MOTYW_KOLOR_WYLACZONY), s);
    }
    // Dla testow i ukladu: wymus dopasowanie do biezacej szerokosci.
    void dopasuj() {
        szer_cache_ = obszar_.szerokosc; dpi_cache_ = skala_dpi_.procent();
        const int w = obszar_.szerokosc;
        obciety_ = false;
        for (int k = skala_maks_; k >= 1; --k) {
            skala_ = k;
            const SkalaUI s = SkalaUI::procent(skala_dpi_.procent() * uint32_t(k));
            if (CzcionkaUI::zmierz_tekst(tekst(), s) <= w || k == 1) {
                dopasuj_tekst(tekst(), w, s, widoczny_, sizeof(widoczny_));
                obciety_ = !rowne(widoczny_, tekst());
                break;
            }
        }
        ustaw_podpowiedz(obciety_ ? tekst() : "");
    }
private:
    int skala_maks_ = 1, skala_ = 1;
    uint32_t rola_ = MOTYW_KOLOR_TEKST;
    WyrownaniePoziome wyrownanie_ = WyrownaniePoziome::Poczatek;
    bool wg_tekstu_ = false;
    int szer_cache_ = -1;
    uint32_t dpi_cache_ = 0;
    bool obciety_ = false;
    char widoczny_[MAKS_DLUGOSC_TEKSTU_GRAJKA + 4]{};
};

// Panel dokowany (Lewo/Prawo) o szerokosci = procent dostepnej, w granicach [min, maks] DIP. Minimum moze dodatkowo
// pochodzic z zawartosci (funkcja liczaca szerokosc w pikselach z metryk czcionki), np. 10 kolumn korektora albo
// przyciski kolejki - przy waskim oknie panel nie zejdzie ponizej tego, co mieszcza jego teksty.
class PanelElastyczny : public PanelDokowany {
public:
    using FunkcjaMinimum = int (*)(const void* kontekst, SkalaUI s);
    PanelElastyczny(int procent = 30, int min_dip = 260, int maks_dip = 480) : procent_(procent), min_(min_dip), maks_(maks_dip) {}
    void ustaw_proporcje(int procent, int min_dip, int maks_dip) { procent_ = procent; min_ = min_dip; maks_ = maks_dip; }
    void ustaw_minimum_zawartosci(FunkcjaMinimum f, const void* kontekst) { min_f_ = f; min_ctx_ = kontekst; }
    int minimum_px() const {
        const SkalaUI s = skala_dpi_;
        int m = min_ * s;
        if (min_f_) { const int z = min_f_(min_ctx_, s) + wypelnienie().poziomo() * s; if (z > m) m = z; }   // + wlasne wypelnienie
        return m;
    }
    Rozmiar zmierz(Rozmiar dostepny) override {
        Rozmiar r = PanelDokowany::zmierz(dostepny);
        const SkalaUI s = skala_dpi_;
        int w = dostepny.szerokosc * procent_ / 100;
        const int mn = minimum_px();
        int mx = maks_ * s; if (mx < mn) mx = mn;
        if (w < mn) w = mn;
        if (w > mx) w = mx;
        r.szerokosc = w;
        return r;
    }
private:
    int procent_, min_, maks_;
    FunkcjaMinimum min_f_ = nullptr;
    const void* min_ctx_ = nullptr;
};

// Etykiety 10 pasm korektora (Hz albo dB) wyrownane do srodkow kolumn siatki suwakow (te same wypelnienie i
// odstep). Gdy najszerszy tekst nie miesci sie w kolumnie (np. "125" w TTF przy oknie 1080 px), etykiety ukladaja
// sie naprzemiennie w dwoch rzedach - kazda ma wtedy szerokosc dwoch kolumn. Bez stykania sie i bez obcinania,
// a panel nie musi byc szerszy niz 10 suwakow.
class EtykietyPasm : public Kontrolka {
public:
    static constexpr int N = Tabele::LICZBA_PASM;
    EtykietyPasm(int wypelnienie_dip, int odstep_dip, uint32_t rola) : pad_(wypelnienie_dip), gap_(odstep_dip), rola_(rola) {}
    void ustaw(int i, const char* t) {
        if (i < 0 || i >= N || rowne(t_[i], t ? t : "")) return;
        kopiuj_tekst(t_[i], sizeof(t_[i]), t ? t : "");
        uniewaznij();
    }
    const char* tekst_pasma(int i) const { return i >= 0 && i < N ? t_[i] : ""; }
    // Najszerszy tekst w pikselach (bez wielokrotnego pomiaru przy kazdym rysowaniu - teksty sa krotkie).
    int najszerszy() const {
        int w = 0;
        for (int i = 0; i < N; ++i) { const int x = CzcionkaUI::zmierz_tekst(t_[i], skala_dpi_); if (x > w) w = x; }
        return w;
    }
    int kolumna(int szerokosc) const {
        const SkalaUI s = skala_dpi_;
        const int k = (szerokosc - 2 * pad_ * s - (N - 1) * gap_ * s) / N;
        return k > 0 ? k : 1;
    }
    bool dwa_rzedy(int szerokosc) const { return najszerszy() + 4 * skala_dpi_ > kolumna(szerokosc) + gap_ * skala_dpi_; }
    bool dwa_rzedy() const { return dwa_rzedy(obszar_.szerokosc); }
    // Prostokat etykiety i (wspolrzedne kontrolki) - dla rysowania i testow.
    Prostokat obszar_etykiety(int i) const {
        const SkalaUI s = skala_dpi_;
        const int k = kolumna(obszar_.szerokosc), h = CzcionkaUI::pobierz_wysokosc(s);
        const int srodek = pad_ * s + i * (k + gap_ * s) + k / 2;
        const int w = CzcionkaUI::zmierz_tekst(t_[i], s);
        const bool dwa = dwa_rzedy();
        const int y = dwa && (i % 2) ? h + 2 * s : 0;
        return Prostokat(srodek - w / 2, y + (dwa ? 0 : (obszar_.wysokosc - h) / 2), w, h);
    }
    Rozmiar zmierz(Rozmiar dostepny) override {
        const SkalaUI s = skala_dpi_;
        const int h = CzcionkaUI::pobierz_wysokosc(s);
        const bool dwa = dostepny.szerokosc > 0 && dwa_rzedy(dostepny.szerokosc);
        return Rozmiar(-1, dwa ? 2 * h + 2 * s : h + 2 * s);
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        for (int i = 0; i < N; ++i) {
            if (!t_[i][0]) continue;
            const Prostokat r = obszar_etykiety(i);
            ctx.rysuj_tekst(Punkt(r.x, r.y), t_[i], motyw_kolor(czy_wlaczona() ? rola_ : MOTYW_KOLOR_WYLACZONY), skala_dpi_);
        }
    }
private:
    int pad_, gap_;
    uint32_t rola_;
    char t_[N][8]{};
};

// Siatka przyciskow: kolumny gwiazdkowe z waga = naturalna szerokosc przyciskow danej kolumny (tekst + ramka),
// liczona przy kazdym rozmieszczeniu z biezacej czcionki. Nadmiar miejsca dzieli sie proporcjonalnie, wiec
// dopoki siatka ma co najmniej minimum_px(), zaden tekst nie jest przyciety (rowne kolumny przycinaly "Wyczyść").
class SiatkaPrzyciskow : public Siatka {
public:
    int minimum_px() {
        const SkalaUI s = skala_dpi_;
        int w[MAKS_KOLUMN]{}; const int n = naturalne(w);
        int suma = wypelnienie().poziomo() * s + (n > 1 ? (n - 1) * odstep_poziomy() * s : 0);
        for (int k = 0; k < n; ++k) suma += w[k];
        return suma;
    }
    void rozmiesc(Prostokat r) override {
        int w[MAKS_KOLUMN]{}; const int n = naturalne(w);
        for (int k = 0; k < n; ++k) ustaw_kolumne(size_t(k), DefinicjaWymiaru::gwiazdka(w[k] > 0 ? w[k] : 1));
        Siatka::rozmiesc(r);
    }
    // Jak dodaj_do_siatki, z zapamietaniem kolumny (Siatka nie udostepnia komorek).
    void dodaj_przycisk(Kontrolka* k, int wiersz, int kolumna, int span_wierszy = 1, int span_kolumn = 1) {
        if (n_ < MAKS_KOMOREK) kom_[n_++] = {k, kolumna, span_kolumn};
        dodaj_do_siatki(k, wiersz, kolumna, span_wierszy, span_kolumn);
    }
private:
    struct Wpis { Kontrolka* k; int kolumna, span; };
    Wpis kom_[MAKS_KOMOREK]{};
    size_t n_ = 0;
    // Naturalna szerokosc kolumny: najszersza kontrolka zajmujaca tylko te kolumne; komorka laczona rozdziela swoja
    // naturalna szerokosc rowno na objete kolumny (gdy to wiecej niz ich wlasne minimum).
    int naturalne(int* w) {
        const int n = int(liczba_kolumn());
        const SkalaUI s = skala_dpi_;
        for (int faza = 0; faza < 2; ++faza)
            for (size_t i = 0; i < n_; ++i) {
                const Wpis& e = kom_[i];
                if (!e.k || e.kolumna < 0 || e.kolumna + e.span > n || (faza == 0) != (e.span == 1)) continue;
                const int nat = e.k->zmierz(Rozmiar(-1, -1)).szerokosc + e.k->margines().poziomo() * s;
                if (e.span == 1) { if (nat > w[e.kolumna]) w[e.kolumna] = nat; continue; }
                int mam = (e.span - 1) * odstep_poziomy() * s;
                for (int k = 0; k < e.span; ++k) mam += w[e.kolumna + k];
                if (nat > mam) { const int dod = (nat - mam + e.span - 1) / e.span; for (int k = 0; k < e.span; ++k) w[e.kolumna + k] += dod; }
            }
        return n;
    }
};

// Kwadratowa okladka: bok = min(wysokosc, procent szerokosci srodka) w granicach [min, maks] DIP.
class ObrazOkladki : public BursztynUI::Obraz {
public:
    explicit ObrazOkladki(int maks_dip) : maks_(maks_dip) { preferowany_rozmiar_ = Rozmiar(-1, -1); }
    // Obraz::ustaw_obraz ustawia preferowany rozmiar na wymiary bitmapy; okladka liczy bok sama (zmierz).
    void ustaw_okladke(const uint32_t* px, int w, int h, bool kopiuj) {
        ustaw_obraz(px, w, h, kopiuj);
        preferowany_rozmiar_ = Rozmiar(-1, -1);
    }
    Rozmiar zmierz(Rozmiar dostepny) override {
        const int s = skala_dpi_;
        int bok = dostepny.szerokosc * 36 / 100;
        if (bok > maks_ * s) bok = maks_ * s;
        if (bok > dostepny.wysokosc) bok = dostepny.wysokosc;
        if (bok < 72 * s) bok = 72 * s;
        return Rozmiar(bok, bok);
    }
private:
    int maks_;
};

// Pasek komunikatow: zawsze JEDEN wiersz o stalej wysokosci (wysokosc czcionki): ikona rodzaju + komunikat z
// wielokropkiem po lewej, a przy wlaczonej diagnostyce krotkie podsumowanie po prawej (szczegoly: karta
// "Diagnostyka"). Wlaczenie diagnostyki nie zmienia wysokosci, wiec sterowanie nad paskiem nie "skacze" i nie ma
// przeliczenia ukladu; zmiana tekstu uniewaznia tylko pasek.
class PasekStanuGrajka : public Kontrolka {
public:
    enum class Rodzaj : uint8_t { Informacja, Sukces, Ostrzezenie, Blad };
    using FunkcjaKlikniecia = void (*)(void*);
    void ustaw_komunikat(const char* t, Rodzaj r) {
        if (r != rodzaj_) { rodzaj_ = r; uniewaznij(); }
        if (!rowne(tekst(), t ? t : "")) { Kontrolka::ustaw_tekst(t); szer_cache_ = -1; }
    }
    Rodzaj rodzaj() const { return rodzaj_; }
    // Krotkie podsumowanie diagnostyki (pusty tekst = ukryte); nie zmienia wysokosci paska.
    void ustaw_skrot(const char* t) {
        if (rowne(skrot_, t ? t : "")) return;
        kopiuj_tekst(skrot_, sizeof(skrot_), t ? t : "");
        szer_cache_ = -1;
        uniewaznij();
    }
    const char* skrot() const { return skrot_; }
    const char* skrot_widoczny() const { return skrot_widoczny_; }
    void przy_kliknieciu_skrotu(FunkcjaKlikniecia f, void* d) { klik_ = f; klik_dane_ = d; }
    const char* tekst_widoczny() const { return widoczny_; }
    // Obszar podsumowania (wspolrzedne kontrolki) - dla testow i trafien myszy.
    Prostokat obszar_skrotu() const { return obszar_skrotu_; }
    Rozmiar zmierz(Rozmiar) override {
        const SkalaUI s = skala_dpi_;
        return Rozmiar(-1, CzcionkaUI::pobierz_wysokosc(s) + 8 * s);
    }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override {
        if (klik_ && skrot_[0] && e.czy_lewy_przycisk() && obszar_skrotu_.zawiera(e.x - obszar_ekranu_.x, e.y - obszar_ekranu_.y)) { klik_(klik_dane_); e.obsluzone = true; return; }
        Kontrolka::obsluz_wcisniecie_myszy(e);
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        const SkalaUI s = skala_dpi_;
        const Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        ctx.rysuj_zaokraglony_prostokat(b, motyw_kolor(MOTYW_KOLOR_PANEL), 4 * s);
        const int fh = CzcionkaUI::pobierz_wysokosc(s);
        const int ik = 8 * s, x0 = 8 * s, y = (obszar_.wysokosc - fh) / 2;
        if (obszar_.szerokosc != szer_cache_ || s.procent() != dpi_cache_) przelicz(s);
        if (tekst()[0]) {
            ctx.rysuj_zaokraglony_prostokat(Prostokat(x0, (obszar_.wysokosc - ik) / 2, ik, ik), motyw_kolor(kolor_rodzaju()), ik / 2);
            ctx.rysuj_tekst(Punkt(x0 + ik + 8 * s, y), widoczny_,
                            motyw_kolor(rodzaj_ == Rodzaj::Informacja ? MOTYW_KOLOR_TEKST_DRUGI : MOTYW_KOLOR_TEKST), s);
        }
        if (skrot_widoczny_[0])
            ctx.rysuj_tekst(Punkt(obszar_skrotu_.x, y), skrot_widoczny_, motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);
    }
    // Podzial szerokosci: podsumowanie najwyzej 45 % paska (wielokropek), komunikat - reszta.
    void przelicz(SkalaUI s) {
        szer_cache_ = obszar_.szerokosc; dpi_cache_ = s.procent();
        const int x0 = 8 * s, ik = 8 * s, odstep = 16 * s;
        int w_skr = 0;
        skrot_widoczny_[0] = '\0';
        if (skrot_[0]) w_skr = dopasuj_tekst(skrot_, obszar_.szerokosc * 45 / 100, s, skrot_widoczny_, sizeof(skrot_widoczny_));
        const int prawa = obszar_.szerokosc - 8 * s;
        obszar_skrotu_ = w_skr ? Prostokat(prawa - w_skr, 0, w_skr, obszar_.wysokosc) : Prostokat(prawa, 0, 0, 0);
        const int dostepne = (w_skr ? obszar_skrotu_.x - odstep : prawa) - (x0 + ik + 8 * s);
        dopasuj_tekst(tekst(), dostepne, s, widoczny_, sizeof(widoczny_));
        ustaw_podpowiedz(rowne(widoczny_, tekst()) ? (skrot_[0] ? "Szczegóły diagnostyki: kliknij podsumowanie" : "") : tekst());
    }
private:
    uint32_t kolor_rodzaju() const {
        switch (rodzaj_) {
            case Rodzaj::Sukces: return MOTYW_KOLOR_SUKCES;
            case Rodzaj::Ostrzezenie: return MOTYW_KOLOR_AKCENT;
            case Rodzaj::Blad: return MOTYW_KOLOR_BLAD;
            default: return MOTYW_KOLOR_TEKST_DRUGI;
        }
    }
    Rodzaj rodzaj_ = Rodzaj::Informacja;
    char skrot_[128]{};
    char widoczny_[MAKS_DLUGOSC_TEKSTU_GRAJKA + 4]{};
    char skrot_widoczny_[136]{};
    Prostokat obszar_skrotu_{};
    FunkcjaKlikniecia klik_ = nullptr;
    void* klik_dane_ = nullptr;
    int szer_cache_ = -1;
    uint32_t dpi_cache_ = 0;
};

// Dostawca, ktory dzieli szerokosc tabeli miedzy kolumny (wolany przy kazdym rozmieszczeniu tabeli). Szerokosci
// minimalne pochodza z pomiaru naglowkow i typowej zawartosci w biezacej czcionce (TabelaWirtualna dodaje do
// tekstu naglowka 2 x 6 DIP wypelnienia i 2 DIP), wiec przy szerokosci >= minimum_dip() naglowki sie mieszcza.
class DostawcaKolumnGrajka : public DostawcaDanychTabeli {
public:
    static constexpr int ZAPAS_NAGLOWKA_DIP = 2 * 6 + 2 + 1;
    virtual void ustaw_szerokosc_tabeli(int dip, SkalaUI s) = 0;
    virtual int minimum_dip(SkalaUI s) = 0;
    static int kolumna_dip(SkalaUI s, const char* a, const char* b = nullptr, int min_dip = 0) {
        int w = dip_tekstu(a, s, ZAPAS_NAGLOWKA_DIP);
        if (b) { const int x = dip_tekstu(b, s, ZAPAS_NAGLOWKA_DIP); if (x > w) w = x; }
        return w > min_dip ? w : min_dip;
    }
};

class TabelaGrajka : public TabelaWirtualna {
public:
    void rysuj_siebie(KontekstRysowania& ctx) override {
        g_pompa_pcm();
        PomiarSekcji p(Sekcja::Tabele);
        TabelaWirtualna::rysuj_siebie(ctx);
    }
    void ustaw_dostawce_grajka(DostawcaKolumnGrajka* d) { kolumny_ = d; ustaw_dostawce(d); }
    void rozmiesc(Prostokat r) override {
        TabelaWirtualna::rozmiesc(r);
        const SkalaUI s = skala_dpi_;
        const int p = int(s.procent() ? s.procent() : 100);
        const int dip = (obszar_.szerokosc - 12 * s - 4 * s) * 100 / p;
        const uint64_t klucz = (uint64_t(uint32_t(dip)) << 32) ^ (uint64_t(p) << 16) ^ uint64_t(CzcionkaUI::zmierz_tekst("Wykonawca", s));
        if (kolumny_ && klucz != ostatni_klucz_) {     // zmiana szerokosci, skali albo czcionki (BSF)
            ostatni_klucz_ = klucz;
            kolumny_->ustaw_szerokosc_tabeli(dip, s);
            uniewaznij_szerokosci();
        }
    }
    // Najmniejsza szerokosc tabeli w pikselach (kolumny + pasek przewijania + ramka) dla minimum panelu.
    int minimum_px() const {
        if (!kolumny_) return 0;
        const SkalaUI s = skala_dpi_;
        return kolumny_->minimum_dip(s) * s + 12 * s + 4 * s;
    }
    // Uniewaznia tylko jeden wiersz (np. wskaznik biezacego utworu) zamiast calej tabeli; od_kolumny > 0 - tylko
    // czesc wiersza od tej kolumny (np. wartosci diagnostyki bez kolumny nazw).
    void uniewaznij_wiersz(size_t w, size_t od_kolumny = 0) {
        if (!dostawca() || w >= dostawca()->liczba_wierszy()) return;
        const int rh = wysokosc_wiersza();
        const int y = poczatek_wierszy() + int(w) * rh - przewiniecie_pionowe();
        if (y + rh <= poczatek_wierszy() || y >= obszar_.wysokosc) return;
        int x = 0;
        for (size_t k = 0; k < od_kolumny && k < dostawca()->liczba_kolumn(); ++k) x += szerokosc_kolumny_px(k);
        x -= przewiniecie_poziome();
        if (x < 0) x = 0;
        if (x >= obszar_.szerokosc) return;
        uniewaznij_obszar(Prostokat(x, y, obszar_.szerokosc - x, rh));
    }
    using FunkcjaAktywacji = void (*)(TabelaGrajka*, size_t wiersz, void*);
    void przy_aktywacji(FunkcjaAktywacji cb, void* dane) { cb_ = cb; dane_ = dane; }
    void obsluz_podwojne_klikniecie(ZdarzenieMyszki& e) override {
        TabelaWirtualna::obsluz_podwojne_klikniecie(e);
        if (e.czy_lewy_przycisk() && liczba_wierszy() && cb_) { cb_(this, wybrany_wiersz(), dane_); e.obsluzone = true; }
    }
    void obsluz_wcisniecie_klawisza(ZdarzenieKlawiatury& e) override {
        const uint32_t k = e.polecenie();
        if ((k == 0x1CU || k == 0x9CU) && liczba_wierszy() && cb_) { cb_(this, wybrany_wiersz(), dane_); e.obsluzone = true; return; }   // Enter
        TabelaWirtualna::obsluz_wcisniecie_klawisza(e);
    }
private:
    FunkcjaAktywacji cb_ = nullptr;
    void* dane_ = nullptr;
    DostawcaKolumnGrajka* kolumny_ = nullptr;
    uint64_t ostatni_klucz_ = ~0ULL;
};

// Ikona stanu w pierwszej kolumnie: trojkat (gra), dwie belki (pauza), kropka (zaladowany/zatrzymany).
inline void rysuj_wskaznik(KontekstRysowania& ctx, Prostokat r, Stan s) {
    const uint32_t c = motyw_kolor(MOTYW_KOLOR_AKCENT);
    const int h = r.wysokosc < r.szerokosc ? r.wysokosc : r.szerokosc;
    const int bok = h * 3 / 5 < 4 ? 4 : h * 3 / 5;
    const int x0 = r.x + (r.szerokosc - bok) / 2, y0 = r.y + (r.wysokosc - bok) / 2;
    if (s == Stan::Odtwarzanie || s == Stan::Ladowanie || s == Stan::Przewijanie) {
        for (int y = 0; y < bok; ++y) {
            const int odl = y < bok / 2 ? y : bok - 1 - y;
            const int w = (odl * 2 * bok) / (bok > 1 ? bok : 1) + 1;
            ctx.rysuj_prostokat(Prostokat(x0, y0 + y, w > bok ? bok : w, 1), c);
        }
    } else if (s == Stan::Pauza) {
        const int b = bok / 3 < 2 ? 2 : bok / 3;
        ctx.rysuj_prostokat(Prostokat(x0, y0, b, bok), c);
        ctx.rysuj_prostokat(Prostokat(x0 + bok - b, y0, b, bok), c);
    } else {
        ctx.rysuj_zaokraglony_prostokat(Prostokat(x0 + bok / 4, y0 + bok / 4, bok / 2, bok / 2), s == Stan::Blad ? motyw_kolor(MOTYW_KOLOR_BLAD) : c, bok / 4);
    }
}

class DaneBiblioteki : public DostawcaKolumnGrajka {
public:
    explicit DaneBiblioteki(Odtwarzacz* o) : o_(o) {}
    // Wskaznik 22 DIP; Czas i Format - szerokosc naglowka lub typowej wartosci (pomiar czcionki); reszta: Tytul 56 %,
    // Wykonawca 44 % (kazda co najmniej szerokosc swojego naglowka). Ponizej minimum_dip() - przewijanie poziome.
    void ustaw_szerokosc_tabeli(int dip, SkalaUI s) override {
        s_[0] = 22;
        s_[3] = kolumna_dip(s, "Czas", "00:00", 44);
        s_[4] = kolumna_dip(s, "Format", "MP3", 48);
        const int min_t = kolumna_dip(s, "Tytuł", nullptr, 72), min_w = kolumna_dip(s, "Wykonawca");
        int reszta = dip - s_[0] - s_[3] - s_[4];
        if (reszta < min_t + min_w) reszta = min_t + min_w;
        s_[1] = reszta * 56 / 100; if (s_[1] < min_t) s_[1] = min_t;
        s_[2] = reszta - s_[1]; if (s_[2] < min_w) { s_[2] = min_w; s_[1] = reszta - min_w; }
    }
    int minimum_dip(SkalaUI s) override {
        return 22 + kolumna_dip(s, "Czas", "00:00", 44) + kolumna_dip(s, "Format", "MP3", 48) +
               kolumna_dip(s, "Tytuł", nullptr, 72) + kolumna_dip(s, "Wykonawca");
    }
    size_t liczba_wierszy() override { return o_->biblioteka().widoczne(); }
    size_t liczba_kolumn() override { return 5; }
    const char* naglowek_kolumny(size_t k) override {
        static const char* const N[5] = {"", "Tytuł", "Wykonawca", "Czas", "Format"};
        return k < 5 ? N[k] : "";
    }
    int szerokosc_kolumny(size_t k) override { return k < 5 ? s_[k] : 50; }
    const char* pobierz_komorke(size_t w, size_t k, char* buf, size_t cap) override {
        const int idx = o_->biblioteka().widoczny(uint32_t(w));
        const Utwor* u = idx >= 0 ? o_->biblioteka().utwor(uint32_t(idx)) : nullptr;
        if (!u || !cap) return "";
        Tekst t(buf, cap);
        switch (k) {
            case 1: t.dodaj(u->tytul()); break;
            case 2: t.dodaj(u->meta.wykonawca[0] ? u->meta.wykonawca : "—"); break;
            case 3: if (u->czas_ms) t.czas(u->czas_ms); else t.dodaj("?"); break;
            case 4: t.dodaj(nazwa_formatu(u->format)); break;
            default: break;
        }
        return buf;
    }
    int szerokosc_ikony_komorki(size_t k) override { return k == 0 ? 14 : 0; }
    void rysuj_ikone_komorki(KontekstRysowania& ctx, size_t w, size_t k, Prostokat r) override {
        if (k != 0) return;
        const int idx = o_->biblioteka().widoczny(uint32_t(w));
        if (idx >= 0 && idx == o_->biezacy_utwor()) rysuj_wskaznik(ctx, r, o_->silnik().stan());
    }
private:
    Odtwarzacz* o_;
    int s_[5] = {22, 170, 110, 56, 60};
};

class DaneKolejki : public DostawcaKolumnGrajka {
public:
    explicit DaneKolejki(Odtwarzacz* o) : o_(o) {}
    // Wskaznik 22 DIP; Nr i Czas - z pomiaru naglowka/wartosci; Tytul wypelnia reszte (min. 120 DIP).
    void ustaw_szerokosc_tabeli(int dip, SkalaUI s) override {
        s_[0] = 22; s_[1] = kolumna_dip(s, "Nr", "999", 30); s_[3] = kolumna_dip(s, "Czas", "00:00", 44);
        s_[2] = dip - s_[0] - s_[1] - s_[3];
        const int min_t = kolumna_dip(s, "Tytuł", nullptr, 120);
        if (s_[2] < min_t) s_[2] = min_t;
    }
    int minimum_dip(SkalaUI s) override {
        return 22 + kolumna_dip(s, "Nr", "999", 30) + kolumna_dip(s, "Czas", "00:00", 44) + kolumna_dip(s, "Tytuł", nullptr, 120);
    }
    size_t liczba_wierszy() override { return o_->kolejka().liczba(); }
    size_t liczba_kolumn() override { return 4; }
    const char* naglowek_kolumny(size_t k) override {
        static const char* const N[4] = {"", "Nr", "Tytuł", "Czas"};
        return k < 4 ? N[k] : "";
    }
    int szerokosc_kolumny(size_t k) override { return k < 4 ? s_[k] : 50; }
    const char* pobierz_komorke(size_t w, size_t k, char* buf, size_t cap) override {
        const int idx = o_->kolejka().utwor(uint32_t(w));
        const Utwor* u = idx >= 0 ? o_->biblioteka().utwor(uint32_t(idx)) : nullptr;
        if (!u || !cap) return "";
        Tekst t(buf, cap);
        switch (k) {
            case 1: t.liczba(w + 1); break;
            case 2:
                if (u->meta.wykonawca[0]) { t.dodaj(u->meta.wykonawca); t.dodaj(" – "); }
                t.dodaj(u->tytul());
                break;
            case 3: if (u->czas_ms) t.czas(u->czas_ms); else t.dodaj("?"); break;
            default: break;
        }
        return buf;
    }
    int szerokosc_ikony_komorki(size_t k) override { return k == 0 ? 14 : 0; }
    void rysuj_ikone_komorki(KontekstRysowania& ctx, size_t w, size_t k, Prostokat r) override {
        if (k == 0 && int(w) == o_->kolejka().biezaca()) rysuj_wskaznik(ctx, r, o_->silnik().stan());
    }
private:
    Odtwarzacz* o_;
    int s_[4] = {22, 34, 180, 56};
};

// Karta "Diagnostyka": wszystkie liczniki w wierszach "Pomiar | Wartosc" (bez obcinania - kolumna nazw ma
// szerokosc najdluzszej nazwy w biezacej czcionce). odswiez() porownuje z poprzednim stanem i zwraca maske
// zmienionych wierszy, wiec okno uniewaznia tylko te wiersze (raz na ~1 s i tylko przy widocznej karcie).
class DaneDiagnostyki : public DostawcaKolumnGrajka {
public:
    static constexpr size_t N = Odtwarzacz::LICZBA_POMIAROW;
    static_assert(N <= 32, "maska zmian ma 32 bity");
    explicit DaneDiagnostyki(Odtwarzacz* o) : o_(o) {}
    uint32_t odswiez() {
        uint32_t zm = 0;
        for (size_t i = 0; i < N; ++i) {
            char n[sizeof(nazwy_[0])], w[sizeof(wartosci_[0])];
            o_->pomiar(i, n, sizeof(n), w, sizeof(w));
            if (!rowne(n, nazwy_[i]) || !rowne(w, wartosci_[i])) {
                kopiuj_tekst(nazwy_[i], sizeof(nazwy_[i]), n); kopiuj_tekst(wartosci_[i], sizeof(wartosci_[i]), w);
                zm |= 1u << i;
            }
        }
        return zm;
    }
    const char* nazwa(size_t i) const { return i < N ? nazwy_[i] : ""; }
    const char* wartosc(size_t i) const { return i < N ? wartosci_[i] : ""; }
    // Najdluzsza nazwa / wartosc w DIP (z zapasem naglowka) - z biezacej czcionki, takze dla nazw nieodswiezonych.
    int szerokosc_nazw(SkalaUI s) const {
        int w = kolumna_dip(s, "Pomiar");
        for (size_t i = 0; i < N; ++i) { char n[48], v[32]; o_->pomiar(i, n, sizeof(n), v, sizeof(v)); const int x = kolumna_dip(s, n); if (x > w) w = x; }
        return w;
    }
    static int szerokosc_wartosci(SkalaUI s) { return kolumna_dip(s, "Wartość", "999,9 ms/s"); }
    void ustaw_szerokosc_tabeli(int dip, SkalaUI s) override {
        s_[0] = szerokosc_nazw(s);
        s_[1] = dip - s_[0]; const int mw = szerokosc_wartosci(s); if (s_[1] < mw) s_[1] = mw;
    }
    int minimum_dip(SkalaUI s) override { return szerokosc_nazw(s) + szerokosc_wartosci(s); }
    size_t liczba_wierszy() override { return N; }
    size_t liczba_kolumn() override { return 2; }
    const char* naglowek_kolumny(size_t k) override { return k == 0 ? "Pomiar" : (k == 1 ? "Wartość" : ""); }
    int szerokosc_kolumny(size_t k) override { return k < 2 ? s_[k] : 50; }
    const char* pobierz_komorke(size_t w, size_t k, char*, size_t) override { return k == 0 ? nazwa(w) : wartosc(w); }
private:
    Odtwarzacz* o_;
    char nazwy_[N][48]{};
    char wartosci_[N][32]{};
    int s_[2] = {180, 110};
};

class SuwakPostepu : public Suwak {
public:
    bool przeciagany() const { return przeciagany_; }
    void obsluz_wcisniecie_myszy(ZdarzenieMyszki& e) override { przeciagany_ = e.czy_lewy_przycisk(); Suwak::obsluz_wcisniecie_myszy(e); }
    void obsluz_zwolnienie_myszy(ZdarzenieMyszki& e) override { Suwak::obsluz_zwolnienie_myszy(e); przeciagany_ = false; }
private:
    bool przeciagany_ = false;
};

// Wskazniki L/P i widmo 24 pasm. Przyrostowo: odswiez() porownuje nowe wartosci z narysowanymi i uniewaznia
// tylko zmienione obszary (tor wskaznikow, pole widma). Tlo, ramka i etykiety sa rysowane tylko, gdy przecinaja
// przyciecie (pelne odrysowanie, zmiana stanu) - przy klatce wizualizacji nie sa ponownie skladane z glifow.
class WidokWizualizacji : public Kontrolka {
public:
    explicit WidokWizualizacji(const Analizator* a) : a_(a) { preferowany_rozmiar_ = Rozmiar(-1, 110); }
    void ustaw_aktywna(bool a) { if (a != aktywna_) { aktywna_ = a; uniewaznij(); } }
    bool aktywna() const { return aktywna_; }
    // Zwraca liczbe uniewaznionych obszarow (0 = brak zmian - nic nie jest rysowane ani publikowane).
    int odswiez() {
        if (!aktywna_ || !a_) return 0;
        int n = 0;
        bool poziomy = false, widmo = false;
        for (int k = 0; k < 2; ++k) if (a_->poziom(k) != poziom_[k]) poziomy = true;
        for (int b = 0; b < PASMA_WIDMA; ++b) if (a_->widmo(b) != widmo_[b]) widmo = true;
        const Geometria g = geometria();
        if (poziomy) { uniewaznij_obszar(g.wskazniki); ++n; }
        if (widmo) { uniewaznij_obszar(g.widmo); ++n; }
        return n;
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        g_pompa_pcm();
        PomiarSekcji pom(Sekcja::Wizualizacja);
        const SkalaUI s = skala_dpi_;
        const Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        const Geometria g = geometria();
        const bool tlo_calosc = !ctx.przecina_klip(g.wskazniki) || !ctx.przecina_klip(g.widmo) ||
                                rama_przecina(ctx, b, g);
        if (tlo_calosc || !aktywna_ || !a_) {
            ctx.rysuj_zaokraglony_prostokat(b, motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE), 6 * s);
            ctx.rysuj_zaokraglona_ramke(b, motyw_kolor(MOTYW_KOLOR_RAMKA), 6 * s, 1);
        }
        if (!aktywna_ || !a_) {
            ctx.rysuj_tekst_wysrodkowany(b, "Wizualizacja wyłączona", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);
            return;
        }
        // tylko obszary danych (tlo pola + slupki): klatka wizualizacji nie dotyka ramki ani etykiet
        if (ctx.przecina_klip(g.wskazniki)) {
            ctx.rysuj_prostokat(g.wskazniki, motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE));
            for (int k = 0; k < 2; ++k) {
                const Prostokat tor = g.tor[k];
                ctx.rysuj_prostokat(tor, motyw_kolor(MOTYW_KOLOR_PANEL));
                const int p = a_->poziom(k);
                poziom_[k] = p;
                const int w = tor.szerokosc * p / 100;
                if (w > 0) ctx.rysuj_prostokat(Prostokat(tor.x, tor.y, w, tor.wysokosc), p > 95 ? motyw_kolor(MOTYW_KOLOR_BLAD) : motyw_kolor(MOTYW_KOLOR_AKCENT));
            }
        }
        if (tlo_calosc) {
            for (int k = 0; k < 2; ++k)
                ctx.rysuj_tekst(Punkt(g.etykieta_x, g.tor[k].y + (g.tor[k].wysokosc - CzcionkaUI::pobierz_wysokosc(s)) / 2),
                                k == 0 ? "L" : "P", motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);
        }
        if (ctx.przecina_klip(g.widmo) && g.pas > 1) {
            ctx.rysuj_prostokat(g.widmo, motyw_kolor(MOTYW_KOLOR_POLETEKSTOWE));
            const int dol = g.widmo.dolny(), wys = g.widmo.wysokosc;
            for (int b2 = 0; b2 < PASMA_WIDMA; ++b2) {
                const int v = a_->widmo(b2);
                widmo_[b2] = v;
                const int h = wys * v / 100;
                if (h > 0) ctx.rysuj_prostokat(Prostokat(g.widmo.x + b2 * g.pas, dol - h, g.pas - 2 * s, h), motyw_kolor(b2 % 2 ? MOTYW_KOLOR_AKCENT : MOTYW_KOLOR_AKCENT_NAJECHANY));
            }
        }
    }
private:
    struct Geometria { Prostokat wskazniki, widmo, tor[2]; int etykieta_x = 0, pas = 0; };
    // Pola danych maja ograniczony rozmiar (wskazniki <= 2 x 20 DIP, widmo <= 200 DIP wysokosci i 24 DIP na pasmo),
    // wyposrodkowane w kontrolce: koszt klatki wizualizacji nie rosnie z rozmiarem okna (np. 1920x1080).
    Geometria geometria() const {
        const SkalaUI s = skala_dpi_;
        Geometria g;
        const int m = 8 * s;
        const int etyk = CzcionkaUI::zmierz_tekst("P", s) + 6 * s;
        int wys_pola = obszar_.wysokosc - 2 * m;
        if (wys_pola > 200 * s) wys_pola = 200 * s;
        if (wys_pola < 8) wys_pola = 8;
        const int y0 = (obszar_.wysokosc - wys_pola) / 2;
        int wys = wys_pola / 4;
        if (wys > 20 * s) wys = 20 * s;
        if (wys < 4) wys = 4;
        int szer = obszar_.szerokosc / 3;
        if (szer > 260 * s) szer = 260 * s;
        const int ym = y0 + (wys_pola - (2 * wys + 4 * s)) / 2;
        g.etykieta_x = m;
        for (int k = 0; k < 2; ++k) g.tor[k] = Prostokat(m + etyk, ym + k * (wys + 4 * s), szer - etyk - m > 1 ? szer - etyk - m : 1, wys);
        g.wskazniki = Prostokat(m + etyk, ym, g.tor[0].szerokosc, 2 * wys + 4 * s);
        const int x0 = szer + m, x1 = obszar_.szerokosc - m;
        g.pas = (x1 - x0) / PASMA_WIDMA;
        if (g.pas > 24 * s) g.pas = 24 * s;
        const int sz_widma = g.pas > 0 ? g.pas * PASMA_WIDMA : 1;
        const int xw = x0 + ((x1 - x0) - sz_widma) / 2;
        g.widmo = Prostokat(xw, y0, sz_widma, wys_pola);
        return g;
    }
    // Czy przyciecie obejmuje cos poza dwoma polami danych (ramka, etykiety) - wtedy rysujemy tlo calosci.
    static bool rama_przecina(const KontekstRysowania& ctx, Prostokat b, const Geometria& g) {
        const Prostokat k = ctx.lokalny_klip().przeciecie(b);
        if (k.czy_pusty()) return false;
        auto w = [](Prostokat a, Prostokat c) { return c.x >= a.x && c.y >= a.y && c.prawy() <= a.prawy() && c.dolny() <= a.dolny(); };
        return !(w(g.wskazniki, k) || w(g.widmo, k));
    }
    const Analizator* a_;
    bool aktywna_ = true;
    int poziom_[2]{-1, -1};
    int widmo_[PASMA_WIDMA]{};
};

// Domyslna okladka Grajka: bursztynowa plyta z etykieta i nuta, generowana arytmetyka calkowita.
inline uint32_t* grafika_domyslna(int bok, uint32_t akcent, uint32_t tlo) {
    if (bok < 16 || bok > 512) return nullptr;
    uint32_t* px = new uint32_t[size_t(bok) * bok];
    if (!px) return nullptr;
    const int c = bok / 2, r2 = (bok * 46 / 100) * (bok * 46 / 100), e2 = (bok * 16 / 100) * (bok * 16 / 100), d2 = (bok * 3 / 100 + 1) * (bok * 3 / 100 + 1);
    auto mieszaj = [](uint32_t a, uint32_t b, int t) {     // t: 0..256
        const uint32_t ra = (a >> 16) & 0xFF, ga = (a >> 8) & 0xFF, ba = a & 0xFF;
        const uint32_t rb = (b >> 16) & 0xFF, gb = (b >> 8) & 0xFF, bb = b & 0xFF;
        return 0xFF000000u | (((ra * uint32_t(256 - t) + rb * uint32_t(t)) >> 8) << 16) | (((ga * uint32_t(256 - t) + gb * uint32_t(t)) >> 8) << 8) |
               ((ba * uint32_t(256 - t) + bb * uint32_t(t)) >> 8);
    };
    for (int y = 0; y < bok; ++y)
        for (int x = 0; x < bok; ++x) {
            const int dx = x - c, dy = y - c, q = dx * dx + dy * dy;
            uint32_t v = 0xFF000000u | (tlo & 0xFFFFFF);
            if (q <= r2) {
                // rowki plyty: pierscienie co ~4 px
                const int rr = q * 64 / (r2 ? r2 : 1);
                v = mieszaj(0x1C1410, 0x2A1E16, (rr & 3) * 40);
                if (q <= e2) v = mieszaj(akcent, 0xFFFFFF, (dy + c) * 40 / bok);
                if (q <= d2) v = 0xFF000000u | (tlo & 0xFFFFFF);
            }
            px[size_t(y) * bok + x] = v;
        }
    // nuta (glowka + laska) w prawym gornym rogu, kolor akcentu
    const int nx = bok * 72 / 100, ny = bok * 30 / 100, nr = bok / 16 + 1;
    for (int y = -nr; y <= nr; ++y)
        for (int x = -nr - nr / 2; x <= nr + nr / 2; ++x)
            if (x * x * 4 + y * y * 9 <= nr * nr * 9 && nx + x >= 0 && nx + x < bok && ny + y >= 0 && ny + y < bok)
                px[size_t(ny + y) * bok + (nx + x)] = 0xFF000000u | (akcent & 0xFFFFFF);
    for (int y = ny - bok / 5; y <= ny; ++y)
        for (int x = nx + nr; x <= nx + nr + bok / 60 + 1; ++x)
            if (x >= 0 && x < bok && y >= 0 && y < bok) px[size_t(y) * bok + x] = 0xFF000000u | (akcent & 0xFFFFFF);
    return px;
}

} // namespace Grajek
