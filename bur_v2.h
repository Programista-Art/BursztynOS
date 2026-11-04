/*
 * Bursztyn OS — Specyfikacja techniczna i walidator formatu wykonywalnego BUR v2
 * oraz ukladu przestrzeni adresowej Ring 3 ASLR (bur_v2.h).
 *
 * WAZNE:
 * Ten naglowek jest celowo odizolowany od produkcyjnego loadera BUR v1 (loader.h / loader.cpp),
 * dzieki czemu wszystkie obecne aplikacje Ring 3 (.bur v1 z magia "BUR\0") zachowuja
 * 100% kompatybilnosci binarnej, natomiast walidator, relokator i planer ASLR BUR v2
 * sa w pelni przetestowane na hoscie (ASan/UBSan) i gotowe do przyszlej migracji.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "pamiec.h"
#include "pzb.h"

/* =========================================================================
 * 1. STALE I MAGIA FORMATU BUR v2
 * ========================================================================= */

inline constexpr uint8_t BUR_V2_MAGIA_0 = 'B';
inline constexpr uint8_t BUR_V2_MAGIA_1 = 'U';
inline constexpr uint8_t BUR_V2_MAGIA_2 = 'R';
inline constexpr uint8_t BUR_V2_MAGIA_3 = 0x02U;

inline constexpr uint16_t BUR_V2_WERSJA_AKTUALNA = 2U;

/* Flagi naglowka BUR v2 */
inline constexpr uint16_t BUR_V2_FLAGA_PIE          = 1U << 0; // Obraz relokowalny (ASLR-ready)
inline constexpr uint16_t BUR_V2_FLAGA_SSP          = 1U << 1; // Wymaga inicjalizacji __stack_chk_guard
inline constexpr uint16_t BUR_V2_FLAGA_WX_SCISLY    = 1U << 2; // Scisle rozdzielenie .text (RX) i .rodata (RO+NX)
inline constexpr uint16_t BUR_V2_FLAGI_ZNANE        =
    BUR_V2_FLAGA_PIE | BUR_V2_FLAGA_SSP | BUR_V2_FLAGA_WX_SCISLY;

/* Typy relokacji BUR v2 */
enum BurV2TypRelokacji : uint32_t {
    BUR_V2_RELOC_BRAK  = 0U,
    BUR_V2_RELOC_ABS64 = 1U, // *(uint64_t*)(baza + offset) += delta_bazy
    BUR_V2_RELOC_SIEC_DANE64 = 2U, // *(uint64_t*)(baza_danych + offset) = baza_obrazu + addend
};

/* Pojedynczy wpis tablicy relokacji (24 bajty, naturalnie wyrownany) */
struct BurV2Relokacja {
    uint64_t offset_w_obrazie; // Offset wzgledem bazy wirtualnej obrazu (musi lezec w .rodata lub .data)
    int64_t  addend;           // Wartosc dodawana do bazy obrazu
    uint32_t typ;              // BurV2TypRelokacji
    uint32_t zarezerwowane;    // Musi wynosic 0
} __attribute__((packed));

static_assert(sizeof(BurV2Relokacja) == 24, "BurV2Relokacja musi miec 24 bajty");

/*
 * Naglowek binarny BUR v2 (128 bajtow):
 *   - Jawne oddzielenie .text (RX), .rodata (R-- NX), .data (RW- NX) oraz .bss (RW- NX).
 *   - Jawny rozmiar danych w pliku vs rozmiar .bss w pamieci.
 *   - Tablica relokacji dla PIE / Ring 3 ASLR.
 *   - Jawny offset zmiennej `__stack_chk_guard` wewnatrz segmentu `.data`.
 */
struct NaglowekBurV2 {
    uint8_t  magia[4];                 // 0x00: "BUR\x02"
    uint16_t wersja;                   // 0x04: 2
    uint16_t flagi;                    // 0x06: BUR_V2_FLAGA_*
    uint64_t punkt_wejscia_rva;        // 0x08: Offset punktu wejscia wzgledem bazy obrazu (w .text)

    uint64_t tekst_przesuniecie;       // 0x10: Offset .text w pliku (4 KiB aligned)
    uint64_t tekst_rozmiar;            // 0x18: Rozmiar .text (4 KiB aligned, RX)
    uint64_t tekst_rva;                // 0x20: RVA .text wzgledem bazy obrazu (4 KiB aligned)

    uint64_t rodata_przesuniecie;      // 0x28: Offset .rodata w pliku (4 KiB aligned)
    uint64_t rodata_rozmiar;           // 0x30: Rozmiar .rodata (4 KiB aligned, RO + NX, 0 jesli brak)
    uint64_t rodata_rva;               // 0x38: RVA .rodata (4 KiB aligned)

    uint64_t dane_przesuniecie;        // 0x40: Offset .data w pliku (4 KiB aligned)
    uint64_t dane_rozmiar_pliku;       // 0x48: Liczba bajtow .data kopiowanych z pliku
    uint64_t dane_rozmiar_pamieci;     // 0x50: Rozmiar .data + .bss w pamieci (4 KiB aligned, RW + NX)
    uint64_t dane_rva;                 // 0x58: RVA .data (4 KiB aligned)

    uint64_t reloc_przesuniecie;       // 0x60: Offset tablicy BurV2Relokacja w pliku
    uint32_t reloc_liczba;             // 0x68: Liczba wpisow relokacji
    uint32_t domyślne_strony_stosu;    // 0x6C: Liczba stron 4 KiB stosu (min. 4 = 16 KiB, maks. 256 = 1 MiB)

    uint64_t offset_straznika_stosu;   // 0x70: RVA 8-bajtowego __stack_chk_guard wewnatrz .data (lub 0)
    uint64_t suma_kontrolna_naglowka;  // 0x78: Rezerwacja / znacznik integralnosci (0 lub FNV-1a)
} __attribute__((packed));

static_assert(sizeof(NaglowekBurV2) == 128, "NaglowekBurV2 musi miec dokladnie 128 bajtow");

/* Limity bezpieczenstwa BUR v2 */
inline constexpr uint64_t BUR_V2_MAKS_SEGMENT_ROZMIAR = 64ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t BUR_V2_MAKS_OBRAZ_ROZMIAR   = 128ULL * 1024ULL * 1024ULL;
inline constexpr uint32_t BUR_V2_MAKS_RELOKACJI       = 65536U;

/* =========================================================================
 * 2. ARCHITEKTURA PRZESTRZENI ADRESOWEJ RING 3 ASLR
 * =========================================================================
 *
 * Okna losowania (4 KiB page-aligned, z obowiazkowymi stronami straznika P=0):
 *   - Obraz PIE (.text/.rodata/.data/.bss):
 *       Baza losowana z okna [0x0000000100000000, 0x0000000500000000)
 *       (20 bitow entropii stron 4 KiB = 1 048 576 mozliwych pozycji,
 *        calkowicie powyzej niskiego okna jadra i legacy RING3_BASE).
 *   - Sterta procesu (Heap):
 *       Baza losowana z okna [0x0000000800000000, 0x0000000C00000000)
 *       (20 bitow entropii stron 4 KiB).
 *   - Pamiec wspoldzielona (SHM):
 *       Baza losowana z okna [0x0000001000000000, 0x0000001400000000)
 *       (20 bitow entropii stron 4 KiB).
 *   - Stos uzytkownika (Stack):
 *       Szczyt losowany z okna [0x00007FFFE0000000, 0x00007FFFF0000000)
 *       (16 bitow entropii stron 4 KiB) z niezmapowana strona straznika
 *       bezposrednio ponizej dolu stosu oraz powyżej szczytu stosu.
 */

inline constexpr uint64_t ASLR_PIE_WINDOW_BASE   = 0x0000000100000000ULL; // 4 GiB
inline constexpr uint64_t ASLR_PIE_WINDOW_PAGES  = 1ULL << 20;            // 20 bitow entropii (4 GiB okno)

inline constexpr uint64_t ASLR_HEAP_WINDOW_BASE  = 0x0000000800000000ULL; // 32 GiB
inline constexpr uint64_t ASLR_HEAP_WINDOW_PAGES = 1ULL << 20;            // 20 bitow entropii (4 GiB okno)

inline constexpr uint64_t ASLR_SHM_WINDOW_BASE   = 0x0000001000000000ULL; // 64 GiB
inline constexpr uint64_t ASLR_SHM_WINDOW_PAGES  = 1ULL << 20;            // 20 bitow entropii (4 GiB okno)

inline constexpr uint64_t ASLR_STACK_WINDOW_BASE = 0x00007FFFE0000000ULL;
inline constexpr uint64_t ASLR_STACK_WINDOW_PAGES = 1ULL << 16;           // 16 bitow entropii (256 MiB okno)

struct PlanUkladuAslrRing3 {
    uint64_t baza_obrazu;
    uint64_t tekst_va;
    uint64_t rodata_va;
    uint64_t dane_va;
    uint64_t punkt_wejscia_va;
    uint64_t straznik_stosu_va; // Adres __stack_chk_guard w .data procesu (lub 0)

    uint64_t sterta_baza;
    uint64_t sterta_limit;

    uint64_t shm_baza;
    uint64_t shm_limit;

    uint64_t stos_guard_dolny;
    uint64_t stos_dol;
    uint64_t stos_szczyt;
    uint64_t stos_guard_gorny;
};

/* =========================================================================
 * 3. WALIDATOR NAGLOWKA BUR v2 (BEZSTANOWY, ODPORNY NA OVERFLOW)
 * ========================================================================= */

inline const char* bur_v2_blad_naglowka(
    const NaglowekBurV2& n,
    uint64_t rozmiar_pliku
) noexcept {
    if (rozmiar_pliku < sizeof(NaglowekBurV2)) {
        return "plik mniejszy niz NaglowekBurV2 (128 B)";
    }
    if (rozmiar_pliku > BUR_V2_MAKS_OBRAZ_ROZMIAR) {
        return "plik przekracza BUR_V2_MAKS_OBRAZ_ROZMIAR";
    }
    if (n.magia[0] != BUR_V2_MAGIA_0 ||
        n.magia[1] != BUR_V2_MAGIA_1 ||
        n.magia[2] != BUR_V2_MAGIA_2 ||
        n.magia[3] != BUR_V2_MAGIA_3) {
        return "nieprawidlowa magia BUR v2";
    }
    if (n.wersja != BUR_V2_WERSJA_AKTUALNA) {
        return "nieobslugiwana wersja naglowka BUR v2";
    }
    if ((n.flagi & ~BUR_V2_FLAGI_ZNANE) != 0) {
        return "nieznane bity flag w NaglowekBurV2";
    }

    auto wyrownane4k = [](uint64_t v) noexcept {
        return (v & 0xFFFULL) == 0ULL;
    };

    if (n.tekst_rozmiar == 0 || n.tekst_rozmiar > BUR_V2_MAKS_SEGMENT_ROZMIAR ||
        !wyrownane4k(n.tekst_przesuniecie) || !wyrownane4k(n.tekst_rozmiar) ||
        !wyrownane4k(n.tekst_rva) || n.tekst_przesuniecie < sizeof(NaglowekBurV2)) {
        return "nieprawidlowe parametry segmentu .text";
    }

    uint64_t tekst_plik_koniec = 0;
    uint64_t tekst_rva_koniec = 0;
    if (__builtin_add_overflow(n.tekst_przesuniecie, n.tekst_rozmiar, &tekst_plik_koniec) ||
        tekst_plik_koniec > rozmiar_pliku ||
        __builtin_add_overflow(n.tekst_rva, n.tekst_rozmiar, &tekst_rva_koniec)) {
        return "przepelnienie zakresu segmentu .text";
    }

    if (n.punkt_wejscia_rva < n.tekst_rva || n.punkt_wejscia_rva >= tekst_rva_koniec) {
        return "punkt_wejscia_rva poza segmentem .text";
    }

    uint64_t rodata_plik_koniec = tekst_plik_koniec;
    uint64_t rodata_rva_koniec = tekst_rva_koniec;
    if (n.rodata_rozmiar > 0) {
        if (n.rodata_rozmiar > BUR_V2_MAKS_SEGMENT_ROZMIAR ||
            !wyrownane4k(n.rodata_przesuniecie) ||
            !wyrownane4k(n.rodata_rozmiar) ||
            !wyrownane4k(n.rodata_rva) ||
            n.rodata_przesuniecie < tekst_plik_koniec ||
            n.rodata_rva < tekst_rva_koniec) {
            return "nieprawidlowe lub nakladajace sie parametry .rodata";
        }
        if (__builtin_add_overflow(n.rodata_przesuniecie, n.rodata_rozmiar, &rodata_plik_koniec) ||
            rodata_plik_koniec > rozmiar_pliku ||
            __builtin_add_overflow(n.rodata_rva, n.rodata_rozmiar, &rodata_rva_koniec)) {
            return "przepelnienie zakresu segmentu .rodata";
        }
    }

    if (n.dane_rozmiar_pamieci == 0 ||
        n.dane_rozmiar_pamieci > BUR_V2_MAKS_SEGMENT_ROZMIAR ||
        n.dane_rozmiar_pliku > n.dane_rozmiar_pamieci ||
        !wyrownane4k(n.dane_przesuniecie) ||
        !wyrownane4k(n.dane_rozmiar_pamieci) ||
        !wyrownane4k(n.dane_rva) ||
        n.dane_przesuniecie < rodata_plik_koniec ||
        n.dane_rva < rodata_rva_koniec) {
        return "nieprawidlowe lub nakladajace sie parametry .data/.bss";
    }

    uint64_t dane_plik_koniec = 0;
    uint64_t dane_rva_koniec = 0;
    if (__builtin_add_overflow(n.dane_przesuniecie, n.dane_rozmiar_pliku, &dane_plik_koniec) ||
        dane_plik_koniec > rozmiar_pliku ||
        __builtin_add_overflow(n.dane_rva, n.dane_rozmiar_pamieci, &dane_rva_koniec) ||
        dane_rva_koniec > BUR_V2_MAKS_OBRAZ_ROZMIAR) {
        return "przepelnienie zakresu segmentu .data/.bss";
    }

    if ((n.flagi & BUR_V2_FLAGA_SSP) != 0 || n.offset_straznika_stosu != 0) {
        if ((n.offset_straznika_stosu & 0x7ULL) != 0 ||
            n.offset_straznika_stosu < n.dane_rva ||
            n.offset_straznika_stosu + sizeof(uint64_t) > dane_rva_koniec) {
            return "offset_straznika_stosu poza segmentem .data lub niewyrownany do 8B";
        }
    }

    if (n.reloc_liczba > 0) {
        if (n.reloc_liczba > BUR_V2_MAKS_RELOKACJI ||
            (n.reloc_przesuniecie & 0x7ULL) != 0 ||
            n.reloc_przesuniecie < dane_plik_koniec) {
            return "nieprawidlowy naglowek tablicy relokacji";
        }
        const uint64_t bajty_reloc =
            static_cast<uint64_t>(n.reloc_liczba) * sizeof(BurV2Relokacja);
        uint64_t reloc_koniec = 0;
        if (__builtin_add_overflow(n.reloc_przesuniecie, bajty_reloc, &reloc_koniec) ||
            reloc_koniec > rozmiar_pliku) {
            return "tablica relokacji wykracza poza plik";
        }
    }

    if (n.domyślne_strony_stosu < 4U || n.domyślne_strony_stosu > 256U) {
        return "liczba stron stosu poza dozwolonym zakresem 4..256";
    }

    return nullptr;
}

inline bool bur_v2_waliduj_naglowek(
    const NaglowekBurV2& n,
    uint64_t rozmiar_pliku
) noexcept {
    return bur_v2_blad_naglowka(n, rozmiar_pliku) == nullptr;
}

/* =========================================================================
 * 4. PLANER PRZESTRZENI ADRESOWEJ RING 3 ASLR
 * ========================================================================= */

inline bool bur_v2_zaplanuj_aslr(
    const NaglowekBurV2& n,
    uint64_t los_pie,
    uint64_t los_heap,
    uint64_t los_shm,
    uint64_t los_stack,
    PlanUkladuAslrRing3* out_plan
) noexcept {
    if (!out_plan) return false;

    const uint64_t pie_slot = (los_pie & (ASLR_PIE_WINDOW_PAGES - 1ULL)) * ROZMIAR_RAMKI_4K;
    const uint64_t heap_slot = (los_heap & (ASLR_HEAP_WINDOW_PAGES - 1ULL)) * ROZMIAR_RAMKI_4K;
    const uint64_t shm_slot = (los_shm & (ASLR_SHM_WINDOW_PAGES - 1ULL)) * ROZMIAR_RAMKI_4K;
    const uint64_t stack_slot = (los_stack & (ASLR_STACK_WINDOW_PAGES - 1ULL)) * ROZMIAR_RAMKI_4K;

    PlanUkladuAslrRing3 p{};
    p.baza_obrazu = (n.flagi & BUR_V2_FLAGA_PIE)
        ? (ASLR_PIE_WINDOW_BASE + pie_slot)
        : RING3_BASE;

    p.tekst_va = p.baza_obrazu + n.tekst_rva;
    p.rodata_va = n.rodata_rozmiar ? (p.baza_obrazu + n.rodata_rva) : 0ULL;
    p.dane_va = p.baza_obrazu + n.dane_rva;
    p.punkt_wejscia_va = p.baza_obrazu + n.punkt_wejscia_rva;
    p.straznik_stosu_va = n.offset_straznika_stosu
        ? (p.baza_obrazu + n.offset_straznika_stosu)
        : 0ULL;

    p.sterta_baza = ASLR_HEAP_WINDOW_BASE + heap_slot;
    p.sterta_limit = p.sterta_baza + (256ULL * 1024ULL * 1024ULL);

    p.shm_baza = ASLR_SHM_WINDOW_BASE + shm_slot;
    p.shm_limit = p.shm_baza + (256ULL * 1024ULL * 1024ULL);

    const uint64_t rozmiar_stosu =
        static_cast<uint64_t>(n.domyślne_strony_stosu) * ROZMIAR_RAMKI_4K;
    p.stos_szczyt = ASLR_STACK_WINDOW_BASE + stack_slot + rozmiar_stosu + ROZMIAR_RAMKI_4K;
    p.stos_dol = p.stos_szczyt - rozmiar_stosu;
    p.stos_guard_dolny = p.stos_dol - ROZMIAR_RAMKI_4K;
    p.stos_guard_gorny = p.stos_szczyt;

    // Weryfikacja braku nakladania sie wszystkich okien:
    const uint64_t koniec_obrazu = p.dane_va + n.dane_rozmiar_pamieci;
    if (koniec_obrazu >= p.sterta_baza) return false;
    if (p.sterta_limit >= p.shm_baza) return false;
    if (p.shm_limit >= p.stos_guard_dolny) return false;

    *out_plan = p;
    return true;
}

/* =========================================================================
 * 5. MECHANIZM APLIKOWANIA RELOKACJI BUR v2 W SEGMENCIE .DATA
 * ========================================================================= */

inline bool bur_v2_zastosuj_relokacje_danych(
    const NaglowekBurV2& n,
    const BurV2Relokacja* relokacje,
    uint8_t* bufor_danych_pamieci,
    uint64_t baza_obrazu
) noexcept {
    if (n.reloc_liczba == 0) return true;
    if (!relokacje || !bufor_danych_pamieci) return false;

    const uint64_t dane_start = n.dane_rva;
    const uint64_t dane_koniec = n.dane_rva + n.dane_rozmiar_pamieci;

    for (uint32_t i = 0; i < n.reloc_liczba; ++i) {
        const BurV2Relokacja& r = relokacje[i];
        if (r.zarezerwowane != 0) return false;
        if ((r.offset_w_obrazie & 0x7ULL) != 0) return false;
        /* Relokacje moga modyfikowac wylacznie zapisywalny segment .data (W^X!) */
        if (r.offset_w_obrazie < dane_start ||
            r.offset_w_obrazie + sizeof(uint64_t) > dane_koniec) {
            return false;
        }
        const size_t off_w_danych = static_cast<size_t>(r.offset_w_obrazie - dane_start);
        uint64_t* cel = reinterpret_cast<uint64_t*>(bufor_danych_pamieci + off_w_danych);

        if (r.typ == BUR_V2_RELOC_ABS64 || r.typ == BUR_V2_RELOC_SIEC_DANE64) {
            const int64_t signed_base = static_cast<int64_t>(baza_obrazu);
            int64_t wynik = 0;
            if (__builtin_add_overflow(signed_base, r.addend, &wynik) || wynik < 0) {
                return false;
            }
            const uint64_t docelowy_va = static_cast<uint64_t>(wynik);
            if (docelowy_va < baza_obrazu + n.tekst_rva ||
                docelowy_va >= baza_obrazu + dane_koniec) {
                return false;
            }
            *cel = docelowy_va;
        } else {
            return false;
        }
    }
    return true;
}
