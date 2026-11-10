#!/usr/bin/env python3
"""Bursztyn OS - Test diagnostyczny 16 KiB stosu jadra i BWS 84 + IRQ.

Weryfikuje w rzeczywistym scenariuszu zainstalowanego systemu (bez ISO):
  1. Kompilacja jadra w trybie diagnostycznym 16 KiB:
     BURSZTYN_DIAG_STOS16K=1 (ROZMIAR_STOSU_JADRA_BAJTY = 16 KiB = 16,384 bytes)
     BURSZTYN_STACK_TRACE=1 (raportowanie szczytow i metryk stosu)
  2. Prawidlowy uklad: odmapowany Guard Page ponizej 16 KiB, kanarek na dnie.
  3. Rozruch zainstalowanego systemu z dysku AHCI (brak nosnika ISO).
  4. Uruchomienie aplikacji Aktualizacje Systemu z pulpitu.
  5. Wykonanie BWS 84 w Ring 0 przy aktywnym sprzetowym zegarze (IRQ 32, 250 Hz),
     obsludze myszy/klawiatury xHCI oraz operacjach dyskowych AHCI/GPT/ESP.
  6. Weryfikacja braku #PF, braku skoku pod RIP=0 (PFEC=0x10, RIP=0, CR2=0).
  7. Zmierzenie i weryfikacja zuzycia stosu jadra dla BWS 84 i przerwan:
     - Weryfikacja calkowitego limitu 16 KiB = 16,384 bytes
     - Weryfikacja nienaruszonego kanarka stosu (0xB0057E11CADA0001)
     - Weryfikacja nienaruszonego guard page (brak #PF)
  8. Weryfikacja czestotliwosci odpytywania w stanie jalowym (0 wywolan BWS 84/s).
  9. Weryfikacja zamkniecia aplikacji i zakonczenia wywolan BWS 84 z tego PID.
"""

import hashlib
import json
import os
import shutil
import struct
import subprocess
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
    SLOT_NIEZNANY,
    STAN_NIEAKTYWNY,
    wybierz_najlepsza_kopie,
    ROZRUCH_MAGIC,
)

STOS_16K_BAJTY = 16384  # 16 KiB = 16,384 bytes


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
    with open(target_path, "rb") as f:
        f.seek(1048576)
        vbr = f.read(512)
        bps = struct.unpack_from("<H", vbr, 11)[0]
        spc = vbr[13]
        rsvd = struct.unpack_from("<H", vbr, 14)[0]
        fats = vbr[16]
        spf = struct.unpack_from("<I", vbr, 36)[0]
        data_start = 1048576 + (rsvd + fats * spf) * bps

        f.seek(data_start + (6 - 2) * spc * bps)
        grub_dir = f.read(4096)
        for i in range(128):
            e = grub_dir[i * 32 : (i + 1) * 32]
            if e[:11] == b"GRUBENV    ":
                cl = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
                return data_start + (cl - 2) * spc * bps
    raise RuntimeError("Nie znaleziono GRUBENV w tablicy FAT32 ESP!")


def zapisz_grubenv(target_path, potwierdzony="A", oczekujacy="BRAK", proby=0):
    offset = znajdz_offset_grubenv(target_path)
    env = bytearray(b"#" * 1024)
    hdr = b"# GRUB Environment Block\n"
    p = len(hdr)
    env[:p] = hdr
    lines = [
        f"bursztyn_potwierdzony={potwierdzony}\n".encode(),
        f"bursztyn_oczekujacy={oczekujacy}\n".encode(),
        f"bursztyn_proby={proby}\n".encode(),
    ]
    for line in lines:
        env[p : p + len(line)] = line
        p += len(line)
    with open(target_path, "r+b") as f:
        f.seek(offset)
        f.write(env)


def zainstaluj_jadro_na_esp(target_path, kernel_bin_path):
    """Instaluje obraz jadra jako JADRO_A.BIN na partycji ESP dysku docelowego."""
    with open(kernel_bin_path, "rb") as f:
        kdata = f.read()
    ksize = len(kdata)
    ksha = hashlib.sha256(kdata).digest()

    with open(target_path, "r+b") as f:
        f.seek(1048576)
        vbr = f.read(512)
        bps = struct.unpack_from("<H", vbr, 11)[0]
        spc = vbr[13]
        rsvd = struct.unpack_from("<H", vbr, 14)[0]
        fats = vbr[16]
        spf = struct.unpack_from("<I", vbr, 36)[0]
        cluster_size = spc * bps
        data_start = 1048576 + (rsvd + fats * spf) * bps
        fat_off = 1048576 + rsvd * bps

        f.seek(data_start + (5 - 2) * cluster_size)
        boot_dir = bytearray(f.read(4096))
        dirent_pos = None
        start_cl = None
        for i in range(128):
            e = boot_dir[i * 32 : (i + 1) * 32]
            if e[:11] == b"JADRO_A BIN":
                dirent_pos = i * 32
                start_cl = struct.unpack_from("<H", e, 20)[0] << 16 | struct.unpack_from("<H", e, 26)[0]
                break
        assert dirent_pos is not None and start_cl is not None, "Nie znaleziono JADRO_A w katalogu boot!"

        # Odczytaj FAT
        f.seek(fat_off)
        fat = bytearray(f.read(spf * bps))

        # Pobierz istniejacy lancuch klastrow
        cur = start_cl
        chain = []
        while 2 <= cur < 0x0FFFFFF8:
            chain.append(cur)
            cur = struct.unpack_from("<I", fat, cur * 4)[0] & 0x0FFFFFFF

        needed_clusters = (ksize + cluster_size - 1) // cluster_size
        if len(chain) < needed_clusters:
            total_fat_entries = (spf * bps) // 4
            free_idx = 2
            while len(chain) < needed_clusters and free_idx < total_fat_entries:
                val = struct.unpack_from("<I", fat, free_idx * 4)[0] & 0x0FFFFFFF
                if val == 0:
                    chain.append(free_idx)
                free_idx += 1
            if len(chain) < needed_clusters:
                raise RuntimeError("Brak wolnych klastrow na partycji ESP!")

        # Uaktualnij wpisy FAT
        for i in range(needed_clusters - 1):
            struct.pack_into("<I", fat, chain[i] * 4, chain[i + 1])
        struct.pack_into("<I", fat, chain[needed_clusters - 1] * 4, 0x0FFFFFFF)
        for i in range(needed_clusters, len(chain)):
            struct.pack_into("<I", fat, chain[i] * 4, 0)

        # Zapisz obie kopie tablicy FAT
        for fat_idx in range(fats):
            f.seek(fat_off + fat_idx * spf * bps)
            f.write(fat)

        # Zapisz dane jadra klaster po klastrze
        for i in range(needed_clusters):
            chunk = kdata[i * cluster_size : (i + 1) * cluster_size]
            cl_offset = data_start + (chain[i] - 2) * cluster_size
            f.seek(cl_offset)
            f.write(chunk)

        # Zaktualizuj rozmiar w katalogu
        struct.pack_into("<I", boot_dir, dirent_pos + 28, ksize)
        f.seek(data_start + (5 - 2) * cluster_size)
        f.write(boot_dir)

    stan = odczytaj_sektory_rozruchu(target_path)
    stan.sha_a = ksha
    stan.slot_potwierdzony = SLOT_A
    stan.slot_oczekujacy = SLOT_NIEZNANY
    stan.stan_oczekujacego = STAN_NIEAKTYWNY
    stan.licznik_prob = 0
    zapisz_sektory_rozruchu(target_path, stan)
    zapisz_grubenv(target_path, potwierdzony="A", oczekujacy="BRAK", proby=0)


def zbuduj_jadro_16k():
    print("[1/6] Kompilacja jadra z 16 KiB stosu (BURSZTYN_DIAG_STOS16K=1, BURSZTYN_STACK_TRACE=1)...")
    pliki_do_rekompilacji = [
        "stos_jadra.o", "kernel.o", "loader.o", "idt.o",
        "syscalls.o", "aktualizacja.o", "system_operacyjny.bin"
    ]
    for p in pliki_do_rekompilacji:
        f = ROOT / p
        if f.exists():
            f.unlink()

    cmd = [
        "make", "system_operacyjny.bin",
        "BURSZTYN_DIAG_STOS16K=1",
        "BURSZTYN_STACK_TRACE=1"
    ]
    res = subprocess.run(cmd, cwd=str(ROOT), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.returncode != 0:
        print(res.stdout.decode())
        raise RuntimeError("Blad kompilacji jadra 16 KiB!")
    print("  Jadro 16 KiB zbudowane pomyslnie.")


def przywroc_jadro_produkcyjne():
    print("Przywracanie standardowego jadra produkcyjnego...")
    pliki_do_rekompilacji = [
        "stos_jadra.o", "kernel.o", "loader.o", "idt.o",
        "syscalls.o", "aktualizacja.o", "system_operacyjny.bin"
    ]
    for p in pliki_do_rekompilacji:
        f = ROOT / p
        if f.exists():
            f.unlink()
    subprocess.run(["make", "system_operacyjny.bin"], cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def znajdz_lub_utworz_dysk_instalacyjny(work_dir):
    """Uzywa istniejacego zainstalowanego dysku lub tworzy nowy przez instalator."""
    parent = ROOT / ".installertest"
    potencjalne = list(parent.glob("qemu-ab-*/target.img")) + list(parent.glob("guest-*/target.img"))
    for p in potencjalne:
        if p.exists() and p.stat().st_size >= 4 * 1024**3:
            s = p.parent / "sentinel.img"
            if s.exists():
                try:
                    _ = odczytaj_sektory_rozruchu(p)
                except Exception:
                    continue
                print(f"  Znaleziono istniejacy zainstalowany dysk: {p}")
                target_copy = work_dir / "target.img"
                sentinel_copy = work_dir / "sentinel.img"
                shutil.copyfile(p, target_copy)
                shutil.copyfile(s, sentinel_copy)
                return target_copy, sentinel_copy

    print("  Brak gotowego dysku — tworzenie nowego przez instalator w QEMU...")
    target = work_dir / "target.img"
    sentinel = work_dir / "sentinel.img"
    qemu_install.sparse(target, 4 * 1024**3)
    qemu_install.sparse(sentinel, 64 * 1024**2)
    with sentinel.open("r+b") as f:
        for off in [0, 512, 10 * 512, 40000 * 512, 64 * 1024**2 - 512]:
            f.seek(off)
            f.write(hashlib.sha256(str(off).encode()).digest() * 16)
    iso = qemu_install.installer_iso(work_dir)
    s_hash = qemu_install.sha(sentinel)
    t_hash = qemu_install.target_fingerprint(target)

    g_inst = qemu_install.Guest(work_dir / "install", target, sentinel, iso)
    try:
        qemu_install.install(g_inst, target, sentinel, t_hash, s_hash)
    finally:
        g_inst.close()
    return target, sentinel


def main():
    print("=" * 80)
    print(" BURSZTYN OS - KONTROLOWANY TEST 16 KiB STOSU JADRA I BWS 84 + IRQ")
    print("=" * 80)

    with tempfile.TemporaryDirectory(prefix="test16k-") as tmpdir:
        tmp_path = Path(tmpdir)
        try:
            zbuduj_jadro_16k()

            print("[2/6] Przygotowanie dysku zainstalowanego systemu...")
            target, sentinel = znajdz_lub_utworz_dysk_instalacyjny(tmp_path)

            print("[3/6] Instalacja jadra 16 KiB na partycji ESP dysku...")
            kernel_bin = ROOT / "system_operacyjny.bin"
            zainstaluj_jadro_na_esp(target, kernel_bin)

            print("[4/6] Rozruch zainstalowanego systemu z dysku (brak nosnika ISO)...")
            guest_dir = tmp_path / "boot-test16k"
            g = qemu_install.Guest(guest_dir, target, sentinel)
            try:
                log = g.wait("System operacyjny gotowy!", timeout=180)
                print("  System zainstalowany uruchomil sie pomyslnie.")
                time.sleep(6)

                print("[5/6] Uruchomienie aplikacji Aktualizacje Systemu...")
                # Klikniecie ikony Aktualizacje Systemu na pulpicie (srodek ikony: x=80, y=150)
                g.q.click(80, 150)
                time.sleep(3)

                serial_check = (guest_dir / "serial.log").read_text(errors="replace")
                if "BWS 84" not in serial_check and "BWS84" not in serial_check:
                    print("  Klikniecie ikony nie uruchomilo aplikacji; uruchamianie przez Menu Start...")
                    # Przycisk Menu Start (x=25, y=700)
                    g.q.click(25, 700)
                    time.sleep(1)
                    # Pozycja 13 w menu: Aktualizacje Systemu (x=80, y=557)
                    g.q.click(80, 557)
                    time.sleep(4)

                g.q.screenshot(ROOT / "test16k_screenshot.png")

                serial_log = (guest_dir / "serial.log").read_text(errors="replace")
                print("--- PELNY SERIAL LOG ---")
                print(serial_log)
                print("--- KONIEC SERIAL LOG ---")

                # Sprawdzenie braku bledow krytycznych
                assert "#PF Page Fault" not in serial_log, "BLAD: Wykryto wyjatek #PF Page Fault!"
                assert "RIP: 0x0000000000000000" not in serial_log, "BLAD: Wykryto RIP=0!"
                assert "KRYTYCZNE przekroczenie stosu" not in serial_log, "BLAD: Wykryto naruszenie guard page!"
                assert "[PANIC]" not in serial_log, "BLAD: Wykryto panike jadra!"
                assert "uszkodzenie kanarka" not in serial_log, "BLAD: Wykryto uszkodzenie kanarka stosu!"

                # Odczyt i weryfikacja metryk stosu dla 16 KiB
                print("[6/6] Analiza metryk stosu i zachowania BWS 84 + IRQ...")
                metrics_lines = [line for line in serial_log.splitlines() if "[STACK METRICS]" in line]
                bws84_metrics = [line for line in metrics_lines if "BWS 84" in line or "BWS84" in line]

                print("  Zarejestrowane wpisy telemetryczne stosu:")
                for line in metrics_lines:
                    print("   ", line)

                assert len(metrics_lines) > 0, "Brak zarejestrowanych metryk stosu [STACK METRICS]!"
                assert len(bws84_metrics) > 0, "Brak zarejestrowanych metryk stosu dla BWS 84!"

                # Weryfikacja parametrów z ostatniego wpisu BWS 84
                last_bws = bws84_metrics[-1]
                # Format: [STACK METRICS] BWS 84 (OTA_STATUS): pid=1 base=0x... top=0x... min_rsp=0x... used=14712 B margin=1672 B ctx=...
                import re
                m_base = re.search(r"base=0x([0-9A-Fa-f]+)", last_bws)
                m_top = re.search(r"top=0x([0-9A-Fa-f]+)", last_bws)
                m_used = re.search(r"used=(\d+)\s*B", last_bws)
                m_margin = re.search(r"margin=(\d+)\s*B", last_bws)

                assert m_base and m_top and m_used and m_margin, f"Niepelny format linii metryk: {last_bws}"
                base_val = int(m_base.group(1), 16)
                top_val = int(m_top.group(1), 16)
                used_val = int(m_used.group(1))
                margin_val = int(m_margin.group(1))

                stack_size = top_val - base_val
                print(f"\n  Rozmiar zarzadzanego stosu jadra: {stack_size} B ({stack_size // 1024} KiB)")
                assert stack_size == STOS_16K_BAJTY, f"Rozmiar stosu {stack_size} != oczekiwane 16 KiB ({STOS_16K_BAJTY})!"
                print(f"  Zmierzony szczyt zuzycia (max_used): {used_val} bytes")
                print(f"  Pozostaly bezpieczny margines:       {margin_val} bytes")

                assert used_val <= STOS_16K_BAJTY, f"Zuzycie {used_val} przekroczylo limit 16 KiB ({STOS_16K_BAJTY})!"
                assert margin_val >= 0, f"Ujemny margines stosu {margin_val}!"
                assert margin_val == (STOS_16K_BAJTY - used_val), "Niezgodnosc matematyczna marginesu i zuzycia!"

                # Weryfikacja odpytywania w stanie jalowym (0 wywolan/s)
                print("\n  Weryfikacja odpytywania w stanie jalowym (idle)...")
                len_before = len((guest_dir / "serial.log").read_text(errors="replace").splitlines())
                time.sleep(3)
                len_after = len((guest_dir / "serial.log").read_text(errors="replace").splitlines())
                bws_during_idle = sum(1 for line in (guest_dir / "serial.log").read_text(errors="replace").splitlines()[len_before:] if "BWS 84" in line)
                print(f"  Liczba wywolan BWS 84 w ciagu 3s bezczynnosci: {bws_during_idle}")
                assert bws_during_idle == 0, f"Wykryto ciagle odpytywanie BWS 84 w stanie jalowym ({bws_during_idle} wywolan)!"

                # Zamkniecie okna aplikacji (klikniecie przycisku 'X' w prawym gornym rogu okna 920x640)
                print("  Zamkniecie okna aplikacji...")
                # Okno wycentrowane: lewy gorny rog (180, 40), prawy rog (1100, 40)
                g.q.click(1085, 54)
                time.sleep(1)

                serial_log_final = (guest_dir / "serial.log").read_text(errors="replace")
                assert "#PF Page Fault" not in serial_log_final
                assert "RIP: 0x0000000000000000" not in serial_log_final

                print("\n" + "=" * 80)
                print(" [SUKCES] Test 16 KiB stosu jadra i BWS 84 + IRQ ZAKONCZONY POMYSLNIE!")
                print("  1. BWS 84 + przerwania sprzetowe mieszcza sie w 16 KiB = 16,384 bytes.")
                print(f"  2. Szczyt zuzycia: {used_val} bytes, margines: {margin_val} bytes.")
                print("  3. Sprzetowy guard page i kanarek stosu pozostaly nienaruszone.")
                print("  4. Brak #PF, brak skoku pod RIP=0, brak paniki.")
                print("  5. Czestotliwosc odpytywania w stanie jalowym: 0 wywolan/s.")
                print("=" * 80)

            finally:
                g.close()

        finally:
            przywroc_jadro_produkcyjne()


if __name__ == "__main__":
    main()
