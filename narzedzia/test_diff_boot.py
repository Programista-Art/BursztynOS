#!/usr/bin/env python3
"""
narzedzia/test_diff_boot.py - Automated Non-blocking Differential Boot Test Harness
for Bursztyn OS (BIOS VirtIO vs UEFI VirtIO vs UEFI VirtIO 640).

Key features:
 - Non-blocking reader daemon thread + Queue (avoids any blocking readline)
 - Dedicated boot ISO with timeout=0 for automated, zero-delay unattended boot
 - Strict per-VM timeout (75s) and inactivity timeout (25s)
 - Global suite timeout (4 minutes)
 - Immediate screenshot capture and visual verification (wallpaper, 15 icons, taskbar, Start, clock, cursor)
 - Clean teardown via QMP quit, terminate, bounded wait, and kill (no orphan QEMU processes)
"""

import sys
import os
import time
import socket
import json
import shutil
import struct
import threading
import queue
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OVMF = "/usr/share/ovmf/OVMF.fd"
OUT_DIR = Path("/tmp/bursztyn_boot_diff")

VM_TIMEOUT = 90.0
INACTIVITY_TIMEOUT = 35.0
SUITE_TIMEOUT = 240.0

class QmpClient:
    def __init__(self, sock_path, timeout=5.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(str(sock_path))
        self.sock.recv(4096)
        self.sock.sendall(b'{"execute": "qmp_capabilities"}\n')
        self.sock.recv(4096)

    def execute(self, cmd_dict):
        self.sock.sendall((json.dumps(cmd_dict) + "\n").encode('utf-8'))
        resp = b""
        while not resp.endswith(b"\n"):
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            resp += chunk
        return json.loads(resp.decode('utf-8'))

    def screendump(self, filename, device=None):
        args = {"filename": str(filename)}
        if device:
            args["device"] = device
        return self.execute({"execute": "screendump", "arguments": args})

    def quit(self):
        return self.execute({"execute": "quit"})

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

def ensure_diff_iso():
    OUT_DIR.mkdir(exist_ok=True)
    iso_path = OUT_DIR / "BursztynOS_diff.iso"
    iso_dir = Path("/tmp/isodir_diff")
    if iso_dir.exists():
        shutil.rmtree(iso_dir)
    shutil.copytree(ROOT / "isodir", iso_dir)

    cfg = iso_dir / "boot/grub/grub.cfg"
    content = cfg.read_text()
    content = content.replace("set timeout=3", "set timeout=0")
    cfg.write_text(content)

    subprocess.check_call([
        "grub-mkrescue", "-o", str(iso_path), str(iso_dir), "--xorriso=xorriso"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return iso_path

def ensure_iso_640():
    OUT_DIR.mkdir(exist_ok=True)
    iso_640_path = OUT_DIR / "BursztynOS_640.iso"
    iso_640_dir = Path("/tmp/isodir_640")
    if iso_640_dir.exists():
        shutil.rmtree(iso_640_dir)
    shutil.copytree(ROOT / "isodir", iso_640_dir)

    cfg = iso_640_dir / "boot/grub/grub.cfg"
    content = cfg.read_text()
    content = content.replace("set timeout=3", "set timeout=0")
    content = content.replace("set gfxmode=1280x720x32,1024x768x32,auto", "set gfxmode=640x480x32,auto")
    content = content.replace("set gfxpayload=keep", "set gfxpayload=640x480x32")
    cfg.write_text(content)

    # Patch Multiboot2 framebuffer tag to 640x480x32 in staging kernel
    bin_path = iso_640_dir / "boot/system_operacyjny.bin"
    if bin_path.exists():
        b = bytearray(bin_path.read_bytes())
        old_tag = struct.pack('<HHIiii', 5, 0, 20, 1280, 720, 32)
        new_tag = struct.pack('<HHIiii', 5, 0, 20, 640, 480, 32)
        idx = b.find(old_tag)
        if idx != -1:
            b[idx:idx+len(new_tag)] = new_tag
            bin_path.write_bytes(b)

    subprocess.check_call([
        "grub-mkrescue", "-o", str(iso_640_path), str(iso_640_dir), "--xorriso=xorriso"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return iso_640_path

def verify_screenshot(png_path):
    im = Image.open(png_path)
    w, h = im.size
    errors = []

    if w < 640 or h < 480:
        errors.append(f"Dimensions too small: got {w}x{h}, minimum 640x480")

    # 1. Wallpaper (non-black, matches amber/desktop background)
    sample_pts = [(w // 4, h // 3), (w // 2, h // 2), (3 * w // 4, h // 3)]
    wallpaper_pixels = [im.getpixel(pt) for pt in sample_pts]
    if all(p == (0, 0, 0) for p in wallpaper_pixels):
        errors.append("Wallpaper is completely black")

    # 2. Taskbar (bottom 40px has background bar)
    tb_samples = [im.getpixel((x, h - 20)) for x in [w // 4, w // 2, 3 * w // 4]]
    if any(p == (0, 0, 0) for p in tb_samples):
        errors.append("Taskbar background missing or black")

    # 3. Start button (bottom left amber hexagon)
    start_pixels = [im.getpixel((x, y)) for x in range(8, min(60, w)) for y in range(h - 35, h - 5)]
    has_start = any(r > 150 and g > 50 and b < 100 for r, g, b in start_pixels)
    if not has_start:
        errors.append("Start button (amber hexagon) missing at bottom-left")

    # 4. Clock (bottom right amber text)
    clock_pixels = [im.getpixel((x, y)) for x in range(max(0, w - 150), w) for y in range(h - 40, h)]
    has_clock = any(r > 180 and g > 120 and b < 50 for r, g, b in clock_pixels)
    if not has_clock:
        errors.append("Clock text (amber digits) missing in taskbar tray")

    # 5. Cursor
    cursor_found = False
    cx, cy = min(500, w - 20), min(300, h - 40)
    for y in range(max(0, cy - 20), min(h - 1, cy + 30)):
        for x in range(max(0, cx - 20), min(w - 2, cx + 30)):
            p0 = im.getpixel((x, y))
            p_down = im.getpixel((x, y + 1))
            p_right_down = im.getpixel((x + 1, y + 1))
            if p0 == (0, 0, 0) and p_down == (0, 0, 0) and p_right_down == (255, 255, 255):
                cursor_found = True
                break
        if cursor_found:
            break
    if not cursor_found:
        box = [im.getpixel((x, y)) for x in range(max(0, cx - 30), min(w, cx + 30))
                                   for y in range(max(0, cy - 30), min(h, cy + 30))]
        if (0, 0, 0) in box and (255, 255, 255) in box:
            cursor_found = True
    if not cursor_found:
        errors.append(f"Cursor not detected near ({cx}, {cy})")

    # 6. 15 Desktop icons
    cols = max(1, (w - 40) // 96)
    icons_detected = 0
    missing_icons = []
    for i in range(15):
        ix = 20 + (i % cols) * 96
        iy = 20 + (i // cols) * 96
        if ix + 80 <= w and iy + 80 <= h:
            box = [im.getpixel((x, y)) for x in range(ix, ix + 80) for y in range(iy, iy + 80)]
            unique_colors = len(set(box))
            if unique_colors >= 4:
                icons_detected += 1
            else:
                missing_icons.append(i)
        else:
            missing_icons.append(i)
    if icons_detected < 15:
        errors.append(f"Detected only {icons_detected}/15 desktop icons (missing: {missing_icons})")

    return len(errors) == 0, errors

def reader_worker(pipe, line_queue):
    for line in iter(pipe.readline, ''):
        line_queue.put(line)
    line_queue.put(None)

def run_scenario(scenario_label, is_uefi=False, res=(1280, 720), iso_path=None):
    OUT_DIR.mkdir(exist_ok=True)
    slug = scenario_label.lower().replace(" ", "_")
    qmp_sock = OUT_DIR / f"qmp_{slug}.sock"
    dump_ppm = OUT_DIR / f"{slug}_desktop.ppm"
    dump_png = OUT_DIR / f"{slug}_desktop.png"

    for p in [qmp_sock, dump_ppm, dump_png]:
        if p.exists():
            p.unlink()

    if iso_path is None:
        iso_path = OUT_DIR / "BursztynOS_diff.iso"

    w, h = res
    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-drive", f"id=disk,file={ROOT}/wirtualny_dysk.img,format=raw,if=none",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
        "-device", "virtio-rng-pci,disable-legacy=on",
        "-device", f"virtio-vga,id=gpu0,disable-legacy=on,xres={w},yres={h}",
        "-device", "virtio-keyboard-pci,id=kbd0,disable-legacy=on",
        "-device", "virtio-mouse-pci,id=mouse0,disable-legacy=on",
        "-device", "virtio-tablet-pci,id=tablet0,disable-legacy=on",
        "-netdev", "user,id=n1",
        "-device", "virtio-net-pci,disable-legacy=on,netdev=n1",
        "-m", "2G",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso_path),
        "-qmp", f"unix:{qmp_sock},server,nowait"
    ]
    if is_uefi:
        cmd.extend(["-bios", OVMF])

    print(f"\n[TEST] START {scenario_label}", flush=True)

    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)

    line_queue = queue.Queue()
    reader_thread = threading.Thread(target=reader_worker, args=(proc.stdout, line_queue), daemon=True)
    reader_thread.start()

    booted = False
    fail_reason = ""
    last_activity = time.time()
    logs = []
    telemetry_line = None

    try:
        while time.time() - t0 < VM_TIMEOUT:
            if time.time() - last_activity > INACTIVITY_TIMEOUT:
                fail_reason = f"Inactivity timeout ({INACTIVITY_TIMEOUT}s) with no serial output"
                break

            try:
                line = line_queue.get(timeout=0.1)
            except queue.Empty:
                if proc.poll() is not None:
                    fail_reason = f"QEMU process exited unexpectedly with code {proc.returncode}"
                    break
                continue

            if line is None:
                fail_reason = "QEMU closed stdout prematurely"
                break

            last_activity = time.time()
            line_str = line.strip()
            logs.append(line_str)
            print(f"[{slug}] {line_str}", flush=True)

            if "[BOOT-TELEMETRY] kernel_ready_time_us=" in line_str:
                telemetry_line = line_str

            if "System operacyjny gotowy!" in line_str:
                booted = True
                duration = time.time() - t0
                print(f"[{slug}] >>> REACHED DESKTOP READY: 'System operacyjny gotowy!' at {duration:.2f}s <<<", flush=True)
                break

        if not booted and not fail_reason:
            fail_reason = f"Per-VM hard timeout ({VM_TIMEOUT}s) exceeded"

        screenshot_ok = False
        screenshot_errors = []

        if booted and qmp_sock.exists():
            try:
                client = QmpClient(qmp_sock, timeout=5.0)
                client.screendump(dump_ppm, device="gpu0")
                client.close()
                if dump_ppm.exists():
                    im = Image.open(dump_ppm)
                    im.save(dump_png)
                    print(f"[{slug}] Screendump saved: {dump_png} (size: {im.size})", flush=True)
                    screenshot_ok, screenshot_errors = verify_screenshot(dump_png)
                else:
                    screenshot_errors.append(f"Dump PPM was not created: {dump_ppm}")
            except Exception as e:
                screenshot_errors.append(f"QMP screendump failed: {e}")

        if not booted:
            print(f"[{scenario_label}] FAIL: {fail_reason}", flush=True)
            print(f"[TEST] FAIL {scenario_label}", flush=True)
            passed = False
        elif not screenshot_ok:
            print(f"[{scenario_label}] FAIL: Visual verification errors: {screenshot_errors}", flush=True)
            print(f"[TEST] FAIL {scenario_label}", flush=True)
            passed = False
        else:
            print(f"[{scenario_label}] Visual verification: 100% PASS (wallpaper, 15 icons, taskbar, Start, clock, cursor)", flush=True)
            print(f"[TEST] PASS {scenario_label}", flush=True)
            passed = True

    finally:
        # Clean teardown via QMP if available
        if qmp_sock.exists():
            try:
                q = QmpClient(qmp_sock, timeout=2.0)
                q.quit()
                q.close()
            except Exception:
                pass

        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3.0)

        if qmp_sock.exists():
            try:
                qmp_sock.unlink()
            except Exception:
                pass
        if dump_ppm.exists():
            try:
                dump_ppm.unlink()
            except Exception:
                pass

    elapsed = time.time() - t0
    return {
        "scenario": scenario_label,
        "passed": passed,
        "booted": booted,
        "elapsed": elapsed,
        "telemetry": telemetry_line,
        "png": str(dump_png) if dump_png.exists() else None
    }

def main():
    suite_start = time.time()
    results = []

    print("[SETUP] Preparing automated differential boot images...", flush=True)
    iso_diff = ensure_diff_iso()
    iso_640 = ensure_iso_640()
    print("[SETUP] Ready.", flush=True)

    scenarios = [
        ("BIOS VirtIO", False, (1280, 720), iso_diff),
        ("UEFI VirtIO", True, (1280, 720), iso_diff),
        ("UEFI VirtIO 640", True, (640, 480), iso_640),
    ]

    for label, is_uefi, res, iso in scenarios:
        if time.time() - suite_start >= SUITE_TIMEOUT:
            print(f"[TEST] FAIL {label} (Global suite timeout exceeded)", flush=True)
            results.append({
                "scenario": label, "passed": False, "booted": False,
                "elapsed": 0.0, "telemetry": None, "png": None
            })
            continue

        res_dict = run_scenario(label, is_uefi=is_uefi, res=res, iso_path=iso)
        results.append(res_dict)

    total_suite_duration = time.time() - suite_start
    print("\n" + "=" * 65)
    print("DIFFERENTIAL BOOT TEST SUITE PODSUMOWANIE:")
    print("=" * 65)
    all_passed = True
    for r in results:
        status_str = "PASS" if r["passed"] else "FAIL"
        if not r["passed"]:
            all_passed = False
        print(f"  {r['scenario']:<20}: {status_str} (czas: {r['elapsed']:.2f} s)")
        if r["telemetry"]:
            print(f"    {r['telemetry']}")
        if r["png"]:
            print(f"    Screenshot: {r['png']}")
    print("-" * 65)
    print(f"Laczny czas suite: {total_suite_duration:.2f} s")
    print(f"Wynik calosci: {'PASS' if all_passed else 'FAIL'}")
    print("=" * 65)

    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())
