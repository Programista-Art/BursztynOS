#!/usr/bin/env python3
"""Offline contract parser for a staged BursztynOS musl sysroot.

This gate never downloads, configures, or compiles anything.  A missing
sysroot is an explicit dependency skip; a present but malformed sysroot is a
failure, so a broken staging tree cannot look like a passing contract.
"""

from __future__ import annotations

import json
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SYSROOT = Path(os.environ.get("MUSL_SYSROOT", ROOT / ".musl-build" / "sysroot"))
METADATA = SYSROOT / "usr/share/bursztyn-musl.json"


def fail(message: str) -> int:
    print(f"[MUSL-SYSROOT-CONTRACT] FAIL: {message}")
    return 1


def main() -> int:
    if not SYSROOT.is_dir():
        print(
            "[MUSL-SYSROOT-CONTRACT] SKIP: staged musl sysroot is unavailable: "
            f"{SYSROOT}"
        )
        return 0

    if not METADATA.is_file():
        return fail(f"metadata is missing: {METADATA}")

    try:
        metadata = json.loads(METADATA.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return fail(f"cannot parse metadata: {error}")

    if metadata.get("target") != "x86_64-bursztynos":
        return fail("metadata target must be x86_64-bursztynos")
    if metadata.get("syscall_entry") != "int 0x80":
        return fail("metadata syscall_entry must be int 0x80")
    if metadata.get("shared_libraries") is not False:
        return fail("metadata must explicitly disable shared libraries")

    required = (
        "usr/include/stdio.h",
        "usr/include/unistd.h",
        "usr/include/bursztyn/syscall.h",
        "usr/lib/crt1.o",
        "usr/lib/libc.a",
    )
    missing = [str(SYSROOT / path) for path in required if not (SYSROOT / path).is_file()]
    if missing:
        return fail("required sysroot files are missing: " + ", ".join(missing))

    print(
        "[MUSL-SYSROOT-CONTRACT] PASS: x86_64 static sysroot, "
        "int 0x80 ABI, required crt/libc/headers"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
