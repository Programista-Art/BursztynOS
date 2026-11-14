#!/usr/bin/env python3
"""Pakuje dane/zdjecia/ jako modul Multiboot2 zdjecia.pak (kontener BZP3) i dodaje go do menu GRUB.

Zdjecia NIE trafiaja do obrazu jadra (limit 15 MiB): ISO niesie je w /boot/zdjecia.pak, a jadro
przy rozruchu zywego systemu kopiuje je do /dane/zdjecia/ (istniejace pliki nie sa nadpisywane).
Instalator kopiuje je opcjonalnie na dysk docelowy (best effort, bez wplywu na powodzenie instalacji).

Kontener BZP3 jest BINARNY i BEZ KODOWANIA (dane obrazow sa surowe) - patrz jadro/fs/galeria_pak.h:
naglowek (sygnatura, wersja, liczba, rozmiar tabeli, SHA-256 tabeli), tabela (nazwa, rozmiar, przesuniecie,
CRC-32) i dane. Skaner wydania (verifyreleasemetadata.py) rozumie ten kontener: skanuje naglowek, tabele
i nazwy jako tekst oraz kazdy wpis wg jego rzeczywistego formatu (walidacja struktury JPEG/PNG/BTP/BMP, skan
tekstowy segmentow metadanych APPn/COM/tEXt/iTXt/eXIf), a nie skanuje losowych bajtow skompresowanych
pikseli jako tekstu. Pakiet nie omija skanera: nie ma kodowania dobranego pod wzorce.
Uzycie: package_galeria.py <katalog-staging> [<grub.cfg>]
"""
import hashlib, re, struct, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZRODLO = ROOT / 'dane' / 'zdjecia'
ROZSZERZENIA = {'.png', '.jpg', '.jpeg', '.btp', '.bmp'}
MAKS_PLIKOW = 256
MAKS_SUMA = 120 * 1024 * 1024        # limit modulu w jadrze (galeria_pak.h: 200 MiB) z zapasem; PSF RAM disk 128 MiB
MAKS_PLIK = 64 * 1024 * 1024
ROZMIAR_NAGLOWKA = 48


def zbierz(katalog=ZRODLO):
    pliki = []
    if katalog.is_dir():
        for p in sorted(katalog.iterdir(), key=lambda x: x.name.lower()):
            if not p.is_file() or p.suffix.lower() not in ROZSZERZENIA:
                continue
            nazwa = p.name.encode('utf-8')
            if not 1 <= len(nazwa) <= 63 or b'/' in nazwa or b'\\' in nazwa or any(c < 0x20 for c in nazwa):
                print(f'POMINIETO (nazwa): {p.name}')
                continue
            pliki.append((nazwa, p))
    return pliki[:MAKS_PLIKOW]


def zbuduj(pliki):
    wpisy = []
    for nazwa, p in pliki:
        dane = p.read_bytes()
        if not dane:
            continue
        if len(dane) > MAKS_PLIK:
            print(f'POMINIETO (rozmiar > {MAKS_PLIK} B): {p.name}')
            continue
        wpisy.append((nazwa, dane))
    tabela_dl = sum(1 + len(n) + 12 for n, _ in wpisy)
    poz = ROZMIAR_NAGLOWKA + tabela_dl
    tabela = bytearray()
    for nazwa, dane in wpisy:
        tabela += bytes([len(nazwa)]) + nazwa + struct.pack('<III', len(dane), poz, zlib.crc32(dane) & 0xFFFFFFFF)
        poz += len(dane)
    naglowek = b'BZP3' + struct.pack('<III', 1, len(wpisy), len(tabela)) + hashlib.sha256(bytes(tabela)).digest()
    return naglowek + bytes(tabela) + b''.join(d for _, d in wpisy)


def package(stage, cfg=None):
    stage = Path(stage)
    cfg = Path(cfg) if cfg else stage / 'boot/grub/grub.cfg'
    pliki = zbierz()
    blob = zbuduj(pliki)
    if len(blob) > MAKS_SUMA:
        raise SystemExit(f'dane/zdjecia: {len(blob)} B przekracza limit modulu {MAKS_SUMA} B')
    cel = stage / 'boot/zdjecia.pak'
    text = cfg.read_text()
    text = re.sub(r'(?m)^.*module2 /boot/zdjecia.pak.*\n', '', text)
    if not pliki:
        if cel.exists():
            cel.unlink()
        cfg.write_text(text)
        print('Galeria: dane/zdjecia puste - modul zdjecia.pak nie jest dodawany')
        return
    cel.parent.mkdir(parents=True, exist_ok=True)
    cel.write_bytes(blob)
    # Modul dostaja WSZYSTKIE wpisy rozruchowe: zywy system importuje zdjecia do RAM-dysku PSF, a wpis
    # instalatora kopiuje je (best effort) na dysk docelowy; zainstalowany system czyta je juz z dysku.
    wiersze, licznik = [], 0
    for linia in text.splitlines(keepends=True):
        if re.match(r'\s*boot\s*$', linia):
            wiersze.append('    module2 /boot/zdjecia.pak zdjecia.pak\n'); licznik += 1
        wiersze.append(linia)
    cfg.write_text(''.join(wiersze))
    print(f'Galeria: {len(pliki)} plikow, {len(blob)} B, modul w {licznik} wpisach GRUB')


def stempel(wyjscie):
    # Odcisk listy plikow galerii (nazwa, rozmiar, mtime): Make nie radzi sobie ze spacjami w nazwach
    # jako zaleznosciami, wiec cel .galeria.stamp zmienia sie tylko przy zmianie zawartosci katalogu.
    import hashlib
    h = hashlib.sha256()
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
