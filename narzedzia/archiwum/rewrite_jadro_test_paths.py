#!/usr/bin/env python3
from pathlib import Path

mapping = {p.name: p.as_posix() for p in Path("jadro").rglob("*") if p.is_file()}
mapping.update(
    {
        "sterowniki/virtio/virtio_input.cpp": "sterowniki/virtio/virtio_input.cpp",
        "sterowniki/wejscie/input_device.cpp": "sterowniki/wejscie/input_device.cpp",
        "sterowniki/grafika/grafika.cpp": "sterowniki/grafika/grafika.cpp",
        "sterowniki/virtio/virtqueue.cpp": "sterowniki/virtio/virtqueue.cpp",
    }
)

targets = list(Path("testy").rglob("*.py")) + list(Path("narzedzia").rglob("*.py"))

for path in targets:
    text = path.read_text(encoding="utf-8", errors="replace")
    orig = text
    for base in sorted(mapping, key=len, reverse=True):
        dest = mapping[base]
        for q in ("'", '"'):
            text = text.replace(f"{q}{base}{q}", f"{q}{dest}{q}")
        text = text.replace(f"os.path.join(REPO, \"{base}\")", f'os.path.join(REPO, "{dest}")')
        text = text.replace(f"os.path.join(REPO, '{base}')", f"os.path.join(REPO, '{dest}')")
    text = text.replace("jadro/", "jadro/")
    text = text.replace("sterowniki/", "sterowniki/")
    # Avoid rewriting already-correct sterowniki paths that got duplicated domain
    text = text.replace("sterowniki/", "sterowniki/")
    text = text.replace("sterowniki/", "sterowniki/")
    text = text.replace("sterowniki/", "sterowniki/")
    if text != orig:
        path.write_text(text, encoding="utf-8")
        print("patched", path.as_posix())
