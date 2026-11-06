#!/usr/bin/env python3
"""Small-utility file budget, independently of the 128 MiB BUR ABI maximum."""
import struct
import sys
from pathlib import Path

APP_MAXIMUM = 512 * 1024

def verify(path: Path) -> None:
    size_on_disk = path.stat().st_size
    print(f'Test dźwięku: file={size_on_disk} maximum={APP_MAXIMUM} margin={APP_MAXIMUM-size_on_disk}')
    if not 60 <= size_on_disk <= APP_MAXIMUM:
        raise ValueError('Test dźwięku file exceeds its documented build budget')
    data = path.read_bytes()
    if len(data) != size_on_disk:
        raise ValueError('Executable changed during size validation')
    magic, entry, off, size, va, doff, dsize, dva = struct.unpack_from('<4s7Q', data)
    if magic != b'BUR\0' or off + size > len(data) or doff > len(data):
        raise ValueError('Invalid or truncated file-backed BUR segments')
    if not (va <= entry < va + size and doff == off + size and dva == va + size):
        raise ValueError('Invalid BUR layout')
    if any(x % 4096 for x in (off, size, va, doff, dsize, dva)):
        raise ValueError('BUR segment alignment')

if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else 'test_dzwieku.bin'))
