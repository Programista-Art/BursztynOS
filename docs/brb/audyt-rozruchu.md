# BRB — Faza 1: audyt obecnego procesu rozruchu Bursztyn OS

Dokument jest częścią specyfikacji **Bursztynowego Programu Rozruchowego (BRB)**. Opisuje stan repozytorium
w chwili audytu i nie zawiera żadnych zmian w kodzie produkcyjnym.

| Pole | Wartość |
|---|---|
| Gałąź | `feature/bootloader` (zweryfikowano `git branch --show-current`) |
| Commit bazowy | `99046a7` (feat(grajek,audio,bws): głęboka kolejka PCM 512 ms…) |
| Stan drzewa roboczego przed audytem | czysty (`git status --short` bez wpisów) |
| Data audytu | 2026-11-26/27 |
| Narzędzia hosta (zweryfikowane) | GRUB 2.12-1ubuntu7.3 (`grub-mkrescue`, `grub-mkstandalone`, platformy `i386-pc`, `x86_64-efi`, `x86_64-efi-signed`), xorriso 1.5.6, GCC 13.3.0 (`x86_64-linux-gnu-`), GNU ld 2.42 (emulacje `elf_x86_64`, `elf_i386`, `i386pep`, `i386pe`), QEMU 8.2.2, OVMF `OVMF_CODE_4M*.fd`, SeaBIOS `bios-256k.bin` |
| Narzędzia niedostępne | clang, lld/lld-link, nasm, MinGW (`x86_64-w64-mingw32-*`) |

Oznaczenia używane we wszystkich dokumentach `docs/brb/`:

- **[FAKT]** — stwierdzone bezpośrednio w kodzie lub artefaktach repozytorium (ze ścieżką i linią),
- **[WNIOSEK]** — wynik analizy faktów, bez uruchamiania kodu,
- **[PROJEKT]** — propozycja dla BRB (do zatwierdzenia),
- **[DO POTWIERDZENIA]** — kwestia otwarta, wymagająca pomiaru, testu albo decyzji właściciela.

> Numery linii odnoszą się do commitu `99046a7`. W Fazie 1 nie uruchamiano QEMU i nie budowano ISO,
> więc opis zawartości obrazu ISO wynika z receptur `Makefile` i zachowanych kopii stagingu, a nie z inspekcji
> zbudowanego obrazu.

---

## 1. Inwentarz komponentów rozruchu

### 1.1. Kod wykonywany przy wejściu do jądra

| Plik | Element | Linie | Rola |
|---|---|---|---|
| `jadro/arch/boot.S` | nagłówek Multiboot2 (`.multiboot`) | 30–76 | magic `0xE85250D6`, arch 0 (i386), tagi żądań |
| `jadro/arch/boot.S` | `pml4_table`, `pdp_table`, `pd_table`, `pd_table_1..3` | 94–115 | 6 tablic stron w `.bss` |
| `jadro/arch/boot.S` | `stack_bottom`/`stack_top` | 123–130 | stos startowy 64 KiB w `.bss` |
| `jadro/arch/boot.S` | `boot_error_code` | 145–149 | kod błędu wczesnego startu (1–8), tylko dla debuggera |
| `jadro/arch/boot.S` | `gdt64`, `gdt64_pointer32` | 155–178 | tymczasowa GDT: null, 0x08 kod 64-bit, 0x10 dane |
| `jadro/arch/boot.S` | `_start` (`.code32`) | 215–399 | wejście 32-bit, walidacja, CPUID, stronicowanie, Long Mode |
| `jadro/arch/boot.S` | `boot_cpu_has_cpuid` | 409–442 | test bitu EFLAGS.ID |
| `jadro/arch/boot.S` | `long_mode_start` (`.code64`) | 497–562 | segmenty, stos, `stack_protector_early_init`, `call kernel_main` |
| `jadro/arch/linker.ld` | układ obrazu, `PHDRS`, asercje | 45–512 | jądro pod 1 MiB, 4 segmenty `PT_LOAD`, kontrole link-time |
| `jadro/rdzen/kernel.cpp` | `kernel_main(uint64_t, uint64_t)` | 2866 | pierwsza funkcja C++ |
| `jadro/mm/stack_protector.cpp` | `stack_protector_early_init` | 174 | inicjalizacja `__stack_chk_guard` (RDSEED/RDRAND) przed `kernel_main` |

### 1.2. Konsumenci informacji Multiboot2 w jądrze

| Plik | Funkcja | Linie | Czytane tagi |
|---|---|---|---|
| `jadro/rdzen/kernel.cpp` | `kernel_main` | 2880–2892 | magic, wskaźnik MBI |
| `jadro/rdzen/kernel.cpp` | `znajdz_modul_mb2` | 946–990 | 3 (moduł) |
| `jadro/rdzen/kernel.cpp` | `zadano_automatyczne_testy` (ogólne wyszukiwanie tokenów) | 990–1026 | 1 (wiersz poleceń) |
| `jadro/rdzen/kernel.cpp` | `odczytaj_odswiezanie_klatek`, `zadano_test_stack_guard`, `zadano_destrukcyjne_testy_dysku` | 1028–1124 | 1 |
| `jadro/rdzen/kernel.cpp` | import `test8k.bmp`, `zdjecia.pak`, `muzyka.pak`, obrazy pulpitu, `bsf-settings` | 1127, 1258, 1349, 1474, 1919–1950 | 3 |
| `jadro/mm/pmm.cpp` | `pobierz_zakres_multiboot`, `przejdz_po_tagach_multiboot`, `przetworz_mape_pamieci`, `zarezerwuj_modul`, `zarezerwuj_framebuffer`, `InicjalizujPMM` | 776–1090, 1426–1620 | 6, 3, 8 |
| `jadro/mm/pamiec.h` | typy tagów i struktur MB2 | 136–240 | definicje 0, 6, 8, 11, 12, 14, 15 |
| `sterowniki/grafika/grafika.cpp` | `InicjalizujGrafike` | 3529–3700 | 8, 11, 12 |
| `sterowniki/platforma/acpi.cpp` | `acpi_inicjalizuj` | 246–290 | 14, 15 |
| `jadro/gui/instalator.cpp` | `instalator_boot` | 336–350 | 1, 3, 12, 20 |
| `jadro/gui/tapety.cpp` | `znajdz_modul_tapet`, `instaluj`, `tapety_inicjalizuj` | 90–130, 431, 1105 | 3 |
| `bsf/bootstrap.inc` | `bsf_import_fonts` | 10–66 | 3 (etykiety `bsf/…`) |
| `jadro/aktualizacja/aktualizacja.cpp` | `aktualizacja_sprawdz_rozruch` | 477–630 | 1 (`bursztyn.slot=`, `bursztyn.awaria_slotu=`) |
| `jadro/rdzen/opcje_startowe.h` | `opcja_startowa_obecna` | 3–13 | dopasowanie tokenów wiersza poleceń |

### 1.3. Budowanie obrazów i narzędzia

| Plik | Element | Linie | Rola |
|---|---|---|---|
| `Makefile` | `GRUB_MKRESCUE`, `GRUB_FILE`, `XORRISO` | 43–45 | narzędzia GRUB/xorriso |
| `Makefile` | `ISO := BursztynOS.iso`, `ISO_DIR := isodir`, `GRUB_CFG` | 52–64 | artefakty ISO |
| `Makefile` | `CPU_FLAGS`, `HARDEN_FLAGS`, `FREESTANDING_FLAGS`, `KERNEL_LDFLAGS` | 117–135, 416–423 | flagi jądra (`-mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2`, `-fno-pic -fno-pie`, `-T jadro/arch/linker.ld -nostdlib -no-pie -static`) |
| `Makefile` | `$(KERNEL)` | 1255–1256 | linkowanie `system_operacyjny.bin` z `-lgcc` |
| `Makefile` | `check-kernel` | 1262–1273 | `grub-file --is-x86-multiboot2` (pomijane, gdy brak narzędzia) |
| `Makefile` | `$(GRUB_CFG)` | 1300–1357 | staging `isodir/`, generowanie `grub.cfg` (4 wpisy) |
| `Makefile` | `.installer-build/BOOTX64.EFI` | 1359–1361 | `grub-mkstandalone -O x86_64-efi … "boot/grub/grub.cfg=narzedzia/instalator/grub-embedded.cfg"` |
| `Makefile` | `$(ISO)` | 1497–1512 | `grub-mkrescue -o BursztynOS.iso isodir --xorriso=xorriso` + kontrola `tapety.pak` i `grub.cfg` przez `xorriso -osirrox` |
| `Makefile` | `$(CI_TEST_ISO)`, `$(CI_BLK_DESTRUCTIVE_ISO)`, `$(CI_DESTRUCTIVE_ONLY_ISO)`, `$(ISO8K)` | 1519–1618 | ISO testowe (jeden wpis, `timeout=0`) |
| `narzedzia/package_release_kernel.py` | — | całość | `objcopy --strip-all`, dowód identyczności `PT_LOAD` i wejścia, prywatna kopia ELF z symbolami w `.release-private/<sha256>/` |
| `narzedzia/package_release_notices.py` | — | całość | katalog `licenses/` w ISO (w tym `GRUB-copyright.txt`, `GPL-3.txt` z hosta) |
| `narzedzia/package_bsf.py` | — | 34–37 | dopisuje `module2 /boot/ustawienia.bur bsf-settings` i `module2 /czcionki/<dir>/<plik> bsf/<plik>` przed każdym `boot` |
| `narzedzia/package_desktop_images.py` | — | 20, 45–46 | `module2 /boot/pulpit/test.bmp pulpit/test.bmp` (i `test1.bmp`) |
| `narzedzia/package_galeria.py` | — | 23, 71–85 | `module2 /boot/zdjecia.pak zdjecia.pak` (limit 120 MiB) |
| `narzedzia/package_muzyka.py` | — | 61–89 | `module2 /boot/muzyka.pak muzyka.pak` (tylko `MUZYKA_W_ISO=1`) |
| `narzedzia/instalator/grub-embedded.cfg` | — | całość | skrypt A/B wbudowany w `BOOTX64.EFI` zainstalowanego systemu |
| `runwindows.ps1`, `rununix.sh`, `Makefile` (cele `run*`) | — | — | uruchamianie QEMU (BIOS = SeaBIOS, UEFI = OVMF przez `-bios`) |

### 1.4. Instalator i aktualizacje (zależne od rozruchu)

| Plik | Element | Linie | Rola |
|---|---|---|---|
| `jadro/gui/instalator.cpp` | `config[]` | 41–46 | `grub.cfg` zapisywany na ESP (patrz defekt D-07) |
| `jadro/gui/instalator.cpp` | `payloads()` | 146–147 | `installer.efi` musi zaczynać się od `MZ`, `installer.kernel` od `\177ELF` |
| `jadro/gui/instalator.cpp` | `perform()` etapy GPT→ESP→BSP2→…→ZNACZNIK | 154–335 | zapis i weryfikacja dysku |
| `jadro/gui/instalator.cpp` | `instalator_obsluz` | 360–420 | odmowa instalacji, gdy rozruch nie był UEFI (`"boot is not UEFI"`) |
| `jadro/fs/storage_format.h/.cpp` | `plan`, `gpt_install_*`, `fat_install_*`, `esp_*` | `.cpp` 139–600 | GPT, ESP FAT32, sloty jądra, `grubenv` |
| `jadro/aktualizacja/aktualizacja_rozruch.h` | `bursztyn_rozruch_stan` (512 B, CRC32) | 23–240 | autorytatywny stan A/B w LBA 2046/2047 |
| `jadro/aktualizacja/aktualizacja.cpp` | `aktualizacja_sprawdz_rozruch`, `aktualizacja_potwierdz_rozruch`, zapis slotu | 477–690, 815–870, 1015–1050 | weryfikacja uruchomionego slotu, potwierdzenie, zapis jądra na ESP |
| `sterowniki/magazyn/blokowy.cpp` | `blokowy_esp_systemowy` | 239–251 | wyszukanie ESP po GUID typu |

---

## 2. Jak zbudowane jest jądro (format obrazu)

**[FAKT]** Na podstawie `jadro/arch/linker.ld` oraz nagłówków prywatnego ELF
`.release-private/303cb82e…/kernel.elf` (zgodny z `__kernel_end = 0xD6F000` z ostatniego builda):

| Cecha | Wartość |
|---|---|
| Format | ELF64, little-endian, `ET_EXEC`, `EM_X86_64`, OS/ABI = 3 (GNU) |
| Punkt wejścia `e_entry` | `0x1193E2` = `_start` — **kod 32-bitowy** (`.code32`) |
| Adres ładowania | `p_paddr == p_vaddr`, od `0x00100000` (1 MiB); asercja `__kernel_start == KERNEL_VMA_START` (`linker.ld:442`) |
| Segmenty `PT_LOAD` | 4: `.boot` R (0x50 B), `.text` R-X (0x129D70 B), `.rodata` R (0x7E030 B), `.data+.bss` RW (filesz 0x73DB40, memsz 0xAC4D10) |
| Wyrównanie | `p_align = 0x1000`, sekcje wyrównane do 4 KiB, `max-page-size=0x1000` |
| Koniec obrazu w pamięci | `__kernel_end = 0xD6F000` (≈13,4 MiB) |
| `.bss` | `NOLOAD`, **nie jest zerowane przez jądro** — brak odwołań do `__bss_start` poza deklaracją w `jadro/mm/pamiec.h:132` |
| Konstruktory globalne | `.init_array` jest w obrazie, ale brak kodu wywołującego `__init_array_start` (jądro polega na inicjalizacji stałej) |
| Nagłówek MB2 | pierwsza sekcja, pod 1 MiB, w pierwszych 32 KiB pliku (`linker.ld:447–463`) |
| Wersja w ISO | `objcopy --strip-all`, ten sam układ `PT_LOAD` i wejście (`package_release_kernel.py`) |

**[FAKT]** Ograniczenia przestrzeni adresowej (`jadro/mm/pamiec.h:62–91`): `RING3_BASE = 16 MiB`,
`KERNEL_MAX_END_ALLOWED = 0x00E00000` (stała dokumentacyjna z `static_assert`; nie jest sprawdzana względem
`__kernel_end` w czasie linkowania). Pamięć projektu przyjmuje praktyczny budżet „poniżej 15 MiB”.
**[DO POTWIERDZENIA]** Która granica (14 MiB czy 15 MiB) jest wiążąca. Dla BRB istotne jest tylko to, że
**zakres fizyczny 1 MiB … `__kernel_end` musi być wolny** w chwili przekazania sterowania.

**[WNIOSEK]** Jądro jest obrazem o **stałym adresie fizycznym**, bez relokacji. Wymaga ładowarki ELF64,
która umieszcza segmenty dokładnie pod `p_paddr` i zeruje `memsz − filesz`. Nie jest płaskim plikiem binarnym:
nazwa `.bin` jest historyczna.

---

## 3. Obecna sekwencja rozruchu

### 3.1. Live ISO — Legacy BIOS (SeaBIOS / BIOS CSM)

```
 Firmware BIOS (POST)
   │  El Torito no-emulation (CD) albo MBR hybrydowego obrazu (USB)  ← tworzy grub-mkrescue
   ▼
 GRUB i386-pc (core.img z modułami iso9660, multiboot2, vbe, all_video …)
   │  czyta /boot/grub/grub.cfg z ISO9660
   │  menu (timeout=3, default=0), gfxmode=1280x720x32, gfxpayload=keep
   │  multiboot2 /boot/system_operacyjny.bin <parametry>
   │  module2 /boot/tapety.pak tapety.pak, module2 /boot/ustawienia.bur bsf-settings,
   │  module2 /czcionki/... bsf/..., module2 /boot/pulpit/test*.bmp, [zdjecia.pak], [muzyka.pak]
   │  A20, E820 → tag 6, VBE LFB → tag 8, RSDP → tag 14/15
   ▼
 _start (32-bit PM, PG=0, EAX=0x36D76289, EBX=MBI)            jadro/arch/boot.S:215
   │  cli, cld, stos 64 KiB, walidacja magic/MBI, CPUID (MSR, PAE, PSE, LM)
   │  identity map 0..4 GiB (2048 × 2 MiB), lgdt, CR4.PSE|PAE, CR3, EFER.LME, CR0.WP|PG
   ▼  ljmp 0x08:long_mode_start
 long_mode_start (64-bit)                                       jadro/arch/boot.S:497
   │  DS/ES/SS=0x10, FS/GS=0, RSP=stack_top, stack_protector_early_init()
   ▼  RDI=magic, RSI=MBI, RBP=0
 kernel_main                                                    jadro/rdzen/kernel.cpp:2866
```

### 3.2. Live ISO — UEFI (OVMF / fizyczny UEFI)

```
 Firmware UEFI
   │  El Torito: wpis EFI → obraz FAT (efi.img) z \EFI\BOOT\BOOTX64.EFI   ← tworzy grub-mkrescue
   ▼
 GRUB x86_64-efi (EFI app)
   │  czyta ten sam /boot/grub/grub.cfg z ISO9660 (własny sterownik GRUB, nie firmware)
   │  GOP → tag 8; EFI system table → tag 12 (i tag 20 – uchwyt obrazu); mapa pamięci → tag 6
   │  kernel nie żąda tagu 7 (EFI boot services) → GRUB wywołuje ExitBootServices
   │  przejście z 64-bit Long Mode do 32-bit protected mode (PG=0)
   ▼
 _start (identycznie jak w BIOS) → long_mode_start → kernel_main
```

**[WNIOSEK]** Obie ścieżki kończą się **tym samym stanem wejścia**: 32-bitowy tryb chroniony, bez
stronicowania. Jądro nie ma osobnego wejścia 64-bitowego.

**[DO POTWIERDZENIA]** Dokładny układ obrazu hybrydowego (El Torito BIOS + EFI, MBR/GPT dla USB) wynika z
domyślnego zachowania `grub-mkrescue` 2.12 przy zainstalowanych platformach `i386-pc` i `x86_64-efi`.
W Fazie 1 nie zbudowano ISO. Weryfikacja przy najbliższym buildzie (bez QEMU):
`xorriso -indev BursztynOS.iso -report_el_torito plain -report_system_area plain`.

### 3.3. Zainstalowany system — UEFI (jedyny obsługiwany tryb)

```
 Firmware UEFI (wpis rozruchowy nośnika wymiennego / domyślny)
   ▼
 \EFI\BOOT\BOOTX64.EFI  = GRUB x86_64-efi z grub-mkstandalone (moduły: part_gpt fat loadenv test normal
   │                       multiboot2 configfile search search_fs_file all_video gfxterm)
   │  wbudowany skrypt narzedzia/instalator/grub-embedded.cfg:
   │   search --file /EFI/BOOT/BOOTX64.EFI → root = ESP
   │   load_env -f /boot/grub/grubenv  (bursztyn_potwierdzony / _oczekujacy / _proby)
   │   pierwsza próba slotu oczekującego: bursztyn_proby=1 + save_env
   │   kolejna: powrót do potwierdzonego, bursztyn_oczekujacy=NIEUDANY
   │  multiboot2 /boot/jadro_{a|b}.bin bursztyn.installed=1 bursztyn.slot={A|B} [bursztyn.awaria_slotu=B]
   │  BEZ module2 – zainstalowany system nie dostaje żadnych modułów
   ▼
 _start → long_mode_start → kernel_main
   │  aktualizacja_sprawdz_rozruch(): porównanie slotu z autorytatywnym stanem LBA 2046/2047,
   │  samonaprawa grubenv, a przy nieautoryzowanym slocie – zapis grubenv i reset (outb 0xFE → 0x64)
```

**[FAKT]** Instalator odmawia instalacji, gdy MBI nie zawiera tagu 12 ani 20 (`instalator.cpp:341`,
`:378` — `"boot is not UEFI"`). Protective MBR zapisywany przez `gpt_install_*` nie zawiera kodu
rozruchowego (`storage_format.cpp:212`, `:267`). Na dysku nie ma partycji BIOS Boot.
**Zainstalowany system nie obsługuje rozruchu BIOS.**

### 3.4. Różnice BIOS / UEFI w obecnej konfiguracji

| Aspekt | Live BIOS | Live UEFI | Zainstalowany UEFI |
|---|---|---|---|
| Program rozruchowy | GRUB i386-pc z `grub-mkrescue` | GRUB x86_64-efi z `grub-mkrescue` | GRUB x86_64-efi z `grub-mkstandalone` |
| Konfiguracja | `/boot/grub/grub.cfg` w ISO (generowany w `Makefile`) | ten sam | wbudowana `grub-embedded.cfg` + `grubenv` |
| Moduły MB2 | tak (tapety, BSF, pulpit, opcjonalnie galeria/muzyka, instalator) | tak | **brak** |
| Tag 12/20 (EFI) | brak | jest | jest |
| Sterownik ekranu w jądrze | `SterownikVESA` (`grafika.cpp:3662`) | `SterownikGOP` | `SterownikGOP` |
| Instalacja możliwa | **nie** (odmowa) | tak | — |
| Tryb graficzny | VBE 1280×720×32 (`gfxmode`) | GOP wybrany przez GRUB (`gfxmode=1280x720x32`) | GOP (`gfxmode=auto` tylko w nieużywanym `grub.cfg`, patrz D-07) |

---

## 4. Co dokładnie robi GNU GRUB dla Bursztyn OS

| # | Zadanie | Wykonuje | Źródło faktu |
|---|---|---|---|
| G1 | Rozruch z nośnika (El Torito, MBR hybrydowy, BOOTX64.EFI) | GRUB / grub-mkrescue | `Makefile:1498` |
| G2 | Odczyt plików z ISO9660 (Live) i FAT32 (ESP) | GRUB | `grub.cfg`, `grub-embedded.cfg` |
| G3 | Menu rozruchowe (4 wpisy Live, `timeout=3`) | GRUB | `Makefile:1309–1351` |
| G4 | Wybór slotu A/B, licznik prób, zapis `grubenv` | GRUB (skrypt) | `grub-embedded.cfg` |
| G5 | Walidacja nagłówka MB2 i ładowanie ELF pod `p_paddr`, zerowanie BSS | GRUB | wymagane przez D-11 |
| G6 | Ładowanie modułów i nadawanie im etykiet (`cmdline` tagu 3) | GRUB | `module2 … etykieta` |
| G7 | Mapa pamięci (E820 albo UEFI → tag 6) | GRUB | `pmm.cpp:930–990` |
| G8 | Ustawienie trybu graficznego (VBE/GOP) i tag 8 | GRUB | `gfxmode`, `gfxpayload=keep` |
| G9 | Znalezienie RSDP i skopiowanie go do tagu 14/15 | GRUB | `acpi.cpp:263–266` |
| G10 | Tagi EFI (12, 20) jako znacznik rozruchu UEFI | GRUB | `instalator.cpp:341`, `grafika.cpp:3574` |
| G11 | `ExitBootServices` i przejście do 32-bit PM | GRUB | brak tagu 7 w nagłówku |
| G12 | A20, wyłączenie przerwań przed skokiem | GRUB | wymaganie MB2 (stan i386) |
| G13 | Przekazanie wiersza poleceń (tag 1) | GRUB | `kernel.cpp:990–1026` |

**Co robi samo jądro** **[FAKT]**: walidacja magic i wyrównania MBI, testy CPUID, własne stronicowanie
(identity 0..4 GiB), Long Mode, GDT/TSS/IDT, PMM/VMM, parsowanie tagów, framebuffer, ACPI/HPET, APIC
(maskowanie 8259A — `sterowniki/platforma/apic.cpp:6`), weryfikacja slotów A/B i stanu rozruchu.

---

## 5. Kontrakt wejścia do jądra (stan oczekiwany przez `_start`)

**[FAKT]** wynikający z `boot.S` i wymagań Multiboot2 („Multiboot2 Specification version 2.0”,
rozdz. 3.3 *I386 machine state*):

| Element | Wymaganie | Źródło |
|---|---|---|
| Tryb CPU | 32-bit protected mode, CR0.PE=1, **CR0.PG=0** | `boot.S:389` („CR0.PE zostało już ustawione przez GRUB”), MB2 §3.3 |
| EAX | `0x36D76289` | `boot.S:241` |
| EBX | fizyczny adres MBI, ≠0, wyrównany do 8 B, < 4 GiB (rejestr 32-bit) | `boot.S:244–251` |
| CS | 32-bit, odczyt/wykonanie, baza 0, limit 4 GiB | MB2 §3.3 |
| DS/ES/SS (FS/GS) | 32-bit, zapis/odczyt, baza 0, limit 4 GiB | MB2 §3.3; jądro przeładowuje dopiero w `long_mode_start` |
| A20 | włączona | MB2 §3.3 |
| EFLAGS | VM=0, IF=0 (jądro i tak wykonuje `cli; cld`) | `boot.S:219–220` |
| ESP | dowolny (jądro ustawia własny stos przed pierwszym `call`) | `boot.S:235` |
| GDTR/IDTR | dowolne (jądro ładuje własną GDT; IDT dopiero w `InicjalizujIDT`) | `boot.S:358`, `kernel.cpp:2952` |
| Pamięć | segmenty ELF załadowane pod `p_paddr`, BSS **wyzerowane**, MBI i moduły nie nakładają się na obraz | wniosek z D-11 i z braku walidacji nakładania (D-04) |
| Położenie wszystkiego | jądro, MBI, moduły < 4 GiB (identity map w `boot.S`, PMM do 4 GiB, tag modułu ma pola 32-bit) | `linker.ld:476–484`, `pmm.cpp:59` |
| CPU | CPUID, MSR, PAE, PSE, Long Mode (inaczej `hlt` z kodem 3–8); NX i FXSR/SSE sprawdzane później | `boot.S:257–283`, `vmm.cpp:459`, `fpu.cpp:12–13` |

**Stan przy wejściu do `kernel_main`** **[FAKT]**: Long Mode, stronicowanie 4-poziomowe z identity map
0..4 GiB (strony 2 MiB, RW, supervisor), CR0.WP=1, GDT z `boot.S`, IF=0, RSP = `stack_top & ~15` po `call`
(RSP ≡ 8 mod 16 w ciele funkcji), RDI = magic, RSI = MBI, RBP = 0.

**Wczesne inicjowanie** (`kernel.cpp:2873–2961`): `BootTiming::entry()`, `cli`, COM1 0x3F8
(`serial_inicjalizuj`, 38400 bodów), sprawdzenie magic, odczyt opcji z wiersza poleceń, `instalator_boot`,
`InicjalizujPMM`, `InicjalizujGDT`, TSS (`0x28`), `InicjalizujIDT`, `InicjalizujVMM`, `inicjalizuj_syscalls`,
`InicjalizujGrafike` → `framebuffer_ready`, sterta 16 MiB, `acpi_inicjalizuj`, HPET, `powitanie_inicjalizuj`
(ekran powitalny), `inicjalizuj_apic` (`:3093`). Później `aktualizacja_sprawdz_rozruch` (`:3324`),
`instaluj_wbudowane_programy` (`:3381`), `tapety_inicjalizuj` (`:3392`), importy modułów (`:2507–2509`),
`aktualizacja_potwierdz_rozruch` (`:2856`).

### 5.1. Wszystkie założenia jądra wobec programu rozruchowego

| ID | Założenie | Konsekwencja dla BRB |
|---|---|---|
| Z-01 | Wejście w 32-bit PM bez stronicowania | BRB-UEFI musi zejść z Long Mode do 32-bit PM (adapter MB2) albo jądro dostanie wejście 64-bit (protokół natywny, Faza 7) |
| Z-02 | Segmenty ELF pod `p_paddr` od 1 MiB, BSS wyzerowane | ładowarka ELF64 z zerowaniem `memsz−filesz` |
| Z-03 | MBI < 4 GiB, wyrównanie 8 B, pozostaje dostępne na zawsze (PMM rezerwuje je trwale, jądro czyta przez identity map przez cały czas pracy) | BRB umieszcza informacje < 4 GiB w pamięci, która po przekazaniu nie zostanie nadpisana |
| Z-04 | Tag 6 obecny dokładnie raz, z wpisami typu 1 = RAM; bez niego PMM się nie inicjuje (fail-closed) | normalizacja mapy pamięci, typ 1 tylko dla pamięci rzeczywiście wolnej po przekazaniu |
| Z-05 | Tag 8 typu RGB, 32 bpp, pozycje/rozmiary 16/8, 8/8, 0/8 (XRGB8888), szerokość/wysokość ≥ 64, pitch ≥ 4·szerokość i wielokrotność 4; inaczej `cli; hlt` bez komunikatu na ekranie | BRB musi wybrać tryb GOP/VBE w formacie XRGB8888 albo odmówić z polskim komunikatem przed przekazaniem |
| Z-06 | Obecność tagu 11/12 = „UEFI” (wybór `SterownikGOP`, zgoda instalatora — tag 12 lub 20) | adapter MB2 musi przekazać tag 12 (i 20), dopóki jądro nie czyta pola `firmware` protokołu BRB |
| Z-07 | RSDP wyłącznie z tagu 14/15 (kopia w MBI), bez skanowania pamięci BIOS | BRB zawsze dostarcza kopię RSDP (UEFI: tabela konfiguracji; BIOS: skan EBDA/0xE0000) |
| Z-08 | Moduły identyfikowane etykietą (`cmdline` tagu 3); znaczenie mają: `tapety.pak`, `bsf-settings`, `bsf/*`, `pulpit/test.bmp`, `pulpit/test1.bmp`, `zdjecia.pak`, `muzyka.pak`, `test8k.bmp`, `installer.efi`, `installer.kernel` | identyczne etykiety w BRB |
| Z-09 | Moduły < 4 GiB (pola 32-bit), niepuste; PMM rezerwuje tylko moduły ≤ 256 MiB z `start ≠ 0` (D-01) | BRB: moduły wyrównane do 4 KiB, < 4 GiB, limit ≤ 256 MiB na moduł |
| Z-10 | Wiersz poleceń: pierwszy tag 1, tokeny rozdzielone spacją (`bursztyn.*`) | ten sam format; BRB nie interpretuje opcji jądra |
| Z-11 | Brak oczekiwań co do tagów 2, 4, 5, 13, 17–21 (nieużywane) | BRB nie musi ich dostarczać (poza tagiem 20, patrz Z-06) |
| Z-12 | Jądro nie woła usług UEFI (brak użycia tablicy systemowej poza testem obecności tagu) | po przekazaniu nie są potrzebne ani Boot Services, ani Runtime Services |
| Z-13 | Zainstalowany system: parametry `bursztyn.installed=1 bursztyn.slot=A` albo `…slot=B`, brak modułów | BRB zainstalowany dostarcza te same parametry |
| Z-14 | `aktualizacja_sprawdz_rozruch` resetuje maszynę, gdy uruchomiony slot nie jest potwierdzony ani oczekujący w LBA 2046/2047 | ręczny wybór „poprzedniego slotu” w menu BRB zostanie odrzucony przez jądro, dopóki polityka się nie zmieni (patrz `integracja-instalatora.md`) |

---

## 6. Audyt kontraktu Multiboot2

### 6.1. Nagłówek w obrazie (`boot.S:30–76`)

| Pole / tag | Wartość | Uwagi |
|---|---|---|
| magic / architektura / długość / suma | `0xE85250D6` / 0 (i386) / 80 B / poprawna | długość = 0x50 = rozmiar segmentu `.boot` z ELF |
| Tag 1 (information request), flagi 0 = **wymagane** | typy 6 (mapa pamięci), 8 (framebuffer) | loader, który ich nie dostarczy, powinien odmówić |
| Tag 1, flagi 1 = **opcjonalne** | typy 14, 15 (ACPI old/new) | komentarz: brak HPET nie może blokować startu |
| Tag 5 (framebuffer), flagi 0 | 1920×1080×32 jako preferencja | GRUB i tak bierze tryb z `gfxmode`/`gfxpayload=keep` (1280×720) |
| Brak tagów 2/3 (adres/wejście), 6 (wyrównanie modułów), 7 (EFI BS), 8/9 (wejścia EFI), 10 (relokacja) | — | loader używa nagłówków ELF; moduły bez gwarancji wyrównania; GRUB kończy usługi EFI |

### 6.2. Struktury informacji parsowane przez jądro

| Tag | Nazwa | Wymagany przez jądro | Konsumenci | Zachowanie przy braku |
|---|---|---|---|---|
| 0 | koniec | PMM: tak (`rozmiar == 8`) | `pmm.cpp:866` | PMM odrzuca całe MBI |
| 1 | wiersz poleceń | nie | `kernel.cpp`, `instalator.cpp`, `aktualizacja.cpp` | brak opcji = tryb domyślny |
| 3 | moduł | nie (Live: praktycznie tak) | `kernel.cpp`, `tapety.cpp`, `bootstrap.inc`, `instalator.cpp`, `pmm.cpp` | zasoby zastępcze / brak funkcji |
| 6 | mapa pamięci | **tak** | `pmm.cpp` | PMM nie startuje → późniejsze `zatrzymaj_start` |
| 8 | framebuffer | **tak** | `grafika.cpp`, `pmm.cpp` | `"[GRAFIKA] BLAD KRYTYCZNY: Brak LFB w Multiboot2!"` + `hlt` |
| 11/12 | EFI system table | nie | `grafika.cpp`, `instalator.cpp` (12) | sterownik VESA, brak zgody na instalację |
| 14/15 | ACPI RSDP | nie | `acpi.cpp` | brak HPET/ACPI (ostrzeżenie diagnostyczne) |
| 20 | EFI64 image handle | nie | `instalator.cpp` | jak przy braku tagu 12 |

### 6.3. Przechodzenie po tagach — bezpieczeństwo i spójność

**[FAKT]** W repozytorium jest **co najmniej 10 niezależnych pętli** po tagach MB2:

- `pmm.cpp`: `przejdz_po_tagach_multiboot` oraz wariant diagnostyczny,
- `kernel.cpp`: `znajdz_modul_mb2`, `zadano_automatyczne_testy`, `zadano_test_stack_guard`,
  `zadano_destrukcyjne_testy_dysku` i diagnostyka `BURSZTYN_DIAG_PCI_TRACE`,
- `grafika.cpp`, `acpi.cpp`, `tapety.cpp`, `instalator.cpp`, `bootstrap.inc`, `aktualizacja.cpp`.

Różnią się szczegółami:

| Cecha | `pmm.cpp` (najściślejszy) | pozostałe |
|---|---|---|
| Wyrównanie adresu MBI | sprawdza (8 B) | nie sprawdzają (polegają na `boot.S`) |
| Górny limit `total_size` | brak | `instalator.cpp` i `bootstrap.inc`: ≤ 1 MiB; pozostałe: brak |
| Wymagany tag końca o rozmiarze 8 | tak | nie (koniec pętli przy `typ == 0` albo przy wyjściu poza zakres) |
| Przepełnienie przy wyrównaniu kroku | sprawdza (`wyrownaj_w_gore_do_8`) | `grafika.cpp` sprawdza; inne liczą `(off + rozmiar + 7) & ~7` w `uint64_t` (bez przepełnienia dla `uint32_t` rozmiarów) |
| Duplikat mapy pamięci | odrzuca | — |
| Granice tekstu (`cmdline`, etykieta) | — | szukają `NUL` w granicach tagu (poprawnie) |

**[WNIOSEK]** Odczyty są ograniczone do `total_size` deklarowanego przez loader. Ponieważ MBI pochodzi od
programu rozruchowego, któremu jądro ufa, nie stwierdzono podatności możliwej do wykorzystania przez
niezaufane dane użytkownika. Ryzykiem jest **niespójność**: różne części jądra mogą różnie zinterpretować to
samo uszkodzone MBI (np. PMM odrzuca, a grafika akceptuje). BRB i protokół natywny powinny to zamknąć
jednym parserem (Faza 7).

### 6.4. Stwierdzone defekty i słabości (z dowodami)

Żadnego z poniższych nie naprawiono w Fazie 1 (zakaz zmian w kodzie produkcyjnym). Proponowane naprawy są
**[PROJEKT]** do osobnych, recenzowanych zmian.

| ID | Waga | Opis | Dowód | Proponowana naprawa |
|---|---|---|---|---|
| D-01 | średnia | PMM **po cichu nie rezerwuje** modułu o rozmiarze > 256 MiB albo z `mod_start == 0`. Konsumenci (`znajdz_modul_mb2`, `bootstrap.inc`, `instalator_boot`) nadal używają takiego modułu, a alokator może wydać jego ramki → możliwe nadpisanie danych modułu. Dziś mitygowane limitami pakowania (galeria 120 MiB, czcionki `BSF::MaxFile`, instalator ≤ 64 MiB). | `pmm.cpp:994–1036` (`MAKS_ROZMIAR_MODULU`, `start == 0`) vs `kernel.cpp:959–980` | odrzucać (nie ignorować) takie moduły w jednym miejscu i oznaczać je jako nieprawidłowe dla wszystkich konsumentów; BRB nie będzie tworzyć takich modułów |
| D-02 | niska | `znajdz_modul_mb2` dopasowuje etykietę także **podciągiem** (`tekst_zawiera`) — np. etykieta `xtapety.pakx` pasuje do `tapety.pak`. Tylko `bsf-settings` ma dodatkowe `tekst_rowny`. | `kernel.cpp:974` | dopasowanie dokładne; BRB nadaje dokładne etykiety |
| D-03 | niska | ≥ 10 niezależnych parserów MB2 o różnej ścisłości (tabela 6.3). | sekcja 6.3 | jeden zwalidowany parser → wewnętrzna struktura `InformacjeRozruchu` (Faza 7) |
| D-04 | średnia | Jądro nie sprawdza, czy moduły, MBI i framebuffer **nie nakładają się** na obraz jądra, niską pamięć ani na siebie nawzajem, ani czy leżą w RAM z mapy pamięci. | `pmm.cpp:1500–1570` (rezerwacje bez kontroli kolizji) | walidacja nakładania w adapterze; BRB gwarantuje rozłączność (alokator zakresów) |
| D-05 | średnia | Pliki slotów `jadro_a.bin`/`jadro_b.bin` na ESP mają **stałą pojemność** ustaloną przy instalacji (rozmiar jądra zaokrąglony do 4 KiB). Aktualizacja z większym jądrem zawiedzie (`rozmiar > max_bytes`). | `storage_format.cpp:337`, `:525` | rezerwa pojemności slotu przy instalacji (np. 32 MiB/slot) — zmiana instalatora w Fazie 12 |
| D-06 | niska | Wpis LFN `system_operacyjny.bin` (`SYSTEM~1BIN`) wskazuje **te same klastry** co `JADRO_A.BIN` (pliki skrzyżowane w FAT). Po aktualizacji slotu A w miejscu rozmiar aliasu nie jest aktualizowany. Narzędzia `fsck` zgłoszą błąd. Plik służy tylko ścieżce zgodności w `grub-embedded.cfg`. | `storage_format.cpp:403–410`, `:544` | usunąć alias przy migracji do BRB albo nadać mu własne klastry |
| D-07 | niska | `/boot/grub/grub.cfg` zapisywany na ESP (`instalator.cpp:41–46`) **nie jest czytany** przez zainstalowany GRUB: wbudowany skrypt nie wywołuje `configfile`. Plik wskazuje `system_operacyjny.bin` bez slotu. `docs/aktualizacja-systemu.md` (§1.1) opisuje go jako „wbudowany skrypt wyboru slotu”, co jest nieścisłe. | `grub-embedded.cfg` (brak `configfile`) | poprawić dokumentację; przy migracji usunąć plik |
| D-08 | niska (dokumentacja) | `docs/03_proces_rozruchu.md` jest nieaktualny: opisuje identity map 2 MiB (jest 4 GiB), stos 16 KiB (jest 64 KiB), kopiowanie magic do EDI (jest EBP), nie wspomina `stack_protector_early_init`. | porównanie z `boot.S` | aktualizacja dokumentu w osobnej zmianie |
| D-09 | średnia (sprzęt) | Framebuffer inny niż XRGB8888 (np. GOP `PixelRedGreenBlueReserved8BitPerColor`) kończy start `hlt` bez komunikatu na ekranie (tylko COM1). | `grafika.cpp:3579–3627` | BRB wybiera tryb XRGB8888; przy braku — polski komunikat tekstowy przed przekazaniem |
| D-10 | niska | Brak zapasowego wyszukiwania RSDP (EBDA / 0xE0000–0xFFFFF) w jądrze. | `acpi.cpp:246–290` | BRB zawsze przekazuje RSDP |
| D-11 | informacja | Jądro **nie zeruje `.bss`** i nie wywołuje `.init_array`. Poprawny start zależy od zerowania przez loader. | sekcja 2 | wymaganie twarde dla ładowarki BRB |
| D-12 | niska | Część opcji jest dopasowywana podciągiem (`tekst_zawiera`: `bursztyn.test=stack_guard`, `bursztyn.test.diskdestructive=1`; `akt_tekst_zawiera`: `bursztyn.slot=A` i `=B`), część dokładnym tokenem (`opcja_startowa_obecna`). | `kernel.cpp:1075`, `:1111`; `aktualizacja.cpp:494–499` | ujednolicić na `opcja_startowa_obecna` |
| D-13 | informacja | Stała `KERNEL_MAX_END_ALLOWED` (14 MiB) nie jest egzekwowana w linkerze; budżet w praktyce kontroluje się ręcznie. | `pamiec.h:78–91`, `linker.ld` | `ASSERT(__kernel_end <= …)` w `linker.ld` (osobna zmiana) |
| D-14 | niska (budowanie) | Domyślnym celem `Makefile` (wywołanie `make` bez argumentu) jest `virtio_gpu.o`, a nie `all` — pierwsza reguła w pliku nie jest celem `all`. Stwierdzone w Fazie 2 (`make -p`: `.DEFAULT_GOAL := virtio_gpu.o`, identycznie w HEAD). Zgodnie z poleceniem właściciela **nie naprawiane** w Fazach 2–3. | `make -p -n \| grep .DEFAULT_GOAL` | `.DEFAULT_GOAL := all` na początku `Makefile` (osobna, recenzowana zmiana) |

---

## 7. Mapa pamięci i rezerwacje (jak jądro poznaje pamięć)

**[FAKT]** (`pmm.cpp:1426–1620`):

1. PMM bitmapa obejmuje **tylko pierwsze 4 GiB** (`MAKS_ADRES_FIZYCZNY_PMM = 0x1_0000_0000`, `pmm.cpp:59`).
   RAM powyżej 4 GiB jest ignorowany.
2. Za wolne uznaje wyłącznie **pełne strony** z wpisów typu 1 (`MULTIBOOT_MEMORY_AVAILABLE`).
3. Rezerwuje na stałe:
   - 0 … 1 MiB,
   - obraz jądra `__kernel_start … __kernel_end` (dodając go do RAM, nawet jeśli mapa go nie zawiera),
   - całe MBI,
   - framebuffer (`pitch × wysokość`, także wyłączony z HHDM),
   - strony LAPIC `0xFEE00000` i IOAPIC `0xFEC00000`.
4. Rezerwuje moduły w trybie **odblokowywalnym** (`zarezerwuj_zakres_modulu`). Po imporcie konsument może
   zwolnić ramki przez `OdblokujZakres` (np. `galeria_inst_zakoncz`, `kernel.cpp:1455–1458`).
5. Identity map 0..4 GiB (supervisor) jest utrzymywana **na stałe** przez VMM, bo sterowniki używają adresów
   fizycznych MMIO jako wirtualnych (`vmm.cpp:126–131`). Framebuffer jest mapowany osobno pod
   `VADDR_FRAMEBUFFER` (`grafika.cpp:169–199`), więc może leżeć powyżej 4 GiB.

**[WNIOSEK]** Pamięć programu rozruchowego (tablice stron, stos, kod BRB) zgłoszona jako typ 1 zostanie
ponownie użyta przez PMM. Jest to bezpieczne, bo jądro od razu przechodzi na własne struktury. MBI i moduły
**nie mogą** być zgłoszone jako typ 1, a w adapterze MB2 muszą leżeć poza obszarami typu 1 albo zostać
zarezerwowane przez jądro (MBI i moduły są rezerwowane jawnie — warunek spełniony).

---

## 8. Moduły rozruchowe ładowane obecnie przez GRUB

| Etykieta | Plik w ISO | Format | Konsument | Limit / walidacja | Kiedy zwalniany | Wpisy |
|---|---|---|---|---|---|---|
| `tapety.pak` | `/boot/tapety.pak` | pakiet tapet (BTP/BMP) | `tapety.cpp` | `cmp` z `$(TAPETY_PAK)` w buildzie | trwały (rezerwacja modułu) | wszystkie Live |
| `bsf-settings` | `/boot/ustawienia.bur` | aplikacja `.bur` (Ustawienia) | `kernel.cpp:1474` | dokładna etykieta, ≤ 512 KiB | trwały | wszystkie Live |
| `bsf/<plik>` | `/czcionki/{ttf,otf,ttc,otc}/<plik>` | TTF/OTF/TTC/OTC + licencje | `bootstrap.inc` | znaki `[A-Za-z0-9._-]`, ≤ `BSF::MaxFile`, suma ≤ `BSF::MaxLoadedBytes`, liczba ≤ `BSF::MaxFiles` | trwały | wszystkie Live |
| `pulpit/test.bmp`, `pulpit/test1.bmp` | `/boot/pulpit/*.bmp` | BMP | `kernel.cpp:1919–1950` | dokładna etykieta | trwały | wszystkie Live |
| `zdjecia.pak` | `/boot/zdjecia.pak` | kontener BZP3 | `kernel.cpp:1228+`, instalator | ≤ 120 MiB (pakowanie), ≤ 200 MiB (`galeria_pak.h:31`) | po instalacji (`OdblokujZakres`) | gdy `dane/zdjecia` niepuste |
| `muzyka.pak` | `/boot/muzyka.pak` | kontener BZP3 | `kernel.cpp:1337+` | limit pakowania | — | tylko `MUZYKA_W_ISO=1` |
| `test8k.bmp` | `/boot/test8k.bmp` | BMP | `importuj_modul_test8k` | — | — | tylko `BursztynOS_8k.iso` |
| `installer.efi` | `/boot/installer/BOOTX64.EFI` | PE32+ (GRUB standalone) | `instalator.cpp:345` | `MZ`, > 512 B, ≤ 64 MiB | po instalacji | tylko wpis „Instalator” |
| `installer.kernel` | `/boot/system_operacyjny.bin` | ELF64 (to samo jądro) | `instalator.cpp:345` | `\177ELF`, > 64 B, ≤ 64 MiB | po instalacji | tylko wpis „Instalator” |

**[FAKT]** Przykładowa zachowana konfiguracja stagingu (`.bsf-test/resumed-catalog128-safe-fixture/staging/boot/grub/grub.cfg`)
zawiera 130 linii `module2`. **[WNIOSEK]** BRB musi obsłużyć **co najmniej 128 modułów** na wpis;
proponowany limit wynosi 256.

Pliki pomocnicze ISO (`/dane/sponsorze/*.txt`, `/ikonki/przycisk*.png`, `/licenses/*`) **nie są** modułami.
Te same dane są wbudowane w jądro jako bloby (`*_blob.o`) albo mają charakter informacyjny.

---

## 9. Mechanizmy zależne od rozruchu: instalacja, aktualizacja, odzyskiwanie

Szczegóły i projekt integracji: `integracja-instalatora.md`. Fakty kluczowe:

- **Układ dysku** (`storage_format.cpp:139–151`), sektory 512 B:
  - PMBR, GPT (128 wpisów),
  - LBA 2046/2047 — stan rozruchu,
  - ESP FAT32 512 MiB od LBA 2048 (klastry 4 KiB, 2 FAT, etykieta `BURSZTYNESP`, OEM `BURSZTYN`),
  - BSP2 ≤ 2 GiB,
  - reszta nieprzydzielona.
- **ESP** (`storage_format.cpp:396–416`):
  - `\EFI\BOOT\BOOTX64.EFI`,
  - `\BOOT\` z `system_operacyjny.bin` (alias z D-06), `JADRO_A.BIN` i `JADRO_B.BIN`,
  - `\BOOT\GRUB\` z `GRUB.CFG` (D-07) i `GRUBENV` (1024 B).
- **Stan A/B**:
  - autorytatywnie w LBA 2046/2047: `bursztyn_rozruch_stan`, 512 B, CRC32, generacja, SHA-256 jąder obu slotów;
  - kopie w BSP2: `/aktualizacje/stan_rozruchu.k0` i `.k1`;
  - lustro dla GRUB: `grubenv`.
- **Aktualizacja jądra**: zapis do nieaktywnego slotu ESP w miejscu, odczyt kontrolny SHA-256, ustawienie
  „oczekującego” w LBA i `grubenv` (`aktualizacja.cpp:840–870`, `:1015–1045`).
- **Podpisy**: pakiety aktualizacji są weryfikowane RSA-2048 PKCS#1 v1.5 + SHA-256 (mbedTLS,
  `aktualizacja.cpp:203–230`). Sam rozruch nie weryfikuje podpisu jądra. GRUB nie sprawdza SHA-256 ze stanu LBA.
- **Odzyskiwanie**: brak osobnego środowiska odzyskiwania. Jedynym mechanizmem jest automatyczny powrót do
  potwierdzonego slotu (GRUB + jądro).

---

## 10. Podsumowanie audytu

1. Bursztyn OS jest dziś **całkowicie zależny od GRUB** w zakresie nośników, systemów plików, menu, wyboru
   slotu, ładowania ELF i modułów, mapy pamięci, trybu graficznego, RSDP, `ExitBootServices` i przejścia do
   32-bit PM (tabela w sekcji 4).
2. Kontrakt jądra jest wąski i dobrze zdefiniowany: MB2 i386, tagi 1, 3, 6, 8, 12/20, 14/15. Pozwala to
   zastąpić GRUB **bez zmian w jądrze** przez adapter zgodny z MB2 — to rekomendowany pierwszy etap.
3. Zainstalowany system jest wyłącznie UEFI. Instalator wymaga rozruchu UEFI. BIOS dotyczy dziś tylko Live ISO.
4. Stwierdzono 13 słabości (D-01…D-13), z których żadna nie blokuje BRB, a D-01, D-04, D-05 i D-09 muszą być
   uwzględnione w projekcie BRB.
