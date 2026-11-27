# BRB — architektura ścieżki UEFI x86-64

Status: **[PROJEKT]**. Odwołania do specyfikacji: „Unified Extensible Firmware Interface (UEFI)
Specification, Release 2.10” (UEFI Forum, 2022). Numery rozdziałów podano według wydania 2.10 i należy je
potwierdzić podczas implementacji. Fakty o repozytorium: `audyt-rozruchu.md`.

---

## 1. Cel i zakres

BRB-UEFI jest aplikacją UEFI (PE32+, `IMAGE_SUBSYSTEM_EFI_APPLICATION` = 10, maszyna `0x8664`). Zadania:

1. Odczytać konfigurację, jądro i moduły z nośnika rozruchowego.
2. Zwalidować je (`bezpieczenstwo.md`).
3. Ustawić tryb graficzny GOP w formacie XRGB8888.
4. Zebrać RSDP i mapę pamięci, zakończyć usługi rozruchowe (`ExitBootServices`).
5. Przekazać sterowanie zgodnie z `protokol-rozruchowy.md`.

BRB nie implementuje sterowników sprzętu, jeśli istnieje udokumentowany protokół UEFI.

---

## 2. Rozmieszczenie na nośnikach

### 2.1. Ścieżka domyślna nośnika wymiennego

UEFI 2.10, rozdz. 3.5.1.1 (*Removable Media Boot Behavior*): na x64 domyślną ścieżką jest
`\EFI\BOOT\BOOTX64.EFI` na partycji systemowej EFI (FAT). **[FAKT]** Obecny instalator zapisuje tam GRUB
(`storage_format.cpp:400`). Jądro nie wywołuje żadnych usług UEFI (Z-12), więc instalator nie tworzy zmiennych
`Boot####` — firmware uruchamia plik jako domyślny program nośnika. **[WNIOSEK]** BRB może przejąć dokładnie tę
ścieżkę bez zmiany układu ESP.

### 2.2. Nazewnictwo artefaktów BRB

| Artefakt | Nazwa robocza (Fazy 2–14) | Nazwa produkcyjna (po decyzji migracji) |
|---|---|---|
| aplikacja UEFI | `.brb-build/uefi/brb-x64.efi` | na ESP: `\EFI\BOOT\BOOTX64.EFI` |
| kopia rezerwowa | — | `\EFI\BURSZTYN\BRB-X64.EFI` (sekcja 2.3) |
| konfiguracja | `\EFI\BURSZTYN\BRB.CFG` | jw. |
| ISO eksperymentalne | `BursztynOS-BRB-eksperymentalny.iso` | — (nigdy `BursztynOS.iso` przed decyzją) |

### 2.3. Ponowne użycie obecnego ESP zainstalowanego systemu

**[FAKT]** ESP ma 512 MiB, klastry 4 KiB, zajęte jest kilkanaście MiB, a pliki są ciągłe. **[WNIOSEK]** BRB
może czytać istniejące `\BOOT\JADRO_A.BIN`/`\BOOT\JADRO_B.BIN` i nie wymaga przebudowy ESP. Dodatkowy
katalog `\EFI\BURSZTYN\` (BRB, konfiguracja, kopia GRUB na czas przejścia) mieści się w wolnym miejscu.
Zapis wymaga sterownika FAT32 z alokacją klastrów albo użycia `EFI_FILE_PROTOCOL.Write` — integracja w
`integracja-instalatora.md`.

---

## 3. Protokoły UEFI używane przez BRB

| Protokół / usługa | Rozdział UEFI 2.10 | Użycie | Alternatywa przy braku |
|---|---|---|---|
| `EFI_SYSTEM_TABLE`, `EFI_BOOT_SERVICES` | 4.3, 4.4 | wejście, usługi | — (brak = firmware niezgodny) |
| `EFI_LOADED_IMAGE_PROTOCOL` | 9.1 | `DeviceHandle` nośnika BRB, `FilePath`, `ImageBase/ImageSize` (wykluczenie obrazu BRB z celów kopiowania) | — (wymagany) |
| `EFI_DEVICE_PATH_PROTOCOL` | 10 | identyfikacja nośnika: węzły `HardDrive` (GUID partycji GPT), `CDROM`, `USB` | rekord źródła niewypełniony |
| `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` + `EFI_FILE_PROTOCOL` | 13.4, 13.5 | odczyt plików z FAT (ESP, `efi.img` ISO) | `EFI_BLOCK_IO` + własny czytnik FAT/ISO9660 (Faza 6) |
| `EFI_BLOCK_IO_PROTOCOL` / `EFI_DISK_IO_PROTOCOL` | 13.9 / 13.7 | odczyt ISO9660 z dysku nadrzędnego (Live), surowe LBA 2046/2047 | brak funkcji zależnych |
| `EFI_GRAPHICS_OUTPUT_PROTOCOL` | 12.9 | `QueryMode`, `SetMode`, `Mode->FrameBufferBase/Size`, `PixelFormat`, `PixelsPerScanLine` | start bez grafiki niemożliwy dla obecnego jądra (Z-05) → komunikat |
| `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` | 12.4 | komunikaty tekstowe przed GOP i przy błędach | port szeregowy |
| `EFI_SIMPLE_TEXT_INPUT_PROTOCOL` (`…_EX` opcjonalnie) | 12.3 | menu (Faza 11), klawisze | brak menu → wpis domyślny |
| `EFI_SERIAL_IO_PROTOCOL` | 12.8 | opcjonalny log; domyślnie BRB pisze bezpośrednio do COM1 0x3F8 jak jądro | — |
| `GetMemoryMap`, `AllocatePages`, `AllocatePool`, `FreePages/FreePool` | 7.2 | pamięć | — |
| `ExitBootServices` | 7.4 | koniec usług | — |
| `Stall`, `SetWatchdogTimer`, `CreateEvent/SetTimer/WaitForEvent` | 7.5, 7.1 | limit czasu menu; wyłączenie watchdoga menedżera rozruchu | — |
| `GetVariable`/`SetVariable` | 8.2 | **nie w v1** (stan A/B jest na dysku, nie w NVRAM) | — |
| `ResetSystem` | 8.5.1 | menu: „Uruchom ponownie”, „Wyłącz komputer” | brak pozycji w menu |
| Tabela konfiguracji `EFI_ACPI_20_TABLE_GUID`, `ACPI_TABLE_GUID`, `SMBIOS(3)_TABLE_GUID` | 4.6 | RSDP, SMBIOS | rekord pominięty |

BRB **nie** używa: sieci (PXE, HTTP Boot), `LoadImage/StartImage` dla jądra (jądro nie jest PE),
`SetVirtualAddressMap`, protokołów Secure Boot innych niż odczyt zmiennej `SecureBoot` w celach diagnostycznych
(`bezpieczenstwo.md`).

---

## 4. Lokalizacja nośnika i konfiguracji

```
efi_main(ImageHandle, SystemTable)
  ├─ SetWatchdogTimer(0, …)                        – wyłączenie watchdoga ustawionego przez menedżera rozruchu (opis SetWatchdogTimer, rozdz. 7.5)
  ├─ HandleProtocol(ImageHandle, LoadedImage)      → DeviceHandle (partycja/obraz FAT, z którego uruchomiono BRB)
  ├─ HandleProtocol(DeviceHandle, SimpleFileSystem) → wolumin BRB
  ├─ Open "\EFI\BURSZTYN\BRB.CFG"                  – brak/uszkodzony → konfiguracja wbudowana (wpis domyślny)
  ├─ ustalenie rodzaju nośnika (DevicePath):
  │     CDROM node        → Live z ISO (rodzaj 1)
  │     HardDrive + GPT   → odczyt LBA 2046/2047 dysku nadrzędnego (BlockIO) → zainstalowany (rodzaj 3),
  │                         gdy rekord stanu poprawny; inaczej nośnik wymienny/obraz hybrydowy (rodzaj 2)
  └─ ścieżki plików zawsze względem tego samego woluminu (brak wyszukiwania po innych dyskach)
```

**Uzasadnienie braku wyszukiwania po wszystkich dyskach** **[PROJEKT]**: obecny GRUB robi
`search --file /EFI/BOOT/BOOTX64.EFI`, co może trafić na **inny** dysk z takim samym plikiem. BRB ładuje
pliki wyłącznie z woluminu, z którego sam został uruchomiony. Eliminuje to pomyłkę dysku przy kilku
instalacjach albo podłączonym nośniku instalacyjnym.

---

## 5. Ładowanie jądra ELF64

**[FAKT]** Jądro jest ELF64 `ET_EXEC` z 4 segmentami `PT_LOAD` o stałych adresach fizycznych od 1 MiB
(`audyt-rozruchu.md`, sekcja 2). BRB ładuje je jako ELF64. Formatu PE dla jądra nie przewiduje się.

### 5.1. Walidacja (kolejność, każde niepowodzenie = odmowa z komunikatem)

| # | Kontrola | Specyfikacja |
|---|---|---|
| 1 | rozmiar pliku ≥ 64 i ≤ 64 MiB (limit BRB); plik czytany w całości do bufora `EfiLoaderData` | — |
| 2 | `e_ident`: `\x7FELF`, `ELFCLASS64`, `ELFDATA2LSB`, `EV_CURRENT`; OS/ABI ∈ {0 (SYSV), 3 (GNU)} | System V gABI, rozdz. 4 *ELF Header* |
| 3 | `e_type == ET_EXEC`, `e_machine == EM_X86_64 (62)`, `e_version == 1` | gABI |
| 4 | `e_ehsize == 64`, `e_phentsize == 56`, `1 ≤ e_phnum ≤ 16`, `e_phoff + e_phnum·56 ≤ rozmiar` (bez przepełnienia) | gABI |
| 5 | dla każdego `PT_LOAD`: `p_filesz ≤ p_memsz`, `p_offset + p_filesz ≤ rozmiar`, `p_paddr + p_memsz` bez przepełnienia, `p_paddr ≥ 1 MiB`, `p_paddr + p_memsz ≤ 4 GiB`, `p_align` ∈ {0, 1, potęga 2}, `(p_offset − p_paddr) % 4096 == 0` przy `p_align = 4096` | gABI rozdz. 5 *Program Header* |
| 6 | segmenty `PT_LOAD` są posortowane rosnąco po `p_paddr` i nie nakładają się | gABI (kolejność) + wymaganie BRB |
| 7 | brak `PT_INTERP`, `PT_DYNAMIC`, `PT_TLS` (jądro statyczne); `PT_GNU_STACK`/`PT_NOTE` ignorowane | — |
| 8 | żaden segment nie jest jednocześnie W i X (zgodnie z polityką `linker.ld:91–97`); naruszenie → odmowa | wymaganie BRB |
| 9 | nagłówek MB2 w pierwszych 32 KiB z poprawną sumą (tryb zgodności) albo `brb_naglowek_jadra` (natywny) | MB2 §3.1 |
| 10 | adres wejścia (`e_entry` albo `wejscie64`) leży w segmencie wykonywalnym | `linker.ld:465–468` |
| 11 | zakres `[min p_paddr, max p_paddr+p_memsz)` mieści się w RAM możliwym do użycia po `ExitBootServices` (sekcja 6.2) | wymaganie BRB |
| 12 | w systemie zainstalowanym: SHA-256 pliku jądra = `sha256_jadro_{a,b}` ze stanu LBA, gdy pole niezerowe | `aktualizacja_rozruch.h:88–89` |

### 5.2. Plan ładowania

Segmenty są ładowane pod stałe adresy fizyczne 1 MiB…`__kernel_end`. Na rzeczywistym sprzęcie UEFI ten zakres
**może być zajęty** przez firmware jako `EfiBootServicesData/Code` (usługi rozruchowe). Taka pamięć jest
wolna dopiero po `ExitBootServices`. Dlatego BRB stosuje model **przygotuj teraz, skopiuj po EBS**:

1. Próba `AllocatePages(AllocateAddress, EfiLoaderData, …)` dla zakresu jądra. Sukces → kopiowanie
   segmentów od razu (ścieżka szybka, typowa dla OVMF).
2. Porażka → bufor przejściowy `AllocatePages(AllocateMaxAddress < 4 GiB, EfiLoaderData)`, segmenty składane
   w buforze (z wyzerowanym BSS). Plan przeniesienia `{źródło, cel, długość}` zapisany w strukturze trampoliny.
3. Warunek ścieżki 2: każdy fragment docelowego zakresu w ostatecznej mapie pamięci jest typu
   `EfiConventionalMemory`, `EfiBootServicesCode/Data` albo `EfiLoaderCode/Data` należącego do BRB. Inny typ
   (Runtime, ACPI, MMIO, Reserved) → odmowa: „Pamięć wymagana przez jądro jest zajęta przez firmware.”
4. Bufory trampoliny, MBI/BRBI, stos, GDT i tablice stron BRB są alokowane **poza** zakresem jądra
   (`AllocateMaxAddress` z wykluczeniem kolizji) i poza obrazem BRB (`ImageBase … ImageBase+ImageSize`).

BSS (`p_memsz − p_filesz`) jest **zawsze zerowane przez BRB** (D-11).

---

## 6. Alokacja pamięci i zapobieganie nakładaniu

### 6.1. Rejestr zakresów BRB

Wszystkie obszary przekazywane jądru BRB zapisuje w stałej tablicy `brb_zakres[256]` (bez alokacji
dynamicznej). Każdy wpis ma rodzaj: JĄDRO, MODUŁ(n), INFORMACJE, STOS, GDT, TABLICE_STRON, TRAMPOLINA,
FRAMEBUFFER. Przy dodawaniu wpisu BRB sprawdza przepełnienie `poczatek + dlugosc` i rozłączność z każdym
istniejącym wpisem (O(n²) przy n ≤ 256 — akceptowalne). Rejestr jest źródłem wpisów typu 0x100–0x103 w mapie.

### 6.2. Typy pamięci UEFI a cel kopiowania po EBS

| Typ UEFI (UEFI 2.10, rozdz. 7.2, tabela *Memory Type Usage after ExitBootServices*) | Może być celem jądra/modułów po EBS |
|---|---|
| `EfiConventionalMemory`, `EfiBootServicesCode`, `EfiBootServicesData`, `EfiLoaderCode`, `EfiLoaderData` | tak |
| `EfiRuntimeServicesCode/Data`, `EfiACPIReclaimMemory`, `EfiACPIMemoryNVS`, `EfiReservedMemoryType`, `EfiUnusableMemory`, `EfiMemoryMappedIO*`, `EfiPalCode`, `EfiPersistentMemory`, `EfiUnacceptedMemoryType`, nieznane | nie |

### 6.3. Tablice stron i GDT

**[FAKT]** Tablice stron firmware leżą w pamięci usług rozruchowych, którą po EBS wolno nadpisać, i nie
gwarantują identity map całego zakresu < 4 GiB. **[PROJEKT]** Przed EBS BRB:

- alokuje własne PML4/PDPT/4 × PD (identity 0..4 GiB, strony 2 MiB — identycznie jak `boot.S`) w
  `EfiLoaderData` poza celami kopiowania,
- alokuje własną GDT (null, 0x08 kod64, 0x10 dane, 0x18 kod32, 0x20 dane32) i stos 64 KiB.

Po EBS, przed kopiowaniem czegokolwiek, BRB przełącza `CR3`, GDT i `RSP` na te struktury.

---

## 7. GOP — wybór trybu graficznego

1. `LocateHandleBuffer(ByProtocol, GOP)`. Pierwszeństwo ma uchwyt, który ma też `EFI_DEVICE_PATH` urządzenia
   PCI (konsola rzeczywista). Wiele uchwytów → pierwszy pasujący.
2. Dopuszczalne tryby: `PixelBlueGreenRedReserved8BitPerColor` (w pamięci B,G,R,X = XRGB8888 w słowie
   32-bit LE — zgodny z Z-05) albo `PixelBitMask` z maskami R=0x00FF0000, G=0x0000FF00, B=0x000000FF.
   `PixelRedGreenBlueReserved8BitPerColor` (XBGR8888) jest **niezgodny z obecnym jądrem** — pomijany do czasu
   obsługi w jądrze. `PixelBltOnly` (brak LFB) — niedostępny.
   **Kierunek docelowy (korekta Fazy 2):** część sprzętu oferuje wyłącznie XBGR8888. BRB i jądro powinny docelowo
   obsługiwać **oba** formaty 32-bitowe (XRGB8888 i XBGR8888) — rekord framebuffera BRBI ma już pole `format`
   i maski kanałów (`protokol-rozruchowy.md` §4.6). Wymaga to zmiany w jądrze (`grafika.cpp`, Z-05) i jest
   zaplanowane razem z protokołem natywnym (Faza 7); do tego czasu XBGR8888 = komunikat BRB-E040.
3. Wybór rozdzielczości:
   - wartość z konfiguracji (`grafika = 1280x720`) albo z parametru `bursztyn.video=`;
   - w przeciwnym razie 1280×720, jak obecny `gfxmode` (`Makefile:1315`);
   - w przeciwnym razie największy tryb ≤ 1920×1080;
   - w przeciwnym razie tryb bieżący, jeśli zgodny.
4. `SetMode` tylko wtedy, gdy wybrany tryb ≠ bieżący. Po `SetMode` ekran jest czyszczony na czarno (bez migania
   — jądro i tak rysuje ekran powitalny).
5. `pitch = PixelsPerScanLine × 4`; `wymagane = pitch × VerticalResolution` (kontrola przepełnienia),
   `PixelsPerScanLine ≥ HorizontalResolution`, szerokość i wysokość ≥ 64. Jeżeli `wymagane > FrameBufferSize`,
   geometria jest **niespójna i tryb jest odrzucany** (następny kandydat albo BRB-E040). Rozmiar przekazywany
   jądru to `FrameBufferSize` zgłoszone przez firmware — **nigdy** nie jest sztucznie powiększany do
   wartości wyliczonej (korekta Fazy 2: wcześniejszy zapis `max(FrameBufferSize, pitch × wysokość)` mógłby
   skłonić jądro do mapowania i zapisu pamięci poza rzeczywistym buforem ramki).
   Adres `FrameBufferBase` może legalnie leżeć **powyżej 4 GiB** (patrz `protokol-rozruchowy.md` §1) —
   jedynym warunkiem jest brak przepełnienia `FrameBufferBase + FrameBufferSize`.
6. Brak zgodnego trybu → komunikat tekstowy (ConOut + COM1):
   „Nie znaleziono trybu graficznego zgodnego z Bursztyn OS (wymagany 32-bitowy XRGB). Uruchomienie nie jest
   możliwe.” Po nim powrót do menu albo `ResetSystem` po naciśnięciu klawisza.

---

## 8. ACPI i SMBIOS z tabeli konfiguracji

- Przeszukanie `SystemTable->ConfigurationTable[0..NumberOfTableEntries)` (limit 256 wpisów).
- Pierwszeństwo `EFI_ACPI_20_TABLE_GUID` (8868E871-E4F1-11D3-BC22-0080C73C8881), potem `ACPI_TABLE_GUID`
  (EB9D2D30-2D88-11D3-9A16-0090273FC14D).
- Walidacja RSDP: sygnatura `"RSD PTR "`, suma 20 B, a dla rewizji ≥ 2 suma po `Length` (≤ 36 przy kopiowaniu,
  `Length` ≥ 36). Kopia trafia do rekordu 4 / tagu 14/15. Jądro dalej waliduje RSDT/XSDT samo (`acpi.cpp`).
- SMBIOS: `SMBIOS3_TABLE_GUID` (F2FD1544-9794-4A2C-992E-E5BBCF20E394), potem `SMBIOS_TABLE_GUID`
  (EB9D2D31-2D88-11D3-9A16-0090273FC14D). W v1 przekazywany jest tylko adres (rekord 8, opcjonalny).

---

## 9. Mapa pamięci i `ExitBootServices`

Zgodnie z UEFI 2.10, rozdz. 7.4: `ExitBootServices(ImageHandle, MapKey)` wymaga klucza aktualnej mapy.
Przy `EFI_INVALID_PARAMETER` loader musi ponownie pobrać mapę i ponowić wywołanie, a między nimi **nie wolno**
wywoływać usług, które mogą zmienić mapę.

### 9.1. Algorytm

```
przygotuj WSZYSTKO, co wymaga alokacji: jądro, moduły, bufor MBI/BRBI (rozmiar maksymalny), tablice stron,
GDT, stos, trampolinę, bufor mapy pamięci = (rozmiar zgłoszony + 16 deskryptorów) * DescriptorSize, ×2
zamknij pliki, zwolnij bufory tymczasowe (FreePool) – zmiany mapy dozwolone tylko do tego momentu

for proba in 1..4:                                  // ograniczona liczba prób
    rozmiar = pojemnosc_bufora
    s = GetMemoryMap(&rozmiar, bufor, &MapKey, &DescSize, &DescVer)
    if s == EFI_BUFFER_TOO_SMALL: → błąd krytyczny (bufor był przewymiarowany; brak AllocatePool w pętli)
    if s != EFI_SUCCESS:          → błąd krytyczny
    if DescSize < sizeof(EFI_MEMORY_DESCRIPTOR) or DescVer != 1 or rozmiar % DescSize != 0: → błąd
    // ŻADNYCH wywołań usług (w tym ConOut, Stall, AllocatePool) między GetMemoryMap a ExitBootServices
    s = ExitBootServices(ImageHandle, MapKey)
    if s == EFI_SUCCESS: break
    if s != EFI_INVALID_PARAMETER: → błąd krytyczny
koniec pętli bez sukcesu → błąd krytyczny „Nie udało się zakończyć usług rozruchowych UEFI.”
```

Uwagi (korekta Fazy 2 — wcześniejszy zapis dopuszczał ConOut po nieudanych próbach, co jest niezgodne
ze specyfikacją):

- UEFI 2.10, rozdz. 7.4 (opis `ExitBootServices`): firmware **może częściowo wyłączyć usługi rozruchowe już
  przy pierwszym wywołaniu**, także zakończonym błędem. Po pierwszym wywołaniu `ExitBootServices` loader nie
  powinien wywoływać żadnych usług rozruchowych poza usługami alokacji pamięci (w praktyce: wyłącznie
  `GetMemoryMap`, potrzebne do ponowienia). Dlatego **od pierwszej próby** BRB nie używa ConOut, ConIn,
  `Stall`, zdarzeń, `AllocatePool`/`FreePool` ani protokołów.
- Wszystko, co może być potrzebne po pierwszej próbie (bufory, komunikat błędu, struktury przekazania), jest
  przygotowane przed nią. Bufor mapy jest przewymiarowany, więc pętla nie potrzebuje alokacji.
- Strategia porażki: po 4 nieudanych próbach albo przy nieoczekiwanym statusie BRB **nie wraca do firmware**
  (stan usług rozruchowych jest nieokreślony). Wypisuje `[BRB] BLAD KRYTYCZNY ExitBootServices proby=<n>
  status=<hex>` bezpośrednio na COM1 (port, bez usług), a następnie wywołuje usługę **czasu wykonania**
  `ResetSystem(EfiResetCold)` (Runtime Services pozostają dostępne po EBS). Jeżeli i to zawiedzie: `cli; hlt`
  w pętli. Komunikat dla użytkownika na ekranie wymaga własnego renderera GOP (późniejsza faza) — do tego
  czasu ekran nie jest aktualizowany po pierwszej próbie.
- Po sukcesie EBS BRB używa wyłącznie COM1 (`outb`) i nie dotyka `SystemTable` poza polami skopiowanymi
  wcześniej.
- Po `ExitBootServices` BRB normalizuje mapę z kopii pobranej tuż przed EBS (ostatnia udana `GetMemoryMap`).
  Normalizacja jest czystą funkcją bez alokacji, testowaną na hoście (`plan-testow.md`, T-MAP).
- Liczba prób trafia do rekordu diagnostycznego.

### 9.2. Normalizacja mapy

1. Deskryptory przekształcane do `{poczatek = PhysicalStart, dlugosc = NumberOfPages × 4096, typ}`. Kontrola
   przepełnienia `NumberOfPages × 4096` i `PhysicalStart + dlugosc`.
2. Odrzucenie deskryptorów niewyrównanych do 4 KiB (licznik w diagnostyce).
3. Sortowanie (sortowanie przez wstawianie, ≤ 4096 wpisów) i rozstrzyganie nakładań wg priorytetu typów
   (`protokol-rozruchowy.md`, 6.1).
4. „Wycięcie” zakresów z rejestru BRB (jądro, moduły, informacje, obszar odzyskiwalny) i nadanie im typów
   0x100–0x103 albo typu 2 w trybie zgodności MB2.
5. Scalenie sąsiadów tego samego typu.
6. Kontrola, że rejestr BRB w całości leży w pamięci dopuszczalnej według tabeli z sekcji 6.2.

Rozmiar wyniku jest ograniczony z góry: każde wycięcie dodaje ≤ 2 wpisy, więc ≤ n + 2·256. Bufor wyniku
alokowany przed EBS.

---

## 10. Przejście do jądra (trampolina)

Trampolina to kod asemblerowy BRB umieszczony w osobnej stronie `EfiLoaderData` poniżej 4 GiB, poza
zakresami jądra i modułów. Wymaga identity map, bo w trybie zgodności wyłącza stronicowanie.

### 10.1. Wspólna część (po EBS)

```
cli
lgdt  gdt_brb                     ; własna GDT
mov   cr3, pml4_brb               ; własne identity 0..4 GiB
mov   rsp, stos_brb_szczyt        ; RSP % 16 == 0
dalekie przeładowanie CS = 0x08, DS/ES/SS = 0x10
dla każdego wpisu planu przeniesienia: memmove(cel, zrodlo, dlugosc)   ; kopiowanie bez użycia SSE
wyzeruj ewentualne pozostałości BSS (gdy BSS nie mieściło się w buforze)
oblicz CRC32 BRBI (natywny) – opcjonalnie ponowna kontrola SHA-256 jądra w pamięci docelowej (diagnostyka)
COM1: "[BRB] przekazanie"
```

### 10.2. Tryb natywny (BRBI)

```
mov edi, 0x31425242 ; RDI = magic
mov rsi, brbi_fiz
xor edx, edx ; pozostałe GPR = 0
push 0x2 ; popfq → RFLAGS = 0x2
jmp  wejscie64
```

### 10.3. Tryb zgodności MB2 (wejście 32-bit do `_start`)

Zgodnie z Intel SDM tom 3A, rozdział o inicjalizacji i wyłączaniu trybu IA-32e (*Initializing IA-32e Mode*,
*IA-32e Mode Deactivation*):

```
ljmp  0x18:tryb_zgodnosci32       ; segment kodu 32-bit (L=0, D=1) – compatibility mode
.code32
tryb_zgodnosci32:
mov   ax, 0x20 ; ds, es, fs, gs, ss = 0x20 (dane 32-bit)
mov   eax, cr0 ; and eax, ~0x80000000 ; mov cr0, eax     ; PG = 0 (wykonywane z identity map)
mov   ecx, 0xC0000080 ; rdmsr ; and eax, ~0x100 ; wrmsr  ; EFER.LME = 0
mov   eax, cr4 ; and eax, ~0x20 ; mov cr4, eax           ; PAE = 0 (porządek)
mov   esp, stos_brb32
mov   eax, 0x36D76289
mov   ebx, mbi_fiz
jmp   e_entry                     ; _start w jądrze (boot.S:215)
```

### 10.4. Różnice konwencji wywołań

- Kod BRB wywołujący usługi UEFI używa konwencji Microsoft x64 (UEFI 2.10, rozdz. 2.3.4):
  argumenty w RCX, RDX, R8, R9, 32 B *shadow space*, stos wyrównany do 16 B przed `call`, rejestry
  nieulotne RBX, RBP, RDI, RSI, R12–R15, XMM6–XMM15.
- Kod wewnętrzny BRB kompilowany GCC ma konwencję System V. Wszystkie typy wskaźników funkcji UEFI są więc
  oznaczone `__attribute__((ms_abi))`, a `efi_main` jest zadeklarowane jako `ms_abi`. Nie miesza się kodu
  `-mabi=ms` z kodem SysV w jednej funkcji.
- Firmware może używać SSE: BRB kompilowany z `-mgeneral-regs-only` nie używa rejestrów XMM, więc nie musi ich
  zachowywać (kompilator nie wygeneruje ich użycia).
- Przekazanie do jądra nie jest wywołaniem funkcji. Stan rejestrów jest w całości zdefiniowany w
  `protokol-rozruchowy.md`, sekcja 2.

---

## 11. Obsługa błędów

| Sytuacja | Zachowanie | Komunikat (PL) |
|---|---|---|
| brak `LoadedImage`/`SimpleFileSystem` | powrót do firmware (`return EFI_LOAD_ERROR`) po 10 s | „Nie można odczytać nośnika, z którego uruchomiono program rozruchowy.” |
| brak/uszkodzona konfiguracja | wbudowany wpis domyślny + ostrzeżenie | „Plik konfiguracji BRB jest uszkodzony — używam ustawień domyślnych.” |
| brak pliku jądra | menu/następny wpis; w systemie A/B: drugi slot zgodnie z polityką | „Nie znaleziono pliku jądra: <ścieżka>.” |
| niepoprawny ELF / niezgodna architektura | jw. | „Plik jądra jest uszkodzony lub nie jest obrazem x86-64.” |
| niezgodność SHA-256 slotu | traktowane jak nieudana próba slotu | „Suma kontrolna jądra slotu B jest niezgodna — powrót do slotu A.” |
| brak pamięci (`AllocatePages`) | odmowa | „Za mało pamięci, aby załadować system.” |
| CPU bez Long Mode/PAE/NX (sprawdzane CPUID przed EBS) | odmowa | „Procesor nie spełnia wymagań Bursztyn OS (x86-64 z PAE i NX).” |
| moduł uszkodzony / za duży / zbyt wiele modułów | wpis Live: odmowa; moduł opcjonalny (galeria, muzyka): pominięcie + ostrzeżenie | „Moduł <etykieta> jest uszkodzony.” |
| niepowodzenie `ExitBootServices` po 4 próbach | brak powrotu do firmware (usługi mogą być częściowo wyłączone już po 1. próbie, §9.1): COM1 + `ResetSystem` (Runtime Services), w ostateczności `cli; hlt` | COM1: `[BRB] BLAD KRYTYCZNY ExitBootServices …` (ekran: dopiero z własnym rendererem GOP) |
| awaria po EBS (np. błąd kontroli przed skokiem) | COM1 + `cli; hlt` (brak usług) | `"[BRB] BLAD KRYTYCZNY <kod>"` (ASCII na COM1) |

Wszystkie komunikaty dla użytkownika są po polsku. ConOut UEFI przyjmuje UCS-2, więc BRB konwertuje teksty
UTF-8 na UCS-2. Polskie znaki (ą, ć, ę, ł, ń, ó, ś, ź, ż) mieszczą się w BMP. Ich wyświetlenie zależy od fontu
firmware. **[DO POTWIERDZENIA]** Część firmware nie ma glifów spoza ASCII, a braku glifów nie da się
niezawodnie wykryć. Dlatego konfiguracja ma opcję `tekst_ascii = tak` (transliteracja bez polskich znaków)
dla trybu tekstowego. Menu graficzne (Faza 11) rysuje własnym fontem i tego problemu nie ma.

---

## 12. Budowanie aplikacji UEFI — metoda zweryfikowana w Fazie 2

**[FAKT]** Dostępne: GCC 13.3 (`x86_64-linux-gnu-`), GNU ld 2.42 z emulacją `i386pep`, objcopy z
`pei-x86-64`. Niedostępne: clang/lld-link, MinGW. Nie instalowano żadnych pakietów.

### 12.1. Wynik badania metod (Faza 2, 2026-11-27)

| Wariant | Polecenie | Wynik | Werdykt |
|---|---|---|---|
| A + `--gc-sections` (+ `-ffunction-sections`) | `ld -m i386pep … --gc-sections` na obiektach ELF | błędy `undefined reference` dla symboli **globalnych** zdefiniowanych w tych samych obiektach (np. `memmove → brb_memmove` w jednym pliku, odwołanie funkcji do samej siebie). Odtworzone na jednym obiekcie: bez `--gc-sections` link poprawny, z `--gc-sections` błąd, niezależnie od skryptu linkera. Wniosek: odśmiecanie sekcji w emulacji PE nie śledzi poprawnie odwołań z relokacji ELF i usuwa sekcje z definicjami | **odrzucony** — BRB nie używa `--gc-sections` (ani `-ffunction-sections`, bo bez GC nie ma korzyści) |
| A2: `objcopy -O pe-x86-64` każdego obiektu ELF, potem `ld -m i386pep` | konwersja + link | link „udany”, ale objcopy zamienił 154 relokacje `R_X86_64_PC32` na `IMAGE_REL_AMD64_ADDR32` (bezwzględne). Deasemblacja: `lea 0x10001a90(%rip)` — adres bezwzględny użyty jako przesunięcie RIP, cel 0x20003569 zamiast 0x10001A90. **Cicho błędny kod** | **odrzucony** — przykład, że udany link nie dowodzi poprawności |
| A z domyślnym skryptem `i386pep` | `ld -m i386pep` bez `-T` | kod poprawny, ale sekcje ELF `.rodata*`/`.data.rel.ro*` nie są mapowane: osierocone sekcje z obciętymi nazwami (`.rodata.`, `.data.re`), pusty katalog importów (`.idata`), `.comment` „poniżej bazy obrazu” | odrzucony (jakość obrazu) |
| **A z własnym skryptem** `rozruch/brb/uefi/brb_uefi.ld` | `ld -m i386pep -T brb_uefi.ld …` bez `--gc-sections` | 3 sekcje (`.text` R-X, `.rdata` R, `.reloc` R), brak importów, poprawne adresy RIP (zweryfikowane deasemblacją), rzeczywisty wpis `DIR64`, uruchomienie w OVMF pod adresem innym niż `ImageBase` | **przyjęty** |

Metoda B (samorelokacja + `objcopy --target=efi-app-x86_64`) nie była potrzebna i nie została zaimplementowana.

### 12.2. Konfiguracja (cel `make brb-uefi`)

Kompilacja (`BRB_CXXFLAGS` w `Makefile`):

```
-std=c++17 -O2 -g0 -nostdinc -isystem <katalog naglowkow GCC>
-ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-strict-aliasing
-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
-fno-stack-protector -fno-stack-clash-protection -fcf-protection=none
-mno-red-zone -mgeneral-regs-only -mcmodel=small -fpie
-Wall -Wextra -Wpedantic -Werror -Wvla -Wshadow
```

Uzasadnienia: `-nostdinc` + tylko nagłówki GCC (`stdint.h`, `stddef.h` w wersji freestanding) — żadnych
nagłówków glibc; `-fpie` — adresowanie względem RIP, więc jedyne relokacje bezwzględne to 64-bitowe
wskaźniki w danych (`R_X86_64_64` → `DIR64`), nigdy 32-bitowe; `-mno-red-zone` — firmware może używać stosu
pod RSP w przerwaniach; `-mgeneral-regs-only` — brak SSE/x87, więc XMM6–XMM15 (nieulotne w ms_abi) nie są
nigdy modyfikowane; `-fno-stack-protector`, `-fcf-protection=none`, `-fno-stack-clash-protection` — wyłączają
domyślne w Ubuntu dodatki wymagające `__stack_chk_*`/sekcji `.note.gnu.property`; `-fno-tree-loop-distribute-patterns`
— optymalizator nie zamienia pętli na wywołania `memcpy`/`memset` (ochrona przed rekurencją w `pamiec_podstawowa.cpp`).

Linkowanie (`BRB_LDFLAGS`):

```
x86_64-linux-gnu-ld -m i386pep --oformat pei-x86-64 -T rozruch/brb/uefi/brb_uefi.ld -nostdlib
  --subsystem 10 -e efi_main --image-base 0x10000000
  --file-alignment 0x200 --section-alignment 0x1000
  --dynamicbase --nxcompat --enable-reloc-section --no-insert-timestamp [--strip-all]
```

Powstają dwa obrazy: `brb-x64.debug.efi` (z tablicą symboli COFF, do analizy) i `brb-x64.efi`
(`--strip-all`, artefakt). Przed linkowaniem `narzedzia/brb/sprawdz_obiekty_brb.py` sprawdza obiekty:

- brak symboli nierozwiązanych,
- wyłącznie relokacje `R_X86_64_PC32`, `R_X86_64_PLT32` i `R_X86_64_64`,
- brak sekcji `.init_array`, `.ctors`, TLS i unwind,
- funkcje `brb_mem*` bez żadnych wywołań; wrappery `memcpy` i podobne przekazują sterowanie tylko do
  odpowiadającego `brb_*`.

Skrypt zapisuje też liczbę `R_X86_64_64`, którą walidator PE porównuje z liczbą wpisów `DIR64`.

### 12.3. Cechy wygenerowanego obrazu (stan na koniec Fazy 2)

| Cecha | Wartość |
|---|---|
| Plik | `.brb-build/uefi/brb-x64.efi`, 12 800 B, SHA-256 `89030cba156bba156e9fba54edcd63ed15c20a466e4628e888a43e06c7c188dd` (kompilacja deterministyczna: `--no-insert-timestamp`, `-fno-ident`) |
| Nagłówki | PE32+ (`Magic 0x20B`), `Machine 0x8664`, `Subsystem 10` (EFI application), `Characteristics 0x22E` (bez `RELOCS_STRIPPED`), `DllCharacteristics 0x160` (HIGH_ENTROPY_VA, DYNAMIC_BASE, NX_COMPAT) |
| Wyrównanie | `SectionAlignment 0x1000`, `FileAlignment 0x200`, `SizeOfHeaders 0x200`, `SizeOfImage 0x6000` |
| Punkt wejścia | `AddressOfEntryPoint 0x1020` = `efi_main` (RVA z obrazu z symbolami) |
| Sekcje | `.text` VA 0x1000, 0x202E B, R-X; `.rdata` VA 0x4000, 0xB18 B, R; `.reloc` VA 0x5000, 12 B, R |
| Relokacje | 1 blok (strona 0x4000), 1 wpis `DIR64` @ RVA 0x4B10 (`WSKAZNIK_RELOKACJI`) + 1 wpis wypełniający `ABSOLUTE`; zgodne z 1 relokacją `R_X86_64_64` w obiektach |
| Importy / eksporty / TLS / zasoby | brak (katalogi danych zerowe) |
| Zależności czasu wykonania | wyłącznie firmware UEFI (tablica systemowa) i port COM1; brak libc, libstdc++, libgcc, `__stack_chk_*`, `__cxa_*`, `_Unwind_*` |

### 12.4. Funkcje wspierające kompilator

`memcpy`, `memmove`, `memset` i `memcmp` dostarcza `rozruch/brb/wspolne/pamiec_podstawowa.cpp` (kod
własny, licencja projektu). GCC może je generować nawet z `-ffreestanding`, np. przy kopiowaniu i zerowaniu
struktur. Implementacje są bajtowe, `memmove` obsługuje nakładanie w obu kierunkach. Nie linkujemy `libgcc`:
BRB nie używa dzielenia 128-bit ani operacji zmiennoprzecinkowych. Dzielenie 64-bit jest instrukcją x86-64.

---

## 13. Kamienie milowe na porcie szeregowym (UEFI)

Wypisywane na COM1 (0x3F8; prędkość ustawiona przez firmware albo 38400 8N1 jak jądro, gdy port jest
nieskonfigurowany), w ASCII, z prefiksem `[BRB]`. Faza 2 implementuje wyłącznie U1 i kamienie pomocnicze
(sekcja 14.5). U2–U7 są zarezerwowane dla kolejnych faz:

| Kod | Tekst | Moment |
|---|---|---|
| U1 | `[BRB] etap 1: wejscie UEFI` | początek `efi_main` |
| U2 | `[BRB] etap 2: nosnik i konfiguracja` | po otwarciu woluminu i konfiguracji |
| U3 | `[BRB] etap 3: jadro zaladowane i zweryfikowane` | po walidacji ELF/SHA |
| U4 | `[BRB] etap 4: moduly n=<liczba>` | po modułach |
| U5 | `[BRB] etap 5: grafika <szer>x<wys>` | po GOP |
| U6 | `[BRB] etap 6: ExitBootServices proby=<n>` | po EBS |
| U7 | `[BRB] etap 7: przekazanie <MB2/BRBI> wejscie=<hex>` | tuż przed skokiem |

Kolejnym znakiem życia jest log jądra (`BootTiming::entry`, linie COM1 jądra).

---

## 14. Implementacja Fazy 2 (BRB 0.1.0, wersja rozwojowa)

### 14.1. Pliki

| Plik | Rola |
|---|---|
| `rozruch/brb/uefi/uefi_typy.h` | minimalne deklaracje UEFI pisane ze specyfikacji: typy, `EFI_STATUS`, GUID-y, `EFI_TABLE_HEADER`, `EFI_SYSTEM_TABLE`, **kompletna** `EFI_BOOT_SERVICES` (44 wskaźniki), protokoły tekstowe, `EFI_LOADED_IMAGE_PROTOCOL`; `static_assert` rozmiarów i przesunięć |
| `rozruch/brb/uefi/efi_main.cpp` | punkt wejścia `extern "C" EFI_STATUS EFIAPI efi_main(...)`; jedyny plik z instrukcjami `in`/`out` (nie jest kompilowany w testach hosta) |
| `rozruch/brb/uefi/program.h/.cpp` | logika testowalna na hoście: walidacja firmware, kontrola obrazu i relokacji, ekran, klawiatura, statusy |
| `rozruch/brb/uefi/brb_uefi.ld` | skrypt linkera PE32+ |
| `rozruch/brb/wspolne/pamiec_podstawowa.h/.cpp` | `brb_mem*` + eksport `memcpy`/`memmove`/`memset`/`memcmp` |
| `rozruch/brb/wspolne/utf8.h/.cpp` | ograniczona konwersja UTF-8 → UCS-2 i transliteracja ASCII |
| `rozruch/brb/wspolne/log_szeregowy.h/.cpp` | log COM1 przez interfejs `PortyWeWy` |
| `rozruch/brb/wspolne/wersja.h` | `0.1.0` (rozwojowa) |
| `testy/test_brb_host.cpp`, `testy/brb_pe_walidator.cpp` | testy hosta i niezależny walidator PE |
| `narzedzia/brb/sprawdz_obiekty_brb.py`, `narzedzia/brb/uruchom_ovmf.py` | kontrola obiektów, sesja QEMU/OVMF |

`rozruch/protokol/` pozostaje zarezerwowany dla przyszłego ABI BRBI (Faza 7) — w Fazie 2 celowo pusty.

### 14.2. ABI firmware i jego weryfikacja

- **Konwencja wywołań**: wszystkie typy wskaźników funkcji firmware i `efi_main` mają `EFIAPI` =
  `__attribute__((ms_abi))`. Reszta kodu jest w System V. GCC generuje na każdej granicy:
  - argumenty w RCX, RDX, R8 i R9, a piąty i dalsze na stosie za 32 B shadow space;
  - zachowanie RSI i RDI (nieulotne w ms_abi, ulotne w SysV);
  - wyrównanie stosu do 16 B przed `CALL`.
- **Weryfikacja statyczna** (deasemblacja `brb-x64.debug.efi`):
  - `efi_main` przenosi RCX i RDX do rejestrów nieulotnych;
  - zachowuje RBX, RBP, RDI, RSI i R12 (`push`), wyrównuje stos (5×`push` + `sub $0x40` przy wejściu
    RSP ≡ 8 mod 16).
  - Wywołanie `CreateEvent` (`call *0x50(%r15)`, przesunięcie 0x50 zgodne z `static_assert`):
    - argumenty w rejestrach: RCX=`EVT_TIMER`, EDX=`TPL_CALLBACK`, R8=0;
    - piąty argument przez `push`;
    - przed `call`: `sub $0x20,%rsp` (shadow space).
- **Weryfikacja dynamiczna** (`test_brb_host.cpp`):
  - Atrapy firmware są prawdziwymi funkcjami ms_abi. Przy wejściu sprawdzają wyrównanie stosu: RBP ≡ 0 mod 16
    przy `-fno-omit-frame-pointer`.
  - Atrapy zapisują wzorzec w 32 B shadow space wywołującego. Brak rezerwacji zniszczyłby dane
    wywołującego.
  - `CreateEvent` sprawdza wszystkie 5 argumentów, w tym argument przekazany na stosie.
  - Wstawka asemblerowa wywołuje odpowiednik `efi_main` w konwencji ms_abi z wartościami kontrolnymi w RBX,
    RSI, RDI i R12–R15, a po powrocie sprawdza ich zachowanie.
  - **Test mutacyjny** (scratchpad, poza repozytorium) dowodzi, że testy wykrywają błędy:
    - usunięcie `EFIAPI` z jednego typu dało błędy testów (zniszczony shadow space);
    - zmiana wejścia na SysV dała SEGV.
- **Układ struktur**: `static_assert` dla wszystkich kluczowych przesunięć, wyliczonych ręcznie z UEFI 2.10.
  Na przykład `BootServices` w tablicy systemowej pod 0x60, `ExitBootServices` 0xE8, `Stall` 0xF8,
  `CreateEventEx` 0x170, `sizeof(EFI_BOOT_SERVICES)` = 376. Natywnie firmware OVMF zgłosił
  `HeaderSize` tablicy usług = 376 B, zgodnie z deklaracją.

### 14.3. Wymagania minimalne wobec firmware (walidacja przed dereferencją)

Kolejność kontroli w `waliduj_firmware`:

1. `ImageHandle ≠ NULL`, `SystemTable ≠ NULL` (`EFI_INVALID_PARAMETER`).
2. Sygnatura `IBI SYST`, `HeaderSize ≥ 120`, rewizja ≥ 2.0 (`EFI_INCOMPATIBLE_VERSION`).
3. `ConOut` i `ConOut->OutputString` (`EFI_UNSUPPORTED`).
4. `BootServices`: sygnatura `BOOTSERV`, `HeaderSize ≥ 0x100` — koniec ostatniego używanego pola (`Stall`).
   Używane wskaźniki muszą być niezerowe: `CreateEvent`, `SetTimer`, `WaitForEvent`, `CloseEvent`,
   `HandleProtocol`, `Stall`.

`ConIn` nie jest wymagane: przy braku BRB pokazuje komunikat, czeka ograniczony czas (`Stall`) i zwraca
`EFI_UNSUPPORTED`. Następnie `HandleProtocol(LoadedImage)` i `sprawdz_obraz`:

- `Revision ≥ 0x1000`;
- `SystemTable` zgodne z otrzymanym;
- adres kodu i danych BRB wewnątrz `[ImageBase, ImageBase + ImageSize)`, z kontrolą przepełnienia;
- wskaźnik bezwzględny w `.rdata` (wymaga zastosowania relokacji `DIR64`) równy adresowi wyliczonemu
  względem RIP.

Niespójność daje `EFI_LOAD_ERROR`.

### 14.4. Polskie znaki na konsoli firmware (decyzja oparta na teście natywnym)

**[FAKT, OVMF 2024.02-2ubuntu0.10]** Test natywny pokazał, że konsola graficzna OVMF **pomija** litery spoza
Latin-1. Na ekranie było „zakoczona”, „Nacinij”, „ukadowego”, podczas gdy „ó” wyświetlało się poprawnie.
Jednocześnie `TestString` zwracał `EFI_SUCCESS` dla każdej polskiej litery, a nawet dla U+4E2D.
`OutputString` nie zwrócił ani razu `EFI_WARN_UNKNOWN_GLYPH`. Specyfikacja (12.4) przewiduje `EFI_UNSUPPORTED`
lub to ostrzeżenie, ale firmware z nich nie korzysta. Wykrycie obsługi glifów jest więc **niewiarygodne**.

**Polityka**: `USTAWIENIA_DOMYSLNE.polskie_znaki_konsoli = false`. Teksty UTF-8 są konwertowane do UCS-2 i
transliterowane do ASCII (ą→a, ł→l…). Wynik jest kompletny i czytelny na każdej konsoli UEFI. Opcja
`polskie_znaki_konsoli = true` wypisuje pełne UCS-2, o ile `TestString` nie zaprzecza. Jest przeznaczona dla
firmware o zweryfikowanej czcionce i przetestowana na hoście. Pełne polskie glify wymagają własnego
renderowania czcionki na GOP (menu graficzne, późniejsza faza). Log COM1 jest zawsze ASCII.

Polityka UTF-8 (`utf8.h`):
- dekodowanie ścisłe wg RFC 3629;
- każda błędna sekwencja daje jeden `?` (bez „zjadania” następnego znaku);
- znaki spoza BMP i sterujące (poza `\n`, `\t`) dają `?`;
- `\n` → CR LF;
- wynik zawsze kończy się NUL, obcinanie następuje na granicy całego znaku.

### 14.5. Log COM1, klawiatura i statusy powrotu

- **Log szeregowy:**
  - wykrycie obecności UART: LSR ≠ 0xFF i test rejestru SCR z przywróceniem wartości;
  - prędkość ustawiona przez firmware pozostaje bez zmian; inicjalizacja 38400 8N1 (jak `serial_inicjalizuj`
    jądra) tylko przy dzielniku 0 albo formacie innym niż 8 bitów;
  - każdy bajt czeka na THRE najwyżej 100 000 odczytów LSR; po przekroczeniu port jest wyłączany do końca
    działania, bez blokowania programu.
- **Kamienie milowe:**
  - `[BRB] etap 1: wejscie UEFI`, `[BRB] wersja …`;
  - `[BRB] kontrola firmware: OK rewizja=… tablica=… uslugi=… B` albo `… BLAD - <opis>`;
  - `[BRB] obraz: baza=… rozmiar=… relokacja=OK|BLAD`;
  - `[BRB] konsola: …`;
  - `[BRB] klawiatura: oczekiwanie|klawisz odebrany|limit czasu…`;
  - `[BRB] blad kontrolowany: <opis> status=…`;
  - `[BRB] zakonczenie: powrot do firmware status=…`.
- **Klawiatura:**
  - przed oczekiwaniem bufor klawiatury jest opróżniany (najwyżej 32 odczyty);
  - `CreateEvent(EVT_TIMER)` + `SetTimer(TimerRelative, 60 s)` + `WaitForEvent({WaitForKey, zegar})`, bez
    aktywnego odpytywania CPU;
  - najwyżej 16 pobudzeń bez klawisza;
  - zegar jest zawsze anulowany i zamykany.
  - Awaryjnie, gdy zdarzenia zawiodą: `Stall(100 ms)` + `ReadKeyStroke`, ograniczone do tego samego limitu.
- **Statusy powrotu:**
  - klawisz → `EFI_SUCCESS`;
  - limit czasu → `EFI_TIMEOUT`;
  - brak `ConIn` → `EFI_UNSUPPORTED`;
  - błąd konsoli lub klawiatury → status zwrócony przez firmware;
  - błędy walidacji → patrz 14.3.
  - Natywnie: po `EFI_SUCCESS` menedżer rozruchu OVMF uruchomił UiApp. Po `EFI_TIMEOUT` zgłosił
    `failed to start … Time out` i przeszedł do następnej pozycji (EFI Shell). To zachowanie zgodne z
    przeznaczeniem statusu błędu; w kolejnych fazach status zależy od wyniku ładowania jądra.

---

## 15. Procedura testu natywnego (QEMU + OVMF)

Uruchamiać wyłącznie za zgodą właściciela, jeden proces QEMU naraz:

```
make -j1 brb-uefi brb-uefi-wolumin          # wolumin: .brb-build/wolumin-testowy/EFI/BOOT/BOOTX64.EFI
python3 narzedzia/brb/uruchom_ovmf.py --scenariusz klawisz   # zrzut ekranu, Enter, oczekiwany EFI_SUCCESS
python3 narzedzia/brb/uruchom_ovmf.py --scenariusz limit     # bez klawisza, oczekiwany EFI_TIMEOUT po 60 s
```

Skrypt sprawdza, że wolumin zawiera **wyłącznie** `EFI/BOOT/BOOTX64.EFI`, i uruchamia QEMU poleceniem:

```
qemu-system-x86_64 -machine q35 -m 256 -smp 1 -nodefaults -no-user-config
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd
  -drive if=pflash,format=raw,unit=1,file=.brb-build/qemu/<scenariusz>/OVMF_VARS_4M.kopia.fd
  -drive if=none,id=wolumin,format=raw,readonly=on,file=fat:<pełna ścieżka>/.brb-build/wolumin-testowy
  -device virtio-blk-pci,drive=wolumin
  -vga std -display none -serial file:.brb-build/qemu/<scenariusz>/com1.log -net none
  -qmp unix:.brb-build/qemu/<scenariusz>/qmp.sock,server=on,wait=off
```

Zabezpieczenia:
- wolumin vvfat jest tylko do odczytu; `ide-hd` odrzuca urządzenie tylko do odczytu, dlatego użyto `virtio-blk`;
- zmienne NVRAM są w kopii `OVMF_VARS`;
- brak sieci i brak dysków Bursztyn OS.

Wyniki trafiają do `.brb-build/qemu/<scenariusz>/`: `com1.log`, `ekran-oczekiwanie.png`, `ekran-koniec.png`,
`polecenie.txt`.

Kryteria akceptacji:
1. W logu jest `BdsDxe: starting Boot0001 "UEFI Misc Device"`, czyli firmware załadował PE32+.
2. W logu COM1 jest komplet kamieni milowych Fazy 2.
3. `relokacja=OK` przy bazie innej niż `0x10000000`.
4. Zrzut ekranu zawiera 7 linii tekstu powitalnego (transliteracja ASCII) oraz informację o limicie czasu.
5. Po Enter: `klawisz odebrany` i `EFI_SUCCESS`; bez klawisza: `EFI_TIMEOUT` po 60 s.
6. Po powrocie firmware uruchamia kolejną pozycję menedżera rozruchu.

Wyniki Fazy 2 (2026-11-27): wszystkie kryteria spełnione. BRB był ładowany pod `0x0DFFB000`, a w
pierwszej wersji, przed zmianą polityki znaków, pod `0x0DFFC000`.

---

## 16. Ograniczenia znane po Fazie 2

1. Test natywny wykonano tylko na OVMF 2024.02 (QEMU 8.2.2, TCG). Brak testów na sprzęcie fizycznym i
   innych implementacjach firmware.
2. Polskie litery na konsoli firmware są transliterowane (§14.4). Pełne glify wymagają renderera GOP.
3. Gdy firmware nie ustawi COM1, log szeregowy ustawia 38400 8N1. Na sprzęcie, gdzie firmware używa COM1
   jako konsoli z inną prędkością, BRB jej nie zmienia — logi mają wtedy prędkość firmware.
4. Brak `--gc-sections` (§12.1): martwy kod nie jest usuwany. Przy obecnym rozmiarze (12,5 KiB) jest to
   bez znaczenia.
5. Wartość `ImageBase` (0x10000000) jest umowna. Poprawność relokacji potwierdzono symulacją przeniesienia
   o 0x7FFF0000 i natywnie (firmware wybrał inny adres). Obraz ma dziś jedną relokację `DIR64`. Każda nowa
   tablica wskaźników zwiększy ich liczbę, co walidator porównuje z obiektami.
6. Secure Boot: obraz nie jest podpisany i nie uruchomi się przy włączonym Secure Boot (`bezpieczenstwo.md` §5.2).
7. BRB nie ładuje jądra, nie czyta plików i nie kończy usług rozruchowych. To zakres Faz 3–4.
   *(Stan po Fazie 3: BRB czyta i waliduje plik jądra oraz buduje plan ładowania — sekcja 17. Nadal nie kopiuje
   segmentów pod adresy docelowe, nie kończy usług rozruchowych i nie uruchamia jądra.)*

---

## 17. Faza 3 — dostęp do plików UEFI, walidacja ELF64 i plan ładowania (BRB 0.1.0)

Status: **zaimplementowane i zweryfikowane** na hoście (ASan/UBSan) i natywnie w OVMF (2026-11-27).
Granice Fazy 3:
- BRB **nie** kopiuje segmentów pod adresy docelowe, **nie** alokuje stron docelowych jądra, **nie** zmienia
  CR3 ani tablic stron firmware;
- **nie** wywołuje `ExitBootServices` i **nie** przekazuje sterowania do jądra;
- jedyną pamięcią alokowaną dla jądra jest **tymczasowy** bufor pliku (`AllocateAnyPages`, `EfiLoaderData`),
  zwalniany przed powrotem do firmware.

### 17.1. Architektura

```
efi_main → uruchom (program.cpp)
  ├─ walidacja firmware, LoadedImage, kontrola relokacji (Faza 2)
  ├─ ekran_naglowek
  ├─ sprawdz_jadro
  │    ├─ wczytaj_plik_jadra (uefi/plik_jadra.cpp)         – zależne od UEFI
  │    │    LoadedImage->DeviceHandle → HandleProtocol(SFS) → OpenVolume → Open(\boot\system_operacyjny.bin, READ)
  │    │    → GetInfo(EFI_FILE_INFO) → walidacja metadanych → AllocatePages → Read (porcje ≤ 1 MiB) → sonda końca
  │    │    → Close(plik), Close(korzeń) na każdej ścieżce; FreePages przy błędzie
  │    ├─ zbuduj_plan_ladowania (wspolne/elf64.cpp)        – niezależne od firmware, testowane na hoście
  │    ├─ zwolnij_bufor_jadra                              – zawsze, przed dalszym działaniem
  │    └─ log planu (COM1) + 3 linie na ekranie
  ├─ ekran_stopka, oczekiwanie na klawisz (Faza 2)
  └─ powrót: błąd jądra (jeśli wystąpił) > EFI_SUCCESS/EFI_TIMEOUT z Fazy 2
```

Podział odpowiedzialności:
- `wspolne/elf64.*` nie zna UEFI. Dostaje bufor i politykę, zwraca kod błędu albo `PlanLadowania`.
- `wspolne/arytmetyka.h` zawiera arytmetykę z kontrolą przepełnienia oraz odczyt LE bajt po bajcie (bez
  rzutowania na struktury).
- `uefi/plik_jadra.*` używa wyłącznie protokołów UEFI.

### 17.2. Protokoły i usługi firmware (UEFI 2.10)

| Element | Rozdział | Użycie / walidacja |
|---|---|---|
| `EFI_LOADED_IMAGE_PROTOCOL.DeviceHandle` | 9.1 | urządzenie, z którego firmware załadował BRB; `NULL` → BRB-E201 |
| `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` (GUID 964E5B22-6459-11D2-8E39-00A0C969723B) | 13.4 | `HandleProtocol` **tylko** na `DeviceHandle`; `Revision ≥ 0x10000`, `OpenVolume ≠ NULL` |
| `EFI_FILE_PROTOCOL` | 13.5 | korzeń i plik: `Revision ≥ 0x10000`, `Open/Close/Read/GetInfo ≠ NULL`. `Open` z `EFI_FILE_MODE_READ` i `Attributes = 0`. Brak `Write/Delete/SetInfo/Flush` (pola zadeklarowane jako nieużywane) |
| `EFI_FILE_INFO` (GUID 09576E92-6D3F-11D2-8E39-00A0C969723B) | 13.5.16 | bufor 512 B wyrównany do 8; pola czytane przez `memcpy`; walidacja w 17.4 |
| `AllocatePages` / `FreePages` | 7.2 | `AllocateAnyPages`, `EfiLoaderData`, ⌈rozmiar/4096⌉ stron; zwalniane z tą samą liczbą stron |

Układ struktur sprawdzają `static_assert` w `uefi_typy.h` (przesunięcia wyliczone ręcznie ze specyfikacji):
- `sizeof(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL)` = 16;
- `EFI_FILE_PROTOCOL`: `Read` = 0x20, `GetInfo` = 0x40, `Flush` = 0x50, rozmiar rewizji 2 = 0x78;
- `EFI_FILE_INFO`: `FileSize` = 8, `Attribute` = 72, `FileName` = 80;
- `EFI_TIME` = 16 B;
- `BS.FreePages` = 0x30.

Wszystkie typy wskaźników funkcji mają `EFIAPI` (ms_abi).

**Protokół, który nie przeszedł walidacji, nie jest wywoływany wcale — także `Close`.** Jego wskaźniki funkcji
są niezaufane; wyciek uchwytu firmware jest bezpieczniejszy niż skok pod niezweryfikowany adres.

### 17.3. Ścieżka jądra i limity zasobów

| Limit | Wartość | Uzasadnienie |
|---|---|---|
| Ścieżka | `\boot\system_operacyjny.bin` na woluminie BRB | układ nośnika Live (`specyfikacja-techniczna.md` §6.2, `audyt-rozruchu.md` §8). Sloty zainstalowanego systemu (`\BOOT\JADRO_A.BIN`/`JADRO_B.BIN`) — Faza 8. Brak przeszukiwania innych urządzeń |
| Rozmiar pliku | 1 B … 64 MiB | ten sam limit, który instalator stosuje do `installer.kernel` (`jadro/gui/instalator.cpp:345`); jądro produkcyjne: 8,9 MiB (okrojone), 13,7 MiB (prywatny ELF z symbolami) |
| `EFI_FILE_INFO` | ≤ 512 B | stała część 80 B + krótka nazwa pliku; większe żądanie = BRB-E206 |
| Porcja `Read` | ≤ 1 MiB | odczyt jądra produkcyjnego: 9 + 1 wywołań (sonda) |
| Liczba wywołań `Read` | ≤ rozmiar/4096 + 64 | firmware może skracać porcje (np. na granicach klastrów); porcje po 1 B = BRB-E214 |
| Naglówki programu | ≤ 16 (`e_phnum`) | `architektura-uefi.md` §5.1; jądro ma 4 |
| Segmenty `PT_LOAD` | ≤ 16 | j.w. |
| Pamięć stron jądra | ≤ 15 MiB (1 MiB … 16 MiB) | wynika z polityki zakresu (17.5) |

### 17.4. Walidacja pliku (przed alokacją pamięci)

1. **`GetInfo`:**
   - status błędu albo `EFI_BUFFER_TOO_SMALL` (żądanie > 512 B) → E206;
   - zwrócony rozmiar ∈ [82, 512] B;
   - pole `Size` ∈ [82, zwrócony rozmiar];
   - nazwa zakończona `NUL` w granicach `Size`;
   - naruszenie któregokolwiek z tych warunków → E207.
2. **Rodzaj i rozmiar pliku:** atrybut `EFI_FILE_DIRECTORY` → E208; `FileSize = 0` → E209; `FileSize > 64 MiB` → E210.
   Alokacja następuje dopiero po tych kontrolach.
3. **`Read`:**
   - status błędu → E212;
   - firmware zgłasza więcej bajtów niż bufor → E215;
   - 0 B przed końcem → E213 (nieoczekiwany koniec pliku, brak postępu);
   - przekroczony limit wywołań → E214.
4. **Sonda końca:** po `FileSize` bajtach kolejny `Read` 1 B musi zwrócić 0 B. Inaczej E216: plik dłuższy niż
   metadane, czyli niespójny albo złośliwy rozmiar.

### 17.5. Polityka ELF64 (`POLITYKA_JADRA_BURSZTYN`)

Źródła: System V gABI (rozdz. 4 *ELF Header*, 5 *Program Header*, *Program Loading*), System V AMD64 psABI
(`EM_X86_64 = 62`), Multiboot2 Specification 2.0, §3.1.

**Odrzucane z precyzyjnym kodem (kody 1nn w 17.7):**
- *Nagłówek ELF:*
  - plik krótszy niż 64 B;
  - plik większy niż 64 MiB;
  - zła sygnatura;
  - inna klasa niż `ELFCLASS64` albo kolejność bajtów inna niż little-endian;
  - wersja ≠ 1;
  - OS/ABI spoza {SYSV, GNU} albo `EI_ABIVERSION ≠ 0`;
  - `e_machine ≠ EM_X86_64`;
  - `e_type ≠ ET_EXEC`; jądro produkcyjne to `ET_EXEC`, więc `ET_DYN`, `ET_REL` i obrazy relokowalne są
    nieobsługiwane;
  - `e_ehsize ≠ 64` albo `e_phentsize ≠ 56`;
  - `e_phnum` równe 0, `PN_XNUM` (0xFFFF) albo > 16.
- *Tablica nagłówków programu* (`e_phoff`): przepełnienie `e_phnum × 56`, tablica poza plikiem, nachodzenie na
  nagłówek ELF albo przesunięcie niewyrównane do 8 B.
- *Typy segmentów:* `PT_DYNAMIC`, `PT_INTERP`, `PT_SHLIB` i `PT_TLS` są odrzucane — brak dynamicznego
  linkowania, relokacji czasu wykonania i TLS. Pozostałe typy inne niż `PT_LOAD` (`PT_NOTE`, `PT_GNU_STACK`,
  `PT_PHDR`, …) są ignorowane.
- *Rozmiary i położenie w pliku* (`PT_LOAD`):
  - `p_memsz = 0` — segment niejednoznaczny;
  - `p_filesz > p_memsz`;
  - `p_offset + p_filesz` poza plikiem albo z przepełnieniem.
- *Wyrównanie i kongruencja:*
  - `p_align` ∈ {0, 1} jest dozwolone bez dodatkowych wymagań (gABI);
  - inne wartości muszą być potęgą dwójki i spełniać `p_vaddr ≡ p_offset (mod p_align)`.
- *Arytmetyka adresów:* przepełnienie `p_vaddr + p_memsz`, `p_paddr + p_memsz` albo zaokrąglenia końca do
  strony 4 KiB.
- *Polityka zakresu i tożsamości:*
  - `p_paddr ≠ p_vaddr`, bo jądro jest konsolidowane pod adresy fizyczne (identity map w `boot.S`);
  - zakres poza [1 MiB, 16 MiB).
- *Uprawnienia:* brak `PF_R`; jednoczesne `PF_W` i `PF_X` (polityka W^X, zgodna z `linker.ld:91–97`).
- *Kolejność i kolizje:*
  - `PT_LOAD` nieposortowane rosnąco wg `p_vaddr` (wymóg gABI);
  - wspólne bajty dwóch segmentów — fizycznie albo wirtualnie;
  - wspólna strona 4 KiB dla segmentów o **różnych** prawach.
  - Wspólna strona przy **identycznych** prawach jest dozwolona, bo strona ma jedne prawa. Pamięć liczona jest
    wtedy jako suma zbiorów stron, bez powtórzeń. Jądro produkcyjne nie współdzieli stron.
- *Liczba segmentów:* brak `PT_LOAD`; suma stron większa niż 15 MiB.
- *Punkt wejścia:*
  - poza każdym `PT_LOAD`;
  - w segmencie bez `PF_X`;
  - w części zerowanej (BSS);
  - fizyczny adres wejścia ≥ 4 GiB, bo `_start` to kod 32-bitowy (`boot.S`).
- *Multiboot2 (kontrakt trybu zgodności, `protokol-rozruchowy.md` §6):*
  - nagłówek w pierwszych 32 768 B pliku, wyrównany do 8 B;
  - suma kontrolna poprawna, architektura 0 (i386);
  - długość ≥ 24, wielokrotność 8, mieści się w oknie;
  - tagi wyrównane do 8 B, każdy ≥ 8 B, najwyżej 64, zakończone tagiem końca (typ 0, 8 B) dokładnie na końcu
    nagłówka;
  - brak magic w oknie → E135, inny błąd → E136.

**Nagłówki sekcji i nazwy sekcji nie są używane.** Plan dla okrojonego jądra (7 sekcji) i prywatnego ELF
(18 sekcji) jest identyczny.

Wszystkie liczby czytane są bajtowo (`le16/le32/le64`), więc wyrównanie bufora nie ma znaczenia. Każda suma,
iloczyn i zaokrąglenie przechodzi przez `dodaj_u64`/`mnoz_u64`/`wyrownaj_w_gore` (`__builtin_*_overflow`, bez
libgcc). Adresy są zawsze 64-bitowe: polityka ogólna (`POLITYKA_OGOLNA`, używana w testach) akceptuje adresy
powyżej 4 GiB i jądra higher-half, a test sprawdza brak obcięcia do 32 bitów.

**Korekta względem Fazy 1 (z uzasadnieniem):**
- **Koniec zakresu:** §5.1 dopuszczał koniec zakresu jądra do 4 GiB. Polityka Fazy 3 zawęża go do **16 MiB**
  (`RING3_BASE`, `jadro/mm/pamiec.h:76`): powyżej zaczyna się przestrzeń wirtualna Ring 3, a jądro działa w
  identity map, więc jego adresy wirtualne są fizycznymi.
- **Tożsamość adresów:** wymóg `p_paddr == p_vaddr` był w Fazie 1 faktem o jądrze; teraz jest jawną regułą
  polityki.
- **Polityka zależna od jądra:** obie granice są parametrami `PolitykaElf`. Przyszły nagłówek BRBI w jądrze
  (Faza 7) może zadeklarować zakres sam.

### 17.6. Format planu ładowania (`PlanLadowania`, `wspolne/elf64.h`)

Dla każdego segmentu (`SegmentPlanu`):
- `indeks_ph`;
- `prawa` (R/W/X);
- `przesuniecie_pliku` — źródło w pliku;
- `adres_fizyczny` (`p_paddr`) — cel kopiowania w Fazie 4;
- `adres_wirtualny` (`p_vaddr`) — informacyjnie; adres fizyczny i wirtualny są rozróżniane jawnie;
- `bajty_z_pliku`, `rozmiar_w_pamieci`, `bajty_zerowane` (BSS);
- `wyrownanie`;
- `strona_poczatek`, `strona_koniec` (wyłącznie), `liczba_stron`.

Dla całego planu:
- `wejscie_wirtualne`, `wejscie_fizyczne` i `segment_wejscia`;
- `zakres_poczatek` i `zakres_koniec`;
- `pamiec_stron` — suma stron bez powtórzeń;
- sumy `bajty_z_pliku` i `bajty_zerowane`;
- `rozmiar_pliku`;
- opis nagłówka Multiboot2 (przesunięcie, długość, architektura, liczba tagów, segment zawierający).

Plan to **opis przyszłych operacji**: struktura na stosie, kopia danych z nagłówków, bez wskaźników do bufora
pliku. Może więc przeżyć zwolnienie bufora.

**Plan zweryfikowanego jądra produkcyjnego** (SHA-256 `303cb82e…a758d2`, identyczne z jądrem wydania):

| Seg | Plik | Fizyczny = wirtualny | Z pliku | W pamięci | BSS | Prawa | Strony |
|---|---|---|---|---|---|---|---|
| 0 (`.boot`) | 0x1000 | 0x100000 | 0x50 | 0x50 | 0 | R-- | 0x100000–0x101000 (1) |
| 1 (`.text`) | 0x2000 | 0x101000 | 0x129D70 | 0x129D70 | 0 | R-X | 0x101000–0x22B000 (298) |
| 2 (`.rodata`) | 0x12C000 | 0x22B000 | 0x7E030 | 0x7E030 | 0 | R-- | 0x22B000–0x2AA000 (127) |
| 3 (`.data+.bss`) | 0x1AB000 | 0x2AA000 | 0x73DB40 | 0xAC4D10 | 0x3871D0 | RW- | 0x2AA000–0xD6F000 (2757) |

Pozostałe parametry:
- wejście 0x1193E2 (segment 1);
- zakres 0x100000–0xD6F000; pamięć stron 13 037 568 B (12 732 KiB);
- z pliku 9 328 944 B; BSS 3 699 152 B;
- Multiboot2 w pliku od 0x1000, 80 B, architektura 0, 4 tagi, w segmencie 0.

**Zgodność jądra produkcyjnego z architekturą BRB:** pełna. Plan zgadza się z `readelf` i `objdump` (T-ELF-PROD).
Nie stwierdzono niezgodności, które wymagałyby zmian w jądrze, `linker.ld` albo `boot.S`.

### 17.7. Kody błędów i etapy diagnostyczne

| Grupa | Kod | Znaczenie |
|---|---|---|
| plik (E2nn = numer `BladPliku`) | E201 brak systemu plików / `DeviceHandle`; E202 niepoprawny protokół; E203 `OpenVolume`; E204 brak pliku; E205 `Open`; E206 `GetInfo`; E207 niespójne metadane; E208 katalog; E209 pusty; E210 > 64 MiB; E211 brak pamięci; E212 błąd `Read`; E213 przedwczesny koniec; E214 zbyt wiele częściowych odczytów; E215 `Read` > bufor; E216 plik dłuższy niż metadane | status UEFI: błąd firmware przekazywany bez zmian (np. `EFI_NOT_FOUND`, `EFI_DEVICE_ERROR`, `EFI_OUT_OF_RESOURCES`); błędy zawartości → `EFI_LOAD_ERROR`; E210 → `EFI_BAD_BUFFER_SIZE` |
| ELF (E1nn = numer `BladElf`) | E101 za krótki; E102 > 64 MiB; E103 sygnatura; E104 klasa; E105 kolejność bajtów; E106 wersja; E107 ABI; E108 architektura; E109 typ; E110 `e_ehsize`; E111 `e_phentsize`; E112 `e_phnum`; E113 tablica PH; E114 segment nieobsługiwany; E115 za dużo `PT_LOAD`; E116 zerowy segment; E117 `filesz > memsz`; E118 dane poza plikiem; E119 wyrównanie; E120 kongruencja; E121 przepełnienie; E122 zakres; E123 tożsamość; E124 kolejność; E125 nakładanie; E126 konflikt strony; E127 W+X; E128 brak R; E129 brak `PT_LOAD`; E130 wejście poza segmentem; E131 wejście niewykonywalne; E132 wejście w BSS; E133 wejście ≥ 4 GiB; E134 limit pamięci; E135 brak Multiboot2; E136 zły Multiboot2 | `EFI_LOAD_ERROR`; w logu także `ph=<indeks>` |

**Korekta:** przykładowe kody z `specyfikacja-techniczna.md` §11.1 (E010–E012) zastępuje powyższy, jednoznaczny
schemat: numer grupy oraz numer wartości wyliczenia.

Etapy na COM1:
- `[BRB] etap 2: wolumin rozruchowy otwarty (urzadzenie obrazu BRB, tylko odczyt)`;
- `[BRB] plik: \boot\system_operacyjny.bin rozmiar=… B (metadane poprawne)`;
- `[BRB] plik: odczytano … B, wywolania Read=…, bufor strony=…`;
- `[BRB] plik: bufor tymczasowy zwolniony`;
- `[BRB] etap 3: ELF64 poprawny (ET_EXEC, x86-64), PT_LOAD=… wejscie=…`;
- `[BRB] plan: seg… …` (po linii na segment), `[BRB] plan: zakres_fiz=…`, `[BRB] plan: multiboot2 …`;
- `[BRB] etap 3: plan ladowania gotowy - tylko opis; jadro NIE zostalo skopiowane ani uruchomione`;
- przy błędzie: `[BRB] blad pliku: BRB-E2nn …` albo `[BRB] blad ELF: BRB-E1nn … ph=n`.

Znaczenie etapów U2/U3 z §13 jest w Fazie 3 węższe: U2 oznacza otwarcie woluminu, bez konfiguracji. U3
oznacza „zweryfikowane”, a nie „załadowane”.

Ekran (transliteracja ASCII) zawiera trzy linie o jądrze: ścieżka i rozmiar, wynik weryfikacji, a przy sukcesie
plan z zaznaczeniem „Jądro nie zostało uruchomione.”.

### 17.8. Własność i czas życia zasobów

| Zasób | Właściciel | Zwolnienie |
|---|---|---|
| uchwyt korzenia woluminu (`OpenVolume`) | `wczytaj_plik_jadra` | `Close` na każdej ścieżce po udanym, zwalidowanym otwarciu |
| uchwyt pliku (`Open`) | `wczytaj_plik_jadra` | `Close` przed powrotem z funkcji |
| bufor pliku (`AllocatePages`) | przy błędzie `wczytaj_plik_jadra`; przy sukcesie wywołujący | `FreePages` z tą samą liczbą stron; w `sprawdz_jadro` natychmiast po budowie planu |
| `PlanLadowania` | stos `sprawdz_jadro` | koniec funkcji (Faza 4 przekaże go dalej) |

Testy hosta sprawdzają po każdym scenariuszu:
- liczbę otwarć i zamknięć uchwytów;
- brak niezwolnionych przydziałów;
- zwolnienie z właściwą liczbą stron.

### 17.9. Testy i wyniki (2026-11-27)

| Test | Wynik |
|---|---|
| `test-brb-elf`: poprawne obrazy (3 segmenty, BSS, wspólna strona o tych samych prawach, `p_align` 0/1, adresy > 4 GiB, higher-half, segmenty ignorowane) i 60 przypadków błędnych z tabeli (plus 3 osobne: plik > 64 MiB, `NULL`, brak planu), każdy z oczekiwanym kodem | PASS |
| mutacje deterministyczne: każdy bajt obszaru 0…0x1060 × 6 wartości granicznych + każde obcięcie (obraz syntetyczny × 2 polityki, jądro produkcyjne); każdy zaakceptowany plan sprawdzany niezależnymi niezmiennikami | PASS (25 205 + 25 205 + 25 242 przebiegów) |
| fuzzer `brb_fuzz_elf` (200 000 iteracji, ziarno 0x42524233, korpus: syntetyczne + produkcyjne) | PASS: 0 naruszeń, 34 różne kody wyniku |
| `test-brb-host`: warstwa plików (22 scenariusze błędów + porcje 512 B + błędy ELF + protokoły niepoprawne + jądro produkcyjne przez model UEFI), sprzątanie zasobów | PASS (10 635 sprawdzeń) |
| T-ELF-PROD (`make test-brb-jadro`): BRB vs `readelf`/`objdump` | PASS — zgodność pełna |
| OVMF: poprawne jądro (klawisz → `EFI_SUCCESS`; limit → `EFI_TIMEOUT`), brak pliku (`EFI_NOT_FOUND`, E204), pusty (E209), obcięty do 1 MiB (E118), zła sygnatura (E103), zła architektura (E108), zła tablica PH (E113), nakładające się segmenty (E125) | PASS (9/9, bez wyjątków CPU, każdy z deterministyczną diagnostyką i powrotem do firmware) |

Kody nieosiągnięte w fuzzerze:
- E102: wymaga pliku > 64 MiB; sprawdzony osobnym testem jednostkowym;
- E115: nieosiągalny, bo limit `e_phnum` = limit `PT_LOAD` = 16; kontrola zostaje jako obrona w głębi;
- E133: wymaga polityki z wejściem ≥ 4 GiB w zakresie; sprawdzony osobnym testem jednostkowym.

Procedura natywna Fazy 3:

```
make -j1 brb-uefi
make brb-jadro-testowe BRB_JADRO_ZRODLO=.release-private/<sha256>/kernel.elf
python3 narzedzia/brb/przygotuj_woluminy.py .brb-build/uefi/brb-x64.efi .brb-build/jadro/system_operacyjny.bin .brb-build/qemu-woluminy
python3 narzedzia/brb/uruchom_ovmf.py --scenariusz f3-poprawne-klawisz --wolumin .brb-build/qemu-woluminy/poprawne --oczekiwany-status 0x0
python3 narzedzia/brb/uruchom_ovmf.py --scenariusz f3-brak --wolumin .brb-build/qemu-woluminy/brak --oczekiwany-status 0x800000000000000E
… (pozostałe scenariusze: --oczekiwany-status 0x8000000000000001 i --oczekiwany-tekst 'BRB-E1nn …')
```

### 17.10. Ryzyka i ograniczenia Fazy 3

1. **Bufor pliku:** jest w pamięci firmware (`EfiLoaderData`). W Fazie 4 trzeba zdecydować, czy segmenty
   kopiujemy z niego, czy czytamy plik drugi raz po zarezerwowaniu celów. Plan nie zawiera wskaźników do bufora.
2. **Zaufanie do firmware:** firmware może kłamać w danych, które przechodzą walidację formalną (np. podać inne
   bajty pliku). Integralność treści jądra zapewni dopiero porównanie SHA-256 ze stanem A/B (Faza 8) i podpisy
   (Faza 13). Faza 3 sprawdza wyłącznie strukturę.
3. **Sprawdzone środowisko:** tylko OVMF (vvfat, virtio-blk, tylko do odczytu). Inne implementacje FAT,
   nośniki USB i sprzęt fizyczny nie były sprawdzane.
4. **Bezpośredni dostęp do COM1** (porty 0x3F8–0x3FF) to istniejące ryzyko zgodności sprzętowej, bo na
   niektórych płytach pod tym adresem może być inne urządzenie. Ogranicza je wykrywanie (LSR/SCR) i limity
   odczytów.
   - **Propozycja:** opcja `port_szeregowy = tak|nie|auto` w przyszłym `BRB.CFG`
     (`specyfikacja-techniczna.md` §8), domyślnie `auto`.
   - Dalej: opcjonalne użycie `EFI_SERIAL_IO_PROTOCOL` zamiast bezpośrednich portów.
5. **Polskie litery:** pełne polskie znaki w przyszłym interfejsie graficznym wymagają małej, oryginalnej (albo
   odpowiednio licencjonowanej) czcionki bitmapowej rysowanej przez GOP. W Fazie 3 tego nie implementowano —
   konsola firmware nadal używa transliteracji ASCII (§14.4).
6. **Status powrotu:** po błędzie jądra BRB zwraca status błędu. Menedżer rozruchu OVMF przechodzi wtedy do
   kolejnej pozycji (zachowanie firmware, §14.5).

---

## 18. Faza 4A — przydział stron, ładowanie i weryfikacja obrazu jądra (BRB 0.1.0)

**Status: BLOKADA (BLOCKED) dla jądra produkcyjnego w OVMF.** Mechanizm jest zaimplementowany i natywnie
działa (jądro syntetyczne: PASS), ale firmware OVMF nie przydziela zakresu fizycznego wymaganego przez
jądro produkcyjne `303cb82e…` (§18.6). Zgodnie z poleceniem właściciela blokady nie obchodzono.

### 18.1. Zakres i zakazy

Faza 4A: migawka mapy pamięci, transakcyjny przydział `AllocatePages(AllocateAddress, EfiLoaderData)` dla
każdego zakresu stron jądra, kopiowanie `p_filesz`, zerowanie BSS i wypełnienia stron, niezależna weryfikacja
bajt po bajcie, zwolnienie wszystkich zasobów i powrót do firmware. **Nie wykonuje się:** `ExitBootServices`,
skoku do jądra, zmian CR0/CR3/CR4/EFER, tablic stron, GDT/IDT, MB2/BRBI, GOP, ACPI, relokacji, kopiowania
po EBS ani odzyskiwania pamięci firmware. Zapis do strony fizycznej jest dozwolony **wyłącznie** po udanym
`AllocatePages` tej strony przez BRB — typ „wolna” w mapie nie jest własnością.

### 18.2. Moduły

| Plik | Rola |
|---|---|
| `wspolne/mapa_pamieci.{h,cpp}` | niezależna od firmware walidacja migawki mapy: krok deskryptora z firmware (40…512 B, wielokrotność 8; OVMF: 48 B), wersja 1, rozmiar = n × krok, 1…4096 wpisów, strony wyrównane do 4 KiB, `NumberOfPages > 0`, brak przepełnień (pełne 64 bity — adresy > 4 GiB), brak nakładania (O(n²)); klasy pamięci (typy spoza 0…15 i OEM/OS → `Nieznana`, nigdy wolne); `wpisy_w_zakresie` (dowody + bajty nieopisane w mapie) |
| `uefi/mapa_uefi.{h,cpp}` | `GetMemoryMap`: zapytanie o rozmiar (musi być `EFI_BUFFER_TOO_SMALL`), bufor `AllocatePages(AnyPages)` z zapasem 16 deskryptorów (≤ 1 MiB), ≤ 4 próby przy wzroście mapy, walidacja; zwolnienie porzuca własność także przy błędzie `FreePages` (brak podwójnego zwolnienia) |
| `wspolne/ladowanie.{h,cpp}` | zakresy przydziału z planu Fazy 3 (sortowane, strony współdzielone scalane), kontrola przed zapisem, kopiowanie, weryfikacja; dostęp do pamięci fizycznej wyłącznie przez jawny tłumacz `DostepFizyczny` |
| `uefi/ladowanie_jadra.{h,cpp}` | maszyna stanów Fazy 4A, transakcja przydziału z wycofaniem, dowody z mapy, raport i kody BRB-E5nn |
| `uefi/test_konflikt.cpp` | **tylko** wariant `make brb-uefi-test-konflikt`: prawowity przydział jednej strony celu ze strażnikiem |

Parser ELF nie jest powielany: plan pochodzi z `zbuduj_plan_ladowania` (Faza 3), a przed pierwszym zapisem
ten sam parser liczy plan ponownie z tych samych bajtów bufora i porównuje go **pole po polu**. (Porównanie
`memcmp` całej struktury okazało się błędne — bajty dopełnienia po `bool` w `NaglowekMb2` mają nieokreśloną
zawartość; wykryte testem hosta na jądrze produkcyjnym i naprawione, z testem regresyjnym.) Adresy i rozmiary
nie są zakodowane na stałe — wynikają wyłącznie z nagłówków programu.

### 18.3. Bufor źródłowy — własność i czas życia

Bufor pliku jądra z Fazy 3 (`EfiLoaderData`, `AllocateAnyPages`) jest **jedynym źródłem** — plik nie jest
czytany ponownie. Właścicielem jest `sprawdz_jadro()` (`program.cpp`); bufor żyje od odczytu do końca Fazy 4A
i jest zwalniany na każdej ścieżce (sukces, błąd ELF, błąd 4A, błąd konsoli). Faza 4A tylko czyta bufor.
Przed przydziałem sprawdzane jest, że bufor nie nachodzi na żaden zakres celu (BRB-E504) ani obraz BRB
na zakres celu (BRB-E503). W OVMF bufor leżał pod `0xD703000` (2281 stron), poza zakresem jądra.

### 18.4. Maszyna stanów i transakcja

`WejscieZweryfikowane → ZakresyWyznaczone → MapaPobrana → StronyPrzydzielone → SegmentySkopiowane →
ObrazZweryfikowany → ZasobyZwolnione → PowrotDoFirmware`.

1. Zakresy z planu (jeden na segment; strona wspólna dwóch segmentów → jeden zakres, przydzielany raz).
2. Kontrola konfliktów z obrazem BRB i buforem źródła (bez wywołań firmware).
3. Migawka mapy (dowody: deskryptory pokrywające każdy zakres); bufor migawki zwalniany od razu.
4. `AllocateAddress` po kolei. Firmware zwracające inny adres niż żądany: przydział **jest** własnością BRB,
   zostaje zwolniony, wynik BRB-E507. Odmowa: wycofanie w odwrotnej kolejności, druga migawka z dowodami
   dla zakresu, który zawiódł, wynik BRB-E506.
5. Zerowanie wszystkich posiadanych stron, kopiowanie `p_filesz`, jawne zerowanie BSS.
6. Weryfikacja niezależna: dane = bajty źródła, BSS = 0, wypełnienie stron = 0; przy błędzie adres i segment.
   **Nie stosuje się CRC zamiast porównania bajtów.** Prawa `PF_R/W/X` są tylko zadeklarowane w logu —
   BRB nie zmienia tablic stron, więc **W^X nie jest egzekwowane** (Faza 4A tego nie deklaruje).
7. Zwolnienie wszystkich stron jądra na każdej ścieżce. Błąd `FreePages` jest liczony, zakres porzucany
   (bez ponownego zwalniania), wynik BRB-E510, jeśli wcześniej nie było innego błędu. Błędy sprzątania są
   zawsze w logu i raporcie (`bledy_sprzatania`).

### 18.5. Kody BRB-E5nn

| Kod | Znaczenie | Status UEFI |
|---|---|---|
| E501 | brak usług przydziału stron | `EFI_UNSUPPORTED` |
| E502 | plan przydziału odrzucony | `EFI_LOAD_ERROR` |
| E503 | pamięć jądra nachodzi na obraz BRB | `EFI_LOAD_ERROR` |
| E504 | pamięć jądra nachodzi na bufor pliku jądra | `EFI_LOAD_ERROR` |
| E505 | mapa pamięci niedostępna/niepoprawna (szczegół tekstowy w logu) | status firmware albo `EFI_LOAD_ERROR` |
| E506 | pamięć wymagana przez jądro jest zajęta | status `AllocatePages` (OVMF: `EFI_NOT_FOUND`) |
| E507 | firmware przydzieliło inny adres niż żądany | `EFI_LOAD_ERROR` |
| E508 | ładowanie odrzucone przez kontrole (z podkodem BRB-E3nn) | `EFI_LOAD_ERROR` |
| E509 | weryfikacja obrazu nieudana (adres i segment w logu) | `EFI_LOAD_ERROR` |
| E510 | zwolnienie pamięci nieudane | status `FreePages` |

### 18.6. Dowody blokady w OVMF (jądro `303cb82e…`)

Jądro wymaga 4 zakresów (3183 strony, `0x100000–0xD6F000`). Zakresy 0–2 (`0x100000–0x2AA000`) są
przydzielane; zakres 3 (`0x2AA000–0xD6F000`, 2757 stron — `.data` + BSS) jest odrzucany `EFI_NOT_FOUND`.
Deskryptory pokrywające zakres 3 (OVMF edk2 2024.02, QEMU 8.2.2, migawka 126 wpisów, krok 48 B):

| Konfiguracja | `0x2AA000–0x800000` | `0x800000–0x900000` | `0x900000–0xD6F000` |
|---|---|---|---|
| `q35`, 256 MiB (domyślna testów) | wolna (typ 7) | ACPI NVS (typ 10) w `0x800000–0x808000`, `0x80B000–0x80C000`, `0x810000–0x900000` | BootServicesData (typ 4) `0x900000–0x1780000` |
| `pc,hpet=on`, 2 GiB (jak cele `run*`) | wolna | ACPI NVS (jak wyżej) | BootServicesData `0x900000–0x1780000` |
| `q35`, S3 wyłączone (`ICH9-LPC.disable_s3=1`, tylko diagnostyka) | wolna | BootServicesData `0x806000`, `0x80B000`, `0x820000–…` | BootServicesData `…–0x1780000` |

Wnioski: (a) zakres jest własnością firmware przed `ExitBootServices` w każdej sprawdzonej konfiguracji;
(b) przy włączonym S3 część zakresu to ACPI NVS, którego wg §6.2 **nie wolno** nadpisać nawet po EBS, więc
model „skopiuj po EBS” z §5.2 też nie miałby tu zastosowania bez zmiany układu; (c) blokada nie jest błędem
BRB — wariant konfliktowy i jądro syntetyczne potwierdzają poprawność wykrycia i wycofania.

### 18.7. Propozycje bezpiecznego rozwiązania (do decyzji właściciela, NIE zaimplementowane)

1. **Zmiana adresu ładowania jądra** (`linker.ld`, `boot.S`, adresy fizyczne): np. przeniesienie obrazu
   powyżej obszaru używanego przez OVMF albo układ, w którym `.data/.bss` nie wchodzi w `0x800000+`.
   Wymaga zmian w jądrze i regresji GRUB — poza zakresem Fazy 4A.
2. **Jądro relokowalne / tryb MB2 `relocatable`** z przydziałem `AllocateMaxAddress`/`AnyPages` — wymaga
   projektu relokacji (obecnie zakazane).
3. **Kopiowanie po EBS** wyłącznie do zakresów, które w **ostatecznej** mapie są `Conventional`/`BootServices*`
   /`Loader*` (z wykluczeniem ACPI NVS) — wymaga formalnej analizy okna EBS (T-EBS) i osobnej zgody.
4. Do ustalenia: jak obecna ścieżka GRUB ładuje to jądro w tej samej konfiguracji OVMF (nie badano w Fazie 4A).

### 18.8. T-EBS — wymagania przyszłe (tylko dokumentacja)

Faza 4A **nie wywołuje** `ExitBootServices`. Przyszła implementacja (Faza 4C) musi: pobrać mapę tuż przed EBS
z kluczem `MapKey`; nie wywoływać między `GetMemoryMap` a EBS żadnej usługi przydzielającej, logującej przez
`ConOut` ani zdarzeń; przy `EFI_INVALID_PARAMETER` ponowić `GetMemoryMap` + EBS (≤ 4 próby) **bez**
przydziału nowego bufora (bufor z zapasem przygotowany wcześniej); po EBS używać wyłącznie COM1 i własnych
struktur; przy porażce wszystkich prób — `ResetSystem`/`cli; hlt` (zachowanie awaryjne jak w tabeli błędów po EBS). Testy: model hosta zmieniający klucz
mapy, liczący wywołania w oknie i sprawdzający brak alokacji; w OVMF — kamienie COM1 przed i po EBS.

### 18.9. Wyniki testów Fazy 4A (2026-11-27)

| Test | Wynik |
|---|---|
| `make -j1 test-brb-host` (ASan/UBSan): `test-brb-host` 10 998 sprawdzeń, `test-brb-ladowanie` 248 sprawdzeń + fuzz 20 000 iteracji + 2880 mutacji mapy, `test-brb-elf`, fuzzer ELF 200 000, T-PE, T-PE-MUT | PASS |
| `make test-brb-jadro` (T-ELF-PROD) | PASS |
| OVMF `f4a-produkcja` (jądro `303cb82e…`) | **BLOCKED** — BRB-E506 zakres 3, wycofano 3, `bledy_sprzatania=0`, `EFI_NOT_FOUND`, bez wyjątku CPU |
| OVMF `f4a-syntetyczne` (jądro fixture 0x100000–0x104000) | PASS — 3 zakresy, kopiowanie, weryfikacja bajt po bajcie, zwolnione 3/3, `EFI_SUCCESS` |
| OVMF `f4a-konflikt` (strona `0x200000` zajęta, strażnik 4096 B) | PASS — E506 zakres 1, wycofano 1, strażnik NIENARUSZONY |
| OVMF regresje Fazy 3 (7 uszkodzonych jąder) | PASS — kody E204/E209/E118/E103/E108/E113/E125 identyczne jak w Fazie 3; Faza 4A nie startuje |
| OVMF regresje Fazy 2 (`klawisz`, `limit`) | PASS — `EFI_SUCCESS`, `EFI_TIMEOUT` |

Uruchamianie: `make brb-uefi`, `make test-brb-host`, `make brb-uefi-test-konflikt [BRB_KONFLIKT_STRONA=0x…]`,
`test-brb-elf --zapisz-syntetyczne <plik>`, `przygotuj_woluminy.py <efi> <jadro> <katalog> [syntetyczne]`,
`uruchom_ovmf.py --scenariusz … --wolumin … [--maszyna pc,hpet=on --pamiec 2G] [--wylacz-s3]`.
