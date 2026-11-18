#!/usr/bin/env python3
"""Statyczny skan binarki testu hostowego: kod sterownikow skompilowany do procesu Linux (Ring 3) nie moze zawierac
instrukcji uprzywilejowanych (CLI/STI/HLT, port I/O, MSR, CRx, deskryptory, INVLPG...). W Ring 3 kazda z nich konczy
sie SIGSEGV, a wykonana sciezka testu bylaby nieprawdziwa (np. gpu_wait_for_irq, 2026-11-16).

Uzycie: skan_instrukcji_uprzywilejowanych.py BINARKA [--dozwolone FUNKCJA ...]
Domyslnie dozwolone: _start (glibc crt1: nieosiagalny HLT po __libc_start_main). Kod wyjscia 1 przy znalezisku.
"""
import re
import subprocess
import sys

MNEMONIKI = re.compile(
    r'^\s*[0-9a-f]+:\s+(?:rep[a-z]*\s+)?'
    r'(cli|sti|hlt|in|out|insb|insw|insl|outsb|outsw|outsl|invlpg|invpcid|wrmsr|rdmsr|lidt|lgdt|lldt|ltr|'
    r'wbinvd|invd|swapgs|sysretq?|sysexitq?|clts|lmsw|wrpkru|xsetbv|mov\s+%\w+,%(?:cr|dr)\d+|mov\s+%(?:cr|dr)\d+,)\b')
FUNKCJA = re.compile(r'^[0-9a-f]+ <([^>]+)>:')


def skanuj(sciezka, dozwolone):
    dis = subprocess.run(['objdump', '-d', '--no-show-raw-insn', sciezka], stdout=subprocess.PIPE,
                         text=True, check=True).stdout
    funkcja, znaleziska = '?', []
    for linia in dis.splitlines():
        m = FUNKCJA.match(linia)
        if m:
            funkcja = m[1]
            continue
        if MNEMONIKI.match(linia) and funkcja not in dozwolone:
            znaleziska.append(f'{funkcja}: {linia.strip()}')
    return znaleziska


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    dozwolone = {'_start'}
    if '--dozwolone' in argv:
        i = argv.index('--dozwolone')
        dozwolone |= set(argv[i + 1:])
        argv = argv[:i]
    zle = skanuj(argv[0], dozwolone)
    for z in zle:
        print('[UPRZYWILEJOWANA]', z)
    print(f'Skan instrukcji uprzywilejowanych {argv[0]}: {"FAIL" if zle else "PASS"} ({len(zle)} znalezisk)')
    return 1 if zle else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
