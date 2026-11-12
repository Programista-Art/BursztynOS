#!/usr/bin/env python3
"""Verify trusted fonts, notices and the Ring 3 Settings payload in the ISO."""
import hashlib
import pathlib
import subprocess
import sys
import tempfile
from bsf_release_assets import assets as release_assets

root = pathlib.Path(__file__).resolve().parents[1]
iso = pathlib.Path(sys.argv[1]).resolve()
assets = release_assets(root)
with tempfile.TemporaryDirectory(prefix='bsf-iso-') as directory:
    target = pathlib.Path(directory)
    # BSF 2.2: fonts are staged per format directory; notices in /czcionki/ttf.
    font_dir = {'.ttf': 'ttf', '.otf': 'otf', '.ttc': 'ttc', '.otc': 'otc'}
    used = sorted({font_dir.get(p.suffix.lower(), 'ttf') for p in assets})
    for source, destination in [('/czcionki/' + d, target / d) for d in used] + [
            ('/boot/ustawienia.bur', target / 'ustawienia.bur')]:
        subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(iso),
                        '-extract', source, str(destination)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    for original, packaged in [(p, target / font_dir.get(p.suffix.lower(), 'ttf') / p.name) for p in assets] + [
            (root / 'ustawienia.bin', target / 'ustawienia.bur')]:
        if hashlib.sha256(original.read_bytes()).digest() != hashlib.sha256(packaged.read_bytes()).digest():
            raise SystemExit('BSF ISO mismatch: ' + original.name)
print('BSF ISO: PASS,', len(assets), 'font/license/source assets and Settings verified')
