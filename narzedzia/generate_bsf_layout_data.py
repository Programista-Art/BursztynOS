#!/usr/bin/env python3
"""Generate BSF 2.0 text-layout properties from the pinned Unicode 15.1.0 UCD.

Output: bsf/unicode_layout_data.inc (freestanding C++ tables, no allocations).
Inputs live in narzedzia/unicode/15.1.0 and are hashed into SHA256.json.
Tables use the same packed 7-byte range format as bsf/unicode_data.inc:
  first(24-bit LE) last(24-bit LE) value(8-bit)
"""
from pathlib import Path
import hashlib, json

root = Path(__file__).resolve().parents[1]
src = root / 'narzedzia/unicode/15.1.0'

BIDI = ['L', 'R', 'AL', 'EN', 'ES', 'ET', 'AN', 'CS', 'NSM', 'BN', 'B', 'S', 'WS', 'ON',
        'LRE', 'LRO', 'RLE', 'RLO', 'PDF', 'LRI', 'RLI', 'FSI', 'PDI']
JOINING = ['U', 'R', 'D', 'C', 'L', 'T']
LINEBREAK = ['XX', 'BK', 'CR', 'LF', 'NL', 'SP', 'ZW', 'GL', 'CM', 'ZWJ', 'WJ', 'BA', 'BB', 'HY',
             'CB', 'CL', 'CP', 'EX', 'IN', 'NS', 'OP', 'QU', 'IS', 'NU', 'PO', 'PR', 'SY', 'AI',
             'AL', 'CJ', 'EB', 'EM', 'H2', 'H3', 'HL', 'ID', 'JL', 'JV', 'JT', 'RI', 'SA', 'SG',
             'AK', 'AP', 'AS', 'VF', 'VI', 'B2']


def fields_of(name):
    for line in (src / name).read_text(encoding='utf-8').splitlines():
        f = [x.strip() for x in line.split('#')[0].split(';')]
        if len(f) >= 2 and f[0]:
            r = f[0].split('..')
            yield int(r[0], 16), int(r[-1], 16), f


def ranges(rows):
    rows = sorted(rows)
    merged = []
    for a, b, v in rows:
        if merged and merged[-1][1] + 1 == a and merged[-1][2] == v:
            merged[-1] = (merged[-1][0], b, v)
        else:
            merged.append((a, b, v))
    return merged


def packed(name, rows, default=None):
    rows = [r for r in ranges(rows) if r[2] != default]
    data = []
    for a, b, v in rows:
        assert 0 <= v < 256
        data += [a & 255, (a >> 8) & 255, a >> 16, b & 255, (b >> 8) & 255, b >> 16, v]
    out = f'static const uint8_t {name}[] = {{\n'
    for i in range(0, len(data), 28):
        out += ','.join(map(str, data[i:i + 28])) + ',\n'
    return out + '};\n', len(rows)


def triples(name, rows):
    out = f'static const uint32_t {name}[][{len(rows[0])}] = {{\n'
    for r in rows:
        out += '{' + ','.join(f'0x{x:x}' for x in r) + '},\n'
    return out + '};\n'


# Bidi_Class: apply @missing defaults in file order, then explicit assignments.
LONG = {'Left_To_Right': 'L', 'Right_To_Left': 'R', 'Arabic_Letter': 'AL',
        'European_Terminator': 'ET', 'Boundary_Neutral': 'BN'}
values = bytearray(0x110000)
for line in (src / 'extracted_DerivedBidiClass.txt').read_text().splitlines():
    if line.startswith('# @missing:'):
        r, v = [x.strip() for x in line[len('# @missing:'):].split(';')]
        a, b = [int(x, 16) for x in r.split('..')]
        values[a:b + 1] = bytes([BIDI.index(LONG[v])]) * (b - a + 1)
for a, b, f in fields_of('extracted_DerivedBidiClass.txt'):
    values[a:b + 1] = bytes([BIDI.index(f[1])]) * (b - a + 1)
bidi = []
start = 0
for cp in range(1, 0x110001):
    if cp == 0x110000 or values[cp] != values[start]:
        bidi.append((start, cp - 1, values[start]))
        start = cp

joining = [(a, b, JOINING.index(f[1])) for a, b, f in fields_of('extracted_DerivedJoiningType.txt')]

scripts = ['Zyyy', 'Zinh', 'Zzzz']
aliases = {}
for line in (src / 'PropertyValueAliases.txt').read_text().splitlines():
    f = [x.strip() for x in line.split('#')[0].split(';')]
    if len(f) >= 3 and f[0] == 'sc':
        aliases[f[2]] = f[1]
script_rows = []
for a, b, f in fields_of('Scripts.txt'):
    short = aliases[f[1]]
    if short not in scripts:
        scripts.append(short)
    script_rows.append((a, b, scripts.index(short)))
assert len(scripts) < 256

lb = []
for a, b, f in fields_of('LineBreak.txt'):
    lb.append((a, b, LINEBREAK.index(f[1])))

marks = []
decomp, singles = [], []
names = {}
excluded = set()
for line in (src / 'CompositionExclusions.txt').read_text().splitlines():
    field = line.split('#')[0].strip()
    if field:  # entries carry no ';' separator
        r = field.split('..')
        excluded.update(range(int(r[0], 16), int(r[-1], 16) + 1))
assert 0xfb2a in excluded and 0x0958 in excluded
ccc = {}
records = []
for line in (src / 'UnicodeData.txt').read_text().splitlines():
    f = line.split(';')
    cp = int(f[0], 16)
    records.append((cp, f))
    ccc[cp] = int(f[3])
for cp, f in records:
    if f[2] in ('Mn', 'Mc', 'Me'):
        marks.append((cp, cp, {'Mn': 1, 'Mc': 2, 'Me': 3}[f[2]]))
    d = f[5]
    if d and not d.startswith('<'):
        parts = [int(x, 16) for x in d.split()]
        if len(parts) == 2:
            decomp.append((cp, parts[0], parts[1]))
        else:
            singles.append((cp, parts[0]))
# Hangul syllables are algorithmic and are handled in code.
compose = sorted((a, b, cp) for cp, a, b in decomp
                 if cp not in excluded and ccc.get(a, 0) == 0 and ccc.get(cp, 0) == 0)

brackets = []
for a, b, f in fields_of('BidiBrackets.txt'):
    brackets.append((a, int(f[1], 16), 1 if f[2] == 'o' else 2))
mirror = sorted((a, int(f[1], 16)) for a, b, f in fields_of('BidiMirroring.txt'))

# ---------------------------------------------------------------- blob
# One packed, position-independent blob. Ring 3 receives it from the kernel
# through BWS 51 (command 2) instead of linking 60+ KiB into every app.
# Range tables: first(3) last(3) value(1). Scalar tuples: 3-byte LE each.
def ranges_bytes(rows, default=None):
    out = bytearray()
    for a, b, v in ranges(rows):
        if v == default:
            continue
        out += bytes([a & 255, (a >> 8) & 255, a >> 16, b & 255, (b >> 8) & 255, b >> 16, v])
    return bytes(out)
def cp3(v):
    assert 0 <= v < 1 << 24
    return bytes([v & 255, (v >> 8) & 255, v >> 16])
tables = [
    ('bidi', ranges_bytes(bidi, 0), 7),
    ('joining', ranges_bytes(joining, 0), 7),
    ('script', ranges_bytes(script_rows, None), 7),
    ('linebreak', ranges_bytes(lb, 28), 7),
    ('mark', ranges_bytes(marks, None), 7),
    ('brackets', b''.join(cp3(a) + cp3(b) + bytes([t]) for a, b, t in sorted(brackets)), 7),
    ('mirror', b''.join(cp3(a) + cp3(b) for a, b in mirror), 6),
    ('decompose', b''.join(cp3(c) + cp3(a) + cp3(b) for c, a, b in sorted(decomp)), 9),
    ('decompose_single', b''.join(cp3(c) + cp3(a) for c, a in sorted(singles)), 6),
    ('compose', b''.join(cp3(a) + cp3(b) + cp3(c) for a, b, c in compose), 9),
    ('script_names', b''.join(x.encode() for x in scripts), 4),
]
MAGIC, VERSION = 0x55465342, 1  # 'BSFU'
header_size = 16 + 8 * len(tables)
blob = bytearray()
index = []
offset = header_size
for name, data, rec in tables:
    assert len(data) % rec == 0
    index.append((name, offset, len(data), rec))
    blob += data
    offset += len(data)
total = header_size + len(blob)
head = bytearray()
for v in (MAGIC, VERSION, total, 0x0f0100):  # Unicode 15.1.0
    head += v.to_bytes(4, 'little')
for name, off, size, rec in index:
    head += off.to_bytes(4, 'little') + size.to_bytes(4, 'little')
blob = bytes(head) + bytes(blob)
assert len(blob) == total
inc = ('// Generated by narzedzia/generate_bsf_layout_data.py; Unicode 15.1.0.\n'
       '// Unicode data license: narzedzia/unicode/LICENSE.txt\n'
       '// Compiled ONLY into the kernel (served by BWS 51/2) and host tests.\n'
       f'alignas(8) static const uint8_t bsf_unicode_layout_blob[{total}] = {{\n')
for i in range(0, total, 32):
    inc += ','.join(map(str, blob[i:i + 32])) + ',\n'
inc += '};\n'
(root / 'bsf/unicode_layout_data.inc').write_text(inc)
hdr = ('#pragma once\n// Generated by narzedzia/generate_bsf_layout_data.py; Unicode 15.1.0.\n'
       '// Index of the packed BSF Unicode layout blob (no data in this header).\n'
       '#include <stdint.h>\nnamespace BSF {\nnamespace UnicodeBlob {\n'
       f'constexpr uint32_t Magic = 0x{MAGIC:08x}, Version = {VERSION}, Size = {total}, UnicodeVersion = 0x0f0100;\n'
       f'constexpr unsigned TableCount = {len(tables)};\n'
       'enum Table : unsigned { ' + ', '.join('Table' + n.title().replace('_', '') for n, *_ in tables) + ' };\n'
       'constexpr uint32_t TableOffset[TableCount] = {' + ','.join(str(o) for _, o, _, _ in index) + '};\n'
       'constexpr uint32_t TableBytes[TableCount] = {' + ','.join(str(z) for _, _, z, _ in index) + '};\n'
       'constexpr uint32_t TableRecord[TableCount] = {' + ','.join(str(r) for *_, r in index) + '};\n'
       f'constexpr unsigned ScriptCount = {len(scripts)};\n'
       '}\n}\n')
(root / 'bsf/unicode_layout_index.h').write_text(hdr)
print(json.dumps({'blob_bytes': total, **{n: z for n, _, z, _ in index}}))
manifest = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(src.iterdir()) if p.name != 'SHA256.json'}
(src / 'SHA256.json').write_text(json.dumps(manifest, indent=2) + '\n')
