#!/usr/bin/env python3
"""Interactive QMP driver for a private installed JPEG-wallpaper test disk."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'narzedzia/tapety-btp/testy'))
from qemu_etap3 import Qmp


def main():
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=('boot', 'click', 'capture', 'key', 'reset', 'quit', 'wait'))
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--disk', type=Path)
    p.add_argument('--x', type=int, default=0)
    p.add_argument('--y', type=int, default=0)
    p.add_argument('--name', default='capture')
    p.add_argument('--key', default='ret')
    p.add_argument('--marker', default='System operacyjny gotowy!')
    p.add_argument('--timeout', type=int, default=300)
    args = p.parse_args()
    d = args.directory.resolve()
    d.mkdir(parents=True, exist_ok=True)
    socket = d / 'qmp.sock'
    serial = d / 'serial.log'
    if args.action == 'boot':
        if not args.disk or socket.exists():
            raise RuntimeError('disk required; existing QMP socket must be absent')
        firmware = d / 'OVMF.fd'
        shutil.copyfile('/usr/share/ovmf/OVMF.fd', firmware)
        cmd = ['qemu-system-x86_64', '-machine', 'pc,hpet=on', '-cpu', 'max',
               '-accel', 'tcg,thread=single', '-m', '2G', '-vga', 'std',
               '-global', 'VGA.vgamem_mb=64', '-display', 'none',
               '-device', 'ich9-ahci,id=ahci',
               '-drive', f'if=none,id=disk,format=raw,file={args.disk.resolve()}',
               '-device', 'ide-hd,drive=disk,bus=ahci.0,bootindex=1',
               '-drive', f'if=pflash,format=raw,file={firmware}',
               '-serial', f'file:{serial}', '-qmp', f'unix:{socket},server=on,wait=off',
               '-pidfile', str(d / 'pid'), '-daemonize']
        subprocess.run(cmd, check=True, stdout=(d / 'qemu.log').open('wb'),
                       stderr=subprocess.STDOUT)
        print(f'QEMU started: {d}')
        return
    if args.action == 'wait':
        until = time.monotonic() + args.timeout
        while time.monotonic() < until:
            data = serial.read_text(errors='replace') if serial.exists() else ''
            if args.marker in data:
                print(f'QEMU marker PASS: {args.marker}')
                return
            if '[KERNEL-PANIC]' in data or 'PF/' in data:
                raise RuntimeError('guest panic or page fault')
            time.sleep(1)
        raise RuntimeError(f'QEMU marker timeout: {args.marker}')
    q = Qmp(d)
    if args.action == 'click':
        q.click(args.x, args.y)
        time.sleep(2)
        q.capture(d / (args.name + '.png'))
    elif args.action == 'capture':
        q.capture(d / (args.name + '.png'))
    elif args.action == 'key':
        q.hmp('sendkey ' + args.key)
        time.sleep(1)
        q.capture(d / (args.name + '.png'))
    elif args.action == 'reset':
        q.call('system_reset')
    elif args.action == 'quit':
        q.call('quit')


if __name__ == '__main__':
    main()
