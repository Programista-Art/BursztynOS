#pragma once
/*
 * Bursztyn OS - uklad obrazu jadra: rozdzielenie adresow wirtualnych od fizycznego podkladu (Faza R1A).
 *
 * Architektura C + E (docs/brb/decyzja-relokacja-jadra.md): adresy WIRTUALNE jadra pozostaja stale (okno
 * obrazu od KERNEL_VMA_START = 1 MiB, ustalone przez jadro/arch/linker.ld), a FIZYCZNE polozenie obrazu moze byc
 * dowolne. Model podkladu w R1A: JEDEN CIAGLY region fizyczny zachowujacy wzgledne przesuniecia segmentow ELF
 * (PA = baza_fizyczna + (VA - baza_wirtualna)). Nie ma przetwarzania relokacji ELF.
 *
 * Zawartosc (czysta logika, bez asemblera i bez stanu globalnego - testowalna na hoscie):
 *   - Uklad: opis okna obrazu i jego podkladu + segmenty z uprawnieniami,
 *   - zbuduj_uklad(): walidacja (wyrownanie, przepelnienia, zakresy, spojnosc segmentow, W^X deklaracji),
 *   - va_na_pa()/pa_na_va(): tlumaczenie WYLACZNIE dla adresow nalezacych do okna obrazu / jego podkladu,
 *   - zaplanuj_strone_bazowa()/zaplanuj_region_2m(): plan dolnych 4 GiB bazowej przestrzeni jadra,
 *   - zbuduj_dolne_4gib(): wypelnienie PD/PT wedlug planu (uzywane przez vmm.cpp i testy hosta),
 *   - mapowanie_uzytkownika_dozwolone(): straznik izolacji Ring 3 od okna i ramek obrazu.
 *
 * Plan dolnych 4 GiB (kolejnosc regul = priorytet):
 *   1. VA [0, 4 KiB)                         -> brak mapowania (NULL),
 *   2. VA w oknie obrazu                     -> podklad fizyczny obrazu (OKNO_OBRAZU),
 *   3. VA w [PA podkladu) przy obrazie nie-tozsamosciowym -> brak mapowania (DZIURA_ALIASU: bez tego ramki jadra
 *      bylyby drugi raz widoczne pod VA == PA, z prawem zapisu, takze tekst jadra),
 *   4. pozostale VA < 4 GiB                  -> mapowanie tozsamosciowe (TOZSAMOSC), wymagane dzis przez sterowniki
 *      MMIO (BAR-y uzywane jako VA), MB2 i ACPI (ponizej 4 GiB) - patrz decyzja, sekcja zaleznosci.
 * Dla obrazu tozsamosciowego (zrodlo MB2: PA == VA) plan daje DOKLADNIE te same wpisy, ktore vmm.cpp tworzyl
 * przed R1A (PT dla pierwszych 2 MiB, strony 2 MiB w pozostalej czesci, PWT|PCD dla regionow LAPIC/IOAPIC).
 *
 * Tymczasowe ograniczenia implementacji (nie ograniczenia x86-64):
 *   - okno obrazu musi lezec w [1 MiB, 4 GiB) (jest budowane w tablicach mapy tozsamosciowej),
 *   - podklad fizyczny musi lezec w [1 MiB, 4 GiB): PMM sledzi 4 GiB, a Direct-Map (HHDM) obejmuje 4 GiB -
 *     statyczne tablice stron obrazu sa dereferencjonowane przez HHDM.
 */

#include "adresy.h"

#include <stdint.h>
#include <stddef.h>

namespace ObrazJadra {

inline constexpr uint64_t MIB = 0x100000ULL;
inline constexpr uint64_t ROZMIAR_STRONY_4K = 0x1000ULL;
inline constexpr uint64_t ROZMIAR_STRONY_2M = 0x200000ULL;
inline constexpr uint64_t LIMIT_DOLNYCH_4GIB = 0x100000000ULL;
inline constexpr uint64_t MINIMALNY_ADRES_OBRAZU = MIB;              // niski 1 MiB nalezy do firmware/legacy
inline constexpr uint64_t LIMIT_TYMCZASOWY_PODKLADU = LIMIT_DOLNYCH_4GIB;  // PMM + HHDM = 4 GiB (tymczasowe)

/* Bity PTE uzywane przez plan (Intel SDM tom 3A, 4.5, tabele 4-15..4-20). */
inline constexpr uint64_t PTE_P   = 1ULL << 0;
inline constexpr uint64_t PTE_W   = 1ULL << 1;
inline constexpr uint64_t PTE_PWT = 1ULL << 3;
inline constexpr uint64_t PTE_PCD = 1ULL << 4;
inline constexpr uint64_t PDE_PS  = 1ULL << 7;
inline constexpr uint64_t MASKA_ADRESU_4K = 0x000FFFFFFFFFF000ULL;
inline constexpr uint64_t MASKA_ADRESU_2M = 0x000FFFFFFFE00000ULL;

/* Regiony 2 MiB MMIO APIC: zachowanie zgodne z dotychczasowym vmm.cpp (PWT|PCD). */
inline constexpr uint64_t LAPIC_MMIO = 0xFEE00000ULL;
inline constexpr uint64_t IOAPIC_MMIO = 0xFEC00000ULL;

enum Uprawnienie : uint32_t {
    ODCZYT    = 1U << 0,
    ZAPIS     = 1U << 1,
    WYKONANIE = 1U << 2,
};
inline constexpr uint32_t MASKA_UPRAWNIEN = ODCZYT | ZAPIS | WYKONANIE;

struct Segment {
    uint64_t va;           // poczatek (wyrownany do 4 KiB)
    uint64_t rozmiar;      // rozpietosc w oknie (wielokrotnosc 4 KiB) - do poczatku nastepnego segmentu / konca obrazu
    uint64_t zajete;       // bajty faktycznie nalezace do segmentu (p_memsz, z BSS): 0 < zajete <= rozmiar
    uint32_t uprawnienia;  // Uprawnienie
    uint32_t zarezerwowane;
};

inline constexpr size_t MAKS_SEGMENTOW = 8;

struct Uklad {
    uint64_t va_poczatek;    // baza wirtualna okna obrazu
    uint64_t va_koniec;      // koniec (wylacznie)
    uint64_t pa_poczatek;    // baza fizyczna ciaglego podkladu
    uint64_t pa_koniec;      // koniec podkladu (wylacznie)
    uint32_t liczba_segmentow;
    uint32_t poprawny;       // 1 = zbudowany przez zbuduj_uklad() i zwalidowany
    Segment segmenty[MAKS_SEGMENTOW];
};

enum class BladUkladu : uint32_t {
    OK = 0,
    BRAK_DANYCH,
    VA_NIEWYROWNANY,
    VA_POZA_OKNEM_4GIB,
    ROZMIAR_NIEPOPRAWNY,
    PA_NIEWYROWNANY,
    PA_PONIZEJ_1MIB,
    PA_POZA_LIMITEM_ARCHITEKTURY,
    PA_POWYZEJ_4GIB_TYMCZASOWE,
    ZA_DUZO_SEGMENTOW,
    SEGMENTY_NIESPOJNE,
    SEGMENT_UPRAWNIENIA,
    SEGMENT_ZAPIS_I_WYKONANIE,
};

inline const char* opis_bledu_ukladu(BladUkladu b) {
    switch (b) {
    case BladUkladu::OK: return "OK";
    case BladUkladu::BRAK_DANYCH: return "brak danych ukladu obrazu";
    case BladUkladu::VA_NIEWYROWNANY: return "baza wirtualna obrazu niewyrownana do 4 KiB";
    case BladUkladu::VA_POZA_OKNEM_4GIB: return "okno wirtualne obrazu poza [1 MiB, 4 GiB)";
    case BladUkladu::ROZMIAR_NIEPOPRAWNY: return "rozmiar obrazu zerowy albo niewyrownany";
    case BladUkladu::PA_NIEWYROWNANY: return "baza fizyczna obrazu niewyrownana do 4 KiB";
    case BladUkladu::PA_PONIZEJ_1MIB: return "podklad fizyczny obrazu ponizej 1 MiB";
    case BladUkladu::PA_POZA_LIMITEM_ARCHITEKTURY: return "podklad fizyczny poza 52-bitowym limitem albo przepelnienie";
    case BladUkladu::PA_POWYZEJ_4GIB_TYMCZASOWE: return "podklad fizyczny powyzej 4 GiB (tymczasowe ograniczenie PMM/HHDM)";
    case BladUkladu::ZA_DUZO_SEGMENTOW: return "brak segmentow albo za duzo segmentow";
    case BladUkladu::SEGMENTY_NIESPOJNE: return "segmenty nie pokrywaja okna obrazu ciagle i rosnaco";
    case BladUkladu::SEGMENT_UPRAWNIENIA: return "segment bez prawa odczytu albo z nieznanymi bitami";
    case BladUkladu::SEGMENT_ZAPIS_I_WYKONANIE: return "segment jednoczesnie zapisywalny i wykonywalny";
    }
    return "nieznany blad ukladu";
}

/*
 * Buduje i waliduje uklad. Segmenty musza pokrywac okno [va_poczatek, va_poczatek + rozmiar) ciagle, rosnaco,
 * bez dziur i bez nakladania, kazdy wyrownany do 4 KiB. Wynik zapisywany jest tylko przy powodzeniu.
 */
inline BladUkladu zbuduj_uklad(uint64_t va_poczatek, uint64_t rozmiar, uint64_t pa_poczatek,
                               const Segment* segmenty, uint32_t liczba_segmentow, Uklad* wynik) {
    if (!wynik || !segmenty) return BladUkladu::BRAK_DANYCH;
    if (!Adresy::wyrownany_do_strony(va_poczatek)) return BladUkladu::VA_NIEWYROWNANY;
    if (rozmiar == 0 || !Adresy::wyrownany_do_strony(rozmiar)) return BladUkladu::ROZMIAR_NIEPOPRAWNY;
    uint64_t va_koniec = 0;
    if (!Adresy::dodaj_u64(va_poczatek, rozmiar, &va_koniec) || va_poczatek < MINIMALNY_ADRES_OBRAZU ||
        va_koniec > LIMIT_DOLNYCH_4GIB) {
        return BladUkladu::VA_POZA_OKNEM_4GIB;
    }
    if (!Adresy::wyrownany_do_strony(pa_poczatek)) return BladUkladu::PA_NIEWYROWNANY;
    if (pa_poczatek < MINIMALNY_ADRES_OBRAZU) return BladUkladu::PA_PONIZEJ_1MIB;
    if (!Adresy::zakres_fizyczny_poprawny(Adresy::AdresFizyczny(pa_poczatek), Adresy::RozmiarBajtow(rozmiar))) {
        return BladUkladu::PA_POZA_LIMITEM_ARCHITEKTURY;
    }
    const uint64_t pa_koniec = pa_poczatek + rozmiar;
    if (pa_koniec > LIMIT_TYMCZASOWY_PODKLADU) return BladUkladu::PA_POWYZEJ_4GIB_TYMCZASOWE;
    if (liczba_segmentow == 0 || liczba_segmentow > MAKS_SEGMENTOW) return BladUkladu::ZA_DUZO_SEGMENTOW;

    uint64_t oczekiwany = va_poczatek;
    for (uint32_t i = 0; i < liczba_segmentow; ++i) {
        const Segment& s = segmenty[i];
        uint64_t k = 0;
        if (s.va != oczekiwany || s.rozmiar == 0 || !Adresy::wyrownany_do_strony(s.va) ||
            !Adresy::wyrownany_do_strony(s.rozmiar) || !Adresy::dodaj_u64(s.va, s.rozmiar, &k) || k > va_koniec ||
            s.zajete == 0 || s.zajete > s.rozmiar) {
            return BladUkladu::SEGMENTY_NIESPOJNE;
        }
        if ((s.uprawnienia & ~MASKA_UPRAWNIEN) != 0 || (s.uprawnienia & ODCZYT) == 0) {
            return BladUkladu::SEGMENT_UPRAWNIENIA;
        }
        if ((s.uprawnienia & ZAPIS) && (s.uprawnienia & WYKONANIE)) return BladUkladu::SEGMENT_ZAPIS_I_WYKONANIE;
        oczekiwany = k;
    }
    if (oczekiwany != va_koniec) return BladUkladu::SEGMENTY_NIESPOJNE;

    Uklad u{};
    u.va_poczatek = va_poczatek;
    u.va_koniec = va_koniec;
    u.pa_poczatek = pa_poczatek;
    u.pa_koniec = pa_koniec;
    u.liczba_segmentow = liczba_segmentow;
    for (uint32_t i = 0; i < liczba_segmentow; ++i) u.segmenty[i] = segmenty[i];
    u.poprawny = 1;
    *wynik = u;
    return BladUkladu::OK;
}

inline bool tozsamosciowy(const Uklad& u) { return u.poprawny && u.pa_poczatek == u.va_poczatek; }

inline uint64_t rozmiar_obrazu(const Uklad& u) { return u.poprawny ? u.va_koniec - u.va_poczatek : 0; }

/* VA -> PA wylacznie dla adresu nalezacego do okna obrazu. */
inline bool va_na_pa(const Uklad& u, Adresy::AdresWirtualny va, Adresy::AdresFizyczny* pa) {
    if (!pa || !u.poprawny || va.wartosc < u.va_poczatek || va.wartosc >= u.va_koniec) return false;
    *pa = Adresy::AdresFizyczny(u.pa_poczatek + (va.wartosc - u.va_poczatek));
    return true;
}

/* VA -> PA dla calego zakresu [va, va+n) lezacego w oknie obrazu. */
inline bool zakres_va_na_pa(const Uklad& u, Adresy::AdresWirtualny va, Adresy::RozmiarBajtow n,
                            Adresy::AdresFizyczny* pa) {
    if (!pa || !u.poprawny ||
        !Adresy::zakres_zawiera(u.va_poczatek, u.va_koniec - u.va_poczatek, va.wartosc, n.wartosc)) {
        return false;
    }
    *pa = Adresy::AdresFizyczny(u.pa_poczatek + (va.wartosc - u.va_poczatek));
    return true;
}

/* PA -> VA wylacznie dla ramki nalezacej do podkladu obrazu (jawnie ustanowione mapowanie okna). */
inline bool pa_na_va(const Uklad& u, Adresy::AdresFizyczny pa, Adresy::AdresWirtualny* va) {
    if (!va || !u.poprawny || pa.wartosc < u.pa_poczatek || pa.wartosc >= u.pa_koniec) return false;
    *va = Adresy::AdresWirtualny(u.va_poczatek + (pa.wartosc - u.pa_poczatek));
    return true;
}

/* Offset w obrazie (wspolny dla VA i PA). */
inline bool offset_va(const Uklad& u, Adresy::AdresWirtualny va, Adresy::OffsetObrazu* o) {
    if (!o || !u.poprawny || va.wartosc < u.va_poczatek || va.wartosc >= u.va_koniec) return false;
    *o = Adresy::OffsetObrazu(va.wartosc - u.va_poczatek);
    return true;
}

/* Czy [pa, pa+n) przecina podklad fizyczny obrazu. Uklad niepoprawny = przecina (fail-closed). */
inline bool podklad_przecina(const Uklad& u, uint64_t pa, uint64_t n) {
    if (!u.poprawny) return true;
    return Adresy::zakresy_nachodza(u.pa_poczatek, u.pa_koniec - u.pa_poczatek, pa, n);
}

/*
 * Czy [pa, pa+n) przecina BAJTY nalezace do segmentow obrazu (p_memsz), a nie tylko strony podkladu. Luz strony
 * za koncem segmentu nie nalezy do obrazu w sensie ELF/Multiboot2 - program rozruchowy MB2 moze tam umiescic np. MBI
 * (GRUB 2.12 na BIOS: MBI pod 0x100080, tuz za 0x50-bajtowym segmentem .boot). Uklad niepoprawny = przecina.
 */
inline bool bajty_segmentow_przecinaja(const Uklad& u, uint64_t pa, uint64_t n) {
    if (!u.poprawny) return true;
    for (uint32_t i = 0; i < u.liczba_segmentow; ++i) {
        const Segment& s = u.segmenty[i];
        if (Adresy::zakresy_nachodza(u.pa_poczatek + (s.va - u.va_poczatek), s.zajete, pa, n)) return true;
    }
    return false;
}

/* Czy [va, va+n) przecina okno wirtualne obrazu. Uklad niepoprawny = przecina (fail-closed). */
inline bool okno_przecina(const Uklad& u, uint64_t va, uint64_t n) {
    if (!u.poprawny) return true;
    return Adresy::zakresy_nachodza(u.va_poczatek, u.va_koniec - u.va_poczatek, va, n);
}

/* Uprawnienia segmentu dla VA w oknie (0 = poza oknem). */
inline uint32_t uprawnienia_va(const Uklad& u, uint64_t va) {
    if (!u.poprawny) return 0;
    for (uint32_t i = 0; i < u.liczba_segmentow; ++i) {
        const Segment& s = u.segmenty[i];
        if (va >= s.va && va - s.va < s.rozmiar) return s.uprawnienia;
    }
    return 0;
}

/*
 * Straznik izolacji: strona uzytkownika (4 KiB pod `va`, ramka `pa`) nie moze lezec w oknie wirtualnym obrazu
 * ani wskazywac ramki podkladu obrazu. Uklad niezainstalowany = odmowa.
 */
inline bool mapowanie_uzytkownika_dozwolone(const Uklad& u, uint64_t va, uint64_t pa) {
    if (!u.poprawny) return false;
    return !okno_przecina(u, va, ROZMIAR_STRONY_4K) && !podklad_przecina(u, pa, ROZMIAR_STRONY_4K);
}

/* ------------------------------------------------------------------------------------------------------------
 * Plan dolnych 4 GiB bazowej przestrzeni jadra.
 * ---------------------------------------------------------------------------------------------------------- */

enum class RodzajStrony : uint8_t {
    BRAK_NULL = 0,
    OKNO_OBRAZU,
    DZIURA_ALIASU,
    TOZSAMOSC,
};

struct PlanStrony {
    RodzajStrony rodzaj;
    uint64_t pa;            // tylko dla OKNO_OBRAZU / TOZSAMOSC
};

inline bool strona_obecna(const PlanStrony& p) {
    return p.rodzaj == RodzajStrony::OKNO_OBRAZU || p.rodzaj == RodzajStrony::TOZSAMOSC;
}

/* `va` - adres strony 4 KiB ponizej 4 GiB; uklad musi byc poprawny (sprawdza wywolujacy). */
inline PlanStrony zaplanuj_strone_bazowa(const Uklad& u, uint64_t va) {
    if (va < ROZMIAR_STRONY_4K) return {RodzajStrony::BRAK_NULL, 0};
    if (va >= u.va_poczatek && va < u.va_koniec) {
        return {RodzajStrony::OKNO_OBRAZU, u.pa_poczatek + (va - u.va_poczatek)};
    }
    if (!tozsamosciowy(u) && va >= u.pa_poczatek && va < u.pa_koniec) return {RodzajStrony::DZIURA_ALIASU, 0};
    return {RodzajStrony::TOZSAMOSC, va};
}

enum class RodzajRegionu : uint8_t {
    PUSTY = 0,      // wpis PD = 0
    LISC_2M,        // wpis PD = strona 2 MiB pod pa_2m
    TABLICA_4K,     // wpis PD -> PT wypelniona wedlug zaplanuj_strone_bazowa()
};

struct PlanRegionu {
    RodzajRegionu rodzaj;
    uint64_t pa_2m;
};

inline bool region_przecina(uint64_t r, uint64_t p, uint64_t k) { return r < k && p < r + ROZMIAR_STRONY_2M; }

/* `va_2m` - poczatek regionu 2 MiB ponizej 4 GiB (wyrownany). */
inline PlanRegionu zaplanuj_region_2m(const Uklad& u, uint64_t va_2m) {
    const bool szczegolny = va_2m == 0 || region_przecina(va_2m, u.va_poczatek, u.va_koniec) ||
                            (!tozsamosciowy(u) && region_przecina(va_2m, u.pa_poczatek, u.pa_koniec));
    if (!szczegolny) return {RodzajRegionu::LISC_2M, va_2m};

    const PlanStrony pierwsza = zaplanuj_strone_bazowa(u, va_2m);
    bool wszystkie_brak = !strona_obecna(pierwsza);
    bool ciagle = strona_obecna(pierwsza) && (pierwsza.pa & (ROZMIAR_STRONY_2M - 1ULL)) == 0;
    for (uint64_t j = 1; j < 512; ++j) {
        const PlanStrony p = zaplanuj_strone_bazowa(u, va_2m + j * ROZMIAR_STRONY_4K);
        if (strona_obecna(p)) wszystkie_brak = false;
        if (!strona_obecna(p) || !ciagle || p.pa != pierwsza.pa + j * ROZMIAR_STRONY_4K) ciagle = false;
    }
    if (wszystkie_brak) return {RodzajRegionu::PUSTY, 0};
    if (ciagle) return {RodzajRegionu::LISC_2M, pierwsza.pa};
    return {RodzajRegionu::TABLICA_4K, 0};
}

inline uint64_t flagi_mmio_apic(uint64_t pa) {
    const uint64_t r = pa & ~(ROZMIAR_STRONY_2M - 1ULL);
    return (r == LAPIC_MMIO || r == IOAPIC_MMIO) ? (PTE_PWT | PTE_PCD) : 0ULL;
}

/*
 * R1B: czy [pa, pa+n) jest osiagalny jako PA-jako-wskaznik w bazowej przestrzeni jadra, tzn. kazda strona w planie
 * jest obecna i mapuje VA == PA (tozsamosc albo okno obrazu tozsamosciowego). Falsz dla strony 0, okna obrazu
 * nie-tozsamosciowego (VA pokazuje obraz, nie te ramke), dziury aliasu i wszystkiego >= 4 GiB.
 */
inline bool tozsamosc_bezpieczna(const Uklad& u, uint64_t pa, uint64_t n) {
    uint64_t k = 0;
    if (!u.poprawny || n == 0 || !Adresy::dodaj_u64(pa, n, &k) || k > LIMIT_DOLNYCH_4GIB) return false;
    for (uint64_t s = pa & ~(ROZMIAR_STRONY_4K - 1ULL); s < k; s += ROZMIAR_STRONY_4K) {
        const PlanStrony p = zaplanuj_strone_bazowa(u, s);
        if (!strona_obecna(p) || p.pa != s) return false;
    }
    return true;
}

/* Liczba tablic PT (poza tablica pierwszych 2 MiB), ktorych wymaga plan. */
inline size_t potrzebne_tablice_pt(const Uklad& u) {
    if (!u.poprawny) return 0;
    size_t n = 0;
    for (uint64_t r = ROZMIAR_STRONY_2M; r < LIMIT_DOLNYCH_4GIB; r += ROZMIAR_STRONY_2M) {
        if (zaplanuj_region_2m(u, r).rodzaj == RodzajRegionu::TABLICA_4K) ++n;
    }
    return n;
}

/* Tablice dolnych 4 GiB dostarczane przez wywolujacego (statyczne tablice w .bss obrazu albo bufory testu). */
struct TabliceDolne4GiB {
    uint64_t (*pd)[512];          // 4 tablice PD (po jednej na 1 GiB)
    uint64_t* pt_pierwsze_2m;     // PT dla regionu 0 (strona NULL)
    uint64_t (*pula_pt)[512];     // dodatkowe PT dla regionow TABLICA_4K
    size_t rozmiar_puli;
    size_t uzyte_pt;              // wynik
};

/*
 * Wypelnia 4 tablice PD (2048 wpisow) i potrzebne PT wedlug planu. `pa_tabeli(const uint64_t*)` zwraca fizyczny
 * adres tablicy (0 = blad). Brak miejsca w puli albo blad tlumaczenia = false (wywolujacy konczy fail-closed).
 * Wpisy PD -> PDP i PML4 tworzy wywolujacy (vmm.cpp), tez przez pa_tabeli.
 */
template <class PaTabeli>
inline bool zbuduj_dolne_4gib(const Uklad& u, TabliceDolne4GiB& t, PaTabeli pa_tabeli) {
    if (!u.poprawny || !t.pd || !t.pt_pierwsze_2m) return false;
    t.uzyte_pt = 0;
    for (uint64_t g = 0; g < 4; ++g) {
        for (uint64_t i = 0; i < 512; ++i) {
            const uint64_t r = g * 0x40000000ULL + i * ROZMIAR_STRONY_2M;
            const PlanRegionu plan = r == 0 ? PlanRegionu{RodzajRegionu::TABLICA_4K, 0} : zaplanuj_region_2m(u, r);
            if (plan.rodzaj == RodzajRegionu::PUSTY) {
                t.pd[g][i] = 0;
                continue;
            }
            if (plan.rodzaj == RodzajRegionu::LISC_2M) {
                t.pd[g][i] = (plan.pa_2m & MASKA_ADRESU_2M) | PTE_P | PTE_W | PDE_PS | flagi_mmio_apic(plan.pa_2m);
                continue;
            }
            uint64_t* pt = nullptr;
            if (r == 0) {
                pt = t.pt_pierwsze_2m;
            } else {
                if (!t.pula_pt || t.uzyte_pt >= t.rozmiar_puli) return false;
                pt = t.pula_pt[t.uzyte_pt++];
            }
            for (uint64_t j = 0; j < 512; ++j) {
                const PlanStrony p = zaplanuj_strone_bazowa(u, r + j * ROZMIAR_STRONY_4K);
                pt[j] = strona_obecna(p) ? ((p.pa & MASKA_ADRESU_4K) | PTE_P | PTE_W | flagi_mmio_apic(p.pa)) : 0ULL;
            }
            const uint64_t pa_pt = pa_tabeli(static_cast<const uint64_t*>(pt));
            if (pa_pt == 0 || (pa_pt & (ROZMIAR_STRONY_4K - 1ULL)) != 0) return false;
            t.pd[g][i] = (pa_pt & MASKA_ADRESU_4K) | PTE_P | PTE_W;
        }
    }
    return true;
}

} // namespace ObrazJadra

/*
 * Aktywny (zainstalowany przez PMM z zwalidowanego kontraktu pamieci rozruchu) uklad obrazu jadra.
 * nullptr = brak ukladu (PMM niezainicjalizowany albo kontrakt odrzucony). Implementacja: jadro/mm/pmm.cpp.
 */
const ObrazJadra::Uklad* ObrazJadraAktywny();
