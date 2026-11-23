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

// Jednowierszowa etykieta z wielokropkiem. Tekst pelny = Kontrolka::tekst() (podpowiedz, gdy obciety).
// Skala 2 (np. tytul) przechodzi na 1, gdy tekst nie miesci sie w duzej czcionce.
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
    const char* tekst_widoczny() const { return widoczny_; }
    int skala_widoczna() const { return skala_; }
    bool obciety() const { return obciety_; }
    Rozmiar zmierz(Rozmiar) override {
        const SkalaUI s = skala_dpi_;
        const int h = CzcionkaUI::pobierz_wysokosc(SkalaUI::procent(s.procent() * uint32_t(skala_maks_))) + 4 * s;
        return Rozmiar(preferowany_rozmiar_.szerokosc >= 0 ? preferowany_rozmiar_.szerokosc * s : 64 * s,
                       preferowany_rozmiar_.wysokosc >= 0 ? preferowany_rozmiar_.wysokosc * s : h);
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        if (obszar_.szerokosc != szer_cache_ || skala_dpi_.procent() != dpi_cache_) dopasuj();
        if (!widoczny_[0]) return;
        const SkalaUI s = SkalaUI::procent(skala_dpi_.procent() * uint32_t(skala_));
        const int h = CzcionkaUI::pobierz_wysokosc(s);
        ctx.rysuj_tekst(Punkt(0, (obszar_.wysokosc - h) / 2), widoczny_, motyw_kolor(czy_wlaczona() ? rola_ : MOTYW_KOLOR_WYLACZONY), s);
    }
    // Dla testow i ukladu: wymus dopasowanie do biezacej szerokosci.
    void dopasuj() {
        szer_cache_ = obszar_.szerokosc; dpi_cache_ = skala_dpi_.procent();
        skala_ = skala_maks_;
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
    int szer_cache_ = -1;
    uint32_t dpi_cache_ = 0;
    bool obciety_ = false;
    char widoczny_[MAKS_DLUGOSC_TEKSTU_GRAJKA + 4]{};
};

// Panel dokowany (Lewo/Prawo) o szerokosci = procent dostepnej, w granicach [min, maks] DIP.
class PanelElastyczny : public PanelDokowany {
public:
    PanelElastyczny(int procent = 30, int min_dip = 260, int maks_dip = 480) : procent_(procent), min_(min_dip), maks_(maks_dip) {}
    void ustaw_proporcje(int procent, int min_dip, int maks_dip) { procent_ = procent; min_ = min_dip; maks_ = maks_dip; }
    Rozmiar zmierz(Rozmiar dostepny) override {
        Rozmiar r = PanelDokowany::zmierz(dostepny);
        const int s = skala_dpi_;
        int w = dostepny.szerokosc * procent_ / 100;
        if (w < min_ * s) w = min_ * s;
        if (w > maks_ * s) w = maks_ * s;
        r.szerokosc = w;
        return r;
    }
private:
    int procent_, min_, maks_;
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

// Pasek komunikatow: staly wiersz (ikona rodzaju + tekst z wielokropkiem), opcjonalnie drugi wiersz diagnostyki.
// Widocznosc wiersza diagnostyki zmienia wysokosc (przeliczenie ukladu tylko przy przelaczeniu opcji).
class PasekStanuGrajka : public Kontrolka {
public:
    enum class Rodzaj : uint8_t { Informacja, Sukces, Ostrzezenie, Blad };
    void ustaw_komunikat(const char* t, Rodzaj r) {
        if (r != rodzaj_) { rodzaj_ = r; uniewaznij(); }
        if (!rowne(tekst(), t ? t : "")) { Kontrolka::ustaw_tekst(t); szer_cache_ = -1; }
    }
    Rodzaj rodzaj() const { return rodzaj_; }
    void ustaw_diagnostyke(const char* t) {
        if (rowne(diag_, t ? t : "")) return;
        kopiuj_tekst(diag_, sizeof(diag_), t ? t : "");
        szer_cache_ = -1;
        uniewaznij();
    }
    const char* diagnostyka() const { return diag_; }
    bool ustaw_wiersz_diagnostyki(bool w) { if (w == diag_wiersz_) return false; diag_wiersz_ = w; uniewaznij(); return true; }
    bool wiersz_diagnostyki() const { return diag_wiersz_; }
    const char* tekst_widoczny() const { return widoczny_; }
    Rozmiar zmierz(Rozmiar) override {
        const SkalaUI s = skala_dpi_;
        const int wiersz = CzcionkaUI::pobierz_wysokosc(s) + 8 * s;
        return Rozmiar(-1, diag_wiersz_ ? 2 * wiersz : wiersz);
    }
    void rysuj_siebie(KontekstRysowania& ctx) override {
        const SkalaUI s = skala_dpi_;
        const Prostokat b(0, 0, obszar_.szerokosc, obszar_.wysokosc);
        ctx.rysuj_zaokraglony_prostokat(b, motyw_kolor(MOTYW_KOLOR_PANEL), 4 * s);
        const int fh = CzcionkaUI::pobierz_wysokosc(s);
        const int wiersz = fh + 8 * s;
        const int ik = 8 * s, x0 = 8 * s;
        if (obszar_.szerokosc != szer_cache_ || s.procent() != dpi_cache_) {
            szer_cache_ = obszar_.szerokosc; dpi_cache_ = s.procent();
            const int dostepne = obszar_.szerokosc - x0 - ik - 16 * s;
            dopasuj_tekst(tekst(), dostepne, s, widoczny_, sizeof(widoczny_));
            dopasuj_tekst(diag_, obszar_.szerokosc - 16 * s, s, diag_widoczny_, sizeof(diag_widoczny_));
            ustaw_podpowiedz(rowne(widoczny_, tekst()) ? "" : tekst());
        }
        if (tekst()[0]) {
            ctx.rysuj_zaokraglony_prostokat(Prostokat(x0, (wiersz - ik) / 2, ik, ik), motyw_kolor(kolor_rodzaju()), ik / 2);
            ctx.rysuj_tekst(Punkt(x0 + ik + 8 * s, (wiersz - fh) / 2), widoczny_,
                            motyw_kolor(rodzaj_ == Rodzaj::Informacja ? MOTYW_KOLOR_TEKST_DRUGI : MOTYW_KOLOR_TEKST), s);
        }
        if (diag_wiersz_ && diag_widoczny_[0])
            ctx.rysuj_tekst(Punkt(x0, wiersz + (wiersz - fh) / 2 - 2 * s), diag_widoczny_, motyw_kolor(MOTYW_KOLOR_TEKST_DRUGI), s);
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
    char diag_[384]{};
    char widoczny_[MAKS_DLUGOSC_TEKSTU_GRAJKA + 4]{};
    char diag_widoczny_[400]{};
    bool diag_wiersz_ = false;
    int szer_cache_ = -1;
    uint32_t dpi_cache_ = 0;
};

// Dostawca, ktory dzieli szerokosc tabeli miedzy kolumny (wolany przy kazdym rozmieszczeniu tabeli).
class DostawcaKolumnGrajka : public DostawcaDanychTabeli {
public:
    virtual void ustaw_szerokosc_tabeli(int dip) = 0;
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
        const int s = skala_dpi_;
        const int dip = (obszar_.szerokosc - 12 * s - 4 * s) / (s > 0 ? s : 1);
        if (kolumny_ && dip != ostatnia_szerokosc_) {
            ostatnia_szerokosc_ = dip;
            kolumny_->ustaw_szerokosc_tabeli(dip);
            uniewaznij_szerokosci();
        }
    }
    // Uniewaznia tylko jeden wiersz (np. wskaznik biezacego utworu) zamiast calej tabeli.
    void uniewaznij_wiersz(size_t w) {
        if (!dostawca() || w >= dostawca()->liczba_wierszy()) return;
        const int rh = wysokosc_wiersza();
        const int y = poczatek_wierszy() + int(w) * rh - przewiniecie_pionowe();
        if (y + rh <= poczatek_wierszy() || y >= obszar_.wysokosc) return;
        uniewaznij_obszar(Prostokat(0, y, obszar_.szerokosc, rh));
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
    int ostatnia_szerokosc_ = -1;
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
    // Stale: wskaznik 22, Czas 52, Format 60 DIP; reszta: Tytul 56 %, Wykonawca 44 % (min. 76 / 64 DIP). Naglowki
    // nigdy nie sa wezsze niz ich tekst (TabelaWirtualna::ustaw_szerokosc_wg_naglowkow); nadmiar - przewijanie poziome.
    void ustaw_szerokosc_tabeli(int dip) override {
        int reszta = dip - 22 - 52 - 60;
        if (reszta < 140) reszta = 140;
        s_[0] = 22; s_[3] = 52; s_[4] = 60;
        s_[1] = reszta * 56 / 100; if (s_[1] < 76) s_[1] = 76;
        s_[2] = reszta - s_[1]; if (s_[2] < 64) s_[2] = 64;
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
    // Stale: wskaznik 22, Nr 34, Czas 56 DIP; Tytul wypelnia reszte (min. 120 DIP).
    void ustaw_szerokosc_tabeli(int dip) override {
        s_[0] = 22; s_[1] = 34; s_[3] = 56;
        s_[2] = dip - 22 - 34 - 56; if (s_[2] < 120) s_[2] = 120;
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
