#!/usr/bin/env python3
"""
generujmanifest.py - Automatyczne odkrywanie i generowanie manifestu tapet BTP dla Bursztyn OS.
Odkrywa pliki .btp w katalogu zrodlowym (obrazy/tapety), waliduje je i tworzy:
  1. tapety_manifest.h - deklaracje symboli i tablice zasobow dla jadra
  2. tapety_zasoby.rsp - liste plikow dla ld -r -b binary
  3. tapety_zasoby.d   - zaleznosci dla Makefile
"""

import argparse
import os
import re
import struct
import sys

def bezpieczna_nazwa(nazwa: str) -> bool:
    if not nazwa or len(nazwa) > 120 or nazwa.startswith("."):
        return False
    if "/" in nazwa or "\\" in nazwa or "\0" in nazwa or "\r" in nazwa or "\n" in nazwa or ".." in nazwa:
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

def symbol_ld(sciezka: str) -> str:
    # ld zastepuje dowolny znak poza [a-zA-Z0-9] podkresleniem
    zastapione = re.sub(r"[^a-zA-Z0-9]", "_", sciezka)
    return f"_binary_{zastapione}"

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
    parser.add_argument("--rsp", "-r", default="tapety_zasoby.rsp", help="Sciezka wyjsciowa pliku odpowiedzi dla ld")
    parser.add_argument("--dep", "-d", default="tapety_zasoby.d", help="Sciezka wyjsciowa zaleznosci dla Make")
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
            sym = symbol_ld(sciezka)
            tapety.append((nazwa, sciezka, rozmiar, sym))

    if not tapety:
        sys.stderr.write(f"[TAPETA GENERATOR] Ostrzezenie: brak poprawnych tapet .btp w {katalog}!\n")

    # 1. Generowanie tapety_manifest.h
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
        "};",
        ""
    ]

    if tapety:
        linie_h.append("extern \"C\" {")
        for _, _, _, sym in tapety:
            linie_h.append(f"extern const uint8_t {sym}_start[];")
            linie_h.append(f"extern const uint8_t {sym}_end[];")
        linie_h.append("}")
        linie_h.append("")
        linie_h.append("inline constexpr TapetaZasob SYSTEMOWE_TAPETY_ZASOBY[] = {")
        for nazwa, _, rozmiar, sym in tapety:
            linie_h.append(f"    {{ \"{nazwa}\", {sym}_start, {sym}_end, {rozmiar}U }},")
        linie_h.append("};")
        linie_h.append("inline constexpr size_t SYSTEMOWE_TAPETY_LICZBA = sizeof(SYSTEMOWE_TAPETY_ZASOBY) / sizeof(SYSTEMOWE_TAPETY_ZASOBY[0]);")
    else:
        linie_h.append("inline constexpr TapetaZasob SYSTEMOWE_TAPETY_ZASOBY[1] = {")
        linie_h.append("    { nullptr, nullptr, nullptr, 0U }")
        linie_h.append("};")
        linie_h.append("inline constexpr size_t SYSTEMOWE_TAPETY_LICZBA = 0;")

    linie_h.append("")
    zapisz_jesli_zmieniono(args.manifest, "\n".join(linie_h))

    # 2. Generowanie tapety_zasoby.rsp
    linie_rsp = []
    for _, sciezka, _, _ in tapety:
        linie_rsp.append(f"\"{sciezka}\"")
    zapisz_jesli_zmieniono(args.rsp, "\n".join(linie_rsp) + ("\n" if linie_rsp else ""))

    # 3. Generowanie tapety_zasoby.d
    skrypt = "narzedzia/tapety-btp/generujmanifest.py"
    zaleznosci = [katalog, skrypt]
    for _, sciezka, _, _ in tapety:
        zaleznosci.append(sciezka.replace(" ", "\\ "))

    linie_d = [
        f"{args.manifest} tapety_zasoby.o: " + " ".join(zaleznosci),
        ""
    ]
    for _, sciezka, _, _ in tapety:
        linie_d.append(f"{sciezka.replace(' ', '\\ ')}:")
    linie_d.append("")
    zapisz_jesli_zmieniono(args.dep, "\n".join(linie_d))

if __name__ == "__main__":
    main()
