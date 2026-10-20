#!/usr/bin/env python3
"""Bound native stack frames on the real 16 KiB Ring 3 stack.

This complements QEMU navigation: compiler frame sizes cannot establish a full
call-graph bound, but catch large persistent event/publication frames directly.
"""
from pathlib import Path

frames = []
for line in Path('przegladarka_tmp.su').read_text().splitlines():
    name, size, kind = line.rsplit('\t', 2)
    frames.append((name, int(size), kind))
assert frames, 'No compiler stack-usage records'
assert all(size <= 6144 and kind in ('static', 'dynamic,bounded') for _, size, kind in frames), \
    'An unbounded or oversized frame endangers the 16 KiB user stack'
for token, budget in [('_start()', 1024), ('przygotuj_dokument_zakladki(', 1024),
                      ('OknoHusarz::obsluz_zdarzenie(', 512),
                      ('WidokStrony::obsluz_wcisniecie_myszy(', 256)]:
    matches = [(name, size) for name, size, _ in frames if token in name]
    assert matches, 'Missing native frame: ' + token
    assert all(size <= budget for _, size in matches), (token, budget, matches)
print('Husarz native stack budgets PASS; largest frame:', max(frames, key=lambda f: f[1]))
