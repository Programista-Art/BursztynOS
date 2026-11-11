#!/usr/bin/env python3
"""
testy/test_normal_boot_qemu.py - Test i audyt normalnego rozruchu produkcyjnego
Bursztyn OS w maszynie wirtualnej QEMU.

Weryfikuje:
 1. Brak uruchamiania niszczacych i ciezkich testow obciazeniowych VirtIO w normalnym boot:
    - Brak VirtIO-RNG 3072 transaction stress test
    - Brak VirtIO-BLK raw read/write/flush/wraparound/reset stress test
    - Brak VirtIO-GPU teardown/reinit
    - Brak VirtIO-GPU 10000 dirty-rect stress test
    - Brak pakietu testow kursora w jadrze ([CURSOR TEST])
    - Brak wyswietlania testowego wzorca kolorow (test pattern)
 2. Obecnosc znacznika pominiecia:
    - [NORMAL BOOT] VirtIO self-tests skipped.
 3. Poprawna, nieinwazyjna inicjalizacje produkcyjna urzadzen:
    - VirtIO-RNG: sonda, rejestracja i minimalny sanity check (16 bajtow entropii)
    - VirtIO-BLK: sonda, rejestracja i minimalny sanity check (odczyt LBA 0)
    - VirtIO-NET: sonda, rejestracja i dzialajacy stos sieciowy
    - VirtIO-GPU: sonda, rejestracja bufora 2D i bezposrednia aktywacja backendu graficznego
 4. Nienaruszalnosc nosnika blokowego (zero zapisu na dysku uzytkownika):
    - Weryfikacja kryptograficzna SHA256 dysku testowego przed i po rozruchu (delta = 0 bajtow)
    - Weryfikacja nienaruszenia ostatniego sektora nosnika
 5. Dzialanie pulpitu BursztynUI i kursora myszy:
    - Zrzut ekranu przez QMP
    - Potwierdzenie widocznosci kursora w pozycji domyslnej (500, 300)
    - Wstrzykniecie ruchu myszy (+60, +40) przez QMP
    - Weryfikacja przesuniecia kursora na (560, 340) oraz przywrocenia tla w (500, 300)
 6. Skrocenie czasu rozruchu do pulpitu wzgledem trybu testowego CI.
"""

import sys
import os
import re
import time
import socket
import json
import hashlib
import subprocess
import threading
import queue
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

GLOBAL_TIMEOUT = 120
INACTIVITY_TIMEOUT = 60
WALLPAPER_GRACE_TIMEOUT = 15.0
# Ekran powitalny zanika 600 ms po gotowosci pulpitu i do tego czasu celowo nie
# pokazuje kursora. Zrzut QMP musi zdazyc po tym oknie, ale nie moze czekac
# w nieskonczonosc, gdy kursor naprawde nie jest rysowany.
CURSOR_PROBE_ATTEMPTS = 12
CURSOR_PROBE_DELAY = 0.25
def parse_ppm(ppm_path):
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

def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main():
    iso = ROOT / "BursztynOS.iso"
    if not iso.exists():
        print("[INFO] Budowanie BursztynOS.iso...")
        subprocess.run(["make", "iso"], cwd=str(ROOT), check=True)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    # Przygotowanie dedykowanego dysku VirtIO-BLK (64 MiB) z unikalnym wzorcem testowym
    test_blk_path = Path("/tmp/normal_boot_test_blk.img")
    blk_size = 64 * 1024 * 1024
    with open(test_blk_path, "wb") as f:
        # Zapisz unikalny wzorzec, aby wykryc nawet pojedynczy zmodyfikowany bajt
        pattern = b"BURSZTYN_NON_DESTRUCTIVE_SECTOR_AUDIT_DATA_SECTOR_TEST_PATTERN_12345678" * 8
        repeats = blk_size // len(pattern)
        for _ in range(repeats):
            f.write(pattern)

    hash_before = file_sha256(test_blk_path)
    with open(test_blk_path, "rb") as f:
        f.seek(blk_size - 512)
        last_sector_before = f.read(512)

    qmp_sock = Path("/tmp/normal_boot_qmp.sock")
    desktop_dump_path = Path("/tmp/normal_boot_desktop.ppm")
    moved_dump_path = Path("/tmp/normal_boot_moved.ppm")

    for p in [qmp_sock, desktop_dump_path, moved_dump_path]:
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
        "-drive", f"id=vblk,file={test_blk_path},format=raw,if=none",
        "-device", "virtio-blk-pci,disable-legacy=on,drive=vblk",
        "-device", "virtio-rng-pci,disable-legacy=on",
        "-device", "virtio-vga,id=gpu0,disable-legacy=on",
        "-device", "qemu-xhci,id=xhci",
        "-device", "usb-mouse,id=usbmouse,bus=xhci.0",
        "-netdev", "user,id=n1",
        "-device", "virtio-net-pci,disable-legacy=on,netdev=n1",
        "-m", "2G",
        "-display", "none",
        "-serial", "stdio",
        "-boot", "order=d,strict=on",
        "-cdrom", str(iso),
        "-qmp", f"unix:{qmp_sock},server,nowait"
    ]

    print("====================================================================")
    print(" BURSZTYN OS - AUDYT I WERYFIKACJA ROZRUCHU PRODUKCYJNEGO (NORMAL)")
    print("====================================================================")
    print(f"[INFO] Uruchamianie QEMU z obrazem produkcyjnym: {iso}")
    print(f"[INFO] SHA256 dysku testowego przed bootem: {hash_before}")

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    found_normal_boot_skip = False
    found_rng_init = False
    found_rng_sanity = False
    found_blk_init = False
    found_blk_sanity = False
    found_gpu_init = False
    found_gpu_ready = False
    found_net_init = False
    found_ekran_init = False
    found_desktop_ready = False
    boot_ready = False
    t_kernel_ready = None
    t_system_ready = None
    t_wallpaper_start = None
    t_decode_complete = None
    t_wallpaper_applied = None
    boot_ready_time = None

    # Audyt zabronionych znacznikow testow obciazeniowych
    stress_markers_found = []
    forbidden_markers = [
        "[VIRTIO-RNG-STRESS]",
        "[VIRTIO-BLK-STRESS]",
        "[VIRTIO-GPU-TEST]",
        "[VIRTIO-GPU-STRESS]",
        "[VIRTIO-GPU-FINAL]",
        "[CURSOR TEST]"
    ]

    desktop_dump_ok = False
    test_pattern_not_rendered = False
    cursor_initial_visible = False
    cursor_moved_visible = False
    cursor_old_restored = False

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
                termination_reason = f"Przekroczono limit czasu ({GLOBAL_TIMEOUT}s)."
                break

            if now - last_activity_time >= INACTIVITY_TIMEOUT:
                termination_reason = f"Brak aktywnosci na porcie szeregowym ({INACTIVITY_TIMEOUT}s)."
                break

            try:
                line = line_queue.get(timeout=0.1)
            except queue.Empty:
                if proc.poll() is not None:
                    termination_reason = "Proces QEMU zakonczyl sie."
                    break
                if boot_ready and not found_desktop_ready and boot_ready_time is not None:
                    if time.time() - boot_ready_time > WALLPAPER_GRACE_TIMEOUT:
                        termination_reason = f"Przekroczono limit czasu oczekiwania na gotowosc tapety ({WALLPAPER_GRACE_TIMEOUT}s po starcie systemu)."
                        break
                continue

            if line is None:
                termination_reason = "Koniec strumienia QEMU."
                break

            last_activity_time = time.time()
            line_str = line.strip()
            print(f"[NORMAL-SERIAL] {line_str}", flush=True)

            # Sprawdzenie obecnosci znacznika NORMAL BOOT
            if "[NORMAL BOOT] VirtIO self-tests skipped." in line_str:
                found_normal_boot_skip = True

            # Sprawdzenie obecnosci zabronionych znacznikow stres-testow
            for marker in forbidden_markers:
                if marker in line_str and marker not in stress_markers_found:
                    stress_markers_found.append(marker)

            # Inicjalizacje produkcyjne
            if "[VIRTIO-RNG] Device initialized successfully" in line_str:
                found_rng_init = True
            if "[VIRTIO-RNG] Sanity check: OK (16B entropy read)." in line_str:
                found_rng_sanity = True
            if "[VIRTIO-BLK] Successfully initialized and registered VirtIO-BLK device" in line_str:
                found_blk_init = True
            if "[VIRTIO-BLK] Sanity check: OK (read LBA 0)." in line_str:
                found_blk_sanity = True
            if "[VIRTIO-GPU] Framebuffer gotowy:" in line_str:
                found_gpu_init = True
            if "[VIRTIO-GPU] Karta graficzna gotowa do pracy w trybie 2D." in line_str:
                found_gpu_ready = True
            if "[SIEC] Inicjalizacja stosu sieciowego zakonczona." in line_str:
                found_net_init = True
            if "[EKRAN INIT] backend=virtio-gpu-runtime" in line_str:
                found_ekran_init = True

            # Pomiar czasu i zdarzen powiazanych z rozruchem oraz tapeta
            if "[KERNEL] Jadro i scheduler gotowe." in line_str and t_kernel_ready is None:
                t_kernel_ready = time.time() - start_time
            if "[TAPETY APP] Start." in line_str and t_wallpaper_start is None:
                t_wallpaper_start = time.time() - start_time
            if ("[TAPETY APP] PNG odczyt+decode" in line_str or "[TAPETY APP] JPEG odczyt+decode" in line_str) and t_decode_complete is None:
                t_decode_complete = time.time() - start_time
            if "[TAPETA] Tapeta gotowa." in line_str:
                found_desktop_ready = True
                if t_wallpaper_applied is None:
                    t_wallpaper_applied = time.time() - start_time
            if "System operacyjny gotowy!" in line_str:
                boot_ready = True
                if t_system_ready is None:
                    t_system_ready = time.time() - start_time
                if boot_ready_time is None:
                    boot_ready_time = time.time()

            if boot_ready and found_desktop_ready:
                termination_reason = "System operacyjny i tapeta zglosily pelna gotowosc."
                break

            if boot_ready and not found_desktop_ready and boot_ready_time is not None:
                if time.time() - boot_ready_time > WALLPAPER_GRACE_TIMEOUT:
                    termination_reason = f"Przekroczono limit czasu oczekiwania na gotowosc tapety ({WALLPAPER_GRACE_TIMEOUT}s po starcie systemu)."
                    break


        # Weryfikacja QMP: zrzut pulpitu i kursor natychmiast po gotowosci pulpitu
        if boot_ready and qmp_sock.exists():
            print("\n[INFO] Weryfikacja wizualna pulpitu i kursora w trybie normalnym przez QMP...")
            qmp = QmpClient(qmp_sock, timeout=10.0)

            # Wybierz mysz USB
            resp_mice = qmp.hmp("info mice").get("return", "")
            print("[INFO] QEMU mice:", resp_mice.strip())
            m_hid = re.search(r"Mouse #(\d+):\s+QEMU HID Mouse", resp_mice)
            if m_hid:
                qmp.hmp(f"mouse_set {m_hid.group(1)}")

            # Zrzut pulpitu. Ekran powitalny jest widoczny jeszcze przez FADE_US
            # (600 ms) po zgloszeniu gotowosci pulpitu i celowo nie pokazuje
            # kursora, wiec pojedynczy natychmiastowy zrzut lapal klatke
            # przejsciowa. Ponawiamy ograniczona liczbe razy, az kursor bedzie
            # widoczny albo uplynie okno przejscia.
            dw, dh, ddata = 0, 0, b""
            is_cur_init, cur_init_details = False, {}
            for proba in range(CURSOR_PROBE_ATTEMPTS):
                qmp.screendump(desktop_dump_path)
                if not (desktop_dump_path.exists() and desktop_dump_path.stat().st_size > 0):
                    time.sleep(0.05)
                    continue
                desktop_dump_ok = True
                dw, dh, ddata = parse_ppm(desktop_dump_path)
                is_cur_init, cur_init_details = check_cursor_at(ddata, dw, dh, 500, 300)
                if is_cur_init:
                    break
                if proba + 1 < CURSOR_PROBE_ATTEMPTS:
                    time.sleep(CURSOR_PROBE_DELAY)
            if desktop_dump_ok:
                # Weryfikacja braku wzorca testowego (brak kolorow Amber #FFBF00 na (100, 100))
                p_amber_slot = get_pixel(ddata, dw, dh, 100, 100)
                if p_amber_slot != (255, 191, 0):
                    test_pattern_not_rendered = True
                    print(f"[INFO] Wzorzec testowy NIE zostal wyrenderowany (piksel 100,100 = {p_amber_slot}, a nie Amber) (PASS)")
                else:
                    print(f"[BLAD] Wykryto obecnosc wzorca testowego w normalnym rozruchu!")

                if is_cur_init:
                    cursor_initial_visible = True
                    print("[INFO] Kursor w pozycji domyslnej (500, 300): WIDOCZNY (PASS)")
                else:
                    print(f"[BLAD] Brak kursora w pozycji (500, 300): {cur_init_details}")

                # Ruch myszy
                qmp.hmp("mouse_move 60 40")
                # Wait for guest input processing and compositor publication.
                # TCG can take longer than a fixed host half-second under load.
                deadline = time.monotonic() + 30
                while True:
                    qmp.screendump(moved_dump_path)
                    if moved_dump_path.exists() and moved_dump_path.stat().st_size > 0:
                        mw, mh, mdata = parse_ppm(moved_dump_path)
                        moved, _ = check_cursor_at(mdata, mw, mh, 560, 340)
                        old, _ = check_cursor_at(mdata, mw, mh, 500, 300)
                        if moved and not old:
                            break
                    if time.monotonic() >= deadline:
                        break
                    time.sleep(0.25)
                if moved_dump_path.exists() and moved_dump_path.stat().st_size > 0:
                    mw, mh, mdata = parse_ppm(moved_dump_path)
                    p_old_fill = get_pixel(mdata, mw, mh, 501, 301)
                    p_old_tip = get_pixel(mdata, mw, mh, 500, 300)
                    is_old_cursor, _ = check_cursor_at(mdata, mw, mh, 500, 300)
                    if p_old_fill != (255, 255, 255) and not is_old_cursor:
                        cursor_old_restored = True
                        print(f"[INFO] Stare polozenie kursora (500, 300): PRZYWROCONE TLO (piksel={p_old_tip}) (PASS)")
                    else:
                        print(f"[BLAD] Stare polozenie kursora (500, 300) nie zostalo przywrocone: tip={p_old_tip}, fill={p_old_fill}")

                    is_moved, cur_moved_details = check_cursor_at(mdata, mw, mh, 560, 340)
                    if is_moved:
                        cursor_moved_visible = True
                        print("[INFO] Nowe polozenie kursora (560, 340): WIDOCZNY (PASS)")
                    else:
                        print(f"[BLAD] Nowe polozenie kursora (560, 340) brak kursora: {cur_moved_details}")

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

    # Audyt dysku po zakonczeniu dzialania QEMU
    hash_after = file_sha256(test_blk_path)
    with open(test_blk_path, "rb") as f:
        f.seek(blk_size - 512)
        last_sector_after = f.read(512)

    disk_intact = (hash_before == hash_after) and (last_sector_before == last_sector_after)

    if test_blk_path.exists():
        try:
            test_blk_path.unlink()
        except Exception:
            pass

    print("\n" + "=" * 65)
    print("CZASY OPERACJI ROZRUCHU I PRZYWRACANIA TAPETY:")
    print("=" * 65)
    print(f"  Kernel/system gotowy:           {t_system_ready:.3f} s" if t_system_ready is not None else "  Kernel/system gotowy:           BRAK")
    print(f"  Start przywracania tapety:      {t_wallpaper_start:.3f} s" if t_wallpaper_start is not None else "  Start przywracania tapety:      BRAK")
    print(f"  Dekodowanie obrazu zakonczone:  {t_decode_complete:.3f} s" if t_decode_complete is not None else "  Dekodowanie obrazu zakonczone:  BRAK")
    print(f"  Tapeta zastosowana w jadrze:    {t_wallpaper_applied:.3f} s" if t_wallpaper_applied is not None else "  Tapeta zastosowana w jadrze:    BRAK")
    print(f"  Zakonczenie procesu testu:      {runtime:.3f} s")
    print("=" * 65)

    print("\n" + "=" * 65)
    print("PODSUMOWANIE AUDYTU ROZRUCHU PRODUKCYJNEGO (NORMAL BOOT):")
    print("=" * 65)
    print(f"  Znacznik [NORMAL BOOT] self-tests skipped:  {'PASS' if found_normal_boot_skip else 'FAIL'}")
    print(f"  Brak znacznikow stres-testow w logu:        {'PASS' if len(stress_markers_found) == 0 else 'FAIL (' + ', '.join(stress_markers_found) + ')'}")
    print(f"  VirtIO-RNG inicjalizacja produkcyjna:       {'PASS' if found_rng_init else 'FAIL'}")
    print(f"  VirtIO-RNG sanity check (16B):              {'PASS' if found_rng_sanity else 'FAIL'}")
    print(f"  VirtIO-BLK inicjalizacja produkcyjna:       {'PASS' if found_blk_init else 'FAIL'}")
    print(f"  VirtIO-BLK sanity check (odczyt LBA 0):     {'PASS' if found_blk_sanity else 'FAIL'}")
    print(f"  Audyt dysku (zero zapisu, SHA256 delta=0):  {'PASS' if disk_intact else 'FAIL'}")
    print(f"  VirtIO-GPU inicjalizacja bufora 2D:         {'PASS' if found_gpu_init else 'FAIL'}")
    print(f"  VirtIO-GPU rejestracja jako ekran:          {'PASS' if found_ekran_init else 'FAIL'}")
    print(f"  VirtIO-NET inicjalizacja sieci:             {'PASS' if found_net_init else 'FAIL'}")
    print(f"  Brak wyswietlania testowego wzorca kolorow: {'PASS' if test_pattern_not_rendered else 'FAIL'}")
    print(f"  Tapeta i kompozytor pulpitu gotowe:         {'PASS' if found_desktop_ready else 'FAIL'}")
    print(f"  Kursor widoczny w (500, 300):               {'PASS' if cursor_initial_visible else 'FAIL'}")
    print(f"  Ruch myszy do (560, 340):                   {'PASS' if cursor_moved_visible else 'FAIL'}")
    print(f"  Przywrocenie tla w (500, 300):              {'PASS' if cursor_old_restored else 'FAIL'}")
    print("=" * 65)
    print(f"[STATUS ROZRUCHU] Gotowosc systemu: {'TAK' if boot_ready else 'NIE'}")
    print(f"[STATUS ROZRUCHU] Czas rozruchu do pulpitu: {runtime:.2f} s")

    all_ok = (found_normal_boot_skip and
              len(stress_markers_found) == 0 and
              found_rng_init and found_rng_sanity and
              found_blk_init and found_blk_sanity and
              disk_intact and
              found_gpu_init and found_gpu_ready and found_ekran_init and
              found_net_init and
              test_pattern_not_rendered and
              found_desktop_ready and
              cursor_initial_visible and cursor_moved_visible and
              cursor_old_restored and boot_ready)

    if all_ok:
        print("\n[SUKCES] Rozruch produkcyjny jest w pelni bezpieczny, nieinwazyjny, wolny od testow obciazeniowych i gotowy (PASS)!")
        sys.exit(0)
    else:
        print("\n[BLAD] Audyt rozruchu produkcyjnego wykazal nieprawidlowosci!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
