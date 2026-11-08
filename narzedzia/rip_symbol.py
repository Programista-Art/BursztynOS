#!/usr/bin/env python3
"""Map a RIP from a BSOD dump to the nearest kernel symbol."""
import bisect
import subprocess
import sys

BIN = "system_operacyjny.bin"


def main() -> int:
    targets = [int(a, 16) for a in sys.argv[1:]] or [0x18D1E0]
    out = subprocess.check_output(["nm", "-n", "--defined-only", BIN], text=True)
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            addr = int(parts[0], 16)
        except ValueError:
            continue
        syms.append((addr, parts[2]))
    syms.sort()
    addrs = [s[0] for s in syms]
    for t in targets:
        i = bisect.bisect_right(addrs, t) - 1
        if i < 0:
            print(f"{t:#x} -> <below first symbol>")
            continue
        addr, name = syms[i]
        print(f"{t:#x} -> {name} + {t - addr:#x}  (base {addr:#x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
