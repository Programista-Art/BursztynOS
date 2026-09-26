#!/usr/bin/env python3
"""Ring 3 decoder host tests, including the unchanged user JPEG."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PHOTO = ROOT / 'narzedzia/tapety-btp/tapeta.jpg'
ORIGINAL_SHA256 = 'd7e181d92652dcf8ffa20155cb77776bd8ae89b47ba773713abfefe150f18438'


def main():
    if hashlib.sha256(PHOTO.read_bytes()).hexdigest() != ORIGINAL_SHA256:
        raise RuntimeError('original JPEG changed before tests')
    with tempfile.TemporaryDirectory(prefix='bursztyn-jpeg-') as tmp:
        tmp = Path(tmp)
        exe = tmp / 'test_dekoder_obrazu'
        subprocess.run([
            'g++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', str(ROOT / 'obrazy/test_dekoder_obrazu.cpp'),
            str(ROOT / 'obrazy/dekoder_obrazu.cpp'),
            str(ROOT / 'narzedzia/tapety-btp/tapetybtp.cpp'), '-o', str(exe)
        ], check=True, cwd=ROOT)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        subprocess.run([str(exe), str(PHOTO)], check=True, env=env, cwd=ROOT)
        original = PHOTO.read_bytes()
        first_sos = original.find(b'\xff\xda')
        if first_sos < 0:
            raise RuntimeError('reference JPEG has no scan')
        scan_start = first_sos + 2 + int.from_bytes(original[first_sos + 2:first_sos + 4], 'big')
        scan_end = scan_start
        while scan_end + 1 < len(original):
            if original[scan_end] == 0xff and original[scan_end + 1] not in (0x00, 0xff) \
                    and not 0xd0 <= original[scan_end + 1] <= 0xd7:
                break
            scan_end += 1
        incomplete = tmp / 'incomplete-progressive-scans.jpg'
        incomplete.write_bytes(original[:scan_end] + b'\xff\xd9')
        subprocess.run([str(exe), '--reject', str(incomplete)], check=True, env=env, cwd=ROOT)
        for index, cut in enumerate((20200, 30000, 100000, 400000, 900000)):
            truncated = tmp / f'early-eoi-{index}.jpg'
            truncated.write_bytes(original[:cut] + b'\xff\xd9')
            subprocess.run([str(exe), '--reject', str(truncated)], check=True, env=env, cwd=ROOT)
        try:
            from PIL import Image
        except ImportError as exc:
            raise RuntimeError('Pillow required for baseline JPEG fixtures') from exc
        for mode, color, suffix, subsampling in [
            ('L', 117, 'gray', None),
            ('RGB', (42, 126, 213), '444', 0),
            ('RGB', (42, 126, 213), '422', 1),
            ('RGB', (42, 126, 213), '420', 2),
        ]:
            path = tmp / f'{suffix}.jpg'
            image = Image.new(mode, (31, 23), color)
            kwargs = {'progressive': False, 'quality': 95}
            if subsampling is not None:
                kwargs['subsampling'] = subsampling
            image.save(path, **kwargs)
            rgb = (color,) * 3 if isinstance(color, int) else color
            subprocess.run([str(exe), '--fixture', str(path), '31', '23',
                            *(str(c) for c in rgb)], check=True, env=env, cwd=ROOT)
        restarted = tmp / 'restart.jpg'
        Image.new('RGB', (256, 256), (42, 126, 213)).save(
            restarted, quality=95, subsampling=2, restart_marker_blocks=4)
        data = restarted.read_bytes()
        if b'\xff\xdd' not in data or b'\xff\xd0' not in data:
            raise RuntimeError('restart fixture not generated')
        subprocess.run([str(exe), '--fixture', str(restarted), '256', '256',
                        '42', '126', '213'], check=True, env=env, cwd=ROOT)
    if hashlib.sha256(PHOTO.read_bytes()).hexdigest() != ORIGINAL_SHA256:
        raise RuntimeError('original JPEG changed during tests')
    print('JPEG source SHA-256 unchanged; ASan/UBSan PASS')


if __name__ == '__main__':
    main()
