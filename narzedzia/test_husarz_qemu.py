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
    parser.add_argument('--kernel', type=Path, default=ROOT / 'system_operacyjny.bin', help='Matching unmodified kernel ELF for symbol observation and artifact hashing')
    parser.add_argument('--output', type=Path, default=ROOT / '.husarz-test/qemu')
    parser.add_argument('--timeout', type=float, default=900)
    parser.add_argument('--diagnostics-only', action='store_true', help='Development counters observed through serial, without screenshot polling during work')
    parser.add_argument('--performance', action='store_true', help='Heavy supported pages, repeated maximize/restore, cancellation and long scrolling')
    parser.add_argument('--layout-only', action='store_true', help='Native table spans, border conflict and signed margin pixels with resize')
    parser.add_argument('--render-only', action='store_true', help='Development pass over resources, forms and theme isolation')
    parser.add_argument('--javascript-forms-only', action='store_true', help='Focused native JavaScript form and keyboard diagnostic')
    parser.add_argument('--javascript-cancellation-only', action='store_true', help='Focused pending async fetch navigation and tab-close cancellation')
    parser.add_argument('--javascript-storage-only', action='store_true', help='Focused ordinary-ISO storage persistence and isolation acceptance')
    parser.add_argument('--modern-javascript-only', action='store_true', help='New FPU/modern JavaScript acceptance; run --javascript-only separately for existing JS regression')
    parser.add_argument('--modern-javascript', action='store_true', help='Binary64, prototypes, exceptions, Promise/async, fetch/JSON and storage plus existing JS regression')
    parser.add_argument('--javascript-only', action='store_true', help='Native bounded JavaScript, DOM, events, forms, timers and external cache')
    parser.add_argument('--editing-only', action='store_true', help='Release UTF-8 selection, OS clipboard, password protection and GIF pixels')
    parser.add_argument('--images-only', action='store_true', help='Focused native PNG/JPEG pixels and shared-cache regression')
    parser.add_argument('--fault-vector', type=int, choices=[6,14], default=6)
    parser.add_argument('--fault-probe', type=Path, help='Private test kernel with a deliberately faulting Ring 3 browser payload')
    args = parser.parse_args()
    if args.modern_javascript_only:args.modern_javascript=True
    if args.javascript_forms_only or args.modern_javascript or args.javascript_storage_only or args.javascript_cancellation_only:args.javascript_only=True
    if args.images_only or args.layout_only or args.editing_only or args.javascript_only:
        args.render_only = True
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'result.json').unlink(missing_ok=True)
    requests = []
    finished_responses = []
    requests_lock = threading.Lock()

    def request_for_log(path):
        # Keep fixture assertions in memory; persist no submitted control values,
        # including passwords, in diagnostics or machine-readable reports.
        route, mark, query = path.partition('?')
        if not mark:
            return path
        return route + '?' + '&'.join(part.split('=', 1)[0] + '=[redacted]'
                                      for part in query.split('&'))

    def request_log():
        with requests_lock:
            return [request_for_log(path) for path in requests]

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            name = self.path.split('?', 1)[0].lstrip('/')
            if '/' in name or not (FIXTURES / name).is_file():
                self.send_error(404)
                return
            data = (FIXTURES / name).read_bytes()
            with requests_lock:
                requests.append(self.path)
            if name.startswith('slow-'):
                # Cancellation must precede completion even when native USB
                # pointer movement/clicks take several host seconds under TCG.
                time.sleep(30 if self.path == '/slow-js-data.json?cancel=1' else 5)
            if name == 'scroll.html':
                time.sleep(.3)  # Expose the ordinary loading redraws to observation.
            try:
                self.send_response(200)
                self.send_header('Content-Type', 'text/javascript' if name.endswith('.js') else 'text/css' if name.endswith('.css') else 'image/png' if name.endswith('.png') else 'image/jpeg' if name.endswith('.jpg') else 'image/gif' if name.endswith('.gif') else 'text/html; charset=utf-8')
                # Cache assertions test reuse of fresh entries, independent of slow TCG runs.
                self.send_header('Cache-Control', 'no-store' if name.startswith('slow-js-') else 'max-age=3600')
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Connection', 'close')
                self.end_headers()
                self.wfile.write(data)
            finally:
                with requests_lock:
                    finished_responses.append(self.path)

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
    kernel = args.fault_probe.resolve() if args.fault_probe else args.kernel.resolve()
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
    tick_address = symbol('licznik_tykniec_zegara')
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
           '-netdev', 'user,id=n1,guestfwd=tcp:10.0.2.100:80-cmd:' + bridge_command + ',guestfwd=tcp:10.0.2.101:80-cmd:' + bridge_command, '-device', 'virtio-net-pci,netdev=n1,disable-legacy=on']
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
                # Serial writes arrive in fragments; the process path used to
                # distinguish the deliberate FPU fault can be in a later write.
                complete_text=text[:text.rfind('\n')+1]
                faults=[line for line in complete_text.splitlines() if '[USER FAULT]' in line]
                unexpected=[line for line in faults if not (args.modern_javascript and 'vector=6 ' in line and 'program=/programy/fpu-b.cebula/fpu-b.bur' in line)]
                if not args.fault_probe and unexpected:
                    raise RuntimeError('Ring 3 process faulted\n'+text[-2000:])
                if time.monotonic() >= deadline:
                    raise TimeoutError('Husarz regression deadline\n' + text[-2000:])
                return text

            def wait(predicate, label, seconds=90, interval=.15):
                until = min(deadline, time.monotonic() + seconds)
                while time.monotonic() < until:
                    health()
                    if predicate():
                        return
                    time.sleep(interval)
                raise TimeoutError(label)

            wait(lambda: '[DIAG APP] First frame submitted' in health(), 'native desktop ready', args.timeout)
            wait(lambda: all('role=' + role + ' result=interrupt IN armed' in health()
                             for role in ('boot-keyboard', 'boot-mouse')),
                 'USB keyboard and mouse interrupt endpoints ready')
            q = Qmp(sock)
            # TCG screenshot encoding can exceed the shared client's 3s default.
            q.socket.settimeout(15)

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
                    started = guest_ticks()
                    hmp('mouse_button 1')
                    wait(lambda: guest_ticks() - started >= 32, 'guest consumed mouse press', 30)
                    started = guest_ticks()
                    hmp('mouse_button 0')
                    wait(lambda: guest_ticks() - started >= 32, 'guest consumed mouse release', 30)
                time.sleep(.5)

            def guest_ticks():
                match = re.search(r':\s*0x([0-9a-fA-F]+)', hmp(f'xp /1gx 0x{tick_address:x}'))
                if not match:
                    raise RuntimeError('Cannot read guest timer observer')
                return int(match[1], 16)

            def key(*codes):
                started = guest_ticks()
                q.call('send-key', {'keys': [{'type': 'qcode', 'data': c} for c in codes], 'hold-time': 80})
                # send-key releases on guest virtual time. Host sleeps can finish
                # before that release under TCG load, overlapping successive keys.
                # 48 native 250 Hz ticks include release and USB polling latency.
                wait(lambda: guest_ticks() - started >= 48, 'guest consumed key press/release', 30)

            def screenshot(name):
                if not name.endswith('probe') and name not in ('browser-opening', 'notepad-after-fault'):
                    print('Screenshot:', name, flush=True)
                probe=name.endswith('probe') or name=='browser-opening'
                path = out / (name + ('.ppm' if probe else '.png'))
                # Freeze virtual device time only while observing the frame.
                # Host image encoding/file IO must not consume the guest GPU's
                # 500 ms control-command deadline with its backend blocked by QMP.
                q.call('stop')
                try:
                    q.call('screendump', {'filename': str(path), 'format': 'ppm' if probe else 'png'})
                finally:
                    q.call('cont')
                with Image.open(path) as im:
                    return im.convert('RGB').copy()

            def count(path):
                with requests_lock:
                    return requests.count('/' + path)

            def finished(path):
                with requests_lock:
                    return finished_responses.count('/' + path)

            def page_idle():
                # A white mouse pointer over the label is not enabled text.
                # Move it using native input before observing button state.
                pointer=position()
                if frame['x']+160<=pointer[0]<=frame['x']+285 and frame['y']+65<=pointer[1]<=frame['y']+140:
                    move(frame['x']+400,frame['y']+100)
                im=screenshot('idle-probe')
                # Observe enabled/disabled native Stop text after the submitted frame;
                # asynchronous publication and both stock themes are supported.
                area=im.crop((frame['x']+190,frame['y']+94,frame['x']+250,frame['y']+107))
                # Both stock themes use gray disabled text. Enabled text is
                # white on dark chrome and black on light chrome; background
                # brightness must never be counted as enabled label pixels.
                background=im.getpixel((frame['x']+205,frame['y']+90))
                if sum(background)>384:
                    enabled=sum(1 for r,g,b in area.getdata() if max(r,g,b)<80)
                else:
                    enabled=sum(1 for r,g,b in area.getdata() if min(r,g,b)>200)
                return enabled<8

            def navigate(name, settle=2,host="10.0.2.100"):
                print('Navigate:', name, flush=True)
                previous = count(name)
                key('ctrl', 'l')
                time.sleep(.15)
                for c in 'http://' + host + '/' + name:
                    if c == ':':
                        key('shift', 'semicolon')
                    else:
                        key({'/': 'slash', '.': 'dot', '-': 'minus', '_': 'minus'}.get(c, c))
                key('ret')
                wait(lambda: count(name) > previous, 'HTTP request ' + name)
                if settle:
                    started=guest_ticks()
                    wait(lambda:guest_ticks()-started>=int(settle*250),
                         'guest processing time for '+name,60)
                if settle and not name.startswith('slow-'):
                    idle_since=None
                    def settled_idle():
                        nonlocal idle_since
                        if not page_idle():
                            idle_since=None
                            return False
                        now=guest_ticks()
                        if idle_since is None:idle_since=now
                        return now-idle_since>=128
                    wait(settled_idle,'active-tab processing completed for '+name,240,interval=1)
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
                region = image.crop((frame['x']+2, frame['y']+116, frame['x']+frame['w']-19,
                                     frame['y']+frame['h']-24))
                return hashlib.sha256(region.tobytes()).hexdigest()

            time.sleep(2)
            click(1124, 44)  # Close the default diagnostic utility through its normal titlebar.
            if args.modern_javascript:
                click(32,700);click(130,182);time.sleep(2)
                for c in 'uruchom /programy/fpu-a.cebula/fpu-a.bur':
                    key({' ':'spc','/':'slash','.':'dot','-':'minus'}.get(c,c))
                key('ret')
                wait(lambda:'[FPU PROBE] A PASS' in health() and health().count('[FPU PROBE] B PASS')>=2,'native FPU context isolation, fault cleanup and PID reuse',180)
                if '[FPU PROBE] FAIL' in health():raise AssertionError('Native FPU isolation failed')
                result['fpu_process_isolation']=True
                # The native shell implements its exit command; it does not
                # implement the browser's Alt+F4 shortcut or window geometry.
                shell_header=screenshot('fpu-shell-probe').crop((300,116,930,142)).tobytes()
                for code in 'exit':key(code)
                key('ret')
                wait(lambda:screenshot('fpu-shell-probe').crop((300,116,930,142)).tobytes()!=shell_header,'FPU launcher shell closed',60)
                screenshot('fpu-shell-closed')
            move(320,28)
            hmp('mouse_button 2'); time.sleep(.2); hmp('mouse_button 0'); time.sleep(.5)
            screenshot('browser-launch-menu')
            click(345,43)  # Open in the native desktop context menu; no timed double-click.
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
                hmp('mouse_button 2');time.sleep(.2);hmp('mouse_button 0');time.sleep(.5)
                click(105,43)
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
            if args.diagnostics_only:
                def native_perf(nodes,width=None,after_candidates=None):
                    for line in reversed(health().splitlines()):
                        if not line.startswith('[HUSARZ PERF] '):continue
                        fields={key:int(value) for key,value in re.findall(r'(\w+)=(\d+)',line)}
                        if fields.get('nodes')!=nodes or not fields.get('navigationms'):continue
                        if width is not None and fields.get('viewportwidth')!=width:continue
                        if after_candidates is not None and fields.get('paintcandidates',0)<=after_candidates:continue
                        return fields
                    return None
                navigate('heavy-mixed.html',settle=0)
                wait(lambda:native_perf(1266),'serial mixed navigation completion',300)
                normal=native_perf(1266);normal_frame=dict(frame)
                screenshot('diagnostic-mixed')
                click(frame['x']+frame['w']-40,frame['y']+14)
                frame.update(x=0,y=0,w=1280,h=680)
                wait(lambda:native_perf(1266,1262),'serial maximized layout and paint',180)
                maximized=native_perf(1266,1262)
                click(frame['x']+frame['w']-40,frame['y']+14);frame.update(normal_frame)
                wait(lambda:native_perf(1266,782, maximized['paintcandidates']),'serial restored layout and paint',180)
                restored=native_perf(1266,782, maximized['paintcandidates'])
                navigate('heavy-long.html',settle=0)
                wait(lambda:native_perf(1406),'serial long navigation completion',300)
                long_initial=native_perf(1406)
                click(400,400);key('end')
                wait(lambda:native_perf(1406,after_candidates=long_initial['paintcandidates']),'serial long scroll paint',180)
                long_scrolled=native_perf(1406,after_candidates=long_initial['paintcandidates'])
                screenshot('diagnostic-long-scrolled');health()
                samples=[normal,maximized,restored,long_initial,long_scrolled]
                if any(sample['maxslicework']>2048 for sample in samples):raise AssertionError('Native deterministic work cap exceeded')
                if normal['selectorchecks']!=restored['selectorchecks']:raise AssertionError('Resize repeated style matching')
                if long_initial['paintculled']<=long_initial['paintvisible']*5:raise AssertionError('Insufficient native long-page culling')
                result.update(result='PASS',mode='diagnostics',kernel_sha256=kernel_sha,iso_sha256=iso_sha,
                              no_guest_fault=True,http_requests=request_log(),samples=samples)
                (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                print(json.dumps(result),flush=True)
                return
            if args.performance:
                result['mode']='performance'
                navigate('heavy-mixed.html')
                def marker_span():
                    im=screenshot('performance-probe')
                    xs=[x for x in range(frame['x'],frame['x']+frame['w']-18)
                        if im.getpixel((x,frame['y']+126))==(38,162,105)]
                    return (min(xs),max(xs)) if xs else None
                wait(marker_span,'heavy mixed content published',120)
                def marker_width_is(width):
                    span=marker_span()
                    return span is not None and span[1]-span[0]+1==width
                normal_frame=dict(frame)
                initial_span=marker_span();normal_width=initial_span[1]-initial_span[0]+1
                before_resize=list(requests)
                resize_samples=[]
                for cycle in range(4):
                    click(frame['x']+frame['w']-40,frame['y']+14)
                    frame.update(x=0,y=0,w=1280,h=680)
                    # Consume native input before waiting for the new layout.
                    # This observes the WM/input path while reflow is pending.
                    move(1000,500)
                    if position()!=[1000,500]:raise AssertionError('Mouse did not respond during pending maximize')
                    wait(lambda: marker_width_is(normal_width+480),'latest maximized heavy geometry',90,interval=1)
                    move(1000,500)
                    if position()!=[1000,500]:raise AssertionError('Mouse did not respond during maximize')
                    thumb(screenshot('heavy-maximized-'+str(cycle)))
                    resize_samples.append({'maximized':True,'span':marker_span()})
                    click(frame['x']+frame['w']-40,frame['y']+14)
                    frame.update(normal_frame)
                    move(800,500)
                    if position()!=[800,500]:raise AssertionError('Mouse did not respond during pending restore')
                    wait(lambda: marker_width_is(normal_width),'latest restored heavy geometry',90,interval=1)
                    thumb(screenshot('heavy-restored-'+str(cycle)))
                    resize_samples.append({'maximized':False,'span':marker_span()})
                if list(requests)!=before_resize:raise AssertionError('Maximize/restore fetched resources')
                result['maximize_restore']=resize_samples
                # Each fixture is fetched through the unchanged real native worker.
                for name in ['heavy-dom.html','heavy-css.html','heavy-table.html','heavy-images.html']:
                    navigate(name);move(900,650);time.sleep(4)
                    if name=='heavy-images.html':wait(lambda: count('sample.jpg?slot=11')>0,'uncached image storm completed',120)
                    health();screenshot(name[:-5])
                navigate('heavy-long.html')
                wait(lambda: thumb(screenshot('long-ready-probe')),'long page scrollbar',90)
                time.sleep(4);before_scroll=list(requests)
                click(400,400)
                wheel_before=thumb(screenshot('long-wheel-before'))[0]
                q.call('input-send-event',{'events':[{'type':'btn','data':{'button':'wheel-down','down':down}} for _ in range(6) for down in (True,False)]})
                wait(lambda: thumb(screenshot('long-wheel-probe'))[0]>wheel_before,'heavy long page wheel scroll',60)
                wait(page_idle,'heavy long wheel paint',90,interval=1)
                key('pgdn');key('pgdn');key('end');time.sleep(2)
                long_end=screenshot('heavy-long-end')
                start,end=thumb(long_end)
                if end<frame['y']+frame['h']-90:raise AssertionError('Long page End did not reach bottom')
                key('home');time.sleep(1)
                bar=thumb(screenshot('heavy-long-home'));x=frame['x']+frame['w']-9
                move(x,(bar[0]+bar[1])//2);hmp('mouse_button 1');move(x,frame['y']+frame['h']-70);hmp('mouse_button 0');time.sleep(2)
                screenshot('heavy-long-dragged')
                if list(requests)!=before_scroll:raise AssertionError('Long page scroll triggered a fetch')
                result['long_scroll_no_fetch']=True
                # Start another heavy navigation and issue ordinary UI commands.
                # Stop and tab commands must be processed before waiting for page pixels.
                # Open a real second app, then return to the browser before
                # starting work. Title pixels establish that Alt+Tab switched apps.
                def title_hash():
                    return hashlib.sha256(screenshot('title-probe').crop((105,101,800,126)).tobytes()).hexdigest()
                browser_title=title_hash()
                move(80,28);hmp('mouse_button 2');time.sleep(.2);hmp('mouse_button 0');time.sleep(.5)
                click(105,43)
                wait(lambda: title_hash()!=browser_title,'second native app first frame',60)
                key('alt','tab')
                wait(lambda: title_hash()==browser_title,'browser active before heavy navigation',30)
                click(400,400);key('home');move(1000,660)
                wait(page_idle,'long scroll paint finished',90,interval=1)
                old_content=page_hash(screenshot('before-heavy-cancel'))
                navigate('heavy-css.html',settle=0)
                wait(lambda:not page_idle(),'heavy cancellation work visibly pending',30)
                key('alt','tab')
                wait(lambda: title_hash()!=browser_title,'another native app active during document work',30)
                key('alt','tab')
                wait(lambda: title_hash()==browser_title,'returned to Husarz during document work',30)
                key('esc');time.sleep(2)
                if page_hash(screenshot('heavy-stopped'))!=old_content:
                    raise AssertionError('Canceled heavy work published new page pixels')
                def tabs_hash():
                    return hashlib.sha256(screenshot('tabs-probe').crop((101,157,899,185)).tobytes()).hexdigest()
                tabs=tabs_hash();key('ctrl','t')
                wait(lambda: tabs_hash()!=tabs,'new tab command handled',30)
                navigate('heavy-css.html',settle=0)
                wait(lambda:not page_idle(),'tab close work visibly pending',30)
                key('ctrl','w')
                wait(lambda: tabs_hash()==tabs,'tab close command handled',30)
                move(800,600);health();screenshot('heavy-stop-tab-close')
                result.update(result='PASS',kernel_sha256=kernel_sha,iso_sha256=iso_sha,
                              no_guest_fault=True,http_requests=request_log(),mouse_responsive=True,
                              heavy_fixtures=6,stop_tab_close_commands=True,app_switch_commands=True)
                (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                print(json.dumps(result),flush=True)
                return
            if not args.render_only:
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
                    wait(page_idle,'refreshed document processing complete',90,interval=1)
                    time.sleep(1)
                result['refresh_frames_anchored'] = refresh_frames
                def wheel_steps(button, steps, direction):
                    # Wait for each native repaint before sending another detent.
                    # TCG can take longer than a fixed delay to drain USB input and
                    # paint a page; queued down events must not overlap the up test.
                    for _ in range(steps):
                        previous_image=screenshot('wheel-probe')
                        previous=thumb(previous_image)[0];previous_hash=page_hash(previous_image)
                        started=guest_ticks()
                        q.call('input-send-event', {'events': [
                            {'type': 'btn', 'data': {'button': button, 'down': True}}]})
                        wait(lambda:guest_ticks()-started>=32,'guest consumed wheel press',30)
                        started=guest_ticks()
                        q.call('input-send-event', {'events': [
                            {'type': 'btn', 'data': {'button': button, 'down': False}}]})
                        wait(lambda:guest_ticks()-started>=32,'guest consumed wheel release',30)
                        def wheel_repainted():
                            current=screenshot('wheel-probe')
                            return direction*(thumb(current)[0]-previous)>0 and page_hash(current)!=previous_hash
                        wait(wheel_repainted,'native repaint after '+button,90,interval=1)

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
                started=guest_ticks();hmp('mouse_button 1')
                wait(lambda:guest_ticks()-started>=32,'guest consumed scrollbar drag press',30)
                move(sx, frame['y']+frame['h']-55)
                started=guest_ticks()
                wait(lambda:guest_ticks()-started>=32,'guest consumed scrollbar drag move',30)
                started=guest_ticks();hmp('mouse_button 0')
                wait(lambda:guest_ticks()-started>=32,'guest consumed scrollbar drag release',30)
                wait(lambda: thumb(screenshot('drag-probe'))[0] >= frame['y']+frame['h']-55-(thumb(up)[1]-thumb(up)[0])//2-2,
                     'native thumb reached the drag target', 30)
                move(1000,660)
                dragged = screenshot('thumb-dragged')
                if thumb(dragged)[0] <= thumb(up)[0] or page_hash(dragged) == page_hash(up):
                    raise AssertionError('Scrollbar dragging did not move the document')
                click(sx, frame['y']+155)
                wait(lambda: thumb(screenshot('track-probe'))[0] < thumb(dragged)[0],
                     'native track click repaint', 90)
                track = screenshot('track-click')
                if thumb(track)[0] >= thumb(dragged)[0]:
                    raise AssertionError('Scrollbar track click')
                click(400, 300)
                key('home')
                # Match wheel/resize publication deadlines under contended TCG.
                # The observed canonical thumb position must still be exact.
                wait(lambda: thumb(screenshot('home-probe'))[0] == initial_thumb[0],
                     'Home resets the canonical offset', 90)
                if thumb(screenshot('home'))[0] != initial_thumb[0]:
                    raise AssertionError('Home did not reset the canonical offset')
                key('end')
                wait(lambda: thumb(screenshot('end-probe'))[0] > initial_thumb[0],
                     'End scrolls to the bottom', 90)
                end = thumb(screenshot('end'))
                key('pgup')
                wait(lambda: thumb(screenshot('page-up-probe'))[0] < end[0],
                     'Page Up repaint', 90)
                if thumb(screenshot('page-up'))[0] >= end[0]:
                    raise AssertionError('Page Up')
                key('pgdn')
                wait(lambda: thumb(screenshot('page-down-probe'))[0] == end[0],
                     'Page Down returns to the bottom', 90)
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
            if args.javascript_only:
                green=(51,187,119)
                blue=(51,102,204)
                def pixels(color):
                    im=screenshot('javascript-probe')
                    return sum(n for n,c in (im.getcolors(im.width*im.height) or []) if c==color)
                def marker(color, label):
                    wait(lambda:pixels(color)>10000,label,120,interval=.5)
                def click_color(color):
                    points=[]
                    def target_ready():
                        nonlocal points
                        im=screenshot('event-target-probe')
                        points=[(x,y) for y in range(frame['y']+116,min(im.height,frame['y']+frame['h']))
                                for x in range(frame['x'],min(im.width,frame['x']+frame['w']-16)) if im.getpixel((x,y))==color]
                        return bool(points)
                    wait(target_ready,'native event target '+str(color),90,interval=.5)
                    x=min(p[0] for p in points)+12;y=min(p[1] for p in points)+12
                    click(x,y)
                def frame_ready(expected):
                    im=screenshot('frame-geometry-probe')
                    r,g,b=im.getpixel((expected['x']+expected['w']-17,expected['y']+14))
                    return r>150 and g<130 and b<130
                def storage_acceptance():
                    navigate('js-storage.html');marker(green,'first localStorage visit')
                    navigate('js-storage.html');marker(blue,'localStorage reload persistence')
                    navigate('js-session.html');marker(blue,'sessionStorage survives same-tab navigation')
                    key('ctrl','t');navigate('js-session.html');marker(green,'new tab has independent sessionStorage')
                    navigate('js-storage.html');marker(blue,'same-origin localStorage shared across tabs')
                    key('ctrl','w');navigate('js-session.html');marker(blue,'original tab retains sessionStorage')
                    navigate('js-storage.html',host='10.0.2.101');marker(green,'different origin cannot read localStorage')
                    navigate('js-storage.html');marker(blue,'original origin still owns its data')
                    # Worker persistence completes before closing the owning process.
                    started=guest_ticks();wait(lambda:guest_ticks()-started>=1000,'storage worker flush interval',60)
                    for restart in range(3):
                        click(frame['x']+frame['w']-17,frame['y']+14)
                        wait(lambda:not browser_ready(),'browser window closed before restart',90)
                        move(320,28);hmp('mouse_button 2');time.sleep(.2);hmp('mouse_button 0');time.sleep(.5);click(345,43)
                        wait(browser_ready,'browser restart after localStorage write '+str(restart))
                        navigate('js-session.html');marker(green,'browser restart destroys sessionStorage')
                        navigate('js-storage.html');marker(blue,'localStorage survives browser restart')
                def cancellation_acceptance():
                    navigate('js-number.html');marker(green,'baseline before async cancellation')
                    move(frame['x']+30,frame['y']+100)
                    before_forbidden=count('js-forbidden.html');slow_before=count('slow-js-data.json?cancel=1')
                    navigate('js-async-cancel.html',settle=0);wait(lambda:count('slow-js-data.json?cancel=1')>slow_before,'async fetch pending before navigation',90)
                    move(frame['x']+60,frame['y']+100)
                    # Observe a fresh response before checking pixels: the outgoing
                    # document can remain visible while navigation is pending.
                    # Use the native Back button so a held keyboard shortcut cannot
                    # repeat across two fast history entries under loaded TCG.
                    baseline_requests=count('js-number.html')
                    click(frame['x']+30,frame['y']+100)
                    wait(lambda:count('js-number.html')>baseline_requests,'Back loaded the async cancellation baseline',90)
                    marker(green,'navigation invalidates pending async fetch continuation')
                    if count('js-forbidden.html')!=before_forbidden:raise AssertionError('Stale async fetch navigated')
                    slow_before=count('slow-js-data.json?cancel=1');key('ctrl','t');navigate('js-async-cancel.html',settle=0)
                    wait(lambda:count('slow-js-data.json?cancel=1')>slow_before,'async fetch pending before tab close',90);key('ctrl','w');marker(green,'tab close invalidates pending async fetch')
                    expected_responses=count('slow-js-data.json?cancel=1')
                    wait(lambda:finished('slow-js-data.json?cancel=1')>=expected_responses,'old fetch responses completed after cancellation',90)
                    marker(green,'old fetch completions preserve the current document')
                    if count('js-forbidden.html')!=before_forbidden:raise AssertionError('Closed-tab async fetch navigated')
                if args.javascript_cancellation_only:
                    cancellation_acceptance()
                    result.update(result='PASS',mode='javascript-cancellation-only',kernel_sha256=kernel_sha,iso_sha256=iso_sha,async_cancellation=True,no_guest_fault=True,http_requests=request_log())
                    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');return
                if args.javascript_storage_only:
                    storage_acceptance()
                    result.update(result='PASS',mode='javascript-storage-only',kernel_sha256=kernel_sha,iso_sha256=iso_sha,storage=True,no_guest_fault=True,http_requests=request_log())
                    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');return
                def modern_acceptance_record():
                    expected=[line for line in health().splitlines() if '[USER FAULT]' in line]
                    if len(expected)!=1:raise AssertionError('Expected exactly one native FPU partner fault')
                    if any(path.startswith('/js-forbidden.html') or path.startswith('/cross-only.json') for path in requests):raise AssertionError('Canceled or cross-origin JS work reached a forbidden fixture')
                    result.pop('no_guest_fault',None)
                    result.update(no_unexpected_guest_fault=True,no_browser_fault=True,expected_fpu_fault_vector=6,
                                  async_fetch_navigation_cancellation=True,async_fetch_tab_close_cancellation=True,
                                  async_resize=True,local_storage_reload=True,local_storage_browser_restart=True,
                                  storage_origin_isolation=True,session_storage_tab_isolation=True,installed_volume_reboot_tested=False)
                if args.modern_javascript:
                    for name in ['js-number','js-prototype','js-exceptions','js-arrow','js-promise','js-async','js-fetch','js-modern-mixed']:
                        if name=='js-modern-mixed':
                            navigate(name+'.html',settle=0);normal_frame=dict(frame)
                            marker((68,102,136),'new async mixed document readiness stripe')
                            click(frame['x']+frame['w']-40,frame['y']+14);frame.update(x=0,y=0,w=1280,h=680)
                            wait(lambda:frame_ready(frame),'async maximized native window geometry',90)
                            marker(blue,'async fetch DOM completion while maximized')
                            click(frame['x']+frame['w']-40,frame['y']+14);frame.update(normal_frame)
                            wait(lambda:frame_ready(frame),'async restored native window geometry',90)
                            marker(blue,'async document restored')
                        else:navigate(name+'.html');marker(green,name+' modern native result')
                        screenshot(name)
                    if count('cross-only.json'):raise AssertionError('Cross-origin JavaScript fetch reached the network')
                    cancellation_acceptance()
                    navigate('js-async-hostile.html',settle=0);move(1000,500);marker(blue,'hostile async recursion contained')
                    storage_acceptance()
                    navigate('js-microtask-hostile.html',settle=0);move(1000,500)
                    if position()!=[1000,500]:raise AssertionError('Hostile microtasks stopped native mouse input')
                    marker(blue,'hostile Promise chain contained')
                    click(frame['x']+220,frame['y']+100);navigate('js-number.html');marker(green,'Stop and navigation after hostile microtasks')
                    result['modern_javascript']=True
                    if args.modern_javascript_only:
                        click(frame['x']+frame['w']-17,frame['y']+14)
                        health()
                        result.update(result='PASS',mode='modern-javascript-only',kernel_sha256=kernel_sha,iso_sha256=iso_sha,native_vm=True,hostile_microtasks_contained=True,hostile_async_contained=True,http_requests=request_log())
                        modern_acceptance_record()
                        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                        print(json.dumps(result),flush=True)
                        return
                for name,color in ([] if args.javascript_forms_only else [('js-basic.html',green),('js-dom.html',blue),('js-timers.html',green),('js-errors.html',green),('js-hostile.html',blue)]):
                    navigate(name)
                    marker(color,name+' visible script result')
                    screenshot(name[:-5])
                if not args.javascript_forms_only:navigate('js-events.html')
                before_forbidden=count('js-forbidden.html')
                if not args.javascript_forms_only:
                    click_color((153,119,68));marker(green,'click listener')
                    click_color((170,102,187));marker(blue,'preventDefault link')
                    time.sleep(1)
                    if count('js-forbidden.html')!=before_forbidden:raise AssertionError('Prevented link navigated')
                navigate('js-forms.html')
                click(frame['x']+60,frame['y']+116+84)
                key('z');time.sleep(.5)
                if pixels(green)>10000:raise AssertionError('Prevented key inserted text')
                key('a');marker(green,'input event')
                click_color((153,119,68));marker(blue,'submit prevention and form mutation')
                if count('js-forbidden.html')!=before_forbidden:raise AssertionError('Prevented form submitted')
                if args.javascript_forms_only:
                    result.update(result='PASS',mode='javascript-forms-only',kernel_sha256=kernel_sha,iso_sha256=iso_sha,input=True,keydown_prevention=True,submit_prevention=True,no_guest_fault=True)
                    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');return
                navigate('js-external.html');marker(blue,'ordered external plus inline execution')
                loaded=count('js-external.js')
                navigate('js-external.html');marker(blue,'cached external script execution')
                if count('js-external.js')!=loaded:raise AssertionError('External JavaScript cache missed')
                def js_title():
                    return hashlib.sha256(screenshot('js-title-probe').crop((105,101,800,126)).tobytes()).hexdigest()
                browser_title=js_title()
                move(80,28);hmp('mouse_button 2');time.sleep(.2);hmp('mouse_button 0');time.sleep(.5);click(105,43)
                wait(lambda:js_title()!=browser_title,'real second native app opened',60)
                key('alt','tab');wait(lambda:js_title()==browser_title,'browser restored before JS work',30)
                navigate('js-heavy.html',settle=0)
                key('alt','tab');wait(lambda:js_title()!=browser_title,'second app while JS work pending',30)
                key('alt','tab');wait(lambda:js_title()==browser_title,'return to JS browser',30)
                marker(green,'bounded heavy script completion after app switching')
                normal_frame=dict(frame)
                click(frame['x']+frame['w']-40,frame['y']+14)
                frame.update(x=0,y=0,w=1280,h=680)
                marker(green,'script document maximized')
                click(frame['x']+frame['w']-40,frame['y']+14)
                frame.update(normal_frame)
                marker(green,'script document restored')
                click(frame['x']+220,frame['y']+100)
                wait(page_idle,'Stop cancels active script-driven mutation timer',90)
                # Desktop theme changes must not recolor stable web content.
                move(1000,660);before_theme=screenshot('js-theme-before');content_before=page_hash(before_theme)
                click(32,700);click(130,532);time.sleep(2);click(210,350);click(330,466)
                wait(lambda:min(screenshot('js-theme-probe').getpixel((110,550)))>160,'light desktop theme during JS milestone',30)
                click(784,114);wait(lambda:page_hash(screenshot('js-theme-after'))==content_before,'web colors survive desktop theme change',30)
                # Replace a document before a retained timer fires.
                navigate('js-cancel.html',settle=0)
                navigate('js-basic.html');marker(green,'navigation cancels old script runtime')
                time.sleep(.5);marker(green,'stale timer cannot affect new document')
                key('ctrl','t');navigate('js-hostile.html');marker(blue,'hostile second tab contained')
                key('ctrl','w');marker(green,'original tab remains usable')
                before_forbidden=count('js-forbidden.html')
                key('ctrl','t');navigate('js-active.html',settle=0);marker(green,'active bounded timer in new tab')
                key('ctrl','w');time.sleep(31)
                if count('js-forbidden.html')!=before_forbidden:raise AssertionError('Closed tab timer survived')
                marker(green,'surviving tab after active timer closure')
                navigate('js-active.html',settle=0);marker(green,'active script before browser close')
                click(frame['x']+frame['w']-17,frame['y']+14);time.sleep(2)
                health()
                result.update(result='PASS',mode='javascript-only',kernel_sha256=kernel_sha,iso_sha256=iso_sha,
                              native_vm=True,inline_order=True,dom_mutation=True,click=True,prevent_default=True,
                              input_event=True,submit_prevented=True,timer=True,external_cache=True,
                              hostile_budget_contained=True,error_contained=True,app_switch=True,
                              resize=True,navigation_cancellation=True,tab_isolation=True,stop_active_js=True,close_active_timer=True,close_active_js_browser=True,theme_independent=True,no_guest_fault=True,
                              http_requests=request_log())
                if args.modern_javascript:modern_acceptance_record()
                (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                print(json.dumps(result),flush=True)
                return
            if args.layout_only:
                navigate('layout-grid.html')
                points = [(180,10,(34,170,68)), (290,10,(34,68,204)),
                          (290,50,(238,187,34)), (290,90,(238,119,34)),
                          (48,140,(0,0,255)), (50,140,(0,0,255)),
                          (52,140,(0,0,255)), (20,195,(204,34,51)),
                          (20,230,(102,51,204))]
                def layout_pixels():
                    im=screenshot('layout-pixels-probe')
                    return all(im.getpixel((frame['x']+x,frame['y']+116+y))==color for x,y,color in points)
                wait(layout_pixels, 'rowspan/colspan, collapsed-border and negative-margin pixels', 60)
                screenshot('layout-grid')
                with requests_lock:
                    before_resize=len(requests)
                normal_frame=dict(frame)
                click(frame['x']+frame['w']-40,frame['y']+14)
                frame.update(x=0,y=0,w=1280,h=680)
                wait(layout_pixels,'table pixels after maximize',30)
                screenshot('layout-maximized')
                click(frame['x']+frame['w']-40,frame['y']+14)
                frame.update(normal_frame)
                wait(layout_pixels,'table pixels after restore',30)
                screenshot('layout-restored')
                with requests_lock:
                    if len(requests)!=before_resize:
                        raise AssertionError('Layout resize fetched the document again')
                health()
                result.update(result='PASS',kernel_sha256=kernel_sha,iso_sha256=iso_sha,
                              table_rowspan=True,table_combined_spans=True,table_border_conflict=True,
                              negative_empty_margin_strut=True,resize_without_network_fetch=True,
                              no_guest_fault=True,http_requests=request_log(),pixel_samples=points)
                (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                print(json.dumps(result),flush=True)
                return
            if args.editing_only:
                navigate('editing.html')
                wait(lambda:count('sample.gif')>0,'GIF request')
                def gif_pixels():
                    im=screenshot('gif-probe')
                    return sum(n for n,c in (im.getcolors(im.width*im.height) or []) if c==(40,80,120))>11000
                wait(gif_pixels,'decoded GIF and CSS background pixels',60)
                screenshot('gif-loaded')
                result['gif_first_frame_pixels']=True
                result['gif_background_pixels']=True
                gif_count=count('sample.gif')
                # Root viewport starts below the 116px native chrome. All edits
                # use native pointer/keyboard and the existing OS clipboard.
                click(frame['x']+60,frame['y']+150)
                key('ctrl','a')
                def selected():
                    im=screenshot('selection-probe')
                    return sum(n for n,c in (im.getcolors(im.width*im.height) or []) if c==(36,90,196))>200
                wait(selected,'browser selection highlight',30)
                screenshot('utf8-selected')
                key('ctrl','c')
                click(frame['x']+60,frame['y']+194)
                key('ctrl','v');key('ctrl','a');key('ctrl','x');key('ctrl','v')
                # Copy/cut on a password must leave the public selection in the
                # OS clipboard. The password control has no successful name.
                click(frame['x']+60,frame['y']+270)
                key('ctrl','a');key('ctrl','c');key('ctrl','x')
                click(frame['x']+60,frame['y']+150)
                key('ctrl','a');key('ctrl','v')
                with requests_lock:before=len(requests)
                click(frame['x']+80,frame['y']+310)
                def selection_submitted():
                    with requests_lock:return next((p for p in requests[before:] if p.startswith('/editing.html?')),None)
                wait(selection_submitted,'selection/clipboard form submission',60)
                from urllib.parse import parse_qs, urlsplit
                values=parse_qs(urlsplit(selection_submitted()).query)
                expected='aąbćęłńóśźżZ'
                if values.get('q')!=[expected] or values.get('note')!=[expected]:
                    raise AssertionError('UTF-8 clipboard or password copy protection failed')
                if any('private-password' in request for request in requests):
                    raise AssertionError('Password appeared in a request')
                wait(page_idle,'editing page ready after submission',120)
                if count('sample.gif')!=gif_count:raise AssertionError('GIF cache missed after navigation')
                result.update(result='PASS',mode='editing-only',kernel_sha256=kernel_sha,iso_sha256=iso_sha,
                              utf8_selection=True,clipboard_copy_cut_paste=True,password_copy_cut_protected=True,
                              gif_cache_reuse=True,no_guest_fault=True,http_requests=request_log())
                (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                print(json.dumps(result),flush=True)
                return
            for name in ([] if args.images_only else ['box.html', 'percent.html', 'tables.html', 'radius.html', 'wrapping.html']):
                navigate(name)
                screenshot(name.replace('.html', ''))
            navigate('render.html')
            wait(lambda: count('sample.png') and count('sample.jpg') and count('render.css'), 'PNG JPEG and CSS requests')
            def images_visible():
                im=screenshot('image-probe')
                # The repeated background has 12px red stripes. The 96px img
                # has a 48px red quadrant; locate it without hardcoded text metrics.
                palette={(240,40,50),(20,40,240),(240,220,50),(20,220,240)}
                for y in range(frame['y']+116, frame['y']+frame['h']-88):
                    run=0
                    for x in range(frame['x']+2, frame['x']+frame['w']-212):
                        run=run+1 if im.getpixel((x,y))==(240,40,50) else 0
                        if run<40:
                            continue
                        left=x-run+1
                        # JPEG is the next 96x64 image. Its lossy edge colors
                        # differ from the PNG palette; black alt text cannot pass.
                        colors=im.crop((left+96,y,left+192,y+64)).getcolors(6144) or []
                        return sum(n for n,c in colors if c not in palette and
                                   not c[0]==c[1]==c[2])>100
                return False
            wait(images_visible,'decoded PNG and JPEG pixels',45)
            result['png_jpeg_pixels']=True
            screenshot('images-loaded')
            resource_counts={name:count(name) for name in ['sample.png','sample.jpg','render.css']}
            key('f5'); wait(lambda: count('render.html')>=2,'image page refresh'); time.sleep(3)
            if any(count(name)!=value for name,value in resource_counts.items()):
                raise AssertionError('Reload fetched cached CSS or images')
            result['png_jpeg_background_cache']=True
            result['resource_requests_before_reload']=resource_counts
            # Open the same resource page in a second real tab; decoded cache entries remain shared.
            key('ctrl','t'); time.sleep(.5); navigate('render.html'); time.sleep(2)
            if any(count(name)!=value for name,value in resource_counts.items()):
                raise AssertionError('Second tab fetched cached CSS or images')
            key('ctrl','w'); time.sleep(.5)
            result['image_cache_two_tabs']=True
            if args.images_only:
                wait(images_visible,'cached PNG and JPEG pixels after tab closure',45)
                screenshot('images-cached')
                health()
                result.update(result='PASS', mode='images-only', kernel_sha256=kernel_sha,
                              iso_sha256=iso_sha, http_requests=request_log(), no_guest_fault=True)
                (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
                print(json.dumps(result),flush=True)
                return
            navigate('forms-native.html')
            click(frame['x']+50,frame['y']+139)
            key('a'); key('b'); key('backspace'); key('tab'); key('p')
            click(frame['x']+50,frame['y']+204)
            key('x'); key('ret'); key('y'); key('alt_r','l')
            click(frame['x']+16,frame['y']+241)
            click(frame['x']+31,frame['y']+265)
            click(frame['x']+80,frame['y']+297)
            # Native software rasterization may finish after the input injection.
            # Observe the open popup before sending its navigation command.
            wait(lambda: screenshot('select-popup-probe').getpixel(
                (frame['x']+200,frame['y']+320)) == (185,215,255),
                'select popup painted', 30)
            screenshot('select-open')
            key('down')
            wait(lambda: screenshot('select-choice-probe').getpixel(
                (frame['x']+200,frame['y']+348)) == (185,215,255),
                'second select option highlighted', 30)
            key('ret')
            screenshot('forms-edited')
            with requests_lock:
                before_form=len(requests)
            click(frame['x']+80,frame['y']+327)
            def submitted():
                with requests_lock:
                    return next((p for p in requests[before_form:] if p.startswith('/forms-native.html?')),None)
            wait(submitted,'native GET form submission')
            query=submitted()
            for part in ['q=a','p=p','note=x%0Ay%C5%82','check=on','r=two','s=second']:
                if part not in query:
                    raise AssertionError('Incorrect form control '+part.split('=',1)[0])
            result['native_forms_get_utf8']=request_for_log(query)
            time.sleep(2)
            navigate('theme-white.html')
            # Closing an image tab must preserve the previous document and remain interactive.
            key('ctrl','t'); time.sleep(.5); navigate('slow-image.html')
            wait(lambda: count('slow-image.png')>0,'delayed image request')
            move(frame['x']+300,frame['y']+250)
            hmp('mouse_move 0 0 1'); time.sleep(.5)
            screenshot('scroll-image-loading')
            key('ctrl','w'); time.sleep(6)
            move(1000,660)
            closed=screenshot('image-tab-closed')
            if closed.getpixel((frame['x']+20,frame['y']+frame['h']-45))!=(255,255,255):
                raise AssertionError('Image completion changed the surviving tab')
            result['close_image_tab_while_loading']=True
            key('ctrl','t'); time.sleep(.5); navigate('slow-css.html')
            wait(lambda: count('slow-css.css')>0,'delayed stylesheet request')
            key('ctrl','w'); time.sleep(6)
            move(1000,660)
            css_closed=screenshot('css-tab-closed')
            if css_closed.getpixel((frame['x']+20,frame['y']+frame['h']-45))!=(255,255,255):
                raise AssertionError('Stylesheet completion changed the surviving tab')
            result['close_css_tab_while_loading']=True
            navigate('theme-white.html')
            move(1000,660); time.sleep(.3)
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
            if page_hash(before_theme)!=page_hash(after_theme):
                raise AssertionError('Document pixels changed under the light desktop theme')
            result['live_theme'] = True
            result['white_page_theme_isolation'] = True
            navigate('theme-black.html')
            move(1000,660); time.sleep(.3)
            black_before=screenshot('black-theme-before')
            click(32,700); click(130,532); time.sleep(1)
            click(210,262); click(330,466)
            wait(lambda: max(screenshot('dark-theme-probe').getpixel((110,550)))<100,'dark desktop theme',30)
            click(784,114); move(1000,660); time.sleep(1)
            black_after=screenshot('black-theme-after')
            if page_hash(black_before)!=page_hash(black_after):
                raise AssertionError('Black document pixels changed under dark desktop theme')
            result['black_page_theme_isolation']=True
            # Start a delayed response, then cancel and navigate using the live GUI.
            navigate('slow-main.html')
            click(frame['x']+225,frame['y']+100) # native Stop
            navigate('theme-white.html'); time.sleep(5)
            move(1000,660)
            stopped=screenshot('stop-after-navigation')
            if stopped.getpixel((frame['x']+20,frame['y']+frame['h']-45))!=(255,255,255):
                raise AssertionError('Stale delayed HTML appeared after Stop/navigation')
            result['stop_and_stale_html']=True
            navigate('slow-main.html')
            click(frame['x']+frame['w']-16,frame['y']+14)
            time.sleep(7)
            move(80,28); hmp('mouse_button 2'); time.sleep(.2); hmp('mouse_button 0'); time.sleep(.5)
            click(105,43); time.sleep(2); key('a')
            screenshot('native-app-after-browser-close')
            health()
            result['window_close_during_request']=True
            health()
            with requests_lock:
                if any('must-not-run' in p for p in requests):
                    raise AssertionError('Script URL was requested without an engine')
            result.update(result='PASS', kernel_sha256=kernel_sha,
                          iso_sha256=iso_sha, http_requests=request_log(),
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
            (out/'http-requests.json').write_text(json.dumps(request_log(), indent=2)+'\n')
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
