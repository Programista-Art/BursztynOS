# Bursztynowy Program Rozruchowy (BRB) — specyfikacja techniczna

Wersja dokumentu: 1.0 (Faza 1, szkic do zatwierdzenia). Status: **[PROJEKT]**, oparty na audycie
`audyt-rozruchu.md` (commit `99046a7`, gałąź `feature/bootloader`).

> **BRB nie jest zaimplementowany.** Ten dokument i pozostałe pliki `docs/brb/` opisują projekt.
> Produkcyjny rozruch nadal korzysta z GNU GRUB i pozostanie bez zmian do jawnej decyzji o migracji.

## Spis dokumentów

| Dokument | Zawartość |
|---|---|
| `audyt-rozruchu.md` | stan obecny: inwentarz, sekwencje BIOS/UEFI/Live/zainstalowany, rola GRUB, kontrakt wejścia, audyt Multiboot2, defekty D-01…D-13 |
| `specyfikacja-techniczna.md` (ten) | wymagania, architektura, decyzje, nośniki, moduły, konfiguracja, menu, struktura źródeł, budowanie, diagnostyka |
| `protokol-rozruchowy.md` | ABI przekazania: tryb zgodności MB2 i natywny BRBI v1 (struktury, asercje, stan CPU, ewolucja) |
| `architektura-uefi.md` | aplikacja UEFI: protokoły, ELF, pamięć, GOP, ACPI, `ExitBootServices`, trampolina |
| `architektura-bios.md` | ścieżka BIOS: etapy 0/2, INT 13h, A20, E820, VBE, przejścia trybów |
| `bezpieczenstwo.md` | model zagrożeń, zasady kodu, walidacje, podpisy, Secure Boot |
| `integracja-instalatora.md` | instalator, sloty A/B, migracja z GRUB, aktualizacje BRB, odzyskiwanie |
| `licencje-i-zaleznosci.md` | GRUB w projekcie, obowiązki, zależności BRB |
| `plan-testow.md` | testy hosta, fuzzing, QEMU (OVMF/SeaBIOS), sprzęt, kryteria |
| `plan-rozwoju.md` | Fazy 1–15, ryzyka, decyzje właściciela, plan Fazy 2 |

---

## 1. Wymagania

### 1.1. Wymagania funkcjonalne

| ID | Wymaganie | Źródło |
|---|---|---|
| W-01 | Rozruch Bursztyn OS na x86-64 z UEFI (priorytet) i Legacy BIOS | zadanie |
| W-02 | Obsługa nośników: ISO (CD/DVD), obraz hybrydowy USB, dysk zainstalowanego systemu | zadanie, audyt §3 |
| W-03 | Przekazanie sterowania obecnemu jądru **bez zmian w jądrze** (tryb zgodności MB2) | audyt §5, §10 |
| W-04 | Docelowo własny, wersjonowany protokół BRBI z wejściem 64-bit | zadanie |
| W-05 | Moduły z etykietami identycznymi z obecnymi (≥ 128 na wpis) | audyt §8 |
| W-06 | Zachowanie sesji Live, instalatora, zasobów demonstracyjnych (galeria, muzyka), programów `.bur` i pakietów `.cebula` | zadanie |
| W-07 | Sloty A/B: próba oczekującego, powrót, weryfikacja SHA-256 ze stanu LBA 2046/2047 | audyt §9 |
| W-08 | Menu po polsku (graficzne + tekstowe zapasowe), obsługa bez myszy | zadanie |
| W-09 | Konfiguracja deklaratywna, ograniczona, bez skryptów | zadanie |
| W-10 | Komunikaty dla użytkownika po polsku; identyfikatory techniczne zgodne z konwencjami projektu | zadanie |
| W-11 | Port szeregowy COM1 z kamieniami milowymi | §11 |

### 1.2. Wymagania niefunkcjonalne

| ID | Wymaganie |
|---|---|
| N-01 | Wyłącznie x86-64 (UEFI x64, BIOS na CPU x86-64). Brak ARM, ARM64, RISC-V, IA-32 UEFI |
| N-02 | C, C++ (freestanding, bez wyjątków/RTTI) i asembler GNU as |
| N-03 | Brak zależności od środowiska uruchomieniowego Linuksa; Linux tylko jako host budowania |
| N-04 | Oryginalna implementacja, bez kodu z innych programów rozruchowych (`licencje-i-zaleznosci.md` §4) |
| N-05 | Brak nieograniczonej alokacji, rekurencji i pętli (`bezpieczenstwo.md` §2) |
| N-06 | Każdy etap rozwoju niezależnie testowalny i odwracalny (`plan-rozwoju.md`) |
| N-07 | Produkcyjny `BursztynOS.iso` i GRUB bez zmian do decyzji o migracji |

---

## 2. Decyzje architektoniczne

| ID | Decyzja | Uzasadnienie | Status |
|---|---|---|---|
| A-01 | Najpierw **UEFI**, potem BIOS | zainstalowany system i instalator są wyłącznie UEFI (audyt §3.3); BIOS dotyczy tylko Live | rekomendacja |
| A-02 | Pierwsze przekazanie w **trybie zgodności MB2** (32-bit `_start`), bez zmian w jądrze | weryfikacja BRB na niezmienionym jądrze; mały, odwracalny krok | rekomendacja |
| A-03 | Docelowo **BRBI v1** z wejściem 64-bit i jednym parserem w jądrze | usuwa trampolinę 64→32 na UEFI i 10 parserów MB2 (D-03) | rekomendacja (Faza 7) |
| A-04 | Jądro ładowane jako **ELF64** pod `p_paddr`, nie jako plik płaski ani PE | obecny format (audyt §2); brak zmian w buildzie jądra | fakt + decyzja |
| A-05 | Na UEFI pliki czytane przez **firmware (SFS) z woluminu, z którego uruchomiono BRB**; ISO9660 przez własny czytnik na `BLOCK_IO` | firmware zwykle nie czyta ISO9660; brak wyszukiwania po innych dyskach | rekomendacja |
| A-06 | Pełny stan rozruchu A/B tylko w **LBA 2046/2047** (autorytatywnie); BRB zapisuje jedynie licznik prób | jedno źródło prawdy; minimalne zapisy w programie rozruchowym | rekomendacja |
| A-07 | BRB **nie** czyta BSP2 | zainstalowane jądra są na ESP; BSP2 nie jest potrzebny do rozruchu; brak ryzyka dla formatu BSP2 | rekomendacja |
| A-08 | Wspólny kod C++ (ELF, mapa, moduły, konfiguracja, BRBI/MB2, FAT/ISO9660) dla UEFI i BIOS, różnice tylko w warstwie firmware | jeden kod do testów hosta | rekomendacja |
| A-09 | Wspólne nagłówki ABI w `rozruch/protokol/` (C, tylko `stdint.h`/`stddef.h`) używane przez BRB i jądro | brak zależności cyklicznych; wzorzec `jadro/bws/pcm_api.h` | rekomendacja |
| A-10 | Grafika: tylko XRGB8888 32 bpp, dopóki jądro nie obsługuje innych formatów | Z-05, D-09 | fakt + decyzja |
| A-11 | Menu graficzne **nie** jest wymagane do pierwszego udanego startu | zadanie; ryzyko | decyzja |
| A-12 | Na nośniku Live wszystkie pliki BRB w katalogu `/BRB/` (ISO9660) + obraz `efi.img` z BRB i plikami niezbędnymi do startu | sekcja 6 | rekomendacja |

---

## 3. Architektura komponentów

```
                         ┌───────────────────────── BRB ─────────────────────────┐
 Firmware UEFI ─────────▶│ uefi/: efi_main, pliki (SFS), block_io, gop, pamiec,   │
                         │        exit_boot_services, trampolina64→(32|64)        │
 Firmware BIOS ─▶ etap 0 │ bios/: etap0_mbr, etap0_cd, etap2 (thunk RM, int13,    │
                         │        a20, e820, vbe, rsdp, przejscia trybow)         │
                         │──────────────────── wspólne (C++) ─────────────────────│
                         │ elf64        – walidacja i plan ładowania jądra        │
                         │ zakresy      – rejestr rozłącznych zakresów fizycznych │
                         │ mapa_pamieci – normalizacja (UEFI/E820 → BRB/MB2)      │
                         │ moduly       – lista, limity, SHA-256                  │
                         │ konfiguracja – parser BRB.CFG                          │
                         │ stan_ab      – wybór slotu (aktualizacja_rozruch.h)    │
                         │ informacje   – budowa BRBI v1 i MBI (tryb zgodności)   │
                         │ fs/fat32, fs/iso9660 – czytniki tylko do odczytu       │
                         │ log          – COM1 + konsola; komunikaty PL           │
                         │ menu         – (Faza 11) grafika/tekst, klawiatura     │
                         │ krypto       – crc32, sha256, (Faza 13) podpisy        │
                         └───────────────────────────────┬───────────────────────┘
                                                         │ MB2 (32-bit) / BRBI (64-bit)
                                                         ▼
                                          jądro Bursztyn OS (_start / _start_brb64)
```

Granice: warstwa `wspolne/` nie zna usług firmware. Dostaje interfejs `Platforma` (odczyt pliku, alokacja
pamięci przed przekazaniem, log, czas) implementowany przez `uefi/` i `bios/` **oraz przez atrapę w testach
hosta**.

---

## 4. Przepływ rozruchu z BRB (docelowo)

```
1. Wejście (UEFI: efi_main | BIOS: etap 2)                          [U1/B1]
2. Platforma: konsola, COM1, CPUID (LM, PAE, NX, FXSR, SSE2)
3. Nośnik → konfiguracja BRB.CFG (albo wbudowana)                    [U2/B2]
4. Rodzaj nośnika: zainstalowany (stan LBA ok) / Live
5. Zainstalowany: wybór slotu (integracja-instalatora.md §4), zapis licznika prób
6. Menu (opcjonalne, timeout) → wpis
7. Jądro: odczyt, walidacja ELF, [SHA-256], plan ładowania            [U3/B3]
8. Moduły: odczyt, limity, [SHA-256]                                   [U4/B4]
9. Grafika: GOP/VBE XRGB8888                                           [U5/B5]
10. RSDP, (SMBIOS)
11. Rezerwacje: MBI/BRBI, stos, GDT, tablice stron, trampolina
12. UEFI: GetMemoryMap + ExitBootServices (≤ 4 próby) | BIOS: E820    [U6/B6]
13. Normalizacja mapy, budowa BRBI albo MBI, CRC
14. Trampolina: własne CR3/GDT/stos, przeniesienia, skok               [U7/B7]
```

---

## 5. Odczyt pamięci przez jądro — wymagania stałe

Każda ścieżka BRB musi spełnić warunki **Z-01…Z-14** z `audyt-rozruchu.md` §5.1. W skrócie:

- wszystko < 4 GiB,
- jądro pod `p_paddr` z wyzerowanym BSS,
- MBI/BRBI wyrównane do 8 B i trwałe,
- mapa pamięci zawiera dokładnie jeden wpis dla każdego zakresu,
- framebuffer XRGB8888,
- RSDP skopiowane,
- dokładne etykiety modułów,
- na UEFI w trybie MB2 tagi 12 i 20.

---

## 6. Strategia nośników i systemów plików

### 6.1. Stan obecny (fakty)

| Nośnik | Układ | Narzędzie |
|---|---|---|
| Live ISO | ISO 9660 + El Torito (BIOS: GRUB i386-pc; EFI: obraz FAT z GRUB x86_64-efi) + system area dla USB (MBR hybrydowy) | `grub-mkrescue` → `xorriso` (`Makefile:1498`) |
| Zainstalowany dysk | GPT (512 B): PMBR, ESP FAT32 512 MiB @ LBA 2048, BSP2 ≤ 2 GiB, LBA 2046/2047 stan; jądra na ESP | instalator w jądrze |
| BSP2 | własny system plików systemu (nie zmieniamy formatu) | `jadro/fs/psf.cpp` |

### 6.2. Nośnik Live z BRB (eksperymentalny, Fazy 5–6, 9)

```
BursztynOS-BRB-eksperymentalny.iso (ISO 9660, poziom wybrany w P-06; bez Rock Ridge dla plików BRB)
├─ /BRB/BRB.CFG                         konfiguracja Live (wpisy jak dzisiejszy grub.cfg)
├─ /BRB/BRB_BIOS.BIN                    etap 2 BIOS (Faza 9)
├─ /BRB/MODULY/…                        duże moduły (zdjecia.pak, muzyka.pak) – czytnik ISO9660
├─ /boot/system_operacyjny.bin          jądro (stripped, jak dziś)
├─ /boot/tapety.pak, /boot/pulpit/*, /boot/ustawienia.bur, /czcionki/**   (bez zmian ścieżek)
├─ /licenses/…                          informacje licencyjne
└─ El Torito:
     • wpis BIOS no-emulation → etap 0-CD (2 KiB)           (Faza 9)
     • wpis EFI → efi.img (FAT): \EFI\BOOT\BOOTX64.EFI = BRB, \EFI\BURSZTYN\BRB.CFG
   System area: MBR hybrydowy z etapem 0-MBR (Faza 9) + partycja EFI wskazująca efi.img (UEFI z USB)
```

Strategia odczytu modułów Live na UEFI:

- **Faza 5** (pierwszy udany start): jądro i moduły niezbędne (`tapety.pak`, `bsf-settings`, czcionki, obrazy
  pulpitu) umieszczone **także w `efi.img`**. BRB czyta je przez SFS firmware i nie potrzebuje czytnika
  ISO9660. Rozmiar `efi.img` do zmierzenia (**[DO POTWIERDZENIA]**). Uwaga: pole *Sector Count* wpisu El
  Torito jest 16-bitowe (jednostki 512 B), a duże obrazy EFI (> 32 MiB) wymagają, by firmware korzystało z
  partycji, a nie z licznika. Ryzyko R-07.
- **Faza 6**: czytnik ISO9660 na `EFI_BLOCK_IO` urządzenia nadrzędnego. Wszystkie moduły czytane z ISO, a
  `efi.img` zawiera tylko BRB i konfigurację (mały obraz, brak duplikacji).
- **BIOS** (Faza 9): ten sam czytnik ISO9660 przez INT 13h.

### 6.3. Zainstalowany system

- BRB-UEFI czyta jądra slotów z ESP przez SFS firmware (pliki istnieją dziś — bez zmian układu).
- Moduły: zainstalowany system **nie używa modułów** (Z-13) — bez zmian.
- BSP2: BRB go nie czyta (A-07). Nie wprowadzamy sterownika BSP2 do programu rozruchowego, bo brak
  wymagania. Jeżeli w przyszłości jądra miałyby być na BSP2 (np. `/sloty/x/system/boot`), potrzebny jest
  minimalny czytnik tylko do odczytu — osobna decyzja.
- Synchronizacja z aktualizacjami: jądro zapisuje slot ESP i stan LBA (istniejący mechanizm). BRB tylko czyta
  i weryfikuje SHA-256.

### 6.4. Wspólny układ artefaktów BIOS/UEFI

Ta sama konfiguracja `BRB.CFG` i te same ścieżki plików jądra/modułów na nośniku. Różni się tylko program
wejściowy (`BOOTX64.EFI` vs etap 0/2). Na dysku zainstalowanym ewentualny rozruch BIOS (P-04) czytałby ten
sam ESP (czytnik FAT32 w etapie 2).

### 6.5. Kroki GRUB do zastąpienia w budowaniu

| Dziś | Docelowo |
|---|---|
| `grub-mkrescue -o ISO isodir` | `narzedzia/brb/zbuduj_iso.py`: `xorriso -as mkisofs` z jawnymi opcjami El Torito (BIOS + EFI), boot info table, system area z etapem 0-MBR |
| `grub-mkstandalone … BOOTX64.EFI` | `make brb-uefi` (PE32+ BRB) |
| `grub-file --is-x86-multiboot2` | test hosta ładowarki ELF BRB na `system_operacyjny.bin` (`plan-testow.md` T-ELF-PROD) |
| generacja `grub.cfg` + `module2` przez skrypty | generator `BRB.CFG` z jednej listy modułów (koniec z edycją tekstu regexami w 4 skryptach) |

---

## 7. Moduły rozruchowe

Lista i semantyka bez zmian (`audyt-rozruchu.md` §8). Wymagania BRB:

| Reguła | Wartość |
|---|---|
| Liczba modułów na wpis | ≤ 256 |
| Rozmiar modułu | 1 B … 256 MiB (zgodnie z PMM, D-01) |
| Suma modułów | ≤ 1 GiB i ≤ RAM dostępny < 4 GiB minus jądro i rezerwy |
| Wyrównanie | 4 KiB (początek), alokacja w pełnych stronach |
| Położenie | < 4 GiB, poza jądrem, niską pamięcią (< 1 MiB) i innymi zakresami rejestru |
| Etykieta | 1–63 znaków `[a-z0-9._/-]` plus wielkie litery dla `bsf/` (nazwy plików czcionek) — dokładnie jak dziś; unikalna w wpisie |
| Integralność | SHA-256 z manifestu (gdy obecny); w trybie MB2 skrót nie jest przekazywany jądru |
| Moduły opcjonalne | `zdjecia.pak`, `muzyka.pak`, `test8k.bmp`: przy błędzie pomijane z ostrzeżeniem |
| Moduły wymagane wpisu | wszystkie pozostałe: błąd = odmowa startu wpisu z komunikatem |
| Przejęcie własności | po skoku moduły należą do jądra (typ 0x101/2), zwalnia je konsument (`OdblokujZakres`) |

**[DO POTWIERDZENIA]** Dokładny zestaw dozwolonych znaków etykiet `bsf/` wynika z `bootstrap.inc`
(`[A-Za-z0-9._-]`). BRB przyjmie ten sam zestaw dla `bsf/`.

---

## 8. Konfiguracja `BRB.CFG`

### 8.1. Format

- Tekst UTF-8 bez BOM, ≤ 16 KiB, linie ≤ 256 B, zakończenia `\n` albo `\r\n`.
- Komentarz: `#` do końca linii. Puste linie ignorowane.
- Sekcje: `[globalne]` (dokładnie jedna, pierwsza) i `[wpis]` (1–16).
- Para `klucz = wartość`. Klucze ASCII `[a-z_]`. Wartość: tekst do końca linii bez spacji brzegowych albo w
  cudzysłowach `"…"` (bez sekwencji ucieczki; `"` w wartości niedozwolony).
- Brak zmiennych, warunków, dołączania plików i wykonywania czegokolwiek.

### 8.2. Klucze

| Sekcja | Klucz | Typ / zakres | Wymagany | Powtarzalny |
|---|---|---|---|---|
| globalne | `wersja_formatu` | `1` | tak | nie |
| globalne | `domyslny` | nazwa wpisu albo numer 1–16 | nie (1) | nie |
| globalne | `limit_czasu` | 0–30 s (0 = bez menu, chyba że klawisz) | nie (3, jak dziś) | nie |
| globalne | `grafika` | `SZERxWYS` (z listy obsługiwanej przez jądro: `kernel.cpp:2896`) albo `auto` | nie (`1280x720`) | nie |
| globalne | `tekst_ascii` | `tak`/`nie` | nie (`nie`) | nie |
| globalne | `port_szeregowy` | `tak`/`nie` | nie (`tak`) | nie |
| wpis | `nazwa` | tekst ≤ 64 znaków (UTF-8 PL) | tak | nie |
| wpis | `rodzaj` | `system` \| `instalator` \| `awaryjny` \| `diagnostyczny` \| `uruchom_ponownie` \| `wylacz` | tak | nie |
| wpis | `jadro` | ścieżka (ASCII, `/`, ≤ 128) | dla `system`/`instalator`/`awaryjny`/`diagnostyczny` | nie |
| wpis | `parametry` | tekst ≤ 1024, tokeny `bursztyn.*=…` | nie | nie |
| wpis | `modul` | `ścieżka etykieta [opcjonalny]` | nie | tak (≤ 256) |
| wpis | `slot` | `auto` (A/B wg stanu) — tylko na dysku zainstalowanym | nie | nie |

Reguły walidacji:

- nieznany klucz, duplikat klucza niepowtarzalnego, brak wymaganego, przekroczenie limitu, zła wersja →
  **cała konfiguracja odrzucona**, użyta konfiguracja wbudowana i wyświetlone ostrzeżenie;
- duplikat etykiety modułu w wpisie → odrzucenie;
- dla dysku zainstalowanego `parametry` przechodzą przez **listę dozwolonych** (bez `bursztyn.test*`,
  `bursztyn.install=1`) — R-CFG w `bezpieczenstwo.md`.

### 8.3. Przykład (Live, odpowiednik dzisiejszego `grub.cfg`)

```ini
# Bursztynowy Program Rozruchowy – konfiguracja nośnika Live
[globalne]
wersja_formatu = 1
domyslny = 1
limit_czasu = 3
grafika = 1280x720

[wpis]
nazwa = "Uruchom Bursztyn OS"
rodzaj = system
jadro = /boot/system_operacyjny.bin
parametry = "bursztyn.refresh=60 bursztyn.video=1280x720"
modul = /boot/tapety.pak tapety.pak
modul = /boot/ustawienia.bur bsf-settings
modul = /czcionki/ttf/DejaVuSans.ttf bsf/DejaVuSans.ttf
modul = /boot/pulpit/test.bmp pulpit/test.bmp
modul = /boot/zdjecia.pak zdjecia.pak opcjonalny

[wpis]
nazwa = "Instalator Bursztyn OS"
rodzaj = instalator
jadro = /boot/system_operacyjny.bin
parametry = "bursztyn.install=1"
modul = /boot/tapety.pak tapety.pak
modul = /boot/installer/BOOTX64.EFI installer.efi
modul = /boot/system_operacyjny.bin installer.kernel

[wpis]
nazwa = "Uruchom ponownie"
rodzaj = uruchom_ponownie
```

Wartości `bursztyn.refresh` i `bursztyn.video` pochodzą dziś z `BURSZTYN_REFRESH_HZ`/`BURSZTYN_VIDEO_MODE`
w `Makefile`. Generator konfiguracji przejmie je w ten sam sposób.

---

## 9. Menu rozruchowe

### 9.1. Pozycje — dostępność w zależności od istniejących funkcji

| Pozycja | Dostępność | Uzasadnienie |
|---|---|---|
| Uruchom Bursztyn OS | **tak** | istniejący rozruch |
| Instalator Bursztyn OS | **tak** (tylko Live, tylko UEFI) | `bursztyn.install=1` + moduły; instalator wymaga UEFI |
| Tryb diagnostyczny | **częściowo** | dziś: `bursztyn.splash=0` (bez ekranu powitalnego) + log COM1; pełny tryb wymaga opcji jądra (P-07) |
| Tryb awaryjny | **nie** (do czasu obsługi w jądrze) | brak opcji jądra realizującej tryb awaryjny |
| Odzyskiwanie systemu | **nie** | brak środowiska odzyskiwania; zastępczo: automatyczny powrót A/B i nośnik Live |
| Poprzedni slot systemu | **nie** do czasu zmiany polityki jądra | jądro resetuje przy nieautoryzowanym slocie (Z-14, `integracja-instalatora.md` §4.2) |
| Uruchom ponownie | **tak** | UEFI `ResetSystem(EfiResetCold)`; BIOS: 8042 `0xFE` (jak `aktualizacja.cpp:611`), zapasowo potrójny błąd |
| Wyłącz komputer | **UEFI: tak** (`EfiResetShutdown`); **BIOS: nie** | na BIOS wymaga ACPI S5 z AML (poza zakresem BRB) |
| Testy automatyczne (CI) | tylko w ISO testowych | dzisiejsze wpisy testowe; nie w nośniku użytkownika (P-13) |

### 9.2. Zachowanie

- Domyślny wpis z `domyslny`. Odliczanie `limit_czasu` w sekundach, przerywane dowolnym klawiszem.
- Klawisze: ↑/↓ wybór, Enter start, Esc powrót/zatrzymanie odliczania, `e` — **brak** edycji parametrów w v1
  (bezpieczeństwo, R-CFG), F10 ponowne uruchomienie (opcjonalnie).
- UEFI: `SIMPLE_TEXT_INPUT` (`ReadKeyStroke` + `WaitForEvent` z timerem). Mysz i `SIMPLE_POINTER` nie są
  wymagane w v1.
- BIOS: INT 16h AH=10h/11h z ograniczonym czasem (pętla z `rdtsc` albo zegarem BIOS 0x46C).
- Grafika menu (Faza 11): tło w kolorystyce Bursztyn OS (bursztyn/ciemny), logo z istniejących zasobów
  (`logo_png_blob.o`, jeśli licencja i rozmiar pozwalają — do decyzji), własny font bitmapowy z polskimi
  znakami. Bez kopiowania wyglądu GRUB.
- Zapas tekstowy: ConOut UEFI / tryb 80×25 BIOS, te same pozycje i klawisze.
- Menu **nie** jest warunkiem startu: przy `limit_czasu = 0` i bez klawisza BRB od razu uruchamia wpis domyślny.

---

## 10. Struktura katalogów i budowanie

### 10.1. Katalogi (propozycja)

```
rozruch/
├─ protokol/
│  ├─ brb_protokol.h              ABI BRBI v1 (C, wspólny z jądrem)                    [A-09]
│  └─ brb_multiboot2.h            struktury MB2 używane przez tryb zgodności
├─ brb/
│  ├─ wspolne/                    kod niezależny od firmware (C++ freestanding)
│  │  ├─ elf64.cpp/.h             walidacja i plan ładowania
│  │  ├─ zakresy.cpp/.h           rejestr zakresów
│  │  ├─ mapa_pamieci.cpp/.h      normalizacja
│  │  ├─ moduly.cpp/.h
│  │  ├─ konfiguracja.cpp/.h      parser BRB.CFG
│  │  ├─ stan_ab.cpp/.h           wybór slotu (korzysta z jadro/aktualizacja/aktualizacja_rozruch.h)
│  │  ├─ informacje.cpp/.h        budowa BRBI i MBI
│  │  ├─ krypto/crc32.cpp, sha256.cpp
│  │  ├─ fs/fat32.cpp, iso9660.cpp
│  │  ├─ log.cpp/.h, komunikaty.h (polskie teksty, kody BRB-Exxx)
│  │  ├─ pamiec_podstawowa.cpp    memcpy/memset/memmove/memcmp
│  │  └─ platforma.h              interfejs warstwy firmware
│  ├─ uefi/
│  │  ├─ uefi_typy.h              minimalne deklaracje UEFI pisane ze specyfikacji
│  │  ├─ efi_main.cpp, pliki.cpp, block_io.cpp, gop.cpp, pamiec.cpp, wyjscie.cpp
│  │  ├─ trampolina.S             64→64 (BRBI) i 64→32 (MB2)
│  │  └─ brb_uefi.ld              skrypt linkera (PE32+)
│  ├─ bios/
│  │  ├─ etap0_mbr.S, etap0_cd.S
│  │  ├─ etap2_wejscie.S, thunk_rm.S, przejscia.S
│  │  ├─ int13.cpp, e820.cpp, vbe.cpp, a20.cpp, rsdp.cpp
│  │  └─ brb_bios.ld
│  └─ menu/                       (Faza 11) render, font, klawiatura
narzedzia/brb/                    skrypty budowania (efi.img, ISO, wpisywanie LBA etapu 2)
testy/test_brb_*.cpp              testy hosta (ASan/UBSan)
docs/brb/                         dokumentacja (Faza 1)
```

Zależności:

- `rozruch/brb/**` może dołączać `rozruch/protokol/*.h` oraz nagłówki ABI jądra wolne od zależności
  (`jadro/aktualizacja/aktualizacja_rozruch.h` — samodzielny nagłówek C).
- Jądro dołącza wyłącznie `rozruch/protokol/*.h`.
- Żaden plik jądra nie zależy od `rozruch/brb/`, a BRB nie linkuje żadnego obiektu jądra.

### 10.2. Flagi kompilacji (propozycja)

| Grupa | Flagi |
|---|---|
| wspólne C++ | `-std=c++17 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -nostdlib -fno-builtin -fno-strict-aliasing -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone -mgeneral-regs-only -fno-stack-protector -Wall -Wextra -Wpedantic -Werror -Wvla -Wconversion(parsery)` |
| UEFI | + `-fshort-wchar -mcmodel=small -fno-pic` (metoda A) albo `-fpie` (metoda B); `ms_abi` dla usług |
| BIOS etap 2 | 64-bit jak wyżej; 32-bit/16-bit kod tylko w `.S` (`.code16`, `.code32`); link `-m elf_x86_64` z adresem bazowym 0x8000, następnie `objcopy -O binary` |
| BIOS etap 0 | `as --32` (`.code16`), link `-m elf_i386 -Ttext 0x7C00`, `objcopy -O binary`, kontrola rozmiaru ≤ 440 B (MBR) / ≤ 2048 B (CD) |
| testy hosta | `g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined` (jak istniejące testy) |

Funkcje generowane przez kompilator: `memcpy/memset/memmove/memcmp` dostarcza BRB. Kontrola w buildzie:
`nm -u` pusty (UEFI) i brak sekcji W+X (`objdump -h`).

### 10.3. Cele `Makefile` (propozycja, Faza 2+)

| Cel | Wynik | Uwagi |
|---|---|---|
| `brb-uefi` | `.brb-build/uefi/brb-x64.efi` | nie zależy od `iso` |
| `brb-bios` | `.brb-build/bios/etap0_mbr.bin`, `etap0_cd.bin`, `brb_bios.bin` | Faza 9 |
| `test-brb-host` | testy hosta wszystkich parserów | ASan/UBSan, część `make -j1` |
| `iso-brb` | `BursztynOS-BRB-eksperymentalny.iso` | **nigdy** nie nadpisuje `BursztynOS.iso`; osobny katalog stagingu `.isodir_brb/` |
| `run-brb-uefi`, `run-brb-bios` | QEMU z OVMF/SeaBIOS | **tylko za jawną zgodą właściciela** |

Produkcyjne `iso`, `$(GRUB_CFG)`, `.installer-build/BOOTX64.EFI` pozostają **bez zmian** do decyzji migracji
(N-07).

---

## 11. Diagnostyka i kamienie milowe

Port COM1 (0x3F8, 38400 8N1, ten sam co w jądrze — `kernel.cpp:354–362`), linie ASCII z prefiksem `[BRB]`.

| Bit `etapy_zakonczone` | UEFI | BIOS |
|---|---|---|
| 0 | U1 wejście `efi_main` | B0 etap 0 (kropka na INT 10h; brak COM1 w 440 B) |
| 1 | U2 nośnik i konfiguracja | B1 wejście etapu 2, CRC OK |
| 2 | U3 jądro załadowane i zweryfikowane | B2 A20, E820, CPUID |
| 3 | U4 moduły | B3 jądro |
| 4 | U5 grafika | B4 moduły |
| 5 | U6 `ExitBootServices` | B5 VBE |
| 6 | U7 przekazanie | B6 przekazanie |

**Kryterium udanego przekazania**: linia `[BRB] etap 7: przekazanie …` **oraz** pierwsze linie logu jądra
(`serial_inicjalizuj`, `[HAL] Aktywacja sterownika …`). Sam brak błędu BRB nie jest dowodem startu.

### 11.1. Kody i komunikaty błędów (przykłady)

> **Korekta (Faza 3):** kody dotyczące pliku jądra i ELF mają schemat `BRB-E2nn` (numer `BladPliku`) oraz
> `BRB-E1nn` (numer `BladElf`) — pełna tabela w `architektura-uefi.md` §17.7. Przykłady E010–E012 poniżej
> są nieaktualne; pozostałe kody (grafika, CPU, EBS, BIOS) zostaną nadane w swoich fazach.

| Kod | Komunikat dla użytkownika |
|---|---|
| BRB-E001 | Nie można odczytać nośnika, z którego uruchomiono program rozruchowy. |
| BRB-E002 | Plik konfiguracji jest uszkodzony — używam ustawień domyślnych. |
| BRB-E010 | Nie znaleziono pliku jądra. |
| BRB-E011 | Plik jądra jest uszkodzony lub nie jest obrazem x86-64. |
| BRB-E012 | Suma kontrolna jądra jest niezgodna. |
| BRB-E020 | Moduł jest uszkodzony lub za duży. |
| BRB-E030 | Za mało pamięci, aby załadować system. |
| BRB-E031 | Pamięć wymagana przez jądro jest zajęta przez firmware. |
| BRB-E040 | Nie znaleziono trybu graficznego zgodnego z Bursztyn OS. |
| BRB-E050 | Procesor nie spełnia wymagań Bursztyn OS (x86-64 z PAE i NX). |
| BRB-E060 | Nie udało się zakończyć usług rozruchowych UEFI. |
| BRB-E070 | Nie można włączyć linii A20. |
| BRB-E071 | BIOS nie udostępnia mapy pamięci E820. |
| BRB-E080 | Program rozruchowy jest uszkodzony. |

---

## 12. Pytania otwarte i założenia

Pełna lista decyzji właściciela (P-01…P-13) i ryzyk: `plan-rozwoju.md`. Założenia przyjęte w tym dokumencie:

1. Jądro pozostanie ELF64 pod 1 MiB (A-04). Zmiana adresu ładowania wymagałaby zmian w jądrze.
2. PMM pozostanie ograniczony do 4 GiB. Rekord mapy BRB przekazuje także pamięć > 4 GiB (przydatne w
   przyszłości), ale BRB nie umieszcza tam niczego.
3. Instalacja pozostaje UEFI-only, chyba że P-04.
4. Format BSP2 i układ partycji istniejących instalacji pozostają bez zmian.
