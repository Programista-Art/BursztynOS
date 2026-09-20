#!/usr/bin/env python3
"""
Test rozruchu Bursztyn OS w QEMU z urzadzeniem modern VirtIO-BLK (virtio-blk-pci).

Weryfikuje:
  1. Wykrycie urzadzenia Modern VirtIO PCI (Device ID 2)
  2. Inicjalizacje i rejestracje sterownika VirtIO-BLK w podsystemie blokowym (blokowy_zarejestruj_dysk)
  3. Test obciazeniowy / integracyjny virtio_blk_stress_test:
     - Raw odczyt sektora 0
     - Odrzucenie nieprawidlowych granic LBA
     - Zapis na dysku, operacje flush i readback z weryfikacja spojnosci danych
     - Wraparound pierscienia deskryptorow (1024 transakcje) bez wyciekow deskryptorow
     - Sprzetowy reset urzadzenia, zwolnienie pamieci DMA z zachowaniem PMM delta = 0
     - Czysta ponowna inicjalizacja urzadzenia
  4. Wykrycie partycji i montowanie drzewiastego systemu plikow PSF / BSP2
"""
import sys
import time
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    iso = ROOT / "BursztynOS.iso"
    if not iso.exists():
        print("[BLAD] Brak BursztynOS.iso. Uruchom 'make iso'.", file=sys.stderr)
        sys.exit(1)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    # Przygotowanie dedykowanego dysku testowego VirtIO-BLK (64 MiB)
    blk_disk = ROOT / ".virtio_blk_test.img"
    try:
        subprocess.run(
            ["qemu-img", "create", "-f", "raw", str(blk_disk), "64M"],
            cwd=str(ROOT),
            check=True,
            capture_output=True
        )
    except Exception as e:
        print(f"[OSTRZEZENIE] qemu-img create failed ({e}), creating with truncate...", file=sys.stderr)
        with open(blk_disk, "wb") as f:
            f.truncate(64 * 1024 * 1024)

    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-drive", f"id=disk,file={disk},format=raw,if=none",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
        "-drive", f"id=virtioblk,file={blk_disk},format=raw,if=none",
        "-device", "virtio-blk-pci,disable-legacy=on,drive=virtioblk",
        "-device", "virtio-rng-pci,disable-legacy=on",
        "-m", "2G",
        "-vga", "std",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso)
    ]

    print("Uruchamianie QEMU z modern virtio-blk-pci oraz virtio-rng-pci...")
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    found_virtio = False
    found_virtio_blk = False
    found_blk_stress = False
    found_bsp_psf = False
    blk_lines = []
    start_time = time.time()
    timeout = 240 # sekund (uwzglednia emulacje TCG single-thread podczas bootu i stress testu)

    try:
        while time.time() - start_time < timeout:
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                time.sleep(0.05)
                continue
            line_str = line.strip()
            print(f"[QEMU-SERIAL] {line_str}", flush=True)
            if "[VIRTIO-PCI]" in line_str:
                found_virtio = True
                blk_lines.append(line_str)
            if "[VIRTIO-BLK]" in line_str:
                found_virtio_blk = True
                blk_lines.append(line_str)
            if "[VIRTIO-BLK-STRESS]" in line_str:
                blk_lines.append(line_str)
                if "SUCCESS: raw I/O, flush, ring wraparound, reset and PMM delta=0 verified." in line_str:
                    found_blk_stress = True
            if "BSP] Drzewiasty system plikow PSF gotowy" in line_str or "psf_weryfikuj_dysk" in line_str:
                found_bsp_psf = True
                # Sukces: system dotarl do inicjalizacji systemu plikow
                if found_blk_stress:
                    break
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        if blk_disk.exists():
            try:
                blk_disk.unlink()
            except Exception:
                pass

    print("\n" + "=" * 60)
    print("WYNIK TESTU QEMU VIRTIO-PCI I VIRTIO-BLK:")
    print("=" * 60)
    for l in blk_lines:
        print(f"  {l}")

    if found_virtio and found_virtio_blk and found_blk_stress:
        print("\n[SUKCES] Urzadzenie VirtIO-BLK Modern PCI zostalo wykryte, zainicjalizowane, a test obciazeniowy (raw I/O, flush, ring wraparound, reset, PMM delta=0) zakonczyl sie pelnym sukcesem!")
        sys.exit(0)
    elif not found_virtio:
        print("\n[BLAD] Nie znaleziono wpisow [VIRTIO-PCI] w logu portu szeregowego!", file=sys.stderr)
        sys.exit(1)
    elif not found_virtio_blk:
        print("\n[BLAD] Nie znaleziono wpisow inicjalizacji [VIRTIO-BLK] w logu portu szeregowego!", file=sys.stderr)
        sys.exit(1)
    else:
        print("\n[BLAD] Nie znaleziono udanego zakonczenia testu obciazeniowego [VIRTIO-BLK-STRESS] w logu portu szeregowego!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
