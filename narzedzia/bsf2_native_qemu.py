#!/usr/bin/env python3
"""BSF 2.0 native verification on the real Bursztyn OS ISO under QEMU.

Boots the production ISO (UEFI or BIOS), waits for the native desktop,
opens Settings -> Fonts (whose preview contains Hebrew + Arabic samples),
and reads the BSF 2.0 counters straight from the Settings Ring 3 process
memory through the read-only ProcessObserver (QMP physical reads, no guest
writes). Also launches Notepad and the Shell, and checks the serial log for
kernel/user faults. Results: <out>/bsf2-native.json plus PNG screenshots.
"""
import argparse, json, re, struct, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
sys.path.insert(0, str(ROOT / 'testy'))
from bsf_process_observer import ProcessObserver  # noqa: E402
from test_hda_regression_qemu import Qmp  # noqa: E402
from PIL import Image  # noqa: E402

FAULTS = ('[PF]', '[KERNEL-PANIC]', '[GP]', 'KRYTYCZNY BLAD SYSTEMU',
          '[VIRTIO-GPU] BLAD KRYTYCZNY', '[USER FAULT]')


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
    ap.add_argument('--workload', choices=('bsf2', 'apps'), default='bsf2',
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
    vm = subprocess.Popen(cmd, stdout=(out / 'qemu.log').open('w'), stderr=subprocess.STDOUT)
    q = None
    settings = None
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
        if a.workload == 'bsf2':
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
    (out / 'bsf2-native.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result.get('ok') else 1


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
