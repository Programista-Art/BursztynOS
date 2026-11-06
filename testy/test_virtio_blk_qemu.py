#!/usr/bin/env python3
"""
testy/test_virtio_blk_qemu.py - Test bramkowania bezpieczenstwa i weryfikacji niszczacych
testow VirtIO-BLK w srodowisku QEMU.

Macierz testowa (5 scenariuszy bezpieczenstwa):
  1. bursztyn.test=automated (tylko) -> niszczacy zapis BLK pominiety, log informacyjny, dysk nienaruszony.
  2. bursztyn.test.diskdestructive=1 (tylko) -> brak trybu testowego, normal boot sanity check, zero zapisu.
  3. bursztyn.test=automated + diskdestructive=1, ale brak/uszkodzony znacznik -> niszczacy zapis pominiety, zero zapisu.
  4. bursztyn.test=automated + diskdestructive=1 + prawidlowy znacznik testowy -> niszczacy test PASS, wzorzec zweryfikowany.
  5. Rozruch normalny (BursztynOS.iso) -> zero zapisu (weryfikacja przez test_normal_boot_qemu.py).
"""

import sys
import os
import time
import struct
import zlib
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Struktura VirtioBlkTestMarker zgodna z virtio_blk.h:
# - magic: 32 B ("BURSZTYN_TEST_SCRATCH_DISK_V1\0...")
# - signature: uint64 (0x425552535A54594E, "BURSZTYN")
# - version: uint32 (1)
# - flags: uint32 (bit 0 = 1: VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE)
# - scratch_lba: uint64 (0 = ostatni sektor nosnika)
# - scratch_sectors: uint32 (1)
# - reserved: 448 B
# - crc32: uint32 (IEEE 802.3 pierwszych 508 bajtow)
VIRTIO_BLK_TEST_MAGIC = b"BURSZTYN_TEST_SCRATCH_DISK_V1"
VIRTIO_BLK_TEST_SIGNATURE = 0x425552535A54594E
VIRTIO_BLK_TEST_VERSION = 1
VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE = 1

def create_marker_bytes(scratch_lba=0, scratch_sectors=1, flags=VIRTIO_BLK_TEST_FLAG_DESTRUCTIVE,
                        corrupt_magic=False, corrupt_crc=False):
    magic = b"INVALID_CORRUPTED_MAGIC_1234567\x00" if corrupt_magic else (VIRTIO_BLK_TEST_MAGIC + b"\x00" * (32 - len(VIRTIO_BLK_TEST_MAGIC)))
    signature = VIRTIO_BLK_TEST_SIGNATURE
    version = VIRTIO_BLK_TEST_VERSION
    reserved = b"\x00" * 448
    hdr = struct.pack("<32sQIIQI448s", magic, signature, version, flags, scratch_lba, scratch_sectors, reserved)
    crc = zlib.crc32(hdr) & 0xFFFFFFFF
    if corrupt_crc:
        crc ^= 0xDEADBEEF
    return hdr + struct.pack("<I", crc)

def write_test_marker(disk_path, **kwargs):
    marker = create_marker_bytes(**kwargs)
    with open(disk_path, "r+b") as f:
        f.seek(512) # LBA 1
        f.write(marker)

def run_scenario(scenario_num, name, iso_path, marker_fn, expected_logs, forbidden_logs, check_disk_fn, timeout=180):
    print("\n" + "-" * 70)
    print(f"URUCHAMIANIE: Scenariusz {scenario_num} - {name}")
    print(f"  ISO:  {iso_path.name}")
    print("-" * 70)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    blk_disk = ROOT / f".virtio_blk_test_s{scenario_num}.img"
    with open(blk_disk, "wb") as f:
        f.truncate(64 * 1024 * 1024) # 64 MiB scratch disk

    if marker_fn:
        marker_fn(blk_disk)

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
        "-cdrom", str(iso_path)
    ]

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    found_expected = {e: False for e in expected_logs}
    found_forbidden = []
    lines_captured = []
    start_time = time.time()
    early_exit = False

    try:
        while time.time() - start_time < timeout:
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                time.sleep(0.05)
                continue
            line_str = line.strip()
            lines_captured.append(line_str)
            print(f"  [QEMU-SERIAL] {line_str}", flush=True)

            for exp in expected_logs:
                if exp in line_str:
                    found_expected[exp] = True

            for forb in forbidden_logs:
                if forb in line_str and forb not in found_forbidden:
                    found_forbidden.append(forb)

            # Warunki wczesnego zakonczenia po osiagnieciu celu scenariusza:
            if all(found_expected.values()):
                if "[VIRTIO-BLK-STRESS] SUCCESS:" in line_str or "[NORMAL BOOT]" in line_str:
                    early_exit = True
                    break
            if "[BSP] Drzewiasty system plikow PSF gotowy" in line_str:
                early_exit = True
                break

    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    # Weryfikacja logow
    missing_expected = [exp for exp, found in found_expected.items() if not found]
    logs_ok = (len(missing_expected) == 0 and len(found_forbidden) == 0)

    # Weryfikacja zawartosci dysku
    disk_ok, disk_msg = check_disk_fn(blk_disk)

    if blk_disk.exists():
        try:
            blk_disk.unlink()
        except Exception:
            pass

    success = logs_ok and disk_ok
    if success:
        print(f"-> [WYNIK S{scenario_num}] PASS ({disk_msg})")
    else:
        print(f"-> [WYNIK S{scenario_num}] FAIL!")
        if missing_expected:
            print(f"   Brakujace oczekiwane logi: {missing_expected}", file=sys.stderr)
        if found_forbidden:
            print(f"   Niedozwolone logi wystapily: {found_forbidden}", file=sys.stderr)
        if not disk_ok:
            print(f"   Blad weryfikacji dysku: {disk_msg}", file=sys.stderr)

    return success

def main():
    parser = argparse.ArgumentParser(description="Test macierzy bramkowania bezpieczenstwa VirtIO-BLK.")
    parser.add_argument("--scenario", type=int, choices=[1, 2, 3, 4], default=0,
                        help="Uruchom tylko wybrany scenariusz (1..4). Domyslnie: wszystkie.")
    args = parser.parse_args()

    # Zapewnienie istnienia obrazow ISO
    ci_iso = ROOT / ".bursztyn_ci_test.iso"
    if not ci_iso.exists():
        subprocess.run(["make", ".bursztyn_ci_test.iso"], cwd=str(ROOT), check=True)

    dest_only_iso = ROOT / ".bursztyn_ci_destructive_only.iso"
    if not dest_only_iso.exists():
        subprocess.run(["make", ".bursztyn_ci_destructive_only.iso"], cwd=str(ROOT), check=True)

    blk_destructive_iso = ROOT / ".bursztyn_ci_blk_destructive.iso"
    if not blk_destructive_iso.exists():
        subprocess.run(["make", ".bursztyn_ci_blk_destructive.iso"], cwd=str(ROOT), check=True)

    print("====================================================================")
    print(" BURSZTYN OS - AUDYT I BRAMKOWANIE BEZPIECZENSTWA VIRTIO-BLK (QEMU)")
    print("====================================================================")

    scenarios = []

    # Scenariusz 1: Tylko flaga automated (brak bursztyn.test.diskdestructive=1)
    def s1_check_disk(disk_path):
        with open(disk_path, "rb") as f:
            f.seek(64 * 1024 * 1024 - 512)
            last_sec = f.read(512)
        if last_sec == b"\x00" * 512:
            return True, "Ostatni sektor nosnika nienaruszony (same zera)"
        return False, f"Ostatni sektor zmodyfikowany (nie-zera)!"

    scenarios.append((
        1,
        "Flaga 'bursztyn.test=automated' (brak flagi niszczacej)",
        ci_iso,
        lambda p: write_test_marker(p), # Prawidlowy znacznik obecny
        [
            "[VIRTIO-BLK-STRESS] Step 3 skipped: destructive disk test flag not set",
            "[VIRTIO-BLK-STRESS] SUCCESS: read I/O, ring wraparound, reset and PMM delta=0 verified (destructive write skipped)."
        ],
        [
            "[VIRTIO-BLK-STRESS] Step 3: write, flush, readback verification"
        ],
        s1_check_disk
    ))

    # Scenariusz 2: Tylko flaga diskdestructive=1 (brak bursztyn.test=automated)
    def s2_check_disk(disk_path):
        with open(disk_path, "rb") as f:
            f.seek(64 * 1024 * 1024 - 512)
            last_sec = f.read(512)
        if last_sec == b"\x00" * 512:
            return True, "Zero zapisu na dysku (normal boot)"
        return False, "Sektor zmodyfikowany!"

    scenarios.append((
        2,
        "Flaga 'bursztyn.test.diskdestructive=1' (brak bursztyn.test=automated)",
        dest_only_iso,
        lambda p: write_test_marker(p),
        [
            "[NORMAL BOOT] VirtIO self-tests skipped.",
            "[VIRTIO-BLK] Sanity check: OK (read LBA 0)."
        ],
        [
            "[VIRTIO-BLK-STRESS]"
        ],
        s2_check_disk
    ))

    # Scenariusz 3: Obie flagi, ale brak / uszkodzony znacznik dysku testowego
    def s3_check_disk(disk_path):
        with open(disk_path, "rb") as f:
            f.seek(64 * 1024 * 1024 - 512)
            last_sec = f.read(512)
        if last_sec == b"\x00" * 512:
            return True, "Dysk bez poprawnego znacznika ochroniony przed zapisem"
        return False, "Sektor zmodyfikowany mimo braku znacznika!"

    scenarios.append((
        3,
        "Obie flagi aktywne, ale uszkodzony znacznik dysku (bad magic)",
        blk_destructive_iso,
        lambda p: write_test_marker(p, corrupt_magic=True),
        [
            "[VIRTIO-BLK-STRESS] Step 3 skipped: missing or invalid test-disk magic.",
            "[VIRTIO-BLK-STRESS] SUCCESS: read I/O, ring wraparound, reset and PMM delta=0 verified (destructive write skipped)."
        ],
        [
            "[VIRTIO-BLK-STRESS] Step 3: write, flush, readback verification"
        ],
        s3_check_disk
    ))

    # Scenariusz 4: Obie flagi aktywne ORAZ prawidlowy znacznik dysku dedykowanego
    def s4_check_disk(disk_path):
        expected_pattern = bytes((i * 7 + 0xA5) & 0xFF for i in range(512))
        with open(disk_path, "rb") as f:
            f.seek(64 * 1024 * 1024 - 512)
            last_sec = f.read(512)
        if last_sec == expected_pattern:
            return True, "Wzorzec testowy 0xA5 poprawnie zapisany i potwierdzony na dedykowanym dysku"
        return False, f"Zawartosc ostatniego sektora niezgodna ze wzorcem! (pierwszy bajt: {last_sec[0] if last_sec else 'pusty'})"

    scenarios.append((
        4,
        "Obie flagi aktywne + prawidlowy znacznik dysku dedykowanego (Full Destructive Pass)",
        blk_destructive_iso,
        lambda p: write_test_marker(p),
        [
            "[VIRTIO-BLK-STRESS] Test-disk identity verified (dedicated scratch disk). Executing write, flush, readback...",
            "[VIRTIO-BLK-STRESS] Step 3: write, flush, readback verification",
            "[VIRTIO-BLK-STRESS] SUCCESS: raw I/O, flush, ring wraparound, reset and PMM delta=0 verified."
        ],
        [],
        s4_check_disk
    ))

    results = {}
    for sc in scenarios:
        num = sc[0]
        if args.scenario != 0 and args.scenario != num:
            continue
        ok = run_scenario(*sc)
        results[num] = ok

    print("\n" + "=" * 70)
    print("PODSUMOWANIE MACIERZY BRAMKOWANIA BEZPIECZENSTWA VIRTIO-BLK:")
    print("=" * 70)
    for num, ok in results.items():
        status_str = "PASS" if ok else "FAIL"
        print(f"  Scenariusz {num}: {status_str}")
    print("=" * 70)

    if all(results.values()):
        print("\n[SUKCES] Wszystkie scenariusze bramkowania bezpieczenstwa VirtIO-BLK zakonczone sukcesem (PASS)!\n")
        sys.exit(0)
    else:
        print("\n[BLAD] Jeden lub wiecej scenariuszy bezpieczenstwa zakonczylo sie niepowodzeniem!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
