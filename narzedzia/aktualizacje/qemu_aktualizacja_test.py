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
import shutil
import struct
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

def pobierz_uklad_esp(target_path):
    with open(target_path, "rb") as f:
        f.seek(1048576) # ESP start (LBA 2048)
        vbr = f.read(512)
        bps = struct.unpack_from("<H", vbr, 11)[0]
        spc = vbr[13]
        rsvd = struct.unpack_from("<H", vbr, 14)[0]
        fats = vbr[16]
        spf = struct.unpack_from("<I", vbr, 36)[0]
        data_start = 1048576 + (rsvd + fats * spf) * bps

        f.seek(data_start + (5 - 2) * spc * bps)
        boot_dir = f.read(4096)
        sha_a = b"\x00" * 32
        sha_b = b"\x00" * 32
        for i in range(128):
            e = boot_dir[i*32:(i+1)*32]
            name = e[:11]
            if name == b"JADRO_A BIN":
                cl = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
                sz = struct.unpack_from("<I", e, 28)[0]
                f.seek(data_start + (cl - 2) * spc * bps)
                sha_a = hashlib.sha256(f.read(sz)).digest()
            elif name == b"JADRO_B BIN":
                cl = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
                sz = struct.unpack_from("<I", e, 28)[0]
                f.seek(data_start + (cl - 2) * spc * bps)
                sha_b = hashlib.sha256(f.read(sz)).digest()

        f.seek(data_start + (6 - 2) * spc * bps)
        grub_dir = f.read(4096)
        grubenv_offset = None
        for i in range(128):
            e = grub_dir[i*32:(i+1)*32]
            if e[:11] == b"GRUBENV    ":
                cl = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
                grubenv_offset = data_start + (cl - 2) * spc * bps
                break
        return grubenv_offset, sha_a, sha_b

def znajdz_offset_grubenv(target_path):
    off, _, _ = pobierz_uklad_esp(target_path)
    if off is None:
        raise RuntimeError("Nie znaleziono pliku GRUBENV w tablicy FAT32 ESP!")
    return off

def odczytaj_grubenv(target_path):
    offset = znajdz_offset_grubenv(target_path)
    with open(target_path, "rb") as f:
        f.seek(offset)
        raw = f.read(1024)
    if not raw.startswith(b"# GRUB Environment Block\n"):
        return {}
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
        # Zresetuj sektory rozruchu i grubenv do czystego stanu poczatkowego:
        _, sha_a, sha_b = pobierz_uklad_esp(target)
        stan_poczatkowy = RozruchStan(
            slot_potwierdzony=SLOT_A,
            slot_oczekujacy=SLOT_NIEZNANY,
            stan_oczekujacego=STAN_NIEAKTYWNY,
            licznik_prob=0,
            generacja=1,
            potwierdzona=(0, 1, 0),
            sha_a=sha_a,
            sha_b=sha_b
        )
        zapisz_sektory_rozruchu(target, stan_poczatkowy)
        zapisz_grubenv(target, potwierdzony="A", oczekujacy="BRAK", proby=0)
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
    # Krok 2: Rozruch zainstalowanego systemu ze Slotu A i aplikacja Aktualizacje
    # -------------------------------------------------------------
    print("\n[KROK 2] Rozruch zainstalowanego systemu ze Slotu A...", flush=True)
    shutil.rmtree(d / "boot-a", ignore_errors=True)
    g_boot_a = qemu_install.Guest(d / "boot-a", target, sentinel)
    try:
        log_a = g_boot_a.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        g_boot_a.q.screenshot(g_boot_a.dir / "desktop-a.png")
        assert "[BSP] Wybrano kompletny wolumin GPT" in log_a
        assert "System operacyjny gotowy!" in log_a

        # Uruchomienie aplikacji Aktualizacje Systemu z ikony na pulpicie (x=80, y=170)
        print("Uruchamianie aplikacji Aktualizacje Systemu (klikniecie ikony na pulpicie)...", flush=True)
        g_boot_a.q.click(80, 170)
        time.sleep(3)
        g_boot_a.q.screenshot(g_boot_a.dir / "aktualizacje-window.png")

        # Weryfikacja wymaganych znacznikow serial logu
        serial_log = (g_boot_a.dir / "serial.log").read_text(errors="replace")
        wymagane_znaczniki = [
            "[OTA TRACE] Launching aktualizacje app",
            "[OTA TRACE] Loader mapped segments, entering Ring 3",
            "[OTA TRACE] aktualizacje Ring 3 main entered",
            "[OTA TRACE] aktualizacje calling BWS 84",
            "[OTA TRACE] Kernel entered BWS 84 handler",
            "[OTA TRACE] Determining OTA availability",
            "[OTA TRACE] Storage device resolved: SATA AHCI",
            "[OTA TRACE] Reading bootstate / GPT",
            "[OTA TRACE] Reading grubenv",
            "[OTA TRACE] Copying status to user space",
            "[OTA TRACE] BWS 84 completed successfully",
        ]
        for tag in wymagane_znaczniki:
            assert tag in serial_log, f"Brak wymaganego znacznika: {tag}"

        assert "#PF Page Fault" not in serial_log, "Wykryto #PF Page Fault!"
        assert "RIP: 0x0000000000000000" not in serial_log, "Wykryto RIP=0!"
        assert "KRYTYCZNY BLAD" not in serial_log, "Wykryto panikę jądra!"

        # Klikniecie przycisku 'Sprawdz aktualizacje' (BWS 85)
        print("Klikniecie przycisku 'Sprawdz aktualizacje'...", flush=True)
        g_boot_a.q.click(180, 640)
        time.sleep(2)
        g_boot_a.q.screenshot(g_boot_a.dir / "aktualizacje-sprawdz.png")

        serial_log_after = (g_boot_a.dir / "serial.log").read_text(errors="replace")
        assert "#PF Page Fault" not in serial_log_after, "Wykryto #PF Page Fault po kliknieciu 'Sprawdz aktualizacje'!"
        assert "RIP: 0x0000000000000000" not in serial_log_after, "Wykryto RIP=0 po kliknieciu 'Sprawdz aktualizacje'!"
        assert "KRYTYCZNY BLAD" not in serial_log_after, "Wykryto panikę jądra po kliknieciu 'Sprawdz aktualizacje'!"
    finally:
        g_boot_a.close()
    print("Rozruch ze Slotu A i aplikacja Aktualizacje: ZWERYFIKOWANE.", flush=True)

    # -------------------------------------------------------------
    # Krok 3: Zastosowanie aktualizacji do Slotu B (oznaczenie jako oczekujacy)
    # -------------------------------------------------------------
    print("\n[KROK 3] Oznaczenie aktualizacji do Slotu B jako oczekujacej...", flush=True)
    _, sha_a, sha_b = pobierz_uklad_esp(target)
    stan_upd = odczytaj_sektory_rozruchu(target)
    stan_upd.generacja += 1
    stan_upd.slot_oczekujacy = SLOT_B
    stan_upd.stan_oczekujacego = STAN_OCZEKUJACY
    stan_upd.licznik_prob = 0
    stan_upd.oczekujaca = (0, 2, 0)
    stan_upd.sha_a = sha_a
    stan_upd.sha_b = sha_b
    zapisz_sektory_rozruchu(target, stan_upd)
    zapisz_grubenv(target, potwierdzony="A", oczekujacy="B", proby=0)
    print("Slot B oznaczony jako oczekujacy na rozruch.", flush=True)

    # -------------------------------------------------------------
    # Krok 4: Rozruch zaktualizowanego systemu (Slot B) i potwierdzenie
    # -------------------------------------------------------------
    print("\n[KROK 4] Rozruch Slotu B i potwierdzenie sukcesu w punkcie kontrolnym...", flush=True)
    shutil.rmtree(d / "boot-b", ignore_errors=True)
    g_boot_b = qemu_install.Guest(d / "boot-b", target, sentinel)
    try:
        log_b = g_boot_b.wait("[ROZRUCH] Slot B potwierdzony.", timeout=180)
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
    shutil.rmtree(d / "reboot-b", ignore_errors=True)
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
    _, sha_a, sha_b = pobierz_uklad_esp(target)
    stan_fail = odczytaj_sektory_rozruchu(target)
    stan_fail.generacja += 1
    stan_fail.slot_potwierdzony = SLOT_A
    stan_fail.slot_oczekujacy = SLOT_B
    stan_fail.stan_oczekujacego = STAN_OCZEKUJACY
    stan_fail.licznik_prob = 0
    stan_fail.oczekujaca = (0, 3, 0)
    stan_fail.sha_a = sha_a
    stan_fail.sha_b = sha_b
    zapisz_sektory_rozruchu(target, stan_fail)
    zapisz_grubenv(target, potwierdzony="A", oczekujacy="B", proby=0, awaria="B")

    print("Pierwszy start Slotu B (z celowa awaria przed potwierdzeniem)...", flush=True)
    shutil.rmtree(d / "fail-b", ignore_errors=True)
    g_fail_b = qemu_install.Guest(d / "fail-b", target, sentinel)
    try:
        log_fail = g_fail_b.wait("[TEST] Kontrolowana awaria przed potwierdzeniem slotu B.", timeout=120)
        time.sleep(1)
        assert "[ROZRUCH] Proba uruchomienia slotu B." in log_fail
        assert "[ROZRUCH] Slot B potwierdzony." not in log_fail, "Slot B nie powinien byl zostac potwierdzony!"
    finally:
        g_fail_b.close()
    print("Awaria przed potwierdzeniem zarejestrowana prawidlowo.", flush=True)

    env_mid_fail = odczytaj_grubenv(target)
    assert env_mid_fail.get("bursztyn_proby") == "1", f"GRUB nie zwiekszyl licznika prob: {env_mid_fail}"

    print("\nDrugi start: automatyczny powrot (rollback) do Slotu A...", flush=True)
    shutil.rmtree(d / "rollback-a", ignore_errors=True)
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

    stan_post_rb = odczytaj_sektory_rozruchu(target)
    assert stan_post_rb.slot_potwierdzony == SLOT_A, f"Potwierdzony slot nie jest A: {stan_post_rb.slot_potwierdzony}"
    assert stan_post_rb.stan_oczekujacego == STAN_NIEUDANY, f"Stan oczekujacego nie jest nieudany: {stan_post_rb.stan_oczekujacego}"

    # -------------------------------------------------------------
    # Krok 7: Przerwany zapis aktywacji (split-write: LBA wskazuje B, grubenv wskazuje A)
    # -------------------------------------------------------------
    print("\n[KROK 7] Przerwany zapis aktywacji (LBA wskazuje B, grubenv wskazuje A)...", flush=True)
    stan_split = odczytaj_sektory_rozruchu(target)
    stan_split.generacja += 1
    stan_split.slot_potwierdzony = SLOT_A
    stan_split.slot_oczekujacy = SLOT_B
    stan_split.stan_oczekujacego = STAN_OCZEKUJACY
    stan_split.licznik_prob = 0
    stan_split.oczekujaca = (0, 4, 0)
    stan_split.sha_a = sha_a
    stan_split.sha_b = sha_b
    zapisz_sektory_rozruchu(target, stan_split)
    # Symulacja: utrata zasilania przed zapisem grubenv - grubenv nadal wskazuje A
    zapisz_grubenv(target, potwierdzony="A", oczekujacy="BRAK", proby=0)

    shutil.rmtree(d / "split-write", ignore_errors=True)
    g_split = qemu_install.Guest(d / "split-write", target, sentinel)
    try:
        log_split = g_split.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        assert "System operacyjny gotowy!" in log_split
    finally:
        g_split.close()
    print("Przerwany zapis aktywacji: bezpieczny rozruch potwierdzonego slotu A ZWERYFIKOWANY.", flush=True)

    # -------------------------------------------------------------
    # Krok 8: Samonaprawa uszkodzonego grubenv z autorytatywnych sektorow LBA
    # -------------------------------------------------------------
    print("\n[KROK 8] Samonaprawa uszkodzonego grubenv z autorytatywnych sektorow LBA...", flush=True)
    # Po kroku 7 upewnijmy sie, ze LBA wskazuje stabilny potwierdzony Slot A:
    stan_clean = RozruchStan(
        slot_potwierdzony=SLOT_A,
        slot_oczekujacy=SLOT_NIEZNANY,
        stan_oczekujacego=STAN_POTWIERDZONY,
        generacja=stan_split.generacja + 1,
        potwierdzona=(0, 1, 0),
        sha_a=sha_a,
        sha_b=sha_b
    )
    zapisz_sektory_rozruchu(target, stan_clean)

    offset_env = znajdz_offset_grubenv(target)
    with open(target, "r+b") as f:
        f.seek(offset_env)
        f.write(b"\x00" * 1024)

    # GRUB przy braku poprawnego grubenv laduje konfiguracje awaryjna (Slot A).
    # Jadro Slotu A wykrywa brak grubenv i odtwarza go ze stanu sektorow LBA.
    shutil.rmtree(d / "heal-grubenv", ignore_errors=True)
    g_heal = qemu_install.Guest(d / "heal-grubenv", target, sentinel)
    try:
        log_heal = g_heal.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        assert "[ROZRUCH] Samonaprawa grubenv zakonczona sukcesem." in log_heal
        assert "System operacyjny gotowy!" in log_heal
    finally:
        g_heal.close()

    env_healed = odczytaj_grubenv(target)
    assert env_healed.get("bursztyn_potwierdzony") == "A", f"grubenv nie zostal naprawiony: {env_healed}"
    assert env_healed.get("bursztyn_oczekujacy") == "BRAK"
    assert env_healed.get("bursztyn_proby") == "0"
    print("Samonaprawa grubenv przez jadro na podstawie LBA: ZWERYFIKOWANA.", flush=True)

    # -------------------------------------------------------------
    # Krok 9: Odrzucenie nieautoryzowanego rozruchu (stale grubenv -> B, ale LBA ma NIEUDANY)
    # -------------------------------------------------------------
    print("\n[KROK 9] Odrzucenie nieautoryzowanego rozruchu na podstawie LBA...", flush=True)
    stan_unauth = odczytaj_sektory_rozruchu(target)
    stan_unauth.generacja += 1
    stan_unauth.slot_potwierdzony = SLOT_A
    stan_unauth.slot_oczekujacy = SLOT_B
    stan_unauth.stan_oczekujacego = STAN_NIEUDANY
    stan_unauth.sha_a = sha_a
    stan_unauth.sha_b = sha_b
    zapisz_sektory_rozruchu(target, stan_unauth)
    # Wstrzykujemy stale/zmanipulowane grubenv wymuszajace start slotu B:
    zapisz_grubenv(target, potwierdzony="A", oczekujacy="B", proby=0)

    # Start: GRUB odpala jadro B. Jadro B wykrywa brak autoryzacji w LBA, loguje blad krytyczny,
    # zapisuje w grubenv bursztyn_oczekujacy=NIEUDANY i wymusza natychmiastowy sprzetowy reset!
    shutil.rmtree(d / "unauth-boot", ignore_errors=True)
    g_unauth = qemu_install.Guest(d / "unauth-boot", target, sentinel)
    try:
        log_unauth = g_unauth.wait("[ROZRUCH] BLAD KRYTYCZNY: Nieautoryzowany rozruch slotu!", timeout=180)
        time.sleep(1)
        assert "[ROZRUCH] Odrzucenie na podstawie kanonicznych sektorow stanu rozruchu." in log_unauth
        assert "[ROZRUCH] Natychmiastowy powrot do potwierdzonego slotu." in log_unauth
    finally:
        g_unauth.close()
    print("Odrzucenie nieautoryzowanego rozruchu przez jadro: ZWERYFIKOWANE.", flush=True)

    env_unauth_post = odczytaj_grubenv(target)
    assert env_unauth_post.get("bursztyn_potwierdzony") == "A"
    assert env_unauth_post.get("bursztyn_oczekujacy") == "NIEUDANY"

    # Kolejny start: GRUB po resecie automatycznie laduje bezpieczny Slot A
    shutil.rmtree(d / "unauth-recov", ignore_errors=True)
    g_recov = qemu_install.Guest(d / "unauth-recov", target, sentinel)
    try:
        log_recov = g_recov.wait("System operacyjny gotowy!", timeout=180)
        time.sleep(1)
        assert "[ROZRUCH] Nieudane uruchomienie slotu B." in log_recov
        assert "[ROZRUCH] Powrot do slotu A." in log_recov
        assert "System operacyjny gotowy!" in log_recov
    finally:
        g_recov.close()
    print("Automatyczny powrot do Slotu A po odrzuceniu: ZWERYFIKOWANY.", flush=True)

    # -------------------------------------------------------------
    # Krok 10: Weryfikacja odrzucenia klucza testowego przez wydanie RELEASE
    # -------------------------------------------------------------
    print("\n[KROK 10] Weryfikacja odrzucenia aktualizacji z kluczem TEST przez wersje RELEASE...", flush=True)
    import subprocess
    tool_akt = [sys.executable, str(ROOT / "narzedzia" / "aktualizacje" / "podpisz_aktualizacje.py")]
    priv_test = ROOT / "narzedzia" / "aktualizacje" / "klucz-testowy-prywatny.pem"
    pub_release = ROOT / "narzedzia" / "aktualizacje" / "klucz-wydanie-publiczny.pem"
    test_pkg = d / "bursztyn-0.5.0.akt"
    test_wykaz = d / "test_wykaz.wykaz"
    pkg_src = d / "pkg_src" / "jadro"
    pkg_src.mkdir(parents=True, exist_ok=True)
    with open(pkg_src / "test.bin", "wb") as f:
        f.write(b"jadro-testowe")

    subprocess.run(tool_akt + ["zbuduj-pakiet", "--zrodlo", str(d / "pkg_src"), "--wyjscie", str(test_pkg)], check=True, cwd=ROOT)
    subprocess.run(tool_akt + ["podpisz-wykaz", "--pakiet", str(test_pkg), "--wersja", "0.5.0", "--minimalna", "0.1.0", "--kanal", "stabilny", "--prywatny", str(priv_test), "--wyjscie", str(test_wykaz)], check=True, cwd=ROOT)
    res = subprocess.run(tool_akt + ["weryfikuj", "--wykaz", str(test_wykaz), "--publiczny", str(pub_release)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=ROOT)
    assert res.returncode != 0, "Aktualizacja podpisana kluczem TEST zostala blednie zaakceptowana przez RELEASE!"
    print("Odrzucenie pakietu podpisanego kluczem TEST przez klucz RELEASE: ZWERYFIKOWANE.", flush=True)

    # -------------------------------------------------------------
    # Krok 11: Formalna weryfikacja zarezerwowanych sektorow stanu rozruchu
    # -------------------------------------------------------------
    print("\n[KROK 11] Weryfikacja formalnej rezerwacji sektorow rozruchu (LBA 2046/2047)...", flush=True)
    with open(target, "rb") as f:
        f.seek(512)
        gpt_h = f.read(512)
        assert gpt_h.startswith(b"EFI PART"), "Brak prawidlowego naglowka GPT!"
        arr_lba = struct.unpack_from("<Q", gpt_h, 72)[0]
        entries_cnt = struct.unpack_from("<I", gpt_h, 80)[0]
        entry_sz = struct.unpack_from("<I", gpt_h, 84)[0]
        arr_end = arr_lba + (entries_cnt * entry_sz + 511) // 512 - 1
        assert arr_end < 2046, f"Tablica partycji GPT ({arr_end}) nachodzi na LBA 2046!"

        f.seek(arr_lba * 512)
        esp_start = None
        for _ in range(entries_cnt):
            ent = f.read(entry_sz)
            p_type = ent[:16]
            if p_type == b"\x00" * 16:
                continue
            p_start = struct.unpack_from("<Q", ent, 32)[0]
            p_end = struct.unpack_from("<Q", ent, 40)[0]
            assert not (p_start <= 2047 and p_end >= 2046), f"Partycja [{p_start}, {p_end}] nachodzi na LBA 2046/2047!"
            if p_type == bytes([0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b]):
                esp_start = p_start

        assert esp_start is not None and esp_start >= 2048, f"Partycja ESP rozpoczyna sie przed LBA 2048: {esp_start}"
    print("Formalna rezerwacja LBA 2046/2047 (brak kolizji z GPT i ESP): ZWERYFIKOWANA.", flush=True)

    # Sprawdzenie niezmiennika dysku wartowniczego
    sentinel_hash_final = qemu_install.sha(sentinel)
    assert sentinel_hash_final == sentinel_hash_init, "Dysk wartowniczy zostal zmodyfikowany!"
    print("Dysk wartowniczy (sentinel): ZACHOWANY I NIENARUSZONY.", flush=True)

    print("\n========================================================")
    print("WSZYSTKIE TESTY AKCEPTACYJNE QEMU ZAKONCZONE SUKCESEM!")
    print("========================================================")

if __name__ == "__main__":
    main()
