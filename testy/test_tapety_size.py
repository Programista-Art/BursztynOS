#!/usr/bin/env python3
"""Tapety BUR executable size budget and segment integrity verification."""
import struct
import sys
from pathlib import Path

APP_MAXIMUM = 512 * 1024  # 512 KiB application budget for tapety.bin

def verify(path: Path) -> None:
    size_on_disk = path.stat().st_size
    print(f'Tapety: file={size_on_disk} maximum={APP_MAXIMUM} margin={APP_MAXIMUM-size_on_disk}')
    if not 60 <= size_on_disk <= APP_MAXIMUM:
        raise ValueError('Tapety file exceeds its documented build budget')
    data = path.read_bytes()
    if len(data) != size_on_disk:
        raise ValueError('Executable changed during size validation')
    if data[:4] == b'BUR\x02':
        if len(data) < 160:
            raise ValueError('Truncated BUR v2 header')
        entry = struct.unpack_from('<Q', data, 24)[0]
        off, size, va = struct.unpack_from('<3Q', data, 32)
        doff, dfile, dmem, dva = struct.unpack_from('<4Q', data, 80)
        if off + size > len(data) or doff + dfile > len(data) or dfile > dmem:
            raise ValueError('Invalid BUR v2 file-backed ranges')
        if not va <= entry < va + size:
            raise ValueError('BUR v2 entry outside code')
        return
    magic, entry, off, size, va, doff, dsize, dva = struct.unpack_from('<4s7Q', data)
    if magic != b'BUR\0' or off + size > len(data) or doff > len(data):
        raise ValueError('Invalid or truncated file-backed BUR segments')
    if not (va <= entry < va + size and doff == off + size and dva == va + size):
        raise ValueError('Invalid BUR layout')
    if any(x % 4096 for x in (off, size, va, doff, dsize, dva)):
        raise ValueError('BUR segment alignment')

if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else 'tapety.bin'))
