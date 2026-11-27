# BRB — plan testów

Status: **[PROJEKT]**. Zasady:

- **kompilacja to nie dowód rozruchu** — sukces przekazania wymaga kryteriów z sekcji 6;
- testy QEMU (OVMF, SeaBIOS) uruchamia się **wyłącznie za jawną zgodą właściciela**, sekwencyjnie, jeden
  proces QEMU naraz;
- testy nigdy nie zapisują istniejących obrazów `.img`/`.iso` ani dysków z danymi — tylko obrazy tymczasowe
  tworzone przez test w katalogu roboczym testu.

---

## 0. Stan po Fazie 2 (zrealizowane)

| Test | Implementacja | Wynik 2026-11-27 |
|---|---|---|
| T-PE (obraz BRB) | `testy/brb_pe_walidator.cpp`: nagłówki DOS/PE/COFF/PE32+, Machine, Subsystem 10, wyrównania, granice i nakładanie sekcji, W^X, nazwy sekcji, punkt wejścia (= RVA `efi_main`), katalogi (brak importów/eksportów/TLS), relokacje (typy, strony, wskaźniki w obrazie, zgodność liczby z `R_X86_64_64`, symulacja przeniesienia), brak tablicy symboli i zbędnych bajtów | PASS |
| T-PE-MUT | ten sam walidator `--mutacje`: 32 mutacje prawdziwego obrazu (m.in. relokacje pozorne, W+X, importy, zły Machine/Subsystem, obcięcie) muszą zostać odrzucone | PASS (32/32 odrzucone) |
| kontrola obiektów | `narzedzia/brb/sprawdz_obiekty_brb.py` (w `make brb-uefi`) | PASS |
| testy hosta Fazy 2 | `testy/test_brb_host.cpp` (ASan+UBSan): GUID-y i ABI, wejście ms_abi i rejestry nieulotne, shadow space i wyrównanie stosu, pamięć, UTF-8, model UART 16550, walidacja tablic, kontrola obrazu, pełne przebiegi z błędami | PASS (9947 sprawdzeń) |
| natywny OVMF | `narzedzia/brb/uruchom_ovmf.py` (`architektura-uefi.md` §15) | PASS (scenariusze `klawisz`, `limit`) |

| T-ELF (Faza 3) | `testy/test_brb_elf.cpp` + `testy/brb_elf_fixture.h`: poprawne obrazy, 60 przypadków błędnych z oczekiwanym kodem, mutacje deterministyczne z niezależnymi niezmiennikami | PASS |
| fuzzing parsera ELF (Faza 3) | `testy/brb_fuzz_elf.cpp`: 200 000 iteracji, stałe ziarno, korpus syntetyczny + jądro produkcyjne | PASS (0 naruszeń, 34 kody) |
| warstwa plików UEFI (Faza 3) | `testy/test_brb_host.cpp`: model SFS/File/AllocatePages, 22 scenariusze błędów + sprzątanie zasobów, jądro produkcyjne przez model | PASS |
| T-ELF-PROD (Faza 3) | `make test-brb-jadro` + `narzedzia/brb/porownaj_plan_readelf.py` | PASS (zgodność pełna z readelf/objdump) |
| natywny OVMF Faza 3 | `narzedzia/brb/przygotuj_woluminy.py` + `uruchom_ovmf.py` (9 scenariuszy) | PASS |

| T-MAP (Faza 4A) | `testy/test_brb_ladowanie.cpp`: walidacja migawki (kroki 40/48/56/512 B, wersja, wielokrotność, przepełnienia, nakładanie, > 4 GiB, typy nieznane/OEM), 2880 mutacji bajtów deskryptorów; `test_brb_host.cpp`: model `GetMemoryMap` (krok 56/64 B, zła wersja, krok 44 B, sukces zapytania o rozmiar, wzrost mapy, limit prób, błąd firmware, brak funkcji, brak pamięci na bufor) | PASS |
| T-ALLOC / ładowanie (Faza 4A) | `test_brb_ladowanie.cpp`: symulowana pamięć ze strażnikami i wzorcem obcym, pełne cykle (syntetyczne, BSS-only, segment niewyrównany, strona wspólna, > 4 GiB, jądro produkcyjne), 17 odrzuceń bez zapisu, 6 uszkodzeń wykrywanych przez weryfikację (dane/BSS/wypełnienie z adresem), regresja dopełnienia struktury planu, fuzz 20 000 iteracji (0 zapisów poza własnością) | PASS |
| transakcja UEFI (Faza 4A) | `test_brb_host.cpp`: odmowa przy 1./2./3. zakresie, zły adres zwrócony, `FreePages` zawodzi po weryfikacji i podczas wycofania, konflikty z obrazem BRB i buforem, plan niezgodny, jądro produkcyjne (wariant wolny: PASS 3183 stron; wariant jak OVMF: E506 + wycofanie) | PASS |
| natywny OVMF Faza 4A | `f4a-produkcja`, `f4a-syntetyczne`, `f4a-konflikt`, 7 regresji Fazy 3, `klawisz`/`limit` (`architektura-uefi.md` §18.9) | jądro produkcyjne **BLOCKED**; pozostałe PASS |

T-EBS: tylko wymagania (`architektura-uefi.md` §18.8) — `ExitBootServices` nie jest wywoływane w Fazie 4A.
Pozostałe testy z tabeli poniżej (T-MB2, T-EBS, …) dotyczą kolejnych faz.

## 1. Testy hosta (ASan + UBSan, `make -j1 test-brb-host`)

Kompilowane jak istniejące testy (`g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined`).
Kod `rozruch/brb/wspolne/` jest testowany przez atrapę interfejsu `Platforma` (pliki z pamięci, alokator
z pulą o zadanym limicie, log do bufora).

| ID | Obszar | Przypadki (minimum) |
|---|---|---|
| T-ELF | ładowarka ELF64 | poprawny ELF (syntetyczny); obcięty w nagłówku / w tablicy PH / w segmencie; zły magic, klasa 32, big-endian, `EM_386`, `EM_AARCH64`, `ET_DYN`, `ET_REL`; `e_phnum` = 0 i 17; `p_filesz > p_memsz`; `p_offset+p_filesz` > plik; `p_paddr+p_memsz` przepełnia u64; segment < 1 MiB; segment > 4 GiB; segmenty nakładające się; nieposortowane; W+X; `PT_INTERP`/`PT_DYNAMIC`; wejście poza segmentem X; wejście w segmencie bez X; brak nagłówka MB2; zła suma MB2; nagłówek MB2 poza 32 KiB; BSS wyzerowane (bufor wypełniony 0xCC przed ładowaniem) |
| T-ELF-PROD | zgodność z prawdziwym jądrem | `system_operacyjny.bin` i `.release-private/*/kernel.elf`: 4 `PT_LOAD`, wejście = `e_entry`, plan ładowania = `__kernel_start…__kernel_end`, wynik identyczny z `readelf -l`; zastępuje `grub-file --is-x86-multiboot2` |
| T-ZAK | rejestr zakresów | rozłączne/nakładające się/sąsiadujące; przepełnienie; 256 wpisów + 1; zakres zerowy; wykluczenie obrazu BRB |
| T-MAP | normalizacja mapy | UEFI: wszystkie typy, nieznany typ, `DescriptorSize` > 40, niewyrównane, `NumberOfPages` przepełniające, nakładania różnych typów (priorytety), sortowanie odwrotne, 4096 wpisów, 0 wpisów; E820: wpisy 20 i 24 B, bit ACPI 3.0 = 0, długość 0, nakładania; wycięcia rejestru BRB (wynik ≤ n + 512); konwersja do MB2 typu 1–5; wynik akceptowany przez **prawdziwy** `InicjalizujPMM` w trybie `BURSZTYN_PMM_HOST_TEST` |
| T-MB2 | budowa MBI | wynik parsowany przez **prawdziwe** funkcje jądra skompilowane na hoście: `przejdz_po_tagach_multiboot`/`InicjalizujPMM` (`pmm.cpp`), `znajdz_modul_mb2`, `zadano_automatyczne_testy` (`kernel.cpp`, wyodrębnione jak w `test_dzwieku_repair.py`), `acpi_inicjalizuj` (pętla tagów), `instalator_boot` (UEFI = tak) |
| T-BRBI | budowa i parser BRBI v1 | asercje rozmiarów; CRC; nieznany rekord pomijany; nieznany krytyczny odrzucany; `rozmiar` 0/7/niewielokrotność 8/za duży; brak `KONIEC`; dwie mapy pamięci; 257 modułów; etykieta bez NUL; `wersja_glowna = 2`; `rozmiar_naglowka` = 72 (przyszłe pola) |
| T-CFG | parser `BRB.CFG` | przykład z dokumentacji; brak `[globalne]`; dwie sekcje globalne; nieznany klucz; duplikat `jadro`; 17 wpisów; 257 modułów; linia 257 B; plik 16 KiB + 1; brak `NUL`/binarne bajty; BOM; CRLF; niezamknięty cudzysłów; `limit_czasu = 31`; `domyslny` spoza zakresu; parametr testowy w konfiguracji dysku zainstalowanego (lista dozwolonych); UTF-8 niepoprawny w `nazwa`; fuzz (sekcja 2) |
| T-MOD | moduły | limity rozmiaru (0, 256 MiB, 256 MiB + 1), sumy, duplikaty etykiet, znaki niedozwolone, moduł opcjonalny uszkodzony (pominięty) vs wymagany (odmowa), SHA-256 niezgodne |
| T-AB | wybór slotu | wszystkie stany `bursztyn_rozruch_stan` z `aktualizacja_rozruch.h`: potwierdzony A/B, oczekujący z `licznik_prob` 0/1, nieudany, obie kopie uszkodzone, jedna kopia uszkodzona, różne generacje, CRC; SHA-256 niezgodne; symulacja przerwanego zapisu (po kopii 0, przed kopią 1); zgodność z `rozruch_potwierdz_sukces` jądra (sekwencja BRB → jądro) |
| T-FAT | czytnik FAT32 | obraz z `fat_install_*` (prawdziwy instalator na hoście, jak `test-storage`); pętla w FAT; klaster poza zakresem; rozmiar pliku > łańcuch; LFN uszkodzone; ścieżka głębokości 9; `BytsPerSec` 1024; obraz obcięty |
| T-ISO | czytnik ISO9660 | obraz z `xorriso` (tymczasowy); `LEN_DR` < 34; rekord przekracza sektor; extent poza woluminem; multi-extent; nazwy `;1`; brak PVD; terminator |
| T-GOP | wybór trybu | listy trybów: tylko BGRX, tylko RGBX (odmowa), BitMask zgodna/niezgodna, BltOnly, pitch < szer×4, przepełnienie `pitch×wys`, preferencja 1280×720, limit 1920×1080 |
| T-RSDP | RSDP | ACPI 1.0/2.0, złe sumy, `Length` 20/36/4096, brak tabeli; skan BIOS (EBDA, 0xE0000) na syntetycznym obrazie pamięci |
| T-EBS | logika `ExitBootServices` | atrapa usług: sukces 1. próba; `EFI_INVALID_PARAMETER` ×3 + sukces; ×4 → błąd; `EFI_BUFFER_TOO_SMALL` w pętli → błąd (bez `AllocatePool`); weryfikacja, że w oknie GetMemoryMap→EBS atrapa nie odnotowała **żadnego** innego wywołania usługi |
| T-TRAMP | plan przeniesień | cel nakłada się na źródło (memmove), cel nakłada się na trampolinę/stos/tablice (odrzucone przed EBS), wiele segmentów, BSS poza buforem |
| T-ALLOC | brak pamięci | atrapa alokatora odmawiająca n-tej alokacji dla n = 1…N: każda ścieżka kończy się kontrolowanym błędem bez wycieku i bez zapisu poza bufory |
| T-PE | obraz BRB-UEFI | parser PE32+ własnego obrazu: maszyna 0x8664, subsystem 10, `.reloc` niepusta, brak W+X, `nm -u` pusty |
| T-BIOS-ETAP0 | etap 0 | rozmiar ≤ 440 B / 2048 B, sygnatura 0x55AA, pola LBA wpisane przez narzędzie, emulacja dekodowania DAP (struktura 16 B, 64-bit LBA) |
| T-BIOS-E820 | E820 | sekwencje odpowiedzi (CF, `EBX` = 0, 512 iteracji, ECX = 20) z atrapy |

Mutacje (jak w `test_dzwieku_repair`): każda kontrola walidacji ma test, który **musi się nie powieść** po
usunięciu kontroli. Weryfikowane ręcznie w recenzji zmian dla T-ELF, T-MAP, T-BRBI i T-CFG.

---

## 2. Fuzzing parserów

- Cele: `elf64_waliduj`, `konfiguracja_parsuj`, `brbi_parsuj` (strona jądra), `fat32_otworz/czytaj`,
  `iso9660_otworz/czytaj`, `mapa_normalizuj`, `stan_ab_wybierz`.
- Narzędzie: wejście w stylu libFuzzer (`LLVMFuzzerTestOneInput`) budowane z GCC jako pętla po korpusie
  i losowych mutacjach (clang/libFuzzer niedostępne na hoście) + ASan/UBSan. Ewentualnie AFL++, jeśli właściciel
  zgodzi się na zależność developerską.
- Korpus startowy: prawdziwe jądro, syntetyczne ELF, przykład `BRB.CFG`, obrazy FAT z instalatora, małe ISO.
- Kryterium: ≥ 10⁶ wykonań na cel bez błędu sanitizera; czas ograniczony w CI.

---

## 3. Testy zgodności z jądrem (host)

| ID | Test |
|---|---|
| T-ZGD-1 | MBI zbudowane przez BRB z syntetycznej mapy UEFI i E820 → `InicjalizujPMM` (host) → PMM zainicjalizowany, liczba wolnych ramek = oczekiwana, moduły/MBI/jądro zajęte (`pmm_diagnostic_page_owner`) |
| T-ZGD-2 | ten sam MBI → `instalator_boot` rozpoznaje `uefi`, `installer.efi`, `installer.kernel` |
| T-ZGD-3 | ten sam MBI → `bsf_import_fonts`-podobna pętla (wyodrębniona) znajduje wszystkie `bsf/*` |
| T-ZGD-4 | ten sam MBI → `zadano_automatyczne_testy` dla każdego parametru z konfiguracji |
| T-ZGD-5 | (Faza 7) BRBI → adapter jądra → `InformacjeRozruchu` identyczne z wynikiem adaptera MB2 dla tych samych danych |

---

## 4. Testy QEMU (tylko za zgodą, sekwencyjnie)

Konfiguracje (zgodne z istniejącymi profilami `Makefile`/`runwindows.ps1`):

| ID | Firmware | Nośnik | Urządzenia | Pamięć | Ekran |
|---|---|---|---|---|---|
| Q-U1 | OVMF (`OVMF_CODE_4M.fd`) | ISO eksperymentalne (`-cdrom`) | VirtIO Full (jak `runuefi`) | 512 MiB, 2 GiB, 4 GiB, 8 GiB | `-vga std`, `virtio-vga` |
| Q-U2 | OVMF | ISO jako dysk USB (`-drive if=none,id=usb…` + `usb-storage`) | jw. | 2 GiB | std |
| Q-U3 | OVMF | ISO, legacy (AHCI/E1000/STD VGA/PS2, jak `runuefi-legacy`) | legacy | 2 GiB | std |
| Q-U4 | OVMF | **tymczasowy** dysk po instalacji z ISO eksperymentalnego | VirtIO-BLK i AHCI | 2 GiB | std |
| Q-U5 | OVMF | Q-U4 + aktualizacja slotu B + awaria testowa (`bursztyn.awaria_slotu=B`) | jw. | 2 GiB | std |
| Q-U6 | OVMF `secboot` | ISO | — | 2 GiB | std | oczekiwany **odmowny** wynik bez podpisu (dokumentacja zachowania) |
| Q-B1 | SeaBIOS (`bios-256k.bin`) | ISO (`-cdrom`) | VirtIO Full | 512 MiB, 2 GiB | std (VBE) |
| Q-B2 | SeaBIOS | ISO jako dysk (`-hda` tymczasowa kopia) | legacy | 2 GiB | std |
| Q-B3 | SeaBIOS | ISO, `-vga cirrus` (brak 32 bpp?) | — | — | oczekiwany komunikat BRB-E040 |

Zbierane: log COM1 (`-serial file:`), zrzut ekranu (jak `guest_session`/narzędzia w `narzedzia/`), kod wyjścia.
Zasoby hosta: jeden QEMU naraz, partie 2–3 testów (zalecenia z wcześniejszych audytów środowiska).

### 4.1. Scenariusze specjalne

- **ExitBootServices**: test z wymuszonym zdarzeniem timera zmieniającym mapę (aplikacja testowa UEFI
  w ramach BRB w trybie diagnostycznym budowy — `BRB_TEST_EBS_RETRY=1`) → oczekiwane ≥ 2 próby w rekordzie
  diagnostycznym i udany start.
- **Pamięć 1–14 MiB zajęta**: konfiguracja OVMF, w której zakres jądra jest `EfiBootServicesData` (sprawdzić
  w logu BRB, czy użyto ścieżki „kopiuj po EBS”). **[DO POTWIERDZENIA]** jak wymusić w OVMF.
- **BIOS etap 0/2**: obraz z etapem 2 na różnych LBA (porcje > 127 sektorów, granica 64 KiB), CD z 2048 B.
- **Uszkodzenia**: obcięte jądro, zły CRC etapu 2, uszkodzona konfiguracja, brak modułu wymaganego, oba sloty
  uszkodzone — oczekiwane polskie komunikaty i brak zawieszenia.

---

### 4.2. Faza R1A — architektura pamięci jądra

| Test | Polecenie | Kryterium |
|---|---|---|
| T-R1A-HOST | `make test-r1a-pamiec-host` | ASan/UBSan, `-Werror`; typy adresów, układ obrazu, plan dolnych 4 GiB (tożsamość = układ sprzed R1A), kontrakt + mutacje (48+ deterministycznych, 200 000 losowych), klasyfikacja mapy, PMM dla 6 baz, rzeczywiste MBI GRUB; 0 FAIL |
| T-R1A-MUT | mutanty kodu produkcyjnego (raport R1A) | każdy mutant daje FAIL > 0 |
| T-R1A-ELF | `readelf`/`nm`/`objdump`, `make test-brb-jadro BRB_JADRO=<kopia>` | ET_EXEC, wejście, VMA == LMA, nagłówek MB2 identyczny, brak zależności, brak nowych ostrzeżeń |
| T-R1A-BIOS | `diag_grub_ovmf.py --firmware bios` + `sprawdz_kamienie_rozruchu.py` | kamienie milowe do pulpitu |
| T-R1A-UEFI | `diag_grub_ovmf.py --wylacz-s3` + kamienie | jw. (układ bezpieczny: obraz na typie 1) |
| T-R1A-NVS (diagnostyczny) | `diag_grub_ovmf.py` z S3 + `sprawdz_kamienie_rozruchu.py --oczekuj-bledu-pamieci` | jądro odmawia startu z kodem `OBRAZ_NA_ACPI_NVS`; **nie** jest testem akceptacji bezpieczeństwa ścieżki GRUB |
| T-R1B-NATYW (przyszły, obowiązkowy) | jądro uruchomione z PA ≠ VA | kamienie milowe do pulpitu, zrzut NVS = 0 zmienionych bajtów — **NIE URUCHOMIONO** (wymaga R1B/R2) |

## 5. Sprzęt fizyczny

Po zgodzie właściciela, na sprzęcie bez danych produkcyjnych (nośnik USB):

| Klasa | Cel |
|---|---|
| UEFI x64, Secure Boot wyłączony | podstawowa zgodność GOP, mapa pamięci, EBS |
| UEFI z GOP `PixelRedGreenBlueReserved` (RGBX) | oczekiwany komunikat BRB-E040 (do czasu obsługi w jądrze) |
| UEFI z framebufferem > 4 GiB | rekord/tag 8 z adresem 64-bit (jądro mapuje osobno — audyt §7) |
| BIOS/CSM (starszy komputer) | ścieżka BIOS, A20, E820, VBE |
| ≥ 2 dyski (Live USB + zainstalowany) | brak pomyłki nośnika (A-05) |

Wcześniejsze audyty (np. `docs/acer-boot-qemu-audit-2026-10-11/`) dostarczają bazę porównawczą zachowania
GRUB na tych samych maszynach.

---

## 6. Kryteria akceptacji przekazania sterowania

Udane przekazanie = **wszystkie** spełnione:

1. COM1: `[BRB] etap 7: przekazanie …` z poprawnym adresem wejścia.
2. COM1: linie jądra w tej samej kolejności co przy GRUB do etapu `framebuffer_ready` (porównanie z logiem
   referencyjnym GRUB z tego samego ISO/konfiguracji).
3. Brak `[GRAFIKA] BLAD KRYTYCZNY`, `zatrzymaj_start`, `boot_error_code ≠ 0` (debugger).
4. PMM: ilość RAM w diagnostyce (`DIAG_MEMORY`) = wartości przy GRUB ± rozmiar zajęty przez BRB (różnice
   wyjaśnione).
5. Live: pulpit i zasoby z modułów (tapety, czcionki BSF, Ustawienia, obrazy pulpitu) dostępne; testy
   automatyczne (`bursztyn.test=automated`) przechodzą jak przy GRUB.
6. Instalator (UEFI): instalacja na tymczasowy dysk kończy się `[INSTALL] progress=100 stage=COMPLETE`, a
   zainstalowany system startuje (Q-U4).
7. A/B: scenariusze Q-U5 dają te same logi `[ROZRUCH]` co przy GRUB.
8. Brak regresji: istniejące testy hosta i QEMU (po zgodzie) bez zmian wyników.
