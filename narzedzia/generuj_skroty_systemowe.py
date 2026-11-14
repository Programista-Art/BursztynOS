#!/usr/bin/env python3
"""Generuje jadro/fs/skroty_systemowe.generated.h: SHA-256 plikow .bur aplikacji systemowych.

Uzycie: generuj_skroty_systemowe.py WYJSCIE.h /sciezka/w/PSF=plik.bin ...
Tablice trafia do obrazu jadra; patrz jadro/fs/tozsamosc_systemowa.h.
"""
import hashlib, sys
from pathlib import Path

out = Path(sys.argv[1])
rows = []
for spec in sys.argv[2:]:
    path, file = spec.split('=', 1)
    data = Path(file).read_bytes()
    if len(data) < 64:
        raise SystemExit(f'{file}: podejrzanie maly plik ({len(data)} B)')
    d = hashlib.sha256(data).digest()
    rows.append((path, d, len(data)))
rows.sort()
lines = ['// GENEROWANY PLIK (narzedzia/generuj_skroty_systemowe.py) - nie edytowac.',
         '#pragma once', '#include "tozsamosc_typy.h"',
         'inline constexpr Wpis kWbudowaneSkroty[] = {']
for path, d, n in rows:
    lines.append('    {"%s", {%s}, %d},' % (path, ','.join('0x%02x' % b for b in d), n))
lines.append('};')
text = '\n'.join(lines) + '\n'
if not out.exists() or out.read_text() != text:
    out.write_text(text)
