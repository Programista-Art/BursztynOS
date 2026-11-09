#!/usr/bin/env python3
"""Capture canonical Settings Themes and Start power UI on the release ISO."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
if __name__ == '__main__':
    raise SystemExit(subprocess.call([
        sys.executable, str(ROOT/'testy/test_husarz_qemu.py'),
        '--settings-themes-power-only', '--output', str(ROOT/'.motywy_shots'),
        '--timeout', '1800', *sys.argv[1:]], cwd=ROOT))
