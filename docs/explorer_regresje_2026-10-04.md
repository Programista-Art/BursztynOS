# Explorer i pulpit — poprawki regresji, 2026-10-04

Praca kontynuuje bieżący worktree. Wyniki poniżej dotyczą testów hostowych i budowy; **nie są potwierdzeniem działania w QEMU na dysku użytkownika**.

## Przyczyny i zmiany

1. **Odświeżanie katalogu.** Metody mutacji ustawiały tylko `hash_valid_=false`; model nadal zawierał poprzednie wpisy do wywołania `refresh()` przez kod prezentacji. Powiadomienie `BWS_ZDARZENIE_PLIKI_ZMIENIONE` odebrane podczas dialogu/menu było pomijane bez zapamiętania. Teraz każda udana mutacja wykonuje `refresh_after_mutation()`, zachowując katalog, filtr, sortowanie, scroll i nadal prawidłowe zaznaczenie. Rename najpierw zmienia nazwę zachowywanego zaznaczenia; delete/move bezpiecznie je usuwa. Oczekująca zmiana jest przekazywana prezentacji, a `refresh()` nadal odczytuje katalog, aby późniejsze powiadomienie z innej aplikacji nie zniknęło. Powiadomienia w czasie popupów są odkładane do zamknięcia. Usunięto cykliczne odpytywanie Explorera.

   Te błędy są potwierdzone analizą kodu i testami modelu. W poprzednim kodzie niektóre wywołania z GUI już jawnie wykonywały `refresh()` po sukcesie. Bez odtworzenia zgłoszonego przypadku na właściwym dysku nie można uczciwie uznać samego odroczonego odświeżania za dowiedzioną, jedyną przyczynę objawu „dopiero po ponownym otwarciu”.

2. **Miganie przy nawigacji.** `present_activation()` i `present_workspace_change()` wywoływały `draw_workspace()`: wypełnienie całego tła klienta, cały toolbar i cały lewy panel. Nowa ścieżka `draw_directory_change()` odrysowuje ścieżkę, tabelę, szczegóły, status, zmienione zaznaczenia szybkiego dostępu i wyłącznie przyciski ze zmienionym stanem enabled. Nie odrysowuje dekoracji WM. Test rejestruje prostokąty prymitywów rysowania przy 100% i 125% i odrzuca pełne tło klienta, cały toolbar oraz cały panel nawigacji. To test żądań rysowania aplikacji, nie pomiar migania obrazu QEMU ani dowód rozmiaru końcowego prostokąta kompozytora.

3. **Brązowa ramka.** Explorer nadal wywoływał `gui_rysuj_okno()`. Backend `bws_gui_rysuj_okno()` w `grafika.cpp` wypełnia prostokąt kolorem `0x008A5A00` i rysuje starą belkę oraz tło `0x00280F00`. Usunięto wywołanie z Explorera; tytuł rejestrowany jest przez `gui_ustaw_tytul_okna()`. Aktualna ścieżka standardowej belki pozostaje zachowana. Wewnętrzne obramowania korzystają z ról motywu. Nie zmieniono globalnej architektury dekoracji ani starego API używanego przez inne aplikacje.

4. **Przewijanie.** Explorer zawsze rezerwował miejsce i rysował pasek, a jako maksimum podawał liczbę wpisów zamiast `max(0, liczba_wpisów - liczba_widocznych_wierszy)`. Teraz widoczność i szerokość listy zależą od przepełnienia; zakres, rozmiar strony, thumb, renderowanie i hit-test korzystają z tej samej geometrii. Kółko działa nad listą, PageUp/PageDown o stronę, drag zachowuje capture do MouseUp. Ostatni wpis jest osiągalny bez wyjścia poza zakres. Nawigacja i szczegóły nie mają dekoracyjnych scrollbarów. W tabeli natywnego chooser'a poprawiono też zależność osi pionowej od poziomego paska oraz uwzględnianie obramowania i separatora nagłówka.

5. **Backspace/Delete.** `DialogPlikowy::zdarzenie()` konsumował fizyczne klawisze poza Tab, a ścieżka bajtowa wysyłała Backspace jako `0x0e` zamiast kodu `8` oczekiwanego przez `PoleTekstowe`. Wspólny adapter popupów teraz tłumaczy klawisze USB/PS2/E0, kieruje je do aktualnego fokusu oraz dekoduje tekst UTF-8 i sekwencje ANSI. Echo bajtowe fizycznego klawisza nie wykonuje edycji drugi raz. PoleTekstowe pozostaje jedynym edytorem; obsługuje Backspace, Delete, Left/Right, Home/End, wstawianie i zastępowanie zaznaczenia. Escape anuluje, Enter zatwierdza; Tab przenosi fokus. Te same zasady obejmują ścieżkę i wyszukiwanie. Open With jest dialogiem potwierdzenia bez edytowalnego pola.

6. **Przenieś.** Wspólny `WyborKatalogu` używa istniejącego `DialogPliku` w trybie `WybierzKatalog`. Pokazuje źródło w tytule, bieżącą ścieżkę docelową, listę katalogów, nawigację w górę, otwieranie katalogu, wybór i anulowanie. Zwykłe pliki są odfiltrowane. Opcjonalne pole katalogu nadal korzysta z PoleTekstowe. Wynik resetuje się przy ponownym otwarciu. Rzeczywiste przeniesienie korzysta z istniejącego API/PZB; kolizja, plik jako cel, własny katalog i własne poddrzewo są odrzucane bez nadpisywania. Chooser współdzielony jest z pulpitem. Istniejące limity natywnego dialogu (512 wpisów i listing 64 KiB) pozostają; nie rozszerzano formatu BSP2.

7. **Prawy klik pustego pulpitu.** Warstwa pulpitu jest przezroczysta nad tapetą. `wm_warstwa_pod_punktem()` pomijała zerowe piksele tak jak w zwykłych oknach, więc tło nie otrzymywało wejścia. Root desktop (`z_order==0`) teraz posiada wejście również na przezroczystym tle. Wydzielona produkcyjna obsługa prawego kliknięcia ustala kontekst wpisu lub `-1`, otwiera wspólne natywne menu i konsumuje klik. Popup konsumuje także klik zamknięcia i odpowiadający MouseUp. Testy obejmują pusty pulpit, plik, katalog, enabled/disabled, motyw i brak uruchomienia ikony po zamknięciu.

8. **Menu.** Etykieta usuwania we wspólnym menu brzmi dokładnie `Usuń` dla plików i katalogów. Zachowano potwierdzenie, schowek plików, bezpieczeństwo, Notatnik Open With oraz wspólne obliczanie rozmiaru, wyrównanie i DPI popupów.

## Zmienione pliki źródłowe, testy i dokumentacja

- `programy/eksplorator/eksplorator-plikow.cpp` — model mutacji, powiadomienia, odrysowanie, frame, scrollbar, fokus, chooser.
- `programy/eksplorator/eksplorator_linker.ld` — rezerwacja TEXT 256 KiB zgodna z nagłówkiem BUR.
- `plik_popup_ui.h` — routing klawiatury/tekstu, wspólny chooser i etykieta `Usuń`.
- `bursztyn_ui/dialog_pliku.h` — reset wyniku po otwarciu i lista wyłącznie katalogów w trybie wyboru katalogu.
- `bursztyn_ui/tabela.h` — obliczenia viewportu/pasków i zgodny hit-test.
- `menedzer_okien.cpp` — chooser pulpitu, jego lifecycle/motyw, produkcyjny handler prawego kliknięcia i nagłówek BUR.
- `menedzer_okien_linker.ld` — rezerwacja TEXT 256 KiB zgodna z nagłówkiem BUR.
- `wm_input.h` — hit-test przezroczystego tła root desktop.
- `narzedzia/bursztyn_ui_host_fixture.h` — rejestracja prostokątów rysowania do regresji.
- `narzedzia/test_explorer_host.cpp` — integracja nowych regresji i aktualizacja oczekiwań.
- `narzedzia/explorer_regression_cases.h` — przypadki mutacji, utraty powiadomień, repaintu, scrolla, klawiatury i chooser'a.
- `narzedzia/test_desktop_files_host.cpp` — produkcyjne akcje/routing pulpitu i popupy.
- `narzedzia/test_wm_click_routing.cpp` — root desktop, przezroczystość i powiązane regresje.
- `docs/explorer_regresje_2026-10-04.md` — ten raport.

Testy przebudowały także śledzony artefakt `.wm-test/click-routing`. Pozostałe pliki `.o`, `.su`, `.elf`, `.bin`, katalog `isodir/` i `BursztynOS.iso` są artefaktami budowy, nie zmianami źródeł. Nie wykonano `make rm`, zmian sterowników VirtIO, globalnego mapowania kursora, częstotliwości schedulera, rebootu, instalatora ani GRUB. Budowa wygenerowała standardową zawartość ISO.

## Weryfikacja automatyczna

| Zestaw | Wynik końcowy |
|---|---|
| Explorer i wspólne zachowanie plików/menu | 11 893/11 893 PASS |
| Produkcyjne akcje pulpitu na filesystemie RAM | 106/106 PASS |
| BursztynUI (`test-kontrolki4`) | 1379/1379 PASS |
| Command bindings | 23/23 PASS |
| Routing WM | 152/152 PASS |
| Dekoracje WM | PASS; zestaw nie drukuje liczby asercji, obejmuje 100%/125% |
| Motywy unit | 155/155 PASS |
| Motywy GUI, kontrola integracji kodu | 25/25 PASS |
| Storage/BSP2 | 10 343 PASS, 0 FAIL |
| Loader BUR | 35 jednostkowych + 17 kontroli obrazu aktualizacji PASS |
| Nagłówki/layout BUR Explorera i WM | oba PASS |
| `git diff --check` | PASS |
| `make -j2 all iso` | PASS, exit 0 |

Explorer: tworzenie, rename, move, COPY/paste, CUT/paste i delete są sprawdzane bez ręcznego odświeżenia modelu. Testy obejmują zachowanie filtra/sortu/zaznaczenia, późniejsze zdarzenie po mutacji, brak pełnego rysowania przy nawigacji, puste/pojedyncze/dokładnie pełne/przepełnione/długie listy, thumb, wheel, drag, PageUp/PageDown i ostatni wpis. Edycja polskiego UTF-8 testowana jest w Rename, New Folder, New File, Move, Path i Search, z focusem i fizycznym/bajtowym wejściem. Skale: 100% i 125%. Open With testowany jako potwierdzenie bez pola tekstowego.

ASan/UBSan: wszystkie powyższe zestawy C++ poza domyślnym `test-motywy-unit` były kompilowane z `-fsanitize=address,undefined`; dodatkowo motywy unit uruchomiono z tymi sanitizerami (155/155). Brak zgłoszeń ASan/UBSan. Leak detection wyłączona zgodnie z istniejącymi targetami testowymi; nie jest to raport braku wycieków. Testy Pythona sprawdzają integrację/statyczny kontrakt, nie obraz uruchomionego systemu.

Logi odtworzonej weryfikacji: `/tmp/resume-explorer-tests.log`, `/tmp/resume-other-tests.log`, `/tmp/resume-final.log`, `/tmp/resume-theme-sanitized.log`, `/tmp/resume-build-confirmed.log`. `/tmp` jest nietrwałe między restartami hosta.

## Stos Ring 3

Pomiar produkcyjnym kompilatorem z `-fstack-usage`, po końcowej budowie:

| Funkcja | Ramka |
|---|---:|
| Explorer `_start` | 2400 B |
| Explorer `AktowkaModel::load` | 1616 B |
| Explorer `AktowkaModel::run_package` | 2032 B |
| Największa ramka jednostki Explorera: `DialogPliku::normalizuj_sciezke` | 2624 B |
| WM `_start` | 1376 B |
| WM `odswiez_wpisy_pulpitu` | 7344 B — bez wzrostu |
| WM istniejący dekoder PNG `tinf_uncompress` | 3552 B |
| WM `DialogPliku::normalizuj_sciezke` | 2624 B |

To rozmiary pojedynczych ramek, nie pomiar łącznego szczytu zagnieżdżonego stosu. Duże modele/bufory pozostają poza stosem.

## Runtime — niewykonany

**QEMU: 0 sesji. Żaden punkt checklisty runtime nie otrzymuje PASS.**

W katalogu projektu nie ma `wirtualnydysk.img` ani `wirtualny_dysk.img`. Znaleziony `/home/programista/Pulpit/wirtualny_dysk.img` ma 4 GiB, zerowe pierwsze 1024 bajty, sygnaturę MBR `0000` i brak sygnatury GPT. Nie został zidentyfikowany jako dysk używany w ostatnim teście użytkownika. Poproszono o jego właściwą ścieżkę. Nie tworzono ani nie formatowano obrazu, nie instalowano systemu i nie modyfikowano jego zawartości.

Po wskazaniu właściwego obrazu pozostaje jedna dozwolona sesja QEMU z SDL, `virtio-tablet-pci`, `virtio-keyboard-pci` i `tcg,thread=multi`. Mutacje muszą ograniczać się do nowego dedykowanego katalogu testowego na tym dysku. Do obserwacji pozostają: otwarcie Explorera; natychmiastowe create/rename/move; edycja Backspace/Delete; chooser; brak zbędnego scrolla i poprawne przewijanie; nawigacja bez flash; brak starej brązowej ramki; `Usuń` w menu; prawy klik pustego pulpitu; tworzenie na pulpicie; brak click-through; przełączanie motywu. Nie wykonywać reinstalacji, formatowania, testów destrukcyjnych istniejących danych, reboot loopów ani GDB.


## Ponowne wznowienie sesji 01a10575 — aktualny worktree

Powyższe wyniki i lista zmian opisują poprzedni etap. Przy wznowieniu zastano dalsze, niecommitowane zmiany; zachowano je. Bieżąca weryfikacja:

| Zestaw | Wynik |
|---|---|
| Explorer | 11 965/11 965 PASS |
| Operacje pulpitu | 241/241 PASS |
| BursztynUI | 1379/1379 PASS |
| Command bindings | 23/23 PASS |
| Routing WM | 152/152 PASS |
| Dekoracje WM | PASS, 100% i 125% |
| Motywy unit | 155/155 PASS, również ASan/UBSan |
| Motywy GUI | 25/25 PASS |
| Storage/BSP2 | 10 493 PASS, 0 FAIL |
| Autoryzacja przenoszenia BWS | 27 asercji PASS |
| Diagnostyka przenoszenia w kompozytorze, BURSZTYNDIAGMOVE=1 | Kompilacja PASS |
| Końcowe make -j2 all iso | PASS, exit 0 |
| git diff --check | PASS |

W tym wznowieniu poprawiono dwa pliki kodu/testów:

- `skladacz_obrazu.cpp`: diagnostyka odwoływała się do nieistniejącego `warstwa_obrazu::tytul`, blokując budowę. Używa teraz istniejącej `tytul_okna` z walidacją indeksu PID. Odczyt jest kompilowany wyłącznie przy włączonym `BURSZTYNDIAGMOVE`; normalna budowa nie wykonuje diagnostycznej klasyfikacji.
- `narzedzia/instalator/test_storage.cpp`: nowy test oczekiwał automatycznego powrotu trwałego zapisu po błędzie flush. Istniejące BSP2 celowo blokuje trwały zapis po błędzie I/O. Przeniesiono fault test po sprawdzeniach wymagających zdrowego backendu; test sprawdza zachowanie źródła, brak publikacji celu, zwolnienie blokady oraz utrzymanie odmowy flush po ustąpieniu awarii mocka. Nie zmieniano produkcyjnej polityki I/O. Pierwszy przebieg zatrzymał się na tym oczekiwaniu; osobny końcowy przebieg filesystemu przeszedł.
- `docs/explorer_regresje_2026-10-04.md`: niniejsze uzupełnienie wyników.

Pozostałe zastane zmiany worktree obejmowały `Makefile`, `bursztyn_ui/popup.h`, `bursztyn_ui/rysowanie.h`, `grafika.cpp`, `menedzer_okien.cpp`, `narzedzia/bursztyn_ui_host_fixture.h`, `narzedzia/test_desktop_files_host.cpp`, `narzedzia/test_explorer_host.cpp`, `plik_popup_ui.h`, `programy/eksplorator/eksplorator-plikow.cpp`, `psf.cpp`, `psf.h`, `syscalls.cpp` oraz nowe `move_trace.h` i `narzedzia/test_move_authorization.cpp`. Nie cofano ich ani nie przypisywano ich temu wznowieniu.

Brak zgłoszeń ASan/UBSan w końcowych testach C++; leak detection wyłączone. Początkowy błąd testu był nieudaną asercją, nie raportem sanitizera. Testy filesystemu używają RAM/mocka, nie dysku użytkownika.

Aktualne ramki Ring 3: Explorer `_start` 2400 B, `submit_path` 640 B, maksymalna ramka jednostki Explorera `DialogPliku::normalizuj_sciezke` 2624 B; WM `_start` 1376 B, `odswiez_wpisy_pulpitu` 7344 B, `normalizuj_sciezke` 2624 B. Pomiar pojedynczych ramek, bez pomiaru szczytu stosu.

Logi: `/tmp/resume-01a10575-tests.log`, `/tmp/resume-01a10575-filesystem.log`, `/tmp/resume-01a10575-bindings.log`, `/tmp/resume-01a10575-theme.log`, `/tmp/resume-01a10575-build-final.log`.

**Runtime nadal niepotwierdzony.** Historyczna sekcja „QEMU: 0 sesji” powyżej poprzedza późniejszą próbę opisaną w `docs/explorer_live_runtime_2026-10-04.md`: jedna sesja Live ISO bez persistent disk zatrzymała się przed pulpitem. Nie spełniła warunku testowania obecnego `wirtualnydysk.img`. W tym wznowieniu nie uruchomiono QEMU ponownie, nie formatowano ani nie modyfikowano obrazów dysku. Żaden punkt checklisty Explorera w runtime nie otrzymuje PASS. Kolejna sesja wymaga zmiany wcześniej ustalonego limitu jednej próby i identyfikacji właściwego obrazu przez użytkownika.
