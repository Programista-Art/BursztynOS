#!/usr/bin/env python3
"""
narzedzia/analiza_stosu.py - Inspekcja rozmiaru ramek stosu jadra Bursztyn OS (build-time stack frame analyzer).
Parsuje pliki .su wygenerowane przez GCC z opcja -fstack-usage i weryfikuje limity rozmiaru ramek.
"""

import sys
import os
import glob
import argparse

def parse_su_file(filepath):
    results = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='replace') as fp:
            for line in fp:
                line = line.strip()
                if not line:
                    continue
                parts = line.split('\t')
                if len(parts) >= 2:
                    loc = parts[0]
                    try:
                        size = int(parts[1])
                    except ValueError:
                        continue
                    kind = parts[2] if len(parts) > 2 else 'static'
                    results.append((size, loc, kind))
    except Exception as e:
        print(f"[BLAD] Nie mozna odczytac {filepath}: {e}", file=sys.stderr)
    return results

def main():
    parser = argparse.ArgumentParser(description="Analiza rozmiaru ramek stosu jadra Bursztyn OS (-fstack-usage).")
    parser.add_argument("--dir", default=".", help="Katalog z plikami .su (domyslnie '.')")
    parser.add_argument("--max-frame", type=int, default=6144, help="Maksymalny dopuszczalny rozmiar ramki pojedynczej funkcji w bajtach (domyslnie 6144)")
    parser.add_argument("--warn-frame", type=int, default=3072, help="Prog ostrzezenia o duzej ramce w bajtach (domyslnie 3072)")
    parser.add_argument("--top", type=int, default=15, help="Liczba funkcji do wyswietlenia na liscie (domyslnie 15)")
    args = parser.parse_args()

    su_pattern = os.path.join(args.dir, "*.su")
    su_files = glob.glob(su_pattern)
    sub_su = glob.glob(os.path.join(args.dir, "*", "*.su")) + glob.glob(os.path.join(args.dir, "*", "*", "*.su"))
    all_files = sorted(list(set(su_files + sub_su)))

    if not all_files:
        print(f"[STACK-AUDIT] Brak plikow .su w {args.dir}. Upewnij sie, ze kod zostal skompilowany z -fstack-usage.")
        sys.exit(0)

    all_entries = []
    for f in all_files:
        all_entries.extend(parse_su_file(f))

    all_entries.sort(reverse=True, key=lambda x: x[0])

    print("=" * 80)
    print(" BURSZTYN OS - AUDYT ROZMIARU RAMEK STOSU JADRA (BUILD-TIME STACK USAGE)")
    print("=" * 80)
    print(f"Liczba przeanalizowanych plikow .su: {len(all_files)}")
    print(f"Liczba przeanalizowanych funkcji:     {len(all_entries)}")
    print(f"Limit bledu (max frame):             {args.max_frame} B ({args.max_frame // 1024} KiB)")
    print(f"Prog ostrzezenia (warn frame):       {args.warn_frame} B ({args.warn_frame // 1024} KiB)")
    print("-" * 80)
    print(f"{'Rozmiar':>10}  {'Typ':<8}  {'Lokalizacja i funkcja'}")
    print("-" * 80)

    warnings = []
    errors = []

    for size, loc, kind in all_entries[:args.top]:
        status = " "
        if size > args.max_frame:
            status = "!"
            errors.append((size, loc))
        elif size > args.warn_frame:
            status = "*"
            warnings.append((size, loc))
        print(f"{size:8d} B  {kind:<8}  {loc} {status}")

    # Sprawdz czy jakiekolwiek funkcje spoza top przekraczaja progi
    for size, loc, kind in all_entries[args.top:]:
        if size > args.max_frame:
            errors.append((size, loc))
        elif size > args.warn_frame:
            warnings.append((size, loc))

    print("-" * 80)

    if warnings:
        print(f"[OSTRZEZENIE] {len(warnings)} funkcji przekracza prog ostrzegawczy ({args.warn_frame} B):")
        for size, loc in warnings:
            print(f"  - {size} B: {loc}")

    if errors:
        print(f"[BLAD KRYTYCZNY] {len(errors)} funkcji przekracza twardy limit ({args.max_frame} B):")
        for size, loc in errors:
            print(f"  - {size} B: {loc}")
        print("Kompilacja przerwana z powodu ryzyka przepelnienia stosu jadra!")
        sys.exit(1)

    print(f"[SUKCES] Zadna funkcja nie przekracza twardego limitu ({args.max_frame} B).")
    print(f"Najwieksza ramka w jadrze: {all_entries[0][0]} B ({all_entries[0][1].split(' ')[-1] if all_entries else 'brak'}).")
    print("=" * 80)
    sys.exit(0)

if __name__ == "__main__":
    main()
