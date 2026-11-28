# BRB — Faza R2: elastyczne umieszczenie fizyczne jądra, własność pamięci i tablice przejścia

Stan: **zrealizowana, czeka na przegląd właściciela (2026-11-28).** Gałąź `feature/bootloader`, baza `6dc0fdd` (R1B).
Faza R2 jest **diagnostyczna**: BRB ładuje i weryfikuje jądro pod adres wybrany przez firmware, buduje i niezależnie
weryfikuje tablice przejścia, po czym **zwalnia wszystkie zasoby i wraca do firmware**. Nie ma `ExitBootServices`,
skoku do jądra, zmian CR0/CR3/CR4/EFER, GDT/IDT, informacji rozruchu (4B) ani przekazania (4C).
**Bursztyn OS nie został uruchomiony przez BRB w R2.**

Powiązane: `decyzja-relokacja-jadra.md` (opcja C + E), `r1a-rozdzielenie-adresow.md`, `r1b-wejscie-64bit.md`,
`architektura-uefi.md` §18 (historyczna ścieżka 4A).

---

## 1. Aktywna architektura natywna (6 kroków)

Zastępuje model „stały adres + kopiowanie po EBS” (`architektura-uefi.md` §5.2, §6.2, §10.1–10.3, §18.7 —
oznaczone jako **historyczne / ścieżka zgodności**).

| Krok | Treść | Faza | Stan |
|---|---|---|---|
| 1 | walidacja ELF64 (polityka Fazy 3 bez zmian) i metadanych `BRBJADRO` **przed** jakimkolwiek przydziałem | R2 | zrealizowane |
| 2 | przydział ciągłego podkładu przez firmware: `AllocatePages(AllocateMaxAddress, EfiLoaderData)`, < 4 GiB | R2 | zrealizowane |
| 3 | kopiowanie segmentów, zerowanie BSS/luk, niezależna weryfikacja bajt po bajcie — **przed EBS** | R2 | zrealizowane |
| 4 | zasoby przejścia we własności BRB (pula tablic, stos, strona kodu, rezerwa informacji) + tablice przejścia i ich niezależna weryfikacja | R2 | zrealizowane (tablice nieaktywne) |
| 5 | informacje rozruchu (kontrakt V1, mapa, MBI w formacie MB2, GOP, RSDP) | 4B | **nie rozpoczęto** |
| 6 | `ExitBootServices` (z ponowieniami), przełączenie CR3, bezpośredni skok do `wejscie_jadra64` | 4C | **nie rozpoczęto** |

Numeracja faz zachowana; BRBI pozostaje Fazą 7. Brak kopiowania po EBS: obraz leży w pamięci, którą firmware
**przydzieliło BRB** jako `EfiLoaderData` — po EBS jest to pamięć ładowarki, której nikt poza BRB/jądrem nie używa.

## 2. Wykrywanie wejścia natywnego (część 5)

- `program.cpp`: po Fazie 3 wywołuje `znajdz_metadane_wejscia` (ten sam parser ELF, te same bajty w pamięci).
  Metadane są walidowane **przed** prośbą o pamięć (rozmiar, wersja, kontrakt, sygnatury, wyrównanie, baza zgodna
  z planem, wejście w segmencie wykonywalnym, jednoznaczność w oknie 32 KiB).
- Brak/uszkodzone metadane → `BRB-E7xx`, komunikat „Brak próby ładowania pod stały adres.” i powrót do firmware.
  **Nie ma cichego powrotu do ścieżki stałego adresu.**
- Historyczna ścieżka 4A (stały adres `AllocateAddress`) istnieje wyłącznie w wariantach skompilowanych z
  `-DBRB_DIAG_STARY_4A` (`make brb-uefi-diag-4a`, wariant konfliktu, `test-brb-host`) i tylko dla jądra bez metadanych
  (`NieZnaleziono`), z logiem „TRYB DIAGNOSTYCZNY 4A”. Normalny `brb-x64.efi` nie zawiera tego napisu (sprawdzone).
- `e_entry` (wejście MB2, `0x1193E2`) nie jest używane przez ścieżkę natywną; wejście natywne pochodzi z metadanych
  (`0x11960A` w R1B — obserwacja, nie stała BRB).

## 3. Matematyka umieszczenia (części 6–7)

Rozdzielone pojęcia: **VA łączenia** (`p_vaddr`, bez zmian), **`p_paddr`** (w ELF = VA; nie jest celem),
**przesunięcie** `p_vaddr − baza_wirtualna`, **baza fizyczna** wybrana w przydziale.

```
cel_fiz(seg) = baza + (p_vaddr − baza_wirtualna)          // model ciągły, sprawdzana arytmetyka
wejscie_fiz  = baza + (wejscie64 − baza_wirtualna)
```

- `wymagania_obrazu` (`wspolne/umiejscowienie.cpp`): plan musi być tożsamościowy i ciągły (`p_paddr == p_vaddr`,
  strony w zakresie planu), baza metadanych = początek planu, wyrównanie = max(metadane, `p_align` segmentów),
  4 KiB ≤ wyrównanie ≤ 1 GiB, potęga 2, kongruencja `p_vaddr mod p_align`. Rozmiar przydziału = strony obrazu +
  dopełnienie na wyrównanie (`wyrównanie/4K − 1`).
- `wybierz_baze`: baza = najmniejszy wyrównany adres w przydziale; kontrola przepełnień, końca ≤ 4 GiB (limit
  tymczasowy do 4B/4C), końca < 2^MAXPHYADDR. Dopełnienie przed/po jest **posiadane, niezapisywane i nie jest
  częściowo zwalniane** (całość zwalniana jednym `FreePages`).
- `przesun_plan` (`wspolne/ladowanie.cpp`): przesuwa plan zweryfikowany polityką `POLITYKA_JADRA_BURSZTYN`; ładowarka
  przy ponownym wyliczeniu planu (obrona przed niespójnością) przesuwa go **tą samą bazą** i porównuje — walidacja nie
  jest osłabiona (polityka 1–16 MiB/MB2 nadal sprawdzana na planie wzorcowym).
- Przydział: `AllocateMaxAddress`, `Memory = 0xFFFFFFFF`, `EfiLoaderData`. **Brak awaryjnego `AllocateAddress`.**
  Adres `0x2005000` (uprząż R1B) nie występuje w kodzie BRB.

## 4. Własność pamięci (część 8, 15)

`wspolne/rejestr_zasobow.cpp` — stała tablica 16 wpisów `{adres, strony, rodzaj, chroniony, wlasny, porzucony}`:

| Rodzaj | Pochodzenie | Własny |
|---|---|---|
| obraz BRB | `LoadedImage.ImageBase/ImageSize` | nie (chroniony) |
| bufor źródła | bufor pliku jądra (Faza 2) | nie (chroniony) |
| podkład jądra | `AllocateMaxAddress EfiLoaderData` | tak |
| tablice stron (64 ramki) | `AllocateMaxAddress EfiLoaderData` | tak |
| stos przejścia (4 strony) | `AllocateMaxAddress EfiLoaderData` | tak |
| kod przejścia (1 strona) | `AllocateMaxAddress EfiLoaderCode` | tak |
| informacje rozruchu (16 stron) | `AllocateMaxAddress EfiLoaderData` | tak |
| strażnik testowy | tylko wariant testowy 4 | tak (chroniony przed zapisem) |

- Każdy zwrócony adres jest sprawdzany: wyrównanie, przepełnienie, < 2^47, **rozłączność ze wszystkimi żywymi
  wpisami** (obraz BRB, źródło, wcześniejsze przydziały). Kolizja → natychmiastowe `FreePages` tego przydziału i błąd.
- Migawki `GetMemoryMap` przed i po przydziałach: logowane wszystkie zakresy ACPI NVS oraz deskryptory pokrywające
  każdy zasób; nakładanie własnego przydziału na NVS → `BRB-E615` (nie wystąpiło). BRB pisze wyłącznie do pamięci
  przydzielonej sobie przez firmware — nigdy do NVS, Reclaim, Runtime, Reserved, MMIO ani Unusable.
- Sprzątanie `rejestr_zwolnij_wszystko`: odwrotna kolejność, `wlasny=false` **przed** wywołaniem (brak podwójnego
  zwolnienia), chronione pomijane, błąd `FreePages` → wpis „porzucony”, licznik błędów, `BRB-E616`. Ta sama ścieżka
  na sukcesie i każdej porażce.

## 5. Ładowanie i weryfikacja (część 9)

- Źródło = te same, już zwalidowane bajty pliku w pamięci (bez ponownego odczytu dysku).
- Polityka bajtów: `p_filesz` kopiowane, `p_memsz − p_filesz` zerowane, luki między segmentami i wypełnienie do
  końca stron **zerowane**; dopełnienie wyrównania (poza obrazem) nie jest dotykane.
- Niezależna weryfikacja bajt po bajcie (nie CRC): dane = plik, BSS = 0, luki/wypełnienie = 0. Raport SHA-256 pliku
  (Faza 2). Kod jądra nie jest wykonywany.

## 6. Tablice przejścia i ich niezależna weryfikacja (części 10–11)

Budowa: `PlanPrzejscia::zbuduj` (`jadro/mm/plan_przejscia.h`, te same co w R1B), ramki z puli od firmware.
4 poziomy, strony 4 KiB, wyłącznie nadzorca (U=0), okno jądra VA `baza_wirtualna…+rozmiar` → PA `baza…`, uprawnienia
z `p_flags` (`.text` R+X, `.rodata`/`.boot` R+NX, `.data/.bss` RW+NX), ograniczona tożsamość tylko dla: strony kodu
przejścia (R+X), stosu (RW+NX), rezerwy informacji (R+NX) i ramek puli (RW+NX). CR0/CR3/CR4/EFER i tablice firmware
**nie są zmieniane; CR3 nie jest ładowany.**

Weryfikator `wspolne/weryfikacja_tablic.cpp` — **niezależny od budowniczego** (oczekiwania wyprowadza z przesuniętego
planu ELF i listy zasobów, nie z `ObrazJadra::Uklad`):

- CR3 i każda tablica pośrednia wewnątrz puli; brak bitów zarezerwowanych (adres ≥ MAXPHYADDR, PS na PML4E/PDPTE/PDE —
  duże strony zabronione), U=0 na **każdym** poziomie;
- każdy liść sklasyfikowany: strona okna (PA = baza + offset, uprawnienia efektywne = z segmentu), zasób tożsamościowy
  (PA = VA, uprawnienia klasy) — inaczej `MapowanieNieoczekiwane`; liczności = kompletność;
- W+X = 0 (efektywnie, z NX i W wszystkich poziomów); brak aliasu podkładu jądra w tożsamości; tożsamość nie nachodzi
  na okno; strażniki niezmapowane (VA 0, `baza_wirtualna − 4K`, koniec okna);
- `wejscie64` tłumaczy się na `baza + offset` w stronie wykonywalnej; zakres kontraktu (rozmiar `KontraktV1` = 2408 B)
  tożsamościowy R+NX; wymagany NX.

**Tablice są nieaktywne. R2 nie twierdzi, że W^X jest egzekwowane** — opisuje uprawnienia zamierzone w tablicach
przejścia, nie stan wykonania.

## 7. Bezpieczeństwo pierwszego odczytu kontraktu (część 12)

Jądro (R1B) przy wejściu natywnym czyta kontrakt spod RSI **zanim** może cokolwiek zweryfikować o mapowaniu. Dlatego
gwarancje muszą zapewnić BRB i weryfikator przed skokiem:

| Gwarancja | Kto zapewnia w R2 | Bramka |
|---|---|---|
| wyrównanie (8 B), zakres (< 4 GiB, < 2^MAXPHYADDR) | rezerwa informacji wyrównana do 4 KiB, `AllocateMaxAddress < 4 GiB` | 4B |
| mapowanie (tożsamość R+NX całego `KontraktV1`) | weryfikator: `KontraktNiedostepny` | 4B/4C: weryfikacja **po** wypełnieniu, tuż przed EBS |
| nadzorca (U=0) | weryfikator na każdym poziomie | 4C |
| własność (strony BRB, `EfiLoaderData`) | rejestr | 4B: kontrakt musi opisać ten zakres jako `INFORMACJE_ROZRUCHU`/`ODZYSKIWALNY_ROZRUCHU` (wymóg `przyjmij_kontrakt_natywny`) |
| tożsamość ramek tablic (kontrola `cr3_rozruchu`) | pula tożsamościowa RW+NX | 4B: zakres `TABLICE_STRON_ROZRUCHU` w kontrakcie |
| brak zmian po weryfikacji | — | **4C: obowiązkowa ponowna weryfikacja tablic po ostatnim zapisie i po EBS, przed `mov cr3`** |

## 8. Wczesne wyjątki — projekt (część 13, bez implementacji)

Między `mov cr3` w BRB a `InicjalizujIDT` w jądrze nie ma IDT: każdy #PF/#GP/#UD/#DF/NMI kończy się potrójnym błędem
(reset) bez diagnostyki. **CLI nie wystarcza** — nie blokuje NMI, #MC ani wyjątków synchronicznych. Projekt na 4C:

1. BRB ładuje własną minimalną GDT (kod64, dane, TSS) i IDT w stronie kodu przejścia/rezerwie, zmapowane
   tożsamościowo, **przed** przełączeniem CR3; procedury: wypisanie wektora, CR2, RIP i kodu błędu na COM1, `hlt`.
2. TSS z IST: osobne stosy (≥ 4 KiB, ze stroną strażnika) dla #DF, NMI i #MC; #PF na stosie bieżącym.
3. CR3 dostępny: GDT/IDT/TSS/stosy IST muszą być w tożsamości nowych tablic (nowe klasy zasobów w weryfikatorze).
4. Serial: port COM1 wykryty wcześniej (`LogSzeregowy`), procedury bez zależności od usług UEFI (po EBS niedostępne).
5. Jądro zastępuje GDT/IDT w `start_jadra` — wczesna IDT BRB jest porzucana (zakres opisany jako odzyskiwalny).

## 9. W^X — blokada produkcyjna (część 14)

Tablice przejścia mają W^X dla okna jądra, ale własny VMM jądra po przełączeniu nie egzekwuje W^X (stan R1A/R1B).
**W^X pozostaje blokadą produkcyjną.** Zalecany osobny kamień milowy „utwardzenie W^X”: VMM jądra odtwarza uprawnienia
z sekcji, NX dla danych, `.rodata` tylko do odczytu, CR0.WP=1, test negatywny zapisu do `.text`.

## 10. Wstrzykiwanie błędów (część 18)

Wyłącznie warianty `make brb-uefi-test-r2 BRB_R2_TEST=N` (`.brb-build/r2/uefi-test-N/`); normalny EFI ich nie
zawiera (sprawdzone: brak napisów `R2 TEST`, build odtwarzalny):

| N | Scenariusz | Oczekiwanie |
|---|---|---|
| 1 | żądanie przekraczające pamięć (prawdziwa odmowa firmware) | `BRB-E605`, 0 zasobów |
| 2 | wstrzyknięta odmowa podkładu | `BRB-E605` |
| 3 | wstrzyknięta odmowa puli tablic po załadowaniu | `BRB-E611`, wycofanie podkładu |
| 4 | prawowity przydział strażnika przed podkładem | inna baza, strażnik nienaruszony, sukces |
| 5 | zmiana 1 bajtu w załadowanym obrazie | `BRB-E610` (`EFI_ABORTED`), wycofanie |

## 11. Testy

Host (`make test-brb-r2`, ASan/UBSan, `-Werror -Wshadow`, prawdziwe jądro R1B): wymagania/wybór bazy (granice
4 GiB, MAXPHYADDR, przepełnienia, wyrównanie 2 MiB), przesunięcie planu (stałe z `readelf`: seg3 @ 0x3005000 →
0x31B3000), ładowanie pod 3 bazy ze strefami strażnika i uszkodzeniami, rejestr (nakładanie, pełność, błąd FreePages,
brak podwójnego zwolnienia), weryfikator + 18 mutacji tablic i oczekiwań, pełna transakcja `faza_r2_zaladuj_natywnie` na
symulowanym UEFI (ms_abi): sukces, inna baza, brak pamięci, odmowy zasobów, błąd FreePages, adres niewyrównany,
podwójny adres, polityka CPU, metadane niezgodne. Kontrole negatywne: 4 mutanty kodu produkcyjnego (translacja,
własność, uprawnienia w budowniczym, W+X w weryfikatorze) — wszystkie wykryte.

OVMF: wyniki w raporcie Fazy R2; skrót w `plan-testow.md` (T-R2-*).

## 12. Ograniczenia i ryzyka

- Migawki mapy przed EBS nie są mapą ostateczną; 4C musi powtórzyć kontrolę NVS na mapie z `ExitBootServices`.
- Limit < 4 GiB jest tymczasowy (kontrakt V1 i tożsamość przejścia); podniesienie wymaga zmian 4B/4C.
- Pula 64 ramek: jądro R1B używa 11–13; przy wzroście obrazu > ~1 GiB wymagana większa pula (błąd jest jawny).
- NX jest wymagany przez politykę BRB (procesor bez NX → `BRB-E602`).
