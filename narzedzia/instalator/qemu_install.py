#!/usr/bin/env python3
"""AUTOMATED GUEST INSTALL TEST. Only private regular image files, never host devices.
The real GUI receives mouse/keyboard events; there is no auto-erase kernel parameter.
NOT MANUALLY VERIFIED ON REAL HARDWARE.
"""
import argparse
import hashlib
import json
import os
import errno
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(8*1024*1024), b''): h.update(b)
    return h.hexdigest()

def sparse(path, size):
    if path.exists(): raise RuntimeError(f'Refusing to replace {path}')
    with path.open('xb') as f: f.truncate(size)

def target_fingerprint(path):
    """Hash allocated extents with offsets; QMP independently checks zero writes.
    The sentinel uses sha() over its entire logical disk, including all holes.
    """
    h=hashlib.sha256();size=path.stat().st_size;h.update(str(size).encode())
    with path.open('rb') as f:
        off=0
        while off<size:
            try:start=os.lseek(f.fileno(),off,os.SEEK_DATA)
            except OSError as e:
                if e.errno==errno.ENXIO:break
                raise
            end=os.lseek(f.fileno(),start,os.SEEK_HOLE)
            h.update(f'{start}:{end}:'.encode());f.seek(start)
            left=end-start
            while left:
                b=f.read(min(left,8*1024*1024));h.update(b);left-=len(b)
            off=end
    return h.hexdigest()

def no_writes(guest):
    for device in guest.q.call('query-blockstats'):
        if device.get('device') in ('target','sentinel'):
            assert device['stats']['wr_operations']==0,device

class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(60)
        self.sock.connect(str(path))
        self.f = self.sock.makefile('rwb', buffering=0)
        self.f.readline()
        self.call('qmp_capabilities')
    def call(self, cmd, args=None):
        obj = {'execute': cmd}
        if args is not None: obj['arguments'] = args
        self.f.write((json.dumps(obj)+'\n').encode())
        while True:
            r = json.loads(self.f.readline())
            if 'error' in r: raise RuntimeError(r)
            if 'return' in r: return r['return']
    def hmp(self, command): return self.call('human-monitor-command', {'command-line': command})
    def click(self, x, y):
        for _ in range(16): self.hmp('mouse_move -100 -100'); time.sleep(.02)
        while x or y:
            dx, dy = min(x,100), min(y,100)
            self.hmp(f'mouse_move {dx} {dy}'); x-=dx; y-=dy; time.sleep(.05)
        self.hmp('mouse_button 1');time.sleep(.12);self.hmp('mouse_button 0');time.sleep(.4)
    def key(self, text): self.hmp('sendkey '+text);time.sleep(.15)
    def screenshot(self, path): self.call('screendump', {'filename':str(path),'format':'png'})

class Guest:
    def __init__(self, directory, target, sentinel, iso=None, usb=False, visible=False):
        self.dir=directory;directory.mkdir()
        self.log=directory/'serial.log'
        args=['qemu-system-x86_64','-machine','pc,hpet=on','-cpu','max','-accel','tcg,thread=single',
              '-m','2G','-vga','std','-global','VGA.vgamem_mb=64','-audiodev','none,id=snd',
              '-device','intel-hda','-device','hda-output,audiodev=snd','-netdev','user,id=n1','-device','e1000,netdev=n1',
              '-device','ich9-ahci,id=ahci','-drive',f'if=none,id=sentinel,format=raw,file={sentinel}',
              '-device','ide-hd,drive=sentinel,bus=ahci.0,model=SENTINEL DO NOT ERASE,serial=SENTINEL0001',
              '-drive',f'if=none,id=target,format=raw,file={target}',
              '-device','ide-hd,drive=target,bus=ahci.1,model=Bursztyn Installer Target,serial=INSTALLTARGET0001'+(',bootindex=1' if iso is None else ''),
              '-serial',f'file:{self.log}','-qmp',f'unix:{directory/"qmp.sock"},server=on,wait=off']
        if not visible: args+=['-display','none']
        ovmf=Path('/usr/share/ovmf/OVMF.fd')
        if not ovmf.is_file(): raise RuntimeError('OVMF.fd is required')
        # A combined OVMF image can persist BootOrder. Give every guest a
        # private firmware copy: tests stay reproducible and never mutate the
        # host's shared firmware image.
        guest_ovmf=directory/'OVMF.fd'
        shutil.copyfile(ovmf,guest_ovmf)
        args+=['-drive',f'if=pflash,format=raw,file={guest_ovmf}']
        if iso:
            if usb:
                args+=['-device','qemu-xhci,id=xhci','-drive',f'if=none,id=media,format=raw,readonly=on,file={iso}',
                       '-device','usb-storage,drive=media,bus=xhci.0,bootindex=1']
            else:
                # OVMF may remember the fallback loader found on an already
                # installed target. Give the installation CD an explicit EFI
                # boot priority so the reinstall phase cannot silently boot
                # the target instead of installer mode.
                args+=['-drive',f'if=none,id=media,format=raw,readonly=on,file={iso}',
                       '-device','ide-cd,drive=media,bus=ide.0,bootindex=1']
        with (directory/'qemu.log').open('wb') as out: self.proc=subprocess.Popen(args,stdout=out,stderr=out)
        (directory/'pid').write_text(str(self.proc.pid))
        deadline=time.monotonic()+30
        while not (directory/'qmp.sock').exists():
            if self.proc.poll() is not None: raise RuntimeError((directory/'qemu.log').read_text())
            if time.monotonic()>deadline: raise TimeoutError('QMP socket')
            time.sleep(.1)
        self.q=Qmp(directory/'qmp.sock')
    def wait(self, needle, timeout=180):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            text=self.log.read_text(errors='replace') if self.log.exists() else ''
            if needle in text:return text
            if '[KERNEL-PANIC]' in text or '[INSTALL] Instalacja nie powiodla' in text:
                self.q.screenshot(self.dir/'failure.png');raise RuntimeError(text[-3000:])
            if self.proc.poll() is not None:raise RuntimeError('QEMU exited')
            time.sleep(.25)
        self.q.screenshot(self.dir/'timeout.png');raise TimeoutError(needle+'\n'+text[-2000:])
    def close(self):
        if self.proc.poll() is None:
            try:self.q.call('quit');self.proc.wait(timeout=10)
            except Exception:self.proc.terminate();self.proc.wait(timeout=10)

def installer_iso(d):
    tree=d/'iso';shutil.copytree(ROOT/'isodir',tree)
    cfg=tree/'boot/grub/grub.cfg';text=cfg.read_text()
    text=text.replace('set default=0','set default=2').replace('set timeout=3','set timeout=0')
    cfg.write_text(text)
    iso=d/'installer.iso'
    with (d/'mkrescue.log').open('wb') as log:subprocess.run(['grub-mkrescue','-o',str(iso),str(tree)],stdout=log,stderr=log,check=True)
    return iso

def install(g,target,sentinel,before_target,before_sentinel):
    g.wait('[INSTALL] Installer mode ready');time.sleep(2)
    g.q.screenshot(g.dir/'welcome.png')
    # Coordinates for 1280x720 framebuffer, centered 840x570 installer.
    g.q.click(860,590) # welcome -> disks
    g.q.screenshot(g.dir/'disks.png')
    g.q.click(550,230) # target, second actual SATA device
    g.q.click(860,590) # options
    g.q.click(860,590) # whole disk / preview
    g.q.screenshot(g.dir/'summary.png')
    no_writes(g)
    assert target_fingerprint(target)==before_target,'Target changed before confirmation'
    assert sha(sentinel)==before_sentinel,'Sentinel changed during discovery'
    g.q.click(860,590) # first confirmation
    g.q.screenshot(g.dir/'confirmation.png')
    g.q.click(255,430) # source warning acknowledgement
    g.q.click(380,480) # text field
    for c in 'burstztyn':g.q.key('shift-'+c)
    g.q.screenshot(g.dir/'typed.png')
    no_writes(g)
    assert target_fingerprint(target)==before_target,'Typing confirmation changed target'
    g.q.click(860,590) # final destructive confirmation
    text=g.wait('[INSTALL] COMPLETE verified',timeout=900)
    time.sleep(1);g.q.screenshot(g.dir/'complete.png')
    assert '[PF]' not in text and '[KERNEL-PANIC]' not in text

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('action',choices=['test','run','boot-debug']);args=ap.parse_args()
    parent=ROOT/('.installer-run' if args.action=='run' else '.installertest');parent.mkdir(exist_ok=True)
    d=Path(tempfile.mkdtemp(prefix='guest-',dir=parent));print(f'Artifacts: {d}',flush=True)
    target=d/'target.img';sentinel=d/'sentinel.img';sparse(target,4*1024**3);sparse(sentinel,64*1024**2)
    with sentinel.open('r+b') as f:
        for off in [0,512,10*512,40000*512,64*1024**2-512]:f.seek(off);f.write(hashlib.sha256(str(off).encode()).digest()*16)
    iso=installer_iso(d);sentinel_hash=sha(sentinel);target_hash=target_fingerprint(target)
    if args.action in ('run','boot-debug'):
        g=Guest(d/'interactive',target,sentinel,iso,visible=args.action=='run')
        print(f'QEMU pid={g.proc.pid}',flush=True)
        if args.action=='run':g.proc.wait()
        else:g.wait('[INSTALL] Installer mode ready');g.q.screenshot(g.dir/'welcome.png')
        return
    for name in ['blank','reinstall']:
        g=Guest(d/name,target,sentinel,iso)
        try:install(g,target,sentinel,target_hash,sentinel_hash)
        finally:g.close()
        assert sha(sentinel)==sentinel_hash,'Other disk changed'
        print(f'{name}: guest GUI install PASS; second-disk SHA256 unchanged',flush=True)
        g=Guest(d/(name+'-disk-only'),target,sentinel)
        try:
            log=g.wait('System operacyjny gotowy!');time.sleep(3);g.q.screenshot(g.dir/'desktop.png')
            assert '[BSP] Wybrano kompletny wolumin GPT' in log
            assert '[INSTALL] Installer mode ready' not in log
            assert '[TAPETA] Tapeta gotowa.' in log
        finally:g.close()
        target_hash=target_fingerprint(target)
        assert sha(sentinel)==sentinel_hash
        print(f'{name}: UEFI disk-only boot PASS (ISO removed)',flush=True)
    g=Guest(d/'usb',target,sentinel,iso,usb=True)
    try:g.wait('[INSTALL] Installer mode ready');time.sleep(2);g.q.screenshot(g.dir/'usb-installer.png')
    finally:g.close()
    assert sha(sentinel)==sentinel_hash and target_fingerprint(target)==target_hash
    print('Hybrid ISO as USB mass-storage image: UEFI guest boot PASS; 0 target writes',flush=True)
    (d/'result.json').write_text(json.dumps({'blank':True,'reinstall':True,'disk_only':True,'usb':True,'sentinel_sha256':sentinel_hash,'hardware':'NOT MANUALLY VERIFIED ON REAL HARDWARE'},indent=2))
    print('AUTOMATED GUEST INSTALL TEST PASS; NOT MANUALLY VERIFIED ON REAL HARDWARE',flush=True)

if __name__=='__main__':main()
