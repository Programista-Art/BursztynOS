/*
 * Host gate TLS per proces (%fs) - pierwszy krok punktu 2.13.
 *
 * Sprawdza logike, ktora decyduje o izolacji errno miedzy procesami:
 *
 *   1. kazdy PID ma wlasna baze TLS (brak przecieku miedzy slotami),
 *   2. przelaczenie kontekstu zapisuje baze WLASNIE wchodzacego procesu,
 *   3. arch_prctl-owa walidacja odrzuca adresy poza dolna polowa kanoniczna,
 *      nie wyrownane do 8, w stronie 0 i w gornej polowie,
 *   4. recykling PID czysci baze (inaczej nowy proces dziedziczylby cudzy TLS),
 *   5. liczniki odzwierciedlaja liczbe operacji.
 *
 * WRMSR jest podmieniony na zapis do zmiennej (BURSZTYN_TLS_HOST_TEST),
 * wiec test jest w pelni przenosny i nie wymaga QEMU.
 */
#define BURSZTYN_TLS_HOST_TEST 1

#include "jadro/mm/tls.h"
#include "jadro/bws/pzb.h"

#include <cstdio>

/* Zdefiniowane w tls.cpp tylko dla testu hosta. */
uint64_t tls_hosttest_ostatni_msr();
uint64_t tls_hosttest_liczba_zapisow();

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool warunek, const char* opis) {
    ++g_checks;
    if (!warunek) {
        std::printf("FAIL: %s\n", opis);
        ++g_failures;
    }
}

}  // namespace

int main() {
    tls_zainicjalizuj();

    /* --- 1. Walidacja adresu ------------------------------------------- */
    check(tls_baza_poprawna(0x0000000000010000ULL), "baza 64 KiB poprawna");
    check(tls_baza_poprawna(0x00007FFFFFFFFFF8ULL), "gorna granica wyrownana");
    check(!tls_baza_poprawna(0), "zero nie jest poprawna baza");
    check(!tls_baza_poprawna(0x1000), "strona 0 odrzucona");
    check(!tls_baza_poprawna(0x10001), "brak wyrownania do 8 odrzucony");
    check(!tls_baza_poprawna(0x0000800000000000ULL), "gorna polowa kanoniczna odrzucona");
    check(!tls_baza_poprawna(0xFFFFFFFFFFFFFFFFULL), "UINT64_MAX odrzucony");

    /* --- 2. Izolacja per PID ------------------------------------------- */
    check(tls_pobierz_baze(1) == 0, "PID 1 startuje bez TLS");
    check(tls_ustaw_baze(1, 0x100000), "ustawienie TLS PID 1");
    check(tls_ustaw_baze(2, 0x200000), "ustawienie TLS PID 2");
    check(tls_ustaw_baze(3, 0x300000), "ustawienie TLS PID 3");
    check(tls_pobierz_baze(1) == 0x100000, "PID 1 ma wlasna baze");
    check(tls_pobierz_baze(2) == 0x200000, "PID 2 ma wlasna baze");
    check(tls_pobierz_baze(3) == 0x300000, "PID 3 ma wlasna baze");
    check(tls_pobierz_baze(1) != tls_pobierz_baze(2), "PID 1 i 2 sie nie mieszaja");

    /* --- 3. Przelaczenie kontekstu ------------------------------------- */
    const uint64_t przed = tls_hosttest_liczba_zapisow();
    tls_przelacz(2);
    check(tls_hosttest_ostatni_msr() == 0x200000, "przelaczenie na PID 2 zapisuje jego TLS");
    tls_przelacz(1);
    check(tls_hosttest_ostatni_msr() == 0x100000, "przelaczenie na PID 1 zapisuje jego TLS");
    tls_przelacz(3);
    check(tls_hosttest_ostatni_msr() == 0x300000, "przelaczenie na PID 3 zapisuje jego TLS");
    check(tls_hosttest_liczba_zapisow() == przed + 3, "kazde przelaczenie zapisuje MSR");

    /* Przelaczenie na PID bez TLS czysci %fs (nie zostawia poprzednika). */
    tls_ustaw_baze(4, 0);
    tls_przelacz(3);
    tls_przelacz(4);
    check(tls_hosttest_ostatni_msr() == 0, "proces bez TLS dostaje %fs = 0");

    /* --- 4. Odrzucanie niepoprawnych wartosci -------------------------- */
    check(!tls_ustaw_baze(1, 0x1001), "brak wyrownania odrzucony");
    check(tls_pobierz_baze(1) == 0x100000, "odrzucone ustawienie nie zmienia bazy");
    check(!tls_ustaw_baze(1, 0x0000800000000000ULL), "gorna polowa odrzucona");
    check(!tls_ustaw_baze(0, 0x100000), "PID 0 (jadro) odrzucony");
    check(!tls_ustaw_baze(-1, 0x100000), "PID -1 odrzucony");
    check(!tls_ustaw_baze(int(PZB_MAKS_PROCESOW), 0x100000), "PID poza tablica odrzucony");
    check(tls_pobierz_baze(0) == 0, "PID 0 nie ma TLS");
    check(tls_pobierz_baze(int(PZB_MAKS_PROCESOW)) == 0, "PID poza tablica nie ma TLS");

    /* --- 5. Recykling PID --------------------------------------------- */
    check(tls_pobierz_baze(2) == 0x200000, "PID 2 przed zwolnieniem");
    tls_zwolnij_proces(2);
    check(tls_pobierz_baze(2) == 0, "recykling PID czysci TLS");
    tls_przelacz(2);
    check(tls_hosttest_ostatni_msr() == 0, "po recyklingu %fs = 0");

    /* --- 6. Statystyki ------------------------------------------------- */
    const TlsStatystyki s = tls_statystyki();
    check(s.przelaczen >= 5, "licznik przelaczen");
    check(s.ustawien >= 4, "licznik ustawien");
    check(s.odrzuconych >= 5, "licznik odrzuconych");

    /* --- 7. Reset ------------------------------------------------------ */
    tls_zainicjalizuj();
    check(tls_pobierz_baze(1) == 0 && tls_pobierz_baze(3) == 0, "reset czysci tablice");
    check(tls_statystyki().przelaczen == 0, "reset czysci statystyki");

    if (g_failures != 0) {
        std::printf("[TLS-HOST] FAIL %d/%d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("[TLS-HOST] PASS %d asercji\n", g_checks);
    return 0;
}
