#!/usr/bin/env python3
"""One-shot Makefile rewrite for wariant B (jadro/)."""
from pathlib import Path
import re

mapping = {}
for p in Path("jadro").rglob("*"):
    if p.is_file():
        mapping[p.name] = p.as_posix()

makefile = Path("Makefile").read_text(encoding="utf-8", errors="replace")

# Remove previous B1 arch-only block if present.
makefile = re.sub(
    r"# --- jadro/arch \(wariant B\) ---.*?(?=\n# =+\n# KONFIGURACJA KLUCZA|\n# KONFIGURACJA KLUCZA)",
    "",
    makefile,
    count=1,
    flags=re.S,
)
# Also remove a previously generated full block if re-run.
makefile = re.sub(
    r"# --- jadro/\* explicit object rules \(wariant B\) ---.*?(?=\n# =+\n# KONFIGURACJA KLUCZA|\n# KONFIGURACJA KLUCZA)",
    "",
    makefile,
    count=1,
    flags=re.S,
)

special = {
    "aktualizacja.o",
    "bws_nowoczesne.o",
    "syscalls.o",
    "bursztyn_gui_base.o",
    "bursztyn_gui_minimal_base.o",
    "mbedtls_port.o",
}

lines = ["# --- jadro/* explicit object rules (wariant B) ---"]
for name in sorted(mapping):
    if not name.endswith((".cpp", ".S")):
        continue
    src = mapping[name]
    obj = name.rsplit(".", 1)[0] + ".o"
    if obj in special:
        continue
    if name.endswith(".S"):
        lines += [f"{obj}: {src}", "\t$(CC) $(ASM_FLAGS) -c $< -o $@"]
    else:
        lines += [f"{obj}: {src}", "\t$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@"]
lines.append("")
rules = "\n".join(lines) + "\n"

idx = makefile.find("# KONFIGURACJA KLUCZA AKTUALIZACJI")
if idx < 0:
    raise SystemExit("marker not found")
block_start = makefile.rfind("# ===", 0, idx)
if block_start < 0:
    block_start = idx
makefile = makefile[:block_start] + rules + makefile[block_start:]


def rewrite_token(text: str, base: str, dest: str) -> str:
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
    return "".join(out)


for base in sorted(mapping.keys(), key=len, reverse=True):
    if not base.endswith((".cpp", ".S", ".h", ".inc", ".ld")):
        continue
    makefile = rewrite_token(makefile, base, mapping[base])

makefile = makefile.replace(
    "--wyjscie aktualizacja_klucz.h",
    "--wyjscie jadro/aktualizacja/aktualizacja_klucz.h",
)
makefile = makefile.replace(
    "aktualizacja_klucz.h: .aktualizacja_klucz_check",
    "jadro/aktualizacja/aktualizacja_klucz.h: .aktualizacja_klucz_check",
)
makefile = makefile.replace(
    "-include $(wildcard *.d biblioteki/mbedtls/library/*.d)",
    "-include $(wildcard *.d jadro/*/*.d biblioteki/mbedtls/library/*.d)",
)

Path("Makefile").write_text(makefile, encoding="utf-8")
print("ok", "pmm" in makefile, mapping["jadro/mm/pmm.cpp"] in makefile, mapping["jadro/rdzen/kernel.cpp"] in makefile)
