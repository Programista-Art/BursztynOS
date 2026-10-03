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
QMP_SOCK = Path("/tmp/qmp-shell-val.sock")
DUMP_DIR = Path("/tmp/shell_val_dumps")
DUMP_DIR.mkdir(parents=True, exist_ok=True)

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

print(f"[TEST RUNNER] Starting single QEMU UEFI run at {screen_w}x{screen_h}...")
proc = subprocess.Popen(cmd)

start_time = time.time()
timeout_seconds = 600
desktop_seen_time = None
enter_sent = False
test_executed = False

def send_qmp_command(sock, cmd_name, args=None):
    req = {"execute": cmd_name}
    if args:
        req["arguments"] = args
    sock.sendall((json.dumps(req) + "\n").encode())
    buf = ""
    while True:
        data = sock.recv(4096).decode('utf-8', errors='replace')
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

def qmp_drag(sock, from_x, from_y, to_x, to_y, steps=10):
    qmp_move(sock, from_x, from_y)
    time.sleep(0.1)
    send_qmp_command(sock, "input-send-event", {
        "events": [{"type": "btn", "data": {"button": "left", "down": True}}]
    })
    time.sleep(0.1)
    for i in range(1, steps + 1):
        cur_x = int(from_x + (to_x - from_x) * i / steps)
        cur_y = int(from_y + (to_y - from_y) * i / steps)
        qmp_move(sock, cur_x, cur_y)
        time.sleep(0.05)
    send_qmp_command(sock, "input-send-event", {
        "events": [{"type": "btn", "data": {"button": "left", "down": False}}]
    })
    time.sleep(0.1)

def qmp_wheel_up(sock, count=3):
    for _ in range(count):
        send_qmp_command(sock, "input-send-event", {
            "events": [
                {"type": "btn", "data": {"button": "wheel-up", "down": True}},
                {"type": "btn", "data": {"button": "wheel-up", "down": False}}
            ]
        })
        time.sleep(0.1)

def qmp_wheel_down(sock, count=3):
    for _ in range(count):
        send_qmp_command(sock, "input-send-event", {
            "events": [
                {"type": "btn", "data": {"button": "wheel-down", "down": True}},
                {"type": "btn", "data": {"button": "wheel-down", "down": False}}
            ]
        })
        time.sleep(0.1)

def send_key(sock, key):
    send_qmp_command(sock, "human-monitor-command", {"command-line": f"sendkey {key}"})
    time.sleep(0.04)

try:
    while time.time() - start_time < timeout_seconds:
        if proc.poll() is not None:
            print("[TEST RUNNER] QEMU process exited prematurely.")
            break



        if SERIAL_LOG.exists():
            try:
                content = SERIAL_LOG.read_text(errors="replace")
            except Exception:
                content = ""

            if "[TAPETA] Tapeta gotowa." in content and not test_executed:
                if desktop_seen_time is None:
                    print("[TEST RUNNER] Reached [TAPETA] Tapeta gotowa.")
                    desktop_seen_time = time.time()

                if time.time() - desktop_seen_time > 2.0:
                    test_executed = True
                    try:
                        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                        s.connect(str(QMP_SOCK))
                        s.recv(4096)
                        send_qmp_command(s, "qmp_capabilities")
                        print("[TEST RUNNER] Connected to QMP.")

                        # 1. Open Start menu
                        start_y = screen_h - 20
                        qmp_move(s, 35, start_y)
                        time.sleep(0.3)
                        qmp_click(s)
                        print(f"[TEST RUNNER] Clicked Start button at (35, {start_y}).")
                        time.sleep(0.8)

                        # 2. Click "Powłoka Bursztyna" (item 0)
                        menu_y = screen_h - 40 - 470
                        item0_y = menu_y + 10 + 12
                        qmp_move(s, 100, item0_y)
                        time.sleep(0.3)
                        qmp_click(s)
                        print(f"[TEST RUNNER] Clicked Powłoka Bursztyna at (100, {item0_y}).")
                        time.sleep(1.5)

                        # Save dump 01: shell opened
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "01_shell_opened.ppm")})
                        print("[TEST RUNNER] Saved 01_shell_opened.ppm")

                        # 3. Type ASCII fast: abcdefghijklmnop
                        print("[TEST RUNNER] Typing ASCII: abcdefghijklmnop...")
                        for ch in "abcdefghijklmnop":
                            send_key(s, ch)
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "02_shell_ascii.ppm")})
                        print("[TEST RUNNER] Saved 02_shell_ascii.ppm")

                        # 4. Type Polish UTF-8: Zażółć gęślą jaźń
                        print("[TEST RUNNER] Typing Polish UTF-8: Zażółć gęślą jaźń...")
                        polish_keys = [
                            "shift-z", "a", "alt_r-z", "alt_r-o", "alt_r-l", "alt_r-c", "spc",
                            "g", "alt_r-e", "alt_r-s", "l", "alt_r-a", "spc",
                            "j", "a", "alt_r-x", "alt_r-n"
                        ]
                        for k in polish_keys:
                            send_key(s, k)
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "03_shell_polish.ppm")})
                        print("[TEST RUNNER] Saved 03_shell_polish.ppm")

                        # 5. Repeatedly Backspace
                        print("[TEST RUNNER] Pressing Backspace repeatedly...")
                        for _ in range(40):
                            send_key(s, "backspace")
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "04_shell_backspace.ppm")})
                        print("[TEST RUNNER] Saved 04_shell_backspace.ppm")

                        # 6. Type "pomoc" + Enter
                        print("[TEST RUNNER] Typing 'pomoc' + Enter...")
                        for ch in "pomoc":
                            send_key(s, ch)
                        send_key(s, "ret")
                        time.sleep(1.0)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "05_shell_pomoc.ppm")})
                        print("[TEST RUNNER] Saved 05_shell_pomoc.ppm")

                        # 7. Type another "pomoc" + Enter to fill scrollback and verify vertical scrollbar
                        print("[TEST RUNNER] Typing 'pomoc' + Enter again for scrollback overflow...")
                        for ch in "pomoc":
                            send_key(s, ch)
                        send_key(s, "ret")
                        time.sleep(1.0)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "06_shell_scrollbar.ppm")})
                        print("[TEST RUNNER] Saved 06_shell_scrollbar.ppm")

                        # 8. Test PageUp
                        print("[TEST RUNNER] Testing PageUp...")
                        send_key(s, "pgup")
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "07_shell_pageup.ppm")})
                        print("[TEST RUNNER] Saved 07_shell_pageup.ppm")

                        # 9. Test PageDown
                        print("[TEST RUNNER] Testing PageDown...")
                        send_key(s, "pgdn")
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "08_shell_pagedown.ppm")})
                        print("[TEST RUNNER] Saved 08_shell_pagedown.ppm")

                        # 10. Test mouse wheel
                        print("[TEST RUNNER] Testing mouse wheel up inside shell...")
                        # Shell is centered around (290, 115) to (990, 565). Move to (500, 300)
                        qmp_move(s, 500, 300)
                        time.sleep(0.3)
                        qmp_wheel_up(s, count=5)
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "09_shell_wheel_up.ppm")})
                        print("[TEST RUNNER] Saved 09_shell_wheel_up.ppm")

                        # 11. Test scrollbar thumb drag
                        print("[TEST RUNNER] Testing scrollbar thumb drag...")
                        # Scrollbar track is at client right: x = 290 + 698 - 14 = ~974..986.
                        # Thumb is near top or middle. Let's drag from (980, 200) down to (980, 450)
                        qmp_drag(s, 980, 200, 980, 450, steps=8)
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "10_shell_sb_dragged.ppm")})
                        print("[TEST RUNNER] Saved 10_shell_sb_dragged.ppm")

                        # 12. Test window drag
                        print("[TEST RUNNER] Testing window dragging by titlebar...")
                        # Titlebar is at (290, 115) to (990, 143). Drag from (400, 130) to (460, 180)
                        qmp_drag(s, 400, 130, 460, 180, steps=6)
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "11_shell_window_dragged.ppm")})
                        print("[TEST RUNNER] Saved 11_shell_window_dragged.ppm")

                        # 12b. Test close button
                        print("[TEST RUNNER] Closing shell window...")
                        qmp_move(s, 1034, 179)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(0.8)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "12_shell_closed.ppm")})
                        print("[TEST RUNNER] Saved 12_shell_closed.ppm")

                        # 13. Open Start Menu and launch Ustawienia Ekranu to switch to 125% scale
                        print("[TEST RUNNER] Opening Ustawienia ekranu...")
                        qmp_move(s, 35, start_y)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(0.5)

                        # Item 12: Ustawienia ekranu at y=532
                        qmp_move(s, 100, 532)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(1.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "13_ekran_opened.ppm")})
                        print("[TEST RUNNER] Saved 13_ekran_opened.ppm")

                        # 14. Select 125% scale in combobox, click Zastosuj
                        print("[TEST RUNNER] Selecting 125% scale...")
                        # Scale combobox is at (400, 390)
                        qmp_move(s, 400, 390)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(0.3)
                        send_key(s, "down")
                        time.sleep(0.2)
                        send_key(s, "ret")
                        time.sleep(0.5)
                        # Click Zastosuj button at (295, 465)
                        qmp_move(s, 295, 465)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(1.0)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "14_scale_changed_125.ppm")})
                        print("[TEST RUNNER] Saved 14_scale_changed_125.ppm")

                        # Close Ustawienia ekranu window at (916, 182)
                        print("[TEST RUNNER] Closing Ustawienia ekranu...")
                        qmp_move(s, 916, 182)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(0.8)

                        # 15. Re-open Powłoka Bursztynowa at 125% UI scale
                        print("[TEST RUNNER] Re-opening Powłoka Bursztynowa at 125% UI scale...")
                        qmp_move(s, 35, start_y)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(0.5)

                        # At 125% scale, Item 0 (> Powloka Bursztyna) is at (100, 115)
                        qmp_move(s, 100, 115)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(1.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "15_shell_125scale_opened.ppm")})
                        print("[TEST RUNNER] Saved 15_shell_125scale_opened.ppm")

                        # 16. Type fast ASCII + Polish UTF-8 at 125% scale
                        print("[TEST RUNNER] Typing at 125% scale...")
                        for ch in "abcdefghijklmnop":
                            send_key(s, ch)
                        for k in polish_keys:
                            send_key(s, k)
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "16_shell_125scale_typing.ppm")})
                        print("[TEST RUNNER] Saved 16_shell_125scale_typing.ppm")

                        # 17. Repeatedly Backspace at 125% scale
                        print("[TEST RUNNER] Pressing Backspace at 125% scale...")
                        for _ in range(40):
                            send_key(s, "backspace")
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "17_shell_125scale_backspace.ppm")})
                        print("[TEST RUNNER] Saved 17_shell_125scale_backspace.ppm")

                        # 18. Window dragging at 125% scale
                        print("[TEST RUNNER] Dragging window at 125% scale...")
                        # Window is centered around (202, 79). Titlebar y=79..114. Drag from (400, 95) to (460, 140)
                        qmp_drag(s, 400, 95, 460, 140, steps=6)
                        time.sleep(0.5)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "18_shell_125scale_dragged.ppm")})
                        print("[TEST RUNNER] Saved 18_shell_125scale_dragged.ppm")

                        # 19. Close window at 125% scale
                        print("[TEST RUNNER] Closing window at 125% scale...")
                        # Dragged to x=262, y=124. Width=875, Height=562. Close button at (262 + 875 - 20, 124 + 17) = (1117, 141)
                        qmp_move(s, 1117, 141)
                        time.sleep(0.3)
                        qmp_click(s)
                        time.sleep(1.0)
                        send_qmp_command(s, "screendump", {"filename": str(DUMP_DIR / "19_shell_125scale_closed.ppm")})
                        print("[TEST RUNNER] Saved 19_shell_125scale_closed.ppm")

                        s.close()
                        print("[TEST RUNNER] All steps completed successfully!")
                        break
                    except Exception as ex:
                        print(f"[TEST RUNNER] Error during test execution: {ex}")
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

print("[TEST RUNNER] Execution finished.")
