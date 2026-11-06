# Jedna sesja Live ISO — Explorer, 2026-10-04

Walidacja runtime została podjęta w **jednej** sesji QEMU. Zakończyła się blokadą startu przed pojawieniem się pulpitu. Nie jest to wynik PASS ani dowód regresji samego Explorera.

## Uruchomienie

Użyto istniejącego `/home/programista/Pulpit/Burstzyn/BursztynOS/BursztynOS.iso` i targetu `make runuefi`, z jednorazowym pominięciem przygotowania dysku i ponownej budowy ISO:

```sh
make -o prepare-disk -o uefi runuefi \
  QEMU_DISK_DRIVE= QEMU_VIRTIO_BLK= \
  'QEMUDISPLAYARGS=-display sdl -qmp unix:/tmp/bursztyn-live-once.sock,server=on,wait=off'
```

Makefile nie był edytowany. Zachowano `/usr/share/ovmf/OVMF.fd`, SDL, `virtio-tablet-pci`, `virtio-keyboard-pci`, `tcg,thread=multi`, 1 vCPU, 2 GiB RAM oraz pozostałe urządzenia produkcyjnego profilu. Wyłączono wyłącznie argumenty brakującego dysku/BLK. QMP służył do odczytu stanu i zrzutów; nie używano GDB, zapisu pamięci gościa, rebootów ani stress loopów.

`query-block` potwierdziło tylko zamontowane, readonly `BursztynOS.iso` oraz puste napędy floppy/SD. **Nie użyto ani nie utworzono persistent virtual disk; nie użyto `disp.img`.**

## Zaobserwowany problem

GRUB automatycznie uruchomił normalny wpis `Bursztyn OS`. System doszedł do jądra i logowania inicjalizacji urządzeń. Pulpit nie pojawił się. Okno SDL pozostawało czarne przez ponad trzy minuty od uruchomienia procesu QEMU. Log zatrzymał się po:

```text
[VIRTIO-PCI] device=VirtIO-RNG (Generator losowy) modern transport found (BDF 00:04.0)
[VIRTIO-PCI] common cfg BAR=4 offset=0x0 len=0x1000
[VIRTIO-PCI] notify cfg BAR=4 offset=0x3000 len=0x1000 multiplier=4
[VIRTIO-PCI] features device=0x10130000000 driver=0x100000000
```

QMP zaczął przekraczać limit odpowiedzi; końcowy zrzut wykonano z okna hosta. Ostatni komunikat logu nie dowodzi, że przyczyną jest RNG. Nie wykonywano spekulatycznych napraw ani drugiego uruchomienia. QEMU nie zakończyło się po SIGTERM, więc ten konkretny proces zakończono SIGKILL.

## Wyniki runtime

| Kontrola | Wynik |
|---|---|
| Explorer otwiera się | NIE SPRAWDZONO — brak pulpitu |
| Natychmiastowe odświeżenie create folder/file | NIE SPRAWDZONO |
| Rename Backspace/Delete | NIE SPRAWDZONO |
| Polski UTF-8 i edycja New Folder/New File | NIE SPRAWDZONO |
| Move chooser i nawigacja po katalogach | NIE SPRAWDZONO |
| Natychmiastowe odświeżenie move/copy/paste/delete | NIE SPRAWDZONO |
| Widoczność scrollbara, thumb i ostatni wpis | NIE SPRAWDZONO |
| Kółko i przeciąganie scrollbara | NIE SPRAWDZONO |
| Nawigacja bez flash/full repaint | NIE SPRAWDZONO |
| Brak starej brązowej ramki i poprawne dekoracje WM | NIE SPRAWDZONO |
| Explorer context menu: dokładne `Usuń` | NIE SPRAWDZONO |
| Puste tło pulpitu: menu i enabled/disabled | NIE SPRAWDZONO |
| Kontekst pliku i katalogu pulpitu | NIE SPRAWDZONO |
| Brak click-through | NIE SPRAWDZONO |
| Live theme switching i font Unicode | NIE SPRAWDZONO |

Nie utworzono żadnych plików ani katalogów w gościu, nie modyfikowano istniejących danych. Liczba mutacji filesystemu: **0**. Sprzątanie plików testowych nie było potrzebne. Żadna mutacja nie trafiła na persistent storage; planowane testy były ograniczone do Live ISO RAM filesystem. Persistencja po reboot nie była testowana.

Dowody: `.wm-test/live-explorer-runtime/serial.log`, `results.json`, zrzuty `01-boot.png`–`04-live.png` oraz końcowy `07-host-window.png`. Automatyczne wyniki z wcześniejszej pracy pozostają odrębne od tej nieukończonej walidacji runtime.
