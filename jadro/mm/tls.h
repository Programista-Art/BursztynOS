#pragma once
#include <stdint.h>

/*
 * Bursztyn OS - TLS per proces (%fs).
 *
 * Dlaczego: `errno` w kazdej libc to `*__errno_location()` wskazujace na
 * komorke w TLS. Bez przelaczania %fs przy zmianie kontekstu dwa procesy
 * dziela jeden TLS, wiec bledy jednego widzi drugi - to nie kosmetyka,
 * a niemozliwosc poprawnego dzialania jakiegokolwiek watku lub errno.
 *
 * Uklad:
 *
 *   - baza %fs jest trzymana per PID w tablicy jadra (nie w proces_t, zeby
 *     nie zmieniac ABI deskryptora procesu),
 *   - przy kazdym przelaczeniu kontekstu jadro zapisuje IA32_FS_BASE
 *     (MSR 0xC0000100) dla wchodzacego procesu,
 *   - blok TLS jest alokowany w przestrzeni procesu i zapisywany w tablicy
 *     przy arch_prctl(ARCH_SET_FS).
 *
 * Bezpieczenstwo:
 *
 *   - arch_prctl przyjmuje wylacznie adres w dolnej polowie kanonicznej,
 *     wyrownany do 8, w granicach przestrzeni uzytkownika,
 *   - jadro nie czyta i nie pisze pod ta baza; tylko przekazuje ja do MSR,
 *   - zapis MSR jest wykonywany wylacznie dla wchodzacego PID, wiec proces
 *     nie moze podmienic TLS innego procesu.
 */

/* Rozmiar bloku TLS, ktory jadro rezerwuje na zadanie uzytkownika (16 KiB). */
constexpr uint64_t TLS_ROZMIAR_BLOKU = 16ULL * 1024ULL;

/* MSR dla IA32_FS_BASE. */
constexpr uint32_t MSR_IA32_FS_BASE = 0xC0000100U;
/* MSR dla IA32_GS_BASE jest uzywany przez bramke BWS - nie ruszamy go. */

/* Dolna granica poprawnych baz TLS (nie strona 0). */
constexpr uint64_t TLS_MIN_BAZA = 0x0000000000010000ULL;
/* Gorna granica: dolna polowa kanoniczna. */
constexpr uint64_t TLS_MAKS_BAZA = 0x00007FFFFFFFFFFFULL;

/* Czy adres jest poprawna baza TLS. */
constexpr bool tls_baza_poprawna(uint64_t adres) {
    return adres >= TLS_MIN_BAZA && adres <= TLS_MAKS_BAZA && (adres & 7ULL) == 0;
}

/* =========================================================================
 * API JADRA
 * ========================================================================= */

/* Zeruje stan TLS wszystkich slotow. Wolane przy starcie planisty. */
void tls_zainicjalizuj();

/* Baza TLS dla PID (0, gdy brak). */
uint64_t tls_pobierz_baze(int pid);

/*
 * Ustawia baze TLS dla PID i - jezeli proces jest aktualny - natychmiast
 * wpisuje ja do MSR. Zwraca false dla niepoprawnego PID/adresu.
 */
bool tls_ustaw_baze(int pid, uint64_t adres);

/*
 * Wolane przy kazdym przelaczeniu kontekstu, po zmianie CR3 i przed
 * powrotem do Ring 3. Wpisuje baze TLS wchodzacego PID.
 */
void tls_przelacz(int pid);

/* Czyszczenie przy recyklingu PID. */
void tls_zwolnij_proces(int pid);

/* Liczniki diagnostyczne: ile razy przelaczono i ile razy odmowiono. */
struct TlsStatystyki {
    uint64_t przelaczen;
    uint64_t ustawien;
    uint64_t odrzuconych;
};
TlsStatystyki tls_statystyki();
