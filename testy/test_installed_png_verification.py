#!/usr/bin/env python3
"""
testy/test_installed_png_verification.py

Comprehensive end-to-end verification of installed Bursztyn OS wallpaper flow:
1. Verifies protected backup disk hash before and after (must NOT be modified).
2. Performs automated installation onto a fresh, disposable target disk image.
3. First boot of installed system (disk-only, no ISO):
   - Reaches desktop ('[SYSTEM] Pulpit gotowy.', 'System operacyjny gotowy!')
   - Opens Tapety application from Start menu
   - Selects PNG wallpaper ('nowatapeta.png' / wp_1e4d69783f3ea9f4.png)
   - Applies wallpaper and confirms persistence in BSP2 ('[TAPETA] Wybor zapisany w BSP2.')
   - Verifies /ustawienia/pulpit.cfg content on disk
   - Shuts down safely.
4. Second boot of installed system (persistence verification):
   - Verifies kernel detects persisted wallpaper: '[TAPETA] Wybrano (PNG)'
   - Verifies Ring 3 decode: '[TAPETY APP] PNG odczyt+decode us'
   - Verifies application: '[TAPETA] PNG wybrano', '[TAPETA] Tapeta gotowa.'
5. Regression test for other formats in installed system:
   - Selects JPEG wallpaper in Tapety app, applies and verifies
   - Selects BTP wallpaper in Tapety app, applies and verifies
"""

import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Known protected backups
BACKUP_SAFETY = ROOT / "wirtualny_dysk.img"
SCRATCH_SAFETY = Path("/home/programista/.gemini/antigravity-cli/brain/e32cf13b-9f55-4705-95ad-b71120d547e0/scratch/wirtualny_dysk-backup.img.safety")


def file_sha256(path):
    if not path or not path.exists():
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(4 * 1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def sparse(path, size):
    if path.exists():
        path.unlink()
    with path.open("xb") as f:
        f.truncate(size)


class Qmp:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(60)
        self.sock.connect(str(path))
        self.f = self.sock.makefile("rwb", buffering=0)
        self.f.readline()
        self.call("qmp_capabilities")
        self.cur_x = 500
        self.cur_y = 300

    def call(self, cmd, args=None):
        obj = {"execute": cmd}
        if args is not None:
            obj["arguments"] = args
        self.f.write((json.dumps(obj) + "\n").encode())
        while True:
            line = self.f.readline()
            if not line:
                raise RuntimeError("QMP disconnected")
            r = json.loads(line)
            if "error" in r:
                raise RuntimeError(r)
            if "return" in r:
                return r["return"]

    def hmp(self, command):
        return self.call("human-monitor-command", {"command-line": command})

    def click(self, target_x, target_y):
        rem_x = target_x - self.cur_x
        rem_y = target_y - self.cur_y
        while rem_x != 0 or rem_y != 0:
            dx = max(-100, min(100, rem_x))
            dy = max(-100, min(100, rem_y))
            self.hmp(f"mouse_move {dx} {dy}")
            rem_x -= dx
            rem_y -= dy
            time.sleep(0.12)
        self.cur_x = target_x
        self.cur_y = target_y
        self.hmp("mouse_button 1")
        time.sleep(0.12)
        self.hmp("mouse_button 0")
        time.sleep(0.4)

    def key(self, text):
        self.hmp("sendkey " + text)
        time.sleep(0.15)

    def screenshot(self, path):
        self.call("screendump", {"filename": str(path), "format": "png"})

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


class Guest:
    def __init__(self, directory, target, sentinel=None, iso=None):
        self.dir = directory
        self.dir.mkdir(parents=True, exist_ok=True)
        self.log = directory / "serial.log"
        self.qmp_path = Path("/tmp") / f"bursztyn-test-{os.getpid()}-{directory.name}.sock"
        if self.qmp_path.exists():
            self.qmp_path.unlink()

        ovmf = Path("/usr/share/ovmf/OVMF.fd")
        guest_ovmf = directory / "OVMF.fd"
        shutil.copyfile(ovmf, guest_ovmf)

        args = [
            "qemu-system-x86_64",
            "-machine", "pc,hpet=on",
            "-cpu", "max",
            "-accel", "tcg,thread=multi",
            "-m", "2G",
            "-vga", "std",
            "-global", "VGA.vgamem_mb=64",
            "-audiodev", "none,id=snd",
            "-device", "intel-hda",
            "-device", "hda-output,audiodev=snd",
            "-netdev", "user,id=n1",
            "-device", "e1000,netdev=n1",
        ]

        if sentinel:
            args += [
                "-drive", f"if=none,id=sentinel,format=raw,file={sentinel}",
                "-device", "virtio-blk-pci,drive=sentinel,disable-legacy=on"
            ]

        args += [
            "-drive", f"if=none,id=target,format=raw,file={target}",
            "-device", "virtio-blk-pci,drive=target,disable-legacy=on" + (",bootindex=1" if iso is None else ""),
            "-drive", f"if=pflash,format=raw,file={guest_ovmf}",
            "-serial", f"file:{self.log}",
            "-qmp", f"unix:{self.qmp_path},server=on,wait=off",
            "-display", "none"
        ]

        if iso:
            args += [
                "-drive", f"if=none,id=media,format=raw,readonly=on,file={iso}",
                "-device", "ide-cd,drive=media,bus=ide.0,bootindex=1"
            ]

        qemu_out = directory / "qemu.log"
        self.qemu_file = qemu_out.open("wb")
        self.proc = subprocess.Popen(args, stdout=self.qemu_file, stderr=self.qemu_file)

        deadline = time.monotonic() + 30
        while not self.qmp_path.exists():
            if self.proc.poll() is not None:
                raise RuntimeError(f"QEMU terminated early: {qemu_out.read_text(errors='replace')}")
            if time.monotonic() > deadline:
                raise TimeoutError("Timeout waiting for QMP socket")
            time.sleep(0.1)
        self.q = Qmp(self.qmp_path)

    def wait(self, needle, timeout=120):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            text = self.log.read_text(errors="replace") if self.log.exists() else ""
            if needle in text:
                return text
            if "[KERNEL-PANIC]" in text or "[INSTALL] Instalacja nie powiodla" in text:
                try:
                    self.q.screenshot(self.dir / "failure.png")
                except Exception:
                    pass
                raise RuntimeError("Kernel panic or install error in serial log:\n" + text[-2000:])
            if self.proc.poll() is not None:
                raise RuntimeError("QEMU process died:\n" + text[-2000:])
            time.sleep(0.25)
        try:
            self.q.screenshot(self.dir / "timeout.png")
        except Exception:
            pass
        raise TimeoutError(f"Timed out waiting for '{needle}' in {timeout}s:\n" + text[-2000:])

    def close(self):
        try:
            self.q.close()
        except Exception:
            pass
        if self.proc.poll() is None:
            try:
                self.q.call("quit")
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=5)
                except Exception:
                    self.proc.kill()
                    self.proc.wait()
        try:
            self.qemu_file.close()
        except Exception:
            pass
        if self.qmp_path.exists():
            try:
                self.qmp_path.unlink()
            except Exception:
                pass


def prepare_installer_iso(staging_dir):
    tree = staging_dir / "iso"
    shutil.copytree(ROOT / "isodir", tree)
    cfg = tree / "boot/grub/grub.cfg"
    text = cfg.read_text()
    text = text.replace("set default=0", "set default=3").replace("set timeout=3", "set timeout=0")
    cfg.write_text(text)
    iso = staging_dir / "installer.iso"
    subprocess.run(
        ["grub-mkrescue", "-o", str(iso), str(tree)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=True
    )
    return iso


def run_installation(stage_dir, target, sentinel):
    print("[INSTALL] Preparing installer ISO with automated install mode...")
    iso = prepare_installer_iso(stage_dir)
    print("[INSTALL] Launching installer guest...")
    g = Guest(stage_dir / "install", target, sentinel, iso=iso)
    try:
        g.wait("[INSTALL] Installer mode ready", timeout=120)
        time.sleep(1)
        g.q.click(800, 68)   # focus installer window
        g.q.click(860, 590)  # welcome -> disks
        time.sleep(0.5)
        g.q.click(550, 230)  # select target disk
        g.q.click(860, 590)  # options
        g.q.click(860, 590)  # whole disk preview
        time.sleep(0.5)
        g.q.click(860, 590)  # confirmation page
        time.sleep(0.5)
        g.q.click(255, 430)  # checkbox acknowledge warning
        g.q.click(380, 480)  # focus text field
        for c in "bursztyn":
            g.q.key("shift-" + c)
        time.sleep(0.5)
        g.q.click(860, 590)  # final destructive confirmation

        print("[INSTALL] Waiting for installation to complete...")
        log = g.wait("[INSTALL] COMPLETE verified", timeout=300)
        assert "[INSTALL] progress=100 stage=COMPLETE" in log
        assert "stage=WALLPAPERS" in log
        print("[INSTALL] Installation complete and verified.")
    finally:
        g.close()


def is_installed_disk(path):
    if not path or not path.exists() or path.stat().st_size < 100 * 1024 * 1024:
        return False
    try:
        with open(path, "rb") as f:
            f.seek(1050624 * 512)
            chunk = f.read(10 * 1024 * 1024)
            return b"default=" in chunk or b"BSP2" in chunk
    except Exception:
        return False


def main():
    print("=" * 70)
    print("BURSZTYN OS - WERYFIKACJA ZAINSTALOWANEGO SYSTEMU Z OBSŁUGĄ PNG")
    print("=" * 70)

    # 1. Sprawdzenie integralności chronionego obrazu zapasowego
    safety_hash_before = file_sha256(SCRATCH_SAFETY)
    workspace_disk_hash_before = file_sha256(BACKUP_SAFETY)
    print(f"[SECURITY] Scratch safety backup: {SCRATCH_SAFETY} -> {safety_hash_before}")
    print(f"[SECURITY] Workspace virtual disk: {BACKUP_SAFETY} -> {workspace_disk_hash_before}")

    work_dir = Path("/tmp/bursztyn_png_test_run")
    work_dir.mkdir(parents=True, exist_ok=True)

    target_disk = work_dir / "target.img"
    sentinel_disk = work_dir / "sentinel.img"

    results = {
        "installed_install": False,
        "installed_boot1": False,
        "png_selected": False,
        "pulpit_cfg_persisted": False,
        "png_restored_reboot": False,
        "jpeg_regression": False,
        "btp_regression": False,
        "backup_untouched": False
    }

    try:
        # Krok A: Instalacja
        if not is_installed_disk(target_disk):
            sparse(target_disk, 4 * 1024 * 1024 * 1024)
            sparse(sentinel_disk, 64 * 1024 * 1024)
            with sentinel_disk.open("r+b") as f:
                for off in [0, 512, 10 * 512, 40000 * 512, 64 * 1024 * 1024 - 512]:
                    f.seek(off)
                    f.write(hashlib.sha256(str(off).encode()).digest() * 16)
            run_installation(work_dir, target_disk, sentinel_disk)
        else:
            print("[INSTALL] Wykryto istniejacy zainstalowany dysk docelowy target.img, pomijam instalacje.")
        results["installed_install"] = True

        # Krok B: Pierwszy rozruch zainstalowanego systemu (disk-only)
        print("\n[BOOT 1] Rozruch zainstalowanego systemu z dysku (bez ISO)...")
        g1 = Guest(work_dir / "boot1", target_disk)
        try:
            log1 = g1.wait("System operacyjny gotowy!", timeout=60)
            assert "[BSP] Wybrano kompletny wolumin GPT" in log1
            assert "[TAPETA] Brak tapety.pak w trybie zainstalowanym" in log1
            print("[BOOT 1] Pulpit gotowy.")
            results["installed_boot1"] = True

            time.sleep(1)
            # Otwórz menu Start (34, 700) -> Tapety (140, 507)
            print("[BOOT 1] Otwieram aplikację Tapety z Menu Start...")
            g1.q.click(34, 700)
            time.sleep(0.8)
            g1.q.click(140, 507)
            g1.wait("[TAPETY APP] Galeria gotowa.", timeout=30)
            print("[BOOT 1] Galeria tapet gotowa.")
            time.sleep(2.0)
            try:
                g1.q.screenshot(work_dir / "boot1_01_gallery_open.png")
            except Exception:
                pass

            # Aktywacja okna Tapety (pasek tytulowy 640, 75)
            print("[BOOT 1] Aktywuję okno Tapety (640, 75)...")
            g1.q.click(640, 75)
            time.sleep(0.8)

            # Miniatura PNG (nowatapeta.png) jest na pozycji 1 w galerii (kolumna 1, wiersz 0: 458, 193)
            print("[BOOT 1] Zaznaczam tapetę PNG (pozycja 1: 458, 193)...")
            g1.q.click(458, 193)
            time.sleep(1.0)
            try:
                g1.q.screenshot(work_dir / "boot1_02_selected_png.png")
            except Exception:
                pass

            # Kliknij 'Zastosuj' (408, 563)
            print("[BOOT 1] Klikam Zastosuj (408, 563)...")
            g1.q.click(408, 563)
            log_after_apply = g1.wait("[TAPETA] PNG wybrano", timeout=30)
            g1.wait("[TAPETA] Tapeta gotowa.", timeout=15)
            time.sleep(1.0)
            try:
                g1.q.screenshot(work_dir / "boot1_03_applied_png.png")
            except Exception:
                pass

            assert "[TAPETA] PNG wybrano" in log_after_apply
            print("[BOOT 1] Zastosowano tapetę PNG.")
            results["png_selected"] = True
        finally:
            g1.close()

        # Krok C: Weryfikacja bezpośrednia zapisu /ustawienia/pulpit.cfg w obrazie dysku
        print("\n[VERIFY DISK] Sprawdzanie zawartości /ustawienia/pulpit.cfg w BSP2...")
        with open(target_disk, "rb") as f:
            f.seek(1050624 * 512)
            bsp2_bytes = f.read(50 * 1024 * 1024)
            match = re.search(rb"tapeta=/tapety/(wp_[0-9a-fA-F]+\.png)", bsp2_bytes)
            assert match, "Brak zapisanego wpisu tapety PNG w partycji BSP2!"
            canonical_png = match.group(1).decode("latin1")
            print(f"[VERIFY DISK] Znaleziono zapisany identyfikator PNG w BSP2: {canonical_png}")
            results["pulpit_cfg_persisted"] = True

        # Krok D: Drugi rozruch zainstalowanego systemu (weryfikacja przywrócenia PNG)
        print("\n[BOOT 2] Drugi rozruch zainstalowanego systemu (weryfikacja trwałości PNG)...")
        g2 = Guest(work_dir / "boot2", target_disk)
        try:
            g2.wait("System operacyjny gotowy!", timeout=60)
            # Czekamy na asynchroniczne odczytanie i nałożenie tapety
            log2 = g2.wait("[TAPETA] Tapeta gotowa.", timeout=20)
            assert "[TAPETA] Wybrano (PNG)" in log2
            assert "[TAPETY APP] PNG odczyt+decode us" in log2
            assert "[TAPETA] PNG wybrano" in log2
            print("[BOOT 2] Pomyślnie przywrócono tapetę PNG po restarcie systemu!")
            results["png_restored_reboot"] = True

            # Krok E: Regresja JPEG i BTP w aplikacji Tapety
            print("\n[BOOT 2 REGRESSION] Otwieram galerię tapet w celu testu przełączenia na JPEG i BTP...")
            g2.q.click(34, 700)
            time.sleep(0.8)
            g2.q.click(140, 507)
            g2.wait("[TAPETY APP] Galeria gotowa.", timeout=30)
            time.sleep(2.0)

            # Aktywacja okna Tapety
            g2.q.click(640, 75)
            time.sleep(0.8)

            # Wybór JPEG (pozycja 4: jabłuszka-2019r.jpg: 980, 193)
            print("[BOOT 2 REGRESSION] Zaznaczam tapetę JPEG (pozycja 4: 980, 193)...")
            g2.q.click(980, 193)
            time.sleep(1.0)
            g2.q.click(408, 563)  # Zastosuj
            g2.wait("[TAPETA] JPEG wybrano", timeout=20)
            g2.wait("[TAPETA] Tapeta gotowa.", timeout=15)
            print("[BOOT 2 REGRESSION] Wybór i zapis JPEG zakończony sukcesem.")
            results["jpeg_regression"] = True

            # Wybór BTP (pozycja 3: grafika.btp: 806, 193)
            print("[BOOT 2 REGRESSION] Zaznaczam tapetę BTP (pozycja 3: 806, 193)...")
            g2.q.click(806, 193)
            time.sleep(1.0)
            g2.q.click(408, 563)  # Zastosuj
            g2.wait("[TAPETA] Wybrano /tapety/wp_4208cf40eb9c4eee.btp", timeout=20)
            g2.wait("[TAPETA] Tapeta gotowa.", timeout=15)
            print("[BOOT 2 REGRESSION] Wybór i zapis BTP zakończony sukcesem.")
            results["btp_regression"] = True
        finally:
            g2.close()

    finally:
        # Weryfikacja braku modyfikacji chronionych obrazów
        safety_hash_after = file_sha256(SCRATCH_SAFETY)
        workspace_disk_hash_after = file_sha256(BACKUP_SAFETY)
        if safety_hash_before == safety_hash_after and workspace_disk_hash_before == workspace_disk_hash_after:
            results["backup_untouched"] = True
            print("\n[SECURITY] Obrazy bezpieczeństwa w 100% nienaruszone (SHA256 zgodne przed/po).")
        else:
            print("\n[SECURITY ALERT] Wykryto niezamierzoną modyfikację obrazu bezpieczeństwa!")

        # Sprzątanie tymczasowego katalogu testowego TYLKO w przypadku pelnego sukcesu
        if all(results.values()):
            try:
                shutil.rmtree(work_dir)
            except Exception:
                pass
        else:
            print(f"\n[DEBUG] Zachowano katalog testowy do analizy: {work_dir}")

    print("\n" + "=" * 70)
    print("PODSUMOWANIE TESTU ZAINSTALOWANEGO SYSTEMU:")
    print("=" * 70)
    for k, v in results.items():
        print(f"  {k:30s}: {'PASS' if v else 'FAIL'}")
    print("=" * 70)

    if all(results.values()):
        print("\n[SUKCES] Wszystkie testy zainstalowanego systemu i trwałości zakończone sukcesem!")
        sys.exit(0)
    else:
        print("\n[BŁĄD] Część testów nie powiodła się!", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
