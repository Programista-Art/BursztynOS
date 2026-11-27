# BRB — integracja z instalatorem, aktualizacjami i odzyskiwaniem

Status: **[PROJEKT]** (Faza 12). Fakty: `audyt-rozruchu.md`, sekcje 3.3 i 9, oraz `docs/aktualizacja-systemu.md`.
W Fazie 1 **nie zmieniono** instalatora ani mechanizmu aktualizacji. Nie zapisano niczego na istniejących
obrazach dysków.

---

## 1. Stan obecny — artefakty instalacji związane z GRUB

| Artefakt | Gdzie | Tworzy | Używa |
|---|---|---|---|
| `\EFI\BOOT\BOOTX64.EFI` (GRUB standalone, ~kilka MiB) | ESP | instalator z modułu `installer.efi` | firmware UEFI |
| skrypt A/B wbudowany w `BOOTX64.EFI` | wewnątrz obrazu (memdisk) | `grub-mkstandalone` z `narzedzia/instalator/grub-embedded.cfg` | GRUB |
| `\BOOT\GRUB\GRUBENV` (1024 B) | ESP | instalator (`inicjalizuj_domyslny_grubenv`) | GRUB (`load_env`/`save_env`), jądro (`esp_zapisz_grubenv`, samonaprawa) |
| `\BOOT\GRUB\GRUB.CFG` | ESP | instalator (`instalator.cpp:41–46`) | **nikt** (D-07) |
| `\BOOT\JADRO_A.BIN`, `\BOOT\JADRO_B.BIN` | ESP, klastry ciągłe, stała pojemność (D-05) | instalator (kopie `installer.kernel`), aktualizacja (slot nieaktywny) | GRUB |
| `\BOOT\system_operacyjny.bin` (LFN, skrzyżowany z JADRO_A) | ESP | instalator | GRUB (ścieżka zgodności) (D-06) |
| LBA 2046/2047 `bursztyn_rozruch_stan` | dysk | instalator (etap ZNACZNIK), aktualizacja, jądro | jądro (autorytatywny stan) |
| `/aktualizacje/stan_rozruchu.k0/.k1` | BSP2 | instalator, aktualizacja, jądro | jądro (zapas) |
| PMBR bez kodu | LBA 0 | `gpt_install_*` | — |

**Weryfikacja instalacji** **[FAKT]**:

- każdy zapis ESP ma odczyt kontrolny (`write_verify`), a CRC32 jądra liczone jest porcjami
  (`fat_install_step`, faza 1);
- po stanie ZNACZNIK sprawdzane są sektory stanu i `grubenv` (`instalator.cpp:292–325`);
- znacznik `BSPINST1` jest pisany z CRC i podwójnie (`storage_format.cpp:298–308`, funkcje `marker` i `complete`);
- ścisła kontrola plików systemowych jest w `jadro/gui/instalacja_plikow.h`. Źródła muszą przetrwać cały
  etap: obraz jądra albo zarezerwowany moduł, nigdy bufor na stosie (naprawa z commita 08c03ba — zachować).

---

## 2. Zasady integracji BRB

1. **Nigdy nie usuwać jedynego działającego programu rozruchowego przed walidacją nowego.**
2. **Instalacja BRB jest transakcją**: zapis → odczyt kontrolny SHA-256 → aktywacja jedną zamianą pliku
   (rename) albo zapisem w miejscu jednego pliku o stałym rozmiarze → potwierdzenie po udanym starcie.
3. **Stan A/B ma jedno autorytatywne źródło** — sektory LBA 2046/2047. `grubenv` pozostaje lustrem
   tylko na czas, gdy na dysku jest jeszcze GRUB.
4. **BRB zapisuje wyłącznie stan rozruchu** (licznik prób), nigdy pliki ESP ani BSP2.
5. Zmiany instalatora są **osobnymi zmianami** z testami hosta (`test-instalacja-host`, `test-storage`)
   i testem QEMU wykonywanym tylko za zgodą właściciela.

---

## 3. Układ ESP z BRB (nowe instalacje)

```
\EFI\BOOT\BOOTX64.EFI          BRB (aktywny program rozruchowy – ścieżka domyślna nośnika)
\EFI\BURSZTYN\BRB-X64.EFI      kopia poprzedniej, potwierdzonej wersji BRB (zapas)
\EFI\BURSZTYN\BRB.CFG          konfiguracja (deklaratywna, specyfikacja-techniczna.md §8)
\EFI\BURSZTYN\BRB.MANIFEST     (Faza 13) podpisany manifest skrótów
\BOOT\JADRO_A.BIN              jądro slotu A – pojemność zarezerwowana (np. 32 MiB, decyzja P-05)
\BOOT\JADRO_B.BIN              jądro slotu B – j.w.
\BOOT\GRUB\GRUBENV             tylko w okresie przejściowym (gdy GRUB może być uruchomiony)
```

Usunięte z nowych instalacji: `\BOOT\GRUB\GRUB.CFG` (D-07) i alias `system_operacyjny.bin` (D-06).
**[DO POTWIERDZENIA]** czy usuwanie aliasu jest bezpieczne dla starszych narzędzi.

Pojemność slotów: rezerwa usuwa D-05. FAT32 ma 512 MiB, więc 2 × 32 MiB + BRB jest marginalne. Rezerwa
wymaga zmiany `fat_install_start` (długość klastrów niezależna od `files[i].size`) i `esp_odczytaj_uklad`
(rozmiar pliku ≠ pojemność). Pole `jadro_x_max_bytes` już istnieje.

---

## 4. Wybór slotu w BRB (zastępuje skrypt `grub-embedded.cfg`)

Algorytm odpowiada semantyce obecnego GRUB + jądra, ale opiera się na stanie autorytatywnym:

```
stan = rozruch_wybierz_poprawna_kopie(LBA 2046, LBA 2047)        // ten sam kod co jądro (nagłówek C)
jeśli brak poprawnego stanu:
    slot = A; powod = "brak stanu"; NIE zapisuj stanu; start (jądro odtworzy stan z kopii BSP2 lub zgłosi błąd)
inaczej jeśli stan.slot_oczekujacy ∈ {A,B} i stan_oczekujacego == OCZEKUJACY:
    jeśli licznik_prob < maks_prob (1):
        licznik_prob += 1; generacja += 1; zapisz obie kopie (starsza najpierw); sprawdź odczytem
        slot = oczekujący; powod = "próba oczekującego"
    inaczej:
        stan_oczekujacego = NIEUDANY; generacja += 1; zapisz obie kopie
        slot = potwierdzony; powod = "powrót po nieudanej próbie"
inaczej:
    slot = potwierdzony; powod = "potwierdzony"
plik = slot == A ? \BOOT\JADRO_A.BIN : \BOOT\JADRO_B.BIN
jeśli sha256_jadro_<slot> ≠ 0 i SHA-256(plik) ≠ sha256_jadro_<slot>:
    traktuj jak nieudaną próbę (gdy slot był oczekujący) albo spróbuj drugiego slotu (gdy jego skrót się zgadza)
parametry = "bursztyn.installed=1 bursztyn.slot=<A|B>" + parametry z konfiguracji (lista dozwolonych)
```

**[DO POTWIERDZENIA]** Semantyka `licznik_prob`/`maks_prob` w rekordzie LBA jest dziś zarządzana przez
`rozruch_*` w jądrze, a GRUB używa własnego `bursztyn_proby` w `grubenv`. Przed Fazą 12 trzeba ustalić, czy
jądro po starcie oczekuje `licznik_prob == 1` (zgodność z `rozruch_potwierdz_sukces`). Ustalenie wymaga
testu hosta `aktualizacja_rozruch.h` dla sekwencji BRB → jądro.

### 4.1. Okres przejściowy: dwa źródła stanu

Dopóki na dysku może być uruchomiony GRUB (np. zapasowy), BRB po każdym zapisie stanu LBA aktualizuje też
`grubenv` tą samą funkcją co jądro (`esp_zapisz_grubenv` — **wymaga zapisu ESP przez BRB**, co narusza
zasadę 4). **[PROJEKT]** Zamiast tego: BRB **nie** pisze `grubenv`. Rozbieżność `grubenv` z LBA samonaprawia
jądro, które już to robi (`aktualizacja.cpp:581–592`). GRUB jako zapas po awarii BRB może więc wybrać slot
na podstawie starszego `grubenv`, a jądro skoryguje stan po starcie (przy nieautoryzowanym slocie jądro
resetuje maszynę — Z-14). Akceptowalne, bo dotyczy tylko awaryjnego użycia GRUB.

### 4.2. Ręczny wybór „poprzedniego slotu” w menu

**[FAKT]** `aktualizacja_sprawdz_rozruch` resetuje maszynę, gdy uruchomiony slot nie jest ani potwierdzony,
ani oczekujący (`aktualizacja.cpp:615–628`). Pozycja menu „Poprzedni slot systemu” wymaga więc zmiany polityki
w jądrze, np. akceptowania `bursztyn.slot=X` z rekordem `brb_rekord_slot_systemu.powod = 4 (wybór ręczny)`,
tylko gdy `sha256_jadra` zgadza się ze stanem. Do tego czasu pozycja jest **niedostępna** (P-07).

---

## 5. Instalator a BRB

| Etap | Zmiana | Faza |
|---|---|---|
| Moduł instalatora | nowy moduł `installer.brb` (aplikacja BRB-UEFI) obok `installer.efi`; walidacja `MZ` + PE32+ + SHA-256 z manifestu ISO | 12 |
| Zapis ESP | `fat_install_start` dostaje listę plików (BRB jako `BOOTX64.EFI`, `\EFI\BURSZTYN\…`), rezerwy pojemności slotów, brak D-06/D-07 | 12 |
| Weryfikacja | jak dziś (`write_verify`) + SHA-256 BRB i jądra po zapisie + kontrola składni `BRB.CFG` tym samym parserem co BRB (host + jądro) | 12 |
| GRUB na nowych instalacjach | przez okres przejściowy: kopia GRUB w `\EFI\BURSZTYN\GRUBX64.EFI` jako ręczny zapas (P-03) | 12 |
| Wymóg UEFI | bez zmian (instalacja z BIOS dopiero po P-04) | — |

Instalator **nie może** dostać źródeł BRB z bufora tymczasowego. Moduł `installer.brb` jest rezerwowany jak
`installer.efi` (ta sama klasa źródła co w `instalacja_plikow.h`).

---

## 6. Migracja istniejących instalacji (GRUB → BRB)

Wymaga decyzji P-03. Proponowana procedura, wykonywana przez **zaktualizowane jądro** (mechanizm aktualizacji,
nie BRB):

1. Zapis `\EFI\BURSZTYN\BRB-X64.EFI` i `\EFI\BURSZTYN\BRB.CFG`, odczyt kontrolny SHA-256.
2. Zapis kopii GRUB: `\EFI\BURSZTYN\GRUBX64.EFI` = obecny `\EFI\BOOT\BOOTX64.EFI` (odczyt kontrolny).
3. Ustawienie w stanie A/B flagi „BRB oczekuje na potwierdzenie” (nowe pole w `padding` rekordu
   `bursztyn_rozruch_stan` → `wersja_formatu = 2`, z kompatybilnym odczytem v1). **[DO POTWIERDZENIA]**
4. Zamiana `\EFI\BOOT\BOOTX64.EFI` ← BRB. Krytyczny moment: zapis jednego pliku. Plik ma inny rozmiar niż
   GRUB, więc potrzebna jest alokacja klastrów albo przygotowanie pliku o zarezerwowanym rozmiarze.
5. Restart. BRB uruchamia potwierdzony slot. Jądro po udanym starcie (`aktualizacja_potwierdz_rozruch`)
   oznacza BRB jako potwierdzony.
6. Brak potwierdzenia (np. BRB nie przekazał sterowania): ponieważ firmware zawsze uruchamia
   `\EFI\BOOT\BOOTX64.EFI`, nie ma automatycznego powrotu do GRUB bez NVRAM. Mitygacje:
   - (a) wpis NVRAM `Boot####` dla GRUB jako drugi w `BootOrder` — wymaga `SetVariable`, którego jądro
     nie używa (Z-12) → nowa funkcja;
   - (b) nośnik Live jako ratunek (procedura „Odzyskiwanie” w dokumentacji użytkownika);
   - (c) BRB z obsługą „łańcuchowego” uruchomienia GRUB (`LoadImage`/`StartImage` `\EFI\BURSZTYN\GRUBX64.EFI`)
     przy wykryciu, że ostatni start BRB nie został potwierdzony.

   Rekomendacja: (c) + (b). Wymaga `EFI_LOADED_IMAGE` i `LoadImage` tylko dla tej ścieżki.

   **Ograniczenie (korekta Fazy 2):** mitygacja (c) działa **tylko wtedy, gdy nowy BRB w ogóle się
   uruchamia** i dochodzi do własnej logiki wyboru. Jeżeli plik `\EFI\BOOT\BOOTX64.EFI` jest uszkodzony,
   obcięty, niepoprawnie podpisany (Secure Boot) albo pada przed tą logiką, firmware nie wykona żadnego kodu
   BRB, a obecność `\EFI\BURSZTYN\BRB-X64.EFI` albo `GRUBX64.EFI` **niczego nie gwarantuje** — firmware o nich nie
   wie. Niezawodny automatyczny powrót wymaga mechanizmu niezależnego od aktualizowanego pliku:
   - (d) wpisy NVRAM `Boot####` z `BootOrder`/`BootNext` (zachowanie zależne od firmware; część firmware
     nie przechodzi do następnego wpisu po błędzie ładowania, a niektóre kasują lub ignorują własne wpisy),
   - (e) mały, **niezmienny** pierwszy etap BRB (selektor) w `\EFI\BOOT\BOOTX64.EFI`, który nigdy nie jest
     aktualizowany w zwykłym trybie i tylko wybiera/weryfikuje jeden z dwóch pełnych BRB (A/B) przez
     `LoadImage`/`StartImage` — zawodzi tylko przy uszkodzeniu samego selektora,
   - (f) zewnętrzny ratunek: nośnik Live z BRB i procedura odzyskiwania.
   Wybór architektury (d/e/f) należy do decyzji P-03 przed Fazą 12; Faza 2 niczego tu nie implementuje.

---

## 7. Aktualizacje BRB po migracji

- BRB jest artefaktem pakietu aktualizacji (nowa kategoria `brb`, obok `jadro` i `boot` w
  `aktualizacja.cpp:838–840`).
- Procedura: zapis nowego BRB jako `\EFI\BURSZTYN\BRB-NOWY.EFI` → odczyt kontrolny SHA-256 → zachowanie
  bieżącego jako `BRB-X64.EFI` (potwierdzony) → zamiana `BOOTX64.EFI` → restart → potwierdzenie przez jądro.
  Brak potwierdzenia → BRB łańcuchowo uruchamia `BRB-X64.EFI` (ta sama logika co 6(c)) — **tylko jeśli nowy
  BRB się uruchomił**; nowy BRB, który nie startuje wcale, nie może uruchomić zapasu (ograniczenie z §6:
  potrzebny selektor (e), NVRAM (d) albo nośnik ratunkowy (f)).
- Aktualizacja jądra (slot A/B) pozostaje bez zmian, poza wyeliminowaniem D-05.

---

## 8. Odzyskiwanie

| Scenariusz | Działanie |
|---|---|
| Uszkodzona konfiguracja BRB | konfiguracja wbudowana, komunikat |
| Uszkodzone oba jądra (SHA-256) | menu z komunikatem; ratunek — nośnik Live (instalator); przyszłe „Odzyskiwanie systemu” wymaga środowiska, którego dziś nie ma (P-07) |
| Uszkodzone LBA 2046/2047 | start A bez zapisu; jądro odtwarza z kopii BSP2 |
| Uszkodzony BRB | łańcuchowy zapas (6c, 7) tylko gdy BRB się uruchamia; gdy nie startuje wcale — selektor (6e), NVRAM (6d) albo nośnik Live (6f) |
| Uszkodzony ESP | nośnik Live; BRB nie naprawia FAT |
