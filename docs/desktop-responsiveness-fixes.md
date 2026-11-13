# Responsywność pulpitu, Eksplorator, okna — podsumowanie zmian

Gałąź `feature/bsf-2-2-font-formats`, HEAD `2c30b5d`, zmiany niezatwierdzone. Szczegóły i
dowody w dokumentach tematycznych:

| problem | dokument | stan |
|---|---|---|
| A. Husarz spowalnia cały system | `husarz-loading-performance.md` | **częściowo** — udowodniona i poprawiona jedna przyczyna (niepreemptowalna kryptografia TLS w jądrze) + granularność budżetu; głównego źródła tła (aktywne oczekiwanie na GPU) nie zmieniono; brak odtworzenia dotkliwości z raportu |
| B. Eksplorator: „odmowa” przy uruchamianiu | `explorer-launch-permissions.md` | **naprawione**; natywnie zweryfikowane starty z Eksploratora na ISO produkcyjnym: Aktualizacje (PZB 2), Test dźwięku i Ustawienia (PZB 3); Diagnostyka — tylko aktywacja działającego okna |
| C. Migotanie Eksploratora | `explorer-rendering-stability.md` | **naprawione w jądrze** (cień warstwy), zmierzone: 39 → 0 rozdartych złożeń / 20 s |
| D. Zmiana rozmiaru i ruch poza ekran | `window-manager-resizing.md` | **zaimplementowane** dla aplikacji BursztynUI, Eksploratora, Terminala i Przeglądarki Obrazów; natywnie zweryfikowano Notatnik i Eksplorator (1280×720) |
| E. Opóźnienie startu aplikacji | `app-startup-performance.md` | **dominujący etap znaleziony i zredukowany** (katalog czcionek per proces); dodano kursor „zajęty” |
| testy | `desktop-regression-testreport.md` | — |

## Zmiany w kodzie (pliki)

**Jądro / sterowniki:** `jadro/bws/syscalls.cpp` (delegowanie startu aplikacji systemowych, BWS 100,
rezerwacja BWS 99), `jadro/bws/uruchamianie_polityka.h` (nowy), `jadro/bws/bws_api.h`,
`jadro/bws/scheduler.{cpp,h}` (metryka odroczonych tików), `jadro/arch/idt.cpp`,
`jadro/fs/loader.{cpp,h}` (kody błędów, kontrola zgodności z wzorcem, znacznik pierwszej klatki),
`jadro/rdzen/kernel.cpp` (`kernel_wbudowany_zgodny`, wzorzec Ustawień),
`jadro/gui/skladacz_obrazu.cpp` (cień, zachowanie treści przy zmianie rozmiaru, okna do 2× ekranu),
`jadro/gui/gui_runtime_diag.h`, `jadro/gui/okno_geometria.h` (nowy),
`sterowniki/grafika/grafika.{cpp,h}` (cień warstwy), `jadro/siec/siec.cpp`, `jadro/siec/tls.cpp`,
`biblioteki/mbedtls/library/bignum.c`, `biblioteki/mbedtls/bursztyn_mbedtls_config.h`.

**Biblioteka UI / aplikacje:** `bursztyn_ui/okno.h`, `jadro/gui/bursztyn_gui.{cpp,h}`,
`programy/eksplorator/eksplorator-plikow.cpp`, `programy/shell/shell.cpp`,
`programy/przegladarka-obrazow/przegladarka-obrazow.cpp`, `programy/menedzer_okien/menedzer_okien.cpp`
(kursor „zajęty”), `programy/kalkulator/kalkulator.cpp`, `programy/instalator/instalator.cpp`,
`programy/husarz/work.h`, `bsf/runtime.cpp`, `bsf/limits.h`.

**Testy / narzędzia / build:** `testy/test_uruchamianie_polityka.cpp`, `testy/test_okno_geometria.cpp`
(nowe), `testy/husarz_load/generate.py`, `testy/bursztyn_ui_host_fixture.h`,
`testy/test_shell_terminal.cpp`, `testy/test_husarz_performance.cpp`,
`testy/test_hda_bws_isolation.cpp`, `testy/fuzz/fuzz_bws.cpp`, `testy/file_operations_host_fixture.h`,
`narzedzia/desktop_qemu_lib.py`, `narzedzia/guest_session.py`, `narzedzia/guest_ctl.py`,
`narzedzia/desktop_responsiveness_qemu.py`, `narzedzia/app_launch_bench.sh`, `Makefile`
(`test-launch-policy-host`, `test-window-geometry-host`, przełączniki diagnostyczne).

## Bezpieczeństwo (skrót)

Bez zmian: separacja Ring 0/3, NX, W^X, SMEP/SMAP, ASLR, SSP, podpisy pakietów, PZB, walidacja
argumentów syscalli. Nowe powierzchnie: (1) delegowanie startu czterech wbudowanych aplikacji —
dokładne ścieżki, poziom/prawa z tablicy jądra, bajt-w-bajt zgodność z wzorcem dla rodzica bez
pełnego zaufania; (2) cień warstwy — pamięć jądra, nie wspólna pamięć procesów; (3) geometria
okien — arytmetyka `int64` z przycinaniem, rozmiary do 2× ekranu w ramach limitu slotu 64 MiB;
(4) BWS 100 zwraca tylko kod własnej ostatniej próby. Limit obrazu jądra: patrz raport końcowy.

## Znane ograniczenia (zbiorczo)

* Brak KVM i internetu: wszystkie pomiary pod TCG; brak realnych stron HTTPS.
* Instalator: nie zmieniany; nie wykonano testu zainstalowanego systemu (właściciel potwierdził ręczną instalację).
* Zmiana rozmiaru natywnie niezweryfikowana dla Terminala i Przeglądarki Obrazów oraz dla rozdzielczości innych niż 1280×720.
* `test-explorer-host` pada na HEAD (stare oczekiwanie testu) — patrz `explorer-rendering-stability.md`.
* Nie zaimplementowano: przesuwania okien klawiaturą, rozmiaru okna Snake’a, debounce wyszukiwania w Eksploratorze,
  testów 1000+ plików, testów z tabletem VirtIO.
