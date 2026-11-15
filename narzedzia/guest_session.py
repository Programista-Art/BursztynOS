#!/usr/bin/env python3
"""Long-lived native QEMU session controlled over a localhost TCP socket.

    guest_session.py --iso X.iso --out DIR --pages DIR --port 5555 &
    guest_ctl.py --port 5555 "click 100 200"

One command per connection, one reply line (JSON). Commands:
  ping | click X Y | dclick X Y | rclick X Y | move X Y | drag X0 Y0 X1 Y1 [steps]
  down X Y | up (przycisk przytrzymany: zrzuty w trakcie przeciagania) | wheel DZ | key K1 [K2..] | type TEXT | shot NAME | launch QUERY | wait SECONDS
  peek SYMBOL [OFF..] | ctrlwheel N | perf (aggregated [GUI PERF] since last perf call) | serial N (last N lines)
  grep PATTERN (count/last match in serial) | cursor | quit
Scenarios script their own flow from outside, reading screenshots in between.
"""
import argparse, json, re, socketserver, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
from desktop_qemu_lib import Guest  # noqa: E402

G = None
perf_offset = 0


def perf_summary():
    """Aggregate [GUI PERF] lines since the previous call."""
    global perf_offset
    text = G.serial_path.read_text(errors='replace')
    chunk = text[perf_offset:]
    perf_offset = len(text)
    agg = {}
    for line in chunk.splitlines():
        if '[GUI PERF]' not in line:
            continue
        f = dict(re.findall(r'(\w+)=(\S+)', line))
        pid = f.get('pid', '?')
        a = agg.setdefault(pid, {'path': f.get('path', ''), 'n': 0})
        a['n'] += 1
        if f.get('path'):
            a['path'] = f['path']
        for k, v in f.items():
            if not v.isdigit() or k in ('pid', 'interval_ns'):
                continue
            if k.endswith('_max_ns'):
                a[k] = max(a.get(k, 0), int(v))
            else:
                a[k] = a.get(k, 0) + int(v)
    return {pid: v for pid, v in agg.items()}


def handle(cmd):
    p = cmd.split(' ', 1)
    op, arg = p[0], (p[1] if len(p) > 1 else '')
    a = arg.split()
    if op == 'ping':
        return {'ok': True, 'w': G.w, 'h': G.h}
    if op == 'click':
        G.click(int(a[0]), int(a[1]))
    elif op == 'dclick':
        G.move(int(a[0]), int(a[1]))
        for _ in range(2):
            G.button(True)
            G.button(False)
        t = G.ticks()
        G.wait(lambda: G.ticks() - t >= 32, 'GUI dispatch', 180, .15)
        time.sleep(.3)
    elif op == 'rclick':
        G.move(int(a[0]), int(a[1]))
        if G.tablet:          # HMP mouse_button nie steruje tabletem: prawy przycisk przez QMP
            for down in (True, False):
                G.q.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'right', 'down': down}}]})
                time.sleep(.2)
        else:
            G.hmp('mouse_button 4')
            time.sleep(.2)
            G.hmp('mouse_button 0')
        time.sleep(.5)
    elif op == 'move':
        G.move(int(a[0]), int(a[1]))
    elif op == 'down':
        G.move(int(a[0]), int(a[1]))
        G.button(True)
    elif op == 'up':
        G.button(False)
    elif op == 'drag':
        G.drag(int(a[0]), int(a[1]), int(a[2]), int(a[3]), int(a[4]) if len(a) > 4 else 8)
    elif op == 'wheel':
        n = int(a[0])
        btn = 'wheel-down' if n > 0 else 'wheel-up'
        t0 = time.monotonic()
        ev = []
        for _ in range(abs(n)):
            ev += [{'type': 'btn', 'data': {'button': btn, 'down': True}},
                   {'type': 'btn', 'data': {'button': btn, 'down': False}}]
        G.q.call('input-send-event', {'events': ev})
        return {'ok': True, 'wheel_seconds': round(time.monotonic() - t0, 2)}
    elif op == 'stress':
        # Rapid wheel batches interleaved with pointer motion: motion forces
        # compositions of the window while the app is mid-draw.
        end = time.monotonic() + float(a[0])
        sign = 1
        sent = 0
        while time.monotonic() < end:
            btn = 'wheel-down' if sign > 0 else 'wheel-up'
            ev = []
            for _ in range(3):
                ev += [{'type': 'btn', 'data': {'button': btn, 'down': True}},
                       {'type': 'btn', 'data': {'button': btn, 'down': False}}]
            G.q.call('input-send-event', {'events': ev})
            for d in (3, -3, 3, -3):
                G.hmp('mouse_move %d 0' % d)
            sent += 1
            if sent % 4 == 0:
                sign = -sign
        return {'ok': True, 'batches': sent}
    elif op == 'closeapp':
        elf, title = a[0], arg.split(' ', 1)[1]
        obs = G.observer(elf, title)
        x, y, w, h = obs.window()
        G.click(x + w - 16, y + 12)
        G.wait(lambda: not obs.pid(), 'app closed', 60, 1)
    elif op == 'bench':
        # bench ELF TITLE QUERY: launch via start menu, host-side wall time until the
        # kernel logs the first published frame for the new process; reports guest lines.
        elf, title, query = a[0], a[1].replace('_', ' '), a[2]
        before = len(G.serial_path.read_text(errors='replace').splitlines())
        t0 = time.monotonic()
        G.launch(query)
        t_enter = time.monotonic()
        def first_frame():
            lines = G.serial_path.read_text(errors='replace').splitlines()[before:]
            return [l for l in lines if '[APP LAUNCH]' in l]
        G.wait(lambda: len(first_frame()) > 0, 'first frame', 120, .1)
        t1 = time.monotonic()
        lines = G.serial_path.read_text(errors='replace').splitlines()[before:]
        return {'ok': True, 'enter_to_first_frame_s': round(t1 - t_enter, 2),
                'guest': [l for l in lines if any(k in l for k in ('LOADER PERF', 'APP START', 'APP LAUNCH'))]}
    elif op == 'winrect':
        elf, title = a[0], arg.split(' ', 1)[1]
        return {'ok': True, 'rect': list(G.observer(elf, title).window())}
    elif op == 'resizetest':
        # Real mouse edge/corner drags on a launched app: grow (corner), grow (left edge),
        # shrink below the minimum (clamped), move partly off-screen; reports rectangles.
        elf, title = a[0], arg.split(' ', 1)[1]
        obs = G.observer(elf, title)
        out = {'start': list(obs.window())}
        x, y, w, h = out['start']
        G.drag(x + w - 5, y + h - 5, x + w + 115, y + h + 35, 6)
        time.sleep(1.5)
        out['grow_corner'] = list(obs.window())
        x, y, w, h = out['grow_corner']
        G.drag(x + 2, y + h // 2, x - 58, y + h // 2, 6)
        time.sleep(1.5)
        out['grow_left'] = list(obs.window())
        x, y, w, h = out['grow_left']
        G.drag(x + w - 5, y + h - 5, x + 20, y + 20, 8)
        time.sleep(1.5)
        out['shrink_min'] = list(obs.window())
        x, y, w, h = out['shrink_min']
        G.drag(x + w // 2, y + 12, min(G.w - 2, x + w // 2 + 900), y + 12, 8)
        time.sleep(1.5)
        out['moved_right'] = list(obs.window())
        G.screenshot('resize-' + elf.split('.')[0])
        out['ok'] = True
        return out
    elif op == 'peek':
        # peek SYMBOL [OFFSET...]: odczyt 32-bitowych slow z pamieci gościa (tylko do odczytu; diagnostyka)
        base = int(a[0], 16) if a[0].startswith('0x') else G.symbol(a[0])
        return {'ok': True, 'addr': hex(base), 'words': [G.word(base + int(o), 'w') for o in (a[1:] or ['0'])]}
    elif op == 'ctrlwheel':
        # ctrlwheel N: Ctrl + kolko myszy (N>0 w dol, N<0 w gore)
        n = int(a[0]); btn = 'wheel-down' if n > 0 else 'wheel-up'
        ev = [{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': 'ctrl'}}}]
        for _ in range(abs(n)):
            ev += [{'type': 'btn', 'data': {'button': btn, 'down': True}}, {'type': 'btn', 'data': {'button': btn, 'down': False}}]
        ev += [{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': 'ctrl'}}}]
        G.q.call('input-send-event', {'events': ev})
        time.sleep(.5)
    elif op == 'key':
        G.key(*a)
    elif op == 'type':
        G.type_text(arg)
    elif op == 'shot':
        G.screenshot(arg.strip())
    elif op == 'launch':
        G.launch(arg.strip())
    elif op == 'wait':
        time.sleep(float(a[0]))
    elif op == 'perf':
        return {'ok': True, 'perf': perf_summary()}
    elif op == 'serial':
        lines = G.serial_path.read_text(errors='replace').splitlines()
        return {'ok': True, 'lines': lines[-int(a[0]):]}
    elif op == 'grep':
        lines = [l for l in G.serial_path.read_text(errors='replace').splitlines() if re.search(arg, l)]
        return {'ok': True, 'count': len(lines), 'last': lines[-3:]}
    elif op == 'cursor':
        return {'ok': True, 'cursor': G.cursor()}
    elif op == 'quit':
        return {'ok': True, 'quit': True}
    else:
        return {'ok': False, 'error': 'unknown command ' + op}
    return {'ok': True}


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        cmd = self.rfile.readline().decode().strip()
        try:
            reply = handle(cmd)
        except Exception as e:  # report to the controller, keep the guest alive
            reply = {'ok': False, 'error': f'{type(e).__name__}: {e}'}
        self.wfile.write((json.dumps(reply, ensure_ascii=False) + '\n').encode())
        if reply.get('quit'):
            self.server.shutdown_requested = True


def main():
    global G
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--pages', type=Path)
    ap.add_argument('--port', type=int, default=5555)
    ap.add_argument('--resolution', default='1280x720')
    ap.add_argument('--bios', action='store_true')
    ap.add_argument('--tablet', action='store_true', help='VirtIO tablet (absolute pointer) instead of usb-mouse')
    ap.add_argument('--https', action='store_true', help='also serve the pages over local TLS (10.0.2.100:443)')
    ap.add_argument('--build', type=Path, default=ROOT)
    ap.add_argument('--timeout', type=int, default=7200)
    a = ap.parse_args()
    G = Guest(a.iso, a.out, a.build, a.bios, a.resolution, a.pages, a.timeout, https=a.https, tablet=a.tablet)
    try:
        G.boot_to_desktop()
        print('READY', G.w, G.h, G.desktop_seconds, flush=True)
        socketserver.TCPServer.allow_reuse_address = True
        with socketserver.TCPServer(('127.0.0.1', a.port), Handler) as srv:
            srv.shutdown_requested = False
            srv.timeout = 1
            while not srv.shutdown_requested:
                srv.handle_request()
                G.log()
    finally:
        G.close()


if __name__ == '__main__':
    main()
