/*
 * Bursztyn OS - Bezpieczny rozruch i stan slotow A/B (aktualizacja_rozruch.h).
 *
 * Czysty model stanu rozruchu i partycji/slotow:
 *   - dwa sloty logiczne: Slot A i Slot B,
 *   - stany logiczne slotu: nieaktywny, aktywny, oczekujacy, potwierdzony, nieudany,
 *   - rekord 512 bajtow chroniony suma CRC32,
 *   - dwie niezalezne kopie stanu (generacja + CRC32) chroniace przed torn writes
 *     i nagla utrata zasilania (power loss),
 *   - scisly limit prob rozruchu (domyslnie 1 proba przed automatycznym rollbackiem).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROZRUCH_MAGIC                0x5354414EU /* "STAN" */
#define ROZRUCH_WERSJA_FORMATU       1U
#define ROZRUCH_DOMYSLNY_LIMIT_PROB  1U
#define ROZRUCH_SEKTOR_KOPIA_0       2046ULL
#define ROZRUCH_SEKTOR_KOPIA_1       2047ULL

/* Identyfikator slotu systemu. */
typedef enum bursztyn_slot {
    BURSZTYN_SLOT_A = 0,
    BURSZTYN_SLOT_B = 1,
    BURSZTYN_SLOT_NIEZNANY = 0xFF
} bursztyn_slot_t;

/* Stan logiczny slotu w cyklu zycia aktualizacji. */
typedef enum bursztyn_stan_slotu {
    BURSZTYN_STAN_NIEAKTYWNY    = 0,
    BURSZTYN_STAN_AKTYWNY       = 1,
    BURSZTYN_STAN_OCZEKUJACY    = 2,
    BURSZTYN_STAN_POTWIERDZONY  = 3,
    BURSZTYN_STAN_NIEUDANY      = 4
} bursztyn_stan_slotu_t;

/* Wynik operacji na stanie rozruchu. */
typedef enum bursztyn_rozruch_wynik {
    ROZRUCH_OK = 0,
    ROZRUCH_BLAD_ARGUMENT = 1,
    ROZRUCH_BLAD_MAGIC = 2,
    ROZRUCH_BLAD_WERSJA = 3,
    ROZRUCH_BLAD_CRC = 4,
    ROZRUCH_BLAD_SLOT = 5,
    ROZRUCH_BLAD_STAN = 6,
    ROZRUCH_BLAD_USZKODZONE_OBIE_KOPIE = 7
} bursztyn_rozruch_wynik_t;

/*
 * Rekord stanu rozruchu: dokladnie 512 bajtow (1 sektor dyskowy).
 * Zapisywany w dwoch kopiach z rosnacym licznikiem generacji i CRC32.
 */
struct bursztyn_rozruch_stan {
    uint32_t magic;                 /* 0x5354414E ("STAN") */
    uint32_t wersja_formatu;        /* 1 */
    uint64_t generacja;             /* Monotoniczny licznik zmian stanu */
    uint8_t  slot_potwierdzony;     /* BURSZTYN_SLOT_A lub BURSZTYN_SLOT_B */
    uint8_t  slot_oczekujacy;       /* BURSZTYN_SLOT_A, BURSZTYN_SLOT_B lub BURSZTYN_SLOT_NIEZNANY */
    uint8_t  stan_oczekujacego;     /* bursztyn_stan_slotu_t */
    uint8_t  licznik_prob;          /* Liczba wykonanych prob uruchomienia slotu oczekujacego */
    uint8_t  maks_prob;             /* Maksymalna dozwolona liczba prob (np. 1) */
    uint8_t  zarezerwowane[3];

    /* Potwierdzona wersja systemu */
    uint32_t potwierdzona_major;
    uint32_t potwierdzona_minor;
    uint32_t potwierdzona_patch;

    /* Oczekujaca wersja systemu (w trakcie aktualizacji) */
    uint32_t oczekujaca_major;
    uint32_t oczekujaca_minor;
    uint32_t oczekujaca_patch;

    /* Skroty SHA-256 jader dla obu slotow */
    uint8_t  sha256_jadro_a[32];
    uint8_t  sha256_jadro_b[32];

    /* Pole wolne pod przyszle rozszerzenia ABI (wyzerowane) */
    uint8_t  padding[396];

    /* Suma kontrolna CRC32 liczona nad pierwszymi 508 bajtami */
    uint32_t crc32;
} __attribute__((packed));

#ifdef __cplusplus
static_assert(sizeof(struct bursztyn_rozruch_stan) == 512,
              "Struktura bursztyn_rozruch_stan musi miec dokladnie 512 bajtow");
#endif

/*
 * Implementacja CRC32 (standard IEEE 802.3 / 0xEDB88320).
 */
static inline uint32_t rozruch_oblicz_crc32(const void* dane, size_t n) {
    uint32_t c = ~0U;
    const uint8_t* b = (const uint8_t*)dane;
    while (n--) {
        c ^= *b++;
        for (unsigned i = 0; i < 8; ++i) {
            c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
        }
    }
    return ~c;
}

/*
 * Sprawdza poprawnosc strukturalna i integralnosc rekordu stanu.
 */
static inline bool rozruch_waliduj_rekord(const struct bursztyn_rozruch_stan* s) {
    if (!s) return false;
    if (s->magic != ROZRUCH_MAGIC) return false;
    if (s->wersja_formatu != ROZRUCH_WERSJA_FORMATU) return false;
    if (s->slot_potwierdzony != BURSZTYN_SLOT_A && s->slot_potwierdzony != BURSZTYN_SLOT_B) return false;
    if (s->slot_oczekujacy != BURSZTYN_SLOT_A &&
        s->slot_oczekujacy != BURSZTYN_SLOT_B &&
        s->slot_oczekujacy != BURSZTYN_SLOT_NIEZNANY) return false;
    if (s->stan_oczekujacego > BURSZTYN_STAN_NIEUDANY) return false;
    if (s->maks_prob == 0 || s->maks_prob > 10) return false;
    if (s->licznik_prob > s->maks_prob + 1) return false;

    uint32_t oczekiwane_crc = rozruch_oblicz_crc32(s, offsetof(struct bursztyn_rozruch_stan, crc32));
    return s->crc32 == oczekiwane_crc;
}

/*
 * Aktualizuje sume CRC32 w rekordzie.
 */
static inline void rozruch_podpisz_rekord(struct bursztyn_rozruch_stan* s) {
    if (!s) return;
    s->crc32 = rozruch_oblicz_crc32(s, offsetof(struct bursztyn_rozruch_stan, crc32));
}

/*
 * Tworzy poczatkowy stan dla czystej instalacji (Slot A potwierdzony, Slot B nieaktywny).
 */
static inline void rozruch_stan_poczatkowy(struct bursztyn_rozruch_stan* s,
                                          uint32_t major, uint32_t minor, uint32_t patch) {
    if (!s) return;
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < sizeof(*s); ++i) p[i] = 0;

    s->magic = ROZRUCH_MAGIC;
    s->wersja_formatu = ROZRUCH_WERSJA_FORMATU;
    s->generacja = 1;
    s->slot_potwierdzony = BURSZTYN_SLOT_A;
    s->slot_oczekujacy = BURSZTYN_SLOT_NIEZNANY;
    s->stan_oczekujacego = BURSZTYN_STAN_NIEAKTYWNY;
    s->licznik_prob = 0;
    s->maks_prob = ROZRUCH_DOMYSLNY_LIMIT_PROB;

    s->potwierdzona_major = major;
    s->potwierdzona_minor = minor;
    s->potwierdzona_patch = patch;

    s->oczekujaca_major = 0;
    s->oczekujaca_minor = 0;
    s->oczekujaca_patch = 0;

    rozruch_podpisz_rekord(s);
}

/*
 * Wybiera poprawna kopie sposrod dwoch kopii redundantnych.
 * W przypadku gdy obie sa poprawne, wybiera kopie z wyzsza generacja.
 * Zwraca false tylko gdy obie kopie sa uszkodzone.
 */
static inline bool rozruch_wybierz_poprawna_kopie(const struct bursztyn_rozruch_stan* k0,
                                                  const struct bursztyn_rozruch_stan* k1,
                                                  struct bursztyn_rozruch_stan* wybrana) {
    bool poprawna_0 = rozruch_waliduj_rekord(k0);
    bool poprawna_1 = rozruch_waliduj_rekord(k1);

    if (!poprawna_0 && !poprawna_1) return false;
    if (!wybrana) return true;

    if (poprawna_0 && !poprawna_1) {
        *wybrana = *k0;
        return true;
    }
    if (!poprawna_0 && poprawna_1) {
        *wybrana = *k1;
        return true;
    }
    /* Obie poprawne - wyzsza generacja wygrywa. */
    if (k1->generacja > k0->generacja) {
        *wybrana = *k1;
    } else {
        *wybrana = *k0;
    }
    return true;
}

/*
 * Przygotowuje przejscie stanu na slot oczekujacy (po weryfikacji pakietu .akt).
 * Podbija licznik generacji i resetuje licznik prob.
 */
static inline bool rozruch_ustaw_oczekujacy(struct bursztyn_rozruch_stan* s,
                                            bursztyn_slot_t docelowy_slot,
                                            uint32_t major, uint32_t minor, uint32_t patch,
                                            const uint8_t sha_jadra[32]) {
    if (!s || !rozruch_waliduj_rekord(s)) return false;
    if (docelowy_slot != BURSZTYN_SLOT_A && docelowy_slot != BURSZTYN_SLOT_B) return false;
    if (docelowy_slot == s->slot_potwierdzony) return false; /* Zakaz pisania po potwierdzonym */

    s->generacja += 1;
    s->slot_oczekujacy = (uint8_t)docelowy_slot;
    s->stan_oczekujacego = BURSZTYN_STAN_OCZEKUJACY;
    s->licznik_prob = 0;
    s->oczekujaca_major = major;
    s->oczekujaca_minor = minor;
    s->oczekujaca_patch = patch;

    if (sha_jadra) {
        uint8_t* dst = (docelowy_slot == BURSZTYN_SLOT_A) ? s->sha256_jadro_a : s->sha256_jadro_b;
        for (int i = 0; i < 32; ++i) dst[i] = sha_jadra[i];
    }

    rozruch_podpisz_rekord(s);
    return true;
}

/*
 * Odnotowuje probe rozruchu slotu oczekujacego.
 * Jesli licznik prob osiagnie maks_prob, oznacza slot jako nieudany.
 */
static inline bool rozruch_odnotuj_probe(struct bursztyn_rozruch_stan* s) {
    if (!s || !rozruch_waliduj_rekord(s)) return false;
    if (s->slot_oczekujacy == BURSZTYN_SLOT_NIEZNANY ||
        s->stan_oczekujacego != BURSZTYN_STAN_OCZEKUJACY) {
        return false;
    }

    s->generacja += 1;
    s->licznik_prob += 1;
    if (s->licznik_prob >= s->maks_prob) {
        s->stan_oczekujacego = BURSZTYN_STAN_NIEUDANY;
    }
    rozruch_podpisz_rekord(s);
    return true;
}

/*
 * Potwierdza pomyslne uruchomienie nowego slotu po przejsciu punktu kontrolnego.
 * Nowy slot staje sie slotem potwierdzonym, a dawny przechodzi w stan nieaktywny.
 */
static inline bool rozruch_potwierdz_sukces(struct bursztyn_rozruch_stan* s,
                                            bursztyn_slot_t uruchomiony_slot) {
    if (!s || !rozruch_waliduj_rekord(s)) return false;
    if (uruchomiony_slot != BURSZTYN_SLOT_A && uruchomiony_slot != BURSZTYN_SLOT_B) return false;
    if (s->slot_oczekujacy != uruchomiony_slot) return false;
    if (s->stan_oczekujacego != BURSZTYN_STAN_OCZEKUJACY) return false;

    s->generacja += 1;
    s->slot_potwierdzony = (uint8_t)uruchomiony_slot;
    s->slot_oczekujacy = BURSZTYN_SLOT_NIEZNANY;
    s->stan_oczekujacego = BURSZTYN_STAN_POTWIERDZONY;
    s->licznik_prob = 0;

    s->potwierdzona_major = s->oczekujaca_major;
    s->potwierdzona_minor = s->oczekujaca_minor;
    s->potwierdzona_patch = s->oczekujaca_patch;

    rozruch_podpisz_rekord(s);
    return true;
}

/*
 * Wykonuje automatyczny powrot (rollback) do poprzedniego potwierdzonego slotu.
 * Dozwolone specyficznie po nieudanym rozruchu nowego slotu (nawet jesli
 * potwierdzona wersja jest numerycznie nizsza - ochrona przed petla restartow).
 */
static inline bool rozruch_wykonaj_rollback(struct bursztyn_rozruch_stan* s) {
    if (!s || !rozruch_waliduj_rekord(s)) return false;
    s->generacja += 1;
    s->stan_oczekujacego = BURSZTYN_STAN_NIEUDANY;
    s->licznik_prob = s->maks_prob;
    rozruch_podpisz_rekord(s);
    return true;
}

/* Nazwa tekstowa slotu ("A", "B", "?"). */
static inline const char* rozruch_nazwa_slotu(bursztyn_slot_t slot) {
    if (slot == BURSZTYN_SLOT_A) return "A";
    if (slot == BURSZTYN_SLOT_B) return "B";
    return "?";
}

/* Nazwa stanu logicznego. */
static inline const char* rozruch_nazwa_stanu(bursztyn_stan_slotu_t stan) {
    switch (stan) {
        case BURSZTYN_STAN_NIEAKTYWNY:   return "nieaktywny";
        case BURSZTYN_STAN_AKTYWNY:       return "aktywny";
        case BURSZTYN_STAN_OCZEKUJACY:    return "oczekujacy";
        case BURSZTYN_STAN_POTWIERDZONY:  return "potwierdzony";
        case BURSZTYN_STAN_NIEUDANY:      return "nieudany";
        default:                          return "nieznany";
    }
}

#ifdef __cplusplus
}
#endif
