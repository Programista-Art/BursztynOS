#pragma once
/*
 * Bursztyn OS - jawne typy adresow (Faza R1A).
 *
 * Lekka, wolnostojaca warstwa (bez zaleznosci od jadra, kompilowalna na hoscie), ktora rozroznia:
 *   - adres fizyczny (AdresFizyczny)        - to, co trafia do PTE, CR3 i rejestrow DMA urzadzen,
 *   - adres wirtualny (AdresWirtualny)      - to, co procesor tlumaczy przez tablice stron,
 *   - numer ramki fizycznej (NumerRamki)    - adres fizyczny / 4 KiB,
 *   - offset w obrazie jadra (OffsetObrazu) - odleglosc od poczatku obrazu, wspolna dla VA i PA,
 *   - liczbe stron i rozmiar w bajtach (LiczbaStron, RozmiarBajtow).
 *
 * Zasady:
 *   1. Typy sa silne: konstruktory sa jawne, nie ma niejawnej konwersji miedzy rodzajami adresow ani do uint64_t.
 *      Wartosc surowa jest dostepna wylacznie przez pole `wartosc` - kazde jej uzycie jest widoczne w kodzie.
 *   2. Cala arytmetyka jest sprawdzana: kazda operacja zwraca false przy przepelnieniu, niedomiarze, braku
 *      wyrownania albo wyjsciu poza dopuszczalny zakres i nie zapisuje wtedy wyniku.
 *   3. NIE ma uniwersalnej konwersji VA <-> PA. Tlumaczenie obrazu jadra realizuje obraz_jadra.h (tylko dla
 *      udokumentowanego okna obrazu), a tlumaczenie pamieci dynamicznej - Direct-Map w pmm.cpp (tylko dla
 *      zakresow zweryfikowanych w PMM).
 *   4. Reprezentacja jest pelna, 64-bitowa. Limit 52 bitow adresu fizycznego to architektoniczne maksimum
 *      MAXPHYADDR (Intel SDM tom 3A 4.1.4, AMD APM tom 2 5.3.1); konkretny procesor moze miec mniej.
 *      Ograniczenia ponizej 4 GiB (PMM, Direct-Map) sa TYMCZASOWYMI ograniczeniami implementacji i sa
 *      sprawdzane osobno, w miejscu, ktore ich wymaga - nie w tych typach.
 *
 * Koszt w czasie wykonania: zero (struktury z jednym polem uint64_t, funkcje constexpr inline).
 */

#include <stdint.h>
#include <stddef.h>

namespace Adresy {

inline constexpr uint64_t ROZMIAR_STRONY = 0x1000ULL;
inline constexpr uint64_t MASKA_STRONY = ROZMIAR_STRONY - 1ULL;
inline constexpr unsigned PRZESUNIECIE_STRONY = 12U;

/* Architektoniczne maksimum szerokosci adresu fizycznego x86-64 (MAXPHYADDR <= 52). */
inline constexpr unsigned MAKS_BITOW_FIZYCZNYCH = 52U;
inline constexpr uint64_t LIMIT_FIZYCZNY = 1ULL << MAKS_BITOW_FIZYCZNYCH;   // pierwszy niedozwolony adres

/* 4-poziomowe stronicowanie (CR4.LA57 = 0): kanoniczne adresy maja bity 63..47 rowne. */
inline constexpr uint64_t KONIEC_DOLNEJ_POLOWY = 0x0000800000000000ULL;
inline constexpr uint64_t POCZATEK_GORNEJ_POLOWY = 0xFFFF800000000000ULL;

struct AdresFizyczny {
    uint64_t wartosc;
    constexpr AdresFizyczny() : wartosc(0) {}
    constexpr explicit AdresFizyczny(uint64_t w) : wartosc(w) {}
};

struct AdresWirtualny {
    uint64_t wartosc;
    constexpr AdresWirtualny() : wartosc(0) {}
    constexpr explicit AdresWirtualny(uint64_t w) : wartosc(w) {}
};

struct NumerRamki {
    uint64_t wartosc;
    constexpr NumerRamki() : wartosc(0) {}
    constexpr explicit NumerRamki(uint64_t w) : wartosc(w) {}
};

struct OffsetObrazu {
    uint64_t wartosc;
    constexpr OffsetObrazu() : wartosc(0) {}
    constexpr explicit OffsetObrazu(uint64_t w) : wartosc(w) {}
};

struct LiczbaStron {
    uint64_t wartosc;
    constexpr LiczbaStron() : wartosc(0) {}
    constexpr explicit LiczbaStron(uint64_t w) : wartosc(w) {}
};

struct RozmiarBajtow {
    uint64_t wartosc;
    constexpr RozmiarBajtow() : wartosc(0) {}
    constexpr explicit RozmiarBajtow(uint64_t w) : wartosc(w) {}
};

static_assert(sizeof(AdresFizyczny) == 8 && sizeof(AdresWirtualny) == 8 && sizeof(NumerRamki) == 8 &&
              sizeof(OffsetObrazu) == 8 && sizeof(LiczbaStron) == 8 && sizeof(RozmiarBajtow) == 8,
              "Typy adresow musza miec dokladnie 64 bity");

template <class T>
constexpr bool rowne(T a, T b) { return a.wartosc == b.wartosc; }

/* ------------------------------------------------------------------------------------------------------------
 * Sprawdzana arytmetyka na surowych wartosciach (wspolna dla wszystkich typow).
 * ---------------------------------------------------------------------------------------------------------- */

constexpr bool dodaj_u64(uint64_t a, uint64_t b, uint64_t* wynik) {
    if (!wynik || a > UINT64_MAX - b) return false;
    *wynik = a + b;
    return true;
}

constexpr bool odejmij_u64(uint64_t a, uint64_t b, uint64_t* wynik) {
    if (!wynik || b > a) return false;
    *wynik = a - b;
    return true;
}

constexpr bool pomnoz_u64(uint64_t a, uint64_t b, uint64_t* wynik) {
    if (!wynik) return false;
    if (a != 0 && b > UINT64_MAX / a) return false;
    *wynik = a * b;
    return true;
}

constexpr bool wyrownany_do_strony(uint64_t w) { return (w & MASKA_STRONY) == 0; }

constexpr bool wyrownaj_w_gore_do_strony(uint64_t w, uint64_t* wynik) {
    if (!wynik) return false;
    if (wyrownany_do_strony(w)) { *wynik = w; return true; }
    return dodaj_u64(w & ~MASKA_STRONY, ROZMIAR_STRONY, wynik);
}

constexpr uint64_t wyrownaj_w_dol_do_strony(uint64_t w) { return w & ~MASKA_STRONY; }

/* ------------------------------------------------------------------------------------------------------------
 * Rozmiary i strony.
 * ---------------------------------------------------------------------------------------------------------- */

constexpr bool strony_na_bajty(LiczbaStron s, RozmiarBajtow* wynik) {
    uint64_t b = 0;
    if (!wynik || !pomnoz_u64(s.wartosc, ROZMIAR_STRONY, &b)) return false;
    *wynik = RozmiarBajtow(b);
    return true;
}

/* Liczba stron potrzebnych do pokrycia `n` bajtow (zaokraglenie w gore). Zero bajtow = zero stron. */
constexpr bool bajty_na_strony_w_gore(RozmiarBajtow n, LiczbaStron* wynik) {
    uint64_t z = 0;
    if (!wynik || !wyrownaj_w_gore_do_strony(n.wartosc, &z)) return false;
    *wynik = LiczbaStron(z >> PRZESUNIECIE_STRONY);
    return true;
}

/* ------------------------------------------------------------------------------------------------------------
 * Adresy fizyczne.
 * ---------------------------------------------------------------------------------------------------------- */

constexpr bool fizyczny_w_limicie(AdresFizyczny a) { return a.wartosc < LIMIT_FIZYCZNY; }

constexpr bool fizyczny_strony_poprawny(AdresFizyczny a) {
    return fizyczny_w_limicie(a) && wyrownany_do_strony(a.wartosc);
}

/* Niepusty zakres [p, p+n) bez przepelnienia i w calosci ponizej LIMIT_FIZYCZNY. */
constexpr bool zakres_fizyczny_poprawny(AdresFizyczny p, RozmiarBajtow n) {
    uint64_t k = 0;
    return n.wartosc != 0 && dodaj_u64(p.wartosc, n.wartosc, &k) && k <= LIMIT_FIZYCZNY;
}

constexpr bool przesun_fizyczny(AdresFizyczny a, RozmiarBajtow o, AdresFizyczny* wynik) {
    uint64_t w = 0;
    if (!wynik || !dodaj_u64(a.wartosc, o.wartosc, &w) || w >= LIMIT_FIZYCZNY) return false;
    *wynik = AdresFizyczny(w);
    return true;
}

constexpr bool ramka_z_adresu(AdresFizyczny a, NumerRamki* wynik) {
    if (!wynik || !fizyczny_strony_poprawny(a)) return false;
    *wynik = NumerRamki(a.wartosc >> PRZESUNIECIE_STRONY);
    return true;
}

constexpr bool adres_ramki(NumerRamki r, AdresFizyczny* wynik) {
    if (!wynik || r.wartosc >= (LIMIT_FIZYCZNY >> PRZESUNIECIE_STRONY)) return false;
    *wynik = AdresFizyczny(r.wartosc << PRZESUNIECIE_STRONY);
    return true;
}

/* ------------------------------------------------------------------------------------------------------------
 * Adresy wirtualne (4-poziomowe stronicowanie).
 * ---------------------------------------------------------------------------------------------------------- */

constexpr bool kanoniczny(AdresWirtualny a) {
    return a.wartosc < KONIEC_DOLNEJ_POLOWY || a.wartosc >= POCZATEK_GORNEJ_POLOWY;
}

constexpr bool w_dolnej_polowie(AdresWirtualny a) { return a.wartosc < KONIEC_DOLNEJ_POLOWY; }

/* Niepusty zakres [v, v+n) bez przepelnienia, w calosci w jednej polowie kanonicznej. */
constexpr bool zakres_wirtualny_poprawny(AdresWirtualny v, RozmiarBajtow n) {
    uint64_t k = 0;
    if (n.wartosc == 0 || !dodaj_u64(v.wartosc, n.wartosc, &k)) return false;
    if (v.wartosc < KONIEC_DOLNEJ_POLOWY) return k <= KONIEC_DOLNEJ_POLOWY;
    return v.wartosc >= POCZATEK_GORNEJ_POLOWY;   // gorna polowa siega do 2^64 (k == 0 wykluczone przez dodaj_u64)
}

/* ------------------------------------------------------------------------------------------------------------
 * Zakresy polotwarte [p, p+n).
 * ---------------------------------------------------------------------------------------------------------- */

/* Czy niepuste zakresy bez przepelnienia nachodza na siebie. Zakres niepoprawny = nachodzi (fail-closed). */
constexpr bool zakresy_nachodza(uint64_t p1, uint64_t n1, uint64_t p2, uint64_t n2) {
    uint64_t k1 = 0, k2 = 0;
    if (n1 == 0 || n2 == 0 || !dodaj_u64(p1, n1, &k1) || !dodaj_u64(p2, n2, &k2)) return true;
    return p1 < k2 && p2 < k1;
}

/* Czy [p2, p2+n2) zawiera sie w calosci w [p1, p1+n1). Zakres niepoprawny = nie zawiera sie. */
constexpr bool zakres_zawiera(uint64_t p1, uint64_t n1, uint64_t p2, uint64_t n2) {
    uint64_t k1 = 0, k2 = 0;
    if (n1 == 0 || n2 == 0 || !dodaj_u64(p1, n1, &k1) || !dodaj_u64(p2, n2, &k2)) return false;
    return p2 >= p1 && k2 <= k1;
}

} // namespace Adresy
