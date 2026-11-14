#!/usr/bin/env python3
"""Skaner wydania: kontener galerii BZP3 i walidacja zdjec wg RZECZYWISTEGO formatu.

Zamiast skanowac losowe bajty skompresowanych pikseli jako tekst (co daje przypadkowe dopasowania) i zamiast
ukrywac dane w alfabecie dobranym pod wzorce, skaner:
  1. waliduje kontener (naglowek, SHA-256 tabeli, nazwy, rozszerzenia, uklad, CRC-32 kazdego wpisu) - zgodnie
     z jadro/fs/galeria_pak.h;
  2. kazdy wpis waliduje wg formatu (JPEG: marker po markerze do EOI; PNG: chunki z CRC; BMP: naglowki i rozmiar;
     BTP1: naglowek i dokladny rozmiar danych RAW/RLE; BTP2: pelne dekodowanie btpcodec);
  3. zwraca REGIONY TEKSTOWE do skanu wzorcami: naglowek+tabela (nazwy plikow) oraz segmenty metadanych zdjec
     (JPEG APPn/COM, PNG chunki inne niz IHDR/PLTE/IDAT/IEND, naglowki BMP/BTP). Dane pikseli (entropia) NIE sa
     skanowane jako tekst - sa walidowane strukturalnie.
Wpis niepoprawny albo kontener niepoprawny NIE jest pomijany: wolajacy zglasza finding i skanuje surowe bajty.
"""
import hashlib, pathlib, struct, sys, zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / 'tapety-btp'))

MAKS_WPISOW = 256
MAKS_NAZWA = 63
MAKS_KONTENER = 200 * 1024 * 1024
MAKS_PLIK = 64 * 1024 * 1024
ROZSZERZENIA = {'png', 'jpg', 'jpeg', 'btp', 'bmp'}


class BladKontenera(ValueError):
    pass


def parsuj_kontener(data):
    """Zwraca (liczba_tabela_bytes, wpisy[(nazwa, offset, rozmiar, crc)]) albo BladKontenera."""
    n = len(data)
    if n < 48: raise BladKontenera('za maly')
    if n > MAKS_KONTENER: raise BladKontenera('za duzy')
    if data[:4] != b'BZP3': raise BladKontenera('sygnatura')
    wersja, liczba, tabela = struct.unpack_from('<III', data, 4)
    if wersja != 1: raise BladKontenera('wersja')
    if not 1 <= liczba <= MAKS_WPISOW: raise BladKontenera('liczba')
    if tabela < liczba * 14 or tabela > MAKS_WPISOW * (1 + MAKS_NAZWA + 12) or 48 + tabela > n: raise BladKontenera('tabela')
    t = data[48:48 + tabela]
    if hashlib.sha256(t).digest() != data[16:48]: raise BladKontenera('sha256 tabeli')
    poz, oczekiwane, wpisy, nazwy = 0, 48 + tabela, [], set()
    for _ in range(liczba):
        if poz + 1 > tabela: raise BladKontenera('tabela')
        dl = t[poz]; poz += 1
        if not 1 <= dl <= MAKS_NAZWA or poz + dl + 12 > tabela: raise BladKontenera('tabela')
        nb = t[poz:poz + dl]; poz += dl
        if nb in (b'.', b'..') or any(c < 0x20 or c == 0x7F or c in b'/\\' for c in nb): raise BladKontenera('nazwa')
        try: nazwa = nb.decode('utf-8')
        except UnicodeDecodeError: raise BladKontenera('nazwa utf-8')
        ext = nazwa.rsplit('.', 1)[-1].lower() if '.' in nazwa[1:] else ''
        if ext not in ROZSZERZENIA: raise BladKontenera('rozszerzenie')
        klucz = nazwa.encode('ascii', 'ignore').lower() + nb.lower()
        if klucz in nazwy: raise BladKontenera('duplikat')
        nazwy.add(klucz)
        rozmiar, off, crc = struct.unpack_from('<III', t, poz); poz += 12
        if rozmiar == 0 or rozmiar > MAKS_PLIK: raise BladKontenera('rozmiar')
        if off != oczekiwane: raise BladKontenera('uklad')
        if off + rozmiar > n: raise BladKontenera('zakres')
        oczekiwane += rozmiar
        if zlib.crc32(data[off:off + rozmiar]) & 0xFFFFFFFF != crc: raise BladKontenera('crc ' + nazwa)
        wpisy.append((nazwa, off, rozmiar, crc))
    if poz != tabela or oczekiwane != n: raise BladKontenera('uklad koncowy')
    return 48 + tabela, wpisy


class BladObrazu(ValueError):
    pass


def regiony_jpeg(d):
    if d[:2] != b'\xff\xd8': raise BladObrazu('jpeg: SOI')
    i, n, reg, sof, sos = 2, len(d), [], False, False
    while i < n:
        if d[i] != 0xFF: raise BladObrazu('jpeg: marker')
        while i < n and d[i] == 0xFF: i += 1
        if i >= n: raise BladObrazu('jpeg: uciety')
        m = d[i]; i += 1
        if m == 0xD9:
            if not (sof and sos) or i != n: raise BladObrazu('jpeg: EOI')
            return reg
        if m in (0x01,) or 0xD0 <= m <= 0xD8 or m == 0x00: raise BladObrazu('jpeg: marker poza skanem')
        if i + 2 > n: raise BladObrazu('jpeg: dlugosc')
        L = struct.unpack_from('>H', d, i)[0]
        if L < 2 or i + L > n: raise BladObrazu('jpeg: segment')
        seg = d[i + 2:i + L]
        if m in (0xC0, 0xC1, 0xC2):
            if len(seg) < 6: raise BladObrazu('jpeg: SOF')
            h, w = struct.unpack_from('>HH', seg, 1)
            if not w or not h: raise BladObrazu('jpeg: wymiary')
            sof = True
        elif 0xE0 <= m <= 0xEF or m == 0xFE:
            reg.append(bytes(seg))            # metadane: APPn, COM
        i += L
        if m == 0xDA:
            sos = True
            while i < n:                      # dane entropii: do nastepnego markera innego niz RSTn / 0x00
                j = d.find(b'\xff', i)
                if j < 0 or j + 1 >= n: raise BladObrazu('jpeg: ucieta entropia')
                c = d[j + 1]
                if c == 0 or 0xD0 <= c <= 0xD7 or c == 0xFF: i = j + 2 if c != 0xFF else j + 1; continue
                i = j; break
            else: raise BladObrazu('jpeg: brak EOI')
    raise BladObrazu('jpeg: brak EOI')


def regiony_png(d):
    if d[:8] != b'\x89PNG\r\n\x1a\n': raise BladObrazu('png: sygnatura')
    i, n, reg, pierwszy, idat, koniec = 8, len(d), [], True, False, False
    while i < n:
        if i + 12 > n: raise BladObrazu('png: ucieta')
        L, typ = struct.unpack_from('>I4s', d, i)
        if L > 0x7FFFFFFF or i + 12 + L > n: raise BladObrazu('png: chunk')
        body = d[i + 8:i + 8 + L]
        crc = struct.unpack_from('>I', d, i + 8 + L)[0]
        if zlib.crc32(typ + body) & 0xFFFFFFFF != crc: raise BladObrazu('png: crc')
        if pierwszy:
            if typ != b'IHDR' or L != 13: raise BladObrazu('png: IHDR')
            w, h = struct.unpack_from('>II', body)
            if not w or not h or w > 0x7FFFFFFF or h > 0x7FFFFFFF: raise BladObrazu('png: wymiary')
            pierwszy = False
        elif typ == b'IDAT': idat = True
        elif typ == b'IEND':
            if L != 0 or i + 12 != n or not idat: raise BladObrazu('png: IEND')
            koniec = True
        elif typ != b'PLTE':
            reg.append(typ + bytes(body))     # metadane: tEXt, iTXt, zTXt, eXIf, iCCP, ...
        i += 12 + L
    if not koniec: raise BladObrazu('png: brak IEND')
    return reg


def regiony_bmp(d):
    if len(d) < 26 or d[:2] != b'BM': raise BladObrazu('bmp: sygnatura')
    rozmiar, _, off = struct.unpack_from('<IIi', d, 2)
    dib = struct.unpack_from('<I', d, 14)[0]
    if rozmiar != len(d) or dib < 12 or 14 + dib > len(d) or not 14 + dib <= off <= len(d): raise BladObrazu('bmp: naglowek')
    return [bytes(d[:off])]                   # naglowki i paleta; dane pikseli = struktura


def regiony_btp(d):
    if d[:4] == b'BTP2':
        import btpcodec
        try: btpcodec.decode(bytes(d))
        except Exception as e: raise BladObrazu('btp2: ' + str(e)[:40])
        return [bytes(d[:32])]
    if d[:4] != b'BTP1' or len(d) < 14: raise BladObrazu('btp: sygnatura')
    w, h, bpp, mode = struct.unpack_from('<IIBB', d, 4)
    if not w or not h or w > 8192 or h > 8192 or bpp != 32 or mode not in (0, 1): raise BladObrazu('btp1: naglowek')
    px = w * h
    if mode == 0:
        if len(d) != 14 + px * 4: raise BladObrazu('btp1: rozmiar RAW')
    else:
        i, got = 14, 0
        while i < len(d):
            run = d[i]
            if run == 0 or i + 5 > len(d): raise BladObrazu('btp1: RLE')
            got += run; i += 5
            if got > px: raise BladObrazu('btp1: RLE za dlugi')
        if got != px: raise BladObrazu('btp1: RLE niepelny')
    return [bytes(d[:14])]


def regiony_wpisu(nazwa, dane):
    ext = nazwa.rsplit('.', 1)[-1].lower()
    if ext in ('jpg', 'jpeg'): return regiony_jpeg(dane)
    if ext == 'png': return regiony_png(dane)
    if ext == 'bmp': return regiony_bmp(dane)
    if ext == 'btp': return regiony_btp(dane)
    raise BladObrazu('format')


def skanuj(data):
    """Zwraca (regiony_tekstowe, problemy, liczba_wpisow). Regiony: [(etykieta, bytes)]. problemy: [(etykieta, opis)]."""
    try:
        koniec_tabeli, wpisy = parsuj_kontener(data)
    except BladKontenera as e:
        return None, [('kontener', str(e))], 0
    regiony, problemy = [('naglowek+tabela', bytes(data[:koniec_tabeli]))], []
    for nazwa, off, rozmiar, _ in wpisy:
        dane = bytes(data[off:off + rozmiar])
        try:
            for i, r in enumerate(regiony_wpisu(nazwa, dane)): regiony.append((f'{nazwa}#meta{i}', r))
        except (BladObrazu, struct.error, UnicodeDecodeError) as e:
            problemy.append((nazwa, str(e)))
            regiony.append((nazwa + '#surowe', bytes(dane)))        # niepoprawny wpis: skan CALOSCI jako tekst
    return regiony, problemy, len(wpisy)
