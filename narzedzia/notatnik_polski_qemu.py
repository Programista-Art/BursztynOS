#!/usr/bin/env python3
"""Jeden krotki natywny przebieg QEMU: Notatnik z polskim AltGr, menu kontekstowe,
Zapisz jako / Otworz przez natywny DialogPliku. Zrzuty ekranu + log seryjny.

Uzycie: python3 narzedzia/notatnik_polski_qemu.py KATALOG_WYJSCIOWY
Wymaga zbudowanego BursztynOS.iso i system_operacyjny.bin (symbole).
Watek pulsu mierzy przestoje hosta (VirtualBox) w trakcie przebiegu.
"""
import sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'narzedzia'))
from desktop_qemu_lib import Guest  # noqa: E402

out = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / '.notatnik-qemu')
out.mkdir(parents=True, exist_ok=True)
events = []
stalls = []
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


# Puls przed startem QEMU: srodowisko musi byc responsywne.
th = threading.Thread(target=pulse, daemon=True)
th.start()
time.sleep(20)
if stalls:
    note(f'PRZERWANO: przestoje hosta przed testem {stalls}')
    stop.set()
    (out / 'zdarzenia.txt').write_text('\n'.join(events) + '\n')
    sys.exit(2)
note('puls hosta 20 s bez przestojow, start QEMU')

POLSKIE = [('a', False), ('c', False), ('e', False), ('l', False), ('n', False), ('o', False),
           ('s', False), ('x', False), ('z', False)]
SCENARIUSZ = sys.argv[2] if len(sys.argv) > 2 else 'notatnik'
g = Guest(ROOT / 'BursztynOS.iso', out, timeout=1500)
rc = 1


def altgr(letter, shift=False):
    g.key(*((['shift'] if shift else []) + ['alt_r', letter]))


def husarz():
    # Pasek adresu Husarza: natywne PoleTekstowe w oknie z MenuGlowne.
    g.launch('husarz')
    time.sleep(10)
    g.screenshot('h01-husarz-start')
    g.key('ctrl', 'l')
    time.sleep(1)
    g.type_text('szukaj ')
    for letter, _ in POLSKIE:
        altgr(letter)
    g.key('spc')
    for letter, _ in POLSKIE:
        altgr(letter, True)
    time.sleep(1.5)
    g.screenshot('h02-pasek-adresu')
    note('Husarz: wpisano w pasek adresu "szukaj ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ"')


try:
    g.boot_to_desktop()
    if SCENARIUSZ == 'husarz':
        note(f'pulpit gotowy po {g.desktop_seconds} s')
        husarz()
        rc = 0
        raise SystemExit
    note(f'pulpit gotowy po {g.desktop_seconds} s')
    g.screenshot('01-pulpit')
    g.launch('notatnik')
    time.sleep(6)
    g.screenshot('02-notatnik-start')
    note('Notatnik uruchomiony z menu Start')

    # "Zażółć gęślą jaźń" na ukladzie Polski Programisty.
    for part in ['Za', ('z',), ('o',), ('l',), ('c',), ' g', ('e',), ('s',), 'l', ('a',), ' ja', ('x',), ('n',)]:
        if isinstance(part, tuple):
            altgr(part[0])
        else:
            g.type_text(part)
    g.key('ret')
    for letter, _ in POLSKIE:
        altgr(letter, True)
    g.key('spc')
    for letter, _ in POLSKIE:
        altgr(letter)
    g.key('ret')
    g.type_text('abc 123')
    time.sleep(1.5)
    g.screenshot('03-polskie-znaki')
    note('wpisano: Zażółć gęślą jaźń / ĄĆĘŁŃÓŚŹŻ ąćęłńóśźż / abc 123')

    # Zaznaczenie Shift+Home i menu kontekstowe pod prawym przyciskiem.
    g.key('shift', 'home')
    wx, wy = g.w // 2, g.h // 2
    g.move(wx, wy)
    g.hmp('mouse_button 2')
    time.sleep(0.3)
    g.hmp('mouse_button 0')
    time.sleep(2)
    g.screenshot('04-menu-kontekstowe')
    note('menu kontekstowe otwarte prawym przyciskiem')
    g.key('esc')
    time.sleep(1)
    g.screenshot('05-menu-zamkniete')

    # Zapisz (nienazwany dokument) -> natywny dialog Zapisz jako.
    g.key('ctrl', 's')
    time.sleep(3)
    g.screenshot('06-zapisz-jako')
    note('Ctrl+S otworzyl dialog Zapisz jako')
    g.key('ctrl', 'a')
    g.type_text('Notatka ')
    altgr('z')
    g.type_text('r')
    altgr('o')
    g.type_text('dlo')
    time.sleep(1)
    g.screenshot('07-nazwa-pliku')
    g.key('ret')
    time.sleep(3)
    g.screenshot('08-zapisano')
    note('zatwierdzono nazwe "Notatka żródlo" (rozszerzenie z filtra)')

    # Nowy dokument, potem Otworz i ponowne wczytanie pliku.
    g.key('ctrl', 'n')
    time.sleep(2)
    g.screenshot('09-nowy')
    g.key('ctrl', 'o')
    time.sleep(3)
    g.screenshot('10-otworz')
    note('Ctrl+O otworzyl dialog Otworz')
    g.type_text('Notatka ')
    altgr('z')
    g.type_text('r')
    altgr('o')
    g.type_text('dlo.txt')
    g.key('ret')
    time.sleep(3)
    g.screenshot('11-otwarty-ponownie')
    note('plik otwarty ponownie przez dialog Otworz')
    rc = 0
except SystemExit:
    pass
except Exception as exc:  # zachowaj dowody
    note(f'BLAD: {type(exc).__name__}: {str(exc)[:400]}')
    try:
        g.screenshot('99-blad')
    except Exception:
        pass
finally:
    try:
        g.close()
    except Exception:
        try:
            g.vm.kill()
        except Exception:
            pass
    stop.set()
    note(f'przestoje hosta (start s, dlugosc s): {stalls}')
    (out / 'zdarzenia.txt').write_text('\n'.join(events) + '\n')
sys.exit(rc)
