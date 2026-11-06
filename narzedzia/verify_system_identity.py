#!/usr/bin/env python3
"""Check production OS versions against the preprocessed canonical header."""
import ast
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
source='#include "jadro/rdzen/wersja_systemu.h"\nconst char* checked_version=BURSZTYN_NAZWA_WERSJI;\n'
expanded=subprocess.check_output(['g++','-E','-P','-x','c++','-I',str(ROOT),'-'],input=source,text=True)
line=re.search(r'checked_version\s*=([^;]+);',expanded).group(1)
version=''.join(ast.literal_eval(x) for x in re.findall(r'"[^"\\]*(?:\\.[^"\\]*)*"',line))
raw_version=version.removeprefix('Bursztyn OS v')
results={}
for name in ('system_operacyjny.bin','shell.bin','ustawienia.bin','diagnostyka.bin'):
    data=(ROOT/name).read_bytes()
    versions={v.decode() for v in re.findall(rb'Bursztyn OS v[0-9]+(?:\.[0-9]+){2,3}',data)}
    assert versions=={version},(name,versions,version)
    results[name]=sorted(versions)
assert raw_version.encode() in (ROOT/'aktualizacje.bin').read_bytes(),'Updater fallback does not use canonical version'
assert 'BURSZTYN_WERSJA_TEKST' in (ROOT/'jadro/aktualizacja/aktualizacja.cpp').read_text(),'Updater status source'
assert 'BURSZTYN_NAZWA_WERSJI' in (ROOT/'jadro/diag/diagnostyka_systemu_format.h').read_text(),'Diagnostics source'
assert 'BURSZTYN_NAZWA_WERSJI' in (ROOT/'jadro/rdzen/kernel.cpp').read_text(),'Boot banner source'
assert (ROOT/'isodir/boot/ustawienia.bur').read_bytes()==(ROOT/'ustawienia.bin').read_bytes(),'Packaged Settings payload'
print(json.dumps(dict(result='PASS',version=version,binaries=results,updater_fallback=raw_version),ensure_ascii=False,indent=2))
