#!/usr/bin/env python3
"""Pakuje dane/muzyka/ jako modul Multiboot2 muzyka.pak (kontener BZP3, jak zdjecia.pak) dla Grajka.

Muzyka NIE trafia do obrazu jadra (limit 15 MiB). ISO niesie ja w /boot/muzyka.pak, a jadro przy rozruchu ZYWEGO
systemu kopiuje pliki do /dane/muzyka/ (istniejace nie sa nadpisywane; import od najmniejszych; wpis, po ktorym na
128 MiB RAM-dysku zostaloby < 24 MiB wolnego miejsca, jest pomijany). Instalator muzyki nie kopiuje.

Do ISO trafia WYLACZNIE to, co uzytkownik umiescil w dane/muzyka/ (pliki .wav/.mp3, nazwa UTF-8 1..55 bajtow -
limit nazwy PSF). Odpowiedzialnosc za prawa do tych nagran spoczywa na osobie budujacej ISO; repozytorium nie zawiera
zadnej muzyki. Modul jest WYLACZNIE opcjonalny (demonstracja/test): domyslnie (brak zmiennej albo MUZYKA_W_ISO=0)
nic nie jest pakowane i dane/muzyka nie jest czytane. Wlaczenie: MUZYKA_W_ISO=1 (zmienna srodowiskowa / make).
Kontener jest binarny i bez kodowania (patrz jadro/fs/galeria_pak.h); skaner wydania skanuje go jak kazdy plik.
Uzycie: package_muzyka.py <katalog-staging> [<grub.cfg>]   |   package_muzyka.py --stamp <plik>
"""
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_galeria  # noqa: E402  (wspolny budowniczy kontenera BZP3)

ROOT = Path(__file__).resolve().parents[1]
ZRODLO = ROOT / 'dane' / 'muzyka'
ROZSZERZENIA = {'.wav', '.mp3'}
MAKS_PLIKOW = 256
MAKS_NAZWA_PSF = 55
MAKS_PLIK = 64 * 1024 * 1024          # galeria_pak.h: MAKS_PLIK
MAKS_SUMA = 160 * 1024 * 1024         # < MAKS_KONTENER (200 MiB); RAM-dysk i tak przyjmie mniej (rezerwa)
RAM_DYSK_ORIENTACYJNIE = 64 * 1024 * 1024   # orientacyjnie: wolne miejsce zywego RAM-dysku minus rezerwa 24 MiB


def wlaczona():
    return os.environ.get('MUZYKA_W_ISO', '0').strip().lower() in ('1', 'tak', 'yes', 'true')


def zbierz(katalog=ZRODLO):
    pliki = []
    if not wlaczona() or not katalog.is_dir():
        return pliki
    for p in sorted(katalog.iterdir(), key=lambda x: x.name.lower()):
        if not p.is_file() or p.suffix.lower() not in ROZSZERZENIA:
            continue
        nazwa = p.name.encode('utf-8')
        if not 1 <= len(nazwa) <= MAKS_NAZWA_PSF or b'/' in nazwa or b'\\' in nazwa or any(c < 0x20 for c in nazwa):
            print(f'POMINIETO (nazwa, limit PSF {MAKS_NAZWA_PSF} B): {p.name}')
            continue
        if p.stat().st_size > MAKS_PLIK:
            print(f'POMINIETO (rozmiar > {MAKS_PLIK} B): {p.name}')
            continue
        pliki.append((nazwa, p))
    return pliki[:MAKS_PLIKOW]


def package(stage, cfg=None):
    stage = Path(stage)
    cfg = Path(cfg) if cfg else stage / 'boot/grub/grub.cfg'
    pliki = zbierz()
    cel = stage / 'boot/muzyka.pak'
    text = cfg.read_text()
    text = re.sub(r'(?m)^.*module2 /boot/muzyka.pak.*\n', '', text)
    if not pliki:
        if cel.exists():
            cel.unlink()
        cfg.write_text(text)
        print('Muzyka: modul muzyka.pak nie jest dodawany (domyslnie wylaczony; MUZYKA_W_ISO=1 wlacza muzyke demonstracyjna z dane/muzyka)')
        return
    suma = 0
    wybrane = []
    for nazwa, p in sorted(pliki, key=lambda np: np[1].stat().st_size):   # od najmniejszych do limitu modulu
        r = p.stat().st_size
        if suma + r > MAKS_SUMA:
            print(f'POMINIETO (limit modulu {MAKS_SUMA} B): {p.name}')
            continue
        suma += r
        wybrane.append((nazwa, p))
    wybrane.sort(key=lambda np: np[1].name.lower())
    blob = package_galeria.zbuduj(wybrane)
    cel.parent.mkdir(parents=True, exist_ok=True)
    cel.write_bytes(blob)
    # Modul tylko we wpisach zywego systemu (nie w instalatorze: instalacja nie kopiuje muzyki).
    wiersze, licznik, instalator = [], 0, False
    for linia in text.splitlines(keepends=True):
        if linia.lstrip().startswith('menuentry'):
            instalator = 'Instalator' in linia
        if 'bursztyn.install=1' in linia:
            instalator = True
        if re.match(r'\s*boot\s*$', linia) and not instalator:
            wiersze.append('    module2 /boot/muzyka.pak muzyka.pak\n')
            licznik += 1
        wiersze.append(linia)
    cfg.write_text(''.join(wiersze))
    print(f'Muzyka: {len(wybrane)} plikow, {len(blob)} B, modul w {licznik} wpisach GRUB')
    # Informacja (nie blad): modul przyjmuje do MAKS_SUMA, ale ZYWY system importuje na 128 MiB RAM-dysk tylko to,
    # co zmiesci sie z rezerwa 24 MiB (od najmniejszych). Pominiete pliki jadro wypisuje z nazwa w logu [MUZYKA].
    if suma > RAM_DYSK_ORIENTACYJNIE:
        print(f'UWAGA: muzyka w module = {suma // (1024 * 1024)} MiB; zywy system (RAM-dysk 128 MiB, rezerwa 24 MiB) '
              f'zaimportuje zwykle ok. {RAM_DYSK_ORIENTACYJNIE // (1024 * 1024)} MiB od najmniejszych plikow. '
              'Wieksze pliki pozostana w muzyka.pak, ale nie trafia do /dane/muzyka (log jadra: [MUZYKA] pominieto ...).')


def stempel(wyjscie):
    import hashlib
    h = hashlib.sha256()
    h.update(b'wl' if wlaczona() else b'wyl')
    for nazwa, p in zbierz():
        st = p.stat()
        h.update(nazwa + b'\0' + str(st.st_size).encode() + b'\0' + str(st.st_mtime_ns).encode() + b'\n')
    tekst = h.hexdigest() + '\n'
    w = Path(wyjscie)
    if not w.exists() or w.read_text() != tekst:
        w.write_text(tekst)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--stamp':
        stempel(sys.argv[2])
        raise SystemExit(0)
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    package(*sys.argv[1:])
