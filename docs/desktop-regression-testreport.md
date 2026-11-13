# Raport testów regresyjnych — responsywność pulpitu, Eksplorator, okna

Gałąź `feature/bsf-2-2-font-formats`, HEAD `2c30b5d`, zmiany niezatwierdzone. Środowisko: Linux,
4 rdzenie, QEMU bez KVM (TCG), brak internetu. Wszystkie wyniki poniżej z tej sesji.

## Testy hosta (ASan/UBSan tam, gdzie target je włącza)

| target | wynik |
|---|---|
| `test-launch-policy-host` (nowy) | PASS — 50 sprawdzeń |
| `test-window-geometry-host` (nowy) | PASS — 771 385 sprawdzeń |
| `test-desktop-files-host` | PASS — 86 403 |
| `test-file-popup-host` | PASS — 308 494 |
| `test-desktop-settings-host` | PASS — 234 |
| `test-desktop-capacity-host` | PASS — 882 605 |
| `test-filesystem-move-host` | PASS — 438 |
| `test-desktop-ttf-host` | PASS — 188 398 |
| `test-bsf-ui-host` / `test-bsf-host` | PASS — 9 069 / 12 238 |
| `test-notepad-unicode-host` / `test-notepad-bidi-host` | PASS — 60 662 / 6 100 |
| `test-bursztynui-bidi-host` | PASS — 803 |
| `test-shell-host` / `test-shell-ttf-host` | PASS |
| `test-display-geometry-host` / `test-display-snapshot-host` | PASS — 634 / PASS |
| `test-cursor-host`, `test-compositor-cursor`, `test-gui-runtime-diag` | PASS |
| `test-mouse-pipeline-host` / `test-pointer-regression-host` / `test-pointer-graphics-host` | PASS — 2 657 / 6 / 91 |
| `test-gui-text-input-host` | PASS — 7 293 |
| `test-husarz-engine-host` | PASS — 127 432 |
| `test-husarz-runtime-host` | PASS — 2 312 |
| `test-husarz-fidelity-host` / `-layout-host` / `-selection-host` | PASS — 3 697 / 259 225 / 987 380 |
| `test-husarz-performance-host` | PASS — 4 638 (po aktualizacji stałej `NativeClockPollWork`) |
| `test-husarz-gif-host` / `-javascript-host` / `-css-host` / `-bidi-host` | PASS — 11 656 / 10 865 / 362 / 24 003 |
| `test-security-host` (loader, SSP, ASLR, fuzz smoke) | PASS — 45 114 wykonań fuzz, 0 awarii |
| `test-stack-protector-host`, `test-bur-v2-aslr-host`, `test-package-signature-host` | PASS |
| `test-kernel-blobz-host` | PASS — 3 422 |
| `test-network-lease-host` | PASS — 7 693 |
| `test-bsf22-host` (zestaw), `test-bsf-resources-host`, `test-bsf22-font-catalog-host`, `test-bsf22-collections-host` | PASS (m.in. 16 758 / 645 / 21 401) |
| `test-hda-diag` (zawiera izolację BWS) | PASS — 165 testów, 0 błędów |
| **`test-explorer-host`** | **FAIL — istniejący błąd na HEAD** (asercja `opaque`, patrz `explorer-rendering-stability.md`); nie osłabiono asercji, nie naprawiono |

Zmiany w istniejących testach (każda uzasadniona zmianą kontraktu, bez osłabiania asercji):
`test_husarz_performance.cpp` (granularność zegara natywnego 16 zamiast 128 jednostek),
`test_hda_bws_isolation.cpp` (BWS 100, `BWS_PIERWSZY_WOLNY == 101`), `fuzz_bws.cpp` (model
dispatchera zna zarezerwowany BWS 99), atrapy `gui_ustaw_kursor` / `gui_czas_monotoniczny_ns`
w fixture’ach hosta.

Nie uruchomiono w tej sesji: testów instalatora (QEMU), `test-bsf22-shaping-host` i innych
zestawów BSF spoza listy wyżej, ani zestawów QEMU Husarza (`testy/test_husarz_qemu.py`).

## Testy natywne (QEMU/TCG, ISO produkcyjne i diagnostyczne)

Narzędzia: `narzedzia/guest_session.py`, `narzedzia/desktop_responsiveness_qemu.py`,
`narzedzia/app_launch_bench.sh`, istniejący `narzedzia/bsf2_native_qemu.py`.

| scenariusz | wynik |
|---|---|
| Eksplorator: uruchomienie Aktualizacji (PZB 2) z wpisu Pulpit, ISO produkcyjne | okno „Aktualizacje Systemu” otwarte |
| Eksplorator: uruchomienie Testu dźwięku (PZB 3) i Ustawień (PZB 3), ISO produkcyjne | okna otwarte |
| Eksplorator: Diagnostyka (już działa) | aktywacja istniejącego okna (bez duplikatu) |
| Eksplorator: pusty plik `fake.bur` | „…to nie jest poprawny program Bursztyn OS.” |
| Eksplorator: kółko myszy + ruch wskaźnika, 20 s (diag) | rozdarte złożenia 39 → 0 (cień) |
| Notatnik: rozciągnięcie rogu i krawędzi, przesunięcie poza prawą krawędź | OK, układ dostosowany, brak błędów jądra |
| Eksplorator: rozciągnięcie rogu i krawędzi poza ekran | OK |
| Husarz: ładowanie stron lokalnych (WordPress, obrazy) + metryki gościa | patrz `husarz-loading-performance.md` |
| Start aplikacji: 6 aplikacji × zimny/ciepły (diag) | patrz `app-startup-performance.md` |

### Zestaw natywny `bsf2_native_qemu.py --workload bsf22` na ISO produkcyjnym (36 kontroli każdy)

| tryb | wynik | czas do pulpitu | GPU |
|---|---|---|---|
| UEFI 1280×720 | **36/36** | 9,5 s | 2 miękkie timeouty, odzyskane, 0 trwałych |
| BIOS (gość wybrał 1920×1080) | **36/36** | 22,9 s | 0 timeoutów |
| UEFI 1920×1080 | **36/36** | 12,4 s | 0 timeoutów |

Kontrole obejmują m.in. start pulpitu, Ustawienia → Czcionki, Notatnik, Powłokę, Husarza z
11 stronami z webfontami (w tym nagłówek `Origin` i odrzucenie zbyt dużego fontu). Zestaw nie
ćwiczy zmiany rozmiaru okien ani Eksploratora — to pokrywają osobne scenariusze wyżej.

Nie wykonano: testów natywnych 800×600, 1024×768 i 2560×1440; testu z tabletem VirtIO; testów
zainstalowanego systemu (instalator, dysk) — właściciel potwierdził ręczną instalację,
automatyczny harness instalatora jest w tym środowisku niestabilny (timeouty VirtIO-BLK, niezwiązane
z tą pracą); testów ASan na kodzie jądra (tylko testy hosta).

## Obraz jądra

`__kernel_end` = `0xAC9000` (10,79 MiB); przed sesją `0xABF000`. Limit 15 MiB (`0xF00000`): zapas
**4,21 MiB** (było ~4,3 MiB; +40 KiB na cień warstwy, politykę uruchamiania, kod diagnostyczny
wyłączony w produkcji nie wchodzi). Limit linkera nie został zmieniony; kompresja BZB1 aplikacji
zachowana.
