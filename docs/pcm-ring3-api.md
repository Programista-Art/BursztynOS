# Natywne PCM aplikacji Ring 3

BWS 110 udostępnia mikser z istniejącego sterownika HDA. Rejestr BWS ma teraz pierwszy wolny numer 111. BWS 89/90 i 107–109 zachowują swoje numery. Definicje ABI znajdują się w `jadro/bws/pcm_api.h`. Aplikacja nie potrzebuje uprawnień administracyjnych. Demonstracje mają PZB 4 i wyłącznie prawo GUI.

Wywołanie ma postać `bws_wywolaj(110, operacja, a, b, c)`. Wynik należy interpretować jako `int64_t`. Operacje mają numery od 1 do 6.

| Operacja | a | b | c | Sukces |
| --- | --- | --- | --- | --- |
| OPEN (1) | wskaźnik BwsPcmFormat | 24 | wersja 1 | dodatni uchwyt |
| WRITE (2) | uchwyt | wskaźnik PCM16 | liczba ramek 1–256 | rzeczywista liczba przyjętych ramek |
| QUERY (3) | uchwyt | wskaźnik BwsPcmState | 32 | 0 |
| GAIN (4) | uchwyt | głośność 0–100 | mute 0 lub 1 | 0 |
| CLOSE (5) | uchwyt | 0 | 0 | 0 |
| FINISH (6) | uchwyt | 0 | 0 | 0 |

GAIN ustawia jednocześnie poziom i mute strumienia. QUERY zwraca oba pola. FINISH oznacza naturalny koniec produkcji i pozwala opróżnić kolejkę bez naliczania głodzenia. Kolejny przyjęty WRITE wznawia oczekiwanie próbek. Aplikacja może sprawdzać QUERY do queued=0, oddając CPU między próbami. CLOSE usuwa oczekującą kolejkę. Opublikowany sygnał może jeszcze wybrzmieć z okresów DMA.

BwsPcmFormat zawiera sześć kolejnych uint32_t, w porządku version, size, rate, channels, encoding, reserved. Wersja wynosi 1, size 24, rate 48000, channels 1 lub 2, encoding 1 (signed PCM16 LE), reserved 0. Mono jest powielane do stereo przez obecny mikser. Inne częstotliwości lub formaty zwracają UNSUPPORTED.

BwsPcmState zawiera osiem uint32_t, w porządku version, size, queued, capacity, volume, muted, channels, rate. Ma 32 bajty. Struktury nie zawierają wskaźników i mają sprawdzane rozmiary i przesunięcia. Nagłówek nadaje się do C i C++.

Wyniki błędów to INVALID −1, UNSUPPORTED −2, LIMIT −3, HANDLE −4, FULL −5, BUSY −6, DEVICE −7, MEMORY −8. BUSY pozwala spróbować przy następnym zdarzeniu lub ticku. FULL oznacza zero przyjętych ramek. Dodatni wynik WRITE może być mniejszy od żądania. Należy przesunąć wskaźnik tylko o przyjęty prefiks. Przekroczenie 256 ramek jest błędem, także dla UINT64_MAX.

## Własność i synchronizacja

Dispatcher ustala PID i generację z bieżącego procesu i schedulera. ABI nie przyjmuje tożsamości właściciela. Mikser weryfikuje parę PID/generacja przy każdej operacji oraz uchwyt, który nie jest ponownie przydzielany. Licznik uchwytów zatrzymuje się przy INT64_MAX, aby zachować dodatni zakres wyników OPEN. Limit wynosi osiem strumieni globalnie, dwa na właściciela i 4096 ramek stereo na strumień.

Najpierw QUERY sprawdza właściciela i pobiera liczbę kanałów pod try-lockiem HDA. Potem zamek zostaje zwolniony. Istniejące copy_from_user waliduje cały zakres, wszystkie strony PTE oraz USER/PRESENT i kopiuje do lokalnego bufora maksymalnie 1024 bajtów. Odejmowanie od granicy przestrzeni użytkownika i jawna kontrola przepełnienia adresu chronią zakres. Dopiero pełna udana kopia pozwala wejść pod zamek i zapisać prefiks do kolejki. Nieudana kopia nie publikuje żadnej próbki. QUERY kopiuje lokalny snapshot po zwolnieniu zamka. Nie ma asynchronicznych odwołań do pamięci procesu ani DMA z jego stron.

Ten sam GuardHDA chroni operacje aplikacji, miksowanie, sterowanie masterem i kolejki. Kod nie czeka na pamięć użytkownika z tym zamkiem. Operacja aplikacji nie wyłącza IRQ na czas kopii. Przetwarzanie ma ograniczoną porcję, a brak zamka daje BUSY.

Odroczone sprzątanie procesu wywołuje pcm_posprzataj_proces przed odzyskaniem pamięci i udostępnieniem slotu PID. Hook pod tym samym try-lockiem usuwa dokładnie generację kończącego procesu. Jeśli zamek jest zajęty, sprzątanie pozostaje do ponowienia i PID nie jest ponownie używany. Bufory miksera są statyczne. Nie ma zwalniania stron, które mógłby nadal czytać worker. Opublikowane okresy DMA pozostają własnością sterownika do zwykłego wybrzmienia lub bezpiecznego STOP.

## Demonstracje Live ISO

Pakiety `/programy/pcm440.cebula` i `/programy/pcm660.cebula` można uruchomić niezależnie z Eksploratora. Każdy ma osobny proces i generuje w pamięci Ring 3 trójkątny sygnał PCM16, odpowiednio 440 lub 660 Hz. Przyciski Play/Stop, 100%/25% i Mute/Unmute działają tylko na jego uchwycie. Zasobnik reguluje wspólny master. Menedżer zadań może zakończyć producenta. Kolejny producent powinien pozostać słyszalny.

Scenariusz `bursztyn.test=hda-ring3` uruchamia oba pakiety po gotowości pulpitu. Aplikacje dostają wspólny czas początkowy jako argument. A ustawia 25% po 4 s, mute od 6 do 8 s. Master jest wyciszony od 10 do 12 s. Po 14 s jądro używa scheduler_zazadaj_zakonczenia, tej samej ścieżki co żądanie zakończenia procesu. B zamyka swój strumień po 17 s. Raport po 18 s zawiera liczniki HDA. Żaden producent scenariusza nie generuje PCM w jądrze.

Harness `testy/test_pcm_ring3_qemu.py --out NOWY_KATALOG` uruchamia jedną maszynę bez dysków danych. Tryb `--analyze` odczytuje istniejące artefakty. Oprócz testu sygnału raportuje ciągłość. Przerwy czasowe i odzyskiwanie DMA znane z wcześniejszych testów nadal ograniczają odtwarzanie.

## Weryfikacja

Sekwencyjne testy hosta zaliczyły mikser PCM, rzeczywisty sterownik HDA i warstwę poleceń PCM (14 scenariuszy, 296 sprawdzeń), transport i topologię HDA (49 scenariuszy, 11 287 sprawdzeń), diagnostykę HDA (241), izolację BWS (181), zakończenie procesów (4063), Zasobnik (1468), Kalendarz (16 102), Menedżer zadań (1802), oba loadery Testu dźwięku, GUI (2662) i BWS 89/90 (2156). Testy obejmują także częściowy zapis do prawie pełnej kolejki i odmowę publikacji po częściowo nieudanej kopii. Kopiowanie spoza dostępnego zakresu jest modelowane przez callback hostowy, a demonstracje mają dodatkowe sprawdzenie błędnego adresu w rzeczywistym Ring 3. Sanitizery ASan i UBSan nie zgłosiły błędów w zaliczonych uruchomieniach. Nagłówek ABI przeszedł też sprawdzenie składni C.

Pierwszy rozruch QEMU zakończył się przed uruchomieniem producenta 440 Hz. Brak katalogów pcm440.cebula i pcm660.cebula w tablicy bootstrapu PSF uniemożliwił zapis pakietów. WAV ma zero ramek. Katalogi zostały dopisane, a błędy instalacji nowych pakietów trafiają do istniejącej kontroli startu. Obie konfiguracje testu rzeczywistego PSF/bootstrap/Live przeszły z ASan i UBSan (21 scenariuszy, 25 187 620 oraz 25 187 285 sprawdzeń). Poprawione ISO przeszło ponowną kontrolę Multiboot2, kontrolę fontów i audyt wydania z zerem ustaleń.

ELF produkcyjnego ISO ma 8 490 736 bajtów, plik z symbolami 13 206 304 bajty, a wyrównany zakres pamięci od 0x100000 wynosi 11 808 768 bajtów. Adres końcowy 0xC43000 mieści się poniżej 0xF00000. Logi hosta i sumy SHA-256 są w .hda-test/pcm-ring3-host-20261120/. Artefakty pierwszego rozruchu znajdują się w .hda-test/pcm-ring3-20261120/.

Po pierwszej próbie odtwarzanie dwóch aplikacji Ring 3 wymagało potwierdzenia na poprawionym ISO. Wynik kolejnej, osobno autoryzowanej sesji opisano poniżej. Wcześniejszy wynik 6 przerw i 149,333 ms dotyczył producentów wewnętrznych.

## Pliki tej zmiany

Nowe pliki obejmują jadro/bws/pcm_api.h, jadro/bws/pcm_jadro_api.h, jadro/dzwiek/pcm_aplikacji.h, programy/pcm_demo/pcm_demo.cpp, sterowniki/dzwiek/hda_ring3_regression.h, testy/test_pcm_ring3_qemu.py i niniejszy dokument.

Zmienione pliki obejmują Makefile, jadro/bws/bws_api.h, jadro/bws/syscalls.cpp, jadro/bws/scheduler.cpp, jadro/dzwiek/mikser_pcm.h, jadro/dzwiek/pcm_jadro.h, jadro/fs/psf_bootstrap.h, jadro/rdzen/kernel.cpp, sterowniki/dzwiek/hda.cpp, testy/test_hda_tone.cpp, testy/test_hda_bws_isolation.cpp, testy/test_hda_pcm_qemu.py, testy/test_menedzer_zadan_host.cpp, testy/test_zasobnik_host.cpp i docs/bursztynowy-system-dzwieku-hda.md.

## Natywna weryfikacja poprawionego ISO

Wykonano jedną dodatkową sesję QEMU, osobno autoryzowaną po błędzie pakowania. Artefakty są w .hda-test/pcm-ring3-native-20261120/, a kontrola obrazu w .hda-test/pcm-ring3-verification-host/preflight.json. ISO miało SHA-256 14ce57f2b33ebd5190937d4fd9539bf2b4c82eddcc50eb2a0c772c72c13c7c44. Wydobyte z ISO jądro miało SHA-256 02f37821be3108ae33cad5e181ee5b4e9eb8f74657a6b55b599e487f4ab61939.

Przed rozruchem odczytano segmenty ELF jądra z ISO i odzyskano oba osadzone bloby BZB1. Sprawdzono CRC, długości oraz zgodność zdekompresowanych plików z plikami BUR. Każdy ma 266 520 bajtów i format BUR v2. SHA-256 pcm440.bur wynosi df972b0b657816d277c76d533242803bbaba6c13f30df7d710e2649c6831595b, a pcm660.bur 29432d68cfd8ec304f677ba81c888275827b6092f72e2e9662095f18c26a3e07. Ścieżki katalogów bootstrapu, manifesty i ścieżki wykonywalne są w tym samym obrazie. Instalator rozruchowy tworzy pakiety w PSF. Zawartość programów jest osadzona w jądrze ISO.

Pakiety uruchomił zwykły loader plików z argumentem startowym. Manifesty ustawiają PZB 4 i prawo GUI. Polityka loadera dla pakietów instalowanych z medium w /programy pozwala na uruchomienie z tej lokalizacji. Nie użyto ścieżki żądającej uprzywilejowanej tożsamości systemowej. Log potwierdził PID 3 dla pcm440 oraz PID 4 dla pcm660. Oba procesy były widoczne w Menedżerze zadań. Brak komunikatów błędu zapisu pakietów, BUR FAIL, panic lub naruszenia stosu w tej sesji.

### Wyniki BWS 110

Licznik PcmAplikacji zarejestrował dwa udane OPEN. Istnienie sygnału obu aplikacji potwierdza przyjmowanie próbek przez WRITE. Każda aplikacja dopuszcza produkcję dopiero po sprawdzeniu MEMORY (−8) dla zapisu z niezmapowanego adresu 0x70000ffe, INVALID (−1) dla UINT64_MAX ramek oraz udanego QUERY (0) z pustą kolejką. Native nie loguje oddzielnie każdego wyniku syscalla. Nie zebrano dokładnej liczby przyjętych ramek ani rozkładu FULL/BUSY i poziomów kolejek na proces.

Zmiany GAIN wykonano z kodu Ring 3. Przywrócenie mastera wykonał scenariusz jądra przez istniejący interfejs HDA, wspólny z Zasobnikiem. Zrzut panelu pokazał backend wzmacniacza kodeka, 100% oraz zmieniający się stan mute. Nie wykonywano pomiaru przy innym ustawieniu suwaka mastera. Po 14 s scenariusz wywołał scheduler_zazadaj_zakonczenia dla PID 3, tę samą ścieżkę żądania zakończenia procesu, z której korzysta Menedżer zadań. Licznik sprzątania potwierdził usunięcie jednego strumienia. Nie symulowano kliknięcia przycisku zakończenia w GUI. Po 17 s aplikacja B przestała produkować i wywołała CLOSE. Po ostatnim sygnale nagrano 126,313 ms ciszy.

### Pomiary WAV

Nagranie ma 774 296 ramek PCM16 LE stereo przy 48 kHz, czyli 16,131167 s. Poniżej amplitudy szczytowe składowych w stabilnych półsekundowych oknach lewego kanału.

| Faza | Przedział WAV (s) | 440 Hz | 660 Hz |
| --- | --- | ---: | ---: |
| Dwa procesy | 0,399771–0,899771 | 4882,17 | 4882,51 |
| A na 25% | 3,651104–4,151104 | 1220,30 | 4882,50 |
| A mute | 5,113771–5,613771 | <0,001 | 4882,48 |
| A przywrócony | 7,119104–7,619104 | 1220,30 | 4882,50 |
| Master mute | 9,124438–9,624438 | 0 | 0 |
| Master przywrócony | 11,500438–12,000438 | 1220,30 | 4882,50 |
| Po zakończeniu PID 3 | 13,135104–13,635104 | <0,001 | 4882,48 |

Względny poziom 440 Hz po regulacji wynosił 24,995%, a poziom 660 Hz 99,9999% poziomu początkowego. W całym ustalonym obszarze master mute obejmującym 80 448 ramek szczyt wyniósł 0. Celowy odcinek ciszy w pełnym przebiegu trwał 2038,250 ms od 8,877000 s.

Pierwsza automatyczna analiza wybrała w fazie BOTH okno 0,249771–0,749771 s z osłabioną składową B (3891,50). Dało to błędną podstawę porównania poziomów i wynik FAIL gain_a_25. Zachowano tę analizę w measurements-initial.json. Analizator uzupełniono o sprawdzanie stabilności obu składowych w pięciu podoknach po 100 ms. Wybrane okno BOTH ma stabilne obie składowe. Pozostałe okna zachowały wcześniejsze położenie. Analiza całych faz, lista przerw i wynik ciągłości zachowują wszystkie próbki. Ponowna analiza tego samego WAV dała PASS kontroli funkcjonalnej i FAIL ciągłości.

| Początek niezamierzonej ciszy WAV (s) | Długość (ms) | Kontekst |
| ---: | ---: | --- |
| 0,085333 | 52,458 | Rozruch obu źródeł |
| 0,223125 | 21,333 | Początkowa faza obu źródeł |
| 11,098188 | 21,333 | Po przywróceniu mastera |
| 11,322188 | 10,667 | Po przywróceniu mastera |
| 11,439521 | 42,667 | Po przywróceniu mastera |
| 12,079521 | 21,333 | Po przywróceniu mastera |
| 12,207521 | 21,333 | Po przywróceniu mastera |
| 12,954188 | 21,333 | W pobliżu zakończenia A według znacznika fazy |
| 15,044854 | 21,333 | Faza tylko B |

Zmierzono 9 odcinków, łącznie 233,792 ms, maksymalnie 52,458 ms. Są to ciągi co najmniej 2 ms z amplitudą bezwzględną najwyżej 1, poza celowym master mute. W ustalonych aktywnych fazach wystąpiło 14 skoków kolejnych próbek o ponad 1000 jednostek PCM16. W fazie BOTH zmieniały się też poziomy składowych bez pełnej ciszy. Stabilne okna powyżej służą sprawdzeniu regulacji i mieszania.

### Ciągłość i ograniczenia dowodu

Gość zgłosił 13 824 ramek z brakiem próbek co najmniej jednego źródła oczekującego danych. Licznik może wzrosnąć, gdy drugie źródło nadal gra lub master jest wyciszony. Osobno odnotowano 13 spóźnionych obsług HDA, 10 okresów pozostawionych w ciszy ze względu na własność DMA, 4 starty i 3 odzyskania DMA. Maksymalny odstęp dostarczenia powiadomień IRQ wyniósł 334,741 ms, a między odczytami LPIB 414,064 ms. Liczników nie można dodać do czasu ciszy w WAV ani utożsamić ze sobą.

Znaczniki faz są w HPET i rozmiarach WAV zaobserwowanych przez host. Pełny scenariusz trwał około 18 s w gościu, a WAV 16,131 s. Buforowanie WAV i odzyskania uniemożliwiają dokładne przypisanie pojedynczego cichego odcinka do IRQ lub decyzji własności DMA. Ten scenariusz wypisuje agregaty HDA, bez śladu każdego odzyskania. Maksymalna przerwa próbkowania hosta wyniosła 363,578 ms. Przyczyny przerw między wykonaniem gościa, QEMU i VirtualBox pozostają nierozstrzygnięte.

Zrzuty potwierdzają panel Zasobnika ze stanem mute oraz Menedżera zadań z listą procesów i aktualizacją zegara. Zrzut calendar.ppm przedstawia pulpit bez otwartego panelu, więc ta sesja nie potwierdziła responsywności Kalendarza. Sprawdzenie Kalendarza z poprzedniej regresji pozostaje dowodem hostowym. Brak zarejestrowanego panic lub błędu loadera ma zakres tej sesji.

Funkcjonalne odtwarzanie PCM przez dwa niezależne procesy Ring 3, wspólny mikser i HDA zostało potwierdzone natywnie. Potwierdzono też regulację strumienia, mute, master mute, sprzątanie po zakończeniu A i dalszy sygnał B. Izolacja właścicieli i pozostałe przypadki bezpieczeństwa mają wcześniej opisane wyniki hostowe. Ciągłość nadal nie przeszła. Do porównania czasowego przydatna będzie ta sama wersja ISO uruchomiona w QEMU bezpośrednio na fizycznym Linuksie.

Weryfikacja wymagała zmiany wyłącznie analizatora testy/test_pcm_ring3_qemu.py oraz uzupełnienia dokumentacji. Kod produkcyjny i ISO podczas tej próby nie były zmieniane.
