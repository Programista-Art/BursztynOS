#!/usr/bin/env python3
"""Ponowne uruchomienie jobow nightly, ktore padly przez overflow linkerow / brak test_gui."""

from __future__ import annotations

import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPORT_DIR = ROOT / ".local-ci"
LOG_DIR = REPORT_DIR / "logs"

JOBS = [
    ("test-stack-protector-qemu", 900),
    ("test-bws-abuse-qemu", 900),
    ("test-guard-page", 900),
    ("test-virtio-qemu", 1200),
    ("test-virtio-blk-qemu", 1200),
    ("test-virtio-net-qemu", 1200),
    ("test-virtio-gpu-qemu", 1200),
    ("test-virtio-input-qemu", 1200),
    ("test-ota", 2400),
    ("installer-test", 1800),
    ("test-hda-regression-qemu", 1200),
    ("audit-stos-jadra", 1800),
]


def main() -> int:
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    results = []

    for target, timeout in JOBS:
        print(f"[rerun] >> make {target}", flush=True)
        t0 = time.monotonic()
        try:
            proc = subprocess.run(
                ["make", "-C", str(ROOT), target],
                cwd=str(ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=timeout,
                check=False,
            )
            duration = round(time.monotonic() - t0, 3)
            log_path = LOG_DIR / f"rerun-{target}.log"
            log_path.write_text(proc.stdout or "", encoding="utf-8", errors="replace")
            status = "pass" if proc.returncode == 0 else "fail"
            print(
                f"[rerun] << {target}: {status} exit={proc.returncode} duration={duration}s",
                flush=True,
            )
            results.append(
                {
                    "target": target,
                    "status": status,
                    "exit_code": proc.returncode,
                    "duration_s": duration,
                    "log": str(log_path.relative_to(ROOT)).replace("\\", "/"),
                }
            )
        except subprocess.TimeoutExpired as exc:
            duration = round(time.monotonic() - t0, 3)
            log_path = LOG_DIR / f"rerun-{target}.log"
            out = exc.stdout if isinstance(getattr(exc, "stdout", None), str) else ""
            log_path.write_text(out or "", encoding="utf-8", errors="replace")
            print(f"[rerun] << {target}: timeout duration={duration}s", flush=True)
            results.append(
                {
                    "target": target,
                    "status": "timeout",
                    "exit_code": 124,
                    "duration_s": duration,
                    "log": str(log_path.relative_to(ROOT)).replace("\\", "/"),
                }
            )

    failed = sum(1 for item in results if item["status"] != "pass")
    payload = {
        "suite": "bursztyn-local-ci-rerun-after-fixes",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "summary": {
            "overall": "pass" if failed == 0 else "fail",
            "total": len(results),
            "passed": len(results) - failed,
            "failed": failed,
            "duration_s": round(sum(item["duration_s"] for item in results), 3),
        },
        "jobs": results,
        "fixes_applied": [
            "programy/tapety/tapety_linker.ld TEXT 0x44000->0x48000",
            "programy/przegladarka/przegladarka_linker.ld + przegladarka.cpp TEXT 0x88000->0xE0000",
            "narzedzia/aktualizacje/test_gui_aktualizacje.py restored (host ABI/GUI gate)",
        ],
    }
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    path = REPORT_DIR / f"raport-rerun-{stamp}.json"
    text = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    path.write_text(text, encoding="utf-8")
    (REPORT_DIR / "raport-rerun-latest.json").write_text(text, encoding="utf-8")
    print(f"[rerun] raport: {path.relative_to(ROOT)} failed={failed}/{len(results)}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
