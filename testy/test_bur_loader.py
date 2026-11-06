#!/usr/bin/env python3
"""
Bursztyn OS - Testy loadera programow .bur (Ring 3 Executable Loader).

Weryfikuje:
  1. Kompilacje i uruchomienie hostowego harnessu testowego C++ (35 testow jednostkowych):
     - small existing application (32 KiB)
     - Aktualizacje Systemu (192 KiB)
     - exactly at TEXT maximum (64 MiB)
     - 1 byte above maximum / not page-aligned (64 MiB + 1 B)
     - truncated text
     - oversized text
     - text offset overflow
     - text plus rodata overflow
     - data overlap (w pliku i w VA)
     - stack overlap
     - entry point outside TEXT
     - malformed header (uszkodzona magia, rozmiar < 60)
     - old-format application compatibility (BUR\0 oraz nieznana wersja)
  2. Sprawdzenie integralnosci rzeczywistego pliku aktualizacje.bin wygenerowanego przez build:
     - magia, entry point wewnatrz TEXT, offsety i rozmiary wyrownane do 4 KiB, brak kolizji ze stosem/sterta.
"""

import os
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOADER_H = os.path.join(REPO, "jadro/fs/loader.h")
LOADER_CPP = os.path.join(REPO, "jadro/fs/loader.cpp")
TEST_CPP = os.path.join(REPO, "testy", "test_bur_loader.cpp")
TEST_BIN = os.path.join(REPO, "testy", "test_bur_loader")
AKT_BIN = os.path.join(REPO, "aktualizacje.bin")

zdane = 0
zlane = 0

def raport(nazwa, ok):
    global zdane, zlane
    if ok:
        zdane += 1
        print("PASS %s" % nazwa)
    else:
        zlane += 1
        print("FAIL %s" % nazwa)

def test_host_harness():
    print("--- 1. Uruchamianie testow jednostkowych C++ ---")
    cmd_build = [
        "g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic",
        "-I.", "-Ijadro/arch", "-Ijadro/mm", "-Ijadro/fs", "-Ijadro/bws", "-Ijadro/gui",
        "-Ijadro/siec", "-Ijadro/aktualizacja", "-Ijadro/diag", "-Ijadro/rdzen",
        "-DBURSZTYN_LOADER_HOST_TEST", LOADER_CPP, TEST_CPP, "-o", TEST_BIN
    ]
    res_build = subprocess.run(cmd_build, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res_build.returncode != 0:
        print("Blad kompilacji test_bur_loader.cpp:")
        print(res_build.stdout.decode())
        raport("harness-kompilacja", False)
        return False
    raport("harness-kompilacja", True)

    res_run = subprocess.run([TEST_BIN], cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = res_run.stdout.decode()
    print(out, end="")
    raport("harness-wszystkie-testy-wykonane", res_run.returncode == 0)
    return res_run.returncode == 0

def test_aktualizacje_bin_integrity():
    print("--- 2. Weryfikacja binarnego pliku aktualizacje.bin ---")
    if not os.path.isfile(AKT_BIN):
        print("Plik aktualizacje.bin nie istnieje - buduje...")
        subprocess.run(["make", "aktualizacje_blob.o"], cwd=REPO, check=True)

    with open(AKT_BIN, "rb") as f:
        data = f.read()

    file_len = len(data)
    raport("aktualizacje-bin-rozmiar-naglowka", file_len >= 60)

    magic, entry, toff, tsz, tva, doff, dsz, dva = struct.unpack("<4sQQQQQQQ", data[:60])
    raport("aktualizacje-bin-magia-BUR0", magic == b"BUR\x00")
    raport("aktualizacje-bin-text-przesuniecie-4k", (toff % 4096) == 0 and toff >= 60)
    raport("aktualizacje-bin-text-rozmiar-4k", (tsz % 4096) == 0 and tsz == 0x30000)
    raport("aktualizacje-bin-text-va-4k", (tva % 4096) == 0 and tva == 0x1001000)
    raport("aktualizacje-bin-data-przesuniecie-4k", (doff % 4096) == 0 and doff == toff + tsz)
    raport("aktualizacje-bin-data-rozmiar-4k", (dsz % 4096) == 0 and dsz == 0x20000)
    raport("aktualizacje-bin-data-va-4k", (dva % 4096) == 0 and dva == tva + tsz)
    raport("aktualizacje-bin-entry-point-w-tekscie", tva <= entry < tva + tsz)
    raport("aktualizacje-bin-tekst-miesci-sie-w-pliku", toff + tsz <= file_len)

    # Limity
    BUR_MAKS_TEKST = 64 * 1024 * 1024
    BUR_MAKS_DANE = 64 * 1024 * 1024
    BUR_MAKS_CALKOWITY = 128 * 1024 * 1024
    RING3_HEAP_BASE = 0x0000000800000000
    RING3_STACK_TOP = 0x00007FFFF0000000

    raport("aktualizacje-bin-text-pod-limitem-64m", tsz <= BUR_MAKS_TEKST)
    raport("aktualizacje-bin-data-pod-limitem-64m", dsz <= BUR_MAKS_DANE)
    raport("aktualizacje-bin-calkowity-pod-limitem-128m", tsz + dsz <= BUR_MAKS_CALKOWITY)
    raport("aktualizacje-bin-brak-kolizji-ze-sterta", (tva + tsz <= RING3_HEAP_BASE) and (dva + dsz <= RING3_HEAP_BASE))
    raport("aktualizacje-bin-brak-kolizji-ze-stosem", (tva + tsz < RING3_STACK_TOP) and (dva + dsz < RING3_STACK_TOP))

def main():
    print("==================================================")
    print("  Bursztyn OS - Weryfikacja Loadera i Aktualizacji")
    print("==================================================")
    harness_ok = test_host_harness()
    test_aktualizacje_bin_integrity()

    print("==================================================")
    print("PODSUMOWANIE: zdane=%d, zlane=%d" % (zdane, zlane))
    if zlane != 0 or not harness_ok:
        sys.exit(1)
    print("LOADER BUR: WSZYSTKIE TESTY ZDANE.")
    sys.exit(0)

if __name__ == "__main__":
    main()
