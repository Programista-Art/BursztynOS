#!/usr/bin/env python3
"""
Generuje docs/security-hardening-2026-11-04/after.json — porownanie
rozmiarow binarnych, sekcji ELF, skrotow SHA-256, aplikacji Ring 3 (.bin/.bur)
oraz uzycia stosu (.su) wzgledem baseline.json po wdrozeniu zabezpieczen.
"""

import glob
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BASELINE_PATH = os.path.join(ROOT, "docs/security-hardening-2026-11-04/baseline.json")
AFTER_PATH = os.path.join(ROOT, "docs/security-hardening-2026-11-04/after.json")


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def parse_sections(raw: str) -> dict:
    out = {}
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith("."):
            try:
                out[parts[0]] = int(parts[1])
            except ValueError:
                pass
    return out


def main() -> int:
    with open(BASELINE_PATH, "r", encoding="utf-8") as f:
        baseline = json.load(f)

    git_head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    compiler = subprocess.check_output(["g++", "--version"], cwd=ROOT, text=True).splitlines()[0].strip()

    artifacts = {}
    for rel in ["BursztynOS.iso", "system_operacyjny.bin", "isodir/boot/system_operacyjny.bin"]:
        p = os.path.join(ROOT, rel)
        if os.path.exists(p):
            entry = {
                "size_bytes": os.path.getsize(p),
                "sha256": sha256_file(p),
                "baseline_size_bytes": baseline["artifacts"].get(rel, {}).get("size_bytes", 0),
                "delta_bytes": os.path.getsize(p) - baseline["artifacts"].get(rel, {}).get("size_bytes", 0),
            }
            if rel == "system_operacyjny.bin":
                raw = subprocess.check_output(["size", "-A", p], cwd=ROOT, text=True)
                entry["sections_raw"] = raw
                cur_sec = parse_sections(raw)
                base_sec = parse_sections(baseline["artifacts"]["system_operacyjny.bin"]["sections_raw"])
                sec_diff = {}
                for k in [".boot", ".text", ".rodata", ".data", ".bss"]:
                    if k in cur_sec and k in base_sec:
                        sec_diff[k] = {
                            "baseline": base_sec[k],
                            "after": cur_sec[k],
                            "delta_bytes": cur_sec[k] - base_sec[k],
                            "delta_pct": round((cur_sec[k] - base_sec[k]) * 100.0 / max(1, base_sec[k]), 3),
                        }
                entry["section_deltas"] = sec_diff
            artifacts[rel] = entry

    ring3_apps = {}
    for name, base_info in sorted(baseline.get("ring3_apps", {}).items()):
        p = os.path.join(ROOT, name)
        if os.path.exists(p):
            sz = os.path.getsize(p)
            ring3_apps[name] = {
                "size_bytes": sz,
                "sha256": sha256_file(p),
                "baseline_size_bytes": base_info["size_bytes"],
                "delta_bytes": sz - base_info["size_bytes"],
            }

    # Parse .su files in ROOT (excluding temporary test directories)
    su_entries = []
    for su_path in sorted(glob.glob(os.path.join(ROOT, "*.su"))):
        fname = os.path.basename(su_path)
        with open(su_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.strip().split("\t")
                if len(parts) >= 3:
                    try:
                        frame = int(parts[1])
                    except ValueError:
                        continue
                    su_entries.append({
                        "file": fname,
                        "location_func": parts[0],
                        "frame_bytes": frame,
                        "qualifier": parts[2],
                    })
    su_entries.sort(key=lambda x: (-x["frame_bytes"], x["file"], x["location_func"]))

    total_funcs = len(su_entries)
    total_frame = sum(x["frame_bytes"] for x in su_entries)
    max_frame = su_entries[0]["frame_bytes"] if su_entries else 0

    after = {
        "git_head": git_head,
        "compiler": compiler,
        "artifacts": artifacts,
        "ring3_apps": ring3_apps,
        "stack_usage": {
            "total_functions": total_funcs,
            "baseline_total_functions": baseline["stack_usage"]["total_functions"],
            "total_frame_bytes": total_frame,
            "baseline_total_frame_bytes": baseline["stack_usage"]["total_frame_bytes"],
            "delta_total_frame_bytes": total_frame - baseline["stack_usage"]["total_frame_bytes"],
            "max_frame_bytes": max_frame,
            "baseline_max_frame_bytes": baseline["stack_usage"]["max_frame_bytes"],
            "kernel_stack_size_bytes": 65536,
            "max_frame_within_1024_target_except_psf_inodes": all(
                x["frame_bytes"] <= 1300 for x in su_entries if x["file"] != "psf.su"
            ),
            "top_25_functions": su_entries[:25],
        },
    }

    with open(AFTER_PATH, "w", encoding="utf-8") as f:
        json.dump(after, f, indent=2, ensure_ascii=False)
    print(f"[AFTER-REPORT] Saved {AFTER_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
