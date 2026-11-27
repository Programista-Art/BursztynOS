#!/usr/bin/env python3
"""BRB Faza R1A - kamienie milowe rozruchu jadra na COM1 (tylko odczyt logu z .brb-build/).

Uzycie: sprawdz_kamienie_rozruchu.py <com1.log> [--oczekuj-bledu-pamieci]
Bez flagi: wszystkie kamienie musza wystapic, a krytyczny blad wlasnosci pamieci NIE moze wystapic.
Z flaga (diagnostyczny przebieg znanej niebezpiecznej sciezki): musi wystapic blad pamieci, a PMM/VMM nie moga
zostac zgloszone jako gotowe. Wynik: tabela PASS/FAIL na stdout, kod wyjscia 0/1.
"""
import re
import sys

KAMIENIE = [
    ('PMM', '[PMM] Skan pamieci fizycznej'),
    ('VMM', '[VMM] Paging 4-poziomowy aktywowany.'),
    ('sterta jadra', '[HEAP] Sterta jadra 16 MiB gotowa.'),
    ('przerwania (APIC)', '[APIC] LAPIC/IOAPIC uruchomiony.'),
    ('wywolania systemowe', '[BWS] API Wywolan Systemowych gotowe.'),
    ('planista', '[SCHED] Wielozadaniowosc Round-Robin aktywna.'),
    ('ladowanie BUR / menedzer okien', '[BSP] Menedzer Okien zainstalowany.'),
    ('Ring 3', 'launched in Ring 3'),
    ('modul zwolniony do PMM (OdblokujZakres)', 'zwolniona do puli PMM'),
    ('pulpit', 'Pulpit gotowy.'),
]
BLAD = '[PAMIEC] KRYTYCZNY BLAD WLASNOSCI PAMIECI ROZRUCHU'


def main():
    tekst = open(sys.argv[1], 'rb').read().decode('latin-1').replace('\r', '')
    tekst = re.sub(r'\x1b\[[0-9;=?]*[a-zA-Z]', '', tekst)
    oczekuj_bledu = '--oczekuj-bledu-pamieci' in sys.argv[2:]
    ok = True
    blad = BLAD in tekst
    for nazwa, znacznik in KAMIENIE:
        jest = znacznik in tekst
        if oczekuj_bledu:
            stan = 'NIEOBECNY (oczekiwane)' if not jest else 'OBECNY (BLAD)'
            ok &= not jest
        else:
            stan = 'PASS' if jest else 'FAIL'
            ok &= jest
        print(f'  {nazwa:42s} {stan}')
    if oczekuj_bledu:
        print(f'  {"krytyczny blad wlasnosci pamieci":42s} {"PASS (zgloszony)" if blad else "FAIL (brak)"}')
        ok &= blad
        for linia in tekst.splitlines():
            if linia.startswith('[PAMIEC]') or 'KERNEL-PANIC' in linia:
                print('   > ' + linia.strip())
    else:
        print(f'  {"brak bledu wlasnosci pamieci":42s} {"PASS" if not blad else "FAIL"}')
        ok &= not blad
    print('WYNIK:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
