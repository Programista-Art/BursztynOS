#!/usr/bin/env python3
"""CRC-valid tiny PNG with a 4 MiB inflated IDAT; decoder must reject within budget."""
from pathlib import Path
import os,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d))
image=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',128,128,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(4*1024*1024)))+chunk(b'IEND',b'')
assert len(image)<256*1024
with tempfile.TemporaryDirectory(prefix='power-png-') as tmp:
    path=Path(tmp)/'bomb.png';path.write_bytes(image)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(ROOT/'.bsf-test/power-png-decoder'),str(path)],cwd=ROOT,env=env,check=True)
