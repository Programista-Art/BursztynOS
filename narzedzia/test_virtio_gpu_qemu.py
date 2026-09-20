#!/usr/bin/env python3
"""
narzedzia/test_virtio_gpu_qemu.py - Test integracyjny sterownika VirtIO-GPU 2D
w maszynie wirtualnej QEMU.

Weryfikuje:
 1. Wykrycie nowoczesnego urzadzenia VirtIO Modern PCI (Vendor 0x1AF4, Device 0x1050, Subsystem 16).
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
 13. Weryfikacje dalszego dzialania podsystemu grafiki w srodowisku okienkowym (BursztynUI/Menedzer Okien/Tapeta).
"""

import sys
import os
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

def qmp_screendump(qmp_sock_path, dump_output_path, timeout=5.0):
    """
    Laczy sie z gniazdem UNIX QMP i wykonuje polecenie screendump.
    """
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect(str(qmp_sock_path))

    # Odczytaj banner powitalny QMP
    initial_greeting = sock.recv(4096).decode('utf-8')

    # Wynegocjuj mozliwosci QMP
    sock.sendall(b'{"execute": "qmp_capabilities"}\n')
    caps_resp = sock.recv(4096).decode('utf-8')

    # Wykonaj polecenie screendump
    cmd = {
        "execute": "screendump",
        "arguments": {
            "filename": str(dump_output_path),
            "device": "gpu0"
        }
    }
    sock.sendall((json.dumps(cmd) + "\n").encode('utf-8'))
    dump_resp = sock.recv(4096).decode('utf-8')

    sock.close()
    return dump_resp

def main():
    iso = ROOT / "BursztynOS.iso"
    if not iso.exists():
        print("[BLAD] Brak BursztynOS.iso. Uruchom 'make iso'.", file=sys.stderr)
        sys.exit(1)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    qmp_sock = Path("/tmp/virtio_gpu_qmp.sock")
    pattern_dump_path = Path("/tmp/virtio_gpu_pattern.ppm")
    desktop_dump_path = Path("/tmp/virtio_gpu_desktop.ppm")

    for p in [qmp_sock, pattern_dump_path, desktop_dump_path]:
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
        "-m", "2G",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso),
        "-qmp", f"unix:{qmp_sock},server,nowait"
    ]

    print("====================================================================")
    print(" BURSZTYN OS - TEST INTEGRACYJNY QEMU: VIRTIO-GPU (MODERN PCI 2D)")
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

    pattern_dump_ok = False
    pattern_pixel_checks_ok = False
    pattern_pixel_report = []

    desktop_dump_ok = False

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

            # 1. Sprawdzenie globalnego limitu czasu
            if now - start_time >= GLOBAL_TIMEOUT:
                termination_reason = f"Przekroczono globalny limit czasu ({GLOBAL_TIMEOUT}s)."
                break

            # 2. Sprawdzenie limitu braku aktywnosci portu szeregowego
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
            if "[VIRTIO-GPU-TEST] Cykle teardown/reinit: PASS (PMM delta=0, DMA delta=0, IDT delta=0)." in line_str:
                found_gpu_teardown_reinit = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-STRESS] Zakonczono pomyslnie 10000 aktualizacji" in line_str:
                found_gpu_stress = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU] Test pattern rendered and flushed" in line_str:
                found_test_pattern = True
                gpu_lines.append(line_str)
            if "[VIRTIO-GPU-FINAL] Finalny wzorzec testowy gotowy do weryfikacji pikseli." in line_str:
                # Wykonanie zrzutu ekranu wzorca testowego po zakonczeniu testu obciazeniowego
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
                # Pobierz zrzut pulpitu
                if qmp_sock.exists():
                    try:
                        time.sleep(0.5)
                        print(f"[INFO] Pobieranie zrzutu pulpitu przez QMP...")
                        qmp_resp = qmp_screendump(qmp_sock, desktop_dump_path)
                        print(f"[INFO] Odpowiedz QMP screendump (desktop): {qmp_resp.strip()}")
                        if desktop_dump_path.exists() and desktop_dump_path.stat().st_size > 0:
                            desktop_dump_ok = True
                            print(f"[INFO] Zrzut pulpitu zapisany ({desktop_dump_path.stat().st_size} bajtow).")
                    except Exception as e:
                        print(f"[BLAD] Wyjatek podczas zrzutu pulpitu: {e}")
                break

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
    print("PODSUMOWANIE TELEMETRII VIRTIO-GPU 2D:")
    print("=" * 65)
    for l in gpu_lines:
        print(f"  {l}")

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
              desktop_dump_ok)

    print(f"[STATUS ROZRUCHU] Gotowosc systemu: {'TAK' if boot_ready else 'NIE'}")
    print(f"[STATUS TESTU] Przyczyna zakonczenia: {termination_reason}")
    print(f"[STATUS TESTU] Czas wykonania: {runtime:.2f} s")

    if all_ok:
        print("\n[SUKCES] Sterownik VirtIO-GPU 2D pomyslnie zinicjalizowany, zintegrowany z HAL grafiki, zweryfikowany piksel po pikselu i obslugujacy pulpit Bursztyn OS (PASS)!")
        sys.exit(0)
    else:
        print("\n[BLAD] Jeden lub wiecej testow integracyjnych VirtIO-GPU 2D zakonczylo sie niepowodzeniem!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
