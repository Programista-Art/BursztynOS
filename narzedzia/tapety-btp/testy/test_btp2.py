import importlib.util
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import btpcodec
import konwerterbtp
import generujpakiet

def parse_package(data):
    magic, version, count, hsize, table_size, payload_size, total = struct.unpack_from('<8sIIIIII', data)
    entries = []
    cursor = hsize
    for _ in range(count):
        name_len, size, crc, offset = struct.unpack_from('<IIII', data, cursor)
        cursor += 16
        name = data[cursor:cursor + name_len].decode('utf-8')
        cursor += (name_len + 3) & ~3
        entries.append((name, size, crc, offset, data[offset:offset + size]))
    return magic, version, count, hsize, table_size, payload_size, total, entries

def canonical_wallpaper_name(display_name):
    lower = display_name.lower()
    if len(lower) == 14 and lower.startswith('bursztyn') and lower.endswith('.btp'):
        num = lower[8:10]
        if num.isdigit() and 1 <= int(num) <= 10:
            return lower
    h = 0xcbf29ce484222325
    for b in display_name.encode('utf-8'):
        h ^= b
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    ext = display_name.rsplit('.', 1)[-1].lower()
    return f'wp_{h:016x}.{ext}'

def thumbnail_name_for(display_name):
    return 'miniatury/' + canonical_wallpaper_name(display_name).rsplit('.', 1)[0] + '.btp'


class Btp2Tests(unittest.TestCase):
    def test_round_trips(self):
        rng = random.Random(1729)
        for w, h, pixels in (
            (1, 1, b'\0\1\2\xff'),
            (17, 19, b'\x12\x34\x56\xff' * (17 * 19)),
            (73, 21, bytes(rng.randrange(256) for _ in range(73 * 21 * 4))),
            (1366, 3, bytes((i // 4 + i // (1366 * 4)) & 255 for i in range(1366 * 3 * 4))),
        ):
            for size in (32768, 65536, 131072):
                blob = btpcodec.encode(pixels, w, h, chunk_size=size)
                self.assertEqual(btpcodec.decode(blob), (w, h, pixels))

    def test_arbitrary_large_dimensions(self):
        for width, height in ((1366, 768), (1600, 900), (1920, 1080),
                              (1920, 1200), (2560, 1080), (2560, 1440),
                              (3440, 1440), (3840, 2160)):
            with self.subTest(width=width, height=height):
                pixels = bytes(width * height * 4)
                blob = btpcodec.encode(pixels, width, height, method='raw')
                self.assertEqual(btpcodec.decode(blob), (width, height, pixels))

    def test_malformed(self):
        blob = btpcodec.encode(b'\x40\x41\x42\xff' * 1000, 100, 10, method='hybrid', filters=(0,))
        for offset, value in ((0, 0), (4, 3), (6, 0), (8, 0), (12, 0),
                              (16, 9), (17, 9), (18, 1), (20, 0), (36, 0),
                              (40, 0), (44, 0), (52, 0), (60, 0), (64, 0),
                              (68, 1), (72, 0)):
            broken = bytearray(blob)
            broken[offset] = value if broken[offset] != value else value ^ 1
            with self.subTest(offset=offset), self.assertRaises(btpcodec.BtpError):
                btpcodec.decode(broken)
        for n in (0, 1, 70, 72, len(blob) - 1):
            with self.subTest(length=n), self.assertRaises(btpcodec.BtpError):
                btpcodec.decode(blob[:n])
        with self.assertRaises(btpcodec.BtpError):
            btpcodec.decode(blob + b'\0')

    def test_malformed_tokens_and_chunks(self):
        pixels = b'ABCD' * 100
        base = btpcodec.encode(pixels, 100, 1, method='hybrid', filters=(0,), chunk_size=200)

        def change(offset, value, payload_crc=False):
            data = bytearray(base)
            data[offset] = value
            if payload_crc:
                begin = btpcodec.HEADER.unpack_from(data)[13]
                struct.pack_into('<I', data, 64, zlib.crc32(data[begin:]))
            with self.assertRaises(btpcodec.BtpError):
                btpcodec.decode(data)

        # Chunk table offset, overlap, forbidden filter and malformed token.
        change(72, 0)
        change(72 + btpcodec.CHUNK.size, 0)
        change(72 + 17, 3)
        start = btpcodec.HEADER.unpack_from(base)[13]
        change(start, 0xff, True)
        change(start + 1, 0, True)
        # Independently constructed one-chunk streams exercise exact token rules.
        def stream(token_bytes, decoded=4):
            header = btpcodec.HEADER.pack(b'BTP2', 2, 72, 1, 1, 1, 1, 0,
                4, len(token_bytes), 1, 4, 72, 92, zlib.crc32(pixels[:4]),
                zlib.crc32(token_bytes), 0)
            entry = btpcodec.CHUNK.pack(92, len(token_bytes), decoded, 1, 0, 0)
            return header + entry + token_bytes
        for bad in (b'\x03ABC', b'\x40\xff', b'\x80AB',
                    b'\x7f\0\0A', b'\xbf\0\0ABCD', b'\xff\0\0\x01\0',
                    b'\xc0\x00\x00', b'\xc0\x05\x00',
                    b'\xc0\x01\x00', b'\0A', b'\0A\0B'):
            with self.subTest(token=bad), self.assertRaises(btpcodec.BtpError):
                btpcodec.decode(stream(bad))

    def test_host_decoder(self):
        binary = os.environ.get('BTP2_HOST_DECODER')
        if not binary:
            self.skipTest('set BTP2_HOST_DECODER to the compiled host test')
        with tempfile.TemporaryDirectory() as directory:
            for name, width, height, pixels in (
                ('varied', 129, 57, bytes((i * 13 + i // 4) & 255 for i in range(129 * 57 * 4))),
                ('solid', 257, 255, b'\x11\x22\x33\xff' * (257 * 255)),
            ):
                encoded = Path(directory) / f'{name}.btp'
                raw = Path(directory) / f'{name}.raw'
                encoded.write_bytes(btpcodec.encode(pixels, width, height))
                raw.write_bytes(pixels)
                subprocess.run([binary, str(encoded), str(raw)], check=True)

    def test_mixed_package(self):
        with tempfile.TemporaryDirectory() as directory:
            place = Path(directory)
            assets = place / 'assets'
            assets.mkdir()
            pixel = b'\x01\x02\x03\xff'
            (assets / 'legacy.btp').write_bytes(struct.pack('<4sIIBB', b'BTP1', 1, 1, 32, 0) + pixel)
            (assets / 'modern.btp').write_bytes(btpcodec.encode(pixel, 1, 1))
            target = place / 'tapety.pak'
            subprocess.run([sys.executable, '-B', str(ROOT / 'generujpakiet.py'),
                            '--katalog', str(assets), '--wyjscie', str(target),
                            '--dep', str(place / 'tapety.pak.d')], check=True, capture_output=True)
            data = target.read_bytes()
            magic, version, count, hsize, table_size, payload_size, total, entries = parse_package(data)
            self.assertEqual((magic, version, count, hsize, total), (b'BTPK1\0\0\0', 1, 4, 32, len(data)))
            self.assertEqual(hsize + table_size + payload_size, total)
            names = [entry[0] for entry in entries]
            self.assertEqual(names, ['legacy.btp', thumbnail_name_for('legacy.btp'),
                                     'modern.btp', thumbnail_name_for('modern.btp')])
            for name, size, crc, offset, payload in entries:
                self.assertEqual(zlib.crc32(payload), crc)
                if name.startswith('miniatury/'):
                    self.assertEqual(btpcodec.decode(payload)[:2], (generujpakiet.MINIATURA_W, generujpakiet.MINIATURA_H))

    def test_package_generates_jpg_png_btp_thumbnails(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            place = Path(directory)
            assets = place / 'assets'
            assets.mkdir()
            with Image.new('RGB', (400, 200), (10, 40, 90)) as image:
                image.save(assets / 'wall.jpg')
            with Image.new('RGBA', (120, 300), (90, 40, 10, 255)) as image:
                image.save(assets / 'wall.png')
            (assets / 'bursztyn01.btp').write_bytes(btpcodec.encode(b'\x09\x08\x07\xff' * (64 * 36), 64, 36))

            target = place / 'tapety.pak'
            subprocess.run([sys.executable, '-B', str(ROOT / 'generujpakiet.py'),
                            '--katalog', str(assets), '--wyjscie', str(target),
                            '--dep', str(place / 'tapety.pak.d')], check=True, capture_output=True)
            *_, entries = parse_package(target.read_bytes())
            names = [entry[0] for entry in entries]
            originals = [name for name in names if not name.startswith('miniatury/')]
            previews = [name for name in names if name.startswith('miniatury/')]
            self.assertEqual(len(originals), 3)
            self.assertEqual(len(previews), 3)
            self.assertIn('miniatury/bursztyn01.btp', previews)
            expected_previews = {thumbnail_name_for(name) for name in originals}
            self.assertEqual(set(previews), expected_previews)
            for name, _, _, _, payload in entries:
                if name.startswith('miniatury/'):
                    w, h, pixels = btpcodec.decode(payload)
                    self.assertEqual((w, h), (generujpakiet.MINIATURA_W, generujpakiet.MINIATURA_H))
                    self.assertEqual(len(pixels), generujpakiet.MINIATURA_W * generujpakiet.MINIATURA_H * 4)

    def test_thumbnail_cover_has_no_letterbox_for_wide_source(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            place = Path(directory)
            assets = place / 'assets'
            assets.mkdir()
            # Very wide source; cover crop must fill the 16:9 thumbnail with source color,
            # not add transparent/black bars.
            with Image.new('RGBA', (800, 100), (25, 50, 75, 255)) as image:
                image.save(assets / 'wide.png')
            target = place / 'tapety.pak'
            subprocess.run([sys.executable, '-B', str(ROOT / 'generujpakiet.py'),
                            '--katalog', str(assets), '--wyjscie', str(target),
                            '--dep', str(place / 'tapety.pak.d')], check=True, capture_output=True)
            *_, entries = parse_package(target.read_bytes())
            preview = next(payload for name, _, _, _, payload in entries if name.startswith('miniatury/'))
            w, h, pixels = btpcodec.decode(preview)
            self.assertEqual((w, h), (generujpakiet.MINIATURA_W, generujpakiet.MINIATURA_H))
            self.assertNotEqual(pixels[:4], b'\0\0\0\0')
            self.assertNotEqual(pixels[(w * (h - 1)) * 4:(w * (h - 1)) * 4 + 4], b'\0\0\0\0')

    def test_thumbnail_validator_rejects_corrupt_and_wrong_dimensions(self):
        good = btpcodec.encode(b'\x01\x02\x03\xff' * (generujpakiet.MINIATURA_W * generujpakiet.MINIATURA_H),
                               generujpakiet.MINIATURA_W, generujpakiet.MINIATURA_H)
        wrong_size = btpcodec.encode(b'\x01\x02\x03\xff' * (128 * 72), 128, 72)
        corrupt = bytearray(good)
        corrupt[-1] ^= 0x55
        self.assertTrue(generujpakiet.waliduj_miniature_blob(good))
        self.assertFalse(generujpakiet.waliduj_miniature_blob(wrong_size))
        self.assertFalse(generujpakiet.waliduj_miniature_blob(bytes(corrupt)))

    def test_converter_native_and_resize(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            place = Path(directory)
            source = place / 'source.png'
            with Image.new('RGBA', (13, 7), (11, 22, 33, 44)) as image:
                image.save(source)
            native, _ = konwerterbtp.konwertuj_v2(source, place / 'native.btp', verify=True)
            w, h, pixels = btpcodec.decode(native.read_bytes())
            self.assertEqual((w, h, pixels[:4]), (13, 7, bytes((33, 22, 11, 44))))
            exact, _ = konwerterbtp.konwertuj_v2(source, place / 'exact.btp',
                                                   width=31, height=19, fit='contain', verify=True)
            self.assertEqual(btpcodec.decode(exact.read_bytes())[:2], (31, 19))
            bounded, _ = konwerterbtp.konwertuj_v2(source, place / 'bounded.btp',
                                                     max_width=10, max_height=10, verify=True)
            self.assertEqual(btpcodec.decode(bounded.read_bytes())[:2], (10, 5))
            with self.assertRaisesRegex(konwerterbtp.BladKonwersji, '4194304 pixels'):
                konwerterbtp.konwertuj_v2(source, place / 'too-big.btp', width=3840, height=2160)
            self.assertFalse((place / 'too-big.btp').exists())


if __name__ == '__main__':
    unittest.main()
