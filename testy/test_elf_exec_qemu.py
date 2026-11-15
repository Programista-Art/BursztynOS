#!/usr/bin/env python3
"""
Bursztyn OS - test QEMU loadera ELF64 (ET_EXEC) w prawdziwym Ring 3.

Cel: udowodnic, ze obraz ELF64 z obcym kompilatorem (gcc, nie .bur) jest
czytany, walidowany, mapowany i uruchamiany jako proces Ring 3, a nie tylko
rozpoznawany po magic.

Sprawdzane kategorie:

  1. proces ELF w ogole startuje i wykonuje kod z .text,
  2. bramka SysV (int 0x80) dziala z procesu zaladowanego z ELF,
  3. .rodata i .data sa czytelne, a BSS jest wyzerowane,
  4. stos Ring 3 ma 2 MiB (gleboki zapis 1,5 MiB nie wywoluje #PF),
  5. BWS nadal dziala po uruchomieniu procesu ELF (brak regresji).

Statyczny ET_DYN/PIE ma osobny gate hostowy w test_elf_loader_host.cpp:
sprawdza SHT_RELA/R_X86_64_RELATIVE, load bias i odrzucenie innych relokacji.
Ten scenariusz QEMU pozostaje ET_EXEC, zeby nie mieszac testu ABI SysV z
testem mapowania relokacji.

Metoda jest ta sama co w test_posix_gate_qemu.py: podmieniamy blob testowej
aplikacji w obrazie ISO i uruchamiamy QEMU z serialem do pliku. Produkcyjny
BursztynOS.iso nie jest dotykany.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORK_DIR = ROOT / ".elf-exec-test"
OVMF_SRC = Path("/usr/share/ovmf/OVMF.fd")

ELF_SOURCE = r'''
#include <stdint.h>

extern "C" void _start();

/* .data: wartosc musi przezyc kopiowanie segmentu RW. */
static volatile uint64_t g_dane[2] = { 0x1122334455667788ULL, 0xAABBCCDDEEFF0011ULL };

/* .bss: musi byc WYZEROWANE, bo strona przyszla z PMM i nie ma jej w pliku. */
static volatile uint8_t g_bss[4096];

/* .rodata: tylko do odczytu, ale czytelne. */
static const char g_tekst[] = "ELF-BSS-DATA-OK";

/*
 * Wywolanie bramki SysV: numer w RAX, argumenty w RDI/RSI/RDX/R10/R8/R9.
 *
 * Trzeci argument MUSI trafic do RDX - R10 jest dopiero czwartym argumentem
 * ABI SysV. Pomylka tych dwoch rejestrow daje bledne wywolanie, ktore
 * wyglada jak awaria jadra.
 */
static long sys0(long nr) {
    long out;
    __asm__ volatile("int $0x80"
                     : "=a"(out)
                     : "a"(nr)
                     : "rcx", "r11", "memory");
    return out;
}

static long sys1(long nr, long a1) {
    long out;
    __asm__ volatile("int $0x80"
                     : "=a"(out)
                     : "a"(nr), "D"(a1)
                     : "rcx", "r11", "memory");
    return out;
}

static long sys3(long nr, long a1, long a2, long a3) {
    long out;
    __asm__ volatile("int $0x80"
                     : "=a"(out)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return out;
}

/*
 * Czwarty argument ABI SysV idzie do R10, nie do RCX. To jedyne miejsce,
 * gdzie pomylka jest niewidoczna bez testu: prlimit64 z limitem w R10.
 */
static long sys4(long nr, long a1, long a2, long a3, long a4) {
    long out;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("int $0x80"
                     : "=a"(out)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                     : "rcx", "r11", "memory");
    return out;
}

enum : long {
    O_RDONLY = 0,
    O_RDWR   = 2,
    O_CREAT  = 0x40,
    O_TRUNC  = 0x200,
    SEEK_SET = 0,
    EBADF    = 9,
    EACCES   = 13
};

static void log_s(const char* s) {
    long n = 0;
    while (s[n]) ++n;
    (void)sys3(1, 1, (long)s, n);
}

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            log_s("\n[ELF-EXEC-FAIL] " msg "\n");                            \
            (void)sys3(60, 1, 0, 0);                                         \
            for (;;) {}                                                      \
        }                                                                    \
    } while (0)

struct Utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

extern "C" __attribute__((noreturn)) void _start() {
    log_s("\n[ELF-EXEC] Start testu loadera ELF64 (ET_EXEC) w Ring 3\n");

    /* 1. Kod z .text sie wykonuje - jestesmy tutaj. */
    log_s("[ELF-EXEC] 1/9 kod .text wykonany: OK\n");

    /* 2. Bramka SysV dziala z procesu zaladowanego z ELF. */
    const long pid = sys3(39, 0, 0, 0);
    CHECK(pid > 0 && pid < 64, "getpid nie zwrocil sensownego PID");
    log_s("[ELF-EXEC] 2/9 bramka SysV (getpid) dziala: OK\n");

    /* 3. .data skopiowane z pliku, BSS wyzerowane, .rodata czytelne. */
    CHECK(g_dane[0] == 0x1122334455667788ULL, ".data nie zostalo skopiowane");
    CHECK(g_dane[1] == 0xAABBCCDDEEFF0011ULL, ".data: drugie slowo uszkodzone");
    for (int i = 0; i < 4096; ++i) {
        CHECK(g_bss[i] == 0, "BSS nie jest wyzerowane");
    }
    CHECK(g_tekst[0] == 'E' && g_tekst[4] == 'B', ".rodata nieczytelne");
    log_s("[ELF-EXEC] 3/9 .data, .bss (zerowane) i .rodata: OK\n");

    /*
     * 4. Stos Ring 3 = 2 MiB. Zapis 1,5 MiB w dol od biezacego RSP dowodzi,
     *    ze strony sa realnie zmapowane, a nie tylko zadeklarowane. Stos
     *    .bur (256 KiB) nie pomiescilby tego.
     */
    {
        volatile uint8_t bufor[1536 * 1024];
        for (unsigned i = 0; i < sizeof(bufor); i += 4096) bufor[i] = (uint8_t)(i >> 12);
        for (unsigned i = 0; i < sizeof(bufor); i += 4096) {
            CHECK(bufor[i] == (uint8_t)(i >> 12), "stos 2 MiB: zapis/odczyt nie dziala");
        }
    }
    log_s("[ELF-EXEC] 4/9 stos Ring 3 2 MiB (zapis 1,5 MiB): OK\n");

    /* 5. uname(2) dziala i raportuje BursztynOS. */
    {
        Utsname u{};
        const long r = sys3(63, (long)&u, 0, 0);
        CHECK(r == 0, "uname nie powiodlo sie");
        CHECK(u.sysname[0] == 'B', "uname: zla nazwa systemu");
        CHECK(u.machine[0] == 'x', "uname: zla architektura");
    }
    log_s("[ELF-EXEC] 5/9 uname(2) z procesu ELF: OK\n");

    /* 6. Zapis markera przez write(1) trafia do logu szeregowego. */
    const char marker[] = "[ELF-EXEC] 6/9 write(1) z procesu ELF: OK\n";
    const long zapisane = sys3(1, 1, (long)marker, sizeof(marker) - 1);
    CHECK(zapisane == (long)(sizeof(marker) - 1), "write(1) nie zwrocil dlugosci");

    /*
     * 7. VFS: open(O_CREAT) -> write -> close -> open(O_RDONLY) -> read.
     *
     * To jest pelny cykl deskryptora pliku, na ktorym stoi kazda libc.
     * Sprawdzamy takze, ze dane przezyly zapis (PSF), a nie tylko bufor.
     */
    {
        const char* sciezka = "/tymczasowe/elf_vfs.bin";
        const long fd = sys3(2, (long)sciezka, O_RDWR | O_CREAT | O_TRUNC, 0644);
        CHECK(fd >= 3, "open(O_CREAT) nie zwrocil fd >= 3");

        const char dane[] = "Bursztyn-VFS-1234567890";
        const long zap = sys3(1, fd, (long)dane, sizeof(dane) - 1);
        CHECK(zap == (long)(sizeof(dane) - 1), "write(fd, ...) nie zapisal calosci");

        CHECK(sys3(3, fd, 0, 0) == 0, "close(fd) nie powiodl sie");

        /* Ponowne otwarcie: dane musza byc na dysku, nie w buforze procesu. */
        const long fd2 = sys3(2, (long)sciezka, O_RDONLY, 0);
        CHECK(fd2 >= 3, "open(O_RDONLY) nie zwrocil fd");

        char odczyt[64] = {};
        const long r = sys3(0, fd2, (long)odczyt, sizeof(odczyt) - 1);
        CHECK(r == (long)(sizeof(dane) - 1), "read() nie zwrocil dlugosci zapisu");
        for (long i = 0; i < r; ++i) {
            CHECK(odczyt[i] == dane[i], "read(): dane roznia sie od zapisanych");
        }

        /* EOF: drugi read na koncu pliku zwraca 0, nie blad i nie dane. */
        CHECK(sys3(0, fd2, (long)odczyt, sizeof(odczyt) - 1) == 0,
              "read() na koncu pliku nie zwrocil 0 (EOF)");

        /* lseek(SEEK_SET) i ponowny odczyt pierwszego bajtu. */
        CHECK(sys3(8, fd2, 0, SEEK_SET) == 0, "lseek(SEEK_SET, 0) nie powiodl sie");
        char jeden = 0;
        CHECK(sys3(0, fd2, (long)&jeden, 1) == 1, "read() po lseek nie zwrocil 1");
        CHECK(jeden == 'B', "read() po lseek zwrocil zly bajt");

        /* fstat: rozmiar musi byc zgodny z zapisanym. */
        struct StatBuf {
            uint64_t st_dev, st_ino, st_nlink;
            uint32_t st_mode, st_uid, st_gid, pad;
            uint64_t st_rdev;
            int64_t st_size, st_blksize, st_blocks;
            int64_t ts[6];
            int64_t unused[3];
        } st{};
        CHECK(sys3(5, fd2, (long)&st, 0) == 0, "fstat nie powiodl sie");
        CHECK(st.st_size == (long)(sizeof(dane) - 1), "fstat: zly rozmiar pliku");
        CHECK((st.st_mode & 0xF000U) == 0x8000U, "fstat: to nie jest zwykly plik");

        CHECK(sys3(3, fd2, 0, 0) == 0, "close(fd2) nie powiodl sie");
        /* Podwojne zamkniecie musi byc bledem, a nie cichym sukcesem. */
        CHECK(sys3(3, fd2, 0, 0) == -EBADF, "podwojne close nie zwrocilo -EBADF");
    }
    log_s("[ELF-EXEC] 7/9 VFS: open/write/close/read/lseek/fstat: OK\n");

    /*
     * 8. REGRESJA BEZPIECZENSTWA: PZB-4 nie moze pisac do drzew chronionych.
     *
     * Ten sam proces ma teraz PRAWO_PLIKI_ZAPISZ (potrzebne do punktu 7),
     * wiec odmowa MUSI pochodzic z reguly drzewa, a nie z braku prawa.
     * To jest wlasciwy test polityki: gdyby ktos usunal spraedzanie
     * sciezki w VFS, ten punkt by sie wywalil.
     */
    {
        const long zly = sys3(2, (long)"/system/konfig/atak.cfg",
                             O_RDWR | O_CREAT, 0644);
        CHECK(zly == -EACCES, "zapis do /system nie zostal odrzucony");
        const long zly2 = sys3(2, (long)"/jadro/test.cfg", O_RDWR | O_CREAT, 0644);
        CHECK(zly2 == -EACCES, "zapis do /jadro nie zostal odrzucony");
        const long zly3 = sys3(2, (long)"/programy/test.cebula/x.bin",
                              O_RDWR | O_CREAT, 0644);
        CHECK(zly3 == -EACCES, "zapis do /programy nie zostal odrzucony");
    }
    log_s("[ELF-EXEC] 8/9 PZB: zapis do drzew chronionych odrzucony: OK\n");

    /*
     * 9. WYWOLANIA STARTOWE LIBC w prawdziwym Ring 3.
     *
     * To dokladnie ta lista, ktorej brak powoduje, ze musl/glibc przerywa
     * start przed main(). Sprawdzamy nie tylko kod wyjscia, ale i wartosci:
     * -ENOSYS przeszloby "wystarczy != blad", ale nie uruchomiloby musl.
     */
    {
        /* set_tid_address zwraca PID wolajacego. */
        const long tid = sys1(218, 0);
        CHECK(tid > 0 && tid < 64, "set_tid_address nie zwrocil PID");

        /* set_robust_list: rejestracja przyjeta (0). */
        CHECK(sys3(273, 0, 24, 0) == 0, "set_robust_list nie zwrocil 0");

        /* prlimit64(RLIMIT_NOFILE): stary limit = 64 deskryptory VFS. */
        struct Rlimit { uint64_t cur, max; } lim{};
        CHECK(sys4(302, 0, 7, 0, (long)&lim) == 0, "prlimit64 nie powiodl sie");
        CHECK(lim.cur == 64 && lim.max == 64, "prlimit64: zly limit NOFILE");

        /* prlimit64(RLIMIT_AS) = nieskonczonosc. */
        struct Rlimit lim_as{};
        CHECK(sys4(302, 0, 9, 0, (long)&lim_as) == 0, "prlimit64(AS) nie powiodl sie");
        CHECK(lim_as.cur == 0xFFFFFFFFFFFFFFFFULL, "prlimit64(AS) != infinity");

        /* Nieznany zasob: EINVAL, nie "nieskonczonosc". */
        struct Rlimit lim_zly{};
        CHECK(sys4(302, 0, 999, 0, (long)&lim_zly) == -22,
              "prlimit64(zly zasob) nie zwrocil -EINVAL");

        /* sched_getaffinity: jeden bit, bo planista jest jednordzeniowy. */
        uint64_t maska = 0;
        const long a = sys3(204, 0, 8, (long)&maska);
        CHECK(a == 8, "sched_getaffinity nie zwrocil rozmiaru maski");
        CHECK(maska == 1, "sched_getaffinity: maska nie ma dokladnie 1 bitu");

        /* sysinfo: totalram i freeram musza byc niezerowe. */
        struct SysinfoBuf {
            int64_t uptime; uint64_t loads[3];
            uint64_t totalram, freeram, sharedram, bufferram;
            uint64_t totalswap, freeswap;
            uint16_t procs, pad; uint32_t pad2;
            uint64_t totalhigh, freehigh; uint32_t mem_unit, pad3;
        } si{};
        CHECK(sys1(99, (long)&si) == 0, "sysinfo nie powiodl sie");
        CHECK(si.totalram > 0 && si.freeram > 0, "sysinfo: zerowa pamiec");
        CHECK(si.mem_unit == 1, "sysinfo: mem_unit != 1");

        /* access() na istniejacym pliku i na nieistniejacym. */
        CHECK(sys3(21, (long)"/tymczasowe/elf_vfs.bin", 0, 0) == 0,
              "access(istniejacy plik) nie zwrocil 0");
        CHECK(sys3(21, (long)"/nie/ma/takiego", 0, 0) == -2,
              "access(nieistniejacy) nie zwrocil -ENOENT");

        /* brk: zapytanie zwraca biezaca granice, rozszerzenie dziala. */
        const long b0 = sys1(12, 0);
        CHECK(b0 > 0, "brk(0) nie zwrocil granicy sterty");
        const long b1 = sys1(12, (long)(b0 + 8192));
        CHECK(b1 == b0 + 8192, "brk: rozszerzenie nie powiodlo sie");
        /* Zapis do nowej pamieci musi byc mozliwy. */
        volatile char* nowa = (volatile char*)b1 - 4096;
        nowa[0] = 'X';
        CHECK(nowa[0] == 'X', "brk: nowa strona nie jest zapisywalna");
        /* Nie wyrownana granica: EINVAL, nie ciche wyrownanie. */
        CHECK(sys1(12, (long)(b1 + 1)) == -22, "brk(±1) nie zwrocil -EINVAL");
    }
    log_s("[ELF-EXEC] 9/9 wywolania startowe libc (tid/robust/prlimit/affinity/sysinfo/access/brk): OK\n");

    log_s("[ELF-EXEC] ALL_CHECKS_PASSED: loader ELF64 dziala w Ring 3\n");
    (void)sys3(60, 0, 0, 0);
    for (;;) {}
}
'''

ELF_LINKER = r'''
ENTRY(_start)
SECTIONS
{
  . = 0x0000000002100000;
  .text : { *(.text) *(.text.*) }
  .rodata : { *(.rodata) *(.rodata.*) }
  . = ALIGN(0x1000);
  .data : { *(.data) *(.data.*) }
  .bss  : { *(.bss) *(.bss.*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
'''

# Ten sam launcher co w tescie bramki SysV: jedyne zadanie to uruchomic
# testowa aplikacje przez BWS, zeby jadro mialo kogo sklonowac do Ring 3.
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
    res = subprocess.run([str(c) for c in cmd], cwd=str(cwd),
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True)
    if res.returncode != 0:
        print(f"[ELF-EXEC] Command failed ({res.returncode}): "
              f"{' '.join(str(c) for c in cmd)}")
        print(res.stdout)
        sys.exit(1)
    return res


def get_make_objs():
    res = subprocess.run(["make", "-qp"], cwd=str(ROOT),
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True)
    for line in res.stdout.splitlines():
        if line.startswith("OBJS := ") or line.startswith("OBJS = "):
            parts = line.split("=", 1)[1].strip().split()
            return [p for p in parts if not p.startswith("$(")]
    raise RuntimeError("Could not parse OBJS from Makefile")


def build_elf() -> Path:
    WORK_DIR.mkdir(parents=True, exist_ok=True)

    src = WORK_DIR / "elf_exec_app.cpp"
    src.write_text(ELF_SOURCE, encoding="utf-8")
    linker = WORK_DIR / "elf_exec.ld"
    linker.write_text(ELF_LINKER, encoding="utf-8")

    obj = WORK_DIR / "elf_exec_app.o"
    elf = WORK_DIR / "elf_exec_app.elf"

    flags = [
        "x86_64-linux-gnu-g++",
        "-std=gnu++17", "-O2", "-ffreestanding",
        "-fno-asynchronous-unwind-tables", "-fno-unwind-tables",
        "-mcmodel=large", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2",
        "-fno-stack-protector",
        "-fcf-protection=none", "-fno-pic", "-fno-pie",
        "-ffunction-sections", "-fdata-sections",
        "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
    ]

    run_cmd([*flags, "-c", src, "-o", obj])
    run_cmd([
        "x86_64-linux-gnu-ld", "--gc-sections", "-T", linker,
        "-nostdlib", "-no-pie", "--build-id=none",
        obj, "-o", elf,
    ])

    # Kontrola formatu: musi byc ET_EXEC, inaczej test sprawdzalby zla sciezke.
    info = run_cmd(["x86_64-linux-gnu-readelf", "-h", elf]).stdout
    if "EXEC (Executable file)" not in info:
        print("[ELF-EXEC] FAIL: zbudowany obraz nie jest ET_EXEC")
        print(info)
        sys.exit(1)

    return elf


def build_bur_payload(elf: Path) -> Path:
    """Pakuje ELF jako blob test.bin dokladnie tak, jak robi to Makefile dla .bur."""
    payload = WORK_DIR / "test.bin"
    shutil.copy2(elf, payload)
    run_cmd(["x86_64-linux-gnu-ld", "-r", "-b", "binary", "test.bin",
             "-o", "test_blob.o"], cwd=WORK_DIR)
    return WORK_DIR / "test_blob.o"


def build_wm_launcher() -> Path:
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

    wm_src = WORK_DIR / "wm_launcher.cpp"
    wm_src.write_text(WM_LAUNCHER, encoding="utf-8")
    wm_obj = WORK_DIR / "wm_launcher.o"
    wm_elf = WORK_DIR / "menedzer_okien.elf"
    wm_bin = WORK_DIR / "menedzer_okien.bin"

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
             "-o", "menedzer_okien_blob.o"], cwd=WORK_DIR)
    return WORK_DIR / "menedzer_okien_blob.o"


def build_iso(elf: Path) -> Path:
    test_blob = build_bur_payload(elf)
    wm_blob = build_wm_launcher()

    base_objs = get_make_objs()
    objs = []
    for o in base_objs:
        if o == "test_blob.o":
            objs.append(str(test_blob))
        elif o == "menedzer_okien_blob.o":
            objs.append(str(wm_blob))
        else:
            objs.append(o)

    kernel_elf = WORK_DIR / "kernel_elf_exec.bin"
    run_cmd([
        "x86_64-linux-gnu-g++",
        "-T", "jadro/arch/linker.ld", "-nostdlib", "-no-pie", "-static",
        "-Wl,-z,noexecstack", "-Wl,-z,max-page-size=0x1000",
        "-Wl,--build-id=none",
        "-o", kernel_elf,
        *objs, "-lgcc",
    ])
    run_cmd(["x86_64-linux-gnu-strip", "--strip-debug", "--strip-unneeded",
             kernel_elf])

    iso_dir = WORK_DIR / "isodir_elf"
    if iso_dir.exists():
        shutil.rmtree(iso_dir)
    if not (ROOT / "isodir").exists():
        print("[ELF-EXEC] FAIL: brak isodir/ - najpierw `make iso`.")
        sys.exit(1)
    shutil.copytree(ROOT / "isodir", iso_dir)
    (iso_dir / "boot/tapety.pak").unlink(missing_ok=True)
    shutil.copy2(kernel_elf, iso_dir / "boot/system_operacyjny.bin")
    (iso_dir / "boot/grub/grub.cfg").write_text(
        "set timeout=0\n"
        "set default=0\n"
        'menuentry "Bursztyn ELF Exec Test" {\n'
        "    insmod all_video\n"
        "    set gfxmode=1280x720x32,1024x768x32,auto\n"
        "    set gfxpayload=keep\n"
        "    multiboot2 /boot/system_operacyjny.bin\n"
        "    boot\n"
        "}\n",
        encoding="utf-8",
    )
    iso = WORK_DIR / "elf_exec_test.iso"
    run_cmd(["grub-mkrescue", "-o", iso, iso_dir, "--xorriso=xorriso"])
    return iso


def przeczytaj_serial(sciezka: Path) -> str:
    """Czyta log serial odpornie na rownolegly zapis QEMU (dysk 9p)."""
    for _ in range(6):
        try:
            return sciezka.read_text(errors="replace")
        except OSError:
            time.sleep(0.15)
    return ""


def main() -> int:
    print("[ELF-EXEC] Budowanie obrazu testowego ELF64...")
    elf = build_elf()
    iso = build_iso(elf)

    serial = WORK_DIR / "elf_exec_serial.log"
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

    print("[ELF-EXEC] Uruchamianie testu w QEMU...")
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
                if "[ELF-EXEC-FAIL]" in text:
                    failed = True
                    break
                if "[ELF-EXEC] ALL_CHECKS_PASSED" in text:
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
        if "[ELF-EXEC" in line:
            print(f"  {line}")

    if failed:
        print("[ELF-EXEC] FAIL: test zglosil blad w Ring 3.")
        return 1
    if not passed:
        print("[ELF-EXEC] FAIL: brak potwierdzenia PASS w logu szeregowym.")
        return 1
    print("[ELF-EXEC] PASS: loader ELF64 (ET_EXEC) i VFS dzialaja w Ring 3 (9/9 kategorii).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
