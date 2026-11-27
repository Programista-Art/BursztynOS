# BRB — protokół rozruchowy (przekazanie sterowania BRB → jądro)

Status: **[PROJEKT]**, wersja protokołu **1.0** (szkic do zatwierdzenia). Fakty o obecnym jądrze pochodzą z
`audyt-rozruchu.md`. W Fazie 1 nie zmieniono ABI wejścia do jądra.

Dokument definiuje dwa mechanizmy:

1. **Tryb zgodności MB2 (przejściowy)** — BRB buduje strukturę Multiboot2 i wchodzi do istniejącego
   `_start` w 32-bitowym trybie chronionym. Jądro pozostaje bez zmian (Fazy 2–6, 8–9).
2. **Protokół natywny BRBI v1 (docelowy)** — wersjonowana struktura `brb_naglowek` i rekordy oraz
   64-bitowe wejście do jądra. Jądro normalizuje oba formaty do jednej wewnętrznej struktury (Faza 7).

---

## 1. Zasady projektowe

| Zasada | Uzasadnienie |
|---|---|
| Liczby całkowite o stałej szerokości, little-endian, naturalne wyrównanie pól | x86-64 jest wyłącznie little-endian; identyczny układ w C i C++, 32- i 64-bit (sprawdzone kompilacją, sekcja 4.9) |
| Wszystkie adresy to **adresy fizyczne** (`uint64_t`) | brak wskaźników wirtualnych firmware, ważnych tylko przed `ExitBootServices` |
| Pamięć **przydzielana i zapisywana przez BRB** dla wczesnego wejścia jądra (obraz jądra, moduły, struktura informacji/MBI, stos, GDT, tablice stron, trampolina) leży **poniżej 4 GiB** | jądro trzyma identity map 0..4 GiB i PMM do 4 GiB (Z-03, Z-09); tag modułu MB2 ma pola 32-bitowe |
| Adresy **opisywane** przez BRB, ale należące do firmware lub sprzętu (framebuffer, wpisy mapy pamięci, RSDP, punkt wejścia SMBIOS, MMIO) mogą legalnie leżeć **powyżej 4 GiB** i są przekazywane jako pełne 64-bitowe adresy fizyczne | jądro mapuje framebuffer osobno pod `VADDR_FRAMEBUFFER` (`grafika.cpp:169–199`); mapa pamięci > 4 GiB jest dziś ignorowana przez PMM, ale nie może być obcinana; BRB nie przenosi ani nie zmienia tych obszarów (korekta Fazy 2) |
| Brak wskaźników do struktur firmware (tablica systemowa UEFI, protokoły, uchwyty) | jądro ich nie używa (Z-12); po `ExitBootServices` usługi rozruchowe nie istnieją |
| Dane kopiowane (RSDP, wiersz poleceń, etykiety), nie wskazywane | odporność na zmianę układu pamięci firmware |
| Rekordy typu TLV o rozmiarze wielokrotności 8 B, nieznane rekordy są pomijane | ewolucja bez łamania starszych jąder |
| Flaga „krytyczny” w typie rekordu | możliwość wprowadzenia rekordu, którego starsze jądro **nie może** zignorować |
| Suma CRC32 całej struktury | wykrywanie uszkodzeń w pamięci i błędów BRB (integralność, nie autentyczność) |
| Jeden limit rozmiaru (1 MiB) i liczby rekordów (1024) | ograniczone pętle po stronie jądra |

---

## 2. Stan procesora przy przekazaniu sterowania

### 2.1. Tryb zgodności MB2 (wejście do istniejącego `_start`)

Zgodny z „Multiboot2 Specification version 2.0”, rozdz. 3.3 *I386 machine state* oraz z wymaganiami
`boot.S` (`audyt-rozruchu.md`, sekcja 5).

| Element | Wartość ustawiana przez BRB |
|---|---|
| Tryb | 32-bit protected mode, CR0.PE=1, **CR0.PG=0**, EFER.LME=0 |
| CS | selektor 32-bit kod, baza 0, limit 4 GiB, DPL 0 (GDT należąca do BRB) |
| DS, ES, FS, GS, SS | selektor 32-bit dane, baza 0, limit 4 GiB |
| EAX | `0x36D76289` |
| EBX | fizyczny adres MBI (wyrównanie 8 B, < 4 GiB) |
| EFLAGS | IF=0, DF=0, VM=0 |
| A20 | włączona (BIOS: BRB; UEFI: zawsze włączona) |
| ESP | wskazuje stos BRB (≥ 16 KiB, wyrównany do 16 B); jądro i tak ustawia własny |
| CR4 | po wyłączeniu stronicowania BRB zeruje CR4 poza bitami wymaganymi przez firmware. MB2 nie wymaga konkretnej wartości; `boot.S:366–368` sam ustawia PSE i PAE |
| IDT | nieokreślona; przerwania i NMI muszą być niemożliwe do obsłużenia — patrz ryzyko R-NMI w `bezpieczenstwo.md` |
| 8259A / LAPIC | BIOS: BRB maskuje 8259A (`0xFF` → porty `0x21`/`0xA1`); UEFI: stan po firmware; jądro i tak maskuje 8259A (`apic.cpp`) |

### 2.2. Protokół natywny BRBI v1 (wejście 64-bitowe)

Jądro deklaruje obsługę protokołu nagłówkiem `brb_naglowek_jadra` (sekcja 5). BRB przekazuje sterowanie
**w Long Mode**, bez schodzenia do 32-bit PM.

| Element | Wartość |
|---|---|
| Tryb | IA-32e 64-bit, CR0.PE=1, CR0.PG=1, **CR0.WP=1**, CR4.PAE=1, CR4.LA57=0 (4 poziomy), CR4.PCIDE=0, EFER.LME=1, EFER.LMA=1 |
| EFER.NXE | 1, jeżeli CPUID 0x80000001 EDX[20]; flaga `BRB_FLAGA_NXE_WLACZONE`. Jądro i tak ustawia NXE samo (`vmm.cpp`) |
| Stronicowanie | identity map **co najmniej** 0..4 GiB (strony 2 MiB, RW, supervisor, bez NX); tablice w obszarze `BRB_PAMIEC_BRB_ODZYSKIWALNA`. Jądro nie zakłada ich położenia i od razu buduje własne |
| CS | 0x08: kod 64-bit (L=1, D=0), DPL 0 |
| DS, ES, SS | 0x10: dane, DPL 0; FS=GS=0, bazy FS/GS = 0 |
| GDT | w obszarze `BRB_PAMIEC_BRB_ODZYSKIWALNA`, ważna do załadowania GDT przez jądro |
| RFLAGS | 0x2 (IF=0, DF=0) |
| RSP | szczyt stosu BRB ≥ 64 KiB w obszarze odzyskiwalnym, **RSP ≡ 0 (mod 16)** w chwili skoku (skok `jmp`, nie `call`) |
| RDI | `BRB_MAGIC_PRZEKAZANIA` = `0x31425242` („BRB1”) |
| RSI | fizyczny adres `brb_naglowek` (wyrównanie 8 B, < 4 GiB) |
| RDX | 0 (zarezerwowane) |
| Pozostałe GPR | 0 |
| x87/SSE | stan nieokreślony; jądro inicjuje FPU (`fpu_initialize`) |
| UEFI | `ExitBootServices` zakończone sukcesem; `SetVirtualAddressMap` **nie** wywołane; flaga `BRB_FLAGA_USLUGI_UEFI_ZAKONCZONE` |
| BIOS | A20 włączona, 8259A zamaskowany, przerwania BIOS nie będą więcej używane |

**Uzasadnienie wejścia 64-bit**: na UEFI procesor już działa w Long Mode. Zejście do 32-bit PM wymaga
trampoliny pod 4 GiB i jest źródłem dodatkowego ryzyka. Na BIOS etap 2 BRB i tak wchodzi w Long Mode, żeby
korzystać ze wspólnego kodu 64-bit.

---

## 3. Własność i czas życia pamięci

| Typ w mapie BRB | Zawartość | Właściciel po przekazaniu | Kiedy jądro może odzyskać |
|---|---|---|---|
| `BRB_PAMIEC_WOLNA` (1) | RAM bez zawartości | jądro (PMM) | od razu |
| `BRB_PAMIEC_ZAREZERWOWANA` (2) | firmware, nieznane | nikt (nie dotykać) | nigdy |
| `BRB_PAMIEC_ACPI_ODZYSKIWALNA` (3) | tabele ACPI | jądro | po skopiowaniu potrzebnych tabel ACPI (dziś: nigdy — jądro czyta tabele w miejscu) |
| `BRB_PAMIEC_ACPI_NVS` (4) | ACPI NVS | firmware | nigdy |
| `BRB_PAMIEC_USZKODZONA` (5) | uszkodzony RAM | — | nigdy |
| `BRB_PAMIEC_FIRMWARE_RUNTIME` (6) | `EfiRuntimeServicesCode/Data`, `EfiPalCode` | firmware | nigdy |
| `BRB_PAMIEC_MMIO` (7) | `EfiMemoryMappedIO*` | urządzenia | nigdy |
| `BRB_PAMIEC_BRB_JADRO` (0x100) | segmenty `PT_LOAD` jądra (z BSS) | jądro | nigdy (obraz jądra) |
| `BRB_PAMIEC_BRB_MODUL` (0x101) | jeden moduł na zakres | jądro | gdy konsument modułu zwolni dane (dziś: `OdblokujZakres`) |
| `BRB_PAMIEC_BRB_INFORMACJE` (0x102) | `brb_naglowek` + rekordy (+ MBI w trybie zgodności) | jądro | po przeniesieniu danych do struktur jądra; dziś MBI rezerwowane na stałe |
| `BRB_PAMIEC_BRB_ODZYSKIWALNA` (0x103) | stos, GDT, tablice stron BRB, kod trampoliny | jądro | po załadowaniu własnej GDT, CR3 i stosu |

Reguły:

1. Wpisy mapy są **posortowane rosnąco**, **rozłączne** i nie mają zerowej długości. Zakresy typów 1 i 0x100–0x103
   są wyrównane do 4 KiB.
2. Każdy moduł, struktura informacji, obraz jądra i obszar odzyskiwalny jest opisany **dokładnie jednym**
   wpisem własnego typu, a nie typem 1.
3. Framebuffer nie musi występować w mapie (UEFI zgłasza go zwykle jako MMIO albo wcale). Jego zakres jest
   w rekordzie framebuffera, a jądro rezerwuje go samo (tak jak dziś).
4. W trybie zgodności MB2 typy 0x100–0x103 są zamieniane na typ 2 (zarezerwowany). Obszar odzyskiwalny jest
   zgłaszany jako typ 1, bo jądro w `boot.S` przed `kernel_main` przechodzi na własny stos i własne tablice
   stron. **[DO POTWIERDZENIA]** w Fazie 4 testem hosta i QEMU: czy `stack_protector_early_init`, wywoływane
   na stosie jądra (`stack_top`), nie dotyka niczego z obszaru BRB.

---

## 4. Format binarny BRBI v1

Wszystkie definicje są **przykładami dokumentacyjnymi**. Docelowe położenie nagłówka:
`rozruch/protokol/brb_protokol.h` (patrz `specyfikacja-techniczna.md`, sekcja 9). Nagłówek kompiluje się bez
ostrzeżeń (`-std=c11 -Wall -Wextra -Wpedantic -ffreestanding`, także `-m32`, oraz `-std=c++17`). Sprawdzono
to w Fazie 1 poza repozytorium.

### 4.1. Stałe

```c
#define BRB_SYGNATURA_INFORMACJI      0x004F464E49425242ULL /* "BRBINFO\0" (LE) */
#define BRB_MAGIC_PRZEKAZANIA         0x31425242U           /* "BRB1" (LE) w EDI/RDI */
#define BRB_PROTOKOL_WERSJA_GLOWNA    1U
#define BRB_PROTOKOL_WERSJA_POBOCZNA  0U
#define BRB_MAKS_ROZMIAR_INFORMACJI   (1024U * 1024U)
#define BRB_MAKS_LICZBA_REKORDOW      1024U
#define BRB_WYROWNANIE_REKORDU        8U

#define BRB_FIRMWARE_UEFI             1U
#define BRB_FIRMWARE_BIOS             2U

#define BRB_TRYB_SYSTEM               1U   /* zwykły rozruch (Live albo zainstalowany) */
#define BRB_TRYB_INSTALATOR           2U
#define BRB_TRYB_AWARYJNY             3U   /* zarezerwowane: wymaga obsługi w jądrze */
#define BRB_TRYB_DIAGNOSTYCZNY        4U   /* zarezerwowane: wymaga obsługi w jądrze */

#define BRB_FLAGA_USLUGI_UEFI_ZAKONCZONE  (1U << 0)
#define BRB_FLAGA_FRAMEBUFFER             (1U << 1)
#define BRB_FLAGA_NXE_WLACZONE            (1U << 2)

#define BRB_REKORD_KONIEC             0U
#define BRB_REKORD_MAPA_PAMIECI       1U
#define BRB_REKORD_MODUL              2U
#define BRB_REKORD_FRAMEBUFFER        3U
#define BRB_REKORD_ACPI_RSDP          4U
#define BRB_REKORD_WIERSZ_POLECEN     5U
#define BRB_REKORD_ZRODLO_ROZRUCHU    6U
#define BRB_REKORD_SLOT_SYSTEMU       7U
#define BRB_REKORD_SMBIOS             8U
#define BRB_REKORD_DIAGNOSTYKA        9U
#define BRB_REKORD_FLAGA_KRYTYCZNY    0x80000000U  /* nieznany typ z tą flagą => odmowa startu */

#define BRB_PAMIEC_WOLNA              1U
#define BRB_PAMIEC_ZAREZERWOWANA      2U
#define BRB_PAMIEC_ACPI_ODZYSKIWALNA  3U
#define BRB_PAMIEC_ACPI_NVS           4U
#define BRB_PAMIEC_USZKODZONA         5U
#define BRB_PAMIEC_FIRMWARE_RUNTIME   6U
#define BRB_PAMIEC_MMIO               7U
#define BRB_PAMIEC_BRB_JADRO          0x100U
#define BRB_PAMIEC_BRB_MODUL          0x101U
#define BRB_PAMIEC_BRB_INFORMACJE     0x102U
#define BRB_PAMIEC_BRB_ODZYSKIWALNA   0x103U
```

### 4.2. Nagłówek (64 B)

```c
typedef struct brb_naglowek {
    uint64_t sygnatura;                /*  0  BRB_SYGNATURA_INFORMACJI                       */
    uint16_t wersja_glowna;            /*  8  =1; inna => jądro odmawia                       */
    uint16_t wersja_poboczna;          /* 10  >=0; większa niż znana => nadal akceptowana     */
    uint32_t rozmiar_naglowka;         /* 12  >= 64; jądro czyta min(znany, podany)          */
    uint32_t rozmiar_calkowity;        /* 16  nagłówek + rekordy, <= 1 MiB                    */
    uint32_t liczba_rekordow;          /* 20  łącznie z rekordem KONIEC, <= 1024              */
    uint32_t flagi;                    /* 24  BRB_FLAGA_*                                     */
    uint32_t crc32;                    /* 28  CRC-32 (IEEE 802.3) całości z tym polem = 0     */
    uint8_t  firmware;                 /* 32  BRB_FIRMWARE_*                                  */
    uint8_t  tryb_rozruchu;            /* 33  BRB_TRYB_*                                      */
    uint16_t zarezerwowane0;           /* 34  0                                               */
    uint32_t wersja_brb;               /* 36  (major<<16)|(minor<<8)|patch                    */
    uint64_t jadro_poczatek_fizyczny;  /* 40  najniższy p_paddr PT_LOAD                       */
    uint64_t jadro_koniec_fizyczny;    /* 48  najwyższy p_paddr+p_memsz (wyłącznie)           */
    uint64_t jadro_wejscie;            /* 56  użyty adres wejścia (fizyczny)                  */
} brb_naglowek;                        /* 64 */
```

Rekordy zaczynają się pod przesunięciem `rozmiar_naglowka` (wyrównanym do 8 B). Polem `crc32` jest CRC-32
o wielomianie `0xEDB88320` (odbity), wartości początkowej `0xFFFFFFFF` i negacji końcowej — tym samym co
`Storage::crc32` (`storage_format.cpp:19`) i `rozruch_oblicz_crc32`.

### 4.3. Rekord ogólny (8 B)

```c
typedef struct brb_rekord {
    uint32_t typ;      /* 0  BRB_REKORD_* [| BRB_REKORD_FLAGA_KRYTYCZNY]            */
    uint32_t rozmiar;  /* 4  z tym nagłówkiem; >= 8; wielokrotność 8                */
} brb_rekord;
```

Ostatnim rekordem jest `KONIEC` o rozmiarze 8, a jego koniec musi równać się `rozmiar_calkowity`.

### 4.4. Mapa pamięci (rekord 1, dokładnie jeden, wymagany)

```c
typedef struct brb_wpis_pamieci {
    uint64_t poczatek;   /*  0 */
    uint64_t dlugosc;    /*  8  > 0, poczatek + dlugosc bez przepełnienia */
    uint32_t typ;        /* 16  BRB_PAMIEC_*                               */
    uint32_t atrybuty;   /* 20  UEFI: bity EFI_MEMORY_UC/WC/WT/WB/RUNTIME (kopiowane 1:1 z młodszych 32 bitów i bitu 63 → bit 31); BIOS: atrybuty rozszerzone E820 */
} brb_wpis_pamieci;      /* 24 */

typedef struct brb_rekord_mapa_pamieci {
    brb_rekord naglowek;     /*  0 */
    uint32_t liczba_wpisow;  /*  8  <= 4096                          */
    uint32_t rozmiar_wpisu;  /* 12  >= 24 (większy = przyszłe pola)  */
    /* brb_wpis_pamieci wpisy[liczba_wpisow] od przesunięcia 16 */
} brb_rekord_mapa_pamieci;  /* 16 */
```

### 4.5. Moduł (rekord 2, 0–256 wystąpień)

```c
typedef struct brb_rekord_modul {
    brb_rekord naglowek;     /*   0 */
    uint64_t poczatek;       /*   8  fizyczny, wyrównany do 4 KiB, < 4 GiB             */
    uint64_t rozmiar;        /*  16  bajty, > 0, <= 256 MiB, koniec <= 4 GiB             */
    uint32_t flagi;          /*  24  bit0: sha256 zweryfikowane przez BRB                */
    uint32_t zarezerwowane;  /*  28  0                                                   */
    uint8_t  sha256[32];     /*  32  skrót zawartości (gdy bit0) albo zera               */
    char     etykieta[64];   /*  64  NUL w granicach; znaki [a-z0-9._/-]; dokładna nazwa */
} brb_rekord_modul;          /* 128 */
```

Etykiety są identyczne z obecnymi (`audyt-rozruchu.md`, sekcja 8), np. `tapety.pak`, `bsf/DejaVuSans.ttf`,
`pulpit/test.bmp`, `installer.kernel`. Najdłuższa obecna etykieta ma około 40 znaków.

### 4.6. Framebuffer (rekord 3, 0 albo 1)

```c
typedef struct brb_rekord_framebuffer {
    brb_rekord naglowek;     /*  0 */
    uint64_t poczatek;       /*  8  fizyczny adres LFB (może być > 4 GiB)            */
    uint64_t rozmiar;        /* 16  >= pitch * wysokosc                               */
    uint32_t szerokosc;      /* 24 */
    uint32_t wysokosc;       /* 28 */
    uint32_t pitch;          /* 32  bajty na linię                                    */
    uint8_t  bpp;            /* 36  v1: tylko 32                                      */
    uint8_t  format;         /* 37  1 = XRGB8888, 2 = XBGR8888, 0xFF = maski poniżej  */
    uint8_t  zrodlo;         /* 38  1 = UEFI GOP, 2 = VBE                             */
    uint8_t  zarezerwowane0; /* 39 */
    uint8_t  pozycja_r, rozmiar_r, pozycja_g, rozmiar_g;   /* 40..43 */
    uint8_t  pozycja_b, rozmiar_b, pozycja_x, rozmiar_x;   /* 44..47 */
} brb_rekord_framebuffer;    /* 48 */
```

Obecne jądro akceptuje wyłącznie XRGB8888 (Z-05). Dopóki to się nie zmieni, BRB **wybiera tylko taki tryb**
i odmawia startu z komunikatem tekstowym, gdy go nie ma (`architektura-uefi.md`, sekcja 7).

### 4.7. ACPI RSDP (rekord 4, 0 albo 1)

```c
typedef struct brb_rekord_acpi_rsdp {
    brb_rekord naglowek;      /*  0 */
    uint64_t adres_fizyczny;  /*  8  oryginalne położenie RSDP                          */
    uint8_t  rewizja;         /* 16  0 = ACPI 1.0 (20 B), >= 2 = ACPI 2.0+ (36 B)       */
    uint8_t  zarezerwowane[3];/* 17 */
    uint32_t dlugosc_kopii;   /* 20  20 albo 36 (z pola Length dla rewizji >= 2, <= 36) */
    uint8_t  kopia[40];       /* 24  kopia RSDP, sumy kontrolne zweryfikowane przez BRB */
} brb_rekord_acpi_rsdp;       /* 64 */
```

### 4.8. Pozostałe rekordy

```c
typedef struct brb_rekord_wiersz_polecen {      /* rekord 5, 0 albo 1 */
    brb_rekord naglowek;     /*  0 */
    uint32_t dlugosc;        /*  8  z NUL, 1..4096; tekst ASCII 0x20..0x7E + NUL */
    uint32_t zarezerwowane;  /* 12 */
    /* char tekst[dlugosc] od przesunięcia 16 */
} brb_rekord_wiersz_polecen; /* 16 */

typedef struct brb_rekord_zrodlo_rozruchu {     /* rekord 6, 0 albo 1 */
    brb_rekord naglowek;        /*  0 */
    uint8_t  rodzaj;            /*  8  1 = nośnik optyczny (ISO), 2 = nośnik wymienny/obraz hybrydowy,
                                       3 = dysk zainstalowanego systemu                              */
    uint8_t  schemat_partycji;  /*  9  0 = brak, 1 = MBR, 2 = GPT                                    */
    uint16_t rozmiar_sektora;   /* 10  512 / 2048 / 4096                                             */
    uint32_t numer_partycji;    /* 12  numer partycji, z której czytano pliki (od 1), 0 = brak       */
    uint8_t  guid_dysku[16];    /* 16  GPT DiskGUID (zera dla MBR/ISO)                               */
    uint8_t  guid_partycji[16]; /* 32  GPT UniquePartitionGUID ESP (zera dla MBR/ISO)                */
    uint64_t lba_partycji;      /* 48 */
    uint64_t sektory_partycji;  /* 56 */
} brb_rekord_zrodlo_rozruchu;   /* 64 */

typedef struct brb_rekord_slot_systemu {        /* rekord 7, 0 albo 1 (tylko system zainstalowany) */
    brb_rekord naglowek;        /*  0 */
    uint8_t  slot;              /*  8  0 = A, 1 = B                                                 */
    uint8_t  powod;             /*  9  1 = potwierdzony, 2 = próba oczekującego,
                                       3 = powrót po nieudanej próbie, 4 = wybór ręczny             */
    uint8_t  licznik_prob;      /* 10  wartość zapisana przez BRB przed startem                      */
    uint8_t  zarezerwowane0;    /* 11 */
    uint32_t zarezerwowane1;    /* 12 */
    uint64_t generacja_stanu;   /* 16  generacja rekordu LBA 2046/2047 po zapisie BRB                */
    uint8_t  sha256_jadra[32];  /* 24  skrót załadowanego jądra (porównany ze stanem rozruchu)       */
} brb_rekord_slot_systemu;      /* 56 */

typedef struct brb_rekord_smbios {              /* rekord 8, opcjonalny */
    brb_rekord naglowek;            /*  0 */
    uint64_t adres_punktu_wejscia;  /*  8  SMBIOS 2.x "_SM_" albo 3.x "_SM3_" */
    uint8_t  wersja_glowna;         /* 16  2 albo 3 */
    uint8_t  zarezerwowane[7];      /* 17 */
} brb_rekord_smbios;                /* 24 */

typedef struct brb_rekord_diagnostyka {         /* rekord 9, opcjonalny */
    brb_rekord naglowek;              /*  0 */
    uint32_t flagi;                   /*  8  bit0: log szeregowy aktywny, bit1: tryb tekstowy (awaria grafiki) */
    uint32_t etapy_zakonczone;        /* 12  maska kamieni milowych BRB (specyfikacja-techniczna.md §11) */
    uint64_t tsc_start;               /* 16  TSC przy wejściu do BRB                                      */
    uint64_t tsc_przekazanie;         /* 24  TSC tuż przed skokiem do jądra                               */
    uint32_t proby_exit_boot_services;/* 32  1 = sukces za pierwszym razem                                 */
    uint32_t zarezerwowane;           /* 36 */
} brb_rekord_diagnostyka;             /* 40 */
```

### 4.9. Asercje rozmiarów i przesunięć

```c
#ifdef __cplusplus
#define BRB_ASSERT static_assert
#else
#define BRB_ASSERT _Static_assert
#endif
BRB_ASSERT(sizeof(brb_naglowek) == 64, "brb_naglowek");
BRB_ASSERT(offsetof(brb_naglowek, crc32) == 28, "crc32");
BRB_ASSERT(offsetof(brb_naglowek, firmware) == 32, "firmware");
BRB_ASSERT(offsetof(brb_naglowek, wersja_brb) == 36, "wersja_brb");
BRB_ASSERT(offsetof(brb_naglowek, jadro_poczatek_fizyczny) == 40, "jadro_poczatek");
BRB_ASSERT(offsetof(brb_naglowek, jadro_wejscie) == 56, "jadro_wejscie");
BRB_ASSERT(sizeof(brb_rekord) == 8, "brb_rekord");
BRB_ASSERT(sizeof(brb_wpis_pamieci) == 24, "brb_wpis_pamieci");
BRB_ASSERT(sizeof(brb_rekord_mapa_pamieci) == 16, "mapa");
BRB_ASSERT(sizeof(brb_rekord_modul) == 128, "modul");
BRB_ASSERT(offsetof(brb_rekord_modul, sha256) == 32, "modul.sha256");
BRB_ASSERT(offsetof(brb_rekord_modul, etykieta) == 64, "modul.etykieta");
BRB_ASSERT(sizeof(brb_rekord_framebuffer) == 48, "framebuffer");
BRB_ASSERT(offsetof(brb_rekord_framebuffer, bpp) == 36, "framebuffer.bpp");
BRB_ASSERT(offsetof(brb_rekord_framebuffer, pozycja_r) == 40, "framebuffer.maski");
BRB_ASSERT(sizeof(brb_rekord_acpi_rsdp) == 64, "acpi");
BRB_ASSERT(offsetof(brb_rekord_acpi_rsdp, kopia) == 24, "acpi.kopia");
BRB_ASSERT(sizeof(brb_rekord_wiersz_polecen) == 16, "cmdline");
BRB_ASSERT(sizeof(brb_rekord_zrodlo_rozruchu) == 64, "zrodlo");
BRB_ASSERT(offsetof(brb_rekord_zrodlo_rozruchu, guid_dysku) == 16, "zrodlo.guid");
BRB_ASSERT(offsetof(brb_rekord_zrodlo_rozruchu, lba_partycji) == 48, "zrodlo.lba");
BRB_ASSERT(sizeof(brb_rekord_slot_systemu) == 56, "slot");
BRB_ASSERT(offsetof(brb_rekord_slot_systemu, generacja_stanu) == 16, "slot.generacja");
BRB_ASSERT(offsetof(brb_rekord_slot_systemu, sha256_jadra) == 24, "slot.sha256");
BRB_ASSERT(sizeof(brb_rekord_smbios) == 24, "smbios");
BRB_ASSERT(sizeof(brb_rekord_diagnostyka) == 40, "diagnostyka");
```

---

## 5. Deklaracja obsługi BRBI w obrazie jądra (Faza 7)

Jądro dostaje drugi, opcjonalny nagłówek w sekcji `.boot`, tuż po nagłówku MB2. Mieści się w limicie
`MULTIBOOT_BOOT_MAX_SIZE = 0x7000` (`linker.ld:74`). Wymaga nowego symbolu wejścia 64-bit, np. `_start_brb64`
w `boot.S`.

```c
typedef struct brb_naglowek_jadra {
    uint64_t sygnatura;          /*  0  "BRBJADRO" = 0x4F5244414A425242 (LE)            */
    uint16_t wersja_glowna;      /*  8  maksymalna obsługiwana wersja główna protokołu  */
    uint16_t wersja_poboczna;    /* 10 */
    uint32_t rozmiar;            /* 12  = 32                                            */
    uint64_t wejscie64;          /* 16  fizyczny adres _start_brb64                     */
    uint32_t wymagane_rekordy;   /* 24  maska bitów typów rekordów wymaganych (1<<typ)  */
    uint32_t suma;               /* 28  CRC-32 pól 0..27                                */
} brb_naglowek_jadra;            /* 32 */
```

BRB szuka tej struktury **tylko** w pierwszych 32 KiB pliku, z wyrównaniem do 8 B, tak jak nagłówka MB2.
Reguła wyboru:

1. Jest `brb_naglowek_jadra` o zgodnej wersji głównej → protokół natywny, wejście `wejscie64`.
2. W przeciwnym razie jest nagłówek MB2 → tryb zgodności, wejście `e_entry` (32-bit).
3. Żadnego → odmowa z komunikatem „Obraz jądra nie jest zgodny z Bursztynowym Programem Rozruchowym.”

Po stronie jądra `_start_brb64` (64-bit) przełącza się na własny stos i własne tablice stron (te same
`pml4_table`… co w `boot.S`, budowane w trybie 64-bit), a potem woła `kernel_main` z nową sygnaturą
rozpoznającą oba formaty. Adapter w jądrze (np. `jadro/rdzen/rozruch_informacje.cpp`) wypełnia jedną
strukturę wewnętrzną `InformacjeRozruchu`. Z niej czytają PMM, grafika, ACPI, instalator, tapety, BSF
i aktualizacja — zamiast dziesięciu pętli po tagach (D-03).

---

## 6. Tryb zgodności Multiboot2 (Fazy 4–6)

BRB buduje MBI zgodne z „Multiboot2 Specification version 2.0”, rozdz. 3.6 *Boot information format*,
w obszarze `BRB_PAMIEC_BRB_INFORMACJE`. Kolejność tagów:

| Tag | Typ | Zawartość | Wymagane przez jądro |
|---|---|---|---|
| nagłówek MBI | — | `total_size`, `reserved = 0` | tak |
| 1 | wiersz poleceń | tekst z konfiguracji wpisu | nie (praktycznie tak) |
| 2 | nazwa programu rozruchowego | `"Bursztynowy Program Rozruchowy <wersja>"` | nie (diagnostyka) |
| 3 | moduły | jeden tag na moduł: `mod_start`, `mod_end` (32-bit, < 4 GiB), etykieta | nie |
| 6 | mapa pamięci | `entry_size = 24`, `entry_version = 0`; typy 1–5 (tabela 6.1) | **tak** |
| 8 | framebuffer | typ 1 (RGB), 32 bpp, maski XRGB8888 | **tak** |
| 12 | EFI 64-bit system table | tylko UEFI; fizyczny adres tablicy systemowej | Z-06 (tylko test obecności) |
| 20 | EFI 64-bit image handle | tylko UEFI; uchwyt obrazu BRB | Z-06 (tylko test obecności) |
| 14 albo 15 | ACPI old/new | kopia RSDP 1.0 (20 B) albo 2.0+ (36 B) | nie |
| 0 | koniec | rozmiar 8 | **tak** (PMM) |

Wyjątek od zasady „brak wskaźników firmware” (tagi 12/20) dotyczy wyłącznie trybu zgodności. Jądro tylko
sprawdza obecność tych tagów (Z-06) i ich nie dereferencjonuje (Z-12). Protokół natywny ich nie ma: wystarcza
pole `firmware`.

### 6.1. Konwersja typów pamięci w trybie zgodności

| Źródło | Typ MB2 |
|---|---|
| UEFI `EfiConventionalMemory`, `EfiBootServicesCode/Data`, `EfiLoaderCode/Data` niezajęte przez BRB | 1 (dostępna) |
| obszar odzyskiwalny BRB (stos, GDT, tablice stron, trampolina) | 1 (jądro nie używa ich po `long_mode_start`; **[DO POTWIERDZENIA]** testem) |
| obraz jądra, moduły, MBI | 2 (jądro i tak rezerwuje je jawnie) |
| `EfiACPIReclaimMemory` | 3 |
| `EfiACPIMemoryNVS` | 4 |
| `EfiUnusableMemory` | 5 |
| `EfiRuntimeServices*`, `EfiReservedMemoryType`, `EfiMemoryMappedIO*`, `EfiPalCode`, `EfiPersistentMemory`, `EfiUnacceptedMemoryType`, nieznane | 2 |
| E820: 1/2/3/4/5 | 1:1; nieznane → 2 |

Sąsiednie wpisy tego samego typu MB2 są scalane. Na koniec mapa jest sortowana i sprawdzana pod kątem
nakładania (nakładające się zakresy różnych typów → wygrywa typ bardziej restrykcyjny: 5 > 4 > 2 > 3 > 1).

---

## 7. Walidacja po stronie jądra (wymagania dla adaptera z Fazy 7)

Kolejność kontroli `brb_naglowek` (każde niepowodzenie → `zatrzymaj_start` z polskim komunikatem):

1. `RDI == BRB_MAGIC_PRZEKAZANIA`, `RSI != 0`, `RSI % 8 == 0`, `RSI + 64 <= 4 GiB`.
2. `sygnatura`, `wersja_glowna == 1`; `rozmiar_naglowka >= 64`, `% 8 == 0`.
3. `rozmiar_naglowka <= rozmiar_calkowity <= BRB_MAKS_ROZMIAR_INFORMACJI`, `RSI + rozmiar_calkowity <= 4 GiB`.
4. CRC-32 zgodne.
5. Iteracja: `off = rozmiar_naglowka`; dopóki `off + 8 <= rozmiar_calkowity` i `licznik < 1024`:
   `rozmiar >= 8`, `rozmiar % 8 == 0`, `rozmiar <= rozmiar_calkowity - off`. Rekord znany, ale krótszy niż
   jego minimalny rozmiar → błąd. Nieznany bez flagi krytycznej → pominięcie. Nieznany z flagą → błąd.
6. Rekord `KONIEC` musi zamykać strukturę dokładnie w `rozmiar_calkowity`; `liczba_rekordow` musi się zgadzać.
7. Liczności: mapa pamięci = 1, framebuffer ≤ 1, RSDP ≤ 1, wiersz poleceń ≤ 1, źródło ≤ 1, slot ≤ 1,
   moduły ≤ 256.
8. Spójność: zakresy jądra, modułów i struktury informacji są rozłączne, każdy jest opisany w mapie pamięci
   właściwym typem i każdy leży poniżej 4 GiB.

---

## 8. Ewolucja protokołu

| Zmiana | Jak | Zgodność |
|---|---|---|
| Nowe pole w nagłówku | dopisanie na końcu, zwiększenie `rozmiar_naglowka` i `wersja_poboczna` | starsze jądro czyta znane 64 B |
| Nowe pole w rekordzie | dopisanie na końcu, większy `rozmiar` rekordu | starsze jądro czyta znany prefiks |
| Nowy rekord opcjonalny | nowy numer typu | starsze jądro pomija |
| Nowy rekord obowiązkowy | nowy numer typu z `BRB_REKORD_FLAGA_KRYTYCZNY` | starsze jądro odmawia startu z komunikatem (zamiast działać błędnie) |
| Zmiana semantyki istniejących pól | nowa `wersja_glowna` | jądro deklaruje w `brb_naglowek_jadra` maksymalną obsługiwaną wersję; BRB może dostarczyć starszą |
| Większa wersja w jądrze niż w BRB | jądro akceptuje każdą `wersja_poboczna` danej wersji głównej | — |

Numery typów rekordów i pamięci są nadawane wyłącznie w `rozruch/protokol/brb_protokol.h`. Każda zmiana
wymaga aktualizacji testów zgodności (`plan-testow.md`, sekcja 3.1).

---

## 9. Przejście od trybu zgodności do protokołu natywnego

| Etap | BRB przekazuje | Jądro | Ryzyko |
|---|---|---|---|
| Fazy 4–6 | MB2 (tryb zgodności), 32-bit | bez zmian | trampolina 64→32 na UEFI |
| Faza 7a | MB2 **albo** BRBI (wg nagłówka w jądrze) | nowe `_start_brb64` + adapter; nadal pełna obsługa MB2 | dwa wejścia do utrzymania |
| Faza 7b | BRBI domyślnie, MB2 jako zapas | parser BRBI jedynym źródłem dla podsystemów; MB2 tłumaczone do `InformacjeRozruchu` | — |
| Faza 15 | tylko BRBI | MB2 może zostać jako ścieżka zgodności (np. dla GRUB w środowisku developerskim) — decyzja właściciela | — |
