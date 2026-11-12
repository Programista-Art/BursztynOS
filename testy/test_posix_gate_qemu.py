#!/usr/bin/env python3
"""
Bursztyn OS - test QEMU bramki ABI SysV (`int 0x80`).

Cel: udowodnic w prawdziwym Ring 3, ze:

  1. bramka jest dostepna z DPL=3 (wywolanie nie konczy sie #GP),
  2. numer jest czytany z RAX, a argumenty z RDI/RSI/RDX/R10/R8/R9,
  3. wynik wraca w RAX i jest to -errno z zakresu -1..-4095 przy bledzie,
  4. nieznany numer zwraca -ENOSYS (a nie 0 i nie panike jadra),
  5. `write(1, ...)` trafia na terminal jadra,
  6. bramka nie psuje BWS: BWS dziala przed i po `int 0x80`.

Metoda jest ta sama co w test_bws_abuse_qemu.py: podmieniamy jeden program
testowy w obrazie ISO i uruchamiamy QEMU z serialem do pliku. Produkcyjny
BursztynOS.iso nie jest dotykany.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
POSIX_DIR = ROOT / ".posix-test"
OVMF_SRC = Path("/usr/share/ovmf/OVMF.fd")

# Linux x86-64 ABI.
SYS_WRITE = 1
SYS_GETPID = 39
SYS_UNAME = 63
EBADF = 9
ENOSYS = 38

APP = r'''
#include <stdint.h>
#include <stddef.h>
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
    0x1000ULL, 0x80000ULL, 0x1001000ULL,
    0x81000ULL, 0x20000ULL, 0x1081000ULL
};
}

static void log_s(const char* s) {
    (void)bws_wywolaj(BWS_WYPISZ, reinterpret_cast<uint64_t>(s), 0, 0, 0);
}

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            log_s("\n[POSIX-QEMU-FAIL] " msg "\n"); \
            bws_zakoncz_proces(); \
            for (;;) {} \
        } \
    } while (0)

enum : int64_t {
    SYS_WRITE = 1,
    SYS_GETPID = 39,
    SYS_MMAP = 9,
    SYS_MUNMAP = 11,
    SYS_UNAME = 63,
    SYS_ARCH_PRCTL = 158,
    EBADF = 9,
    ENOSYS = 38,
    PROT_READ = 1,
    PROT_WRITE = 2,
    PROT_EXEC = 4,
    MAP_PRIVATE = 0x02,
    MAP_ANONYMOUS = 0x20,
    ARCH_SET_GS = 0x1001,
    ARCH_SET_FS = 0x1002,
    ARCH_GET_FS = 0x1003
};

/*
 * Wywolanie bramki SysV: numer w RAX, argumenty RDI/RSI/RDX/R10/R8/R9.
 * RCX i R11 deklarowane jako clobbered, bo tak robi kazdy kompilator;
 * `int 0x80` ich nie niszczy, ale kontrakt ma byc identyczny z Linuxem.
 */
static int64_t sys(int64_t nr, int64_t a1 = 0, int64_t a2 = 0, int64_t a3 = 0,
                   int64_t a4 = 0, int64_t a5 = 0, int64_t a6 = 0) {
    int64_t wynik;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    register int64_t r9  __asm__("r9")  = a6;
    __asm__ volatile(
        "int $0x80"
        : "=a"(wynik)
        : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return wynik;
}

extern "C" __attribute__((noreturn)) void _start() {
    log_s("\n[POSIX-QEMU] Start testu bramki ABI SysV (int 0x80)\n");

    /* 0. BWS dziala przed bramka SysV. */
    const uint64_t tick_przed = bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0);
    CHECK(tick_przed != 0, "BWS 60 nie dziala przed int 0x80");
    log_s("[POSIX-QEMU] 0/6 BWS dziala przed bramka SysV: OK\n");

    /* 1. Bramka dostepna z DPL=3 - wywolanie nie konczy sie #GP. */
    const int64_t pid = sys(SYS_GETPID);
    CHECK(pid > 0 && pid < 64, "getpid nie zwrocil sensownego PID");
    log_s("[POSIX-QEMU] 1/6 Bramka dostepna z Ring 3 (getpid): OK\n");

    /* 2. Nieznany numer -> -ENOSYS, nigdy 0 i nigdy panika. */
    const int64_t nieznany = sys(9999);
    CHECK(nieznany == -ENOSYS, "nieznany numer nie zwrocil -ENOSYS");
    log_s("[POSIX-QEMU] 2/6 Nieznany numer -> -ENOSYS: OK\n");

    /* 3. Blad z zakresu -1..-4095. */
    CHECK(nieznany <= -1 && nieznany >= -4095, "-ENOSYS poza zakresem errno");
    const int64_t zle_uname = sys(SYS_UNAME, 0x10 /* niezmapowany Ring 3 */);
    CHECK(zle_uname <= -1 && zle_uname >= -4095, "uname(0x10) nie zwrocil errno");
    log_s("[POSIX-QEMU] 3/6 Bledy w zakresie -1..-4095: OK\n");

    /* 4. write(1, ...) trafia na terminal jadra i zwraca liczbe bajtow. */
    static const char tekst[] = "[POSIX-QEMU] 4/6 write(1) z Ring 3: OK\n";
    const int64_t zapisane = sys(SYS_WRITE, 1,
                                 reinterpret_cast<int64_t>(tekst),
                                 static_cast<int64_t>(sizeof(tekst) - 1));
    CHECK(zapisane == static_cast<int64_t>(sizeof(tekst) - 1),
          "write(1) nie zwrocil dlugosci");

    /* 5. Zly deskryptor -> EBADF, nie sukces. */
    const int64_t zly_fd = sys(SYS_WRITE, 99,
                               reinterpret_cast<int64_t>(tekst), 4);
    CHECK(zly_fd == -9, "write(99) nie zwrocil -EBADF");
    log_s("[POSIX-QEMU] 5/6 Zly deskryptor -> -EBADF: OK\n");

    /* 6. BWS dziala po wywolaniach SysV. */
    const uint64_t tick_po = bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0);
    CHECK(tick_po >= tick_przed, "BWS 60 nie dziala po int 0x80");
    log_s("[POSIX-QEMU] 6/6 BWS dziala po bramce SysV: OK\n");

    /*
     * 7. TLS per proces: arch_prctl(ARCH_SET_FS) + odczyt przez %fs.
     *
     * To jest wlasciwy test izolacji errno: ustawiamy baze TLS na wlasny
     * bufor, zapisujemy przez %fs, czytamy z powrotem. Bez przelaczania
     * IA32_FS_BASE przy zmianie kontekstu ten test przechodzilby tylko
     * przypadkiem (na wartosci pozostawionej przez poprzedni proces).
     */
    {
        static struct { uint64_t errno_cell; uint64_t zapas[7]; } tls{};
        const int64_t set = sys(SYS_ARCH_PRCTL, ARCH_SET_FS,
                                reinterpret_cast<int64_t>(&tls));
        CHECK(set == 0, "arch_prctl(ARCH_SET_FS) nie powiodl sie");

        /* Zapis i odczyt przez %fs - dokladnie tak dziala errno w libc. */
        const uint64_t znacznik = 0x5BA5BA5BA5ULL;
        __asm__ volatile("movq %0, %%fs:0" ::"r"(znacznik) : "memory");
        uint64_t odczyt = 0;
        __asm__ volatile("movq %%fs:0, %0" : "=r"(odczyt) :: "memory");
        CHECK(odczyt == znacznik, "%fs nie wskazuje ustawionego TLS");

        /* GET_FS zwraca te sama baze. */
        uint64_t zwrot = 0;
        const int64_t get = sys(SYS_ARCH_PRCTL, ARCH_GET_FS,
                                reinterpret_cast<int64_t>(&zwrot));
        CHECK(get == 0 && zwrot == reinterpret_cast<uint64_t>(&tls),
              "arch_prctl(ARCH_GET_FS) nie zwrocil ustawionej bazy");

        /* ARCH_SET_GS musi byc odrzucony: %gs nalezy do jadra. */
        const int64_t gs = sys(SYS_ARCH_PRCTL, ARCH_SET_GS, 0x100000);
        CHECK(gs < 0, "ARCH_SET_GS nie zostal odrzucony z Ring 3");
        log_s("[POSIX-QEMU] 7/8 TLS per proces (%fs) i odmowa ARCH_SET_GS: OK\n");
    }

    /*
     * 8. mmap anonimowy: mapowanie, zapis, odczyt, munmap.
     *
     * To sprawdza realna sciezke pamieci, na ktorej stoi malloc() kazdej
     * libc: pamiec musi byc zapisywalna, wyzerowana (brak wycieku danych
     * poprzedniego procesu) i zwalnialna.
     */
    {
        const int64_t mapa = sys(SYS_MMAP, 0, 3ULL * 4096ULL,
                                 PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(mapa > 0, "mmap anonimowy nie zwrocil adresu");

        uint8_t* const pamiec = reinterpret_cast<uint8_t*>(mapa);
        /* PMM zeruje ramki - strona nie moze zawierac cudzych danych. */
        bool wyzerowana = true;
        for (int i = 0; i < 4096; ++i) if (pamiec[i] != 0) { wyzerowana = false; break; }
        CHECK(wyzerowana, "mmap nie zwrocil wyzerowanej pamieci");

        /* Zapis i odczyt przez wszystkie trzy strony. */
        for (int strona = 0; strona < 3; ++strona) {
            pamiec[strona * 4096] = static_cast<uint8_t>(0xA0 + strona);
            pamiec[strona * 4096 + 4095] = static_cast<uint8_t>(0xB0 + strona);
        }
        for (int strona = 0; strona < 3; ++strona) {
            CHECK(pamiec[strona * 4096] == static_cast<uint8_t>(0xA0 + strona),
                  "mmap: zapis/odczyt poczatku strony");
            CHECK(pamiec[strona * 4096 + 4095] == static_cast<uint8_t>(0xB0 + strona),
                  "mmap: zapis/odczyt konca strony");
        }

        /* W^X: PROT_WRITE|PROT_EXEC musi byc odrzucone. */
        const int64_t wx = sys(SYS_MMAP, 0, 4096,
                               PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(wx < 0, "mmap z PROT_WRITE|PROT_EXEC nie zostal odrzucony");

        /* Nieznany typ mapowania -> EINVAL. */
        const int64_t zly_typ = sys(SYS_MMAP, 0, 4096, PROT_READ,
                                    MAP_ANONYMOUS, -1, 0);
        CHECK(zly_typ < 0, "mmap bez SHARED/PRIVATE nie zostal odrzucony");

        /* munmap zwalnia region. */
        const int64_t unm = sys(SYS_MUNMAP, mapa, 3ULL * 4096ULL);
        CHECK(unm == 0, "munmap nie powiodl sie");
        /* Drugi munmap tego samego zakresu musi zawiesc. */
        const int64_t unm2 = sys(SYS_MUNMAP, mapa, 3ULL * 4096ULL);
        CHECK(unm2 < 0, "podwojny munmap nie zostal odrzucony");
        log_s("[POSIX-QEMU] 8/8 mmap anonimowy, W^X i munmap: OK\n");
    }

    log_s("[POSIX-QEMU] ALL_CHECKS_PASSED: bramka SysV dziala w Ring 3\n");
    bws_zakoncz_proces();
    for (;;) {}
}
'''


WM_LAUNCHER = r'''
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

/*
 * Minimalny "menedzer okien": jedyne zadanie to uruchomic program testowy
 * z /programy/test.cebula/test.bur, zeby kernal mial kogo sklonowac do
 * Ring 3. Nie rysuje pulpitu - ten test sprawdza wylacznie bramke SysV.
 */
extern "C" __attribute__((noreturn)) void _start() {
    (void)bws_wywolaj(BWS_URUCHOM_PROGRAM,
                      reinterpret_cast<uint64_t>("/programy/test.cebula/test.bur"),
                      0, 0, 0);
    for (;;) {
        (void)bws_wywolaj(BWS_CZAS_MONOTONICZNY_NS, 0, 0, 0, 0);
    }
}
'''


def run_cmd(cmd, cwd=ROOT):
    res = subprocess.run([str(c) for c in cmd], cwd=str(cwd), stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    if res.returncode != 0:
        print(f"[POSIX-QEMU] Command failed ({res.returncode}): "
              f"{' '.join(str(c) for c in cmd)}")
        print(res.stdout)
        sys.exit(1)
    return res


def get_make_objs():
    res = subprocess.run(["make", "-qp"], cwd=str(ROOT), stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    for line in res.stdout.splitlines():
        if line.startswith("OBJS := ") or line.startswith("OBJS = "):
            parts = line.split("=", 1)[1].strip().split()
            return [p for p in parts if not p.startswith("$(")]
    raise RuntimeError("Could not parse OBJS from Makefile")


def build_iso() -> Path:
    POSIX_DIR.mkdir(parents=True, exist_ok=True)
    base_objs = get_make_objs()

    src = POSIX_DIR / "posix_gate_app.cpp"
    src.write_text(APP, encoding="utf-8")

    common_flags = [
        "x86_64-linux-gnu-g++",
        "-std=gnu++17", "-O2", "-ffreestanding",
        "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2",
        "-fstack-protector-strong", "-mstack-protector-guard=global",
        "-fcf-protection=none", "-fno-pic", "-fno-pie",
        "-ffunction-sections", "-fdata-sections",
        "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
        "-I.", "-Ijadro/arch", "-Ijadro/mm", "-Ijadro/fs", "-Ijadro/bws",
        "-Ijadro/gui", "-Ijadro/siec", "-Ijadro/aktualizacja", "-Ijadro/diag",
        "-Ijadro/posix", "-Ijadro/rdzen",
    ]

    obj = POSIX_DIR / "posix_gate_app.o"
    elf = POSIX_DIR / "posix_gate_app.elf"
    bur = POSIX_DIR / "test.bin"
    test_blob = POSIX_DIR / "test_blob.o"

    run_cmd([*common_flags, "-c", src, "-o", obj])
    run_cmd([
        "x86_64-linux-gnu-ld", "--gc-sections",
        "-T", "programy/test/test_linker.ld",
        "-nostdlib", "-no-pie",
        "--undefined=__stack_chk_guard", "--undefined=__stack_chk_fail",
        obj, "bursztyn_gui.o", "-o", elf,
    ])
    run_cmd(["x86_64-linux-gnu-objcopy", "-O", "binary", elf, bur])
    run_cmd(["x86_64-linux-gnu-ld", "-r", "-b", "binary", "test.bin", "-o", "test_blob.o"],
            cwd=POSIX_DIR)

    # Launcher WM: podmienia menedzer_okien_blob.o, zeby uruchomic test.bur.
    wm_src = POSIX_DIR / "posix_wm_launcher.cpp"
    wm_src.write_text(WM_LAUNCHER, encoding="utf-8")
    wm_obj = POSIX_DIR / "posix_wm.o"
    wm_elf = POSIX_DIR / "posix_wm.elf"
    wm_bin = POSIX_DIR / "menedzer_okien.bin"
    wm_blob = POSIX_DIR / "menedzer_okien_blob.o"
    run_cmd([*common_flags, "-c", wm_src, "-o", wm_obj])
    run_cmd([
        "x86_64-linux-gnu-ld", "--gc-sections",
        "-T", "programy/menedzer_okien/menedzer_okien_linker.ld",
        "-nostdlib", "-no-pie",
        "--undefined=__stack_chk_guard", "--undefined=__stack_chk_fail",
        wm_obj, "bursztyn_gui.o", "-o", wm_elf,
    ])
    run_cmd(["x86_64-linux-gnu-objcopy", "-O", "binary", wm_elf, wm_bin])
    run_cmd(["x86_64-linux-gnu-ld", "-r", "-b", "binary", "menedzer_okien.bin",
             "-o", "menedzer_okien_blob.o"], cwd=POSIX_DIR)

    kernel_elf = POSIX_DIR / "kernel_posix_gate.bin"
    objs = []
    for o in base_objs:
        if o == "test_blob.o":
            objs.append(str(test_blob))
        elif o == "menedzer_okien_blob.o":
            objs.append(str(wm_blob))
        else:
            objs.append(o)

    run_cmd([
        "x86_64-linux-gnu-g++",
        "-T", "jadro/arch/linker.ld", "-nostdlib", "-no-pie", "-static",
        "-Wl,-z,noexecstack", "-Wl,-z,max-page-size=0x1000", "-Wl,--build-id=none",
        "-o", kernel_elf,
        *objs, "-lgcc",
    ])
    run_cmd(["x86_64-linux-gnu-strip", "--strip-debug", "--strip-unneeded", kernel_elf])

    iso_dir = POSIX_DIR / "isodir_posix"
    if iso_dir.exists():
        shutil.rmtree(iso_dir)
    shutil.copytree(ROOT / "isodir", iso_dir)
    (iso_dir / "boot/tapety.pak").unlink(missing_ok=True)
    shutil.copy2(kernel_elf, iso_dir / "boot/system_operacyjny.bin")
    (iso_dir / "boot/grub/grub.cfg").write_text(
        "set timeout=0\n"
        "set default=0\n"
        'menuentry "Bursztyn POSIX Gate Test" {\n'
        "    insmod all_video\n"
        "    set gfxmode=1280x720x32,1024x768x32,auto\n"
        "    set gfxpayload=keep\n"
        "    multiboot2 /boot/system_operacyjny.bin\n"
        "    boot\n"
        "}\n",
        encoding="utf-8",
    )
    iso = POSIX_DIR / "posix_gate_test.iso"
    run_cmd(["grub-mkrescue", "-o", iso, iso_dir, "--xorriso=xorriso"])
    return iso


def przeczytaj_serial(sciezka: Path) -> str:
    """Czyta log serial odpornie na rownolegly zapis QEMU.

    Na montowanym dysku (9p / CIFS) odczyt pliku, ktory wlasnie jest
    zapisywany, potrafi zwrocic OSError 'No data available'. To nie blad
    testu, tylko wlasnosc systemu plikow, wiec ponawiamy kilka razy
    i traktujemy nieudany odczyt jako "jeszcze nic nie ma".
    """
    for _ in range(6):
        try:
            return sciezka.read_text(errors="replace")
        except OSError:
            time.sleep(0.15)
    return ""


def main() -> int:
    print("[POSIX-QEMU] Budowanie izolowanego testu bramki SysV w Ring 3...")
    iso = build_iso()

    serial = POSIX_DIR / "posix_gate_serial.log"
    if serial.exists():
        serial.unlink()

    qemu = ["qemu-system-x86_64", "-machine", "pc,hpet=on", "-cpu", "max",
            "-accel", "tcg,thread=single", "-smp", "1", "-m", "2G",
            "-bios", str(OVMF_SRC), "-display", "none",
            "-serial", f"file:{serial}", "-no-reboot", "-boot", "d",
            "-audiodev", "none,id=snd", "-device", "intel-hda",
            "-device", "hda-output,audiodev=snd",
            "-drive", f"file={iso},if=none,id=boot,format=raw,readonly=on",
            "-device", "virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1",
            "-object", "rng-random,id=rng0,filename=/dev/urandom",
            "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",
            "-vga", "none",
            "-device", "virtio-vga,id=gpu0,disable-legacy=on,xres=1280,yres=720",
            "-device", "virtio-keyboard-pci", "-device", "virtio-mouse-pci"]

    print("[POSIX-QEMU] Uruchamianie testu bramki SysV w QEMU...")
    proc = subprocess.Popen([str(c) for c in qemu],
                            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    passed = False
    failed = False
    try:
        deadline = time.monotonic() + 300.0
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                break
            if serial.exists():
                text = przeczytaj_serial(serial)
                if "[POSIX-QEMU-FAIL]" in text:
                    failed = True
                    break
                if "[POSIX-QEMU] ALL_CHECKS_PASSED" in text:
                    passed = True
                    break
            time.sleep(0.4)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    text = przeczytaj_serial(serial)
    for line in text.splitlines():
        if "[POSIX-QEMU" in line:
            print(f"  {line}")

    if failed:
        print("[POSIX-QEMU] FAIL: test zglosil blad w Ring 3.")
        return 1
    if not passed:
        print("[POSIX-QEMU] FAIL: brak potwierdzenia PASS w logu szeregowym.")
        return 1
    print("[POSIX-QEMU] PASS: bramka SysV dziala w Ring 3 (8/8 kategorii).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
