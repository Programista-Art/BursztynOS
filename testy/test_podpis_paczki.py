#!/usr/bin/env python3
"""Hermetic test key; production trust configuration is never replaced."""
from pathlib import Path
import ctypes.util
import os
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / '.signature-test'
out.mkdir(exist_ok=True)

def run(args):
    subprocess.run([str(a) for a in args], cwd=root, check=True,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                            UBSAN_OPTIONS='halt_on_error=1'))

run(['python3', 'narzedzia/aktualizacje/generuj_naglowek_klucza.py',
     '--tryb', 'test', '--wyjscie', out / 'aktualizacja_klucz.h'])
run(['openssl', 'dgst', '-sha256', '-sign',
     'narzedzia/aktualizacje/klucz-testowy-prywatny.pem',
     '-out', out / 'test.podpis', 'test_dzwieku.bin'])
database = subprocess.run(['make', '-qp'], cwd=root, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout
includes = next(line.split(':=', 1)[1] for line in database.splitlines()
                if line.startswith('HOST_INCLUDES := '))
library = ctypes.util.find_library('mbedcrypto')
if not library:
    raise RuntimeError('Host libmbedcrypto (Mbed TLS 2.x) is required')
run(['g++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
     '-fno-pie', '-no-pie', '-I.signature-test', *shlex.split(includes),
     '-Ibiblioteki/mbedtls/include', 'testy/test_podpis_paczki_host.cpp',
     '-Wl,-l:' + library, '-o', out / 'test-signature'])
run([out / 'test-signature', 'test_dzwieku.bin', out / 'test.podpis'])
