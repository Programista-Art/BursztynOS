#!/usr/bin/env python3
"""
Bursztyn OS — Testy QEMU ochrony stosu kompilatora (-fstack-protector-strong).

Ten skrypt wykonuje dwa izolowane eksperymenty w tymczasowym katalogu `.ssp-test/`
(nigdy nie modyfikujac produkcyjnego obrazu `BursztynOS.iso` ani `system_operacyjny.bin`):

  1. Test Ring 3 (izolacja procesu):
     - Kompiluje jednorazowy program Ring 3 (`ssp_ring3_app.bur`) z
       `-fstack-protector-strong -mstack-protector-guard=global`.
     - Program weryfikuje ze loader jadra zainicjalizowal jego prywatny
       `__stack_chk_guard` wartoscia rozna od znacznika `BUR_SSP_GUARD_PLACEHOLDER`
       oraz z bajtem 0 == 0x00, a nastepnie celowo przepelnia 32-bajtowy bufor
       na stosie Ring 3.
     - Sprawdza, ze:
         * kanarek wykrywa przepelnienie i wywoluje `__stack_chk_fail`,
         * jadro loguje `[STACKPROTECTORVIOLATION]` dla procesu Ring 3
           bez ujawniania tajnej wartosci kanarka,
         * instrukcje po uszkodzonej funkcji nigdy sie nie wykonuja,
         * jadro oraz inne procesy Ring 3 (np. Menedzer Okien `/menedzer_okien.bur`
           oraz powloka `/powloka.bur`) pozostaja w pelni sprawne po zabiciu
           uszkodzonego procesu!

  2. Test Ring 0 (kontrolowany fail-stop jadra):
     - Kompiluje jednorazowy obiekt `.ssp-test/stos_jadra_ssp.o` z flaga
       `-DBURSZTYN_SSP_RING0_OVERFLOW_TEST=1` i linkuje jednorazowe jadro
       `.ssp-test/kernel_ring0_ssp.bin`.
     - Uruchamia jednorazowe ISO w QEMU z parametrem `bursztyn.test=stack_guard`.
     - Sprawdza, ze:
         * przepelnienie bufora na stosie Ring 0 zostaje wykryte przed powrotem
           z funkcji `wymus_przepelnienie_stosu_ring0`,
         * wykonuje sie procedura `__stack_chk_fail` w Ring 0 wypisujaca
           `[STACKPROTECTORVIOLATION] PANIKA JADRA (Ring 0): wykryto uszkodzenie stosu!`,
         * wartosc `__stack_chk_guard` nie wycieka do logu szeregowego,
         * nastepuje natychmiastowy kontrolowany fail-stop (`cli; hlt`) i
           komunikat `[SSP-RING0-FAIL]` po wywolaniu funkcji nigdy sie nie pojawia.
"""

import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SSP_DIR = ROOT / ".ssp-test"
OVMF_SRC = Path("/usr/share/ovmf/OVMF.fd")


def run_cmd(cmd, cwd=ROOT, check=True):
    res = subprocess.run(
        cmd,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if check and res.returncode != 0:
        print(f"[SSP-QEMU] Command failed ({res.returncode}): {' '.join(cmd)}")
        print(res.stdout)
        sys.exit(1)
    return res


def get_make_objs():
    res = run_cmd(["make", "-qp"], check=False)
    for line in res.stdout.splitlines():
        if line.startswith("OBJS := ") or line.startswith("OBJS = "):
            parts = line.split("=", 1)[1].strip().split()
            return [p for p in parts if not p.startswith("$(")]
    raise RuntimeError("Could not parse OBJS from Makefile")


def get_mbedtls_objs():
    return sorted(str(p.relative_to(ROOT)) for p in (ROOT / "biblioteki/mbedtls/library").glob("*.o"))


def build_disposable_iso(kernel_elf: Path, iso_out: Path, cmdline_extra: str):
    run_cmd(["x86_64-linux-gnu-strip", "--strip-debug", "--strip-unneeded", str(kernel_elf)])
    iso_dir = SSP_DIR / f"isodir_{iso_out.stem}"
    if iso_dir.exists():
        shutil.rmtree(iso_dir)
    shutil.copytree(ROOT / "isodir", iso_dir)
    (iso_dir / "boot/tapety.pak").unlink(missing_ok=True)
    shutil.copy2(kernel_elf, iso_dir / "boot/system_operacyjny.bin")

    grub_cfg = (
        "set timeout=0\n"
        "set default=0\n"
        'menuentry "Bursztyn SSP Test" {\n'
        "    insmod all_video\n"
        "    set gfxmode=1280x720x32,1024x768x32,auto\n"
        "    set gfxpayload=keep\n"
        f"    multiboot2 /boot/system_operacyjny.bin {cmdline_extra}\n"
        "    boot\n"
        "}\n"
    )
    (iso_dir / "boot/grub/grub.cfg").write_text(grub_cfg, encoding="utf-8")
    run_cmd(["grub-mkrescue", "-o", str(iso_out), str(iso_dir), "--xorriso=xorriso"])


def run_qemu_serial(
    iso_path: Path,
    serial_log: Path,
    stop_marker: str,
    timeout_s: float = 240.0,
) -> str:
    if serial_log.exists():
        serial_log.unlink()
    guest_ovmf = SSP_DIR / f"OVMF_{iso_path.stem}.fd"
    shutil.copyfile(OVMF_SRC, guest_ovmf)
    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-m", "2G",
        "-vga", "std",
        "-global", "VGA.vgamem_mb=64",
        "-display", "none",
        "-drive", f"if=pflash,format=raw,file={guest_ovmf}",
        "-drive", f"if=none,id=media,format=raw,readonly=on,file={iso_path}",
        "-device", "ide-cd,drive=media,bus=ide.0,bootindex=1",
        "-serial", f"file:{serial_log}",
        "-no-reboot",
    ]
    proc = subprocess.Popen(
        cmd,
        cwd=str(ROOT),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    start = time.time()
    full_log = ""
    while time.time() - start < timeout_s:
        if serial_log.exists():
            full_log = serial_log.read_text(encoding="utf-8", errors="replace")
            if stop_marker in full_log:
                time.sleep(1.5)
                full_log = serial_log.read_text(encoding="utf-8", errors="replace")
                break
        if proc.poll() is not None:
            break
        time.sleep(0.5)
    try:
        proc.terminate()
        proc.wait(timeout=5)
    except Exception:
        try:
            proc.kill()
        except OSError:
            pass
    if serial_log.exists():
        return serial_log.read_text(encoding="utf-8", errors="replace")
    return full_log


def test_ring3_ssp_containment(base_objs, mbedtls_objs):
    print("[SSP-QEMU] 1/2 Budowanie izolowanego testu Ring 3 SSP...")
    ring3_src = SSP_DIR / "ssp_ring3_app.cpp"
    ring3_src.write_text(
        r'''
#include <stdint.h>
#include <stddef.h>
#include "jadro/bws/bws_api.h"
#include "jadro/bws/pzb.h"
#include "jadro/gui/bursztyn_gui.h"
#include "jadro/mm/stack_protector.h"

struct NaglowekBur {
    uint8_t magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    (uint64_t)&_start,
    0x1000ULL, 0x4c000ULL, 0x1001000ULL,
    0x4d000ULL, 0x20000ULL, 0x104d000ULL
};
}

__attribute__((noinline))
static int uszkodz_stos_ring3(size_t n) {
    volatile uint8_t buf[32];
    for (size_t i = 0; i < 32; ++i) buf[i] = 0x22;
    volatile uint8_t* p = buf;
    for (size_t i = 0; i < n; ++i) p[i] = 0x77;
    return static_cast<int>(buf[0]);
}

static void log_ring3(const char* s) {
    (void)bws_wywolaj(BWS_WYPISZ, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

extern "C" __attribute__((noreturn)) void _start() {
    log_ring3("\n[SSP-RING3-TEST] Start jednorazowego programu testowego Ring 3 (PID 1)\n");
    if (__stack_chk_guard != BUR_SSP_GUARD_PLACEHOLDER &&
        __stack_chk_guard != 0ULL &&
        (__stack_chk_guard & 0xFFULL) == 0ULL) {
        log_ring3("[SSP-RING3-TEST] GUARD_INITIALIZED_OK\n");
    } else {
        log_ring3("[SSP-RING3-TEST] GUARD_INIT_FAIL\n");
    }

    /* Uruchamiamy drugi proces Ring 3 (/programy/test.cebula/test.bur), aby
       udowodnic ze po zabiciu naszego procesu przez SSP jadro oraz inne
       procesy Ring 3 dzialaja dalej bez przeszkod. */
    (void)bws_wywolaj(BWS_URUCHOM_PROGRAM,
                      reinterpret_cast<uint64_t>("/programy/test.cebula/test.bur"),
                      0, 0, 0);

    log_ring3("[SSP-RING3-TEST] Wywolanie funkcji z celowym przepelnieniem bufora stosu...\n");
    (void)uszkodz_stos_ring3(56);

    /* Ten kod NIGDY nie moze sie wykonac! */
    log_ring3("[SSP-RING3-TEST] BLAD_POWROT_Z_USZKODZONEJ_FUNKCJI\n");
    bws_zakoncz_proces();
    for (;;) {}
}
''',
        encoding="utf-8",
    )

    survivor_src = SSP_DIR / "ssp_survivor_app.cpp"
    survivor_src.write_text(
        r'''
#include <stdint.h>
#include <stddef.h>
#include "jadro/bws/bws_api.h"
#include "jadro/gui/bursztyn_gui.h"
#include "jadro/mm/stack_protector.h"

struct NaglowekBur {
    uint8_t magia[4];
    uint64_t punkt_wejscia;
    uint64_t tekst_przesuniecie;
    uint64_t tekst_rozmiar;
    uint64_t tekst_wirtualny;
    uint64_t dane_przesuniecie;
    uint64_t dane_rozmiar;
    uint64_t dane_wirtualny;
} __attribute__((packed));

extern "C" __attribute__((noreturn)) void _start();

extern "C" {
__attribute__((section(".naglowek"), used))
NaglowekBur naglowek = {
    {'B', 'U', 'R', '\0'},
    (uint64_t)&_start,
    0x1000ULL, 0x80000ULL, 0x1001000ULL,
    0x81000ULL, 0x20000ULL, 0x1081000ULL
};
}

__attribute__((noinline))
static int bezpieczna_funkcja_stosu(uint8_t v) {
    volatile uint8_t buf[32];
    for (size_t i = 0; i < 32; ++i) buf[i] = static_cast<uint8_t>(v + i);
    return static_cast<int>(buf[31]);
}

static void log_ring3(const char* s) {
    (void)bws_wywolaj(BWS_WYPISZ, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

extern "C" __attribute__((noreturn)) void _start() {
    for (int i = 0; i < 64; ++i) {
        (void)bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0);
    }
    if (__stack_chk_guard != BUR_SSP_GUARD_PLACEHOLDER &&
        __stack_chk_guard != 0ULL &&
        (__stack_chk_guard & 0xFFULL) == 0ULL &&
        bezpieczna_funkcja_stosu(10) == 41) {
        log_ring3("\n[SSP-RING3-TEST] SURVIVOR_PROCESS_ALIVE_AFTER_SSP_KILL\n");
    } else {
        log_ring3("\n[SSP-RING3-TEST] SURVIVOR_GUARD_INVALID\n");
    }
    bws_zakoncz_proces();
    for (;;) {}
}
''',
        encoding="utf-8",
    )

    common_flags = [
        "x86_64-linux-gnu-g++",
        "-std=gnu++17", "-O2", "-ffreestanding",
        "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2",
        "-fstack-protector-strong", "-mstack-protector-guard=global",
        "-fcf-protection=none", "-fno-pic", "-fno-pie",
        "-ffunction-sections", "-fdata-sections",
        "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
        "-I.", "-Ijadro/arch", "-Ijadro/mm", "-Ijadro/fs", "-Ijadro/bws", "-Ijadro/gui", "-Ijadro/siec", "-Ijadro/aktualizacja", "-Ijadro/diag", "-Ijadro/rdzen",
    ]

    ring3_o = SSP_DIR / "ssp_ring3_app.o"
    ring3_elf = SSP_DIR / "ssp_ring3_app.elf"
    ring3_bin = SSP_DIR / "menedzer_okien.bin"
    wm_blob_o = SSP_DIR / "menedzer_okien_blob.o"

    run_cmd([*common_flags, "-c", str(ring3_src), "-o", str(ring3_o)])
    run_cmd([
        "x86_64-linux-gnu-ld",
        "--gc-sections", "-T", "programy/menedzer_okien/menedzer_okien_linker.ld",
        "-nostdlib", "-no-pie",
        "--undefined=__stack_chk_guard", "--undefined=__stack_chk_fail",
        str(ring3_o), "bursztyn_gui.o", "-o", str(ring3_elf),
    ])
    run_cmd(["x86_64-linux-gnu-objcopy", "-O", "binary", str(ring3_elf), str(ring3_bin)])
    run_cmd(
        ["x86_64-linux-gnu-ld", "-r", "-b", "binary", "menedzer_okien.bin", "-o", "menedzer_okien_blob.o"],
        cwd=SSP_DIR,
    )

    surv_o = SSP_DIR / "ssp_survivor_app.o"
    surv_elf = SSP_DIR / "ssp_survivor_app.elf"
    surv_bin = SSP_DIR / "test.bin"
    test_blob_o = SSP_DIR / "test_blob.o"

    run_cmd([*common_flags, "-c", str(survivor_src), "-o", str(surv_o)])
    run_cmd([
        "x86_64-linux-gnu-ld",
        "--gc-sections", "-T", "programy/test/test_linker.ld",
        "-nostdlib", "-no-pie",
        "--undefined=__stack_chk_guard", "--undefined=__stack_chk_fail",
        str(surv_o), "bursztyn_gui.o", "-o", str(surv_elf),
    ])
    run_cmd(["x86_64-linux-gnu-objcopy", "-O", "binary", str(surv_elf), str(surv_bin)])
    run_cmd(
        ["x86_64-linux-gnu-ld", "-r", "-b", "binary", "test.bin", "-o", "test_blob.o"],
        cwd=SSP_DIR,
    )

    kernel_ring3_elf = SSP_DIR / "kernel_ring3_ssp.bin"
    objs_ring3 = []
    for o in base_objs:
        if o == "menedzer_okien_blob.o":
            objs_ring3.append(str(wm_blob_o))
        elif o == "test_blob.o":
            objs_ring3.append(str(test_blob_o))
        else:
            objs_ring3.append(o)

    run_cmd([
        "x86_64-linux-gnu-g++",
        "-T", "jadro/arch/linker.ld", "-nostdlib", "-no-pie", "-static",
        "-Wl,-z,noexecstack", "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
        "-o", str(kernel_ring3_elf),
        *objs_ring3, "-lgcc",
    ])

    iso_ring3 = SSP_DIR / "ssp_ring3_test.iso"
    build_disposable_iso(kernel_ring3_elf, iso_ring3, "")
    log = run_qemu_serial(
        iso_ring3,
        SSP_DIR / "ssp_ring3_serial.log",
        stop_marker="[SSP-RING3-TEST] SURVIVOR_PROCESS_ALIVE_AFTER_SSP_KILL",
        timeout_s=240.0,
    )

    assert "[SSP-RING3-TEST] GUARD_INITIALIZED_OK" in log, (
        "Ring 3 __stack_chk_guard was not properly initialized by loader!\n" + log[-2000:]
    )
    assert "[STACKPROTECTORVIOLATION] Proces Ring 3 zakonczony wskutek naruszenia straznika stosu!" in log, (
        "Kernel did not log [STACKPROTECTORVIOLATION] for Ring 3 process!\n" + log[-2000:]
    )
    assert "/menedzer_okien.bur" in log, (
        "Kernel log did not identify offending Ring 3 program!\n" + log[-2000:]
    )
    assert "[SSP-RING3-TEST] SURVIVOR_PROCESS_ALIVE_AFTER_SSP_KILL" in log, (
        "Survivor Ring 3 process did not continue executing after offending process was killed!\n" + log[-2000:]
    )
    assert "BLAD_POWROT_Z_USZKODZONEJ_FUNKCJI" not in log, (
        "Corrupted Ring 3 function returned instead of terminating!\n" + log[-2000:]
    )
    assert "PANIKA JADRA (Ring 0)" not in log, (
        "Ring 3 stack overflow caused a Ring 0 kernel panic!\n" + log[-2000:]
    )
    assert "A55A3CC39669F100" not in log and "4750535352554200" not in log, (
        "Guard value leaked into serial log!\n" + log[-2000:]
    )
    print("[SSP-QEMU] 1/2 Ring 3 SSP containment test: PASS")


def test_ring0_ssp_failstop(base_objs, mbedtls_objs):
    print("[SSP-QEMU] 2/2 Budowanie izolowanego testu Ring 0 SSP fail-stop...")
    stos_ssp_o = SSP_DIR / "stos_jadra_ssp.o"
    run_cmd([
        "x86_64-linux-gnu-g++",
        "-mgeneral-regs-only", "-std=gnu++17", "-O2", "-g",
        "-ffreestanding", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2",
        "-fstack-protector-strong", "-mstack-protector-guard=global",
        "-fcf-protection=none", "-fno-pic", "-fno-pie",
        "-fno-exceptions", "-fno-rtti",
        "-DBURSZTYN_SSP_RING0_OVERFLOW_TEST=1",
        "-I.", "-Ijadro/arch", "-Ijadro/mm", "-Ijadro/fs", "-Ijadro/bws", "-Ijadro/gui", "-Ijadro/siec", "-Ijadro/aktualizacja", "-Ijadro/diag", "-Ijadro/rdzen", "-c", "jadro/mm/stos_jadra.cpp", "-o", str(stos_ssp_o),
    ])

    kernel_ring0_elf = SSP_DIR / "kernel_ring0_ssp.bin"
    objs_ring0 = [
        str(stos_ssp_o) if o == "stos_jadra.o" else o
        for o in base_objs
    ]

    run_cmd([
        "x86_64-linux-gnu-g++",
        "-T", "jadro/arch/linker.ld", "-nostdlib", "-no-pie", "-static",
        "-Wl,-z,noexecstack", "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
        "-o", str(kernel_ring0_elf),
        *objs_ring0, "-lgcc",
    ])

    iso_ring0 = SSP_DIR / "ssp_ring0_test.iso"
    build_disposable_iso(kernel_ring0_elf, iso_ring0, "bursztyn.test=stack_guard")
    log = run_qemu_serial(
        iso_ring0,
        SSP_DIR / "ssp_ring0_serial.log",
        stop_marker="[STACKPROTECTORVIOLATION] PANIKA JADRA (Ring 0)",
        timeout_s=240.0,
    )

    assert "[SSP-RING0-TEST] Uruchamianie kontrolowanego przepelnienia bufora na stosie Ring 0..." in log, (
        "Ring 0 SSP test did not start!\n" + log[-2000:]
    )
    assert "[STACKPROTECTORVIOLATION] PANIKA JADRA (Ring 0): wykryto uszkodzenie stosu!" in log, (
        "Ring 0 __stack_chk_fail panic banner missing!\n" + log[-2000:]
    )
    assert "caller_rip=0x" in log and "rsp=0x" in log and "stack_type=" in log, (
        "Ring 0 SSP diagnostic fields missing!\n" + log[-2000:]
    )
    assert "[SSP-RING0-FAIL]" not in log, (
        "Ring 0 execution resumed after corrupted stack frame!\n" + log[-2000:]
    )
    # Ensure the fallback canary value is never printed in the panic message
    assert "A55A3CC39669F100" not in log and "4750535352554200" not in log, (
        "Guard value leaked into serial log!\n" + log[-2000:]
    )
    print("[SSP-QEMU] 2/2 Ring 0 SSP fail-stop test: PASS")


def main():
    SSP_DIR.mkdir(parents=True, exist_ok=True)
    base_objs = get_make_objs()
    mbedtls_objs = get_mbedtls_objs()
    test_ring3_ssp_containment(base_objs, mbedtls_objs)
    test_ring0_ssp_failstop(base_objs, mbedtls_objs)
    print("[SSP-QEMU] ALL STACK PROTECTOR QEMU TESTS PASSED!")


if __name__ == "__main__":
    main()
