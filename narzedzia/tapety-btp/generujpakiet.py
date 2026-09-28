#!/usr/bin/env python3
"""
generujpakiet.py - Generator deterministycznego pakietu zasobow tapet BTP dla Bursztyn OS.

Format pakietu tapety.pak (Little-Endian):
1. Naglowek pakietu (32 bajty):
   - [0..7]   magic: b"BTPK1\x00\x00\x00"
   - [8..11]  version: uint32 = 1
   - [12..15] file_count: uint32
   - [16..19] header_size: uint32 = 32
   - [20..23] table_size: uint32 (rozmiar sekcji tabeli)
   - [24..27] data_size: uint32 (rozmiar sekcji danych)
   - [28..31] total_pack_size: uint32

2. Tabela wpisow (table_size bajtow):
   Dla kazdego pliku:
   - [0..3]   nazwa_len: uint32
   - [4..7]   btp_rozmiar: uint32
   - [8..11]  crc32: uint32 (IEEE 802.3)
   - [12..15] payload_offset: uint32 (od poczatku tapety.pak)
   - [16..16+nazwa_len-1] bajty nazwy UTF-8
   - dopelnienie zerami do wyrownania 4 bajtow

3. Sekcja danych (data_size bajtow):
   Dla kazdego pliku:
   - surowe bajty pliku BTP1
   - dopelnienie zerami do wyrownania 4 bajtow
"""

import argparse
import binascii
import os
import struct
import sys
from pathlib import Path
import btpcodec

U32_MAX = (1 << 32) - 1

def u32(value: int, name: str) -> int:
    if not 0 <= value <= U32_MAX:
        raise ValueError(f'{name} exceeds BTPK1 uint32 limit')
    return value

def bezpieczna_nazwa(nazwa: str) -> bool:
    if not nazwa or len(nazwa) > 120 or len(nazwa) < 5 or nazwa.startswith("."):
        return False
    if "/" in nazwa or "\\" in nazwa or "\0" in nazwa or "\r" in nazwa or "\n" in nazwa or ".." in nazwa or '"' in nazwa:
        return False
    try:
        dane = nazwa.encode("utf-8")
    except UnicodeEncodeError:
        return False
    i = 0
    n = len(dane)
    while i < n:
        c = dane[i]
        if c < 128:
            if c < 0x20 or c == 0x7F:
                return False
            i += 1
        else:
            if 0xC2 <= c <= 0xDF:
                cp = c & 31
                ile = 2
                minimum = 0x80
            elif 0xE0 <= c <= 0xEF:
                cp = c & 15
                ile = 3
                minimum = 0x800
            elif 0xF0 <= c <= 0xF4:
                cp = c & 7
                ile = 4
                minimum = 0x10000
            else:
                return False
            if i + ile > n:
                return False
            for j in range(1, ile):
                d = dane[i + j]
                if (d & 0xC0) != 0x80:
                    return False
                cp = (cp << 6) | (d & 63)
            if cp < minimum or cp > 0x10FFFF or (0xD800 <= cp <= 0xDFFF):
                return False
            i += ile
    ext = nazwa.rsplit(".", 1)[-1].lower() if "." in nazwa else ""
    return ext in ("btp", "jpg", "jpeg", "png")

def tapeta_czy_bursztyn_systemowa(nazwa: str) -> bool:
    nl = nazwa.lower()
    if len(nl) == 14 and nl.startswith("bursztyn") and nl.endswith(".btp"):
        num = nl[8:10]
        return num.isdigit() and 1 <= int(num) <= 10
    return False

def tapeta_fnv1a64(dane: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in dane:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h

def tapeta_kanoniczna_nazwa(display_name: str) -> str:
    if tapeta_czy_bursztyn_systemowa(display_name):
        return display_name.lower()
    dane = display_name.encode("utf-8")
    h = tapeta_fnv1a64(dane)
    ext = display_name.rsplit(".", 1)[-1].lower()
    return f"wp_{h:016x}.{ext}"

def waliduj_btp(sciezka: str) -> bool:
    try:
        rozmiar = os.path.getsize(sciezka)
        if rozmiar > 16 * 1024 * 1024:
            return False
        if rozmiar < 14:
            return False
        with open(sciezka, "rb") as f:
            naglowek = f.read(btpcodec.HEADER.size)
        if naglowek[:4] == b'BTP2':
            if len(naglowek) < btpcodec.HEADER.size:
                return False
            width, height = struct.unpack_from('<II', naglowek, 8)
            if width == 0 or height == 0 or width * height > 4 * 1024 * 1024:
                return False
            with open(sciezka, 'rb') as f:
                btpcodec.decode(f.read())
            return True
        if len(naglowek) < 14:
            return False
        magic, w, h, bpp, kompresja = struct.unpack("<4sIIBB", naglowek[:14])
        if magic != b"BTP1":
            return False
        if w == 0 or h == 0 or w > 8192 or h > 8192:
            return False
        if bpp != 32:
            return False
        if kompresja not in (0, 1):
            return False
        return True
    except (OSError, struct.error, btpcodec.BtpError):
        return False

def waliduj_jpeg(sciezka: str) -> bool:
    try:
        rozmiar = os.path.getsize(sciezka)
        if rozmiar < 4 or rozmiar > 16 * 1024 * 1024:
            return False
        with open(sciezka, "rb") as f:
            dane = f.read()
        if len(dane) < 4 or dane[:2] != b"\xff\xd8":
            return False
        i = 2
        n = len(dane)
        has_sof = False
        while i < n - 1:
            if dane[i] != 0xFF:
                i += 1
                continue
            marker = dane[i + 1]
            if marker == 0xD9:
                break
            if marker in (0xD8, 0x00, 0x01) or (0xD0 <= marker <= 0xD7):
                i += 2
                continue
            if i + 4 > n:
                break
            seg_len = struct.unpack(">H", dane[i + 2:i + 4])[0]
            if seg_len < 2 or i + 2 + seg_len > n:
                break
            if marker in (0xC0, 0xC1, 0xC2):
                if seg_len >= 7:
                    h, w = struct.unpack(">HH", dane[i + 5:i + 9])
                    if w == 0 or h == 0 or w > 8192 or h > 8192 or w * h > 8 * 1024 * 1024:
                        return False
                    has_sof = True
            i += 2 + seg_len
        return has_sof or dane.endswith(b"\xff\xd9")
    except (OSError, struct.error):
        return False

def waliduj_png(sciezka: str) -> bool:
    try:
        rozmiar = os.path.getsize(sciezka)
        if rozmiar < 8 + 12 + 13 + 12 or rozmiar > 16 * 1024 * 1024:
            return False
        with open(sciezka, "rb") as f:
            dane = f.read()
        if len(dane) < 8 or dane[:8] != b"\x89PNG\r\n\x1a\n":
            return False
        offset = 8
        if len(dane) < offset + 8:
            return False
        ihdr_len, ihdr_type = struct.unpack(">I4s", dane[offset:offset + 8])
        if ihdr_type != b"IHDR" or ihdr_len != 13:
            return False
        offset += 8
        w, h, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", dane[offset:offset + 13])
        if w == 0 or h == 0 or w > 8192 or h > 8192 or w * h > 8 * 1024 * 1024:
            return False
        if comp != 0 or filt != 0:
            return False
        offset += 13 + 4
        has_idat = False
        has_iend = False
        while offset + 8 <= len(dane):
            chunk_len, chunk_type = struct.unpack(">I4s", dane[offset:offset + 8])
            offset += 8
            if offset + chunk_len + 4 > len(dane):
                break
            if chunk_type == b"IDAT":
                has_idat = True
            elif chunk_type == b"IEND":
                has_iend = True
                break
            offset += chunk_len + 4
        return has_idat and has_iend
    except (OSError, struct.error):
        return False

def zapisz_jesli_zmieniono(sciezka: str, nowe_dane: bytes):
    if os.path.isfile(sciezka):
        try:
            with open(sciezka, "rb") as f:
                stare = f.read()
            if stare == nowe_dane:
                return
        except OSError:
            pass
    with open(sciezka, "wb") as f:
        f.write(nowe_dane)

def zapisz_tekst_jesli_zmieniono(sciezka: str, nowy_tekst: str):
    if os.path.isfile(sciezka):
        try:
            with open(sciezka, "r", encoding="utf-8") as f:
                stary = f.read()
            if stary == nowy_tekst:
                return
        except OSError:
            pass
    with open(sciezka, "w", encoding="utf-8") as f:
        f.write(nowy_tekst)

def main():
    parser = argparse.ArgumentParser(description="Generuj deterministyczny pakiet tapet tapety.pak dla Bursztyn OS")
    parser.add_argument("--katalog", "-k", default="obrazy/tapety", help="Katalog zrodlowy tapet .btp, .jpg, .jpeg, .png")
    parser.add_argument("--wyjscie", "-w", default="tapety.pak", help="Sciezka wyjsciowa pliku pakietu (.pak)")
    parser.add_argument("--dep", "-d", default="tapety.pak.d", help="Sciezka wyjsciowa zaleznosci dla Make (.d)")
    parser.add_argument("--domyslna", "--default", dest="domyslna", default=None, help="Domyslna tapeta (kanoniczna lub nazwa zrodlowa)")
    parser.add_argument("--manifest", default=None, help="Sciezka wyjsciowa wygenerowanego manifestu tekstowego")
    parser.add_argument("--manifest-only", help="Zapisz tylko deterministyczna liste nazw zrodel i zakoncz")
    args = parser.parse_args()

    katalog = args.katalog
    tapety = []
    kanoniczne_nazwy = {}

    if os.path.isdir(katalog):
        wpisy = sorted(os.listdir(katalog))
        for nazwa in wpisy:
            nazwa_lower = nazwa.lower()
            is_btp = nazwa_lower.endswith(".btp")
            is_jpeg = nazwa_lower.endswith(".jpg") or nazwa_lower.endswith(".jpeg")
            is_png = nazwa_lower.endswith(".png")
            if not is_btp and not is_jpeg and not is_png:
                continue
            if not bezpieczna_nazwa(nazwa):
                sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: pomijam niebezpieczna nazwe pliku: {nazwa}\n")
                continue
            sciezka = os.path.join(katalog, nazwa)
            if not os.path.isfile(sciezka):
                continue
            canon = tapeta_kanoniczna_nazwa(nazwa)
            if canon in kanoniczne_nazwy:
                raise ValueError(
                    f"Kolizja kanonicznej nazwy: '{nazwa}' i '{kanoniczne_nazwy[canon]}' mapuja sie do '{canon}'"
                )
            kanoniczne_nazwy[canon] = nazwa
            if is_btp and not waliduj_btp(sciezka):
                sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: pomijam niepoprawny plik BTP: {sciezka}\n")
                continue
            if is_jpeg and not waliduj_jpeg(sciezka):
                sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: pomijam niepoprawny plik JPEG: {sciezka}\n")
                continue
            if is_png and not waliduj_png(sciezka):
                sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: pomijam niepoprawny plik PNG: {sciezka}\n")
                continue
            with open(sciezka, "rb") as f:
                zawartosc = f.read()
            rozmiar = len(zawartosc)
            crc32 = binascii.crc32(zawartosc) & 0xFFFFFFFF
            tapety.append({
                "nazwa": nazwa,
                "kanoniczna": canon,
                "sciezka": sciezka,
                "dane": zawartosc,
                "rozmiar": rozmiar,
                "crc32": crc32
            })

    # Wybor i walidacja domyslnej tapety
    domyslna_wpis = None
    if args.domyslna:
        for t in tapety:
            if t["kanoniczna"].lower() == args.domyslna.lower() or t["nazwa"].lower() == args.domyslna.lower():
                domyslna_wpis = t
                break
        if not domyslna_wpis:
            raise ValueError(
                f"[TAPETY PAK] Zadeklarowana domyslna tapeta '{args.domyslna}' nie istnieje w {katalog}!"
            )
    elif tapety:
        # Deterministyczna domyslna: pierwszy poprawny wpis w posortowanym porzadku
        domyslna_wpis = tapety[0]

    # Ustawienie domyslnej tapety na indeksie 0 pakietu
    if domyslna_wpis and tapety and tapety[0] != domyslna_wpis:
        tapety.remove(domyslna_wpis)
        tapety.insert(0, domyslna_wpis)

    if args.manifest:
        linie_m = []
        if domyslna_wpis:
            linie_m.append(f"default={domyslna_wpis['kanoniczna']}")
        for t in tapety:
            linie_m.append(f"wpis={t['kanoniczna']}|{t['rozmiar']}|{t['crc32']:08X}|{t['nazwa']}")
        zapisz_tekst_jesli_zmieniono(args.manifest, "\n".join(linie_m) + "\n")

    if args.manifest_only:
        manifest = ("default=" + domyslna_wpis["kanoniczna"] + "\n" if domyslna_wpis else "") + "".join(t["nazwa"] + "\n" for t in tapety)
        zapisz_tekst_jesli_zmieniono(args.manifest_only, manifest)
        return

    if not tapety:
        sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: brak poprawnych tapet w {katalog}!\n")

    if len(tapety) > 64:
        raise ValueError('BTPK1 runtime accepts at most 64 wallpapers')
    planned_bytes = 32 + sum(16 + ((len(t['nazwa'].encode('utf-8')) + 3) & ~3) +
                             ((t['rozmiar'] + 3) & ~3) for t in tapety)
    u32(planned_bytes, 'total_pack_size')
    if planned_bytes > 256 * 1024 * 1024:
        raise ValueError('wallpaper package exceeds 256 MiB runtime/build budget')

    # Obliczenie rozmiaru tabeli wpisow
    HEADER_SIZE = 32
    MAGIC = b"BTPK1\x00\x00\x00"
    VERSION = 1
    file_count = len(tapety)

    table_entries_bytes = bytearray()
    # Pierwszy przebieg: budowa wpisow tabeli (tymczasowe offsety)
    for t in tapety:
        nazwa_bytes = t["nazwa"].encode("utf-8")
        nazwa_len = len(nazwa_bytes)
        pad_len = (4 - (nazwa_len % 4)) % 4
        # 16 bajtow stalej czesci: nazwa_len, btp_rozmiar, crc32, payload_offset
        # payload_offset zostanie zaktualizowany w drugim przebiegu
        entry_head = struct.pack("<IIII", nazwa_len, t["rozmiar"], t["crc32"], 0)
        table_entries_bytes += entry_head
        table_entries_bytes += nazwa_bytes
        if pad_len:
            table_entries_bytes += b"\x00" * pad_len

    table_size = len(table_entries_bytes)
    data_start = HEADER_SIZE + table_size
    u32(file_count, 'file_count')
    u32(table_size, 'table_size')
    u32(data_start, 'data_start')

    # Drugi przebieg: wyznaczenie wlasciwych payload_offset i zlaczenie danych
    current_payload_offset = data_start
    rebuilt_table_bytes = bytearray()
    payload_bytes = bytearray()

    for t in tapety:
        nazwa_bytes = t["nazwa"].encode("utf-8")
        nazwa_len = len(nazwa_bytes)
        pad_len = (4 - (nazwa_len % 4)) % 4

        u32(t['rozmiar'], 'BTP size')
        u32(current_payload_offset, 'payload offset')
        entry_head = struct.pack("<IIII", nazwa_len, t["rozmiar"], t["crc32"], current_payload_offset)
        rebuilt_table_bytes += entry_head
        rebuilt_table_bytes += nazwa_bytes
        if pad_len:
            rebuilt_table_bytes += b"\x00" * pad_len

        payload_bytes += t["dane"]
        payload_pad = (4 - (t["rozmiar"] % 4)) % 4
        if payload_pad:
            payload_bytes += b"\x00" * payload_pad
        current_payload_offset += t["rozmiar"] + payload_pad

    data_size = len(payload_bytes)
    total_pack_size = HEADER_SIZE + table_size + data_size
    u32(data_size, 'data_size')
    u32(total_pack_size, 'total_pack_size')

    header_bytes = struct.pack(
        "<8sIIIIII",
        MAGIC,
        VERSION,
        file_count,
        HEADER_SIZE,
        table_size,
        data_size,
        total_pack_size
    )

    pack_complete = bytes(header_bytes + rebuilt_table_bytes + payload_bytes)

    # Niezalezny przebieg po gotowym strumieniu sprawdza granice tabeli,
    # offsety oraz CRC kazdego payloadu, zamiast ufania samemu generatorowi.
    magic, version, entries, header_size, packed_table_size, packed_data_size, packed_total = struct.unpack_from(
        "<8sIIIIII", pack_complete, 0)
    if magic != MAGIC or version != VERSION or entries != file_count or header_size != HEADER_SIZE or \
       packed_table_size != table_size or packed_data_size != data_size or packed_total != len(pack_complete):
        raise RuntimeError("walidacja naglowka BTPK1 nie powiodla sie")
    verify_offset = header_size
    verify_table_end = header_size + packed_table_size
    verified_entries = 0
    while verified_entries < entries:
        if verify_offset + 16 > verify_table_end:
            raise RuntimeError("wpis BTPK1 wychodzi poza tabele")
        name_len, payload_size, expected_crc, payload_offset = struct.unpack_from("<IIII", pack_complete, verify_offset)
        verify_offset += 16
        name_pad = (4 - name_len % 4) % 4
        if name_len == 0 or verify_offset + name_len + name_pad > verify_table_end:
            raise RuntimeError("niepoprawna nazwa wpisu BTPK1")
        verify_offset += name_len + name_pad
        if payload_offset < verify_table_end or payload_offset + payload_size > len(pack_complete):
            raise RuntimeError("payload BTPK1 wychodzi poza pakiet")
        actual_crc = binascii.crc32(pack_complete[payload_offset:payload_offset + payload_size]) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise RuntimeError("CRC payloadu BTPK1 nie zgadza sie")
        verified_entries += 1
    if verify_offset != verify_table_end:
        raise RuntimeError("tabela BTPK1 ma nadmiarowe bajty")
    zapisz_jesli_zmieniono(args.wyjscie, pack_complete)

    # Generowanie zaleznosci Makefile (.d)
    if args.dep:
        zaleznosci = [katalog, "narzedzia/tapety-btp/generujpakiet.py", "narzedzia/tapety-btp/btpcodec.py"]
        for t in tapety:
            zaleznosci.append(t["sciezka"].replace(" ", "\\ "))
        linie_d = [
            f"{args.wyjscie}: " + " ".join(zaleznosci),
            ""
        ]
        for t in tapety:
            linie_d.append(f"{t['sciezka'].replace(' ', '\\ ')}:")
        linie_d.append("")
        zapisz_tekst_jesli_zmieniono(args.dep, "\n".join(linie_d))

    # Raport statystyk
    total_btp_bytes = sum(t["rozmiar"] for t in tapety)
    sys.stdout.write(f"[TAPETY PAK] Zbudowano pakiet tapet: {args.wyjscie}\n")
    sys.stdout.write(f"[TAPETY PAK] Liczba tapet: {file_count}\n")
    for idx, t in enumerate(tapety):
        def_mark = " [DEFAULT]" if t == domyslna_wpis else ""
        sys.stdout.write(f"  [{idx:02d}] {t['nazwa']} (canon: {t['kanoniczna']}){def_mark}: {t['rozmiar']} B (CRC32: 0x{t['crc32']:08X})\n")
    sys.stdout.write(f"[TAPETY PAK] Laczny rozmiar zrodlowych BTP: {total_btp_bytes} B ({total_btp_bytes / 1024 / 1024:.2f} MiB)\n")
    sys.stdout.write(f"[TAPETY PAK] Laczny rozmiar {args.wyjscie}: {len(pack_complete)} B ({len(pack_complete) / 1024 / 1024:.2f} MiB)\n")
    sys.stdout.write(f"[TAPETY BUILD] sources={len(tapety)} entries={verified_entries} bytes={len(pack_complete)} crc=OK\n")

if __name__ == "__main__":
    main()
