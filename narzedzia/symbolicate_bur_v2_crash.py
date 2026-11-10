#!/usr/bin/env python3
"""Symbolicate Bursztyn OS Ring 3 ASLR [USER FAULT] crash reports against private ELF symbols."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

FAULT_RE = re.compile(
    r'\[USER FAULT\] process terminated:\s*'
    r'pid=(\d+)\s+'
    r'vector=(\d+)\s+'
    r'rip=0x([0-9a-fA-F]+)'
    r'(?:\s+base=0x([0-9a-fA-F]+))?'
    r'(?:\s+rva=0x([0-9a-fA-F]+))?'
    r'(?:\s+bur=v(\d+))?'
    r'\s+program=(\S+)'
)


def resolve_elf(bur_path: pathlib.Path | None, elf_path: pathlib.Path | None) -> pathlib.Path | None:
    if elf_path and elf_path.exists():
        return elf_path
    if bur_path and bur_path.exists():
        sha = hashlib.sha256(bur_path.read_bytes()).hexdigest()
        archived = pathlib.Path('.release-private/apps') / sha / 'app.elf'
        if archived.exists():
            return archived
        candidate = bur_path.with_suffix('.elf')
        if candidate.exists():
            return candidate
    return None


def symbolicate_line(line: str, bur_path: pathlib.Path | None = None,
                     elf_path: pathlib.Path | None = None,
                     override_base: int | None = None) -> dict | None:
    m = FAULT_RE.search(line)
    if not m:
        return None
    pid = int(m.group(1))
    vector = int(m.group(2))
    rip = int(m.group(3), 16)
    base = int(m.group(4), 16) if m.group(4) else (override_base or 0)
    logged_rva = int(m.group(5), 16) if m.group(5) else None
    bur_ver = int(m.group(6)) if m.group(6) else 1
    program = m.group(7)

    computed_rva = (rip - base) if (base and rip >= base) else 0
    if logged_rva is not None and base and rip >= base and logged_rva != computed_rva:
        raise ValueError(f'RVA mismatch: logged={logged_rva:#x} computed={computed_rva:#x}')
    rva = logged_rva if logged_rva is not None else computed_rva

    elf = resolve_elf(bur_path, elf_path)
    symbol = None
    location = None
    if elf and rva > 0:
        proc = subprocess.run(
            ['addr2line', '-f', '-C', '-e', str(elf), hex(rva)],
            capture_output=True, text=True, check=False
        )
        if proc.returncode == 0:
            lines = [x.strip() for x in proc.stdout.splitlines() if x.strip()]
            if len(lines) >= 2:
                symbol, location = lines[0], lines[1]

    return {
        'pid': pid,
        'vector': vector,
        'rip': hex(rip),
        'base': hex(base),
        'rva': hex(rva),
        'bur_version': bur_ver,
        'program': program,
        'elf': str(elf) if elf else None,
        'symbol': symbol,
        'location': location,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=pathlib.Path, help='Serial log file containing [USER FAULT] lines')
    ap.add_argument('--bur', type=pathlib.Path, default=None, help='BUR v2 binary to resolve via .release-private/apps/<sha256>/app.elf')
    ap.add_argument('--elf', type=pathlib.Path, default=None, help='Explicit ELF file with symbols')
    ap.add_argument('--json', type=pathlib.Path, default=None, help='Optional output JSON path')
    args = ap.parse_args()

    text = args.log.read_text(errors='replace')
    reports = []
    for line in text.splitlines():
        rep = symbolicate_line(line, args.bur, args.elf)
        if rep:
            reports.append(rep)
    if not reports:
        ap.exit(1, 'No [USER FAULT] lines found in log\n')
    out_text = json.dumps(reports, indent=2) + '\n'
    if args.json:
        args.json.write_text(out_text)
    sys.stdout.write(out_text)


if __name__ == '__main__':
    main()
