#pragma once
#include <stdint.h>

struct czas_rtc {
    uint8_t sekundy;
    uint8_t minuty;
    uint8_t godziny;
    uint8_t dzien;
    uint8_t miesiac;
    uint32_t rok;
};

// Funkcje w stylu snake_case
void pobierz_czas_rtc(czas_rtc* czas);
void formatuj_czas_do_stringa(const czas_rtc* czas, char* bufor);