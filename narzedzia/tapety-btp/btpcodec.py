"""BTP2 wire format and deterministic, independent-chunk LZ/RLE codec."""
from __future__ import annotations

import struct
import zlib

HEADER = struct.Struct('<4sHHIIBBHQQIIQQIII')
CHUNK = struct.Struct('<QIIBBH')
MAX_DIM = 8192
MAX_PIXELS = 8192 * 8192
MAX_CHUNK = 131072
WINDOW = 65535


class BtpError(ValueError):
    pass


def _dimensions(width, height):
    if not (isinstance(width, int) and isinstance(height, int) and
            1 <= width <= MAX_DIM and 1 <= height <= MAX_DIM and
            width * height <= MAX_PIXELS):
        raise BtpError('invalid BTP dimensions')
    return width * height * 4


def _tokens(data: bytes, method: str, window: int) -> bytes:
    out = bytearray()
    literals = bytearray()
    heads: dict[bytes, list[int]] = {}
    n = len(data)

    def flush():
        while literals:
            block = literals[:64]
            del literals[:64]
            out.append(len(block) - 1)
            out.extend(block)

    def remember(pos):
        if pos + 3 <= n:
            key = data[pos:pos + 3]
            chain = heads.setdefault(key, [])
            chain.append(pos)
            if len(chain) > 24:
                del chain[:-24]

    i = 0
    while i < n:
        limit = min(66, n - i)
        byte_run = 1
        if method in ('rle', 'hybrid'):
            while byte_run < min(65535, n - i) and data[i + byte_run] == data[i]:
                byte_run += 1
        pixel_run = 1
        if method in ('rle', 'hybrid') and i + 8 <= n:
            pattern = data[i:i + 4]
            while pixel_run < min(65535, (n - i) // 4) and data[i + pixel_run * 4:i + (pixel_run + 1) * 4] == pattern:
                pixel_run += 1
        match_len = match_dist = 0
        if method in ('lz', 'hybrid') and i + 3 <= n:
            for pos in reversed(heads.get(data[i:i + 3], ())):
                dist = i - pos
                if dist > window:
                    break
                length = 3
                while length < limit and data[pos + length] == data[i + length]:
                    length += 1
                if length > match_len:
                    match_len, match_dist = length, dist
                    if length == limit:
                        break
        # Compare saved bytes against a literal. Ties prefer the cheaper decoder.
        candidates = []
        if byte_run >= 4:
            candidates.append((byte_run - (4 if byte_run >= 66 else 2), 1, 'byte', byte_run))
        if pixel_run >= 3:
            candidates.append((pixel_run * 4 - (7 if pixel_run >= 65 else 5), 2, 'pixel', pixel_run))
        if match_len >= 5:
            candidates.append((match_len - (5 if match_len >= 66 else 3), 0, 'lz', match_len))
        if candidates:
            _, _, kind, length = max(candidates)
            flush()
            if kind == 'byte':
                if length >= 66:
                    out.append(0x7f)
                    out.extend(struct.pack('<H', length))
                else:
                    out.append(0x40 | (length - 3))
                out.append(data[i])
                consumed = length
            elif kind == 'pixel':
                if length >= 65:
                    out.append(0xbf)
                    out.extend(struct.pack('<H', length))
                else:
                    out.append(0x80 | (length - 2))
                out.extend(data[i:i + 4])
                consumed = length * 4
            else:
                if length >= 66:
                    out.append(0xff)
                    out.extend(struct.pack('<H', length))
                else:
                    out.append(0xc0 | (length - 3))
                out.extend(struct.pack('<H', match_dist))
                consumed = length
            for p in range(i, i + consumed):
                remember(p)
            i += consumed
        else:
            literals.append(data[i])
            remember(i)
            i += 1
            if len(literals) == 64:
                flush()
    flush()
    return bytes(out)


def _filter(block: bytes, rowbytes: int, kind: int) -> bytes:
    if kind == 0:
        return block
    distance = 4 if kind == 1 else rowbytes
    result = bytearray(block)
    for i in range(distance, len(block)):
        result[i] = (block[i] - block[i - distance]) & 255
    return bytes(result)


def _unfilter(block: bytearray, rowbytes: int, kind: int) -> None:
    if kind:
        distance = 4 if kind == 1 else rowbytes
        for i in range(distance, len(block)):
            block[i] = (block[i] + block[i - distance]) & 255


def encode(pixels: bytes, width: int, height: int, *, method='auto', chunk_size=65536,
           window=65535, filters=(0,)) -> bytes:
    expected = _dimensions(width, height)
    if len(pixels) != expected:
        raise BtpError('pixel size does not match dimensions')
    if chunk_size < 4 or chunk_size > MAX_CHUNK or chunk_size % 4:
        raise BtpError('chunk size must be a multiple of four, 4..131072')
    if not 1 <= window <= WINDOW:
        raise BtpError('window must be 1..65535')
    if method not in ('auto', 'raw', 'rle', 'lz', 'hybrid'):
        raise BtpError('unknown compression method')
    if not filters or any(f not in (0, 1, 2) for f in filters):
        raise BtpError('unknown filter')
    entries = bytearray()
    payload = bytearray()
    count = (expected + chunk_size - 1) // chunk_size
    payload_offset = HEADER.size + count * CHUNK.size
    for start in range(0, expected, chunk_size):
        block = pixels[start:start + chunk_size]
        choices = [(len(block), 0, 0, block)]
        modes = ('rle', 'lz', 'hybrid') if method == 'auto' else (() if method == 'raw' else (method,))
        for filt in filters:
            filtered = _filter(block, width * 4, filt)
            for mode in modes:
                encoded = _tokens(filtered, mode, window)
                choices.append((len(encoded), 1, filt, encoded))
        _, mode_id, filt_id, selected = min(choices, key=lambda item: item[0])
        entries.extend(CHUNK.pack(payload_offset + len(payload), len(selected), len(block), mode_id, filt_id, 0))
        payload.extend(selected)
    header = HEADER.pack(b'BTP2', 2, HEADER.size, width, height, 1, 1, 0,
                         expected, len(payload), count, chunk_size, HEADER.size,
                         payload_offset, zlib.crc32(pixels), zlib.crc32(payload), 0)
    return header + entries + payload


def decode(blob: bytes) -> tuple[int, int, bytes]:
    if len(blob) < HEADER.size:
        raise BtpError('short header')
    (magic, version, hsize, width, height, fmt, method, flags, raw_size,
     compressed, count, chunk_size, table, payload_start, raw_crc, payload_crc,
     reserved) = HEADER.unpack_from(blob)
    if magic != b'BTP2' or version != 2 or hsize != HEADER.size or fmt != 1 or method != 1 or flags or reserved:
        raise BtpError('unsupported header')
    if raw_size != _dimensions(width, height):
        raise BtpError('decoded size mismatch')
    if not 4 <= chunk_size <= MAX_CHUNK or chunk_size % 4 or count != (raw_size + chunk_size - 1) // chunk_size:
        raise BtpError('invalid chunks')
    if table != HEADER.size or payload_start != table + count * CHUNK.size or payload_start > len(blob) or compressed != len(blob) - payload_start:
        raise BtpError('invalid offsets or trailing data')
    if zlib.crc32(blob[payload_start:]) != payload_crc:
        raise BtpError('payload CRC mismatch')
    output = bytearray()
    cursor = payload_start
    for index in range(count):
        offset, size, decoded, mode, filt, pad = CHUNK.unpack_from(blob, table + index * CHUNK.size)
        required = min(chunk_size, raw_size - index * chunk_size)
        if offset != cursor or not size or decoded != required or mode > 1 or filt > 2 or pad or size > len(blob) - cursor:
            raise BtpError('invalid chunk')
        source = blob[cursor:cursor + size]
        cursor += size
        if mode == 0:
            if size != decoded or filt:
                raise BtpError('raw chunk length mismatch')
            output.extend(source)
            continue
        block = bytearray()
        pos = 0
        while pos < size:
            command = source[pos]
            pos += 1
            kind = command >> 6
            low = command & 63
            length = low + (1, 3, 2, 3)[kind]
            if kind and low == 63:
                if size - pos < 2:
                    raise BtpError('truncated extended token')
                length = struct.unpack_from('<H', source, pos)[0]
                pos += 2
                if length < (0, 66, 65, 66)[kind]:
                    raise BtpError('invalid extended token length')
            amount = length * 4 if kind == 2 else length
            if amount > decoded - len(block):
                raise BtpError('token expands past chunk')
            if kind == 0:
                if length > size - pos:
                    raise BtpError('truncated literal')
                block.extend(source[pos:pos + length]); pos += length
            elif kind == 1:
                if pos == size:
                    raise BtpError('truncated RLE byte')
                block.extend(source[pos:pos + 1] * length); pos += 1
            elif kind == 2:
                if size - pos < 4:
                    raise BtpError('truncated RLE pixel')
                block.extend(source[pos:pos + 4] * length); pos += 4
            else:
                if size - pos < 2:
                    raise BtpError('truncated LZ distance')
                dist = struct.unpack_from('<H', source, pos)[0]; pos += 2
                if not dist or dist > len(block):
                    raise BtpError('LZ distance exceeds chunk history')
                for _ in range(length):
                    block.append(block[-dist])
        if len(block) != decoded:
            raise BtpError('incomplete chunk')
        _unfilter(block, width * 4, filt)
        output.extend(block)
    if cursor != len(blob) or zlib.crc32(output) != raw_crc:
        raise BtpError('decoded CRC or length mismatch')
    return width, height, bytes(output)


def decode_v1(blob: bytes) -> tuple[int, int, bytes]:
    if len(blob) < 14:
        raise BtpError('short BTP1 header')
    magic, width, height, bpp, mode = struct.unpack_from('<4sIIBB', blob)
    if magic != b'BTP1' or bpp != 32 or mode not in (0, 1):
        raise BtpError('unsupported BTP1 header')
    expected = _dimensions(width, height)
    if mode == 0:
        if len(blob) != 14 + expected:
            raise BtpError('invalid BTP1 RAW length')
        return width, height, blob[14:]
    result = bytearray()
    if (len(blob) - 14) % 5:
        raise BtpError('invalid BTP1 RLE length')
    for pos in range(14, len(blob), 5):
        run = blob[pos]
        if not run or run * 4 > expected - len(result):
            raise BtpError('invalid BTP1 run')
        result.extend(blob[pos + 1:pos + 5] * run)
    if len(result) != expected:
        raise BtpError('incomplete BTP1 image')
    return width, height, bytes(result)
