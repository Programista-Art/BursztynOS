#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
src = root / "isodir"
dst = root / ".isodir_1366"
iso = root / "BursztynOS-1366.iso"

if dst.exists():
    shutil.rmtree(dst)
shutil.copytree(src, dst)

cfg = dst / "boot" / "grub" / "grub.cfg"
text = cfg.read_text(encoding="utf-8")
old = "set gfxmode=1920x1080x32,1280x720x32,1024x768x32,auto"
new = "set gfxmode=1366x768x32,1280x720x32,1024x768x32,auto"
if old not in text:
    sys.exit(f"expected gfxmode line missing in {cfg}")
cfg.write_text(text.replace(old, new), encoding="utf-8")
print("gfxmode lines:")
for line in cfg.read_text(encoding="utf-8").splitlines():
    if "gfxmode" in line:
        print(" ", line.strip())

subprocess.run(
    ["grub-mkrescue", "-o", str(iso), str(dst), "--xorriso=xorriso"],
    check=True,
)
print("ISO:", iso, "bytes=", iso.stat().st_size)
