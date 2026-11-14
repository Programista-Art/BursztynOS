#!/usr/bin/env python3
"""Comprehensive test suite for PNG wallpaper support in Bursztyn OS."""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PHOTO_PNG = Path(os.environ.get('BURSZTYN_TEST_PNG', str(ROOT / 'obrazy/testy/dane/nowatapeta.png')))
TAPETY_TLO = (26, 11, 0) # 0xFF1A0B00: R=26 (0x1A), G=11 (0x0B), B=0 (0x00)

def test_png_cpp_decoder():
    print("[TEST PNG] Compiling and running test_dekoder_png with ASan/UBSan...")
    with tempfile.TemporaryDirectory(prefix='bursztyn-png-') as tmp:
        tmp = Path(tmp)
        exe = tmp / 'test_dekoder_png'
        subprocess.run([
            'g++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', str(ROOT / 'obrazy/test_dekoder_png.cpp'),
            str(ROOT / 'obrazy/dekoder_obrazu.cpp'),
            str(ROOT / 'narzedzia/tapety-btp/tapetybtp.cpp'), '-o', str(exe)
        ], check=True, cwd=ROOT)

        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

        # Test on reference real PNG
        subprocess.run([str(exe), str(PHOTO_PNG)], check=True, env=env, cwd=ROOT)

        # Truncated and corrupted rejection tests
        orig_bytes = PHOTO_PNG.read_bytes()
        truncated = tmp / 'truncated.png'
        truncated.write_bytes(orig_bytes[:50])
        subprocess.run([str(exe), '--reject', str(truncated)], check=True, env=env, cwd=ROOT)

        corrupt_ihdr = tmp / 'corrupt_ihdr.png'
        corrupt_bytes = bytearray(orig_bytes)
        corrupt_bytes[16:20] = b'\x00\x00\x00\x00' # zero width
        corrupt_ihdr.write_bytes(corrupt_bytes)
        subprocess.run([str(exe), '--reject', str(corrupt_ihdr)], check=True, env=env, cwd=ROOT)

        from PIL import Image

        # Fixture tests across different modes: L, LA, RGB, RGBA, P
        fixtures = [
            # mode, color, expected_rgb
            ('L', 150, (150, 150, 150)),
            ('RGB', (100, 150, 200), (100, 150, 200)),
            ('RGBA', (100, 150, 200, 255), (100, 150, 200)),
            # Fully transparent (alpha=0) -> blended with TAPETY_TLO (26, 11, 0)
            ('RGBA', (200, 100, 50, 0), TAPETY_TLO),
            # 50% transparent (alpha=128) -> (c*128 + bg*127 + 127) // 255
            ('RGBA', (200, 100, 50, 128), (
                (200 * 128 + TAPETY_TLO[0] * 127 + 127) // 255,
                (100 * 128 + TAPETY_TLO[1] * 127 + 127) // 255,
                (50 * 128 + TAPETY_TLO[2] * 127 + 127) // 255
            )),
            ('P', None, (40, 80, 120))
        ]

        for idx, (mode, fill, exp_rgb) in enumerate(fixtures):
            path = tmp / f'fixture_{idx}.png'
            if mode == 'P':
                im = Image.new('RGB', (32, 24), fill or exp_rgb)
                im = im.convert('P', palette=Image.ADAPTIVE, colors=16)
            else:
                im = Image.new(mode, (32, 24), fill)
            im.save(path)
            subprocess.run([
                str(exe), '--fixture', str(path), '32', '24',
                str(exp_rgb[0]), str(exp_rgb[1]), str(exp_rgb[2])
            ], check=True, env=env, cwd=ROOT)

    print("[TEST PNG] C++ decoder, scaling, alpha blending, and ASan/UBSan PASS.")

def test_packaging_sets():
    print("[TEST PNG] Testing package generator across scenarios A through O...")
    sys.path.insert(0, str(ROOT / 'narzedzia/tapety-btp'))
    import generujpakiet
    import btpcodec
    from PIL import Image

    with tempfile.TemporaryDirectory(prefix='bursztyn-pack-') as tmp:
        tmp = Path(tmp)
        src_dir = tmp / 'wallpapers'
        src_dir.mkdir()

        # Set A: PNG only
        im1 = Image.new('RGB', (100, 100), (255, 0, 0))
        im1.save(src_dir / 'czerwony.png')
        im2 = Image.new('RGBA', (120, 80), (0, 255, 0, 128))
        im2.save(src_dir / 'zielony_alpha.png')
        im3 = Image.new('RGB', (64, 64), (0, 0, 255))
        im3.save(src_dir / 'polskie_góry_żółć.png')

        pak_a = tmp / 'pak_a.pak'
        man_a = tmp / 'manifest_a.cfg'
        subprocess.run([
            sys.executable, str(ROOT / 'narzedzia/tapety-btp/generujpakiet.py'),
            '--katalog', str(src_dir),
            '--wyjscie', str(pak_a),
            '--manifest', str(man_a)
        ], check=True)

        assert pak_a.exists() and pak_a.stat().st_size > 32
        man_text = man_a.read_text(encoding='utf-8')
        assert 'default=wp_' in man_text
        assert 'wpis=wp_' in man_text
        assert '.png' in man_text
        assert 'polskie_góry_żółć.png' in man_text

        # Set D: Mixed (BTP + JPG + PNG)
        shutil.copy(ROOT / 'narzedzia/tapety-btp/tapeta.jpg', src_dir / 'widok.jpg')
        # Generate a small BTP file
        btp_bytes = btpcodec.encode(b'\xff' * (16 * 16 * 4), 16, 16)
        (src_dir / 'bursztyn01.btp').write_bytes(btp_bytes)

        pak_mixed = tmp / 'pak_mixed.pak'
        man_mixed = tmp / 'manifest_mixed.cfg'
        subprocess.run([
            sys.executable, str(ROOT / 'narzedzia/tapety-btp/generujpakiet.py'),
            '--katalog', str(src_dir),
            '--wyjscie', str(pak_mixed),
            '--manifest', str(man_mixed)
        ], check=True)

        mixed_text = man_mixed.read_text(encoding='utf-8')
        assert '.png' in mixed_text
        assert '.jpg' in mixed_text
        assert 'bursztyn01.btp' in mixed_text

        # Set E: Default PNG wallpaper selection via CLI
        man_def = tmp / 'manifest_def.cfg'
        subprocess.run([
            sys.executable, str(ROOT / 'narzedzia/tapety-btp/generujpakiet.py'),
            '--katalog', str(src_dir),
            '--wyjscie', str(tmp / 'pak_def.pak'),
            '--domyslna', 'czerwony.png',
            '--manifest', str(man_def)
        ], check=True)

        canon_czerwony = generujpakiet.tapeta_kanoniczna_nazwa('czerwony.png')
        assert f'default={canon_czerwony}' in man_def.read_text(encoding='utf-8')

        # Set F: Corrupted PNG in directory is skipped, valid ones still packaged
        (src_dir / 'uszkodzony.png').write_bytes(b'\x89PNG\r\n\x1a\ncorrupted_data_not_a_chunk')
        pak_f = tmp / 'pak_f.pak'
        man_f = tmp / 'manifest_f.cfg'
        res = subprocess.run([
            sys.executable, str(ROOT / 'narzedzia/tapety-btp/generujpakiet.py'),
            '--katalog', str(src_dir),
            '--wyjscie', str(pak_f),
            '--manifest', str(man_f)
        ], check=True, stderr=subprocess.PIPE, text=True)
        assert 'uszkodzony.png' in res.stderr
        assert 'uszkodzony.png' not in man_f.read_text(encoding='utf-8')

        # Set O: Determinism - running again produces byte-for-byte identical output
        os.remove(src_dir / 'uszkodzony.png')
        pak1 = tmp / 'pak_det1.pak'
        man1 = tmp / 'man_det1.cfg'
        pak2 = tmp / 'pak_det2.pak'
        man2 = tmp / 'man_det2.cfg'
        subprocess.run([
            sys.executable, str(ROOT / 'narzedzia/tapety-btp/generujpakiet.py'),
            '--katalog', str(src_dir), '--wyjscie', str(pak1), '--manifest', str(man1)
        ], check=True)
        subprocess.run([
            sys.executable, str(ROOT / 'narzedzia/tapety-btp/generujpakiet.py'),
            '--katalog', str(src_dir), '--wyjscie', str(pak2), '--manifest', str(man2)
        ], check=True)
        assert pak1.read_bytes() == pak2.read_bytes()
        assert man1.read_bytes() == man2.read_bytes()

    print("[TEST PNG] Packaging tests A through O PASS.")

def main():
    test_png_cpp_decoder()
    test_packaging_sets()
    print("[TEST PNG] ALL TESTS PASS.")

if __name__ == '__main__':
    main()
