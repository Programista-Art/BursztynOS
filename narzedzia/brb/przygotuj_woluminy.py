#!/usr/bin/env python3
"""BRB - przygotowanie odizolowanych woluminow testowych OVMF dla Fazy 3.

Kazdy wolumin (.brb-build/qemu-woluminy/<scenariusz>/) zawiera WYLACZNIE:
  EFI/BOOT/BOOTX64.EFI          - kopia .brb-build/uefi/brb-x64.efi
  boot/system_operacyjny.bin    - kopia jadra testowego (poprawna albo celowo uszkodzona) - z wyjatkiem 'brak'
Zrodlowe jadro (.brb-build/jadro/system_operacyjny.bin) i obraz BRB nie sa modyfikowane; wszystkie zmiany
dotycza kopii w katalogu scenariusza. Zadnych dyskow ani obrazow ISO Bursztyn OS.

Uzycie: przygotuj_woluminy.py <brb-x64.efi> <jadro> <katalog_wyjsciowy> [jadro_syntetyczne]
  jadro_syntetyczne (Faza 4A): male jadro 0x100000-0x104000 z fixture testow -> wolumin 'syntetyczne'.
"""
import shutil
import struct
import sys
from pathlib import Path


def mutuj(dane: bytearray, scenariusz: str):
    phoff = struct.unpack_from('<Q', dane, 32)[0]
    if scenariusz == 'zla_sygnatura':
        dane[0] = 0x00
    elif scenariusz == 'zla_architektura':
        struct.pack_into('<H', dane, 18, 3)           # EM_386
    elif scenariusz == 'zle_naglowki_programu':
        struct.pack_into('<Q', dane, 32, len(dane))   # e_phoff poza plikiem
    elif scenariusz == 'zly_uklad_pamieci':
        # PT_LOAD nr 2 przeniesiony na adres segmentu nr 1 (nakladanie zakresow fizycznych i wirtualnych)
        v1 = struct.unpack_from('<Q', dane, phoff + 56 * 1 + 16)[0]
        struct.pack_into('<QQ', dane, phoff + 56 * 2 + 16, v1, v1)
    return dane


SCENARIUSZE = {
    # nazwa: (opis, funkcja dlugosci/obecnosci)
    'poprawne': 'kopia jadra produkcyjnego',
    'brak': 'brak pliku jadra (brak katalogu boot)',
    'puste': 'plik jadra o rozmiarze 0 B',
    'obciete': 'jadro obciete do 1 MiB (segmenty poza plikiem)',
    'zla_sygnatura': 'pierwszy bajt ELF zmieniony na 0x00',
    'zla_architektura': 'e_machine = EM_386',
    'zle_naglowki_programu': 'e_phoff wskazuje poza plik',
    'zly_uklad_pamieci': 'PT_LOAD nr 2 nachodzi na PT_LOAD nr 1',
}


def main():
    if len(sys.argv) not in (4, 5):
        print(__doc__)
        return 2
    efi, jadro, wyjscie = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    if wyjscie.resolve().parts[-2:] != ('.brb-build', 'qemu-woluminy') and '.brb-build' not in wyjscie.resolve().parts:
        print('[WOLUMINY] BLAD: katalog wyjsciowy musi lezec w .brb-build/')
        return 2
    wzor = jadro.read_bytes()
    scenariusze = dict(SCENARIUSZE)
    if len(sys.argv) == 5:
        scenariusze['syntetyczne'] = 'male syntetyczne jadro (fixture) 0x100000-0x104000 - natywna proba Fazy 4A'
    for nazwa, opis in scenariusze.items():
        kat = wyjscie / nazwa
        (kat / 'EFI' / 'BOOT').mkdir(parents=True, exist_ok=True)
        shutil.copyfile(efi, kat / 'EFI' / 'BOOT' / 'BOOTX64.EFI')
        cel = kat / 'boot' / 'system_operacyjny.bin'
        if nazwa == 'brak':
            if cel.exists():
                cel.unlink()          # tylko kopia w katalogu scenariusza w .brb-build
            if (kat / 'boot').exists():
                (kat / 'boot').rmdir()
        else:
            cel.parent.mkdir(exist_ok=True)
            if nazwa == 'puste':
                dane = b''
            elif nazwa == 'syntetyczne':
                dane = Path(sys.argv[4]).read_bytes()
            elif nazwa == 'obciete':
                dane = wzor[:1024 * 1024]
            else:
                dane = bytes(mutuj(bytearray(wzor), nazwa))
            cel.write_bytes(dane)
        print(f'[WOLUMINY] {nazwa:22s} {opis}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
