#pragma once
/*
 * Bursztyn OS - plan tablic stron przejscia dla natywnego wejscia 64-bit (Faza R1B).
 *
 * Komponent wielokrotnego uzytku (czysta logika, bez asemblera, testowalny na hoscie): buduje 4-poziomowa hierarchie,
 * pod ktora program rozruchowy (R2: BRB; R1B: wylacznie uprzaz testowa) przelacza CR3 i skacze do wejscie_jadra64.
 *
 * Mapowania (bez blanketowej tozsamosci 0..4 GiB):
 *   1. okno obrazu: VA z linkera -> ciagly podklad fizyczny, strony 4 KiB z uprawnieniami segmentow
 *      (.text R+X, .rodata R+NX, .data/.bss R+W+NX, .boot R+NX) - W^X od pierwszej instrukcji jadra,
 *   2. tozsamosc WYLACZNIE dla jawnie podanych zakresow (kod przejscia, stos przejscia, informacje rozruchu,
 *      kontrakt, mapa, moduly) z klasa uprawnien; pula ramek tablic jest mapowana tozsamosciowo R+NX (jadro czyta
 *      tablice przejscia, mierzac podklad obrazu),
 *   3. nic wiecej: strona 0 i reszta przestrzeni pozostaja niezmapowane.
 * Pierwszenstwo: kolizje sa BLEDAMI, a nie cichym nadpisaniem: zakres tozsamosci w oknie VA obrazu, zakres
 * tozsamosci na podkladzie obrazu (alias), dwa zakresy o roznych klasach na tej samej stronie, pula ramek
 * przecinajaca cokolwiek innego.
 * Wymog dla przejscia CR3 (zapewnia wywolujacy, plan to weryfikuje przez zakresy): kod wykonujacy `mov cr3`
 * i skok, aktywny stos, GDT uzywana do skoku i dane czytane tuz po przelaczeniu musza byc zmapowane pod tym samym
 * VA przed i po przelaczeniu - albo jako tozsamosc (klasa KOD_PRZEJSCIA/STOS), albo w oknie obrazu z identyczna
 * zawartoscia.
 *
 * Wszystkie ramki tablic pochodza z puli [pula_pa, +n*4 KiB) przekazanej przez wywolujacego (pamiec zdobyta
 * zgodnie z regulami wlasnosci). Kazda ramka jest walidowana (wyrownanie, szerokosc fizyczna, rozlacznosc).
 */

#include "adresy.h"
#include "obraz_jadra.h"

#include <stdint.h>
#include <stddef.h>

namespace PlanPrzejscia {

inline constexpr uint64_t PTE_P = 1ULL << 0, PTE_W = 1ULL << 1, PTE_U = 1ULL << 2, PTE_PS = 1ULL << 7;
inline constexpr uint64_t PTE_NX = 1ULL << 63;
inline constexpr uint64_t MASKA_ADRESU = 0x000FFFFFFFFFF000ULL;
inline constexpr uint64_t STRONA = 0x1000ULL;

enum Klasa : uint32_t {
    KLASA_KOD_PRZEJSCIA = 1,   // R+X
    KLASA_STOS = 2,            // R+W+NX
    KLASA_TYLKO_ODCZYT = 3,    // R+NX (informacje, kontrakt, mapa, moduly, tablice)
    KLASA_DANE = 4,            // R+W+NX (np. GDT bez ustawionych bitow accessed)
};

struct ZakresTozsamosci {
    uint64_t pa;
    uint64_t dlugosc;
    uint32_t klasa;
    uint32_t zarezerwowane;
};

struct Parametry {
    const ObrazJadra::Uklad* uklad;
    const ZakresTozsamosci* tozsamosc;
    uint32_t liczba_tozsamosci;
    bool nx;                   // CPUID NX (i EFER.NXE = 1 przed przelaczeniem)
    unsigned bity_fizyczne;    // MAXPHYADDR z CPUID 80000008h
    uint64_t pula_pa;          // PA pierwszej ramki puli (wyrownany do 4 KiB)
    uint64_t* pula_wsk;        // dostep do puli (BRB przed EBS: tozsamosc firmware; testy: bufor hosta)
    uint32_t pula_ramek;
};

enum class Blad : uint32_t {
    OK = 0,
    BRAK_DANYCH,
    UKLAD_NIEPOPRAWNY,
    SZEROKOSC_FIZYCZNA,
    PODKLAD_POZA_SZEROKOSCIA,
    PULA_NIEPOPRAWNA,
    PULA_KOLIZJA,
    PULA_WYCZERPANA,
    ZAKRES_NIEPOPRAWNY,
    ZAKRES_NIEKANONICZNY,
    ZAKRES_POZA_SZEROKOSCIA,
    ZAKRES_STRONA_ZERO,
    KOLIZJA_Z_OKNEM,
    ALIAS_PODKLADU,
    KONFLIKT_KLAS,
    KLASA_NIEZNANA,
};

inline const char* opis(Blad b) {
    switch (b) {
    case Blad::OK: return "OK";
    case Blad::BRAK_DANYCH: return "brak danych planu";
    case Blad::UKLAD_NIEPOPRAWNY: return "niepoprawny uklad obrazu";
    case Blad::SZEROKOSC_FIZYCZNA: return "nieobslugiwana szerokosc fizyczna";
    case Blad::PODKLAD_POZA_SZEROKOSCIA: return "podklad obrazu poza szerokoscia fizyczna CPU";
    case Blad::PULA_NIEPOPRAWNA: return "pula ramek niewyrownana, pusta albo poza szerokoscia";
    case Blad::PULA_KOLIZJA: return "pula ramek przecina podklad, okno albo zakres tozsamosci";
    case Blad::PULA_WYCZERPANA: return "pula ramek wyczerpana";
    case Blad::ZAKRES_NIEPOPRAWNY: return "zakres tozsamosci pusty albo z przepelnieniem";
    case Blad::ZAKRES_NIEKANONICZNY: return "zakres tozsamosci poza kanoniczna dolna polowa";
    case Blad::ZAKRES_POZA_SZEROKOSCIA: return "zakres tozsamosci poza szerokoscia fizyczna CPU";
    case Blad::ZAKRES_STRONA_ZERO: return "zakres tozsamosci obejmuje strone 0";
    case Blad::KOLIZJA_Z_OKNEM: return "zakres tozsamosci w oknie VA obrazu";
    case Blad::ALIAS_PODKLADU: return "zakres tozsamosci tworzy alias podkladu obrazu";
    case Blad::KONFLIKT_KLAS: return "rozne klasy uprawnien na tej samej stronie";
    case Blad::KLASA_NIEZNANA: return "nieznana klasa uprawnien";
    }
    return "nieznany blad planu przejscia";
}

struct Wynik {
    uint64_t cr3;
    uint32_t uzyte_ramki;
    bool wx_egzekwowane;       // false bez NX: strony zapisywalne sa wykonywalne
};

inline bool flagi_klasy(uint32_t klasa, bool nx, uint64_t* flagi) {
    const uint64_t n = nx ? PTE_NX : 0;
    switch (klasa) {
    case KLASA_KOD_PRZEJSCIA: *flagi = PTE_P; return true;
    case KLASA_STOS:
    case KLASA_DANE: *flagi = PTE_P | PTE_W | n; return true;
    case KLASA_TYLKO_ODCZYT: *flagi = PTE_P | n; return true;
    default: return false;
    }
}

/* Uprawnienia strony okna z segmentu obrazu (planowany model W^X jadra). */
inline uint64_t flagi_okna(const ObrazJadra::Uklad& u, uint64_t va, bool nx) {
    const uint32_t up = ObrazJadra::uprawnienia_va(u, va);
    uint64_t f = PTE_P;
    if (up & ObrazJadra::ZAPIS) f |= PTE_W;
    if (!(up & ObrazJadra::WYKONANIE) && nx) f |= PTE_NX;
    return f;
}

namespace detal {

struct Stan {
    const Parametry* p;
    uint32_t uzyte;
    Blad blad;
};

inline uint64_t* tabela(Stan& s, uint64_t pa) {
    const uint64_t i = (pa - s.p->pula_pa) / STRONA;
    return s.p->pula_wsk + i * 512;
}

inline bool nowa_ramka(Stan& s, uint64_t* pa) {
    if (s.uzyte >= s.p->pula_ramek) { s.blad = Blad::PULA_WYCZERPANA; return false; }
    *pa = s.p->pula_pa + static_cast<uint64_t>(s.uzyte) * STRONA;
    uint64_t* t = tabela(s, *pa);
    for (unsigned i = 0; i < 512; ++i) t[i] = 0;
    ++s.uzyte;
    return true;
}

/* Wpis PTE dla strony 4 KiB; tablice posrednie: P|W (bez U, bez NX - uprawnienia decyduja w lisciu). */
inline bool mapuj(Stan& s, uint64_t cr3, uint64_t va, uint64_t pa, uint64_t flagi, bool* juz_bylo, uint64_t* stare) {
    uint64_t t_pa = cr3;
    for (int poziom = 4; poziom > 1; --poziom) {
        uint64_t* t = tabela(s, t_pa);
        const unsigned idx = static_cast<unsigned>((va >> (12 + 9 * (poziom - 1))) & 511U);
        if (!(t[idx] & PTE_P)) {
            uint64_t nowa = 0;
            if (!nowa_ramka(s, &nowa)) return false;
            t[idx] = nowa | PTE_P | PTE_W;
        }
        t_pa = t[idx] & MASKA_ADRESU;
    }
    uint64_t* pt = tabela(s, t_pa);
    const unsigned idx = static_cast<unsigned>((va >> 12) & 511U);
    *juz_bylo = (pt[idx] & PTE_P) != 0;
    *stare = pt[idx];
    if (!*juz_bylo) pt[idx] = (pa & MASKA_ADRESU) | flagi;
    return true;
}

} // namespace detal

inline Blad zbuduj(const Parametry& p, Wynik* wynik) {
    if (!wynik || !p.uklad || !p.pula_wsk || (p.liczba_tozsamosci && !p.tozsamosc)) return Blad::BRAK_DANYCH;
    const ObrazJadra::Uklad& u = *p.uklad;
    if (!u.poprawny) return Blad::UKLAD_NIEPOPRAWNY;
    if (p.bity_fizyczne < 36 || p.bity_fizyczne > Adresy::MAKS_BITOW_FIZYCZNYCH) return Blad::SZEROKOSC_FIZYCZNA;
    const uint64_t limit = 1ULL << p.bity_fizyczne;
    if (u.pa_koniec > limit) return Blad::PODKLAD_POZA_SZEROKOSCIA;
    uint64_t pula_koniec = 0;
    if (p.pula_ramek == 0 || (p.pula_pa & (STRONA - 1)) != 0 ||
        !Adresy::dodaj_u64(p.pula_pa, static_cast<uint64_t>(p.pula_ramek) * STRONA, &pula_koniec) ||
        pula_koniec > limit || pula_koniec > Adresy::KONIEC_DOLNEJ_POLOWY || p.pula_pa == 0) {
        return Blad::PULA_NIEPOPRAWNA;
    }
    const uint64_t pula_dl = pula_koniec - p.pula_pa;
    if (ObrazJadra::podklad_przecina(u, p.pula_pa, pula_dl) || ObrazJadra::okno_przecina(u, p.pula_pa, pula_dl)) {
        return Blad::PULA_KOLIZJA;
    }
    // Walidacja zakresow tozsamosci (zaokraglanych na zewnatrz do stron).
    for (uint32_t i = 0; i < p.liczba_tozsamosci; ++i) {
        const ZakresTozsamosci& z = p.tozsamosc[i];
        uint64_t f = 0, k = 0, kw = 0;
        if (!flagi_klasy(z.klasa, p.nx, &f)) return Blad::KLASA_NIEZNANA;
        if (z.dlugosc == 0 || !Adresy::dodaj_u64(z.pa, z.dlugosc, &k) || !Adresy::wyrownaj_w_gore_do_strony(k, &kw)) {
            return Blad::ZAKRES_NIEPOPRAWNY;
        }
        const uint64_t pw = Adresy::wyrownaj_w_dol_do_strony(z.pa);
        if (pw == 0) return Blad::ZAKRES_STRONA_ZERO;
        if (kw > Adresy::KONIEC_DOLNEJ_POLOWY) return Blad::ZAKRES_NIEKANONICZNY;
        if (kw > limit) return Blad::ZAKRES_POZA_SZEROKOSCIA;
        if (ObrazJadra::okno_przecina(u, pw, kw - pw)) return Blad::KOLIZJA_Z_OKNEM;
        if (ObrazJadra::podklad_przecina(u, pw, kw - pw)) return Blad::ALIAS_PODKLADU;
        if (Adresy::zakresy_nachodza(pw, kw - pw, p.pula_pa, pula_dl)) return Blad::PULA_KOLIZJA;
    }
    detal::Stan s{&p, 0, Blad::OK};
    uint64_t cr3 = 0;
    if (!detal::nowa_ramka(s, &cr3)) return s.blad;
    bool bylo = false;
    uint64_t stare = 0;
    // 1. Okno obrazu.
    for (uint64_t va = u.va_poczatek; va < u.va_koniec; va += STRONA) {
        if (!detal::mapuj(s, cr3, va, u.pa_poczatek + (va - u.va_poczatek), flagi_okna(u, va, p.nx), &bylo, &stare)) {
            return s.blad;
        }
    }
    // 2. Zakresy tozsamosci (te same strony tylko przy identycznej klasie).
    for (uint32_t i = 0; i < p.liczba_tozsamosci; ++i) {
        const ZakresTozsamosci& z = p.tozsamosc[i];
        uint64_t f = 0, kw = 0;
        flagi_klasy(z.klasa, p.nx, &f);
        Adresy::wyrownaj_w_gore_do_strony(z.pa + z.dlugosc, &kw);
        for (uint64_t pa = Adresy::wyrownaj_w_dol_do_strony(z.pa); pa < kw; pa += STRONA) {
            if (!detal::mapuj(s, cr3, pa, pa, f, &bylo, &stare)) return s.blad;
            if (bylo && stare != ((pa & MASKA_ADRESU) | f)) return Blad::KONFLIKT_KLAS;
        }
    }
    // 3. Pula ramek tablic: tozsamosc R+NX (tablice zuzyte w trakcie budowy mapuja tez siebie).
    uint64_t f_pula = 0;
    flagi_klasy(KLASA_TYLKO_ODCZYT, p.nx, &f_pula);
    for (uint64_t pa = p.pula_pa; pa < pula_koniec; pa += STRONA) {
        if (!detal::mapuj(s, cr3, pa, pa, f_pula, &bylo, &stare)) return s.blad;
        if (bylo && stare != ((pa & MASKA_ADRESU) | f_pula)) return Blad::KONFLIKT_KLAS;
    }
    wynik->cr3 = cr3;
    wynik->uzyte_ramki = s.uzyte;
    wynik->wx_egzekwowane = p.nx;
    return Blad::OK;
}

/* ------------------------------------------------------------------------------------------------------------
 * Analiza W^X i aliasow dowolnej 4-poziomowej hierarchii (bootstrap albo symulowana przestrzen jadra).
 * `dostep(pa)` zwraca tablice (512 x u64) albo nullptr. Uprawnienia efektywne: W i U = iloczyn po poziomach,
 * NX = suma (Intel SDM t. 3A 4.6). Bez NX w CPU kazda strona jest wykonywalna (nx_aktywne = false).
 * ---------------------------------------------------------------------------------------------------------- */

struct Lisc {
    uint64_t va, pa, rozmiar;
    bool w, x, u;
};

struct RaportWx {
    bool poprawny;           // przejscie kompletne, bufor wystarczyl
    uint64_t liscie;
    uint64_t liscie_wx;      // jednoczesnie W i X
    uint64_t liscie_user;
    uint64_t aliasy_wx;      // pary lisci na wspolnej ramce: jeden W, drugi X
    uint64_t pierwszy_wx_va;
    uint64_t pierwszy_alias_pa;
};

template <class Dostep>
inline bool przejdz(uint64_t tab_pa, int poziom, uint64_t baza_va, bool w, bool u, bool nx_bit, bool nx_aktywne,
                    Dostep& dostep, Lisc* bufor, size_t pojemnosc, size_t* n, uint32_t glebokosc_kontrola) {
    const uint64_t* t = dostep(tab_pa);
    if (!t || glebokosc_kontrola > 4) return false;
    const uint64_t zasieg = 1ULL << (12 + 9 * (poziom - 1));
    for (unsigned i = 0; i < 512; ++i) {
        const uint64_t e = t[i];
        if (!(e & PTE_P)) continue;
        uint64_t va = baza_va + i * zasieg;
        if (poziom == 4 && i >= 256) va |= 0xFFFF000000000000ULL;   // rozszerzenie kanoniczne
        const bool w2 = w && (e & PTE_W), u2 = u && (e & PTE_U), nx2 = nx_bit || (nx_aktywne && (e & PTE_NX));
        const bool lisc = poziom == 1 || ((poziom == 2 || poziom == 3) && (e & PTE_PS));
        if (lisc) {
            if (*n >= pojemnosc) return false;
            const uint64_t maska = (poziom == 1) ? MASKA_ADRESU : (MASKA_ADRESU & ~(zasieg - 1));
            bufor[(*n)++] = {va, e & maska, zasieg, w2, !nx2, u2};
            continue;
        }
        if (!przejdz(e & MASKA_ADRESU, poziom - 1, va, w2, u2, nx2, nx_aktywne, dostep, bufor, pojemnosc, n,
                     glebokosc_kontrola + 1)) {
            return false;
        }
    }
    return true;
}

inline void sortuj_po_pa(Lisc* a, size_t n) {   // heapsort: O(n log n), bez alokacji
    auto przesiej = [&](size_t i, size_t m) {
        for (;;) {
            size_t c = 2 * i + 1;
            if (c >= m) return;
            if (c + 1 < m && a[c + 1].pa > a[c].pa) ++c;
            if (a[i].pa >= a[c].pa) return;
            const Lisc t = a[i]; a[i] = a[c]; a[c] = t;
            i = c;
        }
    };
    for (size_t i = n / 2; i-- > 0;) przesiej(i, n);
    for (size_t k = n; k-- > 1;) { const Lisc t = a[0]; a[0] = a[k]; a[k] = t; przesiej(0, k); }
}

template <class Dostep>
inline RaportWx analizuj(uint64_t cr3, bool nx_aktywne, Dostep dostep, Lisc* bufor, size_t pojemnosc) {
    RaportWx r{};
    size_t n = 0;
    r.poprawny = przejdz(cr3 & MASKA_ADRESU, 4, 0, true, true, false, nx_aktywne, dostep, bufor, pojemnosc, &n, 1);
    r.liscie = n;
    for (size_t i = 0; i < n; ++i) {
        if (bufor[i].w && bufor[i].x) { if (!r.liscie_wx) r.pierwszy_wx_va = bufor[i].va; ++r.liscie_wx; }
        if (bufor[i].u) ++r.liscie_user;
    }
    sortuj_po_pa(bufor, n);
    // Aliasy: dla kazdego liscia sprawdz kolejne lisci nachodzace fizycznie (posortowane po poczatku PA).
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n && bufor[j].pa < bufor[i].pa + bufor[i].rozmiar; ++j) {
            const bool wx = (bufor[i].w && bufor[j].x) || (bufor[i].x && bufor[j].w);
            if (wx) { if (!r.aliasy_wx) r.pierwszy_alias_pa = bufor[j].pa; ++r.aliasy_wx; }
        }
    }
    return r;
}

} // namespace PlanPrzejscia
