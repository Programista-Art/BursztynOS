# Shell: zmiany i granice weryfikacji — 2026-10-03

Zmiany wykonano w bieżącym worktree. Zachowano parser, publiczne polecenia, historię,
model pierścienia scrollbacku, BursztynUI, extronic16Bunicode, motyw, standardową
belkę oraz istniejącą obsługę przeciągania i zamykania okna.

**Stan: implementacja i testy hostowe ukończone; zachowania poprawionej wersji nie
zostały potwierdzone w uruchomionym Bursztyn OS. Nie należy traktować kompilacji ani
testów z symulowanym BWS jako sukcesu runtime.**

## Dokładne pliki źródłowe

- `shell.cpp`: zdarzenia, edycja UTF-8, historia, odrzucanie pustego wejścia,
  polecenie `system`, zakończenie pomocy, inicjalizacja scrollbacku.
- `shell_terminal.h`: prywatne funkcje istniejącego terminala wyodrębnione z
  `shell.cpp`, geometria, mierzone zawijanie, renderowanie, zaznaczanie, schowek.
- `Makefile`: zależności `shell_tmp.o` obejmujące nowy nagłówek i używane API UI.
- `narzedzia/test_shell_terminal.cpp`: testy rzeczywistego kodu Shell z symulowanymi
  zdarzeniami i powierzchnią BWS.
- `narzedzia/shell_runtime_probe.py`: sterowanie pojedynczą sesją przez QMP;
  narzędzie nie uruchamia QEMU ani nie zmienia jego urządzeń.
- `docs/shell_runtime_regressions_2026-10-03.md`: ten raport.

Normalne buildy pozostawiły również artefakty kompilacji, `shell.bin`, `shell.elf`,
`system_operacyjny.bin`, `BursztynOS.iso` i katalog stagingu ISO. Normalny
`make runuefi` utworzył brakujący `wirtualny_dysk.img`.

Nie zmieniano SDL, VirtIO-GPU, timeoutów, virtio-tablet-pci, obsługi restartu,
schedulera, BSP2, instalatora, GRUB ani ABI BUR. Nie włączano legacy GUI i nie
uruchamiano `make rm`. Przebudowanie ich obiektów przez `make all` nie oznacza
zmian w ich źródłach.

## Przyczyny ustalone w źródłach

| Problem | Ustalenie audytu | Wprowadzona zmiana |
|---|---|---|
| Wejście pod paskiem | Obie ścieżki wejścia wysyłały cały bufor do `gui_wypisz_tekst_kolor_skala`. Ten syscall nie przyjmuje klipu kontekstu. Brakowało klipu edytora i poziomego offsetu. | Osobny klip wyznaczony z klienta i prostokąta paska; pomiar tekstu i przewijanie poziome. |
| Śmieci po strzałkach | `klawiatura.cpp` wysyła fizyczny klawisz oraz `ESC [ A/B/C/D`. Shell obsługiwał fizycznie tylko klawisze przewijania; bajtowy odbiorca oddawał każdą składową ANSI jako tekst. | Ruch i historia z kodów fizycznych; automat konsumuje następujący strumień ANSI. |
| Brak ruchu kursora i edycji w środku | `pozycja` była jednocześnie długością wejścia i miejscem dopisywania. Backspace usuwał tylko koniec bufora. | Oddzielny kursor, wstawianie i usuwanie zakresów na granicach UTF-8. |
| Brak zaznaczania | Mysz obsługiwała wyłącznie belkę i pasek; nie było punktów zaznaczenia ani hit-testu znaków. | Hit-test przez metryki czcionki, capture myszy, logiczne zakresy i kolor zaznaczenia motywu. |
| Puste prompty | `pobierz_linie` wypisywało prompt, tekst i LF przed sprawdzeniem pustej komendy w `_start`. | Sprawdzenie białych znaków następuje przed jakimkolwiek echo, historią i wykonaniem. |
| Pomoc zlewa się z kolejnym poleceniem | Pomoc już tworzyła logiczne wiersze, lecz ostatni pozostawał otwarty. Następne `wypisz("powłoka> ")` dopisywało do niego tekst. Długa komenda mogła też przesuwać kolumnę opisu indywidualnie. | Każdy wiersz pomocy jest zamknięty; na końcu jest pusty separator. Stała mierzona kolumna i osobny wiersz opisu dla zbyt długiej komendy. |
| Zlane pola `system` | Literały autora i e-maila nie kończyły się LF, więc kompilator łączył je z następnymi polami w jednym ciągu. Samo `shell_wypisz` już obsługiwało LF. | Dodano brakujące LF; zachowano dzielenie każdego LF na nowy `ShellLine`. Poprawiono `Jądro`, `Plików`, `Sieć`. |
| Długie wyjście przy pasku | Podział następował po około 150 bajtach, bez pomiaru szerokości. Tekst i opisy omijały klip UI. | Większe buforowane linie, zawijanie po mierzonych kodach Unicode, wspólny model wierszy wizualnych i klip przed paskiem. |
| Zaznaczanie i przewijanie | W starej wersji zaznaczania nie było, więc nie istniał mechanizm unieważniania jego współrzędnych. | Przewijanie bezpiecznie czyści zaznaczenie wyjścia i odmalowuje widok; przeciąganie przy krawędzi tylko ogranicza hit-test, bez auto-scroll. |
| Niespójność odmalowania | Duże odmalowanie najpierw czyściło żywą warstwę, następnie wykonywało wiele syscalli tekstowych. Klip kontekstu nie ograniczał tych syscalli. | Czyszczenie i tekst powstają w prywatnym buforze; gotowy klient lub sam wiersz wejścia jest przekazywany BWS57. |

### Znikanie treści przy szybkim przewijaniu: granica ustalenia przyczyny

Audyt nie wykazał nadpisywania pending dirty rect: `grafika.cpp` sumuje prostokąty.
Stary kod również ograniczał offset po zdarzeniach paska i kółka. Nie ma podstaw,
aby podać utratę dirty rect albo ujemny offset jako ustaloną przyczynę.

Istniała natomiast ścieżka pokazania wyczyszczonej warstwy między osobnymi
syscallami czyszczenia i tekstu: blokada chroniła pojedyncze wywołanie, nie cały
render Shell, a warstwa była bezpośrednio modyfikowana. Ciągłe zdarzenia ponawiały
ten kosztowny cykl. Nowa ścieżka przekazuje kompletne piksele i nie publikuje
oddzielnego czyszczenia. Dla 1280×720 i 1920×1080 przekazanie klienta mieści się
w jednym BWS57. Dla większych prostokątów istnieje ścieżka gotowych kafelków
wierszy respektująca limit wymiaru 2048.

**Nie odtworzono ani nie wyizolowano całego zgłoszonego zanikania w runtime.
Powyższy mechanizm jest ustaleniem audytu źródeł, a nie dowodem, że wyjaśnia każdą
obserwację użytkownika. Ten punkt pozostaje do potwierdzenia w OS.**

## Mechanika edytora i widoku

`g_caret` i `g_input_anchor` są offsetami bajtowymi zawsze na granicy kompletnego
kodu UTF-8. Lewo cofa do bajtu wiodącego; prawo korzysta z
`CzcionkaUI::dekoduj_utf8`. Wstawienie przesuwa ogon razem z NUL, Backspace usuwa
poprzedni kod, Delete następny. Istniejący limit wejścia 2047 bajtów pozostaje;
poziome przewijanie niczego w buforze nie obcina.

Prostokąt wejścia kończy się na lewej krawędzi rzeczywistego prostokąta paska.
Prompt ma własne miejsce, a edytowalny tekst własny klip. `g_input_scroll_px`
przechowuje przesunięcie w pikselach. Szerokość prefiksu do kursora i całego tekstu
pochodzi z CzcionkaUI. Offset przesuwa się tylko, gdy kursor opuszcza widoczny
obszar, i jest ograniczany po skróceniu tekstu. Rysowane są glify przecinające
widoczny obszar; kursor również podlega temu klipowi.

Up przechodzi od najnowszej do starszych pozycji istniejącej pięcioelementowej
historii. Pierwsze Up zachowuje roboczy tekst; Down wraca ku nowszym wpisom, a
po przejściu poza najnowszy przywraca ten tekst. Polecenie `historia` pozostaje.
Lewo/Prawo przy zaznaczeniu zwijają je odpowiednio do początku/końca.

Zaznaczenie wyjścia to dwa punkty `(logiczna linia, offset UTF-8)`, niezależne od
pikseli. Mapa widocznych wierszy wskazuje zakresy tej samej linii również po
zawinięciu. Kolumny pomocy są reprezentowane do kopiowania przez tabulator;
rysowanie wykorzystuje jego mierzony offset kolumny. Wcięcia pomocy są osobną
metryką `tekst_x`; spacje z deklaracji pomocy nie służą do rysowania kolumn.
Nowe wyjście, usunięcie linii i przewijanie unieważniają zaznaczenie bez pozostawiania
starych pikseli. Zaznaczenie wejścia używa kursora i kotwicy; tekst, Backspace i
Delete zastępują/usuwają wybrany zakres.

Schowek istnieje: `clipboard_set("text/plain", ..., length + 1)` jest również
konwencją `PoleTekstowe`. Ctrl+C przekazuje wybrane wejście lub wyjście przez to
API. Następująca bajtowa litera `c` z klawiatury przy Ctrl jest odrzucana. Nie
utworzono osobnego schowka. Test hostowy sprawdza routing i tekst; rzeczywisty
schowek jądra nie został przetestowany w tej sesji.

`g_scroll_linia` jest jedynym offsetem widoku, teraz w jednostkach **wierszy
wizualnych**. Wszystkie źródła przewijania korzystają z `shell_set_scroll` i tego
samego zakresu `max(0, suma wierszy wizualnych − liczba widocznych wierszy)`.
Pasek otrzymuje ten zakres, rozmiar strony i wartość; podczas drag jego wartość
jest od razu odczytywana, ograniczana i wykorzystywana przez renderer. Render i
licznik wierszy wykonują ten sam algorytm pomiaru i zawijania.

Scrollback nadal ma 512 logicznych linii, z buforem do 4095 bajtów na linię na
istniejącej stercie BWS. Przekroczenie tego limitu tworzy następną linię, bez
dzielenia UTF-8. Zawijanie według szerokości nie zmienia komendy ani historii i
nie zużywa dodatkowych slotów scrollbacku. Przy wyrzucaniu najstarszej linii offset
czytelnika jest pomniejszany o jej liczbę wierszy wizualnych.

Nowe wyjście przewija do końca tylko przy wcześniej aktywnym auto-scroll.
Samo rozpoczęcie czytania kolejnego wejścia nie włącza go ponownie. Zatwierdzenie
niepustej komendy może wrócić na dół. Puste lub złożone ze spacji/tabulatorów
Enter czyści bieżące wejście i pozostawia jeden prompt, bez historii i echo.

Pisanie, edycja i zaznaczanie wejścia przekazują wyłącznie wyczyszczony wiersz
wejścia, z pominięciem obszaru paska. Zmiany wyjścia i przewijanie przekazują
kompletnego klienta, bez belki WM. Belka jest rysowana przy zmianie geometrii lub
skali. Liczba wierszy jest cache'owana; zwykłe klawisze nie przeliczają całego
scrollbacku. Geometria i metryki używają bieżącej skali procentowej.

## Buildy i testy hostowe

Kolejność: `make shell_blob.o`, testy Shell, następnie `make -j4 all iso`.
Po korekcie stosu powtórzono build i testy. Finalny Shell kompiluje się i linkuje
bez ostrzeżeń z jego nowych funkcji. Normalny system i ISO zostały przebudowane.

Test jest uruchamialny poleceniami:

```sh
g++ -std=c++17 -O1 -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_shell_terminal.cpp -Wl,--gc-sections -o /tmp/test_shell_terminal_sanitized
ASAN_OPTIONS=detect_leaks=0 /tmp/test_shell_terminal_sanitized
```

Sprawdza produkcyjny kod: puste Enter, strzałki i towarzyszące ANSI, polską edycję
w środku, Delete, historię i przywracanie roboczego wejścia, zaznaczenie myszą,
Ctrl+C bez dopisania litery, zastępowanie zaznaczenia, osobne pola `system`,
poziomy viewport, kurczenie offsetu po skróceniu tekstu, klip, zawijanie długiej
linii bez zużywania slotów, limit 512, synchronizację paska, skrajne offsety,
brak wymuszonego powrotu na dół oraz logiczne zamknięcie pomocy. Testy obejmują
100% i 125%; symulują BWS, nie uruchamiają Bursztyn OS.

## Rzeczywista sesja runtime

Uruchomiono **jeden** normalny `make runuefi` i **jeden proces QEMU**, z normalnym
SDL, VirtIO i dodatkowym QMP oraz logiem serial. Nie uruchomiono drugiej sesji.

Potwierdzono start pulpitu 1280×720 przy 100%. Pierwsza próba otwarcia Shell
ujawniła regresję w mojej implementacji: zagnieżdżone lokalne bufory układu
przekroczyły 16 KiB stosu Ring 3. Log wskazuje #PF w `/shell.bur`, RIP
`0x100DA18`, CR2 `0x7FFFEFFFFFD8`; disassembly wskazuje `shell_layout_rows`.
Bufory robocze przeniesiono do pamięci statycznej. Audyt stosu po korekcie
pokazał 208 bajtów dla układu i 832 bajty dla renderera, zamiast ponad 8 KiB
samej ramki układu. Testy ASan/UBSan nie wykazały błędów.

W tym samym procesie QEMU przeładowano ISO i wykonano reset maszyny przez QMP,
aby odzyskać sesję po zatrzymaniu gościa. Próba nie dotarła ponownie do jądra:
ekran pozostał czarny po GRUB, a rejestry pokazywały wykonanie w wysokim obszarze
firmware/bootloadera, przed startem Bursztyna. Nie diagnozowano tego przez zmiany
w zakazanych komponentach. Sesję zakończono przez QMP.

**Nie potwierdzono w działającym OS:** poziomego przewijania wejścia, nawigacji
UTF-8 i historii, braku śmieci ANSI, pustego Enter, wyglądu pomocy i `system`,
wolnego/szybkiego kółka i drag paska, zaznaczania, rzeczywistego kopiowania,
przeciągania/zamykania poprawionej wersji ani 1920×1080 przy 125%.

Dowody sesji pozostają w `/tmp/bursztyn-shell-current-serial.log` i plikach
`/tmp/bursztyn-shell-current-*.png`. Zrzuty nazwane `opened-corrected` oraz
`caret-middle-corrected` przedstawiają nieudany powrót do systemu, **nie**
potwierdzone działanie edytora. Weryfikacja finalnego obrazu w OS pozostaje
konieczna przed nazwaniem zgłoszonych zachowań naprawionymi.
