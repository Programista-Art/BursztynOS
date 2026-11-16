"""Kontrakt generowanych masek napisow ekranu powitalnego.

Naglowek jadro/gui/napisy_powitania_dane.h jest artefaktem rasteryzacji czcionki
OpenType (Noto Sans) rasteryzatorem BSF. Test kompiluje generator, regeneruje
naglowek do pliku tymczasowego i porownuje go bajt w bajt, wiec zmiana czcionki,
napisow lub generatora bez regeneracji konczy sie bledem.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
generator = root / 'narzedzia' / 'generuj_napisy_powitania.cpp'
czcionka = root / 'czcionki' / 'otf' / 'NotoSans-Regular.otf'
naglowek = root / 'jadro' / 'gui' / 'napisy_powitania_dane.h'
renderer = root / 'jadro' / 'gui' / 'ekran_powitalny.h'

bledy = 0
sprawdzenia = 0


def check(warunek, opis):
    global bledy, sprawdzenia
    sprawdzenia += 1
    if not warunek:
        print(f'FAIL: {opis}')
        bledy += 1


def fnv1a(dane):
    h = 0x811C9DC5
    for b in dane:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


check(generator.exists(), 'generator istnieje')
check(czcionka.exists(), 'zrodlowa czcionka istnieje')
check(naglowek.exists(), 'wygenerowany naglowek jest w repozytorium')
tekst = naglowek.read_text(encoding='utf-8')

with tempfile.TemporaryDirectory() as tmp:
    exe = pathlib.Path(tmp) / 'gen'
    wynik = pathlib.Path(tmp) / 'napisy.h'
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wno-unused-function',
                    f'-I{root}', str(generator), '-o', str(exe)], check=True)
    subprocess.run([str(exe), '--czcionka', str(czcionka), '--wyjscie', str(wynik)],
                   check=True, stdout=subprocess.DEVNULL)
    check(wynik.read_bytes() == naglowek.read_bytes(),
          'naglowek jest zgodny z generatorem i czcionka (uruchom make jadro/gui/napisy_powitania_dane.h)')

check('PLIK GENEROWANY' in tekst and 'namespace NapisyPowitania' in tekst, 'naglowek jest generowany')
check('czcionki/otf/NotoSans-Regular.otf' in tekst, 'naglowek dokumentuje zrodlowa czcionke')
m = re.search(r'SUMA_KONTROLNA_CZCIONKI = 0x([0-9A-F]{8})u', tekst)
check(m is not None and int(m.group(1), 16) == fnv1a(czcionka.read_bytes()),
      'suma kontrolna odpowiada bajtom czcionki')
check('"Bursztyn OS"' in tekst, 'tytul w zapisie mieszanym, bez rozstrzelenia')
check('"Polski system operacyjny"' in tekst, 'pierwszy podtytul')
check('"Niezależny system"' in tekst, 'drugi podtytul z polskim znakiem (UTF-8)')

# Wymiary i zawartosc masek.
calkowite = 0
for nazwa in ('TYTUL', 'LINIA1', 'LINIA2'):
    d = re.search(rf'inline constexpr uint8_t {nazwa}_PIKSELE\[(\d+)\] = \{{(.*?)\}};', tekst, re.S)
    s = re.search(rf'inline constexpr Maska {nazwa}\{{{nazwa}_PIKSELE, (\d+), (\d+), (\d+), (\d+), (\d+), (\d+)\}};', tekst)
    check(d is not None and s is not None, f'{nazwa}: dane i opis maski')
    if not d or not s:
        continue
    w, h, baza, wersalik, x_h, em = map(int, s.groups())
    wartosci = [int(v) for v in d.group(2).replace('\n', ' ').split(',') if v.strip()]
    calkowite += len(wartosci)
    check(len(wartosci) == int(d.group(1)) == w * h, f'{nazwa}: rozmiar tablicy == w*h')
    check(all(0 <= v <= 255 for v in wartosci), f'{nazwa}: pokrycie 0..255')
    # Przyciecie do atramentu: kazda krawedz maski ma atrament.
    check(any(wartosci[:w]) and any(wartosci[-w:]), f'{nazwa}: gorny i dolny wiersz z atramentem')
    check(any(wartosci[y * w] for y in range(h)) and any(wartosci[y * w + w - 1] for y in range(h)),
          f'{nazwa}: lewa i prawa kolumna z atramentem')
    # Wygladzanie: sa posrednie poziomy pokrycia, a nie tylko 0/255 (raster bitmapowy).
    posrednie = sum(1 for v in wartosci if 0 < v < 255)
    check(posrednie > w, f'{nazwa}: krawedzie wygladzone ({posrednie} posrednich pikseli)')
    check(0 < wersalik <= baza <= h and 0 < x_h < wersalik < em, f'{nazwa}: metryki typograficzne')
    # Brak rozstrzelenia: szerokosc ~0.45..0.75 em na znak (Noto Sans), a nie 1+ em.
    znaki = {'TYTUL': 11, 'LINIA1': 24, 'LINIA2': 17}[nazwa]
    check(w < em * znaki * 0.75, f'{nazwa}: naturalne odstepy miedzy znakami')
b = re.search(r'BAJTY_MASEK = (\d+);', tekst)
check(b is not None and int(b.group(1)) == calkowite, 'suma bajtow masek')
check(calkowite < 128 * 1024, f'maski sa male ({calkowite} B < 128 KiB)')

r = renderer.read_text(encoding='utf-8')
check('napisy_powitania_dane.h' in r and 'probkuj_obszarowo(' in r, 'renderer uzywa masek i filtra obszarowego')
check('stbtt_' not in r and 'stb_truetype' not in r, 'jadro nie zawiera rasteryzatora TTF')

if bledy:
    print(f'Maski napisow powitania: {bledy} bledow / {sprawdzenia}')
    sys.exit(1)
print(f'Maski napisow powitania (Noto Sans, regeneracja bajt w bajt): {sprawdzenia}/{sprawdzenia} PASS')
