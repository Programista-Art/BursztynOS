#!/usr/bin/env python3
"""QEMU contract gate for the staged static CPython runtime.

The gate validates the staged runtime offline first.  It runs a guest only
when an explicitly prepared ISO is supplied through CPYTHON_GUEST_ISO.  This
keeps the normal contract gate deterministic and prevents a missing guest
packaging step from becoming a false PASS.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "narzedzia/python/cpython-runtime.manifest.json"
RUNTIME_ROOT = Path(
    os.environ.get("CPYTHON_RUNTIME_ROOT", ROOT / ".cpython-build" / "runtime")
)
FIXTURE = ROOT / "testy/fixtures/cpython_guest_contract.py"


def skip(message: str) -> int:
    print(f"[CPYTHON-GUEST] SKIP: {message}")
    return 0


def fail(message: str) -> int:
    print(f"[CPYTHON-GUEST] FAIL: {message}")
    return 1


def native_qemu() -> str | None:
    configured = os.environ.get("QEMU_X86_64")
    candidate = configured or shutil.which("qemu-system-x86_64.exe")
    if not candidate:
        return None
    if Path(candidate).name.lower() != "qemu-system-x86_64.exe":
        return None
    return candidate


def ovmf_path() -> Path | None:
    configured = os.environ.get("OVMF_FD")
    candidates = (
        Path(configured) if configured else None,
        Path(r"C:\Program Files\qemu\share\edk2-x86_64-code.fd"),
        Path(r"C:\Program Files\qemu\share\edk2-x86_64.fd"),
        ROOT / "OVMF.fd",
    )
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate
    return None


def safe_runtime_path(relative: str) -> Path:
    path = (RUNTIME_ROOT / relative.lstrip("/")).resolve()
    root = RUNTIME_ROOT.resolve()
    if path != root and root not in path.parents:
        raise ValueError(f"runtime path escapes staging root: {relative}")
    return path


def validate_static_elf(path: Path) -> str | None:
    try:
        image = path.read_bytes()
    except OSError as error:
        return f"cannot read interpreter: {error}"
    if len(image) < 64 or image[:4] != b"\x7fELF":
        return "interpreter is not an ELF image"
    if image[4] != 2 or image[5] != 1:
        return "interpreter is not little-endian ELF64"

    e_type, e_machine = struct.unpack_from("<HH", image, 16)
    if e_type != 2 or e_machine != 62:
        return "interpreter must be x86_64 ET_EXEC"
    phoff = struct.unpack_from("<Q", image, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", image, 54)
    if phentsize < 56 or phnum == 0:
        return "interpreter has no valid program-header table"
    table_end = phoff + phentsize * phnum
    if phoff > len(image) or table_end > len(image):
        return "program-header table exceeds interpreter"

    executable_entry = False
    load_count = 0
    for index in range(phnum):
        offset = phoff + index * phentsize
        p_type, flags = struct.unpack_from("<II", image, offset)
        if p_type == 3:
            return "static interpreter unexpectedly has PT_INTERP"
        if p_type == 2:
            return "static interpreter unexpectedly has PT_DYNAMIC"
        if p_type != 1:
            continue
        load_count += 1
        if flags & 0x2 and flags & 0x1:
            return "interpreter contains a writable/executable PT_LOAD"
        vaddr = struct.unpack_from("<Q", image, offset + 16)[0]
        memsz = struct.unpack_from("<Q", image, offset + 40)[0]
        entry = struct.unpack_from("<Q", image, 24)[0]
        if flags & 0x1 and vaddr <= entry < vaddr + memsz:
            executable_entry = True
    if load_count == 0:
        return "interpreter has no PT_LOAD"
    if not executable_entry:
        return "interpreter entry point is outside executable PT_LOAD"
    return None


def validate_staging() -> int:
    try:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return fail(f"cannot read source manifest: {error}")
    try:
        staged_manifest = safe_runtime_path("/usr/share/cpython-runtime.manifest.json")
    except ValueError as error:
        return fail(str(error))
    if not staged_manifest.is_file():
        return fail(f"staged runtime manifest is missing: {staged_manifest}")
    try:
        staged = json.loads(staged_manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return fail(f"cannot parse staged manifest: {error}")
    if staged.get("version") != manifest.get("version"):
        return fail("staged manifest version differs from source manifest")

    layout = manifest.get("runtime_layout", {})
    required = (
        layout.get("interpreter"),
        layout.get("versioned_interpreter"),
        layout.get("stdlib"),
        layout.get("encodings"),
        layout.get("sysconfig_module"),
        layout.get("site_packages"),
    )
    if any(not isinstance(item, str) or not item.startswith("/") for item in required):
        return fail("source manifest has an invalid runtime layout")

    try:
        paths = [safe_runtime_path(item) for item in required]
    except ValueError as error:
        return fail(str(error))
    missing = [str(path) for path in paths if not path.exists()]
    if missing:
        return fail("staged runtime is incomplete: " + ", ".join(missing))
    if not FIXTURE.is_file():
        return fail(f"guest fixture is missing: {FIXTURE}")
    try:
        compile(FIXTURE.read_text(encoding="utf-8"), str(FIXTURE), "exec")
    except (OSError, SyntaxError) as error:
        return fail(f"guest fixture is invalid: {error}")

    error = validate_static_elf(paths[0])
    if error:
        return fail(error)
    dynamic_files = [
        path
        for path in RUNTIME_ROOT.rglob("*")
        if path.is_file() and path.suffix in {".so", ".a"}
    ]
    if dynamic_files:
        return fail("staged runtime contains forbidden dynamic/archive files")
    print("[CPYTHON-GUEST] CHECK: staged static runtime and guest fixture contract")
    return 0


def run_guest(qemu: str, bios: Path, iso: Path) -> int:
    serial = ROOT / ".cpython-build" / "guest-contract-serial.log"
    serial.parent.mkdir(parents=True, exist_ok=True)
    serial.unlink(missing_ok=True)
    command = [
        qemu,
        "-machine",
        "pc,hpet=on",
        "-cpu",
        "max",
        "-accel",
        "tcg,thread=single",
        "-smp",
        "1",
        "-m",
        "2G",
        "-bios",
        str(bios),
        "-display",
        "none",
        "-serial",
        f"file:{serial}",
        "-no-reboot",
        "-boot",
        "d",
        "-drive",
        f"file={iso},if=none,id=boot,format=raw,readonly=on",
        "-device",
        "virtio-blk-pci,drive=boot,disable-legacy=on,bootindex=1",
        "-vga",
        "none",
        "-device",
        "virtio-vga,id=gpu0,disable-legacy=on,xres=1280,yres=720",
        "-device",
        "virtio-keyboard-pci",
        "-device",
        "virtio-mouse-pci",
    ]
    print("[CPYTHON-GUEST] starting native Windows QEMU contract gate...")
    process = subprocess.Popen(
        command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
    )
    marker = os.environ.get("CPYTHON_GUEST_MARKER", "BURSZTYN-PYTHON-CONTRACT: PASS")
    failed_marker = "BURSZTYN-PYTHON-CONTRACT: FAIL"
    try:
        deadline = time.monotonic() + 180.0
        while time.monotonic() < deadline:
            text = serial.read_text(errors="replace") if serial.exists() else ""
            if failed_marker in text:
                return fail("guest fixture reported failure")
            if marker in text:
                print("[CPYTHON-GUEST] PASS: guest fixture marker observed")
                return 0
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
    return fail("guest fixture marker was not observed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", default="hello", choices=("hello",))
    args = parser.parse_args()
    del args

    if not RUNTIME_ROOT.is_dir():
        return skip(f"staged CPython runtime is unavailable: {RUNTIME_ROOT}")
    result = validate_staging()
    if result != 0:
        return result
    qemu = native_qemu()
    if not qemu:
        return skip("native qemu-system-x86_64.exe is unavailable")
    bios = ovmf_path()
    if not bios:
        return skip("native QEMU found, but Windows OVMF firmware is unavailable")
    guest_iso = os.environ.get("CPYTHON_GUEST_ISO")
    if not guest_iso:
        return skip(
            "CPYTHON_GUEST_ISO is not set; staged runtime is not yet integrated "
            "into a guest image"
        )
    iso = Path(guest_iso)
    if not iso.is_file():
        return fail(f"CPYTHON_GUEST_ISO does not exist: {iso}")
    return run_guest(qemu, bios, iso)


if __name__ == "__main__":
    raise SystemExit(main())
