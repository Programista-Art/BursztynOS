#pragma once
#include <stdint.h>
bool hpet_inicjalizuj();
bool hpet_dostepny();
uint64_t hpet_adres_fizyczny();
uint64_t hpet_period_fs();
uint64_t hpet_czestotliwosc_hz();
uint64_t hpet_odczytaj_tick();
uint64_t czas_monotoniczny_ns();
uint64_t czas_monotoniczny_us();
uint64_t czas_monotoniczny_ms();
bool hpet_czekaj_ns_boot(uint64_t ns);
bool hpet_test_wrap_diagnostyczny();
