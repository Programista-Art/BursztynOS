#!/usr/bin/env python3
"""Run only the native loader regression (extract + compile + execute)."""
import importlib.util
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".hda-test" / "repair"

spec = importlib.util.spec_from_file_location("repair", ROOT / "testy" / "test_dzwieku_repair.py")
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)

OUT.mkdir(parents=True, exist_ok=True)
repair.extract_boot_install()
repair.extract_filesystem_bws()
repair.extract_checked_vmm()
print("extract OK")

flags = ["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Wpedantic",
         "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections",
         "-fsanitize=address,undefined", "-Wl,--gc-sections",
         "-I.", "-Ijadro/arch", "-Ijadro/mm", "-Ijadro/fs", "-Ijadro/bws", "-Ijadro/gui",
         "-Ijadro/siec", "-Ijadro/aktualizacja", "-Ijadro/diag", "-Ijadro/rdzen",
         "-Isterowniki/pci", "-Isterowniki/virtio", "-Isterowniki/czas", "-Isterowniki/platforma",
         "-Isterowniki/magazyn", "-Isterowniki/grafika", "-Isterowniki/siec", "-Isterowniki/wejscie",
         "-Isterowniki"]

env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

for trace in (0, 1):
    binary = OUT / f"loader-only-{trace}"
    subprocess.run(flags + ["-DBURSZTYN_LOADER_RUNTIME_HOST_TEST", "-DBURSZTYN_PSF_HOST_TEST",
                            f"-DBURSZTYNDIAGTESTDZWIEKU={trace}", "jadro/fs/loader.cpp",
                            "jadro/fs/storage_format.cpp", "testy/test_dzwieku_loader.cpp",
                            "-o", str(binary)], cwd=ROOT, check=True)
    print(f"compiled loader-only-{trace}")
    subprocess.run([str(binary), str(ROOT / "test_dzwieku.bin")], cwd=ROOT, env=env, check=True)
    print(f"loader W^X/NX scenario PASS (trace={trace})")
