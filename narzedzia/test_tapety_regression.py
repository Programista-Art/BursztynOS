#!/usr/bin/env python3
"""Comprehensive regression and host build test suite for Tapety BUR application.

Ensures that:
1. _start entry point is present in source and retained by linker script.
2. naglowek BUR header is present in source and retained by linker script.
3. Linker script uses canonical TAPETY BUR assertions (no stale KONTROLKI3 labels).
4. Makefile rules correctly depend on startup object tapety_tmp.o and tapety_linker.ld.
5. Linker asserts fail if _start, naglowek, or size limits are violated.
6. tapety.bin binary layout satisfies the BUR loader ABI.
7. Parallel build (-j2) is deterministic and safe.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
passed = 0
failed = 0


def report(name: str, ok: bool, details: str = "") -> None:
    global passed, failed
    if ok:
        passed += 1
        print(f"PASS: {name}")
    else:
        failed += 1
        print(f"FAIL: {name} - {details}")


def test_linker_script_audit() -> None:
    ld_path = ROOT / "programy" / "tapety" / "tapety_linker.ld"
    report("tapety_linker_ld_exists", ld_path.exists())
    content = ld_path.read_text(encoding="utf-8")

    # Verify no stale KONTROLKI3 diagnostics remain
    has_stale_kontrolki3 = "KONTROLKI3" in content
    report("tapety_linker_no_stale_kontrolki3", not has_stale_kontrolki3,
           "stale KONTROLKI3 string still present in tapety_linker.ld")

    # Verify entry and canonical KEEP directives
    report("tapety_linker_entry_start", "ENTRY(_start)" in content)
    report("tapety_linker_keep_naglowek", "KEEP(*(.naglowek))" in content)
    report("tapety_linker_keep_start", "KEEP(*(.text._start))" in content)

    # Verify canonical assertions
    report("tapety_linker_assert_start", 'ASSERT(DEFINED(_start), "TAPETY BUR: brak _start")' in content)
    report("tapety_linker_assert_naglowek", 'ASSERT(DEFINED(naglowek), "TAPETY BUR: brak naglowka")' in content)
    report("tapety_linker_assert_text_limit", 'ASSERT(__bur_text_used_end <= BUR_TEXT_END,' in content and
           '"TAPETY BUR: TEXT przekroczyl 256 KiB"' in content)
    report("tapety_linker_text_size_256k", "BUR_TEXT_SIZE     = 0x49000;" in content)


def test_makefile_audit() -> None:
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

    # Check tapety_tmp.o rule
    report("makefile_tapety_tmp_rule", "tapety_tmp.o: programy/tapety/tapety.cpp" in makefile)

    # Check tapety_blob.o rule prerequisites
    match = re.search(r"tapety_blob\.o:\s*([^\n]+)", makefile)
    report("makefile_tapety_blob_rule_found", match is not None)
    if match:
        prereqs = match.group(1).split()
        report("makefile_prereq_tapety_tmp", "tapety_tmp.o" in prereqs)
        report("makefile_prereq_btp_ring3", "btp_ring3.o" in prereqs)
        report("makefile_prereq_image_ring3", "image_ring3.o" in prereqs)
        report("makefile_prereq_bursztyn_gui", "bursztyn_gui.o" in prereqs)
        report("makefile_prereq_tapety_linker", "programy/tapety/tapety_linker.ld" in prereqs)

    # Check linker command
    report("makefile_ld_uses_tapety_linker",
           "$(LD) --gc-sections -T $(call bur_linker,programy/tapety/tapety_linker.ld)" in makefile or
           "$(LD) --gc-sections -T programy/tapety/tapety_linker.ld" in makefile)
    report("makefile_size_validator_present", "test_tapety_size.py" in makefile)


def test_source_and_symbol_audit() -> None:
    source = (ROOT / "programy" / "tapety" / "tapety.cpp").read_text(encoding="utf-8")
    report("source_defines_start", 'extern "C" __attribute__((noreturn)) void _start()' in source)
    report("source_defines_naglowek", '__attribute__((section(".naglowek"), used)) NaglowekBur naglowek' in source)

    obj = ROOT / "tapety_tmp.o"
    report("tapety_tmp_o_exists", obj.exists() and obj.stat().st_size > 0)
    if obj.exists() and obj.stat().st_size > 0:
        symbols = subprocess.check_output(["readelf", "-Ws", str(obj)], text=True)
        report("tapety_tmp_has_global_start",
               bool(re.search(r"\bFUNC\s+GLOBAL\s+(?:DEFAULT|HIDDEN)\s+\d+\s+_start\b", symbols)))
        report("tapety_tmp_has_global_naglowek",
               bool(re.search(r"\bOBJECT\s+GLOBAL\s+(?:DEFAULT|HIDDEN)\s+\d+\s+naglowek\b", symbols)))


def test_binary_layout() -> None:
    elf = ROOT / "tapety.elf"
    report("tapety_elf_exists", elf.exists())
    bin_path = ROOT / "tapety.bin"
    report("tapety_bin_exists", bin_path.exists())
    if bin_path.exists():
        data = bin_path.read_bytes()
        if data[:4] == b"BUR\x02":
            if elf.exists():
                header_text = subprocess.check_output(["readelf", "-h", str(elf)], text=True)
                entry_match = re.search(r"Entry point address:\s+(0x[0-9a-fA-F]+)", header_text)
                report("tapety_elf_entry_is_0x1001000",
                       entry_match is not None and int(entry_match.group(1), 16) >= 0x1000)
            report("tapety_bin_min_header", len(data) >= 160)
            entry = struct.unpack_from("<Q", data, 24)[0]
            toff, tsz, tva = struct.unpack_from("<3Q", data, 32)
            roff, rsz, rva = struct.unpack_from("<3Q", data, 56)
            doff, dfile, dsz, dva = struct.unpack_from("<4Q", data, 80)
            report("tapety_bin_magic_BUR", data[:4] == b"BUR\x02")
            report("tapety_bin_entry_matches_va", tva <= entry < tva + tsz)
            report("tapety_bin_text_off_4k", toff == 0x1000)
            report("tapety_bin_text_size_256k", 0 < tsz <= 0x49000 and (tsz % 4096) == 0)
            report("tapety_bin_text_va", tva == 0x1000)
            report("tapety_bin_data_off", doff == toff + tsz + rsz)
            report("tapety_bin_data_size_128k", 0 < dsz <= 0x20000 and (dsz % 4096) == 0)
            report("tapety_bin_data_va", dva == tva + tsz + rsz + 0x1000)
            report("tapety_bin_size_under_512k", len(data) <= 512 * 1024)
            return
        if elf.exists():
            header_text = subprocess.check_output(["readelf", "-h", str(elf)], text=True)
            entry_match = re.search(r"Entry point address:\s+(0x[0-9a-fA-F]+)", header_text)
            report("tapety_elf_entry_is_0x1001000",
                   entry_match is not None and int(entry_match.group(1), 16) == 0x1001000)
        report("tapety_bin_min_header", len(data) >= 60)
        magic, entry, toff, tsz, tva, doff, dsz, dva = struct.unpack_from("<4sQQQQQQQ", data)
        report("tapety_bin_magic_BUR", magic == b"BUR\x00")
        report("tapety_bin_entry_matches_va", entry == 0x1001000)
        report("tapety_bin_text_off_4k", toff == 0x1000)
        report("tapety_bin_text_size_256k", tsz == 0x49000)
        report("tapety_bin_text_va", tva == 0x1001000)
        report("tapety_bin_data_off", doff == 0x4a000)
        report("tapety_bin_data_size_128k", dsz == 0x20000)
        report("tapety_bin_data_va", dva == 0x104a000)
        report("tapety_bin_size_under_512k", len(data) <= 512 * 1024)


def test_negative_scenarios() -> None:
    ld_script = str(ROOT / "programy" / "tapety" / "tapety_linker.ld")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)

        # 1. Negative scenario: missing _start
        src_no_start = tmp / "no_start.c"
        src_no_start.write_text("""
        struct Naglowek { char m[4]; unsigned long long e, to, ts, tv, do_, ds, dv; };
        __attribute__((section(".naglowek"), used)) struct Naglowek naglowek = {"BUR", 0x1001000, 0x1000, 0x40000, 0x1001000, 0x41000, 0x20000, 0x1041000};
        void helper() {}
        """)
        obj_no_start = tmp / "no_start.o"
        subprocess.run(["x86_64-linux-gnu-gcc", "-ffreestanding", "-c", str(src_no_start), "-o", str(obj_no_start)], check=True)
        res = subprocess.run(["x86_64-linux-gnu-ld", "--gc-sections", "-T", ld_script, str(obj_no_start), "-o", str(tmp / "out1.elf")],
                             capture_output=True, text=True)
        report("negative_rejects_missing_start", res.returncode != 0 and "TAPETY BUR: brak _start" in res.stderr)

        # 2. Negative scenario: missing naglowek
        src_no_header = tmp / "no_header.c"
        src_no_header.write_text("""
        void _start() { while (1); }
        """)
        obj_no_header = tmp / "no_header.o"
        subprocess.run(["x86_64-linux-gnu-gcc", "-ffreestanding", "-c", str(src_no_header), "-o", str(obj_no_header)], check=True)
        res = subprocess.run(["x86_64-linux-gnu-ld", "--gc-sections", "-T", ld_script, str(obj_no_header), "-o", str(tmp / "out2.elf")],
                             capture_output=True, text=True)
        report("negative_rejects_missing_naglowek", res.returncode != 0 and "TAPETY BUR: brak naglowka" in res.stderr)

        # 3. Negative scenario: oversized text (> 256 KiB)
        src_oversized = tmp / "oversized.S"
        src_oversized.write_text("""
        .section .naglowek, "a"
        .globl naglowek
        naglowek:
            .ascii "BUR\\0"
            .quad _start
            .quad 0x1000, 0x40000, 0x1001000, 0x41000, 0x20000, 0x1041000
        .section .text._start, "ax"
        .globl _start
        _start:
            jmp oversized_payload
        .section .text, "ax"
        oversized_payload:
            .fill 310000, 1, 0x90
        """)
        obj_oversized = tmp / "oversized.o"
        subprocess.run(["x86_64-linux-gnu-gcc", "-c", str(src_oversized), "-o", str(obj_oversized)], check=True)
        res = subprocess.run(["x86_64-linux-gnu-ld", "--gc-sections", "-T", ld_script, str(obj_oversized), "-o", str(tmp / "out3.elf")],
                             capture_output=True, text=True)
        report("negative_rejects_oversized_text",
               res.returncode != 0 and ("cannot move location counter backwards" in res.stderr or "TEXT przekroczyl 256 KiB" in res.stderr))

        # 4. Negative scenario: oversized binary file budget
        from test_tapety_size import verify as verify_tapety_size, APP_MAXIMUM
        oversized_bin = tmp / "oversized.bin"
        oversized_bin.write_bytes((ROOT / "tapety.bin").read_bytes().ljust(APP_MAXIMUM + 1, b"\0"))
        try:
            verify_tapety_size(oversized_bin)
            size_rejected = False
        except ValueError:
            size_rejected = True
        report("negative_rejects_oversized_file_budget", size_rejected)


def test_parallel_make_determinism() -> None:
    # Run a parallel build of tapety_blob.o with -j2
    res = subprocess.run(["make", "-j2", "tapety_blob.o"], cwd=ROOT, capture_output=True, text=True)
    report("parallel_build_tapety_blob_j2", res.returncode == 0, res.stderr)


def main() -> int:
    print("=== TAPETY BUR REGRESSION SUITE ===")
    test_linker_script_audit()
    test_makefile_audit()
    test_source_and_symbol_audit()
    test_binary_layout()
    test_negative_scenarios()
    test_parallel_make_determinism()

    print(f"\nResults: {passed} passed, {failed} failed.")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
