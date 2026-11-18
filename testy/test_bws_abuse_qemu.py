#!/usr/bin/env python3
"""
Bursztyn OS — Izolowany test QEMU odpornosci BWS na zlosliwe wywolania z Ring 3
(testy/test_bws_abuse_qemu.py).

Buduje jednorazowy obraz ISO w `.bws-test/` (bez dotykania produkcyjnego
`BursztynOS.iso` ani `system_operacyjny.bin`) i uruchamia nieuprzywilejowany
proces Ring 3 (`PZB_UZYTKOWNIK`), ktory wykonuje serie zlosliwych wywolan BWS:
  1. Wskazniki NULL (`0x0`) do wywolan odczytujacych i zapisujacych pamiec.
  2. Wskazniki do przestrzeni jadra (`0x100000`, `0xFFFFFFFF80000000`, HHDM `0xFFFF800000000000`)
     oraz adresy niekanoniczne (`0x0001000000000000`).
  3. Wskazniki do niezmapowanej strony straznika stosu Ring 3 (`0x3FFFF000`)
     oraz zakresy przekraczajace granice zmapowanych stron.
  4. Proby zapisu przez jadro do segmentu tylko do odczytu `.text` (`0x1001000`)
     przez `BWS_CZAS_RTC`, `BWS_CZYTAJ_PLIK`, `BWS_GUI_ROZDZIELCZOSC`, `BWS_ARGUMENT_STARTOWY`.
  5. Przepelnienia arytmetyczne rozmiarow (`UINT64_MAX`, `BWS_ALOKUJ_STERTE(UINT64_MAX)`,
     `BWS_GUI_BLIT` z wymiarami przekraczajacymi limit).
  6. Proby nieautoryzowanych operacji uprzywilejowanych z `PZB_UZYTKOWNIK`:
     - `BWS_RESET (25)` i `BWS_SHUTDOWN (26)` -> musza zwrocic 0 i NIE zresetowac systemu,
     - `BWS_OTA_STATUS (84)`, `BWS_OTA_POLECENIE (85)`, `BWS_BSF_CONFIG (92)`,
       `BWS_PULPIT_GOTOWY (88)` -> musza zwrocic 0.
  7. Test regresyjny Bug #1:
     - `bws_wywolaj(99, 0xDEADBEEFCAFEULL, 0, 0, 0)` z Ring 3 -> musi zwrocic
       `UINT64_MAX` i NIE wywolac kontrolowanego przepelnienia stosu jadra!
  8. Testy traversalu sciezek (`"/../../etc/passwd"`, `"/.system/slot_a/../../tajne"`).
"""

import shutil
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BWS_DIR = ROOT / ".bws-test"
OVMF_SRC = Path("/usr/share/ovmf/OVMF.fd")


def run_cmd(cmd, cwd=ROOT):
    res = subprocess.run(
        cmd,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if res.returncode != 0:
        print(f"[BWS-QEMU] Command failed ({res.returncode}): {' '.join(cmd)}")
        print(res.stdout)
        sys.exit(1)
    return res


def get_make_objs():
    res = subprocess.run(
        ["make", "-qp"],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    for line in res.stdout.splitlines():
        if line.startswith("OBJS := ") or line.startswith("OBJS = "):
            parts = line.split("=", 1)[1].strip().split()
            return [p for p in parts if not p.startswith("$(")]
    raise RuntimeError("Could not parse OBJS from Makefile")


def main():
    BWS_DIR.mkdir(parents=True, exist_ok=True)
    base_objs = get_make_objs()

    print("[BWS-QEMU] Budowanie izolowanego testu naduzyc BWS w Ring 3...")
    wm_src = BWS_DIR / "bws_wm_launcher.cpp"
    wm_src.write_text(
        r'''
#include <stdint.h>
#include "jadro/bws/bws_api.h"
#include "jadro/gui/bursztyn_gui.h"

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
    0x1000ULL, 0x4e000ULL, 0x1001000ULL,
    0x4f000ULL, 0x20000ULL, 0x104f000ULL
};
}

extern "C" __attribute__((noreturn)) void _start() {
    (void)bws_wywolaj(BWS_URUCHOM_PROGRAM,
                      reinterpret_cast<uint64_t>("/programy/test.cebula/test.bur"),
                      0, 0, 0);
    for (;;) {
        (void)bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0);
    }
}
''',
        encoding="utf-8",
    )

    abuse_src = BWS_DIR / "bws_abuse_app.cpp"
    abuse_src.write_text(
        r'''
#include <stdint.h>
#include <stddef.h>
#include "jadro/bws/bws_api.h"
#include "jadro/bws/pzb.h"
#include "jadro/gui/bursztyn_gui.h"

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

static void log_s(const char* s) {
    (void)bws_wywolaj(BWS_WYPISZ, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

#define CHECK_BWS(cond, msg) \
    do { \
        if (!(cond)) { \
            log_s("\n[BWS-ABUSE-FAIL] " msg "\n"); \
            bws_zakoncz_proces(); \
            for (;;) {} \
        } \
    } while (0)

extern "C" __attribute__((noreturn)) void _start() {
    log_s("\n[BWS-ABUSE-QEMU] Start testow odpornosci BWS z nieuprzywilejowanego procesu Ring 3 (PZB_UZYTKOWNIK)\n");

    /* 1. Regresja Bug #1: BWS 99 z 0xDEADBEEFCAFEULL NIGDY nie moze wywolac paniki jadra! */
    const uint64_t r99 = bws_wywolaj(99ULL, 0xDEADBEEFCAFEULL, 0, 0, 0);
    CHECK_BWS(r99 == UINT64_MAX, "Bug #1 regression failed: BWS 99 did not return UINT64_MAX");
    log_s("[BWS-ABUSE-QEMU] 1/6 Regresja Bug #1 (BWS 99 + 0xDEADBEEFCAFEULL): OK\n");

    /* 2. Wskazniki NULL, jadro, HHDM, niekanoniczne oraz guard page stosu Ring 3 */
    const uint64_t bad_ptrs[] = {
        0x0ULL,
        0x100000ULL,               // Niska pamiec jadra
        0xFFFFFFFF80000000ULL,     // Wysoka pamiec kanoniczna
        0xFFFF800000100000ULL,     // HHDM jadra
        0x0001000000000000ULL,     // Adres niekanoniczny x86-64
        0x000000003FFFF000ULL,     // Niezmapowany adres w przestrzeni uzytkownika
        0x00007FFFEFFFF000ULL,     // Strona guard page ponizej stosu Ring 3 (RING3_STACK_TOP - 4096)
        0x00007FFFF0010000ULL,     // Niezmapowana strona powyzej 16-stronicowego stosu
    };
    for (size_t i = 0; i < sizeof(bad_ptrs) / sizeof(bad_ptrs[0]); ++i) {
        const uint64_t p = bad_ptrs[i];
        CHECK_BWS(bws_wywolaj(BWS_WYPISZ, p, 0, 0, 0) == 0, "BWS_WYPISZ accepted bad pointer");
        CHECK_BWS(bws_wywolaj(BWS_CZAS_RTC, p, 0, 0, 0) == 0, "BWS_CZAS_RTC accepted bad pointer");
        CHECK_BWS(bws_wywolaj(BWS_CZYTAJ_PLIK, reinterpret_cast<uint64_t>("/cytaty.txt"), p, 64, 0) == 0,
                  "BWS_CZYTAJ_PLIK accepted bad output pointer");
        CHECK_BWS(bws_wywolaj(BWS_GUI_POBIERZ_MYSZ, p, p, p, 0) == 0,
                  "BWS_GUI_POBIERZ_MYSZ accepted bad pointer");
        CHECK_BWS(bws_wywolaj(BWS_ZDARZENIE_POLL, p, 0, 0, 0) == 0,
                  "BWS_ZDARZENIE_POLL accepted bad pointer");
    }
    log_s("[BWS-ABUSE-QEMU] 2/6 Odrzucanie wskaznikow NULL/Jadro/HHDM/Niekanonicznych/GuardPage: OK\n");

    /* 3. Proba zapisu przez jadro do segmentu kodu tylko do odczytu (.text = 0x1001000) */
    const uint64_t ro_text_va = 0x1001000ULL;
    CHECK_BWS(bws_wywolaj(BWS_CZAS_RTC, ro_text_va, 0, 0, 0) == 0,
              "BWS_CZAS_RTC wrote into read-only .text segment");
    CHECK_BWS(bws_wywolaj(BWS_CZYTAJ_PLIK, reinterpret_cast<uint64_t>("/cytaty.txt"), ro_text_va, 32, 0) == 0,
              "BWS_CZYTAJ_PLIK wrote into read-only .text segment");
    CHECK_BWS(bws_wywolaj(BWS_GUI_ROZDZIELCZOSC, ro_text_va, ro_text_va + 4, 0, 0) == 0,
              "BWS_GUI_ROZDZIELCZOSC wrote into read-only .text segment");
    log_s("[BWS-ABUSE-QEMU] 3/6 Ochrona W^X przed zapisem jadra do .text procesu Ring 3: OK\n");

    /* 4. Przepelnienia liczb calkowitych w rozmiarach alokacji i buforow */
    CHECK_BWS(bws_wywolaj(BWS_ALOKUJ_STERTE, UINT64_MAX, 0, 0, 0) == 0,
              "BWS_ALOKUJ_STERTE(UINT64_MAX) did not fail");
    CHECK_BWS(bws_wywolaj(BWS_ALOKUJ_STERTE, 0x80000000ULL, 0, 0, 0) == 0,
              "BWS_ALOKUJ_STERTE(2GiB) did not fail");
    CHECK_BWS(bws_wywolaj(BWS_GUI_BLIT, 0, (4096ULL << 32) | 4096ULL, 0x1081000ULL, 0) == 0,
              "BWS_GUI_BLIT(4096x4096) did not fail");
    log_s("[BWS-ABUSE-QEMU] 4/6 Odrzucanie przepelnien rozmiarow (sterta/blit): OK\n");

    /* 5. Egzekwowanie PZB i PRAWO_* dla operacji systemowych z PZB_UZYTKOWNIK */
    uint8_t process_list[2048] = {};
    CHECK_BWS(bws_wywolaj(BWS_PROCESY_LISTA, reinterpret_cast<uint64_t>(process_list), 1, 0, 0) == 0,
              "Unprivileged process enumerated privileged process metadata");
    CHECK_BWS(bws_wywolaj(BWS_PROCES_ZABIJ, 1, 0, 0, 0) == 0,
              "Unprivileged process killed the system manager");
    CHECK_BWS(bws_wywolaj(BWS_RESET, 0, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_RESET");
    CHECK_BWS(bws_wywolaj(BWS_SHUTDOWN, 0, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_SHUTDOWN");
    CHECK_BWS(bws_wywolaj(BWS_PULPIT_GOTOWY, 0, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_PULPIT_GOTOWY");
    CHECK_BWS(bws_wywolaj(BWS_OTA_STATUS, 0x1081000ULL, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_OTA_STATUS");
    CHECK_BWS(bws_wywolaj(BWS_OTA_POLECENIE, 1, 0, 0, 0) != 0,
              "Unprivileged process allowed to call BWS_OTA_POLECENIE (expected BWS_OTA_BLAD_BRAK_UPRAWNIEN != 0)");
    CHECK_BWS(bws_wywolaj(BWS_BSF_CONFIG, 0x1081000ULL, 0, 0, 0) == 0,
              "Unprivileged process allowed to call BWS_BSF_CONFIG");
    log_s("[BWS-ABUSE-QEMU] 5/6 Egzekwowanie polityki PZB/PRAWO_* dla wywolan uprzywilejowanych: OK\n");

    /* 6. Odrzucanie traversalu sciezek w wywolaniach plikowych */
    CHECK_BWS(bws_wywolaj(BWS_ROZMIAR_PLIKU, reinterpret_cast<uint64_t>("/../../cytaty.txt"), 0, 0, 0) == 0,
              "Path traversal /../../ accepted");
    CHECK_BWS(bws_wywolaj(BWS_ROZMIAR_PLIKU, reinterpret_cast<uint64_t>("/.system/slot_a/../../cytaty.txt"), 0, 0, 0) == 0,
              "Slot path traversal accepted");
    log_s("[BWS-ABUSE-QEMU] 6/6 Odrzucanie traversalu sciezek BWS: OK\n");

    log_s("[BWS-ABUSE-QEMU] ALL_CHECKS_PASSED\n");
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

    wm_o = BWS_DIR / "bws_wm.o"
    wm_elf = BWS_DIR / "bws_wm.elf"
    wm_bin = BWS_DIR / "menedzer_okien.bin"
    wm_blob_o = BWS_DIR / "menedzer_okien_blob.o"

    run_cmd([*common_flags, "-c", str(wm_src), "-o", str(wm_o)])
    run_cmd([
        "x86_64-linux-gnu-ld", "--gc-sections", "-T", "programy/menedzer_okien/menedzer_okien_linker.ld",
        "-nostdlib", "-no-pie",
        "--undefined=__stack_chk_guard", "--undefined=__stack_chk_fail",
        str(wm_o), "bursztyn_gui.o", "-o", str(wm_elf),
    ])
    run_cmd(["x86_64-linux-gnu-objcopy", "-O", "binary", str(wm_elf), str(wm_bin)])
    # Jadro oczekuje blobow SKOMPRESOWANYCH (symbole _binary_*_bin_bz_*), jak w produkcyjnym Makefile.
    run_cmd(["python3", str(ROOT / "narzedzia" / "kompresuj_blob.py"), str(BWS_DIR / "menedzer_okien.bin"), str(BWS_DIR / "menedzer_okien.bin.bz")])
    run_cmd(
        ["x86_64-linux-gnu-ld", "-r", "-b", "binary", "menedzer_okien.bin.bz", "-o", "menedzer_okien_blob.o"],
        cwd=BWS_DIR,
    )

    ab_o = BWS_DIR / "bws_abuse.o"
    ab_elf = BWS_DIR / "bws_abuse.elf"
    ab_bin = BWS_DIR / "test.bin"
    test_blob_o = BWS_DIR / "test_blob.o"

    run_cmd([*common_flags, "-c", str(abuse_src), "-o", str(ab_o)])
    run_cmd([
        "x86_64-linux-gnu-ld", "--gc-sections", "-T", "programy/test/test_linker.ld",
        "-nostdlib", "-no-pie",
        "--undefined=__stack_chk_guard", "--undefined=__stack_chk_fail",
        str(ab_o), "bursztyn_gui.o", "-o", str(ab_elf),
    ])
    run_cmd(["x86_64-linux-gnu-objcopy", "-O", "binary", str(ab_elf), str(ab_bin)])
    # Jadro oczekuje blobow SKOMPRESOWANYCH (symbole _binary_*_bin_bz_*), jak w produkcyjnym Makefile.
    run_cmd(["python3", str(ROOT / "narzedzia" / "kompresuj_blob.py"), str(BWS_DIR / "test.bin"), str(BWS_DIR / "test.bin.bz")])
    run_cmd(
        ["x86_64-linux-gnu-ld", "-r", "-b", "binary", "test.bin.bz", "-o", "test_blob.o"],
        cwd=BWS_DIR,
    )

    kernel_elf = BWS_DIR / "kernel_bws_abuse.bin"
    objs = []
    if "fpu_probe_blob.o" not in base_objs:   # wymagany przez kernel.o (sonda FPU)
        base_objs = [*base_objs, "fpu_probe_blob.o"]
    for o in base_objs:
        if o == "menedzer_okien_blob.o":
            objs.append(str(wm_blob_o))
        elif o == "test_blob.o":
            objs.append(str(test_blob_o))
        else:
            objs.append(o)

    run_cmd([
        "x86_64-linux-gnu-g++",
        "-T", "jadro/arch/linker.ld", "-nostdlib", "-no-pie", "-static",
        "-Wl,-z,noexecstack", "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
        "-o", str(kernel_elf),
        *objs, "-lgcc",
    ])
    run_cmd(["x86_64-linux-gnu-strip", "--strip-debug", "--strip-unneeded", str(kernel_elf)])

    iso_dir = BWS_DIR / "isodir_bws"
    if iso_dir.exists():
        shutil.rmtree(iso_dir)
    shutil.copytree(ROOT / "isodir", iso_dir)
    (iso_dir / "boot/tapety.pak").unlink(missing_ok=True)
    shutil.copy2(kernel_elf, iso_dir / "boot/system_operacyjny.bin")
    (iso_dir / "boot/grub/grub.cfg").write_text(
        "set timeout=0\n"
        "set default=0\n"
        'menuentry "Bursztyn BWS Abuse Test" {\n'
        "    insmod all_video\n"
        "    set gfxmode=1920x1080x32,1280x720x32,1024x768x32,auto\n"
        "    set gfxpayload=keep\n"
        "    multiboot2 /boot/system_operacyjny.bin\n"
        "    boot\n"
        "}\n",
        encoding="utf-8",
    )
    iso_out = BWS_DIR / "bws_abuse_test.iso"
    run_cmd(["grub-mkrescue", "-o", str(iso_out), str(iso_dir), "--xorriso=xorriso"])

    serial_log = BWS_DIR / "bws_abuse_serial.log"
    if serial_log.exists():
        serial_log.unlink()
    guest_ovmf = BWS_DIR / "OVMF_bws.fd"
    shutil.copyfile(OVMF_SRC, guest_ovmf)

    print("[BWS-QEMU] Uruchamianie testu naduzyc BWS w QEMU...")
    proc = subprocess.Popen(
        [
            "qemu-system-x86_64",
            "-machine", "pc,hpet=on",
            "-cpu", "max",
            "-accel", "tcg,thread=single",
            "-m", "2G",
            "-vga", "std",
            "-global", "VGA.vgamem_mb=64",
            "-display", "none",
            "-drive", f"if=pflash,format=raw,file={guest_ovmf}",
            "-drive", f"if=none,id=media,format=raw,readonly=on,file={iso_out}",
            "-device", "ide-cd,drive=media,bus=ide.0,bootindex=1",
            "-serial", f"file:{serial_log}",
            "-no-reboot",
        ],
        cwd=str(ROOT),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    start = time.time()
    full_log = ""
    timeout = float(os.environ.get("BURSZTYN_QEMU_TIMEOUT", "240"))
    while time.time() - start < timeout:
        if serial_log.exists():
            full_log = serial_log.read_text(encoding="utf-8", errors="replace")
            if "[BWS-ABUSE-QEMU] ALL_CHECKS_PASSED" in full_log or "[BWS-ABUSE-FAIL]" in full_log:
                time.sleep(1.0)
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

    assert "[BWS-ABUSE-FAIL]" not in full_log, "BWS abuse test reported failure:\n" + full_log[-2000:]
    assert "[BWS-ABUSE-QEMU] ALL_CHECKS_PASSED" in full_log, (
        "BWS abuse test did not complete all checks:\n" + full_log[-2000:]
    )
    assert "PANIKA JADRA" not in full_log and "KRYTYCZNE przekroczenie stosu jadra" not in full_log, (
        "BWS abuse test triggered a kernel panic!\n" + full_log[-2000:]
    )
    print("[BWS-QEMU] PASS: All 6 categories of BWS Ring 3 abuse tests (including Bug #1 regression) passed in QEMU!")


if __name__ == "__main__":
    main()
