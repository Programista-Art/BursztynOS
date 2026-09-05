#!/usr/bin/env python3
"""
generujmanifest.py - Kompatybilnosciowy wrapper przekierowujacy do generujpakiet.py.
Architektura Bursztyn OS zostala zaktualizowana: tapety nie sa linkowane do jadra,
lecz pakowane deterministycznie do pliku tapety.pak za pomoca generujpakiet.py.
"""
import sys
import subprocess
from pathlib import Path

def main():
    skrypt = Path(__file__).parent / "generujpakiet.py"
    cmd = [sys.executable, str(skrypt)] + sys.argv[1:]
    res = subprocess.run(cmd)
    sys.exit(res.returncode)

if __name__ == "__main__":
    main()
