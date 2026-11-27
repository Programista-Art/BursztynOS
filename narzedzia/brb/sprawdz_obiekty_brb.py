#!/usr/bin/env python3
"""BRB - kontrola obiektow ELF przed linkowaniem obrazu PE32+ (cel make brb-uefi).

Sprawdza zaleznosci, ktorych nie widac po samym udanym linkowaniu:
  1. brak symboli nierozwiazanych w zbiorze obiektow (zadnych odwolan do libc, libgcc, __stack_chk_*,
     __cxa_*, _Unwind_*, tablicy GOT itp.),
  2. dozwolone wylacznie relokacje R_X86_64_PC32, R_X86_64_PLT32 (wzgledne, rozwiazywane przez ld)
     i R_X86_64_64 (wskazniki bezwzgledne -> wpisy DIR64 w .reloc); inne typy (np. R_X86_64_32/32S,
     GOTPCREL) oznaczalyby kod nierelokowalny albo zalezny od GOT,
  3. brak sekcji konstruktorow/destruktorow globalnych, TLS i unwind (brak srodowiska uruchomieniowego),
  4. funkcje pamieci nie wywoluja samych siebie ani innych funkcji mem* (ochrona przed zamiana petli
     na wywolanie memcpy przez optymalizator); wrappery memcpy/... moga przekazac sterowanie tylko do
     odpowiadajacej funkcji brb_*,
  5. zapis liczby relokacji R_X86_64_64 (walidator PE porownuje ja z liczba wpisow DIR64).
"""
import argparse
import re
import subprocess
import sys

DOZWOLONE_RELOKACJE = {'R_X86_64_PC32', 'R_X86_64_PLT32', 'R_X86_64_64'}
ZAKAZANE_SEKCJE = re.compile(r'^\.(init_array|fini_array|ctors|dtors|preinit_array|tdata|tbss|eh_frame|gcc_except_table)')
FUNKCJE_PAMIECI = {'memcpy': 'brb_memcpy', 'memmove': 'brb_memmove', 'memset': 'brb_memset', 'memcmp': 'brb_memcmp'}


def uruchom(*argumenty):
    return subprocess.run(argumenty, check=True, capture_output=True, text=True).stdout


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--readelf', required=True)
    p.add_argument('--objdump', required=True)
    p.add_argument('--wynik', required=True)
    p.add_argument('obiekty', nargs='+')
    a = p.parse_args()
    bledy = []
    zdefiniowane, nierozwiazane = set(), {}
    relokacje_64 = 0
    typy = {}
    for obj in a.obiekty:
        for linia in uruchom(a.readelf, '-sW', obj).splitlines():
            pola = linia.split()
            if len(pola) < 8 or not pola[0].endswith(':'):
                continue
            wiazanie, ndx, nazwa = pola[4], pola[6], pola[7]
            if wiazanie not in ('GLOBAL', 'WEAK'):
                continue
            if ndx == 'UND':
                nierozwiazane.setdefault(nazwa, obj)
            else:
                zdefiniowane.add(nazwa)
        for linia in uruchom(a.readelf, '-SW', obj).splitlines():
            m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+\S+\s+\S+\s+([0-9a-f]+)', linia)
            if m and ZAKAZANE_SEKCJE.match(m.group(1)) and int(m.group(2), 16) > 0:
                bledy.append(f'{obj}: niedozwolona sekcja {m.group(1)} (brak srodowiska uruchomieniowego)')
        for linia in uruchom(a.readelf, '-rW', obj).splitlines():
            m = re.match(r'^[0-9a-f]+\s+[0-9a-f]+\s+(R_X86_64_\w+)', linia)
            if not m:
                continue
            typy[m.group(1)] = typy.get(m.group(1), 0) + 1
            if m.group(1) not in DOZWOLONE_RELOKACJE:
                bledy.append(f'{obj}: niedozwolony typ relokacji {m.group(1)}: {linia.strip()}')
            if m.group(1) == 'R_X86_64_64':
                relokacje_64 += 1
    for nazwa, obj in sorted(nierozwiazane.items()):
        if nazwa not in zdefiniowane:
            bledy.append(f'{obj}: nierozwiazany symbol {nazwa}')

    # Funkcje pamieci: analiza deasemblacji z relokacjami.
    for obj in a.obiekty:
        if not obj.endswith('pamiec_podstawowa.o'):
            continue
        funkcja = None
        for linia in uruchom(a.objdump, '-dr', '--no-show-raw-insn', obj).splitlines():
            m = re.match(r'^[0-9a-f]+ <([^>]+)>:', linia)
            if m:
                funkcja = m.group(1)
                continue
            m = re.search(r'R_X86_64_(?:PLT32|PC32)\s+(\S+?)(?:[-+]0x[0-9a-f]+)?$', linia)
            if m and funkcja:
                cel = m.group(1)
                if funkcja.startswith('brb_'):
                    bledy.append(f'{funkcja} odwoluje sie do {cel} (oczekiwana petla bez wywolan)')
                elif funkcja in FUNKCJE_PAMIECI and cel != FUNKCJE_PAMIECI[funkcja]:
                    bledy.append(f'{funkcja} odwoluje sie do {cel} (dozwolone tylko {FUNKCJE_PAMIECI[funkcja]})')
            # Rekurencja = call/jmp na POCZATEK funkcji (<nazwa>); skoki wewnetrzne maja postac <nazwa+0x..>.
            if funkcja and re.search(r'\s(call|jmp)\s+[0-9a-f]+ <' + re.escape(funkcja) + r'>', linia):
                bledy.append(f'{funkcja} wywoluje sama siebie')
        brakujace = set(FUNKCJE_PAMIECI) - zdefiniowane
        if brakujace:
            bledy.append('brak definicji: ' + ', '.join(sorted(brakujace)))

    print('[BRB-OBJ] typy relokacji ELF: ' + ', '.join(f'{k}={v}' for k, v in sorted(typy.items())))
    print(f'[BRB-OBJ] symbole globalne zdefiniowane: {len(zdefiniowane)}, nierozwiazane: '
          f'{len([n for n in nierozwiazane if n not in zdefiniowane])}')
    for b in bledy:
        print('[BRB-OBJ] BLAD: ' + b)
    if bledy:
        return 1
    with open(a.wynik, 'w', encoding='ascii') as f:
        f.write(f'{relokacje_64}\n')
    print(f'[BRB-OBJ] OK; relokacje R_X86_64_64: {relokacje_64}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
