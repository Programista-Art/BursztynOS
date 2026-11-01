#!/usr/bin/env python3
"""Stage only the five canonical power assets, verifying exact bytes."""
from pathlib import Path
import shutil
import sys
ROOT=Path(__file__).resolve().parents[1]
NAMES=tuple('przycisk'+n+'.png' for n in ('zasilania','zablokuj','uspij','zamknij','uruchomponownie'))
def package(destination):
    target=Path(destination)/'ikonki';target.mkdir(parents=True,exist_ok=True)
    for old in target.glob('przycisk_*.png'):old.unlink()
    for name in NAMES:
        source=ROOT/'ikonki'/name
        shutil.copyfile(source,target/name)
        assert source.read_bytes()==(target/name).read_bytes(),name
    print('Power: staged five byte-identical PNG assets')
if __name__=='__main__':package(sys.argv[1])
