#!/usr/bin/env python3
"""Generuje jadro/gui/kursor_rozciagania.generated.h z ikonki/kursor_rozciagania.png.

Uzycie: generuj_kursor_rozciagania.py WYJSCIE.h [PLIK.png ...]
Pierwszy istniejacy plik z listy jest zrodlem; gdy zadnego nie ma, powstaje
naglowek z KURSOR_ROZCIAGANIA_DOSTEPNY=0 (jadro uzywa wtedy wbudowanych ksztaltow).

Rysunek jest POZIOMA dwustronna strzalka. Wariant EW to rysunek 1:1 pomniejszony
proporcjonalnie (usrednianie powierzchni, bez znieksztalcenia proporcji); NS to
obrot o 90 stopni, NWSE/NESW obrot o 45 stopni tego samego rysunku - nie powstaje
zadna nowa grafika. Alfa jest progowana (>=128), bo kursor jadra ma alfe binarna.
Kolory zrodla nie sa zmieniane. Wynik jest deterministyczny (czysty Python, bez
zewnetrznych bibliotek; tylko PNG 8-bit RGBA/RGB bez przeplotu).
"""
import math, struct, sys, zlib
from pathlib import Path

DOCELOWA_SZEROKOSC = 32      # px poziomej strzalki (1x); pozostale warianty z niej wynikaja
MAKS_BOK = 64                # limit plane kursora w jadrze (kursor_plane_piksele 64x64)
PROG_ALFY = 128
PROBKI = 4                   # PROBKI x PROBKI probek na piksel docelowy


def wczytaj_png(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('to nie jest PNG')
    pos, idat, ihdr = 8, b'', None
    while pos + 8 <= len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if len(body) != n:
            raise ValueError('uciety fragment PNG')
        pos += 12 + n
        if typ == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', body)
        elif typ == b'IDAT':
            idat += body
        elif typ == b'IEND':
            break
    if not ihdr:
        raise ValueError('brak IHDR')
    w, h, depth, ctype, _, _, inter = ihdr
    if depth != 8 or ctype not in (2, 6) or inter != 0:
        raise ValueError('obslugiwany tylko PNG 8-bit RGB/RGBA bez przeplotu')
    if not (0 < w <= 1024 and 0 < h <= 1024):
        raise ValueError('niedozwolone wymiary PNG')
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    if len(raw) != (stride + 1) * h:
        raise ValueError('zly rozmiar danych PNG')
    out, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        cur = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: cur[i] = (cur[i] + a) & 255
            elif f == 2: cur[i] = (cur[i] + b) & 255
            elif f == 3: cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 255
            elif f != 0:
                raise ValueError('nieznany filtr PNG')
        out.append(cur)
        prev = cur
    px = [[(r[x * bpp], r[x * bpp + 1], r[x * bpp + 2], r[x * bpp + 3] if bpp == 4 else 255)
           for x in range(w)] for r in out]
    return w, h, px


def probka(px, w, h, fx, fy):
    """Alfa i kolor (premultiplied) w punkcie zrodla; poza obrazem = przezroczysty."""
    if fx < 0 or fy < 0 or fx > w - 1 or fy > h - 1:
        return 0.0, 0.0, 0.0, 0.0
    x0, y0 = int(fx), int(fy)
    x1, y1 = min(x0 + 1, w - 1), min(y0 + 1, h - 1)
    tx, ty = fx - x0, fy - y0
    acc = [0.0] * 4
    for (xx, yy, wgt) in ((x0, y0, (1 - tx) * (1 - ty)), (x1, y0, tx * (1 - ty)),
                          (x0, y1, (1 - tx) * ty), (x1, y1, tx * ty)):
        r, g, b, a = px[yy][xx]
        al = a / 255.0
        acc[0] += wgt * al; acc[1] += wgt * r * al; acc[2] += wgt * g * al; acc[3] += wgt * b * al
    return tuple(acc)


def wariant(px, w, h, kat_deg, skala):
    """Obraca zrodlo o kat (zgodnie z ruchem wskazowek) i skaluje; zwraca (W,H,[ARGB])."""
    kat = math.radians(kat_deg)
    c, s = math.cos(kat), math.sin(kat)
    sw, sh = w * skala, h * skala
    bw = abs(sw * c) + abs(sh * s)
    bh = abs(sw * s) + abs(sh * c)
    W, H = int(math.ceil(bw - 1e-6)), int(math.ceil(bh - 1e-6))
    if W > MAKS_BOK or H > MAKS_BOK:
        raise ValueError('wariant przekracza limit kursora')
    cx, cy = W / 2.0, H / 2.0
    out = []
    for y in range(H):
        for x in range(W):
            sa = sr = sg = sb = 0.0
            for j in range(PROBKI):
                for i in range(PROBKI):
                    dx = x + (i + .5) / PROBKI - cx
                    dy = y + (j + .5) / PROBKI - cy
                    # odwrotny obrot -> wspolrzedne zrodla
                    ux, uy = dx * c + dy * s, -dx * s + dy * c
                    a, r, g, b = probka(px, w, h, ux / skala + (w - 1) / 2.0, uy / skala + (h - 1) / 2.0)
                    sa += a; sr += r; sg += g; sb += b
            n = PROBKI * PROBKI
            alfa = sa / n
            if alfa * 255 < PROG_ALFY:
                out.append(0)
                continue
            r, g, b = (min(255, int(round(v / n / alfa))) for v in (sr, sg, sb))
            out.append(0xFF000000 | r << 16 | g << 8 | b)
    return W, H, out


def zbuduj(path):
    w, h, px = wczytaj_png(Path(path).read_bytes())
    skala = DOCELOWA_SZEROKOSC / float(w)
    return [('EW', wariant(px, w, h, 0, skala)),
            ('NS', wariant(px, w, h, 90, skala)),
            ('NWSE', wariant(px, w, h, 45, skala)),
            ('NESW', wariant(px, w, h, -45, skala))], (w, h)


def naglowek(zrodlo, warianty, wym):
    L = ['// GENEROWANY PLIK (narzedzia/generuj_kursor_rozciagania.py) - nie edytowac.',
         '#pragma once', '#include <stdint.h>',
         '#define KURSOR_ROZCIAGANIA_DOSTEPNY %d' % (1 if warianty else 0)]
    if not warianty:
        L.append('// Zrodlo ikonki/kursor_rozciagania.png nie istnieje: uzywane sa ksztalty wbudowane.')
        return '\n'.join(L) + '\n'
    L.append('// Zrodlo: %s (%dx%d)' % (Path(zrodlo).name, wym[0], wym[1]))
    for nazwa, (W, H, dane) in warianty:
        L.append('inline constexpr uint16_t kKursorRozciagania%sSzer = %d;' % (nazwa, W))
        L.append('inline constexpr uint16_t kKursorRozciagania%sWys = %d;' % (nazwa, H))
        L.append('inline constexpr uint32_t kKursorRozciagania%s[%d] = {' % (nazwa, W * H))
        for i in range(0, len(dane), 8):
            L.append('    ' + ','.join('0x%08x' % v for v in dane[i:i + 8]) + ',')
        L.append('};')
    return '\n'.join(L) + '\n'


def main():
    out = Path(sys.argv[1])
    zrodlo = next((p for p in sys.argv[2:] if Path(p).is_file()), None)
    if zrodlo:
        try:
            warianty, wym = zbuduj(zrodlo)
        except Exception as e:  # uszkodzony plik nie moze wywrocic budowania systemu
            sys.stderr.write('OSTRZEZENIE: %s: %s - uzywam ksztaltow wbudowanych\n' % (zrodlo, e))
            zrodlo, warianty, wym = None, [], (0, 0)
    else:
        warianty, wym = [], (0, 0)
    text = naglowek(zrodlo or '', warianty, wym)
    if not out.exists() or out.read_text() != text:
        out.write_text(text)


if __name__ == '__main__':
    main()
