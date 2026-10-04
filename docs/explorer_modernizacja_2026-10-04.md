# Modernizacja Explorera i operacji pulpitu — 2026-10-04

## Zakres i architektura

Explorer zachowuje model katalogów, historię, sortowanie, wyszukiwanie, panel szybkiego dostępu, metadane/PZB, uruchamianie i drag/drop. Nie zmieniono PSF/BSP2, formatu ani usług ABI BUR, schedulera, GPU, dekoracji WM lub konfiguracji wejścia. Windows nadal GTK, Linux nadal SDL; tablet i klawiatura VirtIO pozostają w konfiguracjach.

Audyt BursztynUI wykazał odpowiedniki starych kontrolek. Zastąpiono:

| Stary element Explorera / pulpitu | Obecna implementacja |
|---|---|
| `RysujPrzycisk`, ręczny rysunek i kolory przycisków | trwałe `BursztynUI::Przycisk`, stany disabled/hover |
| ręczne pola, bufor edycji i znak `|` jako karetka | `PoleTekstowe`, jego karetka i edycja Unicode |
| ręczny nagłówek, komórki i wiersze tabeli | `TabelaWirtualna` + `DostawcaDanychTabeli` nad dotychczasowym modelem |
| dwa przyciski przewijania i pusty tor | `PasekPrzewijania`, model strony i przeciągany uchwyt |
| menu Explorera o szerokości 190 i menu pulpitu 184 | `MenuKontekstowe` → `WidokMenuPopup` → `ZarzadcaPopupow` |
| `GuiModalDialog` w obu aplikacjach; Move w polu wyszukiwania | wspólny `DialogPlikowy` z natywną powierzchnią popup, `Etykieta`, `PoleTekstowe`, `Przycisk` |
| stała brązowa paleta | centralne role motywu dla wszystkich powierzchni i tekstu |

Panel szybkiego dostępu, szczegóły, status i podziały pozostają prostym app-level layoutem, z tekstem i clippingiem BursztynUI i kolorami z ról systemowych. Nie utworzono drugiego zestawu widgetów. Wirtualna tabela otrzymała niewielki adapter stanu modelu: zaznaczenie, pierwszy wiersz, metryki DPI, hover/drop i przewijanie dostarczane przez właściciela. Dzięki temu nie kopiuje do widgetu 8192 rekordów i używa wspólnego scrollbara. Repaint hover obejmuje tylko zmienione kontrolki i stary/nowy wiersz; rysowanie tabeli odpyta tylko wiersze przecinające klip.

## Font, DPI i motywy

Ścieżka fontu: `BursztynUI::CzcionkaUI` / `KontekstRysowania` → `czcionki/extronic16Bunicode.h`. Ten sam font służy pomiarom menu i rysowaniu Unicode. Explorer przestał uruchamiać adapter `gui_legacy_wlacz`: współrzędne, hit testing i popupy używają bezpośrednio geometrii BursztynUI; metryki są zaokrąglane przez `SkalaUI`/`ui_skaluj`, bez podwójnego skalowania przez adapter legacy.

Po `BWS_ZDARZENIE_MOTYW_ZMIENIONY` Explorer odświeża wspólny `Motyw`, style trwałych kontrolek i popupów oraz rysuje klienta. Nie odświeża modelu katalogu ani nie zeruje zaznaczenia, scrolla, filtra, bieżącej edycji czy dialogu. Pulpit odświeża również style otwartych menu i dialogów. Kontrolki i etykiety korzystają z ról systemowych: tło okna/panel/podniesiony, ramka, tekst/drugi/disabled, zaznaczenie, akcent i semantyczne role ikon. Przenoszenie okna aktualizuje położenia trwałych kontrolek bez pełnego repaintu klienta przy każdym ruchu.

W obu BUR-ach zwiększono rezerwację TEXT do 192 KiB i odpowiednio przesunięto DATA, aby pomieścić kod współdzielonych komponentów oraz Unicode font. Format nagłówka, loader i ABI wywołań pozostają bez zmian. Linkery nadal sprawdzają granice segmentów i zakaz konstruktorów globalnych. Explorer przechowuje duże trwałe obiekty na stercie; popupy pulpitu również są konstruowane po wejściu procesu.

## Menu i bezpieczne operacje

`plik_popup_ui.h` zawiera wspólną politykę enabled/disabled oraz adapter zdarzeń BWS dla infrastruktury popupów. Menu używa rzeczywistych pomiarów tekstu Unicode i istniejącego kompaktowego layoutu, wyrównania do lewej i ograniczenia do obszaru roboczego. Wybrany plik/folder jest celem menu. Na pustym tle operacje zależne od elementu pozostają widoczne i wyłączone; tworzenie jest włączone, Wklej zależy od kompatybilnego rekordu schowka. Notatnik/Open with są dostępne wyłącznie dla plików `.txt` (bez względu na wielkość liter), zgodnie z aktualną polityką `otworz_plik_skojarzony`; foldery i BUR nie są traktowane jako tekst.

Menu obejmuje Otwórz, Otwórz za pomocą, Notatnik, Zmień nazwę, Wytnij, Kopiuj, Wklej, Przenieś, Usuń plik, Utwórz folder, Utwórz plik tekstowy i Odśwież. Explorer nadal otwiera programy/paczki przez dotychczasowy mechanizm i kontrole PZB. Pulpit nadal ma dotychczasowe lewy klik/double-click, Start, fokus i drag/drop. Right-click resetuje kandydaturę double-click/drag; zdarzenia otwartego popupu są zużywane przed normalną aktywacją ikon. Klik zamykający poza menu i następujący release są konsumowane. Test produkcyjnego adaptera pulpitu sprawdza, że te zdarzenia nie uruchamiają aplikacji.

`plik_operacje_ui.h` współdzieli walidację nazw, tworzenie, politykę Notatnika i paste. Istniejący schowek plików BWS jest oddzielny od schowka tekstowego Shell. COPY kopiuje przez dotychczasowe API; CUT zapisuje zamiar, a źródło zostaje przeniesione przez atomowe BWS48 dopiero po udanym paste. Rekord CUT jest czyszczony tylko po sukcesie i z pasującą generacją. Kolizja, błąd lub odmowa PZB nie powoduje usunięcia źródła ani nadpisania celu. Nie wprowadzono żadnych operacji filesystemowych przez `text/plain`.

Tworzenie, Rename, Move, Delete, Open with i komunikat błędu używają wspólnego dialogu. Jest pozycjonowany względem właściciela i przycinany do obszaru roboczego; szerokość wynika z tekstu z kompaktowym ograniczeniem DPI. Pole nazwy ma limit 55 bajtów, pole ścieżki 511. New text file proponuje `Nowy plik.txt`, tworząc pusty plik przez istniejące API. Enter akceptuje z pola, Escape anuluje; w potwierdzeniach fokus początkowy jest na Anuluj. Kliknięcie poza modalem nie potwierdza operacji. Delete wymienia element i wykonuje trwałe usunięcie dopiero po zatwierdzeniu. Nieudane operacje pokazują natywny dialog błędu (bez nadpisywania i bez obchodzenia uprawnień).

Open with ma jedną obsługiwaną opcję: Notatnik dla tekstu. Dialog pokazuje nazwę aplikacji i wymaga jawnego Otwórz. Dispatch używa `uruchom_program_z_argumentem_uzytkownika("/programy/notatnik.cebula/notatnik.bur", path)` — istniejącej usługi uruchamiania z argumentem plikowym, bez nowego protokołu command-line. PZB nadal sprawdza istniejąca ścieżka uruchamiania/BWS.

Nazwy są sprawdzane po dekodowaniu UTF-8 i porównaniu z kanonicznym kodowaniem: odrzucane są sekwencje obcięte, overlong, surogaty, wartości poza Unicode, znaki kontrolne, `/`, `\`, nazwy puste oraz `.` i `..`. Limit dotyczy bajtów dotychczasowego filesystemu. Polskie nazwy pozostają niezmienionym UTF-8; nie są konwertowane do ASCII. Wszystkie bufory są ograniczone.

## Weryfikacja

Wszystkie operacje destrukcyjne testów wykonują się na mapie RAM lub istniejącym disposable/mock storage. Nie wykorzystują roboczego dysku Bursztyna. ASan/UBSan używa tego samego ustawienia co istniejące testy (`detect_leaks=0`); jest to kontrola dostępu do pamięci i UB, nie audyt wycieków.

| Kontrola | Wynik |
|---|---|
| `make test-explorer-host` | 302/302 PASS, ASan/UBSan; rzeczywisty model/widok, wspólne menu i filesystem RAM |
| `make test-desktop-files-host` | 39/39 PASS, ASan/UBSan; rzeczywiste funkcje WM/operacji pulpitu i zużycie zamykającego click/release |
| `make test-kontrolki4` | 1379/1379 PASS, ASan/UBSan |
| `make test-bursztyn-ui-polecenia` | 23/23 PASS, ASan/UBSan |
| `make test-motywy-unit` | 155/155 PASS |
| `make test-motywy-gui` | 25 PASS, 0 FAIL (statyczna integracja; nie runtime GUI) |
| `make test-wm-click-routing` | 1 zestaw PASS, ASan/UBSan (test nie podaje liczbowego licznika assertions) |
| `make test-wm-dekoracje` | 1 zestaw PASS, ASan/UBSan, 100/125% (test nie podaje liczbowego licznika assertions) |
| `make installer-unit-test` | 10343 PASS, 0 FAIL, ASan/UBSan; Storage/BSP2 |
| `make test-bur-loader` | 35 kontroli host loader + 17 kontroli loader/integracji, 0 FAIL |
| `python3 narzedzia/test_windows_launcher_config.py` | 7/7 PASS; polityka GTK Windows / SDL Linux zachowana |
| build Explorer → build WM | oba PASS |
| `make -j2 all iso` | PASS; końcowe ISO zawiera bieżące źródła obu aplikacji |
| `git diff --check` | PASS |

Nowe testy obejmują plik, folder, pusty cel, tekst i BUR; politykę disabled; pusty/COPY/CUT schowek; tworzenie, rename, move, delete cancel/accept; kolizję COPY i CUT; awarię paste z zachowaniem źródła; Unicode, błędny UTF-8 i limit 55 bajtów; stan modelu/edycji/dialogu przy zmianie motywu, także motyw z identycznym kolorem ramki i disabled; geometrię przy 100/125% i krawędziach 640×440; dispatch Notatnika; escape/ANSI i brak click-through. Testy nie zastępują oceny wyglądu lub pełnego runtime w gościu.

### Ramki stosu Ring 3

`-fstack-usage` dla obu zmienionych aplikacji; wartości z finalnych plików `.su`:

| Funkcja | Bajty |
|---|---:|
| Explorer `_start` | 2416 |
| Explorer `AktowkaModel::run_package` | 2032 |
| Explorer `AktowkaModel::load` | 1616 |
| Explorer `AktowkaView::draw_workspace` | 1120 |
| Explorer `AktowkaView::open_context` | 752 |
| Explorer `AktowkaView::open_modal` | 352 |
| WM `_start` | 1360 |
| WM `otworz_pulpit_modal` | 64 |
| WM `odswiez_wpisy_pulpitu` (dotychczasowa funkcja) | 7344 |
| Wspólne `utworz_element` | 544 |
| Wspólne `ZarzadcaPopupow::odrysuj_popupy` | 544 |

Są to statyczne rozmiary pojedynczych ramek (nie pomiar high-water całego stosu w gościu). Duże kontrolki, popupy i model są trwałe/heap-backed, a bufor drop-targetów pozostaje poza stosem. Nie dodano dużych tablic lokalnych. W istniejącym dekoderze PNG WM największa ramka pomocnicza ma 3552 B; dekoder nie był zmieniany.

Artefakty: `BursztynOS.iso` (~39 MiB), końcowe ELF/BIN aplikacji i zwykłe obiekty/cache builda. Nie zmieniono roboczego dysku w testach. Generowany, śledzony w Git binarny `.wm-test/click-routing` został po sprawdzeniu przywrócony do zapisanej wersji; nie jest częścią zmiany źródeł.

## Pliki źródłowe i dokumentacja

- `Makefile`
- `bursztyn_ui/tabela_wirtualna.h`
- `plik_operacje_ui.h`
- `plik_popup_ui.h`
- `programy/eksplorator/eksplorator-plikow.cpp`
- `programy/eksplorator/eksplorator_linker.ld`
- `menedzer_okien.cpp`
- `menedzer_okien_linker.ld`
- `narzedzia/test_kontrolki4.cpp` (wydzielenie istniejących makiet, te same testy)
- `narzedzia/bursztyn_ui_host_fixture.h`
- `narzedzia/file_operations_host_fixture.h`
- `narzedzia/test_explorer_host.cpp`
- `narzedzia/test_desktop_files_host.cpp`
- `docs/explorer_modernizacja_2026-10-04.md`

## Akceptacja runtime

Nie uruchamiano QEMU, `make runuefi`, `make rm` ani `make clean`. Brak deklaracji runtime PASS. Ręczny test użytkownika ma potwierdzić wygląd i proporcje przy 100/125%, zmianę motywu z zaznaczeniem/filtrami/scroll/dialogiem, clipping długich polskich nazw, fokus Enter/Escape, menu Explorera i pulpitu przy krawędziach, brak click-through po zamknięciu menu, COPY/CUT/paste z kolizją/odmową, Notatnik, potwierdzenie usuwania, Start i drag/drop. Dotyczy także mniejszych wspieranych rozdzielczości. Wewnętrzna przyczyna Windows SDL pozostaje poza zakresem; platform policy GTK Windows / SDL Linux pozostaje zachowana.
