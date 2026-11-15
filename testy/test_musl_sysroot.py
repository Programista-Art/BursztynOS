#!/usr/bin/env python3
"""Host gate for the relocatable BursztynOS musl sysroot."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SYSROOT = Path(os.environ.get("MUSL_SYSROOT", ROOT / ".musl-build" / "sysroot"))
WORK = ROOT / ".musl-build" / "host-test"


def fail(message: str) -> None:
    print(f"[MUSL-HOST] FAIL: {message}")
    raise SystemExit(1)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        print(result.stdout)
        fail("command failed: " + " ".join(command))
    return result


def main() -> int:
    required = [
        SYSROOT / "usr/include/stdio.h",
        SYSROOT / "usr/include/unistd.h",
        SYSROOT / "usr/lib/crt1.o",
        SYSROOT / "usr/lib/libc.a",
        SYSROOT / "usr/include/bursztyn/syscall.h",
        SYSROOT / "usr/share/bursztyn-musl.json",
    ]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        fail("sysroot missing: " + ", ".join(missing))

    WORK.mkdir(parents=True, exist_ok=True)
    source = WORK / "hello.c"
    binary = WORK / "hello"
    source.write_text(
        '#include <stdio.h>\n'
        'int main(void) { puts("BURSZTYN-MUSL-HELLO"); return 0; }\n',
        encoding="utf-8",
    )

    compiler = os.environ.get("MUSL_CC", "x86_64-linux-gnu-gcc")
    run(
        [
            compiler,
            f"--sysroot={SYSROOT}",
            "-static",
            "-O2",
            "-fno-pie",
            "-no-pie",
            "-Wl,--build-id=none",
            str(source),
            "-o",
            str(binary),
        ]
    )

    readelf = os.environ.get("READELF", "x86_64-linux-gnu-readelf")
    header = run([readelf, "-h", str(binary)]).stdout
    program_headers = run([readelf, "-l", str(binary)]).stdout
    if "EXEC (Executable file)" not in header:
        fail("hello is not ET_EXEC")
    if "INTERP" in program_headers:
        fail("static hello unexpectedly has PT_INTERP")

    image = binary.read_bytes()
    if b"\xcd\x80" not in image:
        fail("static hello contains no int 0x80 syscall instruction")

    abi_source = WORK / "syscall_abi.c"
    abi_object = WORK / "syscall_abi.o"
    abi_source.write_text(
        '#include <bursztyn/syscall.h>\n'
        'long probe(long a, long b, long c, long d) {\n'
        '    return bursztyn_syscall4(9, a, b, c, d);\n'
        '}\n',
        encoding="utf-8",
    )
    run(
        [
            compiler,
            f"--sysroot={SYSROOT}",
            "-ffreestanding",
            "-fno-builtin",
            "-c",
            str(abi_source),
            "-o",
            str(abi_object),
        ]
    )

    print("[MUSL-HOST] PASS: static musl hello, ET_EXEC, no PT_INTERP, int 0x80")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
