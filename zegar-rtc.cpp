/*
 * Mechanizm: Sterownik Zegara Czasu Rzeczywistego (RTC / CMOS)
 * Opis: Komunikuje sie z ukladem plyty glownej by pobrac obecny, fizyczny
 * czas komputera. Konwertuje archaiczny format BCD na zwykle liczby dziesietne.
 */

#include "zegar-rtc.h"

static inline void outb_rtc(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_rtc(uint16_t port) {
    uint8_t val;
    asm volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static int czy_aktualizacja_w_toku() {
    outb_rtc(0x70, 0x0A);
    return (inb_rtc(0x71) & 0x80);
}

static uint8_t pobierz_rejestr_rtc(int rejestr) {
    outb_rtc(0x70, rejestr);
    return inb_rtc(0x71);
}

void PobierzCzasRTC(CzasRTC* czas) {
    // Czekamy, az uklad CMOS skonczy ewentualna, wlasna aktualizacje
    while (czy_aktualizacja_w_toku());
    
    uint8_t sekundy = pobierz_rejestr_rtc(0x00);
    uint8_t minuty = pobierz_rejestr_rtc(0x02);
    uint8_t godziny = pobierz_rejestr_rtc(0x04);
    uint8_t dzien = pobierz_rejestr_rtc(0x07);
    uint8_t miesiac = pobierz_rejestr_rtc(0x08);
    uint8_t rok = pobierz_rejestr_rtc(0x09);
    
    uint8_t rejestr_b = pobierz_rejestr_rtc(0x0B);

    // Konwersja z BCD (Binary-Coded Decimal) z powrotem do systemu binarnego
    // (Płyty główne zazwyczaj zapisują czas jako BCD by było to czytelne hexadecymalnie)
    if (!(rejestr_b & 0x04)) {
        sekundy = (sekundy & 0x0F) + ((sekundy / 16) * 10);
        minuty = (minuty & 0x0F) + ((minuty / 16) * 10);
        godziny = ((godziny & 0x0F) + (((godziny & 0x70) / 16) * 10)) | (godziny & 0x80);
        dzien = (dzien & 0x0F) + ((dzien / 16) * 10);
        miesiac = (miesiac & 0x0F) + ((miesiac / 16) * 10);
        rok = (rok & 0x0F) + ((rok / 16) * 10);
    }

    // Korekta strefy czasowej (Dla Polski GMT+2 w czasie letnim).
    // Możesz to zoptymalizować dodając parametry dla czasu zimowego/letniego.
    godziny = (godziny + 2) % 24;

    czas->sekundy = sekundy;
    czas->minuty = minuty;
    czas->godziny = godziny;
    czas->dzien = dzien;
    czas->miesiac = miesiac;
    czas->rok = 2000 + rok; // Zakladamy XXI wiek
}

void FormatujCzasDoStringa(const CzasRTC* czas, char* bufor) {
    // Prosta konwersja cyfr na tekst ASCII w formacie HH:MM:SS
    bufor[0] = (czas->godziny / 10) + '0';
    bufor[1] = (czas->godziny % 10) + '0';
    bufor[2] = ':';
    bufor[3] = (czas->minuty / 10) + '0';
    bufor[4] = (czas->minuty % 10) + '0';
    bufor[5] = ':';
    bufor[6] = (czas->sekundy / 10) + '0';
    bufor[7] = (czas->sekundy % 10) + '0';
    bufor[8] = '\0';
}