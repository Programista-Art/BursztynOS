#!/usr/bin/env python3
"""Explicit status gate for the CPython/POSIX guest matrix.

Only the hello row has an executable guest harness today.  Rows whose
required kernel or packaging facilities are not implemented are reported as
SKIP with the concrete missing contract; they are never reported as PASS.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
GUEST_GATE = ROOT / "testy/test_cpython_guest_qemu.py"

SKIPS = {
    "stdlib-import": (
        "CPython stdlib is not integrated into a guest ISO; "
        "set up the staged guest image first"
    ),
    "threading": "manifest disables pthread_create/pthread_atfork",
    "subprocess": "manifest disables fork/vfork and the process model is incomplete",
    "socket-asyncio": (
        "the event/socket facilities required by the asyncio contract "
        "are not implemented"
    ),
    "pip-pure": "ensurepip and a package manager are intentionally not shipped",
    "c-extension": "ld.so/dlopen and loadable extension modules are unsupported",
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--case",
        required=True,
        choices=("hello", *SKIPS),
    )
    args = parser.parse_args()

    if args.case == "hello":
        return subprocess.call([sys.executable, str(GUEST_GATE), "--case", "hello"])

    print(f"[CPYTHON-MATRIX] SKIP: {args.case}: {SKIPS[args.case]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
