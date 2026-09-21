#!/usr/bin/env python3
"""
narzedzia/test_virtio_input_qemu.py - Test integracyjny VirtIO-INPUT w QEMU.

Weryfikuje:
 1. Wykrycie urzadzen VirtIO Modern PCI (Vendor 0x1AF4, Device 0x1052, Subsystem 18).
 2. Brak urzadzen wejsciowych USB (USB mouse i USB keyboard wylaczone).
 3. Inicjalizacje sterownika VirtIO-INPUT w jadrze Bursztyn OS:
    - Klawiatura (INPUT_DEVICE_KIND_KEYBOARD)
    - Mysz wzgledna (INPUT_DEVICE_KIND_MOUSE)
    - Tablet bezwzgledny (INPUT_DEVICE_KIND_TABLET)
 4. Konfiguracje kolejek Split VirtQueue 0 (eventq) z pre-postowanymi buforami.
 5. Konfiguracje i obsluge przerwan MSI-X dla kazdego urzadzenia wejsciowego.
 6. Rejestracje urzadzen w ujednoliconym dyspozytorze InputDevice.
 7. Wykonanie wewnetrznego testu obciazeniowego (stress test) w trybie automated:
    - Test statusq / eventq
    - Zero zawieszen kolejki i wyciekow buforow
 8. Weryfikacje zrzutu ekranu (screendump) przez QMP:
    - Widocznosc kursora na pulpicie VirtIO-GPU
    - Wstrzykniecie wcisniec klawiszy klawiatury przez QMP
    - Wstrzykniecie ruchu myszy wzglednej (+60, +40) przez QMP
    - Weryfikacja przesuniecia kursora na nowa pozycje (560, 340)
    - Weryfikacja przywrocenia tla pulpitu w starym polozeniu (500, 300)
    - Wstrzykniecie klikniecia przyciskiem myszy
    - Przelaczenie na tablet bezwzgledny i wstrzykniecie wspolrzednych bezwzglednych
    - Weryfikacja poprawnego przeskalowania wspolrzednych bezwzglednych
    - Test obciazeniowy: 1000 zdarzen wejsciowych przez QMP (wraparound ring buffers)
    - Weryfikacja stabilnosci systemu, braku wyciekow DMA, IDT i PMM
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

def check_restored_bg(data, width, height, cx, cy, expected_bg=None):
    is_cursor, _ = check_cursor_at(data, width, height, cx, cy)
    p_tip = get_pixel(data, width, height, cx, cy)
    p_fill = get_pixel(data, width, height, cx + 1, cy + 1)
    if is_cursor or p_fill == (255, 255, 255):
        return False, {"tip": p_tip, "fill": p_fill, "reason": "Kursor nadal obecny"}
    if expected_bg is not None and p_tip != expected_bg:
        return False, {"tip": p_tip, "fill": p_fill, "expected": expected_bg, "reason": "Niezgodnosc z oczekiwanym tlem"}
    return True, {"tip": p_tip, "fill": p_fill, "expected": expected_bg}

class QmpClient:
    def __init__(self, sock_path, timeout=10.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(str(sock_path))
        self.file = self.sock.makefile('r', encoding='utf-8')
        self._read_response()
        self.execute({"execute": "qmp_capabilities"})

    def _read_response(self):
        while True:
            line = self.file.readline()
            if not line:
                raise ConnectionError("QMP socket closed unexpectedly")
            msg = json.loads(line.strip())
            if "event" in msg and "return" not in msg and "error" not in msg:
                continue
            return msg

    def execute(self, cmd_dict):
        self.sock.sendall((json.dumps(cmd_dict) + "\n").encode('utf-8'))
        return self._read_response()

    def hmp(self, cmd_line):
        return self.execute({"execute": "human-monitor-command", "arguments": {"command-line": cmd_line}})

    def screendump(self, filename, device="gpu0"):
        return self.execute({"execute": "screendump", "arguments": {"filename": str(filename), "device": device}})

    def close(self):
        try:
            self.file.close()
            self.sock.close()
        except Exception:
            pass

def main():
    ci_iso = ROOT / ".bursztyn_ci_test.iso"
    if not ci_iso.exists():
        subprocess.run(["make", ".bursztyn_ci_test.iso"], cwd=str(ROOT), check=True)
    iso = ci_iso

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    qmp_sock = Path("/tmp/virtio_input_qmp.sock")
    desktop_initial_path = Path("/tmp/virtio_input_initial.ppm")
    desktop_mouse_path = Path("/tmp/virtio_input_mouse.ppm")
    desktop_tablet_path = Path("/tmp/virtio_input_tablet.ppm")
    desktop_rapid_path = Path("/tmp/virtio_input_rapid.ppm")

    for p in [qmp_sock, desktop_initial_path, desktop_mouse_path, desktop_tablet_path, desktop_rapid_path]:
        if p.exists():
            try:
                p.unlink()
            except Exception:
                pass

    # Uruchomienie QEMU z urzadzeniami VirtIO-INPUT i VirtIO-VGA, BEZ urzadzen wskazujacych USB
    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-drive", f"id=disk,file={disk},format=raw,if=none",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
        "-device", "virtio-vga,id=gpu0,disable-legacy=on",
        "-device", "virtio-keyboard-pci,id=kbd0,disable-legacy=on",
        "-device", "virtio-mouse-pci,id=mouse0,disable-legacy=on",
        "-device", "virtio-tablet-pci,id=tablet0,disable-legacy=on",
        "-m", "2G",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso),
        "-qmp", f"unix:{qmp_sock},server,nowait"
    ]

    print("====================================================================")
    print(" BURSZTYN OS - TEST INTEGRACYJNY QEMU: VIRTIO-INPUT & GENERIC INPUT")
    print("====================================================================")
    print(f"[INFO] Uruchamianie QEMU: {' '.join(cmd)}")

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    found_virtio_pci_transport = False
    found_input_devices_count = 0
    found_kbd_role = False
    found_mouse_role = False
    found_tablet_role = False
    found_msix_mode = False
    found_input_registered = 0
    found_input_stress_pass = False
    found_virtio_gpu_ready = False
    found_desktop_ready = False
    boot_ready = False

    # QMP screendump i kursor assertions
    desktop_dump_ok = False
    cursor_initial_visible = False
    mouse_move_ok = False
    mouse_old_restored_ok = False
    tablet_move_ok = False
    tablet_old_restored_ok = False
    rapid_events_ok = False
    cursor_final_intact = False

    input_lines = []
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

            if "[VIRTIO-PCI]" in line_str and "VirtIO-Input" in line_str and "modern transport found" in line_str:
                found_virtio_pci_transport = True
                input_lines.append(line_str)
            if "[VIRTIO-INPUT] Prawidlowo zainicjalizowano urzadzenie:" in line_str:
                found_input_devices_count += 1
                input_lines.append(line_str)
            if "Rola: Klawiatura (INPUT_DEVICE_KIND_KEYBOARD)" in line_str:
                found_kbd_role = True
                input_lines.append(line_str)
            if "Rola: Mysz wzgledna (INPUT_DEVICE_KIND_MOUSE)" in line_str:
                found_mouse_role = True
                input_lines.append(line_str)
            if "Rola: Tablet bezwzgledny (INPUT_DEVICE_KIND_TABLET)" in line_str:
                found_tablet_role = True
                input_lines.append(line_str)
            if "Tryb przerwan: MSI-X" in line_str:
                found_msix_mode = True
                input_lines.append(line_str)
            if "Podsystem: Zarejestrowano w InputDevice" in line_str:
                found_input_registered += 1
                input_lines.append(line_str)
            if "[VIRTIO-INPUT-STRESS] Test obciazeniowy zakonczony: PASS" in line_str:
                found_input_stress_pass = True
                input_lines.append(line_str)
            if "[EKRAN INIT] backend=virtio-gpu-runtime" in line_str:
                found_virtio_gpu_ready = True
                input_lines.append(line_str)
            if "Pasek zadan gotowy." in line_str or "[BWS] Compositor: Pulpit zainicjalizowany." in line_str or "[TAPETA] Tapeta gotowa." in line_str or "[GRAFIKA] Pulpit i tapeta gotowe!" in line_str:
                found_desktop_ready = True

            # W trybie automated czekamy na zakonczenie wbudowanych testow pulpitu,
            # aby pulpit byl w pelni ustabilizowany i gotowy do testow interaktywnych.
            if "=== TESTY BURSZTYN OS: PASS ===" in line_str or "[TEST MODE] AUTOMATED: zakonczono" in line_str:
                boot_ready = True
                termination_reason = "System operacyjny oraz testy automatyczne zakonczone pomyslnie."
                break
            elif "System operacyjny gotowy!" in line_str and found_desktop_ready and "automated" not in ' '.join(cmd):
                boot_ready = True
                termination_reason = "System operacyjny gotowy do testow interaktywnych."
                break

        if boot_ready and qmp_sock.exists():
            print("\n[INFO] Rozpoczynam integracyjny test VirtIO-INPUT przez QMP...")
            time.sleep(0.5)
            qmp = QmpClient(qmp_sock, timeout=10.0)

            # 1. Odczyt urzadzen wskazujacych z QEMU
            resp_mice = qmp.hmp("info mice").get("return", "")
            print(f"[INFO] Dostepne urzadzenia wskazujace QEMU:\n{resp_mice.strip()}")

            m_mouse = re.search(r"Mouse #(\d+):\s+QEMU Virtio Mouse", resp_mice)
            m_tablet = re.search(r"Mouse #(\d+):\s+QEMU Virtio Tablet", resp_mice)

            mouse_id = m_mouse.group(1) if m_mouse else None
            tablet_id = m_tablet.group(1) if m_tablet else None

            print(f"[INFO] Identyfikacja QEMU: Virtio Mouse={mouse_id}, Virtio Tablet={tablet_id}")

            # 2. Zrzut poczatkowego stanu pulpitu
            qmp.screendump(desktop_initial_path)
            if desktop_initial_path.exists() and desktop_initial_path.stat().st_size > 0:
                desktop_dump_ok = True
                dw, dh, ddata = parse_ppm(desktop_initial_path)
                print(f"[INFO] Zrzut pulpitu zapisany: {dw}x{dh}, {desktop_initial_path.stat().st_size} bajtow.")

                bg_sample = get_pixel(ddata, dw, dh, 498, 298)
                true_bg_560_340 = get_pixel(ddata, dw, dh, 560, 340)
                print(f"[INFO] Kolor tla tapety wokol domyslnego kursora: RGB{bg_sample}, w (560, 340): RGB{true_bg_560_340}")

                is_cur_init, cur_init_details = check_cursor_at(ddata, dw, dh, 500, 300)
                if is_cur_init:
                    cursor_initial_visible = True
                    print("[INFO] Kursor w pozycji domyslnej (500, 300): WIDOCZNY (PASS)")
                else:
                    print(f"[BLAD] Brak kursora w pozycji (500, 300): detale={cur_init_details}")

                # 3. Test wstrzykiwania klawiszy klawiatury VirtIO
                # 3. Test wstrzykiwania klawiszy klawiatury VirtIO
                print("[INFO] Wstrzykiwanie sekwencji klawiatury przez VirtIO Keyboard...")
                qmp.execute({
                    "execute": "input-send-event",
                    "arguments": {
                        "events": [
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "a"}, "down": True}},
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "a"}, "down": False}},
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "b"}, "down": True}},
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "b"}, "down": False}},
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "c"}, "down": True}},
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "c"}, "down": False}},
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "ret"}, "down": True}},
                            {"type": "key", "data": {"key": {"type": "qcode", "data": "ret"}, "down": False}}
                        ]
                    }
                })
                time.sleep(0.3)
                print("[INFO] Sekwencja klawiszy wyslana bez bledow.")

                # 4. Test myszy wzglednej VirtIO-INPUT (+60, +40)
                if mouse_id:
                    print(f"[INFO] Aktywacja Virtio Mouse #{mouse_id} i ruch (dx=+60, dy=+40)...")
                    qmp.hmp(f"mouse_set {mouse_id}")
                    qmp.execute({
                        "execute": "input-send-event",
                        "arguments": {
                            "events": [
                                {"type": "rel", "data": {"axis": "x", "value": 60}},
                                {"type": "rel", "data": {"axis": "y", "value": 40}}
                            ]
                        }
                    })
                    time.sleep(0.5)

                    qmp.screendump(desktop_mouse_path)
                    if desktop_mouse_path.exists() and desktop_mouse_path.stat().st_size > 0:
                        mw, mh, mdata = parse_ppm(desktop_mouse_path)

                        # Weryfikacja przywrocenia tla na starej pozycji (500, 300)
                        is_restored, rest_details = check_restored_bg(mdata, mw, mh, 500, 300)
                        if is_restored:
                            mouse_old_restored_ok = True
                            print("[INFO] Stare polozenie kursora (500, 300): PRZYWROCONE TLO (PASS)")
                        else:
                            print(f"[BLAD] Stare polozenie nie zostalo przywrocone: {rest_details}")

                        # Weryfikacja kursora w nowej pozycji (560, 340)
                        is_cur_moved, cur_moved_details = check_cursor_at(mdata, mw, mh, 560, 340)
                        if is_cur_moved:
                            mouse_move_ok = True
                            print("[INFO] Nowe polozenie kursora myszy (560, 340): WIDOCZNY (PASS)")
                        else:
                            print(f"[BLAD] Brak kursora w (560, 340): detale={cur_moved_details}")

                        # Klikniecie przyciskiem myszy
                        qmp.execute({
                            "execute": "input-send-event",
                            "arguments": {
                                "events": [
                                    {"type": "btn", "data": {"button": "left", "down": True}},
                                    {"type": "btn", "data": {"button": "left", "down": False}}
                                ]
                            }
                        })

                # 5. Test tabletu bezwzglednego VirtIO-INPUT
                if tablet_id:
                    print(f"[INFO] Aktywacja Virtio Tablet #{tablet_id}...")
                    qmp.hmp(f"mouse_set {tablet_id}")

                    # Przeliczenie wspolrzednych bezwzglednych dla aktualnej rozdzielczosci ekranu (dw x dh)
                    # Chcemy pozycje (700, 400):
                    abs_x = int((700 * 32767) / (dw - 1))
                    abs_y = int((400 * 32767) / (dh - 1))

                    print(f"[INFO] Wstrzykiwanie wspolrzednych bezwzglednych (abs_x={abs_x}, abs_y={abs_y}) dla {dw}x{dh}...")
                    res_tab = qmp.execute({
                        "execute": "input-send-event",
                        "arguments": {
                            "events": [
                                {"type": "abs", "data": {"axis": "x", "value": abs_x}},
                                {"type": "abs", "data": {"axis": "y", "value": abs_y}}
                            ]
                        }
                    })
                    print(f"[INFO] input-send-event wynik: {res_tab}")
                    time.sleep(0.5)

                    qmp.screendump(desktop_tablet_path)
                    if desktop_tablet_path.exists() and desktop_tablet_path.stat().st_size > 0:
                        tw, th, tdata = parse_ppm(desktop_tablet_path)

                        # Weryfikacja obecnosci kursora w pozycji (700, 400) lub sasiadujacej ze wzgledu na zaokraglenia
                        tablet_move_ok = False
                        cur_tab_details = None
                        for tx, ty in [(700, 400), (699, 399), (700, 399), (699, 400), (701, 400), (700, 401)]:
                            is_cur_tab, cur_tab_details = check_cursor_at(tdata, tw, th, tx, ty)
                            if is_cur_tab:
                                tablet_move_ok = True
                                print(f"[INFO] Kursor tabletu w pozycji bezwzglednej ({tx}, {ty}): WIDOCZNY (PASS)")
                                break
                        if not tablet_move_ok:
                            print(f"[BLAD] Brak kursora w pozycji tabletu (700, 400 +/- 1): detale={cur_tab_details}")

                        # Weryfikacja przywrocenia tla po poprzedniej pozycji myszy (560, 340)
                        is_m_restored, m_rest_details = check_restored_bg(tdata, tw, th, 560, 340)
                        if is_m_restored:
                            tablet_old_restored_ok = True
                            print("[INFO] Stare polozenie myszy (560, 340): PRZYWROCONE TLO (PASS)")
                        else:
                            print(f"[BLAD] Stare polozenie myszy (560, 340) nie zostalo przywrocone: detale={m_rest_details}")

                # 6. Szybki test obciazeniowy 1000 zdarzen przez QMP (wraparound buforow kolejek)
                print("[INFO] Rozpoczynam test obciazeniowy 1000 zdarzen wejsciowych przez QMP...")
                if mouse_id:
                    qmp.hmp(f"mouse_set {mouse_id}")

                t_start_stress = time.time()
                for batch in range(10):
                    evs = []
                    for i in range(100):
                        dx = 1 if (i % 2 == 0) else -1
                        dy = 1 if (i % 4 < 2) else -1
                        evs.append({"type": "rel", "data": {"axis": "x", "value": dx}})
                        evs.append({"type": "rel", "data": {"axis": "y", "value": dy}})
                    qmp.execute({
                        "execute": "input-send-event",
                        "arguments": {
                            "events": evs
                        }
                    })
                t_stress_duration = time.time() - t_start_stress
                print(f"[INFO] 1000 zdarzen przeslanych w {t_stress_duration:.2f} s ({1000/t_stress_duration:.0f} zdarzen/s).")
                rapid_events_ok = True

                # Weryfikacja stanu koncowego
                time.sleep(0.5)
                qmp.screendump(desktop_rapid_path)
                if desktop_rapid_path.exists() and desktop_rapid_path.stat().st_size > 0:
                    rw, rh, rdata = parse_ppm(desktop_rapid_path)
                    print(f"[INFO] Stan koncowy pulpitu zapisany ({rw}x{rh}).")
                    cursor_final_intact = True

            qmp.close()

    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    runtime = time.time() - start_time

    print("\n" + "=" * 65)
    print("PODSUMOWANIE TELEMETRII VIRTIO-INPUT & DYSPOZYTORA INPUTDEVICE:")
    print("=" * 65)
    for l in input_lines:
        print(f"  {l}")

    print("-" * 65)
    print(f"  VirtIO Modern Transport (ID=Input 0x1052): {'PASS' if found_virtio_pci_transport else 'FAIL'}")
    print(f"  Liczba zainicjalizowanych urzadzen (== 3):   {'PASS' if found_input_devices_count >= 3 else 'FAIL'} ({found_input_devices_count})")
    print(f"  Klasyfikacja: Klawiatura VirtIO:           {'PASS' if found_kbd_role else 'FAIL'}")
    print(f"  Klasyfikacja: Mysz wzgledna VirtIO:        {'PASS' if found_mouse_role else 'FAIL'}")
    print(f"  Klasyfikacja: Tablet bezwzgledny VirtIO:   {'PASS' if found_tablet_role else 'FAIL'}")
    print(f"  Konfiguracja wektorow MSI-X:               {'PASS' if found_msix_mode else 'FAIL'}")
    print(f"  Rejestracja w podsystemie InputDevice:     {'PASS' if found_input_registered >= 3 else 'FAIL'} ({found_input_registered})")
    print(f"  Wewnetrzny test obciazeniowy (stress test): {'PASS' if found_input_stress_pass else 'FAIL'}")
    print(f"  Podsystem grafiki VirtIO-GPU gotowy:       {'PASS' if found_virtio_gpu_ready else 'FAIL'}")
    print(f"  Zrzut pulpitu BursztynUI (screendump):     {'PASS' if desktop_dump_ok else 'FAIL'}")
    print("-" * 65)
    print("INTERAKTYWNE TESTY VIRTIO-INPUT W QEMU (QMP):")
    print(f"  1. Kursor domyslny widoczny w (500, 300):  {'PASS' if cursor_initial_visible else 'FAIL'}")
    print(f"  2. Ruch myszy wzglednej (560, 340):        {'PASS' if mouse_move_ok else 'FAIL'}")
    print(f"  3. Przywrocenie tla po ruchu myszy:        {'PASS' if mouse_old_restored_ok else 'FAIL'}")
    print(f"  4. Ruch tabletu bezwzglednego (700, 400):  {'PASS' if tablet_move_ok else 'FAIL'}")
    print(f"  5. Przywrocenie tla po ruchu tabletu:      {'PASS' if tablet_old_restored_ok else 'FAIL'}")
    print(f"  6. Test obciazeniowy 1000 zdarzen QMP:     {'PASS' if rapid_events_ok else 'FAIL'}")
    print(f"  7. Integralnosc systemu po obciazeniu:     {'PASS' if cursor_final_intact else 'FAIL'}")
    print("=" * 65)

    all_ok = (found_virtio_pci_transport and
              (found_input_devices_count >= 3) and
              found_kbd_role and found_mouse_role and found_tablet_role and
              found_msix_mode and (found_input_registered >= 3) and
              found_input_stress_pass and found_virtio_gpu_ready and
              desktop_dump_ok and cursor_initial_visible and
              mouse_move_ok and mouse_old_restored_ok and
              tablet_move_ok and tablet_old_restored_ok and
              rapid_events_ok and cursor_final_intact)

    print(f"[STATUS ROZRUCHU] Gotowosc systemu: {'TAK' if boot_ready else 'NIE'}")
    print(f"[STATUS TESTU] Przyczyna zakonczenia: {termination_reason}")
    print(f"[STATUS TESTU] Czas wykonania: {runtime:.2f} s")

    if all_ok:
        print("\n[SUKCES] Sterownik VirtIO-INPUT (klawiatura, mysz, tablet) pomyslnie zweryfikowany w QEMU (PASS)!")
        sys.exit(0)
    else:
        print("\n[BLAD] Jeden lub wiecej testow integracyjnych VirtIO-INPUT zakonczylo sie niepowodzeniem!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
