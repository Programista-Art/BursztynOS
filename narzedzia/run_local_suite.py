#!/usr/bin/env python3
"""
Bursztyn OS — lokalny orkiestrator weryfikacji (bez GitHuba).

Uruchamia warstwy smoke / security / regression / nightly jako cele Makefile
z timeoutami, zbiera wynik do .local-ci/raport-*.json i zwraca kod wyjścia.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
REPORT_DIR = ROOT / ".local-ci"

# (make_target, timeout_seconds)
SUITE_JOBS: dict[str, list[tuple[str, int]]] = {
    "smoke": [
        ("test-python-contract", 180),
        ("test-stack-protector-host", 300),
        ("test-bur-v2-aslr-host", 300),
        ("test-fuzz-smoke", 900),
        ("test-virtio-pci", 300),
        ("test-motywy-unit", 300),
        ("test-wm-click-routing", 300),
        ("test-normal-boot", 900),
    ],
    "security": [
        ("test-security-host", 1200),
        ("test-stack-protector-qemu", 900),
        ("test-bws-abuse-qemu", 900),
        ("test-guard-page", 900),
    ],
    "regression": [
        ("test-virtio-qemu", 1200),
        ("test-virtio-blk-qemu", 1200),
        ("test-virtio-net-qemu", 1200),
        ("test-virtio-gpu-qemu", 1200),
        ("test-virtio-input-qemu", 1200),
        ("test-ota", 2400),
        ("installer-test", 1800),
        ("test-husarz-engine-host", 600),
        ("test-input-coexistence-host", 600),
        ("test-hda-regression-qemu", 1200),
    ],
    "nightly": [],  # zbudowane poniżej z security + regression + fuzz-extended
}

SUITE_JOBS["nightly"] = (
    list(SUITE_JOBS["security"])
    + list(SUITE_JOBS["regression"])
    + [("fuzz-extended", 7200)]
)


def find_make() -> str:
    for candidate in ("make", "gmake", "mingw32-make"):
        path = shutil.which(candidate)
        if path:
            return path
    raise SystemExit(
        "Nie znaleziono `make` w PATH. Uruchom suite na Linuxie/WSL "
        "(wymagany cross x86_64-linux-gnu-* + QEMU)."
    )


def run_make_target(
    make_bin: str,
    target: str,
    timeout_s: int,
    extra_env: dict[str, str] | None = None,
) -> dict[str, Any]:
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)

    cmd = [make_bin, "-C", str(ROOT), target]
    started = time.monotonic()
    started_iso = datetime.now(timezone.utc).isoformat()
    result: dict[str, Any] = {
        "target": target,
        "timeout_s": timeout_s,
        "started_at": started_iso,
        "command": cmd,
    }

    try:
        proc = subprocess.run(
            cmd,
            cwd=str(ROOT),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout_s,
            check=False,
        )
        duration = time.monotonic() - started
        log_name = f"{target.replace('/', '_')}.log"
        log_path = REPORT_DIR / "logs" / log_name
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(proc.stdout or "", encoding="utf-8", errors="replace")

        skip_lines = [
            line for line in (proc.stdout or "").splitlines()
            if " SKIP:" in line or " BLOCKED:" in line
        ]
        pass_lines = [
            line for line in (proc.stdout or "").splitlines()
            if " PASS" in line
        ]
        status = "pass" if proc.returncode == 0 else "fail"
        if status == "pass" and skip_lines and not pass_lines:
            status = "skipped"
        result.update(
            {
                "status": status,
                "exit_code": proc.returncode,
                "duration_s": round(duration, 3),
                "log": str(log_path.relative_to(ROOT)).replace("\\", "/"),
                "skip_reasons": skip_lines,
            }
        )
    except subprocess.TimeoutExpired as exc:
        duration = time.monotonic() - started
        log_name = f"{target.replace('/', '_')}.log"
        log_path = REPORT_DIR / "logs" / log_name
        log_path.parent.mkdir(parents=True, exist_ok=True)
        output = ""
        if exc.stdout:
            output = exc.stdout if isinstance(exc.stdout, str) else exc.stdout.decode(
                "utf-8", errors="replace"
            )
        log_path.write_text(output, encoding="utf-8", errors="replace")
        result.update(
            {
                "status": "timeout",
                "exit_code": 124,
                "duration_s": round(duration, 3),
                "log": str(log_path.relative_to(ROOT)).replace("\\", "/"),
            }
        )

    return result


def write_report(mode: str, jobs: list[dict[str, Any]], continue_on_error: bool) -> Path:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    path = REPORT_DIR / f"raport-{mode}-{stamp}.json"

    passed = sum(1 for j in jobs if j.get("status") == "pass")
    failed = sum(1 for j in jobs if j.get("status") in ("fail", "timeout"))
    skipped = sum(1 for j in jobs if j.get("status") == "skipped")
    overall = "pass" if failed == 0 and any(j.get("status") == "pass" for j in jobs) else "fail"
    if not jobs:
        overall = "fail"

    payload = {
        "suite": "bursztyn-local-ci",
        "mode": mode,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "continue_on_error": continue_on_error,
        "summary": {
            "overall": overall,
            "total": len(jobs),
            "passed": passed,
            "failed": failed,
            "skipped": skipped,
            "duration_s": round(sum(float(j.get("duration_s") or 0) for j in jobs), 3),
        },
        "jobs": jobs,
    }
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    latest = REPORT_DIR / f"raport-{mode}-latest.json"
    latest.write_text(path.read_text(encoding="utf-8"), encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Lokalny orkiestrator weryfikacji Bursztyn OS (smoke/security/regression/nightly)."
    )
    parser.add_argument(
        "--mode",
        choices=sorted(SUITE_JOBS.keys()),
        default="smoke",
        help="Warstwa suite (domyslnie: smoke).",
    )
    parser.add_argument(
        "--continue-on-error",
        action="store_true",
        help="Kontynuuj po bledzie (zalecane dla nightly); domyslnie stop na pierwszym failu.",
    )
    parser.add_argument(
        "--stack-audit",
        action="store_true",
        help="Po security dodaj audit-stos-jadra (ciezki).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Wypisz plan jobow bez uruchamiania make.",
    )
    args = parser.parse_args()

    jobs_spec = list(SUITE_JOBS[args.mode])
    if args.mode == "security" and args.stack_audit:
        jobs_spec.append(("audit-stos-jadra", 1800))
    if args.mode == "nightly" and args.stack_audit:
        jobs_spec.append(("audit-stos-jadra", 1800))

    # Nightly i regression: kontynuuj po bledzie domyslnie, chyba ze wylaczone.
    continue_on_error = args.continue_on_error or args.mode in ("nightly", "regression")

    if args.dry_run:
        print(f"mode={args.mode} continue_on_error={continue_on_error}")
        for target, timeout_s in jobs_spec:
            print(f"  {target}  timeout={timeout_s}s")
        return 0

    make_bin = find_make()
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    (REPORT_DIR / "logs").mkdir(parents=True, exist_ok=True)

    results: list[dict[str, Any]] = []
    abort = False

    print(f"[local-ci] mode={args.mode} jobs={len(jobs_spec)} make={make_bin}")
    for target, timeout_s in jobs_spec:
        if abort:
            results.append(
                {
                    "target": target,
                    "timeout_s": timeout_s,
                    "status": "skipped",
                    "exit_code": None,
                    "duration_s": 0.0,
                    "log": None,
                    "reason": "poprzedni job zakonczyl sie bledem",
                }
            )
            continue

        print(f"[local-ci] >> make {target} (timeout {timeout_s}s)")
        job = run_make_target(make_bin, target, timeout_s)
        results.append(job)
        print(
            f"[local-ci] << {target}: {job['status']} "
            f"exit={job.get('exit_code')} duration={job.get('duration_s')}s"
        )

        if job["status"] != "pass" and not continue_on_error:
            abort = True

    report_path = write_report(args.mode, results, continue_on_error)
    failed = sum(1 for j in results if j.get("status") in ("fail", "timeout"))
    print(f"[local-ci] raport: {report_path.relative_to(ROOT)}")
    print(f"[local-ci] failed={failed}/{len(results)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
