#pragma once
#include <stdint.h>


struct CzasRTC {
    uint8_t sekundy;
    uint8_t minuty;
    uint8_t godziny;
    uint8_t dzien;
    uint8_t miesiac;
    uint32_t rok;
};

// Pobiera fizyczny czas ze sprzetowego ukladu CMOS plyty glownej
void PobierzCzasRTC(CzasRTC* czas);

// Tlumaczy strukture czasu na ladny tekst (np. "14:05:09") dla GUI
void FormatujCzasDoStringa(const CzasRTC* czas, char* bufor);