#!/usr/bin/env python3
"""Stage the three plain-text Support assets; preserve exact source bytes."""
from pathlib import Path
import shutil
import sys
ROOT=Path(__file__).resolve().parents[1]
NAMES=('mecenasi.txt','sponsorzy.txt','wkladtworczy.txt')
def package(destination):
    target=Path(destination)/'dane/sponsorze';target.mkdir(parents=True,exist_ok=True)
    for name in NAMES:
        source=ROOT/'dane/sponsorze'/name
        shutil.copyfile(source,target/name)
        if (target/name).read_bytes()!=source.read_bytes():raise RuntimeError(name)
    print('Support: staged three byte-identical source files')
if __name__=='__main__':package(sys.argv[1])
