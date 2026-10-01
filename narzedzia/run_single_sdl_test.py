#!/usr/bin/env python3
import subprocess
import time
import sys
import os
import signal
import socket
import json
from pathlib import Path

SERIAL_LOG = Path("/home/programista/Pulpit/Burstzyn/BursztynOS/serial_output.log")
ISO = Path("/home/programista/Pulpit/Burstzyn/BursztynOS/BursztynOS.iso")
DISK = Path("/home/programista/Pulpit/Burstzyn/BursztynOS/wirtualny_dysk.img")
OVMF = Path("/usr/share/ovmf/OVMF.fd")
QMP_SOCK = Path("/tmp/qmp-sdl-mouse.sock")

if SERIAL_LOG.exists():
    SERIAL_LOG.unlink()
if QMP_SOCK.exists():
    QMP_SOCK.unlink()

cmd = [
    "qemu-system-x86_64",
    "-bios", str(OVMF),
    "-machine", "pc,hpet=on",
    "-cpu", "max",
    "-accel", "tcg,thread=multi",
    "-smp", "1",
    "-m", "2G",
    "-audiodev", "alsa,id=snd",
    "-device", "intel-hda",
    "-device", "hda-output,audiodev=snd",
    "-display", "sdl",
    "-serial", f"file:{SERIAL_LOG}",
    "-qmp", f"unix:{QMP_SOCK},server,nowait",
    "-drive", f"id=disk,file={DISK},format=raw,if=none",
    "-device", "virtio-blk-pci,drive=disk,disable-legacy=on",
    "-netdev", "user,id=n1",
    "-device", "virtio-net-pci,netdev=n1,disable-legacy=on",
    "-object", "rng-random,id=rng0,filename=/dev/urandom",
    "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",
    "-vga", "none",
    "-device", "virtio-vga,disable-legacy=on,xres=1280,yres=720",
    "-device", "virtio-keyboard-pci",
    "-device", "virtio-mouse-pci",
    "-boot", "order=d",
    "-cdrom", str(ISO),
]

print("[TEST RUNNER] Starting single QEMU SDL run...")
proc = subprocess.Popen(cmd)

start_time = time.time()
timeout_seconds = 300
desktop_seen_time = None
timeout_seen = False
motion_sent = False
enter_sent = False

def send_qmp_command(sock, cmd_name, args=None):
    req = {"execute": cmd_name}
    if args:
        req["arguments"] = args
    sock.sendall((json.dumps(req) + "\n").encode())
    buf = ""
    while True:
        data = sock.recv(4096).decode()
        if not data:
            break
        buf += data
        lines = buf.split("\n")
        for line in lines[:-1]:
            if line.strip():
                try:
                    obj = json.loads(line)
                    if "return" in obj or "error" in obj:
                        return obj
                except json.JSONDecodeError:
                    pass
        buf = lines[-1]
    return {}

try:
    while time.time() - start_time < timeout_seconds:
        if proc.poll() is not None:
            print("[TEST RUNNER] QEMU process exited prematurely.")
            break

        # Fast-track GRUB by pressing enter once OVMF has loaded
        if not enter_sent and (time.time() - start_time > 15.0) and QMP_SOCK.exists():
            try:
                s_grub = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s_grub.connect(str(QMP_SOCK))
                s_grub.recv(4096)
                send_qmp_command(s_grub, "qmp_capabilities")
                send_qmp_command(s_grub, "human-monitor-command", {"command-line": "sendkey ret"})
                s_grub.close()
                enter_sent = True
                print("[TEST RUNNER] Sent enter to GRUB.")
            except Exception:
                pass

        if SERIAL_LOG.exists():
            try:
                content = SERIAL_LOG.read_text(errors="replace")
            except Exception:
                content = ""

            if "[VIRTIO-GPU-DIAG]" in content or "Timeout polecenia kontrolnego" in content:
                print("[TEST RUNNER] Detected GPU timeout diagnostic in serial output!")
                timeout_seen = True
                time.sleep(3)
                break

            if "[TAPETA] Tapeta gotowa." in content:
                if desktop_seen_time is None:
                    print("[TEST RUNNER] Reached [TAPETA] Tapeta gotowa.")
                    desktop_seen_time = time.time()

                # Send test mouse movement via QMP after desktop has been ready for 2s
                if not motion_sent and (time.time() - desktop_seen_time > 2.0):
                    motion_sent = True
                    try:
                        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                        s.connect(str(QMP_SOCK))
                        s.recv(4096)
                        send_qmp_command(s, "qmp_capabilities")
                        print("[TEST RUNNER] Connected to QMP at desktop.")
                        # Check mice
                        mice_resp = send_qmp_command(s, "human-monitor-command", {"command-line": "info mice"})
                        print(f"[TEST RUNNER] QEMU info mice:\n{mice_resp.get('return', '')}")
                        # Send relative mouse motion: dx=47, dy=0
                        move_resp = send_qmp_command(s, "human-monitor-command", {"command-line": "mouse_move 47 0"})
                        print(f"[TEST RUNNER] Sent mouse_move 47 0: {move_resp}")
                        time.sleep(1.0)
                        # Send second relative mouse motion: dx=20, dy=30
                        move_resp2 = send_qmp_command(s, "human-monitor-command", {"command-line": "mouse_move 20 30"})
                        print(f"[TEST RUNNER] Sent mouse_move 20 30: {move_resp2}")
                        time.sleep(1.0)
                        # Take screendump
                        dump_resp = send_qmp_command(s, "screendump", {"filename": "/tmp/sdl_mouse_dump.ppm"})
                        print(f"[TEST RUNNER] Screendump saved: {dump_resp}")
                        s.close()
                    except Exception as ex:
                        print(f"[TEST RUNNER] QMP error: {ex}")

                if desktop_seen_time is not None and (time.time() - desktop_seen_time > 8.0):
                    print("[TEST RUNNER] Observation period completed. Ending test cleanly.")
                    break

        time.sleep(0.5)
finally:
    if proc.poll() is None:
        print("[TEST RUNNER] Terminating QEMU...")
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

print("[TEST RUNNER] QEMU execution completed.")
