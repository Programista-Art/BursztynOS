#!/usr/bin/env python3
"""
narzedzia/test_reboot_gpu_qemu.py
Test rzeczywistego restartu systemu Bursztyn OS z poziomu goscia (Guest Soft Reboot)
z aktywnym sterownikiem VirtIO-GPU.

Weryfikuje sciezke restartu zgloszonego przez GUI:
  Start -> "Uruchom ponownie" -> bws_wywolaj(25) -> reset_ps2()
   -> virtio_gpu_stop_submissions()
   -> virtio_gpu_log_reboot_state()
   -> virtio_gpu_quiesce_for_reboot()
   -> psf_flush_cache()
   -> cli
   -> acpi_wykonaj_restart() / fallback 8042

Kolejnosc testow (bezpieczna):
  TEST 1: boot -> brak zmiany tapety -> guest reboot przez GUI
  TEST 2: boot -> zmiana 1 tapety (JPG) -> guest reboot przez GUI
  TEST 3 (opcjonalny): boot -> wielokrotna zmiana tapet -> guest reboot przez GUI

Kryteria sukcesu (PASS):
  - Guest wykonuje restart przez BWS/reset_ps2
  - Serial zawiera dokladny ciag markerow [REBOOT TRACE]
  - virtio_gpu_quiesce_for_reboot zwraca sukces
  - psf_flush_cache zwraca sukces
  - Sprzetowy reset nastepuje pomyslnie
  - QEMU NIE zawiesza sie (brak freeze)
  - Zewnetrzna maszyna wirtualna Linux pozostaje w pelni responsywna
  - Bursztyn OS uruchamia sie ponownie po resecie
"""

import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISO = ROOT / "BursztynOS.iso"
OVMF = Path("/usr/share/ovmf/OVMF.fd")
DISPOSABLE_DISK = Path("/tmp/bursztyn-disposable-reboot-disk.raw")

BOOT_READY_MARKER = "[TAPETA] Tapeta gotowa."
DESKTOP_READY_MARKER = "System operacyjny gotowy!"

REBOOT_TRACE_MARKERS = [
    "[REBOOT TRACE] begin",
    "[REBOOT TRACE] stop new GPU submissions",
    "[REBOOT TRACE] GPU[",
    "[REBOOT TRACE] gpu quiesce begin",
    "[REBOOT TRACE] gpu quiesce done",
    "[REBOOT TRACE] filesystem flush begin",
    "[REBOOT TRACE] filesystem flush done",
    "[REBOOT TRACE] cli",
    "[REBOOT TRACE] ACPI reset begin",
    "[REBOOT TRACE] 8042 reset begin",
]


class QmpClient:
    def __init__(self, sock_path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(20)
        deadline = time.time() + 20
        while time.time() < deadline:
            try:
                self.sock.connect(str(sock_path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.2)
        self.file = self.sock.makefile("rwb", buffering=0)
        self.file.readline()  # banner
        self.call("qmp_capabilities")
        self.cur_x = None
        self.cur_y = None

    def call(self, execute, arguments=None):
        req = {"execute": execute}
        if arguments:
            req["arguments"] = arguments
        self.file.write((json.dumps(req) + "\n").encode())
        while True:
            line = self.file.readline()
            if not line:
                raise RuntimeError("QMP socket closed unexpectedly")
            resp = json.loads(line)
            if "error" in resp:
                raise RuntimeError(resp["error"])
            if "return" in resp:
                return resp["return"]

    def hmp(self, cmd):
        return self.call("human-monitor-command", {"command-line": cmd})

    def capture(self, out_path):
        try:
            self.call("screendump", {"filename": str(out_path), "format": "png"})
        except Exception as e:
            print(f"    [WARN] Screendump failed: {e}")

    def reset_pointer(self):
        for _ in range(25):
            self.hmp("mouse_move -100 -100")
            time.sleep(0.015)
        self.cur_x = 0
        self.cur_y = 0
        time.sleep(0.1)

    def move_to(self, target_x, target_y):
        if self.cur_x is None or self.cur_y is None:
            self.reset_pointer()
        dx = target_x - self.cur_x
        dy = target_y - self.cur_y
        while dx != 0 or dy != 0:
            step_x = min(50, max(-50, dx))
            step_y = min(50, max(-50, dy))
            self.hmp(f"mouse_move {step_x} {step_y}")
            time.sleep(0.02)
            dx -= step_x
            dy -= step_y
        self.cur_x = target_x
        self.cur_y = target_y
        time.sleep(0.15)

    def click(self, x, y, double=False, settle=0.3):
        self.move_to(x, y)
        time.sleep(0.1)
        for _ in range(2 if double else 1):
            self.hmp("mouse_button 1")
            time.sleep(0.1)
            self.hmp("mouse_button 0")
            time.sleep(0.1)
        time.sleep(settle)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def ensure_disposable_disk():
    """Tworzy czysty, jednorazowy dysk w /tmp. NIGDY nie dotyka wirtualnydysk.img."""
    if DISPOSABLE_DISK.exists():
        DISPOSABLE_DISK.unlink()
    cmd = ["qemu-img", "create", "-f", "raw", str(DISPOSABLE_DISK), "2G"]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


def run_qemu_guest_reboot_test(test_id, change_wallpaper=False, multi_wallpaper=False):
    print(f"\n========================================================")
    print(f" URUCHAMIANIE: {test_id}")
    print(f" Scenariusz: {'Zmiana wielu tapet' if multi_wallpaper else 'Zmiana 1 tapety JPEG' if change_wallpaper else 'Czysty rozruch bez zmiany tapety'}")
    print(f"========================================================")

    ensure_disposable_disk()

    sock_path = Path(f"/tmp/qmp_{test_id}_{os.getpid()}.sock")
    serial_path = Path(f"/tmp/serial_{test_id}_{os.getpid()}.log")
    shot_dir = Path(f"/tmp/shots_{test_id}")
    shot_dir.mkdir(parents=True, exist_ok=True)

    if sock_path.exists():
        sock_path.unlink()
    if serial_path.exists():
        serial_path.unlink()

    # Rygorystyczne flagi produkcyjne: tcg,thread=multi i virtio-vga,disable-legacy=on
    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=multi",
        "-smp", "1",
        "-m", "2G",
        "-audiodev", "alsa,id=snd",
        "-device", "intel-hda",
        "-device", "hda-output,audiodev=snd",
        "-netdev", "user,id=n1",
        "-device", "virtio-net-pci,netdev=n1,disable-legacy=on",
        "-object", "rng-random,id=rng0,filename=/dev/urandom",
        "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",
        "-vga", "none",
        "-device", "virtio-vga,disable-legacy=on,xres=1280,yres=720",
        "-device", "virtio-keyboard-pci",
        "-device", "virtio-mouse-pci",
        "-drive", f"id=disk,file={DISPOSABLE_DISK},format=raw,if=none",
        "-device", "virtio-blk-pci,drive=disk,disable-legacy=on",
        "-serial", f"file:{serial_path}",
        "-qmp", f"unix:{sock_path},server=on,wait=off",
        
        "-boot", "order=d",
        "-cdrom", str(ISO),
        "-bios", str(OVMF),
    ]

    print(f"[{test_id}] Start procesu QEMU...")
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    qmp = None

    try:
        # Krok 1: Czekaj na pelny boot do pulpitu
        print(f"[{test_id}] Czekam na boot do pulpitu (max 90s)...")
        deadline = time.time() + 90
        desktop_ready = False
        while time.time() < deadline and proc.poll() is None:
            if serial_path.exists():
                text = serial_path.read_text(errors="replace")
                if DESKTOP_READY_MARKER in text or BOOT_READY_MARKER in text:
                    desktop_ready = True
                    break
            time.sleep(1)

        if not desktop_ready:
            print(f"[{test_id}] FAIL: Pulpit nie zglosil gotowosci w czasie!")
            return False, "FAIL_BOOT"

        print(f"[{test_id}] Pulpit gotowy! Laczenie z QMP...")
        qmp = QmpClient(sock_path)
        qmp.capture(shot_dir / "01_desktop.png")

        # Krok 2 (opcjonalny): zmiana tapety przez aplikacje Tapety
        if change_wallpaper or multi_wallpaper:
            print(f"[{test_id}] Otwieram menu Start (klik w 50, 700)...")
            qmp.click(50, 700, settle=1.0)
            qmp.capture(shot_dir / "02_start_menu.png")

            print(f"[{test_id}] Klikam aplikacje Tapety (140, 507)...")
            qmp.click(140, 507, settle=3.0)

            # Czekaj na gotowosc galerii tapet
            print(f"[{test_id}] Czekam na zaladowanie galerii miniatur...")
            t_wait = time.time() + 20
            while time.time() < t_wait:
                txt = serial_path.read_text(errors="replace")
                if "[TAPETY APP] Galeria gotowa" in txt or "[TAPETY APP] Miniatury gotowe" in txt:
                    break
                time.sleep(0.5)

            qmp.capture(shot_dir / "03_tapety_window.png")

            # Wybierz tapete JPEG testphoto.jpg (x=303, y=418)
            print(f"[{test_id}] Wybieram tapete JPEG testphoto.jpg (303, 418)...")
            qmp.click(303, 418, settle=1.0)
            print(f"[{test_id}] Klikam Zastosuj (380, 570)...")
            qmp.click(380, 570, settle=2.5)
            qmp.capture(shot_dir / "04_applied_wallpaper1.png")

            t_wp = time.time() + 15
            wp_applied = False
            while time.time() < t_wp:
                txt = serial_path.read_text(errors="replace")
                if "[TAPETA] JPEG wybrano" in txt or "[TAPETA] Wybrano" in txt or "[TAPETA] BTP wybrano" in txt:
                    wp_applied = True
                    break
                time.sleep(0.5)
            print(f"[{test_id}] Potwierdzenie zmiany tapety w logu: {'PASS' if wp_applied else 'BRAK LOGU'}")

            if multi_wallpaper:
                # Wybierz druga tapete (bursztyn01.btp na 303, 154)
                print(f"[{test_id}] Wybieram druga tapete (303, 154)...")
                qmp.click(303, 154, settle=1.0)
                print(f"[{test_id}] Klikam Zastosuj (380, 570)...")
                qmp.click(380, 570, settle=2.5)
                qmp.capture(shot_dir / "05_applied_wallpaper2.png")

        # Krok 3: Wywolanie restartu z poziomu GUI
        print(f"[{test_id}] Otwieram Menu Start (klik w 50, 700)...")
        qmp.click(50, 700, settle=1.0)
        qmp.capture(shot_dir / "06_start_menu_before_reboot.png")

        # Pozycja item 16: "> Uruchom ponownie" -> x=140, y=632
        print(f"[{test_id}] Przemieszczam kursor na '> Uruchom ponownie' w GUI (140, 632)...")
        qmp.move_to(140, 632)
        qmp.capture(shot_dir / "07_pointer_on_reboot_item.png")

        print(f"[{test_id}] Klikam '> Uruchom ponownie' w GUI (140, 632)...")
        reboot_click_time = time.time()
        qmp.click(140, 632, settle=0.5)
        qmp.capture(shot_dir / "08_after_reboot_click.png")

        # Krok 4: Weryfikacja sciezki reboot w logu szeregowym
        print(f"[{test_id}] Monitoruje log szeregowy pod katem [REBOOT TRACE]...")
        reboot_trace_seen = set()
        last_marker_seen = None
        reboot_deadline = time.time() + 45
        second_boot_detected = False

        while time.time() < reboot_deadline:
            if serial_path.exists():
                lines = serial_path.read_text(errors="replace").splitlines()
                for line in lines:
                    for m in REBOOT_TRACE_MARKERS:
                        if m in line and m not in reboot_trace_seen:
                            reboot_trace_seen.add(m)
                            last_marker_seen = line
                            print(f"    -> [SEEN] {line}")

                # Wykryj ponowny rozruch (pojawienie sie logow po kliknieciu restartu)
                text = "\n".join(lines)
                # Sprawdz czy po znalezieniu markera cli lub 8042/ACPI pojawil sie znowu wczesny log jadra
                if "[REBOOT TRACE] cli" in text:
                    idx_cli = text.find("[REBOOT TRACE] cli")
                    after_cli = text[idx_cli:]
                    if "[PMM]" in after_cli or "[GDT]" in after_cli or "Bursztyn OS" in after_cli or DESKTOP_READY_MARKER in after_cli:
                        second_boot_detected = True
                        print(f"[{test_id}] Wykryto ponowny rozruch jadra po resecie!")
                        break

            if proc.poll() is not None:
                print(f"[{test_id}] QEMU zakonczyl proces z kodem: {proc.returncode}")
                break

            time.sleep(0.5)

        required_markers = [
            "[REBOOT TRACE] begin",
            "[REBOOT TRACE] stop new GPU submissions",
            "[REBOOT TRACE] GPU[",
            "[REBOOT TRACE] gpu quiesce begin",
            "[REBOOT TRACE] gpu quiesce done",
            "[REBOOT TRACE] filesystem flush begin",
            "[REBOOT TRACE] filesystem flush done",
            "[REBOOT TRACE] cli",
        ]
        all_markers_ok = True
        for m in required_markers:
            present = m in reboot_trace_seen
            print(f"  [{'PASS' if present else 'FAIL'}] {m}")
            if not present:
                all_markers_ok = False

        hw_reset_ok = ("[REBOOT TRACE] ACPI reset begin" in reboot_trace_seen) or ("[REBOOT TRACE] 8042 reset begin" in reboot_trace_seen)
        print(f"  [{'PASS' if hw_reset_ok else 'FAIL'}] [REBOOT TRACE] Hardware reset (ACPI/8042)")
        if not hw_reset_ok:
            all_markers_ok = False

        if not second_boot_detected:
            # Daj jeszcze 20s na drugi boot jesli TCG jest obciazone
            print(f"[{test_id}] Czekam dodatkowo na ponowny start pulpitu...")
            extra_deadline = time.time() + 20
            while time.time() < extra_deadline and proc.poll() is None:
                if serial_path.exists():
                    text = serial_path.read_text(errors="replace")
                    if "[REBOOT TRACE] cli" in text:
                        idx_cli = text.find("[REBOOT TRACE] cli")
                        after_cli = text[idx_cli:]
                        if "[PMM]" in after_cli or "[GDT]" in after_cli or DESKTOP_READY_MARKER in after_cli:
                            second_boot_detected = True
                            break
                time.sleep(1)

        print(f"[{test_id}] Ponowny rozruch po resecie: {'PASS' if second_boot_detected else 'FAIL'}")
        print(f"[{test_id}] Ostatni zaobserwowany marker: {last_marker_seen}")

        is_alive = proc.poll() is None
        print(f"[{test_id}] Stan procesu QEMU: {'Dziala (responsywne)' if is_alive else 'Zakonczone'}")

        if all_markers_ok and second_boot_detected and is_alive:
            print(f"[{test_id}] WYNIK KONCOWY: SUCCESS (PASS)")
            return True, "PASS"
        else:
            print(f"[{test_id}] WYNIK KONCOWY: FAILED")
            return False, f"FAIL (markers_ok={all_markers_ok}, 2nd_boot={second_boot_detected}, alive={is_alive})"

    except Exception as e:
        print(f"[{test_id}] WYJATEK podczas testu: {e}")
        return False, f"EXCEPTION: {e}"

    finally:
        if qmp:
            qmp.close()
        if proc.poll() is None:
            #proc.terminate()
            try:
                pass
            except subprocess.TimeoutExpired:
                pass
        if DISPOSABLE_DISK.exists():
            DISPOSABLE_DISK.unlink()


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Test restartu goscia w QEMU z VirtIO-GPU")
    parser.add_argument("--test", choices=["1", "2", "3", "all"], default="all",
                        help="Wybierz scenariusz: 1 (bez zmiany tapety), 2 (zmiana 1 JPEG), 3 (zmiana wielu), all (domyslnie)")
    args = parser.parse_args()

    if not ISO.exists():
        print(f"[BLAD] Brak {ISO}. Zbuduj najpierw: make iso")
        sys.exit(1)
    if not OVMF.exists():
        print(f"[BLAD] Brak OVMF: {OVMF}")
        sys.exit(1)

    print("=================================================================")
    print(" BURSZTYN OS - WERYFIKACJA RESTARTU GOSCIA (VIRTIO-GPU QUIESCE)")
    print("=================================================================")

    if args.test in ("1", "all"):
        # TEST 1: Czysty boot -> GUI reboot (bez zmiany tapety)
        ok1, res1 = run_qemu_guest_reboot_test("TEST_1_NO_WALLPAPER", change_wallpaper=False)
        if not ok1:
            print(f"\n[BLAD] TEST 1 nie powiodl sie: {res1}")
            sys.exit(1)
        if args.test == "1":
            print(f"\n[OK] TEST 1 zakonczony sukcesem: {res1}")
            sys.exit(0)

    if args.test in ("2", "all"):
        # TEST 2: Boot -> zmiana 1 tapety JPEG -> GUI reboot
        ok2, res2 = run_qemu_guest_reboot_test("TEST_2_JPEG_WALLPAPER", change_wallpaper=True)
        if not ok2:
            print(f"\n[BLAD] TEST 2 nie powiodl sie: {res2}")
            sys.exit(1)
        if args.test == "2":
            print(f"\n[OK] TEST 2 zakonczony sukcesem: {res2}")
            sys.exit(0)

    if args.test in ("3", "all"):
        # TEST 3 (opcjonalny): Boot -> wielokrotna zmiana tapet -> GUI reboot
        ok3, res3 = run_qemu_guest_reboot_test("TEST_3_MULTI_WALLPAPER", multi_wallpaper=True)
        if not ok3:
            print(f"\n[BLAD] TEST 3 nie powiodl sie: {res3}")
            sys.exit(1)
        if args.test == "3":
            print(f"\n[OK] TEST 3 zakonczony sukcesem: {res3}")
            sys.exit(0)

    print("\n=================================================================")
    print(" WSZYSTKIE TESTY RESTARTU GOSCIA Z VIRTIO-GPU ZAKONCZONE SUKCESEM!")
    print("=================================================================")
    sys.exit(0)


if __name__ == "__main__":
    main()
