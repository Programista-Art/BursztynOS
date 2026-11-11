#!/usr/bin/env python3
"""Native mouse-pipeline verification on the real Bursztyn OS ISO.

Boots the exact `make runuefi` argv (taken from `make -s runuefi-argv`), with
only host-side substitutions: `-display none` (or --display), serial to a
file, a local QMP socket, `-snapshot` (the virtual disk is never written) and
`-audiodev none`. Pointer input is injected at QEMU's input layer through QMP
`input-send-event`, i.e. the same QemuInputEvent path SDL/GTK feed after host
conversion; the guest cursor is read from the kernel's mysz_x/mysz_y.

What this does NOT exercise: the host frontend (SDL/GTK) turning the host
pointer into QEMU events. That conversion is the root cause of the reversed
movement on X11 hosts with an absolute pointer; see
docs/virtio-gpu-mouse-reliability-2026-11-11.md for the manual procedure.

Checks: QEMU pointer routing (query-mice), relative directions/diagonals/zero/
reversals/edge clamping for every relative device (virtio-mouse, PS/2),
absolute min/mid/max mapping and monotonicity, cursor pixels drawn at the
logical position, Start-menu click, desktop-icon double click, window
title-bar drag and close-button hit test.
Writes <out>/mouse-native.json, serial.log and screenshots.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
sys.path.insert(0, str(ROOT / 'testy'))
from bsf_process_observer import ProcessObserver  # noqa: E402
from test_hda_regression_qemu import Qmp  # noqa: E402
from PIL import Image, ImageChops  # noqa: E402

FAULTS = ('[PF]', '[KERNEL-PANIC]', '[GP]', 'KRYTYCZNY BLAD SYSTEMU', '[USER FAULT]',
          '[VIRTIO-GPU-RECOVERY] PERMANENT_FAILURE')
ABS_MAX = 0x7FFF  # QEMU INPUT_EVENT_ABS_MAX (virtio-tablet / usb-tablet)


def runuefi_argv(root):
    data = json.loads(subprocess.check_output(['make', '-s', 'runuefi-argv'], cwd=root, text=True))
    return data['argv']


def substitute(argv, serial, sock, iso, display, extra_devices, drop_devices, bios):
    # Every option of the audited runuefi argv carries exactly one value.
    out = [argv[0]]
    for i in range(1, len(argv), 2):
        opt, val = argv[i], argv[i + 1]
        if not opt.startswith('-') or val.startswith('-'):
            raise ValueError(f'unexpected runuefi argv shape at {opt} {val}')
        if opt == '-display':
            val = display
        elif opt == '-serial':
            val = 'file:' + str(serial)
        elif opt == '-audiodev':
            val = 'none,id=snd'
        elif opt == '-cdrom':
            val = str(iso)
        elif opt == '-bios' and bios:
            continue  # SeaBIOS (QEMU default) instead of OVMF, same devices
        elif opt == '-device' and val.split(',')[0] in drop_devices:
            continue
        out += [opt, val]
    for dev in extra_devices:
        out += ['-device', dev]
    out += ['-snapshot', '-no-reboot', '-qmp', f'unix:{sock},server=on,wait=off']
    return out


def make_video_fixture(iso, video, tmp, log):
    vw, vh = (int(v) for v in video.split('x'))
    staging = tmp / 'staging'
    subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(iso), '-extract', '/', str(staging)],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
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
    return fixture


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', type=Path, default=ROOT / 'BursztynOS.iso')
    ap.add_argument('--root', type=Path, default=ROOT, help='build tree holding system_operacyjny.bin and ELFs')
    ap.add_argument('--video', help='boot the guest in WxH (bursztyn.video GRUB fixture)')
    ap.add_argument('--bios', action='store_true', help='SeaBIOS instead of OVMF (same devices)')
    ap.add_argument('--display', default='none')
    ap.add_argument('--add-device', action='append', default=[], help='extra -device (A/B only)')
    ap.add_argument('--drop-device', action='append', default=[], help='remove a -device by driver name (A/B only)')
    ap.add_argument('--tag', default='')
    ap.add_argument('--out', type=Path, default=ROOT / '.mouse-test')
    ap.add_argument('--timeout', type=int, default=1500)
    a = ap.parse_args()

    build = a.root.resolve()
    kernel = build / 'system_operacyjny.bin'
    symbols = subprocess.check_output(['nm', '-C', str(kernel)], text=True)

    def symbol(name):
        m = re.search(r'^([0-9a-f]+) \w ' + re.escape(name) + '$', symbols, re.M)
        if not m:
            raise KeyError(name)
        return int(m[1], 16)

    mode = ('bios' if a.bios else 'uefi') + '-' + (a.video or 'default') + (('-' + a.tag) if a.tag else '')
    out = a.out / mode
    out.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix='mouse-qemu-'))
    serial, sock = out / 'serial.log', tmp / 'qmp.sock'
    if serial.exists():
        serial.unlink()
    iso = a.iso.resolve()
    if a.video:
        with (out / 'iso-build.log').open('w') as log:
            iso = make_video_fixture(iso, a.video, tmp, log)
    base = runuefi_argv(ROOT)
    cmd = substitute(base, serial, sock, iso, a.display, a.add_device, set(a.drop_device), a.bios)
    result = {'mode': mode, 'runuefi_argv': base, 'qemu_argv': cmd,
              'iso_sha256': subprocess.check_output(['sha256sum', str(a.iso)], text=True).split()[0],
              'checks': {}, 'measurements': {}, 'stage': 'boot'}
    deadline = time.monotonic() + a.timeout
    vm = subprocess.Popen(cmd, cwd=ROOT, stdout=(out / 'qemu.log').open('w'), stderr=subprocess.STDOUT)
    q = None

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

    try:
        started = time.monotonic()
        wait(lambda: '[DIAG APP] First frame submitted' in health(), 'native desktop ready', 900)
        result['desktop_seconds'] = round(time.monotonic() - started, 1)
        result['stage'] = 'desktop'
        q = Qmp(sock)
        q.socket.settimeout(20)

        def hmp(command):
            reply = q.call('human-monitor-command', {'command-line': command})
            if 'error' in reply:
                raise RuntimeError(reply)
            return reply.get('return', '')

        def word(addr, fmt='w'):
            return int(re.search(r':\s*0x([0-9a-fA-F]+)', hmp(f'xp /1{fmt}x 0x{addr:x}'))[1], 16)

        def s32(addr):
            v = word(addr)
            return v - (1 << 32) if v & 0x80000000 else v

        cursor_addr = (symbol('mysz_x'), symbol('mysz_y'))
        ticks = symbol('licznik_tykniec_zegara')
        buttons_addr = symbol('zaktualizuj_mysze_wspolne(long, long, unsigned char, char const*)::poprzednie_przyciski_gui')

        def pos():
            return s32(cursor_addr[0]), s32(cursor_addr[1])

        def guest_ticks():
            return word(ticks, 'g')

        def screenshot(name, keep=True):
            path = tmp / (name + '.ppm')
            q.call('stop')
            try:
                q.call('screendump', {'filename': str(path), 'format': 'ppm'})
            finally:
                q.call('cont')
            with Image.open(path) as im:
                im.load()
                frame = im.convert('RGB')
            if keep:
                frame.save(out / (name + '.png'))
            path.unlink()
            return frame

        w, h = screenshot('size-probe', False).size
        result['screen'] = f'{w}x{h}'

        mice = q.call('query-mice')['return']
        result['query_mice'] = mice
        current = next((m for m in mice if m.get('current')), None)
        result['qemu_pointer_current'] = current
        has_abs = any(m['absolute'] for m in mice)
        rel_mice = [m for m in mice if not m['absolute']]

        def send(events):
            reply = q.call('input-send-event', {'events': events})
            if 'error' in reply:
                raise RuntimeError(reply)

        def rel(dx, dy, expect_change=True):
            old = pos()
            ev = []
            if dx:
                ev.append({'type': 'rel', 'data': {'axis': 'x', 'value': dx}})
            if dy:
                ev.append({'type': 'rel', 'data': {'axis': 'y', 'value': dy}})
            if not ev:
                ev.append({'type': 'rel', 'data': {'axis': 'x', 'value': 0}})
            send(ev)
            if expect_change:
                wait(lambda: pos() != old, f'rel {dx},{dy} consumed', 30, .05)
            else:
                t = guest_ticks()
                wait(lambda: guest_ticks() - t >= 16, 'settle', 30, .05)
            time.sleep(.05)
            return old, pos()

        def absolute(xr, yr):
            old = pos()
            send([{'type': 'abs', 'data': {'axis': 'x', 'value': xr}},
                  {'type': 'abs', 'data': {'axis': 'y', 'value': yr}}])
            t = guest_ticks()
            wait(lambda: pos() != old or guest_ticks() - t >= 24, f'abs {xr},{yr}', 30, .05)
            time.sleep(.05)
            return pos()

        def abs_for_pixel(px, size):
            # QEMU's own conversion of a frontend pixel (qemu_input_scale_axis)
            return px * ABS_MAX // size

        def move_to(x, y):
            if has_abs:
                absolute(abs_for_pixel(x, w), abs_for_pixel(y, h))
                if pos() == (x, y):
                    return
            for _ in range(80):
                cx, cy = pos()
                dx, dy = max(-100, min(100, x - cx)), max(-100, min(100, y - cy))
                if not dx and not dy:
                    return
                rel(dx, dy)
            raise RuntimeError(f'cursor did not reach {x},{y}: {pos()}')

        def button(down):
            send([{'type': 'btn', 'data': {'down': down, 'button': 'left'}}])
            wait(lambda: bool(word(buttons_addr, 'b') & 1) == down, 'button ' + ('press' if down else 'release'), 30, .05)

        def click(x, y, settle=True):
            move_to(x, y)
            button(True)
            button(False)
            if settle:
                t = guest_ticks()
                wait(lambda: guest_ticks() - t >= 32, 'GUI dispatch', 120, .1)
                time.sleep(.4)

        # Close the autostarted System Diagnostics window like a user.
        diag = ProcessObserver(hmp, symbol, build / 'diagnostyka.elf', 'Diagnostyka systemu')
        try:
            wait(diag.pid, 'diagnostics autostart', 45, 1)
            time.sleep(3)
            x, y, ww, hh = diag.window()
            click(x + ww - 16, y + 12)
            wait(lambda: not diag.pid(), 'diagnostics closed', 60, 1)
            result['checks']['diagnostics_closed_by_click'] = True
        except TimeoutError:
            result['checks']['diagnostics_closed_by_click'] = False
        time.sleep(1)

        # ---- relative pipeline, for every relative device QEMU exposes
        result['stage'] = 'relative'
        rel_results = {}
        for m in rel_mice:
            hmp(f'mouse_set {m["index"]}')
            move_to(w // 2, h // 2)
            cases = []
            for dx, dy, label in ((40, 0, 'right'), (-40, 0, 'left'), (0, 40, 'down'), (0, -40, 'up'),
                                  (30, 30, 'down-right'), (-30, 30, 'down-left'),
                                  (30, -30, 'up-right'), (-30, -30, 'up-left'), (1, 0, 'min+x'),
                                  (-1, 0, 'min-x'), (0, 1, 'min+y'), (0, -1, 'min-y')):
                old, new = rel(dx, dy)
                cases.append({'case': label, 'sent': [dx, dy], 'moved': [new[0] - old[0], new[1] - old[1]]})
            old, new = rel(0, 0, expect_change=False)
            cases.append({'case': 'zero', 'sent': [0, 0], 'moved': [new[0] - old[0], new[1] - old[1]]})
            start = pos()
            for i in range(20):
                rel(7 if i % 2 == 0 else -7, -5 if i % 2 == 0 else 5)
            cases.append({'case': 'reversals-x20', 'sent': [0, 0], 'moved': [pos()[0] - start[0], pos()[1] - start[1]]})
            edges = {}
            # Steps of 100 (QEMU's PS/2 model queues at most a few packets per
            # event and defers the remainder); continue past the edge.
            for name, dx, dy in (('left', -100, 0), ('right', 100, 0), ('top', 0, -100), ('bottom', 0, 100)):
                for _ in range((max(w, h) // 100) + 4):
                    send([{'type': 'rel', 'data': {'axis': 'x' if dx else 'y', 'value': dx or dy}}])
                    t = guest_ticks()
                    wait(lambda: guest_ticks() - t >= 4, 'edge step', 30, .02)
                t = guest_ticks()
                wait(lambda: guest_ticks() - t >= 16, 'edge settle', 30, .05)
                edges[name] = pos()
            ok_dirs = all(c['moved'] == c['sent'] for c in cases if c['case'] not in ('zero', 'reversals-x20')) and \
                cases[-2]['moved'] == [0, 0] and cases[-1]['moved'] == [0, 0]
            ok_edges = edges['left'][0] == 0 and edges['right'][0] == w - 1 and \
                edges['top'][1] == 0 and edges['bottom'][1] == h - 1
            rel_results[m['name']] = {'cases': cases, 'edges': edges, 'directions_ok': ok_dirs, 'edges_ok': ok_edges}
            result['checks'][f'relative_directions[{m["name"]}]'] = ok_dirs
            result['checks'][f'relative_edges[{m["name"]}]'] = ok_edges
        result['measurements']['relative'] = rel_results
        if current:
            hmp(f'mouse_set {current["index"]}')

        # ---- absolute pipeline
        if has_abs:
            result['stage'] = 'absolute'
            points = []
            for xr, yr in ((0, 0), (ABS_MAX, ABS_MAX), (ABS_MAX // 2, ABS_MAX // 2), (0, ABS_MAX), (ABS_MAX, 0)):
                points.append({'raw': [xr, yr], 'guest': list(absolute(xr, yr))})
            sweep = []
            for i in range(0, 11):
                px, py = (w - 1) * i // 10, (h - 1) * i // 10
                g = absolute(abs_for_pixel(px, w), abs_for_pixel(py, h))
                sweep.append({'host_pixel': [px, py], 'guest': list(g), 'error': [g[0] - px, g[1] - py]})
            try:
                result['measurements']['last_abs_seen_by_inputdevice'] = [
                    s32(symbol('(anonymous namespace)::s_last_tablet_raw_x')),
                    s32(symbol('(anonymous namespace)::s_last_tablet_raw_y'))]
            except KeyError:
                pass
            mono = all(sweep[i + 1]['guest'][0] > sweep[i]['guest'][0] and sweep[i + 1]['guest'][1] > sweep[i]['guest'][1]
                       for i in range(len(sweep) - 1))
            err = max(max(abs(s['error'][0]), abs(s['error'][1])) for s in sweep)
            result['measurements']['absolute'] = {'endpoints': points, 'sweep': sweep, 'max_error_px': err}
            result['checks']['absolute_min'] = points[0]['guest'] == [0, 0]
            result['checks']['absolute_max'] = points[1]['guest'] == [w - 1, h - 1]
            result['checks']['absolute_corners'] = points[3]['guest'] == [0, h - 1] and points[4]['guest'] == [w - 1, 0]
            result['checks']['absolute_monotonic'] = mono
            result['checks']['absolute_error_le_1px'] = err <= 1

        # ---- visible cursor is drawn where hit-testing happens
        result['stage'] = 'cursor-pixels'
        tx, ty = w // 3, h // 3
        move_to(tx + 200, ty + 120)
        time.sleep(.8)
        away = screenshot('cursor-away', False)
        move_to(tx, ty)
        time.sleep(.8)
        here = screenshot('cursor-here')
        # Only the neighbourhood of the target: the sprite hotspot is its
        # top-left pixel, so the changed pixels must start at (tx, ty).
        win = (tx - 24, ty - 24, tx + 48, ty + 48)
        box = ImageChops.difference(away.crop(win), here.crop(win)).getbbox()
        box = [box[0] + win[0], box[1] + win[1], box[2] + win[0], box[3] + win[1]] if box else None
        result['measurements']['cursor_diff_bbox'] = box
        result['checks']['cursor_drawn_at_logical_position'] = bool(box) and abs(box[0] - tx) <= 1 and abs(box[1] - ty) <= 1

        # ---- Start menu click
        result['stage'] = 'start-menu'
        region = (15, h - 140, 120, h - 60)  # popup area just above the Start button, any mode
        before = screenshot('start-closed', False).crop(region).tobytes()
        click(32, h - 20)
        wait(lambda: screenshot('start-probe', False).crop(region).tobytes() != before, 'start menu opened', 60, 1)
        screenshot('start-open')
        result['checks']['start_menu_click'] = True
        send([{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': 'esc'}}}])
        send([{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': 'esc'}}}])
        time.sleep(1)
        click(w * 2 // 3, h // 2)  # click on empty desktop closes any popup
        time.sleep(1)

        # ---- double click a desktop icon, drag the window, close it by hit test
        if (w, h) == (1280, 720):
            result['stage'] = 'icon-double-click'
            viewer = ProcessObserver(hmp, symbol, build / 'przegladarka_obrazow.elf', 'Przeglądarka Obrazów - test1.bmp')
            move_to(439, 150)
            for _ in range(2):
                button(True)
                button(False)
            wait(viewer.pid, 'image viewer opened by double click', 180, 1)
            time.sleep(2)
            result['checks']['icon_double_click'] = True
            x0, y0, ww, hh = viewer.window()
            result['stage'] = 'drag'
            grab = (x0 + 60, y0 + 12)
            move_to(*grab)
            button(True)
            ddx, ddy = 150, 90
            steps = 6
            for i in range(1, steps + 1):
                move_to(grab[0] + ddx * i // steps, grab[1] + ddy * i // steps)
            button(False)
            t = guest_ticks()
            wait(lambda: guest_ticks() - t >= 32, 'drag settle', 60, .1)
            time.sleep(1)
            x1, y1, _, _ = viewer.window()
            screenshot('after-drag')
            result['measurements']['drag'] = {'before': [x0, y0], 'after': [x1, y1], 'requested': [ddx, ddy]}
            result['checks']['window_drag_follows_cursor'] = (x1 - x0, y1 - y0) == (ddx, ddy)
            result['stage'] = 'close-button'
            click(x1 + ww - 16, y1 + 12)
            wait(lambda: not viewer.pid(), 'viewer closed by close-button click', 60, 1)
            result['checks']['close_button_hit_test'] = True
        health()
        result['stage'] = 'done'
        result['checks']['no_faults'] = True
        result['ok'] = all(result['checks'].values())
    except Exception as e:  # report, then fail
        result['ok'] = False
        result['error'] = str(e)[-3000:]
        try:
            q.call('screendump', {'filename': str(out / 'failure.png'), 'format': 'png'})
        except Exception:
            pass
    finally:
        if q:
            try:
                q.call('quit')
            except Exception:
                pass
        try:
            vm.wait(10)
        except subprocess.TimeoutExpired:
            vm.kill()
    text = serial.read_text(errors='replace') if serial.exists() else ''
    result['gpu_recovery'] = {
        'soft_timeouts': text.count('Timeout polecenia kontrolnego'),
        'late_completions': text.count('[VIRTIO-GPU-RECOVERY] late completion'),
        'permanent_failures': text.count('[VIRTIO-GPU-RECOVERY] PERMANENT_FAILURE'),
    }
    result['input_owner_log'] = [l for l in text.splitlines() if '[INPUT OWNER]' in l or '[INPUT REGISTER]' in l][-12:]
    (out / 'mouse-native.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result.get(k) for k in ('mode', 'ok', 'screen', 'checks', 'error', 'gpu_recovery',
                                                 'qemu_pointer_current')}, indent=2))
    sys.exit(0 if result['ok'] else 1)


if __name__ == '__main__':
    main()
