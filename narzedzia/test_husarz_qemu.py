#!/usr/bin/env python3
"""Interactive native Husarz regression over real HTTP, USB input and BWS.

Boots the built ISO read-only, without guest data disks. An isolated loopback
HTTP server supplies fixtures through QEMU's guestfwd virtual HTTP endpoint. No browser
or kernel test entry points are installed. Screenshots observe native controls.
"""
import argparse
import hashlib
import http.server
import json
import os
import selectors
import shlex
import socket
import sys
import shutil
import tempfile
import re
import subprocess
import threading
import time
from pathlib import Path
from PIL import Image
from test_hda_regression_qemu import Qmp

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'narzedzia/fixtures/husarz'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, default=ROOT / 'BursztynOS.iso')
    parser.add_argument('--output', type=Path, default=ROOT / '.husarz-test/qemu')
    parser.add_argument('--timeout', type=float, default=600)
    parser.add_argument('--fault-vector', type=int, choices=[6,14], default=6)
    parser.add_argument('--fault-probe', type=Path, help='Private test kernel with a deliberately faulting Ring 3 browser payload')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'result.json').unlink(missing_ok=True)
    requests = []
    requests_lock = threading.Lock()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            name = self.path.split('?', 1)[0].lstrip('/')
            if '/' in name or not (FIXTURES / name).is_file():
                self.send_error(404)
                return
            data = (FIXTURES / name).read_bytes()
            with requests_lock:
                requests.append(self.path)
            if name == 'scroll.html':
                time.sleep(.3)  # Expose the ordinary loading redraws to observation.
            self.send_response(200)
            self.send_header('Content-Type', 'text/css' if name.endswith('.css') else 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *_):
            pass

    # BWS HTTP retains port 80; guestfwd connects each request to an unprivileged loopback port.
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    bridge_command = shlex.join([sys.executable, str(Path(__file__).resolve()), '--bridge', str(server.server_port)])
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    sock = Path('/tmp') / f'husarz-{time.time_ns()}.sock'
    serial = out / 'serial.log'
    serial.unlink(missing_ok=True)
    temporary_iso = None
    kernel = args.fault_probe.resolve() if args.fault_probe else ROOT / 'system_operacyjny.bin'
    if args.fault_probe:
        temporary_iso = tempfile.TemporaryDirectory(prefix='husarz-user-fault-')
        staging = Path(temporary_iso.name) / 'staging'
        boot = staging / 'boot'
        (boot / 'grub').mkdir(parents=True)
        shutil.copy2(kernel, boot / 'system_operacyjny.bin')
        (boot/'grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "User fault isolation" {\ninsmod all_video\nset gfxmode=1280x720x32,auto\nset gfxpayload=keep\nmultiboot2 /boot/system_operacyjny.bin\nboot\n}\n')
        args.iso = Path(temporary_iso.name)/'probe.iso'
        with (out/'iso-build.log').open('w') as iso_log:
            subprocess.run(['grub-mkrescue','-o',str(args.iso),str(staging)],stdout=iso_log,stderr=subprocess.STDOUT,check=True,timeout=45)
    symbols = subprocess.check_output(['nm', '-C', str(kernel)], text=True)

    def symbol(name):
        m = re.search(r'^([0-9a-f]+) [bBdD] ' + re.escape(name) + r'$', symbols, re.M)
        if not m:
            raise RuntimeError('Missing cursor observer ' + name)
        return int(m[1], 16)

    cursor_addresses = [symbol('mysz_x'), symbol('mysz_y')]
    kernel_sha=hashlib.sha256(kernel.read_bytes()).hexdigest()
    iso_sha=hashlib.sha256(args.iso.read_bytes()).hexdigest()
    cmd = ['qemu-system-x86_64', '-machine', 'pc,hpet=on,i8042=off', '-accel', 'tcg', '-cpu', 'max',
           '-smp', '1', '-m', '2048', '-bios', '/usr/share/ovmf/OVMF.fd', '-display', 'none',
           '-serial', 'file:' + str(serial), '-no-reboot', '-qmp', f'unix:{sock},server=on,wait=off',
           '-vga', 'none', '-device', 'virtio-vga,disable-legacy=on,xres=1280,yres=720',
           '-drive', f'file={args.iso.resolve()},id=boot,if=none,format=raw,readonly=on',
           '-device', 'virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1',
           '-device', 'qemu-xhci,id=usb', '-device', 'usb-kbd,bus=usb.0', '-device', 'usb-mouse,bus=usb.0',
           '-audiodev', 'none,id=snd', '-device', 'intel-hda', '-device', 'hda-output,audiodev=snd',
           '-netdev', 'user,id=n1,guestfwd=tcp:10.0.2.100:80-cmd:' + bridge_command, '-device', 'virtio-net-pci,netdev=n1,disable-legacy=on']
    q = None
    vm = None
    deadline = time.monotonic() + args.timeout
    frame = {'x': 100, 'y': 100, 'w': 800, 'h': 550}
    result = {}
    with (out / 'qemu.log').open('w') as log:
        try:
            vm = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)

            def health():
                text = serial.read_text(errors='replace') if serial.exists() else ''
                if vm.poll() is not None or any(s in text for s in (
                        '[PF]', '[KERNEL-PANIC]', '[GP]', 'KRYTYCZNY BLAD SYSTEMU',
                        '[VIRTIO-GPU] BLAD KRYTYCZNY')):
                    raise RuntimeError('Guest stopped or faulted\n' + text[-2000:])
                if not args.fault_probe and '[USER FAULT]' in text:
                    raise RuntimeError('Browser process faulted\n'+text[-2000:])
                if time.monotonic() >= deadline:
                    raise TimeoutError('Husarz regression deadline\n' + text[-2000:])
                return text

            def wait(predicate, label, seconds=90):
                until = min(deadline, time.monotonic() + seconds)
                while time.monotonic() < until:
                    health()
                    if predicate():
                        return
                    time.sleep(.15)
                raise TimeoutError(label)

            wait(lambda: '[DIAG APP] First frame submitted' in health(), 'native desktop ready', args.timeout)
            wait(lambda: all('role=' + role + ' result=interrupt IN armed' in health()
                             for role in ('boot-keyboard', 'boot-mouse')),
                 'USB keyboard and mouse interrupt endpoints ready')
            q = Qmp(sock)

            def hmp(command):
                reply = q.call('human-monitor-command', {'command-line': command})
                if 'error' in reply:
                    raise RuntimeError(reply)
                return reply.get('return', '')

            mice = q.call('query-mice')['return']
            mouse = next(m for m in mice if 'USB' in m['name'] or 'HID' in m['name'])
            hmp(f'mouse_set {mouse["index"]}')

            def position():
                values = []
                for address in cursor_addresses:
                    match = re.search(r':\s*0x([0-9a-fA-F]+)', hmp(f'xp /1wx 0x{address:x}'))
                    if not match:
                        raise RuntimeError('Cannot read cursor observer')
                    values.append(int(match[1], 16))
                return values

            def move(x, y):
                for _ in range(60):
                    health()
                    old = position()
                    dx, dy = max(-64, min(64, x-old[0])), max(-64, min(64, y-old[1]))
                    if not dx and not dy:
                        return
                    hmp(f'mouse_move {dx} {dy}')
                    wait(lambda: position() != old, 'guest consumed mouse movement', 10)
                raise RuntimeError('Cursor did not reach target')

            def click(x, y, double=False):
                move(x, y)
                for _ in range(2 if double else 1):
                    hmp('mouse_button 1')
                    time.sleep(.15)
                    hmp('mouse_button 0')
                    time.sleep(.15)
                time.sleep(.5)

            def key(*codes):
                q.call('send-key', {'keys': [{'type': 'qcode', 'data': c} for c in codes], 'hold-time': 80})
                time.sleep(.16)

            def screenshot(name):
                if not name.endswith('probe') and name not in ('browser-opening', 'notepad-after-fault'):
                    print('Screenshot:', name, flush=True)
                path = out / (name + '.png')
                q.call('screendump', {'filename': str(path), 'format': 'png'})
                with Image.open(path) as im:
                    return im.convert('RGB').copy()

            def count(path):
                with requests_lock:
                    return requests.count('/' + path)

            def navigate(name):
                print('Navigate:', name, flush=True)
                previous = count(name)
                key('ctrl', 'l')
                time.sleep(.15)
                for c in 'http://10.0.2.100/' + name:
                    if c == ':':
                        key('shift', 'semicolon')
                    else:
                        key({'/': 'slash', '.': 'dot', '-': 'minus', '_': 'minus'}.get(c, c))
                key('ret')
                wait(lambda: count(name) > previous, 'HTTP request ' + name)
                time.sleep(2)
                health()

            def thumb(image):
                x = frame['x'] + frame['w'] - 9
                ys = []
                for y in range(frame['y'] + 136, frame['y'] + frame['h'] - 44):
                    r, g, b = image.getpixel((x, y))
                    if r > 110 and r > g * 1.05 and g > b * 1.4:
                        ys.append(y)
                if not ys:
                    raise AssertionError('No native scrollbar thumb at ' + str(x))
                return min(ys), max(ys)

            def page_hash(image):
                region = image.crop((frame['x']+12, frame['y']+136, frame['x']+frame['w']-30,
                                     frame['y']+frame['h']-30))
                return hashlib.sha256(region.tobytes()).hexdigest()

            time.sleep(2)
            click(1124, 44)  # Close the default diagnostic utility through its normal titlebar.
            click(320, 28, double=True)
            if not args.fault_probe:
                def browser_ready():
                    im = screenshot('browser-opening')
                    r, g, b = im.getpixel((883, 114))
                    return r > 150 and g < 130 and b < 130
                wait(browser_ready, 'first complete native browser frame')
            screenshot('browser-open')
            if args.fault_probe:
                wait(lambda: '[USER FAULT] process terminated:' in health(), 'Ring 3 exception isolated')
                text = health()
                if f'vector={args.fault_vector} ' not in text:
                    raise AssertionError('Unexpected exception vector')
                if 'program=/programy/przegladarka.cebula/przegladarka.bur' not in text:
                    raise AssertionError('Wrong process faulted')
                time.sleep(1)
                move(80,28)
                desktop=screenshot('fault-isolated-desktop')
                click(80,28,double=True)
                def notepad_ready():
                    im=screenshot('notepad-after-fault')
                    a=desktop.crop((100,100,650,450));b=im.crop((100,100,650,450))
                    return sum(x!=y for x,y in zip(a.getdata(),b.getdata()))>=100000
                wait(notepad_ready, 'native app first frame after user fault')
                key('a')
                time.sleep(.5)
                screenshot('notepad-input-after-fault')
                health()
                result={'result':'PASS','fault_isolated':True,'native_app_after_fault':True,
                        'kernel_sha256':kernel_sha,'fault_vector':args.fault_vector}
                (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                print(json.dumps(result),flush=True)
                return
            navigate('scroll.html')
            wait(lambda: count('site.css') > 0, 'external stylesheet')
            move(400, 300)
            time.sleep(1)
            def loaded_thumb():
                try:
                    return thumb(screenshot('loading-probe'))
                except AssertionError:
                    return False
            wait(loaded_thumb, 'document published and scrollbar painted')
            initial = screenshot('page-loaded')
            initial_thumb = thumb(initial)
            bar_y = frame['y'] + frame['h'] - 24
            column = frame['x'] + 400
            signature = [initial.getpixel((column, y)) for y in range(bar_y, bar_y+24)]
            refresh_frames = 0
            for refresh in range(3):
                previous = count('scroll.html')
                key('f5')
                for sample in range(8):
                    im = screenshot(f'refresh-{refresh}-{sample}')
                    matches = [y for y in range(frame['y']+116, bar_y+1)
                               if all(im.getpixel((column, y+i)) == signature[i] for i in range(24))]
                    if matches != [bar_y]:
                        raise AssertionError('Status panel moved or duplicated: ' + str(matches))
                    refresh_frames += 1
                    time.sleep(.15)
                wait(lambda: count('scroll.html') > previous, 'refresh HTTP')
                time.sleep(1)
            result['refresh_frames_anchored'] = refresh_frames
            def wheel_steps(button, steps, direction):
                # Wait for each native repaint before sending another detent.
                # TCG can take longer than a fixed delay to drain USB input and
                # paint a page; queued down events must not overlap the up test.
                for _ in range(steps):
                    previous = thumb(screenshot('wheel-probe'))[0]
                    q.call('input-send-event', {'events': [
                        {'type': 'btn', 'data': {'button': button, 'down': True}},
                        {'type': 'btn', 'data': {'button': button, 'down': False}}]})
                    wait(lambda: direction * (thumb(screenshot('wheel-probe'))[0] - previous) > 0,
                         'native repaint after ' + button, 20)

            before = screenshot('wheel-before')
            wheel_steps('wheel-down', 8, 1)
            down = screenshot('wheel-down')
            if thumb(down)[0] <= thumb(before)[0] or page_hash(down) == page_hash(before):
                raise AssertionError('Wheel/page/thumb are not synchronized')
            wheel_steps('wheel-up', 3, -1)
            up = screenshot('wheel-up')
            if thumb(up)[0] >= thumb(down)[0]:
                raise AssertionError('Wheel up direction')
            sx = frame['x']+frame['w']-9
            sy = sum(thumb(up))//2
            move(sx, sy)
            hmp('mouse_button 1')
            time.sleep(.2)
            move(sx, frame['y']+frame['h']-55)
            time.sleep(.3)
            hmp('mouse_button 0')
            wait(lambda: thumb(screenshot('drag-probe'))[0] > thumb(up)[0],
                 'native thumb drag repaint', 20)
            dragged = screenshot('thumb-dragged')
            if thumb(dragged)[0] <= thumb(up)[0] or page_hash(dragged) == page_hash(up):
                raise AssertionError('Scrollbar dragging did not move the document')
            click(sx, frame['y']+155)
            wait(lambda: thumb(screenshot('track-probe'))[0] < thumb(dragged)[0],
                 'native track click repaint', 20)
            track = screenshot('track-click')
            if thumb(track)[0] >= thumb(dragged)[0]:
                raise AssertionError('Scrollbar track click')
            click(400, 300)
            key('home')
            wait(lambda: thumb(screenshot('home-probe'))[0] == initial_thumb[0],
                 'Home resets the canonical offset', 20)
            if thumb(screenshot('home'))[0] != initial_thumb[0]:
                raise AssertionError('Home did not reset the canonical offset')
            key('end')
            wait(lambda: thumb(screenshot('end-probe'))[0] > initial_thumb[0],
                 'End scrolls to the bottom', 20)
            end = thumb(screenshot('end'))
            key('pgup')
            wait(lambda: thumb(screenshot('page-up-probe'))[0] < end[0],
                 'Page Up repaint', 20)
            if thumb(screenshot('page-up'))[0] >= end[0]:
                raise AssertionError('Page Up')
            key('pgdn')
            wait(lambda: thumb(screenshot('page-down-probe'))[0] == end[0],
                 'Page Down returns to the bottom', 20)
            screenshot('page-down')
            result['scrolling'] = ['wheel down/up', 'thumb drag', 'track click', 'Home/End', 'Page Up/Down']
            with requests_lock:
                network_before_resize = len(requests)
            # Native BursztynUI supports maximize/restore, but has no edge-drag
            # resize handler. Exercise both actual viewport-width changes.
            normal_frame = dict(frame)
            click(frame['x']+frame['w']-40, frame['y']+14)
            frame.update(x=0, y=0, w=1280, h=680)
            wait(loaded_thumb, 'maximized page reflow and native scrollbar')
            resized = screenshot('resized')
            thumb(resized)
            click(frame['x']+frame['w']-40, frame['y']+14)
            frame.update(normal_frame)
            wait(loaded_thumb, 'restored page reflow and native scrollbar')
            screenshot('restored')
            with requests_lock:
                if len(requests) != network_before_resize:
                    raise AssertionError('Resize fetched the document again')
            result['resize_without_network_fetch'] = True
            result['resize_actions'] = ['native maximize', 'native restore']
            navigate('malformed.html')
            screenshot('malformed')
            previous = count('scroll.html')
            key('alt', 'left')
            wait(lambda: count('scroll.html') > previous, 'Back navigation')
            time.sleep(2)
            screenshot('back')
            previous = count('malformed.html')
            key('alt', 'right')
            wait(lambda: count('malformed.html') > previous, 'Forward navigation')
            time.sleep(2)
            screenshot('forward')
            result['back_forward'] = True
            for name in ['box.html', 'percent.html', 'tables.html', 'radius.html', 'wrapping.html']:
                navigate(name)
                screenshot(name.replace('.html', ''))
            before_theme = screenshot('theme-before')
            click(32, 700)
            click(130, 532)  # Native Start entry: Motywy systemowe.
            time.sleep(2)
            screenshot('themes-open')
            click(210, 350)  # Bursztyn Jasny, fifth row in the native theme list.
            click(330, 466)  # Apply, in the private Live session only.
            wait(lambda: min(screenshot('theme-probe').getpixel((110, 550))) > 160,
                 'light theme applied to the native window', 30)
            screenshot('theme-applied')
            click(784, 114)
            region = (frame['x']+10, frame['y']+30, frame['x']+frame['w']-20, frame['y']+45)
            wait(lambda: before_theme.crop(region).tobytes() !=
                 screenshot('browser-theme-probe').crop(region).tobytes(),
                 'browser chrome updated after theme change', 30)
            after_theme = screenshot('browser-themed')
            if before_theme.crop(region).tobytes() == after_theme.crop(region).tobytes():
                raise AssertionError('Browser chrome did not update after the theme change')
            result['live_theme'] = True
            health()
            with requests_lock:
                if any('must-not-run' in p for p in requests):
                    raise AssertionError('Script URL was requested without an engine')
            result.update(result='PASS', kernel_sha256=kernel_sha,
                          iso_sha256=iso_sha, http_requests=list(requests),
                          external_css=True, malformed_page=True, no_guest_fault=True)
            (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
            print(json.dumps(result), flush=True)
        except BaseException:
            if q:
                dump=[]
                for _ in range(12):
                    dump.append(hmp('info registers'))
                    dump.append(hmp('x /24gx $rsp'))
                    time.sleep(.05)
                (out/'guest-state.txt').write_text('\n'.join(dump))
                q.call('screendump', {'filename': str(out/'failure.png'), 'format': 'png'})
            raise
        finally:
            (out/'http-requests.json').write_text(json.dumps(requests, indent=2)+'\n')
            if q:
                q.close()
            if vm:
                vm.terminate()
                try:
                    vm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    vm.kill()
                    vm.wait(timeout=5)
            sock.unlink(missing_ok=True)
            server.shutdown()
            server.server_close()
            if temporary_iso:
                temporary_iso.cleanup()


def bridge(port):
    # QEMU guestfwd spawns one pipe-backed process for each TCP connection.
    # This relay is test-only and never runs in Bursztyn OS.
    with socket.create_connection(('127.0.0.1', port), timeout=10) as peer:
        peer.settimeout(None)
        selector = selectors.DefaultSelector()
        selector.register(0, selectors.EVENT_READ)
        selector.register(peer, selectors.EVENT_READ)
        while True:
            ready = selector.select(timeout=15)
            if not ready:
                break
            for item, _ in ready:
                if item.fileobj == 0:
                    data = os.read(0, 65536)
                    if not data:
                        return
                    peer.sendall(data)
                else:
                    data = peer.recv(65536)
                    if not data:
                        return
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--bridge':
        bridge(int(sys.argv[2]))
    else:
        main()
