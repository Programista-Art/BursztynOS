#!/usr/bin/env python3
"""BRB Faza 4A-R - analiza zrzutow sesji diag_grub_ovmf.py (tryb grub). Tylko odczyt plikow w .brb-build/.

Wejscie: katalog wynikow scenariusza + jadro ELF (ta sama kopia, ktora byla w fixture).
Wyjscie (stdout, ASCII): mapy lsefimmap przed/po multiboot2 i roznice (przydzialy GRUB), tagi MBI
(mapa MB2 typ 6, mapa EFI typ 17, adres bazowy typ 21, tagi EFI), zgodnosc pamieci w chwili wejscia do jadra
z obrazem oczekiwanym (dane z pliku + zera BSS), zmiany w obszarze 0x800000-0x900000.
"""
import re
import struct
import sys
from pathlib import Path

TYPY_EFI = ['reserved', 'ldr-code', 'ldr-data', 'BS-code', 'BS-data', 'RT-code', 'RT-data', 'conv-mem',
            'unusable', 'ACPI-rec', 'ACPI-nvs', 'MMIO', 'IO-ports', 'pal-code', 'persist', 'unaccept']
TYPY_MB2 = {1: 'dostepna', 2: 'zarezerwowana', 3: 'ACPI-reclaim', 4: 'ACPI-NVS', 5: 'uszkodzona'}
JADRO_P, JADRO_K = 0x100000, 0xD6F000


def mapy_lsefimmap(tekst):
    """Zwraca liste blokow [(typ, p, k_wyl)] po kolejnych naglowkach 'Type Physical start'."""
    bloki, biezacy = [], None
    for linia in tekst.splitlines():
        if 'Physical start' in linia:
            if biezacy:
                bloki.append(biezacy)
            biezacy = []
            continue
        m = re.match(r'^(\S+)\s+\S+\s+([0-9a-f]{16})-([0-9a-f]{16})\s', linia)
        if m and biezacy is not None:
            biezacy.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16) + 1))
        elif biezacy and linia.startswith('GRUB-DIAG'):
            bloki.append(biezacy)
            biezacy = None
    if biezacy:
        bloki.append(biezacy)
    # GRUB pisze na konsole i port szeregowy (obie na COM1) - linie sa zdublowane w tym samym bloku.
    wynik = []
    for b in bloki:
        u = []
        for w in b:
            if w not in u:
                u.append(w)
        if u and (not wynik or u != wynik[-1]):
            wynik.append(u)
    return wynik


def nakladajace(mapa, p, k):
    return [w for w in mapa if w[1] < k and p < w[2]]


def wypisz_zakres(nazwa, mapa, zakresy):
    print(f'== {nazwa}: deskryptory pokrywajace segmenty jadra')
    for (zp, zk, opis) in zakresy:
        print(f'  segment {opis} 0x{zp:X}-0x{zk:X}:')
        for t, p, k in nakladajace(mapa, zp, zk):
            print(f'    {t:9s} 0x{p:09X}-0x{k:09X} ({(k - p) // 4096} stron)')


def segmenty(elf):
    d = elf
    phoff, = struct.unpack_from('<Q', d, 32)
    phnum, = struct.unpack_from('<H', d, 56)
    s = []
    for i in range(phnum):
        t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', d, phoff + 56 * i)
        if t == 1:
            s.append((i, fl, off, pa, fs, ms))
    return s


def obraz_oczekiwany(elf, segs):
    o = bytearray(JADRO_K - JADRO_P)
    maska = bytearray(JADRO_K - JADRO_P)   # 1 = bajt nalezy do segmentu
    for (_, _, off, pa, fs, ms) in segs:
        o[pa - JADRO_P:pa - JADRO_P + fs] = elf[off:off + fs]
        for x in range(pa - JADRO_P, pa - JADRO_P + ms):
            maska[x] = 1
    return o, maska


def tagi_mbi(mbi):
    rozmiar, = struct.unpack_from('<I', mbi, 0)
    o = 8
    while o + 8 <= rozmiar:
        typ, dl = struct.unpack_from('<II', mbi, o)
        yield typ, mbi[o:o + dl]
        if typ == 0:
            break
        o += (dl + 7) & ~7


def main():
    kat, elf_p = Path(sys.argv[1]), Path(sys.argv[2])
    elf = elf_p.read_bytes()
    segs = segmenty(elf)
    zakresy = [(pa, pa + ms, f'ph{i}') for (i, _, _, pa, _, ms) in segs]
    tekst = (kat / 'com1-czysty.txt').read_text(errors='replace') if (kat / 'com1-czysty.txt').exists() else \
        re.sub(r'\x1b\[[0-9;=?]*[a-zA-Z]', '', (kat / 'com1.log').read_bytes().decode('latin-1').replace('\r', ''))
    m = re.search(r'platforma=(\S+?) cpu=(\S+?)(GRUB|\s|$)', tekst)
    print('GRUB platforma:', m.group(1) if m else 'UNKNOWN', 'cpu:', m.group(2) if m else 'UNKNOWN')
    bloki = mapy_lsefimmap(tekst)
    print('bloki lsefimmap (po deduplikacji):', len(bloki), [len(b) for b in bloki])
    if len(bloki) >= 1:
        wypisz_zakres('lsefimmap PRZED multiboot2', bloki[0], zakresy)
    if len(bloki) >= 2:
        wypisz_zakres('lsefimmap PO multiboot2', bloki[-1], zakresy)
        przed, po = set(bloki[0]), set(bloki[-1])
        print('== roznice map (PO minus PRZED) - nowe deskryptory:')
        for t, p, k in sorted(po - przed, key=lambda w: w[1]):
            print(f'  + {t:9s} 0x{p:09X}-0x{k:09X} ({(k - p) // 4096} stron, {(k - p) // 1024} KiB)')
        print('== zniknely:')
        for t, p, k in sorted(przed - po, key=lambda w: w[1]):
            print(f'  - {t:9s} 0x{p:09X}-0x{k:09X} ({(k - p) // 4096} stron)')
    mbi_p = kat / 'mbi.bin'
    if mbi_p.exists():
        mbi = mbi_p.read_bytes()
        print(f'== MBI: {len(mbi)} B')
        for typ, t in tagi_mbi(mbi):
            if typ == 6:
                rozm_w, wer = struct.unpack_from('<II', t, 8)
                print(f'  tag 6 (mapa MB2) wpis={rozm_w} B wersja={wer}; wpisy pokrywajace zakres jadra:')
                for o in range(16, len(t), rozm_w):
                    a, dl, ty = struct.unpack_from('<QQI', t, o)
                    if a < JADRO_K and JADRO_P < a + dl:
                        print(f'    0x{a:09X}-0x{a + dl:09X} typ={ty} ({TYPY_MB2.get(ty, "?")})')
            elif typ == 17:
                krok, wer = struct.unpack_from('<II', t, 8)
                n = (len(t) - 16) // krok if krok else 0
                print(f'  tag 17 (mapa EFI) krok={krok} wersja={wer} wpisy={n}; pokrywajace zakres jadra:')
                for i in range(n):
                    ty, _, pa, va, np_, at = struct.unpack_from('<IIQQQQ', t, 16 + i * krok)
                    if pa < JADRO_K and JADRO_P < pa + np_ * 4096:
                        nz = TYPY_EFI[ty] if ty < len(TYPY_EFI) else f'typ{ty}'
                        print(f'    0x{pa:09X}-0x{pa + np_ * 4096:09X} {nz}')
            elif typ == 21:
                baza, = struct.unpack_from('<I', t, 8)
                print(f'  tag 21 (adres bazowy obrazu) = 0x{baza:X}')
            elif typ == 1:
                print('  tag 1 (wiersz polecen):', t[8:].split(b'\0')[0].decode(errors='replace'))
            elif typ == 2:
                print('  tag 2 (bootloader):', t[8:].split(b'\0')[0].decode(errors='replace'))
            else:
                print(f'  tag {typ} dl={len(t)}')
    wej_p = kat / 'jadro-wejscie.bin'
    if wej_p.exists():
        wej = wej_p.read_bytes()
        ocz, maska = obraz_oczekiwany(elf, segs)
        print('== pamiec 0x100000-0xD6F000 w chwili wejscia do jadra vs obraz oczekiwany:')
        for (i, fl, off, pa, fs, ms) in segs:
            a, b = pa - JADRO_P, pa - JADRO_P + fs
            rd = sum(1 for x in range(a, b) if wej[x] != ocz[x])
            bss = sum(1 for x in range(b, a + (ms)) if wej[x] != 0)
            print(f'  ph{i} fiz=0x{pa:X} z_pliku={fs} rozne={rd}; BSS={ms - fs} niezerowe={bss}')
        przed_p = kat / 'jadro-przed.bin'
        if przed_p.exists():
            przed = przed_p.read_bytes()
            for nazwa, p, k in (('0x800000-0x808000 (ACPI NVS)', 0x800000, 0x808000),
                                ('0x80B000-0x80C000 (ACPI NVS)', 0x80B000, 0x80C000),
                                ('0x810000-0x900000 (ACPI NVS)', 0x810000, 0x900000),
                                ('0x900000-0xD6F000 (BS data)', 0x900000, 0xD6F000)):
                a, b = p - JADRO_P, k - JADRO_P
                zmien = sum(1 for x in range(a, b) if przed[x] != wej[x])
                zgodne = sum(1 for x in range(a, b) if wej[x] == ocz[x])
                przed_zero = sum(1 for x in range(a, b) if przed[x] == 0)
                print(f'  {nazwa}: bajtow={b - a} zmienionych(przed->wejscie)={zmien} '
                      f'rownych obrazowi jadra={zgodne} zer_przed={przed_zero}')
    nk = kat / 'nvs-koniec.bin'
    nw = kat / 'nvs-wejscie.bin'
    if nk.exists() and nw.exists():
        a, b = nw.read_bytes(), nk.read_bytes()
        print(f'== 0x800000-0x900000: bajtow zmienionych od wejscia do konca sesji = {sum(1 for x, y in zip(a, b) if x != y)}')


if __name__ == '__main__':
    main()
