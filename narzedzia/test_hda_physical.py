#!/usr/bin/env python3
"""Actual HDA transport and scanner on deterministic PCI/MMIO/DMA; no VM."""
import os
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.hda-test' / 'physical'
OUT.mkdir(parents=True, exist_ok=True)
flags = ['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-fno-omit-frame-pointer',
         '-fno-pie', '-no-pie', '-DBURSZTYN_HDA_HOST_TEST', '-fsanitize=address,undefined',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
subprocess.run(flags + ['narzedzia/test_hda_transport.cpp', 'sterowniki/dzwiek/hda_transport.cpp',
                       'sterowniki/dzwiek/hda_diag.cpp', '-o', str(OUT / 'transport')], cwd=ROOT, check=True)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
subprocess.run([str(OUT / 'transport')], cwd=ROOT, env=env, check=True)
