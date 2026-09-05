#!/usr/bin/env python3
"""
generujmanifest.py - Automatyczne odkrywanie i generowanie manifestu tapet BTP dla Bursztyn OS.
Odkrywa pliki .btp w katalogu zrodlowym (obrazy/tapety), waliduje je i tworzy:
  1. tapety_manifest.h - deklaracje symboli i tablice zasobow dla jadra
  2. tapetyzasoby.S    - asemblerowe dyrektywy .incbin z deterministycznymi symbolami ASCII
  3. tapetyzasoby.d    - zaleznosci dla Makefile
"""

import argparse
import binascii
import os
import struct
import sys

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

def zapisz_jesli_zmieniono(sciezka: str, nowa_zawartosc: str):
    if os.path.isfile(sciezka):
        try:
            with open(sciezka, "r", encoding="utf-8") as f:
                stara = f.read()
            if stara == nowa_zawartosc:
                return
        except OSError:
            pass
    with open(sciezka, "w", encoding="utf-8") as f:
        f.write(nowa_zawartosc)

def main():
    parser = argparse.ArgumentParser(description="Generuj manifest tapet BTP dla Bursztyn OS")
    parser.add_argument("--katalog", "-k", default="obrazy/tapety", help="Katalog zrodlowy tapet .btp")
    parser.add_argument("--manifest", "-m", default="tapety_manifest.h", help="Sciezka wyjsciowa naglowka C++")
    parser.add_argument("--asm", "-a", default="tapetyzasoby.S", help="Sciezka wyjsciowa pliku assemblera (.S)")
    parser.add_argument("--dep", "-d", default="tapetyzasoby.d", help="Sciezka wyjsciowa zaleznosci dla Make")
    args = parser.parse_args()

    katalog = args.katalog
    tapety = []

    if os.path.isdir(katalog):
        wpisy = sorted(os.listdir(katalog))
        for nazwa in wpisy:
            if not nazwa.endswith(".btp"):
                continue
            if not bezpieczna_nazwa(nazwa):
                sys.stderr.write(f"[TAPETA GENERATOR] Ostrzezenie: pomijam niebezpieczna nazwe pliku: {nazwa}\n")
                continue
            sciezka = os.path.join(katalog, nazwa)
            if not os.path.isfile(sciezka):
                continue
            if not waliduj_btp(sciezka):
                sys.stderr.write(f"[TAPETA GENERATOR] Ostrzezenie: pomijam uszkodzony plik BTP: {sciezka}\n")
                continue
            rozmiar = os.path.getsize(sciezka)
            with open(sciezka, "rb") as f:
                zawartosc = f.read()
            crc32 = binascii.crc32(zawartosc) & 0xFFFFFFFF
            tapety.append((nazwa, sciezka, rozmiar, crc32))

    if not tapety:
        sys.stderr.write(f"[TAPETA GENERATOR] Ostrzezenie: brak poprawnych tapet .btp w {katalog}!\n")

    # 1. Generowanie pliku assemblera (.S) z .incbin
    linie_asm = [
        "// Automatycznie wygenerowany plik przez generujmanifest.py. Nie edytuj recznie.",
        ".section .rodata.tapety,\"a\",@progbits",
        ""
    ]
    for idx, (nazwa, sciezka, _, _) in enumerate(tapety):
        sym_start = f"btpzasob{idx:03d}start"
        sym_end = f"btpzasob{idx:03d}end"
        linie_asm.append(".balign 16")
        linie_asm.append(f".global {sym_start}")
        linie_asm.append(f".global {sym_end}")
        linie_asm.append(f"{sym_start}:")
        linie_asm.append(f'.incbin "{sciezka}"')
        linie_asm.append(f"{sym_end}:")
        linie_asm.append("")

    zapisz_jesli_zmieniono(args.asm, "\n".join(linie_asm))

    # 2. Generowanie tapety_manifest.h
    linie_h = [
        "// Automatycznie wygenerowany plik przez generujmanifest.py. Nie edytuj recznie.",
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "struct TapetaZasob {",
        "    const char* nazwa;",
        "    const uint8_t* poczatek;",
        "    const uint8_t* koniec;",
        "    size_t rozmiar;",
        "    uint32_t crc32;",
        "};",
        ""
    ]

    if tapety:
        linie_h.append("extern \"C\" {")
        for idx in range(len(tapety)):
            sym_start = f"btpzasob{idx:03d}start"
            sym_end = f"btpzasob{idx:03d}end"
            linie_h.append(f"extern const uint8_t {sym_start}[];")
            linie_h.append(f"extern const uint8_t {sym_end}[];")
        linie_h.append("}")
        linie_h.append("")
        linie_h.append("inline constexpr TapetaZasob SYSTEMOWE_TAPETY_ZASOBY[] = {")
        for idx, (nazwa, _, rozmiar, crc32) in enumerate(tapety):
            sym_start = f"btpzasob{idx:03d}start"
            sym_end = f"btpzasob{idx:03d}end"
            linie_h.append(f"    {{ \"{nazwa}\", {sym_start}, {sym_end}, {rozmiar}U, 0x{crc32:08X}U }},")
        linie_h.append("};")
        linie_h.append("inline constexpr size_t SYSTEMOWE_TAPETY_LICZBA = sizeof(SYSTEMOWE_TAPETY_ZASOBY) / sizeof(SYSTEMOWE_TAPETY_ZASOBY[0]);")
    else:
        linie_h.append("inline constexpr TapetaZasob SYSTEMOWE_TAPETY_ZASOBY[1] = {")
        linie_h.append("    { nullptr, nullptr, nullptr, 0U, 0U }")
        linie_h.append("};")
        linie_h.append("inline constexpr size_t SYSTEMOWE_TAPETY_LICZBA = 0;")

    linie_h.append("")
    zapisz_jesli_zmieniono(args.manifest, "\n".join(linie_h))

    # 3. Generowanie pliku zaleznosci (.d)
    skrypt = "narzedzia/tapety-btp/generujmanifest.py"
    zaleznosci = [katalog, skrypt]
    for _, sciezka, _, _ in tapety:
        zaleznosci.append(sciezka.replace(" ", "\\ "))

    obj_name = os.path.splitext(args.asm)[0] + ".o"
    linie_d = [
        f"{args.manifest} {args.asm} {obj_name}: " + " ".join(zaleznosci),
        ""
    ]
    for _, sciezka, _, _ in tapety:
        linie_d.append(f"{sciezka.replace(' ', '\\ ')}:")
    linie_d.append("")
    zapisz_jesli_zmieniono(args.dep, "\n".join(linie_d))

if __name__ == "__main__":
    main()
