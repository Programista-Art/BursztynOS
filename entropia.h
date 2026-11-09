#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * Kernelowy CSPRNG ChaCha20 oparty wylacznie o zaufana entropie sprzetowa
 * (VirtIO-RNG, RDSEED, RDRAND). Brak zaufanego zrodla sprzetowego -> fail-closed
 * (zwraca false, nigdy nie degraduje do RDTSC/RTC/PID).
 */
extern "C" bool entropia_pobierz_bajty(uint8_t* out, size_t size);
extern "C" bool entropia_czy_gotowa(void);
extern "C" uint64_t entropia_liczba_reseedow(void);
