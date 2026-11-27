#!/usr/bin/env python3
"""BRB Faza 4A-R - porownawcza sesja diagnostyczna QEMU/OVMF: GRUB (fixture) albo BRB, ten sam sprzet.

W obu trybach podlaczone sa IDENTYCZNE urzadzenia: CD-ROM z fixture GRUB (tylko odczyt) i virtio-blk z woluminem
BRB (vvfat, tylko odczyt). Rozni sie wylacznie bootindex. Firmware: OVMF_CODE (tylko odczyt) + KOPIA zmiennych
w katalogu wynikow. Brak dyskow produkcyjnych, brak zapisywalnych nosnikow.

Tryb grub: QEMU startuje zatrzymany (-S) z gdbstub na 127.0.0.1; gdb ustawia sprzetowy punkt zatrzymania na
wejsciu jadra (e_entry z ELF), zrzuca rejestry, strukture MBI (EBX) i pamiec zakresu jadra, po czym odlacza sie
(maszyna dziala dalej). Przed poleceniem multiboot2 (znacznik "GRUB-DIAG: PRZED-MULTIBOOT" na COM1, GRUB czeka
4 s) zrzucany jest przez QMP stan obszaru ACPI NVS i calego zakresu jadra. Po --czas-jadra s: zrzut koncowy.
Jadro nie jest modyfikowane; zrzuty to odczyt pamieci goscia (pmemsave / gdb dump).

Wyniki: .brb-build/qemu-diag/<scenariusz>/ (com1.log, polecenie.txt, *.bin, gdb.log, ekran-*.png)
"""
import argparse
import json
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
        self.s.settimeout(60)
        self.bufor = b''
        self._czytaj()
        self.polecenie('qmp_capabilities')

    def _czytaj(self):
        while b'\n' not in self.bufor:
            d = self.s.recv(65536)
            if not d:
                raise RuntimeError('QMP zamkniete')
            self.bufor += d
        linia, self.bufor = self.bufor.split(b'\n', 1)
        return json.loads(linia)

    def polecenie(self, nazwa, **arg):
        self.s.sendall(json.dumps({'execute': nazwa, 'arguments': arg}).encode() + b'\n')
        while True:
            o = self._czytaj()
            if 'return' in o or 'error' in o:
                return o


def log_zawiera(p, t):
    return p.exists() and t in p.read_bytes().decode('latin-1')


def czekaj(p, t, limit):
    k = time.monotonic() + limit
    while time.monotonic() < k:
        if log_zawiera(p, t):
            return True
        time.sleep(0.3)
    return False


def e_entry(elf):
    import struct
    return struct.unpack_from('<Q', elf.read_bytes(), 24)[0]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--scenariusz', required=True)
    p.add_argument('--tryb', choices=('grub', 'brb'), required=True)
    p.add_argument('--iso', default='.brb-build/grub-fixture/fixture-grub.iso')
    p.add_argument('--wolumin', default='.brb-build/qemu-woluminy/poprawne')
    p.add_argument('--jadro', default='.brb-build/jadro/system_operacyjny.bin')
    p.add_argument('--maszyna', default='q35')
    p.add_argument('--pamiec', default='256')
    p.add_argument('--wylacz-s3', action='store_true')
    p.add_argument('--profil', choices=('std', 'produkcja'), default='std',
                   help='std: -vga std; produkcja: urzadzenia jak runuefi (bez dysku, audio, SDL)')
    p.add_argument('--czas-jadra', type=int, default=45)
    p.add_argument('--port-gdb', type=int, default=12345)
    p.add_argument('--firmware', choices=('ovmf', 'bios'), default='ovmf',
                   help='ovmf: OVMF_CODE (tylko odczyt) + kopia zmiennych; bios: domyslny SeaBIOS QEMU (Faza R1A)')
    a = p.parse_args()
    if not all(c.isalnum() or c in '_-' for c in a.scenariusz):
        return 2
    for x in (a.iso, a.wolumin):
        if '.brb-build' not in Path(x).resolve().parts:
            print('[DIAG] BLAD: nosniki testowe musza lezec w .brb-build/')
            return 2
    wyn = Path('.brb-build/qemu-diag') / a.scenariusz
    wyn.mkdir(parents=True, exist_ok=True)
    for st in wyn.iterdir():
        if st.suffix in ('.log', '.bin', '.png', '.txt', '.sock'):
            st.unlink()   # wylacznie wlasne wyniki poprzedniej sesji tego scenariusza
    zm = wyn / 'OVMF_VARS_4M.kopia.fd'
    if a.firmware == 'ovmf':
        zm.write_bytes(ZMIENNE.read_bytes())
    qmp_s = wyn / 'qmp.sock'
    com1 = wyn / 'com1.log'
    cd_boot, blk_boot = (1, 2) if a.tryb == 'grub' else (2, 1)
    cmd = [
        'qemu-system-x86_64', '-machine', a.maszyna, '-m', a.pamiec, '-smp', '1', '-nodefaults', '-no-user-config',
    ] + ([
        '-drive', f'if=pflash,format=raw,unit=0,readonly=on,file={KOD}',
        '-drive', f'if=pflash,format=raw,unit=1,file={zm}',
    ] if a.firmware == 'ovmf' else []) + [
        '-drive', f'if=none,id=cd,media=cdrom,readonly=on,format=raw,file={Path(a.iso).resolve()}',
        '-device', f'ide-cd,drive=cd,bootindex={cd_boot}',
        '-drive', f'if=none,id=wolumin,format=raw,readonly=on,file=fat:{Path(a.wolumin).resolve()}',
        '-device', f'virtio-blk-pci,drive=wolumin,bootindex={blk_boot}',
        '-display', 'none', '-serial', f'file:{com1}',
        '-qmp', f'unix:{qmp_s},server=on,wait=off',
    ]
    if a.profil == 'std':
        cmd += ['-vga', 'std', '-net', 'none']
    else:
        cmd += ['-cpu', 'max', '-accel', 'tcg,thread=single', '-vga', 'none',
                '-device', 'virtio-vga,disable-legacy=on,xres=1280,yres=720',
                '-netdev', 'user,id=n1,restrict=on', '-device', 'virtio-net-pci,netdev=n1,disable-legacy=on',
                '-object', 'rng-builtin,id=rng0', '-device', 'virtio-rng-pci,rng=rng0,disable-legacy=on',
                '-device', 'virtio-keyboard-pci', '-device', 'virtio-mouse-pci', '-device', 'virtio-tablet-pci']
    if a.wylacz_s3:
        cmd += ['-global', ('ICH9-LPC' if 'q35' in a.maszyna else 'PIIX4_PM') + '.disable_s3=1']
    gdb = None
    if a.tryb == 'grub':
        cmd += ['-gdb', f'tcp:127.0.0.1:{a.port_gdb}', '-S']
    (wyn / 'polecenie.txt').write_text(' '.join(cmd) + '\n')
    print('[DIAG] ' + ' '.join(cmd))
    with open(wyn / 'qemu.log', 'w') as ql:
        proc = subprocess.Popen(cmd, stdout=ql, stderr=subprocess.STDOUT)
    wynik = 1
    try:
        qmp = Qmp(qmp_s)
        if a.tryb == 'grub':
            wej = e_entry(Path(a.jadro))
            skrypt = wyn / 'gdb-polecenia.txt'
            skrypt.write_text('\n'.join([
                'set pagination off', 'set confirm off', 'set architecture i386:x86-64',
                f'target remote 127.0.0.1:{a.port_gdb}',
                f'hbreak *0x{wej:x}', 'continue',
                'echo GDB-WEJSCIE\\n', 'info registers rip rax rbx rsp eflags cr0 cr3 cr4 efer cs',
                'set $mbi = (unsigned long)$rbx & 0xffffffff',
                'set $mbi_dl = *(unsigned int*)$mbi',
                'printf "MBI adres=0x%lx rozmiar=%u\\n", $mbi, $mbi_dl',
                f'dump binary memory {wyn.resolve()}/mbi.bin $mbi $mbi+$mbi_dl',
                f'dump binary memory {wyn.resolve()}/jadro-wejscie.bin 0x100000 0xd6f000',
                f'dump binary memory {wyn.resolve()}/nvs-wejscie.bin 0x800000 0x900000',
                'echo GDB-ZRZUT-OK\\n', 'delete', 'detach', 'quit']) + '\n')
            with open(wyn / 'gdb.log', 'w') as gl:
                gdb = subprocess.Popen(['gdb', '-nx', '-batch', '-x', str(skrypt)], stdout=gl, stderr=subprocess.STDOUT)
            # Maszyne wznawia wylacznie gdb ("continue" po ustawieniu punktu zatrzymania) - bez wyscigu.
            if czekaj(com1, 'GRUB-DIAG: PRZED-MULTIBOOT', 150):
                time.sleep(0.5)
                print('[DIAG] zrzut przed multiboot2:',
                      qmp.polecenie('pmemsave', val=0x800000, size=0x100000, filename=str((wyn / 'nvs-przed.bin').resolve())),
                      qmp.polecenie('pmemsave', val=0x100000, size=0xC6F000, filename=str((wyn / 'jadro-przed.bin').resolve())))
            else:
                print('[DIAG] BLAD: brak znacznika GRUB-DIAG: PRZED-MULTIBOOT')
            try:
                gdb.wait(timeout=240)
            except subprocess.TimeoutExpired:
                print('[DIAG] BLAD: gdb nie zakonczyl w 240 s (brak wejscia do jadra?)')
                gdb.kill()
            print('[DIAG] gdb:', (wyn / 'gdb.log').read_text(errors='replace').count('GDB-ZRZUT-OK') and 'zrzut OK' or 'BRAK zrzutu')
            st = qmp.polecenie('query-status')
            if st.get('return', {}).get('status') != 'running':
                qmp.polecenie('cont')
            time.sleep(a.czas_jadra)
            print('[DIAG] zrzut koncowy:', qmp.polecenie('pmemsave', val=0x800000, size=0x100000,
                                                         filename=str((wyn / 'nvs-koniec.bin').resolve())))
            qmp.polecenie('screendump', filename=str((wyn / 'ekran-koniec.png').resolve()), format='png')
            wynik = 0
        else:
            if czekaj(com1, '[BRB] zakonczenie: powrot do firmware', 150):
                wynik = 0
            else:
                print('[DIAG] BLAD: brak zakonczenia BRB')
        qmp.polecenie('quit')
    finally:
        if gdb and gdb.poll() is None:
            gdb.kill()
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=20)
    print(f'[DIAG] QEMU zakonczony kodem {proc.returncode}; wyniki: {wyn}')
    return wynik


if __name__ == '__main__':
    sys.exit(main())
