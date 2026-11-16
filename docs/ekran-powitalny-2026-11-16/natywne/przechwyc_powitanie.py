#!/usr/bin/env python3
"""Jeden przebieg QEMU (UEFI, virtio-vga 1280x720): zrzuty ekranu powitalnego co ~0,3 s.
Pierwszoplanowy, ograniczony czasowo, zawsze konczy QEMU."""
import hashlib, json, socket, subprocess, sys, tempfile, time
from pathlib import Path
from PIL import Image
iso, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
w, h = (int(v) for v in sys.argv[3].split('x')) if len(sys.argv) > 3 else (1280, 720)
LIMIT = float(sys.argv[4]) if len(sys.argv) > 4 else 300
out.mkdir(parents=True, exist_ok=True)
tmp = Path(tempfile.mkdtemp(prefix='powitanie-'))
sock = tmp / 'qmp.sock'
serial = out / 'serial.log'
cmd = ['qemu-system-x86_64', '-machine', 'pc,hpet=on,i8042=off', '-accel', 'tcg', '-cpu', 'max',
       '-smp', '1', '-m', '2048', '-display', 'none', '-serial', f'file:{serial}', '-no-reboot',
       '-qmp', f'unix:{sock},server=on,wait=off', '-device', 'qemu-xhci,id=usb',
       '-device', 'usb-kbd,bus=usb.0', '-device', 'usb-mouse,bus=usb.0',
       '-bios', '/usr/share/ovmf/OVMF.fd', '-vga', 'none',
       '-device', f'virtio-vga,disable-legacy=on,xres={w},yres={h}',
       '-drive', f'file={iso},id=boot,if=none,format=raw,readonly=on',
       '-device', 'virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1']
start = time.monotonic()
vm = subprocess.Popen(cmd, stdout=(out / 'qemu.log').open('w'), stderr=subprocess.STDOUT)
zdarzenia, poprzedni, ostatni_hash, n = [], start, None, 0
gotowy_od = None
class Qmp:
    def __init__(s, p):
        s.s = socket.socket(socket.AF_UNIX); s.s.connect(str(p)); s.f = s.s.makefile('rw')
        json.loads(s.f.readline()); s.call('qmp_capabilities')
    def call(s, c, a=None):
        s.f.write(json.dumps({'execute': c, **({'arguments': a} if a else {})}) + '\n'); s.f.flush()
        while True:
            r = json.loads(s.f.readline())
            if 'event' not in r: return r
try:
    q = None
    while time.monotonic() - start < LIMIT:
        teraz = time.monotonic()
        if teraz - poprzedni > 2.0:
            zdarzenia.append(f'STALL hosta/petli: {teraz - poprzedni:.1f} s przy t={teraz - start:.1f} s')
        poprzedni = teraz
        if vm.poll() is not None:
            zdarzenia.append(f'QEMU zakonczyl sie kodem {vm.returncode}'); break
        if q is None:
            try: q = Qmp(sock); q.s.settimeout(30)
            except OSError: time.sleep(0.3); continue
        p = out / 'tmp.ppm'
        try:
            q.call('screendump', {'filename': str(p), 'format': 'ppm'})
            with Image.open(p) as im: im.load(); ramka = im.convert('RGB')
        except Exception as e:
            zdarzenia.append(f'screendump: {e!r}'); time.sleep(0.3); continue
        hsh = hashlib.sha1(ramka.tobytes()).hexdigest()
        if hsh != ostatni_hash:
            ostatni_hash = hsh; n += 1
            ramka.save(out / f'klatka_{n:03d}_{teraz - start:07.2f}s.png')
        log = serial.read_text(errors='replace') if serial.exists() else ''
        for f in ('[PF]', '[KERNEL-PANIC]', '[GP]', 'KRYTYCZNY BLAD', '[USER FAULT]'):
            if f in log: zdarzenia.append('BLAD w logu: ' + f); LIMIT = 0
        if gotowy_od is None and ('[DIAG APP] First frame submitted' in log or '[STARTUP] desktop ready' in log):
            gotowy_od = teraz; zdarzenia.append(f'pulpit gotowy (log) przy t={teraz - start:.1f} s')
        if gotowy_od is not None and teraz - gotowy_od > 12: break
        time.sleep(0.3)
finally:
    try:
        vm.terminate(); vm.wait(timeout=20)
    except Exception:
        vm.kill(); vm.wait()
zdarzenia.append(f'koniec po {time.monotonic() - start:.1f} s, unikalnych klatek: {n}, QEMU rc={vm.returncode}')
(out / 'zdarzenia.txt').write_text('\n'.join(zdarzenia) + '\n')
print('\n'.join(zdarzenia))
