#!/usr/bin/env python3
"""Static consistency gate for the CPython cross-build/runtime contract."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "narzedzia" / "python" / "cpython-runtime.manifest.json"
SCRIPT = ROOT / "narzedzia" / "python" / "buduj_cpython.sh"


def fail(message: str) -> None:
    raise SystemExit(f"[CPYTHON-MANIFEST] FAIL: {message}")


def main() -> int:
    try:
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read manifest: {exc}")

    version = data.get("version")
    if not isinstance(version, str) or not re.fullmatch(r"3\.\d+\.\d+", version):
        fail("version must be a full CPython 3.x.y version")
    major_minor = ".".join(version.split(".")[:2])

    source = data.get("source", {})
    expected_name = f"Python-{version}.tgz"
    if not str(source.get("url", "")).endswith("/" + expected_name):
        fail("source URL and version disagree")
    digest = source.get("sha256", "")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        fail("source SHA-256 is not a lowercase 64-character digest")

    layout = data.get("runtime_layout", {})
    expected_paths = {
        "interpreter": "/usr/bin/python3",
        "versioned_interpreter": f"/usr/bin/python{major_minor}",
        "stdlib": f"/usr/lib/python{major_minor}",
        "encodings": f"/usr/lib/python{major_minor}/encodings",
        "sysconfig_module": f"/usr/lib/python{major_minor}/sysconfig.py",
        "site_packages": f"/usr/lib/python{major_minor}/site-packages",
    }
    for key, expected in expected_paths.items():
        if layout.get(key) != expected:
            fail(f"{key} must be {expected!r}")

    target = data.get("target", {})
    if target.get("linkage") != "static" or target.get("elf_type") != "ET_EXEC":
        fail("runtime must remain static ET_EXEC")
    if target.get("dynamic_loader") is not False:
        fail("manifest must explicitly reject a dynamic loader")

    script = SCRIPT.read_text(encoding="utf-8")
    for required in (
        f'VERSION="${{CPYTHON_VERSION:-{version}}}"',
        f'ARCHIVE_SHA256="${{CPYTHON_SHA256:-{digest}}}"',
        'URL="${CPYTHON_URL:-https://www.python.org/ftp/python/$VERSION/Python-$VERSION.tgz}"',
        'BUILD_ROOT="${CPYTHON_BUILD_ROOT:-$ROOT/.cpython-build}"',
        "sha256sum --check --strict",
        "--disable-shared",
        "-static",
        "MUSL_SYSROOT",
    ):
        if required not in script:
            fail(f"build script is missing {required!r}")

    print(
        "[CPYTHON-MANIFEST] PASS: "
        f"CPython {version}, {layout['stdlib']}, static ET_EXEC"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
