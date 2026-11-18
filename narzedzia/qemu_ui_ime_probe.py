#!/usr/bin/env python3
"""Probnik frontendu QEMU (GTK/SDL) z IBus i bez IBus - BEZ systemu goscia.

QEMU startuje bez dysku i ISO (SeaBIOS: brak nosnika; NIE z -S, bo zatrzymana maszyna odrzuca wejscie
przed trace), a trace `input_event_key_qcode`
pokazuje dokladnie te klawisze, ktore warstwa UI QEMU przekazalaby gosciowi. Klawisze sa wysylane przez
XTest do okna QEMU (prawdziwa sciezka serwera X). Oslona: ekran odblokowany i faktyczny fokus wejscia X11
w oknie QEMU przed KAZDYM klawiszem; inaczej przerwanie bez wysylania.

Uzycie: python3 narzedzia/qemu_ui_ime_probe.py [gtk|sdl ...]
"""
import ctypes, os, re, subprocess, sys, tempfile, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sdl_ime_probe import _ekran_zablokowany, _fokus_wejscia_w_oknie  # noqa: E402

IBUS_OFF = 'unix:path=/nonexistent/bursztyn-qemu-bez-ibus'
KROKI = [('a', [38]), ('AltGr+a (ą)', [108, 38]), ('AltGr+s (ś)', [108, 39]),
         ('Shift+AltGr+z (Ż)', [50, 108, 52]), ('LCtrl+AltGr+e (ę)', [37, 108, 26]), ('Enter', [36])]

x11 = ctypes.CDLL('libX11.so.6')
xtst = ctypes.CDLL('libXtst.so.6')
x11.XOpenDisplay.restype = ctypes.c_void_p
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
dpy = x11.XOpenDisplay(None)


def wariant(display, ibus_wylaczony):
    nazwa = 'bursztyn-ui-probe-%d-%s-%d' % (os.getpid(), display, int(ibus_wylaczony))
    tmp = Path(tempfile.mkdtemp(prefix='qemu-ui-probe-'))
    trace = tmp / 'trace.log'
    env = dict(os.environ)
    if ibus_wylaczony:
        env['IBUS_ADDRESS'] = IBUS_OFF
    cmd = ['qemu-system-x86_64', '-name', nazwa, '-machine', 'pc', '-nodefaults', '-m', '64',
           '-vga', 'std', '-display', display, '-monitor', 'none',
           '-trace', 'input_event_key_qcode', '-D', str(trace)]
    vm = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    wynik = {'display': display, 'ibus': 'odlaczony' if ibus_wylaczony else 'aktywny', 'kroki': [], 'blad': None}
    try:
        time.sleep(3)
        subprocess.run(['wmctrl', '-a', nazwa], check=False)
        time.sleep(1)
        for opis, klawisze in KROKI:
            if _ekran_zablokowany() or not _fokus_wejscia_w_oknie(nazwa):
                wynik['blad'] = 'ekran zablokowany albo fokus X11 poza oknem QEMU - przerwano'
                break
            przed = trace.read_text(errors='replace') if trace.exists() else ''
            for k in klawisze:
                xtst.XTestFakeKeyEvent(dpy, k, 1, 0); x11.XSync(dpy, 0); time.sleep(0.06)
            time.sleep(0.08)
            for k in reversed(klawisze):
                xtst.XTestFakeKeyEvent(dpy, k, 0, 0); x11.XSync(dpy, 0); time.sleep(0.06)
            time.sleep(0.5)
            po = trace.read_text(errors='replace') if trace.exists() else ''
            nowe = po[len(przed):]
            wcisniete = re.findall(r'qcode (\S+), down 1', nowe)
            wynik['kroki'].append((opis, wcisniete, nowe.strip()[:300]))
    finally:
        vm.terminate()
        try:
            vm.wait(timeout=10)
        except subprocess.TimeoutExpired:
            vm.kill()
    return wynik


if __name__ == '__main__':
    if _ekran_zablokowany():
        sys.exit('ekran hosta jest zablokowany - odblokuj go przed testem')
    frontendy = sys.argv[1:] or ['gtk', 'sdl']
    for d in frontendy:
        for wyl in (False, True):
            w = wariant(d, wyl)
            print('== QEMU -display %s, IBus %s' % (w['display'], w['ibus']))
            if w['blad']:
                print('  BLAD:', w['blad'])
            for opis, keys, surowe in w['kroki']:
                print('  %-20s klawisze do goscia (down): %s' % (opis, ', '.join(keys) or '-'))
            if w['kroki'] and not any(k for _, k, _ in w['kroki']):
                print('  surowy slad (1. krok):', w['kroki'][0][2] or '(pusty)')
