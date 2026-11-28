# BRB — bezpieczeństwo i odporność

Status: **[PROJEKT]**. BRB działa przed inicjalizacją modelu bezpieczeństwa PZB i w najwyższych
uprawnieniach procesora. Każdy błąd parsera to błąd o skutkach dla całego systemu.

---

## 1. Model zagrożeń

| Źródło danych | Zaufanie | Przykład zagrożenia |
|---|---|---|
| Pliki z nośnika (jądro, moduły, konfiguracja) | **niezaufane** | uszkodzony/zmodyfikowany ELF z segmentem nadpisującym BRB albo firmware |
| Sektory LBA 2046/2047 (stan A/B) | niezaufane (CRC32 = tylko integralność) | podrobiony stan wymuszający start slotu B |
| Struktury systemów plików (FAT, ISO9660) | niezaufane | pętla łańcucha FAT, długość pliku > rozmiar woluminu |
| Struktury firmware (mapa pamięci, GOP, RSDP, E820, VBE) | częściowo zaufane | nakładające się wpisy mapy, pitch < szerokość × 4 |
| Wejście użytkownika (klawiatura w menu) | niezaufane, ograniczone | bardzo długi ciąg parametrów |
| Firmware jako kod | zaufane (poza zakresem BRB) | — |

Cel bezpieczeństwa v1: **żaden niepoprawny plik ani struktura nie może spowodować zapisu poza zakresem
zaplanowanym przez BRB ani wykonania kodu innego niż wejście zwalidowanego jądra**. Autentyczność (podpisy)
to cel późniejszych faz (sekcja 5).

---

## 2. Zasady implementacyjne (obowiązkowe)

1. **Brak alokacji dynamicznej o rozmiarze wynikającym z danych bez limitu.** Każdy bufor ma górny limit stałą:
   jądro ≤ 64 MiB, moduł ≤ 256 MiB, suma modułów ≤ 1 GiB, konfiguracja ≤ 16 KiB, MBI/BRBI ≤ 1 MiB,
   wpisy mapy ≤ 4096, moduły ≤ 256, wpisy menu ≤ 16.
2. **Arytmetyka z kontrolą przepełnienia** (`dodaj_u64`, `mnoz_u64` zwracające `bool`) dla każdego
   `przesuniecie + rozmiar`, `liczba × rozmiar`, `adres + długość`. Wzorzec istnieje w `pmm.cpp`
   (`dodaj_u64`, `wyrownaj_w_gore_do_8`).
3. **Każda pętla ma ograniczenie** liczby iteracji niezależne od danych (np. długość łańcucha FAT ≤ liczba
   klastrów woluminu; liczba rekordów ≤ 1024; próby EBS ≤ 4; odczyty INT 13h ≤ rozmiar pliku / sektor).
4. **Brak rekurencji.** Katalogi przeglądane iteracyjnie ze stałą głębokością ścieżki ≤ 8.
5. **Bufory o stałym rozmiarze** i kopiowanie z jawną długością. Brak `strcpy`/`sprintf`. Teksty zawsze z
   limitem i kontrolą `NUL` w granicach (wzorzec: `instalator.cpp:token`, `bootstrap.inc`).
6. **Walidacja przed użyciem, jednokrotny odczyt** (unikanie TOCTOU): nagłówki są kopiowane do lokalnej
   struktury, walidowane i dopiero używane. Plik jądra jest czytany w całości do bufora przed walidacją.
7. **Rejestr zakresów** (`architektura-uefi.md`, 6.1) gwarantuje rozłączność jądra, modułów, informacji,
   stosu, tablic stron, trampoliny i obrazu BRB. Każdy zapis do pamięci fizycznej docelowej przechodzi przez
   funkcję sprawdzającą przynależność do zakresu z rejestru.
8. **Zakaz zapisu do regionów firmware**: cel musi być typu dopuszczalnego (`architektura-uefi.md`, 6.2;
   E820 typ 1).
9. **Kompilacja**: `-Wall -Wextra -Wpedantic -Werror` dla kodu BRB, `-fno-strict-aliasing`, brak VLA
   (`-Wvla`), `-Wconversion` dla parserów. Host testy z ASan/UBSan (`plan-testow.md`).
10. **Zero wyjątków C++, RTTI, konstruktorów globalnych** (`-fno-exceptions -fno-rtti`, brak `.init_array`).
11. **Jasne polskie komunikaty o błędach** z kodem (np. `BRB-E012`) i bez ujawniania adresów w tekście dla
    użytkownika (adresy tylko na COM1).

---

## 3. Walidacje szczegółowe

| Obiekt | Kontrole | Dokument |
|---|---|---|
| ELF jądra | 12 kontroli (nagłówek, `PT_LOAD`, przepełnienia, W^X, < 4 GiB, ≥ 1 MiB, nakładanie, wejście w segmencie X, nagłówek MB2/BRB) | `architektura-uefi.md` §5.1 |
| Nagłówek MB2 w jądrze | magic, suma, długość ≤ 32 KiB, tagi w granicach, wyrównanie 8 B, tag końca | MB2 §3.1 |
| Moduły | rozmiar > 0 i ≤ limit, etykieta `[a-z0-9._/-]` z NUL, brak duplikatów etykiet, opcjonalnie SHA-256 z manifestu | `specyfikacja-techniczna.md` §7 |
| Konfiguracja | składnia, limity, typy, wymagane pola, duplikaty | `specyfikacja-techniczna.md` §8 |
| FAT32 | sygnatura 0x55AA, `BytsPerSec` ∈ {512, 4096}, `SecPerClus` potęga 2, `RootClus` ≥ 2, łańcuchy w zakresie, wykrywanie cykli (limit), rozmiar pliku ≤ łańcuch × klaster | `architektura-bios.md` §4 |
| ISO 9660 | `CD001`, wersja 1, rozmiar bloku 2048, rekordy w granicach sektora, `LEN_DR ≥ 34`, zakresy plików w woluminie | ECMA-119 |
| GPT (odczyt) | nagłówek `EFI PART`, CRC nagłówka i tablicy, granice LBA — wzorzec `storage_format.cpp:153–160` | UEFI 2.10 rozdz. 5 |
| Stan A/B (LBA 2046/2047) | `rozruch_waliduj_rekord` + wybór kopii wg generacji (`aktualizacja_rozruch.h:126–240`) — **ta sama implementacja** współdzielona z jądrem jako nagłówek C | — |
| Mapa pamięci firmware | wyrównanie, przepełnienia, nakładania, sortowanie | `architektura-uefi.md` §9.2 |
| GOP / VBE | format pikseli, pitch ≥ szerokość × 4, rozmiar bez przepełnienia, LFB ≠ 0 | `architektura-uefi.md` §7 |
| RSDP | sygnatura, sumy, długość | `architektura-uefi.md` §8 |

---

### 3.1. Reguły Fazy 4A (przydział i ładowanie)

> **[HISTORYCZNE — stały adres, tylko wariant `BRB_DIAG_STARY_4A`]** Wymóg `AllocateAddress` pod stały adres
> jądra nie dotyczy już normalnego BRB. Aktywne reguły: §3.2.

- Zapis do pamięci fizycznej wyłącznie po udanym `AllocatePages(AllocateAddress)` tej strony przez BRB; wpis
  „wolna” w mapie nie daje prawa zapisu. Brak odzyskiwania pamięci firmware, brak relokacji, brak kopiowania po EBS.
- Każda kontrola (zakresy, źródło, plan, tłumaczenie adresów) przed **pierwszym** zapisem; odrzucenie = zero zapisów.
- Przydział transakcyjny: odmowa któregokolwiek zakresu → wycofanie wszystkich w odwrotnej kolejności, dowody z mapy,
  deterministyczny kod BRB-E5nn, powrót do firmware.
- Przydział z innym adresem niż żądany jest traktowany jako własność BRB i zwalniany.
- Błędy `FreePages` są raportowane (BRB-E510, `bledy_sprzatania`), zakres porzucany bez ponownego zwalniania.
- Weryfikacja bajt po bajcie (dane, BSS, wypełnienie); CRC nie zastępuje porównania. W^X **nie** jest egzekwowane
  (tablice stron firmware bez zmian) — prawa ELF są tylko zadeklarowane.

### 3.2. Reguły Fazy R2 (elastyczne umieszczenie, `r2-elastyczne-ladowanie.md`)

- Metadane natywne walidowane przed jakimkolwiek przydziałem; brak lub błąd → `BRB-E7xx`, **brak powrotu do stałego
  adresu**. Brak relokacji ELF, brak ET_DYN, brak kopiowania po EBS.
- Zapis wyłącznie do stron zwróconych BRB przez `AllocatePages(AllocateMaxAddress < 4 GiB)`; każdy adres sprawdzony
  (wyrównanie, przepełnienie, MAXPHYADDR, rozłączność z obrazem BRB, buforem źródła i innymi zasobami) przed zapisem.
- Migawki mapy przed/po: każdy własny przydział porównany z ACPI NVS (`BRB-E615`); NVS, Reclaim, Runtime, Reserved,
  MMIO i Unusable nigdy nie są celem.
- Rejestr własności: zwalnianie w odwrotnej kolejności na sukcesie i porażce, brak podwójnego zwolnienia, błędy
  `FreePages` raportowane (`BRB-E616`), zasób porzucany.
- Tablice przejścia budowane i niezależnie weryfikowane (U=0, brak W+X, brak dużych stron, brak aliasów, strażniki),
  ale **nieaktywne**: W^X jądra nie jest w R2 egzekwowane i pozostaje blokadą produkcyjną.
- Wstrzykiwanie błędów tylko w wariantach `BRB_TEST_R2`; normalny EFI bez kodu testowego.

## 4. Odporność na awarie (fail-safe)

| Awaria | Zachowanie BRB |
|---|---|
| Uszkodzona konfiguracja | wbudowana konfiguracja domyślna (jeden wpis „Uruchom Bursztyn OS” z typowymi ścieżkami) |
| Uszkodzone jądro slotu oczekującego | oznaczenie próby jako nieudanej (zapis stanu jak przy nieudanym starcie), start slotu potwierdzonego |
| Uszkodzone jądro slotu potwierdzonego | próba drugiego slotu, jeśli jego SHA-256 zgadza się ze stanem; w przeciwnym razie menu z komunikatem |
| Obie kopie stanu LBA uszkodzone | `ROZRUCH_BLAD_USZKODZONE_OBIE_KOPIE`: start slotu A w trybie „bez zapisu stanu”, komunikat; jądro samo odtwarza stan z kopii BSP2 (`aktualizacja.cpp:644–655`) |
| Przerwany zapis stanu przez BRB | dwie kopie z generacją (zapis najpierw kopii starszej, potem drugiej — jak w jądrze) |
| Uszkodzony BRB (UEFI) | firmware nie uruchomi pliku albo BRB wykryje błąd w samosprawdzeniu; ratunek: nośnik Live |
| Uszkodzony etap 2 BIOS | CRC32 obrazu → komunikat |

Zapisy wykonywane przez BRB są minimalne i ograniczone do **jednego** sektora stanu (dwie kopie). BRB nigdy
nie zapisuje BSP2 ani plików ESP.

---

## 5. Integralność a autentyczność; podpisy

| Mechanizm | Chroni przed | Nie chroni przed |
|---|---|---|
| CRC32 (stan A/B, BRBI, etap 2 BIOS) | przypadkowe uszkodzenia, błędy zapisu | celową modyfikacją (CRC można przeliczyć) |
| SHA-256 jądra w stanie A/B (istniejące pole `sha256_jadro_{a,b}`) | podmiana pliku jądra **bez** modyfikacji stanu | atakującym, który zmieni też stan LBA (brak podpisu stanu) |
| Podpis cyfrowy jądra i modułów (propozycja) | podmianą przez osobę bez klucza prywatnego | kompromitacją samego BRB, jeśli BRB nie jest chroniony (Secure Boot) |

### 5.1. Propozycja podpisów artefaktów (Faza 13)

- **Algorytm**: wyłącznie uznane algorytmy, bez własnej kryptografii. Dwie opcje:
  1. **RSA-2048/3072 PKCS#1 v1.5 + SHA-256** — spójne z istniejącą weryfikacją pakietów aktualizacji
     (`aktualizacja.cpp:203–230`, mbedTLS 2.28.8 z `biblioteki/mbedtls`, licencja Apache-2.0 albo GPL-2.0-or-later do wyboru); kod weryfikacji
     już przetestowany w jądrze.
  2. **Ed25519** — mniejszy kod i klucz; wymaga dodania implementacji (np. z istniejącej biblioteki na
     licencji zgodnej). **[DO POTWIERDZENIA]** P-08.
- **Format**: oddzielny plik podpisu `<artefakt>.podpis` (nagłówek z wersją, identyfikatorem klucza, SHA-256
  artefaktu, podpis) albo manifest podpisanych skrótów dla całego zestawu (jądro + moduły + konfiguracja).
  Rekomendacja: **manifest** (`BRB.MANIFEST`) — jeden podpis, mniej odczytów, spójność zestawu.
- **Klucze**: publiczny klucz weryfikacji wbudowany w BRB w czasie budowania. Klucz prywatny poza
  repozytorium (tak jak obecny klucz aktualizacji — `aktualizacja_klucz.h` zawiera tylko część publiczną).
  Rotacja: BRB zawiera listę ≤ 4 kluczy z identyfikatorami; odwołanie klucza = wydanie BRB bez niego (wymaga
  aktualizacji BRB, czyli ochrony BRB przez Secure Boot, żeby miało sens).
- **Polityka**: v1 — weryfikacja SHA-256 ze stanu A/B (już dostępna). Później tryb „ostrzegaj” i tryb
  „wymagaj” (analogicznie do `bursztyn.podpis=wymagaj`). Domyślny tryb wymaga decyzji P-08.

### 5.2. UEFI Secure Boot

**[FAKT]** Obecny `BOOTX64.EFI` jest budowany lokalnie przez `grub-mkstandalone` z katalogu `x86_64-efi`, a nie
`x86_64-efi-signed` (`Makefile:1361`). Obraz nie jest podpisany kluczem z bazy `db` typowego komputera.
**[WNIOSEK]** Przy włączonym Secure Boot firmware odmówi jego uruchomienia (UEFI 2.10, rozdz. 32 *Secure Boot
and Driver Signing*). Dziś Bursztyn OS wymaga więc wyłączenia Secure Boot albo własnej konfiguracji kluczy.
BRB **nie zmienia tego automatycznie**: aplikacja UEFI nie jest „zgodna z Secure Boot” tylko dlatego, że jest
aplikacją UEFI.

Możliwe drogi (decyzja P-09):

| Droga | Opis | Wymagania | Ograniczenia |
|---|---|---|---|
| S1: własne klucze użytkownika | użytkownik rejestruje certyfikat Bursztyn OS w `db` (tryb Setup / narzędzia firmware) | podpis Authenticode (PE/COFF) obrazu BRB kluczem projektu | trudne dla użytkownika; zależne od firmware |
| S2: shim + MOK | podpisany przez Microsoft UEFI CA program pośredni, który weryfikuje BRB kluczem projektu (MOK) | proces przeglądu i podpisu shim, wymagania polityki podpisów (m.in. SBAT) | długi proces zewnętrzny; shim ma własną licencję i wymagania |
| S3: bezpośredni podpis Microsoft UEFI CA dla BRB | BRB jako podpisany loader | wymagania programu podpisów (przegląd, podatności, odwołania) | proces zewnętrzny, wymagania dotyczące tego, co loader wolno uruchamiać |

Niezależnie od drogi: Secure Boot ma sens tylko wtedy, gdy BRB **weryfikuje to, co uruchamia** (podpis jądra
i modułów, sekcja 5.1). W przeciwnym razie łańcuch zaufania kończy się na BRB. Odwołania: baza `dbx`, SBAT
(przy S2). Docelowym rozwiązaniem nie może być trwałe wyłączenie Secure Boot przez użytkownika.

### 5.3. TPM / measured boot

Nie wprowadzamy w pierwszych fazach (repozytorium nie ma obsługi TPM). Możliwe w przyszłości przez
`EFI_TCG2_PROTOCOL` (pomiary BRB, jądra, konfiguracji do PCR). Wymaga osobnej decyzji.

---

## 6. Ryzyka specyficzne

| ID | Ryzyko | Mitygacja |
|---|---|---|
| R-NMI | NMI/SMI między przełączeniem IDT/CR3 a wejściem jądra (brak poprawnej IDT) | okno minimalne, kod bez pętli zależnych od danych poza kopiowaniem; to samo ryzyko ma GRUB i każdy loader; tablica IDT BRB z procedurą „BLAD KRYTYCZNY” w Long Mode do chwili skoku |
| R-EBS | firmware zmienia mapę między `GetMemoryMap` a `ExitBootServices` (zdarzenia, timery); po 1. próbie usługi mogą być częściowo wyłączone | ograniczone ponowienia (4), po 1. próbie wyłącznie `GetMemoryMap`, bufor mapy przewymiarowany; porażka = COM1 + `ResetSystem`, bez powrotu do firmware (`architektura-uefi.md` §9.1) |
| R-KOP | kopiowanie po EBS nadpisuje kod/dane, których BRB jeszcze używa | trampolina, GDT, stos i tablice stron poza celami (rejestr zakresów); test hosta planu przeniesienia |
| R-FW | błędy firmware (np. GOP z błędnym `FrameBufferSize`) | `pitch × wysokość > FrameBufferSize` = tryb odrzucony; rozmiar nigdy nie jest powiększany ponad wartość firmware (`architektura-uefi.md` §7) |
| R-FW-TXT | firmware zgłasza `EFI_SUCCESS` w `TestString` i nie zwraca `EFI_WARN_UNKNOWN_GLYPH`, choć nie ma glifów (stwierdzone w OVMF 2024.02, Faza 2) | domyślna transliteracja ASCII na konsoli firmware; pełne polskie znaki tylko własnym rendererem GOP |
| R-STAN | podrobiony stan A/B wymuszający start starego (podatnego) jądra | po wprowadzeniu podpisów: wersja minimalna w podpisanym manifeście (ochrona przed downgrade jak w `akt_czy_dozwolona_aktualizacja`) |
| R-NVS | zewnętrzny program rozruchowy (GRUB 2.12 UEFI) umieszcza obraz jądra na ACPI NVS / pamięci firmware (zmierzone w 4A-R na OVMF z S3); jądro może to jedynie wykryć po wejściu, nie cofnąć | R1A: PMM klasyfikuje podkład obrazu wobec mapy i odmawia startu (`[PAMIEC] KRYTYCZNY BLAD WLASNOSCI PAMIECI ROZRUCHU`); wydania: ścieżka GRUB UEFI z kolizją uznana za niebezpieczną, test „zrzut NVS = 0 zmienionych bajtów”; docelowo BRB R2 (przydział przez firmware) — `r1a-rozdzielenie-adresow.md` §12 |
| R-WX | W^X jądra nieegzekwowane: okno obrazu RWX (strony 2 MiB) i alias RW ramek `.text` w HHDM (analiza R1B: tysiące liści W+X, setki aliasów W/X) | tablice przejścia R1B egzekwują W^X od wejścia; pełna egzekucja w VMM (PT 4 KiB okna, alias HHDM R+NX, audyt zapisów `.rodata`) jest bramką przed produkcyjną integracją BRB — `r1b-wejscie-64bit.md` §8 |
| R-WEJ64 | nieprawidłowy stan CPU/kontrakt na natywnym wejściu | `waliduj_stan` + `przyjmij_kontrakt_natywny` (pomiar podkładu, CR3, samozakres), ścieżka terminalna COM1 + `hlt`, bez powrotu do programu rozruchowego |
| R-CFG | złośliwa konfiguracja dodająca parametry jądra (np. `bursztyn.test.diskdestructive=1`) | lista dozwolonych parametrów dla systemu zainstalowanego; parametry testowe tylko z nośnika Live/CI; konfiguracja objęta manifestem podpisów (Faza 13) |
