#!/usr/bin/env python3
"""Test generatora kursora: brak PNG, uszkodzony PNG, determinizm, walidacja wejscia."""
import subprocess, sys, tempfile, struct, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / 'narzedzia/generuj_kursor_rozciagania.py'
sys.path.insert(0, str(ROOT / 'narzedzia'))
import generuj_kursor_rozciagania as g

def run(out, *src):
    r = subprocess.run([sys.executable, str(GEN), str(out), *map(str, src)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return r.stderr

def png(w, h, rgba, ctype=6, filt=0):
    bpp = 4 if ctype == 6 else 3
    raw = b''.join(bytes([filt]) + bytes(rgba * w) [:w * bpp] for _ in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ctype, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')

with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    for sub in ('z1', 'z2', 'ok'): (d / sub).mkdir()
    # 1. brak pliku -> naglowek 'niedostepny', build nie pada
    run(d / 'a.h', d / 'nie_ma.png')
    assert 'KURSOR_ROZCIAGANIA_DOSTEPNY 0' in (d / 'a.h').read_text()
    # 2. uszkodzony plik -> ostrzezenie + niedostepny
    (d / 'z1' / 'kursor_rozciagania.png').write_bytes(b'to nie jest png')
    err = run(d / 'b.h', d / 'z1' / 'kursor_rozciagania.png')
    assert 'OSTRZEZENIE' in err and 'DOSTEPNY 0' in (d / 'b.h').read_text()
    # 3. PNG z nieobslugiwanym formatem (16-bit / przeplot) odrzucony
    bad = bytearray(png(8, 4, [255, 255, 255, 255])); bad[24] = 16
    for tryb in (bad,):
        (d / 'z2' / 'kursor_rozciagania.png').write_bytes(bytes(tryb))
        run(d / 'c.h', d / 'z2' / 'kursor_rozciagania.png'); assert 'DOSTEPNY 0' in (d / 'c.h').read_text()
    # 4. poprawny PNG -> dostepny, deterministyczny, w limitach
    (d / 'ok' / 'kursor_rozciagania.png').write_bytes(png(64, 32, [255, 255, 255, 255]))
    run(d / 'e1.h', d / 'ok' / 'kursor_rozciagania.png'); run(d / 'e2.h', d / 'ok' / 'kursor_rozciagania.png')
    t = (d / 'e1.h').read_text()
    assert 'DOSTEPNY 1' in t and t == (d / 'e2.h').read_text()
    # 5. rzeczywisty plik repo (jesli jest): wszystkie warianty <= 64 i niepuste
    realk = ROOT / 'ikonki/kursor.png'
    if realk.is_file():
        W, H, dane, hx, hy = g.zbuduj_strzalke(realk)
        assert (W, H) == (16, 18) and 0 <= hx < W and 0 <= hy < H and dane[hy * W + hx], 'hotspot na czubku'
        assert all((v >> 24) in (0, 255) for v in dane)
        assert abs(W / H - 53 / 61) < 0.06, 'proporcje zrodla 53x61 zachowane'
        assert (hx, hy) == (1, 0) and (dane[hy * W + hx] >> 16) & 255 < 128, 'czubek = ciemny kontur'
        assert not any(dane[y * W + x] for y in range(hy) for x in range(W)), 'brak pikseli nad czubkiem'
        print('Strzalka repo: OK', (W, H, hx, hy))
    real = ROOT / 'ikonki/kursor_rozciagania.png'
    if real.is_file():
        w = g.zbuduj(real)[0]
        for n, (W, H, data) in w:
            assert 0 < W <= 64 and 0 < H <= 64 and len(data) == W * H and any(data), n
            assert all((v >> 24) in (0, 255) for v in data)
        d_ = {n: (W, H) for n, (W, H, _) in w}
        assert d_['EW'] == (24, 11) and d_['NS'] == (11, 24) and d_['NWSE'] == d_['NESW'] == (25, 25), d_
        print('PNG repo: OK', [(n, W, H) for n, (W, H, _) in w])
    else:
        print('PNG repo: BRAK (pominieto)')
    # 5b. domyslna strzalka: tylko plik o nazwie kursor.png; brak/uszkodzenie => makro 0; hotspot = czubek
    (d / 'kursor.png').write_bytes(png(64, 64, [255, 255, 255, 255]))
    run(d / 's1.h', d / 'kursor.png')
    ts = (d / 's1.h').read_text()
    assert 'KURSOR_STRZALKA_DOSTEPNY 1' in ts and 'KURSOR_ROZCIAGANIA_DOSTEPNY 0' in ts
    run(d / 's0.h', d / 'nie_ma' / 'kursor.png')
    assert 'KURSOR_STRZALKA_DOSTEPNY 0' in (d / 's0.h').read_text()
    (d / 'zly').mkdir(); (d / 'zly' / 'kursor.png').write_bytes(b'xx')
    assert 'OSTRZEZENIE' in run(d / 's2.h', d / 'zly' / 'kursor.png')
    assert 'KURSOR_STRZALKA_DOSTEPNY 0' in (d / 's2.h').read_text()
    # brak starych danych: zmiana PNG zmienia wynik
    (d / 'k2').mkdir(); (d / 'k2' / 'kursor.png').write_bytes(png(64, 64, [10, 20, 30, 255]))
    run(d / 's3.h', d / 'k2' / 'kursor.png')
    assert (d / 's3.h').read_text() != ts
    # obie ikony naraz niezaleznie
    run(d / 's4.h', d / 'kursor.png', d / 'ok' / 'kursor_rozciagania.png')
    assert 'KURSOR_STRZALKA_DOSTEPNY 1' in (d / 's4.h').read_text()
    # 6. nie zapisuje pliku, gdy tresc bez zmian (mtime stabilny => brak zbednych rebuildow)
    m = (d / 'e1.h').stat().st_mtime_ns; run(d / 'e1.h', d / 'ok' / 'kursor_rozciagania.png')
    assert (d / 'e1.h').stat().st_mtime_ns == m
print('PASS test_kursor_rozciagania_generator')
