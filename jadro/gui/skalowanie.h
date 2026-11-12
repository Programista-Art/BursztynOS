#pragma once
#include <stdint.h>
#include <stddef.h>
#include "graphics_copy.h"

/*
 * Bursztyn OS - skalowanie obrazu dla kompozytora i BWS.
 *
 * Ten naglowek jest jedynym zrodlem matematyki skalowania w systemie.
 * Zasady, ktorych nie wolno lamac:
 *
 *   - ZERO float/double. Jadro jest budowane z -mno-sse -mno-sse2, wiec
 *     kazda sciezka musi pozostac w rejestrach ogolnych.
 *   - ZERO alokacji. Funkcje dostaja bufory wywolujacego.
 *   - ZERO zaleznosci od stanu globalnego - czyste funkcje, testowalne
 *     na hoscie bez jadra.
 *   - Mapowanie pikseli liczone jest w stalym punkcie 16.16 na int64_t,
 *     wiec nie ma dryfu przy dlugich wierszach.
 *
 * Semantyka koloru jest identyczna z kompozytorem (skladacz_obrazu.h):
 *
 *   0x00000000 = piksel pusty (przezroczysty),
 *   0x01000000 = widoczna czern (starszy marker grafika.cpp),
 *   0x00RRGGBB = nieprzezroczysty kolor,
 *   0xAARRGGBB = kolor z prostym kanalem alfa.
 *
 * Wynik skalowania zachowuje te konwencje: coverage 0 daje 0, coverage
 * pelne daje 0x00RRGGBB, coverage czesciowe daje 0xAARRGGBB. Dzieki temu
 * ten sam wynik dziala na pulpicie (motyw_pulpit_skladaj) i w warstwie
 * okna (BWS57 tryb 3, source-over).
 */

/* =========================================================================
 * 1. FILTRY SKALOWANIA
 * ========================================================================= */

enum SkalowanieFiltr : uint32_t {
    /* Jeden najblizszy piksel. Najtanszy; dobry do powiekszen calkowitych. */
    SKALOWANIE_NEAREST = 0U,
    /* Cztery probki z wagami 16.16. Domyslny filtr tekstu i UI. */
    SKALOWANIE_BILINEAR = 1U,
    /* Srednia obszarowa. Jedyny filtr bez aliasingu przy zmniejszaniu. */
    SKALOWANIE_BOX = 2U
};

constexpr bool skalowanie_filtr_poprawny(uint32_t filtr) {
    return filtr <= SKALOWANIE_BOX;
}

/* =========================================================================
 * 2. OPIS ZRODLA
 * ========================================================================= */

struct SkalowanieZrodlo {
    const uint32_t* piksele;
    uint32_t szerokosc;
    uint32_t wysokosc;
};

inline bool skalowanie_zrodlo_poprawne(const SkalowanieZrodlo& z) {
    return z.piksele != nullptr &&
           z.szerokosc > 0U && z.wysokosc > 0U &&
           z.szerokosc <= 8192U && z.wysokosc <= 8192U;
}

/* =========================================================================
 * 3. GEOMETRIA: DOPASOWANIE Z ZACHOWANIEM PROPORCJI
 * ========================================================================= */

enum SkalowanieTryb : uint32_t {
    /* Wypelnij caly prostokat, ignorujac proporcje. */
    SKALOWANIE_ROZCIAGNIJ = 0U,
    /* Zmiesc calosc (letterbox). */
    SKALOWANIE_DOPASUJ = 1U,
    /* Wypelnij calosc i przytnij nadmiar (pillarbox/letterbox cropped). */
    SKALOWANIE_WYPELNIJ = 2U
};

struct SkalowanieProstokat {
    int32_t x;
    int32_t y;
    int32_t szerokosc;
    int32_t wysokosc;
};

/*
 * Wyznacza prostokat docelowy w ukladzie lokalnym (0,0 = lewy gorny rog
 * obszaru docelowego). Wymiary wejsciowe musza byc dodatnie.
 */
inline SkalowanieProstokat skalowanie_dopasuj(
    uint32_t src_w, uint32_t src_h,
    uint32_t dst_w, uint32_t dst_h,
    uint32_t tryb
) {
    SkalowanieProstokat out{0, 0, static_cast<int32_t>(dst_w), static_cast<int32_t>(dst_h)};
    if (src_w == 0U || src_h == 0U || dst_w == 0U || dst_h == 0U) {
        out.szerokosc = 0;
        out.wysokosc = 0;
        return out;
    }
    if (tryb == SKALOWANIE_ROZCIAGNIJ || tryb > SKALOWANIE_WYPELNIJ) {
        return out;
    }

    /* Porownanie ulamkow bez dzielenia: src_w/src_h vs dst_w/dst_h. */
    const uint64_t lewa = static_cast<uint64_t>(src_w) * static_cast<uint64_t>(dst_h);
    const uint64_t prawa = static_cast<uint64_t>(dst_w) * static_cast<uint64_t>(src_h);

    if (tryb == SKALOWANIE_DOPASUJ) {
        if (lewa > prawa) {
            /* Zrodlo szersze: dopasuj do szerokosci. */
            const uint32_t h = static_cast<uint32_t>(
                (static_cast<uint64_t>(dst_w) * src_h) / src_w);
            out.szerokosc = static_cast<int32_t>(dst_w);
            out.wysokosc = static_cast<int32_t>(h ? h : 1U);
            out.x = 0;
            out.y = static_cast<int32_t>((dst_h - static_cast<uint32_t>(out.wysokosc)) / 2U);
        } else {
            const uint32_t w = static_cast<uint32_t>(
                (static_cast<uint64_t>(dst_h) * src_w) / src_h);
            out.wysokosc = static_cast<int32_t>(dst_h);
            out.szerokosc = static_cast<int32_t>(w ? w : 1U);
            out.y = 0;
            out.x = static_cast<int32_t>((dst_w - static_cast<uint32_t>(out.szerokosc)) / 2U);
        }
        return out;
    }

    /* SKALOWANIE_WYPELNIJ: nadmiar jest przycinany przez wywolujacego. */
    if (lewa > prawa) {
        const uint32_t h = static_cast<uint32_t>(
            (static_cast<uint64_t>(dst_w) * src_h) / src_w);
        out.szerokosc = static_cast<int32_t>(dst_w);
        out.wysokosc = static_cast<int32_t>(h ? h : 1U);
        out.y = static_cast<int32_t>((dst_h - static_cast<uint32_t>(out.wysokosc)) / 2U);
    } else {
        const uint32_t w = static_cast<uint32_t>(
            (static_cast<uint64_t>(dst_h) * src_w) / src_h);
        out.wysokosc = static_cast<int32_t>(dst_h);
        out.szerokosc = static_cast<int32_t>(w ? w : 1U);
        out.x = static_cast<int32_t>((dst_w - static_cast<uint32_t>(out.szerokosc)) / 2U);
    }
    return out;
}

/* =========================================================================
 * 4. KOLOR: DEKOMPOZYCJA I SKLADANIE
 * ========================================================================= */

/*
 * Rozklada piksel warstwy na coverage (0..255) i skladowe proste.
 * Coverage 0 oznacza piksel pusty - jego RGB jest bez znaczenia.
 */
inline void skalowanie_rozloz(
    uint32_t piksel,
    uint32_t& coverage,
    uint32_t& r,
    uint32_t& g,
    uint32_t& b
) {
    if (piksel == 0U) {
        coverage = 0U;
        r = g = b = 0U;
        return;
    }
    uint32_t a = (piksel >> 24) & 0xFFU;
    /* 0x00RRGGBB i marker 0x01000000 sa nieprzezroczyste (konwencja skladacza). */
    if (a == 0U || piksel == 0x01000000U) a = 255U;
    coverage = a;
    r = (piksel >> 16) & 0xFFU;
    g = (piksel >> 8) & 0xFFU;
    b = piksel & 0xFFU;
}

/* Sklada wynik z coverage; zwraca 0 dla piksela pustego. */
inline uint32_t skalowanie_zloz(uint32_t coverage, uint32_t r, uint32_t g, uint32_t b) {
    if (coverage == 0U) return 0U;
    if (r > 255U) r = 255U;
    if (g > 255U) g = 255U;
    if (b > 255U) b = 255U;
    if (coverage >= 255U) return (r << 16) | (g << 8) | b;
    return (coverage << 24) | (r << 16) | (g << 8) | b;
}

/* =========================================================================
 * 5. POZYCJA ZRODLA (STALY PUNKT 16.16)
 * ========================================================================= */

/*
 * Pozycja srodka piksela docelowego 'i' w ukladzie pikseli zrodla,
 * przesunieta o -0.5 tak, ze 0 = srodek piksela zrodla 0.
 *
 * Jednostka: 1/65536 piksela. Wynik moze byc ujemny (przy powiekszeniu
 * pierwszy piksel lezy przed srodkiem pierwszego piksela zrodla).
 *
 * dst i src sa dodatnie; iloczyn (2*i+1)*src*65536 miesci sie w int64_t
 * dla dopuszczalnych limitow (i < 8192, src <= 8192).
 */
inline int64_t skalowanie_pozycja(uint32_t i, uint32_t dst, uint32_t src) {
    const int64_t num = (static_cast<int64_t>(2) * i + 1) *
                        static_cast<int64_t>(src) * 65536;
    const int64_t den = static_cast<int64_t>(2) * static_cast<int64_t>(dst);
    return num / den - 32768;
}

/*
 * Indeks piksela zrodla dla filtru nearest (probkowanie ze srodka
 * piksela docelowego). Wynik jest zawsze w [0, src-1].
 */
inline uint32_t skalowanie_indeks_nearest(uint32_t i, uint32_t dst, uint32_t src) {
    if (src == 0U) return 0U;
    /*
     * Nearest probkuje piksel, ktorego srodek jest najblizej srodka piksela
     * docelowego: floor((i+0.5) * src / dst). Pozycja z skalowanie_pozycja()
     * jest przesunieta o pol piksela, wiec dodajemy je z powrotem przed
     * obcieciem do liczby calkowitej.
     */
    const int64_t pos = skalowanie_pozycja(i, dst, src) + 32768;
    if (pos <= 0) return 0U;
    const uint32_t idx = static_cast<uint32_t>(pos >> 16);
    return idx >= src ? src - 1U : idx;
}

/*
 * Rozklad bilinear na os: x0/x1 (indeksy) i waga x1 w 0..65535.
 * x1 == x0 oznacza brak interpolacji (skrajne piksele sa powtarzane).
 */
struct SkalowanieOs {
    uint32_t i0;
    uint32_t i1;
    uint32_t waga1;
};

inline SkalowanieOs skalowanie_os(uint32_t i, uint32_t dst, uint32_t src) {
    SkalowanieOs os{0U, 0U, 0U};
    if (src == 0U) return os;
    const int64_t pos = skalowanie_pozycja(i, dst, src);
    if (pos <= 0) {
        return os;                       /* i0 == i1 == 0 */
    }
    const int64_t ostatni = (static_cast<int64_t>(src) - 1) * 65536;
    if (pos >= ostatni) {
        os.i0 = os.i1 = src - 1U;
        return os;
    }
    os.i0 = static_cast<uint32_t>(pos >> 16);
    os.waga1 = static_cast<uint32_t>(pos & 0xFFFF);
    os.i1 = os.i0 + 1U;
    if (os.i1 >= src) {
        os.i1 = src - 1U;
        os.waga1 = 0U;
    }
    return os;
}

/* =========================================================================
 * 6. PROBKOWANIE
 * ========================================================================= */

inline uint32_t skalowanie_piksel_nearest(
    const SkalowanieZrodlo& z, uint32_t dst_x, uint32_t dst_y,
    uint32_t dst_w, uint32_t dst_h
) {
    const uint32_t sx = skalowanie_indeks_nearest(dst_x, dst_w, z.szerokosc);
    const uint32_t sy = skalowanie_indeks_nearest(dst_y, dst_h, z.wysokosc);
    return z.piksele[static_cast<size_t>(sy) * z.szerokosc + sx];
}

/*
 * Bilinear w przestrzeni premultiplikowanej: wagi mnoza coverage razem
 * z RGB, wiec krawedz ksztaltu nie sciaga koloru z przezroczystego tla.
 */
inline uint32_t skalowanie_piksel_bilinear(
    const SkalowanieZrodlo& z, uint32_t dst_x, uint32_t dst_y,
    uint32_t dst_w, uint32_t dst_h
) {
    const SkalowanieOs ox = skalowanie_os(dst_x, dst_w, z.szerokosc);
    const SkalowanieOs oy = skalowanie_os(dst_y, dst_h, z.wysokosc);

    const uint32_t w0x = 65535U - ox.waga1;
    const uint32_t w0y = 65535U - oy.waga1;

    const size_t row0 = static_cast<size_t>(oy.i0) * z.szerokosc;
    const size_t row1 = static_cast<size_t>(oy.i1) * z.szerokosc;

    const uint32_t p00 = z.piksele[row0 + ox.i0];
    const uint32_t p10 = z.piksele[row0 + ox.i1];
    const uint32_t p01 = z.piksele[row1 + ox.i0];
    const uint32_t p11 = z.piksele[row1 + ox.i1];

    /* Waga kazdej probki: 0..65535^2, suma = 65535^2. Uzywamy 32-bit skali. */
    const uint64_t w00 = static_cast<uint64_t>(w0x) * w0y;
    const uint64_t w10 = static_cast<uint64_t>(ox.waga1) * w0y;
    const uint64_t w01 = static_cast<uint64_t>(w0x) * oy.waga1;
    const uint64_t w11 = static_cast<uint64_t>(ox.waga1) * oy.waga1;

    uint32_t a00, r00, g00, b00;
    uint32_t a10, r10, g10, b10;
    uint32_t a01, r01, g01, b01;
    uint32_t a11, r11, g11, b11;
    skalowanie_rozloz(p00, a00, r00, g00, b00);
    skalowanie_rozloz(p10, a10, r10, g10, b10);
    skalowanie_rozloz(p01, a01, r01, g01, b01);
    skalowanie_rozloz(p11, a11, r11, g11, b11);

    /* Premultiplikacja: waga * coverage, potem kolor * (waga * coverage). */
    const uint64_t s00 = w00 * a00, s10 = w10 * a10;
    const uint64_t s01 = w01 * a01, s11 = w11 * a11;
    const uint64_t suma_a = s00 + s10 + s01 + s11;
    if (suma_a == 0U) return 0U;

    const uint64_t sr = w00 * a00 * r00 + w10 * a10 * r10 +
                        w01 * a01 * r01 + w11 * a11 * r11;
    const uint64_t sg = w00 * a00 * g00 + w10 * a10 * g10 +
                        w01 * a01 * g01 + w11 * a11 * g11;
    const uint64_t sb = w00 * a00 * b00 + w10 * a10 * b10 +
                        w01 * a01 * b01 + w11 * a11 * b11;

    const uint64_t max_waga = 65535ULL * 65535ULL;
    const uint32_t coverage = static_cast<uint32_t>(
        (suma_a + max_waga / 2U) / max_waga);

    const uint32_t r = static_cast<uint32_t>((sr + suma_a / 2U) / suma_a);
    const uint32_t g = static_cast<uint32_t>((sg + suma_a / 2U) / suma_a);
    const uint32_t b = static_cast<uint32_t>((sb + suma_a / 2U) / suma_a);
    return skalowanie_zloz(coverage, r, g, b);
}

/*
 * Srednia obszarowa jednego piksela docelowego. Wykorzystywana tylko przy
 * zmniejszaniu; przy powiekszeniu wywolujacy powinien wybrac nearest.
 *
 * Koszt jest ograniczony: jezeli na jeden piksel docelowy przypada wiecej
 * niz SKALOWANIE_BOX_MAKS_PROBEK pikseli zrodla, funkcja degraduje sie do
 * probkowania nearest (zachowanie okreslone, nie blad).
 */
constexpr uint32_t SKALOWANIE_BOX_MAKS_PROBEK = 64U;

inline uint32_t skalowanie_piksel_box(
    const SkalowanieZrodlo& z, uint32_t dst_x, uint32_t dst_y,
    uint32_t dst_w, uint32_t dst_h
) {
    const uint64_t x0 = (static_cast<uint64_t>(dst_x) * z.szerokosc) / dst_w;
    const uint64_t x1 = (static_cast<uint64_t>(dst_x + 1U) * z.szerokosc) / dst_w;
    const uint64_t y0 = (static_cast<uint64_t>(dst_y) * z.wysokosc) / dst_h;
    const uint64_t y1 = (static_cast<uint64_t>(dst_y + 1U) * z.wysokosc) / dst_h;

    const uint64_t sx0 = x0 < z.szerokosc ? x0 : z.szerokosc - 1U;
    const uint64_t sx1 = x1 <= z.szerokosc ? x1 : z.szerokosc;
    const uint64_t sy0 = y0 < z.wysokosc ? y0 : z.wysokosc - 1U;
    const uint64_t sy1 = y1 <= z.wysokosc ? y1 : z.wysokosc;

    const uint64_t kolumny = (sx1 > sx0 ? sx1 - sx0 : 1U);
    const uint64_t wiersze = (sy1 > sy0 ? sy1 - sy0 : 1U);
    if (kolumny * wiersze > SKALOWANIE_BOX_MAKS_PROBEK) {
        return skalowanie_piksel_nearest(z, dst_x, dst_y, dst_w, dst_h);
    }

    uint64_t suma_a = 0, suma_r = 0, suma_g = 0, suma_b = 0;
    for (uint64_t sy = sy0; sy < sy1; ++sy) {
        const uint32_t* row = z.piksele + static_cast<size_t>(sy) * z.szerokosc;
        for (uint64_t sx = sx0; sx < sx1; ++sx) {
            uint32_t a, r, g, b;
            skalowanie_rozloz(row[sx], a, r, g, b);
            if (a == 0U) continue;
            suma_a += a;
            suma_r += static_cast<uint64_t>(a) * r;
            suma_g += static_cast<uint64_t>(a) * g;
            suma_b += static_cast<uint64_t>(a) * b;
        }
    }
    if (suma_a == 0U) return 0U;
    const uint32_t coverage = static_cast<uint32_t>(
        (suma_a + kolumny * wiersze / 2U) / (kolumny * wiersze));
    const uint32_t r = static_cast<uint32_t>((suma_r + suma_a / 2U) / suma_a);
    const uint32_t g = static_cast<uint32_t>((suma_g + suma_a / 2U) / suma_a);
    const uint32_t b = static_cast<uint32_t>((suma_b + suma_a / 2U) / suma_a);
    return skalowanie_zloz(coverage, r, g, b);
}

inline uint32_t skalowanie_piksel(
    const SkalowanieZrodlo& z, uint32_t dst_x, uint32_t dst_y,
    uint32_t dst_w, uint32_t dst_h, uint32_t filtr
) {
    switch (filtr) {
        case SKALOWANIE_BILINEAR:
            return skalowanie_piksel_bilinear(z, dst_x, dst_y, dst_w, dst_h);
        case SKALOWANIE_BOX:
            return skalowanie_piksel_box(z, dst_x, dst_y, dst_w, dst_h);
        case SKALOWANIE_NEAREST:
        default:
            return skalowanie_piksel_nearest(z, dst_x, dst_y, dst_w, dst_h);
    }
}

/* =========================================================================
 * 7. WIERSZ I BLOK
 * ========================================================================= */

/*
 * Wypelnia jeden wiersz docelowy (dst_w pikseli) z pelnego zrodla.
 * Wywolujacy gwarantuje, ze dst ma miejsce na dst_w pikseli.
 */
inline void skalowanie_wiersz(
    uint32_t* dst,
    const SkalowanieZrodlo& z,
    uint32_t dst_y,
    uint32_t dst_w,
    uint32_t dst_h,
    uint32_t filtr
) {
    if (!dst || !skalowanie_zrodlo_poprawne(z) || dst_w == 0U || dst_h == 0U) return;
    for (uint32_t x = 0; x < dst_w; ++x) {
        dst[x] = skalowanie_piksel(z, x, dst_y, dst_w, dst_h, filtr);
    }
}

/*
 * Wypelnia wycinek wiersza docelowego: dst[0..liczba-1] odpowiada pikselom
 * dst_x = pierwszy_x + i. Pozwala pisac bezposrednio do przycietego wiersza
 * warstwy bez bufora posredniego (jadro ma male stosy).
 */
inline void skalowanie_wiersz_wycinek(
    uint32_t* dst,
    const SkalowanieZrodlo& z,
    uint32_t dst_y,
    uint32_t dst_w,
    uint32_t dst_h,
    uint32_t pierwszy_x,
    uint32_t liczba,
    uint32_t filtr
) {
    if (!dst || !skalowanie_zrodlo_poprawne(z) || dst_w == 0U || dst_h == 0U) return;
    for (uint32_t i = 0; i < liczba; ++i) {
        const uint32_t x = pierwszy_x + i;
        dst[i] = skalowanie_piksel(z, x, dst_y, dst_w, dst_h, filtr);
    }
}

/*
 * Wybiera filtr wynikajacy z proporcji. Powolne sciezki zachowuja jakosc,
 * szybkie - predykowalny koszt:
 *
 *   powiekszenie >= 2x  : nearest (interpolacja calkowita jest tu wizualnie
 *                         rownowazna, a 4x tansza),
 *   zmniejszenie >= 2x  : box (bez aliasingu),
 *   pozostale           : bilinear.
 */
inline uint32_t skalowanie_filtr_dla(uint32_t src_w, uint32_t src_h,
                                     uint32_t dst_w, uint32_t dst_h) {
    if (dst_w == 0U || dst_h == 0U) return SKALOWANIE_NEAREST;
    if (dst_w >= src_w * 2U && dst_h >= src_h * 2U) return SKALOWANIE_NEAREST;
    if (src_w >= dst_w * 2U && src_h >= dst_h * 2U) return SKALOWANIE_BOX;
    return SKALOWANIE_BILINEAR;
}

/*
 * Szybka sciezka 1:1 - bit-identyczna kopia bez zadnej matematyki.
 * Wykorzystuje te sama kopie blokowa co reszta grafiki jadra.
 */
inline void skalowanie_kopia_1do1(uint32_t* dst, const uint32_t* src, size_t count) {
    if (!dst || !src || count == 0U) return;
    graphics_copy_pixels(dst, src, count);
}
