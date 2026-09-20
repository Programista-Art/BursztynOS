#!/usr/bin/env python3
"""
Test rozruchu Bursztyn OS w QEMU z urzadzeniem modern VirtIO PCI (virtio-rng-pci).
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

    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-drive", f"id=disk,file={disk},format=raw,if=none",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
        "-device", "virtio-rng-pci,disable-legacy=on",
        "-m", "2G",
        "-vga", "std",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso)
    ]

    print("Uruchamianie QEMU z modern virtio-rng-pci...")
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    found_virtio = False
    found_virtio_rng = False
    found_rng_stress = False
    virtio_lines = []
    start_time = time.time()
    timeout = 180 # sekund (uwzglednia powolna emulacje TCG single-thread podczas bootu AHCI i stress testu)

    try:
        while time.time() - start_time < timeout:
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                time.sleep(0.05)
                continue
            line_str = line.strip()
            print(f"[QEMU-SERIAL] {line_str}")
            if "[VIRTIO-PCI]" in line_str:
                found_virtio = True
                virtio_lines.append(line_str)
            if "[VIRTIO-RNG]" in line_str:
                found_virtio_rng = True
                virtio_lines.append(line_str)
            if "[VIRTIO-RNG-STRESS]" in line_str:
                virtio_lines.append(line_str)
                if "SUCCESS: 3072 transactions across 3 cycles completed. PMM delta=0, no queue corruption." in line_str:
                    found_rng_stress = True
            if "BSP] Drzewiasty system plikow PSF gotowy" in line_str:
                # Kernel dotarl do pelnej inicjalizacji
                break
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("\n" + "=" * 60)
    print("WYNIK TESTU QEMU VIRTIO-PCI I VIRTIO-RNG:")
    print("=" * 60)
    for l in virtio_lines:
        print(f"  {l}")

    if found_virtio and found_virtio_rng and found_rng_stress:
        print("\n[SUKCES] Urzadzenie VirtIO Modern PCI zostalo wykryte oraz test obciazeniowy VirtIO-RNG (3072 transakcje, 3 cykle, PMM delta=0) zakonczyl sie pelnym sukcesem!")
        sys.exit(0)
    elif not found_virtio:
        print("\n[BLAD] Nie znaleziono wpisow [VIRTIO-PCI] w logu portu szeregowego!", file=sys.stderr)
        sys.exit(1)
    elif not found_virtio_rng:
        print("\n[BLAD] Nie znaleziono wpisow inicjalizacji [VIRTIO-RNG] w logu portu szeregowego!", file=sys.stderr)
        sys.exit(1)
    else:
        print("\n[BLAD] Nie znaleziono udanego zakonczenia testu obciazeniowego [VIRTIO-RNG-STRESS] w logu portu szeregowego!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
