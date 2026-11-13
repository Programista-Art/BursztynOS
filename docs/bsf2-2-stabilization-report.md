# BSF 2.2 — raport stabilizacji

Gałąź `feature/bsf-2-2-font-formats` (HEAD `0ef1ef7`), zmiany niezatwierdzone
(bez commitów, pushy, merge'y, nowych gałęzi). Szczegóły w dokumentach
tematycznych podanych przy każdym punkcie.

## Podsumowanie priorytetów

| # | zadanie | stan |
|---|---|---|
| 1 | rozmiar obrazu jądra | **zrobione** — `0xE9F000` → `0xABF000`, zapas 388 KiB → ~4,3 MiB (`bsf2-2-kernel-size-optimization.md`) |
| 2 | `test-desktop-ttf-host` | **naprawione** — 188 398 checks PASS; przyczyną były nieaktualne oczekiwania testu, nie kod produkcyjny |
| 3 | wydajność CFF/CFF2 | **zrobione** — 8,3× (CFF), 5,6–6,7× (CFF2) (`bsf2-2-cff-performance.md`) |
| 4 | duże pliki fontów / zapis porcjami | **zrobione (host)** — BWS 98 + instalacja > 16 MiB (`bsf2-2-large-font-files.md`) |
| 5 | Husarz: Origin, większe fonty | **Origin zrobione**; limity rozmiaru fontów sieciowych **bez zmian** (`bsf2-2-husarz-networking.md`) |
| 6 | font-variation-settings, suwaki osi, FeatureVariations, avar2 | **nie zrobione** (`bsf2-2-variable-ui.md`) |
| 7 | shaping Devanagari | **nie zrobione** (`bsf-indic-shaping.md`) |

## 2. test-desktop-ttf-host

* `FAIL desktop_layout.cell(i)==original.cell(i)` (ikona 13): test ustawiał
  celową pozycję w kolumnie 2 z przesunięciem +7,+5 px. Od czasu rozszerzenia
  katalogu aplikacji do 15 (`DesktopEntries::Count`) ikony plików trafiają do
  kolumny 3, więc stopka ikony 13 (szer. 118 px) nachodziła 5 px na ikonę 11.
  `reflow()` poprawnie przenosi późniejszą kolidującą ikonę, zachowując
  zapamiętaną kotwicę. Błąd występował już na `544a51e` i `58131c0`
  (sprawdzone w odłączonych worktree).
  Poprawka testu: wolna kolumna 5 + **nowe asercje**, że pozycja bazowa jest
  widoczna i nie nachodzi na żadną inną ikonę (test silniejszy, nie słabszy).
* Następna asercja: nawigacja Ustawień oczekiwała 5 pozycji; od `c4e7f0c`
  jest 6 (dodano „Energia”). Zaktualizowano na 6 nazw + asercję, że strona
  Energia jest domyślnie ukryta.

## Inne zmiany wykryte i naprawione po drodze

* Dekompresja blobu w instalatorze w jednym kroku dawała 3,7 s bez oddania
  CPU (TCG) — dekoder przerobiony na porcjowy (4 KiB/krok).
* Harness instalatora (`narzedzia/instalator/qemu_install.py`) klikał
  współrzędne 1280x720, gdy GOP dawał 1920x1080 — przesunięcie o offset
  centrowania odczytany z logu jądra.
* `narzedzia/bsf2_native_qemu.py`: nowa kontrola `webfont_origin_header` —
  serwer stron rejestruje nagłówek `Origin` każdego żądania.

## Weryfikacja końcowa

Na finalnym ISO (`__kernel_end` `0xABF000`):

| test | wynik |
|---|---|
| `make test-bsf22-host` | PASS (CFF 65 702, kolekcje 20 790, katalog 307, WOFF 62 085, Husarz webfonts 185, wariacje 43 107, kolor 1 093, emoji 192; shaping vs HarfBuzz 120+44+51+22 wierszy exact) |
| `make test-kernel-blobz-host` | 3 422 checks PASS |
| `make test-desktop-ttf-host` | 188 398 checks PASS |
| `make test-husarz-runtime-host` | 2 312 checks PASS |
| `make test-bsf-host`, `test-bsf-ui-host` | 12 238 / 9 069 PASS |
| HDA/BWS isolation (numer 98) | 162 testów, 0 błędów |
| `fuzz-bsf22` (100k iteracji każdy) | font 3 361 krawędzi, webfont 2 635, png 370 — brak awarii |
| natywnie UEFI 1280x720 (finalne ISO) | **36/36** (z nową kontrolą `webfont_origin_header`) |
| natywnie BIOS 1280x720 | **36/36**; Origin fontów `http://10.0.2.100`, stron brak |
| natywnie UEFI 1920x1080 | 35/35 (przed dodaniem kontroli Origin; 1 miękki timeout GPU odzyskany) |
| instalator E2E | **niezaliczony** — patrz ograniczenia |

## Ograniczenia i ryzyka

* **Instalator E2E nie przeszedł w tym środowisku** — także na czystym HEAD:
  losowe `[VIRTIO-BLK] ERROR: request timed out` (GPT/ESP/SYSTEMFILES/
  WALLPAPERS) przy zdrowym `fdatasync` hosta. Ścieżka planowanej instalacji
  skompresowanych plików jest pokryta testem hosta (porcje 4 KiB jak w
  kroku jądra) i raz przeszła natywnie cały etap SYSTEMFILES (wersja
  jednorazowa dekodera); wersja porcjowa nie przeszła natywnie SYSTEMFILES
  do końca z powodu tych timeoutów.
* Natywna instalacja fontu > 16 MiB przez GUI Ustawień — niewykonana.
* Większe fonty sieciowe (> 240 KiB wejścia) — niezmienione.
* Podczas pracy w drzewie roboczym zniknęły śledzone pliki artefaktów testów
  (`.kontrolki4-qemu/*.png`, `.qemu_ci/*`, `.suite_test/*`,
  `.test_run_uefi/*`, `.virtiotest/forensic-validation/*`). Żadne polecenie
  tej sesji ich nie usuwało; równolegle działała druga sesja Claude Code na
  tym repozytorium, a dysk był w 100% zapełniony. Nie przywracałem ich
  samodzielnie — decyzja należy do właściciela (`git restore <ścieżki>`).
