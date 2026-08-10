#include "uefi_gop.h"

// Inicjalizacja zerami
GOP_Zmienne gop_ekran = {nullptr, 0, 0, 0, 0};

// Tę funkcję wywołasz zaraz po starcie Jądra, podając jej dane z bootloadera
void InicjalizujGOP(uint64_t adres_fb, uint32_t szer, uint32_t wys, uint32_t pitch, uint32_t bpp) {
    gop_ekran.framebuffer = (uint32_t*)adres_fb;
    gop_ekran.szerokosc = szer;
    gop_ekran.wysokosc = wys;
    gop_ekran.pitch = pitch;
    gop_ekran.bpp = bpp;
}

// Superszybka funkcja stawiająca piksel prosto do pamięci karty graficznej
void GOP_PostawPiksel(int x, int y, uint32_t kolor) {
    // Zabezpieczenie przed rysowaniem poza ekranem (bardzo ważne, inaczej wywoła Kernel Panic)
    if (x < 0 || (uint32_t)x >= gop_ekran.szerokosc || y < 0 || (uint32_t)y >= gop_ekran.wysokosc) {
        return;
    }
    
    // Zabezpieczenie na wypadek, gdyby bootloader nie przekazał adresu
    if (!gop_ekran.framebuffer) {
        return; 
    }

    // Obliczanie fizycznego adresu piksela w pamięci
    // 'pitch' to liczba bajtów, więc dzielimy ją przez 4 (ponieważ uint32_t zajmuje 4 bajty)
    uint32_t indeks = (y * (gop_ekran.pitch / 4)) + x;
    
    gop_ekran.framebuffer[indeks] = kolor;
}