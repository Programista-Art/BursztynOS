#!/usr/bin/env python3
"""BRB - pojedyncza sesja testowa QEMU/OVMF dla aplikacji UEFI Fazy 2 (uruchamiac tylko za zgoda wlasciciela).

Bezpieczenstwo danych:
  - jedynym dyskiem jest wirtualny FAT QEMU (vvfat) z katalogu .brb-build/wolumin-testowy, ktory zawiera
    wylacznie EFI/BOOT/BOOTX64.EFI (= brb-x64.efi); brak obrazow .img/.iso Bursztyn OS, brak sieci,
  - firmware: OVMF_CODE tylko do odczytu; zmienne NVRAM w KOPII OVMF_VARS w .brb-build/qemu/ (pliki
    systemowe /usr/share/OVMF nie sa modyfikowane),
  - wszystkie czasy oczekiwania sa ograniczone; QEMU jest zawsze zamykany (quit przez QMP, potem kill).

Scenariusze (dowolna nazwa; katalog wynikow .brb-build/qemu/<scenariusz>):
  domyslnie: czeka na "[BRB] klawiatura: oczekiwanie", zrzut ekranu, wysyla Enter, czeka na koniec,
  --bez-klawisza (albo --scenariusz limit): nie wysyla klawisza; oczekuje powrotu po limicie 60 s.
  --oczekiwany-status 0x...: status w linii "[BRB] zakonczenie" musi byc dokladnie taki,
  --oczekiwany-tekst T (wielokrotnie): kazdy tekst musi wystapic w logu COM1.
Wolumin moze zawierac wylacznie EFI/BOOT/BOOTX64.EFI oraz (Faza 3) boot/system_operacyjny.bin.
Wynik: .brb-build/qemu/<scenariusz>/com1.log, ekran-*.png, qemu.log, polecenie.txt
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

KOD = Path('/usr/share/OVMF/OVMF_CODE_4M.fd')
ZMIENNE = Path('/usr/share/OVMF/OVMF_VARS_4M.fd')


class Qmp:
    def __init__(self, sciezka, limit=20.0):
        koniec = time.monotonic() + limit
        while True:
            try:
                self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.s.connect(str(sciezka))
                break
            except OSError:
                if time.monotonic() > koniec:
                    raise
                time.sleep(0.2)
        self.s.settimeout(10)
        self.bufor = b''
        self._czytaj()  # powitanie
        self.polecenie('qmp_capabilities')

    def _czytaj(self):
        while b'\n' not in self.bufor:
            dane = self.s.recv(65536)
            if not dane:
                raise RuntimeError('QMP zamkniete')
            self.bufor += dane
        linia, self.bufor = self.bufor.split(b'\n', 1)
        return json.loads(linia)

    def polecenie(self, nazwa, **argumenty):
        self.s.sendall(json.dumps({'execute': nazwa, 'arguments': argumenty}).encode() + b'\n')
        while True:
            odp = self._czytaj()
            if 'return' in odp or 'error' in odp:
                return odp


def czekaj_na(log, tekst, limit):
    koniec = time.monotonic() + limit
    while time.monotonic() < koniec:
        if log.exists() and tekst in log.read_bytes().decode('latin-1'):
            return True
        time.sleep(0.5)
    return False


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--scenariusz', required=True)
    p.add_argument('--wolumin', default='.brb-build/wolumin-testowy')
    p.add_argument('--qemu', default='qemu-system-x86_64')
    p.add_argument('--bez-klawisza', action='store_true')
    p.add_argument('--maszyna', default='q35', help='q35 (domyslnie) albo pc,hpet=on (jak cele run* produkcji)')
    p.add_argument('--pamiec', default='256', help='rozmiar RAM QEMU, np. 256 albo 2G')
    p.add_argument('--wylacz-s3', action='store_true', help='diagnostyka: -global ICH9-LPC.disable_s3=1 (q35)')
    p.add_argument('--oczekiwany-status')
    p.add_argument('--oczekiwany-tekst', action='append', default=[])
    a = p.parse_args()
    if not all(c.isalnum() or c in '_-' for c in a.scenariusz):
        print('[OVMF] BLAD: niedozwolona nazwa scenariusza')
        return 2
    bez_klawisza = a.bez_klawisza or a.scenariusz == 'limit'
    wolumin = Path(a.wolumin).resolve()
    if '.brb-build' not in wolumin.parts and 'scratchpad' not in wolumin.parts:
        print('[OVMF] BLAD: wolumin testowy musi lezec w .brb-build/ (albo w scratchpadzie sesji)')
        return 2
    pliki = sorted(str(x.relative_to(wolumin)) for x in wolumin.rglob('*') if x.is_file())
    dozwolone = {'EFI/BOOT/BOOTX64.EFI', 'boot/system_operacyjny.bin'}
    if 'EFI/BOOT/BOOTX64.EFI' not in pliki or not set(pliki) <= dozwolone:
        print(f'[OVMF] BLAD: wolumin moze zawierac wylacznie {sorted(dozwolone)}, jest: {pliki}')
        return 2
    wyniki = Path('.brb-build/qemu') / a.scenariusz
    wyniki.mkdir(parents=True, exist_ok=True)
    for stary in ('com1.log', 'qemu.log', 'ekran-oczekiwanie.png', 'ekran-koniec.png'):
        if (wyniki / stary).exists():
            (wyniki / stary).unlink()   # tylko wlasne wyniki poprzedniej sesji w .brb-build/qemu
    zmienne = wyniki / 'OVMF_VARS_4M.kopia.fd'
    shutil.copyfile(ZMIENNE, zmienne)
    qmp_gniazdo = wyniki / 'qmp.sock'
    if qmp_gniazdo.exists():
        qmp_gniazdo.unlink()
    com1 = wyniki / 'com1.log'
    polecenie = [
        a.qemu, '-machine', a.maszyna, '-m', a.pamiec, '-smp', '1', '-nodefaults', '-no-user-config',
        '-drive', f'if=pflash,format=raw,unit=0,readonly=on,file={KOD}',
        '-drive', f'if=pflash,format=raw,unit=1,file={zmienne}',
        '-drive', f'if=none,id=wolumin,format=raw,readonly=on,file=fat:{wolumin}',
        '-device', 'virtio-blk-pci,drive=wolumin',
        '-vga', 'std', '-display', 'none',
        '-serial', f'file:{com1}',
        '-net', 'none',
        '-qmp', f'unix:{qmp_gniazdo},server=on,wait=off',
    ]
    if a.wylacz_s3:
        polecenie += ['-global', 'ICH9-LPC.disable_s3=1']
    (wyniki / 'polecenie.txt').write_text(' '.join(polecenie) + '\n')
    print('[OVMF] ' + ' '.join(polecenie))
    with open(wyniki / 'qemu.log', 'w') as qlog:
        proc = subprocess.Popen(polecenie, stdout=qlog, stderr=subprocess.STDOUT)
    wynik = 1
    try:
        qmp = Qmp(qmp_gniazdo)
        if not czekaj_na(com1, '[BRB] klawiatura: oczekiwanie', 120):
            print('[OVMF] BLAD: brak kamienia milowego oczekiwania na klawisz w 120 s')
            return 1
        time.sleep(2)
        print('[OVMF] zrzut ekranu:', qmp.polecenie('screendump', filename=str((wyniki / 'ekran-oczekiwanie.png').resolve()), format='png'))
        if not bez_klawisza:
            print('[OVMF] klawisz Enter:', qmp.polecenie('send-key', keys=[{'type': 'qcode', 'data': 'ret'}]))
            limit = 30
        else:
            limit = 90
        if czekaj_na(com1, '[BRB] zakonczenie: powrot do firmware', limit):
            time.sleep(3)  # firmware po powrocie (menedzer rozruchu)
            print('[OVMF] zrzut ekranu:', qmp.polecenie('screendump', filename=str((wyniki / 'ekran-koniec.png').resolve()), format='png'))
            log = com1.read_bytes().decode('latin-1')
            wynik = 0
            if a.oczekiwany_status:
                oczekiwana = f'[BRB] zakonczenie: powrot do firmware status=0x{int(a.oczekiwany_status, 16):016X}'
                if oczekiwana not in log:
                    print(f'[OVMF] BLAD: brak linii "{oczekiwana}"')
                    wynik = 1
            for t in a.oczekiwany_tekst:
                if t not in log:
                    print(f'[OVMF] BLAD: brak tekstu w logu COM1: {t}')
                    wynik = 1
            if '!!!! X64 Exception' in log:
                print('[OVMF] BLAD: wyjatek procesora zgloszony przez firmware')
                wynik = 1
        else:
            print(f'[OVMF] BLAD: brak powrotu do firmware w {limit} s')
        qmp.polecenie('quit')
    finally:
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=15)
    print(f'[OVMF] QEMU zakonczony kodem {proc.returncode}; log COM1: {com1}')
    return wynik


if __name__ == '__main__':
    sys.exit(main())
