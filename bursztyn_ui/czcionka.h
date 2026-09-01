#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "../czcionki/extronic16B_unicode.h"

namespace BursztynUI {

class CzcionkaUI {
public:
    static constexpr int WYSOKOSC = 16;
    static constexpr uint32_t LICZBA_GLIFOW = sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]);

    // Dekoduje jeden punkt kodowy UTF-8 z ciągu bajtów
    static size_t dekoduj_utf8(const char* tekst, size_t i, uint32_t* znak) {
        if (!tekst || !znak || tekst[i] == '\0') return 0;
        const uint8_t b0 = static_cast<uint8_t>(tekst[i]);

        // 1 bajt (ASCII: 0x00 .. 0x7F)
        if (b0 < 0x80U) {
            *znak = b0;
            return 1;
        }

        // 2 bajty (0xC0 .. 0xDF)
        if ((b0 & 0xE0U) == 0xC0U) {
            const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
            if ((b1 & 0xC0U) != 0x80U) {
                *znak = b0;
                return 1;
            }
            *znak = (static_cast<uint32_t>(b0 & 0x1FU) << 6) |
                    static_cast<uint32_t>(b1 & 0x3FU);
            return 2;
        }

        // 3 bajty (0xE0 .. 0xEF)
        if ((b0 & 0xF0U) == 0xE0U) {
            const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
            const uint8_t b2 = static_cast<uint8_t>(tekst[i + 2]);
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) {
                *znak = b0;
                return 1;
            }
            *znak = (static_cast<uint32_t>(b0 & 0x0FU) << 12) |
                    (static_cast<uint32_t>(b1 & 0x3FU) << 6) |
                    static_cast<uint32_t>(b2 & 0x3FU);
            return 3;
        }

        // 4 bajty (0xF0 .. 0xF7)
        if ((b0 & 0xF8U) == 0xF0U) {
            const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
            const uint8_t b2 = static_cast<uint8_t>(tekst[i + 2]);
            const uint8_t b3 = static_cast<uint8_t>(tekst[i + 3]);
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U || (b3 & 0xC0U) != 0x80U) {
                *znak = b0;
                return 1;
            }
            *znak = (static_cast<uint32_t>(b0 & 0x07U) << 18) |
                    (static_cast<uint32_t>(b1 & 0x3FU) << 12) |
                    (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
                    static_cast<uint32_t>(b3 & 0x3FU);
            return 4;
        }

        *znak = b0;
        return 1;
    }

    // Koduje punkt kodowy Unicode do ciągu bajtów UTF-8
    static size_t zakoduj_utf8(uint32_t cp, char* out) {
        if (!out) return 0;
        if (cp <= 0x7F) {
            out[0] = static_cast<char>(cp);
            out[1] = '\0';
            return 1;
        } else if (cp <= 0x7FF) {
            out[0] = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
            out[1] = static_cast<char>(0x80 | (cp & 0x3F));
            out[2] = '\0';
            return 2;
        } else if (cp <= 0xFFFF) {
            out[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
            out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[2] = static_cast<char>(0x80 | (cp & 0x3F));
            out[3] = '\0';
            return 3;
        } else if (cp <= 0x10FFFF) {
            out[0] = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
            out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (cp & 0x3F));
            out[4] = '\0';
            return 4;
        }
        return 0;
    }

    // Zwraca rzeczywistą szerokość glifu w pikselach (1..16) z tablicy czcionki
    static int szerokosc_znaku(uint32_t cp) {
        if (cp < LICZBA_GLIFOW) {
            int w = nowa_czcionka_szerokosci[cp];
            if (w < 1) w = 1;
            if (w > 16) w = 16;
            return w;
        }
        // Znak zastępczy dla nieobsługiwanych punktów kodowych Unicode ('?')
        int w = nowa_czcionka_szerokosci['?'];
        if (w < 1) w = 1;
        if (w > 16) w = 16;
        return w;
    }

    // Kanoniczna bitmapa glifu uzywana przez renderer z przycinaniem.
    // Dla nieobslugiwanego punktu kodowego zachowujemy ten sam znak
    // zastepczy co pomiar, aby geometria i rysowanie nie rozjezdzaly sie.
    static const uint16_t* bitmapa_znaku(uint32_t cp) {
        if (cp < LICZBA_GLIFOW) return nowa_czcionka_16x16[cp];
        return nowa_czcionka_16x16['?'];
    }

    // Zwraca krok pozycji X dla znaku (szerokość + 1px odstępu * skala)
    static int postep_znaku(uint32_t cp, int skala = 1) {
        int s = (skala < 1) ? 1 : skala;
        return (szerokosc_znaku(cp) + 1) * s;
    }

    // Zwraca wysokość linii czcionki (16px * skala)
    static int pobierz_wysokosc(int skala = 1) {
        int s = (skala < 1) ? 1 : skala;
        return WYSOKOSC * s;
    }

    // Mierzy całkowitą szerokość tekstu w pikselach
    static int zmierz_tekst(const char* tekst, int skala = 1) {
        if (!tekst || tekst[0] == '\0') return 0;
        int s = (skala < 1) ? 1 : skala;
        int szer = 0;
        size_t i = 0;
        while (tekst[i] != '\0') {
            uint32_t cp = 0;
            size_t zuzyte = dekoduj_utf8(tekst, i, &cp);
            if (zuzyte == 0) break;
            szer += (szerokosc_znaku(cp) + 1) * s;
            i += zuzyte;
        }
        return szer;
    }

    // Mierzy rozmiar tekstu (szerokość x wysokość)
    static Rozmiar zmierz_rozmiar(const char* tekst, int skala = 1) {
        return Rozmiar(zmierz_tekst(tekst, skala), pobierz_wysokosc(skala));
    }

    // Zwraca szerokość tekstu od początku do bajt_limit
    static int pozycja_x_z_bajtu(const char* tekst, size_t bajt_limit, int skala = 1) {
        if (!tekst || tekst[0] == '\0' || bajt_limit == 0) return 0;
        int s = (skala < 1) ? 1 : skala;
        int szer = 0;
        size_t i = 0;
        while (tekst[i] != '\0' && i < bajt_limit) {
            uint32_t cp = 0;
            size_t zuzyte = dekoduj_utf8(tekst, i, &cp);
            if (zuzyte == 0) break;
            szer += (szerokosc_znaku(cp) + 1) * s;
            i += zuzyte;
        }
        return szer;
    }

    // Zwraca przesunięcie w bajtach dla zadanej współrzędnej X wewnątrz tekstu
    static size_t znak_z_pozycji_x(const char* tekst, int target_x, int skala = 1) {
        if (!tekst || tekst[0] == '\0' || target_x <= 0) return 0;
        int s = (skala < 1) ? 1 : skala;
        size_t biezacy_bajt = 0;
        int poprz_x = 0;

        while (tekst[biezacy_bajt] != '\0') {
            uint32_t cp = 0;
            size_t zuzyte = dekoduj_utf8(tekst, biezacy_bajt, &cp);
            if (zuzyte == 0) break;

            int postep = (szerokosc_znaku(cp) + 1) * s;
            int nastepny_x = poprz_x + postep;

            if (target_x < (poprz_x + nastepny_x) / 2) {
                return biezacy_bajt;
            }

            poprz_x = nastepny_x;
            biezacy_bajt += zuzyte;
        }

        return biezacy_bajt;
    }

    // Zlicza punkty kodowe UTF-8 w tekście
    static size_t liczba_znakow(const char* tekst) {
        if (!tekst) return 0;
        size_t count = 0;
        size_t i = 0;
        while (tekst[i] != '\0') {
            if ((static_cast<uint8_t>(tekst[i]) & 0xC0U) != 0x80U) ++count;
            ++i;
        }
        return count;
    }

    // Przesuwa indeks w przód o 1 punkt kodowy
    static size_t nastepny_znak(const char* tekst, size_t pos, size_t max_len) {
        if (!tekst || pos >= max_len) return max_len;
        pos++;
        while (pos < max_len && (static_cast<uint8_t>(tekst[pos]) & 0xC0U) == 0x80U) {
            pos++;
        }
        return pos;
    }

    // Przesuwa indeks w tył o 1 punkt kodowy
    static size_t poprzedni_znak(const char* tekst, size_t pos) {
        if (!tekst || pos == 0) return 0;
        pos--;
        while (pos > 0 && (static_cast<uint8_t>(tekst[pos]) & 0xC0U) == 0x80U) {
            pos--;
        }
        return pos;
    }
};

} // namespace BursztynUI
