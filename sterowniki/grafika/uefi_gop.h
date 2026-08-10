#pragma once
#include <stdint.h>
#include <stddef.h>

// Struktura przechowująca parametry ekranu z UEFI
struct GOP_Zmienne {
    uint32_t* framebuffer; // Adres bufora obrazu w pamięci
    uint32_t szerokosc;    // Rozdzielczość X
    uint32_t wysokosc;     // Rozdzielczość Y
    uint32_t pitch;        // Liczba bajtów na jedną linię (BARDZO WAŻNE!)
    uint32_t bpp;          // Bity na piksel (zazwyczaj 32)
};

// Globalny dostęp do informacji o ekranie
extern GOP_Zmienne gop_ekran;

// Funkcje sterownika
void InicjalizujGOP(uint64_t adres_fb, uint32_t szer, uint32_t wys, uint32_t pitch, uint32_t bpp);
void GOP_PostawPiksel(int x, int y, uint32_t kolor);