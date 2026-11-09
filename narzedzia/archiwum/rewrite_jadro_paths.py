#!/usr/bin/env python3
from pathlib import Path
import re

mapping = {p.name: p.as_posix() for p in Path("jadro").rglob("*") if p.is_file()}


def rewrite_text(text: str) -> str:
    for base in sorted(mapping, key=len, reverse=True):
        if not base.endswith((".cpp", ".S", ".h", ".inc", ".ld")):
            continue
        dest = mapping[base]
        pattern = re.compile(rf"(?<![\w./]){re.escape(base)}(?![\w./])")
        out = []
        i = 0
        while True:
            m = pattern.search(text, i)
            if not m:
                out.append(text[i:])
                break
            look = text[max(0, m.start() - 48) : m.start()]
            if "jadro/" in look:
                out.append(text[i : m.end()])
                i = m.end()
                continue
            out.append(text[i : m.start()])
            out.append(dest)
            i = m.end()
        text = "".join(out)
    return text


targets = [
    "narzedzia/run_fuzz_suite.py",
    "testy/test_bws_abuse_qemu.py",
    "testy/test_stack_protector_qemu.py",
    "testy/test_xhci_incremental_before_after.py",
    "testy/test_input_diagnostic_parity.py",
]
for path in targets:
    p = Path(path)
    if not p.exists():
        continue
    orig = p.read_text(encoding="utf-8", errors="replace")
    text = rewrite_text(orig)
    if path.endswith("run_fuzz_suite.py"):
        text = text.replace('"-c", "jadro/fs/psf.cpp"', '"-c", "jadro/fs/psf.cpp"')
        text = text.replace('"-c", "jadro/fs/storage_format.cpp"', '"-c", "jadro/fs/storage_format.cpp"')
        text = text.replace(
            "psf.cpp + storage_format.cpp",
            "jadro/fs/psf.cpp + jadro/fs/storage_format.cpp",
        )
        text = text.replace(
            "Kompilacja wspolnych obiektow jadro/fs/psf.cpp + jadro/fs/storage_format.cpp",
            "Kompilacja wspolnych obiektow jadro/fs/psf.cpp + jadro/fs/storage_format.cpp",
        )
    if text != orig:
        p.write_text(text, encoding="utf-8")
        print("patched", path)
    else:
        print("unchanged", path)

# Spot-check Makefile host source mentions
mk = Path("Makefile").read_text(encoding="utf-8", errors="replace")
for needle in [
    "jadro/mm/pmm.cpp",
    "jadro/mm/heap.cpp",
    "jadro/gui/ekran_powitalny.cpp",
    "jadro/gui/bursztyn_gui.cpp",
    "jadro/fs/psf.cpp",
    "jadro/rdzen/kernel.cpp",
    "jadro/aktualizacja/aktualizacja_klucz.h",
]:
    print(needle, needle in mk)
