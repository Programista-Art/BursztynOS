#!/usr/bin/env python3
"""Boot a temporary BTP2 ISO and retain serial/QMP evidence under --workdir."""
import argparse
import json
from pathlib import Path
import re
import socket
import subprocess
import time


def qmp(path, command, arguments=None):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(10)
        connection.connect(str(path))
        stream = connection.makefile('rwb', buffering=0)
        stream.readline()
        for request in ({'execute': 'qmp_capabilities'},
                        {'execute': command, **({'arguments': arguments} if arguments else {})}):
            stream.write((json.dumps(request) + '\n').encode())
            while True:
                answer = json.loads(stream.readline())
                if 'error' in answer:
                    raise RuntimeError(answer)
                if 'return' in answer:
                    result = answer['return']
                    break
        return result


def wait_for_serial(path, marker, count=1, timeout=120):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        content = path.read_text(errors='replace') if path.exists() else ''
        if content.count(marker) >= count:
            return content
        time.sleep(1)
    raise RuntimeError(f'guest did not emit {marker!r} {count} time(s); inspect {path}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--workdir', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--switches', action='store_true')
    parser.add_argument('--inspect-menu', action='store_true')
    parser.add_argument('--automated', action='store_true')
    args = parser.parse_args()
    directory = args.workdir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    disk = directory / 'disk.img'
    if not disk.exists():
        with disk.open('wb') as out:
            out.truncate(4 * 1024 * 1024 * 1024)
    serial = directory / 'serial.log'
    socket_path = directory / 'qmp.sock'
    screenshot = directory / 'desktop.png'
    socket_path.unlink(missing_ok=True)
    serial.unlink(missing_ok=True)
    command = ['qemu-system-x86_64', '-machine', 'pc,hpet=on', '-cpu', 'max',
               '-accel', 'tcg,thread=single', '-m', '2G', '-vga', 'std',
               '-global', 'VGA.vgamem_mb=64', '-display', 'none',
               '-drive', f'id=disk,file={disk},format=raw,if=none',
               '-device', 'ich9-ahci,id=ahci', '-device', 'ide-hd,drive=disk,bus=ahci.0,unit=0',
               '-device', 'qemu-xhci,id=xhci', '-device', 'usb-mouse,id=usbmouse,bus=xhci.0',
               '-serial', f'file:{serial}', '-qmp', f'unix:{socket_path},server=on,wait=off',
               '-cdrom', str(args.iso.resolve()), '-boot', 'd']
    with (directory / 'qemu.log').open('wb') as log:
        proc = subprocess.Popen(command, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline and proc.poll() is None:
                content = serial.read_text(errors='replace') if serial.exists() else ''
                if args.automated and content.count('[TAPETY BTP] PASS') == 8:
                    if '[TAPETY BTP] FAIL' in content or 'PF/' in content or '[PANIC]' in content:
                        raise RuntimeError('automated BTP2 wallpaper group failed')
                    if ('[TAPETA TEST] Zmian z 100: 100' not in content or
                        '[PMM] Pamiec fizyczna modulu tapety.pak zwolniona' not in content):
                        raise RuntimeError('missing wallpaper stress or module-release evidence')
                    before = re.search(r'\[TAPETA TEST\] Heap przed: (\d+)', content)
                    after = re.search(r'\[TAPETA TEST\] Heap po: (\d+)', content)
                    if not before or not after or before.group(1) != after.group(1):
                        raise RuntimeError('wallpaper stress heap delta is nonzero')
                    print(f'BTP2 automated wallpaper QEMU PASS: {serial}')
                    return
                if not args.automated and 'System operacyjny gotowy!' in content:
                    qmp(socket_path, 'screendump', {'filename': str(screenshot), 'format': 'png'})
                    if '[TAPETA] Tapeta gotowa.' not in content:
                        raise RuntimeError('desktop ready without wallpaper-ready marker')
                    if 'PF/' in content or '[PANIC]' in content:
                        raise RuntimeError('page fault or panic in serial log')
                    if args.switches or args.inspect_menu:
                        from qemu_etap3 import Qmp
                        guest = Qmp(directory)
                        mice = guest.hmp('info mice')
                        print(f'QEMU mice: {mice}', flush=True)
                        mouse = re.search(r'Mouse #(\d+):\s+QEMU HID Mouse', mice)
                        if mouse:
                            guest.hmp(f'mouse_set {mouse.group(1)}')
                        guest.click(50, 700)
                        guest.capture(directory / 'start_menu.png')
                        if args.inspect_menu:
                            print(f'Start menu captured: {directory / "start_menu.png"}')
                            return
                        # At 1280x720, Tapety is Start-menu entry 11 (not
                        # entry 14, Motywy Systemowe).
                        guest.click(50, 507)
                        wait_for_serial(serial, '[TAPETY APP] Miniatury gotowe')
                        guest.capture(directory / 'selector.png')
                        guest.click(450, 170)
                        guest.click(250, 565)
                        wait_for_serial(serial, '[TAPETA] Wybrano /tapety/bursztyn02.btp')
                        guest.capture(directory / 'wallpaper02.png')
                        guest.click(280, 170)
                        guest.click(250, 565)
                        wait_for_serial(serial, '[TAPETA] Wybrano /tapety/bursztyn01.btp', count=2)
                        guest.capture(directory / 'wallpaper01.png')
                        content = serial.read_text(errors='replace')
                        if '[TAPETA] Wybrano /tapety/bursztyn02.btp' not in content or content.count('[TAPETA] Wybrano /tapety/bursztyn01.btp') < 2:
                            raise RuntimeError('wallpaper switches did not complete')
                        if 'PF/' in content or '[PANIC]' in content:
                            raise RuntimeError('page fault or panic after switches')
                    print(f'BTP2 QEMU PASS: {serial} {screenshot}')
                    return
                time.sleep(1)
            if socket_path.exists():
                qmp(socket_path, 'screendump', {'filename': str(screenshot), 'format': 'png'})
            raise RuntimeError(f'guest did not reach desktop; inspect {serial} and {screenshot}')
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == '__main__':
    main()
