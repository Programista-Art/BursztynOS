#!/usr/bin/env python3
"""Create one private installed BSP2 QEMU disk for the JPEG wallpaper test."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'narzedzia/instalator'))
import qemu_install as installer


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--workdir', type=Path, required=True)
    args = p.parse_args()
    directory = args.workdir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / 'target.img'
    sentinel = directory / 'sentinel.img'
    installer.sparse(target, 4 * 1024**3)
    installer.sparse(sentinel, 64 * 1024**2)
    tree = directory / 'iso'
    shutil.copytree(ROOT / 'isodir', tree)
    grub = tree / 'boot/grub/grub.cfg'
    grub.write_text(grub.read_text().replace('set default=0', 'set default=3')
                    .replace('set timeout=3', 'set timeout=0'))
    iso = directory / 'installer.iso'
    with (directory / 'mkrescue.log').open('wb') as log:
        subprocess.run(['grub-mkrescue', '-o', str(iso), str(tree)],
                       stdout=log, stderr=log, check=True)
    before_target = installer.target_fingerprint(target)
    before_sentinel = installer.sha(sentinel)
    guest = installer.Guest(directory / 'install', target, sentinel, iso)
    try:
        # Two software-emulated SATA disks under OVMF can make storage probe
        # slower than the installer's general 300-second readiness budget.
        guest.wait('[INSTALL] Installer mode ready', timeout=600)
        installer.install(guest, target, sentinel, before_target, before_sentinel)
    finally:
        guest.close()
    if installer.sha(sentinel) != before_sentinel:
        raise RuntimeError('sentinel disk changed')
    print(f'installed BSP2 disk PASS: {target}')


if __name__ == '__main__':
    main()
