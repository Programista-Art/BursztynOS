#!/usr/bin/env python3
"""Verify trusted fonts, notices and the Ring 3 Settings payload in the ISO."""
import hashlib
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
iso = pathlib.Path(sys.argv[1]).resolve()
assets = sorted(p for p in (root / 'czcionki/ttf').iterdir()
                if p.is_file() and p.suffix.lower() in ('.ttf', '.txt'))
with tempfile.TemporaryDirectory(prefix='bsf-iso-') as directory:
    target = pathlib.Path(directory)
    for source, destination in [('/czcionki/ttf', target / 'fonts'),
                                ('/boot/ustawienia.bur', target / 'ustawienia.bur')]:
        subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(iso),
                        '-extract', source, str(destination)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    for original, packaged in [(p, target / 'fonts' / p.name) for p in assets] + [
            (root / 'ustawienia.bin', target / 'ustawienia.bur')]:
        if hashlib.sha256(original.read_bytes()).digest() != hashlib.sha256(packaged.read_bytes()).digest():
            raise SystemExit('BSF ISO mismatch: ' + original.name)
print('BSF ISO: PASS,', len(assets), 'font/license/source assets and Settings verified')
