#!/usr/bin/env python3
"""Kazdy slaby symbol niezdefiniowany w obiektach jadra musi byc zdefiniowany w zlinkowanym obrazie.

Regresja bledu "okien-duchow": scheduler deklarowal slaby hook `extern "C" usun_warstwe`, a kompozytor
eksportowal `usun_warstwe(int)` z linkowaniem C++ (_Z12usun_warstwei). Slaby symbol pozostawal pusty, wiec
sprzatanie procesu zakonczonego z zewnatrz (BWS 97/104, eskalacja okien podrzednych) nie usuwalo warstwy
okna. Kompilacja i linkowanie przechodzily bez ostrzezenia - wykrywa to dopiero ten test.

Uzycie: test_slabe_hooki_jadra.py <obraz_jadra.elf> <obiekt.o>...
Symbole celowo opcjonalne (np. hooki diagnostyczne) mozna dopisac do OPCJONALNE z uzasadnieniem.
"""
import subprocess
import sys

# Slabe referencje, ktorych brak w obrazie jest zamierzony (nazwa -> powod). Obecnie brak takich.
OPCJONALNE = {}

# Hooki sprzatania procesu, ktore MUSZA istniec (jawnie, niezaleznie od skanu).
WYMAGANE_SCHEDULER = [
    "_Z12usun_warstwei",
    "_Z29skladacz_obrazu_bajty_warstwyi",
    "bws_gui_powiadom_lifecycle",
    "bws_gui_usun_stan_procesu",
    "bws_gui_zwolnij_mysz_procesu",
    "bws_nowoczesne_posprzataj_proces",
    "okna_podrzedne_przed_sprzataniem",
    "ZniszczPrzestrzenAdresowaProcesu",
]


def nm(path):
    out = subprocess.run(["nm", path], check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            yield parts[1], parts[2]
        elif len(parts) == 2:
            yield parts[0], parts[1]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    obraz, obiekty = sys.argv[1], sys.argv[2:]
    zdefiniowane = {s for t, s in nm(obraz) if t not in ("U", "w", "v")}
    bledy = []
    slabe = 0
    for o in obiekty:
        for t, s in nm(o):
            if t in ("w", "v"):
                slabe += 1
                if s not in zdefiniowane and s not in OPCJONALNE:
                    bledy.append(f"{o}: slaby symbol {s} nie jest zdefiniowany w {obraz} (zly linkage C/C++?)")
    for s in WYMAGANE_SCHEDULER:
        if s not in zdefiniowane:
            bledy.append(f"hook sprzatania procesu {s} nie istnieje w {obraz}")
    sched = [o for o in obiekty if o.endswith("scheduler.o")]
    if sched:
        ref = {s for t, s in nm(sched[0]) if t in ("w", "U")}
        if "usun_warstwe" in ref:
            bledy.append("scheduler.o odwoluje sie do symbolu C 'usun_warstwe' (kompozytor eksportuje linkowanie C++)")
        if "_Z12usun_warstwei" not in ref:
            bledy.append("scheduler.o nie odwoluje sie do usun_warstwe(int) - sprzatanie nie usunie warstwy okna")
    for b in bledy:
        print("FAIL:", b)
    print(f"slabe referencje: {slabe}, obiekty: {len(obiekty)}, bledy: {len(bledy)}")
    return 1 if bledy else 0


if __name__ == "__main__":
    sys.exit(main())
