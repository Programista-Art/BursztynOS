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

def bezpieczna_nazwa(nazwa: str) -> bool:
    if not nazwa or len(nazwa) > 120 or nazwa.startswith("."):
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
            ch = chr(c)
            if not (ch.isalnum() or ch in "_- ."):
                return False
            i += 1
        else:
            if 0xC2 <= c <= 0xDF:
                ile = 2
            elif 0xE0 <= c <= 0xEF:
                ile = 3
            elif 0xF0 <= c <= 0xF4:
                ile = 4
            else:
                return False
            if i + ile > n:
                return False
            i += ile
    return True

def waliduj_btp(sciezka: str) -> bool:
    try:
        rozmiar = os.path.getsize(sciezka)
        if rozmiar < 14:
            return False
        with open(sciezka, "rb") as f:
            naglowek = f.read(14)
        if len(naglowek) < 14:
            return False
        magic, w, h, bpp, kompresja = struct.unpack("<4sIIBB", naglowek)
        if magic != b"BTP1":
            return False
        if w == 0 or h == 0 or w > 8192 or h > 8192:
            return False
        if bpp != 32:
            return False
        if kompresja not in (0, 1):
            return False
        return True
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
    parser.add_argument("--katalog", "-k", default="obrazy/tapety", help="Katalog zrodlowy tapet .btp")
    parser.add_argument("--wyjscie", "-w", default="tapety.pak", help="Sciezka wyjsciowa pliku pakietu (.pak)")
    parser.add_argument("--dep", "-d", default="tapety.pak.d", help="Sciezka wyjsciowa zaleznosci dla Make (.d)")
    args = parser.parse_args()

    katalog = args.katalog
    tapety = []

    if os.path.isdir(katalog):
        wpisy = sorted(os.listdir(katalog))
        for nazwa in wpisy:
            if not nazwa.endswith(".btp"):
                continue
            if not bezpieczna_nazwa(nazwa):
                sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: pomijam niebezpieczna nazwe pliku: {nazwa}\n")
                continue
            sciezka = os.path.join(katalog, nazwa)
            if not os.path.isfile(sciezka):
                continue
            if not waliduj_btp(sciezka):
                sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: pomijam niepoprawny plik BTP: {sciezka}\n")
                continue
            with open(sciezka, "rb") as f:
                zawartosc = f.read()
            rozmiar = len(zawartosc)
            crc32 = binascii.crc32(zawartosc) & 0xFFFFFFFF
            tapety.append({
                "nazwa": nazwa,
                "sciezka": sciezka,
                "dane": zawartosc,
                "rozmiar": rozmiar,
                "crc32": crc32
            })

    if not tapety:
        sys.stderr.write(f"[TAPETY PAK] Ostrzezenie: brak poprawnych tapet w {katalog}!\n")

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

    # Drugi przebieg: wyznaczenie wlasciwych payload_offset i zlaczenie danych
    current_payload_offset = data_start
    rebuilt_table_bytes = bytearray()
    payload_bytes = bytearray()

    for t in tapety:
        nazwa_bytes = t["nazwa"].encode("utf-8")
        nazwa_len = len(nazwa_bytes)
        pad_len = (4 - (nazwa_len % 4)) % 4

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
    zapisz_jesli_zmieniono(args.wyjscie, pack_complete)

    # Generowanie zaleznosci Makefile (.d)
    if args.dep:
        zaleznosci = [katalog, "narzedzia/tapety-btp/generujpakiet.py"]
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
        sys.stdout.write(f"  [{idx:02d}] {t['nazwa']}: {t['rozmiar']} B (CRC32: 0x{t['crc32']:08X})\n")
    sys.stdout.write(f"[TAPETY PAK] Laczny rozmiar zrodlowych BTP: {total_btp_bytes} B ({total_btp_bytes / 1024 / 1024:.2f} MiB)\n")
    sys.stdout.write(f"[TAPETY PAK] Laczny rozmiar {args.wyjscie}: {len(pack_complete)} B ({len(pack_complete) / 1024 / 1024:.2f} MiB)\n")

if __name__ == "__main__":
    main()
