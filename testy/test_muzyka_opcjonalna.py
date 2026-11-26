#!/usr/bin/env python3
"""Domyslne ISO nie pakuje i nie czyta prywatnej muzyki; MUZYKA_W_ISO=1 jest jawnym wlaczeniem demonstracji.

Bez zmian w dane/muzyka uzytkownika: zrodlo jest podmieniane na katalog tymczasowy.
"""
import importlib
import os
import sys
import tempfile
from pathlib import Path

KORZEN = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(KORZEN / 'narzedzia'))
n = 0


def check(w, opis):
    global n
    n += 1
    if not w:
        raise SystemExit('FAIL: ' + opis)


mk = (KORZEN / 'Makefile').read_text()
check('MUZYKA_W_ISO ?= 0' in mk and 'MUZYKA_W_ISO ?= 1' not in mk, 'Makefile: domyslnie bez muzyki')
with tempfile.TemporaryDirectory() as tmp:
    for env, oczek in ((None, False), ('0', False), ('nie', False), ('1', True), ('tak', True)):
        if env is None:
            os.environ.pop('MUZYKA_W_ISO', None)
        else:
            os.environ['MUZYKA_W_ISO'] = env
        pm = importlib.reload(importlib.import_module('package_muzyka'))
        check(pm.wlaczona() is oczek, 'wlaczona() dla %r' % env)
        zrodlo = Path(tmp) / 'muzyka'
        zrodlo.mkdir(exist_ok=True)
        (zrodlo / 'utwor.wav').write_bytes(b'RIFF' + bytes(40))
        odczytano = []
        pm.ZRODLO = zrodlo
        orig_iterdir = Path.iterdir

        def sledz(self):
            odczytano.append(str(self))
            return orig_iterdir(self)
        Path.iterdir = sledz
        try:
            pliki = pm.zbierz(zrodlo)
        finally:
            Path.iterdir = orig_iterdir
        check(bool(pliki) is oczek, 'zbierz() dla %r' % env)
        check(oczek or not odczytano, 'wylaczone: katalog muzyki nie jest nawet listowany (%r)' % env)
        stage = Path(tmp) / ('stage_%s' % env)
        (stage / 'boot/grub').mkdir(parents=True, exist_ok=True)
        (stage / 'boot/grub/grub.cfg').write_text('menuentry "x" {\n    module2 /boot/muzyka.pak muzyka.pak\n}\n')
        (stage / 'boot/muzyka.pak').write_bytes(b'stary')
        if not oczek:
            pm.package(stage)
            check(not (stage / 'boot/muzyka.pak').exists(), 'stary modul usuniety ze stagingu')
            check('muzyka.pak' not in (stage / 'boot/grub/grub.cfg').read_text(), 'grub.cfg bez modulu muzyki')
os.environ.pop('MUZYKA_W_ISO', None)
print('Muzyka w ISO: %d sprawdzen OK (domyslnie wylaczona, MUZYKA_W_ISO=1 jawnie wlacza)' % n)
