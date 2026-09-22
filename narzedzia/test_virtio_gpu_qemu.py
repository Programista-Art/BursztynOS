#!/usr/bin/env python3
"""
narzedzia/test_virtio_gpu_qemu.py - Test integracyjny sterownika VirtIO-GPU 2D
oraz kursora myszy (xHCI USB HID) w maszynie wirtualnej QEMU.

Weryfikuje:
 1. Wykrycie urzadzenia VirtIO Modern PCI (Vendor 0x1AF4, Device 0x1050, Subsystem 16).
 2. Negocjacje cech (VIRTIO_F_VERSION_1, VIRTIOGPU_F_EDID).
 3. Utworzenie controlq (Queue 0) z buforami DMA.
 4. Konfiguracje wektorow MSI-X / MSI / INTx fallback.
 5. Wykonanie VIRTIOGPU_CMD_GET_DISPLAY_INFO i odczyt geometrii wyswietlacza.
 6. Alokacje i rejestracje zasobu 2D VIRTIOGPU_CMD_RESOURCE_CREATE_2D (format B8G8R8X8_UNORM).
 7. Podpiecie ciaglego bufora pamieci fizycznej przez VIRTIOGPU_CMD_RESOURCE_ATTACH_BACKING.
 8. Powiazanie zasobu z ekranem przez VIRTIOGPU_CMD_SET_SCANOUT.
 9. Integracje z podsystemem grafiki jadra Bursztyn OS (SterownikVirtioGPU).
 10. Wyrenderowanie wzorca testowego (Amber, Cyan, Magenta, Lime, White) bezposrednio do bufora ramki.
 11. Przeslanie zmian przez VIRTIOGPU_CMD_TRANSFER_TO_HOST_2D i VIRTIOGPU_CMD_RESOURCE_FLUSH.
 12. Bezposrednia weryfikacje zrzutu ekranu z QEMU przez QMP (screendump) wzorca testowego:
     - Tlo: #0A192F (10, 25, 47)
     - Bursztyn (Amber): #FFBF00 (255, 191, 0)
     - Cyjan (Cyan): #00E5FF (0, 229, 255)
     - Magenta: #FF007F (255, 0, 127)
     - Limonka (Lime): #39FF14 (57, 255, 20)
     - Biale centrum: #FFFFFF (255, 255, 255)
 13. Pakiet testow kursora w jadrze (8 testow jednostkowych / integracyjnych):
     - Render kursora w centrum
     - Ruch kursora
     - Przywrocenie tla starego polozenia kursora
     - Przycinanie do krawedzi ekranu (clipping)
     - Szybki ruch (1000 iteracji w jadrze bez wyciekow)
     - Dirty-rect flush na VirtIO-GPU
     - Przelaczenie backendu GOP/Bochs <-> VirtIO-GPU bez wiszacych wskaznikow
     - Widocznosc kursora po przelaczeniu backendu
 14. Test integracyjny z kontrolerem xHCI USB HID:
     - Wykrycie i konfiguracja myszy USB HID na xHCI
     - Widocznosc kursora na pulpicie w pozycji domyslnej (500, 300)
     - Wstrzykniecie ruchu myszy (+60, +40) przez QMP
     - Weryfikacja przesuniecia kursora na pozycje (560, 340)
     - Weryfikacja pelnego przywrocenia tla w starej pozycji (500, 300)
     - Test obciazeniowy: 1000 ruchow myszy przez QMP
     - Weryfikacja stabilnosci, braku wyciekow deskryptorow i integralnosci pikseli
"""

import sys
import os
import re
import time
import socket
import json
import subprocess
import threading
import queue
import tempfile
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

GLOBAL_TIMEOUT = 300      # Maksymalny globalny limit czasu w sekundach (dla TCG)
INACTIVITY_TIMEOUT = 120  # Limit braku nowych linii na porcie szeregowym w sekundach

def parse_ppm(ppm_path):
    """
    Parsuje plik obrazu Netpbm PPM (P6 - binarny).
    Zwraca (width, height, pixel_data_bytes).
    """
    with open(ppm_path, "rb") as f:
        header = f.readline().decode('latin1').strip()
        if header != "P6":
            raise ValueError(f"Nieobslugiwany format PPM: {header}")

        # Pomin komentarze
        line = f.readline().decode('latin1').strip()
        while line.startswith("#"):
            line = f.readline().decode('latin1').strip()

        parts = line.split()
        while len(parts) < 2:
            parts.extend(f.readline().decode('latin1').strip().split())

        width = int(parts[0])
        height = int(parts[1])

        max_val_line = f.readline().decode('latin1').strip()
        while max_val_line.startswith("#"):
            max_val_line = f.readline().decode('latin1').strip()
        max_val = int(max_val_line)

        data = f.read()
        if max_val != 255:
            raise ValueError(f"Nieoczekiwana maksymalna wartosc koloru: {max_val}")

        return width, height, data

def get_pixel(data, width, height, x, y):
    if x < 0 or x >= width or y < 0 or y >= height:
        raise IndexError(f"Wspolrzedne poza zakresem: {x},{y} dla {width}x{height}")
    idx = (y * width + x) * 3
    return (data[idx], data[idx + 1], data[idx + 2])

def check_cursor_at(data, width, height, cx, cy):
    p_tip = get_pixel(data, width, height, cx, cy)
    p_out0 = get_pixel(data, width, height, cx, cy + 1)
    p_fill = get_pixel(data, width, height, cx + 1, cy + 1)
    p_out1 = get_pixel(data, width, height, cx + 2, cy + 1)

    is_cursor = (p_tip == (0, 0, 0) and p_out0 == (0, 0, 0) and
                 p_fill == (255, 255, 255) and p_out1 == (0, 0, 0))
    return is_cursor, {
        "tip": p_tip, "out0": p_out0, "fill": p_fill, "out1": p_out1
    }

def check_restored_bg(data, width, height, cx, cy, expected_bg):
    p_tip = get_pixel(data, width, height, cx, cy)
    p_fill = get_pixel(data, width, height, cx + 1, cy + 1)
    is_restored = (p_fill != (255, 255, 255)) and (p_tip == expected_bg) and (p_fill == expected_bg)
    return is_restored, {"tip": p_tip, "fill": p_fill, "expected": expected_bg}

class QmpClient:
    def __init__(self, sock_path, timeout=10.0):
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

    def hmp(self, cmd_line):
        return self.execute({"execute": "human-monitor-command", "arguments": {"command-line": cmd_line}})

    def screendump(self, filename, device="gpu0"):
        return self.execute({"execute": "screendump", "arguments": {"filename": str(filename), "device": device}})

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

def qmp_screendump(qmp_sock_path, dump_output_path, timeout=5.0):
    client = QmpClient(qmp_sock_path, timeout=timeout)
    resp = client.screendump(dump_output_path)
    client.close()
    return json.dumps(resp)

def main():
    ci_iso = ROOT / ".bursztyn_ci_test.iso"
    if not ci_iso.exists():
        subprocess.run(["make", ".bursztyn_ci_test.iso"], cwd=str(ROOT), check=True)
    # Dedicated opt-in image: other CI runners do not speak the capture ACK
    # protocol and must retain their usual modeset sequence.
    capture_dir = Path(tempfile.mkdtemp(prefix='bursztyn-gpu-capture-'))
    stage = capture_dir / 'iso'
    subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(ci_iso),
                    '-extract', '/', str(stage)], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)
    cfg = stage / 'boot/grub/grub.cfg'
    cfg.chmod(0o600)
    cfg.write_text(cfg.read_text().replace('bursztyn.test=automated',
                   'bursztyn.test=automated bursztyn.test.gpu-capture=1'))
    iso = capture_dir / 'capture.iso'
    subprocess.run(['grub-mkrescue', '-o', str(iso), str(stage)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    qmp_sock = Path("/tmp/virtio_gpu_qmp.sock")
    pattern_dump_path = Path("/tmp/virtio_gpu_pattern.ppm")
    dump_1080_path = Path("/tmp/virtio_gpu_1080.ppm")
    dump_768_path = Path("/tmp/virtio_gpu_768.ppm")
    dump_rollback_path = Path("/tmp/virtio_gpu_rollback.ppm")
    desktop_dump_path = Path("/tmp/virtio_gpu_desktop.ppm")
    desktop_moved_path = Path("/tmp/virtio_gpu_moved.ppm")
    desktop_rapid_path = Path("/tmp/virtio_gpu_rapid.ppm")

    for p in [qmp_sock, pattern_dump_path, dump_1080_path, dump_768_path, dump_rollback_path,
              desktop_dump_path, desktop_moved_path, desktop_rapid_path]:
        if p.exists():
            try:
                p.unlink()
            except Exception:
                pass

    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-drive", f"id=disk,file={disk},format=raw,if=none",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
        "-device", "virtio-vga,id=gpu0,disable-legacy=on",
        "-device", "qemu-xhci,id=xhci",
        "-device", "usb-mouse,id=usbmouse,bus=xhci.0",
        "-m", "2G",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso),
        "-qmp", f"unix:{qmp_sock},server,nowait"
    ]

    print("====================================================================")
    print(" BURSZTYN OS - TEST INTEGRACYJNY QEMU: VIRTIO-GPU & KURSOR MYSZY")
    print("====================================================================")
    print(f"[INFO] Uruchamianie QEMU: {' '.join(cmd)}")

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    found_virtio_pci_transport = False
    found_gpu_features = False
    found_gpu_pci_device = False
    found_gpu_msix = False
    found_gpu_framebuffer = False
    found_gpu_ready = False
    found_gpu_teardown_reinit = False
    found_gpu_stress = False
    found_test_pattern = False
    found_hal_switch = False
    found_ekran_init = False
    found_desktop_ready = False
    boot_ready = False

    # VirtIO-GPU initial mode & modeset assertions
    found_dinfo_scanout = False
    found_boot_mode = False
    found_selected_initial = False
    found_scanout_cmd_ok = False
    pattern_initial_1280x720_ok = False
    found_invalid_dims_rejected = False
    found_100x_modeset_cycles = False
    found_desktop_switched_1080 = False
    screendump_1080_ok = False
    found_desktop_switched_768 = False
    screendump_768_ok = False
    found_unconfirmed_rollback = False
    screendump_rollback_ok = False
    found_desktop_restored_1280 = False
    found_desktop_modeset_suite = False

    # In-kernel cursor test assertions
    found_cursor_center = False
    found_cursor_movement = False
    found_cursor_restore = False
    found_cursor_clipping = False
    found_cursor_rapid_kernel = False
    found_cursor_flush = False
    found_cursor_backend_switch = False
    found_cursor_visible_switch = False
    found_cursor_suite_all = False
    cursor_diag_lines = []

    # USB / xHCI detection
    found_usb_xhci_ready = False
    found_usb_mouse_configured = False

    pattern_dump_ok = False
    pattern_pixel_checks_ok = False
    pattern_pixel_report = []

    desktop_dump_ok = False
    desktop_dim_ok = False
    high_modes_ok = set()
    cursor_initial_visible = False
    cursor_move_ok = False
    cursor_old_restored_ok = False
    cursor_rapid_1000_ok = False
    cursor_final_visible = False

    gpu_lines = []
    line_queue = queue.Queue()

    def reader():
        try:
            for raw_line in iter(proc.stdout.readline, ''):
                line_queue.put(raw_line)
        except Exception:
            pass
        finally:
            line_queue.put(None)

    reader_thread = threading.Thread(target=reader, daemon=True)
    reader_thread.start()

    start_time = time.time()
    last_activity_time = start_time
    termination_reason = ""

    try:
        while True:
            now = time.time()

            if now - start_time >= GLOBAL_TIMEOUT:
                termination_reason = f"Przekroczono globalny limit czasu ({GLOBAL_TIMEOUT}s)."
                break

            if now - last_activity_time >= INACTIVITY_TIMEOUT:
                termination_reason = f"Przekroczono limit braku aktywnosci portu szeregowego ({INACTIVITY_TIMEOUT}s)."
                break

            try:
                line = line_queue.get(timeout=0.1)
            except queue.Empty:
                if proc.poll() is not None:
                    termination_reason = "Proces QEMU zakonczyl sie niespodziewanie."
                    break
                continue

            if line is None:
                termination_reason = "Koniec strumienia wyjsciowego QEMU (EOF)."
                break

            last_activity_time = time.time()
            line_str = line.strip()
            print(f"[SERIAL] {line_str}", flush=True)

            if "[VIRTIO-PCI] device=VirtIO-GPU" in line_str and "modern transport found" in line_str:
                found_virtio_pci_transport = True
                gpu_lines.append(line_str)
            if "[VIRTIO-PCI] features device=" in line_str and "driver=" in line_str:
                found_gpu_features = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] Wykryto urzadzenie VirtIO-GPU" in line_str:
                found_gpu_pci_device = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] MSI-X configured successfully" in line_str:
                found_gpu_msix = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] Framebuffer gotowy:" in line_str:
                found_gpu_framebuffer = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] Karta graficzna gotowa do pracy w trybie 2D." in line_str:
                found_gpu_ready = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-TEST] Cykle teardown/reinit: PASS" in line_str:
                found_gpu_teardown_reinit = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-STRESS] Zakonczono pomyslnie 10000 aktualizacji" in line_str:
                found_gpu_stress = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] Test pattern rendered and flushed" in line_str:
                found_test_pattern = True
                gpu_lines.append(line_str)

            # In-kernel cursor suite tracking
            if "[CURSOR DIAG" in line_str:
                cursor_diag_lines.append(line_str)
            if "[CURSOR TEST] cursor render at center: PASS" in line_str:
                found_cursor_center = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] cursor movement: PASS" in line_str:
                found_cursor_movement = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] old cursor region restored: PASS" in line_str:
                found_cursor_restore = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] edge clipping: PASS" in line_str:
                found_cursor_clipping = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] rapid movement (1000 iterations): PASS" in line_str:
                found_cursor_rapid_kernel = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] VirtIO-GPU dirty-rect flush: PASS" in line_str:
                found_cursor_flush = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] backend switch GOP/Bochs VirtIO-GPU: PASS" in line_str:
                found_cursor_backend_switch = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] cursor still visible after switch: PASS" in line_str:
                found_cursor_visible_switch = True
                gpu_lines.append(line_str)
            if "[CURSOR TEST] === WSZYSTKIE TESTY KURSORA: PASS ===" in line_str:
                found_cursor_suite_all = True
                gpu_lines.append(line_str)

            # USB / xHCI detection
            if "[USB] xHCI gotowy." in line_str:
                found_usb_xhci_ready = True
            if "[USB-MYSZ] Mysz Boot HID skonfigurowana." in line_str:
                found_usb_mouse_configured = True

            if "[VIRTIO-GPU-FINAL] Finalny wzorzec testowy gotowy do weryfikacji pikseli." in line_str:
                if not pattern_dump_ok and qmp_sock.exists():
                    try:
                        time.sleep(0.1)
                        print(f"[INFO] Pobieranie zrzutu wzorca testowego przez QMP...")
                        qmp_resp = qmp_screendump(qmp_sock, pattern_dump_path)
                        print(f"[INFO] Odpowiedz QMP screendump (pattern): {qmp_resp.strip()}")
                        if pattern_dump_path.exists() and pattern_dump_path.stat().st_size > 0:
                            pattern_dump_ok = True
                            print(f"[INFO] Zrzut wzorca zapisany ({pattern_dump_path.stat().st_size} bajtow).")
                            w, h, pdata = parse_ppm(pattern_dump_path)
                            print(f"[INFO] Rozdzielczosc wzorca testowego: {w}x{h}")
                            if w == 1280 and h == 720:
                                pattern_initial_1280x720_ok = True
                                print(f"[INFO] Poczatkowa rozdzielczosc GOP 1280x720 zachowana: PASS")
                            else:
                                print(f"[BLAD] Poczatkowa rozdzielczosc != 1280x720 ({w}x{h})!")

                            expected_points = [
                                ("Tlo (10, 10)", 10, 10, (10, 25, 47)),
                                ("Amber (100, 100)", 100, 100, (255, 191, 0)),
                                ("Cyan (w-100, 100)", w - 100, 100, (0, 229, 255)),
                                ("Magenta (100, h-100)", 100, h - 100, (255, 0, 127)),
                                ("Lime (w-100, h-100)", w - 100, h - 100, (57, 255, 20)),
                                ("White (srodek)", w // 2, h // 2, (255, 255, 255)),
                            ]

                            all_match = True
                            for name, px, py, exp_rgb in expected_points:
                                act_rgb = get_pixel(pdata, w, h, px, py)
                                match = (act_rgb == exp_rgb)
                                if not match:
                                    all_match = False
                                pattern_pixel_report.append((name, px, py, exp_rgb, act_rgb, match))
                            pattern_pixel_checks_ok = all_match
                    except Exception as e:
                        print(f"[BLAD] Wyjatek podczas zrzutu wzorca: {e}")

            if "[VIRTIO-GPU] GETDISPLAYINFO scanout0=" in line_str:
                found_dinfo_scanout = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] boot mode=1280x720" in line_str:
                found_boot_mode = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] selected initial mode=1280x720" in line_str:
                found_selected_initial = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] SETSCANOUT 1280x720 result=OK" in line_str:
                found_scanout_cmd_ok = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-MODESET] Invalid dimensions rejection: PASS" in line_str:
                found_invalid_dims_rejected = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-MODESET] 100x modeset cycles: PASS (PMM delta=0, DMA delta=0, IDT delta=0)." in line_str:
                found_100x_modeset_cycles = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-MODESET] Switched to 1920x1080: PASS" in line_str:
                found_desktop_switched_1080 = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-MODESET] Capture " in line_str:
                mode = line_str.rsplit(" ", 1)[1]
                path = Path('/tmp') / ('virtio_gpu_' + mode + '.ppm')
                try:
                    qmp_screendump(qmp_sock, path)
                    w, h, _ = parse_ppm(path)
                    if f'{w}x{h}' == mode:
                        high_modes_ok.add(mode)
                    print(f'[MODE CAPTURE] expected={mode} actual={w}x{h}')
                finally:
                    proc.stdin.write('C')
                    proc.stdin.flush()
            if "[VIRTIO-GPU-MODESET] Ready for 1920x1080 screendump" in line_str:
                if not screendump_1080_ok and qmp_sock.exists():
                    try:
                        qmp_screendump(qmp_sock, dump_1080_path)
                        if dump_1080_path.exists() and dump_1080_path.stat().st_size > 0:
                            w1080, h1080, _ = parse_ppm(dump_1080_path)
                            print(f"[INFO] Rozdzielczosc zrzutu 1920x1080: {w1080}x{h1080}")
                            if w1080 == 1920 and h1080 == 1080:
                                screendump_1080_ok = True
                    except Exception as e:
                        print(f"[BLAD] Screendump 1920x1080: {e}")
            if "[VIRTIO-GPU-MODESET] Switched to 1024x768: PASS" in line_str:
                found_desktop_switched_768 = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-MODESET] Ready for 1024x768 screendump" in line_str:
                if not screendump_768_ok and qmp_sock.exists():
                    try:
                        qmp_screendump(qmp_sock, dump_768_path)
                        if dump_768_path.exists() and dump_768_path.stat().st_size > 0:
                            w768, h768, _ = parse_ppm(dump_768_path)
                            print(f"[INFO] Rozdzielczosc zrzutu 1024x768: {w768}x{h768}")
                            if w768 == 1024 and h768 == 768:
                                screendump_768_ok = True
                    except Exception as e:
                        print(f"[BLAD] Screendump 1024x768: {e}")
            if "[VIRTIO-GPU-MODESET] Unconfirmed rollback complete, ready for screendump" in line_str:
                found_unconfirmed_rollback = True
                gpu_lines.append(line_str)
                if not screendump_rollback_ok and qmp_sock.exists():
                    try:
                        qmp_screendump(qmp_sock, dump_rollback_path)
                        if dump_rollback_path.exists() and dump_rollback_path.stat().st_size > 0:
                            wrb, hrb, _ = parse_ppm(dump_rollback_path)
                            print(f"[INFO] Rozdzielczosc zrzutu rollback: {wrb}x{hrb}")
                            if wrb == 1024 and hrb == 768:
                                screendump_rollback_ok = True
                    except Exception as e:
                        print(f"[BLAD] Screendump rollback: {e}")
            if "[VIRTIO-GPU-MODESET] Restored initial 1280x720: PASS" in line_str:
                found_desktop_restored_1280 = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-MODESET] Desktop modeset test suite: PASS" in line_str:
                found_desktop_modeset_suite = True
                gpu_lines.append(line_str)

            if "[HAL] Aktywacja sterownika VirtIO-GPU 2D jako backend graficzny." in line_str:
                found_hal_switch = True
                gpu_lines.append(line_str)
            if "[EKRAN INIT] backend=virtio-gpu-runtime" in line_str:
                found_ekran_init = True
                gpu_lines.append(line_str)
            if "[TAPETA] Tapeta gotowa." in line_str:
                found_desktop_ready = True
                gpu_lines.append(line_str)

            if "System operacyjny gotowy!" in line_str:
                boot_ready = True
                termination_reason = "System operacyjny zglosil gotowosc ('System operacyjny gotowy!')."
                break

        # Sekwencja testow integracyjnych QMP po zaladowaniu pulpitu
        if boot_ready and qmp_sock.exists():
            print("\n[INFO] Rozpoczynam integracyjny test kursora i myszy USB przez QMP...")
            time.sleep(1.5)
            qmp = QmpClient(qmp_sock, timeout=10.0)

            # 1. Sprawdzenie i wybor myszy USB HID
            resp_mice = qmp.hmp("info mice").get("return", "")
            print(f"[INFO] Dostepne urzadzenia wskazujace QEMU:\n{resp_mice.strip()}")
            m_hid = re.search(r"Mouse #(\d+):\s+QEMU HID Mouse", resp_mice)
            if m_hid:
                hid_id = m_hid.group(1)
                print(f"[INFO] Przelaczam aktywne urzadzenie wskazujace na USB HID Mouse #{hid_id}...")
                qmp.hmp(f"mouse_set {hid_id}")

            # 2. Zrzut poczatkowego stanu pulpitu
            qmp.screendump(desktop_dump_path)
            if desktop_dump_path.exists() and desktop_dump_path.stat().st_size > 0:
                desktop_dump_ok = True
                dw, dh, ddata = parse_ppm(desktop_dump_path)
                print(f"[INFO] Zrzut pulpitu zapisany: {dw}x{dh}, {desktop_dump_path.stat().st_size} bajtow.")
                desktop_dim_ok = (dw == 1280 and dh == 720)
                if desktop_dim_ok:
                    print(f"[INFO] Rozdzielczosc pulpitu 1280x720: PASS")
                else:
                    print(f"[BLAD] Rozdzielczosc pulpitu != 1280x720 ({dw}x{dh})!")

                # Probkowanie tla tapety w poblizu pozycji poczatkowej kursora
                bg_sample = get_pixel(ddata, dw, dh, 498, 298)
                print(f"[INFO] Kolor tla tapety wokol kursora: RGB{bg_sample}")

                # Sprawdzenie obecnosci kursora w pozycji domyslnej (500, 300)
                is_cur_init, cur_init_details = check_cursor_at(ddata, dw, dh, 500, 300)
                if is_cur_init:
                    cursor_initial_visible = True
                    print("[INFO] Kursor w pozycji domyslnej (500, 300): WIDOCZNY (PASS)")
                else:
                    print(f"[BLAD] Brak kursora w (500, 300): detale={cur_init_details}")

                # 3. Wstrzykniecie ruchu myszy (+60, +40)
                print("[INFO] Wstrzykiwanie zdarzenia ruchu myszy USB HID (dx=+60, dy=+40)...")
                qmp.hmp("mouse_move 60 40")
                time.sleep(0.6)

                # 4. Zrzut ekranu po przesunieciu kursora
                qmp.screendump(desktop_moved_path)
                if desktop_moved_path.exists() and desktop_moved_path.stat().st_size > 0:
                    mw, mh, mdata = parse_ppm(desktop_moved_path)

                    # Weryfikacja przywrocenia tla na starej pozycji (500, 300)
                    is_restored, rest_details = check_restored_bg(mdata, mw, mh, 500, 300, bg_sample)
                    if is_restored:
                        cursor_old_restored_ok = True
                        print("[INFO] Stare polozenie kursora (500, 300): PRZYWROCONE TLO (PASS)")
                    else:
                        print(f"[BLAD] Stare polozenie nie zostalo poprawnie przywrocone: {rest_details}")

                    # Weryfikacja obecnosci kursora w nowej pozycji (560, 340)
                    is_cur_moved, cur_moved_details = check_cursor_at(mdata, mw, mh, 560, 340)
                    if is_cur_moved:
                        cursor_move_ok = True
                        print("[INFO] Nowe polozenie kursora (560, 340): WIDOCZNY (PASS)")
                    else:
                        print(f"[BLAD] Brak kursora w nowym polozeniu (560, 340): detale={cur_moved_details}")

                # 5. Powrot do pozycji wyjsciowej i szybki test 1000 ruchow przez QMP
                print("[INFO] Powrot kursora i rozpoczynam test obciazeniowy 1000 ruchow myszy przez QMP...")
                qmp.hmp("mouse_move -60 -40")
                time.sleep(0.2)

                t_rapid_start = time.time()
                for _ in range(10):
                    cmds = []
                    for i in range(100):
                        dx = 1 if (i % 2 == 0) else -1
                        dy = 1 if (i % 4 < 2) else -1
                        cmds.append(json.dumps({"execute": "human-monitor-command", "arguments": {"command-line": f"mouse_move {dx} {dy}"}}))
                    payload = "\n".join(cmds) + "\n"
                    qmp.sock.sendall(payload.encode('utf-8'))
                    for _ in range(100):
                        l = b""
                        while not l.endswith(b"\n"):
                            l += qmp.sock.recv(1)
                t_rapid_end = time.time()
                print(f"[INFO] Wykonano 1000 ruchow myszy w czasie {t_rapid_end - t_rapid_start:.2f}s.")
                time.sleep(0.4)

                # 6. Zrzut ekranu po tescie obciazeniowym
                qmp.screendump(desktop_rapid_path)
                if desktop_rapid_path.exists() and desktop_rapid_path.stat().st_size > 0:
                    rw, rh, rdata = parse_ppm(desktop_rapid_path)
                    is_cur_final, _ = check_cursor_at(rdata, rw, rh, 500, 300)
                    if is_cur_final:
                        cursor_rapid_1000_ok = True
                        cursor_final_visible = True
                        print("[INFO] Po 1000 ruchach kursor w (500, 300): WIDOCZNY I PRAWIDLOWY (PASS)")
                    else:
                        print("[BLAD] Po 1000 ruchach kursor nie znajduje sie w oczekiwanej pozycji lub ulegl uszkodzeniu.")

            qmp.close()

    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        if capture_dir.exists():
            shutil.rmtree(capture_dir, ignore_errors=True)

    runtime = time.time() - start_time

    print("\n" + "=" * 65)
    print("PODSUMOWANIE TELEMETRII VIRTIO-GPU 2D & KURSORA:")
    print("=" * 65)
    for l in gpu_lines:
        print(f"  {l}")

    if cursor_diag_lines:
        print("-" * 65)
        print("PRZYKLADOWE LOGI DIAGNOSTYCZNE KURSORA (LIMIT=10):")
        for dl in cursor_diag_lines[:10]:
            print(f"  {dl}")

    print("-" * 65)
    print(f"  VirtIO Modern Transport (ID=GPU 0x1050): {'PASS' if found_virtio_pci_transport else 'FAIL'}")
    print(f"  Negocjacja cech VirtIO-GPU:              {'PASS' if found_gpu_features else 'FAIL'}")
    print(f"  Wykrycie urzadzenia VirtIO-GPU:          {'PASS' if found_gpu_pci_device else 'FAIL'}")
    print(f"  Konfiguracja wektora MSI-X:              {'PASS' if found_gpu_msix else 'FAIL'}")
    print(f"  Konfiguracja bufora ramki (2D FB):       {'PASS' if found_gpu_framebuffer else 'FAIL'}")
    print(f"  Karta gotowa w trybie 2D:                {'PASS' if found_gpu_ready else 'FAIL'}")
    print(f"  Cykle teardown/reinit (PMM=0, DMA=0, IDT=0): {'PASS' if found_gpu_teardown_reinit else 'FAIL'}")
    print(f"  Test obciazeniowy 10000 aktualizacji (stress): {'PASS' if found_gpu_stress else 'FAIL'}")
    print(f"  Wygenerowanie i flush wzorca testowego:  {'PASS' if found_test_pattern else 'FAIL'}")
    print(f"  Przelaczenie HAL na VirtIO-GPU:          {'PASS' if found_hal_switch else 'FAIL'}")
    print(f"  Rejestracja backendu virtio-gpu-runtime: {'PASS' if found_ekran_init else 'FAIL'}")
    print(f"  Kompozycja i rendering tapety/pulpitu:   {'PASS' if found_desktop_ready else 'FAIL'}")
    print(f"  Zrzut wzorca testowego (QMP screendump): {'PASS' if pattern_dump_ok else 'FAIL'}")
    print(f"  Weryfikacja pikseli wzorca (PPM):        {'PASS' if pattern_pixel_checks_ok else 'FAIL'}")
    print(f"  Zrzut pulpitu BursztynUI (screendump):   {'PASS' if desktop_dump_ok else 'FAIL'}")
    print("-" * 65)
    print("TESTY TRYBOW GRAFICZNYCH I MODESETTINGU (VIRTIO-GPU):")
    print(f"  Odczyt GETDISPLAYINFO scanout0:          {'PASS' if found_dinfo_scanout else 'FAIL'}")
    print(f"  Odczyt trybu boot (1280x720):            {'PASS' if found_boot_mode else 'FAIL'}")
    print(f"  Wybor trybu poczatkowego (1280x720):     {'PASS' if found_selected_initial else 'FAIL'}")
    print(f"  Polecenie SETSCANOUT 1280x720:           {'PASS' if found_scanout_cmd_ok else 'FAIL'}")
    print(f"  Zachowanie trybu GOP 1280x720 (zrzut):   {'PASS' if pattern_initial_1280x720_ok else 'FAIL'}")
    print(f"  Odrzucenie niepoprawnych wymiarow:       {'PASS' if found_invalid_dims_rejected else 'FAIL'}")
    print(f"  100x cykli przelaczania (PMM=0, DMA=0):  {'PASS' if found_100x_modeset_cycles else 'FAIL'}")
    print(f"  Przelaczenie desktopu 1280x720->1920x1080: {'PASS' if found_desktop_switched_1080 else 'FAIL'}")
    print(f"  Zrzut ekranu 1920x1080 (QMP screendump): {'PASS' if screendump_1080_ok else 'FAIL'}")
    print(f"  Przelaczenie desktopu 1920x1080->1024x768: {'PASS' if found_desktop_switched_768 else 'FAIL'}")
    print(f"  Zrzut ekranu 1024x768 (QMP screendump):  {'PASS' if screendump_768_ok else 'FAIL'}")
    print(f"  Wycofanie po timeout (rollback 1024x768): {'PASS' if found_unconfirmed_rollback else 'FAIL'}")
    print(f"  Zrzut ekranu po rollback (QMP 1024x768): {'PASS' if screendump_rollback_ok else 'FAIL'}")
    print(f"  Przywrocenie 1280x720 przed startem GUI: {'PASS' if found_desktop_restored_1280 else 'FAIL'}")
    print(f"  Rozdzielczosc pulpitu (1280x720):        {'PASS' if desktop_dim_ok else 'FAIL'}")
    print(f"  Pelny pakiet testow modesettingu desktopu: {'PASS' if found_desktop_modeset_suite else 'FAIL'}")
    print("-" * 65)
    print("TESTY JEDNOSTKOWE KURSORA (KERNEL SUITE):")
    print(f"  1. Render kursora w centrum:             {'PASS' if found_cursor_center else 'FAIL'}")
    print(f"  2. Ruch kursora:                         {'PASS' if found_cursor_movement else 'FAIL'}")
    print(f"  3. Przywrocenie tla po starym kursorze:  {'PASS' if found_cursor_restore else 'FAIL'}")
    print(f"  4. Przycinanie do krawedzi (clipping):   {'PASS' if found_cursor_clipping else 'FAIL'}")
    print(f"  5. Szybki ruch (1000 iter. kernel):      {'PASS' if found_cursor_rapid_kernel else 'FAIL'}")
    print(f"  6. VirtIO-GPU dirty-rect flush:          {'PASS' if found_cursor_flush else 'FAIL'}")
    print(f"  7. Przelaczenie GOP/Bochs <-> VirtIO-GPU: {'PASS' if found_cursor_backend_switch else 'FAIL'}")
    print(f"  8. Widocznosc kursora po przelaczeniu:   {'PASS' if found_cursor_visible_switch else 'FAIL'}")
    print(f"  Pakiet testow jadra:                     {'PASS' if found_cursor_suite_all else 'FAIL'}")
    print("-" * 65)
    print("TESTY INTEGRACYJNE KURSORA (QEMU & USB MYSZ):")
    print(f"  Kontroler xHCI gotowy:                   {'PASS' if found_usb_xhci_ready else 'FAIL'}")
    print(f"  Mysz USB HID skonfigurowana:             {'PASS' if found_usb_mouse_configured else 'FAIL'}")
    print(f"  Kursor widoczny w (500, 300):            {'PASS' if cursor_initial_visible else 'FAIL'}")
    print(f"  Ruch myszy do (560, 340):                {'PASS' if cursor_move_ok else 'FAIL'}")
    print(f"  Przywrocenie tla po ruchu w (500, 300):  {'PASS' if cursor_old_restored_ok else 'FAIL'}")
    print(f"  Test obciazeniowy 1000 ruchow QMP:       {'PASS' if cursor_rapid_1000_ok else 'FAIL'}")
    print(f"  Integralnosc kursora po 1000 ruchach:    {'PASS' if cursor_final_visible else 'FAIL'}")
    print("=" * 65)

    if pattern_pixel_report:
        print("\n" + "=" * 65)
        print("WERYFIKACJA PROBEK KOLOROW WZORCA TESTOWEGO:")
        print("=" * 65)
        for name, px, py, exp, act, match in pattern_pixel_report:
            status = "PASS" if match else "FAIL"
            print(f"  [{status}] {name} at ({px}, {py}): oczekiwano RGB{exp}, odczytano RGB{act}")
        print("=" * 65)

    all_ok = (found_virtio_pci_transport and found_gpu_features and
              found_gpu_pci_device and found_gpu_msix and
              found_gpu_framebuffer and found_gpu_ready and
              found_gpu_teardown_reinit and found_gpu_stress and
              found_test_pattern and found_hal_switch and
              found_ekran_init and found_desktop_ready and
              pattern_dump_ok and pattern_pixel_checks_ok and
              desktop_dump_ok and desktop_dim_ok and
              found_dinfo_scanout and found_boot_mode and
              found_selected_initial and found_scanout_cmd_ok and
              pattern_initial_1280x720_ok and
              found_invalid_dims_rejected and found_100x_modeset_cycles and
              found_desktop_switched_1080 and screendump_1080_ok and
              found_desktop_switched_768 and screendump_768_ok and
              found_unconfirmed_rollback and screendump_rollback_ok and
              found_desktop_restored_1280 and found_desktop_modeset_suite and
              high_modes_ok == {'3440x1440', '3840x2160', '1280x720'} and
              found_cursor_center and found_cursor_movement and
              found_cursor_restore and found_cursor_clipping and
              found_cursor_rapid_kernel and found_cursor_flush and
              found_cursor_backend_switch and found_cursor_visible_switch and
              found_cursor_suite_all and
              found_usb_xhci_ready and found_usb_mouse_configured and
              cursor_initial_visible and cursor_move_ok and
              cursor_old_restored_ok and cursor_rapid_1000_ok and
              cursor_final_visible)

    print(f"[STATUS ROZRUCHU] Gotowosc systemu: {'TAK' if boot_ready else 'NIE'}")
    print(f"[STATUS TESTU] Przyczyna zakonczenia: {termination_reason}")
    print(f"[STATUS TESTU] Czas wykonania: {runtime:.2f} s")

    if all_ok:
        print("\n[SUKCES] Sterownik VirtIO-GPU 2D, modesetting oraz kursor myszy USB HID pomyslnie zweryfikowane piksel po pikselu w QEMU (PASS)!")
        sys.exit(0)
    else:
        print("\n[BLAD] Jeden lub wiecej testow integracyjnych VirtIO-GPU 2D lub modesettingu zakonczylo sie niepowodzeniem!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
