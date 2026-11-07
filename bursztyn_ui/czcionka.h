#pragma once

#include <stdint.h>
#include <stddef.h>
#include "typy.h"
#if defined(BSF_NATIVE)
#include "../bsf/runtime.h"
#endif
#if defined(BSF_NATIVE)
#include "../bws_czcionka.h"
#elif defined(BURSZTYN_UI_CZCIONKA_SYSTEMOWA)
#include "../bws_czcionka.h"
#include "../bursztyn_gui.h"
#else
#include "../czcionki/extronic16Bunicode.h"
#endif

namespace BursztynUI {

class CzcionkaUI {
public:
#if defined(BSF_NATIVE)
    template<class Mask>
    static bool rysuj_bsf(const char* text,int x,int y,int px,Mask mask,bool bold=false,const char* family=nullptr) {
        if(!BSF::ready(family)||px<1||px>128)return false;
        size_t size=0;while(text&&text[size]&&size<65536)++size;uint32_t previous=0,previous_face=0,cluster_face=0;size_t cluster_end=0,cluster_begin=0;BSF::MarkPen marks;marks.base=x;
        for(size_t at=0;at<size;){
            if(at==cluster_end){cluster_begin=at;cluster_end=BSF::next_cluster(text,at,size);cluster_face=BSF::resolved_cluster(text+at,cluster_end-at,bold,family);}
            size_t byte_begin=at;uint32_t cp=0;at+=BSF::utf8(text+at,size-at,cp);if(BSF::default_ignorable(cp))continue;
            uint32_t id=cluster_face?cluster_face:BSF::resolved_face(cp,bold,family);
            bool mark=BSF::combining(cp);if(previous&&!mark&&id&&id==previous_face)x+=BSF::kern_face(previous,cp,id,px);
            const auto* g=id?BSF::glyph_face(cp,id,px,bold):nullptr;
            if(g){
                int gx=x+g->x,gy=y+BSF::ascent(px,family)+g->y;
                marks.place(cp,x,gx,gy,g->width,g->height,g->advance);
                BSF::PositionedGlyph positioned{g->face,g->id,mark?0:g->advance,gx-x,gy-y,byte_begin,at,cluster_begin,BSF::TextDirection::LogicalLTR};
                mask(x+positioned.offset_x,y+positioned.offset_y,g->width,g->height,g->alpha);x+=positioned.advance;
            }
            else {int w=(szerokosc_znaku(cp)*px+8)/16,h=px;if(w>128)w=128;if(h>128)h=128;auto* a=new uint8_t[size_t(w)*h];if(a){const auto* bits=bitmapa_znaku(cp);for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx)a[yy*w+xx]=(bits[yy*16/h]&(1u<<(15-xx*16/px)))?255:0;mask(x,y,w,h,a);delete[] a;}if(!mark)x+=(szerokosc_znaku(cp)+1)*px/16;}
            if(!mark){previous=cp;previous_face=id;}
        }return true;
    }
#endif
    static constexpr int WYSOKOSC = 16;
#if defined(BSF_NATIVE)
    static constexpr uint32_t LICZBA_GLIFOW=BWS_CZCIONKA_GLIFY;
#elif defined(BURSZTYN_UI_CZCIONKA_SYSTEMOWA)
    static constexpr uint32_t LICZBA_GLIFOW = BWS_CZCIONKA_GLIFY;
    static bool pobierz_systemowa() {
        if (systemowa_gotowa_) return true;
        if (bws_wywolaj(51, 1, reinterpret_cast<uint64_t>(&systemowa_),
                sizeof(systemowa_), 0) != 1 ||
            systemowa_.wersja != BWS_CZCIONKA_WERSJA ||
            systemowa_.rozmiar != sizeof(systemowa_) ||
            systemowa_.liczba_glifow != LICZBA_GLIFOW || systemowa_.wysokosc != 16)
            return false;
        systemowa_gotowa_ = true;
        return true;
    }
#else
    static constexpr uint32_t LICZBA_GLIFOW = sizeof(nowa_czcionka_16x16) / sizeof(nowa_czcionka_16x16[0]);
#endif

    // Dekoduje jeden punkt kodowy UTF-8 z ciągu bajtów.
    // Zapewnia pełną odporność na sekwencje obcięte i zniekształcone:
    // - Nigdy nie czyta poza znak null '\0'.
    // - Odrzuca nadmiarowe kodowania (overlong: 0xC0, 0xC1, 0xE0 < 0xA0, 0xF0 < 0x90).
    // - Odrzuca surogaty UTF-16 (0xD800..0xDFFF: 0xED 0xA0..0xBF) oraz kody > 0x10FFFF.
    // - W przypadku błędu konsumuje bezpiecznie 1 bajt i zwraca glif zastępczy '?'.
    static size_t dekoduj_utf8(const char* tekst, size_t i, uint32_t* znak) {
        if (!tekst || !znak || tekst[i] == '\0') return 0;
#if defined(BSF_NATIVE)
        size_t available = 0;
        while (available < 4 && tekst[i + available]) ++available;
        return BSF::utf8(tekst + i, available, *znak);
#endif
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
#if defined(BSF_NATIVE)
        return BSF::bitmap_width(cp);
#elif defined(BURSZTYN_UI_CZCIONKA_SYSTEMOWA)
        if (!pobierz_systemowa()) return 1;
        int w = systemowa_.szerokosci[cp < LICZBA_GLIFOW ? cp : '?'];
        return w < 1 ? 1 : w > 16 ? 16 : w;
#else
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
#endif
    }

    // Kanoniczna bitmapa glifu uzywana przez renderer z przycinaniem.
    // Dla nieobslugiwanego punktu kodowego zachowujemy ten sam znak
    // zastepczy co pomiar, aby geometria i rysowanie nie rozjezdzaly sie.
    static const uint16_t* bitmapa_znaku(uint32_t cp) {
#if defined(BSF_NATIVE)
        return BSF::bitmap(cp);
#elif defined(BURSZTYN_UI_CZCIONKA_SYSTEMOWA)
        if (!pobierz_systemowa()) { static const uint16_t pusty[16]{}; return pusty; }
        return systemowa_.bitmapy[cp < LICZBA_GLIFOW ? cp : '?'];
#else
        if (cp < LICZBA_GLIFOW) return nowa_czcionka_16x16[cp];
        return nowa_czcionka_16x16['?'];
#endif
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
#if defined(BSF_NATIVE)
        int a=BSF::measure_cp(cp,ui_skaluj(BSF::ui_size(),skala.procent()));
        if(a>=0)return a;
#endif
        SkalaUI s = skala;
        return (szerokosc_znaku(cp) + 1) * s;
    }

    // Zwraca wysokość linii czcionki (16px * skala)
    static int pobierz_wysokosc(SkalaUI skala = 1) {
#if defined(BSF_NATIVE)
        int h=BSF::height(ui_skaluj(BSF::ui_size(),skala.procent()));
        if(h>0)return h;
#endif
        SkalaUI s = skala;
        return WYSOKOSC * s;
    }

    // Mierzy całkowitą szerokość tekstu w pikselach
    static int zmierz_tekst(const char* tekst, SkalaUI skala = 1) {
#if defined(BSF_NATIVE)
        if(BSF::ready())return BSF::measure_text(tekst,ui_skaluj(BSF::ui_size(),skala.procent()));
#endif
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
#if defined(BSF_NATIVE)
        if(BSF::ready()){size_t boundary=0;while(tekst&&tekst[boundary]&&boundary<bajt_limit){uint32_t cp;size_t n=dekoduj_utf8(tekst,boundary,&cp);if(!n||n>bajt_limit-boundary)break;boundary+=n;}return BSF::measure_text(tekst,ui_skaluj(BSF::ui_size(),skala.procent()),false,nullptr,boundary);}
#endif
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

            int postep = postep_znaku(cp,s);
#if defined(BSF_NATIVE)
            if(BSF::ready()&&biezacy_bajt){uint32_t prev=0;size_t before=poprzedni_znak(tekst,biezacy_bajt);dekoduj_utf8(tekst,before,&prev);postep+=BSF::kern(prev,cp,ui_skaluj(BSF::ui_size(),s.procent()));}
#endif
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
#if defined(BSF_NATIVE)
        return BSF::next_cluster(tekst,pos,max_len);
#endif
        pos++;
        while (pos < max_len && (static_cast<uint8_t>(tekst[pos]) & 0xC0U) == 0x80U) {
            pos++;
        }
        return pos;
    }

    // Przesuwa indeks w tył o 1 punkt kodowy
    static size_t poprzedni_znak(const char* tekst, size_t pos) {
        if (!tekst || pos == 0) return 0;
#if defined(BSF_NATIVE)
        return BSF::previous_cluster(tekst,pos);
#endif
        pos--;
        while (pos > 0 && (static_cast<uint8_t>(tekst[pos]) & 0xC0U) == 0x80U) {
            pos--;
        }
        return pos;
    }
#if defined(BURSZTYN_UI_CZCIONKA_SYSTEMOWA)
private:
    static inline BwsCzcionkaSystemowa systemowa_{}; // BSS, never font bytes in BUR
    static inline bool systemowa_gotowa_{false};
#endif
};

} // namespace BursztynUI
