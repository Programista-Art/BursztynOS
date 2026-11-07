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


if __name__ == '__main__':
    unittest.main()
