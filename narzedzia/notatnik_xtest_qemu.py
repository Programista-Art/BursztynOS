#!/usr/bin/env python3
"""Jeden natywny przebieg QEMU w konfiguracji `make runuefi` z PRAWDZIWA sciezka klawiatury:
XTest (serwer X) -> okno SDL QEMU -> virtio-keyboard / PS/2 -> sterownik Bursztyn OS -> Notatnik.

Mysz i zrzuty ekranu ida przez QMP (nie sa przedmiotem testu). Dysk uzytkownika jest podlaczony
z snapshot=on: wirtualny_dysk.img nie jest modyfikowany. Przed KAZDYM zdarzeniem klawiatury skrypt
sprawdza, czy aktywnym oknem X jest okno tego QEMU (unikalny tytul z -name); inaczej przerywa,
aby klawisze nie trafily do innej aplikacji.

Uzycie: python3 narzedzia/notatnik_xtest_qemu.py KATALOG_WYJSCIOWY
"""
import ctypes, os, re, subprocess, sys, tempfile, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
from desktop_qemu_lib import Guest, Qmp  # noqa: E402

out = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / '.notatnik-xtest')
out.mkdir(parents=True, exist_ok=True)
NAZWA = 'bursztyn-xtest-%d' % os.getpid()
events, stalls = [], []
stop = threading.Event()
t0 = time.monotonic()


def note(msg):
    line = f'{time.monotonic() - t0:8.2f} s  {msg}'
    events.append(line)
    print(line, flush=True)


def pulse():
    last = time.monotonic()
    while not stop.wait(0.05):
        now = time.monotonic()
        if now - last > 1.0:
            stalls.append((round(last - t0, 2), round(now - last, 2)))
        last = now


# ---- XTest ---------------------------------------------------------------
x11 = ctypes.CDLL('libX11.so.6')
xtst = ctypes.CDLL('libXtst.so.6')
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
dpy = x11.XOpenDisplay(None)
if not dpy:
    sys.exit('brak polaczenia z serwerem X')
# Kody evdev (xmodmap tego hosta): 108 = ISO_Level3_Shift (AltGr ukladu pl), 37 = Control_L.
KC = dict(ctrl=37, altgr=108, shift=50, lalt=64, ret=36, bs=22, esc=9, home=110, end=115, down=116,
          up=111, left=113, right=114, spc=65, tab=23,
          a=38, b=56, c=54, d=40, e=26, f=41, g=42, h=43, i=31, j=44, k=45, l=46, m=58, n=57, o=32, p=33,
          q=24, r=27, s=39, t=28, u=30, v=55, w=25, x=53, y=29, z=52, dot=60, minus=20)
for d in '1234567890':
    KC[d] = 10 + ('1234567890'.index(d))
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


PL = 'acelnosxz'   # Polski programisty: AltGr + litera -> ą ć ę ł ń ó ś ź ż


def aktywne_okno():
    try:
        wid = subprocess.check_output(['xprop', '-root', '_NET_ACTIVE_WINDOW'], text=True).split()[-1]
        return subprocess.check_output(['xprop', '-id', wid, 'WM_NAME'], text=True)
    except Exception as exc:  # noqa: BLE001
        return f'? {exc}'


def upewnij_fokus():
    if _ekran_zablokowany():
        raise RuntimeError('ekran hosta jest zablokowany (wygaszacz) - przerwano, klawisze trafilyby do ekranu blokady')
    if NAZWA in aktywne_okno() and _fokus_wejscia_w_oknie(NAZWA):
        return
    subprocess.run(['wmctrl', '-a', NAZWA], check=False)
    time.sleep(0.6)
    if NAZWA not in aktywne_okno() or not _fokus_wejscia_w_oknie(NAZWA):
        raise RuntimeError('fokus wejscia X11 nie jest w oknie QEMU - przerwano, aby nie wysylac klawiszy gdzie indziej: '
                           + aktywne_okno().strip())


def x_key(code, down):
    upewnij_fokus()
    xtst.XTestFakeKeyEvent(dpy, code, 1 if down else 0, 0)
    x11.XSync(dpy, 0)
    time.sleep(0.07)


def nacisnij(*nazwy, hold=0.08):
    for n in nazwy:
        x_key(KC[n], True)
    time.sleep(hold)
    for n in reversed(nazwy):
        x_key(KC[n], False)
    time.sleep(0.12)


def pisz(tekst):
    for ch in tekst:
        if ch == ' ':
            nacisnij('spc')
        elif ch == '\n':
            nacisnij('ret')
        elif ch == '.':
            nacisnij('dot')
        elif ch == '-':
            nacisnij('minus')
        elif ch.isupper():
            nacisnij('shift', ch.lower())
        else:
            nacisnij(ch)


def altgr(litera, shift=False, ctrl=False):
    mods = (['ctrl'] if ctrl else []) + ['altgr'] + (['shift'] if shift else [])
    nacisnij(*mods, litera)


# ---- QEMU (argumenty jak `make runuefi`) -------------------------------------
if _ekran_zablokowany():
    sys.exit('ekran hosta jest zablokowany - odblokuj go przed testem (klawisze XTest trafilyby do ekranu blokady)')
th = threading.Thread(target=pulse, daemon=True)
th.start()
time.sleep(20)
if stalls:
    note(f'PRZERWANO: przestoje hosta przed testem {stalls}')
    (out / 'zdarzenia.txt').write_text('\n'.join(events) + '\n')
    sys.exit(2)
note('puls hosta 20 s bez przestojow, start QEMU (konfiguracja runuefi, dysk snapshot=on, SDL)')

tmp = Path(tempfile.mkdtemp(prefix='notatnik-xtest-'))
sock = tmp / 'qmp.sock'
serial = out / 'serial.log'
serial.write_text('')
cmd = ['qemu-system-x86_64', '-name', NAZWA, '-bios', '/usr/share/ovmf/OVMF.fd', '-machine', 'pc,hpet=on',
       '-cpu', 'max', '-accel', 'tcg,thread=single', '-smp', '1', '-m', '2G',
       '-audiodev', 'none,id=snd', '-device', 'intel-hda', '-device', 'hda-output,audiodev=snd',
       '-serial', 'file:' + str(serial), '-display', 'sdl', '-qmp', f'unix:{sock},server=on,wait=off',
       '-drive', f'id=disk,file={ROOT / "wirtualny_dysk.img"},format=raw,if=none,snapshot=on',
       '-device', 'virtio-blk-pci,drive=disk,disable-legacy=on',
       '-netdev', 'user,id=n1', '-device', 'virtio-net-pci,netdev=n1,disable-legacy=on',
       '-object', 'rng-builtin,id=rng0', '-device', 'virtio-rng-pci,rng=rng0,disable-legacy=on',
       '-vga', 'none', '-device', 'virtio-vga,disable-legacy=on,xres=1280,yres=720',
       '-device', 'virtio-keyboard-pci', '-device', 'virtio-mouse-pci', '-device', 'virtio-tablet-pci',
       '-boot', 'order=d', '-cdrom', str(ROOT / 'BursztynOS.iso'), '-no-reboot']
(out / 'qemu-cmd.txt').write_text(' '.join(cmd) + '\n')

# Guest jest uzywany tylko jako zestaw metod QMP (mysz, zrzuty, symbole); proces startujemy sami.
g = Guest.__new__(Guest)
g.out, g.build, g.tablet, g.timeout = out, ROOT, True, 1500
g.w, g.h = 1280, 720
g.serial_path = serial
g._symbols = subprocess.check_output(['nm', '-C', str(ROOT / 'system_operacyjny.bin')], text=True)
g.server = g.https_server = None
g.deadline = time.monotonic() + 1500
g.vm = subprocess.Popen(cmd, stdout=(out / 'qemu.log').open('w'), stderr=subprocess.STDOUT,
                        env=dict(os.environ, IBUS_ADDRESS='unix:path=/nonexistent/bursztyn-qemu-bez-ibus'))  # jak Makefile
g.sock, g.q, g.boot_started = sock, None, time.monotonic()
rc = 1


def czekaj_log(fragment, sekundy=30):
    g.wait(lambda: fragment in g.log(), fragment, sekundy, 0.3)


def liczba_w_logu(fragment):
    return g.log().count(fragment)


try:
    g.wait(lambda: '[DIAG APP] First frame submitted' in g.log(), 'pulpit', 900)
    note(f'pulpit po {round(time.monotonic() - g.boot_started, 1)} s')
    g.q = Qmp(sock)
    g.q.socket.settimeout(15)
    g._cursor = [g.symbol('mysz_x'), g.symbol('mysz_y')]
    g._ticks = g.symbol('licznik_tykniec_zegara')
    g._buttons = g.symbol('zaktualizuj_mysze_wspolne(long, long, unsigned char, char const*)::poprzednie_przyciski_gui')
    time.sleep(4)
    note('instalacja Notatnika: ' + ('OK' if '[BSP] Notatnik zainstalowany' in g.log() else 'BRAK/BLAD'))
    upewnij_fokus()
    note('fokus wejscia X11 w oknie QEMU, ekran odblokowany: ' + aktywne_okno().strip())
    g.screenshot('00-pulpit')

    # 1. Start -> "notatnik"; weryfikacja zrzutem (belka + pasek menu) przed jakimkolwiek pisaniem w edytorze.
    from PIL import Image, ImageChops, ImageStat
    wzorzec = os.environ.get('NOTATNIK_WZORZEC')

    def roznica(a, b, pole):
        return max(ImageStat.Stat(ImageChops.difference(a.convert('RGB').crop(pole), b.convert('RGB').crop(pole))).mean)

    for proba in range(2):
        g.click(32, g.h - 20)
        time.sleep(5)
        pisz('notatnik')
        time.sleep(1)
        g.screenshot('01a-wyszukiwanie-start-%d' % proba)
        nacisnij('ret')
        time.sleep(8)
        kadr = g.screenshot('01-notatnik-bez-paska-narzedzi')
        if not wzorzec:
            break
        with Image.open(wzorzec) as wz:
            r = roznica(kadr, wz, (104, 104, 770, 158))
        note('zgodnosc belki i menu z wzorcem Notatnika: %.1f (prog 12)' % r)
        if r < 12:
            break
        raise RuntimeError('na ekranie nie ma Notatnika - przerwano przed pisaniem w edytorze')
    note('PASS? Notatnik uruchomiony (zrzut 01: pasek menu, edytor bezposrednio pod nim)')

    # 2. Zwykle pisanie i 18 polskich liter przez AltGr (bez klikniecia: fokus startowy w edytorze).
    pisz('Abc 123 ')
    for l in PL:
        altgr(l)
    nacisnij('ret')
    for l in PL:
        altgr(l, shift=True)
    nacisnij('ret')
    pisz('Zaz'); altgr('o'); altgr('l'); altgr('c'); pisz(' g'); altgr('e'); altgr('s'); pisz('l'); altgr('a')
    pisz(' ja'); altgr('x'); altgr('n')
    nacisnij('ret')
    for l in PL:
        altgr(l, ctrl=True)       # wariant LCtrl+AltGr (np. VirtualBox na hoscie Windows)
    nacisnij('ret')
    time.sleep(2)
    g.screenshot('02-polskie-znaki')
    note('wpisano: "Abc 123 ąćęłńóśźż" / "ĄĆĘŁŃÓŚŹŻ" / "Zażółć gęślą jaźń" / (LCtrl+AltGr) "ąćęłńóśźż"')

    # 3. Fokus po otwarciu i zamknieciu menu Plik, Edycja, Pomoc (klik, Esc, pisanie bez klikniecia).
    for nazwa, x, znak in (('Plik', 126, '1'), ('Edycja', 185, '2'), ('Pomoc', 327, '3')):
        g.click(x, 143)
        time.sleep(1.5)
        g.screenshot('03-menu-%s' % nazwa.lower())
        nacisnij('esc')
        time.sleep(0.8)
        pisz(znak)
    altgr('z')
    time.sleep(1.5)
    g.screenshot('04-po-menu')
    note('po kazdym menu (klik, Esc) wpisano 1/2/3, na koncu AltGr+z')

    # 4. Okna podrzedne Pomocy (BWS 101/102); po zamknieciu pisanie bez klikniecia.
    def otworz_pomoc(ile_w_dol, nazwa_zrzutu):
        przed = liczba_w_logu('[OKNA-PODRZEDNE] utworzono')
        g.click(327, 143)
        time.sleep(1.5)
        for _ in range(ile_w_dol):
            nacisnij('down')
        nacisnij('ret')
        time.sleep(6)
        g.screenshot(nazwa_zrzutu)
        wpis = (re.findall(r'\[OKNA-PODRZEDNE\] utworzono[^\n]*', g.log()) or ['-'])[-1]
        return liczba_w_logu('[OKNA-PODRZEDNE] utworzono') - przed, wpis

    n, wpis = otworz_pomoc(1, '05-sterowanie')
    note('Pomoc > Sterowanie: nowych okien=%d (%s)' % (n, wpis))
    nacisnij('esc')
    time.sleep(3)
    nacisnij('ret'); pisz('po Sterowaniu '); altgr('a')
    n, wpis = otworz_pomoc(2, '06-o-notatniku')
    note('Pomoc > O Notatniku: nowych okien=%d (%s)' % (n, wpis))
    nacisnij('esc')
    time.sleep(3)
    pisz(' po O Notatniku '); altgr('z')
    time.sleep(1.5)
    g.screenshot('07-po-oknach-pomocy')
    note('log okien podrzednych: ' + ' | '.join(re.findall(r'\[OKNA-PODRZEDNE\] [a-z-]+ a=\d+ b=\d+ c=\d+', g.log())))

    # 5. Menu kontekstowe pod prawym przyciskiem (QMP), Esc, pisanie dalej.
    g.move(440, 470)
    g.q.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'right', 'down': True}}]})
    time.sleep(0.3)
    g.q.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'right', 'down': False}}]})
    time.sleep(2)
    g.screenshot('08-menu-kontekstowe')
    nacisnij('esc')
    time.sleep(1)
    nacisnij('ctrl', 'end')
    pisz(' k')
    time.sleep(1.5)
    przed_zapisem = g.screenshot('09-przed-zapisem')

    # 6. Zapisz (Ctrl+S -> Zapisz jako), Nowy, Otworz, porownanie tresci, Zapisz jako.
    nacisnij('ctrl', 's')
    time.sleep(3)
    g.screenshot('10-zapisz-jako')
    nacisnij('ctrl', 'a')
    pisz('xtest-'); altgr('z'); pisz('o'); altgr('l'); pisz('w')
    time.sleep(1)
    g.screenshot('11-nazwa-pliku')
    nacisnij('ret')
    time.sleep(4)
    zapisany = g.screenshot('12-zapisano')
    nacisnij('ctrl', 'n')
    time.sleep(3)
    g.screenshot('13-nowy-dokument')
    nacisnij('ctrl', 'o')
    time.sleep(3)
    g.screenshot('14-otworz')
    pisz('xtest-'); altgr('z'); pisz('o'); altgr('l'); pisz('w.txt')
    nacisnij('ret')
    time.sleep(4)
    ponownie = g.screenshot('15-otwarty-ponownie')
    pole_tekstu = (104, 165, 776, 570)
    note('roznica obszaru tekstu: zapisany vs ponownie otwarty = %.2f, przed zapisem vs ponownie = %.2f' % (
        roznica(zapisany, ponownie, pole_tekstu), roznica(przed_zapisem, ponownie, pole_tekstu)))
    nacisnij('ctrl', 'shift', 's')
    time.sleep(3)
    nacisnij('ctrl', 'a')
    pisz('kopia-'); altgr('s'); pisz('ciezki')
    nacisnij('ret')
    time.sleep(4)
    g.screenshot('16-zapisz-jako-kopia')
    rc = 0
except Exception as exc:  # noqa: BLE001 - zachowaj dowody
    note(f'BLAD: {type(exc).__name__}: {str(exc)[:600]}')
    try:
        g.screenshot('99-blad')
    except Exception:  # noqa: BLE001
        pass
finally:
    try:
        if g.q:
            g.q.call('quit')
        g.vm.wait(timeout=30)
    except Exception:  # noqa: BLE001
        g.vm.kill()
    stop.set()
    note(f'przestoje hosta (start s, dlugosc s): {stalls}')
    (out / 'zdarzenia.txt').write_text('\n'.join(events) + '\n')
sys.exit(rc)
