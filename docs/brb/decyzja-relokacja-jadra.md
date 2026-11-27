# Decyzja architektoniczna: umieszczenie jądra w pamięci fizycznej (BRB Faza 4A-R)

| | |
|---|---|
| Status | **ZAAKCEPTOWANA (ACCEPTED): opcja C + E** — decyzja właściciela z 2026-11-27 (po raporcie 4A-R). Pełna relokacja ELF w czasie pracy (opcja D) **nie** jest zaakceptowana. Każdy etap migracji wymaga osobnej zgody (§0) |
| Data | 2026-11-27 (analiza 4A-R: PROPONOWANA; akceptacja C + E: 2026-11-27) |
| Dotyczy | jądro Bursztyn OS (`jadro/arch/boot.S`, `jadro/arch/linker.ld`, `jadro/mm/*`), BRB (Faza 4), ścieżka GRUB |
| Jądro badane | `system_operacyjny.bin` SHA-256 `303cb82e8a87ec525dbc50349b67d257dde2a016c8d433dd7528ddc318a758d2` (prywatny ELF `fa8bea5aacf04bafdd728519bf155f74e83518deb0953c95f96f23d907785635`, manifest: segmenty LOAD identyczne) |
| BRB badany | `brb-x64.efi` SHA-256 `29132619227d8ad72e553c3a930b70fb7cb84ad596d7062fcab02882263e3341` |

Oznaczenia: **[OBSERWACJA]** — zmierzone bezpośrednio w tej fazie; **[WNIOSEK]** — wywnioskowane z obserwacji
i źródeł; **[NIEZNANE]** — niezmierzone.

---

## 0. Decyzja (zaakceptowana)

**Architektura docelowa:** jądro fizycznie ruchome ze **stałymi adresami wirtualnymi** (opcja C), wspierane
małym bootstrapem i jawnie zdefiniowanym kontraktem przekazania (opcja E).

Ograniczenia obowiązujące przez całą migrację:
1. Adresy wirtualne jądra pozostają stabilne.
2. Fizyczne położenie jądra staje się niezależne od zlinkowanych adresów wirtualnych.
3. Układ VA Ring 3 pozostaje bez zmian (`RING3_BASE`, sterta, SHM, stos).
4. Zachowana zostaje zgodność istniejących aplikacji BUR.
5. Pierwsza implementacja nie wymaga przetwarzania relokacji ELF w czasie pracy.
6. Migracja jądra do wyższej połowy jest odroczona.
7. KASLR jest odroczony.
8. SMP jest odroczone.
9. GRUB pozostaje dostępny w czasie kontrolowanej migracji, ale jego obecne niebezpieczne zachowanie w UEFI
   (nadpisanie ACPI NVS, §4) **nie** może być traktowane jako akceptowalne rozwiązanie trwałe.

Podział etapu R1 (§11) na dwie kontrolowane podfazy:
- **R1A** — rozdzielenie adresów fizycznych i wirtualnych, kontrakt pamięci rozruchu, fundamenty PMM i VMM
  (realizacja: `r1a-rozdzielenie-adresow.md`);
- **R1B** — przygotowanie niezależnej ścieżki wejścia 64-bit jądra i integracja z bootstrapem.

Numeracja i zakres kolejnych etapów (R2, R3, R4) oraz faz BRB (4B, 4C, 5…) pozostają bez zmian.

## 1. Problem

Jądro jest obrazem ELF64 `ET_EXEC` zlinkowanym pod stałe adresy `VMA == LMA` od 1 MiB (`linker.ld`:
`KERNEL_VMA_START = 0x00100000`, asercja „bootstrap wymaga VMA == LMA”). Obraz wymaga zakresu fizycznego
`0x100000–0xD6F000` (12,43 MiB, 3183 strony):

| Segment | Prawa | Adres fizyczny | Plik | Pamięć |
|---|---|---|---|---|
| ph0 `.boot` (nagłówek MB2) | R | `0x100000–0x101000` | 0x50 | 0x50 |
| ph1 `.text` | RX | `0x101000–0x22B000` | 0x129D70 | 0x129D70 |
| ph2 `.rodata` | R | `0x22B000–0x2AA000` | 0x7E030 | 0x7E030 |
| ph3 `.data` + `.bss` | RW | `0x2AA000–0xD6F000` | 0x73DB40 | 0xAC4D10 |

BRB w Fazie 4A przydziela te strony wyłącznie przez `AllocatePages(AllocateAddress)`. W OVMF zakres 3 jest
odrzucany (`EFI_NOT_FOUND`, BRB-E506), bo firmware jest właścicielem `0x800000–0xD6F000`. BRB nie nadpisuje
cudzej pamięci, więc jądra produkcyjnego nie da się załadować — Faza 4A pozostaje **ZABLOKOWANA**.

## 2. Zweryfikowany konflikt pamięci

Środowisko **[OBSERWACJA]**:
- QEMU 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18);
- OVMF edk2 2024.02-2ubuntu0.10: `OVMF_CODE_4M.fd` SHA-256 `ad23261d…eccb083`, `OVMF_VARS_4M.fd` `5d2ac383…be5d1e`;
- produkcyjne `runuefi` używa `-bios /usr/share/ovmf/OVMF.fd` (`7104ea75…2429186`). Ten plik jest **bajt w bajt**
  połączeniem `OVMF_VARS_4M.fd` + `OVMF_CODE_4M.fd`, więc to ten sam kod firmware.

Deskryptory firmware nad segmentami jądra są identyczne w ścieżce GRUB (`lsefimmap` przed `multiboot2`) i BRB
(migawka `GetMemoryMap`). Są też takie same we wszystkich badanych konfiguracjach z S3: q35/256 MiB, pc/2 GiB,
profil urządzeń `std` i profil produkcyjny (virtio-vga/net/rng/input, `-cpu max`).

| Zakres | S3 włączone (domyślnie w QEMU) | S3 wyłączone (tylko diagnostyka) |
|---|---|---|
| ph0–ph2 `0x100000–0x2AA000` | `EfiConventionalMemory` | `EfiConventionalMemory` |
| ph3 `0x2AA000–0x800000` | `EfiConventionalMemory` | `EfiConventionalMemory` (do `0x806000`) |
| ph3 `0x800000–0x808000`, `0x80B000–0x80C000`, `0x810000–0x900000` | **`EfiACPIMemoryNVS`** (249 stron) | `EfiBootServicesData` (`0x806000`, `0x80B000`) / wolna |
| ph3 `0x900000–0xD6F000` | `EfiBootServicesData` (`0x900000–0x1780000`) | `EfiBootServicesData` (`0x820000–0x1780000`) |

**Wnioski:**
- Urządzenia, ilość RAM i wybór programu rozruchowego **nie zmieniają** deskryptorów nad zakresem jądra.
- Zmienia je tylko konfiguracja S3: od niej zależy, czy `0x800000–0x900000` jest ACPI NVS.
- **[WNIOSEK]** Układ odpowiada regionom roboczym OVMF (tablice stron SEC, lockbox S3, obszar roboczy, PEI/DXE FV).
  W tej fazie nie weryfikowano tego w źródłach edk2.

Specyfikacja UEFI 2.10, rozdz. 7.2 (tabela *Memory Type Usage after ExitBootServices()*):
- `EfiBootServicesCode/Data` staje się dostępna dla systemu dopiero **po** `ExitBootServices()`;
- `EfiACPIMemoryNVS` musi zostać **zachowana** przez system operacyjny w stanach roboczych i ACPI S1–S3.

Specyfikacja ACPI 6.5, §15 (mapy pamięci systemu): pamięć NVS jest zarezerwowana dla firmware, a OSPM jej nie używa.

## 3. Porównanie GRUB i BRB

Oba programy uruchamiano na **identycznym sprzęcie wirtualnym**: ten sam CD z fixture GRUB i ten sam wolumin
BRB, różniące się tylko `bootindex`, oraz osobne kopie zmiennych OVMF. Fixture zbudowano produkcyjnym
`grub-mkrescue` (GRUB 2.12-1ubuntu7.3); to hybryda El Torito BIOS+UEFI, jak `BursztynOS.iso`. Zawiera:
- dokładne jądro `303cb82e…`;
- `grub.cfg` odwzorowujący wpis „Bursztyn OS”: `all_video`, `gfxmode=1280x720x32`, `gfxpayload=keep`,
  `multiboot2 … bursztyn.refresh=60 bursztyn.video=1280x720`, `module2 /boot/tapety.pak`;
- dodatkowe polecenia diagnostyczne (`echo`, `lsefimmap`, `lsmmap`) wypisywane na COM1.

ISO produkcyjne nie istnieje w drzewie ani w katalogu domowym. Pełny `make iso` przebudowałby jądro i nie
zmieścił się w budżecie dysku, więc go nie wykonano.

Wejście do jądra przechwycono sprzętowym punktem zatrzymania gdbstub QEMU na `e_entry = 0x1193E2`. Zrzucono
rejestry, MBI i pamięć — **bez zmian w jądrze**.

| Pomiar | GRUB 2.12 | BRB 0.1.0 |
|---|---|---|
| Tryb rozruchu | **UEFI** (`grub_platform=efi`, `grub_cpu=x86_64`), x86_64-efi z El Torito UEFI | UEFI |
| Mapa firmware nad jądrem | jak §2 | jak §2 (identyczne deskryptory) |
| Zakresy ph0–ph2 | przydział `ldr-data` `0x100000–0x2AA000` (426 stron) **pod adresem docelowym** | przydział `AllocateAddress` (3 zakresy, 426 stron) |
| Zakres ph3 | **bufor pośredni** `ldr-data` `0x1780000–0x2245000` (2757 stron = rozmiar ph3); z `tapety.pak` `0x1780000–0x2CBD000` (5437 = 2757 + 2680 stron modułu) | odmowa `EFI_NOT_FOUND` → wycofanie, BRB-E506 |
| Wejście do jądra | 32-bit PM, stronicowanie wyłączone (`CR0=0x10033`), `EAX=0x36D76289`, `EBX=0x2000` (MBI), tag 21 = `0x100000` | nie wykonuje jądra (zakaz) |
| Obraz w chwili wejścia | ph0–ph3 bajt w bajt zgodne z plikiem, BSS = 0 (wszystkie konfiguracje) | — |
| Dopełnienie ostatniej strony za `p_memsz` (752 B) | **niezerowane** (752 B ≠ 0) | zerowane i weryfikowane |
| Bootloader w MBI | `GRUB 2.12-1ubuntu7.3`, tagi 1, 2, 4, 6, 8, 9, 12, 14, 15, 17, 21 (bez 20: usługi zakończone) | — |
| Wynik jądra, profil produkcyjny (pc/2 GiB, virtio) | **`[SYSTEM] Pulpit gotowy`, `System operacyjny gotowy!`** | BLOCKED (E506) |
| Wynik jądra, profil `-vga std` (q35/256 MiB, pc/2 GiB, bez S3) | inicjalizacja do BSP/PSF, potem `KERNEL-PANIC: Loader nie utworzył procesu Menedżera Okien` | BLOCKED (E506) |

**Jak GRUB obsługuje to jądro** **[OBSERWACJA + WNIOSEK]**:
1. Przed EBS GRUB przydziela przez firmware wszystko, co może dostać pod adresem docelowym (ph0–ph2).
2. Dla części, której firmware nie oddaje (ph3), przydziela bufor pośredni w wolnej pamięci i składa w nim segment.
3. Po `ExitBootServices` kopiuje bufor pod `0x2AA000` (mechanizm „relocator” GRUB) **niezależnie od typu pamięci
   docelowej**, także na ACPI NVS.
4. Sam GRUB nadal opisuje ten obszar jądru jako NVS: w mapie MB2 (tag 6) jako typ 4 i w mapie EFI (tag 17) jako
   `EfiACPIMemoryNVS`.

Specyfikacja Multiboot2 wymaga, by program rozruchowy załadował segmenty pod adresy fizyczne
z nagłówków. Nie zwalnia go to z ograniczeń własności pamięci UEFI.

## 4. Dowód problemu bezpieczeństwa w obecnym rozruchu produkcyjnym

**[OBSERWACJA]** Porównanie zrzutów z chwili przed poleceniem `multiboot2` i z chwili wejścia do jądra w każdej
konfiguracji z S3:

| Region ACPI NVS | Bajty | Zmienione przez ścieżkę GRUB | Równe obrazowi jądra przy wejściu | Zera przed |
|---|---|---|---|---|
| `0x800000–0x808000` | 32 768 | 32 662 | 32 768 | 26 626 |
| `0x80B000–0x80C000` | 4 096 | 4 086 | 4 096 | 4 096 |
| `0x810000–0x900000` | 983 040 | 978 971 | 983 040 | 47 674 |

- Cały ACPI NVS w zakresie jądra (249 stron, 996 KiB) zawiera po rozruchu obraz jądra. Są tam głównie
  skompresowane bloby aplikacji `kontrolki4`, `snake`, `okno_informacyjne`, `przegladarka_obrazow`,
  `aktualizacje`, `test_dzwieku`, `diagnostyka` oraz kilka zmiennych globalnych (`g_aktywny_motyw`,
  `boot_preferences`, `hda_boot_active`).
- Wcześniejsza zawartość firmware (niezerowa) jest **zniszczona**.
- Jądro **zajmuje** te adresy przez cały czas pracy: w ciągu 40–60 s po wejściu obszar się nie zmienił
  (to bloby tylko do odczytu).
- W konfiguracji bez S3 nadpisywana jest wyłącznie `EfiBootServicesData`, co po EBS jest zgodne ze specyfikacją.

**Wniosek:** obecna produkcyjna ścieżka GRUB UEFI na OVMF z domyślnym S3 **narusza** wymóg zachowania
`EfiACPIMemoryNVS`. Działający pulpit nie dowodzi bezpieczeństwa.

**[WNIOSEK]** Skutki praktyczne:
- OVMF używa tego obszaru dla ścieżki wznawiania S3;
- jądro nie implementuje S3 (w źródłach brak `_S3`; log pokazuje tylko odczyt `_S5` z DSDT, pełnego interpretera AML nie stwierdzono), więc na OVMF
  awaria w bieżącej pracy jest mało prawdopodobna;
- na prawdziwym sprzęcie ACPI NVS może zawierać dane używane przez AML/SMM; nadpisanie grozi trudnymi do
  zdiagnozowania błędami. **[NIEZNANE]** — nie testowano na sprzęcie.

Panika „Menedżer Okien” w profilu `-vga std` **nie wynika z ładowania**:
- obraz jest bajt w bajt poprawny, a nadpisany region nie zmienia się po wejściu;
- blob `menedzer_okien.bin.bz` (format `BZB1`, CRC poprawne) rozpakowuje się do nagłówka `BUR\x02`;
- w profilu produkcyjnym pulpit startuje.

Przyczyna paniki to **[NIEZNANE]** — osobne zgłoszenie, poza zakresem tej fazy.

**Pamięć w jądrze** (audyt źródeł `jadro/mm/pmm.cpp`):
- PMM udostępnia wyłącznie wpisy typu 1 (`MULTIBOOT_MEMORY_AVAILABLE`); ACPI reclaim, NVS, reserved i MMIO nigdy
  nie trafiają do puli;
- pierwsze 1 MiB, LAPIC, IOAPIC, MBI, moduły i framebuffer są rezerwowane na stałe, a zwalnianie modułów pomija
  ramki obrazu jądra;
- PMM śledzi tylko pierwsze 4 GiB;
- **odstępstwo:** `dodaj_dostepny_zakres(__kernel_start…__kernel_end)` oznacza wszystkie ramki obrazu jako
  „dostępne” w `mapa_dostepnosci`, także te, które mapa MB2 opisuje jako NVS, a dopiero potem rezerwuje je na
  stałe. Te ramki nigdy nie są przydzielane (niskie ryzyko), ale bitmapa dostępności przestaje odzwierciedlać
  mapę firmware (diagnostyka `pmm_pobierz_dostepne_ramki` liczy je jako RAM).
- Jądro nie korzysta z tagu 17 (mapa EFI).

## 5. Zależności od stałych adresów (audyt tylko do odczytu)

Kompilacja jądra: `-mcmodel=large -fno-pic -fno-pie`, link `-no-pie -static`, `ET_EXEC` bez sekcji relokacji.
W prywatnym ELF jest **20 894** instrukcji `movabs` z adresem wewnątrz obrazu, **240** 32-bitowych argumentów
natychmiastowych z adresem obrazu (głównie `boot.S`), **10** odwołań RIP-relative oraz do **2830**
wyrównanych słów 64-bit w `.rodata` (vtable, tablice skoków) i ok. 24 w `.data`, których wartość wskazuje obraz
(oszacowanie górne).

Klasyfikacja: **SPRZ** — wymaganie sprzętowe; **KON** — obecny kontrakt rozruchu; **VM** — obecny projekt pamięci
wirtualnej; **WYG** — wygoda implementacji; **USUW** — usuwalne ograniczenie architektury.

| Plik / symbol | Obecne założenie | Uzasadnienie | Klasa | Ryzyko | Przyszłe zastąpienie |
|---|---|---|---|---|---|
| `linker.ld` `KERNEL_VMA_START=0x100000`, asercja VMA==LMA | obraz pod 1 MiB, PA==VA | GRUB/MB2 bez tagu relokacji | KON | konflikt z firmware (§2) | LMA ruchome, VMA stałe (`AT()`), albo `PT_LOAD` z `p_paddr ≠ p_vaddr` |
| `linker.ld` asercja `__kernel_end ≤ 15 MiB` | koniec obrazu < `RING3_BASE − 1 MiB` | VA jądra pod `RING3_BASE` | VM | limit wzrostu | po rozdzieleniu PA/VA — VA jądra w górnej połowie albo zachowany |
| `pamiec.h` `KERNEL_MAX_END_ALLOWED=0xE00000` | limit 14 MiB (komentarz: „~5.99 MiB”) | niespójne z linkerem (15 MiB), nieegzekwowane w `.cpp` | WYG | mylące | ujednolicić |
| `boot.S` nagłówek MB2 | tagi: info (6, 8, 14, 15), framebuffer 1920×1080×32; **brak** tagu 10 (relokowalny) i 7/9 (EFI) | GRUB przekazuje sterowanie w 32-bit PM | KON | wymusza stały adres | tag 10 albo natywne wejście 64-bit BRB |
| `boot.S` `_start` | `movl $stack_top`, `$pml4_table`, `lgdt gdt64_pointer32` (`.long gdt64`), `ljmp $0x08,$long_mode_start` — adresy bezwzględne 32-bit = PA | wykonywane przy wyłączonym stronicowaniu | KON | nie działa pod innym PA | kod niezależny od pozycji (delta `call/pop`) albo pominięcie przy wejściu 64-bit |
| `boot.S` tablice `pml4/pdp/pd×4` | identity 0–4 GiB stronami 2 MiB, adresy tablic = PA | bootstrap long mode | KON | jw. | tablice z deltą PA; mapowanie VA jądra→PA |
| `boot.S` stos 64 KiB w `.bss` | `stack_top` jako PA i VA | jw. | KON | jw. | jw. |
| `jadro/mm/vmm.cpp` `st_pml4/st_pdp/st_pd/st_hhdm_*` | `st_pml4[0] = (uint64_t)st_pdp \| flagi` — **VA statycznej tablicy używany jako PA** | obraz identity-mapowany | VM | błąd po rozdzieleniu PA/VA | `va_obrazu_na_pa()` = VA − `__kernel_start` + `baza_fizyczna` |
| `vmm.cpp` identity 0–4 GiB (supervisor) | sterowniki AHCI/E1000/LAPIC/IOAPIC używają fizycznych BAR jako VA | uproszczenie | WYG | obejmuje też VA obrazu | jawne mapowania MMIO; okno VA jądra nadpisuje identity |
| `vmm.cpp` HHDM `0xFFFF800000000000` | RAM z mapy MB2 przez HHDM | — | VM | brak | bez zmian |
| `pmm.cpp` rezerwacja obrazu | `__kernel_start/__kernel_end` (VA) jako zakres fizyczny | PA==VA | VM | zła rezerwacja po relokacji | rezerwacja `[baza_fizyczna, +rozmiar)` z informacji rozruchowej |
| `pmm.cpp` `GRANICA_NISKIEJ_PAMIECI` (1 MiB) | rezerwacja pierwszego MiB | BIOS/EBDA/VGA, adres 0 | SPRZ | brak | bez zmian |
| `pmm.cpp`/`vmm.cpp` `LAPIC_MMIO=0xFEE00000`, `IOAPIC_MMIO=0xFEC00000` | stałe adresy MMIO | wartości domyślne architektury | SPRZ/WYG | inne płyty | ACPI MADT (komentarz w kodzie) |
| `pamiec.h` `RING3_BASE=0x1000000`, `RING3_*_VA` | pliki `.bur` pod stałymi VA ≥ 16 MiB | format BUR | VM | koliduje z przeniesieniem **VA** jądra w górę | niezależne od PA jądra — bez zmian w opcji C |
| `zarezerwowane_va.h` okna `0x140000000…`, `0x200000000` (PSF), `0x500000000` (sterta 16 MiB), górna połowa `0xFFFF9…/A…` | stałe okna VA | projekt VM | VM | brak (VA) | bez zmian |
| `sterowniki/dma.cpp` i inne | DMA z ramek PMM (adres fizyczny z PMM) | — | — | brak zależności od PA obrazu | bez zmian |
| `gdt.cpp`, `idt.cpp`, `tss.cpp`, `syscall.S` | `lgdt/lidt`, MSR `LSTAR` z VA | VA stałe | — | brak przy stałym VA | bez zmian (opcja C) |
| Moduły MB2 / MBI | MBI czytane przez identity (< 4 GiB, rezerwowane) | 32-bit MB2 | KON | przy natywnym protokole adres dowolny | odczyt przez HHDM albo kopia |
| SMP / trampolina AP | **brak** (1 CPU) | — | — | przyszła trampolina będzie wymagać strony < 1 MiB | przydział przez firmware/PMM |

## 6. Zużycie pamięci statycznej

**[OBSERWACJA]** `nm -S` prywatnego ELF i symbole `_binary_*_size`:

| Składnik | Rozmiar | W pliku |
|---|---|---|
| `.text` | 1,16 MiB (0x129D70) | tak |
| `.rodata` | 0,49 MiB (0x7E030), m.in. maska logo 92 KiB, czcionka 16×16 ×3 kopie po 37 KiB, `bsf_unicode_layout_blob` 60 KiB | tak |
| `.data` | 7,24 MiB (0x73DB40) | tak |
| — osadzone bloby (86): aplikacje BUR `*.bin.bz` (27 aplikacji, np. `przegladarka` 364 KiB, `test` 321 KiB, `grajek` 261 KiB, `menedzer_okien` 256 KiB), ikony PNG/JPG, logo | 5,75 MiB | tak |
| — `xhci.cpp` `controllers[8]` | 1,42 MiB | tak (**prawie same zera** — trafia do `.data`, bo `Controller` ma niezerowe inicjalizatory `discovery_port{1}`, `hid_service_slot{1}`) |
| `.bss` | 3,53 MiB (memsz − filesz ph3) | nie |
| — `bufor_wymiany_plikow` 512 KiB, `cele_drop` 264 KiB, `DiagStore::logs` 256 KiB, `mbedtls_heap` 256 KiB, `g_silnik` 228 KiB, `pcm_mixer` 227 KiB, `DiagStore::devices` 150 KiB, `VirtioInputForensic::sources` 145 KiB, bitmapy PMM 2×128 KiB, `term_buf` 88 KiB, `kolejki_zdarzen` 80 KiB, tablice `st_hhdm_pt` 48 KiB, stosy IST 3×32 KiB, stos startowy 64 KiB | | |

Wnioski:
- Obraz przekracza 8 MiB (`0x800000`) już samymi danymi z pliku: koniec plikowej części ph3 to `0x9E7B40`.
- Zejście pod `0x800000` wymagałoby usunięcia ok. 5,4 MiB funkcji — **odrzucone** (część 8).
- Propozycje z niezależnym uzasadnieniem (nie rozwiązują konfliktu):
  - **xHCI `controllers` do `.bss`** (zerowe inicjalizatory + inicjalizacja w kodzie): −1,42 MiB pliku jądra
    i ISO, krótszy odczyt przez program rozruchowy; rozpiętość pamięci bez zmian.
  - **Aplikacje BUR jako pliki/moduły** zamiast blobów w jądrze: rozdzielenie cyklu życia aplikacji i jądra;
    budżet 15 MiB jest już dziś ograniczeniem wzrostu (`kernel-image-15mib-budget`).
  - **Bufory diagnostyczne `.bss`** (`DiagStore`, `VirtioInputForensic`, ok. 0,55 MiB) przydzielane dynamicznie
    po starcie PMM albo tylko w kompilacjach diagnostycznych.

## 7. Porównanie architektur

### Opcja A — obecne jądro o stałym adresie
- Bezpiecznie przed EBS: niemożliwe na OVMF (BS data od `0x900000`).
- Po EBS: kopiowanie na `BootServices*`/`Conventional` jest zgodne ze specyfikacją, ale **ACPI NVS przy S3
  wyklucza to na domyślnym OVMF**. Jedyną drogą byłoby nadpisanie NVS, a to jest zakazane.
- Na innych implementacjach UEFI wynik jest nieprzewidywalny (zależy od rozmieszczenia regionów firmware
  w 1–14 MiB). Nie da się zagwarantować bezpieczeństwa.
- Ograniczenia: BRB nie może poprawnie wystartować tego jądra na OVMF z S3; GRUB robi to z naruszeniem NVS.

### Opcja B — inny stały adres fizyczny
- Technicznie wykonalna (np. LMA powyżej 32 MiB, gdzie OVMF ma dziś wolne `0x1780000…`), ale:
  - żaden adres nie jest gwarantowany: mapa zależy od firmware, RAM, opcji (S3, SEV), modułów;
  - przeniesienie **VA** razem z PA koliduje z `RING3_BASE` (16 MiB) i stałymi VA aplikacji BUR;
  - przeniesienie samego PA to już opcja C.
- Wymagane zmiany: `linker.ld`, `boot.S`, PMM, VMM; jedna zmiana stałej i pełna regresja.
- Ocena: **przesuwa problem**, nie rozwiązuje go. Odrzucona jako rozwiązanie docelowe.

### Opcja C — fizycznie ruchome jądro ze stałymi adresami wirtualnymi (zalecana)
- Program rozruchowy przydziela dowolny odpowiedni ciągły region (`AllocatePages(AllocateMaxAddress <4 GiB` albo
  `AnyPages)`, `EfiLoaderData`, wyrównanie 2 MiB), ładuje segmenty i przekazuje `baza_fizyczna`.
- Adresy wirtualne zostają bez zmian (C1: obecne `0x100000…`; C2, późniejszy wariant: górna połowa).
  Dzięki temu **20 894 `movabs` i wskaźniki w danych nie wymagają żadnej relokacji**: kod nadal odwołuje się do
  tych samych VA.
- Wymagane: rozdzielenie PA/VA w trzech miejscach:
  - bootstrap: tablice stron mapujące okno VA jądra na `baza_fizyczna`, z kodem bootstrapu niezależnym od pozycji
    albo z wejściem 64-bit, w którym tablice buduje BRB;
  - VMM: `st_*` jako PA przez deltę;
  - PMM: rezerwacja prawdziwego zakresu fizycznego.
  Okno VA jądra musi nadpisywać identity-map 0–4 GiB w zakresie 1–14 MiB. Fizyczny RAM 1–14 MiB jest wtedy
  dostępny tylko przez HHDM; dynamiczne tablice i DMA już dziś idą przez HHDM/PMM.
- Ścieżka GRUB: tag MB2 10 („relocatable header”) pozwala GRUB-owi wybrać adres. **[NIEZNANE]** Pełną obsługę
  ELF + tagu 10 w GRUB 2.12 trzeba potwierdzić eksperymentem przed decyzją; bootstrap 32-bit musiałby być PIC.
  Alternatywa: GRUB pozostaje na obecnym kontrakcie do czasu wycofania (P-11).
- W^X: przy osobnym mapowaniu jądra naturalne jest nadanie R/RX/RW per segment (obecnego stanu egzekwowania praw segmentów przez VMM nie audytowano w tej fazie).
- KASLR: możliwy fizyczny od razu; wirtualny dopiero w C2/D.
- Natywny protokół BRB: naturalne dopasowanie — BRB zna `baza_fizyczna` i może przekazać ją w BRBI.

### Opcja D — w pełni relokowalne jądro
- `ET_DYN` (`-fpie`, model `small`/`medium-pic`) albo `ET_EXEC` z `--emit-relocs` i własną tabelą.
- Przy obecnym `-mcmodel=large` to ok. 21 tys. relokacji `R_X86_64_64` w kodzie plus ok. 2,8 tys. w danych;
  po przejściu na PIE głównie `R_X86_64_RELATIVE`.
- Wymaga procesora relokacji (w BRB albo w bootstrapie) z walidacją: typy wyłącznie `RELATIVE`/`64`, przesunięcia
  w segmentach RW lub tymczasowo RW, kontrola przepełnień, brak symboli zewnętrznych, potem przywrócenie R/RX.
  **Bez** ogólnego dynamicznego linkera.
- Ryzyko bezpieczeństwa: kod relokacji przetwarza dane obrazu przed jego uruchomieniem — większa powierzchnia ataku
  i więcej trybów awarii.
- Zaleta: pełny KASLR (VA i PA). Koszt: zmiana modelu kodu całego jądra, regresje wydajności i ABI.
  **Odroczona.**

### Opcja E — mały bootstrap + dynamiczne umieszczenie głównego jądra
- Mały bootstrap (kilka stron) przydzielany przez firmware gdziekolwiek (`AnyPages`) albo będący częścią BRB:
  buduje tablice stron dla stałych VA jądra, wchodzi w long mode i skacze pod VA.
- Bootstrap musi być PIC (albo to sam BRB, który już jest PE32+ relokowany przez firmware). Strony bootstrapu
  w `EfiLoaderCode`, oznaczone w mapie przekazanej jądru, zwalniane przez jądro po starcie.
- Ścieżka migracji: z MB2 32-bit (obecnej) do natywnego wejścia 64-bit BRB. **E jest mechanizmem realizacji C**
  od strony programu rozruchowego, a nie konkurencyjną architekturą.

### Zestawienie

| Kryterium | A | B | C | D | E |
|---|---|---|---|---|---|
| Bezpieczeństwo pamięci firmware | ✗ (OVMF+S3) | ~ (zależne od platformy) | ✓ | ✓ | ✓ (z C) |
| Zmiany w jądrze | brak | małe | średnie (bootstrap, VMM, PMM) | duże (model kodu) | jak C |
| Zmiany w BRB | brak (blokada) | stała | przydział dowolny + mapowanie | procesor relokacji | bootstrap/wejście 64-bit |
| Zgodność z GRUB | obecna (z naruszeniem NVS) | obecna | wymaga tagu 10 + PIC albo zostaje stara ścieżka | wymaga zmian | brak (natywne) |
| Legacy BIOS (przyszłe) | zwykle działa (e820) | jw. | ✓ | ✓ | ✓ |
| W^X / KASLR | — | — | W^X ✓, KASLR fizyczny | W^X ✓, KASLR pełny | jak C |
| Złożoność | — | niska | średnia | wysoka | średnia |

## 8. Odrzucone obejścia (część 8 polecenia)

- Nadpisywanie ACPI NVS, pamięci zarezerwowanej i runtime.
- Ignorowanie błędów `AllocatePages`.
- Traktowanie `BootServicesData` jako wolnej przed EBS.
- Traktowanie całej pamięci RAM jako wolnej po EBS.
- Trwałe wyłączenie S3.
- Modyfikacja OVMF.
- Inny stały adres bez analizy.
- Usuwanie funkcji.
- Osłabianie walidacji ELF, kontroli własności i mechanizmu wycofania.

Żadne z nich nie zostało zastosowane.

## 9. Wymagane zmiany (po akceptacji — NIE teraz)

**Jądro:**
- `linker.ld`: LMA oddzielone od VMA albo informacja o bazie przekazywana przy starcie;
- `boot.S`: bootstrap PIC lub nowe wejście 64-bit (`wejscie64`) z kontraktem BRBI;
- `vmm.cpp`: funkcja `va_obrazu_na_pa`, okno VA jądra nadpisujące identity;
- `pmm.cpp`: rezerwacja prawdziwego zakresu fizycznego i niedodawanie ramek NVS jako „dostępnych”;
- nagłówek MB2: opcjonalnie tag 10;
- poza tym (osobne decyzje): `controllers` xHCI do `.bss`, ujednolicenie limitów 14/15 MiB.

**BRB:**
- nowa polityka przydziału (jedna transakcja `AllocateMaxAddress`/`AnyPages`, wyrównanie, region ciągły, bez
  zmiany zasady własności);
- przekazanie bazy fizycznej w BRBI;
- tablice stron dla wejścia 64-bit (Faza 4B/4C);
- testy jak w Fazie 4A.

**Protokół:** BRBI — pole `baza_fizyczna_jadra`, rozmiar, mapa pamięci z oznaczeniem stron jądra, bootstrapu
i informacji rozruchowej.

## 10. Wymagane testy

- Host: model ładowania pod losową bazą (wyrównanie, granice, przepełnienia), mutacje mapy, weryfikacja
  bajt po bajcie.
- Jądro (host): test translacji `va_obrazu_na_pa`, PMM z bazą ≠ 1 MiB, VMM z oknem nadpisującym identity.
- QEMU/OVMF: q35/256 MiB i pc/2 GiB z S3 i bez S3, profil produkcyjny. Kryterium: zrzut ACPI NVS przed i po
  rozruchu (narzędzia tej fazy) wykazujący **0 zmienionych bajtów**, pulpit gotowy, regresje 4A.
- GRUB: ta sama metoda zrzutu, aby potwierdzić, że ścieżka zgodności nie nadpisuje NVS (po tagu 10) albo
  udokumentować, że nadal to robi.
- Sprzęt fizyczny: co najmniej dwie płyty UEFI (plan-testow §5).

## 11. Migracja i wycofanie

1. **4A-R (ta faza):** analiza, bez zmian w jądrze.
2. **R1:** jądro rozpoznaje bazę fizyczną (domyślnie `0x100000`, zachowanie identyczne jak dziś); testy PMM/VMM
   na hoście. Wycofanie: jeden commit, ścieżka GRUB bez zmian. Podzielone (§0) na:
   - **R1A** (zrealizowana jako fundament, czeka na przegląd): typy adresów, układ obrazu, kontrakt pamięci
     rozruchu v1 z adapterem MB2, PMM rezerwujący rzeczywisty podkład, VMM z PA tablic i oknem obrazu, strażniki
     izolacji, testy hosta. Ścieżka wejścia bez zmian (MB2, PA == VA).
   - **R1B** (zrealizowana jako kamień milowy wejścia i bootstrapu, czeka na przegląd — `r1b-wejscie-64bit.md`):
     wejście `wejscie_jadra64` z metadanymi w okrojonym ELF, kontrakt stanu CPU, tablice przejścia z W^X
     (`plan_przejscia.h`), przyjęcie kontraktu natywnego z pomiarem podkładu, wspólny potok `start_jadra`,
     ograniczony dostęp fizyczny (ACPI), bramka MBI/modułów. Natywny test PA ≠ VA wykonany uprzężą testową
     (GRUB BIOS, pulpit); wykonanie przez BRB pozostaje bramką R2/4C.
3. **R2:** bootstrap PIC lub wejście 64-bit; BRB ładuje pod adres przydzielony przez firmware (Faza 4A
   z polityką przydziału dowolnego). GRUB nadal działa na starym kontrakcie. Wycofanie: wpis GRUB w menu.
4. **R3 (opcjonalnie):** tag MB2 10, by także GRUB przestał nadpisywać NVS; pomiar zrzutem.
5. **R4:** BRB jako ścieżka domyślna po testach akceptacyjnych (decyzja P-11), GRUB jako zapas.

Każdy etap: pełna regresja `test-brb-*`, rozruch GRUB do pulpitu i zrzut NVS.

## 12. Zgodność

- **GRUB:** opcja C nie psuje obecnego wpisu, dopóki jądro akceptuje bazę `0x100000`. Usunięcie naruszenia NVS
  w ścieżce GRUB wymaga R3 albo wycofania GRUB.
- **Legacy BIOS (przyszłe BRB BIOS):** e820 zwykle udostępnia 1–14 MiB, ale opcja C działa niezależnie od tego.
- **Natywny protokół Bursztyn:** opcja C + E jest jego naturalną podstawą.

## 13. Rekomendacja (zaakceptowana — §0)

**Opcja C (fizycznie ruchome jądro ze stałymi VA), realizowana od strony BRB jako opcja E (natywne wejście
64-bit z tablicami stron BRB), z wariantem C1 (zachowanie obecnych VA) jako pierwszym krokiem.**

Uzasadnienie:
1. Jedyna z ocenianych opcji, która usuwa konflikt bez nadpisywania pamięci firmware na OVMF z S3.
2. Nie wymaga przetwarzania ok. 24 tys. relokacji (opcja D).
3. Nie przesuwa problemu (opcja B).
4. Zachowuje układ VA aplikacji BUR i `RING3_BASE`.
5. Zmiany w jądrze są lokalne (bootstrap, VMM, PMM).
6. Ścieżka GRUB pozostaje działająca w okresie przejściowym.

## 14. Otwarte pytania

1. Czy GRUB 2.12 obsługuje ELF z tagiem MB2 10 bez ograniczeń? (eksperyment z fixture)
2. Jaka jest zawartość i rola `0x800000–0x900000` w OVMF (edk2 `OvmfPkgX64.fdf`, MEMFD)? Jakie są skutki dla
   wznawiania S3? (nie badano)
3. Przyczyna paniki „Menedżer Okien” w profilu `-vga std`. (osobne zgłoszenie)
4. Zachowanie na sprzęcie fizycznym. Czy któraś platforma ma regiony firmware w 1–14 MiB poza OVMF?
5. Czy jądro powinno przejść na VA w górnej połowie (C2) przed wprowadzeniem SMP?
6. Polityka wyrównania (2 MiB vs 4 KiB) i maksymalny adres bazy (< 4 GiB ze względu na bootstrap 32-bit MB2).

## 15. Źródła i komponenty zewnętrzne

- UEFI Specification 2.10, rozdz. 7.2 (Memory Allocation Services, typy pamięci po EBS).
- Multiboot2 Specification 1.0 (nagłówek, tagi 6, 10, 17, 21).
- ACPI Specification 6.5, §15 (System Address Map Interfaces).
- GRUB 2.12-1ubuntu7.3 — **wyłącznie** jako zewnętrzny program obserwowany (konfiguracja, wyjście COM1,
  `lsefimmap`, zachowanie w czasie pracy). **Nie** kopiowano ani nie analizowano kodu źródłowego GRUB.
  Opis mechanizmu „relocator” pochodzi z obserwacji map i zrzutów.
- QEMU 8.2.2 (gdbstub, QMP `pmemsave`), GNU gdb, OVMF edk2 2024.02 — narzędzia testowe.
- Narzędzia tej fazy (oryginalne, BRB): `narzedzia/brb/zbuduj_fixture_grub.sh`, `narzedzia/brb/diag_grub_ovmf.py`,
  `narzedzia/brb/analizuj_diag_grub.py`. Wyniki: `.brb-build/qemu-diag/`.
