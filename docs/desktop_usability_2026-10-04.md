# Pulpit i menu tekstowe ścieżki — 2026-10-04

Zmiany wykonano w bieżącym worktree, przyjmując ostatnią pozytywną weryfikację ręczną użytkownika jako stan bazowy. Nie uruchamiano QEMU. Weryfikacja nowych zachowań w działającym systemie pozostaje ręczna.

## Zaznaczenie i motywy

Zaznaczenie jest sumą prostokąta ikony (48 logicznych px) oraz faktycznej szerokości i wysokości wszystkich zawiniętych linii nazwy, powiększoną o 3 logiczne px z każdej strony. Nie wypełnia komórki 120 × 100. Ramka ma 1 logiczny px, a wszystkie wymiary skalują się przez istniejące DPI. Hit-test zachowuje wygodną szerokość komórki; wysokie nazwy zajmują odpowiednią liczbę rzędów. Zmniejszenie zaznaczenia nie zmniejsza celu kliknięcia.

Centralne role `MOTYW_KOLOR_PULPIT_ZAZNACZENIE`, `MOTYW_KOLOR_PULPIT_RAMKA` i `MOTYW_KOLOR_PULPIT_TEKST` są pochodnymi istniejącej palety v2. Tło miesza `panel` z `tekst` w proporcji 231:24, ramka 175:80, tekst używa `tekst`. Definicja obowiązuje wszystkie motywy i aliasy oraz fallback, bez zmiany 172-bajtowego ABI motywu. Paleta przycisków akcentowych i żółte `zaznaczenie` motywu jasnego nie sterują zaznaczeniem pulpitu. Testy obejmują Bursztyn Jasny, Nocny, Klasyczny, Grafit i Bałtycka Noc.

To **obliczone mieszanie RGB**, nie alpha nad tapetą. Prywatna warstwa pulpitu traktuje zero jako przezroczysty otwór; pozostałe piksele są nieprzezroczyste. Nie wprowadzono częściowych wartości alpha. Przełączenie motywu odrysowuje zaznaczenie i zachowuje zestaw wybranych wpisów. Niezaznaczone etykiety zachowują istniejącą biel z cieniem nad tapetą.

## Nazwy i układ

Usunięto obcinanie po dziewięciu znakach. `Desktop::wrap` dekoduje UTF-8 po punktach kodowych i mierzy narastającą linię przez aktualny systemowy font Unicode (`oblicz_szerokosc_tekstu`). Każda linia ma granice na pełnych znakach i jest wyśrodkowana pod ikoną. Nie usuwamy spacji ani części nazwy między liniami.

Szerokość etykiety wynosi 112 logicznych px; wysokość linii 16, odstęp 2. Metadane układu mają limit **16 linii**, dodatkowo ograniczony dostępną wysokością obszaru pulpitu. Obecny filesystem obsługuje nazwy do 55 bajtów; normalne nazwy mieszczą się w tym limicie linii, gdy dostępne są odpowiednie rzędy. Wyjątkowo długi tekst kończy układ na granicy pełnego znaku i zgłasza `complete=false`. Nigdy nie wychodzi poza zarezerwowane rzędy.

Ikony plików/katalogów mają niezależne `col`, `row`, `span`, `custom` i zaznaczenie. Automatyczne krótkie etykiety zachowują początkowy układ wierszowy. Długie etykiety rezerwują dodatkowe rzędy; konfliktujące nowe wpisy dostają najbliższą wolną pozycję. Użytkownik może przeciągnąć ikonę lub grupę, oglądając obrysy przesuwanych elementów; po puszczeniu wracają one do siatki. Rezerwujemy rzędy wbudowanych aplikacji oraz obszar paska zadań. Gdy brakuje miejsca dla nowych wpisów, model nie umieszcza ich poza ekranem ani na sobie — istniejący limit widocznej pojemności pulpitu pozostaje. Nie dodano przewijania ani stron pulpitu.

Zrzut szuka najbliższego **wspólnego** przesunięcia w komórkach siatki, które mieści wszystkie elementy i nie koliduje z niezaznaczonymi. Jeśli istnieje tylko przesunięcie zerowe, grupa pozostaje na poprzednim miejscu. W ruchu wyznaczamy jeden ograniczony delta dla całej grupy, zachowując wzajemne położenie.

Pozycje i zaznaczenie znajdują się w trwałych dla procesu, ograniczonych tablicach `Desktop::Layout<128>`. Odświeżanie listy przenosi je między wpisami według nazwy i typu. **Pozycje obowiązują tylko w bieżącej sesji WM, nie po restarcie.** Audyt znalazł `ustawienia_sciezka`, pomocnik budujący ścieżki w `/uzytkownicy/domyslny/Ustawienia/`, lecz nie używany mechanizm odczytu/zapisu układu pulpitu ani jego walidacji i publikacji. Zgodnie z dopuszczonym wariantem nie wprowadzono ad hoc pliku konfiguracji; trwały zapis układu pozostaje dalszą pracą.

## Wejście i odrysowanie

Lewy klik wybiera pojedynczy wpis; Ctrl+klik przełącza dany wpis. Naciśnięcie na już wybranym członku grupy zachowuje grupę na potrzeby przeciągania; zwykłe puszczenie bez ruchu wybiera tylko ten element. Kliknięcie pustego pulpitu bez Ctrl czyści zaznaczenie, także w pustych marginesach ponad rzędami plików. Wbudowane skróty aplikacji nadal korzystają z istniejącej obsługi.

Ruch po pustym pulpicie uruchamia prostokąt zaznaczania po przekroczeniu progu 4 logicznych px. Przecięcie prostokąta z kompaktowym wizualnym prostokątem wpisu wybiera go; Ctrl zachowuje wcześniej wybrane elementy. Ctrl+A korzysta z fizycznego routingu USB/PS2. Stan modyfikatorów śledzimy również w czasie otwartego popupu; utrata fokusu go zeruje.

Capture pochodzi z istniejącego `gui_ustaw_capture_myszy`. MouseUp, Blur, prawy przycisk i zmiana geometrii zwalniają go. Przeciąganie ikon nigdy nie wywołuje filesystem move, rename ani systemowego drag-drop plików. Nadal działa zewnętrzny drop do katalogów pulpitu oraz chooser Przenieś.

Ruch odrysowuje stare/nowe krawędzie obrysów oraz ikony/etykiety ze zmienionym zaznaczeniem. Po zrzucie odrysowuje stare i nowe pozycje. Nie czyści całej warstwy, nie dekoduje tapety i pomija powtarzające się delty. Zero usuwa lokalne piksele nakładki; kompozytor odtwarza swoją scenę z buforowanej tapety. Wbudowane ikony przecinające usuwany obrys są odtwarzane lokalnie. Zachowano istniejący present i transakcję kursora. Istniejący backend łączy żądania aplikacji do jednego obejmującego dirty rect; testy sprawdzają żądania rysowania aplikacji, nie liczbę flushów GPU w runtime.

## Menu plikowe i schowek

Prawy klik niewybranego wpisu wybiera go; klik wybranego członka grupy zachowuje grupę. Kopiuj, Wytnij i Usuń obejmują cały zestaw. Otwórz, Otwórz za pomocą, Notatnik, Zmień nazwę i Przenieś są wyłączone dla grupy. Zewnętrzne menu i popupy korzystają z dotychczasowej infrastruktury bez click-through.

Usuwanie grupowe wymaga potwierdzenia z liczbą elementów. Przed otwarciem dialogu zapamiętuje cały zestaw ścieżek, niezależnie od późniejszych zmian indeksów listy. Każde usunięcie przechodzi dotychczasowe BWS/PZB. Pierwszy błąd zatrzymuje operację, odświeża pulpit i zgłasza liczbę wykonanych operacji oraz ścieżkę błędu. Zatwierdzone wcześniejsze usunięcia nie są udawane jako transakcja grupowa.

Istniejący bezplikowy schowek BWS 52–54 został rozszerzony addytywnie: do 128 ścieżek po 512 bajtów, publikacja całego zestawu po walidacji każdego źródła, odczyt indeksowany z kontrolą generacji. Rozmiar starego rekordu (528 B) jest zachowany; pole `zarezerwowane` informuje o liczbie wpisów. Wszystkie aktualne konsumenty w repozytorium korzystają ze wspólnego `PlikiUI::wklej`, który obsługuje pełny zestaw. Starszy zewnętrzny konsument musi respektować liczbę wpisów. Kernel przechowuje i publikuje zestaw pod istniejącą blokadą schowka. Bufor ścieżek po stronie WM jest alokowany na stercie z kontrolą niepowodzenia alokacji, aby zachować limit DATA+BSS 128 KiB.

Wklejanie wywołuje dotychczasowe bezpieczne copy/move dla każdego elementu, bez nadpisywania celu. Przy częściowym błędzie zgłasza wykonane/żądane i pierwszy błędny element. Po częściowo udanym CUT usuwa wyłącznie udany prefiks schowka z kontrolą generacji, aby ponowienie nie próbowało już przeniesionych źródeł. COPY zachowuje zestaw w schowku. Explorer również obsługuje grupowe wklejanie i odrysowuje częściowo udany wynik przed pokazaniem błędu. Schowek plików pozostaje oddzielny od MIME schowka tekstowego; nie zmieniano BSP2.

## Pole ścieżki

`BursztynUI::MenuTekstowe` jest ponownie używalnym adapterem `PoleTekstowe`, wykorzystującym `MenuKontekstowe`, `WidokMenuPopup`, `ZarzadcaPopupow` i istniejący adapter eventów BWS. Nie ma renderera popupu Explorera.

Menu zawiera Kopiuj, Wytnij, Wklej, Usuń zaznaczone i Zaznacz wszystko. Akcje delegują do istniejących metod kontrolki i schowka `text/plain`. Enabled zależy od zaznaczenia, trybu read-only/hasła, zgodnego MIME i niepustego tekstu. Prawy klik nie przenosi karetki ani nie kasuje istniejącego zaznaczenia. Escape zamyka najpierw menu; klik poza nim i klik akcji są konsumowane wraz z odpowiednimi zwolnieniami. Po zamknięciu działa Enter nawigacji i normalna edycja. Istniejące wspólne Ctrl+A/C/X/V pozostają w `PoleTekstowe`.

Poprawiono również ograniczenia kopiowania, ustawiania i zastępowania tekstu oraz odczytu schowka przy końcu bufora: obcięcie nie zostawia fragmentu znaku UTF-8.

## Pliki źródłowe, testy i dokumentacja

- `Makefile` — zależności nowych nagłówków i współdzielonego ABI.
- `desktop_layout.h` — mierzone zawijanie, kompaktowe granice, siatka, kolizje i wspólne delta.
- `menedzer_okien.cpp` — integracja modelu, renderowanie, capture, gesty, grupowe akcje i odrysowanie.
- `motyw.h` — centralne pochodne role zaznaczenia i mieszanie RGB, bez zmiany wire ABI.
- `bursztyn_gui.h`, `bursztyn_gui.cpp` — API grupowego schowka i odczyt ról motywu.
- `bws_pliki.h` — addytywny kontrakt schowka grupowego.
- `syscalls.cpp` — walidacja i publikacja zestawu, odczyt z generacją, usuwanie prefiksu CUT.
- `plik_operacje_ui.h` — wspólne grupowe wklejanie i raport częściowego błędu.
- `plik_popup_ui.h` — stany menu zależne od liczby wybranych wpisów.
- `bursztyn_ui/menu_tekstowe.h` — wspólny natywny adapter menu tekstowego.
- `bursztyn_ui/pole_tekstowe.h` — zachowanie granic UTF-8 przy ograniczeniu bufora.
- `programy/eksplorator/eksplorator-plikow.cpp` — menu ścieżki, grupowe wklejanie i odrysowanie częściowego wyniku.
- `narzedzia/bursztyn_ui_host_fixture.h` — MIME, capture, centralne role motywu.
- `narzedzia/file_operations_host_fixture.h` — schowek grupowy, częściowa odmowa i rzeczywista skala pomiaru fontu.
- `narzedzia/desktop_usability_cases.h` — produkcyjne przypadki geometrii, nazw, gestów, grup i akcji.
- `narzedzia/text_context_cases.h` — natywne akcje menu, UTF-8, disabled, zamknięcie i routing.
- `narzedzia/test_desktop_files_host.cpp` — integracja i aktualizacja oczekiwań kolorystycznych.
- `narzedzia/test_explorer_host.cpp` — integracja menu oraz zachowania Enter/edycji po zamknięciu.
- `narzedzia/test_move_authorization.cpp` — produkcyjny BWS/PZB, generacje, odmowa częściowa i limit 128 źródeł.
- `docs/desktop_usability_2026-10-04.md` — ten raport.

Standardowe artefakty budowy: `.o`, `.su`, `.elf`, `.bin`, ISO, `isodir/`, manifest/pak tapet oraz binaria testów. Nie są zmianami źródeł. Nie wykonywano `make rm`, `make clean`, formatowania ani QEMU. Nie zmieniono źródeł VirtIO GPU/Input, mapowania kursora, schedulera, HPET, BSP2, instalatora, GRUB, reboot ani polityk GTK/SDL; standardowa budowa ISO regeneruje swoją zawartość.

## Testy i stos

Wszystkie poniższe wyniki dotyczą hosta/RAM lub budowy. Nowe testy geometrii, nazw, gestów i menu obejmują 100%/125% oraz 1280×720/1920×1080.

| Zestaw | Wynik końcowy |
|---|---|
| Pulpit, produkcyjne operacje i usability | **1406/1406 PASS** |
| Explorer, regresje i menu tekstowe | **12 191/12 191 PASS** |
| BursztynUI `test-kontrolki4` | **1379/1379 PASS** |
| Command bindings | **23/23 PASS** |
| Routing WM | **152/152 PASS** |
| Dekoracje WM, 100%/125% | **PASS** — runner nie podaje liczby |
| Motywy unit | **155/155 PASS**, także ASan/UBSan |
| Motywy GUI/statyczna integracja | **25/25 PASS** |
| Storage/BSP2 | **10 493 PASS, 0 FAIL**; w tym runner operacji move zgłasza 138 asercji |
| Produkcyjny BWS/PZB, move i schowek grupowy | **438 asercji PASS** |
| Shell, edytor/popup/schowek/sponsorzy, 100%/125% | **PASS** — runner nie podaje liczby |
| Kursor, clipping/coalescing/present | **PASS** — runner nie podaje liczby |
| Produkcyjny kompozytor, transakcje kursora/pełnej sceny | **PASS** — runner nie podaje liczby |
| `git diff --check` | **PASS** |
| Końcowe `make -j2 all iso`, po testach | **PASS, exit 0** |

Pierwsza budowa ujawniła przekroczenie limitu DATA+BSS WM przez 64 KiB statycznych ścieżek grupy. Przeniesiono je na stertę i wykonano ponowną budowę oraz testy produkcyjnego pulpitu. Linker zachował dotychczasowy limit 128 KiB DATA+BSS i 256 KiB TEXT. Nie powiększano formatu BUR. Wszystkie powyższe końcowe wyniki są po naprawie.

Testy desktop/Explorer wykonano przez istniejące targety ASan/UBSan `-O1`; po końcowych zmianach i uzupełnieniach uruchomiono najnowsze zestawy również z ASan/UBSan `-O0` (1406 i 12 191). Pozostałe zestawy C++ korzystają z sanitizerów w istniejących targetach, z dodatkowym osobnym sanitized runnerem motywów unit. **Brak zgłoszeń ASan/UBSan.** Leak detection wyłączona w runnerach z wyjątkiem produkcyjnego kompozytora, który przeszedł z `detect_leaks=1`. Nie jest to pełny audyt wycieków ani runtime systemu.

Pomiary produkcyjnego kompilatora `-fstack-usage`:

| Funkcja / jednostka Ring 3 | Ramka |
|---|---:|
| Explorer `_start` — największa ramka w tej jednostce | **2688 B** |
| WM `_start` — największa zmieniona ramka WM | **1264 B** |
| WM `odswiez_wpisy_pulpitu` | **240 B**, poprzednio 7344 B |
| WM `aktualizuj_layout_pulpitu` | **304 B**, dynamic bounded |
| WM `obsluz_gest_pulpitu` | **128 B**, dynamic bounded |
| WM `RysujWpisPulpitu` | **240 B**, dynamic bounded |
| Wspólny, niezmieniony `DialogPliku::normalizuj_sciezke` | **2624 B** |
| Niezmieniony dekoder PNG w WM `tinf_uncompress` | **3552 B** — największa ramka całej jednostki WM |

To pojedyncze ramki, nie pomiar łącznego szczytu stosu. Tablice do 128 wpisów, poprzedniego układu, parsowania listy i bazowego zaznaczenia są poza lokalnym stosem, a zestaw ścieżek na stercie. Kernelowy setter schowka ma ramkę 112 B, getter 576 B; staging grupy jest poza stosem.

Logi: `/tmp/desktop-usability-regressions.log`, `/tmp/desktop-filesystem.log`, `/tmp/desktop-usability-explorer-final.log`, `/tmp/desktop-usability-latest-desktop.log`, `/tmp/desktop-theme-sanitized.log`, `/tmp/desktop-usability-build-final.log`, `/tmp/desktop-usability-build-after-tests.log`. Ostatni zestaw Explorera 12 191 i BWS 438 zgłoszono również bezpośrednio w wynikach narzędzi po wykonaniu końcowych runnerów. `/tmp` jest nietrwałe.

Przebudowano śledzone artefakty testów `.wm-test/click-routing`, `.wm-test/dekoracje`, `.wm-test/shell` i `.wm-test/compositor-cursor`.

## Akceptacja ręczna

W 100% i 125%, przy 1280×720 i 1920×1080: kompaktowe zaznaczenie w każdym motywie (szczególnie Jasnym), pełne polskie nazwy i brak nakładania etykiet, prawy klik/akcje pola ścieżki i Enter po zamknięciu, pojedyncze oraz grupowe przeciąganie do wszystkich krawędzi, zajęte pola siatki, Ctrl+klik, Ctrl+A, prostokąt i Ctrl+prostokąt, zachowanie zaznaczenia przy przełączeniu motywu, grupowe menu oraz potwierdzenie usuwania. Sprawdzić też natychmiastowe odświeżanie Explorera, scrollbar, chooser Przenieś, brak click-through i brak pogorszenia płynności kursora. **Nowe zachowania nie mają runtime PASS; akceptację wykonuje użytkownik.**
