# BSF 2.2 — duże pliki fontów (TTC/OTC > 16 MiB)

## Stan przed zmianą

* Katalog fontów już obsługiwał pliki do `MaxFontFile` (64 MiB) przez odczyty
  zakresowe (BWS 59, 64 KiB/wywołanie) i leniwe ładowanie tabel kolekcji
  (`docs/bsf2-2-collections.md`).
* **Instalacja** (`BSF::install_font`, Ustawienia → Czcionki) czytała cały
  plik (`read()`, limit `MaxFile` = 16 MiB − 32 B) i zapisywała go jednym
  `zapisz_plik` — kolekcje > 16 MiB (np. Noto Sans CJK 19,5 MB) były
  odrzucane jako `InvalidFont`.
* BWS 3 (zapis pliku) w jądrze już strumieniował duże zapisy do PSF porcjami
  64 KiB, ale wymagał bufora całego pliku w Ring 3.

## Zmiany

1. **BWS 98 `BWS_ZAPISZ_FRAGMENT_PLIKU`** (`jadro/bws/bws_api.h`,
   `jadro/bws/syscalls.cpp` `bws_zapisz_fragment_pliku`): zapis
   `(ścieżka, offset, bufor, długość)` do istniejącego pliku.
   * 1 B … 1 MiB na wywołanie, `offset + długość ≤ PSF_MAKS_ROZMIAR_PLIKU`
     (bez przepełnienia), zakres użytkownika sprawdzany (`…_do_odczytu`,
     `bws_vmm_sprawdz_zakres_uzytkownika`), kopia przez bufor jądra (SMAP).
   * **`offset ≤ bieżący rozmiar`** — bez dziur: `zapisz_fragment_pliku`
     alokuje bloki za końcem pliku, więc dziura ujawniłaby niezainicjowane
     bloki.
   * Te same kontrole PZB co BWS 3 (`proces_moze_modyfikowac_obiekt`, log
     `[PZB] file_write=DENY`), powiadomienie `BWS_FILE_MODIFY`.
   * `BWS_PIERWSZY_WOLNY` 98 → 99; test `test_hda_bws_isolation.cpp`
     zaktualizowany (numery istniejących wywołań bez zmian).
2. **`install_large_font`** (`bsf/runtime.cpp`) dla plików > `MaxFile`
   (≤ `MaxFontFile`): każda twarz kolekcji jest walidowana z tabel metadanych
   czytanych zakresowo — ten sam kod co skan katalogu (wydzielony helper
   `with_large_face`); kontury są walidowane przy pierwszym renderze, jak dla
   każdej dużej kolekcji. Następnie kopia porcjami 1 MiB (BWS 59 → BWS 98)
   bez bufora całego pliku, kontrola rozmiaru wyniku; każdy błąd usuwa
   częściowy plik docelowy (BWS 7). Pliki ≤ 16 MiB idą dotychczasową ścieżką
   (pełna walidacja całego pliku).

Parsowanie fontów pozostaje w Ring 3; jądro tylko kopiuje bajty.

## Nie zmieniono

* Import modułów Multiboot2 przy starcie (`bsf/bootstrap.inc`) nadal ma limit
  `MaxFile` na plik — dotyczy wyłącznie fontów dostarczanych przez GRUB,
  a nie instalowanych przez użytkownika.

## Weryfikacja

`make test-bsf22-collections-host` (produkcyjny `bsf/runtime.cpp`, makieta
FS egzekwująca politykę jądra BWS 98: istniejący plik, ≤ 1 MiB, bez dziur):

* kolekcja 19 935 257 B (> `MaxFile`, z niezerowym „ogonem”) zainstalowana
  w 20 zapisach ≤ 1 MiB, kopia identyczna bajt w bajt, żadnego odczytu
  całego pliku źródłowego ani docelowego, 3 twarze w katalogu po `rescan`;
* drugi raz → `Exists`; uszkodzony nagłówek kolekcji → `InvalidFont` bez
  żadnego zapisu; błąd zapisu w 7. porcji → `IO` i brak pliku docelowego;
  plik > 64 MiB → `InvalidFont`;
* 20 790 checks PASS (ASan/UBSan).

Jądro z BWS 98 kompiluje się i startuje natywnie (bsf22 35/35); natywna
instalacja pliku > 16 MiB przez GUI Ustawień **nie była wykonana** w tym
cyklu (brak takiego pliku w obrazie testowym).
