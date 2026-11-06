#!/usr/bin/env python3
"""
testy/test_e1000_qemu.py - Test regresyjny karty Intel E1000 i NetworkDevice API w QEMU.

Weryfikuje:
 1. Wykrycie sprzetowe kontrolera Intel E1000 (8086:100E) na magistrali PCI.
 2. Odczyt adresu MAC karty z rejestrow / EEPROM.
 3. Detekcje stanu fizycznego lacza (link status: Up).
 4. Rejestracje karty jako NetworkDevice ("eth0") w generycznym rejestrze NetworkDevice API.
 5. Negocjacje DHCPv4 (DISCOVER -> OFFER -> REQUEST -> ACK) przez NetworkDevice.
 6. Prawidlowe przypisanie konfiguracji IPv4 (IP, brama, maska, DNS).
 7. Rozwiazanie adresu MAC bramy domyslnej przez ARP (request -> reply -> cache update).
 8. Brak regresji w inicjalizacji podsystemu sieciowego jądra.
"""

import sys
import time
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    iso = ROOT / "BursztynOS.iso"
    if not iso.exists():
        print("[BLAD] Brak BursztynOS.iso. Uruchom 'make iso'.", file=sys.stderr)
        sys.exit(1)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    cmd = [
        "qemu-system-x86_64",
        "-machine", "pc,hpet=on",
        "-cpu", "max",
        "-accel", "tcg,thread=single",
        "-drive", f"id=disk,file={disk},format=raw,if=none",
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0,unit=0",
        "-netdev", "user,id=n1",
        "-device", "e1000,netdev=n1",
        "-m", "2G",
        "-vga", "std",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso)
    ]

    print("====================================================================")
    print(" BURSZTYN OS - TEST REGRESYJNY QEMU: E1000 I NETWORKDEVICE API")
    print("====================================================================")
    print(f"[INFO] Uruchamianie QEMU: {' '.join(cmd)}")

    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    found_e1000_pci = False
    found_e1000_mac = False
    found_e1000_link = False
    found_netdev_reg = False
    found_dhcp_discover = False
    found_dhcp_offer = False
    found_dhcp_ip = False
    found_dhcp_gateway = False
    found_dhcp_mask = False
    found_dhcp_dns = False
    found_arp_request = False
    found_arp_resolved = False
    found_arp_gateway = False
    found_net_complete = False

    e1000_lines = []
    start_time = time.time()
    timeout = 180  # sekund (uwzglednia powolna emulacje TCG single-thread na maszynach CI)

    try:
        while time.time() - start_time < timeout:
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                time.sleep(0.05)
                continue
            line_str = line.strip()
            print(f"[SERIAL] {line_str}")

            if "[E1000] Szukam Intel 82540EM (8086:100E)..." in line_str:
                found_e1000_pci = True
                e1000_lines.append(line_str)
            if "[E1000] MAC:" in line_str:
                found_e1000_mac = True
                e1000_lines.append(line_str)
            if "[E1000] Karta gotowa; link aktywny." in line_str:
                found_e1000_link = True
                e1000_lines.append(line_str)
            if "[E1000] Zarejestrowano jako NetworkDevice (eth0)." in line_str:
                found_netdev_reg = True
                e1000_lines.append(line_str)
            if "[DHCP] Wysylam DHCPDISCOVER..." in line_str:
                found_dhcp_discover = True
                e1000_lines.append(line_str)
            if "[DHCP] DHCPOFFER odebrany, wysylam DHCPREQUEST..." in line_str:
                found_dhcp_offer = True
                e1000_lines.append(line_str)
            if "[DHCP] IPv4: 10.0.2.15" in line_str:
                found_dhcp_ip = True
                e1000_lines.append(line_str)
            if "[DHCP] Brama: 10.0.2.2" in line_str:
                found_dhcp_gateway = True
                e1000_lines.append(line_str)
            if "[DHCP] Maska: 255.255.255.0" in line_str:
                found_dhcp_mask = True
                e1000_lines.append(line_str)
            if "[DHCP] DNS: 10.0.2.3" in line_str:
                found_dhcp_dns = True
                e1000_lines.append(line_str)
            if "[ARP] request ip=10.0.2.2" in line_str:
                found_arp_request = True
                e1000_lines.append(line_str)
            if "[ARP] resolved ip=10.0.2.2" in line_str:
                found_arp_resolved = True
                e1000_lines.append(line_str)
            if "[SIEC] Brama domyslna (ARP) rozwiazana pomyslnie." in line_str:
                found_arp_gateway = True
                e1000_lines.append(line_str)
            if "[SIEC] Inicjalizacja stosu sieciowego zakonczona." in line_str:
                found_net_complete = True
                e1000_lines.append(line_str)

            if "System operacyjny gotowy!" in line_str or "[BSP] Drzewiasty system plikow PSF gotowy" in line_str:
                # Kernel osiagnal gotowosc
                if found_net_complete and found_arp_gateway:
                    break
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("\n" + "=" * 60)
    print("PODSUMOWANIE TELEMETRII SIECIOWEJ (E1000 / NETWORKDEVICE):")
    print("=" * 60)
    for l in e1000_lines:
        print(f"  {l}")

    print("-" * 60)
    print(f"  E1000 PCI detekcja:             {'PASS' if found_e1000_pci else 'FAIL'}")
    print(f"  E1000 MAC odczyt:               {'PASS' if found_e1000_mac else 'FAIL'}")
    print(f"  E1000 Link Up:                  {'PASS' if found_e1000_link else 'FAIL'}")
    print(f"  Rejestracja NetworkDevice eth0: {'PASS' if found_netdev_reg else 'FAIL'}")
    print(f"  DHCP DISCOVER wyslane:          {'PASS' if found_dhcp_discover else 'FAIL'}")
    print(f"  DHCP OFFER / REQUEST:           {'PASS' if found_dhcp_offer else 'FAIL'}")
    print(f"  DHCP IPv4 (10.0.2.15):          {'PASS' if found_dhcp_ip else 'FAIL'}")
    print(f"  DHCP Brama (10.0.2.2):          {'PASS' if found_dhcp_gateway else 'FAIL'}")
    print(f"  DHCP Maska (255.255.255.0):     {'PASS' if found_dhcp_mask else 'FAIL'}")
    print(f"  DHCP DNS (10.0.2.3):            {'PASS' if found_dhcp_dns else 'FAIL'}")
    print(f"  ARP request do bramy:           {'PASS' if found_arp_request else 'FAIL'}")
    print(f"  ARP resolved dla bramy:         {'PASS' if found_arp_resolved else 'FAIL'}")
    print(f"  Potwierdzenie bramy ARP:        {'PASS' if found_arp_gateway else 'FAIL'}")
    print(f"  Stos sieciowy zainicjalizowany: {'PASS' if found_net_complete else 'FAIL'}")
    print("=" * 60)

    if (found_e1000_pci and found_e1000_mac and found_e1000_link and
        found_netdev_reg and found_dhcp_discover and found_dhcp_offer and
        found_dhcp_ip and found_dhcp_gateway and found_dhcp_mask and
        found_dhcp_dns and found_arp_request and found_arp_resolved and
        found_arp_gateway and found_net_complete):
        print("\n[SUKCES] E1000 pomyslnie zarejestrowany jako NetworkDevice, DHCP, ARP i stos sieciowy PASS!")
        sys.exit(0)
    else:
        print("\n[BLAD] Jeden lub wiecej testow regresyjnych sieci zakonczylo sie niepowodzeniem!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
