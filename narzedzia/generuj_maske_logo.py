#!/usr/bin/env python3
"""Generuje maske pokrycia logotypu Bursztyn OS dla ekranu powitalnego.

Zrodlem jest oficjalny plik obrazy/logo.png (monochromatyczny bursztynowy
krysztal na przezroczystym tle). Jadro nie dekoduje PNG podczas startu:
dekoder DEFLATE wymagalby wielomegabajtowego sterty i setek milisekund.
Zamiast tego maska pokrycia (kanal alfa) jest filtrowana box-filterem do
stalej rozdzielczosci i wkompilowana w .rodata jako uint8_t[].

Uzycie:
    python narzedzia/generuj_maske_logo.py \
        --zrodlo obrazy/logo.png --wyjscie jadro/gui/logo_maska_dane.h

Plik wyjsciowy jest deterministyczny: ten sam PNG daje identyczny naglowek.
"""

import argparse
import os
import struct
import sys
import zlib

# Docelowa wysokosc maski. 320 px pokrywa 720p, 1080p i 1080p przy 125%
# skalowaniu UI bez widocznej utraty ostrosci (docelowo 144..300 px).
WYSOKOSC_MASKI = 320

# Rozmyta poswiata pod logotypem: 1/5 rozdzielczosci rdzenia, wiec jej koszt
# w .rodata jest pomijalny, a miekkie przejscia i tak powstaja z interpolacji.
WYSOKOSC_POSWIATY = 64
PROMIEN_ROZMYCIA = 2
PRZEJSCIA_ROZMYCIA = 2

# Pole widzenia poswiaty jest wieksze od rdzenia o tyle procent z kazdej strony.
ROZSZERZENIE_PROC = 9

# Prog alfa przycinajacy puste marginesy pliku zrodlowego.
PROG_PRZYCIA = 8


def fnv1a(dane):
    """FNV-1a 32 bit; skrot zrodla jest zapisany w naglowku i sprawdzany testem."""
    h = 0x811C9DC5
    for b in dane:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def wczytaj_png(sciezka):
    """Zwraca (szerokosc, wysokosc, bajty RGBA)."""
    dane = open(sciezka, 'rb').read()
    if dane[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit('To nie jest plik PNG: %s' % sciezka)
    pos, idat, w, h, bpp = 8, b'', 0, 0, 0
    while pos + 8 <= len(dane):
        dlugosc = struct.unpack('>I', dane[pos:pos + 4])[0]
        typ = dane[pos + 4:pos + 8]
        cialo = dane[pos + 8:pos + 8 + dlugosc]
        if pos + 12 + dlugosc > len(dane):
            raise SystemExit('Obciety chunk PNG')
        if typ == b'IHDR':
            w, h, bit, kolor, kompresja, filtr, przeplot = struct.unpack('>IIBBBBB', cialo)
            if bit != 8 or kolor not in (2, 6) or kompresja != 0 or filtr != 0 or przeplot != 0:
                raise SystemExit('Nieobslugiwany PNG (wymagane 8 bit, RGB/RGBA, bez przeplotu)')
            bpp = 4 if kolor == 6 else 3
        elif typ == b'IDAT':
            idat += cialo
        pos += 12 + dlugosc
    if not w or not h:
        raise SystemExit('Brak naglowka IHDR')

    surowe = zlib.decompress(idat)
    krok = w * bpp
    if len(surowe) != h * (krok + 1):
        raise SystemExit('Nieoczekiwany rozmiar danych PNG')

    rgba = bytearray(w * h * 4)
    poprzedni = bytearray(krok)
    p = 0
    for y in range(h):
        filtr = surowe[p]
        p += 1
        linia = bytearray(surowe[p:p + krok])
        p += krok
        if filtr == 1:
            for x in range(bpp, krok):
                linia[x] = (linia[x] + linia[x - bpp]) & 0xFF
        elif filtr == 2:
            for x in range(krok):
                linia[x] = (linia[x] + poprzedni[x]) & 0xFF
        elif filtr == 3:
            for x in range(krok):
                a = linia[x - bpp] if x >= bpp else 0
                linia[x] = (linia[x] + ((a + poprzedni[x]) >> 1)) & 0xFF
        elif filtr == 4:
            for x in range(krok):
                a = linia[x - bpp] if x >= bpp else 0
                b = poprzedni[x]
                c = poprzedni[x - bpp] if x >= bpp else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                linia[x] = (linia[x] + pred) & 0xFF
        elif filtr != 0:
            raise SystemExit('Nieznany filtr PNG: %d' % filtr)
        baza = y * w * 4
        for x in range(w):
            o = x * bpp
            j = baza + x * 4
            rgba[j] = linia[o]
            rgba[j + 1] = linia[o + 1]
            rgba[j + 2] = linia[o + 2]
            rgba[j + 3] = linia[o + 3] if bpp == 4 else 255
        poprzedni = linia
    return w, h, bytes(rgba)


def przycienije(px, w, h):
    """Prostokat otaczajacy piksele o alfie powyzej progu."""
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            if px[(y * w + x) * 4 + 3] > PROG_PRZYCIA:
                if x < x0:
                    x0 = x
                if x > x1:
                    x1 = x
                if y < y0:
                    y0 = y
                if y > y1:
                    y1 = y
    if x1 < x0 or y1 < y0:
        raise SystemExit('Logo nie zawiera zadnego widocznego piksela')
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1


def maska(px, w, h, x0, y0, bw, bh, wys):
    """Box-filter alfa z prostokata (x0,y0,bw,bh) do maski o wysokosci wys.

    Prostokat moze wykraczac poza obraz (poswiata): takie piksele maja alfe 0.
    Zachowane sa proporcje, wiec maska zawsze opisuje to samo pole widzenia.
    """
    szer = max(1, (bw * wys + bh // 2) // bh)
    out = bytearray(szer * wys)
    for ty in range(wys):
        sy0 = y0 + (ty * bh) // wys
        sy1 = max(sy0 + 1, y0 + ((ty + 1) * bh) // wys)
        wiersz = ty * szer
        for tx in range(szer):
            sx0 = x0 + (tx * bw) // szer
            sx1 = max(sx0 + 1, x0 + ((tx + 1) * bw) // szer)
            suma = 0
            ile = 0
            for sy in range(sy0, sy1):
                if sy < 0 or sy >= h:
                    ile += sx1 - sx0
                    continue
                baza = sy * w * 4
                for sx in range(sx0, sx1):
                    if 0 <= sx < w:
                        suma += px[baza + sx * 4 + 3]
                    ile += 1
            out[wiersz + tx] = suma // ile if ile else 0
    return szer, wys, bytes(out)


def rozmycie(m, szer, wys, promien, przejscia):
    """Box-blur maski; poswiata nie moze byc jasniejsza od rdzenia."""
    biezacy = bytearray(m)
    for _ in range(przejscia):
        etap = bytearray(len(biezacy))
        for y in range(wys):
            baza = y * szer
            for x in range(szer):
                suma = 0
                ile = 0
                for k in range(x - promien, x + promien + 1):
                    if 0 <= k < szer:
                        suma += biezacy[baza + k]
                        ile += 1
                etap[baza + x] = suma // ile
        wynik = bytearray(len(etap))
        for y in range(wys):
            for x in range(szer):
                suma = 0
                ile = 0
                for k in range(y - promien, y + promien + 1):
                    if 0 <= k < wys:
                        suma += etap[k * szer + x]
                        ile += 1
                wynik[y * szer + x] = suma // ile
        biezacy = wynik
    return bytes(biezacy)


def przytnij_poswiate(poswiata, pw, ph, rdzen, sw, sh, ox, oy, ow, oh):
    """Poswiata nie moze byc jasniejsza od rdzenia w tym samym punkcie obrazu.

    Obie tablice opisuja rozne pola widzenia, wiec porownanie wymaga mapowania
    wspolrzednych przez promile zapisane w naglowku.
    """
    wynik = bytearray(poswiata)
    for y in range(ph):
        ry = oy + y * (oh - oy) // ph
        sy = ry * sh // 1000
        if sy < 0 or sy >= sh:
            continue
        for x in range(pw):
            rx = ox + x * (ow - ox) // pw
            sx = rx * sw // 1000
            if sx < 0 or sx >= sw:
                continue
            i = y * pw + x
            j = sy * sw + sx
            if wynik[i] > rdzen[j]:
                wynik[i] = rdzen[j]
    return bytes(wynik)


def tablica(nazwa, dane, kolumny):
    linie = []
    for y in range(len(dane) // kolumny):
        baza = y * kolumny
        wartosci = ['%3d' % dane[baza + x] for x in range(kolumny)]
        linie.append('    ' + ','.join(wartosci) + ',')
    return 'inline constexpr uint8_t %s[%d] = {\n%s\n};\n' % (nazwa, len(dane), '\n'.join(linie))


def zakres_wierszy(dane, szer):
    """Pierwszy i ostatni wiersz z niezerowym pokryciem (ink bounds)."""
    gora, dol = -1, -1
    for y in range(len(dane) // szer):
        if any(dane[y * szer:(y + 1) * szer]):
            if gora < 0:
                gora = y
            dol = y
    return (gora, dol) if gora >= 0 else (0, len(dane) // szer - 1)


def zakres_kolumn(dane, szer):
    """Pierwsza i ostatnia kolumna z niezerowym pokryciem."""
    lewy, prawy = -1, -1
    for x in range(szer):
        if any(dane[y * szer + x] for y in range(len(dane) // szer)):
            if lewy < 0:
                lewy = x
            prawy = x
    return (lewy, prawy) if lewy >= 0 else (0, szer - 1)


def zapisz(sciezka, zw, zh, szer, wys, m, pw, ph, poswiata, ox, ow, oy, oh,
           suma_kontrolna, zrodlo):
    mg, md = zakres_wierszy(m, szer)
    pg, pd = zakres_wierszy(poswiata, pw)
    ml, mr = zakres_kolumn(m, szer)
    pl, pr = zakres_kolumn(poswiata, pw)
    tresc = (
        '#pragma once\n'
        '#include <stdint.h>\n'
        '\n'
        '// PLIK GENEROWANY przez narzedzia/generuj_maske_logo.py -- nie edytowac.\n'
        '// Zrodlo: %s (8-bit RGBA PNG).\n'
        '// Maski pokrycia logotypu: kanal alfa po box-filterze i przycieciu marginesow.\n'
        '// Kolor logotypu jest jednolity (bursztyn), wiec pokrycie == alfa niezaleznie\n'
        '// od jasnosci; renderer mnozy maske przez wybierany kolor i alfe klatki.\n'
        'namespace BursztynLogo {\n'
        'inline constexpr uint32_t SUMA_KONTROLNA_ZRODLA = 0x%08Xu;\n'
        'inline constexpr int ZRODLO_W = %d;\n'
        'inline constexpr int ZRODLO_H = %d;\n'
        '// Rdzen logotypu (ostry, z pelnym pokryciem krawedzi).\n'
        'inline constexpr int SZEROKOSC = %d;\n'
        'inline constexpr int WYSOKOSC = %d;\n'
        '// Rozmyta poswiata: nizsza rozdzielczosc, szersze pole widzenia.\n'
        'inline constexpr int POSWIATA_SZEROKOSC = %d;\n'
        'inline constexpr int POSWIATA_WYSOKOSC = %d;\n'
        '// Pole widzenia poswiaty wzgledem rdzenia w promilach (0 = ta sama krawedz).\n'
        'inline constexpr int POSWIATA_OD_X = %d;\n'
        'inline constexpr int POSWIATA_DO_X = %d;\n'
        'inline constexpr int POSWIATA_OD_Y = %d;\n'
        'inline constexpr int POSWIATA_DO_Y = %d;\n'        '// Rzeczywiste granice atramentu w wierszach maski (uzywane przez uklad,\n'
        '// zeby grupa byla wysrodkowana na widocznych pikselach, nie na marginesach).\n'
        'inline constexpr int MASKA_ATRAMENT_OD = %d;\n'
        'inline constexpr int MASKA_ATRAMENT_DO = %d;\n'
        'inline constexpr int MASKA_ATRAMENT_LEWY = %d;\n'
        'inline constexpr int MASKA_ATRAMENT_PRAWY = %d;\n'
        'inline constexpr int POSWIATA_ATRAMENT_OD = %d;\n'
        'inline constexpr int POSWIATA_ATRAMENT_DO = %d;\n'
        'inline constexpr int POSWIATA_ATRAMENT_LEWY = %d;\n'
        'inline constexpr int POSWIATA_ATRAMENT_PRAWY = %d;\n'
        '%s'
        '%s'
        '}\n'
    ) % (
        zrodlo, suma_kontrolna, zw, zh, szer, wys, pw, ph,
        ox, ow, oy, oh, mg, md + 1, ml, mr + 1, pg, pd + 1, pl, pr + 1,
        tablica('MASKA', m, 32),
        tablica('POSWIATA', poswiata, 32),
    )
    with open(sciezka, 'w', encoding='utf-8', newline='\n') as f:
        f.write(tresc)


def main():
    ap = argparse.ArgumentParser(description='Generuje maske logo dla ekranu powitalnego.')
    ap.add_argument('--zrodlo', default='obrazy/logo.png')
    ap.add_argument('--wyjscie', default='jadro/gui/logo_maska_dane.h')
    ap.add_argument('--wysokosc', type=int, default=WYSOKOSC_MASKI)
    args = ap.parse_args()
    # Sciezka zrodla w naglowku jest niezalezna od systemu plikow, wiec
    # regeneracja na Windows i w WSL daje identyczny plik.
    args.zrodlo = args.zrodlo.replace(os.sep, '/')

    w, h, px = wczytaj_png(args.zrodlo)
    x0, y0, bw, bh = przycienije(px, w, h)
    szer, wys, m = maska(px, w, h, x0, y0, bw, bh, args.wysokosc)
    # Poswiata: to samo pole widzenia plus margines z KAZDEJ strony. Zapisywane
    # promile to lewa i prawa krawedz wzgledem rdzenia, wiec pole jest symetryczne.
    padx = max(1, bw * ROZSZERZENIE_PROC // 100)
    pady = max(1, bh * ROZSZERZENIE_PROC // 100)
    pw, ph, poswiata = maska(px, w, h, x0 - padx, y0 - pady, bw + 2 * padx, bh + 2 * pady,
                             WYSOKOSC_POSWIATY)
    poswiata = rozmycie(poswiata, pw, ph, PROMIEN_ROZMYCIA, PRZEJSCIA_ROZMYCIA)
    # Promile pola widzenia poswiaty wzgledem rdzenia (lewa/prawa, gora/dol).
    ox = (-padx * 1000) // bw
    ow = ((bw + padx) * 1000) // bw
    oy = (-pady * 1000) // bh
    oh = ((bh + pady) * 1000) // bh
    poswiata = przytnij_poswiate(poswiata, pw, ph, m, szer, wys, ox, oy, ow, oh)
    zapisz(args.wyjscie, w, h, szer, wys, m, pw, ph, poswiata,
           ox, ow, oy, oh,
           fnv1a(open(args.zrodlo, 'rb').read()), args.zrodlo)
    print('Logo %dx%d bbox %dx%d -> maska %dx%d (%d B) + poswiata %dx%d (%d B) -> %s'
          % (w, h, bw, bh, szer, wys, szer * wys, pw, ph, pw * ph, args.wyjscie))
    return 0


if __name__ == '__main__':
    sys.exit(main())
