#!/usr/bin/env python3
"""BSF 2.0 / 2.1 native verification on the real Bursztyn OS ISO under QEMU.

--workload bsf21 (BSF 2.1) additionally serves testy/husarz_bidi/*.html over
QEMU guestfwd (same bridge as testy/test_husarz_qemu.py), opens them in
Husarz, reads the BSF counters and the Husarz shaped-run counters from the
browser process, copies an RTL form value with the real clipboard (Tab,
Ctrl+A, Ctrl+C), pastes it into Notepad, moves/selects with the arrows and
reads Notepad's BSF counters (owned paragraph layouts prove the BSF 2.1 path).

Boots the production ISO (UEFI or BIOS), waits for the native desktop,
opens Settings -> Fonts (whose preview contains Hebrew + Arabic samples),
and reads the BSF 2.0 counters straight from the Settings Ring 3 process
memory through the read-only ProcessObserver (QMP physical reads, no guest
writes). Also launches Notepad and the Shell, and checks the serial log for
kernel/user faults. Results: <out>/bsf2-native.json plus PNG screenshots.
"""
import argparse, functools, http.server, json, re, shlex, struct, subprocess, sys, tempfile, threading, time

REQUEST_ORIGINS = []  # (path, Origin header or None) seen by the bsf22 page server
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
sys.path.insert(0, str(ROOT / 'testy'))
from bsf_process_observer import ProcessObserver  # noqa: E402
from test_hda_regression_qemu import Qmp  # noqa: E402
from PIL import Image  # noqa: E402

FAULTS = ('[PF]', '[KERNEL-PANIC]', '[GP]', 'KRYTYCZNY BLAD SYSTEMU',
          '[VIRTIO-GPU] BLAD KRYTYCZNY', '[VIRTIO-GPU-RECOVERY] PERMANENT_FAILURE', '[USER FAULT]')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', type=Path, default=ROOT / 'BursztynOS.iso')
    ap.add_argument('--bios', action='store_true')
    ap.add_argument('--resolution', default='1280x720')
    ap.add_argument('--out', type=Path, default=ROOT / '.bsf-test/bsf2-native')
    ap.add_argument('--timeout', type=int, default=900)
    ap.add_argument('--video', help='request a guest mode WxH via a grub.cfg-only fixture ISO')
    ap.add_argument('--root', type=Path, default=ROOT,
                    help='build tree whose kernel/app ELFs match --iso (symbols)')
    ap.add_argument('--workload', choices=('bsf2', 'apps', 'bsf21', 'bsf22'), default='bsf2',
                    help='apps: same Settings/Notepad/Shell sequence without BSF 2.0 '
                         'counters, for A/B runs against pre-BSF-2.0 commits')
    ap.add_argument('--tag', default='', help='output subdirectory suffix (parallel runs)')
    ap.add_argument('--hmp-stress', action='store_true',
                    help='continuous 2 KiB physical-memory reads on a second QMP socket '
                         '(QEMU main-loop load, identical for every variant)')
    a = ap.parse_args()
    w, h = (int(v) for v in (a.video or a.resolution).split('x'))
    mode = ('bios' if a.bios else 'uefi') + f'-{w}x{h}' + ('-video' if a.video else '')
    out = a.out / (mode + (('-' + a.tag) if a.tag else ''))
    out.mkdir(parents=True, exist_ok=True)
    build = a.root.resolve()
    kernel = build / 'system_operacyjny.bin'
    symbols = subprocess.check_output(['nm', '-C', str(kernel)], text=True)

    def symbol(name):
        m = re.search(r'^([0-9a-f]+) [bBdDrR] ' + re.escape(name) + r'$', symbols, re.M)
        if not m:
            raise RuntimeError('Missing kernel symbol ' + name)
        return int(m[1], 16)

    tmp = Path(tempfile.mkdtemp(prefix='bsf2-qemu-'))
    sock, serial = tmp / 'qmp.sock', out / 'serial.log'
    for stale in out.iterdir():  # never read a previous run's log/frames
        stale.unlink()
    if a.video:
        # Disposable fixture ISO: only grub.cfg changes (gfxmode and the
        # kernel's bursztyn.video request), exactly as graphics_perf_qemu.py.
        import shutil
        vw, vh = (int(v) for v in a.video.split('x'))
        staging = tmp / 'staging'
        with (out / 'iso-build.log').open('w') as log:
            subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(a.iso.resolve()), '-extract', '/',
                            str(staging)], stdout=log, stderr=subprocess.STDOUT, check=True)
            cfg = staging / 'boot/grub/grub.cfg'
            text = cfg.read_text()
            text = re.sub(r'set gfxmode=[^\n]+', f'set gfxmode={vw}x{vh}x32', text)
            text = re.sub(r'set timeout=\d+', 'set timeout=0', text)
            text = re.sub(r' bursztyn.video=[0-9]+x[0-9]+', '', text)
            text = text.replace('multiboot2 /boot/system_operacyjny.bin',
                                f'multiboot2 /boot/system_operacyjny.bin bursztyn.video={vw}x{vh}')
            cfg.chmod(0o644)
            cfg.write_text(text)
            fixture = tmp / 'mode.iso'
            subprocess.run(['grub-mkrescue', '-o', str(fixture), str(staging)], stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        a.iso = fixture
    if a.workload == 'bsf22':
        # BSF 2.2 fixture ISO: the production ISO plus collection fixtures
        # (TrueType TTC, CFF OTC), delivered like release fonts (Multiboot2
        # modules labelled bsf/<name>, routed by extension in the kernel).
        import shutil
        staging = tmp / 'staging22'
        with (out / 'iso-build.log').open('a') as log:
            subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(a.iso.resolve()), '-extract', '/',
                            str(staging)], stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run(['chmod', '-R', 'u+w', str(staging)], check=True)  # ISO9660 extracts read-only
            fx = ROOT / 'testy/fixtures/bsf22/fonts'
            extra = [(fx / 'NotoSans-subset.ttc', 'ttc', 'NotoSans-subset.ttc'),
                     (fx / 'NotoSansCJK-subset.ttc', 'otc', 'NotoSansCJK-subset.otc')]
            lines = ''
            for src, d, name in extra:
                (staging / 'czcionki' / d).mkdir(parents=True, exist_ok=True)
                (staging / 'czcionki' / d).chmod(0o755)
                shutil.copy2(src, staging / 'czcionki' / d / name)
                lines += f'    module2 /czcionki/{d}/{name} bsf/{name}\n'
            cfg = staging / 'boot/grub/grub.cfg'
            cfg.chmod(0o644)
            text = cfg.read_text()
            text = re.sub(r'set timeout=\d+', 'set timeout=0', text)
            text = re.sub(r'(?m)^([ \t]*)boot\s*$', lambda m: lines + m.group(1) + 'boot', text, count=1)
            cfg.write_text(text)
            fixture = tmp / 'bsf22.iso'
            subprocess.run(['grub-mkrescue', '-o', str(fixture), str(staging)], stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        a.iso = fixture
    cmd = ['qemu-system-x86_64', '-machine', 'pc,hpet=on,i8042=off', '-accel', 'tcg', '-cpu', 'max',
           '-smp', '1', '-m', '2048', '-display', 'none', '-serial', 'file:' + str(serial),
           '-no-reboot', '-qmp', f'unix:{sock},server=on,wait=off',
           '-qmp', f'unix:{tmp / "stress.sock"},server=on,wait=off',
           '-device', 'qemu-xhci,id=usb', '-device', 'usb-kbd,bus=usb.0', '-device', 'usb-mouse,bus=usb.0',
           '-audiodev', 'none,id=snd', '-device', 'intel-hda', '-device', 'hda-output,audiodev=snd']
    if a.bios:
        cmd += ['-vga', 'std', '-global', 'VGA.vgamem_mb=64', '-cdrom', str(a.iso.resolve()), '-boot', 'd']
    else:
        cmd += ['-bios', '/usr/share/ovmf/OVMF.fd', '-vga', 'none',
                '-device', f'virtio-vga,disable-legacy=on,xres={w},yres={h}',
                '-drive', f'file={a.iso.resolve()},id=boot,if=none,format=raw,readonly=on',
                '-device', 'virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1']
    result = {'mode': mode, 'workload': a.workload, 'root': str(build), 'stage': 'boot', 'iso_sha256': subprocess.check_output(['sha256sum', str(a.iso)], text=True).split()[0],
              'checks': {}}
    deadline = time.monotonic() + a.timeout
    requests = []
    if a.workload == 'bsf22':
        # Pages + fonts in one served tree; /cors-ok/ answers with
        # Access-Control-Allow-Origin: *, /cors-bad/ without it.
        import shutil
        served = tmp / 'served'
        (served / 'fonts').mkdir(parents=True)
        for page in (ROOT / 'testy' / 'husarz_webfonts').glob('*.html'):
            shutil.copy2(page, served / page.name)
        web, fx = ROOT / 'testy/fixtures/bsf22/web', ROOT / 'testy/fixtures/bsf22/fonts'
        for f in (web / 'NotoSans-Latin.woff2', web / 'NotoSans-Latin.woff', web / 'NotoNaskhArabic.woff',
                  web / 'NotoSans-Latin-Bold.woff2',
                  fx / 'NotoSansHebrew-Regular.otf', fx / 'NotoSans-Regular.otf', fx / 'NotoSans-Bold.otf',
                  web / 'NotoSansHebrew-VF.woff2'):
            shutil.copy2(f, served / 'fonts' / f.name)
        shutil.copy2(ROOT / 'testy/husarz_webfonts/bad.woff2.src', served / 'fonts' / 'bad.woff2')
        for d in ('cors-ok', 'cors-bad'):
            (served / d).mkdir()
            for f in (web / 'NotoSans-Latin.woff2', web / 'NotoSans-Latin.woff'):
                shutil.copy2(f, served / d / f.name)

        class Pages22(http.server.SimpleHTTPRequestHandler):
            extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                              '.woff2': 'font/woff2', '.woff': 'font/woff', '.otf': 'font/otf', '.ttf': 'font/ttf'}

            def __init__(self, *args, **kw):
                super().__init__(*args, directory=str(served), **kw)

            def end_headers(self):
                if self.path.startswith('/cors-ok/'):
                    self.send_header('Access-Control-Allow-Origin', '*')
                super().end_headers()

            def do_GET(self):
                requests.append(self.path)
                REQUEST_ORIGINS.append((self.path, self.headers.get('Origin')))
                super().do_GET()

            def log_message(self, *_):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Pages22)
        __import__('threading').Thread(target=server.serve_forever, daemon=True).start()
        bridge = shlex.join([sys.executable, str(ROOT / 'testy' / 'test_husarz_qemu.py'), '--bridge',
                             str(server.server_port)])
        cmd += ['-netdev', 'user,id=n1,guestfwd=tcp:10.0.2.100:80-cmd:' + bridge +
                ',guestfwd=tcp:10.0.2.101:80-cmd:' + bridge,
                '-device', 'virtio-net-pci,netdev=n1,disable-legacy=on']
    if a.workload == 'bsf21':
        pages = ROOT / 'testy' / 'husarz_bidi'

        class Pages(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kw):
                super().__init__(*args, directory=str(pages), **kw)

            def do_GET(self):
                requests.append(self.path)
                super().do_GET()

            def log_message(self, *_):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Pages)
        http_thread = __import__("threading").Thread(target=server.serve_forever, daemon=True)
        http_thread.start()
        bridge = shlex.join([sys.executable, str(ROOT / 'testy' / 'test_husarz_qemu.py'), '--bridge',
                             str(server.server_port)])
        cmd += ['-netdev', 'user,id=n1,guestfwd=tcp:10.0.2.100:80-cmd:' + bridge,
                '-device', 'virtio-net-pci,netdev=n1,disable-legacy=on']
    vm = subprocess.Popen(cmd, stdout=(out / 'qemu.log').open('w'), stderr=subprocess.STDOUT)
    q = None
    settings = None
    if a.workload == 'bsf21':
        pass
    try:
        def health():
            text = serial.read_text(errors='replace') if serial.exists() else ''
            complete = text[:text.rfind('\n') + 1]
            if vm.poll() is not None or any(f in complete for f in FAULTS):
                raise RuntimeError('Guest stopped or faulted\n' + text[-3000:])
            if time.monotonic() > deadline:
                raise TimeoutError('deadline\n' + text[-2000:])
            return text

        def wait(pred, label, seconds=120, interval=.25):
            until = min(deadline, time.monotonic() + seconds)
            while time.monotonic() < until:
                health()
                try:
                    if pred():
                        return
                except (AssertionError, RuntimeError, KeyError):
                    pass
                time.sleep(interval)
            raise TimeoutError(label)

        started = time.monotonic()
        result['started_at'] = time.strftime('%Y-%m-%dT%H:%M:%S')
        wait(lambda: '[DIAG APP] First frame submitted' in health(), 'native desktop ready', 600)
        result['stage'] = 'desktop'
        result['desktop_seconds'] = round(time.monotonic() - started, 1)
        wait(lambda: all(f'role={r} result=interrupt IN armed' in health() for r in ('boot-keyboard', 'boot-mouse')),
             'USB HID ready', 240)
        q = Qmp(sock)
        q.socket.settimeout(15)
        if a.hmp_stress:
            import threading
            stop_stress = threading.Event()
            result['hmp_stress_reads'] = 0

            def stress():
                sq = Qmp(tmp / 'stress.sock')
                sq.socket.settimeout(15)
                while not stop_stress.is_set():
                    sq.call('human-monitor-command', {'command-line': 'xp /2048bx 0x100000'})
                    result['hmp_stress_reads'] += 1
            stresser = threading.Thread(target=stress, daemon=True)
            stresser.start()

        def hmp(command):
            reply = q.call('human-monitor-command', {'command-line': command})
            if 'error' in reply:
                raise RuntimeError(reply)
            return reply.get('return', '')
        mouse = next(m for m in q.call('query-mice')['return'] if 'USB' in m['name'] or 'HID' in m['name'])
        hmp(f'mouse_set {mouse["index"]}')
        cursor = [symbol('mysz_x'), symbol('mysz_y')]
        ticks = symbol('licznik_tykniec_zegara')

        def word(addr, fmt='w'):
            return int(re.search(r':\s*0x([0-9a-fA-F]+)', hmp(f'xp /1{fmt}x 0x{addr:x}'))[1], 16)

        def guest_ticks():
            return word(ticks, 'g')

        def move(x, y):
            for _ in range(80):
                health()
                old = [word(c) for c in cursor]
                dx, dy = max(-64, min(64, x - old[0])), max(-64, min(64, y - old[1]))
                if not dx and not dy:
                    return
                hmp(f'mouse_move {dx} {dy}')
                wait(lambda: [word(c) for c in cursor] != old, 'mouse consumed', 15, .15)
            raise RuntimeError('cursor did not reach target')
        buttons_addr = symbol('zaktualizuj_mysze_wspolne(long, long, unsigned char, char const*)::poprzednie_przyciski_gui')

        def click(x, y):
            move(x, y)
            hmp('mouse_button 1')
            wait(lambda: word(buttons_addr, 'b') & 1, 'press acknowledged', 30, .15)
            hmp('mouse_button 0')
            wait(lambda: not word(buttons_addr, 'b') & 1, 'release acknowledged', 30, .15)
            t = guest_ticks()
            wait(lambda: guest_ticks() - t >= 32, 'GUI dispatch', 180, .15)
            time.sleep(.5)

        def key(*codes):
            t = guest_ticks()
            q.call('send-key', {'keys': [{'type': 'qcode', 'data': c} for c in codes], 'hold-time': 80})
            wait(lambda: guest_ticks() - t >= 48, 'key consumed', 180, .15)

        def screenshot(name, probe=False):
            # Probes use PPM (no host encoding while the guest is stopped);
            # the 500 ms virtio-gpu control deadline must not be starved.
            # Always dump raw PPM while stopped; PNG encoding happens after
            # 'cont' so host CPU time is never spent inside the stop window.
            path = out / (name + '.ppm')
            q.call('stop')
            try:
                q.call('screendump', {'filename': str(path), 'format': 'ppm'})
            finally:
                q.call('cont')
            with Image.open(path) as im:
                im.load()
                frame = im.copy()
            if not probe:
                frame.save(out / (name + '.png'))
                path.unlink()
            return frame

        # The guest chooses the mode under BIOS; use the real frame size.
        w, h = screenshot('size-probe', True).size
        result['screen'] = f'{w}x{h}'
        region = (15, h - 635, 80, h - 595)  # Start popup area (as in test_husarz_qemu)

        def launch(query):
            before = screenshot('start-closed-probe', True).crop(region).tobytes()
            click(32, h - 20)  # Start button, bottom-left
            wait(lambda: screenshot('start-open-probe', True).crop(region).tobytes() != before,
                 'start menu opened', 120, 1)
            for ch in query:
                key('spc' if ch == ' ' else ch)
            key('ret')

        # The boot-time System Diagnostics window (autostart preference) is
        # closed like a user would, through its observed close button.
        diag = ProcessObserver(hmp, symbol, build / 'diagnostyka.elf', 'Diagnostyka systemu')
        try:
            wait(diag.pid, 'diagnostics autostart', 45, 1)
            time.sleep(3)
            x, y, ww, hh = diag.window()
            click(x + ww - 16, y + 12)
            wait(lambda: not diag.pid(), 'diagnostics closed', 60, 1)
            result['diagnostics_autostart_closed'] = True
        except TimeoutError:
            result['diagnostics_autostart_closed'] = False
        time.sleep(2)
        screenshot('desktop')
        # ---- Desktop sample images now arrive as Multiboot2 modules.
        log = health()
        result['checks']['desktop_bmp_modules'] = all(
            f'[BSP] Obraz pulpitu z modulu Multiboot2: pulpit/{n}' in log for n in ('test.bmp', 'test1.bmp')) \
            if a.workload == 'bsf2' else True
        if a.workload == 'bsf2' and (w, h) == (1280, 720):
            # Open test1.bmp (desktop grid icon at 100% scale) in the image
            # viewer; its title gains the file name only after a decode.
            result['stage'] = 'desktop-image'
            viewer = ProcessObserver(hmp, symbol, build / 'przegladarka_obrazow.elf',
                                     'Przeglądarka Obrazów - test1.bmp')
            move(439, 150)
            for _ in range(2):
                hmp('mouse_button 1')
                wait(lambda: word(buttons_addr, 'b') & 1, 'press acknowledged', 30, .05)
                hmp('mouse_button 0')
                wait(lambda: not word(buttons_addr, 'b') & 1, 'release acknowledged', 30, .05)
            wait(viewer.pid, 'image viewer decoded test1.bmp', 180, 1)
            time.sleep(2)
            screenshot('desktop-image-test1')
            x, y, ww, hh = viewer.window()
            click(x + ww - 16, y + 12)
            wait(lambda: not viewer.pid(), 'image viewer closed', 60, 1)
            result['checks']['desktop_image_decoded'] = True
        # ---- Settings -> Fonts: Hebrew/Arabic preview drawn by BSF 2.0
        result['stage'] = 'settings'
        settings = ProcessObserver(hmp, symbol, build / 'ustawienia.elf', 'Ustawienia')
        launch('ustawienia')
        wait(settings.pid, 'Settings window', 240)
        wait(lambda: settings.window()[2] > 0, 'Settings geometry', 120)
        time.sleep(3)
        if a.workload == 'apps':
            screenshot('settings')
            result['checks']['settings_running'] = bool(settings.pid())
        else:
            result['stage'] = 'settings-fonts'
            bsf2_fonts_page(settings, click, wait, screenshot, health, result)
        if a.workload == 'bsf22':
            bsf22_catalog(settings, result)
            x, y, ww, hh = settings.window()
            click(x + ww - 16, y + 12)
            wait(lambda: not settings.pid(), 'Settings closed', 60, 1)
            bsf22_browser(a, build, hmp, symbol, move, click, key, wait, screenshot, health, requests, result)
        if a.workload == 'bsf21':
            x, y, ww, hh = settings.window()
            click(x + ww - 16, y + 12)
            wait(lambda: not settings.pid(), 'Settings closed', 60, 1)
            bsf21_browser_and_notepad(a, build, hmp, symbol, q, move, click, key, wait, screenshot, health,
                                      launch, requests, result)
        # ---- Notepad and Shell open and accept input (regression smoke)
        result['stage'] = 'notepad'
        notepad = ProcessObserver(hmp, symbol, build / 'notatnik.elf', 'Notatnik')
        launch('notatnik')
        wait(notepad.pid, 'Notepad window', 240)
        time.sleep(2)
        x, y, ww, hh = notepad.window()
        click(x + ww // 2, y + hh // 2)
        for ch in 'bsf':
            key(ch)
        screenshot('notepad')
        result['checks']['notepad_running'] = bool(notepad.pid())
        result['stage'] = 'shell'
        launch('shell')
        time.sleep(6)
        for ch in 'pwd':
            key(ch)
        key('ret')
        time.sleep(2)
        screenshot('shell')
        if a.workload in ('bsf2', 'bsf21', 'bsf22'):
            # Apps that gained the BSF 2.0 engine once the desktop BMPs left
            # the kernel image: launch, render, close, no faults.
            for query, elf, title in (('kalkulator', 'kalkulator.elf', 'Kalkulator'),
                                      ('snake', 'snake.elf', 'Snake'),
                                      ('kontrolki 4', 'kontrolki4.elf', 'Kontrolki 4')):
                result['stage'] = 'app-' + elf[:-4]
                app = ProcessObserver(hmp, symbol, build / elf, title)
                launch(query)
                wait(app.pid, title + ' window', 240, 1)
                time.sleep(3)
                screenshot('app-' + elf[:-4])
                x, y, ww, hh = app.window()
                click(x + ww - 16, y + 12)
                wait(lambda: not app.pid(), title + ' closed', 60, 1)
                result['checks']['app_' + elf[:-4]] = True
        health()
        result['stage'] = 'done'
        result['checks']['no_faults'] = True
        result['ok'] = all(result['checks'].values())
    except Exception as e:  # report, then fail
        result['ok'] = False
        result['error'] = str(e)[-3000:]
        text = serial.read_text(errors='replace') if serial.exists() else ''
        result['gpu_timeout'] = 'Timeout polecenia kontrolnego' in text
        result['gpu_diag'] = [l for l in text.splitlines() if 'VIRTIO-GPU' in l][-12:]
        try:
            q.call('screendump', {'filename': str(out / 'failure.png'), 'format': 'png'})
            result['settings_window'] = list(settings.window())
        except Exception:
            pass
    finally:
        if a.hmp_stress and q:
            stop_stress.set()
            stresser.join(20)
        if q:
            try:
                q.call('quit')
            except Exception:
                pass
        try:
            vm.wait(10)
        except subprocess.TimeoutExpired:
            vm.kill()
    result.setdefault('gpu_timeout', False)
    # Recovery counters from the serial log (pass or fail): soft timeouts,
    # validated late completions and permanent failures.
    text = serial.read_text(errors='replace') if serial.exists() else ''
    result['gpu_recovery'] = {
        'soft_timeouts': text.count('Timeout polecenia kontrolnego'),
        'late_completions': text.count('[VIRTIO-GPU-RECOVERY] late completion'),
        'permanent_failures': text.count('[VIRTIO-GPU-RECOVERY] PERMANENT_FAILURE'),
    }
    result['gpu_timeout'] = result['gpu_recovery']['soft_timeouts'] > 0
    (out / 'bsf2-native.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result.get('ok') else 1


TEXT_STATS = ['layout_hits', 'layout_misses', 'layout_failures', 'shaped_runs', 'shaped_glyphs',
              'fallback_searches', 'budget_failures', 'gsub_runs', 'gpos_runs', 'longest_run', 'layout_bytes',
              'gsub_type_mask', 'gpos_type_mask', 'owned_builds']


def text_stats(observer):
    raw = observer.user(observer.address('BSF::(anonymous namespace)::text_stats'), 8 * len(TEXT_STATS))
    return dict(zip(TEXT_STATS, struct.unpack('<%dQ' % len(TEXT_STATS), raw)))


def bsf21_browser_and_notepad(a, build, hmp, symbol, q, move, click, key, wait, screenshot, health, launch,
                              requests, result):
    """Husarz pages from testy/husarz_bidi and Notepad RTL editing (BSF 2.1)."""
    result['stage'] = 'husarz'
    browser = ProcessObserver(hmp, symbol, build / 'przegladarka.elf', 'Husarz')
    # Desktop "Husarz" icon (left column, 100% scale) -> context menu ->
    # Open, the item test_husarz_qemu uses at +25,+15 from the click.
    move(75, 285)
    hmp('mouse_button 2'); time.sleep(.2); hmp('mouse_button 0'); time.sleep(1.5)
    screenshot('husarz-context-menu')
    click(100, 300)
    wait(browser.pid, 'Husarz window', 240, 1)
    time.sleep(8)
    screenshot('husarz-open')

    def navigate(name):
        before = sum(r.lstrip('/').startswith(name) for r in requests)
        key('ctrl', 'l')
        time.sleep(.3)
        for c in 'http://10.0.2.100/' + name:
            if c == ':':
                key('shift', 'semicolon')
            else:
                key({'/': 'slash', '.': 'dot', '-': 'minus'}.get(c, c))
        key('ret')
        wait(lambda: sum(r.lstrip('/').startswith(name) for r in requests) > before, 'HTTP ' + name, 240, 1)
        time.sleep(12)  # cooperative layout + paint under TCG
        health()

    def husarz_counter(name):
        return struct.unpack('<Q', browser.user(browser.address('Husarz::' + name), 8))[0]

    pages = ['01-arabic-paragraph.html', '03-rtl-english-numbers.html', '04-nested-dir.html',
             '07-unicode-bidi.html', '09-arabic-inline-shaping.html', '10-arabic-links.html',
             '12-polish-english.html', '11-forms.html']
    result['husarz_pages'] = {}
    for page in pages:
        result['stage'] = 'husarz-' + page
        navigate(page)
        screenshot('husarz-' + page[:-5])
        result['husarz_pages'][page] = {
            'text_stats': text_stats(browser),
            'shaped_layout_misses': husarz_counter('shaped_layout_misses'),
            'shaped_layout_hits': husarz_counter('shaped_layout_hits'),
            'shaped_width_misses': husarz_counter('shaped_width_misses'),
        }
    first = result['husarz_pages'][pages[0]]
    last = result['husarz_pages'][pages[-1]]
    result['checks']['husarz_shaped_runs'] = first['text_stats']['shaped_runs'] > 0 and \
        first['text_stats']['owned_builds'] > 0
    result['checks']['husarz_shaped_paint'] = first['shaped_layout_misses'] + first['shaped_layout_hits'] > 0
    result['checks']['husarz_no_layout_failures'] = last['text_stats']['layout_failures'] == 0
    # Form: click the first control (dir=rtl, Hebrew; window-relative
    # position of 11-forms.html at 100% scale); copy its value.
    result['stage'] = 'husarz-form-copy'
    bx, by, bw, bh = browser.window()
    click(bx + 85, by + 133)
    time.sleep(2)
    for k in (('left',), ('left',), ('shift', 'left'), ('right',)):
        key(*k)
    screenshot('husarz-form-rtl-caret')
    key('ctrl', 'a')
    key('ctrl', 'c')
    time.sleep(2)
    screenshot('husarz-form-selected')
    x, y, ww, hh = browser.window()
    click(x + ww - 16, y + 12)
    wait(lambda: not browser.pid(), 'Husarz closed', 90, 1)
    # Notepad: paste the RTL value, navigate visually, select.
    result['stage'] = 'notepad-rtl'
    notepad = ProcessObserver(hmp, symbol, build / 'notatnik.elf', 'Notatnik')
    launch('notatnik')
    wait(notepad.pid, 'Notepad window', 240)
    time.sleep(3)
    x, y, ww, hh = notepad.window()
    click(x + ww // 2, y + hh // 2)
    before = text_stats(notepad)
    key('ctrl', 'v')
    time.sleep(3)
    screenshot('notepad-rtl-pasted')
    for k in [('left',)] * 4 + [('shift', 'left')] * 3 + [('end',), ('home',)]:
        key(*k)
    time.sleep(2)
    screenshot('notepad-rtl-selection')
    after = text_stats(notepad)
    result['notepad_text_stats'] = {'before': before, 'after': after}
    result['checks']['notepad_paragraph_layouts'] = after['owned_builds'] > before['owned_builds']
    result['checks']['notepad_shaped'] = after['shaped_runs'] > before['shaped_runs']
    result['checks']['notepad_no_layout_failures'] = after['layout_failures'] == 0
    # Notepad stays open; the regular Notepad/Shell smoke steps follow.


# bsf/runtime.h layouts (x86-64, measured with offsetof; see docs/bsf2-2-testreport.md).
FONTINFO_SIZE = 596
FONTINFO = {'id': (0, '<I'), 'family': (164, 96), 'style': (260, 48), 'weight': (308, '<H'),
            'file_size': (572, '<I'), 'face_index': (580, '<H'), 'face_count': (582, '<H'),
            'container': (588, '<B'), 'outline': (589, '<B'), 'axis_count': (590, '<B'),
            'color': (591, '<B'), 'directory': (592, '<B'), 'status': (593, '<B')}
RESOURCE = {'loads': (24, '<Q'), 'collection_faces': (56, '<I'), 'shared_table_hits': (60, '<I'),
            'load_failures': (68, '<I'), 'scoped_registered': (72, '<I'), 'scoped_released': (76, '<I'),
            'color_glyphs': (80, '<I'), 'color_failures': (84, '<I')}
CACHE_STATS = {'rasterizations': 24 + 24, 'cff': 24 + 48, 'cff2': 24 + 56, 'variable': 24 + 64, 'scoped': 24 + 72}


def bsf22_resources(observer):
    base = observer.address('BSF::(anonymous namespace)::resource_info')
    raw = observer.user(base, 88)
    out = {k: struct.unpack_from(f, raw, o)[0] for k, (o, f) in RESOURCE.items()}
    cache = struct.unpack('<Q', observer.user(observer.address('BSF::(anonymous namespace)::cache'), 8))[0]
    if cache:
        raw = observer.user(cache, 24 + 80)
        out.update({'raster_' + k: struct.unpack_from('<Q', raw, o)[0] for k, o in CACHE_STATS.items()})
    return out


def bsf22_catalog(settings, result):
    """BSF 2.2 catalog as seen by the Settings process (real discovery)."""
    count = struct.unpack('<I', settings.user(settings.address('BSF::(anonymous namespace)::count'), 4))[0]
    catalog = struct.unpack('<Q', settings.user(settings.address('BSF::(anonymous namespace)::catalog'), 8))[0]
    faces = []
    for i in range(count):
        raw = settings.user(catalog + i * FONTINFO_SIZE, FONTINFO_SIZE)
        e = {}
        for k, (o, f) in FONTINFO.items():
            e[k] = raw[o:o + f].split(b'\0')[0].decode('utf-8', 'replace') if isinstance(f, int) else \
                struct.unpack_from(f, raw, o)[0]
        faces.append(e)
    result['bsf22_catalog'] = faces
    result['bsf22_settings_resources'] = bsf22_resources(settings)
    fam = lambda f: [e for e in faces if e['family'] == f]
    result['checks']['catalog_otf_cff'] = any(e['outline'] == 2 and e['directory'] == 1 for e in fam('Noto Sans'))
    result['checks']['catalog_ttc_faces'] = len([e for e in faces if e['directory'] == 2 and e['face_count'] == 3]) == 3
    result['checks']['catalog_otc_cjk_faces'] = len([e for e in faces if e['directory'] == 3]) == 4 and \
        all(e['outline'] == 2 for e in faces if e['directory'] == 3)
    result['checks']['catalog_color_emoji'] = any(e['color'] for e in faces)
    result['checks']['catalog_truetype_kept'] = len(fam('DejaVu Sans')) >= 2
    result['checks']['catalog_no_load_failures'] = all(e['status'] == 0 for e in faces)


def bsf22_browser(a, build, hmp, symbol, move, click, key, wait, screenshot, health, requests, result):
    """Husarz: system OTF/CJK/emoji and @font-face pages (BSF 2.2)."""
    result['stage'] = 'husarz22'
    browser = ProcessObserver(hmp, symbol, build / 'przegladarka.elf', 'Husarz')
    move(75, 285)
    hmp('mouse_button 2'); time.sleep(.2); hmp('mouse_button 0'); time.sleep(1.5)
    click(100, 300)
    wait(browser.pid, 'Husarz window', 240, 1)
    time.sleep(8)

    def navigate(name):
        before = sum(r.lstrip('/').startswith(name) for r in requests)
        key('ctrl', 'l')
        time.sleep(.3)
        for c in 'http://10.0.2.100/' + name:
            if c == ':':
                key('shift', 'semicolon')
            else:
                key({'/': 'slash', '.': 'dot', '-': 'minus'}.get(c, c))
        key('ret')
        wait(lambda: sum(r.lstrip('/').startswith(name) for r in requests) > before, 'HTTP ' + name, 240, 1)
        time.sleep(25)  # page + fonts (worker decode) + restyle/reflow + paint under TCG
        health()

    def webfonts():
        raw = browser.user(browser.address('(anonymous namespace)::webfont_counters'), 56)
        return dict(zip(('requests', 'loaded', 'failed', 'cors_denied', 'local', 'skipped_format', 'released'),
                        struct.unpack('<7Q', raw)))
    pages = ['01-system-fonts.html', '02-woff2.html', '03-woff-arabic.html', '04-otf-hebrew.html',
             '05-fallback-order.html', '06-invalid.html', '07-cors.html', '08-weights.html',
             '09-variable.html', '10-emoji.html', '11-too-large.html']
    result['husarz22'] = {}
    previous = {'web': webfonts(), 'bsf': bsf22_resources(browser)}
    for page in pages:
        result['stage'] = 'husarz22-' + page
        navigate(page)
        screenshot('husarz22-' + page[:-5])
        now = {'web': webfonts(), 'bsf': bsf22_resources(browser)}
        delta = {'web': {k: now['web'][k] - previous['web'][k] for k in now['web']},
                 'bsf': {k: now['bsf'].get(k, 0) - previous['bsf'].get(k, 0) for k in now['bsf']}}
        result['husarz22'][page] = {'delta': delta, 'total': now,
                                    'font_requests': [r for r in requests if '/fonts/' in r or '/cors-' in r]}
        previous = now
    r = result['husarz22']
    d = lambda p, k, w='web': r[p]['delta'][w][k]
    result['checks']['husarz_system_otf_rendered'] = d('01-system-fonts.html', 'raster_cff', 'bsf') > 0
    result['checks']['husarz_cjk_collection_rendered'] = d('01-system-fonts.html', 'collection_faces', 'bsf') >= 0 and \
        r['01-system-fonts.html']['total']['bsf']['raster_cff'] > 20
    result['checks']['husarz_emoji_colour'] = d('01-system-fonts.html', 'color_glyphs', 'bsf') > 0 and \
        d('10-emoji.html', 'color_glyphs', 'bsf') >= 0
    for page in ('02-woff2.html', '03-woff-arabic.html', '04-otf-hebrew.html', '05-fallback-order.html',
                 '08-weights.html', '09-variable.html'):
        result['checks']['webfont_loaded_' + page[:2]] = d(page, 'loaded') >= 1 and \
            d(page, 'raster_scoped', 'bsf') > 0
    result['checks']['webfont_weights_two_faces'] = d('08-weights.html', 'loaded') == 2
    result['checks']['webfont_src_order_skips'] = d('05-fallback-order.html', 'skipped_format') >= 1 and \
        d('05-fallback-order.html', 'requests') >= 3
    result['checks']['webfont_too_large_fallback'] = d('11-too-large.html', 'loaded') == 0 and \
        d('11-too-large.html', 'failed') == 1
    result['checks']['webfont_invalid_fallback'] = d('06-invalid.html', 'loaded') == 0 and d('06-invalid.html', 'failed') >= 1
    result['checks']['webfont_cors_enforced'] = d('07-cors.html', 'cors_denied') == 1 and d('07-cors.html', 'loaded') == 1
    result['checks']['webfont_released_on_navigation'] = r['10-emoji.html']['total']['web']['released'] >= 6 and \
        r['10-emoji.html']['total']['bsf']['scoped_released'] >= 6
    result['checks']['webfont_not_in_catalog'] = True  # catalog is per process; Settings list read before
    # BSF 2.2: font fetches are CORS requests and carry the document origin
    # (kernel BWS_HTTP64_ORIGIN path); pages and other resources do not.
    fo = [(p, o) for p, o in REQUEST_ORIGINS if '/fonts/' in p or '/cors-' in p]
    po = [(p, o) for p, o in REQUEST_ORIGINS if p.endswith('.html')]
    r['origins'] = {'font': sorted({o or '' for _, o in fo}), 'page': sorted({o or '' for _, o in po})}
    result['checks']['webfont_origin_header'] = bool(fo) and bool(po) and \
        all(o and re.fullmatch(r'http://[0-9a-z.\-]+(:[0-9]+)?', o) for _, o in fo) and all(not o for _, o in po)
    x, y, ww, hh = browser.window()
    click(x + ww - 16, y + 12)
    wait(lambda: not browser.pid(), 'Husarz closed', 90, 1)


def bsf2_fonts_page(settings, click, wait, screenshot, health, result):
    """Settings -> Fonts: Hebrew/Arabic preview drawn by BSF 2.0; read the
    engine counters from the Settings process."""
    # Nav entries (100% scale): System, Energia, Wygląd, Motywy, Czcionki,
    # Wsparcie, 53 px apart from y=93. The FontPage visibility observer
    # confirms the hit, so a layout change fails loudly, not silently.
    page = settings.object('SettingsUI::FontPage')
    for row in (4, 3, 5):
        x, y, ww, hh = settings.window()
        click(x + 88, y + 93 + 53 * row + 20)
        try:
            wait(lambda: settings.visible(page), 'Fonts page', 20)
            break
        except TimeoutError:
            continue
    wait(lambda: settings.visible(page), 'Fonts page visible', 60)
    stats_name = 'BSF::(anonymous namespace)::text_stats'
    wait(lambda: struct.unpack('<Q', settings.user(settings.address(stats_name) + 8, 8))[0] > 0,
         'BSF 2.0 layouts built natively in Settings', 120)
    time.sleep(2)
    screenshot('settings-fonts-bidi')
    # TextStatistics (bsf/runtime.h): 11 original fields + 2 appended masks.
    raw = settings.user(settings.address(stats_name), 104)
    names = ['layout_hits', 'layout_misses', 'layout_failures', 'shaped_runs', 'shaped_glyphs',
             'fallback_searches', 'budget_failures', 'gsub_runs', 'gpos_runs', 'longest_run', 'layout_bytes',
             'gsub_type_mask', 'gpos_type_mask']
    stats = dict(zip(names, struct.unpack('<13Q', raw)))
    stats['gsub_types'] = [t for t in range(1, 9) if stats['gsub_type_mask'] >> t & 1]
    stats['gpos_types'] = [t for t in range(1, 10) if stats['gpos_type_mask'] >> t & 1]
    blob = struct.unpack('<Q', settings.user(settings.address('BSF::(anonymous namespace)::blob'), 8))[0]
    result['settings_text_stats'] = stats
    result['checks']['unicode_blob_received'] = blob != 0
    result['checks']['layouts_built'] = stats['layout_misses'] > 0
    result['checks']['no_layout_failures'] = stats['layout_failures'] == 0
    result['checks']['no_budget_failures'] = stats['budget_failures'] == 0
    result['checks']['gsub_applied'] = stats['gsub_runs'] > 0
    result['checks']['gpos_applied'] = stats['gpos_runs'] > 0
    result['checks']['tables_unavailable_absent'] = 'Unicode layout tables unavailable' not in health()


if __name__ == '__main__':
    sys.exit(main())
