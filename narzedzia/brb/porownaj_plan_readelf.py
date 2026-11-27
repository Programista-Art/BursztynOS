#!/usr/bin/env python3
"""BRB - T-ELF-PROD: porownanie planu ladowania parsera BRB z niezaleznymi narzedziami (readelf, objdump).

Uzycie: porownaj_plan_readelf.py <test-brb-elf> <plik_jadra> [--readelf R] [--objdump O]
Porownuje: typ i architekture, punkt wejscia, liczbe naglowkow programu, kazdy PT_LOAD (offset, vaddr,
paddr, filesz, memsz, align, flagi), zakres stron i sumy, oraz polozenie naglowka Multiboot2 (wyszukane
niezaleznie w pliku). Kod wyjscia 0 = pelna zgodnosc.
"""
import argparse
import re
import struct
import subprocess
import sys


def main():
    a = argparse.ArgumentParser()
    a.add_argument('test')
    a.add_argument('jadro')
    a.add_argument('--readelf', default='x86_64-linux-gnu-readelf')
    a.add_argument('--objdump', default='x86_64-linux-gnu-objdump')
    a = a.parse_args()
    bledy = []
    brb = subprocess.run([a.test, '--jadro', a.jadro], capture_output=True, text=True)
    print(brb.stdout, end='')
    if brb.returncode != 0:
        print('[T-ELF-PROD] BLAD: parser BRB odrzucil jadro albo testy mutacyjne zawiodly')
        return 1
    re_out = subprocess.run([a.readelf, '-hlW', a.jadro], capture_output=True, text=True, check=True).stdout
    od_out = subprocess.run([a.objdump, '-p', a.jadro], capture_output=True, text=True, check=True).stdout

    def pole(wzor, tekst):
        m = re.search(wzor, tekst, re.M)
        return m.group(1) if m else None

    if 'EXEC' not in (pole(r'^\s*Type:\s+(.*)$', re_out) or ''):
        bledy.append('readelf: typ inny niz EXEC')
    if 'X86-64' not in (pole(r'^\s*Machine:\s+(.*)$', re_out) or ''):
        bledy.append('readelf: maszyna inna niz X86-64')
    wejscie_re = int(pole(r'^\s*Entry point address:\s+(0x[0-9a-f]+)', re_out), 16)
    wejscie_brb = int(pole(r'^PROD wejscie=(0x[0-9a-f]+)', brb.stdout), 16)
    if wejscie_re != wejscie_brb:
        bledy.append(f'wejscie: readelf {wejscie_re:#x} != BRB {wejscie_brb:#x}')
    phnum = int(pole(r'^\s*Number of program headers:\s+(\d+)', re_out))
    segmenty_re = []
    for m in re.finditer(r'^\s*LOAD\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+([RWE ]{1,3}?)\s+(0x[0-9a-f]+)\s*$',
                         re_out, re.M):
        segmenty_re.append(tuple(int(m.group(i), 16) for i in (1, 2, 3, 4, 5)) + (m.group(6).replace(' ', ''), int(m.group(7), 16)))
    segmenty_brb = []
    for m in re.finditer(r'^PROD seg ph=\d+ offset=(0x[0-9a-f]+) vaddr=(0x[0-9a-f]+) paddr=(0x[0-9a-f]+) filesz=(0x[0-9a-f]+) '
                         r'memsz=(0x[0-9a-f]+) align=(0x[0-9a-f]+) flagi=([RWE ]{3})', brb.stdout, re.M):
        segmenty_brb.append(tuple(int(m.group(i), 16) for i in (1, 2, 3, 4, 5)) + (m.group(7).replace(' ', ''), int(m.group(6), 16)))
    if not segmenty_re:
        bledy.append('readelf: brak PT_LOAD')
    if segmenty_re != segmenty_brb:
        bledy.append(f'PT_LOAD: readelf {segmenty_re} != BRB {segmenty_brb}')
    # objdump -p: niezalezny odczyt naglowkow programu (LOAD off/vaddr/paddr/filesz/memsz/flags).
    od_load = re.findall(r'^\s*LOAD off\s+(0x[0-9a-f]+) vaddr (0x[0-9a-f]+) paddr (0x[0-9a-f]+) align 2\*\*(\d+)\s*\n'
                         r'\s*filesz (0x[0-9a-f]+) memsz (0x[0-9a-f]+) flags ([rwx-]{3})', od_out, re.M)
    od_seg = [(int(o, 16), int(v, 16), int(p, 16), int(f, 16), int(m, 16), fl.replace('-', '').upper().replace('X', 'E'), 1 << int(al))
              for o, v, p, al, f, m, fl in od_load]
    if od_seg != segmenty_brb:
        bledy.append(f'PT_LOAD: objdump {od_seg} != BRB {segmenty_brb}')
    # Zakres stron i sumy liczone niezaleznie z danych readelf.
    pocz = min(s[2] for s in segmenty_re) & ~0xFFF
    kon = max((s[2] + s[4] + 0xFFF) & ~0xFFF for s in segmenty_re)
    zakres = re.search(r'^PROD zakres=(0x[0-9a-f]+)-(0x[0-9a-f]+) pamiec_stron=(0x[0-9a-f]+) z_pliku=(0x[0-9a-f]+) zera=(0x[0-9a-f]+)',
                       brb.stdout, re.M)
    if (int(zakres.group(1), 16), int(zakres.group(2), 16)) != (pocz, kon):
        bledy.append(f'zakres stron: readelf {pocz:#x}-{kon:#x} != BRB {zakres.group(1)}-{zakres.group(2)}')
    if int(zakres.group(4), 16) != sum(s[3] for s in segmenty_re) or int(zakres.group(5), 16) != sum(s[4] - s[3] for s in segmenty_re):
        bledy.append('sumy z_pliku/zera niezgodne')
    # Multiboot2: niezalezne wyszukanie magic z poprawna suma w pierwszych 32 KiB.
    dane = open(a.jadro, 'rb').read(32768)
    mb2 = None
    for off in range(0, len(dane) - 15, 8):
        magic, arch, dl, suma = struct.unpack_from('<IIII', dane, off)
        if magic == 0xE85250D6 and (magic + arch + dl + suma) & 0xFFFFFFFF == 0:
            mb2 = (off, dl, arch)
            break
    m = re.search(r'^PROD mb2 obecny=1 offset=(0x[0-9a-f]+) dlugosc=(\d+) architektura=(\d+)', brb.stdout, re.M)
    if not mb2 or not m or (int(m.group(1), 16), int(m.group(2)), int(m.group(3))) != mb2:
        bledy.append(f'Multiboot2: niezaleznie {mb2} != BRB {m.groups() if m else None}')
    print(f'[T-ELF-PROD] readelf: EXEC X86-64, wejscie={wejscie_re:#x}, naglowkow programu={phnum}, PT_LOAD={len(segmenty_re)}')
    print(f'[T-ELF-PROD] objdump: PT_LOAD={len(od_seg)}; zakres stron {pocz:#x}-{kon:#x}; Multiboot2 offset={mb2[0]:#x} dlugosc={mb2[1]}' if mb2 else '')
    for b in bledy:
        print('[T-ELF-PROD] BLAD: ' + b)
    print('[T-ELF-PROD] WYNIK: ' + ('zgodnosc pelna' if not bledy else 'NIEZGODNOSC'))
    return 0 if not bledy else 1


if __name__ == '__main__':
    sys.exit(main())
