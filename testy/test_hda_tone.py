#!/usr/bin/env python3
"""Production HDA playback/periodic path with deterministic clock + MMIO."""
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.hda-test' / 'tone'
OUT.mkdir(parents=True, exist_ok=True)
subprocess.run([
    'g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra',
    '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
    '-DBURSZTYN_HDA_HOST_TEST', '-fsanitize=address,undefined',
    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
    'testy/test_hda_tone.cpp', '-o', str(OUT / 'tone'),
], cwd=ROOT, check=True)
subprocess.run([str(OUT / 'tone')], cwd=ROOT, check=True, env=dict(
    os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
    UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
