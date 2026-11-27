# Faza R1B — niezależne wejście 64-bit jądra i przygotowanie bootstrapu

| | |
|---|---|
| Status | **ZREALIZOWANA JAKO KAMIEŃ MILOWY WEJŚCIA I BOOTSTRAPU — czeka na przegląd właściciela.** Produkcyjne ładowanie przez BRB (R2) **nie** jest zrealizowane |
| Data | 2026-11-27 |
| Decyzja nadrzędna | `decyzja-relokacja-jadra.md` (C + E ZAAKCEPTOWANA), fundament: `r1a-rozdzielenie-adresow.md` |
| Gałąź / bazowy commit | `feature/bootloader`, `3e19b8dfa2ccc73b44be57bda6dbaa033c21ec46` (R1A) |
| Testy | `make test-r1b-wejscie-host`, `make test-r1a-pamiec-host`, uprząż QEMU (wariant `BURSZTYN_TEST_WEJSCIE64=1`) |

Oznaczenia: **[WYMÓG ZEWN.]** — specyfikacja zewnętrzna; **[DECYZJA]** — decyzja Bursztyn OS; **[OBSERWACJA]** —
zmierzone; **[SYMULACJA]** — test hosta bez wykonania instrukcji uprzywilejowanych.

---

## 1. Architektura dwóch wejść

| | Wejście Multiboot2 (bez zmian) | Wejście natywne 64-bit (nowe) |
|---|---|---|
| Symbol | `_start` (`jadro/arch/boot.S`) = `e_entry` = `0x1193E2` | `wejscie_jadra64` (`jadro/arch/wejscie64.S`) |
| Tryb przy wejściu | 32-bit PM, stronicowanie wyłączone | IA-32e 64-bit, stronicowanie 4-poziomowe |
| Kto przygotowuje środowisko | `boot.S`: kontrole CPU, tablice tożsamości 0..4 GiB (strony 2 MiB), GDT, EFER.LME, CR0.PG/WP, stos | program rozruchowy (R2: BRB; R1B: wyłącznie uprząż testowa) wg kontraktu §3 + samo jądro (GDT, stos, kanarek) |
| Położenie fizyczne | PA == VA (wymóg `boot.S`, kod zależny od położenia) | dowolne (ciągły podkład, < 4 GiB tymczasowo) |
| Informacje rozruchu | RDI/RSI = magic MB2, MBI | RDI = `MAGIC_WEJSCIA_NATYWNEGO`, RSI = PA `KontraktV1` (MBI w formacie MB2 wskazane w kontrakcie) |
| Dalej | `kernel_main` → `start_jadra` | `wejscie_jadra64_c` → `kernel_main_natywny` → `start_jadra` |

**[DECYZJA]** Oba wejścia zbiegają się w **jednym** potoku `start_jadra()` (`kernel.cpp`): ten sam PMM (z kontraktu),
GDT/TSS/IDT, VMM, sterowniki, planista, BWS i aplikacje. Nic nie jest zdublowane. Jedyna rozgałęziona operacja to
inicjalizacja PMM: `InicjalizujPMM(mbi)` (adapter MB2) albo `InicjalizujPMMZKontraktem(kopia)`.

Podział obowiązków (co robi `boot.S`, a co musi zapewnić ścieżka natywna):

| Operacja | `boot.S` | Ścieżka natywna |
|---|---|---|
| Kontrola CPUID (LM, PAE, PSE, MSR) | tak | `waliduj_stan` (LM, PAE, LA57, PCIDE, NX/NXE, MAXPHYADDR) |
| Long Mode, PAE, EFER.LME, CR0.PG | ustawia | wymaga (program rozruchowy) i weryfikuje |
| CR0.WP | ustawia | wymaga i weryfikuje |
| Tablice stron | identity 0..4 GiB | wymaga (tablice przejścia, §4), mierzy podkład przez nie |
| GDT | tymczasowa (32-bit `lgdt`) | **własna** `gdt_wejscia64` (bity accessed ustawione, `.rodata` tylko do odczytu), `lretq` przeładowuje CS |
| Segmenty DS/ES/SS/FS/GS | ustawia | ustawia |
| Stos | `stack_top` (`.bss`) | **ten sam** `stack_top` (VA okna), stos przejścia porzucony |
| `stack_protector_early_init` | tak | tak |
| Zerowanie BSS | GRUB (MB2 wymaga) | program rozruchowy (kontrakt) |
| IDT | brak do `InicjalizujIDT` | jw. — wyjątek przed IDT = potrójny błąd (udokumentowane) |
| FPU/SSE | `fpu_initialize` później | jw.; jądro kompilowane `-mgeneral-regs-only -mno-sse`, stan x87/SSE nieokreślony |

## 2. Wykrywanie wejścia natywnego w okrojonym ELF

**[DECYZJA]** 64-bajtowa struktura `MetadaneJadra::Uklad` (`jadro/arch/metadane_jadra.h`, stałe dla asemblera
`metadane_jadra_asm.h`, `static_assert` zgodności) w sekcji `.multiboot.metadane`. Linker składa ją za nagłówkiem
Multiboot2 (`KEEP(*(.multiboot.*))` — **bez zmian w `linker.ld` i `boot.S`**). Trafia do pierwszego segmentu
PT_LOAD (R, bez W/X), więc jest w danych pliku i **przeżywa `strip`**: zweryfikowano na pliku okrojonym, offset
`0x1050`.

| Off. | Pole | Wartość (jądro R1B) |
|---|---|---|
| 0 | sygnatura | `0x4F5244414A425242` („BRBJADRO”) |
| 8 / 10 | wersja / rozmiar | 1 / 64 |
| 12 | kontrakt wejścia | 1 = R1B (RDI magic, RSI kontrakt) |
| 16 | baza wirtualna | `0x100000` (kontrola zgodności z najniższym `p_vaddr`) |
| 24 | wejście 64-bit (VA) | `0x11960A` |
| 32 | wymagane wyrównanie podkładu | `0x1000` |
| 40 | wymagane cechy | LM, PAE, stronicowanie 4-poziomowe, NXE gdy NX |
| 44 / 48 | flagi / zarezerwowane | 0 / 0 |
| 56 | sygnatura końcowa | `~sygnatura` |

Czytelnik (`rozruch/brb/wspolne/metadane_jadra.cpp`, `znajdz_metadane_wejscia`):
- działa na **zwalidowanym planie** istniejącego parsera BRB (`zbuduj_plan_ladowania`), więc nie ma drugiego parsera ELF;
- nie używa nazw sekcji ani symboli;
- przeszukuje pierwsze 32 KiB pliku (okno nagłówka MB2) na przesunięciach wyrównanych do 8 i wymaga **dokładnie
  jednego** trafienia: brak = błąd, wiele = niejednoznaczność;
- odrzuca kolejno: strukturę poza danymi z pliku segmentu R bez W/X, obcięcie, złą sygnaturę końcową, wersję,
  rozmiar, kontrakt, pola zarezerwowane, nieznane cechy, złe wyrównanie, bazę niezgodną z planem, wejście
  niekanoniczne, wejście poza segmentami, w segmencie bez `PF_X`, w części BSS (poza `p_filesz`) oraz wejście
  równe `e_entry`.

Wartości wyprowadzalne z nagłówków programu nie są duplikowane, z wyjątkiem bazy wirtualnej, która służy jako
kontrola spójności. W R1B czytelnik **nie** jest linkowany do `brb-x64.efi`, więc ścieżka ładowania BRB się nie
zmienia (suma kontrolna BRB jest identyczna).

## 3. Kontrakt stanu procesora (`jadro/arch/stan_wejscia64.h`)

Źródła **[WYMÓG ZEWN.]**:
- Intel SDM t. 3A: §2.5 (rejestry kontrolne), §2.2.1 (EFER), §4.1 (tryby stronicowania), §4.1.4 (CPUID,
  MAXPHYADDR), §4.5 (PTE), §4.6 (uprawnienia);
- AMD APM t. 2: §3.1.7, §5.3; t. 3: CPUID 80000001h/80000008h;
- UEFI 2.10 §2.3.4 (konwencja x64 firmware) i §7.4 (`ExitBootServices`).

| Element | Wymaganie | Kontrola |
|---|---|---|
| Tryb | IA-32e: CR0.PE=1, CR0.PG=1, CR4.PAE=1, EFER.LME=1, EFER.LMA=1 | `TRYB_NIE_IA32E` |
| CPL | 0 (CS.RPL = 0) | `CPL_NIE_ZERO` |
| Stronicowanie | 4-poziomowe; CR4.LA57=1 → **odmowa** (nieobsługiwane) | `STRONICOWANIE_5P` |
| CR0 | PE, PG, **WP=1** | `WP_WYLACZONE` |
| CR3 | wyrównany do 4 KiB, ≠ 0, < 2^MAXPHYADDR; CR4.PCIDE=0; bity PWT/PCD dozwolone (maskowane) | `CR3_NIEPOPRAWNY`, `PCID_WLACZONE` |
| CR4 | PAE=1, LA57=0, PCIDE=0; SMEP/SMAP ustawia później VMM | jw. |
| EFER | LME, LMA; **NXE=1 ⇔ CPUID NX** (tablice przejścia używają bitu 63) | `NXE_NIEZGODNE` |
| Szerokość fizyczna | z CPUID 80000008h EAX[7:0] (brak liścia = 36), zakres 36..52; **nie** zakłada 52 bitów | `SZEROKOSC_FIZYCZNA` |
| Szerokość liniowa / kanoniczność | 48 bitów (4 poziomy); VA jądra i tożsamości walidowane jako kanoniczne dolnej połowy | `PlanPrzejscia` |
| RFLAGS | IF=0, DF=0 (zapis RFLAGS przed `cli/cld`) | `PRZERWANIA_WLACZONE`, `KIERUNEK_USTAWIONY` |
| Segmenty / GDT | dowolny poprawny 64-bit CS przy wejściu; jądro natychmiast ładuje własną GDT (0x08 kod, 0x10 dane) | — |
| IDT | nie wymagana; przerwania wyłączone do `InicjalizujIDT` | — |
| Stos | RSP ≡ 0 (mod 16) w chwili skoku (`jmp`, nie `call`), ≥ 4 KiB zapisywalne (jądro odkłada 8 B); własność: program rozruchowy, obszar `ODZYSKIWALNY_ROZRUCHU` | `STOS_NIEWYROWNANY` |
| Argumenty | RDI = magic, RSI = PA kontraktu (≠0, wyrównany do 8, < 4 GiB, < 2^MAXPHYADDR) | `ZLY_MAGIC`, `KONTRAKT_NIEPOPRAWNY` |
| FPU/SIMD | brak wymagań; jądro bez SSE, `fpu_initialize` później | — |
| Konwencja wywołań | **System V AMD64** (jądro, `-mno-red-zone` w `CPU_FLAGS`); Microsoft x64 dotyczy wyłącznie wywołań usług UEFI w BRB | zweryfikowano w deasemblacji: argumenty RDI/RSI/RDX/RCX |
| Powrót | niedozwolony: `wejscie_jadra64_c` jest `[[noreturn]]`, za `call` jest `cli; hlt` | — |
| Ścieżka błędu | COM1 (`[WEJSCIE64] KRYTYCZNY BLAD …`) + `cli; hlt` w pętli; bez usług UEFI | — |

Kolejność w `wejscie64.S`:
1. Zapis RFLAGS, RSP, RDI i RSI; potem `cli` i `cld`.
2. Własny stos jądra i własna GDT, przeładowanie CS.
3. Kanarek stosu.
4. Walidacja w C++.

Przed walidacją nie wykonuje się żadna instrukcja zależna od opcjonalnej cechy: `rdmsr EFER` istnieje w każdym
procesorze x86-64, a `rdrand`/`rdseed` w `stack_protector_early_init` są sondowane przez CPUID.

## 4. Tablice stron przejścia (`jadro/mm/plan_przejscia.h`)

Komponent czysty, testowalny na hoście, do użycia przez BRB w R2. Wykorzystuje typy i układ obrazu z R1A.

- **Okno obrazu:** VA z linkera → ciągły podkład, strony 4 KiB z uprawnieniami segmentów: `.text` R+X,
  `.rodata` R+NX, `.data/.bss` RW+NX, `.boot` R+NX. **W^X obowiązuje od pierwszej instrukcji jądra.**
- **Tożsamość wyłącznie dla jawnie podanych zakresów** z klasą uprawnień:
  - kod przejścia: R+X;
  - stos: RW+NX;
  - tylko odczyt (informacje, kontrakt, mapa, moduły): R+NX;
  - dane (np. GDT bez bitów accessed): RW+NX.

  Pula ramek tablic jest mapowana tożsamościowo jako R+NX, bo jądro mierzy przez nią podkład.
- **Brak blanketowej tożsamości 0..4 GiB.** Strona 0, niski 1 MiB, alias podkładu, LAPIC i reszta przestrzeni
  pozostają niezmapowane (test C).
- **Pierwszeństwo = błąd, nie nadpisanie:**
  - tożsamość w oknie VA → `KOLIZJA_Z_OKNEM`;
  - tożsamość na podkładzie → `ALIAS_PODKLADU`;
  - różne klasy na tej samej stronie → `KONFLIKT_KLAS`;
  - pula przecinająca podkład, okno albo zakres tożsamości → `PULA_KOLIZJA`.
- Walidacja: szerokość fizyczna z CPUID, kanoniczność (< 2^47), strona 0, przepełnienia, wyrównanie i
  wyczerpanie puli. Każda ramka pochodzi z puli przekazanej przez wywołującego (pamięć zdobyta zgodnie
  z regułami własności).
- Wpisy pośrednie mają P|W bez U i bez NX; o uprawnieniach decyduje liść. Nie ma bitu U nigdzie.

**Wymogi przejścia CR3** (§8 polecenia): kod wykonujący `mov cr3` i skok, aktywny stos, GDT używana przy skoku
i dane czytane tuż po przełączeniu muszą być zmapowane pod tym samym VA przed i po. Możliwe są dwa sposoby:
1. jako tożsamość — klasy `KOD_PRZEJSCIA` i `STOS`;
2. w oknie obrazu z identyczną zawartością — tak robi uprząż R1B: kod przejścia leży w `.text`, a VA okna
   przed przełączeniem wskazuje oryginał, po przełączeniu kopię o tych samych bajtach.

Jądro natychmiast po wejściu porzuca stos przejścia i ładuje własną GDT. Prawdziwe przełączenie CR3 przez BRB
nie zostało wykonane (R2). Nie modyfikowano tablic firmware.

## 5. Normalizacja informacji rozruchu

**[DECYZJA]** Wersja kontraktu pozostaje **1** (2408 B, układ bez zmian), bo R1B nie potrzebuje nowych pól.
Rozszerzono wyłącznie semantykę:
- nowa flaga `FLAGA_TOZSAMOSC_ZASOBOW` (bit 2): przed VMM dostępne tożsamościowo są tylko zasoby opisane
  w kontrakcie;
- źródło `ZRODLO_NATYWNE_64` (2);
- mapa musi leżeć w strukturze informacji dla każdego źródła;
- nowe kody błędów przyjęcia.

Obie ścieżki dają tę samą reprezentację: adapter MB2 (R1A) i `przyjmij_kontrakt_natywny` (R1B). Różnicę od
publicznego BRBI (Faza 7) zachowano; natywne wejście R1B nadal wskazuje MBI w formacie MB2.

Bezpieczny dostęp do wskaźnika początkowego, **bez zależności cyklicznej**:
1. RSI jest sprawdzany wyłącznie arytmetycznie: ≠ 0, wyrównany do 8, `+sizeof` ≤ 4 GiB. Czytane jest dokładnie
   `sizeof(KontraktV1)` bajtów i kopiowane do `.bss` jądra.
2. Pełna walidacja kopii, także względem symboli linkera.
3. Spójność z samym sobą: kontrakt musi leżeć w zakresie `ODZYSKIWALNY_ROZRUCHU` albo `INFORMACJE_ROZRUCHU`,
   który sam opisuje, a `cr3_rozruchu` musi równać się aktywnemu CR3 (z maską PWT/PCD).
4. **Pomiar** podkładu: przejście po aktywnych tablicach, przy czym czytać wolno tylko tablice leżące
   w zakresach `TABLICE_STRON_ROZRUCHU`. Wynik musi zgadzać się z kontraktem
   (`PODKLAD_NIEZGODNY_Z_KONTRAKTEM`, `PODKLAD_NIECIAGLY`, `TLUMACZENIE_OBRAZU`).

Ustalenia dla poszczególnych składników:
- **ACPI:** RSDP pozostaje w tagach 14/15 MBI; tabele są czytane przez `PamiecFizycznaDoOdczytu` albo okno ACPI (§6).
- **Framebuffer:** zakres w kontrakcie (bez Direct-Map); mapowanie jak dotąd.
- **Moduły:** zakresy `MODUL` z czasem życia „do zwolnienia przez konsumenta”.

## 6. Zależności od tożsamości: usunięte i pozostałe

| Zależność | Stan R1B |
|---|---|
| Odczyt kontraktu, mapy i MBI przed VMM | ograniczony do zakresów kontraktu (natywnie) / tożsamość `boot.S` (MB2) |
| Tabele ACPI < 4 GiB (`acpi.cpp mapuj_tabele`) | **usunięta:** `PamiecFizycznaDoOdczytu` daje HHDM (RAM typu 1) albo tożsamość **potwierdzoną planem** (`ObrazJadra::tozsamosc_bezpieczna`: nigdy okno obrazu nie-tożsamościowego, dziura aliasu ani strona 0); inaczej okno ACPI. Długości i sumy kontrolne jak dotąd |
| MBI i moduły po VMM (≥ 10 konsumentów: `kernel.cpp`, instalator, tapety, grafika, aktualizacja, BSF) | **pozostaje** PA-jako-wskaźnik, ale za **bramką** `PmmZasobyRozruchuTozsamosciowe` (krytyczny błąd, jeśli którakolwiek strona nie jest tożsamościowa w planie). Pełna migracja na kopię/HHDM: Faza 7 (`InformacjeRozruchu`, D-03) |
| BAR/MMIO VA == PA (virtio, E1000, AHCI, HPET < 4 GiB, APIC) | pozostaje; strażnik okna (R1A) blokuje trafienie w okno |
| DMA | bez zależności (ramki PMM + HHDM) |
| Rezerwacja ramek „cienia okna” (`[1 MiB, koniec obrazu)` przy obrazie nie-tożsamościowym) | **zachowana** zgodnie z poleceniem — konsumenci PA-jako-wskaźnik nadal istnieją |
| Okno ACPI 1 MiB nadpisywane przy każdym mapowaniu (`OKNO_ACPI` od offsetu 0) | ryzyko: tabele > 4 GiB albo w cieniu okna są mapowane do jednego okna; równoczesne wskaźniki do dwóch takich tabel byłyby niespójne. Udokumentowane, R2 |

## 7. Własność i czas życia zasobów rozruchu

| Zasób | Właściciel po wejściu | Czas życia (R1B) |
|---|---|---|
| Podkład obrazu | jądro | trwały |
| Tablice przejścia (pula) | jądro, `TABLICE_STRON_ROZRUCHU` | do przełączenia CR3; PMM trzyma je trwale (odzyskiwanie po R2) |
| Kod przejścia | program rozruchowy (w R1B: `.text` jądra) | nie dotyczy |
| Stos przejścia + kontrakt | `ODZYSKIWALNY_ROZRUCHU` | do przełączenia CR3; PMM trzyma trwale (kopia kontraktu w `.bss`) |
| MBI | `INFORMACJE_ROZRUCHU` | trwały |
| Mapa pamięci firmware | w MBI | trwała |
| Moduły | `MODUL` | do `OdblokujZakres` po imporcie |
| Tabele ACPI | firmware (typ 3/4) | nigdy nie trafiają do PMM |
| Aktywny stos i aktywne tablice jądra | jądro (`.bss`) | trwałe |

Test F potwierdza, że żadne API (`ZwolnijRamke`, `ZwolnijCiagleRamki`, `OdblokujZakres`, `OdblokujRamke`) nie zwalnia
tablic przejścia, stosu ani kontraktu. Rozróżnienie z R1A (luz bajtowy MB2 kontra wyłączność stron dla BRB) jest
zachowane; test MBI pod `0x100080` przechodzi nadal (R1A).

## 8. Uprawnienia pamięci, aliasy HHDM i W^X

| Alias obrazu | Uprawnienia | W^X |
|---|---|---|
| Okno VA w przestrzeni jądra (`zbuduj_dolne_4gib`) | strony 2 MiB RW + wykonywalne | **nie** |
| HHDM podkładu | RW + NX | alias zapisywalny ramek `.text` |
| Tablice przejścia (R1B) | z segmentów, W^X | **tak** (z NX) |
| Tożsamość przejścia | wyłącznie zasoby, bez podkładu | brak aliasu |
| Przestrzenie procesów | klon dolnej połowy (okno RWX supervisor), HHDM współdzielony | jak przestrzeń jądra |
| Okna tymczasowe (ACPI, HPET) | RW, poza obrazem | n/d |

Analizator (`PlanPrzejscia::analizuj`) zastosowany do symulowanej przestrzeni jądra zbudowanej produkcyjnym
`zbuduj_dolne_4gib` + HHDM daje:
- baza `0x100000`: 2558 liści W+X i 517 aliasów W/X;
- baza `0x3005000`: 6018 liści W+X i 3584 aliasy W/X;
- liści z bitem U: 0.

Dla tablic przejścia: 0 W+X, 0 aliasów, 0 U. Bez NX planu przejścia nie da się egzekwować (`wx_egzekwowane = false`).

**W^X jądra NIE jest egzekwowane** w działającym systemie. Pełna egzekucja wymaga:
1. okna obrazu w PT 4 KiB z uprawnieniami segmentów (pula PT już istnieje, potrzeba ok. 7 PT);
2. usunięcia albo ograniczenia do R+NX aliasu HHDM dla `.text`/`.rodata` (HHDM buduje 2 MiB — dzielenie na brzegach);
3. audytu zapisów do `.rodata`/`.text` w jądrze (np. dane modyfikowane przez `const_cast`);
4. CR0.WP (już włączone) i EFER.NXE (już włączane);
5. testu natywnego.

W^X pozostaje **bramką akceptacji bezpieczeństwa** przed produkcyjną integracją BRB.

## 9. Bezpieczne zachowanie przy błędach

- Wejście natywne: `[WEJSCIE64] KRYTYCZNY BLAD <etap>: <opis> kod=… adres=…` na COM1, potem `cli; hlt`.
- Krytyczny błąd własności pamięci (R1A) zachowany. Ścieżka GRUB UEFI z ACPI NVS pod obrazem nadal kończy się
  odmową (§12). Kolizja zostaje jedynie wykryta, nie cofnięta.
- Diagnostyka na framebufferze **nie** została dodana. Na etapie PMM framebuffer nie jest zmapowany: przed VMM
  w trybie natywnym nie ma tożsamości poza zasobami. Bezpieczny dostęp wymagałby osobnego mapowania przed
  powstaniem sterty i VMM. Zostaje komunikat COM1; ograniczenie udokumentowane. Konsoli UEFI nie używa się
  na ścieżce po EBS.

## 10. Interpretacja `p_paddr` / `p_vaddr`

- **Ścieżka MB2:**
  - **[WYMÓG ZEWN.]** program rozruchowy ładuje segmenty pod `p_paddr` (obraz nie-relokowalny);
  - **[DECYZJA]** `linker.ld` utrzymuje `p_paddr == p_vaddr` (`boot.S` wykonuje się bez stronicowania).
- **Ścieżka natywna:**
  - **[DECYZJA]** `p_vaddr` to obowiązujące VA;
  - `p_paddr` **nie** jest obowiązkowym adresem docelowym. Program rozruchowy wybiera ciągły region (wyrównanie
    z metadanych), zachowuje względne przesunięcia `p_vaddr − baza_wirtualna` i opisuje go w kontrakcie.
    Zgodność weryfikuje pomiar w jądrze.
- `linker.ld` pozostał bez zmian: ET_EXEC, VMA == LMA, okno od 1 MiB, nagłówek MB2 identyczny (80 B).

## 11. Testy

| Grupa | Kod produkcyjny | Symulowane |
|---|---|---|
| A stan CPU | `StanWejscia64::waliduj_stan`, `bity_fizyczne` | stan rejestrów |
| B metadane | `znajdz_metadane_wejscia` na planie `zbuduj_plan_ladowania`; jądro R1B okrojone i nieokrojone | obrazy syntetyczne (fixture BRB) |
| C tablice przejścia | `PlanPrzejscia::zbuduj`, `analizuj` | PA tablic (bufory hosta) |
| D W^X przestrzeni jądra | `ObrazJadra::zbuduj_dolne_4gib` + `analizuj` | HHDM (odwzorowany jak w `vmm.cpp`) |
| E przyjęcie kontraktu | `KontraktPamieci::przyjmij_kontrakt_natywny` koniec-do-końca z tablicami z `PlanPrzejscia` | pamięć fizyczna |
| F PMM | `PrzyjmijKontraktNatywny`, `InicjalizujPMMZKontraktem`, `PamiecFizycznaDoOdczytu`, `PmmZasobyRozruchuTozsamosciowe`, API zwalniania | pamięć fizyczna, tłumaczenie |

Testy działają z ASan, UBSan (`-fno-sanitize-recover=all`), `-Werror -Wshadow` i skanem instrukcji
uprzywilejowanych. Mutacje losowe: 100 000 (metadane) i 20 000 (kontrakt natywny). Kontrola negatywna: mutanty
kodu produkcyjnego (raport). Test hosta symuluje stan procesora i nie wykonuje instrukcji uprzywilejowanych.

**Natywne wykonanie (QEMU):** wariant testowy `BURSZTYN_TEST_WEJSCIE64=1` (uprząż w `start_natywny.cpp`,
`r1b_przejscie_testowe` w `wejscie64.S`; w jądrze produkcyjnym tych symboli nie ma).
- Uprząż korzysta wyłącznie z pamięci typu 1 z mapy MB2 poza zasobami rozruchu.
- Kopiuje obraz pod inną bazę (niewyrównaną do 2 MiB) i kopiuje MBI.
- Buduje kontrakt i tablice `PlanPrzejscia` (W^X, tożsamość tylko zasobów).
- Przełącza CR3 i skacze do `wejscie_jadra64`.
- Rezygnuje, gdy obraz MB2 koliduje z pamięcią nie-RAM.

Wyniki: §13.

## 12. ACPI NVS i bezpieczeństwo wydania

Bez zmian względem R1A §12. Dowody 4A-R są zachowane (`.brb-build/qemu-diag/grub-*`). Obowiązuje
**[WYMÓG ZEWN.]** UEFI 2.10 §7.2: `EfiACPIMemoryNVS` musi zostać zachowany. Ścieżka GRUB UEFI z kolizją pozostaje
niebezpieczna i nieakceptowalna produkcyjnie; jądro R1B nadal odmawia startu. Przyszłe natywne testy BRB (R2)
muszą porównywać zakresy NVS przed i po ładowaniu oraz odróżniać zapisy BRB/jądra od zmian wykonywanych przez
firmware. Testy na OVMF nie dowodzą zgodności ze sprzętem fizycznym.

## 13. Wyniki

Artefakty: `.brb-build/r1b/`, `.brb-build/qemu-diag/r1b-*`.

| Element | Wynik |
|---|---|
| Jądro R1B (wariant produkcyjny) | ELF `0d0a143c…`, okrojone `72ddb782…`; ET_EXEC, `e_entry` = `_start` = `0x1193E2`, `wejscie_jadra64` = `0x11960A`, metadane pod offsetem pliku `0x1050` (także po `strip`), koniec obrazu `0xD7C000`; konsolidacja deterministyczna; 0 nowych ostrzeżeń |
| Wariant testowy (`BURSZTYN_TEST_WEJSCIE64=1`) | ELF `8b841dbe…`, okrojone `a048212e…` |
| `test-r1b-wejscie-host` | PASS = 187, FAIL = 0 (ASan/UBSan) |
| `test-r1a-pamiec-host` | PASS = 501, FAIL = 0 |
| Mutanty R1B (14) | wszystkie wykryte |
| QEMU: wariant testowy, GRUB BIOS, **PA ≠ VA** | **PASS do pulpitu**: obraz przeniesiony z PA `0x100000` pod `0x2005000`, wejście przez `wejscie_jadra64`; monitor QEMU (`gva2gpa`) przy działającym pulpicie: VA `0x100000` → PA `0x2005000`, VA `0x2AE000` → `0x21B3000`, VA `0x2005000` (alias) **niezmapowany**, CR3 `0x2A4E000` w przeniesionym obrazie |
| QEMU: jądro produkcyjne R1B, GRUB BIOS (MB2) | PASS do pulpitu, VA == PA |
| QEMU: jądro produkcyjne R1B, GRUB UEFI z S3 (diagnostyczny) | odmowa: `OBRAZ_NA_ACPI_NVS` pod `0x800000` (bez zmian) |
| BRB OVMF (13 scenariuszy + jądro R1B) | PASS, kody jak w 4A-R; jądro R1B przez BRB: E506 + wycofanie; `brb-x64.efi` bez zmian (`29132619…`) |
| Produkcyjne jądro wykonane przez BRB z PA ≠ VA | **NIE URUCHOMIONO** — wymaga R2 i 4C (obowiązkowa bramka końcowa) |

## 14. Wymagania wstępne R2, 4B i 4C

1. BRB: polityka przydziału ciągłego regionu przez firmware (`AllocatePages` `AllocateMaxAddress < 4 GiB`
   z wyrównaniem z metadanych), transakcyjnie, z istniejącym wycofaniem. Zakaz nadpisywania zakresów
   firmware/NVS.
2. BRB: kopiowanie segmentów wg `p_vaddr − baza`, zerowanie BSS, weryfikacja bajt po bajcie (istniejący kod 4A
   z innym celem).
3. BRB: wykrycie wejścia przez `znajdz_metadane_wejscia`, budowa kontraktu (`ZRODLO_NATYWNE_64`, bit 2), MBI
   w formacie MB2 (do Fazy 7) i tablic przez `PlanPrzejscia` (pula, stos, kontrakt, MBI w zakresach
   odzyskiwalnych/informacji).
4. 4C: `ExitBootServices` z ponowieniami (T-EBS), potem przełączenie CR3 i `jmp wejscie_jadra64` bez usług UEFI.
5. Bramki akceptacji:
   - produkcyjne jądro przez BRB z PA ≠ VA do pulpitu;
   - zrzut ACPI NVS przed/po = 0 zmienionych bajtów przypisanych BRB/jądru;
   - W^X (§8);
   - sprzęt fizyczny.

## 15. Ryzyka

| Ryzyko | Stan |
|---|---|
| W^X jądra nieegzekwowane, alias HHDM `.text` | udokumentowane, bramka przed R2/produkcją |
| Konsumenci MBI/modułów przez PA-jako-wskaźnik | za bramką; migracja w Fazie 7 |
| Okno ACPI wspólne dla wszystkich tabel spoza tożsamości | udokumentowane |
| Wyjątek przed `InicjalizujIDT` na ścieżce natywnej = potrójny błąd | nieodłączne; kontrakt minimalizuje okno |
| Uprząż testowa kopiuje `.data` w stanie z początku `kernel_main` (jedyne zmiany przed kopią: kanarek stosu, tablice `boot.S` w `.bss`, które są zerowane) | ograniczone do wariantu testowego |
| GRUB UEFI z S3 | jądro odmawia startu (wykrycie, nie zapobieganie) |
