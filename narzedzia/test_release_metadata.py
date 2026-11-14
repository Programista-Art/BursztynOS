#!/usr/bin/env python3
"""Exercise release gate rejection paths without placing fixtures in the ISO."""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from verifyreleasemetadata import inspect


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def kinds(self):
        return {f['kind'] for f in inspect(self.root)['findings']}

    def test_secret_redaction_and_diagnostics(self):
        token = b'ghp_' + b'A' * 36
        (self.root / 'payload.bin').write_bytes(token + b'\0[ABS MAP]')
        result = inspect(self.root)
        self.assertEqual({f['kind'] for f in result['findings']},
                         {'secret-pattern', 'diagnostic-marker'})
        self.assertNotIn(token.decode(), str(result))

    def test_build_artifacts_and_paths(self):
        (self.root / 'private.map').write_bytes(b'/home/developer/repo/kernel.cpp')
        self.assertEqual(self.kinds(), {'development-file', 'source-path'})

    def test_private_key_parser_text_allowed(self):
        (self.root / 'parser.bin').write_bytes(b'-----BEGIN PRIVATE KEY-----\0')
        self.assertEqual(self.kinds(), set())

    def test_complete_key_payload_rejected_without_disclosure(self):
        payload = b'Q' * 64
        (self.root / 'payload.bin').write_bytes(
            b'-----BEGIN PRIVATE KEY-----\n' + payload +
            b'\n-----END PRIVATE KEY-----')
        result = inspect(self.root)
        self.assertEqual({f['kind'] for f in result['findings']}, {'secret-pattern'})
        self.assertNotIn(payload.decode(), str(result))

    def test_missing_and_empty_artifacts_cannot_pass_cli(self):
        script = pathlib.Path(__file__).with_name('verifyreleasemetadata.py')
        for target in (self.root / 'missing.iso', self.root / 'empty'):
            if target.name == 'empty': target.mkdir()
            result = subprocess.run(
                [sys.executable, str(script), str(target), '--output',
                 str(self.root / 'report.json')], capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_grub_exception_requires_relocatable_runtime_module(self):
        directory = self.root / 'boot/grub/x86_64-efi'
        directory.mkdir(parents=True)
        module = directory / 'sample.mod'
        subprocess.run(['cc', '-x', 'c', '-c', '-o', str(module), '-'],
                       input=b'int sample(void) { return 1; }', check=True)
        self.assertEqual(self.kinds(), set())
        shutil.copy2(module, directory / 'kernel.bin')
        self.assertIn('symbol-table', self.kinds())
        (directory / 'kernel.bin').unlink()
        data = bytearray(module.read_bytes())
        struct.pack_into('<H', data, 16, 2)  # ET_EXEC cannot use module exception.
        module.write_bytes(data)
        self.assertIn('symbol-table', self.kinds())

    def test_efi_debug_directory_and_coff_symbols(self):
        data = bytearray(512)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 64)
        data[64:68] = b'PE\0\0'
        struct.pack_into('<II', data, 76, 400, 1)
        struct.pack_into('<H', data, 88, 0x20b)
        struct.pack_into('<I', data, 196, 16)
        struct.pack_into('<II', data, 248, 4096, 28)
        (self.root / 'BOOTX64.EFI').write_bytes(data)
        self.assertEqual(self.kinds(), {'symbol-table', 'debug-section'})

    def test_truncated_executable_fails_closed(self):
        (self.root / 'kernel.bin').write_bytes(b'\x7fELF')
        self.assertEqual(self.kinds(), {'malformed-executable'})

    def test_efi_filesystem_members_are_scanned(self):
        image = self.root / 'efi.img'
        subprocess.run(['mformat', '-C', '-i', str(image), '-f', '1440', '::'],
                       check=True, capture_output=True)
        with tempfile.TemporaryDirectory() as td:
            payload = pathlib.Path(td) / 'private.map'
            payload.write_bytes(b'private symbol map')
            subprocess.run(['mcopy', '-i', str(image), str(payload), '::/'],
                           check=True, capture_output=True)
        result = inspect(self.root)
        self.assertIn('efi.img!/private.map',
                      {f['path'] for f in result['inventory']})
        self.assertIn({'path': 'efi.img!/private.map', 'kind': 'development-file'},
                      result['findings'])

    def test_invalid_efi_filesystem_cannot_pass(self):
        (self.root / 'efi.img').write_bytes(b'invalid filesystem')
        self.assertIn('efi-extraction-failed', self.kinds())


def _bzp3(entries):
    """Buduje kontener BZP3 z listy (nazwa, bajty) - tak jak package_galeria.py."""
    import hashlib, zlib
    tabela_dl = sum(1 + len(n.encode()) + 12 for n, _ in entries)
    poz, tabela = 48 + tabela_dl, bytearray()
    for n, d in entries:
        nb = n.encode()
        tabela += bytes([len(nb)]) + nb + struct.pack('<III', len(d), poz, zlib.crc32(d) & 0xFFFFFFFF)
        poz += len(d)
    return b'BZP3' + struct.pack('<III', 1, len(entries), len(tabela)) + hashlib.sha256(bytes(tabela)).digest() + bytes(tabela) + b''.join(d for _, d in entries)


def _png(extra_chunks=b'', idat=b'\x78\x9c\x62\x00\x00\x00\x02\x00\x01'):
    import zlib
    def chunk(t, body):
        return struct.pack('>I', len(body)) + t + body + struct.pack('>I', zlib.crc32(t + body) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 0, 0, 0, 0)) +
            extra_chunks + chunk(b'IDAT', idat) + chunk(b'IEND', b''))


def _jpeg(comment=b'', entropy=b'\x00\x01\x02'):
    seg = lambda m, b: b'\xff' + bytes([m]) + struct.pack('>H', len(b) + 2) + b
    return (b'\xff\xd8' + (seg(0xFE, comment) if comment else b'') + seg(0xC0, b'\x08\x00\x01\x00\x01\x01\x01\x11\x00') +
            seg(0xDA, b'\x01\x01\x00\x00\x3f\x00') + entropy + b'\xff\xd9')


class GalleryContainerTests(unittest.TestCase):
    """Kontener galerii: metadane skanowane, piksele walidowane strukturalnie, nic nie znika po cichu."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        (self.root / 'boot').mkdir()

    def scan(self, entries=None, raw=None):
        (self.root / 'boot/zdjecia.pak').write_bytes(raw if raw is not None else _bzp3(entries))
        return inspect(self.root)

    def kinds(self, result):
        return {f['kind'] for f in result['findings']}

    def test_valid_container_passes_and_reports_scanned_regions(self):
        result = self.scan([('a.png', _png()), ('b.jpg', _jpeg(b'komentarz'))])
        self.assertEqual(self.kinds(result), set())
        inv = [i for i in result['inventory'] if i['path'] == 'boot/zdjecia.pak'][0]
        self.assertEqual(inv['gallery']['container'], 'BZP3')
        self.assertEqual(inv['gallery']['entries'], 2)

    def test_pixel_data_is_not_scanned_as_text_but_metadata_is(self):
        # Losowe bajty pikseli przypadkiem zawierajace tekst wzorca nie sa raportowane (walidacja strukturalna)...
        self.assertEqual(self.kinds(self.scan([('a.png', _png(idat=b'/tmp/build/x/y/z'))])), set())
        self.assertEqual(self.kinds(self.scan([('a.jpg', _jpeg(entropy=b'/home/dev/secret/file'))])), set())
        # ...ale ten sam tekst w metadanych (chunk tEXt / komentarz JPEG) JEST raportowany.
        import zlib
        text = b'tEXt'; body = b'Comment\0/home/dev/repo/photo'
        chunk = struct.pack('>I', len(body)) + text + body + struct.pack('>I', zlib.crc32(text + body) & 0xFFFFFFFF)
        self.assertIn('source-path', self.kinds(self.scan([('a.png', _png(extra_chunks=chunk))])))
        self.assertIn('source-path', self.kinds(self.scan([('a.jpg', _jpeg(comment=b'/home/dev/repo/photo'))])))
        token = b'ghp_' + b'B' * 36
        self.assertIn('secret-pattern', self.kinds(self.scan([('a.jpg', _jpeg(comment=token))])))

    def test_file_names_are_scanned(self):
        token = 'ghp_' + 'C' * 36
        result = self.scan([(token + '.png', _png())])
        self.assertIn('secret-pattern', self.kinds(result))
        self.assertNotIn(token, str(result))

    def test_invalid_entry_is_reported_and_scanned_raw(self):
        broken = b'\xff\xd8' + b'no markers here /home/dev/repo/x'
        result = self.scan([('bad.jpg', broken)])
        self.assertIn('gallery-entry-invalid', self.kinds(result))
        self.assertIn('source-path', self.kinds(result))            # surowe bajty zepsutego wpisu sa skanowane
        bad_png = _png()[:-4] + b'\0\0\0\0'                       # zly CRC IEND
        self.assertIn('gallery-entry-invalid', self.kinds(self.scan([('bad.png', bad_png)])))

    def test_tampered_or_malformed_container_is_reported_and_scanned_raw(self):
        pak = bytearray(_bzp3([('a.png', _png())]))
        pak[-3] ^= 0xFF                                              # uszkodzone dane => CRC
        self.assertIn('gallery-container-invalid', self.kinds(self.scan(raw=bytes(pak))))
        pak = bytearray(_bzp3([('a.png', _png())]))
        pak[60] ^= 0x01                                              # uszkodzona tabela => SHA-256
        self.assertIn('gallery-container-invalid', self.kinds(self.scan(raw=bytes(pak))))
        for entries in ([('../evil.png', _png())], [('a/b.png', _png())], [('a.exe', _png())],
                        [('a.png', _png()), ('A.PNG', _png())]):
            self.assertIn('gallery-container-invalid', self.kinds(self.scan(entries)), entries[0][0])
        raw = _bzp3([('a.png', _png())]) + b'/home/dev/leak'
        result = self.scan(raw=raw)                                  # dane po ostatnim wpisie: nie sa "ogonem" do ukrycia
        self.assertIn('gallery-container-invalid', self.kinds(result))
        self.assertIn('source-path', self.kinds(result))

    def test_other_files_are_still_scanned_as_text(self):
        (self.root / 'boot/other.pak').write_bytes(b'BZP3' + b'/home/dev/repo/x' + b'\0' * 64)
        self.assertIn('source-path', self.kinds(inspect(self.root)))


if __name__ == '__main__':
    unittest.main()
