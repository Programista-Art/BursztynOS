#!/usr/bin/env python3
"""Generuje aktualizacja_klucz.h na podstawie wybranego trybu zaufania i klucza publicznego.

Uzycie:
  python3 generuj_naglowek_klucza.py [--tryb release|test|dev] [--publiczny PLIK_PEM] [--wyjscie aktualizacja_klucz.h]
"""
import argparse
import hashlib
import os
import sys
from cryptography.hazmat.primitives import serialization

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NARZ = os.path.join(REPO, "narzedzia", "aktualizacje")


def main():
    ap = argparse.ArgumentParser(description="Generator naglowka klucza aktualizacji Bursztyn OS")
    ap.add_argument("--tryb", choices=["release", "test", "dev"], default="release",
                    help="Tryb zaufania (domyslnie: release)")
    ap.add_argument("--publiczny", default=None,
                    help="Sciezka do klucza publicznego PEM (domyslnie wg trybu)")
    ap.add_argument("--wyjscie", default=os.path.join(REPO, "jadro/aktualizacja/aktualizacja_klucz.h"),
                    help="Sciezka wyjsciowa naglowka (domyslnie: aktualizacja_klucz.h)")
    args = ap.parse_args()

    pem_path = args.publiczny
    if not pem_path:
        if args.tryb == "test":
            pem_path = os.path.join(NARZ, "klucz-testowy-publiczny.pem")
        elif args.tryb == "release":
            pem_path = os.path.join(NARZ, "klucz-wydanie-publiczny.pem")
        else:
            sys.exit("BLAD: Tryb dev wymaga podania --publiczny")

    if not os.path.exists(pem_path):
        sys.exit(f"BLAD: Plik klucza publicznego nie istnieje: {pem_path}")

    with open(pem_path, "rb") as f:
        pub = serialization.load_pem_public_key(f.read())

    num = pub.public_numbers()
    if num.e != 65537:
        sys.exit("BLAD: Obslugiwany jest tylko wykladnik e=65537")

    n_bytes = num.n.to_bytes(256, "big")
    if len(n_bytes) != 256:
        sys.exit("BLAD: Rozmiar klucza musi wynosic dokladnie 2048 bitow (256 bajtow)")

    der = pub.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    fp = hashlib.sha256(der).hexdigest()

    is_test = 1 if args.tryb == "test" else 0
    key_id = f"bursztyn-{args.tryb}-2026-rsa2048"

    lines = [
        "/*",
        f" * Bursztyn OS - wbudowany klucz publiczny aktualizacji ({args.tryb.upper()}).",
        " * Ten plik jest generowany automatycznie lub zarzadzany konfiguracja budowania.",
        f" * Odcisk SHA-256 (DER): {fp}",
        " */",
        "",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        '#define AKTUALIZACJA_KLUCZ_ALGORYTM "RSA-2048-PKCS1v15-SHA256"',
        f'#define AKTUALIZACJA_KLUCZ_TRYB "{args.tryb}"',
        f'#define AKTUALIZACJA_KLUCZ_JEST_TESTOWY {is_test}',
        f'#define AKTUALIZACJA_KLUCZ_ID "{key_id}"',
        f'#define AKTUALIZACJA_KLUCZ_ODCISK_SHA256 "{fp}"',
        '#define AKTUALIZACJA_KLUCZ_ROZMIAR 256U',
        '#define AKTUALIZACJA_KLUCZ_E 65537U',
        "",
        "/* Modul N (big-endian, 256 bajtow). */",
        "static const uint8_t AKTUALIZACJA_KLUCZ_N[256] = {"
    ]

    for row in range(16):
        chunk = n_bytes[row*16:(row+1)*16]
        hex_row = ", ".join(f"0x{b:02X}" for b in chunk)
        comma = "," if row < 15 else ""
        lines.append(f"    {hex_row}{comma}")
    lines.append("};")
    lines.append("")

    content = "\n".join(lines)
    # Check if content changed to avoid unnecessary rebuilds
    old_content = ""
    if os.path.exists(args.wyjscie):
        try:
            with open(args.wyjscie, "r", encoding="utf-8") as f:
                old_content = f.read()
        except Exception:
            pass

    if content != old_content:
        with open(args.wyjscie, "w", encoding="utf-8") as f:
            f.write(content)

    print(f"[AKTUALIZACJA BUILD] Klucz aktualizacji {args.tryb.upper()} (odcisk: {fp[:16]}...)")


if __name__ == "__main__":
    main()
