#!/usr/bin/env python3
"""Reusable QEMU driver for native Bursztyn OS desktop scenarios.

Boots an ISO (UEFI or BIOS) under TCG, serves a directory of pages through the
same guestfwd bridge as testy/test_husarz_qemu.py (guest sees http://10.0.2.100),
and exposes mouse/keyboard/screenshot helpers plus read-only guest memory
probes (QMP/HMP physical reads, no guest writes). Used by
desktop_responsiveness_qemu.py and the Explorer/window scenarios.
"""
import functools, http.server, re, shlex, subprocess, sys, tempfile, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
sys.path.insert(0, str(ROOT / 'testy'))
from bsf_process_observer import ProcessObserver  # noqa: E402
from test_hda_regression_qemu import Qmp  # noqa: E402
from PIL import Image  # noqa: E402

FAULTS = ('[PF]', '[KERNEL-PANIC]', '[GP]', 'KRYTYCZNY BLAD SYSTEMU',
          '[VIRTIO-GPU] BLAD KRYTYCZNY', '[VIRTIO-GPU-RECOVERY] PERMANENT_FAILURE', '[USER FAULT]')


class Guest:
    def __init__(self, iso, out, build=ROOT, bios=False, resolution='1280x720', pages=None,
                 timeout=1500, accel='tcg', extra_args=(), https=False, tablet=False, audio_wav=None):
        self.iso, self.out, self.build = Path(iso).resolve(), Path(out), Path(build).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.bios, self.timeout = bios, timeout
        self.w, self.h = (int(v) for v in resolution.split('x'))
        self.tablet = tablet
        # [USER FAULT] = jadro zakonczylo aplikacje Ring 3 (system dziala dalej); scenariusze moga to tolerowac.
        self.faults = FAULTS
        self.requests = []
        self.https_requests = []
        self.https_server = None
        self.tmp = Path(tempfile.mkdtemp(prefix='desktop-qemu-'))
        self.serial_path = self.out / 'serial.log'
        self.serial_path.write_text('')
        symbols = subprocess.check_output(['nm', '-C', str(self.build / 'system_operacyjny.bin')], text=True)
        self._symbols = symbols
        sock = self.tmp / 'qmp.sock'
        cmd = ['qemu-system-x86_64', '-machine', 'pc,hpet=on,i8042=off', '-accel', accel, '-cpu', 'max',
               '-smp', '1', '-m', '2048', '-display', 'none', '-serial', 'file:' + str(self.serial_path),
               '-no-reboot', '-qmp', f'unix:{sock},server=on,wait=off',
               '-device', 'qemu-xhci,id=usb', '-device', 'usb-kbd,bus=usb.0'] + (['-device', 'virtio-tablet-pci,disable-legacy=on'] if tablet else ['-device', 'usb-mouse,bus=usb.0']) + [
               '-audiodev', ('wav,id=snd,path=' + str(audio_wav)) if audio_wav else 'none,id=snd',
               '-device', 'intel-hda', '-device', 'hda-output,audiodev=snd']
        if bios:
            cmd += ['-vga', 'std', '-global', 'VGA.vgamem_mb=64', '-cdrom', str(self.iso), '-boot', 'd']
        else:
            cmd += ['-bios', '/usr/share/ovmf/OVMF.fd', '-vga', 'none',
                    '-device', f'virtio-vga,disable-legacy=on,xres={self.w},yres={self.h}',
                    '-drive', f'file={self.iso},id=boot,if=none,format=raw,readonly=on',
                    '-device', 'virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1']
        self.server = None
        if pages:
            guest = self

            class Handler(http.server.SimpleHTTPRequestHandler):
                def __init__(self, *a, **kw):
                    super().__init__(*a, directory=str(pages), **kw)

                def do_GET(self):
                    guest.requests.append((time.monotonic(), self.path))
                    super().do_GET()

                def log_message(self, *_):
                    pass

            self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            threading.Thread(target=self.server.serve_forever, daemon=True).start()
            bridge = shlex.join([sys.executable, str(ROOT / 'testy' / 'test_husarz_qemu.py'), '--bridge',
                                 str(self.server.server_port)])
            fwd = 'guestfwd=tcp:10.0.2.100:80-cmd:' + bridge
            if https:
                # Local TLS fixture (testy/husarz_load): same guest address, port 443, raw TCP bridge.
                import subprocess as _sp
                _sp.run([str(ROOT / 'testy/husarz_load/make_tls_fixture.sh')], check=True)
                sys.path.insert(0, str(ROOT / 'testy/husarz_load'))
                import https_server
                self.https_server = https_server.serve(pages, self.https_requests)
                hb = shlex.join([sys.executable, str(ROOT / 'testy' / 'test_husarz_qemu.py'), '--bridge',
                                 str(self.https_server.server_port)])
                fwd += ',guestfwd=tcp:10.0.2.100:443-cmd:' + hb
            cmd += ['-netdev', 'user,id=n1,' + fwd,
                    '-device', 'virtio-net-pci,netdev=n1,disable-legacy=on']
        cmd += list(extra_args)
        self.deadline = time.monotonic() + timeout
        self.vm = subprocess.Popen(cmd, stdout=(self.out / 'qemu.log').open('w'), stderr=subprocess.STDOUT)
        self.sock, self.q = sock, None
        self.boot_started = time.monotonic()

    # ---- observation -------------------------------------------------
    def symbol(self, name):
        m = re.search(r'^([0-9a-f]+) [bBdDrR] ' + re.escape(name) + r'$', self._symbols, re.M)
        if not m:
            raise RuntimeError('Missing kernel symbol ' + name)
        return int(m[1], 16)

    def log(self):
        text = self.serial_path.read_text(errors='replace')
        complete = text[:text.rfind('\n') + 1]
        if self.vm.poll() is not None or any(f in complete for f in self.faults):
            raise RuntimeError('Guest stopped or faulted\n' + text[-3000:])
        if time.monotonic() > self.deadline:
            raise TimeoutError('deadline\n' + text[-2000:])
        return text

    def wait(self, pred, label, seconds=120, interval=.25):
        until = min(self.deadline, time.monotonic() + seconds)
        while time.monotonic() < until:
            self.log()
            try:
                if pred():
                    return
            except (AssertionError, RuntimeError, KeyError):
                pass
            time.sleep(interval)
        raise TimeoutError(label)

    def hmp(self, command):
        reply = self.q.call('human-monitor-command', {'command-line': command})
        if 'error' in reply:
            raise RuntimeError(reply)
        return reply.get('return', '')

    def word(self, addr, fmt='w'):
        return int(re.search(r':\s*0x([0-9a-fA-F]+)', self.hmp(f'xp /1{fmt}x 0x{addr:x}'))[1], 16)

    def ticks(self):
        return self.word(self._ticks, 'g')

    # ---- boot ---------------------------------------------------------
    def boot_to_desktop(self):
        self.wait(lambda: '[DIAG APP] First frame submitted' in self.log(), 'native desktop ready', 900)
        self.desktop_seconds = round(time.monotonic() - self.boot_started, 1)
        self.wait(lambda: all(f'role={r} result=interrupt IN armed' in self.log()
                              for r in (('boot-keyboard',) if self.tablet else ('boot-keyboard', 'boot-mouse'))), 'USB HID ready', 300)
        self.q = Qmp(self.sock)
        self.q.socket.settimeout(15)
        if not self.tablet:
            mouse = next(m for m in self.q.call('query-mice')['return'] if 'USB' in m['name'] or 'HID' in m['name'])
            self.hmp(f'mouse_set {mouse["index"]}')
        self._cursor = [self.symbol('mysz_x'), self.symbol('mysz_y')]
        self._ticks = self.symbol('licznik_tykniec_zegara')
        self._buttons = self.symbol(
            'zaktualizuj_mysze_wspolne(long, long, unsigned char, char const*)::poprzednie_przyciski_gui')
        self.w, self.h = self.screenshot('size-probe', True).size
        self.offset = (0, 0)
        return self

    def cursor(self):
        return [self.word(c) for c in self._cursor]

    # ---- input ---------------------------------------------------------
    def move(self, x, y):
        if self.tablet:
            ax, ay = int(x * 32767 / max(1, self.w - 1)), int(y * 32767 / max(1, self.h - 1))
            self.q.call('input-send-event', {'events': [{'type': 'abs', 'data': {'axis': 'x', 'value': ax}},
                                                         {'type': 'abs', 'data': {'axis': 'y', 'value': ay}}]})
            self.wait(lambda: max(abs(c - t) for c, t in zip(self.cursor(), (x, y))) <= 2, 'tablet moved', 15, .1)
            return
        for _ in range(80):
            self.log()
            old = self.cursor()
            dx, dy = max(-64, min(64, x - old[0])), max(-64, min(64, y - old[1]))
            if not dx and not dy:
                return
            self.hmp(f'mouse_move {dx} {dy}')
            self.wait(lambda: self.cursor() != old, 'mouse consumed', 15, .15)
        raise RuntimeError('cursor did not reach target')

    def button(self, down):
        if self.tablet:
            self.q.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': bool(down)}}]})
        else:
            self.hmp('mouse_button 1' if down else 'mouse_button 0')
        self.wait(lambda: bool(self.word(self._buttons, 'b') & 1) == bool(down),
                  'button acknowledged', 30, .1)

    def click(self, x, y, settle=0.5):
        self.move(x, y)
        self.button(True)
        self.button(False)
        t = self.ticks()
        self.wait(lambda: self.ticks() - t >= 32, 'GUI dispatch', 180, .15)
        time.sleep(settle)

    def drag(self, x0, y0, x1, y1, steps=8):
        self.move(x0, y0)
        self.button(True)
        for i in range(1, steps + 1):
            self.move(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps)
        self.button(False)
        t = self.ticks()
        self.wait(lambda: self.ticks() - t >= 32, 'GUI dispatch', 180, .15)

    def key(self, *codes):
        t = self.ticks()
        self.q.call('send-key', {'keys': [{'type': 'qcode', 'data': c} for c in codes], 'hold-time': 80})
        self.wait(lambda: self.ticks() - t >= 48, 'key consumed', 180, .15)

    def type_text(self, text):
        names = {'/': 'slash', '.': 'dot', '-': 'minus', ' ': 'spc', '_': ('shift', 'minus')}
        for ch in text:
            if ch == ':':
                self.key('shift', 'semicolon')
            elif ch.isupper():
                self.key('shift', ch.lower())
            else:
                n = names.get(ch, ch)
                self.key(*(n if isinstance(n, tuple) else (n,)))

    def screenshot(self, name, probe=False):
        path = self.out / (name + '.ppm')
        self.q.call('stop')
        try:
            self.q.call('screendump', {'filename': str(path), 'format': 'ppm'})
        finally:
            self.q.call('cont')
        with Image.open(path) as im:
            im.load()
            frame = im.copy()
        if not probe:
            frame.save(self.out / (name + '.png'))
            path.unlink()
        return frame

    def launch(self, query):
        """Start menu search + Enter, like a user."""
        region = (15, self.h - 635, 80, self.h - 595)
        before = self.screenshot('start-closed-probe', True).crop(region).tobytes()
        self.click(32, self.h - 20)
        self.wait(lambda: self.screenshot('start-open-probe', True).crop(region).tobytes() != before,
                  'start menu opened', 120, 1)
        self.type_text(query)
        self.key('ret')

    def observer(self, elf, title):
        return ProcessObserver(self.hmp, self.symbol, self.build / elf, title)

    def close(self):
        try:
            self.vm.terminate()
            self.vm.wait(timeout=20)
        except Exception:
            self.vm.kill()
        if self.server:
            self.server.shutdown()
        if self.https_server:
            self.https_server.shutdown()
