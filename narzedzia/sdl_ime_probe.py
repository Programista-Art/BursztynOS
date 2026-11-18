#!/usr/bin/env python3
"""Probnik warstwy hosta (bez QEMU): ktore zdarzenia SDL_KEYDOWN dostaje okno SDL2 dla klawiszy wyslanych
przez XTest (prawdziwa sciezka serwera X, jak przy pisaniu w oknie QEMU -display sdl).

QEMU (ui/sdl2) przekazuje do goscia WYLACZNIE SDL_KEYDOWN/SDL_KEYUP. Jesli IME (IBus) "obsluzy" klawisz,
SDL2 nie generuje SDL_KEYDOWN i gosc nigdy nie dostaje tego klawisza.

Uzycie: python3 narzedzia/sdl_ime_probe.py            (oba warianty: z IBus i bez IBus)
        python3 narzedzia/sdl_ime_probe.py --dziecko   (wewnetrzne: jedno okno, wynik na stdout)
"""
import ctypes, json, os, subprocess, sys, time

SEKWENCJA = [  # (opis, klawisze evdev: modyfikatory + klawisz)
    ('n', [57]), ('o', [32]), ('t', [28]),
    ('AltGr+a (ą)', [108, 38]), ('AltGr+s (ś)', [108, 39]), ('AltGr+z (ż)', [108, 52]),
    ('Shift+AltGr+l (Ł)', [50, 108, 46]), ('LCtrl+AltGr+e (ę)', [37, 108, 26]),
    ('Enter', [36]), ('x', [53]),
]
NAZWA = 'sdl-ime-probe-%d' % os.getpid()


# ---- Osłona: klawisze XTest trafiaja do okna z FAKTYCZNYM fokusem wejscia X11 (nie do _NET_ACTIVE_WINDOW).
# Zablokowany ekran (cinnamon-screensaver) przechwytuje klawiature: wtedy nie wolno wysylac nic.
def _ekran_zablokowany():
    try:
        out = subprocess.run(['cinnamon-screensaver-command', '-q'], capture_output=True, text=True, timeout=5).stdout
    except Exception:  # noqa: BLE001
        return False
    return ('aktywny' in out and 'nieaktywny' not in out) or (' is active' in out)


def _fokus_wejscia_w_oknie(nazwa):
    """True, gdy okno z fokusem wejscia X11 (lub jego przodek) ma tytul zawierajacy `nazwa`."""
    _x = ctypes.CDLL('libX11.so.6')
    _x.XOpenDisplay.restype = ctypes.c_void_p
    _x.XGetInputFocus.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int)]
    _x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                              ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint)]
    _x.XFree.argtypes = [ctypes.c_void_p]
    _x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    d = _x.XOpenDisplay(None)
    if not d:
        return False
    try:
        w, r = ctypes.c_ulong(), ctypes.c_int()
        _x.XGetInputFocus(d, ctypes.byref(w), ctypes.byref(r))
        okno = w.value
        for _ in range(16):
            if okno in (0, 1):
                return False
            nazwy = subprocess.run(['xprop', '-id', hex(okno), 'WM_NAME'], capture_output=True, text=True).stdout
            if nazwa in nazwy:
                return True
            root, parent, dzieci, n = ctypes.c_ulong(), ctypes.c_ulong(), ctypes.c_void_p(), ctypes.c_uint()
            if not _x.XQueryTree(d, okno, ctypes.byref(root), ctypes.byref(parent), ctypes.byref(dzieci), ctypes.byref(n)):
                return False
            if dzieci.value:
                _x.XFree(dzieci)
            if parent.value == root.value:
                return False
            okno = parent.value
        return False
    finally:
        _x.XCloseDisplay(d)



def dziecko():
    sdl = ctypes.CDLL('libSDL2-2.0.so.0')
    x11 = ctypes.CDLL('libX11.so.6')
    xtst = ctypes.CDLL('libXtst.so.6')
    x11.XOpenDisplay.restype = ctypes.c_void_p
    xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    sdl.SDL_CreateWindow.restype = ctypes.c_void_p
    sdl.SDL_CreateWindow.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint32]
    sdl.SDL_PollEvent.argtypes = [ctypes.c_void_p]
    sdl.SDL_GetScancodeName.restype = ctypes.c_char_p
    if sdl.SDL_Init(0x20) != 0:
        print(json.dumps({'blad': 'SDL_Init'})); return
    okno = sdl.SDL_CreateWindow(NAZWA.encode(), 50, 50, 320, 120, 0x4)  # SHOWN
    dpy = x11.XOpenDisplay(None)
    ev = ctypes.create_string_buffer(64)
    zdarzenia = []

    def pompuj(sek):
        koniec = time.monotonic() + sek
        while time.monotonic() < koniec:
            while sdl.SDL_PollEvent(ev):
                typ = int.from_bytes(ev.raw[0:4], 'little')
                if typ in (0x300, 0x301):
                    sc = int.from_bytes(ev.raw[16:20], 'little', signed=True)
                    zdarzenia.append(('DOWN' if typ == 0x300 else 'UP', sdl.SDL_GetScancodeName(sc).decode() or str(sc)))
                elif typ == 0x303:
                    zdarzenia.append(('TEXT', ev.raw[12:44].split(b'\0')[0].decode('utf-8', 'replace')))
            time.sleep(0.005)

    def aktywne():
        try:
            wid = subprocess.check_output(['xprop', '-root', '_NET_ACTIVE_WINDOW'], text=True).split()[-1]
            return subprocess.check_output(['xprop', '-id', wid, 'WM_NAME'], text=True)
        except Exception:  # noqa: BLE001
            return ''

    pompuj(1.0)
    subprocess.run(['wmctrl', '-a', NAZWA], check=False)
    pompuj(1.0)
    wynik = {'ime': os.environ.get('IBUS_ADDRESS', '(domyslny IBus)'), 'kroki': []}
    for opis, klawisze in SEKWENCJA:
        if _ekran_zablokowany() or NAZWA not in aktywne() or not _fokus_wejscia_w_oknie(NAZWA):
            wynik['blad'] = 'ekran zablokowany albo fokus wejscia X11 poza oknem probnika - przerwano'
            break
        zdarzenia.clear()
        for k in klawisze:
            xtst.XTestFakeKeyEvent(dpy, k, 1, 0); x11.XSync(dpy, 0); pompuj(0.06)
        pompuj(0.08)
        for k in reversed(klawisze):
            xtst.XTestFakeKeyEvent(dpy, k, 0, 0); x11.XSync(dpy, 0); pompuj(0.06)
        pompuj(0.4)
        wynik['kroki'].append({'klawisz': opis, 'keydown': [n for t, n in zdarzenia if t == 'DOWN'],
                               'text': [n for t, n in zdarzenia if t == 'TEXT']})
    sdl.SDL_DestroyWindow(ctypes.c_void_p(okno))
    sdl.SDL_Quit()
    print(json.dumps(wynik, ensure_ascii=False))


if __name__ == '__main__':
    if _ekran_zablokowany():
        sys.exit('ekran hosta jest zablokowany - odblokuj go przed testem')
    if '--dziecko' in sys.argv:
        dziecko()
        sys.exit(0)
    for wariant, env in (('IBus aktywny (jak make runuefi)', dict(os.environ)),
                         ('IBus odlaczony (IBUS_ADDRESS=unix:path=/nonexistent)',
                          dict(os.environ, IBUS_ADDRESS='unix:path=/nonexistent'))):
        out = subprocess.run([sys.executable, __file__, '--dziecko'], env=env, capture_output=True, text=True, timeout=60)
        print('==', wariant)
        try:
            w = json.loads(out.stdout.strip().splitlines()[-1])
        except Exception:  # noqa: BLE001
            print(out.stdout, out.stderr); continue
        if 'blad' in w:
            print('  BLAD:', w['blad'])
        for k in w['kroki']:
            print('  %-20s KEYDOWN=%-28s TEXT=%s' % (k['klawisz'], ','.join(k['keydown']) or '-', ''.join(k['text']) or '-'))
