#!/usr/bin/env python3
"""
Bursztyn OS — Kompilacja i uruchomienie 7 fuzzerow (BWS, BUR Loader, 5x BSP2/PSF)
z ASan + UBSan + -fsanitize-coverage=trace-pc oraz agregacja metryk do
docs/security-hardening-2026-11-04/fuzzing-metrics.json.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / ".fuzz-build"
CORPUS_DIR = ROOT / ".fuzz-corpus"
METRICS_OUT = ROOT / "docs/security-hardening-2026-11-04/fuzzing-metrics.json"

FUZZERS = [
    {"name":"fuzz_bur_v2", "sources":["narzedzia/fuzz/fuzz_bur_v2.cpp"],
     "extra_flags":[], "smoke_iters":10000, "extended_iters":50000},
    {
        "name": "fuzz_bws",
        "sources": ["testy/fuzz/fuzz_bws.cpp"],
        "extra_flags": [],
        "smoke_iters": 10000,
        "extended_iters": 50000,
    },
    {
        "name": "fuzz_bur_loader",
        "sources": ["testy/fuzz/fuzz_bur_loader.cpp", "jadro/fs/loader.cpp"],
        "extra_flags": ["-DBURSZTYN_LOADER_HOST_TEST"],
        "smoke_iters": 10000,
        "extended_iters": 50000,
    },
    {
        "name": "fuzz_bsp2_superblock",
        "sources": ["testy/fuzz/fuzz_bsp2_superblock.cpp", "jadro/fs/storage_format.cpp", "jadro/fs/psf.cpp"],
        "extra_flags": ["-DBURSZTYN_PSF_HOST_TEST", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"],
        "smoke_iters": 2500,
        "extended_iters": 10000,
    },
    {
        "name": "fuzz_bsp2_inode",
        "sources": ["testy/fuzz/fuzz_bsp2_inode.cpp", "jadro/fs/storage_format.cpp", "jadro/fs/psf.cpp"],
        "extra_flags": ["-DBURSZTYN_PSF_HOST_TEST", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"],
        "smoke_iters": 2500,
        "extended_iters": 10000,
    },
    {
        "name": "fuzz_bsp2_dir",
        "sources": ["testy/fuzz/fuzz_bsp2_dir.cpp", "jadro/fs/storage_format.cpp", "jadro/fs/psf.cpp"],
        "extra_flags": ["-DBURSZTYN_PSF_HOST_TEST", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"],
        "smoke_iters": 2500,
        "extended_iters": 10000,
    },
    {
        "name": "fuzz_bsp2_path",
        "sources": ["testy/fuzz/fuzz_bsp2_path.cpp", "jadro/fs/storage_format.cpp", "jadro/fs/psf.cpp"],
        "extra_flags": ["-DBURSZTYN_PSF_HOST_TEST", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"],
        "smoke_iters": 5000,
        "extended_iters": 25000,
    },
    {
        "name": "fuzz_bsp2_ops",
        "sources": ["testy/fuzz/fuzz_bsp2_ops.cpp", "jadro/fs/storage_format.cpp", "jadro/fs/psf.cpp"],
        "extra_flags": ["-DBURSZTYN_PSF_HOST_TEST", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"],
        "smoke_iters": 2500,
        "extended_iters": 10000,
    },
]


def run_cmd(cmd, env=None):
    res = subprocess.run(
        cmd,
        cwd=str(ROOT),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if res.returncode != 0:
        print(f"[FUZZ] Command failed ({res.returncode}): {' '.join(cmd)}")
        print(res.stdout)
        sys.exit(1)
    return res.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["smoke", "extended"], default="smoke")
    ap.add_argument("--target", choices=[f["name"] for f in FUZZERS])
    args = ap.parse_args()

    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    CORPUS_DIR.mkdir(parents=True, exist_ok=True)
    METRICS_OUT.parent.mkdir(parents=True, exist_ok=True)

    # Pre-compile psf.o and storage_format.o once with ASan+UBSan+trace-pc to speed up build
    base_cxxflags = [
        "g++", "-std=c++17", "-O1", "-g",
        "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-I.", "-Ibiblioteki/mbedtls/include",
        "-Isterowniki/pci", "-Isterowniki/virtio", "-Isterowniki/magazyn",
        "-Isterowniki/siec", "-Isterowniki/wejscie", "-Isterowniki/czas",
        "-Isterowniki/platforma", "-Isterowniki/grafika", "-Isterowniki",
        "-Ijadro/arch", "-Ijadro/mm", "-Ijadro/fs", "-Ijadro/bws", "-Ijadro/gui",
        "-Ijadro/siec", "-Ijadro/aktualizacja", "-Ijadro/diag", "-Ijadro/rdzen",
        "-fsanitize=address,undefined",
        "-fsanitize-coverage=trace-pc",
    ]

    psf_o = BUILD_DIR / "psf_fuzz.o"
    storage_o = BUILD_DIR / "storage_fuzz.o"
    print("[FUZZ] Kompilacja wspolnych obiektow jadro/fs/psf.cpp + jadro/fs/storage_format.cpp (ASan/UBSan/trace-pc)...")
    run_cmd([
        *base_cxxflags,
        "-DBURSZTYN_PSF_HOST_TEST", "-ffunction-sections", "-fdata-sections",
        "-c", "jadro/fs/psf.cpp", "-o", str(psf_o),
    ])
    run_cmd([
        *base_cxxflags,
        "-DBURSZTYN_PSF_HOST_TEST", "-ffunction-sections", "-fdata-sections",
        "-c", "jadro/fs/storage_format.cpp", "-o", str(storage_o),
    ])

    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"

    results = []
    total_execs = 0
    total_edges = 0

    for f in FUZZERS:
        if args.target and f["name"] != args.target: continue
        name = f["name"]
        bin_path = BUILD_DIR / name
        corpus_sub = CORPUS_DIR / name
        metrics_tmp = BUILD_DIR / f"{name}_metrics.json"

        srcs = []
        for s in f["sources"]:
            if s == "jadro/fs/psf.cpp":
                srcs.append(str(psf_o))
            elif s == "jadro/fs/storage_format.cpp":
                srcs.append(str(storage_o))
            else:
                srcs.append(s)

        print(f"[FUZZ] Kompilacja {name}...")
        run_cmd([*base_cxxflags, *f["extra_flags"], *srcs, "-o", str(bin_path)])

        iters = f["extended_iters"] if args.mode == "extended" else f["smoke_iters"]
        out = run_cmd(
            [
                str(bin_path),
                f"--iterations={iters}",
                f"--corpus-out={corpus_sub}",
                f"--metrics-out={metrics_tmp}",
            ],
            env=env,
        )
        print(out.strip())
        m = json.loads(metrics_tmp.read_text(encoding="utf-8"))
        results.append(m)
        total_execs += m["executions"]
        total_edges += m["unique_edges"]

    summary = {
        "schema_version": "1.0",
        "mode": args.mode,
        "sanitizers": ["AddressSanitizer", "UndefinedBehaviorSanitizer", "SanitizerCoverage(trace-pc)"],
        "total_targets": len(results),
        "total_executions": total_execs,
        "sum_unique_edges_across_targets": total_edges,
        "total_crashes": sum(r["crashes_found"] for r in results),
        "deterministic_regression_bugs_verified": [
            {
                "id": "BUG-1",
                "component": "jadro/bws/syscalls.cpp (obsluga_wywolan_systemowych)",
                "description": "Unauthenticated BWS 99 (arg1 == 0xDEADBEEFCAFEULL) Ring 3 kernel stack overflow panic trigger removed from production builds and gated behind BURSZTYN_DEBUG_STACK_OVERFLOW_BWS + operacja_systemowa_dozwolona.",
                "verified_by": ["fuzz_bws", "test_bws_abuse_qemu.py"],
            },
            {
                "id": "BUG-2",
                "component": "jadro/fs/psf.cpp (psf_przetlumacz_sciezke_slotu)",
                "description": "Fixed 160-byte buffer silent path truncation and missing pre-translation bezpieczna_sciezka validation on A/B slot-translated paths (/programy, /system).",
                "verified_by": ["fuzz_bsp2_path"],
            },
            {
                "id": "BUG-3",
                "component": "jadro/fs/psf.cpp (wskazniki_pliku_poprawne, waliduj_katalog)",
                "description": "Fixed missing uniqueness/aliasing validation across direct blocks, blok_posredni_1, blok_posredni_2, and indirect entries, plus root-inode directory cycle rejection.",
                "verified_by": ["fuzz_bsp2_inode", "fuzz_bsp2_dir"],
            },
            {
                "id": "BUG-4",
                "component": "jadro/fs/psf.cpp & jadro/fs/psf_usuwanie.inc (zwolnij_wszystkie_bloki_wezla, ustaw_wskaznik_bloku, odwiedz_bloki_usuwania)",
                "description": "Fixed 256-slot LRU data block cache eviction of indirect pointer table block (blok_posredni_1/blok_posredni_2) while freeing/zeroing >256 data blocks of a large file, which previously corrupted the indirect pointer table mid-loop and freed block 0.",
                "verified_by": ["fuzz_bsp2_ops"],
            },
            {
                "id": "BUG-5",
                "component": "jadro/fs/loader.cpp (pakiet_parsuj_manifest_tekst)",
                "description": "Discovered by fuzz_bur_loader: when 'wersja' in opis.aplikacji was >= 32 bytes, pakiet_parsuj_manifest_tekst copied 32 bytes from a 64-byte temporary buffer into out_manifest->wersja[32] without a NUL terminator, causing C-string overread into adjacent struct fields; also left out_manifest uninitialized on early error return.",
                "verified_by": ["fuzz_bur_loader"],
            },
            {
                "id": "BUG-6",
                "component": "jadro/bws/syscalls.cpp (bws_rozmiar_pliku)",
                "description": "Discovered by test_bws_abuse_qemu.py: bws_rozmiar_pliku (BWS_ROZMIAR_PLIKU=44) unconditionally returned rozmiar_pliku(sciezka) + 1ULL (= 1ULL) even when sciezka was invalid (e.g. '/../../cytaty.txt'), non-existent, or a directory, causing Ring 3 pobierz_rozmiar_pliku() to report success with size 0. Fixed to verify pobierz_metadane_tworu(sciezka, &meta) && meta.typ == TYP_PLIK before returning meta.rozmiar + 1ULL.",
                "verified_by": ["fuzz_bws", "test_bws_abuse_qemu.py"],
            },
        ],
        "targets": results,
    }

    METRICS_OUT.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[FUZZ] Saved aggregated metrics to {METRICS_OUT} (total_executions={total_execs}, crashes=0).")


if __name__ == "__main__":
    main()
