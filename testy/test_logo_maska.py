"""Kontrakt generowanej maski logotypu.

Naglowek jadro/gui/logo_maska_dane.h jest artefaktem. Ten test regeneruje go
do pliku tymczasowego i porownuje bajt w bajt, wiec zmiana obrazy/logo.png bez
regeneracji naglowka konczy sie bledem, a nie cichym rozjechaniem grafiki.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
generator = root / 'narzedzia' / 'generuj_maske_logo.py'
zrodlo = root / 'obrazy' / 'logo.png'
naglowek = root / 'jadro' / 'gui' / 'logo_maska_dane.h'

bledy = 0


def check(warunek, opis):
    global bledy
    if not warunek:
        print(f'FAIL: {opis}')
        bledy += 1


check(generator.exists(), 'generator maski istnieje')
check(zrodlo.exists(), 'zrodlo obrazy/logo.png istnieje')
check(naglowek.exists(), 'wygenerowany naglowek jest w repozytorium')

with tempfile.TemporaryDirectory() as tmp:
    wyjscie = pathlib.Path(tmp) / 'logo_maska_dane.h'
    proc = subprocess.run(
        [sys.executable, str(generator), '--zrodlo', 'obrazy/logo.png', '--wyjscie', str(wyjscie)],
        capture_output=True, text=True, cwd=str(root))
    check(proc.returncode == 0, f'generator konczy sie kodem 0 (stderr: {proc.stderr.strip()})')
    if proc.returncode == 0:
        nowy = wyjscie.read_text(encoding='utf-8')
        stary = naglowek.read_text(encoding='utf-8')
        check(nowy == stary,
              'naglowek jest identyczny z regeneracja (uruchom: python narzedzia/generuj_maske_logo.py)')

tresc = naglowek.read_text(encoding='utf-8')

# Wymagane stale i tablice.
for symbol in ('SUMA_KONTROLNA_ZRODLA', 'SZEROKOSC', 'WYSOKOSC',
               'POSWIATA_SZEROKOSC', 'POSWIATA_WYSOKOSC',
               'POSWIATA_OD_X', 'POSWIATA_DO_X', 'POSWIATA_OD_Y', 'POSWIATA_DO_Y',
               'MASKA_ATRAMENT_OD', 'MASKA_ATRAMENT_DO',
               'MASKA_ATRAMENT_LEWY', 'MASKA_ATRAMENT_PRAWY',
               'POSWIATA_ATRAMENT_OD', 'POSWIATA_ATRAMENT_DO',
               'POSWIATA_ATRAMENT_LEWY', 'POSWIATA_ATRAMENT_PRAWY'):
    check(symbol in tresc, f'naglowek definiuje {symbol}')


def wartosc(nazwa):
    m = re.search(rf'{nazwa}\s*=\s*(-?\d+)', tresc)
    return int(m.group(1)) if m else None


def tablica(nazwa):
    m = re.search(rf'{nazwa}\[\d+\]\s*=\s*\{{(.*?)\}};', tresc, re.S)
    if not m:
        return []
    return [int(x) for x in re.findall(r'-?\d+', m.group(1))]


szer = wartosc('SZEROKOSC')
wys = wartosc('WYSOKOSC')
maska = tablica('MASKA')
poswiata = tablica('POSWIATA')
pszer = wartosc('POSWIATA_SZEROKOSC')
pwys = wartosc('POSWIATA_WYSOKOSC')

check(szer and szer > 0, 'szerokosc maski jest dodatnia')
check(wys and wys > 0, 'wysokosc maski jest dodatnia')
check(len(maska) == szer * wys, f'maska ma {szer}x{wys} wartosci (jest {len(maska)})')
check(len(poswiata) == pszer * pwys, f'poswiata ma {pszer}x{pwys} wartosci (jest {len(poswiata)})')
check(all(0 <= v <= 255 for v in maska), 'pokrycie maski miesci sie w 0..255')
check(sum(maska) > 0, 'maska ma niezerowe pokrycie')

# Proporcje zblizone do zrodla (807x878 po przycieciu).
stosunek = szer / wys
check(0.85 < stosunek < 0.98, f'proporcje maski odpowiadaja zrodlu ({stosunek:.3f})')

# Atrament rdzenia obejmuje cala wysokosc, a poswiata ma wieksze pole widzenia.
check(wartosc('MASKA_ATRAMENT_OD') == 0, 'gorny atrament rdzenia na wierszu 0')
check(wartosc('MASKA_ATRAMENT_DO') == wys, 'dolny atrament rdzenia na ostatnim wierszu')
check(wartosc('POSWIATA_OD_X') < 0 and wartosc('POSWIATA_OD_Y') < 0, 'poswiata wychodzi poza rdzen z lewej i z gory')
check(wartosc('POSWIATA_DO_X') > 1000 and wartosc('POSWIATA_DO_Y') > 1000,
      'poswiata wychodzi poza rdzen z prawej i z dolu')

# Poswiata jest rozmyciem rdzenia opisujacym to samo pole widzenia (powietrze
# wokol logotypu), wiec w obszarze rdzenia nie jest od niego jasniejsza.
przekroczone = 0
for y in range(pwys):
    ry = wartosc('POSWIATA_OD_Y') + y * (
        wartosc('POSWIATA_DO_Y') - wartosc('POSWIATA_OD_Y')) // pwys
    sy = ry * wys // 1000
    if not 0 <= sy < wys:
        continue
    for x in range(pszer):
        rx = wartosc('POSWIATA_OD_X') + x * (
            wartosc('POSWIATA_DO_X') - wartosc('POSWIATA_OD_X')) // pszer
        sx = rx * szer // 1000
        if not 0 <= sx < szer:
            continue
        if poswiata[y * pszer + x] > maska[sy * szer + sx]:
            przekroczone += 1
check(przekroczone == 0, f'poswiata nie jest jasniejsza od rdzenia (przekroczen: {przekroczone})')

# Naglowek nie zawiera tablic generowanych reczna edycja z zewnetrznym formatem.
check('PLIK GENEROWANY' in tresc, 'naglowek zawiera ostrzezenie o generowaniu')
check('namespace BursztynLogo' in tresc, 'naglowek eksportuje wlasna przestrzen nazw')

if bledy:
    print(f'Logo mask contract: {bledy} FAIL')
    raise SystemExit(1)
print(f'Logo mask contract: PASS ({szer}x{wys} maska, {pszer}x{pwys} poswiata)')
