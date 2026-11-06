#!/usr/bin/env python3
"""Compare native BSF and bitmap fallback using the same instrumented kernel.

Build a private copy with BSF_PROFILE=1 HUSARZ_PERF=1, then pass its ISO and
matching kernel. Only new files under --output are modified; no disks attach.
The bitmap fixture corrupts all packaged TTFs, retaining the normal fallback.
This compares current rendering paths, not historical operating system builds.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def summary(values):
    return dict(samples=values, median=statistics.median(values),
                minimum=min(values), maximum=max(values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / '.bsf-test/native-comparison')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    # Retain the exact instrumented artifacts if the build directory is reused.
    shutil.copy2(args.iso, out / 'profile.iso')
    shutil.copy2(args.kernel, out / 'profile-kernel.bin')
    args.iso = out / 'profile.iso'
    args.kernel = out / 'profile-kernel.bin'
    results = {}
    with tempfile.TemporaryDirectory(prefix='bitmap-', dir=out) as tmp:
        staging = Path(tmp) / 'staging'
        bitmap_iso = out / 'bitmap.iso'
        with (out / 'bitmap-package.log').open('w') as log:
            subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(args.iso.resolve()),
                            '-extract', '/', str(staging)], stdout=log,
                           stderr=subprocess.STDOUT, check=True)
            packaged_kernel = staging / 'boot/system_operacyjny.bin'
            if packaged_kernel.read_bytes() != args.kernel.read_bytes():
                raise RuntimeError('ISO and observer kernel do not match')
            fonts = sorted((staging / 'czcionki/ttf').glob('*.ttf'))
            if not fonts:
                raise RuntimeError('No TTF assets in the supplied ISO')
            for font in fonts:
                font.chmod(font.stat().st_mode | 0o200)
                font.write_bytes(b'INVALID-TRUETYPE' * 3)
            subprocess.run(['grub-mkrescue', '-o', str(bitmap_iso), str(staging)],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        for mode, iso in [('bsf', args.iso.resolve()), ('bitmap', bitmap_iso)]:
            print('Profiling ' + mode, flush=True)
            with (out / (mode + '.log')).open('w') as log:
                subprocess.run([sys.executable, str(ROOT / 'testy/test_husarz_qemu.py'),
                                '--font-profile-only', '--iso', str(iso),
                                '--kernel', str(args.kernel.resolve()),
                                '--output', str(out / mode), '--timeout', '1800'],
                               stdout=log, stderr=subprocess.STDOUT, check=True,
                               timeout=1850)
            result = json.loads((out / mode / 'result.json').read_text())
            if result['result'] != 'PASS':
                raise RuntimeError('Profile did not pass: ' + mode)
            serial = (out / mode / 'serial.log').read_text(errors='replace')
            if mode == 'bitmap' and '[BSF] No usable TTF' not in serial:
                raise RuntimeError('Bitmap fallback was not observed')
            if mode == 'bsf' and any(message in serial for message in
                                    ('[BSF] No usable TTF', '[BSF] Font directory unavailable')):
                raise RuntimeError('The BSF comparison image used bitmap fallback')
            results[mode] = result
    metrics = {}
    for mode, result in results.items():
        # Keep first-use costs separate from the seven warm desktop repaints.
        metrics[mode] = dict(desktop_first_ns=result['desktop'][0]['ns'],
                             desktop_warm_ns=summary([s['ns'] for s in result['desktop'][1:]]),
                             pages={})
        for page in sorted({s['page'] for s in result['samples']}):
            samples = [s for s in result['samples'] if s['page'] == page]
            metrics[mode]['pages'][page] = {
                key: summary([s[key] for s in samples])
                for key in ('navigationms', 'layoutms', 'paintms') if key in samples[0]}
    report = dict(result='PASS', baseline='same-kernel bitmap fallback',
                  kernel_sha256=hashlib.sha256(args.kernel.read_bytes()).hexdigest(),
                  metrics=metrics)
    (out / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
