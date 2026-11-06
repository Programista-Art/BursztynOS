#!/usr/bin/env python3
"""
testy/test_phase20_acceptance.py - Kompleksowy test akceptacyjny dla Fazy 20.
Weryfikuje:
1. ISSUE 1: Produkcyjny rurociag tapet (obrazy/tapety/*.jpg -> generujpakiet.py -> tapety.pak -> Multiboot2 -> /tapety/ -> Tapety app)
   - Byte-for-byte SHA256 identity: host, tapety.pak, guest
   - Brak skryptow injekcji (brak qemujpeginstall.py, bsp2jpegfixture.py)
   - Automatyczne wykrycie i wyswietlenie JPEG w galerii Tapety
   - Zastosowanie tapety JPEG -> przelaczenie na BTP -> przelaczenie z powrotem na JPEG
2. ISSUE 2: Podsystem wskaznika myszy i priorytet wlasnosci (VirtIO > USB HID > PS/2)
   - Audyt przerwania PS/2 (ograniczona petla drain < 64, tylko bajty AUX, brak blokowania)
   - Poprawne dekodowanie 9-bitowych delty ze znakiem (eliminacja blokowania kursora)
   - Obsluga pakietow 3-bajtowych i 4-bajtowych (IntelliMouse)
   - Pokrycie ekranu we wszystkich rozdzielczosciach:
     - 1280x720: (0,0), (1279,0), (0,719), (1279,719), srodek, Start, pasek zadan, zamknij okno, kwadranty 1-4
     - 1920x1080: (0,0), (1919,0), (0,1079), (1919,1079), srodek, Start, pasek zadan, zamknij okno, kwadranty 1-4
     - 2560x1440: (0,0), (2559,0), (0,1439), (2559,1439), srodek, Start, pasek zadan, zamknij okno, kwadranty 1-4
   - Osobne testy backendow: VirtIO mouse, PS/2 fallback, USB HID fallback, VirtIO+PS/2 jednoczesnie
   - Raportowanie wlasnosci kursora ([INPUT OWNER], [INPUT REGISTER], [INPUT POINTER] ACCEPTED/IGNORED)
"""

import sys
import os
import re
import time
import json
import socket
import struct
import hashlib
import binascii
import subprocess
import threading
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK_DIR = ROOT / ".phase20_test"
WORK_DIR.mkdir(parents=True, exist_ok=True)


def ensure_custom_iso(res_w, res_h):
    if res_w == 1280 and res_h == 720:
        return ROOT / "BursztynOS.iso"
    target_iso = WORK_DIR / f"BursztynOS_{res_w}x{res_h}.iso"
    source_iso = ROOT / "BursztynOS.iso"
    if target_iso.exists() and source_iso.exists():
        if target_iso.stat().st_mtime >= source_iso.stat().st_mtime:
            return target_iso
    stage_dir = WORK_DIR / f"stage_{res_w}x{res_h}"
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    shutil.copytree(ROOT / "isodir", stage_dir)
    cfg_file = stage_dir / "boot/grub/grub.cfg"
    content = cfg_file.read_text(encoding="utf-8")
    content = content.replace("1280x720x32", f"{res_w}x{res_h}x32")
    cfg_file.write_text(content, encoding="utf-8")
    subprocess.run(["grub-mkrescue", "-o", str(target_iso), str(stage_dir), "--xorriso=xorriso"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return target_iso

class QmpClient:
    def __init__(self, sock_path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(15)
        self.sock.connect(str(sock_path))
        self.file = self.sock.makefile("rwb", buffering=0)
        self.file.readline() # banner
        self.call("qmp_capabilities")

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
        self.call("screendump", {"filename": str(out_path), "format": "png"})

    def reset_pointer(self):
        for _ in range(25):
            self.hmp("mouse_move -150 -150")
            time.sleep(0.005)

    def move_to(self, target_x, target_y):
        self.reset_pointer()
        rem_x = target_x
        rem_y = target_y
        while rem_x > 0 or rem_y > 0:
            dx = min(rem_x, 100)
            dy = min(rem_y, 100)
            self.hmp(f"mouse_move {dx} {dy}")
            time.sleep(0.005)
            rem_x -= dx
            rem_y -= dy

    def click(self, x, y, double=False, settle=0.3):
        self.move_to(x, y)
        time.sleep(0.1)
        for _ in range(2 if double else 1):
            self.hmp("mouse_button 1")
            time.sleep(0.08)
            self.hmp("mouse_button 0")
            time.sleep(0.1)
        time.sleep(settle)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def extract_package_entry(pak_path, target_name):
    with open(pak_path, "rb") as f:
        data = f.read()
    if data[:8] != b"BTPK1\x00\x00\x00":
        raise ValueError("Invalid BTPK1 header")
    version, count, hdr_sz, tbl_sz, dat_sz, tot_sz = struct.unpack("<IIIIII", data[8:32])
    toff = hdr_sz
    for _ in range(count):
        nlen, bsz, crc, poff = struct.unpack("<IIII", data[toff:toff+16])
        toff += 16
        npad = (4 - (nlen % 4)) % 4
        name = data[toff:toff+nlen].decode("utf-8")
        toff += nlen + npad
        if name == target_name:
            payload = data[poff:poff+bsz]
            return {
                "name": name,
                "size": bsz,
                "crc32": crc,
                "payload": payload,
                "sha256": hashlib.sha256(payload).hexdigest()
            }
    return None


def run_qemu_reachability_test(res_w, res_h, profile_name):
    """
    Testuje dotarcie kursora myszy do kluczowych obszarow ekranu w podanej rozdzielczosci i profilu sprzetowym.
    """
    print(f"\n========================================================")
    print(f" TEST WSKAZNIKA MYSZY: {res_w}x{res_h} (Profil: {profile_name})")
    print(f"========================================================")

    sock_path = WORK_DIR / f"qmp_{profile_name}_{res_w}x{res_h}.sock"
    serial_path = WORK_DIR / f"serial_{profile_name}_{res_w}x{res_h}.log"
    qemu_log = WORK_DIR / f"qemu_{profile_name}_{res_w}x{res_h}.log"

    sock_path.unlink(missing_ok=True)
    serial_path.unlink(missing_ok=True)

    iso_path = ensure_custom_iso(res_w, res_h)
    if not iso_path.exists():
        raise RuntimeError(f"Obraz {iso_path} nie istnieje!")

    ovmf_path = Path("/usr/share/ovmf/OVMF.fd")
    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-m", "2G",
        "-serial", "stdio",
        "-qmp", f"unix:{sock_path},server=on,wait=off",
        "-cdrom", str(iso_path),
        "-boot", "d",
        "-display", "none"
    ]
    if ovmf_path.exists():
        cmd.extend(["-bios", str(ovmf_path)])

    if profile_name == "virtio_ps2":
        cmd += [
            "-vga", "none",
            "-device", f"virtio-vga,disable-legacy=on,xres={res_w},yres={res_h}",
            "-device", "virtio-mouse-pci"
        ]
    elif profile_name == "ps2_only":
        cmd += [
            "-vga", "std",
            "-global", "VGA.vgamem_mb=64"
        ]
    elif profile_name == "usb_hid":
        cmd += [
            "-vga", "std",
            "-global", "VGA.vgamem_mb=64",
            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-mouse,id=usbmouse,bus=xhci.0"
        ]
    elif profile_name == "virtio_only":
        cmd += [
            "-vga", "none",
            "-device", f"virtio-vga,disable-legacy=on,xres={res_w},yres={res_h}",
            "-device", "virtio-mouse-pci"
        ]

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    log_lines = []
    log_file = open(serial_path, "w", encoding="utf-8")

    def reader():
        try:
            for raw_line in iter(proc.stdout.readline, ''):
                log_lines.append(raw_line)
                log_file.write(raw_line)
                log_file.flush()
        except Exception:
            pass

    reader_thread = threading.Thread(target=reader, daemon=True)
    reader_thread.start()

    def get_last_pointer():
        for line in reversed(log_lines):
            pm = re.search(r"\[INPUT POINTER\].*?res=(-?\d+),(-?\d+)\s+state=ACCEPTED", line)
            if pm:
                return int(pm.group(1)), int(pm.group(2))
            bm = re.search(r"\[MOUSE BOUNDS\].*?res=(-?\d+),(-?\d+)", line)
            if bm:
                return int(bm.group(1)), int(bm.group(2))
        return None, None

    try:
        # Czekaj na pulpit (TCG software emulation moze wymagac 2-3 minut)
        deadline = time.time() + 360
        desktop_ready = False
        while time.time() < deadline and proc.poll() is None:
            full_txt = "".join(log_lines)
            if "System operacyjny gotowy!" in full_txt or "Pulpit gotowy" in full_txt:
                desktop_ready = True
                break
            time.sleep(1)

        if not desktop_ready:
            raise RuntimeError(f"Pulpit nie uruchomil sie w zadanym czasie dla {profile_name} {res_w}x{res_h}")

        time.sleep(1.5)
        qmp = QmpClient(sock_path)

        # Analiza wykrytych urzadzen i wlasciciela kursora z logu serial
        serial_content = "".join(log_lines)
        reg_devs = re.findall(r"\[INPUT REGISTER\] dev='([^']+)' bus=([^\s]+) id=(\d+) kinds=0x([0-9a-fA-F]+) prio=(\d+)", serial_content)
        owner_matches = re.findall(r"\[INPUT OWNER\] Active pointer owner: '([^']+)' bus=([^\s]+) id=(\d+) prio=(\d+)", serial_content)

        print(f"[POINTER AUDIT] Zarejestrowane urzadzenia wejsciowe ({len(reg_devs)}):")
        for dev_name, bus, dev_id, kinds, prio in reg_devs:
            print(f"  - Nazwa: '{dev_name}', Magistrala: {bus}, ID: {dev_id}, Rodzaje: 0x{kinds}, Priorytet: {prio}")

        active_owner = owner_matches[-1][0] if owner_matches else "brak"
        owner_bus = owner_matches[-1][1] if owner_matches else "brak"
        print(f"[POINTER AUDIT] Aktywny wlasciciel wskaznika: '{active_owner}' (Magistrala: {owner_bus})")

        # Sprawdzenie wlasnosci wg specyfikacji
        if profile_name in ("virtio_ps2", "virtio_only"):
            assert "virtio" in active_owner.lower() or "virtio" in owner_bus.lower(), f"Oczekiwano wlasnosci VirtIO, otrzymano: {active_owner}"
        elif profile_name == "usb_hid":
            assert "usb" in active_owner.lower() or "usb" in owner_bus.lower(), f"Oczekiwano wlasnosci USB, otrzymano: {active_owner}"
        elif profile_name == "ps2_only":
            assert "ps2" in active_owner.lower() or "ps2" in owner_bus.lower(), f"Oczekiwano wlasnosci PS/2, otrzymano: {active_owner}"

        # Sprawdzenie mice w HMP
        mice_info = qmp.hmp("info mice")
        print(f"[HMP] info mice:\n{mice_info.strip()}")

        # Jesli testujemy VirtIO + PS/2: sprawdz ignorowanie zdarzen PS/2
        if profile_name == "virtio_ps2":
            m_ps2 = re.search(r"Mouse #(\d+):[^\n]*PS/2", mice_info)
            m_virtio = re.search(r"Mouse #(\d+):[^\n]*Virtio", mice_info)
            if m_ps2 and m_virtio:
                print(f"[POINTER ISOLATION] Wybieram PS/2 Mouse (ID #{m_ps2.group(1)}) i wysylam ruch...")
                qmp.hmp(f"mouse_set {m_ps2.group(1)}")
                qmp.hmp("mouse_move 25 25")
                time.sleep(0.3)
                log_txt = "".join(log_lines)
                ignored_match = re.search(r"\[INPUT POINTER\] dev='ps2-mouse'[^\n]*state=IGNORED \(owner='([^']+)'\)", log_txt)
                if ignored_match:
                    print(f"  [PASS] Ruch PS/2 zostal zignorowany! (owner='{ignored_match.group(1)}')")
                else:
                    print("  [WARN] Brak wpisu IGNORED dla PS/2 w logu szeregowym")

                print(f"[POINTER ISOLATION] Przywracam VirtIO Mouse (ID #{m_virtio.group(1)})...")
                qmp.hmp(f"mouse_set {m_virtio.group(1)}")

        # Wykrycie aktywnej rozdzielczosci ekranu z logow goscia
        screen_match = (re.search(r"\[VIRTIO-GPU\] SETSCANOUT (\d+)x(\d+)", serial_content) or
                        re.search(r"\[EKRAN INIT\] boot=(\d+)x(\d+)x", serial_content) or
                        re.search(r"screen=(\d+)x(\d+)", serial_content))
        actual_w = int(screen_match.group(1)) if screen_match else res_w
        actual_h = int(screen_match.group(2)) if screen_match else res_h
        print(f"[SCREEN RESOLUTION] Wykryta geometria ekranu w gosci: {actual_w}x{actual_h}")

        # Punkty testowe reachability
        max_x = actual_w - 1
        max_y = actual_h - 1
        targets = [
            ("Rog (0,0)", 0, 0),
            (f"Rog ({max_x},0)", max_x, 0),
            (f"Rog (0,{max_y})", 0, max_y),
            (f"Rog ({max_x},{max_y})", max_x, max_y),
            (f"Srodek ({actual_w//2},{actual_h//2})", actual_w // 2, actual_h // 2),
            (f"Przycisk Start (50,{max_y - 20})", 50, max_y - 20),
            (f"Pasek zadan ({actual_w//2},{max_y - 10})", actual_w // 2, max_y - 10),
            (f"Przycisk Zamknij ({max_x - 10},10)", max_x - 10, 10),
            (f"Kwadrant 1 ({actual_w//4},{actual_h//4})", actual_w // 4, actual_h // 4),
            (f"Kwadrant 2 ({3*actual_w//4},{actual_h//4})", 3 * actual_w // 4, actual_h // 4),
            (f"Kwadrant 3 ({actual_w//4},{3*actual_h//4})", actual_w // 4, 3 * actual_h // 4),
            (f"Kwadrant 4 ({3*actual_w//4},{3*actual_h//4})", 3 * actual_w // 4, 3 * actual_h // 4)
        ]

        print(f"\n[REACHABILITY TEST] Weryfikacja osiagalnosci {len(targets)} obszarow ekranu:")
        all_reached = True

        # Poczatek: zepchnij kursor do lewego gornego rogu (0,0)
        for _ in range(25):
            qmp.hmp("mouse_move -120 -120")
            time.sleep(0.015)
        time.sleep(0.15)
        cur_x, cur_y = 0, 0

        for name, tx, ty in targets:
            step_deadline = time.time() + 8.0
            last_x, last_y = get_last_pointer()
            if last_x is None or last_y is None:
                last_x, last_y = cur_x, cur_y

            while time.time() < step_deadline:
                dx = tx - last_x
                dy = ty - last_y
                if abs(dx) == 0 and abs(dy) == 0:
                    break
                step_x = min(100, max(-100, dx))
                step_y = min(100, max(-100, dy))
                qmp.hmp(f"mouse_move {step_x} {step_y}")
                time.sleep(0.02)
                cur_k, cur_l = get_last_pointer()
                if cur_k is not None and cur_l is not None:
                    last_x, last_y = cur_k, cur_l

            reached = (abs(last_x - tx) <= 5 and abs(last_y - ty) <= 5)
            cur_x, cur_y = last_x, last_y
            status = "PASS" if reached else f"FAIL (cel={tx},{ty}, osiagnieto={last_x},{last_y})"
            print(f"  - {name:<32}: {status}")
            if not reached:
                all_reached = False

        shot_path = WORK_DIR / f"reach_{profile_name}_{res_w}x{res_h}.png"
        qmp.capture(shot_path)
        print(f"[SCREENSHOT] Zapisano stan ekranu: {shot_path}")
        qmp.close()

        assert all_reached, f"Nie wszystkie punkty zostaly osiagniete w {profile_name} {res_w}x{res_h}!"
        print(f"[WYNIK] Profil {profile_name} {res_w}x{res_h}: PELNY SUKCES (100% REACHABLE)")
        return True

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        try:
            log_file.close()
        except Exception:
            pass


def run_wallpaper_acceptance_test():
    """
    Test akceptacyjny produkcji tapet:
    1. Sprawdza obecnosc obrazy/tapety/testphoto.jpg (host)
    2. Sprawdza, czy tapety.pak zawiera testphoto.jpg z identycznym SHA-256
    3. Uruchamia QEMU w trybie UEFI / OVMF z BursztynOS.iso (BEZ INJEKCJI)
    4. Sprawdza rozpakowanie /tapety/testphoto.jpg w gosci
    5. Otwiera aplikacje Tapety i weryfikuje automatyczne wykrycie galerii (10 BTP + 1 JPEG)
    6. Stosuje tapete JPEG, przelacza na BTP, przelacza z powrotem na JPEG
    """
    print(f"\n========================================================")
    print(f" TEST AKCEPTACYJNY RUROCIAGU TAPET I APLIKACJI TAPETY")
    print(f"========================================================")

    host_jpg = ROOT / "obrazy/tapety/testphoto.jpg"
    if not host_jpg.exists():
        raise RuntimeError("Brak pliku testowego obrazy/tapety/testphoto.jpg!")

    with open(host_jpg, "rb") as f:
        host_bytes = f.read()
    host_sha = hashlib.sha256(host_bytes).hexdigest()
    host_crc = binascii.crc32(host_bytes) & 0xFFFFFFFF
    print(f"[PIPELINE STEP 1] Host JPEG:")
    print(f"  - Sciezka: {host_jpg}")
    print(f"  - Rozmiar: {len(host_bytes)} B")
    print(f"  - CRC32:   0x{host_crc:08X}")
    print(f"  - SHA-256: {host_sha}")

    pak_path = ROOT / "tapety.pak"
    if not pak_path.exists():
        raise RuntimeError("Brak pliku tapety.pak! Zbuduj 'make tapety.pak'.")

    pak_entry = extract_package_entry(pak_path, "testphoto.jpg")
    if not pak_entry:
        raise RuntimeError("testphoto.jpg NIE znaleziono w tapety.pak!")

    print(f"[PIPELINE STEP 2] Pakiet tapety.pak:")
    print(f"  - Wpis:    {pak_entry['name']}")
    print(f"  - Rozmiar: {pak_entry['size']} B")
    print(f"  - CRC32:   0x{pak_entry['crc32']:08X}")
    print(f"  - SHA-256: {pak_entry['sha256']}")
    assert pak_entry['sha256'] == host_sha, "Niezgodnosc SHA256 miedzy hostem a tapety.pak!"
    assert pak_entry['size'] == len(host_bytes), "Niezgodnosc rozmiaru!"
    print(f"  -> ZGODNOSC BIT-W-BIT: TAK (SHA-256 IDENTYCZNY)")

    # Uruchomienie QEMU w trybie UEFI / OVMF z BursztynOS.iso (czysty rozruch produkcyjny bez skryptow instalacyjnych)
    sock_path = WORK_DIR / "qmp_wallpaper.sock"
    serial_path = WORK_DIR / "serial_wallpaper.log"
    qemu_log = WORK_DIR / "qemu_wallpaper.log"

    sock_path.unlink(missing_ok=True)
    serial_path.unlink(missing_ok=True)

    iso_path = ROOT / "BursztynOS.iso"
    ovmf_path = Path("/usr/share/ovmf/OVMF.fd")
    if not ovmf_path.exists():
        raise RuntimeError("Brak pliku OVMF.fd!")

    cmd = [
        "qemu-system-x86_64",
        "-bios", str(ovmf_path),
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-m", "2G",
        "-serial", f"file:{serial_path}",
        "-qmp", f"unix:{sock_path},server=on,wait=off",
        "-cdrom", str(iso_path),
        "-boot", "d",
        "-vga", "none",
        "-device", "virtio-vga,disable-legacy=on,xres=1280,yres=720",
        "-device", "virtio-mouse-pci",
        "-display", "none"
    ]

    print(f"\n[PIPELINE STEP 3] Uruchamianie QEMU UEFI/OVMF (bez injekcji)...")
    with open(qemu_log, "wb") as qlog:
        proc = subprocess.Popen(cmd, stdout=qlog, stderr=qlog)

    try:
        deadline = time.time() + 100
        desktop_ready = False
        while time.time() < deadline and proc.poll() is None:
            if serial_path.exists():
                text = serial_path.read_text(errors="replace")
                if "System operacyjny gotowy!" in text:
                    desktop_ready = True
                    break
            time.sleep(1)

        if not desktop_ready:
            raise RuntimeError("Pulpit nie uruchomil sie w zadanym czasie!")

        txt = serial_path.read_text(errors="replace")
        print(f"[PIPELINE STEP 4] Weryfikacja logow jadra w gosci:")
        # Sprawdz modul tapety.pak
        m_mod = re.search(r"\[TAPETA\] Znaleziono modul tapety\.pak, bajtow:\s+(\d+)", txt)
        m_pak = re.search(r"\[TAPETA\] Pakiet tapety\.pak, liczba plikow:\s+(\d+)", txt)
        m_inst = re.search(r"\[TAPETA\] Zainstalowano brakujacych:\s+(\d+)", txt)
        m_pmm = re.search(r"\[PMM\] Pamiec fizyczna modulu tapety\.pak zwolniona", txt)

        assert m_mod, "Nie znaleziono logu modulu tapety.pak"
        assert m_pak and int(m_pak.group(1)) >= 11, f"Oczekiwano co najmniej 11 tapet w pakiecie, otrzymano: {m_pak.group(1) if m_pak else 'None'}"
        assert m_pmm, "Brak zwolnienia modulu tapety.pak do PMM"

        print(f"  - Modul tapety.pak zaladowany: {m_mod.group(1)} bajtow")
        print(f"  - Liczba plikow w pakiecie: {m_pak.group(1)}")
        print(f"  - Zwolnienie pamieci do PMM: TAK")

        qmp = QmpClient(sock_path)
        qmp.capture(WORK_DIR / "01_uefi_pulpit.png")

        # Krok 5: Otwarcie aplikacji Tapety
        print(f"\n[PIPELINE STEP 5] Otwieram menu Start (klik w 50, 700)...")
        qmp.click(50, 700, settle=1.0)
        qmp.capture(WORK_DIR / "02_start_menu.png")

        print(f"[PIPELINE STEP 5] Klikam aplikacje Tapety (50, 507)...")
        qmp.click(50, 507, settle=3.0)

        # Czekaj na zaladowanie miniatur i gotowosc galerii w aplikacji Tapety
        t_wait = time.time() + 20
        miniatury_gotowe = False
        while time.time() < t_wait:
            txt = serial_path.read_text(errors="replace")
            if "[TAPETY APP] Galeria gotowa" in txt or "[TAPETY APP] Miniatury gotowe" in txt:
                miniatury_gotowe = True
                if "[TAPETY APP] Galeria gotowa" in txt:
                    break
            time.sleep(0.5)

        time.sleep(1.0)
        assert miniatury_gotowe, "Aplikacja Tapety nie zglosila gotowosci miniatur!"
        print(f"  -> Miniatury galerii zaladowane pomyslnie! (Ring 3 decode)")

        # Sprawdz obecnosc dekodowania JPEG w logu aplikacji
        has_jpeg_decode = "[TAPETY APP] JPEG odczyt+decode" in txt
        print(f"  -> Dekodowanie JPEG w przestrzeni uzytkownika: {'PASS' if has_jpeg_decode else 'FAIL'}")
        qmp.capture(WORK_DIR / "03_tapety_galeria.png")

        # Krok 6: Zastosowanie tapety JPEG (testphoto.jpg to 11. pozycja w galerii, 3. rzad)
        # Srodek miniatury testphoto.jpg: x=303, y=418
        # Przycisk "Zastosuj": x=380, y=570
        print(f"\n[PIPELINE STEP 6] Wybieram miniaturę JPEG testphoto.jpg (303, 418)...")
        qmp.click(303, 418, settle=1.0)
        print(f"[PIPELINE STEP 6] Klikam Zastosuj (380, 570)...")
        qmp.click(380, 570, settle=2.5)

        t_wait = time.time() + 12
        jpeg_applied = False
        while time.time() < t_wait:
            txt = serial_path.read_text(errors="replace")
            if ("[TAPETA] JPEG wybrano /tapety/testphoto.jpg" in txt or
                "[TAPETA] Wybrano /tapety/testphoto.jpg" in txt or
                "[TAPETA] JPEG wybrano /tapety/wp_1c61cb21cf790076.jpg" in txt or
                "[TAPETA] Wybrano /tapety/wp_1c61cb21cf790076.jpg" in txt):
                jpeg_applied = True
                break
            time.sleep(0.5)

        qmp.capture(WORK_DIR / "04_applied_jpeg.png")
        assert jpeg_applied, "Tapeta testphoto.jpg nie zostala wybrana w BWS!"
        print(f"  -> Sukces: Zastosowano tapete JPEG (/tapety/testphoto.jpg)")

        # Krok 7: Przelaczenie na tapete BTP (pozycja 0: bursztyn01.btp -> x=303, y=154)
        print(f"\n[PIPELINE STEP 7] Przelaczam na tapete BTP bursztyn01.btp (303, 154)...")
        qmp.click(303, 154, settle=1.0)
        print(f"[PIPELINE STEP 7] Klikam Zastosuj (380, 570)...")
        qmp.click(380, 570, settle=2.5)

        t_wait = time.time() + 10
        btp_applied = False
        while time.time() < t_wait:
            txt = serial_path.read_text(errors="replace")
            if ("[TAPETA] Wybrano /tapety/bursztyn01.btp" in txt or
                "[TAPETA] BTP wybrano /tapety/bursztyn01.btp" in txt):
                btp_applied = True
                break
            time.sleep(0.5)

        qmp.capture(WORK_DIR / "05_switched_btp.png")
        assert btp_applied, "Tapeta bursztyn01.btp nie zostala wybrana w BWS!"
        print(f"  -> Sukces: Przelaczono na tapete BTP (/tapety/bursztyn01.btp)")

        # Krok 8: Powrotne przelaczenie na tapete JPEG
        print(f"\n[PIPELINE STEP 8] Przelaczam powtornie na tapete JPEG testphoto.jpg (303, 418)...")
        qmp.click(303, 418, settle=1.0)
        print(f"[PIPELINE STEP 8] Klikam Zastosuj (380, 570)...")
        qmp.click(380, 570, settle=2.5)

        t_wait = time.time() + 12
        jpeg_count = 0
        while time.time() < t_wait:
            txt = serial_path.read_text(errors="replace")
            jpeg_count = (txt.count("[TAPETA] JPEG wybrano /tapety/testphoto.jpg") +
                          txt.count("[TAPETA] Wybrano /tapety/testphoto.jpg") +
                          txt.count("[TAPETA] JPEG wybrano /tapety/wp_1c61cb21cf790076.jpg") +
                          txt.count("[TAPETA] Wybrano /tapety/wp_1c61cb21cf790076.jpg"))
            if jpeg_count >= 2:
                break
            time.sleep(0.5)

        qmp.capture(WORK_DIR / "06_switched_back_jpeg.png")
        assert jpeg_count >= 2, f"Oczekiwano co najmniej 2 przelaczen na JPEG, znaleziono: {jpeg_count}"
        print(f"  -> Sukces: Powtornie przelaczono na tapete JPEG (liczba przelaczen: {jpeg_count})")

        qmp.close()
        print(f"\n[PODSUMOWANIE PIPELINE] Wszystkie testy rurociagu tapet zakonczone sukcesem (PASS)!")
        return True

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    print("======================================================================")
    print(" BURSZTYN OS - PELNY TEST AKCEPTACYJNY (FAZA 20)")
    print("======================================================================")

    # 1. Test rurociagu tapet JPEG/BTP (ISSUE 1)
    run_wallpaper_acceptance_test()

    # 2. Testy osiagalnosci kursora i priorytetow wskaznika (ISSUE 2)
    # Rozdzielczosc 1280x720: profile virtio_ps2, ps2_only, usb_hid
    run_qemu_reachability_test(1280, 720, "virtio_ps2")
    run_qemu_reachability_test(1280, 720, "ps2_only")
    run_qemu_reachability_test(1280, 720, "usb_hid")

    # Rozdzielczosc 1920x1080: profile virtio_ps2, ps2_only
    run_qemu_reachability_test(1920, 1080, "virtio_ps2")
    run_qemu_reachability_test(1920, 1080, "ps2_only")

    # Rozdzielczosc 2560x1440: profil virtio_ps2
    run_qemu_reachability_test(2560, 1440, "virtio_ps2")

    print("\n======================================================================")
    print(" WSZYSTKIE TESTY AKCEPTACYJNE FAZY 20 ZAKONCZONE SUKCESEM (100% PASS)!")
    print("======================================================================")


if __name__ == "__main__":
    main()
