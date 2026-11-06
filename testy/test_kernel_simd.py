#!/usr/bin/env python3
"""Enforce the eager-FPU ownership contract on kernel text, excluding data blobs."""
import re
import subprocess
import sys
from pathlib import Path
binary = Path(sys.argv[1] if len(sys.argv)>1 else 'system_operacyjny.bin')
text = subprocess.check_output(['objdump','-d',str(binary)],text=True)
forbidden = re.compile(r'%(?:[xyz]mm\d+|mm[0-7]|st(?:\([0-7]\))?)\b|\b(?:fld\w*|fst\w*|fadd\w*|fsub\w*|fmul\w*|fdiv\w*|fcom\w*|fn?init|emms|vzero\w*)\b')
bad = [line for line in text.splitlines() if forbidden.search(line)]
if bad:
    raise SystemExit('Kernel contains forbidden SIMD/x87 instructions:\n'+'\n'.join(bad[:30]))
assert 'fxsave64' in text and 'fxrstor64' in text, 'Missing eager FPU context machinery'
print('Kernel SIMD/x87 exclusion audit: PASS; only explicit FXSAVE/FXRSTOR ownership')
