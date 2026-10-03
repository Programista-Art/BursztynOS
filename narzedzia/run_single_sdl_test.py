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

res_arg = "1280x720"
for arg in sys.argv[1:]:
    if arg.startswith("--res="):
        res_arg = arg.split("=")[1]
    elif arg == "--1080p":
        res_arg = "1920x1080"
screen_w, screen_h = map(int, res_arg.split("x"))

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
    "-device", f"virtio-vga,disable-legacy=on,xres={screen_w},yres={screen_h}",
    "-device", "virtio-keyboard-pci",
    "-device", "virtio-tablet-pci",
    "-boot", "order=d",
    "-cdrom", str(ISO),
]

print(f"[TEST RUNNER] Starting single QEMU SDL run at {screen_w}x{screen_h}...")
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
                        def qmp_move(sock, px, py):
                            vx = int(px * 32767 / screen_w)
                            vy = int(py * 32767 / screen_h)
                            send_qmp_command(sock, "input-send-event", {
                                "events": [
                                    {"type": "abs", "data": {"axis": "x", "value": vx}},
                                    {"type": "abs", "data": {"axis": "y", "value": vy}}
                                ]
                            })

                        def qmp_click(sock):
                            send_qmp_command(sock, "input-send-event", {
                                "events": [{"type": "btn", "data": {"button": "left", "down": True}}]
                            })
                            time.sleep(0.1)
                            send_qmp_command(sock, "input-send-event", {
                                "events": [{"type": "btn", "data": {"button": "left", "down": False}}]
                            })

                        # Move to Start button: x=35, y=screen_h - 20
                        start_y = screen_h - 20
                        qmp_move(s, 35, start_y)
                        time.sleep(0.5)
                        qmp_click(s)
                        print(f"[TEST RUNNER] Clicked Start button at (35, {start_y}).")
                        time.sleep(1.0)

                        # Move to "Powłoka Bursztyna": x=100, y=(screen_h - 40 - 470) + 10 + 12
                        menu_y = screen_h - 40 - 470
                        item0_y = menu_y + 10 + 12
                        qmp_move(s, 100, item0_y)
                        time.sleep(0.5)
                        qmp_click(s)
                        print(f"[TEST RUNNER] Clicked Powłoka Bursztyna at (100, {item0_y}).")
                        time.sleep(2.0)

                        # Take screendump of opened shell
                        dump_resp = send_qmp_command(s, "screendump", {"filename": "/tmp/sdl_mouse_dump.ppm"})
                        print(f"[TEST RUNNER] Screendump saved: {dump_resp}")

                        # Type "pomoc" + Enter into shell
                        print("[TEST RUNNER] Typing 'pomoc' into shell...")
                        for k in ["p", "o", "m", "o", "c", "ret"]:
                            send_qmp_command(s, "human-monitor-command", {"command-line": f"sendkey {k}"})
                            time.sleep(0.1)

                        time.sleep(1.0)
                        # Take screendump after typing
                        dump_resp2 = send_qmp_command(s, "screendump", {"filename": "/tmp/sdl_shell_type_dump.ppm"})
                        print(f"[TEST RUNNER] Screendump 2 saved: {dump_resp2}")
                        s.close()
                    except Exception as ex:
                        print(f"[TEST RUNNER] QMP error: {ex}")

                if desktop_seen_time is not None and (time.time() - desktop_seen_time > 12.0):
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
