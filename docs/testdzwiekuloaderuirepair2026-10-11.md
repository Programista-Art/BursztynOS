Rzeczywiste dane wejściowe z ręcznego uruchomienia `BursztynOS-virtio-input-forensic-diag.iso`: użytkownik potwierdził, że obecna mysz VirtIO działa prawidłowo i płynnie w Windows QEMU. QEMU HDA inicjalizuje się poprawnie. Pakiet „Test dźwięku” instaluje się poprawnie. Uruchomienie aplikacji kończy się **przed wejściem do Ring3**, ponieważ dostarczony log zawiera `LOADER-DBG rsz > MAXLOADERBUF` oraz `LOADER-DBG bspwczytajplikdopamieci fail`.

```text
[HDA] Szukam kontrolera High Definition Audio...
[HDA] Kontroler i kodek QEMU HDA gotowe.
[HDA] Karta HDA gotowa.
[BSP] Test dzwieku zainstalowany jako .cebula.
[LOADER-DBG] rsz > MAXLOADERBUF
[LOADER-DBG] bspwczytajplikdopamieci fail
```

W źródłach odpowiednie identyfikatory mają podkreślenia: `MAX_LOADER_BUF`, `bsp_wczytaj_plik_do_pamieci`. To ten sam warunek odrzucający plik, a nie dowód błędu HDA, BWS 89/90 lub pętli GUI. Audyt rozpoczęto od rozmiaru i segmentów istniejącego pliku, przed zmianą linkera. Prace wykonano 2026-10-12, zachowując żądaną nazwę raportu z datą 2026-10-11.

## Wynik i granica dowodu

Naprawiono **zarówno układ aplikacji, jak i przestarzałą ścieżkę bufora loadera**. Końcowy BUR ma **119 040 B**, zamiast **659 456 B**. Zawiera wyłącznie nagłówek, zaokrąglony do strony TEXT i 256 B inicjalizowanych danych; BSS pozostaje `NOBITS`. Nie zwiększono starej tablicy 512 KiB i nie zmieniono limitów ABI BUR.

Testy wykonują rzeczywiste funkcje PSF, instalację pakietu, walidator BUR, loader, kod wejściowy C++ aplikacji, BursztynUI, usługę czcionki i obsługę zapisu pliku. Instrukcje uprzywilejowane, fizyczne tablice stron oraz sprzęt HDA są zastępowane na hoście. **Nie wykonano `iretq` do CPL3, nie uruchomiono QEMU ani VirtualBox i nie ogłasza się runtime PASS aplikacji.** Ręczna akceptacja pozostaje po stronie użytkownika.

## Rozmiar przed naprawą i przyczyna

Źródło `psf.cpp` definiuje `MAX_LOADER_BUF = 512ULL * 1024ULL`, czyli **524 288 B**. Funkcja `bsp_wczytaj_plik_do_pamieci` odrzucała 659 456 B przed odczytem do globalnego `bufor_wymiany_plikow`. Przekroczenie wynosiło dokładnie **135 168 B**.

Stary dedykowany linker aplikacji rezerwował w pliku:

| Region | Offset w BUR | VA | Bajty w pliku | Bajty w pamięci |
| --- | ---: | ---: | ---: | ---: |
| Nagłówek | `0x0` | `0x1000000` | 4 096 | nagłówek nie jest udostępniany jako USER |
| TEXT + rodata | `0x1000` | `0x1001000` | 393 216 (`0x60000`) | 393 216 |
| DATA, w tym BSS | `0x61000` | `0x1061000` | 262 144 (`0x40000`) | 262 144 |
| Suma | | | **659 456** | **655 360** dla TEXT + DATA |

`readelf -W -S` wykazał `.data PROGBITS`, bez osobnego `.bss NOBITS`. Linker wciągał `*(.bss*)` do sekcji danych i ustawiał jej koniec na stałe 256 KiB. `objcopy -O binary` musiał zatem zapisać te zera. Symbole starego ELF potwierdziły:

- `__bur_text_used_end = 0x1020d70`: 130 416 B użytego TEXT/rodata, **262 800 B** zbędnego dopełnienia TEXT.
- `__bur_data_used_end = 0x1089b40`: 166 720 B danych wraz z BSS, **95 424 B** dodatkowego dopełnienia DATA. Sam BSS również zajmował miejsce w pliku.
- W rodata znajdowało się dodatkowo 37 440 B bitmap systemowej czcionki i 1 170 B szerokości glifów. To była kopia w aplikacji, nie wymaganie diagnostyki HDA.

Stary ELF miał **697 296 B**; jego tablice symboli nie trafiają do instalowanego BUR. Wielkość użytecznej aplikacji nie uzasadniała stałego pliku 644 KiB.

Kopia stanu sprzed zmian: `/tmp/bursztyn-testdzwieku-before-20261012/` — źródła, ELF, BIN, `sizes.json`, `sections.txt` i zastany diff. Rozmiary zmierzono z tych rzeczywistych plików, niezależnie od wcześniejszego raportu.

## Porównanie z działającymi aplikacjami

Poniższe rozmiary zmierzono przed naprawą i sprawdzono po pełnej kompilacji. `.bin` jest dokładnym plikiem instalowanym pod nazwą `.bur`.

| Aplikacja | ELF (B) | BIN/BUR (B) |
| --- | ---: | ---: |
| Kalkulator | 225 824 | 201 088 |
| Notatnik | 249 072 | 200 960 |
| Motywy Systemowe | 232 776 | 200 960 |
| Aktualizacje Systemu | 228 416 | 200 960 |
| Demo BursztynUI | 366 328 | 299 264 |
| Kontrolki 4 | 435 208 | 332 480 |
| Test dźwięku — przed | 697 296 | 659 456 |
| Test dźwięku — po | **163 240** | **119 040** |

Współczesne aplikacje używają tych samych bazowych adresów BUR, inicjalizowanej DATA i zerowania pamięci za końcem pliku. Test dźwięku stosuje tę konwencję bez przenoszenia ich stałych rezerw TEXT do nowego linkera. Wzory `AplikacjaGUI`, własnego `Okno`, standardowych kontrolek i aktualizacji motywu porównano z Motywami Systemowymi, Aktualizacjami Systemu, Kalkulatorem i Kontrolkami 4.

## Układ po naprawie

`programy/test_dzwieku/test_dzwieku_linker.ld` sam emituje nagłówek z końcowych symboli linkera. Usunięto duplikat nagłówka ze źródła C++. Offsety nie są już niezależnymi stałymi, które mogą rozminąć się z sekcjami.

| Region | Offset w BUR | VA | Bajty w pliku | Bajty w pamięci |
| --- | ---: | ---: | ---: | ---: |
| Nagłówek ABI | `0x0` | `0x1000000` | 4 096 | nie jest mapowany jako USER |
| TEXT + rodata | `0x1000` | `0x1001000` | 114 688 (`0x1c000`) | 114 688 |
| Inicjalizowana DATA | `0x1d000` | `0x101d000` | 256 (`0x100`) | 256 |
| `.bss NOBITS` | poza EOF | `0x101d100` | **0** | 598 336 (`0x92140`) |
| Końcowe wyrównanie DATA w pamięci | poza EOF | do `0x10b0000` | **0** | 3 520 |
| Suma | | | **119 040** | **716 800** dla TEXT + DATA |

Nagłówek deklaruje DATA w pamięci jako **602 112 B (`0x93000`)**. Inicjalizowane dane zajmują 256 B; **601 856 B** loader zeruje. Nie jest to ucięty plik: tak działa dotychczasowe ABI v1, w którym długość inicjalizowanych danych wynika z EOF. BUR ma 60-bajtowy nagłówek `BUR\0` z siedmioma polami `uint64_t`; nie posiada osobnego pola wersji ani rozmiaru plikowego DATA. Diagnostyka podaje uczciwie `version=v1-implicit`.

`__bur_text_used_end = 0x101c330` oznacza 111 408 B użytego TEXT/rodata oraz 3 280 B wymaganego wyrównania do strony. Pozostawiono niezbędny nagłówek 4 KiB. Użyto istniejących `-ffunction-sections`, `-fdata-sections`, `--gc-sections` oraz app-specific `-Os`. Nie usunięto funkcjonalności diagnostycznej.

Linker zbiera normalne sekcje kodu, stałych, danych, GOT i BSS. Konstruktorów, TLS, dynamicznego linkowania i relokacji czasu wykonania nie odrzuca po cichu: sekcja zbiorcza musi być pusta (`ASSERT`). `--orphan-handling=error` odrzuca nieoczekiwane sekcje. Usuwane są wyłącznie nieużywane w tym freestanding ABI metadane debug/unwind/note, przy kompilacji bez wyjątków i unwind.

Budżet tego małego narzędzia wynosi **524 288 B**. Końcowy plik pozostawia **405 248 B zapasu (77,294921875% budżetu)**. Linker ma własne asercje, a reguła tworząca blob zawsze uruchamia `narzedzia/test_dzwieku_size.py`; przekroczenie budżetu zatrzymuje build i drukuje faktyczny rozmiar oraz maksimum. Jest to ostrzejszy budżet aplikacji, oddzielny od limitów ABI.

Duże bufory raportu, widgetów i cache fontu są w BSS lub na stercie, a obiekt okna ma jawnego właściciela na stercie. `-fstack-usage` wykazuje największą ramkę app 4 224 B, a ramkę `_start` 736 B. Nie zwiększano standardowego stosu tej aplikacji w celu ukrycia dużych lokalnych obiektów.

## Dlaczego naprawiono również ścieżkę loadera

512 KiB było limitem współdzielonego bufora tymczasowego PSF, **nie limitem formatu wykonywalnego**. Zastane `loader.h` już dopuszczało plik **128 MiB**, TEXT do **64 MiB**, DATA wraz z BSS do **64 MiB**, BSS do **64 MiB**, a sumę mapowanych segmentów do **128 MiB**. Pozostawienie globalnego bufora oznaczałoby rozbieżność kontraktu i możliwość nadpisania danych przez innego czytelnika po zwolnieniu blokady PSF.

Nowe `bsp_wczytaj_obraz_bur` wykonuje pod blokadą PSF:

1. Rozwiązanie ścieżki i kontrolę zwykłego pliku, rozmiaru oraz gotowości PSF.
2. Odczyt najwyżej 60 B nagłówka dla walidacji i pojedynczej diagnostyki.
3. Odrzucenie pliku ponad 134 217 728 B lub niepełnego nagłówka **przed alokacją obrazu**.
4. Pełną walidację nagłówka względem rozmiaru tego samego inode.
5. Prywatną `kmalloc` dokładnie długości pliku; bez zwiększania statycznej tablicy.
6. Odczyt dokładnej liczby bajtów, z kontrolą wyniku i zwolnieniem pamięci przy niepowodzeniu.

Limit jest sprawdzany również względem `UINT32_MAX` i `SIZE_MAX` przed zawężeniem argumentów odczytu. Nagłówek i plik pochodzą z jednej wersji chronionej blokadą; po jej zwolnieniu loader ma własne niezmienne dane. RAII zwalnia je przy sukcesie i przy każdym błędzie. Test przyjmuje poprawny obraz **659 456 B**, większy od dawnego bufora, oraz odrzuca inode długości **134 217 729 B**, bez alokacji takiego obrazu.

Stare `MAX_LOADER_BUF = 524288` i zgodnościowy czytnik pozostają niezmienione dla dotychczasowych klientów PSF. Loader wykonywalny ich nie wywołuje. Nie zmieniono zachowania zwykłych operacji plikowych, formatu BSP, instalatora ani uprawnień PZB.

## Walidacja i mapowanie

Zachowano wszystkie istniejące kontrole walidatora i udostępniono ich dokładne powody przez `bur_blad_naglowka`; `bur_waliduj_naglowek` nadal zwraca bool. Kontrole obejmują:

- minimalny i maksymalny rozmiar pliku, pełny nagłówek i dokładną magię;
- niepusty TEXT, limity TEXT/DATA/BSS i sumy pamięci;
- wyrównanie offsetów, VA i rozmiarów segmentów do 4 KiB;
- offset za nagłówkiem, pełny TEXT w pliku, DATA offset nie większy od EOF;
- dodawanie końców plikowych i wirtualnych bez overflow `uint64_t`;
- obliczenie zera DATA wyłącznie przez ograniczone odejmowanie po sprawdzeniu offsetu;
- brak nachodzenia TEXT/DATA w pliku i w pamięci;
- segmenty w dozwolonym obszarze aplikacji: od `RING3_BASE` 16 MiB do sterty 32 GiB, w dolnej kanonicznej połowie;
- brak kolizji ze stosem, jego guard page i pozostałymi obszarami ABI;
- entry wewnątrz zatwierdzonego TEXT;
- sprawdzenie zakresu stosu i jego końca bez overflow.

Nowe zabezpieczenia samej ścieżki ładowania to walidacja przed alokacją, prywatna migawka pliku, dokładny odczyt, jednoznaczne zwalnianie oraz **sprawdzenie wyniku mapowania strony**. Addytywne `VmmMapujNowaStroneProcesu` zwraca rzeczywisty wynik istniejącej operacji VMM: odrzuca niegotowy VMM, brak CR3, zły lub niewyrównany adres, obszar poza USER i już istniejącą stronę USER. Dopuszcza zastąpienie odziedziczonego supervisor identity mapping, wymagane przez obecny model procesu. Dotychczasowe `void ZmapujStrone` i jego klienci zachowują swoje zachowanie.

Brak ramki lub pamięci na tablice stron kończy próbę bez publikacji procesu. Niezainstalowana ramka wraca do PMM; wcześniej zainstalowane strony i hierarchię usuwa istniejący destruktor przestrzeni procesu. Testy wymuszają awarie przy pierwszym i kolejnym TEXT, DATA oraz stosie i sprawdzają powrót wszystkich zasobów.

Każda ramka jest zerowana przed skopiowaniem jej części plikowej. TEXT otrzymuje USER/PRESENT bez WRITE, DATA i stos USER/PRESENT/WRITE. Aplikacja używa standardowego stosu 16 KiB, z istniejącą guard page. Linker jądra zachowuje limit końca jądra 14 MiB oraz odstęp 2 MiB od `RING3_BASE`. Procesy mają osobne PML4 i ramki; test ładuje drugi proces pod te same VA i dowodzi odrębności fizycznych danych. Nie zmieniono układu sterty, stosu, SHM ani modelu PZB.

## Jeden komunikat błędu i ślad startu

Jedna próba startu emituje najwyżej jeden produkcyjny rekord `[BUR FAIL]`, ograniczony do 639 znaków plus NUL. Zawiera ścieżkę, długość pliku, maksimum, stan magii i nagłówka, wersję implicit v1, offset i długość plikową TEXT/DATA, pamięć DATA, zero-fill, sumę mapowanej pamięci obrazu oraz dokładny powód. Pola uszkodzonych nagłówków są formatowane bez overflow, niedostępne informacje są oznaczone, a ścieżka nie może wstrzyknąć nowej linii. Przy nieprawidłowych parametrach podawany jest powód dotyczący poziomu zaufania, ścieżki lub argumentu.

Przykładowe przyczyny: `file-size-exceeds-maximum`, `truncated-header`, `invalid-magic`, `text-file-end-overflow-or-truncated`, `segments-overlap-in-memory`, `entry-outside-text`, `image-allocation-failed`, `mapuj_segment_z_pliku-kod-failed`. Zwykły start nie przechodzi już przez parę powtarzanych `[LOADER-DBG]` z czytnika bufora.

Tymczasowy, ograniczony ślad jest dostępny wyłącznie przy `BURSZTYNDIAGTESTDZWIEKU=1`. Kernel ogranicza go do kanonicznej ścieżki Testu dźwięku. Pokazuje akceptację BUR, utworzenie procesu, walidację entry, mapowanie stron i publikację `GOTOWY`. Aplikacja pokazuje wejście w `_start`, inicjalizację BursztynUI, utworzenie okna, opublikowanie pierwszej klatki i podłączenie ABI HDA. Pierwsza klatka celowo poprzedza połączenie z HDA.

Ślad jest domyślnie wyłączony. Testy kompilują wariant produkcyjny i diagnostyczny. Produkcyjny BUR nie zawiera prefiksu śladu, a udany start loadera nie emituje logów. Pozostają wyłącznie zwięzłe diagnostyki nieudanego startu.

Deterministyczny test loadera kończy się na **`PROCES_GOTOWY`, RIP `0x1001000`, CS `0x23`, SS `0x1b`, RFLAGS `0x202`, RSP `RING3_STACK_TOP + 4*4096 - 8`**, z 179 stronami USER: 175 obrazu i 4 stosu. Porównuje każdy bajt segmentów i zero-fill z końcowym plikiem. Osobny test wywołuje rzeczywiste źródło `_start` aplikacji na hoście i dochodzi do publikacji klatki oraz ABI HDA. To dowód przygotowania wejścia i działania kodu aplikacji; nie zastępuje sprawdzenia `iretq` oraz schedulera w uruchomionym systemie.

## Pakiet i ścieżki uruchamiania

Projekt ma już ustaloną pisownię z podkreśleniem, zachowaną dokładnie:

```text
/programy/test_dzwieku.cebula/opis.aplikacji
/programy/test_dzwieku.cebula/test_dzwieku.bur
```

`.cebula` jest katalogiem PSF z manifestem i wykonywalnym BUR, nie archiwum z dodatkową kopią aplikacji. Reguły Makefile budują kolejno `test_dzwieku_tmp.o`, `test_dzwieku.elf`, `test_dzwieku.bin`, `test_dzwieku_blob.o`; blob trafia do jądra, a zastany instalator kopiuje go do powyższej ścieżki.

Test generuje harness z **niezmienionych funkcji i rzeczywistego wywołania instalatora w `kernel.cpp`**, instaluje końcowe bajty na rzeczywistym PSF, otwiera pakiet przez ten sam katalog i porównuje odczyt 1:1 z testowanym plikiem. Parsuje również rzeczywisty manifest. Test ścieżek sprawdza dokładny Launcher pulpitu, stałą Start i wywołanie `uruchom_lub_aktywuj`, z uwzględnieniem wielkości liter.

`python3 narzedzia/test_dzwieku_packaging.py --image` dodatkowo odczytuje końcowe ISO bez uruchamiania VM, porównuje jądro staged/ISO z hostowym ELF, wyciąga sekcję danych bloba i jego rzeczywisty zakres `_binary_test_dzwieku_bin_start/end` z segmentu ELF jądra. Wszystkie bajty muszą odpowiadać końcowemu testowanemu BIN. Test potwierdza też brak produkcyjnego śladu startu. Sam katalog `.cebula` powstaje dopiero przez zweryfikowaną instalację PSF podczas bootowania.

Manifest zachowuje poziom zaufania **3** oraz dokładnie `okna`, `pliki_czytaj`, `pliki_zapisz`. To istniejące uprawnienia GUI, diagnostyki 89/90, czcionki i raportu. Nie dodano praw administracyjnych i nie wyłączono żadnego sprawdzania PZB.

## BursztynUI, układ i aktywny motyw

Aplikacja używa `AplikacjaGUI` i normalnego `Okno` 980×620, dostosowywanego przez istniejące API do przestrzeni roboczej. Nie ma własnej pętli starego GUI ani ręcznie rysowanej zastępczej warstwy przycisków. Użyte kontrolki:

- `PanelDokowany`: kompaktowy status i wybór sekcji u góry, główny panel wypełniający i akcje na dole;
- `PasekInformacyjny`: informacja, błąd lub sukces;
- `ListaRozwijana`: Kontroler, Kodeki, Widgety, Ścieżki audio, Testy, Strumień, Raport;
- `ListaWirtualna` z dostawcą wierszy: przewijana diagnostyka i raport;
- `Siatka`, `Panel`, `Etykieta`, `Przycisk`: układ testów i przyciski Odśwież, Zapisz raport, Stop.

`WidokRaportu` jest adapterem danych standardowej listy, nie nowym rendererem UI. Wiersze są indeksowane po zmianie treści/szerokości/skali, zawijane według `CzcionkaUI::postep_znaku` i rysowane tylko w widocznej części. Działają istniejące przewijanie, klawiatura i scrollbar. Nie alokuje się ani nie formatuje całego raportu co klatkę. Bounded `ZegarSkanu` prosi o animację tylko podczas pierwszego połączenia, skanowania lub aktywnego tonu; później normalna pętla GUI czeka na zdarzenie.

Aktywny motyw pochodzi z **tego samego `BursztynMotyw` i BWS 86**, z których korzystają pozostałe aplikacje. `Motyw::zaktualizuj_z_systemu`, `gui_pobierz_motyw` i `motyw_kolor` odczytują wartości utrzymywane przez system na podstawie `/system/konfig/motyw.cfg` i `/system/motywy/*.motyw`. Test dźwięku nie ma własnego pliku motywu ani palety RGB.

| Element | Istniejące role systemowe |
| --- | --- |
| Klient okna, panel | OKNOTLO, PANEL, PODNIESIONY |
| Tekst główny, pomocniczy | TEKST, TEKST_DRUGI |
| Obramowania, fokus | RAMKA, AKCENT |
| Przyciski normalne i hover | PRZYCISK, PRZYCISK_NAJECHANY |
| Przycisk wciśnięty i wyłączony | AKCENT_AKTYWNY; PRZYCISK i WYLACZONY |
| Wybór, lista i scrollbar | ZAZNACZENIE, POLETEKSTOWE, PANEL, RAMKA |
| Błąd i sukces | BLAD, SUKCES |

Zastane `BWS_ZDARZENIE_MOTYW_ZMIENIONY` uruchamia `Okno::zaktualizuj_motyw`, odświeża cache motywu, kontrolki, popupy i układ. App override jedynie odświeża jawnie ustawiony kolor pomocniczej etykiety, tak jak Motywy Systemowe. Test zmienia wartości systemowe przy otwartym oknie, wysyła rzeczywiste zdarzenie i sprawdza aktualne style zamiast starej palety.

Host testuje wszystkie siedem widoków przy **100, 125, 150, 175 i 200%** na 1280×720, granice kontrolek, wysokość widocznego panelu oraz mierzoną szerokość tekstów przycisków. Wygenerowano i obejrzano obrazy klienta z rzeczywistego renderera przy 100 i 200%; są w `.hda-test/repair/client-*.ppm`. To obrazy z testu hostowego, nie zrzuty działającego gościa.

## Czcionka Unicode bez kopii w BUR

Wszystkie zwykłe teksty kontrolek i raportu przechodzą przez `BursztynUI::CzcionkaUI`, istniejący renderer Unicode oraz rzeczywiste proporcjonalne szerokości `extronic16Bunicode.h`. Nie ma aplikacyjnej czcionki ASCII 8×16, mnożenia długości napisu przez 8 ani zastępowania polskich znaków ASCII.

Aby nie umieszczać drugiej kopii bitmap w pliku, dodano operację **BWS 51, arg1=1**: kopiuje niezmienne systemowe szerokości i bitmapy do cache aplikacji w BSS. Dotychczasowa operacja arg1=0 nadal zwraca wysokość 16. Wymagane jest `PRAWO_GUI`, dokładny rozmiar struktury, poprawny zapisywalny zakres i kopiowanie przez bezpieczne API. Nie eksportuje się wskaźnika jądra ani wspólnego zapisywalnego mapowania. Nagłówek wersji cache publikowany jest na końcu, po udanych kopiach danych.

Opt-in `BURSZTYN_UI_CZCIONKA_SYSTEMOWA` dotyczy tej aplikacji; standardowe pomiary, przycinanie i kontrolki pozostają wspólne z BursztynUI. `nm` i przeszukanie końcowego BUR dowodzą nieobecności tablic bitmap i szerokości. Cache ma 38 628 B w pamięci, **zero bajtów fontu w pliku wykonywalnym**.

Sprawdzono poprawne UTF-8 i obecność wymaganych etykiet, między innymi „Test dźwięku”, „Ścieżki audio”, „Odśwież”, „Głośniki”, „Lewy kanał”, „Błąd”, „Skanowanie kodeków”, „Skanowanie widgetów”. Znaki interpunkcyjne spoza 1170 glifów tej czcionki zastąpiono obsługiwaną interpunkcją; polskie litery zachowano. Pomiar, zawijanie i kopiowanie wierszy zachowują granice punktów kodowych UTF-8.

## HDA, błędy i testy tonów

Okno i pierwsza klatka pojawiają się **przed pierwszym BWS 89**. Stan początkowy to „Łączenie z diagnostyką HDA”; następnie wyświetlane są etapy skanowania i wynik. Niepowodzenie połączenia, brak kontrolera, brak odpowiedzi kodeka lub niedostępna migawka pozostawiają otwarte okno z błędem i przyciskiem Odśwież. Zapis raportu także nie kończy aplikacji.

BWS 89 używa wyłącznie istniejących poleceń: STAN, SKAN_START, SKAN_KROK, SNAPSHOT, STRUMIEN i TEST_STAN. App sprawdza wersję/rozmiar migawki, liczniki kontrolerów i ścieżek, długości połączeń widgetów oraz długości ścieżek. Kernel nie czyta drugi raz zmiennego żądania użytkownika po walidacji: używa jednej sprawdzonej kopii, kontroluje overflow adresu, oba wyjściowe zakresy i własną prywatną, zwalnianą migawkę. Nie udostępnia MMIO ani zapisywalnych adresów DMA.

Widoki pokazują PCI BDF, vendor/device kontrolera, GCAP, GCTL, STATESTS, liczbę strumieni, CAD i vendor/device kodeka, AFG, liczbę widgetów, NID/typ/AWCAP, PinCaps/DefCfg z opisem, połączenia oraz odkryte ścieżki. Strumień pokazuje stan, tag, format, rate, bits, kanały, CBL/LVI/LPIB, CTL/STS, BCIS/FIFOE/DESE/RUN i liczbę BDL; adresy pozostają zamaskowane. Brak danych i błąd widgetu mają czytelny opis. Wartości hex pozostają obok opisów.

Nowy test wykonuje rzeczywisty automat skanowania `hda_diag.cpp` i rzeczywiste handlery BWS 89/90 z kontrolowanym transportem odpowiedzi QEMU: kontroler `8086:2668`, kodek `1af4:0022`, AFG, DAC, Pin Complex, połączenie i ścieżka. Sprawdza także brak sprzętu, brak odpowiedzi, niegotową migawkę, błędne wskaźniki, rozmiary, odmowę uprawnień i brak pamięci. Ten model nie jest dowodem rzeczywistego odczytu QEMU MMIO po rozruchu nowego ISO.

Testy dźwięku to **440 Hz, 1000 Hz, Lewy kanał, Prawy kanał, Oba kanały, Krótki sweep i Stop**. Każdy start najpierw zatrzymuje poprzedni test. Domyślny czas wynosi 1500 ms, kernel zachowuje amplitudę 16384 i sweep 200–4000 Hz. Stop jest idempotentny. Nie ma automatycznego odtwarzania po otwarciu aplikacji. Nie dodano surowych verbów, Realtek routing ani Acer-specific NID.

Zastany launcher Windows celowo używa:

```text
-audiodev none,id=snd
-device intel-hda
-device hda-output,audiodev=snd
```

Backend `none` może wyciszać dźwięk hosta, ale pozostawia kontroler i kodek gościa. Aplikacja ma je enumerować również w tym trybie. Odsłuch wymaga później aktywnego backendu audio; nie jest warunkiem uruchomienia i wyświetlenia diagnostyki. Launchera nie zmieniono.

## Zapis raportu

Docelowa ścieżka to dokładnie **`/dane/diagnostyka/hda.txt`**. Używane są normalne BWS 46 (katalog, status mode), 2 (plik, status mode), 3 (zapis) i 47 (metadane). Istniejący katalog musi rzeczywiście być katalogiem. Raport formatuje się do ograniczonego bufora 256 KiB; wykryte przepełnienie powoduje błąd, zamiast zapisu uciętego raportu. Sukces jest pokazywany po udanym zapisie i potwierdzeniu długości pliku.

Błędy pokazują numer BWS i **rzeczywisty kod zwrotny**. Operacje tworzenia zwracają bogatszy `WynikPlikowy`; BWS 3/47 mają zastane ABI bool, więc błąd ma kod 0. Nie wymyślono dokładniejszego errno, którego ABI nie udostępnia. Błąd wewnętrznego limitu raportu ma kod 12. Nie zmieniono filesystem ABI ani reguł PZB.

Test używa rzeczywistych handlerów plikowych wydzielonych bez zmian z `syscalls.cpp` oraz rzeczywistego PSF. Potwierdza tworzenie brakujących katalogów, pełny odczyt zapisanego raportu, odmowę bez WRITE, ochronę pliku PZB, kody błędów każdej operacji, kolizję katalogu z plikiem oraz wykrycie niewłaściwego rozmiaru po zapisie. Raport maksymalnego modelu widgetów ABI miał **141 906 B** i przeszedł zapis/odczyt w całości.

## Testy i sanitizery

Końcowe wyniki: **94 scenariusze C++ / 1 450 995 kontroli nowego zestawu, 63 kontrole statyczne pakietu i GUI, 7 kontroli końcowego obrazu, 2 przypadki bramki rozmiaru oraz 1960 kontroli istniejących zestawów — PASS**. Osobne powtórzenia 35 testów loadera i 155 testów motywu pod sanitizerami nie są ponownie doliczane do 1960.

Nowy zestaw: `make test-dzwieku-repair`. Kompilacja C++: `-fsanitize=address,undefined`, `-fno-omit-frame-pointer`, bez PIE; uruchomienia z `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` oraz `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

| Zestaw | Scenariusze / kontrole | Wynik |
| --- | ---: | --- |
| Loader + prawdziwy PSF + instalacja, produkcja | 30 scenariuszy, 724 204 kontrole | PASS, ASan + UBSan |
| Ten sam loader, ograniczony ślad startu | 30 scenariuszy, 724 209 kontroli | PASS, ASan + UBSan |
| Aplikacja/BursztynUI/font/report/PZB | 28 scenariuszy, 2457 kontroli | PASS, ASan + UBSan |
| Rzeczywiste BWS 89/90 + automat HDA | 6 scenariuszy, 125 kontroli | PASS, ASan + UBSan |
| Statyczny pakiet/ścieżki/UI/UTF-8/font | 63 kontrole | PASS |
| Łańcuch BIN → blob → jądro → końcowe ISO | 7 dodatkowych kontroli (łącznie 70 z `--image`) | PASS |
| Budżet wykonywalny | plik końcowy i plik o 1 B za duży | PASS |
| Istniejący loader BUR | 35 C++ + 17 audytu Python | PASS |
| Istniejąca diagnostyka HDA | 143 | PASS, ASan + UBSan |
| Istniejąca izolacja BWS 89/90 | 148 | PASS, ASan + UBSan |
| Istniejące testy motywu | 155 unit + 25 GUI | PASS |
| BursztynUI polecenia | 23 | PASS, ASan + UBSan |
| Kontrolki 4 | 1379 | PASS, ASan + UBSan |
| Aktualizacje Systemu GUI | 35 | PASS |

Istniejący loader (35) i motyw unit (155) uruchomiono także osobno z ASan i UBSan oraz włączonym wykrywaniem wycieków. Starsze cele HDA/BursztynUI/Kontrolki 4 używają istniejącego `detect_leaks=0`; nowy test rzeczywistej aplikacji i nowe testy loadera/HDA używają `detect_leaks=1`. **ASan: brak zgłoszeń błędów pamięci lub wycieków w nowych zestawach. UBSan: brak zgłoszeń niezdefiniowanego zachowania. Wszystkie uruchomienia zakończone kodem 0.** Liczby kontroli loadera obejmują porównania bajt po bajcie; nie są liczbą osobnych scenariuszy.

Nowe negatywne przypadki obejmują: plik o 1 B ponad limit ABI, plik ponad budżet aplikacji, nagłówek 59 B, invalid magic, overflow/niepoprawny offset segmentu, overflow rozmiaru plikowego, overflow rozmiaru lub adresu wirtualnego, segmenty nakładające się w pliku i VA, entry poza TEXT, zbyt duży BSS, VA jądra, zakres poniżej `RING3_BASE`, kolizje ze stertą/stosem, ucięty TEXT, OOM obrazu/ramek/tablic stron i uszkodzone pola raportu błędu. Wszystkie błędy startu mają jeden bounded failure report; diagnostyczny wariant może wcześniej wypisać ograniczone etapy startu.

Logi: `.hda-test/repair/suite-final.log`, `existing-tests.log`, `existing-loader-run.log`, `theme-run.log`, `production-build.log`, `final-image-check.log`. Po ostatnim buildzie ponownie wykonano istniejący cel loadera i wszystkie cztery nowe binaria hostowe na końcowym pliku aplikacji: `final-existing-bur.log`, `final-loader-0.log`, `final-loader-1.log`, `final-gui.log`, `final-hda-bws.log`; dodatkowe powtórzenia sanitizerów to `final-existing-loader.log` i `final-theme.log`. Testy nie uruchamiają VM.

## Pliki zmienione w tym zadaniu

Zastany worktree zawierał wiele wcześniejszych zmian. Poniższa lista dotyczy tej naprawy, a nie całego `git diff` od HEAD.

Produkcja / integracja systemowa:

- `Makefile` — font kernel object, linker/size gate, test target, domyślnie wyłączony trace;
- `loader.cpp`, `loader.h` — prywatny obraz, kontrola mapowania, dokładne przyczyny i pojedyncza diagnostyka;
- `psf.cpp`, `psf.h` — nowy ograniczony czytnik obrazu BUR, bez zmiany starych operacji;
- `pamiec.h`, `vmm.cpp` — addytywny checked mapper loadera;
- `bws_nowoczesne.cpp` — pojedyncza walidowana kopia żądania SNAPSHOT, overflow i prywatny bufor;
- `syscalls.cpp` — delegacja BWS 51 do zgodnej usługi fontu;
- `bws_czcionka.h`, `czcionka_systemowa.cpp` — kopiowanie systemowej czcionki do BSS aplikacji;
- `bursztyn_ui/czcionka.h` — opt-in wspólnego renderera dla fontu pobieranego z systemu;
- `test_dzwieku_trace.h` — domyślnie wyłączony bounded trace.

Aplikacja:

- `programy/test_dzwieku/test_dzwieku.cpp`;
- `programy/test_dzwieku/test_dzwieku_linker.ld`.

Testy:

- `narzedzia/test_dzwieku_size.py`;
- `narzedzia/test_dzwieku_packaging.py`;
- `narzedzia/test_dzwieku_repair.py`;
- `narzedzia/test_dzwieku_loader.cpp`;
- `narzedzia/test_dzwieku_gui.cpp`;
- `narzedzia/test_dzwieku_hda_bws.cpp`;
- `narzedzia/bursztyn_ui_host_fixture.h` — opcjonalny observer oczekiwania dla testu rzeczywistego entry, domyślnie null.

Dokumentacja: niniejszy raport. Pliki `.o`, `.d`, `.su`, ELF/BIN i ISO są wynikami kompilacji.

Nie zmieniono implementacji VirtIO Input, InputDevice, PS2, USB HID, xHCI, współistnienia kursorów, launchera QEMU, napraw myszy Acer, timingu splash, sieci ani instalatora. Kontrola SHA-256 **16 chronionych plików źródłowych** względem początku zadania potwierdza ich niezmienność. Nowy odczyt obrazu jest osobnym API PSF; zwykłe zachowanie filesystemu i jego kontrole pozostają zachowane.

## Obraz produkcyjny

Wykonano dokładnie `make -B -j2 all iso`, bez `make clean`, `make rm` i bez startu VM. **Kod zakończenia 0; poprawny Multiboot2 i gotowe ISO 35 635 200 B.** Brak ostrzeżeń lub błędów kompilatora. Zastany generator tapet zgłasza pominięcie niebezpiecznej nazwy `Zamek w Pszczynie 2026r..jpg`; nazwy i polityki pakowania tapet nie zmieniano.

Końcowy BUR SHA-256:

```text
dd89ff37ce62a0a7b5a2de83909a3fc80757610b1a5394cfbe8de00e906d464f  test_dzwieku.bin
```

Końcowy `BursztynOS.iso` SHA-256:

```text
d5a37d00ce12f51fec1fbc099613a95af921ddaf38f5603f2fadc12c0af29003  BursztynOS.iso
```

**Potwierdzono zgodność końcowego BIN, sekcji danych `test_dzwieku_blob.o`, rzeczywistego bloba instalacyjnego w `system_operacyjny.bin` oraz jądra wyodrębnionego z `/boot/system_operacyjny.bin` w końcowym ISO.** Produkcyjny kernel i BUR nie zawierają prefiksu tymczasowego śladu Testu dźwięku. Ponowne uruchomienie testów ASan/UBSan na tych końcowych bajtach dało te same wyniki.

## Ręczna akceptacja użytkownika — pozostaje do wykonania

- Windows QEMU nadal ma działającą płynną mysz VirtIO; pulpit startuje normalnie.
- Test dźwięku otwiera się z pulpitu i ze Start; nie pojawia się `LOADER-DBG rsz > MAXLOADERBUF` ani wariant z podkreśleniami.
- Powstaje widoczne okno z aktualnym systemowym motywem; zmiana motywu aktualizuje je zgodnie z systemowym zdarzeniem.
- Tekst używa proporcjonalnej systemowej czcionki Unicode, polskie znaki są poprawne, UI mieści się w 1280×720.
- Aplikacja wykrywa QEMU Intel HDA, pokazuje kontroler i kodek oraz przewijaną diagnostykę.
- Działają Odśwież i Zapisz raport; powstaje `/dane/diagnostyka/hda.txt`.
- Przyciski tonów nie powodują awarii; Stop działa. Przy backendzie `none` brak odsłuchu hosta jest spodziewany.
- Błąd diagnostyki HDA pozostawia otwarte okno z czytelnym komunikatem.

**Runtime aplikacji: NIEZWERYFIKOWANY w nowym ISO.** Dotychczasowe ręczne potwierdzenie dotyczy działającej myszy w bazowym worktree; testy tej naprawy są deterministyczne i hostowe.
