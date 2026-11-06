#!/usr/bin/env python3
"""Rewrite ../../kernel_header.h -> \"kernel_header.h\" after jadro/ move."""
from pathlib import Path
import re

jadro_headers = {p.name for p in Path("jadro").rglob("*.h")}
jadro_headers |= {p.name for p in Path("jadro").rglob("*.inc")}

pattern = re.compile(
    r'#include\s+"(\.\./)+((?P<name>[A-Za-z0-9_.-]+\.(?:h|inc)))"'
)

changed_files = 0
for path in Path(".").rglob("*"):
    if path.suffix not in {".cpp", ".h", ".hpp", ".c", ".inc"}:
        continue
    if "jadro" in path.parts and path.parts[0] == "jadro":
        # leave intra-jadro relative includes alone for now
        pass
    text = path.read_text(encoding="utf-8", errors="replace")
    orig = text

    def repl(m: re.Match) -> str:
        name = m.group("name")
        if name in jadro_headers:
            return f'#include "{name}"'
        return m.group(0)

    text = pattern.sub(repl, text)
    if text != orig:
        path.write_text(text, encoding="utf-8")
        changed_files += 1
        print(path.as_posix())

print("files_changed", changed_files)

# Drop conflicting generic recipes for targets with specialized recipes.
mk = Path("Makefile").read_text(encoding="utf-8", errors="replace")
for obj, src in [
    ("stack_protector.o", "jadro/mm/stack_protector.cpp"),
    ("ring3_ssp.o", "jadro/mm/ring3_ssp.cpp"),
    ("bursztyn_gui.o", "jadro/gui/bursztyn_gui.cpp"),
]:
    mk = re.sub(
        rf"^{re.escape(obj)}: {re.escape(src)}\n\t\$\((?:CXX|CC)\).*?\n",
        "",
        mk,
        count=1,
        flags=re.M,
    )
Path("Makefile").write_text(mk, encoding="utf-8")
print("makefile_specials_deduped")
