#pragma once
#include "posix_dyspozytor.h"
#include "jadro/bws/pzb.h"

/*
 * Bursztyn OS - uslugi bramki SysV po stronie jadra.
 *
 * Dyspozytor (posix_dyspozytor.cpp) jest przenosny i nie zna jadra. Ten
 * naglowek wystawia jedyny punkt wejscia uzywany przez IDT: funkcje, ktora
 * dostaje rame przerwania, wyciaga z niej rejestry SysV, wola dyspozytor
 * i wpisuje wynik w RAX ramy.
 *
 * Zasady bezpieczenstwa (identyczne z BWS):
 *
 *   - kazdy wskaznik Ring 3 jest walidowany i kopiowany przez
 *     copy_from_user/copy_to_user z jadro/bws/bezpieczenstwo.cpp,
 *   - zadna usluga nie dereferencjonuje surowego wskaznika uzytkownika,
 *   - operacje wymagajace prawa (zapis do pliku, siec) sprawdzaja maske
 *     PRAWO_* procesu, tak samo jak BWS.
 */

struct RejestryStanowe;

/* Czy bramka SysV jest dostepna dla danego procesu.
 *
 * Bramka jest otwarta dla kazdego procesu Ring 3 (to warunek uruchomienia
 * obcego binarium), ale proces o PZB_PIASKOWNICA dostaje zawezony zestaw:
 * tylko write/exit/getpid/gettid/uname/clock i mmap anonimowy. Rozszerzenia
 * (arch_prctl, futex, getrandom) sa wylaczone dla piaskownicy. */
bool posix_bramka_dostepna(const proces_t& proces);

/* Obsluga wektora POSIX_WEKTOR. Zwraca wartosc do wpisania w ramie->rax.
 *
 * Wola sie z WspolnaObslugaPrzerwan, po sprawdzeniu wektora i przed EOI
 * (wektor 0x80 nie ma bitu w ISR LAPIC, wiec EOI jest no-op). */
uint64_t posix_obsluz_bramke(RejestryStanowe* rama);

/* Diagnostyka: licznik wywolan bramki i ostatnie wywolanie.
 * Uzywane przez BWS 91 / testy; zawsze bezpieczne do odczytu. */
const PosixDiagnostyka& posix_diagnostyka();

/* Zerowanie stanu per-PID przy recyklingu procesu. */
void posix_zwolnij_proces(int pid);
