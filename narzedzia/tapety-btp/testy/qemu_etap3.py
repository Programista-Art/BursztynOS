#!/usr/bin/env python3
"""Jawne testy automatyczne QEMU; nigdy nie sa weryfikacja czlowieka.
Wszystkie dyski/logi w podanym prywatnym katalogu testowym.
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
class Qmp:
    def __init__(self, directory):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.settimeout(15)
        self.s.connect(str(directory / 'qmp.sock'))
        self.f = self.s.makefile('rwb', buffering=0)
        self.f.readline()
        self.call('qmp_capabilities')
    def call(self, execute, arguments=None):
        c = {'execute': execute}
        if arguments is not None: c['arguments'] = arguments
        self.f.write((json.dumps(c)+'\n').encode())
        while True:
            r = json.loads(self.f.readline())
            if 'error' in r: raise RuntimeError(r)
            if 'return' in r: return r['return']
    def hmp(self, text): return self.call('human-monitor-command', {'command-line': text})
    def click(self, x, y):
        # PS/2: male delty, aby nie przekroczyc reprezentacji pakietu myszy.
        for _ in range(20): self.hmp('mouse_move -100 -100'); time.sleep(.035)
        while x or y:
            dx=min(x,100);dy=min(y,100)
            self.hmp(f'mouse_move {dx} {dy}');time.sleep(.06);x-=dx;y-=dy
        for command in ['mouse_button 1','mouse_button 0']:
            self.hmp(command);time.sleep(.2)
    def capture(self, path): self.call('screendump', {'filename': str(path), 'format': 'png'})

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['boot','selector','click','capture','quit','reset','apply'])
    p.add_argument('--directory', type=Path, required=True)
    p.add_argument('--fresh', action='store_true', help='Usuwa tylko dysk w prywatnym katalogu testu')
    p.add_argument('--uefi', action='store_true')
    p.add_argument('--x', type=int, default=50);p.add_argument('--y', type=int, default=580)
    p.add_argument('--name', default='capture')
    p.add_argument('--wallpaper', type=int, choices=range(1,11), default=3)
    args=p.parse_args();d=args.directory.resolve();d.mkdir(parents=True,exist_ok=True)
    if args.action=='boot':
        disk=d/'wirtualny_dysk.img'
        if args.fresh:
            for f in [disk, d/'boot.log', d/'qemu.log', d/'qmp.sock']:
                if f.exists(): f.unlink()
        if not disk.exists():
            with disk.open('wb') as f:f.truncate(512*1024*1024)
        cmd=['qemu-system-x86_64','-machine','pc,hpet=on','-cpu','max','-accel','tcg,thread=single',
             '-drive',f'id=disk,file={disk},format=raw,if=none','-device','ich9-ahci,id=ahci',
             '-device','ide-hd,drive=disk,bus=ahci.0,unit=0','-netdev','user,id=n1','-device','e1000,netdev=n1',
             '-audiodev','none,id=snd','-device','intel-hda','-device','hda-output,audiodev=snd',
             '-m','2G','-vga','std','-global','VGA.vgamem_mb=64','-display','none','-serial',f'file:{d / "boot.log"}',
             '-qmp',f'unix:{d / "qmp.sock"},server=on,wait=off','-cdrom',str(ROOT/'.bursztyn_ci_test.iso'),'-boot','d']
        if args.uefi:
            firmware=Path('/usr/share/ovmf/OVMF.fd')
            if not firmware.is_file():raise RuntimeError('Brak monolitycznego OVMF.fd')
            cmd+=['-bios',str(firmware)]
        with (d/'qemu.log').open('wb') as log:
            proc=subprocess.Popen(cmd,stdout=log,stderr=log,start_new_session=True)
        (d/'pid').write_text(str(proc.pid))
        print(f'QEMU PID {proc.pid}; dysk {disk}',flush=True)
        deadline=time.monotonic()+360
        try:
            while time.monotonic()<deadline:
                if proc.poll() is not None:raise RuntimeError((d/'qemu.log').read_text())
                log=(d/'boot.log').read_text(errors='replace') if (d/'boot.log').exists() else ''
                if '[TEST MODE] AUTOMATED: zakonczono' in log:
                    try: Qmp(d).capture(d/'desktop.png')
                    except Exception: pass
                    proc.terminate()
                    if '=== TESTY BURSZTYN OS: FAIL' in log:raise RuntimeError('Suite FAIL; sprawdz boot.log')
                    print('Suite PASS; NOT MANUALLY VERIFIED BY HUMAN');return
                time.sleep(1)
            proc.kill()
            raise TimeoutError('Boot/test timeout')
        except Exception:
            try: proc.kill()
            except Exception: pass
            raise
    q=Qmp(d)
    if args.action=='selector':
        q.click(50,700);q.click(50,582);time.sleep(5);q.capture(d/'selector.png')
    elif args.action=='apply':
        i=args.wallpaper-1
        q.click(198+(i%5)*172,190+(i//5)*124);q.click(165,613);time.sleep(3);q.capture(d/'applied.png')
    elif args.action=='click':q.click(args.x,args.y);time.sleep(2);q.capture(d/(args.name+'.png'))
    elif args.action=='capture':q.capture(d/(args.name+'.png'))
    elif args.action=='quit':q.call('quit')
    elif args.action=='reset':q.call('system_reset')
if __name__=='__main__':main()
