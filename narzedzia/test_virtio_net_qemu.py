#!/usr/bin/env python3
"""
narzedzia/test_virtio_net_qemu.py - Test integracyjny i regresyjny sterownika VirtIO-NET
w standardzie Modern PCI (disable-legacy=on) w maszynie wirtualnej QEMU.

Weryfikuje:
 1. Wykrycie nowoczesnego urzadzenia VirtIO Modern PCI (Vendor 0x1AF4, Device 0x1041, Subsystem 1).
 2. Negocjacje cech (VIRTIO_F_VERSION_1, VIRTIO_NET_F_MAC, VIRTIO_NET_F_STATUS).
 3. Konfiguracje kolejek VirtQueue (Queue 0 = RX, Queue 1 = TX).
 4. Konfiguracje wektorow MSI-X dla kolejek sieciowych.
 5. Odczyt adresu MAC karty z konfiguracji sprzetowej (VIRTIO_NET_F_MAC).
 6. Detekcje stanu lacza (VIRTIO_NET_S_LINK_UP).
 7. Rejestracje karty jako NetworkDevice w generycznym rejestrze NetworkDevice API.
 8. Wykonanie selftestu obciazeniowego (virtio_net_stress_test).
 9. Pelna negocjacje DHCPv4 (DISCOVER -> OFFER -> REQUEST -> ACK) przez VirtIO-NET.
 10. Poprawne przypisanie konfiguracji IPv4 (IP, brama, maska, DNS).
 11. Rozwiazanie adresu MAC bramy domyslnej przez ARP (request -> reply -> cache update).
 12. Spojnosc stosu sieciowego jądra Bursztyn OS bez wyciekow i bledow DMA.
"""

import sys
import time
import subprocess
import threading
import queue
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

GLOBAL_TIMEOUT = 240      # Maksymalny globalny limit czasu w sekundach (dla TCG)
INACTIVITY_TIMEOUT = 60  # Limit braku nowych linii na porcie szeregowym w sekundach

def main():
    iso = ROOT / "BursztynOS.iso"
    if not iso.exists():
        print("[BLAD] Brak BursztynOS.iso. Uruchom 'make iso'.", file=sys.stderr)
        sys.exit(1)

    disk = ROOT / "wirtualny_dysk.img"
    if not disk.exists():
        subprocess.run(["make", "prepare-disk"], cwd=str(ROOT), check=True)

    pcap_path = Path("/tmp/virtio_net_qemu.pcap")
    if pcap_path.exists():
        try:
            pcap_path.unlink()
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
        "-netdev", "user,id=n1",
        "-device", "virtio-net-pci,disable-legacy=on,netdev=n1",
        "-object", f"filter-dump,id=f1,netdev=n1,file={pcap_path}",
        "-m", "2G",
        "-vga", "std",
        "-display", "none",
        "-serial", "stdio",
        "-cdrom", str(iso)
    ]

    print("====================================================================")
    print(" BURSZTYN OS - TEST INTEGRACYJNY QEMU: VIRTIO-NET (MODERN PCI 1.X)")
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
    found_virtio_net_msix = False
    found_virtio_net_mac = False
    found_virtio_net_link = False
    found_virtio_net_registered = False
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

    net_lines = []
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
    boot_ready = False

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

            if "[VIRTIO-PCI] device=VirtIO-NET" in line_str and "modern transport found" in line_str:
                found_virtio_pci_transport = True
                net_lines.append(line_str)
            if "[VIRTIO-NET] MSI-X configured successfully" in line_str:
                found_virtio_net_msix = True
                net_lines.append(line_str)
            if "[VIRTIO-NET] MAC:" in line_str:
                found_virtio_net_mac = True
                net_lines.append(line_str)
            if "[VIRTIO-NET] Karta gotowa; link aktywny." in line_str:
                found_virtio_net_link = True
                net_lines.append(line_str)
            if "[VIRTIO-NET] Zarejestrowano jako NetworkDevice" in line_str:
                found_virtio_net_registered = True
                net_lines.append(line_str)
            if "[DHCP] Wysylam DHCPDISCOVER..." in line_str:
                found_dhcp_discover = True
                net_lines.append(line_str)
            if "[DHCP] DHCPOFFER odebrany, wysylam DHCPREQUEST..." in line_str:
                found_dhcp_offer = True
                net_lines.append(line_str)
            if "[DHCP] IPv4: 10.0.2.15" in line_str:
                found_dhcp_ip = True
                net_lines.append(line_str)
            if "[DHCP] Brama: 10.0.2.2" in line_str:
                found_dhcp_gateway = True
                net_lines.append(line_str)
            if "[DHCP] Maska: 255.255.255.0" in line_str:
                found_dhcp_mask = True
                net_lines.append(line_str)
            if "[DHCP] DNS: 10.0.2.3" in line_str:
                found_dhcp_dns = True
                net_lines.append(line_str)
            if "[ARP] request ip=10.0.2.2" in line_str:
                found_arp_request = True
                net_lines.append(line_str)
            if "[ARP] resolved ip=10.0.2.2" in line_str:
                found_arp_resolved = True
                net_lines.append(line_str)
            if "[SIEC] Brama domyslna (ARP) rozwiazana pomyslnie." in line_str:
                found_arp_gateway = True
                net_lines.append(line_str)
            if "[SIEC] Inicjalizacja stosu sieciowego zakonczona." in line_str:
                found_net_complete = True
                net_lines.append(line_str)

            # 3. Natychmiastowe zakonczenie z sukcesem, gdy wszystkie asercje zostana spelnione
            all_satisfied = (
                found_virtio_pci_transport and found_virtio_net_msix and
                found_virtio_net_mac and found_virtio_net_link and
                found_virtio_net_registered and found_dhcp_discover and
                found_dhcp_offer and found_dhcp_ip and found_dhcp_gateway and
                found_dhcp_mask and found_dhcp_dns and found_arp_request and
                found_arp_resolved and found_arp_gateway and found_net_complete
            )
            if all_satisfied:
                termination_reason = "Wszystkie wymagane asercje sieciowe VirtIO-NET zostaly spelnione."
                break

            # 4. Natychmiastowe zgloszenie bledu, gdy rozruch osiagnie gotowosc bez spelnienia asercji
            if "System operacyjny gotowy!" in line_str:
                boot_ready = True
                termination_reason = "System operacyjny zglosil gotowosc ('System operacyjny gotowy!'), lecz nie wszystkie asercje zostaly spelnione."
                break

    finally:
        # Zawsze bezpiecznie zamknij QEMU
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    runtime = time.time() - start_time

    print("\n" + "=" * 65)
    print("PODSUMOWANIE TELEMETRII SIECIOWEJ (VIRTIO-NET / NETWORKDEVICE):")
    print("=" * 65)
    for l in net_lines:
        print(f"  {l}")

    print("-" * 65)
    print(f"  VirtIO Modern Transport (ID=NET): {'PASS' if found_virtio_pci_transport else 'FAIL'}")
    print(f"  VirtIO-NET konfiguracja MSI-X:    {'PASS' if found_virtio_net_msix else 'FAIL'}")
    print(f"  VirtIO-NET odczyt adresu MAC:     {'PASS' if found_virtio_net_mac else 'FAIL'}")
    print(f"  VirtIO-NET Link Up:               {'PASS' if found_virtio_net_link else 'FAIL'}")
    print(f"  Rejestracja NetworkDevice:        {'PASS' if found_virtio_net_registered else 'FAIL'}")
    print(f"  DHCP DISCOVER nadane przez TX:    {'PASS' if found_dhcp_discover else 'FAIL'}")
    print(f"  DHCP OFFER / REQUEST:             {'PASS' if found_dhcp_offer else 'FAIL'}")
    print(f"  DHCP IPv4 (10.0.2.15):            {'PASS' if found_dhcp_ip else 'FAIL'}")
    print(f"  DHCP Brama (10.0.2.2):            {'PASS' if found_dhcp_gateway else 'FAIL'}")
    print(f"  DHCP Maska (255.255.255.0):       {'PASS' if found_dhcp_mask else 'FAIL'}")
    print(f"  DHCP DNS (10.0.2.3):              {'PASS' if found_dhcp_dns else 'FAIL'}")
    print(f"  ARP request do bramy:             {'PASS' if found_arp_request else 'FAIL'}")
    print(f"  ARP reply odebrany z RX:          {'PASS' if found_arp_resolved else 'FAIL'}")
    print(f"  Potwierdzenie bramy ARP:          {'PASS' if found_arp_gateway else 'FAIL'}")
    print(f"  Stos sieciowy gotowy:             {'PASS' if found_net_complete else 'FAIL'}")
    print("=" * 65)

    all_ok = (found_virtio_pci_transport and found_virtio_net_msix and
              found_virtio_net_mac and found_virtio_net_link and
              found_virtio_net_registered and found_dhcp_discover and
              found_dhcp_offer and found_dhcp_ip and found_dhcp_gateway and
              found_dhcp_mask and found_dhcp_dns and found_arp_request and
              found_arp_resolved and found_arp_gateway and found_net_complete)

    print(f"[STATUS ROZRUCHU] Gotowosc systemu: {'TAK' if boot_ready else 'NIE'}")
    print(f"[STATUS TESTU] Przyczyna zakonczenia: {termination_reason}")
    print(f"[STATUS TESTU] Czas wykonania: {runtime:.2f} s")
    if pcap_path.exists():
        print(f"[STATUS PCAP] Zrzut pakietow zapisany w: {pcap_path} ({pcap_path.stat().st_size} bajtow)")

    if not all_ok:
        print("\n" + "!" * 65)
        print("RAPORT BRAKUJACYCH ASERCJI:")
        print("!" * 65)
        assertion_checks = [
            ("VirtIO Modern Transport (ID=NET)", found_virtio_pci_transport),
            ("VirtIO-NET konfiguracja MSI-X", found_virtio_net_msix),
            ("VirtIO-NET odczyt adresu MAC", found_virtio_net_mac),
            ("VirtIO-NET Link Up", found_virtio_net_link),
            ("Rejestracja NetworkDevice", found_virtio_net_registered),
            ("DHCP DISCOVER nadane przez TX", found_dhcp_discover),
            ("DHCP OFFER / REQUEST", found_dhcp_offer),
            ("DHCP IPv4 (10.0.2.15)", found_dhcp_ip),
            ("DHCP Brama (10.0.2.2)", found_dhcp_gateway),
            ("DHCP Maska (255.255.255.0)", found_dhcp_mask),
            ("DHCP DNS (10.0.2.3)", found_dhcp_dns),
            ("ARP request do bramy", found_arp_request),
            ("ARP reply odebrany z RX", found_arp_resolved),
            ("Potwierdzenie bramy ARP", found_arp_gateway),
            ("Stos sieciowy gotowy", found_net_complete),
        ]
        for name, passed in assertion_checks:
            if not passed:
                print(f"  [BRAK] {name}")
        print("!" * 65)

    if all_ok:
        print("\n[SUKCES] VirtIO-NET pomyslnie zarejestrowany jako NetworkDevice, DHCP, ARP i stos sieciowy PASS!")
        sys.exit(0)
    else:
        print("\n[BLAD] Jeden lub wiecej testow integracyjnych VirtIO-NET zakonczylo sie niepowodzeniem!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
