#!/usr/bin/env python3
"""Second-pass rewrite: only skip when token is already jadro/<dir>/<base>."""
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
            # Already rewritten as jadro/<domain>/<base>
            before = text[max(0, m.start() - len(dest) - 1) : m.start()]
            if before.endswith(dest[: -len(base)]) and "jadro/" in before:
                out.append(text[i : m.end()])
                i = m.end()
                continue
            # Also skip explicit jadro/.../base already fully present
            window = text[max(0, m.start() - 64) : m.end()]
            if re.search(rf"jadro/[\w]+/{re.escape(base)}", window):
                # if match start is inside that already-rewritten path, skip
                if text[m.start() - len(base) - 1 : m.end()] != "/" + base and re.search(
                    rf"jadro/[\w]+/{re.escape(base)}$",
                    text[max(0, m.start() - 40) : m.end()],
                ):
                    out.append(text[i : m.end()])
                    i = m.end()
                    continue
            # Precise: if characters immediately before form jadro/xxx/
            pre = text[max(0, m.start() - 48) : m.start()]
            if re.search(r"jadro/[\w]+/$", pre):
                out.append(text[i : m.end()])
                i = m.end()
                continue
            out.append(text[i : m.start()])
            out.append(dest)
            i = m.end()
        text = "".join(out)
    return text


for path in ["Makefile", "narzedzia/run_fuzz_suite.py", "testy/test_bws_abuse_qemu.py", "testy/test_stack_protector_qemu.py"]:
    p = Path(path)
    orig = p.read_text(encoding="utf-8", errors="replace")
    text = rewrite_text(orig)
    # Host flags that still only have -I.
    if path == "Makefile":
        text = text.replace(
            "PSF_BOOTSTRAP_HOST_FLAGS := -std=c++17 -O1 -g -Wall -Wextra -I. -Ibiblioteki/mbedtls/include",
            "PSF_BOOTSTRAP_HOST_FLAGS := -std=c++17 -O1 -g -Wall -Wextra $(HOST_INCLUDES) -Ibiblioteki/mbedtls/include",
        )
        text = text.replace(
            "g++ -std=c++17 -O2 -Wall -Wextra -I. testy/test_motyw_host.cpp",
            "g++ -std=c++17 -O2 -Wall -Wextra $(HOST_INCLUDES) testy/test_motyw_host.cpp",
        )
        text = text.replace(
            "g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -DBURSZTYN_LOADER_HOST_TEST",
            "g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_LOADER_HOST_TEST",
        )
        text = text.replace(
            "g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -fsanitize=address,undefined testy/test_stack_protector_host.cpp",
            "g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -fsanitize=address,undefined testy/test_stack_protector_host.cpp",
        )
        # Fix aktualizacja_klucz.h dep if still bare in aktualizacja.o rule
        text = text.replace(
            "aktualizacja.o: jadro/aktualizacja/aktualizacja.cpp jadro/aktualizacja/aktualizacja.h jadro/aktualizacja/aktualizacja_format.h aktualizacja_klucz.h .aktualizacja_klucz_check",
            "aktualizacja.o: jadro/aktualizacja/aktualizacja.cpp jadro/aktualizacja/aktualizacja.h jadro/aktualizacja/aktualizacja_format.h jadro/aktualizacja/aktualizacja_klucz.h .aktualizacja_klucz_check",
        )
        # common leftover bare klucz
        text = re.sub(
            r"(?<![\w./])aktualizacja_klucz\.h(?![\w./])",
            "jadro/aktualizacja/aktualizacja_klucz.h",
            text,
        )
    if text != orig:
        p.write_text(text, encoding="utf-8")
        print("patched", path)
    else:
        print("unchanged", path)

mk = Path("Makefile").read_text(encoding="utf-8", errors="replace")
for bad in [" pmm.cpp", " heap.cpp", " skladacz_obrazu.cpp", " motyw.h", " bursztyn_gui.h", " aktualizacja_klucz.h"]:
    # count bare leftovers (not under jadro/)
    hits = []
    for m in re.finditer(re.escape(bad.strip()), mk):
        pre = mk[max(0, m.start() - 20) : m.start()]
        if "jadro/" not in pre[-20:]:
            hits.append(mk[max(0, m.start() - 30) : m.start() + len(bad.strip()) + 5].replace("\n", " "))
    print(bad.strip(), "bare-ish", len(hits))
    for h in hits[:5]:
        print(" ", h)
