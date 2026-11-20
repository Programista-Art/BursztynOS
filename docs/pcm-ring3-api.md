# Natywne PCM aplikacji Ring 3

BWS 110 udostępnia mikser z istniejącego sterownika HDA. Rejestr BWS ma teraz pierwszy wolny numer 111. BWS 89/90 i 107–109 zachowują swoje numery. Definicje ABI znajdują się w `jadro/bws/pcm_api.h`. Aplikacja nie potrzebuje uprawnień administracyjnych. Demonstracje mają PZB 4 i wyłącznie prawo GUI.

Wywołanie ma postać `bws_wywolaj(110, operacja, a, b, c)`. Wynik należy interpretować jako `int64_t`. Operacje mają numery od 1 do 7.

| Operacja | a | b | c | Sukces |
| --- | --- | --- | --- | --- |
| OPEN (1) | wskaźnik BwsPcmFormat | 24 | wersja 1 | dodatni uchwyt |
| WRITE (2) | uchwyt | wskaźnik PCM16 | liczba ramek 1–512 | rzeczywista liczba przyjętych ramek |
| QUERY (3) | uchwyt | wskaźnik BwsPcmState | 32 | 0 |
| GAIN (4) | uchwyt | głośność 0–100 | mute 0 lub 1 | 0 |
| CLOSE (5) | uchwyt | 0 | 0 | 0 |
| FINISH (6) | uchwyt | 0 | 0 | 0 |
| QUERY_STATS (7) | uchwyt | wskaźnik BwsPcmStats | 104 | 0 |

GAIN ustawia jednocześnie poziom i mute strumienia. QUERY zwraca oba pola. FINISH oznacza naturalny koniec produkcji i pozwala opróżnić kolejkę bez naliczania głodzenia. Kolejny przyjęty WRITE wznawia oczekiwanie próbek. Aplikacja może sprawdzać QUERY do queued=0, oddając CPU między próbami. CLOSE usuwa oczekującą kolejkę. Opublikowany sygnał może jeszcze wybrzmieć z okresów DMA.

BwsPcmFormat zawiera sześć kolejnych uint32_t, w porządku version, size, rate, channels, encoding, reserved. Wersja wynosi 1, size 24, rate 48000, channels 1 lub 2, encoding 1 (signed PCM16 LE), reserved 0. Mono jest powielane do stereo przez obecny mikser. Inne częstotliwości lub formaty zwracają UNSUPPORTED.

BwsPcmState zawiera osiem uint32_t, w porządku version, size, queued, capacity, volume, muted, channels, rate. Ma 32 bajty. Struktury nie zawierają wskaźników i mają sprawdzane rozmiary i przesunięcia. Nagłówek nadaje się do C i C++.

Wyniki błędów to INVALID −1, UNSUPPORTED −2, LIMIT −3, HANDLE −4, FULL −5, BUSY −6, DEVICE −7, MEMORY −8. BUSY pozwala spróbować przy następnym zdarzeniu lub ticku. FULL oznacza zero przyjętych ramek. Dodatni wynik WRITE może być mniejszy od żądania. Należy przesunąć wskaźnik tylko o przyjęty prefiks. Przekroczenie 512 ramek jest błędem, także dla UINT64_MAX.

## Własność i synchronizacja

Dispatcher ustala PID i generację z bieżącego procesu i schedulera. ABI nie przyjmuje tożsamości właściciela. Mikser weryfikuje parę PID/generacja przy każdej operacji oraz uchwyt, który nie jest ponownie przydzielany. Licznik uchwytów zatrzymuje się przy INT64_MAX, aby zachować dodatni zakres wyników OPEN. Limit wynosi osiem strumieni globalnie, dwa na właściciela i 4096 ramek stereo na strumień.

Najpierw QUERY sprawdza właściciela i pobiera liczbę kanałów pod try-lockiem HDA. Potem zamek zostaje zwolniony. Istniejące copy_from_user waliduje cały zakres, wszystkie strony PTE oraz USER/PRESENT i kopiuje do lokalnego bufora maksymalnie 2048 bajtów. Odejmowanie od granicy przestrzeni użytkownika i jawna kontrola przepełnienia adresu chronią zakres. Dopiero pełna udana kopia pozwala wejść pod zamek i zapisać prefiks do kolejki. Nieudana kopia nie publikuje żadnej próbki. QUERY kopiuje lokalny snapshot po zwolnieniu zamka. Nie ma asynchronicznych odwołań do pamięci procesu ani DMA z jego stron.

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

## Engine 2.0 — przygotowanie i pomiar

Punktem odniesienia jest commit 3252415 oraz zachowany WAV .hda-test/pcm-ring3-native-20261120/mixed.wav. Jego wynik to 9 przerw, 233,792 ms łącznej ciszy, 52,458 ms najdłuższej przerwy, 13 824 brakujących ramek oczekiwanych źródeł, 13 spóźnionych obsług HDA i 3 odzyskania. Poprzedni ślad nie zawierał WRITE na poszczególnych strumieniach, więc nie umożliwia odtworzenia historycznej dystrybucji wywołań ani przypisania każdej brakującej ramki do PID.

Pierwszy WRITE mógł dostarczyć 256 ramek, a worker na powrocie z tego samego BWS uruchamiał cztery okresy po 1024 ramki. Daje to 3840 brakujących ramek już przy starcie pierwszego źródła. Producent uzupełniał kolejkę porcjami dopiero po pierwszym rysowaniu GUI. W dalszej pracy zadanie okresowe jest wywoływane także bez zdarzeń wejścia. Gdy okno ma animację, czas czekania wynika jednak z interwału animacji, domyślnie ośmiu ticków (32 ms). Są to konkretne zależności kodu. Związek pozostałych historycznych braków z długimi przerwami IRQ i pracą GUI nie został wcześniej zmierzony na poziomie pojedynczych zapisów.

Zmiany obejmują początkowe uzupełnienie kolejki przed uruchomieniem pętli GUI, ustawienie interwału animacji producenta na jeden tick oraz politykę dolnego progu 2048 ramek i docelowego poziomu 4096 ramek. Producent odpytuje QUERY i kończy wizytę przy poziomie ponad 2048. Gdy poziom jest niższy, przesyła brakujące ramki w najwyżej 16 zapisach. Zachowuje fazę generatora według faktycznie przyjętych ramek. Kolizja GAIN w scenariuszu automatycznym nie zatwierdza etapu regulacji, więc następna wizyta może go ponowić.

Worker może poczekać na zapełnienie kolejki przed startem DMA, najwyżej jeden okres (21,333 ms). Pełna kolejka pozwala wystartować od razu. Krótkie zapisy starszych aplikacji otrzymują ograniczone opóźnienie początkowe. Recovery zachowuje procedurę STOP i potwierdzanie RUN=0. Reguły własności, zapas 9216 bajtów, weryfikacja LPIB oraz rozmiary BDL i kolejek pozostały jak przed tą zmianą.

BWS 110 otrzymał dodatkowe polecenie PCM_QUERY_STATS (7), z argumentami uchwyt, wskaźnik BwsPcmStats i rozmiar 104. Struktura ma 13 pól uint64_t w kolejności writes, requested, accepted, partial, full, starved, muted_missing, last_write_ns, max_write_gap_ns, min_queued, max_queued, consumed, max_copy_ns. Dostęp wymaga tej samej pary PID/generacja i żywego uchwytu co inne operacje PCM. Zamknięty uchwyt jest odrzucany.

Statystyki resetują się przy OPEN. writes liczy wywołania dochodzące do zapisu pod zamkiem miksera, requested żądane w nich ramki, accepted przyjęte ramki, partial częściowe przyjęcia, full odrzucenia z powodu pełnej kolejki. Błędy walidacji i kolizje przed zdobyciem zamka nie należą do writes. consumed liczy ramki pobrane z kolejki do opublikowanego miksu, także przy mute. starved liczy braki oczekiwanego źródła z niezerową głośnością i bez mute. muted_missing liczy analogiczne braki w wyciszeniu. Starszy globalny licznik braki_ramek zachowuje dotychczasową semantykę i może obejmować mute. Liczniki starvation poszczególnych źródeł nie są sumą równoważną globalnemu licznikowi, bo braki mogą się nakładać.

Zegar last_write_ns i maksymalny odstęp odnoszą się do udanych zapisów. max_copy_ns mierzy czas upływający kopiowania z pamięci użytkownika, także ewentualne wywłaszczenie vCPU. Min/max kolejki są próbkowane przy zapisie i konsumpcji. Minimum 0 po dokładnym opróżnieniu kolejki nie musi oznaczać głodzenia, gdy kolejny okres DMA jest już gotowy. Pierwsze 32 zdarzenia głodzenia zapisują właściciela, HPET, ostatni zapis, liczbę brakujących ramek i stan kolejki. Zakończenie scenariusza zrzuca statystyki obu źródeł oraz wszystkie zapisane odzyskania DMA.

Limit WRITE wzrósł wstecznie kompatybilnie do 512 ramek (10,667 ms sygnału). Starsze zapisy 1–256 ramek pozostają obsługiwane. Pomiar kompilatorem -O2, mgeneral-regs-only i fstack-usage dla rzeczywistego wrappera dał 1152 bajty stosu dla 256 ramek, 2176 dla 512 i 4224 dla 1024. Wyniki są w .hda-test/engine2-host/write-stack.json. Domyślny stos Ring 0 ma 64 KiB, a konfiguracja testowa 16 KiB. Wybrano wariant 2176 bajtów zamiast 4224 bajtów na stosie. Kopia nadal odbywa się przed zamkiem HDA, waliduje całość i publikuje próbki dopiero po pełnym sukcesie. Jedno uzupełnienie kolejki wymaga najwyżej ośmiu pełnych zapisów zamiast szesnastu. Polityka zajętości usuwa zbędne próby FULL bez powiększania kolejki. Nie wprowadzono DSP ani rampowania gain, ponieważ poprzedni przebieg nie rozstrzygał, które skoki wynikają wyłącznie ze zmiany poziomu.

### Wynik jedynej sesji Engine 2.0

Artefakty .hda-test/pcm-engine2-20261120/ zawierają WAV, znaczniki faz, pełny log oraz measurements.json. Testowany ISO miał SHA-256 f55db2ee1946427688bd58f04783303393854b0dc0f8fec6bd3481a5eecad212. Porównanie maszynowe znajduje się w .hda-test/engine2-host/comparison.json. Próba nie osiągnęła celu poprawy ciągłości.

| Miara | Poprzedni Ring 3 | Zarejestrowany Engine 2.0 |
| --- | ---: | ---: |
| Niezamierzone odcinki ciszy | 9 | 12 |
| Łączny czas ciszy | 233,792 ms | 331,438 ms |
| Najdłuższa przerwa | 52,458 ms | 42,667 ms |
| Skoki >1000 w ustalonych aktywnych fazach | 14 | 28 |
| Globalne brakujące ramki, ta sama semantyka | 13 824 | 23 552 |
| Spóźnione obsługi HDA | 13 | 17 |
| Okresy ciszy dla ochrony DMA | 10 | 11 |
| Starty / odzyskania DMA | 4 / 3 | 3 / 2 |
| Maks. odstęp IRQ | 334,741 ms | 324,564 ms |
| Maks. odstęp odczytów LPIB | 414,064 ms | 324,289 ms |

PCM440 uruchomił się jako PID 2, PCM660 jako PID 3. Zarejestrowano dwa udane OPEN i usunięcie jednego strumienia przy zakończeniu procesu. Oba sygnały były jednocześnie obecne, niezależna regulacja i mute działały. Przedział BOTH 1,349771–1,849771 s dał amplitudy 4882,17/4882,51. GAIN_A_25 3,679771–4,179771 s dał 1220,30/4882,50. STREAM_MUTE 5,156438–5,656438 s dał około 0/4882,48. STREAM_RESTORE 7,140438–7,640438 s dał 1220,30/4882,50. MASTER_MUTE 9,145771–9,645771 s dał dokładne zera. ONLY_B 13,529104–14,029104 s dał około 0/4882,48. Po przywróceniu mastera nie znaleziono stabilnego półsekundowego okna, dlatego kontrola funkcjonalna skryptu zgłosiła FAIL master_restore. W odcinku 11,150–11,250 s poziomy wróciły do 1220,30/4882,50, a potem występowały kolejne przerwy. Tego krótszego pomiaru nie użyto do zmiany wyniku testu.

Nagranie ma 762 283 ramki PCM16 stereo 48 kHz, 15,880896 s. Na końcu jest 137,521 ms ciszy po zamknięciu B. Szczyt całego nagrania wynosi 11 645 jednostek PCM16, a próbek na granicach −32768/32767 jest zero. Nie stwierdzono clippingu. Klików po restartach i brakach danych nie wyeliminowano.

| Początek WAV (s) | Cisza (ms) |
| ---: | ---: |
| 0,063229 | 22,104 |
| 0,106667 | 21,333 |
| 0,581313 | 21,333 |
| 11,269313 | 42,667 |
| 11,375979 | 21,333 |
| 11,482646 | 21,333 |
| 11,631979 | 42,667 |
| 11,823979 | 42,667 |
| 12,266042 | 21,333 |
| 12,298042 | 32,000 |
| 13,460708 | 21,333 |
| 14,399375 | 21,333 |

Metoda wykrywania ciszy zachowuje poprzedni próg amplitudy najwyżej 1 przez co najmniej 2 ms. Celowy master mute jest wyłączony z listy przerw, a ustalone fazy mają ten sam margines 250 ms. Odcinki utraty jednej składowej przy ciągłym drugim źródle mogą nie tworzyć pełnej ciszy i są dodatkowo widoczne w statystykach źródeł.

### Dokładne statystyki źródeł

| Licznik | PID 2 / 440 Hz | PID 3 / 660 Hz |
| --- | ---: | ---: |
| Zapisy pod zamkiem | 1220 | 1503 |
| Żądane i przyjęte ramki | 624 640 | 769 536 |
| Częściowe zapisy / FULL | 0 / 0 | 0 / 0 |
| Brakujące aktywne ramki | 15 360 | 10 752 |
| Brakujące ramki podczas mute strumienia | 1024 | 0 |
| Min. / maks. kolejki | 0 / 4096 | 0 / 4096 |
| Maks. przerwa między udanymi WRITE | 404,177 ms | 437,548 ms |
| Maks. czas kopii z pamięci użytkownika (HPET) | 1,615 ms | 3,023 ms |
| Ramki pobrane przez mikser | 621 568 | 766 464 |

Wszystkie 2723 zapisy dochodzące do miksera żądały po 512 ramek i przyjęły całość. Front BWS 110 policzył 2727 prób WRITE: 2723 żądania w przedziale 257–512 ramek, dwa zapisy 256 ramek z niepoprawnym adresem oraz dwa żądania z niepoprawną długością. Wyniki MEMORY=2 i INVALID=2 pochodzą z celowej kontroli startowej aplikacji. FULL=0 i BUSY=0 na froncie WRITE wykluczają odmowę zapisu jako przyczynę głodzenia w tym nagraniu. Brakujące aktywne ramki obu strumieni mogą się nakładać, dlatego ich suma różni się od globalnego licznika. Pozostałe po 3072 ramki na strumień usunięto w ramach udokumentowanego zamknięcia lub zakończenia właściciela.

Zapisano 27 zdarzeń głodzenia z PID, HPET, czasem ostatniego udanego WRITE i zajętością. Ich liczby brakujących ramek sumują się dokładnie do 15 360 i 10 752. Pełny zapis jest w starvation_events w measurements.json. Przykładowo PID 2 zabrakło danych przy 7,480491 s, 48,316 ms po ostatnim zapisie. Podczas wznowienia przy 8,373935 s zabrakło mu kolejnego okresu już 18,917 ms po zapisie. PID 3 zabrakło danych przy 8,391973 s po przerwie od ostatniego zapisu trwającej około 397 ms. Pierwsze dwa przypadki wskazują konsumpcję początkowej lub odtwarzanej kolejki w trakcie uzupełniania. Odstępy rzędu 400 ms przekraczają zarówno kolejkę 85,333 ms, jak i cały użyteczny zapas przygotowanego sygnału. Dokładnego stanu schedulera każdego producenta w tych chwilach nie zarejestrowano.

### HDA i przerwania

| HPET odzyskania (s) | Przerwa IRQ (ms) | Najstarsze zgłoszenie (s) | LPIB (B) | Powód |
| ---: | ---: | ---: | ---: | --- |
| 8,322661 | 324,564 | 8,321949 | 21 500 | Watchdog |
| 20,315077 | 138,307 | 20,291089 | 19 340 | Watchdog |

Nie odnotowano błędów FIFO ani deskryptora. Średnie oczekiwanie zgłoszonego workera wyniosło 0,967 ms, maksymalne 56,016 ms. Worker miał 75 odmów try-locka. Maksymalny czas renderowania okresu według HPET wyniósł 17,050 ms. Ślad shortfalls zawiera wszystkie 11 pominięć ze względu na własność DMA. Odstęp do początku okresu wynosił 2792–9212 bajtów, zawsze poniżej ochronnego progu 9216. Zapis został prawidłowo odrzucony. W dziewięciu z tych decyzji zgłoszenie czekało 37,873–56,078 ms, w dwóch mniej niż 1 ms po długiej przerwie IRQ. Dane źródłowe były wtedy w co najmniej jednej kolejce. Przesunięcia kursora i czasy decyzji zachowano w shortfalls.

Pierwsze 32 odstępy IRQ co najmniej 12 ms zapisano w irq_gaps, a drugi watchdog zachował jeszcze późniejszy odstęp 138,307 ms. Limit śladu nie pozwala podać wszystkich późniejszych odstępów. Maksymalna luka samplera wątku TCG na Linuksie wyniosła 353,789 ms. HPET i te próbki nie rozstrzygają udziału pracy jądra, QEMU i VirtualBox. Nie przypisano tych opóźnień automatycznie do hosta.

Zrzuty potwierdzają otwarcie Kalendarza, panelu Zasobnika i Menedżera zadań oraz postęp zegara. W jednej próbce Menedżer zadań pokazał 92,1% zajętości CPU gościa. Cały proces QEMU zużył 9,27 s CPU w 18,042 s między znacznikami BOTH i DONE. Pomiar ten obejmuje całe QEMU i nie wyznacza kosztu samego miksera.

### Korekta po pomiarze i stan końcowy

Pomiary ujawniły, że koniec pierwszej porcji producenta może zbiec się z opróżnieniem kolejki przez priming DMA. Dlatego końcowa wersja producenta odświeża QUERY po każdej zakończonej porcji i może uzupełnić kolejkę drugi raz w tej samej wizycie, z niezmiennym budżetem maksymalnie 16 WRITE. Dotyczy to też wznowienia DMA. Test korzystający z rzeczywistego workera po każdym zapisie potwierdza, że start pobiera pierwszą kolejkę, a ta sama wizyta pozostawia co najmniej 3072 kolejne ramki bez głodzenia. Implementację polityki współdzieli z testem nagłówek programy/pcm_demo/refill_policy.h.

Ta ostatnia korekta przeszła 18 scenariuszy / 357 sprawdzeń HDA/PCM/API oraz mikser z ASan i UBSan, ale nie została nagrana natywnie. Limit jednej sesji QEMU w tym zadaniu został wykorzystany. Aktualne ISO z korektą ma SHA-256 003fd9ec2a732cbeda6a3ebed7fd757db4e99316fdb5222bf08200e6b93b7f17 i różni się od ISO użytego do powyższego WAV. Końcową zgodność obu osadzonych programów z obrazem sprawdzono w final-preflight.json.

Pełna regresja przed nagraniem zaliczyła mikser, HDA PCM/API, transport i topologię HDA (11 287), diagnostykę HDA (241), izolację BWS (181), zakończenie procesów (4063), Zasobnik (1468), Kalendarz (16 102), Menedżer zadań (1802), oba loadery Testu dźwięku, GUI (2662) i BWS 89/90 (2156). Uruchomione sanitizery nie zgłosiły błędów. Końcowe ISO przeszło Multiboot2, kontrolę fontów i audyt wydania bez ustaleń. Logi są w .hda-test/engine2-host/.

Kolejka ma nadal 16 384 bajty próbek na strumień i 131 072 bajty przy ośmiu strumieniach. Cały Mixer zajmuje 133 592 bajty, w tym statystyki i 32-elementowy ślad. Archiwum zakończonych strumieni zajmuje 960 bajtów, bufor renderowania DMA 4096 bajtów, a BDL z ośmioma stronami DMA 36 864 bajty. Maksymalny bufor kopii WRITE ma 2048 bajtów na stosie. Końcowy pomiar wrappera wraz z licznikami i ochroną stosu dał 2256 bajtów statycznego użycia stosu. Nie jest to pomiar całego zagnieżdżonego stosu syscalla.

Końcowy plik jądra w ISO ma 8 494 992 bajty, ELF z symbolami 13 231 312 bajtów. Wyrównany zakres ładowania od 0x100000 to 11 816 960 bajtów, koniec 0xC45000 wobec limitu 0xF00000. Bufory DMA i stosy procesów są osobnymi przydziałami wykonania.

Funkcjonalny transport Ring 3 pozostaje podstawą do prac nad klientami PCM. Cel Engine 2.0 dotyczący jakości nie został osiągnięty w zarejestrowanej próbie. Bez pomiaru ostatniej korekty i wyjaśnienia długich odstępów wykonania nie ma podstaw do deklaracji stabilnego audio dla Husarza lub odtwarzacza. Porównanie tego samego ISO w QEMU na fizycznym Linuksie pozostaje konkretnym sposobem ograniczenia niepewności środowiska.
