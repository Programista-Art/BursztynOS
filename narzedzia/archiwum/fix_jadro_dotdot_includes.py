#!/usr/bin/env python3
from pathlib import Path
import re

mapping = {p.name: p.as_posix() for p in Path("jadro").rglob("*") if p.is_file()}
pattern = re.compile(r'#include\s+"\.\./([A-Za-z0-9_.-]+\.(?:cpp|h|inc))"')

files = list(Path("testy").rglob("*.cpp")) + list(Path("testy").rglob("*.h"))
files.append(Path("narzedzia/generuj_zrzuty_ui.cpp"))

changed = 0
for path in files:
    if not path.exists():
        continue
    text = path.read_text(encoding="utf-8", errors="replace")

    def repl(m: re.Match) -> str:
        name = m.group(1)
        if name in mapping:
            return f'#include "../{mapping[name]}"'
        return m.group(0)

    new = pattern.sub(repl, text)
    if new != text:
        path.write_text(new, encoding="utf-8")
        print(path.as_posix())
        changed += 1
print("changed", changed)
