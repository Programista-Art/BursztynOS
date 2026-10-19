#!/usr/bin/env python3
"""Validate the shipped diagnostics BUR and its native launcher registration."""
from pathlib import Path
import re,struct,subprocess
root=Path(__file__).resolve().parents[1]
blob=(root/'diagnostyka.bin').read_bytes()
magic,entry,to,ts,tv,do,ds,dv=struct.unpack_from('<I7Q',blob)
assert magic==0x00525542 and to==4096 and tv==0x1001000
assert tv<=entry<tv+ts and to+ts==do and tv+ts==dv
assert len(blob)<512*1024 and 0<ds<=8*1024*1024
assert len(blob)-do<ds,'BUR BSS must use native trailing zero-fill'
elf=root/'diagnostyka.elf'
program_headers=subprocess.check_output(['readelf','-W','-l',str(elf)],text=True)
loads=[line for line in program_headers.splitlines() if line.strip().startswith('LOAD')]
assert len(loads)==3 and not any('RWE' in line.replace(' ','') for line in loads)
assert ' E ' not in loads[-1] and ' RW ' in loads[-1]
symbols=subprocess.check_output(['nm','-C',str(elf)],text=True)
assert not re.search(r'\bU\b|_GLOBAL__sub_I|__cxa_throw|__gxx_personality',symbols)
# Native linker combines read-only constants with TEXT. Inspect executable
# input sections, so font and jump-table data are not decoded as instructions.
assembly=subprocess.check_output(['objdump','-d',str(root/'diagnostyka_tmp.o'),str(root/'bursztyn_gui.o')],text=True)
assert not re.search(r'\t(?:cli|sti|cpuid|rdmsr|wrmsr|in[bwl]?|out[bwl]?)\s',assembly),'Ring 3 utility must not access hardware'
assert 'diag_publish' not in symbols and 'pci_odczytaj' not in symbols and 'hda_transport_' not in symbols
api=(root/'bws_api.h').read_text();assert 'BWS_DIAGNOSTYKA_SYSTEMU = 91' in api and 'BWS_PIERWSZY_WOLNY = 92' in api
path='/programy/diagnostyka.cebula/diagnostyka.bur'
assert path in (root/'desktop_entries.h').read_text()
assert '"/programy/diagnostyka.cebula"' in (root/'psf_bootstrap.h').read_text()
assert path in (root/'menedzer_okien.cpp').read_text()
assert 'MANIFEST_DIAGNOSTYKA' in (root/'kernel.cpp').read_text()
print(f'Diagnostics native BUR/permissions/desktop/Start/bootstrap: PASS (file={len(blob)}, data+BSS={ds})')
