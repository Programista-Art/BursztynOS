#!/usr/bin/env python3
"""Install into a new private image, verify BSF bytes, apply and reboot.

Uses the ordinary release payload and installer GUI. All disk writes are to
new disposable files under .bsf-test; existing user disks are never attached.
"""
import hashlib
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from instalator.qemu_install import (
    Guest, install, installer_iso, sha, sparse, target_fingerprint,
)
from test_diagnostyka_qemu import read_psf_file

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reset-grid-preferences',action='store_true',help='Initialize only a copied private fixture to Sans 16 before acceptance')
    parser.add_argument('--refresh-grid-payloads',action='store_true',help='Refresh final WM and EFI kernel within existing allocation in an offline private fixture before booting')
    parser.add_argument('--desktop-grid',action='store_true',help='Repeated family transitions and disk-only restart anchor comparison')
    parser.add_argument('--resume',type=Path,help='Verify an already completed private test installation')
    args=parser.parse_args()
    parent = ROOT / '.bsf-test'
    parent.mkdir(exist_ok=True)
    out = args.resume.resolve() if args.resume else Path(tempfile.mkdtemp(prefix='installed-', dir=parent))
    if not out.is_relative_to(parent.resolve()):
        raise RuntimeError('Only private .bsf-test installations can be resumed')
    print('Artifacts:', out, flush=True)
    disk, sentinel = out / 'target.img', out / 'sentinel.img'
    if not args.resume:
        sparse(disk, 4 * 1024**3)
        sparse(sentinel, 64 * 1024**2)
        sentinel_sha = sha(sentinel)
        iso = installer_iso(out)
        guest = Guest(out / 'installer', disk, sentinel, iso)
        try:
            install(guest, disk, sentinel, target_fingerprint(disk), sentinel_sha)
        finally:
            guest.close()
        (out/'installed.json').write_text(json.dumps(dict(sentinel_sha256=sentinel_sha)))
    else:
        log=(out/'installer/serial.log').read_text()
        if '[INSTALL] COMPLETE verified' not in log:
            raise RuntimeError('Private installation has not completed')
        manifest=out/'installed.json'
        if manifest.exists():
            sentinel_sha=json.loads(manifest.read_text())['sentinel_sha256']
        else:
            # Compatibility with an interrupted first run of this harness,
            # whose sentinel was a newly created all-zero 64 MiB image.
            blank=hashlib.sha256()
            for _ in range(64):blank.update(bytes(1024*1024))
            sentinel_sha=blank.hexdigest()
    if sha(sentinel) != sentinel_sha:
        raise AssertionError('Sentinel disk changed')
    assets = {}
    # Compare with the exact staged installation media, even if another build
    # has completed in the workspace while the guest was installing.
    for source in sorted((out / 'iso/czcionki/ttf').iterdir()):
        if not source.is_file():
            continue
        data = read_psf_file(disk, '/czcionki/ttf/' + source.name)
        if data != source.read_bytes():
            raise AssertionError('Installed font asset mismatch: ' + source.name)
        assets[source.name] = hashlib.sha256(data).hexdigest()
    # The on-disk observer uses physical A/B paths; /programy and /system
    # are aliases resolved by PSF in the running guest.
    settings = read_psf_file(disk, '/sloty/a/programy/ustawienia.cebula/ustawienia.bur')
    if settings != (out / 'iso/boot/ustawienia.bur').read_bytes():
        raise AssertionError('Installed Settings payload mismatch')
    if args.refresh_grid_payloads:
        if not args.desktop_grid:raise RuntimeError('Payload refresh is only for a desktop-grid fixture')
        from prepare_desktop_grid_fixture import refresh
        refresh(out,reset_preferences=args.reset_grid_preferences)
    scenarios=(('apply', []),('restart',['--desktop-grid-restore',str(out/'apply')])) if args.desktop_grid else (
        ('apply', ['--expected-font-size', '16']),
        ('restart', ['--expected-font-size', '24', '--font-restore-only']),
    )
    for name,extra in scenarios:
        command = [sys.executable, str(ROOT / 'testy/test_husarz_qemu.py'),
                   '--desktop-grid-only' if args.desktop_grid else '--fonts-only', '--installed-disk', str(disk),
                   '--kernel',str(out/'iso/boot/system_operacyjny.bin'),
                   '--iso',str(out/'installer.iso'),
                   '--output', str(out / name), '--timeout', '1500'] + extra
        with (out / (name + '.log')).open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=1550)
        result = json.loads((out / name / 'result.json').read_text())
        if result['result'] != 'PASS':
            raise AssertionError(result)
        stored = read_psf_file(disk, '/sloty/a/system/bsf.cfg')
        if len(stored) != 24:
            raise AssertionError('Persisted config length')
        magic, version, family, size, generation, checksum = struct.unpack('<6I', stored)
        if (magic != 0x31465342 or version != 1 or size != (16 if args.desktop_grid else 24) or
                checksum != (magic ^ version ^ family ^ size ^ 0xb5f01937)):
            raise AssertionError('Persisted config validation failed')
        print(name + ': disk-only font configuration PASS', flush=True)
    result = dict(result='PASS', assets=assets, settings_bytes=len(settings),
                  apply=True, restart=True, disk_only=True,
                  kernel_sha256=sha(out/'iso/boot/system_operacyjny.bin'),
                  sentinel_sha256=sentinel_sha)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Installed BSF: PASS', flush=True)


if __name__ == '__main__':
    main()
