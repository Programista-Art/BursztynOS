#!/usr/bin/env python3
"""Boot the production kernel with two independent xHCI controllers and real USB HID.

Uses a disposable UEFI ISO and read-only VirtIO boot medium; no user data disk.
Controller 0 has a boot keyboard, controller 1 a boot mouse. QMP injects input,
checks per-controller decoded reports, then verifies visible cursor movement.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from test_hda_regression_qemu import Qmp
from test_virtio_input_qemu import parse_ppm, check_cursor_at

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--virtio-input', action='store_true', help='Check VirtIO keyboard/mouse with two idle xHCI controllers')
    args = parser.parse_args()
    out = ROOT / '.input-test' / ('qemu-virtio' if args.virtio_input else 'qemu')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'result.json').unlink(missing_ok=True)
    firmware = Path('/usr/share/ovmf/OVMF.fd')
    if not firmware.is_file():
        parser.error('Monolithic OVMF firmware unavailable: ' + str(firmware))
    kernel = ROOT / 'system_operacyjny.bin'
    kernel_sha256 = hashlib.sha256(kernel.read_bytes()).hexdigest()
    deadline = time.monotonic() + args.timeout
    with tempfile.TemporaryDirectory(prefix='bursztyn-usb-') as temporary:
        temporary = Path(temporary)
        boot = temporary / 'staging' / 'boot'
        (boot / 'grub').mkdir(parents=True)
        shutil.copy2(kernel, boot / kernel.name)
        shutil.copy2(ROOT / 'tapety.pak', boot / 'tapety.pak')
        (boot / 'grub' / 'grub.cfg').write_text(
            'set timeout=0\nset default=0\nmenuentry "USB multi controller regression" {\n'
            'insmod all_video\nset gfxmode=1920x1080x32,1280x720x32,auto\nset gfxpayload=keep\n'
            'multiboot2 /boot/system_operacyjny.bin\nmodule2 /boot/tapety.pak tapety.pak\nboot\n}\n')
        iso = temporary / 'usb.iso'
        with (out / 'iso-build.log').open('w') as log:
            subprocess.run(['grub-mkrescue', '-o', str(iso), str(boot.parent)],
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)
        serial = out / 'serial.log'
        serial.unlink(missing_ok=True)
        qmp_path = temporary / 'qmp.sock'
        command = ['qemu-system-x86_64', '-machine', 'pc,hpet=on' if args.virtio_input else 'q35', '-accel', 'tcg', '-cpu', 'max',
                   '-smp', '1', '-m', '2048', '-bios', str(firmware), '-display', 'none',
                   '-serial', 'file:' + str(serial), '-no-reboot', '-qmp', f'unix:{qmp_path},server=on,wait=off',
                   '-vga', 'none', '-device', 'virtio-vga,disable-legacy=on,xres=1280,yres=720',
                   '-drive', f'file={iso},id=boot,if=none,format=raw,readonly=on',
                   '-device', 'virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1',
                   '-device', 'qemu-xhci,id=usb0', '-device', 'qemu-xhci,id=usb1',
                   '-device', 'usb-kbd,id=keyboard0,bus=usb0.0',
                   '-device', 'usb-mouse,id=mouse1,bus=usb1.0']
        if args.virtio_input:
            command = command[:-4] + ['-device', 'virtio-keyboard-pci,disable-legacy=on',
                                     '-device', 'virtio-mouse-pci,disable-legacy=on']
        (out / 'argv.json').write_text(json.dumps(command, indent=2) + '\n')
        def evidence():
            return serial.read_text(errors='replace') if serial.exists() else ''
        def wait_for(predicate, description):
            while time.monotonic() < deadline:
                if predicate(evidence()):
                    return
                if process.poll() is not None:
                    raise RuntimeError(f'QEMU exited: {process.returncode}')
                time.sleep(0.1)
            raise RuntimeError('Timed out waiting for ' + description + '; see ' + str(serial))
        with (out / 'qemu.log').open('w') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
            qmp = None
            try:
                def devices_ready(text):
                    if '[SYSTEM] Pulpit gotowy.' not in text:
                        return False
                    if args.virtio_input:
                        return all(re.search(r'\[INPUT REGISTER\].*bus=VIRTIO.*kinds=0x' + kind, text)
                                   for kind in ('1', '2'))
                    return all(re.search(r'\[USB HID\] controller=' + str(index) + r'.*result=interrupt IN armed', text)
                               for index in range(2))
                wait_for(devices_ready, 'desktop and input devices armed')
                qmp = Qmp(qmp_path)
                mice = qmp.call('query-mice')
                (out / 'mice.json').write_text(json.dumps(mice, indent=2) + '\n')
                mouse = next(mouse for mouse in mice['return'] if ('Virtio' in mouse['name'] if args.virtio_input else 'USB' in mouse['name'] or 'HID' in mouse['name']))
                result = qmp.call('human-monitor-command', {'command-line': f'mouse_set {mouse["index"]}'})
                if 'error' in result:
                    raise RuntimeError(str(result))
                before, after = out / 'before.ppm', out / 'after.ppm'
                # Desktop-ready precedes the end of the visible splash. Wait
                # for the real cursor at the production default position.
                while time.monotonic() < deadline:
                    qmp.call('screendump', {'filename': str(before)})
                    w, h, pixels = parse_ppm(before)
                    if check_cursor_at(pixels, w, h, 500, 300)[0]:
                        break
                    time.sleep(0.25)
                else:
                    raise RuntimeError('Visible desktop cursor unavailable after splash')
                # Observe the real common keyboard modifier state. QMP only
                # injects hardware input; it never calls a guest decoder/tick.
                symbols = subprocess.check_output(['nm', '-C', str(kernel)], text=True)
                symbol = re.search(r'^([0-9a-fA-F]+) [bBdD] \(anonymous namespace\)::lewy_shift$', symbols, re.M)
                if not symbol:
                    raise RuntimeError('Common keyboard modifier symbol unavailable')
                address = int(symbol.group(1), 16)
                ps2_symbol = re.search(r'^([0-9a-fA-F]+) [bBdD] \(anonymous namespace\)::s_ps2_keys_down$', symbols, re.M)
                if not ps2_symbol:
                    raise RuntimeError('PS/2 source verification symbol unavailable')
                ps2_shift_address = int(ps2_symbol.group(1), 16) + 42
                def read_kernel_byte(physical):
                    reply = qmp.call('human-monitor-command', {'command-line': f'xp /1bx 0x{physical:x}'})
                    value = re.search(r':\s*0x([0-9a-fA-F]+)', reply.get('return', ''))
                    if not value:
                        raise RuntimeError('Cannot observe common keyboard state: ' + str(reply))
                    return bool(int(value.group(1), 16))
                def shift_state():
                    return read_kernel_byte(address)
                if shift_state():
                    raise RuntimeError('Shift is already held before keyboard test')
                for key in ('shift', 'a'):
                    for down in (True, False):
                        result = qmp.call('input-send-event', {'events': [
                            {'type': 'key', 'data': {'key': {'type': 'qcode', 'data': key}, 'down': down}}]})
                        if 'error' in result:
                            raise RuntimeError(str(result))
                        if key == 'shift':
                            wait_for(lambda _: shift_state() == down, 'common keyboard modifier transition')
                            if read_kernel_byte(ps2_shift_address):
                                raise RuntimeError('Keyboard input unexpectedly arrived through PS/2')
                        time.sleep(0.15)
                result = qmp.call('input-send-event', {'events': [
                    {'type': 'rel', 'data': {'axis': 'x', 'value': 20}},
                    {'type': 'rel', 'data': {'axis': 'y', 'value': 10}}]})
                if 'error' in result:
                    raise RuntimeError(str(result))
                pointer_pattern = r'\[MOUSE BOUNDS\] source=' + re.escape(mouse['name'] if args.virtio_input else 'usb-mouse')
                wait_for(lambda text: re.search(pointer_pattern, text), 'pointer publication')
                if not args.virtio_input:
                    wait_for(lambda text: re.search(r'\[USB HID\] controller=0.*result=keyboard report decoded', text) and
                             re.search(r'\[USB HID\] controller=1.*result=pointer report decoded', text), 'decoded USB reports')
                time.sleep(1)
                qmp.call('screendump', {'filename': str(after)})
                width, height, pixels = parse_ppm(before)
                width2, height2, moved = parse_ppm(after)
                if (width, height) != (width2, height2):
                    raise RuntimeError('Display geometry changed during USB input')
                bounds = re.search(pointer_pattern + r' .*cur=(\d+),(\d+) .*res=(\d+),(\d+)', evidence())
                if not bounds:
                    raise RuntimeError('Selected pointer publication record missing')
                cx, cy, rx, ry = map(int, bounds.groups())
                if (rx, ry) != (cx + 20, cy + 10):
                    raise RuntimeError('Selected pointer publication delta is incorrect')
                if not check_cursor_at(pixels, width, height, cx, cy)[0]:
                    raise RuntimeError(f'Initial visible cursor missing at {cx},{cy}')
                if not check_cursor_at(moved, width, height, cx + 20, cy + 10)[0]:
                    raise RuntimeError('Selected mouse report did not move the visible GUI cursor by 20,10')
                text = evidence()
                for index in range(2):
                    if not re.search(r'\[xHCI controller\] index=' + str(index) + r'.*result=ready', text):
                        raise RuntimeError(f'Controller {index} did not initialize')
                result = {'result': 'PASS', 'kernel_sha256': kernel_sha256,
                          'controllers': 2, 'input_transport': 'virtio' if args.virtio_input else 'usb',
                          'keyboard_modifier_verified': True, 'keyboard_non_ps2_verified': True,
                          'keyboard_controller': None if args.virtio_input else 0, 'mouse_controller': None if args.virtio_input else 1,
                          'cursor_before': [cx, cy], 'cursor_after': [cx + 20, cy + 10]}
                (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
                print('Actual QEMU two-xHCI ' + ('VirtIO' if args.virtio_input else 'USB') + ' keyboard/mouse/GUI: PASS', flush=True)
                print(json.dumps(result), flush=True)
            finally:
                if qmp:
                    qmp.close()
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)

if __name__ == '__main__':
    main()
