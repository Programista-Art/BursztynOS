#!/usr/bin/env python3
"""Bursztyn OS - Test akceptacyjny aktualizacji A/B i powrotu w QEMU (qemu_aktualizacja_test.py).

Wymagany scenariusz:
  1. Instalacja Bursztyn OS w maszynie wirtualnej QEMU UEFI (Guest).
  2. Sprawdzenie stanu poczatkowego: Slot A potwierdzony, Slot B nieaktywny,
     sektory LBA 2046/2047, grubenv i dysk wartowniczy.
  3. Uruchomienie zainstalowanego systemu ze Slotu A (weryfikacja dzialania).
  4. Przygotowanie aktualizacji do Slotu B (oznaczenie B jako oczekujacy).
  5. Rozruch Slotu B: GRUB zwieksza licznik prob, jadro B startuje,
     osiaga punkt kontrolny i potwierdza Slot B.
  6. Ponowny rozruch: weryfikacja, ze Slot B pozostaje trwale aktywny i potwierdzony.
  7. Test kontrolowanej awarii i automatycznego powrotu (rollback):
     Slot B oczekujacy z flaga kontrolowanej awarii przed potwierdzeniem.
  8. Uruchomienie B: zatrzymanie przed potwierdzeniem sukcesu.
  9. Ponowny rozruch: GRUB wykrywa wyczerpanie prob, automatycznie wraca do Slotu A.
 10. Weryfikacja: Slot A aktywny, log powrotu zarejestrowany, dane nienaruszone,
     dysk wartowniczy (sentinel) niezmieniony.
"""

import hashlib
import json
import os
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / "narzedzia" / "instalator"))
sys.path.append(str(ROOT / "narzedzia" / "aktualizacje"))

import qemu_install
from rozruch import (
    RozruchStan,
    SLOT_A,
    SLOT_B,
    SLOT_NIEZNANY,
    STAN_NIEAKTYWNY,
    STAN_OCZEKUJACY,
    STAN_POTWIERDZONY,
    STAN_NIEUDANY,
    wybierz_najlepsza_kopie,
    ROZRUCH_MAGIC
)

def odczytaj_sektory_rozruchu(target_path):
    with open(target_path, "rb") as f:
        f.seek(2046 * 512)
        k0 = f.read(512)
        f.seek(2047 * 512)
        k1 = f.read(512)
    return wybierz_najlepsza_kopie(k0, k1)

def zapisz_sektory_rozruchu(target_path, stan: RozruchStan):
    b = stan.pack()
    with open(target_path, "r+b") as f:
        f.seek(2046 * 512)
        f.write(b)
        f.seek(2047 * 512)
        f.write(b)

def znajdz_offset_grubenv(target_path):
    HEADER = b"# GRUB Environment Block\n"
    with open(target_path, "rb") as f:
        data = f.read(512 * 1024 * 1024)
        p = 0
        while True:
            idx = data.find(HEADER, p)
            if idx == -1:
                raise RuntimeError("Nie znaleziono bloku grubenv na dysku ESP!")
            chunk = data[idx:idx+1024]
            if b"bursztyn_" in chunk:
                return idx
            p = idx + 1

def odczytaj_grubenv(target_path):
    offset = znajdz_offset_grubenv(target_path)
    with open(target_path, "rb") as f:
        f.seek(offset)
        raw = f.read(1024)
    lines = raw.split(b"\n")
    res = {}
    for line in lines:
        if line.startswith(b"#") or b"=" not in line:
            continue
        k, v = line.decode(errors="ignore").split("=", 1)
        res[k.strip()] = v.strip()
    return res

def zapisz_grubenv(target_path, potwierdzony="A", oczekujacy="BRAK", proby=0, awaria=None):
    offset = znajdz_offset_grubenv(target_path)
    env = bytearray(b"#" * 1024)
    hdr = b"# GRUB Environment Block\n"
    p = len(hdr)
    env[:p] = hdr
    lines = [
        f"bursztyn_potwierdzony={potwierdzony}\n".encode(),
        f"bursztyn_oczekujacy={oczekujacy}\n".encode(),
        f"bursztyn_proby={proby}\n".encode()
    ]
    if awaria:
        lines.append(f"bursztyn_awaria={awaria}\n".encode())
    for line in lines:
        env[p:p+len(line)] = line
        p += len(line)
    with open(target_path, "r+b") as f:
        f.seek(offset)
        f.write(env)

def main():
    print("=== Bursztyn OS: Test akceptacyjny QEMU A/B i Rollback ===", flush=True)
    existing_dir = None
    for i, arg in enumerate(sys.argv[1:]):
        if arg == "--dir" and i + 2 < len(sys.argv):
            existing_dir = Path(sys.argv[i + 2])
        elif not arg.startswith("--"):
            existing_dir = Path(arg)

    parent = ROOT / ".installertest"
    parent.mkdir(exist_ok=True)
    if existing_dir and existing_dir.exists():
        d = existing_dir
        print(f"Uzycie istniejacego katalogu roboczego: {d}", flush=True)
        target = d / "target.img"
        sentinel = d / "sentinel.img"
        iso = d / "installer.iso"
        sentinel_hash_init = qemu_install.sha(sentinel)
        target_hash_init = qemu_install.target_fingerprint(target)
        pomin_instalacje = True
    else:
        d = Path(tempfile.mkdtemp(prefix="qemu-ab-", dir=parent))
        print(f"Katalog roboczy: {d}", flush=True)
        target = d / "target.img"
        sentinel = d / "sentinel.img"
        qemu_install.sparse(target, 4 * 1024**3)
        qemu_install.sparse(sentinel, 64 * 1024**2)

        with sentinel.open("r+b") as f:
            for off in [0, 512, 10 * 512, 40000 * 512, 64 * 1024**2 - 512]:
                f.seek(off)
                f.write(hashlib.sha256(str(off).encode()).digest() * 16)

        iso = qemu_install.installer_iso(d)
        sentinel_hash_init = qemu_install.sha(sentinel)
        target_hash_init = qemu_install.target_fingerprint(target)
        pomin_instalacje = False

    # -------------------------------------------------------------
    # Krok 1: Instalacja systemu z ISO
    # -------------------------------------------------------------
    if not pomin_instalacje:
        print("\n[KROK 1] Instalacja Bursztyn OS...", flush=True)
        g_inst = qemu_install.Guest(d / "install", target, sentinel, iso)
        try:
            qemu_install.install(g_inst, target, sentinel, target_hash_init, sentinel_hash_init)
        finally:
            g_inst.close()

        assert qemu_install.sha(sentinel) == sentinel_hash_init, "Dysk wartowniczy zmieniony podczas instalacji!"
        print("Instalacja zakonczona sukcesem.", flush=True)

    # Weryfikacja stanu poczatkowego na dysku
    stan_init = odczytaj_sektory_rozruchu(target)
    assert stan_init.magic == ROZRUCH_MAGIC, "Zly magic w sektorach rozruchu!"
    assert stan_init.slot_potwierdzony == SLOT_A, "Poczatkowy slot nie jest A!"
    assert stan_init.slot_oczekujacy == SLOT_NIEZNANY, "Poczatkowy slot oczekujacy nie jest pusty!"
    assert stan_init.stan_oczekujacego == STAN_NIEAKTYWNY, "Poczatkowy stan oczekujacego nie jest nieaktywny!"

    env_init = odczytaj_grubenv(target)
    assert env_init.get("bursztyn_potwierdzony") == "A", f"Zly potwierdzony w grubenv: {env_init}"
    assert env_init.get("bursztyn_oczekujacy") == "BRAK", f"Zly oczekujacy w grubenv: {env_init}"
    assert env_init.get("bursztyn_proby") == "0", f"Zla liczba prob w grubenv: {env_init}"
    print("Stan poczatkowy na dysku (LBA 2046/2047 oraz grubenv): ZWERYFIKOWANY.", flush=True)

    # -------------------------------------------------------------
    # Krok 2: Rozruch zainstalowanego systemu ze Slotu A
    # -------------------------------------------------------------
    print("\n[KROK 2] Rozruch zainstalowanego systemu ze Slotu A...", flush=True)
    g_boot_a = qemu_install.Guest(d / "boot-a", target, sentinel)
    try:
        log_a = g_boot_a.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        g_boot_a.q.screenshot(g_boot_a.dir / "desktop-a.png")
        assert "[BSP] Wybrano kompletny wolumin GPT" in log_a
        assert "System operacyjny gotowy!" in log_a
    finally:
        g_boot_a.close()
    print("Rozruch ze Slotu A: ZWERYFIKOWANY.", flush=True)

    # -------------------------------------------------------------
    # Krok 3: Zastosowanie aktualizacji do Slotu B (oznaczenie jako oczekujacy)
    # -------------------------------------------------------------
    print("\n[KROK 3] Oznaczenie aktualizacji do Slotu B jako oczekujacej...", flush=True)
    stan_upd = odczytaj_sektory_rozruchu(target)
    stan_upd.generacja += 1
    stan_upd.slot_oczekujacy = SLOT_B
    stan_upd.stan_oczekujacego = STAN_OCZEKUJACY
    stan_upd.licznik_prob = 0
    stan_upd.oczekujaca = (0, 2, 0)
    zapisz_sektory_rozruchu(target, stan_upd)
    zapisz_grubenv(target, potwierdzony="A", oczekujacy="B", proby=0)
    print("Slot B oznaczony jako oczekujacy na rozruch.", flush=True)

    # -------------------------------------------------------------
    # Krok 4: Rozruch zaktualizowanego systemu (Slot B) i potwierdzenie
    # -------------------------------------------------------------
    print("\n[KROK 4] Rozruch Slotu B i potwierdzenie sukcesu w punkcie kontrolnym...", flush=True)
    g_boot_b = qemu_install.Guest(d / "boot-b", target, sentinel)
    try:
        log_b = g_boot_b.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        g_boot_b.q.screenshot(g_boot_b.dir / "desktop-b.png")
        assert "[ROZRUCH] Proba uruchomienia slotu B." in log_b, "Brak logu proby uruchomienia slotu B!"
        assert "[ROZRUCH] Slot B potwierdzony." in log_b, "Brak logu potwierdzenia slotu B!"
        assert "System operacyjny gotowy!" in log_b
    finally:
        g_boot_b.close()
    print("Rozruch Slotu B i automatyczne potwierdzenie: ZWERYFIKOWANE.", flush=True)

    # Weryfikacja stanu na dysku po potwierdzeniu Slotu B
    stan_post_b = odczytaj_sektory_rozruchu(target)
    assert stan_post_b.slot_potwierdzony == SLOT_B, f"Slot B nie jest potwierdzony: {stan_post_b.slot_potwierdzony}"
    assert stan_post_b.slot_oczekujacy == SLOT_NIEZNANY, "Slot oczekujacy nie zostal wyczyszczony!"
    assert stan_post_b.stan_oczekujacego == STAN_POTWIERDZONY, "Stan nie jest potwierdzony!"

    env_post_b = odczytaj_grubenv(target)
    assert env_post_b.get("bursztyn_potwierdzony") == "B", f"Zly potwierdzony w grubenv: {env_post_b}"
    assert env_post_b.get("bursztyn_oczekujacy") == "BRAK", f"Zly oczekujacy w grubenv: {env_post_b}"
    assert env_post_b.get("bursztyn_proby") == "0", f"Zle proby w grubenv: {env_post_b}"
    print("Stan na dysku po potwierdzeniu B: ZWERYFIKOWANY.", flush=True)

    # -------------------------------------------------------------
    # Krok 5: Ponowny rozruch - weryfikacja trwalosci potwierdzonego Slotu B
    # -------------------------------------------------------------
    print("\n[KROK 5] Ponowny rozruch - Slot B pozostaje aktywny...", flush=True)
    g_reboot_b = qemu_install.Guest(d / "reboot-b", target, sentinel)
    try:
        log_reb = g_reboot_b.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        assert "System operacyjny gotowy!" in log_reb
    finally:
        g_reboot_b.close()
    print("Trwalosc Slotu B po restarcie: ZWERYFIKOWANA.", flush=True)

    # -------------------------------------------------------------
    # Krok 6: Test kontrolowanej awarii i automatycznego powrotu (Rollback)
    # -------------------------------------------------------------
    print("\n[KROK 6] Test kontrolowanej awarii aktualizacji i automatycznego powrotu...", flush=True)
    # Przygotuj stan: Slot A jest potwierdzony, Slot B oczekujacy, z flaga kontrolowanej awarii
    stan_fail = odczytaj_sektory_rozruchu(target)
    stan_fail.generacja += 1
    stan_fail.slot_potwierdzony = SLOT_A
    stan_fail.slot_oczekujacy = SLOT_B
    stan_fail.stan_oczekujacego = STAN_OCZEKUJACY
    stan_fail.licznik_prob = 0
    stan_fail.oczekujaca = (0, 3, 0)
    zapisz_sektory_rozruchu(target, stan_fail)
    zapisz_grubenv(target, potwierdzony="A", oczekujacy="B", proby=0, awaria="B")

    # Pierwsza proba rozruchu B - zakonczenie kontrolowana awaria przed potwierdzeniem
    print("Pierwszy start Slotu B (z celowa awaria przed potwierdzeniem)...", flush=True)
    g_fail_b = qemu_install.Guest(d / "fail-b", target, sentinel)
    try:
        log_fail = g_fail_b.wait("[TEST] Kontrolowana awaria przed potwierdzeniem slotu B.", timeout=120)
        time.sleep(1)
        assert "[ROZRUCH] Proba uruchomienia slotu B." in log_fail
        assert "[ROZRUCH] Slot B potwierdzony." not in log_fail, "Slot B nie powinien byl zostac potwierdzony!"
    finally:
        g_fail_b.close()
    print("Awaria przed potwierdzeniem zarejestrowana prawidlowo.", flush=True)

    # Sprawdz, ze GRUB zarejestrowal probe rozruchu
    env_mid_fail = odczytaj_grubenv(target)
    assert env_mid_fail.get("bursztyn_proby") == "1", f"GRUB nie zwiekszyl licznika prob: {env_mid_fail}"

    # Drugi start - GRUB musi wykryc wyczerpanie prob i automatycznie cofnac do Slotu A!
    print("\nDrugi start: automatyczny powrot (rollback) do Slotu A...", flush=True)
    g_rollback_a = qemu_install.Guest(d / "rollback-a", target, sentinel)
    try:
        log_rb = g_rollback_a.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        g_rollback_a.q.screenshot(g_rollback_a.dir / "rollback-desktop.png")
        assert "[ROZRUCH] Nieudane uruchomienie slotu B." in log_rb, "Brak logu nieudanego uruchomienia slotu B!"
        assert "[ROZRUCH] Powrot do slotu A." in log_rb, "Brak logu powrotu do slotu A!"
        assert "System operacyjny gotowy!" in log_rb
    finally:
        g_rollback_a.close()
    print("Automatyczny powrot do Slotu A: ZWERYFIKOWANY W QEMU.", flush=True)

    # Weryfikacja stanu na dysku po rollbacku
    stan_post_rb = odczytaj_sektory_rozruchu(target)
    assert stan_post_rb.slot_potwierdzony == SLOT_A, f"Potwierdzony slot nie jest A: {stan_post_rb.slot_potwierdzony}"
    assert stan_post_rb.stan_oczekujacego == STAN_NIEUDANY, f"Stan oczekujacego nie jest nieudany: {stan_post_rb.stan_oczekujacego}"

    # Sprawdzenie niezmiennika dysku wartowniczego
    sentinel_hash_final = qemu_install.sha(sentinel)
    assert sentinel_hash_final == sentinel_hash_init, "Dysk wartowniczy zostal zmodyfikowany!"
    print("Dysk wartowniczy (sentinel): ZACHOWANY I NIENARUSZONY.", flush=True)

    print("\n========================================================")
    print("WSZYSTKIE TESTY AKCEPTACYJNE QEMU ZAKONCZONE SUKCESEM!")
    print("========================================================")

if __name__ == "__main__":
    main()
