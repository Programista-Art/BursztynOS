#pragma once
#include <stddef.h>
#include <stdint.h>
#include "bursztyn_ui/czcionka.h"

// Czysta logika paska zadan (bez wywolan systemowych), wspolna dla Menedzera
// Okien i testow hosta: uklad przyciskow z przepelnieniem, bezpieczny tekst
// tytulu (UTF-8) oraz automat stanow dymka z opoznieniem.
namespace PasekZadan {

inline constexpr int MAKS_OKIEN = 64;

// Obszar przyciskow: od pierwszego przycisku do granicy zarezerwowanej przed
// zegarem (zegar i przycisk Start sa poza nim).
struct Metryki {
    int start_x = 0;        // lewa krawedz pierwszego przycisku
    int koniec_x = 0;       // pierwsza kolumna niedostepna (zegar minus odstep)
    int szer = 44;          // szerokosc przycisku okna
    int odstep = 4;         // odstep miedzy przyciskami
    int szer_przepelnienia = 28;
};

struct Uklad {
    int widoczne[MAKS_OKIEN] = {};   // indeksy okien w kolejnosci slotow
    int liczba_widocznych = 0;
    int ukryte[MAKS_OKIEN] = {};     // pozostale okna (kolejnosc paska)
    int liczba_ukrytych = 0;
    bool przepelnienie = false;
    int x_przepelnienia = 0;

    int x_slotu(const Metryki& m, int slot) const { return m.start_x + slot * (m.szer + m.odstep); }
    int slot_okna(int indeks) const {
        for (int s = 0; s < liczba_widocznych; ++s) if (widoczne[s] == indeks) return s;
        return -1;
    }
};

// Liczba przyciskow mieszczacych sie w szerokosci (bez przycisku przepelnienia).
inline int pojemnosc(const Metryki& m, int szerokosc) {
    if (szerokosc < m.szer || m.szer <= 0) return 0;
    return (szerokosc + m.odstep) / (m.szer + m.odstep);
}

// pokazywane[i] = okno ma przycisk (np. niepusty tytul); aktywne = indeks
// aktywnego okna albo -1. Gdy wszystkie przyciski sie mieszcza, nie ma
// przepelnienia. Inaczej pierwsze k okien zostaje na pasku (k z pozostalej
// szerokosci po przycisku przepelnienia), a aktywne okno spoza nich zajmuje
// ostatni widoczny slot, zeby bylo widoczne po aktywacji z listy.
inline void uloz(const Metryki& m, const bool* pokazywane, int n, int aktywne, Uklad& out) {
    out = Uklad{};
    if (n > MAKS_OKIEN) n = MAKS_OKIEN;
    int kandydaci[MAKS_OKIEN], liczba = 0;
    for (int i = 0; i < n; ++i) if (pokazywane[i]) kandydaci[liczba++] = i;
    const int szerokosc = m.koniec_x - m.start_x;
    if (liczba <= pojemnosc(m, szerokosc)) {
        for (int i = 0; i < liczba; ++i) out.widoczne[out.liczba_widocznych++] = kandydaci[i];
        return;
    }
    int k = pojemnosc(m, szerokosc - m.szer_przepelnienia - m.odstep);
    if (szerokosc - m.szer_przepelnienia - m.odstep < m.szer) k = 0;
    if (k > liczba) k = liczba;
    for (int i = 0; i < k; ++i) out.widoczne[out.liczba_widocznych++] = kandydaci[i];
    bool aktywne_widoczne = false;
    for (int i = 0; i < k; ++i) aktywne_widoczne |= kandydaci[i] == aktywne;
    bool aktywne_kandydatem = false;
    for (int i = 0; i < liczba; ++i) aktywne_kandydatem |= kandydaci[i] == aktywne;
    if (k > 0 && aktywne_kandydatem && !aktywne_widoczne) out.widoczne[k - 1] = aktywne;
    for (int i = 0; i < liczba; ++i) {
        bool widoczne = false;
        for (int s = 0; s < out.liczba_widocznych; ++s) widoczne |= out.widoczne[s] == kandydaci[i];
        if (!widoczne) out.ukryte[out.liczba_ukrytych++] = kandydaci[i];
    }
    out.przepelnienie = szerokosc >= m.szer_przepelnienia;
    out.x_przepelnienia = m.start_x + k * (m.szer + m.odstep);
}

// Kopia tytulu: tylko poprawne, kompletne sekwencje UTF-8 bez znakow
// sterujacych. Tytul z jadra moze byc uciety w srodku znaku - taki ogon
// (i wszystko po pierwszym bledzie) jest pomijany, nigdy zastepowany "?".
inline size_t bezpieczny_tytul(const char* zrodlo, size_t pojemnosc_zrodla, char* cel, size_t pojemnosc_celu) {
    if (!cel || !pojemnosc_celu) return 0;
    cel[0] = '\0';
    if (!zrodlo) return 0;
    char bufor[256];
    size_t n = 0;
    while (n < pojemnosc_zrodla && n + 1 < sizeof(bufor) && zrodlo[n]) { bufor[n] = zrodlo[n]; ++n; }
    bufor[n] = '\0';
    size_t i = 0, wyjscie = 0;
    while (i < n) {
        uint32_t cp = 0;
        const size_t uzyte = BursztynUI::CzcionkaUI::dekoduj_utf8(bufor, i, &cp);
        char wzor[5];
        const size_t dl = BursztynUI::CzcionkaUI::zakoduj_utf8(cp, wzor);
        if (!uzyte || uzyte != dl || i + uzyte > n || cp < 0x20 || cp == 0x7F ||
            (cp >= 0xD800 && cp <= 0xDFFF)) break;
        bool zgodne = true;
        for (size_t k = 0; k < uzyte; ++k) zgodne &= bufor[i + k] == wzor[k];
        if (!zgodne || wyjscie + uzyte + 1 > pojemnosc_celu) break;
        for (size_t k = 0; k < uzyte; ++k) cel[wyjscie++] = bufor[i + k];
        i += uzyte;
    }
    while (wyjscie > 0 && cel[wyjscie - 1] == ' ') --wyjscie;
    cel[wyjscie] = '\0';
    return wyjscie;
}

// Skraca tekst (w miejscu) do szerokosci w pikselach, z "…" na granicy znaku.
inline void skroc_do_szerokosci(char* tekst, size_t pojemnosc, int szer, SkalaUI s) {
    if (!tekst || pojemnosc < 4) return;
    if (BursztynUI::CzcionkaUI::zmierz_tekst(tekst, s) <= szer) return;
    size_t n = 0;
    while (tekst[n]) ++n;
    while (n > 0) {
        n = BursztynUI::CzcionkaUI::poprzedni_znak(tekst, n);
        while (n > 0 && tekst[n - 1] == ' ') --n;
        if (n + 4 > pojemnosc) continue;
        tekst[n] = char(0xE2); tekst[n + 1] = char(0x80); tekst[n + 2] = char(0xA6); tekst[n + 3] = '\0';
        if (BursztynUI::CzcionkaUI::zmierz_tekst(tekst, s) <= szer) return;
    }
}

// Polozenie dymka: nad przyciskiem (z odstepem, bez zaslaniania ikony),
// wysrodkowane i w calosci na ekranie przy lewej/prawej krawedzi.
struct Prostokat { int x = 0, y = 0, w = 0, h = 0; };
inline Prostokat polozenie_dymka(int przycisk_x, int przycisk_w, int pasek_y, int w, int h,
                                 int ekran_w, int margines, int odstep) {
    Prostokat r{przycisk_x + przycisk_w / 2 - w / 2, pasek_y - odstep - h, w, h};
    if (r.w > ekran_w - 2 * margines) r.w = ekran_w - 2 * margines;
    if (r.x + r.w > ekran_w - margines) r.x = ekran_w - margines - r.w;
    if (r.x < margines) r.x = margines;
    if (r.y < margines) r.y = margines;
    return r;
}

// Automat dymka. Czas w ns (monotoniczny). Dymek pojawia sie dopiero po
// OPOZNIENIE ns bez opuszczenia przycisku; przejscie na inny przycisk zaczyna
// odliczanie od nowa (szybkie przesuwanie nie pokazuje dymkow), ruch w obrebie
// przycisku niczego nie zmienia. Po kliknieciu dymek nie wraca, dopoki kursor
// nie opusci przycisku.
class Dymek {
public:
    static constexpr uint64_t OPOZNIENIE = 600000000ULL;

    // Przycisk pod kursorem (0 = zaden). Zwraca true, gdy trzeba ukryc dymek.
    bool najechanie(uint64_t okno, uint64_t teraz) {
        if (okno == okno_) return false;
        const bool ukryj = widoczny_;
        okno_ = okno;
        widoczny_ = false;
        zablokowany_ = false;
        termin_ = okno ? teraz + OPOZNIENIE : 0;
        return ukryj;
    }
    // Klikniecie, menu, przeciaganie, klawiatura: natychmiast ukryj i nie
    // pokazuj ponownie nad tym samym przyciskiem. Zwraca true, gdy byl widoczny.
    bool anuluj() {
        const bool ukryj = widoczny_;
        widoczny_ = false;
        termin_ = 0;
        zablokowany_ = okno_ != 0;
        return ukryj;
    }
    // Okno zniknelo (zamkniete / nowa generacja po ponownym uzyciu pid-u).
    bool okno_zniknelo(uint64_t okno) {
        if (!okno_ || okno != okno_) return false;
        const bool ukryj = widoczny_;
        okno_ = 0; widoczny_ = false; termin_ = 0; zablokowany_ = false;
        return ukryj;
    }
    // true dokladnie raz, gdy minal termin i dymek nalezy pokazac.
    bool czas_pokazac(uint64_t teraz) {
        if (!okno_ || widoczny_ || zablokowany_ || !termin_ || teraz < termin_) return false;
        widoczny_ = true;
        termin_ = 0;
        return true;
    }
    bool czeka() const { return termin_ != 0 && !widoczny_; }
    uint64_t termin() const { return termin_; }
    bool widoczny() const { return widoczny_; }
    uint64_t okno() const { return okno_; }

private:
    uint64_t okno_ = 0, termin_ = 0;
    bool widoczny_ = false, zablokowany_ = false;
};

}  // namespace PasekZadan
