#!/usr/bin/env python3
"""Convert independent static PIE ELF64 into BUR v2; no host runtime is shipped."""
import argparse
import hashlib
import json
import pathlib
import struct
import sys

PAGE = 4096
MAX_IMAGE = 128 * 1024 * 1024


def align(n):
    return (n + PAGE - 1) & -PAGE


def convert(data):
    def take(off, size):
        if off < 0 or size < 0 or off > len(data) or size > len(data) - off:
            raise ValueError('ELF range exceeds file')
        return data[off:off+size]

    def unpack(fmt, off):
        return struct.unpack(fmt, take(off, struct.calcsize(fmt)))

    h = unpack('<16sHHIQQQIHHHHHH', 0)
    if h[0][:7] != b'\x7fELF\x02\x01\x01' or h[1:4] != (3, 62, 1):
        raise ValueError('expected little-endian x86-64 ET_DYN PIE')
    if h[9] != 56 or h[11] != 64 or not h[12] or h[13] >= h[12]:
        raise ValueError('unsupported ELF table geometry')
    for i in range(h[10]):
        ph = unpack('<IIQQQQQQ', h[5] + i * 56)
        if ph[0] == 3:
            raise ValueError('PT_INTERP: host dynamic runtime is forbidden')
    sections = [unpack('<IIQQQQIIQQ', h[6]+i*64) for i in range(h[12])]
    table = sections[h[13]]
    names = take(table[4], table[5])
    named = {}
    for s in sections:
        if s[0] >= len(names):
            raise ValueError('invalid ELF section name')
        name = names[s[0]:].split(b'\0', 1)[0].decode('ascii')
        if name in named and name:
            raise ValueError('duplicate section ' + name)
        named[name] = s
    for name, s in named.items():
        if s[1] == 6:
            for off in range(0, s[5], 16):
                tag, value = unpack('<qQ', s[4]+off)
                if tag in (1, 22):
                    raise ValueError('DT_NEEDED/DT_TEXTREL is unsupported')
        if s[2] & 2 and s[5] and name not in {
            '.text', '.rodata', '.ssp_guard', '.data', '.bss',
            '.rela.dyn', '.dynamic', '.dynsym', '.dynstr', '.hash'}:
            raise ValueError('unsupported allocated section ' + name)
    text, ro, guard, rw, bss = [named.get(k) for k in
                               ('.text', '.rodata', '.ssp_guard', '.data', '.bss')]
    if not all((text, guard, rw)) or guard[5] != PAGE:
        raise ValueError('missing text/data/dedicated canary page')
    for s in (text, ro, guard, rw, bss):
        if s and (s[3] % PAGE or s[5] % PAGE):
            raise ValueError('segment address/size is not page aligned')
    if bss and bss[3] != rw[3] + rw[5]:
        raise ValueError('non-contiguous DATA/BSS')
    mem = rw[5] + (bss[5] if bss else 0)
    if rw[3] + mem > MAX_IMAGE or not (text[3] <= h[4] < text[3]+text[5]):
        raise ValueError('image too large or entry outside code')
    # Read-only canary references must resolve to its first word.
    guard_symbol = None
    for s in sections:
        if s[1] not in (2, 11):
            continue
        st = sections[s[6]]
        strings = take(st[4], st[5])
        if s[9] != 24 or s[5] % 24:
            raise ValueError('invalid symbol table')
        for off in range(0, s[5], 24):
            sym = unpack('<IBBHQQ', s[4]+off)
            name = strings[sym[0]:].split(b'\0', 1)[0]
            if name == b'__stack_chk_guard':
                guard_symbol = sym[4]
    if guard_symbol != guard[3]:
        raise ValueError('canary symbol must begin dedicated page')
    relocs = []
    for s in sections:
        if s[1] == 9 and s[5]:
            raise ValueError('REL relocations unsupported; expected RELA')
        if s[1] != 4 or not s[5]:
            continue
        if s[9] != 24 or s[5] % 24:
            raise ValueError('invalid relocation record geometry')
        for off in range(0, s[5], 24):
            target, info, addend = unpack('<QQq', s[4]+off)
            kind, symbol = info & 0xffffffff, info >> 32
            if kind != 8 or symbol:
                raise ValueError(f'unsupported dynamic relocation type={kind} symbol={symbol} target={target:#x}; only R_X86_64_RELATIVE is supported')
            if target % 8 or not (rw[3] <= target <= rw[3] + mem - 8):
                raise ValueError(f'relocation target outside aligned DATA: {target:#x}')
            valid = any(seg and seg[3] <= addend < seg[3]+seg[5]
                        for seg in (text, ro, guard)) or rw[3] <= addend <= rw[3]+mem
            if not valid:
                raise ValueError(f'relocation addend outside image: {addend:#x}')
            relocs.append((target, addend, 1, 0))
    relocs.sort()
    if any(b[0] < a[0]+8 for a, b in zip(relocs, relocs[1:])):
        raise ValueError('overlapping relocations')
    out = bytearray(PAGE)
    def append(s):
        if not s or not s[5]:
            return 0, 0, 0
        offset = len(out)
        out.extend(take(s[4], s[5]))
        return offset, s[5], s[3]
    tx = append(text)
    rd = append(ro)
    dt = append(rw) if rw[5] else (len(out), 0, rw[3])
    reloc_offset = len(out) if relocs else 0
    for r in relocs:
        out.extend(struct.pack('<QqII', *r))
    header = struct.pack('<4sHHHHIIIQ', b'BUR\x02', 2, 160, 62, 1, 31, 7, PAGE, h[4])
    header += struct.pack('<3Q3Q4Q', *tx, *rd, dt[0], dt[1], mem, rw[3])
    header += struct.pack('<QIHHQIIQ', reloc_offset, len(relocs), 24 if relocs else 0,
                          16, guard[3], 0, 0, 0)
    assert len(header) == 152
    checksum = 14695981039346656037
    for b in header:
        checksum = ((checksum ^ b) * 1099511628211) & ((1 << 64)-1)
    header += struct.pack('<Q', checksum)
    out[:160] = header
    if len(out) > MAX_IMAGE:
        raise ValueError('BUR file exceeds maximum image size')
    return bytes(out), len(relocs)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('elf', type=pathlib.Path)
    ap.add_argument('bur', type=pathlib.Path)
    args = ap.parse_args()
    try:
        data = args.elf.read_bytes()
        result, count = convert(data)
    except (OSError, ValueError, struct.error, IndexError) as exc:
        ap.exit(1, f'BUR v2 conversion failed: {exc}\n')
    args.bur.write_bytes(result)
    # Host-private association; not copied into ISO/CEBULA.
    archive = pathlib.Path('.release-private/apps') / hashlib.sha256(result).hexdigest()
    archive.mkdir(parents=True, exist_ok=True)
    (archive / 'app.elf').write_bytes(data)
    (archive / 'manifest.json').write_text(json.dumps(dict(
        bur_sha256=hashlib.sha256(result).hexdigest(),
        elf_sha256=hashlib.sha256(data).hexdigest(), relocations=count), indent=2)+'\n')
    print(f'BUR v2: {args.bur} bytes={len(result)} relocations={count}')


if __name__ == '__main__':
    main()
