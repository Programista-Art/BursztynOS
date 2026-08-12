#pragma once
#include <stdint.h>
#include <stdbool.h>

// Wpis Buffer Descriptor List (BDL) dla DMA Intel HDA
struct HDA_BDL_Wpis {
    uint32_t adres_dolny;
    uint32_t adres_gorny;
    uint32_t dlugosc;
    uint32_t flagi; // Bit 0 = Interrupt on Completion (IOC)
} __attribute__((packed));

#define HDA_MAX_DESKRYPTORY 255
#define HDA_ROZMIAR_BUFORA  4096
#define HDA_ILOSC_BUFOROW   128

bool inicjalizuj_hda();
bool hda_test_ton(uint32_t czestotliwosc_hz, uint32_t czas_ms);
void hda_stop();