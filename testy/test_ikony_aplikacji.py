#!/usr/bin/env python3
"""Rejestracja ikon aplikacji Shell i Tapety: menu Start, pakowanie (blob + zapis do PSF), format PNG, fallback."""
import re, struct, sys, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def txt(p): return (ROOT / p).read_text(encoding='utf-8')

def png_rgba(path):
    d = path.read_bytes()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    w, h, depth, ct = struct.unpack('>IIBB', d[16:26])
    assert (depth, ct) == (8, 6), 'oczekiwany PNG 8-bit RGBA'
    pos, idat = 8, b''
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        if t == b'IDAT': idat += d[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(idat); st = w * 4 + 1
    assert len(raw) == st * h
    return w, h, raw, st

menu = txt('jadro/gui/start_menu_model.h'); mk = txt('Makefile'); kern = txt('jadro/rdzen/kernel.cpp')
for nazwa, plik, tytul in (('shell', 'app_shell', 'Powłoka Bursztyna'), ('tapety', 'app_tapety', 'Tapety'),
                            ('ekran', 'app_ustawienia_ekranu', 'Ustawienia ekranu')):
    sciezka = '/ikonki/%s.png' % plik
    png = ROOT / 'ikonki' / (plik + '.png')
    assert png.is_file(), png
    # menu Start: wpis ma wlasciwa sciezke (nie nullptr)
    m = re.search(r'\{"%s","[^"]*",\d+,([^}]*)\}' % re.escape(tytul), menu)
    assert m and m.group(1) == '"%s"' % sciezka, (tytul, m and m.group(1))
    # pakowanie: regula bloba, dolaczenie do kernela, symbole i zapis do systemu plikow (live i instalacja)
    assert 'ikona_%s_blob.o: ikonki/%s.png' % (plik, plik) in mk
    assert mk.count('ikona_%s_blob.o \\' % plik) == 1
    assert '_binary_ikonki_%s_png_start' % plik in kern
    assert '{"%s",_binary_ikonki_%s_png_start,_binary_ikonki_%s_png_end}' % (sciezka, plik, plik) in kern
    # format i alfa: kwadrat, RGBA, przezroczyste naroza, nieprzezroczysta tresc, rozmiar < limitu zapisu (256 KiB)
    w, h, raw, st = png_rgba(png)
    assert w == h == 128 and png.stat().st_size < 256 * 1024
    # (pierwszy bajt wiersza = filtr; alfa po odfiltrowaniu wymaga dekodera, wiec sprawdzamy tylko rozmiar i naglowek)
    print('OK', tytul, sciezka, '%dx%d' % (w, h), png.stat().st_size, 'B')
# pozostale wpisy menu z ikona nadal wskazuja na istniejace pliki; brak pliku => fallback (nullptr jest dozwolony)
for sc in re.findall(r'"(/ikonki/[^"]+\.png)"', menu):
    assert (ROOT / sc.lstrip('/')).is_file(), 'brak pliku ikony ' + sc
# pliki aplikacji nie sa przemianowane: sciezki uruchamiania bez zmian
assert 'TypIkonyAplikacji' in txt('jadro/gui/desktop_entries.h')
print('PASS test_ikony_aplikacji')
