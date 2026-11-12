/*
 * Bursztyn OS - TLS per proces (%fs).
 *
 * Implementacja jest celowo minimalna: tablica baz + zapis MSR. Nie alokuje
 * bloku TLS - to robi uzytkownik (przez mmap/brk) albo loader, a jadro
 * jedynie przenosi wartosc miedzy procesami. Dzieki temu ten modul nie ma
 * zaleznosci od VMM i moze byc testowany na hoscie.
 */
#include "tls.h"
#include "pzb.h"

namespace {

uint64_t bazy_tls[PZB_MAKS_PROCESOW] = {};
TlsStatystyki statystyki = {};

inline bool pid_poprawny(int pid) {
    return pid > 0 && pid < static_cast<int>(PZB_MAKS_PROCESOW);
}

#ifdef BURSZTYN_TLS_HOST_TEST
/* Test hosta nie moze wykonac WRMSR; zapamietujemy ostatnia wartosc. */
uint64_t ostatni_zapis_msr = 0;
uint64_t liczba_zapisow_msr = 0;
#endif

void zapisz_fs_base(uint64_t adres) {
    /*
     * IA32_FS_BASE = 0xC0000100.
     *
     * WRMSR jest uprzywilejowany: wolno go wykonac tylko w Ring 0. Zapis
     * jest atomowy dla pojedynczego MSR, wiec nie wymaga dodatkowej
     * synchronizacji poza tym, ze caller trzyma wylaczone przerwania
     * (tak robi PrzelaczKontekst).
     */
#ifdef BURSZTYN_TLS_HOST_TEST
    ostatni_zapis_msr = adres;
    ++liczba_zapisow_msr;
#else
    const uint32_t lo = static_cast<uint32_t>(adres & 0xFFFFFFFFULL);
    const uint32_t hi = static_cast<uint32_t>(adres >> 32);
    asm volatile("wrmsr" ::"c"(MSR_IA32_FS_BASE), "a"(lo), "d"(hi) : "memory");
#endif
}

}  // namespace

#ifdef BURSZTYN_TLS_HOST_TEST
/* Podglad efektow dla testu hosta. */
uint64_t tls_hosttest_ostatni_msr() { return ostatni_zapis_msr; }
uint64_t tls_hosttest_liczba_zapisow() { return liczba_zapisow_msr; }
#endif

void tls_zainicjalizuj() {
    for (unsigned i = 0; i < PZB_MAKS_PROCESOW; ++i) bazy_tls[i] = 0;
    statystyki = {};
}

uint64_t tls_pobierz_baze(int pid) {
    if (!pid_poprawny(pid)) return 0;
    return bazy_tls[pid];
}

bool tls_ustaw_baze(int pid, uint64_t adres) {
    if (!pid_poprawny(pid)) {
        ++statystyki.odrzuconych;
        return false;
    }
    /* Zero wylacza TLS (dozwolone: proces bez TLS). */
    if (adres != 0 && !tls_baza_poprawna(adres)) {
        ++statystyki.odrzuconych;
        return false;
    }
    bazy_tls[pid] = adres;
    ++statystyki.ustawien;
    return true;
}

void tls_przelacz(int pid) {
    const uint64_t adres = pid_poprawny(pid) ? bazy_tls[pid] : 0;
    zapisz_fs_base(adres);
    ++statystyki.przelaczen;
}

void tls_zwolnij_proces(int pid) {
    if (pid_poprawny(pid)) bazy_tls[pid] = 0;
}

TlsStatystyki tls_statystyki() {
    return statystyki;
}
