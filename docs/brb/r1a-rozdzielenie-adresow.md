# Faza R1A — rozdzielenie adresów fizycznych i wirtualnych jądra

| | |
|---|---|
| Status | **ZREALIZOWANA JAKO FUNDAMENT — czeka na przegląd właściciela.** Fizyczna relokacja jądra **nie** jest zakończona (wymaga R1B i R2) |
| Data | 2026-11-27 |
| Decyzja nadrzędna | `decyzja-relokacja-jadra.md` — opcja **C + E ZAAKCEPTOWANA** |
| Gałąź / bazowy commit | `feature/bootloader`, `e24b9713d4543b680582da221f9bffb1c7663fa0` |
| Kod | `jadro/mm/adresy.h`, `jadro/mm/obraz_jadra.h`, `jadro/mm/kontrakt_pamieci.h` (nowe); `jadro/mm/pmm.cpp`, `jadro/mm/vmm.cpp`, `jadro/mm/pamiec.h`, `jadro/rdzen/kernel.cpp` (zmienione) |
| Testy | `make test-r1a-pamiec-host` (`testy/test_r1a_pamiec_host.cpp`) |

Oznaczenia: **[WYMÓG ZEWN.]** — wymaganie ze specyfikacji zewnętrznej; **[DECYZJA]** — wewnętrzna decyzja
projektowa Bursztyn OS; **[OBSERWACJA]** — zmierzone; **[NIEZNANE]** — niezmierzone.

---

## 1. Cel i granice

R1A rozdziela w menedżerach pamięci pojęcia adresu fizycznego i wirtualnego. Dzięki temu jądro **może** w przyszłości
działać z obrazem pod dowolnym adresem fizycznym przy niezmienionych adresach wirtualnych. R1A **nie** zmienia
ścieżki wejścia: jedynym działającym wejściem pozostaje Multiboot2 → `boot.S` (PA == VA), więc w produkcji obraz
jest nadal tożsamościowy.

Zakres R1A:
- jawne typy adresów i sprawdzana arytmetyka (§3);
- układ obrazu i reguły tłumaczenia (§4);
- wersjonowany, wewnętrzny kontrakt pamięci rozruchu (§5);
- PMM rezerwujący rzeczywisty podkład fizyczny (§6);
- VMM z fizycznymi adresami tablic i oknem obrazu (§7);
- izolacja Ring 3 (§8), audyt DMA (§9), rewalidacja zależności adresowych (§10);
- testy hosta (§13).

Poza zakresem: wejście 64-bit, przekazanie z BRB, `ExitBootServices`, relokacje ELF, `ET_DYN`, wyższa połowa,
KASLR, SMP, zmiana `RING3_BASE` i formatu BUR, instalator, ISO, GRUB, OVMF, `docs/brb/brb.zip`.

## 2. Zaakceptowana architektura (skrót)

**[DECYZJA]** Opcja C + E: jądro fizycznie ruchome przy **stałych** adresach wirtualnych. Mały bootstrap (BRB,
R1B/R2) przydziela region przez firmware, buduje tablice stron i przekazuje jawny kontrakt. Obowiązujące
ograniczenia:
1. VA jądra stałe przez całą migrację (okno obrazu od 1 MiB z `linker.ld`).
2. Położenie fizyczne niezależne od VA.
3. Układ VA Ring 3 bez zmian (`RING3_BASE` = 16 MiB, sterta 32 GiB, SHM 36 GiB, stos `0x7FFFF0000000`).
4. Zgodność aplikacji BUR v1/v2.
5. Bez przetwarzania relokacji ELF.
6. Wyższa połowa, KASLR i SMP odroczone.
7. GRUB dostępny w okresie migracji, ale jego obecne zachowanie UEFI (nadpisanie ACPI NVS) **nie** jest
   akceptowalne jako rozwiązanie trwałe (§12).

## 3. Model adresów (`jadro/mm/adresy.h`)

| Typ | Znaczenie | Gdzie trafia |
|---|---|---|
| `AdresFizyczny` | adres w przestrzeni fizycznej | PTE, CR3, rejestry DMA urządzeń |
| `AdresWirtualny` | adres tłumaczony przez MMU | wskaźniki C++ |
| `NumerRamki` | PA / 4 KiB | bitmapy PMM |
| `OffsetObrazu` | odległość od początku obrazu, wspólna dla VA i PA | tłumaczenie okna obrazu |
| `LiczbaStron`, `RozmiarBajtow` | rozmiary | przeliczenia stron |

Zasady:
- **[DECYZJA]** Typy silne: jawne konstruktory, brak niejawnych konwersji między rodzajami adresów i do `uint64_t`.
  Sprawdzają to asercje `std::is_convertible` w teście.
- Arytmetyka sprawdzana: przepełnienie, niedomiar, brak wyrównania i wyjście poza zakres zwracają `false`
  i nie zapisują wyniku.
- Pełna reprezentacja 64-bitowa. **[WYMÓG ZEWN.]** Architektoniczny limit PA to 52 bity (MAXPHYADDR ≤ 52;
  Intel SDM t. 3A §4.1.4, AMD APM t. 2 §5.3.1).
- **[DECYZJA]** Nie ma uniwersalnej konwersji VA ↔ PA:
  - obraz jądra tłumaczy wyłącznie `obraz_jadra.h`;
  - pamięć dynamiczną tłumaczy wyłącznie Direct-Map PMM (`FizycznyZakresDoWirtualnego` / `WirtualnyDoFizycznego`
    dla zakresów zweryfikowanych w PMM).
- Koszt czasu wykonania: zero (struktury z jednym polem `uint64_t`, funkcje `constexpr inline`).

**Tymczasowe ograniczenie implementacji (nie architektury x86-64):** podkład obrazu musi leżeć w `[1 MiB, 4 GiB)`.
Powód: PMM śledzi 4 GiB, a Direct-Map obejmuje 4 GiB, przez który VMM czyta statyczne tablice stron obrazu.
Walidator zgłasza wtedy osobny kod `PA_POWYZEJ_4GIB_TYMCZASOWE`.

## 4. Układ obrazu i reguły tłumaczenia (`jadro/mm/obraz_jadra.h`)

**[DECYZJA]** Model podkładu w R1A: **jeden ciągły region fizyczny** `[pa, pa + rozmiar)`, w którym zachowane są
względne przesunięcia segmentów ELF (PA = `baza_fizyczna` + (VA − `baza_wirtualna`)). Model jest zgodny
z audytem: obraz to 4 segmenty PT_LOAD w jednym oknie, bez relokacji.

`Uklad` opisuje okno VA `[va_poczatek, va_koniec)`, podkład PA i do 8 segmentów. Każdy segment ma:
- `va`;
- `rozmiar` — rozpiętość stron w oknie;
- `zajete` — bajty faktycznie należące do segmentu (`p_memsz` z BSS);
- uprawnienia R/W/X.

`zbuduj_uklad()` odrzuca każdy z poniższych przypadków własnym kodem błędu:
- brak wyrównania do 4 KiB, rozmiar 0 i przepełnienie;
- okno poza `[1 MiB, 4 GiB)` i PA poza 52 bitami;
- PA powyżej 4 GiB (ograniczenie tymczasowe) i PA poniżej 1 MiB;
- segmenty z dziurą, zachodzące albo spoza okna;
- `zajete` równe 0 albo większe niż `rozmiar`;
- segment bez prawa odczytu albo zapisywalny i wykonywalny jednocześnie (W^X **deklaracji**).

Reguły tłumaczenia:
| Funkcja | Przyjmuje wyłącznie | Poza zakresem |
|---|---|---|
| `va_na_pa`, `zakres_va_na_pa` | VA (cały zakres) w oknie obrazu | `false` |
| `pa_na_va` | PA w podkładzie obrazu | `false` |
| `offset_va` | VA w oknie | `false` |
| `podklad_przecina`, `okno_przecina` | dowolne; układ niezainstalowany = „przecina” (fail-closed) | — |
| `bajty_segmentow_przecinaja` | dowolne; sprawdza bajty `zajete`, nie luz stron | — |

Adresy HHDM (`0xFFFF8…`), adres 0, adres tuż przed i tuż za oknem nie są tłumaczone, co sprawdza test B.

## 5. Kontrakt pamięci rozruchu v1 (`jadro/mm/kontrakt_pamieci.h`)

### 5.1. Status i relacja do Fazy 7

**[DECYZJA]** `KontraktV1` to struktura **wewnętrzna** jądra, a **nie** publiczne ABI przekazania sterowania.
- Buduje ją wewnątrz jądra adapter źródła rozruchu; dziś jedynym adapterem jest **tymczasowy adapter zgodności
  Multiboot2** (`zbuduj_z_multiboot2`).
- Natywny protokół BRBI v1 i adapter `InformacjeRozruchu` (`protokol-rozruchowy.md`, `plan-rozwoju.md` Faza 7)
  **nie** są wprowadzane ani przesądzane.
- Typy rodzajów i czasów życia zakresów odpowiadają tabeli własności z `protokol-rozruchowy.md` §3 (np.
  `BRB_PAMIEC_BRB_JADRO`, `…_MODUL`, `…_INFORMACJE`, `…_ODZYSKIWALNA`). Przyszły adapter BRBI może więc wypełnić
  ten sam kontrakt bez zmian w PMM/VMM.
- Źródło `ZRODLO_BRB_NATYWNE_PRZYSZLE` (2) jest zarezerwowane. Walidator akceptuje jego strukturę (testy
  nie-tożsamościowe), ale w jądrze nie istnieje ścieżka wejścia, która by je tworzyła.

### 5.2. Układ (2408 B, `static_assert` każdego przesunięcia)

| Off. | Pole | Opis |
|---|---|---|
| 0 | `sygnatura` | `0x4B504D42` („BMPK”) |
| 4 | `wersja`, `zarezerwowane0` | 1, 0 |
| 8 | `rozmiar` | `sizeof(KontraktV1)` = 2408 |
| 12 | `zrodlo` | 1 = MB2 (zgodność), 2 = BRB (zarezerwowane) |
| 16 | `flagi` | bit 0: tożsamość 0..4 GiB przed VMM; bit 1: CR3 w obrazie |
| 20 | `model_podkladu` | 1 = ciągły |
| 24 / 32 / 40 | `jadro_baza_wirtualna` / `_fizyczna` / `jadro_rozmiar` | okno i podkład |
| 48 | `cr3_rozruchu` | PA aktywnego PML4 przy wejściu (0 = nieznany) |
| 56 / 64 / 72 / 76 | mapa pamięci: adres, rozmiar, format (1 = tag MB2 6; 2 = EFI, zarezerwowany), rozmiar wpisu | |
| 80 / 88 | struktura informacji rozruchu (MBI): adres, rozmiar | |
| 96 / 100 | liczba segmentów / zakresów | ≤ 8 / ≤ 64 |
| 104 | `segmenty[8]` | po 32 B: `va`, `rozmiar`, `zajete`, `uprawnienia` |
| 360 | `zakresy[64]` | po 32 B: `poczatek`, `dlugosc`, `rodzaj`, `czas_zycia`, `flagi` |

Rodzaje zakresów i czas życia:
| Rodzaj | Czas życia | Obsługa w PMM |
|---|---|---|
| `OBRAZ_JADRA` | trwały | Direct-Map bez statusu „dostępny” + stała rezerwacja; musi leżeć na typie 1 |
| `TABLICE_STRON_ROZRUCHU` | trwały / do przełączenia CR3 | stała rezerwacja (R1A); musi leżeć na typie 1 |
| `INFORMACJE_ROZRUCHU` (MBI) | trwały | stała rezerwacja (konsumenci czytają tagi do końca startu) |
| `MODUL` | do zwolnienia przez konsumenta | zajęty z zachowaniem bitu dostępności; `OdblokujZakres` po imporcie |
| `FRAMEBUFFER` | trwały, obowiązkowo `BEZ_DIRECT_MAP` | stała rezerwacja + wyłączenie z HHDM |
| `ODZYSKIWALNY_ROZRUCHU` | do przełączenia CR3 | stała rezerwacja (R1A; odzyskiwanie w R1B) |

**Podkład:** jeden ciągły region (§4). Obraz opisuje dokładnie jeden zakres `OBRAZ_JADRA`, równy podkładowi.

### 5.3. Walidacja (`waliduj`)

Walidator kopiuje kontrakt do zmiennej lokalnej, więc działa także dla bufora niewyrównanego. Kontrole idą
w tej kolejności:
1. Obcięcie i brak danych.
2. Sygnatura, wersja, rozmiar.
3. Pola zarezerwowane i nieużyte wpisy muszą być wyzerowane.
4. Źródło, flagi, model podkładu.
5. Układ obrazu (§4).
6. Zgodność z układem z **symboli linkera**: VA, rozmiar, segmenty, `zajete`, uprawnienia.
7. Źródło MB2 musi być tożsamościowe (`boot.S` jest zależny od położenia).
8. Mapa: format, wyrównanie, rozmiar, podzielność przez rozmiar wpisu; dla MB2 mapa leży wewnątrz MBI.
9. Zakresy: poprawność, dozwolone czasy życia, framebuffer bez Direct-Map, dokładnie jeden obraz i jedna
   struktura informacji.
10. **Podwójna własność**: nakładanie zakresów jest błędem. Wyjątek (wyłącznie MB2): zasób może leżeć w luzie
    stron obrazu poza bajtami segmentów. **[WYMÓG ZEWN.]** Multiboot2 wiąże program rozruchowy tylko
    z `p_memsz`. **[OBSERWACJA]** GRUB 2.12 na BIOS kładzie MBI pod `0x100080`, za 0x50-bajtowym segmentem
    `.boot`. PMM i tak rezerwuje strony obrazu na stałe.
11. Obraz nie-tożsamościowy: zasób czytany przez PA-jako-wskaźnik nie może leżeć fizycznie pod VA okna obrazu
    (`ZASOB_W_CIENIU_OKNA`).
12. CR3: wyrównanie i położenie w obrazie albo w zakresie `TABLICE_STRON_ROZRUCHU`; flaga musi się zgadzać.

### 5.4. Adapter zgodności Multiboot2

Adapter (`zbuduj_z_multiboot2`):
- **Nie zgaduje bazy.** Każdą stronę okna obrazu tłumaczy przez **aktywne tablice stron**: w jądrze przejście
  od CR3 tablic `boot.S`; tablice są czytane przez tożsamość i tylko poniżej 4 GiB. Wymaga ciągłego PA. Gdy
  przejście się nie powiedzie, zwraca `TLUMACZENIE_OBRAZU`, a dla nieciągłego podkładu `PODKLAD_NIECIAGLY`.
- Tag 21 (adres bazowy obrazu), jeśli obecny, musi równać się zmierzonej bazie (`MB2_TAG21_NIEZGODNY`).
  **[OBSERWACJA]** GRUB 2.12 podaje `0x100000`.
- Tagi waliduje według tych samych reguł co dotychczasowe PMM. Wymaga dokładnie jednej mapy (tag 6) i co
  najwyżej jednego framebuffera.
- Reguły pomijania modułów i framebuffera są **identyczne** z dotychczasowym PMM: moduł z `start == 0`,
  odwrócony albo > 256 MiB jest pomijany. Słabość D-01 z audytu pozostaje do Fazy 7.

## 6. Model własności PMM (`jadro/mm/pmm.cpp`)

`InicjalizujPMM(mbi)` jest teraz cienkim wejściem zgodności: buduje kontrakt adapterem MB2 i wywołuje
`InicjalizujPMMZKontraktem()`. Etapy:
1. Ponowna walidacja kontraktu (obrona w głąb), także względem symboli linkera.
2. Mapa: wyłącznie typ 1 trafia do puli (bez zmian).
3. **Klasyfikacja krytyczna.** Obraz, tablice i obszary programu rozruchowego muszą leżeć **w całości** na typie 1
   i nie mogą przecinać żadnego innego typu. Kolizja z ACPI NVS (4), pamięcią zarezerwowaną (2, w MB2 m.in. usługi
   runtime UEFI), ACPI reclaimable (3), pamięcią uszkodzoną (5), nieznanym typem albo dziurą kończy start fail-closed.
   `kernel_main` wypisuje wtedy na COM1 `[PAMIEC] KRYTYCZNY BLAD WLASNOSCI PAMIECI ROZRUCHU` z kodem, typem
   i adresem.
4. Niski 1 MiB: stała rezerwacja (bez zmian).
5. Podkład obrazu trafia do Direct-Map (`dodaj_zakres_direct_map`) **bez** bitu dostępności i jest rezerwowany
   na stałe. Wcześniej obraz był dopisywany jako „dostępny”, co zamieniało NVS pod obrazem w „RAM” w bitmapie
   dostępności.
6. Zakresy według rodzaju (tabela §5.2).
7. Tylko obraz nie-tożsamościowy: ramki fizyczne pod VA okna (`[1 MiB, 14 MiB)`) tracą tożsamość, bo VA pokazuje
   obraz. PMM ich nie wydaje (zachowawczo, ok. 13 MiB) do czasu migracji konsumentów PA-jako-wskaźnik (R1B).
8. LAPIC/IOAPIC (bez zmian). Układ obrazu jest publikowany (`ObrazJadraAktywny`) dopiero po pełnym sukcesie.

Gwarancje (testy G):
- ramki obrazu, cienia okna, MBI, ACPI reclaimable i niskiego 1 MiB nigdy nie są wydawane, co potwierdza
  wyczerpanie alokatora dla 6 baz;
- `ZwolnijRamke`, `ZwolnijCiagleRamki`, `OdblokujRamke` i `OdblokujZakres` na ramkach obrazu nie mają efektu;
- double-free nie zmienia licznika dwukrotnie;
- moduł wraca do puli wyłącznie przez `OdblokujZakres`, a jego część w luzie stron obrazu pozostaje zarezerwowana;
- adresy > 4 GiB (np. framebuffer `0x900000000`, wpisy mapy powyżej 4 GiB) są obsługiwane bez obcięcia.

**Wykrycie a zapobieganie.** Kontrola PMM działa **po** wejściu do jądra. Może jedynie **wykryć**, że zewnętrzny
program rozruchowy umieścił obraz na pamięci firmware, i odmówić dalszej pracy. **Nie cofnie** nadpisania
(zawartość ACPI NVS jest już zniszczona). Zapobieganie wymaga programu rozruchowego, który przydziela pamięć
przez firmware: BRB R2. Dlatego odmowa startu w ścieżce GRUB UEFI z S3 jest **oczekiwaną niezgodnością
bezpieczeństwa**, a nie regresją (§12).

## 7. Model mapowań VMM (`jadro/mm/vmm.cpp`)

- **Adresy tablic.** Każda statyczna tablica `st_*` (PML4, PDP, 4×PD, PT pierwszych 2 MiB, hierarchia HHDM,
  pula PT okna) dostaje PA przez `pa_tabeli_statycznej()` (= `zakres_va_na_pa` okna obrazu, wyrównanie 4 KiB).
  Błąd tłumaczenia kończy inicjalizację VMM fail-closed. Usunięto wszystkie przypadki `(uint64_t)st_x` jako PA.
- **CR3.** `bazowe_pml4_jadra = HHDM(PA(st_pml4))`; `zapisz_cr3` przelicza HHDM → PA (bez zmian). CR3 procesów
  pochodzi z ramek PMM przez HHDM (`loader.cpp`), bez zmian.
- **Dolne 4 GiB** budowane wspólną funkcją `ObrazJadra::zbuduj_dolne_4gib()` z planu (priorytet reguł):
  1. strona 0 jest niezmapowana (NULL);
  2. okno obrazu wskazuje rzeczywisty podkład;
  3. alias podkładu (obraz nie-tożsamościowy) jest niezmapowany, bo inaczej tekst jądra miałby drugi,
     zapisywalny alias pod VA == PA;
  4. pozostałe VA mapują się tożsamościowo jako supervisor (strony 2 MiB, PWT|PCD dla LAPIC/IOAPIC).

  Dla obrazu tożsamościowego (MB2) wynik jest **bit w bit** równy tablicom sprzed R1A, co sprawdza test C,
  porównując wszystkie 2048 wpisów PD i 512 wpisów PT z niezależnie zbudowanym wzorcem. Pula PT okna
  (8 × 4 KiB w `.bss`) jest wtedy nieużywana; najgorszy przypadek dla przeglądu 2095 baz wynosi 8.
- **Strażnik okna obrazu.** `mapuj_strone_wewnetrznie`, `VmmOdmapujStroneJadra` i `VmmOdmapujStroneJadraGlobalnie`
  odmawiają operacji na VA okna. Okno wypełnia wyłącznie budowa bazowej przestrzeni; mapowanie BAR-u VA == PA,
  które trafiłoby w okno, zastąpiłoby kod jądra. Dziś żadne wywołanie nie dotyka okna, więc zmiana nie wpływa
  na zachowanie produkcyjne.
- **Bez dużego Direct-Map.** HHDM pozostaje bez zmian (4 GiB, wyłącznie RAM typu 1 + podkład obrazu, NX).
  Nie dodano nowego okna fizycznego, mapowania rekurencyjnego ani mapowań tymczasowych.
- **W^X jądra NIE jest egzekwowane.** Okno obrazu to strony RW i wykonywalne (2 MiB), a podkład ma alias RW+NX
  w HHDM. Deklaracje segmentów spełniają W^X, ale efektywne uprawnienia stron — nie. Przyszła egzekucja W^X wymaga
  PT 4 KiB z uprawnieniami segmentów i usunięcia aliasu HHDM dla `.text`/`.rodata`.

## 8. Izolacja jądro / Ring 3

- Wpisy dolnych 4 GiB w bazie nie mają `PTE_USER`, co test C sprawdza dla wszystkich wpisów każdej bazy.
  Górna połowa (HHDM, bufory, stosy) jest współdzielona i supervisor-only (bez zmian).
- **Nowe:** strona użytkownika nie może leżeć w oknie VA obrazu **ani** wskazywać ramki podkładu obrazu
  (`ObrazJadra::mapowanie_uzytkownika_dozwolone` w `mapuj_strone_wewnetrznie`, gałąź `PTE_USER`). Obejmuje to
  wszystkie ścieżki Ring 3: loader BUR v1/v2, SHM BWS, sterta `syscalls.cpp`. Audyt potwierdził, że wszystkie
  używają ramek PMM, więc zmiana nie wpływa na produkcję.
- Liczbowe nakładanie się PA podkładu z VA Ring 3 (np. baza 16 MiB) nie jest błędem. Alias w bazie jest
  niezmapowany, a mapowania użytkownika powstają w prywatnych kopiach dolnej połowy. W trybie nie-tożsamościowym
  zakres VA == PA podkładu staje się wolny, więc ASLR BUR v2 (`VmmZakresWolnyWBazie`) może go wybrać. To
  poprawne, bo kernel nie używa tych VA.

## 9. DMA i sprzęt

| Ścieżka | Adres dla urządzenia | Adres dla CPU | Zależność od PA obrazu |
|---|---|---|---|
| `dma_allocate` (`sterowniki/dma.cpp`) — virtio-blk/net/gpu/input/rng, virtqueue, xHCI (pierścienie, DCBAA, scratchpad, ERST), OHCI | PA z `ZaalokujCiagleRamki` | HHDM(PA) | brak |
| AHCI, E1000, HDA (BDL, bufory), AC97 | PA ramek PMM (`void*` = PA) | `FizycznyZakresDoWirtualnego` | brak |
| Bufory grafiki, `virtio-gpu` backing | ramki PMM | górna połowa / HHDM | brak |
| Statyczne bufory w `.data/.bss` jako DMA | **nie znaleziono** (`alignas` i rzutowania przejrzane; `WirtualnyDoFizycznego` odrzuca VA obrazu, test G) | — | — |

Wniosek: żaden sterownik nie zakłada PA == VA dla pamięci DMA. MMIO to osobny temat (§10).

## 10. Rewalidacja zależności adresowych

Klasy: **P** — poprawne; **N→Z** — niepoprawne po rozdzieleniu, **zmienione w R1A**; **T** — zależne od mapy
tożsamościowej (poprawne, dopóki obowiązują opisane warunki); **D** — wymaga dalszego dochodzenia.

| Miejsce | Zachowanie | Klasa | Uwagi |
|---|---|---|---|
| `boot.S` `_start`, tablice `pml4/pdp/pd`, stos, GDT 32-bit | adresy bezwzględne = PA przy wyłączonym stronicowaniu | T | wejście MB2 z natury tożsamościowe; R1B: wejście 64-bit |
| `linker.ld` VMA == LMA, `KERNEL_VMA_START` | stałe VA | P | VA mają pozostać stałe; LMA rozstrzyga R1B |
| `vmm.cpp` `st_pml4[0] = (uint64_t)st_pdp` i 10 podobnych | VA `.bss` jako PA | N→Z | `pa_tabeli_statycznej` |
| `vmm.cpp` `bazowe_pml4 = HHDM((uint64_t)st_pml4)` | VA jako PA | N→Z | jw. |
| `vmm.cpp` tożsamość 0..4 GiB obejmująca okno obrazu | okno = tożsamość | N→Z | plan dolnych 4 GiB |
| `vmm.cpp` CR3 (`zapisz_cr3`), `scheduler.cpp`, `loader.cpp` `proces.cr3` | PA przez HHDM | P | — |
| `pmm.cpp` rezerwacja `__kernel_start…__kernel_end` | VA jako PA | N→Z | podkład z kontraktu |
| `pmm.cpp` `dodaj_dostepny_zakres(obraz)` | NVS pod obrazem jako „dostępny” | N→Z | `dodaj_zakres_direct_map` |
| `pmm.cpp` `OdblokujZakres`, diagnostyka właściciela strony | VA obrazu jako PA | N→Z | podkład |
| `pmm.cpp` odczyt MBI/mapy | PA jako wskaźnik przed VMM, < 4 GiB | T | flaga kontraktu bit 0 (MB2 i BRBI 2.2 gwarantują tożsamość 0..4 GiB) |
| `kernel.cpp` konsumenci MB2 po VMM (`znajdz_modul_mb2`, tapety, `instalator_boot`, grafika, `acpi_inicjalizuj`, aktualizacja, BSF) | MBI i moduły przez PA jako wskaźnik | T | bezpieczne, dopóki zasoby leżą poza oknem VA (`ZASOB_W_CIENIU_OKNA`); Faza 7: kopia albo HHDM |
| `acpi.cpp` `mapuj_tabele` | tabele < 4 GiB przez tożsamość, ≥ 4 GiB przez `OKNO_ACPI` | T/D | przy obrazie nie-tożsamościowym tabela fizycznie w `[1, 14) MiB` zostałaby źle odczytana; **D**: R1B — zawsze mapowanie jawne albo kontrola |
| `hpet.cpp`, `apic.cpp`, `virtio_pci.cpp`, `e1000.cpp`, `ahci.cpp`, `hda_transport.cpp` | BAR/MMIO VA == PA (< 4 GiB) albo okno | T | BAR-y leżą w dziurze PCI poza RAM; trafienie w okno blokuje strażnik (§7); R1B+: jawne okna MMIO |
| `xhci.cpp`, `ohci.cpp`, HPET > 4 GiB | dedykowane okna VA | P | — |
| `grafika.cpp` LFB | `VADDR_FRAMEBUFFER` → PA LFB; LFB poza HHDM | P | — |
| `heap.cpp` sterta jądra (`0x500000000`) | ramki PMM | P | — |
| `stos_jadra.cpp` stosy jądra (`0xFFFF94…`) | ramki PMM | P | stos startowy `stack_top` = VA w `.bss` (P) |
| `gdt.cpp`, `idt.cpp`, `tss.cpp`, `syscall.S` | `lgdt/lidt`, `LSTAR`, `rsp0` = VA (adresy liniowe) | P | — |
| `idt.cpp` obsługa #PF | CR2 = VA, bez tłumaczenia na PA | P | — |
| `loader.cpp` BUR v1/v2, proces Ring 3 | ramki PMM, HHDM, mapowania użytkownika | P | strażnik §8 |
| DEXE64 | **nie znaleziono w źródłach** (`git grep`) | — | NIE DOTYCZY |
| `bufory_grafiki.cpp`, RAM-dysk PSF (`0x200000000`) | ramki PMM | P | — |
| `pamiec.h` `KERNEL_MAX_END_ALLOWED` (14 MiB) vs `linker.ld` (15 MiB) | niespójne limity | P | bez zmian (osobna decyzja); obraz R1A kończy się na `0xD7A000` |
| SMP / trampolina AP | brak | — | odroczone |

## 11. Zgodność z Multiboot2 (ścieżka GRUB)

- Wejście, nagłówek MB2 (`.boot` bajt w bajt identyczny), punkt wejścia `0x1193E2`, VMA == LMA i segmenty
  pozostają bez zmian (`grub-file --is-x86-multiboot2` OK).
- Konfiguracja GRUB, instalator i ISO nie zmieniają się.
- PMM dodaje kontrolę, której wcześniej nie było: GRUB UEFI na OVMF z S3 kończy start komunikatem
  `[PAMIEC] … ACPI NVS` (§12). GRUB BIOS oraz GRUB UEFI bez kolizji z NVS startują normalnie (wyniki §14).

## 12. ACPI NVS — ustalenie, dowody i zalecenie bezpieczeństwa wydania

**[OBSERWACJA 4A-R]** Ścieżka GRUB 2.12 UEFI na OVMF z S3:
- segment `.data/.bss` jest składany w buforze `LoaderData` i po `ExitBootServices` kopiowany pod
  `0x2AA000–0xD6F000`;
- w chwili wejścia do jądra całe 996 KiB ACPI NVS (`0x800000–0x808000`, `0x80B000–0x80C000`,
  `0x810000–0x900000`) zawiera bajty obrazu jądra;
- wcześniejsza, niezerowa zawartość firmware jest zniszczona (`decyzja-relokacja-jadra.md` §4).

**Dowody (zachowane, tylko do odczytu):**
- `.brb-build/qemu-diag/{grub-q35-256,grub-pc-2g,grub-pc-2g-prod,grub-q35-256-tapety,grub-q35-256-bez-s3}/`:
  `mbi.bin`, `nvs-przed.bin`, `nvs-wejscie.bin`, `nvs-koniec.bin`, `jadro-*.bin`, `com1.log`, `polecenie.txt`,
  `analiza.txt`;
- jądro `303cb82e…` (ELF `fa8bea5a…`), fixture `5c4e1017…`, `grub.cfg` `79856871…`, QEMU 8.2.2,
  OVMF edk2 2024.02-2ubuntu0.10, GRUB 2.12-1ubuntu7.3.

**Reprodukcja:**
```
sh narzedzia/brb/zbuduj_fixture_grub.sh <jadro> .brb-build/<katalog> .brb-build/grub-fixture/tapety.pak
python3 narzedzia/brb/diag_grub_ovmf.py --scenariusz <nazwa> --tryb grub --maszyna pc,hpet=on --pamiec 2048 --profil produkcja
python3 narzedzia/brb/analizuj_diag_grub.py .brb-build/qemu-diag/<nazwa> <jadro ELF>
```

**Testowany fixture ≠ oficjalne ISO.** Fixture to małe ISO z `grub-mkrescue`: dokładne jądro, `tapety.pak`
i wpis odwzorowujący „Bursztyn OS” plus polecenia diagnostyczne. Oficjalne `BursztynOS.iso` było niedostępne
(brak w drzewie, pełny `make iso` nieuruchomiony z powodu miejsca). Zachowanie oficjalnego ISO na tej samej
platformie jest **[NIEZNANE]**, ale ten sam GRUB i ta sama metoda ładowania czynią je wysoce prawdopodobnym.

**[WYMÓG ZEWN.]**
- UEFI 2.10 §7.2 (tabela „Memory Type Usage after ExitBootServices()”): `EfiACPIMemoryNVS` program ładujący
  i system operacyjny muszą **zachować** w stanie pracy oraz w stanach ACPI S1–S3.
- ACPI 6.5 §15: `AddressRangeNVS` jest zajęty przez platformę i nie może być używany przez OS.

Brak obsługi S3 w Bursztyn OS **nie** zwalnia z tego wymogu: firmware i AML/SMM mogą korzystać z NVS także
w stanie S0.

**Zalecenie bezpieczeństwa wydania (ścieżka GRUB UEFI):**
1. Do czasu R2 ścieżka GRUB UEFI na platformach, gdzie mapa firmware ma ACPI NVS / pamięć zarezerwowaną w
   `[1 MiB, koniec obrazu)`, jest **niebezpieczna** i **nie może** być traktowana jako zaakceptowana, nawet gdy
   pulpit startuje.
2. Jądro R1A wykrywa ten przypadek i odmawia startu z jednoznacznym komunikatem COM1. Grafika nie jest jeszcze
   zainicjalizowana, więc na ekranie zostaje ostatni obraz GRUB. Rozważyć komunikat na ekranie w kolejnej fazie.
3. Wydania: ścieżki **GRUB BIOS** i **GRUB UEFI bez kolizji** (np. OVMF bez S3, platformy z NVS poza obrazem)
   pozostają dopuszczalne. Dla UEFI zalecane jest jawne ostrzeżenie w informacjach o wydaniu i test akceptacyjny
   „zrzut NVS przed/po = 0 zmienionych bajtów” na każdej platformie referencyjnej.
4. Nie wolno „naprawiać” tego przez trwałe wyłączenie S3, modyfikację OVMF ani osłabienie kontroli PMM.
5. Docelowe rozwiązanie: BRB R2 (przydział przez firmware), opcjonalnie R3 (tag MB2 10 dla GRUB) — każde
   potwierdzone zrzutem NVS.
6. Konfiguracja produkcyjna GRUB **nie** została zmieniona w R1A.

## 13. Testy

`make test-r1a-pamiec-host` uruchamia g++ z `-Werror -Wshadow`, ASan + UBSan (`-fno-sanitize-recover=all`)
i skanem instrukcji uprzywilejowanych.

| Grupa | Testowany kod produkcyjny | Symulowane |
|---|---|---|
| A typy adresów | `adresy.h` w całości | — |
| B układ i tłumaczenie | `zbuduj_uklad`, `va_na_pa`, `pa_na_va`, `zakres_va_na_pa`, `offset_va`, `uprawnienia_va`, `mapowanie_uzytkownika_dozwolone`, `bajty_segmentow_przecinaja` | — |
| C plan dolnych 4 GiB | `zaplanuj_strone_bazowa`, `zaplanuj_region_2m`, `zbuduj_dolne_4gib`, `potrzebne_tablice_pt` (te same funkcje wywołuje `vmm.cpp`) | PA tablic, przejście po tablicach |
| D adapter MB2 | `zbuduj_z_multiboot2` | pamięć fizyczna, tłumaczenie przez tablice (model) |
| E walidator | `waliduj` + mutacje deterministyczne i losowe (xorshift, stały zarodek) | — |
| F klasyfikacja | `klasyfikuj_wobec_mapy_mb2` | — |
| G PMM | `InicjalizujPMM`, `InicjalizujPMMZKontraktem`, alokacja, zwalnianie, `OdblokujZakres`, Direct-Map, status | pamięć fizyczna; brak zerowania przez HHDM |
| H rzeczywiste MBI | adapter + PMM na MBI z GRUB 2.12 (4A-R i R1A) | pamięć fizyczna |
| I układ Ring 3 | stałe `pamiec.h`, plan dla VA Ring 3 | — |

Niepokryte na hoście:
- `vmm.cpp` jako całość (asembler: CR3, CPUID, MSR) — pokrywa ją tylko rozruch QEMU;
- przejście `tlumacz_przez_aktywne_tablice` (CR3) — pokrywa je tylko rozruch QEMU;
- strażniki w `mapuj_strone_wewnetrznie` i odmapowaniach — testowana jest funkcja czysta, wywołanie tylko przez
  kompilację.

**Symulowane mapowanie nie-tożsamościowe nie dowodzi, że fizycznie przeniesione jądro produkcyjne startuje.**
Natywna weryfikacja (PA ≠ VA w działającym jądrze) wymaga R1B/R2. Jest to **obowiązkowa bramka akceptacyjna
na przyszłość**.

Kontrola negatywna: 9 celowych mutantów kodu produkcyjnego (okno jako tożsamość, rezerwacja VA zamiast PA,
NVS jako RAM, brak kontroli nakładania, brak dziury aliasu, wyłączony strażnik Ring 3, brak cienia okna,
ignorowany tag 21, wyjątek luzu dla BRB) musi dawać FAIL > 0. Wyniki zawiera raport Fazy R1A.

## 14. Wyniki R1A

Artefakty: `.brb-build/r1a/` (kompilacje, logi, kamienie milowe, mutanty) i `.brb-build/qemu-diag/r1a-*/`.

| Element | Wynik |
|---|---|
| Jądro bazowe (kompilacja z `e24b971`) | ELF `fa8bea5a…`, po `strip` `303cb82e…` — **odtwarzalne bit w bit** z wydaniem |
| Jądro R1A | ELF `b9e5deb7…`, po `strip` `154dd9e2…`; ET_EXEC x86-64, wejście `0x1193E2`, 4 × PT_LOAD VMA == LMA, `.boot` identyczny, koniec `0xD7A000` (było `0xD6F000`), 0 nowych ostrzeżeń |
| `test-r1a-pamiec-host` | PASS = 501, FAIL = 0 (ASan/UBSan); mutacje losowe 200 000, naruszenia 0 |
| Mutanty (9) | wszystkie wykryte (FAIL 1–15) |
| GRUB BIOS (SeaBIOS), pc 2 GiB, profil produkcyjny | PASS do pulpitu; MBI pod `0x100080` w luzie `.boot` (pierwszy przebieg przed poprawką §5.3 pkt 10: FAIL — fałszywa „podwójna własność”) |
| GRUB UEFI, OVMF bez S3 | PASS do pulpitu |
| GRUB UEFI, OVMF z S3 (diagnostyczny) | jądro odmawia startu: `OBRAZ_NA_ACPI_NVS`, typ 4, `0x800000`; zrzuty: NVS i tak nadpisany przez GRUB w 100 % przed wejściem (wykrycie, nie zapobieganie) |
| Jądro bazowe, GRUB BIOS (porównanie) | PASS do pulpitu; identyczne kamienie milowe, RAM i wyjście sterowników |
| Regresje BRB (host + OVMF 13 scenariuszy) | PASS, kody jak w 4A-R; jądro R1A przez BRB: E506 + wycofanie (Faza 4A nadal BLOKADA) |
| Natywne wykonanie z PA ≠ VA | **NIE URUCHOMIONO** — wymaga R1B/R2 |

## 15. Wymagania wstępne R1B

1. Wejście 64-bit jądra (`_start_brb64` / `wejscie64`), niezależne od `boot.S`, z kontraktem budowanym przez
   adapter BRB. Kontrakt v1 może pozostać wewnętrzny; Faza 7 decyduje o BRBI.
2. Tablice stron bootstrapu mapujące okno VA → podkład i tożsamość 0..4 GiB przed VMM (wymóg flagi bit 0) albo
   zmiana adaptera na dostęp przez inne, jawne okno.
3. Decyzja o LMA w `linker.ld` (`p_paddr`) dla BRB: dziś VMA == LMA, a BRB ładuje pod adres przydzielony,
   więc `p_paddr` jest ignorowany.
4. Migracja konsumentów PA-jako-wskaźnik (MBI, moduły, ACPI < 4 GiB) na kopię albo HHDM. Wtedy można zdjąć
   rezerwację cienia okna (§6 pkt 7).
5. Odzyskiwanie `TABLICE_STRON_ROZRUCHU` / `ODZYSKIWALNY_ROZRUCHU` po przełączeniu CR3.
6. Test natywny: jądro uruchomione z PA ≠ VA (np. baza 0x3000000), kamienie milowe do pulpitu i zrzut NVS = 0.

## 16. Ryzyka i ograniczenia

| Ryzyko | Ocena | Stan |
|---|---|---|
| Ścieżka GRUB UEFI z S3 przestaje startować | oczekiwane (bezpieczeństwo) | udokumentowane, §12 |
| Inne programy rozruchowe MB2 kładą zasoby w luzie stron obrazu | obsłużone (wyjątek MB2, §5.3 pkt 10) | test hosta + QEMU BIOS |
| ACPI w `[1, 14) MiB` przy obrazie nie-tożsamościowym | brak w R1A (tożsamość) | D, R1B |
| BAR VA == PA trafiający w okno | odmowa mapowania (strażnik) | sterownik zgłosi błąd zamiast uszkodzić jądro |
| W^X jądra | nieegzekwowane | udokumentowane |
| Pula PT okna (32 KiB `.bss`) nieużywana w MB2 | koszt pamięci | akceptowalny |
| Obraz rośnie o 44 KiB | `0xD6F000` → `0xD7A000` (limit 14 MiB) | OK |

## 17. Migracja i wycofanie

- R1A jest wycofywalna jednym rewertem plików wymienionych w nagłówku. Ścieżki GRUB, instalator i ISO są
  niezmienione.
- R1A jest zachowawcza: tablice dla MB2 są identyczne, a rezerwacje netto identyczne (poza odmową w przypadku
  NVS).
- Kolejność dalszych etapów: R1B (wejście 64-bit, bootstrap) → R2 (BRB ładuje pod adres przydzielony) →
  R3 (opcjonalnie tag 10) → R4 (BRB domyślny). Każdy etap wymaga osobnej zgody.

## 18. Źródła

- UEFI Specification 2.10, §7.2 (typy pamięci i ich użycie po `ExitBootServices`).
- ACPI Specification 6.5, §15 (System Address Map Interfaces, `AddressRangeNVS`).
- Multiboot2 Specification 1.0: nagłówek obrazu OS (w tym tag „relocatable header”, typ 10), stan maszyny przy
  wejściu i ładowanie ELF według nagłówków programu, tagi informacji „Memory map” (typ 6) i „Image load base
  physical address” (typ 21).
- System V ABI / ELF-64 Object File Format (`PT_LOAD`, `p_paddr`, `p_vaddr`, `p_memsz`).
- Intel 64 and IA-32 Architectures SDM, t. 3A, rozdz. 4 (stronicowanie 4-poziomowe, formaty PTE, MAXPHYADDR).
- AMD64 Architecture Programmer's Manual, t. 2, rozdz. 5 (translacja adresów).
- GRUB 2.12 wyłącznie jako obserwowany program zewnętrzny (położenie MBI, tag 21, mapy); bez kopiowania kodu.
