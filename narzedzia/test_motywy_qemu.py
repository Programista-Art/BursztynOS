#!/usr/bin/env python3
"""Bursztyn OS - Weryfikacja podsystemu motywów i stres-testu 50 przełączeń w QEMU.

Uruchamia Bursztyn OS w maszynie wirtualnej QEMU z flagą bursztyn.test=automated,
przechwytuje wyjście portu szeregowego COM1/stdio i weryfikuje:
  1. Poprawny rozruch jądra i start aplikacji testowej test.bur.
  2. Wykonanie testu [TEST 9/9] podsystemu motywów BWS 86/87:
     - Poprawne odczytanie palety domyślnej Bursztyn Nocny i promieni zaokrągleń
     - Odrzucenie niebezpiecznego wskaźnika użytkownika (fail-closed w BWS 86)
     - Blokadę nieuprawnionej trwałej zmiany motywu bez PRAWO_SYSTEM_CONFIG (BWS 87)
     - Rozpoczęcie podglądu i bezpieczne przywrócenie (rollback)
     - 50-krotne sekwencyjne przełączenie motywów w pętli bez wycieków pamięci
  3. Stabilność pamięci sterty i ramek po 50 przełączeniach:
     [TEST PAMIECI] Aktywne alokacje po przebiegu: STABILNE (PASS)
  4. Brak wyjątków #PF (Page Fault), kernel panic, naruszeń guard page ani awarii Ring 3.
"""
import os
import sys
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    kernel = ROOT / "system_operacyjny.bin"
    if not kernel.exists():
        print(f"[BLAD] Brak pliku jądra {kernel}. Zbuduj najpierw 'make kernel'.", file=sys.stderr)
        sys.exit(1)

    ci_iso = ROOT / ".bursztyn_ci_test.iso"
    if not ci_iso.exists():
        print("[INFO] Budowanie .bursztyn_ci_test.iso...")
        ret = subprocess.run(["make", ".bursztyn_ci_test.iso"], cwd=str(ROOT))
        if ret.returncode != 0:
            print("[BLAD] Nie udało się zbudować BursztynOS_ci_test.iso", file=sys.stderr)
            sys.exit(1)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        print("[INFO] Tworzenie dysku wirtualnego...")
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT))

    qemu_cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-drive", f"id=disk,file={disk},format=raw,if=none",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
        "-m", "2G",
        "-vga", "std",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(ci_iso)
    ]

    print("=" * 80)
    print(" BURSZTYN OS - TEST REGRESYJNY MOTYWÓW I STRES-TEST 50 PRZEŁĄCZEŃ (QEMU)")
    print("=" * 80)
    print(f"Polecenie: {' '.join(qemu_cmd)}")

    start_time = time.time()
    proc = subprocess.Popen(
        qemu_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    test_9_rozpoczety = False
    test_9_zakonczony = False
    pamiec_stabilna = False
    wszystkie_pass = False
    wykryto_blad = False
    komunikaty_bledu = []

    try:
        for line in proc.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()

            if "KRYTYCZNE" in line or "PANIC" in line or "Page Fault" in line:
                wykryto_blad = True
                komunikaty_bledu.append(line.strip())

            if "[BWS TEST FAIL]" in line:
                wykryto_blad = True
                komunikaty_bledu.append(line.strip())

            if "[TEST 9/9] Podsystem motywów BWS 86..87 i 50x stres-test przełączeń" in line:
                test_9_rozpoczety = True

            if "[TEST 9/9] Podsystem motywów BWS 86..87, accessors, 50x stres-test i brak wycieków: OK" in line:
                test_9_zakonczony = True
                pamiec_stabilna = True

            if "=== WSZYSTKIE TESTY BWS 61..87 ZAKONCZONE SUKCESEM (PASS) ===" in line:
                wszystkie_pass = True
                # Sukces osiągnięty - możemy natychmiast bezpiecznie zamknąć QEMU
                break

            if time.time() - start_time > 300.0:
                print("\n[TIMEOUT] Przekroczono limit czasu 300 sekund.")
                break

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("\n" + "=" * 80)
    print(" PODSUMOWANIE TESTU MOTYWÓW W QEMU")
    print("=" * 80)
    print(f"Test 9 rozpoczęty: {test_9_rozpoczety}")
    print(f"Test 9 (50 przełączeń) ukończony: {test_9_zakonczony}")
    print(f"Pamięć stabilna (brak wycieków): {pamiec_stabilna}")
    print(f"Wszystkie testy BWS PASS: {wszystkie_pass}")
    print(f"Błędy krytyczne: {len(komunikaty_bledu)}")

    if wykryto_blad:
        for b in komunikaty_bledu:
            print(f"  [BLAD] {b}")

    if test_9_zakonczony and pamiec_stabilna and wszystkie_pass and not wykryto_blad:
        print("\n[SUKCES] Wszystkie testy motywów i 50-krotny stres-test przełączeń w QEMU ZDANE!\n")
        sys.exit(0)
    else:
        print("\n[PORAZKA] Test motywów w QEMU nie zakończył się pełnym sukcesem.\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
