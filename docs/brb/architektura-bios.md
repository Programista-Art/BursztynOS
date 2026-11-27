# BRB — architektura ścieżki Legacy BIOS (projekt, bez implementacji w Fazie 1)

Status: **[PROJEKT]**. Implementacja należy do Fazy 9 (`plan-rozwoju.md`). Odwołania:

- „El Torito Bootable CD-ROM Format Specification, Version 1.0” (Phoenix/IBM, 1995);
- ECMA-119 (ISO 9660);
- „BIOS Enhanced Disk Drive Specification” (T13, EDD 3.0) — usługi INT 13h AH=41h/42h/48h;
- ACPI Specification 6.5, rozdz. 15 *System Address Map Interfaces* (INT 15h E820h) oraz rozdz. 5.2.5
  *Root System Description Pointer* (wyszukiwanie RSDP na BIOS);
- VESA BIOS Extension (VBE) Core Functions Standard 3.0;
- Intel SDM tom 3A (przejścia trybów).

Numery rozdziałów należy potwierdzić podczas implementacji.

---

## 1. Stan obecny (fakty)

| Zakres | Stan | Źródło |
|---|---|---|
| Live ISO na BIOS | działa przez GRUB i386-pc z `grub-mkrescue` (El Torito + MBR hybrydowy) | `Makefile:1498`; profile BIOS w `runwindows.ps1:1156–1190`, cele `run`, `run-legacy` |
| Instalacja z rozruchu BIOS | **odrzucana** („boot is not UEFI”) | `instalator.cpp:378` |
| Rozruch zainstalowanego dysku z BIOS | **nieobsługiwany**: PMBR bez kodu, brak partycji BIOS Boot | `storage_format.cpp:212`, `:267` |
| Obszar LBA 34…2045 | wolny przy wyrównaniu, ale LBA 2046/2047 to stan rozruchu A/B | `aktualizacja_rozruch.h:33–36`, `docs/aktualizacja-systemu.md` §1.2 |
| Rozmiar sektora instalacji | tylko 512 B | `instalator.cpp:55`, `storage_format.cpp:145` |

**[WNIOSEK]** Celem ścieżki BIOS jest przede wszystkim **Live ISO / nośnik USB**. Rozruch BIOS zainstalowanego
systemu to osobna decyzja właściciela (P-04 w `plan-rozwoju.md`). Wymaga nowego elementu układu dysku, a
istniejących instalacji nie wolno przerabiać.

---

## 2. Architektura wieloetapowa

```
                    ┌──────────────────────── nośnik ────────────────────────┐
 BIOS POST          │                                                        │
   │ CD (El Torito no-emulation)          USB/dysk (MBR LBA 0)               │
   ▼                                                                         │
 Etap 0-CD  (≤ 2 KiB, ładowany przez BIOS)     Etap 0-MBR (≤ 440 B kodu, 0x7C00)
   │ zna LBA i rozmiar etapu 2 z tablicy           │ zna LBA (64-bit) i liczbę sektorów etapu 2
   │ informacji o obrazie (boot info table)        │ wpisane przy budowie/instalacji
   ▼                                               ▼
 Etap 2 „BRB-BIOS” (16/32/64-bit, ≤ 512 KiB, ładowany pod 0x8000…)
   │ 1. walidacja własnego nagłówka (magic, rozmiar, CRC32)
   │ 2. A20, E820, CPUID (LM, PAE, NX), VBE, RSDP
   │ 3. przejście do Long Mode – wspólny kod C++ z BRB-UEFI (ELF, moduły, mapa, BRBI/MB2)
   │ 4. odczyt plików: przez „thunk” do trybu rzeczywistego → INT 13h AH=42h
   │    systemy plików: ISO9660 (CD/USB hybrydowy) albo FAT32 (ESP) – czytniki tylko do odczytu
   ▼
 przekazanie: tryb zgodności MB2 (32-bit PM, jak GRUB) albo BRBI 64-bit
```

Nie próbuje się zmieścić w 512-bajtowym sektorze systemu plików, menu ani ładowania jądra. Etap 0 tylko
ładuje etap 2 i sprawdza jego nagłówek.

### 2.1. Etap 0-MBR (dysk/USB)

| Cecha | Wartość |
|---|---|
| Położenie | LBA 0, bajty 0–439 kodu; 440–445: sygnatura dysku/zarezerwowane; 446–509: tablica partycji (PMBR albo MBR hybrydowy); 510–511: `55 AA` |
| Zadanie | `cli`, stos, `DL` = numer dysku od BIOS, sprawdzenie EDD (AH=41h, `BX=55AAh`), odczyt etapu 2 przez AH=42h (DAP z 64-bit LBA) do 0x0000:0x8000, sprawdzenie magic etapu 2, skok |
| Dane wpisane przy budowie | `lba_etapu2` (u64), `sektory_etapu2` (u16, ≤ 1024 przy 512 B), `rozmiar_sektora` (u16) |
| Błędy | komunikat ASCII przez INT 10h AH=0Eh: `BRB: blad odczytu` / `BRB: brak EDD` / `BRB: zly etap 2`, potem `hlt` |
| Brak EDD (CHS) | **nieobsługiwane** — zbyt stare BIOS-y; komunikat |

Ograniczenie AH=42h: część BIOS-ów obsługuje ≤ 127 sektorów na wywołanie, więc etap 0 czyta porcjami po 64
sektory. **[DO POTWIERDZENIA]** Granica 64 KiB segmentu bufora DAP — porcja nie może przekraczać granicy
segmentu.

### 2.2. Etap 0-CD (El Torito)

- Wpis rozruchowy no-emulation z `Load Segment = 0` (domyślnie 0x07C0) i `Sector Count = 4` (4 × 512 B wirtualne
  = 2 KiB). Wiele BIOS-ów nie ładuje więcej niezawodnie, więc etap 0-CD doczytuje resztę etapu 2 samodzielnie.
- Położenie etapu 2 pochodzi z *boot info table* (xorriso `-boot-info-table`: LBA PVD, LBA obrazu rozruchowego,
  długość, suma — od przesunięcia 8 obrazu) albo z wpisanych przy budowie stałych. **[PROJEKT]** Preferowana
  boot info table, bo nie wymaga łatania po budowie.
- Odczyt: INT 13h AH=42h z numerem napędu z `DL`, sektor 2048 B (rozmiar z AH=48h).

### 2.3. Etap 2 — lokalizacja i walidacja

| Nośnik | Gdzie leży etap 2 | Jak znaleziony |
|---|---|---|
| ISO (CD) | plik `/BRB/BRB_BIOS.BIN` w ISO9660, ciągły | boot info table / LBA wpisane przez narzędzie budowania |
| ISO na USB (hybryda) | ten sam plik; MBR hybrydowy ma etap 0-MBR z LBA w jednostkach 512 B (= LBA ISO × 4) | wpisane przez narzędzie budowania obrazu |
| Zainstalowany dysk (opcjonalnie, P-04) | dedykowana partycja GPT „BRB BIOS” (np. 1 MiB) w nowych instalacjach | wpisane do etapu 0-MBR przy instalacji |

**[PROJEKT]** Etap 2 **nie** jest umieszczany w luce LBA 34–2045. Nie jest formalnie zarezerwowana, sąsiaduje
z autorytatywnym stanem rozruchu LBA 2046/2047 i może być używana przez inne narzędzia. Dla zainstalowanego
systemu wymagana jest jawna partycja GPT. Jej identyfikator typu (GUID) zostanie wygenerowany jako unikalny GUID
Bursztyn OS w Fazie 9. Użycie GUID partycji BIOS Boot GRUB jest możliwe technicznie, ale sugerowałoby
zgodność z GRUB — **[DO POTWIERDZENIA]** decyzja P-04.

Nagłówek etapu 2 (pierwsze 64 B):

```c
typedef struct brb_bios_naglowek_etapu2 {
    uint32_t magic;            /*  0  "BRB2" = 0x32425242                          */
    uint16_t wersja;           /*  4  1                                            */
    uint16_t rozmiar_naglowka; /*  6  64                                           */
    uint32_t rozmiar_obrazu;   /*  8  bajty, <= 512 KiB                            */
    uint32_t crc32_obrazu;     /* 12  CRC-32 obrazu z tym polem = 0                 */
    uint32_t wejscie16;        /* 16  przesunięcie wejścia 16-bit od początku        */
    uint32_t zarezerwowane[11];/* 20..63 */
} brb_bios_naglowek_etapu2;    /* 64 */
```

Etap 0 sprawdza tylko `magic` (brak miejsca). Etap 2 sam liczy CRC32 swojego obrazu przed jakąkolwiek inną
pracą i przy niezgodności zatrzymuje się z komunikatem „Program rozruchowy jest uszkodzony.”

---

## 3. Usługi BIOS w etapie 2

### 3.1. Wywołania BIOS z trybu chronionego/długiego (thunk)

Etap 2 działa głównie w Long Mode (wspólny kod C++ z UEFI). Wywołanie BIOS wygląda tak:

```
long mode → (compatibility mode, PG=0, LME=0) → 32-bit PM → 16-bit PM (segment 16-bit) → real mode (PE=0)
→ IVT BIOS, przerwania włączone tylko na czas wywołania → powrót tą samą drogą
```

- Bufor wymiany i stos real mode leżą poniżej 1 MiB (np. 0x1000–0x7BFF; bufor dyskowy 0x10000–0x1FFFF).
- Dane odczytane z dysku są kopiowane z bufora < 1 MiB do pamięci docelowej po powrocie do Long Mode.
- Przed każdym wejściem do real mode BRB przywraca IDTR real mode (baza 0, limit 0x3FF) i zachowuje
  własną GDT/IDT.
- Liczba wywołań jest ograniczona (pętle odczytu mają limit sektorów wynikający z rozmiaru pliku).

### 3.2. A20

Kolejność (każdy krok kończy się testem zawijania pamięci: porównanie 0x000000 i 0x100000):

1. test — może być już włączona;
2. INT 15h AX=2401h;
3. „Fast A20” (port 0x92, bit 1, bez ustawiania bitu 0 resetu);
4. kontroler klawiatury 8042 (polecenie 0xD1, wartość 0xDF) z ograniczonym oczekiwaniem (limit pętli).

Niepowodzenie → „Nie można włączyć linii A20.” W QEMU/SeaBIOS A20 jest zwykle włączona.

### 3.3. Mapa pamięci E820

- INT 15h EAX=E820h, `EDX = 'SMAP'`, `ECX = 24`, z kontynuacją przez `EBX` do `EBX = 0` albo CF=1.
- Limit 256 wpisów i limit iteracji 512 (ochrona przed zapętleniem BIOS).
- Wpis o `ECX = 20` → atrybuty rozszerzone = 1. Wpis z bitem 0 atrybutów rozszerzonych = 0 jest ignorowany
  (zgodnie z ACPI 3.0+).
- Normalizacja: ta sama funkcja co dla UEFI (sortowanie, nakładania, wycięcia rejestru BRB).
- Brak E820 (bardzo stare BIOS-y) → **nieobsługiwane** (bez E801/88h). Komunikat:
  „BIOS nie udostępnia mapy pamięci E820.”

### 3.4. CPU

- CPUID (EFLAGS.ID), funkcje 0x80000000/0x80000001: LM (EDX[29]), NX (EDX[20]); funkcja 1: PAE (EDX[6]),
  PSE (EDX[3]), MSR (EDX[5]), FXSR (EDX[24]), SSE/SSE2 (EDX[25/26] — wymagane przez `fpu.cpp:12–13`).
- Braki → „Procesor nie spełnia wymagań Bursztyn OS (x86-64 z PAE i NX).”

### 3.5. Przejście trybów i struktury startowe

| Struktura | Położenie | Uwagi |
|---|---|---|
| GDT etapu 2 | w obrazie etapu 2 | null, kod16, dane16, kod32, dane32, kod64, dane64 |
| IDT tymczasowa | w obrazie (Long Mode) | wpisy wyjątków → procedura „BLAD KRYTYCZNY” na COM1 i ekran tekstowy; IF=0 poza thunkiem |
| Tablice stron | 6 stron poza celami jądra | identity 0..4 GiB, strony 2 MiB (jak `boot.S`) |
| Stos | 64 KiB, < 4 GiB | wspólny dla wszystkich trybów poza real mode |

Kolejność: real mode → `lgdt` → CR0.PE → 32-bit PM → PAE + CR3 → EFER.LME → CR0.PG → Long Mode.

### 3.6. Grafika: VBE i tryb tekstowy

- INT 10h AX=4F00h (VbeInfoBlock, `"VBE2"` na wejściu) → lista trybów (segment:offset, limit 256 wpisów).
- AX=4F01h dla każdego: wymagane atrybuty: obsługiwany, graficzny, LFB (bit 7). `BitsPerPixel = 32`,
  `MemoryModel = 6` (direct color), maski R 8@16, G 8@8, B 8@0. Pitch z `LinBytesPerScanLine` (VBE 3.0)
  albo `BytesPerScanLine`.
- Wybór: jak w UEFI (1280×720 → największy ≤ 1920×1080). Ustawienie: AX=4F02h, `BX = tryb | 0x4000` (LFB).
- Rekord framebuffera: `zrodlo = 2 (VBE)`, w trybie MB2 tag 8 jak obecnie od GRUB.
- Tryb tekstowy 80×25 (0xB8000) służy **wyłącznie** menu awaryjnemu i komunikatom BRB. Obecne jądro nie
  działa bez LFB (Z-05), więc brak trybu VBE zgodnego = odmowa startu z komunikatem.

### 3.7. ACPI RSDP na BIOS

Zgodnie z ACPI 6.5 §5.2.5.1: wyszukiwanie sygnatury `"RSD PTR "` na granicach 16 B

1. w pierwszym 1 KiB EBDA (segment z 0x40E × 16),
2. w obszarze 0xE0000–0xFFFFF.

Walidacja sum jak w UEFI. Wynik trafia do rekordu 4 / tagu 14/15 (obecnie robi to GRUB — G9).

### 3.8. 8259A i przerwania

Przed przejściem do Long Mode na stałe: IF=0. Przed przekazaniem: maskowanie 8259A (OCW1 `0xFF` do `0x21` i
`0xA1`). Jądro i tak to robi (`apic.cpp`), ale zamaskowanie przez BRB zamyka okno między ostatnim thunkiem BIOS
a `cli` jądra.

---

## 4. Dostęp do plików

| System plików | Użycie | Zakres implementacji (tylko odczyt) |
|---|---|---|
| ISO 9660 (ECMA-119) | Live: jądro, moduły, konfiguracja | PVD (sektor 16), katalog główny, rekordy katalogów, wyszukiwanie nazw ISO bez Rock Ridge/Joliet. Narzędzie budowania nadaje plikom BRB nazwy zgodne z poziomem ISO wybranym w `xorriso` (P-06). Pliki ciągłe, rozszerzenia multi-extent odrzucane |
| FAT32 | ESP (zainstalowany przy P-04, obraz `efi.img`) | BPB, łańcuchy FAT, katalogi 8.3 + LFN (porównanie bez rozróżniania wielkości liter, tylko ASCII), limit długości łańcucha = rozmiar pliku / rozmiar klastra + 1 |

Ten sam kod czytników jest używany przez BRB-UEFI do odczytu dużych modułów Live z ISO9660 przez
`EFI_BLOCK_IO` (Faza 6). Firmware UEFI zwykle nie udostępnia SFS dla ISO9660.

Rozmiary sektorów: 512 (USB/HDD) i 2048 (CD). Sektory 4096 B (4Kn) — tylko po przetestowaniu
(**[DO POTWIERDZENIA]**). Instalator i tak obsługuje wyłącznie 512 B.

---

## 5. Wspólne przekazanie z UEFI

Po zebraniu danych etap 2 wywołuje ten sam kod budowy MBI/BRBI i ten sam kod przekazania co BRB-UEFI.
Różnice:

| Pole | BIOS | UEFI |
|---|---|---|
| `firmware` | 2 | 1 |
| tagi 12/20 (tryb MB2) | brak (jądro wybiera `SterownikVESA`, jak dziś z GRUB BIOS) | są |
| `BRB_FLAGA_USLUGI_UEFI_ZAKONCZONE` | 0 | 1 |
| framebuffer | VBE | GOP |
| źródło mapy | E820 | GetMemoryMap |

---

## 6. Ograniczenia i ryzyka BIOS

| ID | Ryzyko | Mitygacja |
|---|---|---|
| RB-1 | BIOS-y ładujące tylko część obrazu El Torito no-emulation | etap 0-CD ≤ 2 KiB doczytuje resztę sam |
| RB-2 | INT 13h AH=42h z limitem 127 sektorów / granicą 64 KiB | porcje po 64 sektory, bufor wyrównany |
| RB-3 | BIOS z błędnym E820 (nakładania, zera) | normalizacja i kontrola rozłączności; odrzucenie zerowych długości |
| RB-4 | brak trybu VBE 32 bpp z LFB (starsze karty, niektóre CSM) | komunikat; brak obejścia, dopóki jądro wymaga XRGB8888 |
| RB-5 | CSM wyłączany w nowych komputerach (UEFI Class 3) | BIOS jest ścieżką drugorzędną; UEFI pierwszorzędną |
| RB-6 | USB emulowane przez BIOS jako dyskietka/HDD z różną geometrią | wyłącznie EDD/LBA, brak CHS |
| RB-7 | SMM/firmware nadpisujące pamięć poniżej 1 MiB | bufory BRB tylko w zakresach typu 1 z E820, poza EBDA |
| RB-8 | brak SSE na bardzo starych CPU x86-64 | niemożliwe (SSE2 jest częścią x86-64); i tak sprawdzane |
| RB-9 | luka LBA 34–2045 używana przez narzędzia innych firm | BRB jej nie używa (sekcja 2.3) |
