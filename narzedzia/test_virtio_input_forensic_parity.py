#!/usr/bin/env python3
"""Real compilation and deterministic queue execution; never run an emulator."""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.virtiotest/forensic-validation'
OUT.mkdir(exist_ok=True)


def without_emitter(source):
    ns = source.index('namespace VirtioInputForensic {')
    start = source.index('inline void dump()', ns)
    left = source.index('{', start)
    depth = 1
    for right in range(left + 1, len(source)):
        depth += (source[right] == '{') - (source[right] == '}')
        if depth == 0:
            return source[:left] + '{}' + source[right + 1:]
    raise AssertionError('missing diagnostic emitter boundary')


def run(args):
    result = subprocess.run(args, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                                     UBSAN_OPTIONS='halt_on_error=1'))
    return result


files = ['virtio_input.cpp', 'input_device.cpp', 'grafika.cpp', 'kernel.cpp', 'virtqueue.cpp']
for name in files:
    versions = []
    for flag in (0, 1):
        p = run(['g++', '-E', '-P', '-std=gnu++17', '-I.', '-Ibiblioteki/mbedtls/include',
                 '-Ibiblioteki/mbedtls', '-DMBEDTLS_USER_CONFIG_FILE="bursztyn_mbedtls_config.h"',
                 f'-DBURSZTYNDIAGVIRTIOINPUTFORENSIC={flag}', name])
        assert p.returncode == 0, p.stdout[-5000:]
        versions.append(without_emitter(p.stdout) if 'namespace VirtioInputForensic {' in p.stdout else p.stdout)
    assert versions[0] == versions[1], f'functional compilation differs: {name}'
    print(f'FORENSIC COMPILE PARITY {name}: identical outside diagnostic emitter', flush=True)

outputs = []
for flag in (0, 1):
    p = run(['make', 'test-virtio-input-forensic-host', f'BURSZTYNDIAGVIRTIOINPUTFORENSIC={flag}'])
    (OUT / f'parity-{flag}.log').write_text(p.stdout)
    assert p.returncode == 0, p.stdout[-7000:]
    rows = [line for line in p.stdout.splitlines() if line.startswith('FORENSIC ') and not line.startswith('FORENSIC DIAGNOSTIC')]
    assert rows and re.search(r'failures=0\b', rows[-1]), p.stdout[-7000:]
    outputs.append(rows)
    print(f'FORENSIC EXECUTION {flag}: {rows[-1]}', flush=True)
assert outputs[0] == outputs[1], 'diagnostic mode changes functional queue output'
result = 'FORENSIC parity PASS: 5 production translation units identical outside one emission function; ordered queue/reference records identical with diagnostics off/on. ASan and UBSan passed.\n'
(OUT / 'diagnostic-parity.txt').write_text(result + '\n'.join(outputs[0]) + '\n')
print(result, end='')
