#!/usr/bin/env python3
"""QEMU gate for a statically linked musl ELF in Ring 3."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYSROOT = Path(os.environ.get("MUSL_SYSROOT", ROOT / ".musl-build" / "sysroot"))
WORK = ROOT / ".musl-build" / "guest-test"

# Reuse the production BWS launcher and ISO assembly used by the ELF gate.
sys.path.insert(0, str(ROOT / "testy"))
from test_elf_exec_qemu import build_iso, run_cmd  # noqa: E402


def build_hello() -> Path:
    WORK.mkdir(parents=True, exist_ok=True)
    source = WORK / "hello.c"
    binary = WORK / "hello"
    source.write_text(
        '#include <stdio.h>\n'
        'int main(void) { puts("BURSZTYN-MUSL-HELLO"); return 0; }\n',
        encoding="utf-8",
    )

    compiler = os.environ.get("MUSL_CC", "x86_64-linux-gnu-gcc")
    run_cmd([
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
    ])

    readelf = os.environ.get("READELF", "x86_64-linux-gnu-readelf")
    header = run_cmd([readelf, "-h", str(binary)]).stdout
    program_headers = run_cmd([readelf, "-l", str(binary)]).stdout
    if "EXEC (Executable file)" not in header:
        raise RuntimeError("musl hello is not ET_EXEC")
    if "INTERP" in program_headers:
        raise RuntimeError("static musl hello unexpectedly has PT_INTERP")
    if b"\xcd\x80" not in binary.read_bytes():
        raise RuntimeError("musl hello contains no int 0x80 syscall instruction")
    return binary


def native_qemu() -> str | None:
    configured = os.environ.get("QEMU_X86_64")
    candidate = configured or shutil.which("qemu-system-x86_64.exe")
    if not candidate:
        return None
    if Path(candidate).name.lower() != "qemu-system-x86_64.exe":
        return None
    return candidate


def tool_available(tool: str) -> bool:
    return Path(tool).is_file() or shutil.which(tool) is not None


def staged_sysroot_ready() -> bool:
    required = (
        SYSROOT / "usr/include/stdio.h",
        SYSROOT / "usr/include/unistd.h",
        SYSROOT / "usr/include/bursztyn/syscall.h",
        SYSROOT / "usr/lib/crt1.o",
        SYSROOT / "usr/lib/libc.a",
    )
    return SYSROOT.is_dir() and all(path.is_file() for path in required)


def ovmf_path() -> Path | None:
    configured = os.environ.get("OVMF_FD")
    candidates = [
        Path(configured) if configured else None,
        Path(r"C:\Program Files\qemu\share\edk2-x86_64-code.fd"),
        Path(r"C:\Program Files\qemu\share\edk2-x86_64.fd"),
        ROOT / "OVMF.fd",
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate
    return None


def main() -> int:
    qemu = native_qemu()
    if not qemu:
        print("[MUSL-GUEST] SKIP: native qemu-system-x86_64.exe unavailable")
        return 0
    if not staged_sysroot_ready():
        print(
            "[MUSL-GUEST] SKIP: staged musl sysroot unavailable or incomplete: "
            f"{SYSROOT}"
        )
        return 0
    compiler = os.environ.get("MUSL_CC", "x86_64-linux-gnu-gcc")
    readelf = os.environ.get("READELF", "x86_64-linux-gnu-readelf")
    if not tool_available(compiler):
        print(f"[MUSL-GUEST] SKIP: musl cross compiler unavailable: {compiler}")
        return 0
    if not tool_available(readelf):
        print(f"[MUSL-GUEST] SKIP: readelf unavailable: {readelf}")
        return 0
    bios = ovmf_path()
    if not bios:
        print("[MUSL-GUEST] SKIP: native QEMU found, but no Windows OVMF firmware")
        return 0

    try:
        hello = build_hello()
    except (OSError, RuntimeError, SystemExit) as error:
        print(f"[MUSL-GUEST] FAIL: host build failed: {error}")
        return 1

    try:
        iso = build_iso(hello)
    except (OSError, RuntimeError, SystemExit) as error:
        print(f"[MUSL-GUEST] FAIL: ISO build failed: {error}")
        return 1

    serial = WORK / "musl_guest_serial.log"
    serial.unlink(missing_ok=True)
    command = [
        qemu,
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-smp", "1",
        "-m", "2G",
        "-bios", str(bios),
        "-display", "none",
        "-serial", f"file:{serial}",
        "-no-reboot",
        "-boot", "d",
        "-drive", f"file={iso},if=none,id=boot,format=raw,readonly=on",
        "-device", "virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1",
        "-vga", "none",
        "-device", "virtio-vga,id=gpu0,disable-legacy=on,xres=1280,yres=720",
        "-device", "virtio-keyboard-pci",
        "-device", "virtio-mouse-pci",
    ]

    print("[MUSL-GUEST] Starting native Windows QEMU gate...")
    process = subprocess.Popen(
        command,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )
    marker = False
    try:
        deadline = time.monotonic() + 180.0
        while time.monotonic() < deadline:
            if serial.exists():
                text = serial.read_text(errors="replace")
                if "BURSZTYN-MUSL-HELLO" in text:
                    marker = True
                    break
            if process.poll() is not None:
                break
            time.sleep(0.4)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    text = serial.read_text(errors="replace") if serial.exists() else ""
    for line in text.splitlines():
        if "MUSL" in line or "ELF" in line or "BWS" in line:
            print(f"  {line}")
    if not marker:
        print("[MUSL-GUEST] FAIL: BURSZTYN-MUSL-HELLO marker not observed")
        return 1
    print("[MUSL-GUEST] PASS: static musl ELF reached main() in Ring 3")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
