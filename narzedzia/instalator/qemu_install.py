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
import re
from pathlib import Path
import shutil
import struct
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
    def __init__(self, path, kernel=None):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(60)
        self.sock.connect(str(path))
        self.f = self.sock.makefile('rwb', buffering=0)
        self.f.readline()
        self.call('qmp_capabilities')
        symbols=subprocess.check_output(['nm','-C',str(kernel or ROOT/'system_operacyjny.bin')],text=True)
        self.symbols=symbols
        self.tick_address=int(re.search(r'^([0-9a-f]+) [bBdD] licznik_tykniec_zegara$',symbols,re.M)[1],16)
    def symbol(self,name):
        m=re.search(r'^([0-9a-f]+) [bBdD] '+re.escape(name)+r'$',self.symbols,re.M)
        if not m:raise RuntimeError('Missing kernel observer '+name)
        return int(m[1],16)
    def settle(self, ticks=500):
        def now():
            return int(re.search(r':\s*0x([0-9a-fA-F]+)',self.hmp(f'xp /1gx 0x{self.tick_address:x}'))[1],16)
        start=now();deadline=time.monotonic()+45
        while now()-start<ticks:
            if time.monotonic()>deadline:raise TimeoutError('Installer GUI guest-time progress')
            time.sleep(.05)
    def call(self, cmd, args=None):
        obj = {'execute': cmd}
        if args is not None: obj['arguments'] = args
        self.f.write((json.dumps(obj)+'\n').encode())
        while True:
            r = json.loads(self.f.readline())
            if 'error' in r: raise RuntimeError(r)
            if 'return' in r: return r['return']
    def hmp(self, command): return self.call('human-monitor-command', {'command-line': command})
    def click(self, x, y, settle=True):
        # Kontroler wzgledny moze miec tylko jeden oczekujacy raport. Dajemy
        # gosciowi czas na odebranie kazdego ruchu; szybka seria sama w sobie
        # testowalaby przepełnienie kontrolera QEMU, a nie trafienie w GUI.
        for _ in range(16): self.hmp('mouse_move -100 -100'); time.sleep(.15)
        while x or y:
            dx, dy = min(x,100), min(y,100)
            self.hmp(f'mouse_move {dx} {dy}'); x-=dx; y-=dy; time.sleep(.15)
        address=self.symbol('zaktualizuj_mysze_wspolne(long, long, unsigned char, char const*)::poprzednie_przyciski_gui')
        def buttons():return int(re.search(r':\s*0x([0-9a-fA-F]+)',self.hmp(f'xp /1bx 0x{address:x}'))[1],16)
        def acknowledged(down):
            deadline=time.monotonic()+30
            while bool(buttons()&1)!=down:
                if time.monotonic()>deadline:raise TimeoutError('Native mouse button acknowledgement')
                time.sleep(.05)
        self.hmp('mouse_button 1');acknowledged(True);self.hmp('mouse_button 0');acknowledged(False)
        if settle:self.settle()
    def key(self, text): self.hmp('sendkey '+text);self.settle(60)
    def screenshot(self, path):
        # QEMU 6.x/Ubuntu nie akceptuje arguments.format dla screendump (PPM domyslnie).
        # Zachowujemy kompatybilnosc: dump bez formatu, potem konwersja do PNG gdy trzeba.
        path = Path(path)
        ppm = path if path.suffix.lower() == '.ppm' else path.with_suffix('.ppm')
        self.call('screendump', {'filename': str(ppm)})
        if path.suffix.lower() == '.png' and ppm != path:
            try:
                from PIL import Image
                Image.open(ppm).save(path)
            except Exception:
                # Fallback: zostaw PPM pod oczekiwana sciezka nazwy .png (harness i tak
                # uzywa pliku glownie jako artefakt diagnostyczny).
                if ppm.exists() and not path.exists():
                    ppm.replace(path)

class Guest:
    def __init__(self, directory, target, sentinel, iso=None, usb=False, visible=False):
        self.dir=directory;directory.mkdir()
        self.log=directory/'serial.log'
        self.qmp_path=Path('/tmp')/f'bursztyn-inst-{os.getpid()}-{directory.name}.sock'
        if self.qmp_path.exists(): self.qmp_path.unlink()
        args=['qemu-system-x86_64','-machine','pc,hpet=on','-cpu','max','-accel','tcg,thread=multi',
              '-m','2G','-vga','std','-global','VGA.vgamem_mb=64','-audiodev','none,id=snd',
              '-device','intel-hda','-device','hda-output,audiodev=snd','-netdev','user,id=n1','-device','e1000,netdev=n1',
              '-drive',f'if=none,id=sentinel,format=raw,file={sentinel}',
              '-device','virtio-blk-pci,drive=sentinel,disable-legacy=on',
              '-drive',f'if=none,id=target,format=raw,file={target}',
              '-device','virtio-blk-pci,drive=target,disable-legacy=on'+(',bootindex=1' if iso is None else ''),
              '-serial',f'file:{self.log}','-qmp',f'unix:{self.qmp_path},server=on,wait=off']
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
        while not self.qmp_path.exists():
            if self.proc.poll() is not None: raise RuntimeError((directory/'qemu.log').read_text())
            if time.monotonic()>deadline: raise TimeoutError('QMP socket')
            time.sleep(.1)
        public=directory.parent/'iso/boot/system_operacyjny.bin'
        private=ROOT/'.release-private'/sha(public)/'kernel.elf' if public.is_file() else ROOT/'system_operacyjny.bin'
        self.q=Qmp(self.qmp_path,kernel=private)
    def wait(self, needle, timeout=300):
        end=time.monotonic()+timeout;next_snapshot=time.monotonic()+60
        while time.monotonic()<end:
            text=self.log.read_text(errors='replace') if self.log.exists() else ''
            if needle in text:return text
            if '[KERNEL-PANIC]' in text or '[INSTALL] Instalacja nie powiodla' in text:
                self.q.screenshot(self.dir/'failure.png');raise RuntimeError(text[-3000:])
            if self.proc.poll() is not None:
                s_log = self.log.read_text(errors='replace') if self.log.exists() else ''
                q_log = (self.dir/'qemu.log').read_text(errors='replace') if (self.dir/'qemu.log').exists() else ''
                raise RuntimeError(f'QEMU exited: qemu.log={q_log}\nserial.log=\n{s_log[-2000:]}')
            if needle=='[INSTALL] COMPLETE verified' and time.monotonic()>=next_snapshot:
                evidence=dict(blockstats=self.q.call('query-blockstats'),registers=self.q.hmp('info registers'),serial_tail=text[-2000:])
                (self.dir/f'waiting-{int(timeout-(end-time.monotonic()))}.json').write_text(json.dumps(evidence,indent=2))
                next_snapshot=time.monotonic()+60
            time.sleep(.25)
        self.q.screenshot(self.dir/'timeout.png');raise TimeoutError(needle+'\n'+text[-2000:])
    def close(self):
        if self.proc.poll() is None:
            try:self.q.call('quit');self.proc.wait(timeout=10)
            except Exception:self.proc.terminate();self.proc.wait(timeout=10)
        if self.qmp_path.exists(): self.qmp_path.unlink()

def installer_iso(d):
    tree=d/'iso';shutil.copytree(ROOT/'isodir',tree)
    cfg=tree/'boot/grub/grub.cfg';text=cfg.read_text()
    text=text.replace('set default=0','set default=3').replace('set timeout=3','set timeout=0')
    cfg.write_text(text)
    iso=d/'installer.iso'
    with (d/'mkrescue.log').open('wb') as log:subprocess.run(['grub-mkrescue','-o',str(iso),str(tree)],stdout=log,stderr=log,check=True)
    return iso

def install(g,target,sentinel,before_target,before_sentinel):
    # Czysty start TCG obejmuje przygotowanie zasobow live w RAM i na
    # wolniejszych hostach moze przekroczyc trzy minuty. To nie jest limit
    # czasu samej transakcji instalatora.
    import sys
    sys.path.insert(0,str(ROOT/'narzedzia'))
    from bsf_process_observer import ProcessObserver
    g.wait('[INSTALL] Installer mode ready',timeout=300)
    g.wait('[SYSTEM] Pulpit gotowy',timeout=180)
    layout=json.loads((ROOT/'.bsf-test/installer-observer.json').read_text())
    observer=ProcessObserver(g.q.hmp,g.q.symbol,ROOT/'instalator.elf','Instalator')
    deadline=time.monotonic()+90
    while not observer.pid():
        if time.monotonic()>deadline:raise TimeoutError('Installer GUI process')
        time.sleep(.1)
    table=observer.address('vtable for (anonymous namespace)::Wizard')+16
    block=observer.value('(anonymous namespace)::poczatek_sterty','<Q');wizard=None
    for _ in range(2048):
        if not block:break
        data=observer.user(block,40)
        if not data[8] and struct.unpack_from('<Q',data,32)[0]==table:wizard=block+32;break
        block=struct.unpack_from('<Q',data,16)[0]
    if wizard is None:raise RuntimeError('Native installer Wizard not found')
    def value(name,fmt='<I'):return struct.unpack(fmt,observer.user(wizard+layout[name],struct.calcsize(fmt)))[0]
    def until(predicate,label,seconds=20):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if predicate():return True
            time.sleep(.1)
        return False
    def next_page(page):
        for attempt in range(3):
            current=value('page')
            if current==page:return
            if current!=page-1:raise AssertionError(('unexpected installer page',current,page))
            g.q.click(860,590)
            if until(lambda:value('page')==page,'installer page '+str(page)):return
            g.q.screenshot(g.dir/f'page-{page}-retry-{attempt}.png')
        raise AssertionError(('installer page did not advance',value('page'),page))
    g.q.screenshot(g.dir/'welcome.png');g.q.click(800,68)
    next_page(1);g.q.screenshot(g.dir/'disks.png')
    for attempt in range(3):
        g.q.click(550,230)
        if until(lambda:value('selected')==1,'target disk selected'):break
    assert value('selected')==1,'Disposable target was not selected'
    next_page(2);next_page(3);g.q.screenshot(g.dir/'summary.png')
    no_writes(g)
    assert target_fingerprint(target)==before_target,'Target changed before confirmation'
    assert sha(sentinel)==before_sentinel,'Sentinel changed during discovery'
    next_page(4);g.q.screenshot(g.dir/'confirmation.png')
    g.q.click(255,430);g.q.click(380,480);g.q.key('ctrl-a')
    for c in 'bursztyn':g.q.key('shift-'+c)
    g.q.screenshot(g.dir/'typed.png')
    assert until(lambda:value('enabled','<?'),'final confirmation enabled'),'Final native confirmation remains disabled'
    no_writes(g)
    assert target_fingerprint(target)==before_target,'Typing confirmation changed target'
    g.q.click(860,590,settle=False)
    text=g.wait('[INSTALL] COMPLETE verified',timeout=3600)
    time.sleep(1);g.q.screenshot(g.dir/'complete.png')
    assert '[PF]' not in text and '[KERNEL-PANIC]' not in text
    assert '[EKRAN] install failed' not in text
    assert '[TAPETA] install failed' not in text
    observed=[int(v) for v in re.findall(r'\[INSTALL\] progress=(\d+) stage=',text)]
    assert observed and observed[0]==0 and observed[-1]==100,observed
    assert all(a<=b<=100 for a,b in zip(observed,observed[1:])),observed
    assert len(set(observed))>8,observed
    assert all(0<=v<=100 for v in observed)
    assert all(a<=b for a,b in zip(observed,observed[1:]))
    assert text.index('[INSTALL] progress=100 stage=COMPLETE') < text.index('[INSTALL] COMPLETE verified')
    latency={name:int(value) for name,value in re.findall(
        r'\[INSTALL LATENCY\] stage=([A-Z0-9]+) maxnonyieldus=(\d+)',text)}
    for required in ('GPT','ESP','BSP2FORMAT','SYSTEMFILES','WALLPAPERS','VERIFY'):
        assert required in latency,latency
    print('installer max non-yield us: '+', '.join(
        f'{name}={latency[name]}' for name in
        ('GPT','ESP','BSP2FORMAT','SYSTEMFILES','WALLPAPERS','VERIFY')),flush=True)

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
