#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#include "../czcionki/extronic16Bunicode.h"

namespace BursztynUI {

class CzcionkaUI {
public:
    static constexpr int WYSOKOSC = 16;
    static constexpr uint32_t LICZBA_GLIFOW = sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]);

    // Dekoduje jeden punkt kodowy UTF-8 z ciągu bajtów.
    // Zapewnia pełną odporność na sekwencje obcięte i zniekształcone:
    // - Nigdy nie czyta poza znak null '\0'.
    // - Odrzuca nadmiarowe kodowania (overlong: 0xC0, 0xC1, 0xE0 < 0xA0, 0xF0 < 0x90).
    // - Odrzuca surogaty UTF-16 (0xD800..0xDFFF: 0xED 0xA0..0xBF) oraz kody > 0x10FFFF.
    // - W przypadku błędu konsumuje bezpiecznie 1 bajt i zwraca glif zastępczy '?'.
    static size_t dekoduj_utf8(const char* tekst, size_t i, uint32_t* znak) {
        if (!tekst || !znak || tekst[i] == '\0') return 0;
        const uint8_t b0 = static_cast<uint8_t>(tekst[i]);

        // 1 bajt (ASCII: 0x00 .. 0x7F)
        if (b0 < 0x80U) {
            *znak = b0;
            return 1;
        }

        // Błędny bajt początkowy (bajt kontynuacji 0x80..0xBF lub niedozwolony > 0xF4, albo overlong 0xC0..0xC1)
        if (b0 < 0xC2U || b0 > 0xF4U) {
            *znak = '?';
            return 1;
        }

        // 2 bajty (0xC2 .. 0xDF) -> U+0080 .. U+07FF
        if (b0 <= 0xDFU) {
            if (tekst[i + 1] == '\0') {
                *znak = '?';
                return 1;
            }
            const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
            if ((b1 & 0xC0U) != 0x80U) {
                *znak = '?';
                return 1;
            }
            *znak = (static_cast<uint32_t>(b0 & 0x1FU) << 6) |
                    static_cast<uint32_t>(b1 & 0x3FU);
            return 2;
        }

        // 3 bajty (0xE0 .. 0xEF) -> U+0800 .. U+FFFF
        if (b0 <= 0xEFU) {
            if (tekst[i + 1] == '\0') {
                *znak = '?';
                return 1;
            }
            const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
            if ((b1 & 0xC0U) != 0x80U) {
                *znak = '?';
                return 1;
            }
            // Overlong (0xE0 < 0xA0) lub UTF-16 surrogate (0xED > 0x9F)
            if ((b0 == 0xE0U && b1 < 0xA0U) || (b0 == 0xEDU && b1 > 0x9FU)) {
                *znak = '?';
                return 1;
            }
            if (tekst[i + 2] == '\0') {
                *znak = '?';
                return 1;
            }
            const uint8_t b2 = static_cast<uint8_t>(tekst[i + 2]);
            if ((b2 & 0xC0U) != 0x80U) {
                *znak = '?';
                return 1;
            }
            *znak = (static_cast<uint32_t>(b0 & 0x0FU) << 12) |
                    (static_cast<uint32_t>(b1 & 0x3FU) << 6) |
                    static_cast<uint32_t>(b2 & 0x3FU);
            return 3;
        }

        // 4 bajty (0xF0 .. 0xF4) -> U+10000 .. U+10FFFF
        if (tekst[i + 1] == '\0') {
            *znak = '?';
            return 1;
        }
        const uint8_t b1 = static_cast<uint8_t>(tekst[i + 1]);
        if ((b1 & 0xC0U) != 0x80U) {
            *znak = '?';
            return 1;
        }
        // Overlong (0xF0 < 0x90) lub poza Unicode (0xF4 > 0x8F)
        if ((b0 == 0xF0U && b1 < 0x90U) || (b0 == 0xF4U && b1 > 0x8FU)) {
            *znak = '?';
            return 1;
        }
        if (tekst[i + 2] == '\0') {
            *znak = '?';
            return 1;
        }
        const uint8_t b2 = static_cast<uint8_t>(tekst[i + 2]);
        if ((b2 & 0xC0U) != 0x80U) {
            *znak = '?';
            return 1;
        }
        if (tekst[i + 3] == '\0') {
            *znak = '?';
            return 1;
        }
        const uint8_t b3 = static_cast<uint8_t>(tekst[i + 3]);
        if ((b3 & 0xC0U) != 0x80U) {
            *znak = '?';
            return 1;
        }
        uint32_t cp = (static_cast<uint32_t>(b0 & 0x07U) << 18) |
                      (static_cast<uint32_t>(b1 & 0x3FU) << 12) |
                      (static_cast<uint32_t>(b2 & 0x3FU) << 6) |
                      static_cast<uint32_t>(b3 & 0x3FU);
        if (cp > 0x10FFFFU) {
            *znak = '?';
            return 1;
        }
        *znak = cp;
        return 4;
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

    // Pomiar wspolnego renderera grayscale obejmuje skale duzych napisow.
    static int zmierz_tekst_wygladzony(const char* tekst,uint32_t procent) {
        const uint32_t p=procent>=100&&procent<=800?procent:100;
        int szer=0;
        for(size_t i=0;tekst&&tekst[i];) {
            uint32_t cp=0;i+=dekoduj_utf8(tekst,i,&cp);
            const int krok=ui_skaluj(szerokosc_znaku(cp)+1,p);
            if(szer>INT32_MAX-krok)return INT32_MAX;
            szer+=krok;
        }
        return szer;
    }
    // Wspolny opcjonalny renderer grayscale: te same glify Unicode i metryki.
    // Interpolacja pokrycia, bez FPU, alokacji i subpikselowych kolorow.
    template<class Piksel>
    static void rysuj_tekst_wygladzony(const char* tekst, int x, int y,
                                      uint32_t procent, Piksel piksel) {
        if (!tekst) return;
        const int p = int(procent>=100&&procent<=800?procent:100);
        for (size_t i=0; tekst[i];) {
            uint32_t cp=0; i+=dekoduj_utf8(tekst,i,&cp);
            const auto* glif=bitmapa_znaku(cp);
            const int gw=szerokosc_znaku(cp), dw=ui_skaluj(gw,p), dh=ui_skaluj(WYSOKOSC,p);
            auto probka=[&](int sx,int sy)->unsigned {
                return sx>=0&&sx<gw&&sy>=0&&sy<WYSOKOSC&&
                    (glif[sy]&(1U<<(15-sx))) ? 255U : 0U;
            };
            for(int dy=0;dy<dh;++dy) {
                const int fy=(2*dy+1)*128*100/p-128;
                const int sy=fy>=0?fy/256:(fy-255)/256;
                const unsigned wy=unsigned(fy-sy*256);
                for(int dx=0;dx<dw;++dx) {
                    const int fx=(2*dx+1)*128*100/p-128;
                    const int sx=fx>=0?fx/256:(fx-255)/256;
                    const unsigned wx=unsigned(fx-sx*256);
                    const unsigned a=probka(sx,sy)*(256-wx)+probka(sx+1,sy)*wx;
                    const unsigned b=probka(sx,sy+1)*(256-wx)+probka(sx+1,sy+1)*wx;
                    const unsigned pokrycie=(a*(256-wy)+b*wy+32768)/65536;
                    if(pokrycie) piksel(x+dx,y+dy,pokrycie);
                }
            }
            x+=ui_skaluj(szerokosc_znaku(cp)+1,p);
        }
    }

    // Zwraca krok pozycji X dla znaku (szerokość + 1px odstępu * skala)
    static int postep_znaku(uint32_t cp, SkalaUI skala = 1) {
        SkalaUI s = skala;
        return (szerokosc_znaku(cp) + 1) * s;
    }

    // Zwraca wysokość linii czcionki (16px * skala)
    static int pobierz_wysokosc(SkalaUI skala = 1) {
        SkalaUI s = skala;
        return WYSOKOSC * s;
    }

    // Mierzy całkowitą szerokość tekstu w pikselach
    static int zmierz_tekst(const char* tekst, SkalaUI skala = 1) {
        if (!tekst || tekst[0] == '\0') return 0;
        SkalaUI s = skala;
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
    static Rozmiar zmierz_rozmiar(const char* tekst, SkalaUI skala = 1) {
        return Rozmiar(zmierz_tekst(tekst, skala), pobierz_wysokosc(skala));
    }

    // Zwraca szerokość tekstu od początku do bajt_limit
    static int pozycja_x_z_bajtu(const char* tekst, size_t bajt_limit, SkalaUI skala = 1) {
        if (!tekst || tekst[0] == '\0' || bajt_limit == 0) return 0;
        SkalaUI s = skala;
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
    static size_t znak_z_pozycji_x(const char* tekst, int target_x, SkalaUI skala = 1) {
        if (!tekst || tekst[0] == '\0' || target_x <= 0) return 0;
        SkalaUI s = skala;
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
