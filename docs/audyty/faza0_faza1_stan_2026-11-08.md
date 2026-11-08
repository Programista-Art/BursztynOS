# Bursztyn OS — stan prac: Faza 0 i Faza 1 ukończone

Data: 2026-11-08
Repozytorium: `C:\Users\UNITRONIX\Documents\GitHub\BursztynOS`
Gałąź: `UNITRONIX`, baza `3409e60`

Ten dokument jest punktem wznowienia. Zawiera: co zostało zmienione, dlaczego,
jak to zweryfikowano, co pozostaje otwarte i czego NIE ruszać bez namysłu.

---

## 1. Punkt wyjścia — co było atrapą

Audyt wykazał, że system deklarował funkcje, których nie miał. Konkretne dowody:

| Obszar | Stan przed | Dowód |
|---|---|---|
| Tabela procesów | 1000 wierszy generowanych z `i`, RAM `64+(i*37)%512`, nazwa `PROCESY[i%12]` | `programy/kontrolki2/kontrolki2.cpp:129` |
| Uprawnienia aplikacji | 11 bloków `rowna_sciezka(...)` na sztywno w jądrze | `jadro/bws/syscalls.cpp:1847` |
| NX dla Ring 3 | Maska PTE nie zawierała bitu 63 → cała pamięć aplikacji wykonywalna | `jadro/mm/vmm.cpp:74` |
| SMEP/SMAP | Nie ustawiane; CR4 modyfikowane tylko w `fpu.cpp` | `jadro/arch/fpu.cpp:18` |
| Zerowanie ramek | `ZwolnijRamke` nie czyścił pamięci → wyciek między procesami | `jadro/mm/pmm.cpp:1756` |
| ASLR | Brak w produkcyjnym loaderze | `jadro/fs/loader.cpp:1051` |
| Podpis paczek | Tylko OTA; `.bur` bez weryfikacji | `jadro/fs/loader.cpp:1480` |
| Stos Ring 3 | 16 KiB | `jadro/fs/loader.cpp:158` |
| Ustawienia per app | Funkcja budowała ścieżkę, ale nic nie zapisywała | `jadro/gui/bursztyn_gui.cpp:1566` |
| Bootstrap PSF | 73 katalogi w tablicy `constexpr` | `jadro/fs/psf_bootstrap.h:36` |

---

## 2. Faza 0 — bezpieczeństwo twarde (UKOŃCZONA)

### 0.1 NX dla stron Ring 3 — realne W^X

**Problem:** `MASKA_FLAG_LISCIA_4K` nie zawierała `PTE_NX` (bit 63), a `ZmapujStrone`
przyjmowała `uint32_t`. Bit NX był obcinany, więc `.data`, sterta i stos każdej
aplikacji pozostawały wykonywalne mimo włączonego `EFER.NXE`.

**Zmiany:**
- `jadro/mm/vmm.cpp` — `MASKA_FLAG_LISCIA_4K` uzupełniona o `PTE_NX` + `static_assert`;
  `mapuj_strone_wewnetrznie` przyjmuje `uint64_t flagi`; nowa `ZmapujStrone64Sprawdzona`.
- `jadro/mm/pamiec.h` — deklaracja `ZmapujStrone64Sprawdzona`, komentarz ostrzegawczy
  przy 32-bitowym `ZmapujStrone`.
- `jadro/fs/loader.cpp` — `mapuj_segment_z_pliku` rozróżnia kod (RX) od danych (RW+NX);
  `mapuj_stos_uzytkownika` zawsze NX.
- `jadro/bws/syscalls.cpp` — sterta Ring 3 mapowana jako RW+NX.

**Weryfikacja:** `testy/test_dzwieku_loader.cpp` ma asercję
`CHECK(bool(page.flags & VMM_FLAGA_NX) == writable)` — kod nigdy nie ma NX,
dane zawsze. Test przechodzi: 30 scenariuszy, ~4,75 mln sprawdzeń.

### 0.2 SMEP/SMAP + okna STAC/CLAC

**Zmiany:**
- `jadro/mm/vmm.cpp` — `wlacz_smep_smap_jezeli_obslugiwane()`: detekcja CPUID
  (leaf 7, bity 7 i 20), wpis CR4 bitów 20/21, potwierdzenie odczytem.
  Brak wsparcia = system działa dalej z słabszą izolacją (nie jest to błąd).
- `jadro/mm/pamiec.h` — `smap_stac()`/`smap_clt()` (instrukcje `stac`/`clac`),
  klasa RAII `OknoUzytkownika` z licznikiem zagnieżdżeń.
- `jadro/bws/bezpieczenstwo.cpp` — okna w `skopiuj_z_przestrzeni_uzytkownika`,
  `skopiuj_do_przestrzeni_uzytkownika`, `skopiuj_string_z_uzytkownika`.
- `sterowniki/grafika/grafika.cpp` — okno w `gui_blit_region`.
- `jadro/gui/skladacz_obrazu.cpp` — okno w `skladacz_obrazu_ustaw_maske_tytulu`.
- `jadro/mm/stack_protector.cpp` — okno wokół zapisu kanarka do `.data` procesu.

**SMAP wykrył dwie realne podatności podczas wdrażania** (to najważniejszy efekt
tej fazy, nie sam mechanizm):

1. **`stack_protector_zainicjuj_segment_danych_procesu`** pisała do `.data`
   nowego procesu po przełączeniu CR3, bez okna AC → `#PF` w Ring 0
   przy `CR2=0x104D000`.
2. **`bws_czytaj_fragment_pliku` (BWS 59)** przekazywał bufor Ring 3
   bezpośrednio do PSF. PSF pisał do pamięci użytkownika z pominięciem
   `copy_to_user` → `#PF` przy `CR2=0x80000101F`.
   Poprawka: odczyt do `BuforKernelowy`, potem jedna kontrolowana kopia.
   Dodatkowy zysk: bufor użytkownika nie jest już częściowo zapisany przy błędzie I/O.

**Weryfikacja:** `test_bws_abuse_qemu.py` — PASS (6 kategorii nadużyć).

### 0.3 Zerowanie ramek przy zwolnieniu

`jadro/mm/pmm.cpp` — `ZwolnijRamke` i `ZwolnijCiagleRamki` czyszczą stronę
PRZED oznaczeniem jako wolna.

**Krytyczny szczegół implementacji:** nie wolno użyć `FizycznyZakresDoWirtualnego()`,
bo bierze `BlokadaPMM`, która jest już trzymana — spinlock bez reentrancji =
zakleszczenie. Adres HHDM liczony wprost (`BAZA_MAPOWANIA_FIZYCZNEGO + adres`).
W buildach hostowych (`BURSZTYN_PMM_HOST_TEST`) zerowanie jest wyłączone, bo host
nie ma HHDM.

**Wpływ na wydajność:** boot 14,4 s → 14,4 s (porównane z baseline `3409e60`).

### 0.4 ASLR stosu Ring 3

**Uwaga:** plan mówił „KASLR jądra + ASLR BUR v2". **KASLR jądra nie zostało
zrobione** — wymaga relocation-aware linkera i jest zmianą o skali porównywalnej
z całym VMM. Zamiast tego zrobiono ASLR stosu, które daje realny efekt przy
niskim ryzyku.

**Zmiany:**
- `jadro/mm/stack_protector.cpp/h` — `stack_protector_losowy_offset_stron()`,
  źródło entropii: RDRAND/RDSEED → RDTSC → SplitMix64 (ta sama droga co kanarek).
- `jadro/fs/loader.cpp` — okno 16 MiB (12 bitów entropii), losowanie per PID;
  `GORNA_BAZA_STOSU_USER = RING3_STACK_TOP - ROZMIAR_STOSU_USER`;
  `MIN_BAZA_STOSU_USER`; walidacja nagłówka sprawdza CAŁE okno (ostrzejsza niż przed).
- Tablica `g_baza_stosu_user[PZB_MAKS_PROCESOW]` zerowana przy zwolnieniu PID
  (PID-y są recyklingowane).

**Weryfikacja:** test loadera sprawdza, że `stary_rsp` leży w oknie i jest
wyrównany, ale NIE jest stałą kompilacyjną.

### 0.5 Podpis paczek .bur

**Nowe pliki:**
- `jadro/fs/podpis_paczki.h` / `.cpp` — RSA-2048 PKCS#1 v1.5 / SHA-256, ten sam
  klucz co OTA (`AKTUALIZACJA_KLUCZ_N`). Podpis w `<program>.podpis`, 256 B.
  Fail-closed: brak pliku, zły rozmiar, niezgodność → odmowa uruchomienia.
- `Makefile` — `podpis_paczki.o` + reguła.

**Polityka** (`jadro/fs/loader.cpp`):
- paczki z `/programy/` i `/gry/` oraz `/menedzer_okien.bur`, `/shell.bur`
  są zaufane przez pochodzenie obrazu ISO (nie wymagają podpisu),
- wszystko inne wymaga poprawnego podpisu,
- opcja startowa `bursztyn.podpis=wymagaj` wymusza podpis od każdej aplikacji
  (`loader_ustaw_podpis_wymagany`).

### 0.6 Stos Ring 3 = 256 KiB

`jadro/fs/loader.cpp` — `LICZBA_STRON_STOSU_USER` 4 → 64.
Wymagało to korekty górnej granicy bazy stosu, żeby szczyt (baza + 256 KiB)
nie przekroczył `RING3_STACK_TOP`.

---

## 3. Faza 1 — koniec z atrapami (UKOŃCZONA)

### 1.7 Prawdziwe API procesów

**Nowe:** `jadro/bws/bws_procesy.h` z `BwsProcesInfo`.
**ABI:** `BWS_PROCESY_LISTA = 94`, `BWS_PROCES_ZABIJ = 95`,
`BWS_PIERWSZY_WOLNY = 96`.
**Implementacja:** `jadro/bws/bws_nowoczesne.cpp`, sekcja 22.

Bezpieczeństwo:
- oba wymagają `PRAWO_SYSTEM_CONFIG` i `PZB <= PZB_USLUGI`,
- `BWS_PROCES_ZABIJ` nie pozwala ubić samego siebie (od tego jest BWS 32),
- proces o niższym PZB nie może zabić wyższego,
- stan ustawiany atomowo (`__ATOMIC_RELEASE`), zwalnianie zasobów zostawione
  schedulerowi (`posprzataj_zakonczone_procesy`) — nie zwalniamy PML4 w syscallu.

**Kontrolki 2** (`programy/kontrolki2/kontrolki2.cpp`) — usunięto 1000 fikcyjnych
wierszy; tabela pokazuje realne PID-y, stan, PZB i pamięć z `tablica_procesow[]`.

### 1.8 Manifest jako źródło prawdy

`jadro/bws/syscalls.cpp` (BWS 10) — usunięto 11-pozycyjny whitelist ścieżek.
Teraz manifest `opis.aplikacji` decyduje o uprawnieniach. Gdy manifestu brak,
obowiązuje zasada najmniejszego przywileju: `PRAWO_GUI | PRAWO_PLIKI_CZYTAJ`.
Wyjątek: `/shell.bur` (usługa systemowa instalowana z obrazu, bez manifestu).

**Dlaczego to ważne:** dodanie aplikacji nie wymaga już rekompilacji jądra.

### 1.9 Trwałe ustawienia per aplikacja

`jadro/gui/bursztyn_gui.h/.cpp`:
- `ustawienia_zapisz(app_id, klucz, wartosc)`
- `ustawienia_wczytaj(app_id, klucz, bufor, pojemnosc)`
- `ustawienia_usun(app_id, klucz)`

Ścieżka: `/uzytkownicy/domyslny/Ustawienia/<app>/<klucz>`, limit 4096 B.
`ustawienia_zapisz` tworzy katalog aplikacji i plik (BWS 2), potem zapisuje (BWS 3).

**Integracja:** `programy/ustawienia/ustawienia.cpp` zapisuje rozmiar czcionki
przy „Zastosuj" i wczytuje go przy starcie — preferencja przeżywa restart.

### 1.10 Strona „Energia" w Ustawieniach

**Nowy plik:** `programy/ustawienia/power_ui.h`.

Strona pokazuje wyłącznie dane z realnych źródeł:
- rozdzielczość i skala z BWS 62 (`gui_pobierz_monitor`),
- progi wygaszania i usypiania z trwałych kluczy `ustawienia/wygaszanie`,
  `ustawienia/usypianie`,
- status dźwięku z klucza `test_dzwieku/wynik`.

Nawigacja rozszerzona z 5 do 6 stron: **System, Energia, Wygląd, Motywy, Czcionki, Wsparcie**.
Indeksy stron w `show()` zostały przesunięte — to najłatwiejsze miejsce na regresję.

**Uwaga:** progi wygaszania/usypiania są ZAPISYWANE, ale jądro ich jeszcze nie
egzekwuje (brak DPMS i S3). To jawnie odnotowany dług, nie atrapa — UI nie kłamie,
bo nie twierdzi, że ekran już się wygasza.

### 1.11 Rozszerzalny bootstrap PSF

`jadro/fs/psf_bootstrap.h` — nowe funkcje:
- `psf_sciezka_startowa_poprawna()` — walidacja (bezwzględna, bez `..`, bez `//`, max 200 B),
- `psf_utworz_katalog_z_linii()` — przetwarza jedną linię konfiguracji.

`jadro/rdzen/kernel.cpp` — po `utworz_drabine_katalogow()` czytany jest
`/system/konfig/katalogi.cfg` (jedna ścieżka na linię, `#` komentarz, max 4096 B,
max 128 wpisów). Wpisy niepoprawne są pomijane bez blokowania bootu.

Baza pozostaje w kodzie — bootstrap działa PRZED zamontowaniem PSF, więc nie ma
skąd wczytać konfiguracji. Plik dodaje rozszerzalność, nie zastępuje fundamentu.

---

## 4. Wyniki weryfikacji

| Test | Wynik |
|---|---|
| `make system_operacyjny.bin` | PASS |
| `make iso` | PASS (39,7 MB) |
| `test_normal_boot_qemu.py` | PASS — gotowość TAK, 13,97–14,55 s, tapeta ~11,9 s, 0 BSOD |
| `test-stack-protector-host` | PASS (2007 asercji) |
| `run_loader_regression.py` | PASS — 30 scenariuszy, ~4,75 mln sprawdzeń, W^X/NX trace=0 i 1 |
| `test_bws_abuse_qemu.py` | PASS — 6 kategorii nadużyć Ring 3 |
| `test-guard-page` | PASS — #PF na guard page poprawnie przechwycony |
| `test-security` (SSP-QEMU) | PASS — 2/2 testy containment |
| `test-virtio-pci` | PASS — 169/169 |
| `fuzz-smoke` (7 celów) | PASS — ~35 tys. iteracji, 0 crashy |

### Potwierdzona wcześniejsza usterka (NIE nasza regresja)

Test kursora (`Kursor widoczny w (500,300)` i `Ruch myszy do (560,340)`) nie
przechodzi. Zweryfikowano przez `git worktree` na czystym `3409e60`:
**baseline ma identyczny FAIL**. Przyczyna jest opisana w
`docs/audyty/ui_stability_modernization_inventory_2026-11-07.md` — kursor jest
rysowany sprzętowo przez VirtIO-GPU, a test sprawdza warstwę software.
To zadanie na osobną sesję, nie część Fazy 0/1.

### Naprawione wcześniejsze regresje testów (blokowały gate)

- `testy/test_dzwieku_packaging.py` — `root` → `ROOT` (NameError).
- `testy/test_dzwieku_repair.py` — brakujące `-Isterowniki/{magazyn,grafika,siec,wejscie}`.
- `testy/test_dzwieku_loader.cpp` — stub `fpu_reset_process`, stub podpisu paczki,
  stub ASLR, `Page::flags` 32→64 bit, `mapuj_strone_wewnetrznie` uint64,
  stub `mapuj_strone_wewnetrznie` dopuszcza nieaktywny PML4 (zgodnie z API loadera).
- `jadro/aktualizacja/aktualizacja_klucz.h` jest w `.gitignore`, ale wymagany do
  buildu — **świeży klon się nie zbuduje**. To otwarta wada, patrz sekcja 6.

---

## 5. Pliki zmienione

### Nowe
```
jadro/fs/podpis_paczki.h
jadro/fs/podpis_paczki.cpp
jadro/bws/bws_procesy.h
programy/ustawienia/power_ui.h
narzedzia/run_loader_regression.py
narzedzia/rip_symbol.py
```

### Zmodyfikowane (jądro i sterowniki)
```
jadro/mm/vmm.cpp              NX, SMEP/SMAP, wariant 64-bit mapowania, akcesory
jadro/mm/pamiec.h             SMAP API, OknoUzytkownika, deklaracje
jadro/mm/pmm.cpp              zerowanie ramek (bez zakleszczenia)
jadro/mm/stack_protector.cpp/h  losowy offset ASLR
jadro/fs/loader.cpp           W^X, ASLR stosu, podpis, 256 KiB stos
jadro/fs/psf_bootstrap.h      rozszerzalne katalogi
jadro/bws/syscalls.cpp        sterta NX, BWS59 przez bufor jądra, koniec whitelisty
jadro/bws/bezpieczenstwo.cpp  okna SMAP w copy_*_user
jadro/bws/bws_api.h           BWS 94/95
jadro/bws/bws_nowoczesne.cpp  procesy_lista, proces_zabij
jadro/gui/bursztyn_gui.cpp/h  ustawienia_zapisz/wczytaj/usun
jadro/gui/skladacz_obrazu.cpp okno SMAP w masce tytułu
jadro/rdzen/kernel.cpp        opcja bursztyn.podpis, katalogi.cfg
sterowniki/grafika/grafika.cpp  okno SMAP w blit
Makefile                      podpis_paczki.o
```

### Zmodyfikowane (aplikacje Ring 3)
```
programy/kontrolki2/kontrolki2.cpp   realne dane procesów
programy/ustawienia/layout.h        6 stron nawigacji
programy/ustawienia/ustawienia.cpp  zapis/odczyt preferencji, nowe indeksy
```

### Zmodyfikowane (testy)
```
testy/test_dzwieku_loader.cpp
testy/test_dzwieku_repair.py
testy/test_dzwieku_packaging.py
```

---

## 6. Dług techniczny i otwarte zadania

### Wysoki priorytet
1. **`jadro/aktualizacja/aktualizacja_klucz.h` jest w `.gitignore`, ale wymagany
   do buildu.** Świeży klon nie zbuduje się. Trzeba albo dodać plik do repo,
   albo wygenerować go w Makefile z wartością domyślną (klucz testowy) i jasnym
   komunikatem. **To blokuje każde nowe środowisko.**
2. **Regresja kursora QEMU** — potwierdzona wcześniejsza, opisana wyżej.
3. **KASLR jądra** — świadomie pominięte; wymaga relocation-aware linkera.
4. **Progi energii nie są egzekwowane.** UI je zapisuje, ale brak DPMS i S3.
   Albo dodać wygaszanie w kompozytorze, albo wyraźnie oznaczyć w UI jako
   „zapisane, wejdzie w życie po dodaniu zarządzania energią".

### Średni priorytet
5. Podpisy `.bur` nie są jeszcze generowane przez tooling — trzeba dodać
   skrypt podpisujący paczki przy budowaniu ISO (`narzedzia/`).
6. `docs/` kłamie o kodzie: `docs/06` mówi „26 wywołań" (jest 96),
   `docs/05` mówi o blokach 512 B (jest 4096), `docs/03` podaje stos 16 KiB
   (jest 64 KiB startowy), `docs/10` opisuje strippowanie nagłówków HTTP.
7. Ramki stosu > 6144 B: `virtio_blk_stress_test` (8848 B),
   `psf_przenies_twor_wynik` (8656 B), `shell::_start` (6896 B).

### Niski priorytet
8. `PRAWO_DEBUG` zdefiniowane, ale nieużywane nigdzie w kodzie.
9. Sterta Ring 3 nie zwraca stron do jądra (bump allocator) — `munmap` nie istnieje.

---

## 7. Zasady, których trzeba przestrzegać przy dalszej pracy

1. **Nie używaj `FizycznyZakresDoWirtualnego()` pod `BlokadaPMM`.** Zakleszczenie.
2. **Każdy dostęp jądra do pamięci Ring 3 musi być w `OknoUzytkownika`.**
   Bez tego SMAP wywoła `#PF` w Ring 0. Dotyczy też kodu, który tymczasowo
   przełącza CR3 (jak loader).
3. **`ZmapujStrone()` (32-bit) obcina NX.** Dla stron Ring 3 używaj
   `ZmapujStrone64Sprawdzona()` lub `VmmMapujNowaStroneWPrzestrzeniProcesu(..., wykonuj)`.
4. **Zmiana rozmiaru stosu wymaga korekty `GORNA_BAZA_STOSU_USER`**, inaczej
   szczyt przekroczy `RING3_STACK_TOP`.
5. **PID-y są recyklingowane** — każda tablica per-PID musi być zerowana
   w `zwolnij_rezerwacje_procesu`.
6. **Testy hostowe nie mają HHDM ani CPU** — wszystko sprzętowe potrzebuje
   stubu pod `#ifndef BURSZTYN_*_HOST_TEST`.
7. **Gate przed commitem:** `make test-smoke`. Przy zmianach VMM/BWS:
   `test_bws_abuse_qemu`, `test_guard_page`, `run_loader_regression.py`.

---

## 8. Jak zweryfikować stan od zera

```bash
# WSL (build), QEMU tylko natywnie na Windows
cd /mnt/c/Users/UNITRONIX/Documents/GitHub/BursztynOS

make -j4 clean          # opcjonalnie, jeśli podejrzewasz stale .d
make -j4 iso

# Szybkie gate'y
make test-stack-protector-host
python3 narzedzia/run_loader_regression.py

# Boot w QEMU (wymaga qemu-system-x86_64 w PATH w WSL dla tego skryptu)
python3 testy/test_normal_boot_qemu.py

# Bezpieczeństwo
python3 testy/test_bws_abuse_qemu.py
python3 testy/test_guard_page.py
```

Oczekiwane: gotowość TAK, ~14 s do pulpitu, 0 BSOD, wszystkie testy PASS
(z wyjątkiem znanego FAIL kursora).
