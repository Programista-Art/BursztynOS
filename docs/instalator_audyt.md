# Audyt przed implementacją instalatora (2026-09-09)

Stan wyjściowy, ustalony z kodu przed dodaniem zapisu GPT:

| Obszar | Ustalenie |
|---|---|
| SATA AHCI | `ahci.cpp`: pierwszy kontroler PCI 01:06:01, pierwszy port ATA, osobne ramki DMA, polling, LBA48, 32 sektory/operację. IDENTIFY odczytuje pojemność i rozmiar sektora, ale nie eksportuje modelu. Brak FLUSH CACHE. |
| IDE | Brak sterownika blokowego ATA PIO/IDE. Nazwa QEMU `ide-hd` nie oznacza obsługi kontrolera IDE: urządzenie jest podłączane do AHCI. |
| NVMe | Tylko rozpoznanie klasy PCI w raporcie. Brak sterownika i operacji blokowych. |
| USB mass storage | xHCI/USB obsługują HID; brak transportu BOT/UAS/SCSI i blokowego dostępu do pamięci USB. Firmware/GRUB może odczytać moduły przed uruchomieniem jądra. |
| BSP2 / PSF | To jeden system plików (`psf.cpp`, `psf.h`), nie dwa różne formaty. Bloki 4096 B, superblok 44 B, 64-bitowe adresy bloków. Działają wskaźniki bezpośrednie, pojedynczy i podwójny pośredni; część komentarzy nagłówka jest nieaktualna. |
| Granice BSP2 | Stała baza LBA 40000. Walidacja względem całego dysku, brak partycji. Formatter zwęża `calkowity_rozmiar` do uint32_t pomimo 64-bitowego pola. Metadane muszą zmieścić się w 16 MiB RAM. Plik: (490+512+512²)*4096 = 1 077 846 016 B. Publiczne rozmiary plików nadal uint32_t. |
| Niezamówione zapisy | Brak sygnatury na LBA 40000 uruchamia automatyczny format! Boot instaluje programy/tapety i zapisuje raport PCI. Samo dodanie GUI nie zapewniłoby zerowych zapisów. |
| RAM BSP2 | Cache danych ma 256 bloków; wyrzucenie brudnego bloku w RAM-only gubi dane. Wymagane rzeczywiste przechowywanie danych RAM. |
| Błędy I/O | Odczyt błędnego bloku zastępuje dane zerami; formatter ignoruje wynik początkowego flush. Konieczne zatrzymanie propagacji błędu. |
| Partycje | Brak parsera/writera GPT/MBR i abstrakcji partycji. |
| GRUB / UEFI | Jeden kernel ELF Multiboot2, start 32-bitowy do long mode. GOP przychodzi przez tag framebuffer; brak potrzeby runtime EFI/NVRAM. |
| ISO | `grub-mkrescue` lokalnie generuje BIOS El Torito, UEFI El Torito `/efi.img`, ochronny MBR, GPT/APM. `xorriso -report_system_area` potwierdza układ hybrydowy, ale nie stanowi testu startu USB. |
| Moduły | GRUB ładuje `tapety.pak`; opcjonalne ISO8K również `test8k.bmp`. PMM rezerwuje moduły. Import tapet zwalnia moduł po synchronizacji. Instalator musi zachować payload do końca instalacji. |
| Pliki boot | ISO zawiera `/boot/system_operacyjny.bin`, GRUB i moduły. Brak gotowego instalacyjnego zestawu GRUB EFI dla dysku. Zostanie wygenerowany lokalnie z narzędzi dystrybucji, bez pobierania podczas instalacji. |
| Tapety | Import ma inwentarz i CRC, brak modułu zachowuje istniejące pliki. Legacy fallback odczytuje BMP od LBA 10. |
| Pliki systemowe | Wbudowane aplikacje, manifesty `.cebula`, fonty/zasoby są instalowane przez `instaluj_wbudowane_programy()` i `utworz_drabine_katalogow()`. Można ponownie użyć tego kodu. |
| Ekran | Konfiguracja zawiera rozdzielczość/bpp/skalę. HAL weryfikuje tryb, GOP jest statyczny. Świeża instalacja nie powinna kopiować konfiguracji bieżącego środowiska. |
| QEMU | Normalny `wirtualny_dysk.img` 512 MiB; `prepare-disk` przy każdym wywołaniu wpisuje BMP od LBA 10! Należy usunąć ten zapis. Nowe testy muszą używać własnego katalogu. |
| Uprawnienia | BWS69/70/71 obsługują IPC, istnieją uprzywilejowane usługi ekranu/tapet. BWS 0..83 pozostaje bez zmian. Instalator wymaga trybu startowego, zaufania PZB i PRAWO_SYSTEM_CONFIG, z tożsamością procesu nadaną przez kernel. |

Pierwsza implementacja: UEFI x86-64, porty pierwszego obsługiwanego HBA,
SATA ATA LBA48, sektor logiczny 512 B (512e dopuszczony), wymagany ATA FLUSH.
NVMe, IDE, USB mass storage jako dyski docelowe i 4Kn: nieobsługiwane.
GPT używa 64-bitowych LBA; limit wydania BSP2 będzie jawny i oddzielny od pojemności dysku.
Brak automatycznego formatowania i brak zapisu na fizyczne dyski w środowisku live.
Opcja „Własny rozmiar” jest pokazywana jako niedostępna: obecna implementacja
nie potrafi udowodnić bezpiecznego użycia wolnego miejsca obok istniejących
partycji, nie przesuwa ich i nie zmniejsza systemów plików.

## Stan po implementacji

- Tryb `bursztyn.install=1` uruchamia `/programy/instalator.cebula/instalator.bur`
  z tego samego kernela. Zwykły start live używa BSP2 wyłącznie w RAM.
- GPT: PMBR, nagłówek i tablica główna oraz zapasowa, CRC32 i odczyt kontrolny.
  Typ partycji ABI Bursztyn OS: `b21fe73c-0414-4585-bd07-8ab4980f9c88`.
- Układ pełnego dysku: ESP FAT32 512 MiB, następnie BSP2 (maks. 2 GiB w tym
  wydaniu); reszta pozostaje nieprzydzielona. Wyrównanie wynosi 1 MiB.
- Obsługiwane są tylko logiczne sektory 512 B (w tym dyski 512e). 4Kn jest
  jawnie odrzucane. ESP dostaje `EFI/BOOT/BOOTX64.EFI`, więc zmienne NVRAM nie
  są wymagane.
- BSP2 wykonuje I/O względem `partition.start_lba + relative_lba`; wszystkie
  odwołania do stałej bazy 40000 i surowego LBA 10 usunięto ze ścieżki pracy.
- Dwukopiowy znacznik instalacji z numerem sekwencji i CRC rozróżnia stan
  `in-progress` od zweryfikowanego `complete`; każdy etap kończy ATA FLUSH i
  stosowny odczyt kontrolny.
- `make installer-test` (2026-09-09): 4491/4491 testów hosta, instalacja na
  pustym dysku, start UEFI bez ISO, reinstalacja, ponowny start bez ISO,
  niezmieniony SHA-256 drugiego dysku oraz start hybrydowego ISO jako USB — PASS.

Referencje formatu:
[UEFI GPT](https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html),
[UEFI FAT/Media Access](https://uefi.org/specs/UEFI/2.10/13_Protocols_Media_Access.html),
[Intel AHCI 1.3.1](https://www.intel.com/content/www/us/en/io/serial-ata/serial-ata-ahci-spec-rev1-3-1.html).

Status sprzętu fizycznego: NOT MANUALLY VERIFIED ON REAL HARDWARE.
