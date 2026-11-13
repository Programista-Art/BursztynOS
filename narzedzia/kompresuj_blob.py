#!/usr/bin/env python3
"""Compress an embedded application binary for the kernel image.

Output: b"BZB1" | u32 LE raw size | u32 LE CRC-32 (IEEE) of raw | zlib(level 9).
Decoded by jadro/rdzen/blob_skompresowany.h when the kernel installs the
application (the data stays part of the trusted kernel image)."""
import struct, sys, zlib
src, dst = sys.argv[1], sys.argv[2]
raw = open(src, 'rb').read()
if not raw or len(raw) > 4 * 1024 * 1024:
    raise SystemExit(f'{src}: size {len(raw)} outside 1..4 MiB')
out = b'BZB1' + struct.pack('<II', len(raw), zlib.crc32(raw) & 0xffffffff) + zlib.compress(raw, 9)
# Self-check before the image is linked.
assert zlib.decompress(out[12:]) == raw
open(dst, 'wb').write(out)
