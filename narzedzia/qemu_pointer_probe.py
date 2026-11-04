#!/usr/bin/env python3
"""Read-only, bounded QMP/Windows viewport observer. Never injects guest input.
Export symbols on the build host; use the exact kernel's JSON on Windows.
Serial raw REL/ABS samples require BURSZTYNDIAGABSMAP=1 in the booted ISO.
"""
import argparse
import codecs
import ctypes
import hashlib
import json
import platform
import re
import socket
import struct
import subprocess
import time
from pathlib import Path


class Qmp:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=5)
        self.reader = self.socket.makefile('rb')
        self.greeting = json.loads(self.reader.readline())
        self.call('qmp_capabilities')

    def call(self, command, arguments=None):
        self.socket.sendall((json.dumps({'execute': command, 'arguments': arguments or {}})+'\n').encode())
        while True:
            line = self.reader.readline()
            if not line:
                raise EOFError('QMP disconnected')
            reply = json.loads(line)
            if 'error' in reply:
                raise RuntimeError(reply['error'])
            if 'return' in reply:
                return reply['return']

    def read(self, address, length):
        text = self.call('human-monitor-command', {'command-line': f'xp /{length}bx 0x{address:x}'})
        data = []
        for line in text.splitlines():
            if ':' in line:
                data.extend(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', line.split(':', 1)[1]))
        if len(data) != length:
            raise ValueError(('short physical read', text))
        return bytes(data)

    def close(self):
        self.reader.close()
        self.socket.close()


def export_symbols(kernel):
    symbols = {}
    for line in subprocess.check_output(['nm', '-C', str(kernel)], text=True).splitlines():
        parts = line.split(maxsplit=2)
        if len(parts) == 3 and parts[2] in ('mysz_x', 'mysz_y', 'committed_geometry'):
            symbols[parts[2]] = int(parts[0], 16)
    if len(symbols) != 3:
        raise ValueError('Expected cursor and committed geometry symbols')
    return {'kernel_sha256': hashlib.sha256(kernel.read_bytes()).hexdigest(), 'symbols': symbols}


def guest_snapshot(q, symbols):
    x, y = (struct.unpack('<i', q.read(symbols[n], 4))[0] for n in ('mysz_x', 'mysz_y'))
    g = struct.unpack('<IIiiI10iI', q.read(symbols['committed_geometry'], 64))
    return dict(x=x, y=y, version=g[0], generation=g[1], width=g[2], height=g[3],
                scale=g[4], cursor_max_x=g[13], cursor_max_y=g[14])


def windows_snapshot(pid):
    if platform.system() != 'Windows':
        return {'available': False, 'reason': 'Windows observer requires Windows'}
    from ctypes import wintypes as w
    user = ctypes.WinDLL('user32', use_last_error=True)
    # Observer only. A thread override also works when Python's manifest has
    # already selected process awareness; record failure instead of assuming it.
    previous_dpi_context = None
    dpi_aware = False
    if hasattr(user, 'SetThreadDpiAwarenessContext'):
        user.SetThreadDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        user.SetThreadDpiAwarenessContext.restype = ctypes.c_void_p
        previous_dpi_context = user.SetThreadDpiAwarenessContext(ctypes.c_void_p(-4))
        dpi_aware = bool(previous_dpi_context)
    elif hasattr(user, 'SetProcessDpiAwarenessContext'):
        user.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        dpi_aware = bool(user.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)))
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    class GuiThreadInfo(ctypes.Structure):
        _fields_ = [('cbSize', w.DWORD), ('flags', w.DWORD),
                    ('active', w.HWND), ('focus', w.HWND), ('capture', w.HWND),
                    ('menu_owner', w.HWND), ('move_size', w.HWND), ('caret', w.HWND),
                    ('caret_rect', w.RECT)]
    user.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user.GetWindowThreadProcessId.restype = w.DWORD
    user.GetGUIThreadInfo.argtypes = [w.DWORD, ctypes.POINTER(GuiThreadInfo)]
    user.GetClipCursor.argtypes = [ctypes.POINTER(w.RECT)]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
    user.GetClientRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
    user.ClientToScreen.argtypes = [w.HWND, ctypes.POINTER(w.POINT)]
    user.GetCursorPos.argtypes = [ctypes.POINTER(w.POINT)]
    user.IsZoomed.argtypes = [w.HWND]
    if hasattr(user, 'GetDpiForWindow'):
        user.GetDpiForWindow.argtypes = [w.HWND]
        user.GetDpiForWindow.restype = w.UINT
    windows = []

    @callback_type
    def visit(hwnd, _):
        if len(windows) >= 64:
            return False
        owner = w.DWORD()
        thread = user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value != pid:
            return True
        title = ctypes.create_unicode_buffer(512)
        user.GetWindowTextW(hwnd, title, len(title))
        outer, client, origin = w.RECT(), w.RECT(), w.POINT()
        user.GetWindowRect(hwnd, ctypes.byref(outer))
        user.GetClientRect(hwnd, ctypes.byref(client))
        user.ClientToScreen(hwnd, ctypes.byref(origin))
        info = GuiThreadInfo();info.cbSize = ctypes.sizeof(info)
        has_info = bool(user.GetGUIThreadInfo(thread, ctypes.byref(info)))
        windows.append(dict(title=title.value, outer=[outer.left, outer.top, outer.right, outer.bottom],
                            client_origin=[origin.x, origin.y], client_size=[client.right, client.bottom],
                            capture_window=hex(info.capture) if has_info and info.capture else None,
                            capture_query_succeeded=has_info,
                            dpi=user.GetDpiForWindow(hwnd) if hasattr(user, 'GetDpiForWindow') else None,
                            maximized=bool(user.IsZoomed(hwnd))))
        return True
    user.EnumWindows(visit, 0)
    cursor = w.POINT()
    user.GetCursorPos(ctypes.byref(cursor))
    clip = w.RECT()
    has_clip = bool(user.GetClipCursor(ctypes.byref(clip)))
    if previous_dpi_context:
        user.SetThreadDpiAwarenessContext(previous_dpi_context)
    return dict(available=True, dpi_measurement_override=dpi_aware, cursor=[cursor.x, cursor.y], windows=windows,
                host_cursor_clip=[clip.left, clip.top, clip.right, clip.bottom] if has_clip else None,
                note='Client includes frontend chrome; it is not asserted to be the guest drawable')


def serial_decoder(path):
    # Windows PowerShell 5 Tee-Object writes UTF-16; PowerShell 7 uses UTF-8.
    with path.open('rb') as stream:
        prefix = stream.read(3)
    encoding = 'utf-16-le' if prefix.startswith(b'\xff\xfe') else 'utf-16-be' if prefix.startswith(b'\xfe\xff') else 'utf-8-sig'
    return codecs.getincrementaldecoder(encoding)(errors='replace')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--export-symbols', type=Path)
    p.add_argument('--symbols', type=Path)
    p.add_argument('--port', type=int, default=4444)
    p.add_argument('--pid', type=int, help='Actual Windows qemu-system-x86_64.exe PID')
    p.add_argument('--stage', default='windowed')
    p.add_argument('--seconds', type=int, default=30, choices=range(1, 601), metavar='1..600')
    p.add_argument('--serial-log', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.export_symbols:
        args.output.write_text(json.dumps(export_symbols(args.export_symbols), indent=2)+'\n')
        return
    if not args.symbols:
        p.error('--symbols is required when collecting')
    if platform.system() == 'Windows' and not args.pid:
        p.error('--pid must identify the QEMU process on Windows')
    manifest = json.loads(args.symbols.read_text())
    q = Qmp(args.port)
    serial_offset = args.serial_log.stat().st_size if args.serial_log and args.serial_log.exists() else 0
    decoder = serial_decoder(args.serial_log) if serial_offset else None
    serial_pending = ''
    minimum_y = None
    try:
        with args.output.open('w', encoding='utf8') as out:
            out.write(json.dumps(dict(type='header', host=platform.platform(), stage=args.stage,
                symbols=manifest, qemu=q.greeting, mice=q.call('query-mice'),
                instructions='Move across all four drawable corners and center; repeat each window/fullscreen state.'))+'\n')
            end = time.monotonic()+args.seconds
            while time.monotonic() < end:
                guest = guest_snapshot(q, manifest['symbols'])
                minimum_y = guest['y'] if minimum_y is None else min(minimum_y, guest['y'])
                raw = []
                if args.serial_log and args.serial_log.exists():
                    if decoder is None:
                        decoder = serial_decoder(args.serial_log)
                    with args.serial_log.open('rb') as log:
                        log.seek(serial_offset)
                        chunk = log.read(65536)
                        serial_offset = log.tell()
                    serial_pending += decoder.decode(chunk)
                    complete = serial_pending.split('\n')
                    serial_pending = complete.pop()[-65536:]
                    raw = [line.rstrip('\r') for line in complete
                           if line.startswith(('[ABS MAP]', '[ABS GEOM]', '[INPUT CAPS]'))]
                out.write(json.dumps(dict(type='sample', time_ns=time.time_ns(), stage=args.stage,
                    guest=guest, observed_min_y=minimum_y, mice=q.call('query-mice'),
                    host=windows_snapshot(args.pid), raw=raw))+'\n')
                out.flush()
                time.sleep(0.25)  # Observer cadence only; never changes input timing.
            out.write(json.dumps(dict(type='summary', observed_min_y=minimum_y,
                reached_top=minimum_y == 0, verdict='Measurement only; requires correct kernel/ISO and real host movement.'))+'\n')
    finally:
        q.close()


if __name__ == '__main__':
    main()
