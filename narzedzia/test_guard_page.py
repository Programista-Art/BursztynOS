#!/usr/bin/env python3
"""
narzedzia/test_guard_page.py - Kontrolowany test regresyjny naruszenia guard page stosu jadra w QEMU.
Weryfikuje, ze:
  1. Dolna strona stosu jadra (P=0, unmapped guard page) wywoluje wyjatek #PF przy probie zapisu/przekroczenia.
  2. Obsluga wyjatku w idt.cpp prawidlowo identyfikuje naruszenie guard page.
  3. Zostaje wyemitowany komunikat diagnostyczny ze wszystkimi wymaganymi polami:
     - [STACK] KRYTYCZNE przekroczenie stosu jadra
     - pid, program, rsp, stackbase, stacktop, used, lastbws
  4. Nie dochodzi do skoku pod RIP=0 (PFEC=0x10), triple faultu ani cichej korupcji pamieci.
"""

import os
import sys
import subprocess
import tempfile
import shutil
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    kernel = ROOT / "system_operacyjny.bin"
    tapety = ROOT / "tapety.pak"
    isodir_src = ROOT / "isodir"

    if not kernel.exists():
        print(f"[BLAD] Brak pliku jadra: {kernel}. Zbuduj najpierw 'make kernel'.", file=sys.stderr)
        sys.exit(1)
    if not tapety.exists():
        print(f"[BLAD] Brak pakietu tapet: {tapety}. Zbuduj najpierw 'make tapety.pak'.", file=sys.stderr)
        sys.exit(1)
    if not isodir_src.exists():
        print(f"[BLAD] Brak katalogu staging: {isodir_src}. Zbuduj najpierw 'make iso'.", file=sys.stderr)
        sys.exit(1)

    ovmf = Path('/usr/share/ovmf/OVMF.fd')
    if not ovmf.is_file():
        print("[BLAD] Wymagany plik /usr/share/ovmf/OVMF.fd", file=sys.stderr)
        sys.exit(1)

    print("=" * 80)
    print(" BURSZTYN OS - REGRESYJNY TEST GUARD PAGE STOSU JADRA (QEMU UEFI)")
    print("=" * 80)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)
        iso_dir = tmp_path / "isodir"
        shutil.copytree(isodir_src, iso_dir)

        grub_cfg = iso_dir / "boot" / "grub" / "grub.cfg"
        grub_cfg.write_text(
            'set timeout=0\n'
            'set default=0\n'
            'menuentry "Bursztyn OS - Guard Page Regression Test" {\n'
            '    insmod all_video\n'
            '    set gfxmode=1280x720x32,1024x768x32,auto\n'
            '    set gfxpayload=keep\n'
            '    multiboot2 /boot/system_operacyjny.bin bursztyn.test=stack_guard\n'
            '    module2 /boot/tapety.pak tapety.pak\n'
            '    boot\n'
            '}\n'
        )

        test_iso = tmp_path / "stack_guard_test.iso"
        print("[1/3] Budowanie testowego obrazu ISO z bursztyn.test=stack_guard...")
        cmd_iso = [
            "grub-mkrescue",
            "-o", str(test_iso),
            str(iso_dir),
            "--xorriso=xorriso"
        ]
        res = subprocess.run(cmd_iso, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if res.returncode != 0 or not test_iso.exists():
            print("[BLAD] grub-mkrescue zakonczyl sie bledem!", file=sys.stderr)
            sys.exit(1)

        guest_ovmf = tmp_path / "OVMF.fd"
        shutil.copyfile(ovmf, guest_ovmf)
        serial_log = tmp_path / "serial.log"

        print("[2/3] Uruchamianie QEMU UEFI z przechwytywaniem portu szeregowego do pliku...")
        qemu_cmd = [
            "qemu-system-x86_64",
            "-machine", "pc,hpet=on",
            "-cpu", "max",
            "-accel", "tcg,thread=single",
            "-m", "2G",
            "-vga", "std",
            "-global", "VGA.vgamem_mb=64",
            "-display", "none",
            "-drive", f"if=pflash,format=raw,file={guest_ovmf}",
            "-drive", f"if=none,id=media,format=raw,readonly=on,file={test_iso}",
            "-device", "ide-cd,drive=media,bus=ide.0,bootindex=1",
            "-serial", f"file:{serial_log}",
            "-no-reboot"
        ]

        proc = subprocess.Popen(qemu_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        start_time = time.time()
        test_completed = False
        full_log = ""

        # Pelny rozruch TCG (OVMF + GRUB + kernel_main az do testu przy
        # pid 0) potrafi zajac ponad 3 minuty na wolnym hoscie; limit
        # uwzglednia margines, a postep jest raportowany co 10 s.
        limit_czasu = int(os.environ.get("BURSZTYN_GUARD_TIMEOUT", "420"))
        last_print = start_time
        while time.time() - start_time < limit_czasu:
            now = time.time()
            if now - last_print >= 10:
                print(f"  ...oczekiwanie na rozruch UEFI/jadra ({int(now - start_time)}s)...", flush=True)
                last_print = now
            if serial_log.exists():
                full_log = serial_log.read_text(errors="replace")
                if "[STACK] KRYTYCZNE przekroczenie stosu jadra" in full_log:
                    test_completed = True
                    time.sleep(1)
                    full_log = serial_log.read_text(errors="replace")
                    break
            if proc.poll() is not None:
                break
            time.sleep(0.5)

        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
            except OSError:
                pass
        if serial_log.exists():
            full_log = serial_log.read_text(errors="replace")

        print("[3/3] Analiza raportu z portu szeregowego...")
        print("-" * 80)
        for line in full_log.splitlines():
            if any(k in line for k in ["[STACK", "[TEST", "Page Fault", "CR2", "RIP", "used=", "stackbase=", "lastbws="]):
                print(line)
        print("-" * 80)

        # Dowod, ze blad dotyczy zamierzonej guard page, a nie przypadku:
        # CR2 musi byc rowny celowi z fazy przed-bledem i lezec w oczekiwanym
        # zakresie guard page slotu PID 0; Present musi wynosic 0.
        import re
        m_cel = re.search(r"\[STACK TEST\] cel=0x([0-9A-Fa-f]+)", full_log)
        m_guard = re.search(r"\[STACK TEST\] guard_oczekiwany=0x([0-9A-Fa-f]+)\.\.0x([0-9A-Fa-f]+)", full_log)
        m_cr2 = re.search(r"\[STACK\] cr2=0x([0-9A-Fa-f]+)", full_log)
        assert m_cel and m_guard and m_cr2, "Brak trojki dowodowej cel/guard_oczekiwany/cr2 w logu!"
        cel = int(m_cel.group(1), 16)
        glo = int(m_guard.group(1), 16)
        ghi = int(m_guard.group(2), 16)
        cr2 = int(m_cr2.group(1), 16)
        assert cr2 == cel, f"CR2 (0x{cr2:X}) != cel testu (0x{cel:X})!"
        assert glo <= cr2 < ghi, f"CR2 (0x{cr2:X}) poza guard_oczekiwany [0x{glo:X}, 0x{ghi:X})!"
        print(f"[DOWOD] CR2 == cel == 0x{cr2:X} w guard_oczekiwany [0x{glo:X}, 0x{ghi:X}), Present=0.")

        assert test_completed or "[STACK] KRYTYCZNE przekroczenie stosu jadra" in full_log, f"Brak wykrycia naruszenia guard page w logu! Otrzymano:\n{full_log[-1000:]}"
        assert "[STACK TEST] Rozpoczynam kontrolowany test przekroczenia guard page" in full_log, "Brak wejscia do stos_wykonaj_test_przepelnienia!"
        assert "[STACK TEST] tryb=dostep_bezposredni" in full_log, "Brak jawnego oznaczenia testu jako dostep bezposredni (nie pelzanie)!"
        assert "[STACK TEST] stos_aktywny=BOOTSTACK" in full_log, "Test powinien pracowac na stosie startowym (PID 0)!"
        assert "[STACK] KRYTYCZNE przekroczenie stosu jadra" in full_log, "Brak naglowka diagnostyki stosu!"
        assert "[STACK] pid=0" in full_log, "Brak raportu pid=0 w diagnostyce stosu!"
        assert "[STACK] program=" in full_log, "Brak raportu nazwy programu!"
        assert "[STACK] rsp=0x" in full_log, "Brak raportu RSP w diagnostyce stosu!"
        assert "[STACK] stackbase=0x" in full_log, "Brak raportu stackbase!"
        assert "[STACK] stacktop=0x" in full_log, "Brak raportu stacktop!"
        assert "[STACK] used=UNKNOWN" in full_log, "RSP spoza zarzadzanego stosu musi dac used=UNKNOWN (zakaz unsigned underflow)!"
        assert "[STACK] rspoutsidemanagedstack=1" in full_log, "Brak flagi rspoutsidemanagedstack=1!"
        assert "[STACK] stos=BOOTSTACK" in full_log, "Aktywny stos powinien zostac zidentyfikowany jako BOOTSTACK!"
        assert "[STACK] tss_rsp0=0x" in full_log, "Brak raportu TSS RSP0!"
        assert "[STACK] present=0" in full_log, "Guard page musi byc odmapowana (Present=0)!"
        assert "[STACK] lastbws=" in full_log, "Brak raportu lastbws!"

        # Upewnij sie, ze nie doszlo do bledu wykonania kodu pod adresem 0
        assert "RIP: 0x0000000000000000" not in full_log, "BLAD: Wykryto skok pod adres NULL RIP!"
        assert "Typ PF Page Fault\nKod bledu 0x0000000000000010" not in full_log, "BLAD: Wykryto instrukcyjne PFEC=0x10 pod adresem 0!"

        print("\n[SUKCES] Naruszenie unmapped guard page (P=0) zostalo przechwycone sprzetowo przez MMU.")
        print("[SUKCES] Wyjatek #PF prawidlowo zidentyfikowal adres guard page i wyemitowal pelna diagnostyke.")
        print("[SUKCES] Brak skoku pod RIP=0, brak korupcji powrotu, system zatrzymany bezpiecznie.")
        print("=" * 80)

if __name__ == "__main__":
    main()
