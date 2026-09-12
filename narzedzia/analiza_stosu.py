#!/usr/bin/env python3
"""
narzedzia/analiza_stosu.py - Inspekcja rozmiaru ramek stosu jadra Bursztyn OS (build-time stack frame analyzer).
Parsuje pliki .su wygenerowane przez GCC z opcja -fstack-usage i weryfikuje limity rozmiaru ramek.

Klasyfikacje GCC w plikach .su:
  static           - staly, znany rozmiar ramki; liczba bajtow jest miarodajna.
  dynamic,bounded  - rozmiar zalezny od danych, ale z gorna granica; liczba
                     bajtow jest ta granica i podlega tym samym limitom.
  dynamic          - rozmiar w pelni nieznany (np. alloca/VLA o nieznanym
                     rozmiarze); NIE WOLNO traktowac go jako zero. Raportowany
                     osobno, zawsze wymaga recznej inspekcji.

Skrypt celowo nie sumuje ramek: suma rozmiarow funkcji nie jest uzyciem
stosu (ramki wspolistnieja tylko na sciezce wywolania, a -O2 wstawia
i zmienia zywotnosc zmiennych). Limity dotycza pojedynczych ramek.
"""

import sys
import os
import glob
import re
import argparse

ASM_ONLY_OBJS = {"boot.o", "przerwania.o", "syscall.o", "ring3.o"}


def parse_su_file(filepath):
    static_entries = []
    bounded_entries = []
    dynamic_entries = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='replace') as fp:
            for line in fp:
                line = line.strip()
                if not line:
                    continue
                parts = line.split('\t')
                if len(parts) < 2:
                    continue
                loc = parts[0]
                try:
                    size = int(parts[1])
                except ValueError:
                    continue
                kind = parts[2].strip() if len(parts) > 2 else 'static'
                if kind == 'static':
                    static_entries.append((size, loc, filepath))
                elif kind == 'dynamic,bounded':
                    bounded_entries.append((size, loc, filepath))
                elif kind == 'dynamic':
                    dynamic_entries.append((size, loc, filepath))
                else:
                    # Nieznana przyszla klasyfikacja: traktuj konserwatywnie
                    # jak dynamic (nieznane), nigdy jak zero.
                    dynamic_entries.append((size, loc + ' [' + kind + ']', filepath))
    except Exception as e:
        print(f"[BLAD] Nie mozna odczytac {filepath}: {e}", file=sys.stderr)
    return static_entries, bounded_entries, dynamic_entries


def collect_su_files(root):
    pats = [
        os.path.join(root, "*.su"),
        os.path.join(root, "*", "*.su"),
        os.path.join(root, "*", "*", "*.su"),
        os.path.join(root, "*", "*", "*", "*.su"),
    ]
    out = set()
    for p in pats:
        out.update(glob.glob(p))
    return sorted(out)


def expected_su_from_makefile(makefile_path, root):
    """Zwraca (oczekiwane .su wzgledem root, asm_only, blob_count).

    Parsuje OBJS z Makefile. Pomija obiekty binarne (*_blob.o) oraz
    dopisuje .su dla kazdego C/C++ TU; pliki .S sa liczone osobno.
    """
    with open(makefile_path, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()
    m = re.search(r'^OBJS\s*:=\s*(.*?)(?=\n[^\s\\])', text, re.M | re.S)
    if not m:
        return None, None, None
    chunk = m.group(1).replace('\\\n', ' ')
    tokens = chunk.split()
    # Rozwiniecie $(MBEDTLS_OBJS) przez istniejace zrodla .c.
    expanded = []
    for t in tokens:
        if t == '$(MBEDTLS_OBJS)':
            for c in sorted(glob.glob(os.path.join(root, 'biblioteki', 'mbedtls', 'library', '*.c'))):
                rel = os.path.relpath(c, root)
                expanded.append(os.path.splitext(rel)[0] + '.o')
        elif t.startswith('$'):
            continue
        else:
            expanded.append(t)
    expected = {}
    asm_only = []
    blobs = 0
    for o in expanded:
        base = os.path.basename(o)
        # Obiekty binarne (ld -r -b binary): konwencja *_blob.o oraz
        # historyczna kontrolki3blob.o. Nie zawieraja kodu, nie maja .su.
        if base.endswith('blob.o'):
            blobs += 1
            continue
        if base in ASM_ONLY_OBJS:
            asm_only.append(o)
            continue
        su_rel = os.path.splitext(o)[0] + '.su'
        expected[o] = su_rel
    return expected, asm_only, blobs


def main():
    parser = argparse.ArgumentParser(description="Analiza rozmiaru ramek stosu jadra Bursztyn OS (-fstack-usage).")
    parser.add_argument("--dir", default=".", help="Katalog glowny repo (domyslnie '.')")
    parser.add_argument("--max-frame", type=int, default=6144, help="Maksymalny dopuszczalny rozmiar ramki pojedynczej funkcji w bajtach (domyslnie 6144)")
    parser.add_argument("--warn-frame", type=int, default=3072, help="Prog ostrzezenia o duzej ramce w bajtach (domyslnie 3072)")
    parser.add_argument("--top", type=int, default=15, help="Liczba funkcji do wyswietlenia na liscie (domyslnie 15)")
    parser.add_argument("--makefile", default=None, help="Sciezka do Makefile do weryfikacji pokrycia OBJS (opcjonalnie)")
    parser.add_argument("--fail-on-missing", action="store_true", help="Zakoncz bledem, gdy oczekiwany .su nie istnieje")
    args = parser.parse_args()

    root = args.dir
    all_files = collect_su_files(root)

    static_all, bounded_all, dynamic_all = [], [], []
    for f in all_files:
        s, b, d = parse_su_file(f)
        static_all.extend(s)
        bounded_all.extend(b)
        dynamic_all.extend(d)

    # ---- Weryfikacja pokrycia jednostek translacji ----
    expected = asm_only = blobs = None
    missing = []
    if args.makefile:
        expected, asm_only, blobs = expected_su_from_makefile(args.makefile, root)
        if expected is None:
            print("[BLAD] Nie udalo sie sparsowac OBJS z Makefile.", file=sys.stderr)
            sys.exit(2)
        have = {os.path.relpath(p, root) for p in all_files}
        for o, su_rel in sorted(expected.items()):
            if su_rel not in have:
                missing.append((o, su_rel))

    static_all.sort(reverse=True, key=lambda x: x[0])
    bounded_all.sort(reverse=True, key=lambda x: x[0])

    print("=" * 80)
    print(" BURSZTYN OS - AUDYT ROZMIARU RAMEK STOSU JADRA (BUILD-TIME STACK USAGE)")
    print("=" * 80)
    print(f"Liczba przeanalizowanych plikow .su: {len(all_files)}")
    if expected is not None:
        print(f"Oczekiwane jednostki C/C++ (OBJS):    {len(expected)}")
        print(f"Pokryte przez .su:                    {len(expected) - len(missing)}")
        print(f"Brakujace .su:                        {len(missing)}")
        print(f"Obiekty assembler-only (bez .su):     {len(asm_only)} ({', '.join(sorted({os.path.basename(a) for a in asm_only}))})")
        print(f"Obiekty binarne (*blob.o, bez .su):     {blobs}")
    print(f"Liczba przeanalizowanych funkcji:     {len(static_all) + len(bounded_all) + len(dynamic_all)}")
    print(f"  static:                             {len(static_all)}")
    print(f"  dynamic,bounded:                    {len(bounded_all)}")
    print(f"  dynamic (nieznane, nie zero!):      {len(dynamic_all)}")
    print(f"Limit bledu (max frame):             {args.max_frame} B ({args.max_frame // 1024} KiB)")
    print(f"Prog ostrzezenia (warn frame):       {args.warn_frame} B ({args.warn_frame // 1024} KiB)")
    print("-" * 80)
    print(f"{'Rozmiar':>10}  {'Typ':<16}  {'Lokalizacja i funkcja'}")
    print("-" * 80)

    warnings = []
    errors = []

    def oceniaj(size, loc, path, kind):
        status = " "
        if size > args.max_frame:
            status = "!"
            errors.append((size, loc, path, kind))
        elif size > args.warn_frame:
            status = "*"
            warnings.append((size, loc, path, kind))
        return status

    top_static = static_all[:args.top]
    top_bounded = bounded_all[:args.top]
    for size, loc, path in top_static:
        print(f"{size:8d} B  {'static':<16}  {loc} {oceniaj(size, loc, path, 'static')}")
    for size, loc, path in top_bounded:
        print(f"{size:8d} B  {'dynamic,bounded':<16}  {loc} {oceniaj(size, loc, path, 'dynamic,bounded')}")

    for size, loc, path in static_all[args.top:]:
        oceniaj(size, loc, path, 'static')
    for size, loc, path in bounded_all[args.top:]:
        oceniaj(size, loc, path, 'dynamic,bounded')

    print("-" * 80)

    if dynamic_all:
        print(f"[UWAGA] {len(dynamic_all)} funkcji ma W PELNI dynamiczne (nieznane) zuzycie stosu.")
        print("Nie sa one liczone jako zero; kazda wymaga recznej inspekcji:")
        for size, loc, path in sorted(dynamic_all, reverse=True)[:20]:
            print(f"  - {os.path.basename(path)}: {loc}")
        if len(dynamic_all) > 20:
            print(f"  ... oraz {len(dynamic_all) - 20} kolejnych.")
        print("-" * 80)

    if missing:
        print(f"[BLAD POKRYCIA] {len(missing)} oczekiwanych jednostek nie ma pliku .su:")
        for o, su_rel in missing:
            print(f"  - {o} -> brak {su_rel}")
        print("Przyczyna to zwykle przyrostowa kompilacja albo brak -fstack-usage")
        print("dla danej reguly. Uruchom cel `make audit-stos-jadra` (czysta przebudowa).")
        print("-" * 80)

    if warnings:
        print(f"[OSTRZEZENIE] {len(warnings)} funkcji przekracza prog ostrzegawczy ({args.warn_frame} B):")
        for size, loc, path, kind in warnings:
            print(f"  - {size} B [{kind}] {os.path.basename(path)}: {loc}")

    if errors:
        print(f"[BLAD KRYTYCZNY] {len(errors)} funkcji przekracza twardy limit ({args.max_frame} B):")
        for size, loc, path, kind in errors:
            print(f"  - {size} B [{kind}] {os.path.basename(path)}: {loc}")
        print("Kompilacja przerwana z powodu ryzyka przepelnienia stosu jadra!")
        sys.exit(1)

    if missing and args.fail_on_missing:
        print("Audyt przerwany z powodu niepelnego pokrycia .su!")
        sys.exit(1)

    if static_all:
        print(f"[SUKCES] Zadna zweryfikowana funkcja nie przekracza twardego limitu ({args.max_frame} B).")
        print(f"Najwieksza ramka statyczna: {static_all[0][0]} B ({static_all[0][1]}).")
    else:
        print("[SUKCES] Brak wpisow statycznych do oceny.")
    if not dynamic_all:
        print("Brak funkcji o w pelni dynamicznym zuzyciu.")
    print("=" * 80)
    sys.exit(0)


if __name__ == "__main__":
    main()
