# BRB — plan rozwoju, rejestr ryzyk, decyzje właściciela

Status: **[PROJEKT]**. Każda faza to osobna, recenzowana zmiana, testowalna niezależnie i odwracalna. Produkcja
(GRUB, `BursztynOS.iso`) pozostaje nietknięta aż do Fazy 15 i jawnej decyzji P-11.

Kolejność różni się od zaproponowanej w zadaniu w jednym miejscu: integrację natywnego protokołu (Faza 7)
umieszczono **po** obsłudze modułów i sesji Live w trybie zgodności (Faza 6). Uzasadnienie: tryb zgodności MB2
pozwala zweryfikować BRB na niezmienionym jądrze. Zmiana ABI jądra jest wtedy osobnym, mierzalnym krokiem
(A-02, A-03).

---

## 1. Fazy

### Faza 1 — Audyt i specyfikacja (ta zmiana)

| | |
|---|---|
| Wymagania wstępne | gałąź `feature/bootloader`, czyste drzewo |
| Zakres | dokumenty `docs/brb/*.md`; zero zmian w kodzie i buildzie |
| Rezultaty | 10 dokumentów |
| Akceptacja | przegląd właściciela; decyzje P-01…P-13 |
| Ryzyka | błędne założenie o zachowaniu firmware → mitygacja: oznaczenia [DO POTWIERDZENIA] |
| Wycofanie | usunięcie `docs/brb/` |

### Faza 2 — Minimalna aplikacja UEFI z diagnostyką szeregową

| | |
|---|---|
| Wymagania | P-01 (decyzja o katalogach), P-02 (metoda budowy PE — spike A/B) |
| Zakres | `rozruch/brb/uefi/uefi_typy.h`, `efi_main` wypisujący U1 na COM1 i tekst PL na ConOut, `pamiec_podstawowa.cpp`, cel `brb-uefi`, `test-brb-host` z T-PE |
| Rezultaty | `.brb-build/uefi/brb-x64.efi` |
| Akceptacja | T-PE; `nm -u` pusty; (za zgodą) OVMF: `[BRB] etap 1: wejscie UEFI` i powrót do firmware po klawiszu |
| Ryzyka | GNU ld + PE (R-01) |
| Wycofanie | usunięcie katalogu i celu; produkcja nietknięta |
| **Status (2026-11-27)** | **zrealizowana, czeka na akceptację właściciela**. Metoda A z własnym skryptem linkera, bez `--gc-sections` (R-01 zamknięte z ograniczeniem, `architektura-uefi.md` §12.1); host: `make -j1 test-brb-host` PASS; OVMF: scenariusze `klawisz` i `limit` PASS (relokacja pod adres ≠ `ImageBase`). Odkryte: konsola OVMF nie wyświetla liter spoza Latin-1 i zgłasza fałszywe `EFI_SUCCESS` w `TestString` → domyślna transliteracja ASCII (§14.4) |

### Faza 3 — Bezpieczne ładowanie obrazu jądra (UEFI)

| | |
|---|---|
| Wymagania | Faza 2 |
| Zakres | `wspolne/elf64`, `zakresy`, `log`, `uefi/pliki` (SFS), walidacja ELF, plan ładowania, ładowanie do pamięci bez przekazania |
| Rezultaty | BRB ładuje `system_operacyjny.bin` i wypisuje plan (segmenty, SHA-256) |
| Akceptacja | T-ELF, T-ELF-PROD, T-ZAK, T-ALLOC; (za zgodą) OVMF: U3 i poprawne skróty |
| Ryzyka | zakres 1–14 MiB zajęty w firmware (R-02) |
| Wycofanie | jw. |

**Status Fazy 3 (2026-11-27): zrealizowana, czeka na akceptację właściciela.** Odczyt `\boot\system_operacyjny.bin` z woluminu BRB (SFS, tylko odczyt), walidacja ELF64 (`wspolne/elf64.cpp`), plan ładowania; host (ASan/UBSan, mutacje, fuzzer 200 000 iteracji) PASS; T-ELF-PROD na jądrze wydania `303cb82e…` PASS (pełna zgodność z readelf/objdump); OVMF 9/9 scenariuszy PASS. Jądro produkcyjne zgodne z architekturą BRB — bez zmian w jądrze. Szczegóły: `architektura-uefi.md` §17. Istniejący problem budowania D-14 (cel domyślny `virtio_gpu.o`) udokumentowany, nienaprawiony.

### Faza 4A — Przydział stron, ładowanie i weryfikacja jądra (bez przekazania)

**Status Fazy 4A (2026-11-27): BLOKADA (BLOCKED) dla jądra produkcyjnego w OVMF; czeka na decyzję właściciela.**
Zaimplementowano migawkę mapy pamięci, transakcyjny przydział `AllocateAddress` z pełnym wycofaniem, kopiowanie,
zerowanie BSS i niezależną weryfikację bajt po bajcie oraz zwolnienie zasobów (`architektura-uefi.md` §18).
Testy hosta (ASan/UBSan, wstrzykiwanie błędów, symulowana pamięć ze strażnikami) PASS; OVMF: jądro syntetyczne
PASS, scenariusz konfliktu PASS, regresje Faz 2–3 PASS. Jądro `303cb82e…`: zakres `0x2AA000–0xD6F000`
odrzucany przez OVMF (`EfiBootServicesData` od `0x900000`/`0x820000`, przy S3 także ACPI NVS
`0x800000–0x900000`) → BRB-E506. Fazy 4B/4C nie rozpoczęto. Propozycje rozwiązań: §18.7.

**Faza 4A-R (2026-11-27): analiza zakończona, decyzja PROPONOWANA** (`decyzja-relokacja-jadra.md`): GRUB w tej samej
konfiguracji ładuje jądro przez bufor pośredni i po EBS nadpisuje ACPI NVS; rekomendacja — opcja C (fizycznie ruchome
jądro ze stałymi VA) realizowana przez natywne wejście 64-bit BRB. Bez zmian w jądrze do czasu akceptacji.

**Decyzja 2026-11-27: opcja C + E ZAAKCEPTOWANA** (`decyzja-relokacja-jadra.md` §0). Etap R1 podzielono na
**R1A** (rozdzielenie PA/VA, kontrakt pamięci rozruchu, fundamenty PMM/VMM) i **R1B** (wejście 64-bit jądra,
integracja z bootstrapem). **Faza R1A: zrealizowana jako fundament, czeka na przegląd** (`r1a-rozdzielenie-adresow.md`).
Jedynym wejściem jądra pozostaje MB2 (PA == VA). Natywny test PA ≠ VA jest obowiązkową bramką R1B/R2.
Skutek dla wydań: ścieżka GRUB UEFI na platformie z ACPI NVS pod obrazem (OVMF z S3) kończy się teraz jawnym
błędem własności pamięci zamiast cichego nadpisania NVS (`r1a-rozdzielenie-adresow.md` §12). **Faza 4A pozostaje
BLOKADĄ (BLOCKED)**, dopóki BRB nie załaduje jądra produkcyjnego bezpiecznie (R2). Fazy 4B/4C nie rozpoczęto.

**Faza R1B (2026-11-27): zrealizowana jako kamień milowy wejścia 64-bit i bootstrapu, czeka na przegląd**
(`r1b-wejscie-64bit.md`). Jądro ma drugie, niezależne wejście `wejscie_jadra64` (wykrywalne w okrojonym ELF przez
metadane `BRBJADRO`), oba wejścia zbiegają się w `start_jadra`. Wariant testowy przeniósł jądro fizycznie
(PA `0x2005000`, VA bez zmian) i uruchomił je do pulpitu. Produkcyjne ładowanie przez BRB (R2), `ExitBootServices`
i przekazanie (4C) nie zostały rozpoczęte. **Faza 4A pozostaje BLOKADĄ.** W^X jądra nadal nieegzekwowane (bramka).

### Faza 4 — Informacje rozruchowe i przekazanie (tryb zgodności MB2)

| | |
|---|---|
| Wymagania | Faza 3 |
| Zakres | `mapa_pamieci`, `informacje` (MBI), GOP, RSDP, EBS z ponowieniami, trampolina 64→32, plan przeniesień |
| Rezultaty | BRB przekazuje sterowanie `_start` |
| Akceptacja | T-MAP, T-MB2, T-EBS, T-TRAMP, T-GOP, T-RSDP, T-ZGD-1/2/4; (za zgodą) OVMF: U7 + log jądra do `framebuffer_ready` |
| Ryzyka | R-03 (EBS), R-04 (trampolina), R-NMI |
| Wycofanie | jw. |

### Faza 5 — Eksperymentalny rozruch UEFI Bursztyn OS

| | |
|---|---|
| Wymagania | Faza 4 |
| Zakres | `BRB.CFG` (podzbiór: jeden wpis, moduły), `iso-brb` (xorriso, `efi.img` z BRB + jądrem + modułami niezbędnymi), generator konfiguracji |
| Rezultaty | `BursztynOS-BRB-eksperymentalny.iso` (nigdy `BursztynOS.iso`) |
| Akceptacja | kryteria 1–5 z `plan-testow.md` §6 w Q-U1 |
| Ryzyka | R-07 (duży `efi.img`) |
| Wycofanie | usunięcie celu `iso-brb` |

### Faza 6 — Wszystkie moduły i sesje Live

| | |
|---|---|
| Wymagania | Faza 5 |
| Zakres | czytnik ISO9660 na `BLOCK_IO`, moduły opcjonalne (galeria, muzyka), wpis instalatora (moduły `installer.efi`/`installer.kernel`), pełny parser `BRB.CFG`, T-CFG, T-MOD, T-ISO, fuzzing |
| Rezultaty | ISO eksperymentalne z pełnym zestawem wpisów Live |
| Akceptacja | `plan-testow.md` §6 pkt 1–6 (instalacja z BRB-ISO na tymczasowy dysk — instaluje dzisiejszy GRUB, bez zmian instalatora) |
| Ryzyka | BlockIO na różnych firmware (R-08) |
| Wycofanie | jw. |

### Faza 7 — Integracja natywnego protokołu BRBI

| | |
|---|---|
| Wymagania | Faza 6; P-01 (nagłówek w `rozruch/protokol/`) |
| Zakres | **pierwsza zmiana jądra**: `brb_naglowek_jadra` w `.boot`, `_start_brb64`, adapter `InformacjeRozruchu` (MB2 i BRBI), migracja konsumentów (PMM, grafika, ACPI, instalator, tapety, BSF, aktualizacja) na adapter, naprawa D-01…D-04, D-12; BRB: budowa BRBI, wybór protokołu wg nagłówka |
| Rezultaty | jądro startuje z GRUB (MB2) **i** z BRB (BRBI) |
| Akceptacja | T-BRBI, T-ZGD-5; wszystkie istniejące testy hosta; (za zgodą) QEMU z GRUB **i** BRB z identycznymi logami jądra |
| Ryzyka | regresja wczesnego startu (R-05) |
| Wycofanie | BRB wraca do MB2 (wybór protokołu); zmiany jądra rewertowalne jednym commitem |

### Faza 8 — Rozruch zainstalowanego systemu (UEFI)

| | |
|---|---|
| Wymagania | Faza 7 (lub 6 w trybie MB2) |
| Zakres | `stan_ab` (odczyt LBA, wybór slotu, zapis licznika prób), weryfikacja SHA-256 slotu, parametry `bursztyn.installed=1 bursztyn.slot=X`; BRB uruchamiany **ręcznie** z nośnika testowego (np. z powłoki UEFI) na tymczasowym dysku zainstalowanym przez GRUB |
| Rezultaty | BRB uruchamia zainstalowany system bez instalowania BRB na dysku |
| Akceptacja | T-AB; Q-U4/Q-U5 z BRB uruchamianym z drugiego nośnika |
| Ryzyka | rozbieżność semantyki prób (R-06) |
| Wycofanie | brak zmian na dysku poza stanem LBA (ten sam format) |

### Faza 9 — Legacy BIOS (wieloetapowy)

| | |
|---|---|
| Wymagania | Faza 6 (wspólny kod), P-04, P-06 |
| Zakres | etap 0-CD, etap 0-MBR (dla ISO hybrydowego), etap 2 (thunk, INT 13h, A20, E820, VBE, RSDP), czytnik ISO9660/FAT32 |
| Rezultaty | ISO eksperymentalne bootowalne na BIOS (CD i USB) |
| Akceptacja | T-BIOS-*; Q-B1…Q-B3 |
| Ryzyka | RB-1…RB-7 |
| Wycofanie | usunięcie wpisu BIOS z ISO eksperymentalnego |

### Faza 10 — Wspólna konfiguracja niezależna od firmware

| | |
|---|---|
| Wymagania | Fazy 6, 9 |
| Zakres | jeden generator `BRB.CFG` dla UEFI/BIOS/Live/zainstalowanego; lista dozwolonych parametrów; usunięcie edycji `grub.cfg` regexami z procesu ISO eksperymentalnego |
| Akceptacja | T-CFG; identyczne zachowanie wpisów na UEFI i BIOS |
| Wycofanie | jw. |

### Faza 11 — Polskie menu i opcje odzyskiwania

| | |
|---|---|
| Wymagania | Faza 10; P-07 (opcje jądra: tryb awaryjny/diagnostyczny, poprzedni slot) |
| Zakres | menu graficzne (GOP/VBE) i tekstowe, klawiatura, odliczanie, `ResetSystem` |
| Akceptacja | testy hosta renderu (porównanie bufora), Q-U1/Q-B1 ręcznie i zrzutami |
| Wycofanie | `limit_czasu = 0` wyłącza menu |

### Faza 12 — Integracja z instalatorem i aktualizacjami

| | |
|---|---|
| Wymagania | Fazy 8, 10; P-03, P-05 |
| Zakres | moduł `installer.brb`, nowy układ ESP (`integracja-instalatora.md` §3), rezerwa pojemności slotów (D-05), usunięcie D-06/D-07 w nowych instalacjach, migracja istniejących instalacji (§6), aktualizacja BRB (§7) |
| Akceptacja | `test-instalacja-host`, `test-storage`, nowe testy migracji na obrazach tymczasowych; Q-U4/Q-U5 |
| Ryzyka | R-09 (migracja istniejących instalacji) |
| Wycofanie | instalator nadal potrafi instalować GRUB (flaga budowy) do Fazy 15 |

### Faza 13 — Utwardzanie bezpieczeństwa i szerokie testy regresji

| | |
|---|---|
| Wymagania | Faza 12; P-08, P-09 |
| Zakres | manifest i podpisy, lista dozwolonych parametrów, fuzzing w CI, przegląd kodu pod kątem `bezpieczenstwo.md` §2, Secure Boot (wybrana droga) |
| Akceptacja | ≥ 10⁶ wykonań fuzzera na cel; testy mutacyjne; testy odmowy niepodpisanych artefaktów |

### Faza 14 — Testy akceptacyjne QEMU i sprzętu

| | |
|---|---|
| Zakres | pełna macierz `plan-testow.md` §4–5 |
| Akceptacja | wszystkie kryteria §6 na QEMU (UEFI/BIOS, VirtIO/legacy, ISO/USB/dysk, 512 MiB–8 GiB) i na co najmniej 2 maszynach fizycznych UEFI i 1 BIOS |

### Faza 15 — Usunięcie produkcyjnej zależności od GRUB

| | |
|---|---|
| Wymagania | Faza 14; decyzja P-11 |
| Zakres | `iso` budowane przez BRB, instalator instaluje BRB, usunięcie GRUB z nośników (`licencje-i-zaleznosci.md` §3), aktualizacja dokumentacji (`03_proces_rozruchu.md`, `aktualizacja-systemu.md`) |
| Wycofanie | tag wydania z ostatnim ISO GRUB; ścieżka MB2 w jądrze pozostaje (P-11) |

---

## 2. Rejestr ryzyk

| ID | Ryzyko | Prawd. | Wpływ | Priorytet | Mitygacja |
|---|---|---|---|---|---|
| R-01 | GNU ld nie wytworzy poprawnego PE32+ z relokacjami z wejść ELF | średnie | średni | wysoki | spike w Fazie 2; metoda B (samorelokacja + objcopy) jako zapas |
| R-02 | zakres 1 MiB…`__kernel_end` zajęty przez firmware (BS data) | **wystąpiło** (OVMF, 2026-11-27) | wysoki | krytyczny | potwierdzone w Fazie 4A (`architektura-uefi.md` §18.6): BS data i (przy S3) ACPI NVS w `0x800000–0xD6F000`; kopiowanie po EBS nie usuwa konfliktu z ACPI NVS; decyzja właściciela między zmianą adresu jądra, relokacją a analizą kopiowania po EBS (§18.7) |
| R-03 | zmiana mapy między GetMemoryMap a EBS | średnie | wysoki | wysoki | ≤ 4 próby, brak wywołań w oknie, T-EBS |
| R-04 | błąd trampoliny 64→32 (trudny do debugowania, potrójny błąd) | średnie | wysoki | wysoki | identity map, kod w jednej stronie, gdb QEMU (`gdb_qemu.log` — istniejąca praktyka), kamienie COM1 przed każdym krokiem; przejście na BRBI 64-bit w Fazie 7 |
| R-05 | regresja wczesnego startu jądra przy zmianie ABI | średnie | wysoki | wysoki | adapter z oboma formatami, testy T-ZGD, GRUB jako ścieżka porównawcza |
| R-06 | różna semantyka prób A/B (`grubenv` vs LBA) | średnie | średni | średni | T-AB na prawdziwym `aktualizacja_rozruch.h`; uzgodnienie przed Fazą 8 |
| R-07 | duży `efi.img` (> 32 MiB) a El Torito *Sector Count* | średnie | średni | średni | Faza 6: czytnik ISO9660 i mały `efi.img`; w Fazie 5 zmierzyć rozmiar |
| R-08 | różnice `BLOCK_IO`/DevicePath między firmware | średnie | średni | średni | testy na OVMF + sprzęcie; zapas: pliki niezbędne w `efi.img` |
| R-09 | migracja istniejącej instalacji zostawi system bez działającego rozruchu | niskie | krytyczny | wysoki | kopia GRUB, łańcuchowy zapas, nośnik Live, potwierdzenie po starcie (`integracja-instalatora.md` §6) |
| R-10 | framebuffer RGBX na sprzęcie (D-09) | średnie | średni | średni | komunikat BRB-E040; obsługa XBGR w jądrze jako osobna zmiana |
| R-11 | Secure Boot uniemożliwia start na sprzęcie użytkowników | wysokie | średni | średni | decyzja P-09; dokumentacja dla użytkownika |
| R-12 | wzrost jądra przekroczy pojemność slotów (D-05) | wysokie (z czasem) | wysoki | wysoki | rezerwa pojemności w Fazie 12; do tego czasu kontrola rozmiaru w pakiecie aktualizacji |
| R-NMI | NMI w oknie bez IDT | niskie | wysoki | niski | jak w `bezpieczenstwo.md` §6 |
| R-LIC | niepełne informacje licencyjne GRUB na ESP | — | prawny | średni | P-10 |

---

## 3. Decyzje wymagające potwierdzenia właściciela

| ID | Pytanie | Rekomendacja |
|---|---|---|
| P-01 | Katalogi: `rozruch/brb/` (kod BRB) i `rozruch/protokol/` (wspólne ABI)? | tak |
| P-02 | Metoda budowy PE32+: A (`ld -m i386pep`) czy B (samorelokacja + objcopy) | spike w Fazie 2, preferencja A |
| P-03 | Migracja istniejących instalacji GRUB→BRB: automatyczna przez aktualizację czy tylko nowe instalacje? | najpierw tylko nowe instalacje; migracja jako opcja po Fazie 14 |
| P-04 | Czy zainstalowany system ma obsługiwać rozruch BIOS (nowa partycja GPT „BRB BIOS” w nowych instalacjach)? | nie w pierwszym wydaniu; BIOS tylko dla Live |
| P-05 | Pojemność slotów jądra na ESP (np. 32 MiB/slot) | 32 MiB |
| P-06 | Poziom nazw ISO9660 i (nie)stosowanie Rock Ridge/Joliet w ISO BRB | ISO poziom 3 dla plików BRB/modułów, Rock Ridge zachowany dla reszty (czytnik BRB go ignoruje) |
| P-07 | Opcje jądra dla trybu awaryjnego/diagnostycznego i ręcznego wyboru slotu (zmiana polityki Z-14) | zdefiniować przed Fazą 11 |
| P-08 | Algorytm podpisów: RSA (spójny z aktualizacjami) czy Ed25519 | RSA-3072 + SHA-256 (istniejący kod mbedTLS) |
| P-09 | Droga Secure Boot: S1 (własne klucze), S2 (shim), S3 (podpis CA) | S1 w Fazie 13, ocena S2/S3 później |
| P-10 | Informacje licencyjne GRUB na ESP zainstalowanego systemu | dodać do czasu usunięcia GRUB |
| P-11 | Kryteria i moment usunięcia GRUB; czy jądro zachowuje ścieżkę MB2 na stałe | zachować MB2 jako ścieżkę developerską |
| P-12 | Licencja BRB (ta sama co projekt?) | PolyForm Noncommercial 1.0.0 jak całość |
| P-13 | Czy nośnik użytkownika ma zawierać wpisy testów automatycznych (dziś są w `BursztynOS.iso`) | przenieść do ISO testowych |

---

## 4. Plan Fazy 2 (do natychmiastowej realizacji po zatwierdzeniu)

1. **Spike PE (½–1 dzień)**:
   - minimalny `efi_main` (ms_abi), wypisanie jednego znaku na COM1 i `"Bursztynowy Program Rozruchowy"`
     przez ConOut;
   - zbudowanie metodą A;
   - inspekcja `objdump -p -h -r`: PE32+, subsystem 10, `.reloc`, brak W+X;
   - przy porażce metoda B.
2. **Nagłówki**: `rozruch/brb/uefi/uefi_typy.h` — `EFI_STATUS`, `EFI_HANDLE`, `EFI_GUID`, `EFI_TABLE_HEADER`,
   `EFI_SYSTEM_TABLE`, `EFI_BOOT_SERVICES` (pełny układ wskaźników do `CreateEventEx`, bo kolejność pól jest
   ABI), `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL`, `EFI_SIMPLE_TEXT_INPUT_PROTOCOL`, `EFI_LOADED_IMAGE_PROTOCOL`
   z GUID. `static_assert` przesunięć pól (np. `offsetof(EFI_BOOT_SERVICES, ExitBootServices)`) wyliczonych
   ze specyfikacji.
3. **Wspólne**: `pamiec_podstawowa.cpp` (`memcpy`/`memset`/`memmove`/`memcmp`), `log.cpp` (COM1 jak
   `serial_inicjalizuj`), `komunikaty.h` (UTF-8 → UCS-2 dla ConOut, test hosta konwersji polskich znaków).
4. **Build**:
   - cele `brb-uefi` i `test-brb-host` w `Makefile`, bez zależności od `iso`/`kernel`;
   - `make -j1 test-brb-host` z ASan/UBSan;
   - kontrola, że `make kernel` i `make iso` są bajtowo niezmienione (porównanie SHA-256 artefaktów
     przed/po).
5. **Test hosta T-PE**: parser nagłówków PE32+ zbudowanego obrazu.
6. **(Za zgodą)** jeden przebieg OVMF: `-bios OVMF`, `-drive format=raw,file=fat:rw:<katalog tymczasowy>`
   (wirtualny FAT QEMU z katalogu tymczasowego, bez obrazów użytkownika), `-serial file:` → oczekiwane
   `[BRB] etap 1: wejscie UEFI`.
7. **Dokumentacja**: uzupełnienie `architektura-uefi.md` §12 o wynik spike'a (wybrana metoda, flagi).

Kryterium zakończenia Fazy 2:

- punkty 1–5 przechodzą na hoście;
- punkt 6 wykonany za zgodą albo jawnie odłożony;
- zero zmian w produkcyjnych artefaktach.
