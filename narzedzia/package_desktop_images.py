#!/usr/bin/env python3
"""Stage the desktop sample images as Multiboot2 modules.

test.bmp / test1.bmp used to be linked into the kernel image (2.0 MiB of the
15 MiB image limit). They are now copied byte-for-byte to /boot/pulpit/ in
the ISO staging tree and loaded by GRUB with exact labels
(`pulpit/test.bmp`, `pulpit/test1.bmp`) in every menu entry that boots the
kernel. The kernel validates them (jadro/rdzen/obrazy_startowe.h) and writes
them to /uzytkownicy/Pulpit/ exactly as before.

Usage: package_desktop_images.py <staging-dir> [<grub.cfg>]
"""
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IMAGES = (('test.bmp', 'pulpit/test.bmp'), ('test1.bmp', 'pulpit/test1.bmp'))
MAX_SIZE = 8 * 1024 * 1024  # mirrors obrazy_startowe::MaksRozmiar


def check(path):
    data = path.read_bytes()
    if not 54 <= len(data) <= MAX_SIZE:
        raise SystemExit(f'{path}: size {len(data)} outside 54..{MAX_SIZE}')
    if data[:2] != b'BM' or struct.unpack_from('<I', data, 2)[0] != len(data):
        raise SystemExit(f'{path}: not a self-consistent BMP')
    return data


def package(stage, cfg=None):
    stage = Path(stage)
    cfg = Path(cfg) if cfg else stage / 'boot/grub/grub.cfg'
    target = stage / 'boot/pulpit'
    target.mkdir(parents=True, exist_ok=True)
    for name, _ in IMAGES:
        source = ROOT / 'obrazy' / name
        data = check(source)
        shutil.copyfile(source, target / name)
        if (target / name).read_bytes() != data:
            raise SystemExit(f'{name}: staged copy differs')
    text = cfg.read_text()
    text = re.sub(r'(?m)^.*module2 /boot/pulpit/.*\n', '', text)
    lines = ''.join(f'    module2 /boot/pulpit/{name} {label}\n' for name, label in IMAGES)
    # Insert before each entry's `boot` (only entries that load the kernel).
    text, count = re.subn(r'(?m)^([ \t]*)boot\s*$', lambda m: lines + m.group(1) + 'boot', text)
    if count == 0:
        raise SystemExit(f'{cfg}: no boot entries')
    cfg.write_text(text)
    print(f'Desktop images: staged {len(IMAGES)} BMP modules in {count} GRUB entries')


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    package(*sys.argv[1:])
