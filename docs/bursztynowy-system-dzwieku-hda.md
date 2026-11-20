# Bursztynowy System Dźwięku — transport HDA

Sterownik wybranego wyjścia posiada jedną instancję `Pcm::Mixer` oraz jeden deskryptor wyjściowy HDA. Wewnętrzny interfejs `PcmJadro` przyjmuje próbki stereo PCM16 LE przy 48 kHz i zwraca liczbę przyjętych ramek. Kopiuje je do pierścieni jądra i nie przechowuje wskaźnika producenta. Mikser ma osiem miejsc po 4096 ramek. Zaufani producenci tego etapu współdzielą jedną wewnętrzną tożsamość z limitem dwóch strumieni. Generator Testu dźwięku ma osobną tożsamość i osobny uchwyt. Kod tego etapu nie rejestruje nowych wywołań BWS.

Instancja miksera jest wyzerowana w BSS. Otwarcie jest możliwe po pomyślnej inicjalizacji trasy HDA i HPET. Silnik uruchamia DMA przy pierwszym źródle. `PcmJadro::wylacz()` najpierw potwierdza `RUN=0`, następnie usuwa strumienie i zamyka dostęp do silnika do końca pracy systemu. Strony PMM należą do sterownika przez cały czas pracy systemu. Po zamknięciu obu wewnętrznych źródeł silnik przez krótki okres uzupełnia BDL ciszą, po czym zatrzymuje się. Zamknięcie jednego źródła usuwa od razu ramki z jego kolejki, a dane już opublikowane mogą wybrzmieć raz.

Wielozadaniowość działa na jednym CPU. `GuardHDA` jest try-lockiem wspólnym dla producentów, miksera, zmian głośności i operatora deskryptora. Wywołujący dostaje `Zajete` przy kolizji. Zgłoszenie timera zawiera czas pierwszej nieobsłużonej pracy. Dopiero zdobycie blokady pozwala je pobrać. Timer może dołączyć kolejne zgłoszenie podczas pracy i żadna ścieżka nie kasuje go po zakończeniu miksowania.

Timer LAPIC nominalnie zgłasza pracę co 4 ms. Ten sam worker działa w PID0, na granicach syscalli, w ich istniejącym kooperacyjnym punkcie oraz między porcjami kompozycji i oczekiwania na GPU. Gdy zgłoszenie czeka co najmniej 12 ms, scheduler daje PID0 najwyżej jeden wcześniejszy powrót dla tego zgłoszenia. Kod IRQ nie miksuje próbek. Praca odroczona wymaga włączonych przerwań. HDA nie bierze blokad grafiki ani tablicy procesów i nie oddaje CPU z trzymaną blokadą. Na obecnym jednym CPU krótka sekcja IF=0 serializuje publikację DMA z awaryjnym STOP w timerze. Port SMP będzie wymagał umieszczenia obu ścieżek na tym samym CPU lub osobnej synchronizacji MMIO. Znacznik zgłoszenia i zamek są atomowe.

## Własność i terminy

Okres ma 4096 bajtów (1024 ramki) i trwa 21,333 ms. Fizyczny BDL ma osiem okresów (170,667 ms). Start wypełnia cztery okresy, a pozostałe zeruje. W czasie RUN silnik może przygotować do pięciu okresów łącznie z bieżącym. Piąty uzupełnia wtedy, gdy kolejki mają próbki do opublikowania. Dystans od bieżącego LPIB do początku ostatniego przygotowanego okresu może wynosić najwyżej 4096 ramek (85,333 ms), a do końca 5120 ramek (106,667 ms). Jest to o jeden okres więcej niż w poprzedniej wersji, zarówno dla gotowego sygnału, jak i czasu wybrzmiewania już opublikowanych danych po zmianie poziomu strumienia. Fizyczny BDL zachowuje tę samą długość. Pozycję daje `SDnLPIB`. Sterownik przelicza różnice modulo CBL na postęp w bajtach. Wartość równa CBL jest równoważna granicy obrotu.

1. Opublikowana strona należy do sprzętu do chwili, gdy link przekroczy jej koniec.
2. Sterownik wtedy zeruje stronę, pod warunkiem że do jej ponownego odczytu zostało co najmniej 9216 bajtów. Ten sam próg obowiązuje publikację. Obejmuje sprawdzane FIFO (do 4096 bajtów), dopuszczony skok modelu pozycji (8192 bajtów) i 1024 bajty zapasu.
3. Mikser przygotowuje następny okres w buforze CPU. Przed zapisem do DMA sterownik ponownie odczytuje LPIB. Jeśli miejsce z gotową ciszą weszło już za blisko sprzętu, zostaje pominięte. Silnik uzupełnia kolejne bezpieczne okresy, bez STOP.
4. Jeśli dana strona mogłaby jeszcze zawierać stare próbki i jest już zbyt blisko sprzętu, sterownik wstrzymuje silnik. Dopiero po odczycie potwierdzającym RUN=0 wolno wypełnić wszystkie strony od nowa. Zachowuje kolejki producentów i stan kodeka. Po powodzeniu STOP kolejna obsługa może wznowić DMA od razu. Po nieudanym STOP ponawia próbę po jednym okresie.

Czas HPET ogranicza wiek próbki pozycji. Przy odstępie 128 ms i dłuższym dopuszczony postęp plus zapas na skok obejmuje cały obrót i modulo przestaje rozstrzygać bezpieczeństwo. Watchdog ogranicza ten czas wcześniej, biorąc położenie najbliższej potencjalnie starej strony i odejmując zapisany zapas oraz 8 ms dla timera. Przy zdrowym potoku jego termin wynosi około 93–115 ms od odczytu pozycji. Próg porównawczy spóźnionej obsługi pozostaje przy dwóch okresach (42,667 ms) i sam nie wymusza STOP.

Watchdog w IRQ może skasować RUN i zapamiętać termin przekroczenia. W IRQ nie ma oczekiwania, alokacji ani dostępu do stron próbek. Margines zakłada, że timer dostanie CPU. Jeśli cały gość albo przerwania zostaną wstrzymane na dłużej niż obrót, oprogramowanie może wykryć utratę pewności i odbudować bufor po STOP. Nie może cofnąć próbek zużytych w czasie, w którym CPU nie wykonywał kodu.

Diagnostyka liczy odstępy między tickami, czas od najstarszego zgłoszenia, kolizje blokady i czas jej posiadania, skoki LPIB, odstęp między odczytami, czas wznowienia oraz przyczyny. Zachowuje pierwsze 32 zdarzenia wznowienia z pozycją, terminem i znacznikami. Raport szeregowy powstaje dopiero po teście. HPET mierzy czas upływający. Liczba przygotowanych okresów i liczba okresów przeciętych przez IRQ opisują wykonaną pracę, a pomiar przydziału CPU dla wątku TCG stanowi dodatkowy kontekst. Żaden z tych liczników sam nie identyfikuje zatrzymania zewnętrznej maszyny VirtualBox.

Brak ramek liczy ramki, dla których podczas miksowania co najmniej jedna otwarta kolejka była pusta. Cisza miksera obejmuje zerowe wyniki po uwzględnieniu programowego mastera. Powstawanie okresów przy master mute ma własny licznik. Bezpieczna cisza obejmuje pominięte okresy, których sterownik nie może już zapisać. Jeśli w takim przypadku czekały próbki, rośnie też licznik utraconych okresów. Te liczniki mierzą decyzje sterownika. WAV mierzy odebrany sygnał. Błędy FIFO/BDL i przyczyny odzyskania DMA mają odrębne liczniki.

W scenariuszu PCM rejestrowane są odstępy rzeczywistych dostarczeń przerwań, czas do EOI i obszary IF=0 na BSP. Oznaczenia obszarów to 0 dla timera i schedulera, 1 dla pozostałych IRQ, 2 dla programowego oddania CPU, 3 dla VMM, 4 dla klonowania jego hierarchii, 5 dla liczenia stron VMM, 6 dla PSF, 7 dla sterty i 8 dla alokatora stron PMM. Dwa 32-elementowe bufory rejestrują odstępy i obszary trwające co najmniej 12 ms. Pomiar jest włączony od otwarcia źródeł do końca testu. Odczyt `/proc` dla wątku vCPU wskazanego przez QMP umożliwia zestawienie czasu wątku TCG z pomiarem gościa. Zegar HPET mierzy również czas upływający podczas wywłaszczenia wątku. Przy zwykłym rozruchu zbieranie tych śladów jest wyłączone.

## Zgodność

Generator tonu, sweep i testy kanałów używają własnej kolejki PCM. Rozpoczęcie kolejnego tonu podmienia wyłącznie ten strumień. Gdy to jedyne źródło, zakończenie zachowuje historyczny odczyt RUN=0. Gdy inne źródło gra, RUN pozostaje aktywny. Diagnostyka CORB/RIRB, wybór trasy i punkt odniesienia wzmacniacza pozostają w istniejącym sterowniku. Master używa wzmacniacza kodeka, jeśli trasa go obsługuje. Programowy backend skaluje ostateczne okresy po zmiksowaniu, więc jego zmiany nie wpływają na okresy już przekazane DMA.

Test wewnętrzny wymaga parametru rozruchu `bursztyn.test=hda-pcm`. Po uruchomieniu pulpitu podaje do dwóch osobnych uchwytów 440 Hz i 660 Hz, zmienia poziom pierwszego źródła, wycisza i przywraca master, zamyka źródła po kolei i wypisuje liczniki. `testy/test_hda_pcm_qemu.py` używa tego samego pliku jądra co produkcyjne ISO, uruchamia jedną maszynę bez dysku danych i zapisuje WAV, logi oraz pomiary. Produkcyjne menu rozruchu nie uruchamia testu automatycznie.

## Wynik przed stabilizacją

Testy hosta z ASan/UBSan przeszły dla miksera, sterownika tonu i PCM (10 scenariuszy, 139 sprawdzeń), transportu i topologii (49 scenariuszy, 11 287 sprawdzeń), diagnostyki HDA (241), izolacji HDA BWS (180), ścieżki BWS 89/90 (8 scenariuszy, 2156 sprawdzeń), Zasobnika (1468), zakończenia procesów (4063) i Kalendarza (16 102). Szerszy cel `test-dzwieku-repair` zatrzymał się na brakujących deklaracjach w wydzielonej atrapowej kompilacji loadera. Wydzielony test okna także wymaga aktualizacji zależności atrapy systemu plików. Test BWS 89/90 został uruchomiony osobno.

`make -j1 iso` przeszedł wraz z kontrolą Multiboot2 i weryfikacją wydania. Plik jądra ma 12 742 856 bajtów, a zajęty zakres ładowania 11 501 568 bajtów. Adres końca jądra to `0xBF8000` wobec limitu `0xF00000`. Symbol instancji miksera zajmuje 131 472 bajty BSS, a bufor przygotowania okresu dodatkowe 4096 bajtów. HDA przydziela jedną stronę BDL i cztery strony danych (20 KiB łącznie).

Pojedyncza sesja QEMU 8.2.2, TCG na jednym wirtualnym CPU, Intel HDA i `hda-output`, zapisała 554 992 ramek stereo PCM16 przy 48 kHz (11,562 s). Artefakty znajdują się w `.hda-test/pcm-native-20261120/`. Poniższe amplitudy szczytowe składowych obliczono z lewego kanału w wybranych przedziałach pliku WAV, po pominięciu przejść między fazami.

| Przedział WAV | Faza | 440 Hz | 660 Hz |
| --- | --- | ---: | ---: |
| 1,60–3,00 s | Oba źródła | 4882,17 | 4882,51 |
| 4,00–5,00 s | Pierwsze źródło na 25% | 1220,30 | 4882,50 |
| 5,50–6,80 s | Master mute | 0 | 0 |
| 7,00–7,30 s | Master przywrócony | 1220,30 | 4882,50 |
| 9,00–9,80 s | Tylko drugie źródło | 0 | 4882,48 |

Za ostatnim sygnałem nagrano 158,1 ms ciszy. Zrzuty z pracy obu faz pokazują otwarcie Kalendarza, panel głośności i Menedżera zadań oraz postępujący zegar paska. Menedżer zadań pokazał w jednej próbce 77,1% łącznie i 25,8% dla PID0. To próbka działania całego gościa podczas uruchamiania okien. Licznik HPET odnotował 128,89 ms przygotowywania okresów PCM w scenariuszu 15 s (około 0,86% czasu scenariusza), w tym maksymalnie 10,37 ms na okres. Ten licznik mierzy czas upływający, który może obejmować wywłaszczenie.

**Kryterium ciągłości nie przeszło.** Gość zgłosił 627 przygotowanych okresów, 2560 ramek z brakiem danych w co najmniej jednym strumieniu, 28 zdarzeń opóźnienia lub utraty pewności pozycji, 28 odzyskań i 29 uruchomień deskryptora. Błędów FIFO/BDL odnotowano zero. Największy zarejestrowany odstęp 372,47 ms przekracza zarówno termin 42,67 ms, jak i cały cykl BDL. Fragmenty 660 Hz po zamknięciu pierwszego strumienia mają przerwy lub zmiany fazy zgodne ze wznowieniami DMA. Pierwotny analizator oczekiwał stałego odwzorowania czasu gościa na długość WAV i przerwał pracę przy ostatniej fazie. Pomiary powyżej wykonano z zachowanego pliku, a skrypt na przyszłe uruchomienia zapamiętuje rozmiar nagrania przy zmianach fazy. Wznowienia nadal powodują niezaliczenie weryfikacji.

Sprzęt fizyczny oraz PCM aplikacji Ring 3 pozostają bez weryfikacji na tym etapie.

## Pomiar stabilizacji

Próba z jednym vCPU i QEMU 8.2.2 TCG wykonała ten sam scenariusz bez dysku danych. Katalog `.hda-test/pcm-stability-20261120/` zawiera WAV, znaczniki faz, log, zrzuty pulpitu i `measurements.json`. Poniżej porównano dwie sesje; pomiary sprzed stabilizacji miały mniej liczników.

| Miara | Przed | Próba stabilizacji |
| --- | ---: | ---: |
| Spóźnienia według licznika danej wersji | 28 | 14 |
| Starty deskryptora, w tym start początkowy | 29 | 4 |
| Odzyskania DMA | 28 | 3 |
| Maks. przerwa między odczytami | 372,465 ms | 320,781 ms |
| Ramki z pustą kolejką co najmniej jednego źródła | 2560 | 3072 |
| Pominięte okresy, pozostawione w ciszy | brak licznika | 34 |
| Błędy FIFO/BDL | 0 | 0 |
| Czas przygotowywania okresów (HPET) | 128,890 ms | 84,083 ms |
| Najdłuższe przygotowanie okresu (HPET) | 10,366 ms | 6,610 ms |

Dawny licznik spóźnień obejmował problemy skutkujące odzyskaniem. Obecny zlicza także opóźnienia przekraczające 42,667 ms, przy których DMA może nadal działać. Porównanie jest ścisłe dla liczby restartów i maksymalnej przerwy. Wersja z testu zarejestrowała 2816 obsłużonych zgłoszeń. Średnie oczekiwanie od najstarszego zgłoszenia wyniosło 1,021 ms, maksymalne 320,211 ms. Try-lock odrzucił 89 prób workera. Maksymalny odnotowany czas trzymania blokady wyniósł 11,109 ms. Najdłuższe odzyskanie zajęło 12,317 ms. Maksymalny zmierzony dystans linku do początku nowo publikowanego okresu wyniósł 3071 ramek (63,98 ms).

| Źródło opóźnienia | Co wiadomo z pomiaru |
| --- | --- |
| Dostarczenie IRQ | Trzy restarty wywołał watchdog po odstępach IRQ 316,236 ms, 238,629 ms i 116,172 ms. Najdłuższy odstęp zakończył się w PID0 w Ring 0. |
| Praca odroczona | Przy dwóch pierwszych restartach zgłoszenia już czekały. Czekały 320,211 ms i 242,779 ms, niemal przez całą przerwę IRQ. Przy trzecim worker doszedł do odzyskania około 0,078 ms po zgłoszeniu. Dwa razy scheduler przyspieszył powrót do PID0. |
| Blokada i miksowanie | Zmierzone kolizje są rzeczywiste. Maksymalny czas posiadania zamka i przygotowania okresu był o rząd wielkości mniejszy od okien prowadzących do restartu. |
| Pozycja i sprzęt | Największa zaakceptowana różnica LPIB wyniosła 11 984 bajty. Nie było odzyskań z powodu odrzuconej pozycji, zastoju, niebezpiecznego okna zapisu ani błędu FIFO/BDL. |
| Linux, TCG, VirtualBox | Proces QEMU zużył 6,90 s CPU w 15,01 s scenariusza (45,97% jednego rdzenia). W pliku samplera nie ma próbek wątku TCG, ponieważ QEMU wystartował bez nazwy diagnostycznej wątku. HPET nie odróżnia maskowania IRQ przez gościa od braku wykonania vCPU. Konkretnej przyczyny nadrzędnej warstwy nie ustalono. Harness nadaje teraz nazwy wątkom. |

Nagranie to 668 145 ramek PCM16 stereo, 48 kHz, 13,920 s. Podczas fazy wspólnej amplitudy 440 i 660 Hz wyniosły 4882,17 i 4882,51. Po zmianie poziomu pierwszego źródła wyniosły 1220,30 i 4882,50. Master mute dał dokładne zera, a przywrócenie poziomu dało odczyty 1220,30 i 4882,50. W fazie po zamknięciu pierwszego strumienia odczyty wyniosły około 0 i 4882,48. Na końcu jest 142,90 ms ciszy. W aktywnych fazach były jednak odcinki niezamierzonej ciszy do 85,35 ms. Scenariusz nie spełnił kryterium ciągłości. Kalendarz, Zasobnik, zegar i Menedżer zadań reagowały podczas odtwarzania.

Po tej sesji dodano odczyt kontrolny LPIB przy wygaśnięciu watchdoga. Przedłużenie jest możliwe wtedy, gdy odczyt potwierdza bezpieczny dystans do starej strony, w granicy 128 ms od ostatniego zaufanego odczytu workera. Test hosta potwierdził odroczenie STOP przy wolniej przesuwającym się kursorze oraz wymuszony STOP przy niejednoznacznym położeniu. Wersja z tą zmianą przeszła 192 sprawdzenia ASan/UBSan i ponowną budowę produkcyjnego ISO. Jej plik jądra miał 12 805 192 bajty, a adres końcowy wyniósł `0xBFB000`.

Pozostałe testy hosta przeszły w kolejności `make -j1`: mikser, transport/topologia HDA (11 287), diagnostyka (241), izolacja BWS (180), Zasobnik (1468), zakończenie procesów (4063), Kalendarz (16 102) i VirtIO (186). Naprawiono atrapy testu `test-dzwieku-repair` bez zmian produkcyjnego loadera ani GUI. Test przeszedł obie wersje loadera, GUI/fonty/raport (2662) i BWS 89/90 (2156). Sprzęt fizyczny i PCM Ring 3 pozostają poza tą weryfikacją.

## Końcowa próba IRQ i LPIB

Jedna sesja QEMU 8.2.2 TCG z jednym vCPU uruchomiła jądro z końcowego ISO bez dysku danych. Kopia obrazu dostała parametr testowy w menu GRUB. Katalog `.hda-test/pcm-irq-final-20261120/` zawiera WAV, log szeregowy, znaczniki faz, próbki przydziału CPU dla TCG i sumy SHA-256. Analizę istniejącego nagrania uruchamia `python3 testy/test_hda_pcm_qemu.py --analyze .hda-test/pcm-irq-final-20261120`. Test zwraca kod 1, kiedy kryterium ciągłości jest niespełnione, i zapisuje osobne wyniki dla kontroli sygnału i ciągłości.

| Miara | Poprzednia próba | Końcowa próba |
| --- | ---: | ---: |
| Starty DMA, w tym pierwszy start | 4 | 3 |
| Odzyskania DMA | 3 | 2 |
| Spóźnienia obsługi o progu 42,667 ms | 14 | 13 |
| Maks. odstęp między odczytami LPIB | 320,781 ms | 184,663 ms |
| Maks. odstęp dostarczenia IRQ podczas aktywności HDA | 316,236 ms | 186,937 ms |
| Oczekiwanie workera od powiadomienia, średnia / maks. | 1,021 / 320,211 ms | 0,857 / 52,009 ms |
| Kolizje try-locka / maks. czas posiadania | 89 / 11,109 ms | 62 / 25,033 ms |
| Maks. czas odzyskania od STOP watchdoga do ponownego RUN | 12,317 ms | 27,034 ms |
| Okresy pozostawione w ciszy ze względu na własność DMA | 34 | 11 |
| Z tego okresy pominięte przy dostępnych próbkach | brak licznika | 7 |
| Ramki miksowane przy pustej kolejce co najmniej jednego źródła | 3072 | 12 288 |
| Błędy FIFO / deskryptora | 0 / 0 | 0 / 0 |
| Maks. niezamierzona cisza w aktywnej fazie WAV | 85,354 ms | 64,000 ms |

Timer LAPIC pracował okresowo z nominalnym taktem 4 ms. Średni odstęp dostarczenia wyniósł 4,362 ms. W 3439 odstępach zmierzono 28 przerw na co najmniej 12 ms. Wszystkie 3440 obsłużonych przerwań timera otrzymały EOI, maksymalnie po 1,952 ms. Najdłuższy z obserwowanych odcinków IF=0 wyniósł 4,723 ms. Pomiar IF=0 obejmował wybrane miejsca w kodzie jądra.

| Odstęp między rzeczywistymi IRQ | Kontekst w chwili dostarczenia | Powiadomienie → obsługa odzyskania | Przedział próbek Linuksa / rozliczony CPU wątku TCG / kolejka CPU |
| ---: | --- | ---: | ---: |
| 132,645 ms | Ring 3, PID 2 (Diagnostyka systemu) | 0,547 ms | 186,798 / 185,206 / 0,068 ms |
| 186,936 ms | Ring 3, PID 3 (Menedżer zadań) | 0,318 ms | 345,253 / 325,792 / 1,598 ms |

Oba restarty wywołał watchdog po przerwie w dostarczaniu powiadomień. Zmierzony czas od powiadomienia wyklucza długie oczekiwanie gotowego workera jako bezpośrednią przyczynę tych dwóch restartów. Try-lock miał 62 kolizje, a maksymalny czas przygotowania okresu wyniósł 2,340 ms według HPET. W przybliżonym dopasowaniu śladów do znaczników faz Linux rozliczył znaczną część obu przedziałów jako CPU TCG. Zebrane próbki nie pokazują długiego oczekiwania tego wątku w kolejce Linuksa ani nakładania się ze zrzutem QMP. Maksymalna luka próbkowania wynosiła 186,798 ms. Ten pomiar nie wskazuje, jaką część okna TCG spędził na wykonywaniu instrukcji gościa, i nie rozstrzyga udziału zewnętrznej warstwy wirtualizacji ani przyczyny opóźnienia dostarczenia w QEMU.

Końcowe jądro wykonało dwa odczyty kontrolne LPIB i w obu przypadkach watchdog skasował RUN. Oba odstępy IRQ przekroczyły 128 ms. Granicę pewności pozycji sterownik liczy od ostatniego zaufanego odczytu LPIB workera, a log nie rozróżnia poszczególnych powodów odmowy odroczenia. Odroczenie STOP przy zachowanej pewności pozycji zaliczyło test z modelowanym MMIO. Odczyt kontrolny może zmienić termin watchdoga, a publikacja okresu nadal odbywa się w workerze po ponownej kontroli LPIB i własności. Po awaryjnym STOP strony można wypełnić dopiero po potwierdzeniu RUN=0. Przy zmierzonym wyprzedzeniu 4096 ramek do początku piątego okresu użyteczny zapas kończył się po najwyżej 106,667 ms od pozycji linku. Kontroler może zużyć przygotowane próbki przed obsługą IRQ przy tak długich odstępach.

Nagranie zawiera 674 436 ramek PCM16 stereo przy 48 kHz (14,05075 s). Amplitudy 440/660 Hz w półsekundowych odcinkach bez przerw wyniosły odpowiednio 4882,17/4882,51 przy obu źródłach, 1220,30/4882,50 po ustawieniu 25%, 0/0 przy master mute, 1220,30/4882,50 po przywróceniu mastera i 0/4882,48 po zamknięciu pierwszego źródła. W utrzymanej fazie mute zmierzony szczyt wyniósł 0. Cisza po zamknięciu obu źródeł trwała 146,042 ms. Zrzuty pokazują Kalendarz, panel Zasobnika, stan mute i Menedżera zadań. W scenariuszu kontrolę mastera przełącza producent testowy przez wspólny interfejs sterownika.

W pełnym przebiegu lewego kanału poza celowym wyciszeniem mastera (6,495750 s przez 2005,563 ms) zmierzono 19 odcinków długości co najmniej 2 ms z amplitudą bezwzględną najwyżej 1. Poniższe czasy są pozycjami i długościami w nagraniu. Łączny czas tych odcinków wyniósł 446,313 ms. W ustalonych fazach aktywnych odnotowano także 27 skoków między kolejnymi próbkami o ponad 1000 jednostek PCM16. Przy przejściach między fazami analiza ciągłości używa marginesu 250 ms, ponieważ rozmiar pliku może pozostawać w tyle za sygnałem w urządzeniu.

| Początek w WAV (s) | Długość (ms) | Początek w WAV (s) | Długość (ms) |
| ---: | ---: | ---: | ---: |
| 0,085333 | 42,667 | 8,806042 | 64,000 |
| 0,160000 | 10,667 | 8,902042 | 53,333 |
| 1,146104 | 42,667 | 8,987375 | 10,667 |
| 1,220771 | 10,688 | 9,030042 | 10,667 |
| 1,263437 | 10,667 | 9,072708 | 10,667 |
| 1,306104 | 10,667 | 9,104708 | 21,333 |
| 1,828771 | 64,000 | 9,136708 | 10,667 |
| 4,666104 | 21,333 | 9,179375 | 10,667 |
| 8,731375 | 10,667 | 12,176708 | 21,333 |
| 8,774042 | 8,958 | | |

Gość zgłosił 16 okresów zerowych z miksera poza stanem master mute i 95 okresów przygotowanych podczas master mute. Liczniki decyzji sterownika oraz czasy ciszy w WAV mierzą różne zjawiska. Zebrane dane nie pozwalają przypisać każdego cichego fragmentu w WAV do konkretnej pustej kolejki, pominięcia okresu albo restartu.

`make -j1` zaliczył mikser, HDA/PCM (11 scenariuszy, 205 sprawdzeń), transport i topologię HDA (49 scenariuszy, 11 287 sprawdzeń), diagnostykę HDA (241), izolację BWS (180), zakończenie procesu (4063), Zasobnik (1468), Kalendarz (16 102), VirtIO (186) oraz oba loadery, GUI (2662) i BWS 89/90 (2156) z `test-dzwieku-repair`. Mikser, testy HDA, zakończenie procesu i Test dźwięku przeszły z ASan i UBSan. Produkcyjne ISO przeszło kontrolę Multiboot2, kontrolę fontów i audyt wydania z zerem ustaleń. Plik ELF z symbolami ma 12 885 552 bajty, a plik ELF w ISO ma 8 219 792 bajty. Zakres ładowania po wyrównaniu to 11 530 240 bajtów, a adres końcowy `0xBFF000` mieści się pod limitem `0xF00000`.

Test sygnału przeszedł. Ciągłość odtwarzania pozostaje niezaliczona. Przygotowanie do integracji aplikacji Ring 3 nadal wymaga ustalenia źródła przerw dostarczania IRQ i zweryfikowania odtwarzania pod obciążeniem.

## Analiza 19 przerw z poprzedniej próby

Pozycję w WAV przeliczono na przybliżony czas HPET przez interpolację między rozmiarami pliku przy kolejnych znacznikach faz. Przy przywróceniu mastera znacznik rozmiaru wskazywał 8,597104 s, a pierwszy sygnał po wyciszeniu był już przy 8,501313 s. Wznowienie DMA i buforowanie zapisu WAV zaburzają tę relację także między znacznikami. W tabeli czasy HPET służą do zgrubnego przypisania grup. Pojedynczego odcinka WAV nie można przypisać dokładnie do numeru okresu ani do zgłoszenia workera. Poprzednia próba rejestrowała LPIB i oczekujące zgłoszenia tylko przy odzyskaniach, a stan kolejek i decyzje pominięcia wyłącznie w sumach.

| Nr | WAV (s) | Cisza (ms) | Przybliżony HPET (s) | Klasyfikacja i dostępny ślad |
| ---: | ---: | ---: | ---: | --- |
| 1 | 0,085333 | 42,667 | 5,809–5,851 | Nierozstrzygnięta przy rozruchu audio. Wcześniejsza przerwa IRQ 21,656 ms przy 5,751–5,773 s. |
| 2 | 0,160000 | 10,667 | 5,883–5,894 | Nierozstrzygnięta przy rozruchu. Brak bliskiego odzyskania lub zapisanego długiego odstępu IRQ. |
| 3 | 1,146104 | 42,667 | 6,873–6,916 | Grupa związana czasowo z przerwą IRQ 132,645 ms i pierwszym odzyskaniem. Przybliżone okno wypada podczas przerwy IRQ. |
| 4 | 1,220771 | 10,688 | 6,948–6,959 | Ta sama grupa. Okno wypada pod koniec przerwy IRQ 132,645 ms. |
| 5 | 1,263437 | 10,667 | 6,991–7,002 | Ta sama grupa. Po pierwszym odzyskaniu zarejestrowano kolejną przerwę IRQ 52,079 ms. |
| 6 | 1,306104 | 10,667 | 7,034–7,045 | Ta sama grupa po odzyskaniu. Jest też przerwa IRQ 20,930 ms w przybliżonym oknie. |
| 7 | 1,828771 | 64,000 | 7,558–7,623 | Nierozstrzygnięta. Wcześniejsza przerwa IRQ 24,570 ms, bez kolejnego odzyskania. |
| 8 | 4,666104 | 21,333 | 10,410–10,431 | Nierozstrzygnięta. Faza 25% źródła A. Brak bliskiego zapisanego długiego odstępu IRQ lub odzyskania. |
| 9 | 8,731375 | 10,667 | 14,401–14,413 | Grupa związana czasowo z drugą przerwą IRQ 186,936 ms. Sygnał powrócił już po celowym mute. |
| 10 | 8,774042 | 8,958 | 14,450–14,460 | Ta sama grupa w przybliżonym oknie przerwy IRQ. |
| 11 | 8,806042 | 64,000 | 14,487–14,560 | Ta sama grupa przed drugim odzyskaniem w przybliżonym dopasowaniu. |
| 12 | 8,902042 | 53,333 | 14,597–14,658 | Ta sama grupa w pobliżu drugiego odzyskania. Wystąpiły też odstępy IRQ 26,228 i 26,007 ms. |
| 13 | 8,987375 | 10,667 | 14,695–14,708 | Po drugim odzyskaniu. Przybliżone okno przecina odstęp IRQ 66,149 ms. |
| 14 | 9,030042 | 10,667 | 14,744–14,757 | Po drugim odzyskaniu. W bliskim sąsiedztwie odstępy IRQ 66,149 i 22,198 ms. |
| 15 | 9,072708 | 10,667 | 14,793–14,806 | Po drugim odzyskaniu. W sąsiedztwie odstępy IRQ 28,234 i 70,898 ms. |
| 16 | 9,104708 | 21,333 | 14,830–14,855 | Po drugim odzyskaniu. Przybliżone okno przecina przerwę IRQ 70,898 ms. |
| 17 | 9,136708 | 10,667 | 14,867–14,879 | W pobliżu końca tej samej przerwy IRQ 70,898 ms. |
| 18 | 9,179375 | 10,667 | 14,916–14,928 | Nierozstrzygnięta. Wygasająca grupa po drugim odzyskaniu, bez wyraźnego odpowiadającego zdarzenia. |
| 19 | 12,176708 | 21,333 | 18,137–18,158 | Nierozstrzygnięta w fazie samego B, długo po zamknięciu A. Brak bliskiego odzyskania i zapisanego długiego odstępu IRQ. |

Przy odzyskaniach worker odczytał LPIB 23 444 i 16 628 bajtów. Czas od zgłoszenia do rozpoczęcia odzyskiwania wyniósł odpowiednio 0,547 i 0,318 ms. Kontroler zgłosił zero błędów FIFO/BDL. Timer działa w trybie okresowym LAPIC z kalibracją 250 Hz, bez przeładowywania przez procedurę IRQ. Wszystkie dostarczone ticki otrzymały EOI. Pomiar nie wykazał długiego maskowania w obserwowanych sekcjach. Liczniki HPET, moment zakończenia obsługi IRQ i próbki `/proc` nie rozstrzygają, czy opóźnienie powstało podczas wykonywania gościa, w QEMU, czy w VirtualBox.

### Puste kolejki i późne zapisy

Poprzednie 12 288 oznacza liczbę ramek miksowania, dla których co najmniej jedna **otwarta** kolejka była pusta. Licznik mógł wzrosnąć przy prawidłowym sygnale z drugiego źródła, przed pierwszym zapisem i po naturalnym końcu danych w otwartym strumieniu. Obejmował także wyciszenie, bo mute nie zatrzymuje konsumpcji próbek. Po zamknięciu strumienia jego kolejka znikała z pomiaru. Puste ramki źródłowe i dziewiętnaście cichych odcinków WAV są różnymi miarami.

Producent w PID0 próbował przesłać trzy porcje po 512 ramek raz na co najmniej 4 ms. Pod normalnym obciążeniem to wystarcza. Po dłuższej przerwie taka wizyta daje najwyżej 32 ms danych na źródło, a start lub wznowienie DMA może jednorazowo pobrać 4096 ramek (85,333 ms). Worker mógł działać na granicy syscalla i podczas kompozycji pomiędzy wizytami producenta. Kolizja try-locka była traktowana jak zakończona wizyta, więc ponowną próbę przesuwano o 4 ms nawet przy zerowym zapisie. Stary zapis nie pozwala ustalić, które z tych okoliczności wystąpiły przy danej przerwie WAV.

Siedem z jedenastu pominiętych okresów miało w chwili decyzji przygotowaną porcję, aktywny generator albo kolejkę z próbkami. To warunek ze starego kodu, więc sama flaga przygotowanej porcji nie gwarantowała niezerowych próbek. Każde pominięcie oznaczało, że okres był już przed kursorem albo bliżej niż 9216 bajtów od niego (48 ms w nominalnym tempie). To bezpieczne odrzucenie zapisu do uprzednio wyzerowanej strony. Poprzedni ślad nie zawiera numeru okresu, czasu decyzji, wartości LPIB ani stanu kolejki dla któregokolwiek z siedmiu przypadków. Nie można ustalić ich indywidualnego terminu ani przypisać ich do konkretnych wierszy WAV.

### Poprawki

Sterownik renderuje okres na podstawie podglądu kolejki. Usuwa próbki po ponownym odczycie LPIB i publikacji do bezpiecznej strony, a w czterech okresach startowych po potwierdzeniu `RUN=1`. Test z wymuszonym opóźnieniem między renderowaniem i odczytem LPIB odtwarza wcześniej możliwą utratę próbek. Test z odmową ustawienia RUN sprawdza zachowanie całej kolejki. Opublikowanych już próbek nie odtwarza się ponownie po STOP, bo ich odsłuchania nie można potwierdzić. Chroniona ścieżka STOP i ponowny odczyt LPIB zachowują własność pamięci DMA i ustawienia kodeka.

Worker otwartego pustego strumienia może poczekać na pierwsze próbki przed startem. Piąty okres jest publikowany po zebraniu pełnej porcji z oczekiwanych źródeł. Cztery podstawowe okresy i reguły bezpiecznego pomijania są bez zmian. Próbki przygotowane na piąty okres są wciąż dostępne, jeśli publikacja zostanie przerwana. Pojemność BDL to 170,667 ms, a gotowy sygnał obejmuje do 106,667 ms. Nie zwiększono pojemności obu buforów.

Producent testowy na wizycie może uzupełnić pełną kolejkę i ponawia próbę po kolizji przy następnym punkcie obsługi. Ma punkty w PID0 przed trzema istniejącymi wywołaniami workera. Zmiana dotyczy scenariusza z dwoma generatorami. Mikser nie dopisuje próbek, gdy kolejka jest pusta. Ramki głodzenia są teraz zliczane tylko przy publikacji i od pierwszego przyjętego zapisu danego źródła do oznaczenia skończonej produkcji lub zamknięcia. Mogą wciąż rosnąć podczas mute i podczas prawidłowego sygnału z innych źródeł.

Scenariusz testowy zapisuje 64 pierwsze decyzje o pominięciu z powodu własności DMA lub zbyt krótkiej kolejki. Ślad zawiera aktualny i poprzedni odczyt pozycji, czas oczekiwania workera, ostatni odstęp IRQ, zakres stanu kolejek, numer okresu i stan master mute. Dla okresu `p` granica zapisu wypada przy pozycji `p * 4096 - 9216` bajtów od początku bieżącej generacji DMA. Różnice między próbkami pozycji dają przedział przekroczenia tej granicy, jeśli poprzednia próbka jeszcze na zapis pozwalała. Dodatkowe znaczniki pracy producentów i próbkowanie rozmiaru WAV pomagają porównać osie czasu. Rozmiar WAV nadal zależy od buforowania zapisu QEMU.

## Próba końcowa po naprawie

Pojedynczy rozruch produkcyjnego ISO w QEMU TCG z jednym vCPU, wewnątrz VirtualBox, zapisano w `.hda-test/pcm-root-cause-20261120/`. Maszyna miała tylko kopię ISO z parametrem scenariusza w menu GRUB. Plik WAV zawiera 639 476 ramek stereo PCM16 przy 48 kHz (13,322417 s). W półsekundowych stabilnych oknach zachowano składowe 440 i 660 Hz oraz poziomy 4882,17/4882,51, 1220,30/4882,50 podczas niezależnej regulacji, 0/0 podczas mute, 1220,30/4882,50 po przywróceniu mastera i 0/4882,48 po zamknięciu pierwszego źródła. Po zamknięciu obu pozostało 137,271 ms ciszy.

| Pomiar | Poprzednio | Po naprawie |
| --- | ---: | ---: |
| Niezamierzone odcinki ciszy w WAV | 19 | 6 |
| Łączny czas niezamierzonej ciszy | 446,313 ms | 149,333 ms |
| Maks. odcinek niezamierzonej ciszy | 64,000 ms | 42,667 ms |
| Skoki kolejnych próbek >1000 w ustalonych aktywnych fazach | 27 | 12 |
| Starty DMA / odzyskania | 3 / 2 | 4 / 3 |
| Spóźnienia obsługi / maks. przerwa między odczytami LPIB | 13 / 184,663 ms | 15 / 418,172 ms |
| Średnie / maks. oczekiwanie zgłoszonego workera | 0,857 / 52,009 ms | 1,278 / 286,016 ms |
| Kolizje try-locka / maks. czas posiadania | 62 / 25,033 ms | 4 / 9,078 ms |
| Okresy pozostawione w ciszy dla zachowania własności DMA | 11 | 7 |
| Pominięte okresy ze wskazaniem dostępnego PCM | 7 według poprzedniego warunku | 7 z próbkami w kolejkach |
| Ramki z pustą kolejką | 12 288 w otwartych źródłach | 0 w źródłach oczekujących danych |
| Błędy FIFO / BDL | 0 / 0 | 0 / 0 |

Sterownik rzeczywiście odrzucił siedem spóźnionych zapisów z danymi w kolejkach i bez włączonego mute. Żaden z tych okresów nie był jeszcze przygotowany w buforze CPU. Przy każdej odmowie LPIB pozostawał przed początkiem okresu, ale poniżej wymaganego odstępu 9216 bajtów. Poprzedni odczyt w pętli pokazał już tę samą pozycję, więc chwila przekroczenia granicy nadal jest nieznana. Poniższe przypisanie do WAV korzysta z kursora, nominalnego tempa linku oraz rozmiaru WAV próbkowanego przez host. Wspólny układ długości (pięć pojedynczych okresów i dwa złączone) oraz zgodność kolejności wspierają przypisanie. Pozycje WAV i chwile decyzji sterownika pochodzą z różnych zegarów.

| HPET decyzji (s) | Okres | LPIB (B) | Odstęp przed okresem (B) | Min. / maks. ramek w kolejkach | Od powiadomienia (ms) | Prawdopodobny odcinek WAV (s / ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 6,599799 | 6 | 15 800 | 8776 | 3072 / 3072 | 30,714 | 0,128000 / 21,333 |
| 7,284421 | 8 | 24 276 | 8492 | 4096 / 4096 | 27,007 | 0,390833 / 21,333 |
| 8,150869 | 45 | 13 332 | 7148 | 4096 / 4096 | 35,029 | 1,052167 / 21,333 |
| 8,559461 | 70 | 23 332 | 1244 | 3072 / 3072 | 62,561 | 1,585500 / 42,667, wspólnie z 71 |
| 8,559467 | 71 | 23 332 | 5340 | 3072 / 3072 | 62,567 | 1,585500 / 42,667, wspólnie z 70 |
| 17,593298 | 105 | 29 368 | 7496 | 3072 / 3072 | 47,043 | 10,198479 / 21,333 |
| 19,610799 | 199 | 23 452 | 5220 | 4096 / 4096 | 61,480 | 12,203813 / 21,333 |

Ostatni odstęp IRQ przed pierwszym pominięciem wyniósł 21,139 ms. Przy sześciu pozostałych decyzjach ostatni odstęp wynosił 0,522–3,928 ms, mimo oczekiwania workera przez 27,007–62,567 ms. Przy okresie 8 ślad sekcji IRQ/schedulera pokrywa prawie całą przerwę 23,112 ms. W tej sekcji IF=0 upłynęło 23,077 ms według HPET. Pozostałe pominięcia potwierdzają spóźnione wykonanie gotowej pracy, ale nie wskazują konkretnego odcinka kodu, który je spowodował. Producent odwiedził kolejki 1647 razy bez odmowy zamka. Jego najdłuższa przerwa 426,275 ms objęła pierwsze odzyskanie. Przy żadnym z siedmiu pominięć kolejki nie były puste.

Trzy wznowienia nastąpiły po odstępach dostarczenia IRQ wynoszących 135,803, 276,897 i 158,812 ms. Pierwszy worker doszedł do odzyskania po 286,098 ms od najstarszego zgłoszenia, podczas kilku kolejnych wydłużonych odstępów IRQ. Pozostałe rozpoczęły się po 1,061 i 0,518 ms. Watchdog zrobił cztery kontrolne odczyty i raz bezpiecznie odroczył STOP. Wszystkie 3284 dostarczone ticki otrzymały EOI, maksymalnie po 2,566 ms. Najdłuższa zmierzona sekcja IF=0 (23,077 ms) wystąpiła już po pierwszej sekwencji długich odstępów IRQ. Obejmuje ona czas upływający, więc nawet tam pomiar nie rozdziela pracy jądra od przerwy w wykonaniu vCPU. Ślad Linuksa przy trzech wznowieniach pokazuje oczekiwanie TCG w kolejce do 0,611 ms i przerwy próbkowania do 276,663 ms. Z tego nie da się rozdzielić wykonywania kodu gościa, wewnętrznej pracy QEMU i działania VirtualBox. Przyczyna najdłuższych odstępów pozostaje nierozstrzygnięta.

Testy hostowe przeszły w `make -j1`: mikser, HDA PCM (13 scenariuszy, 232 sprawdzenia), transport/topologia HDA (49 scenariuszy, 11 287), diagnostyka HDA (241), izolacja HDA BWS (180), zakończenie procesu (4063), Zasobnik (1468), Kalendarz (16 102), Menedżer zadań (1802) i `test-dzwieku-repair` (oba loadery, GUI 2662, BWS 89/90 2156). Uruchomione sanitizery ASan i UBSan nie zgłosiły błędów. ISO przeszło kontrolę Multiboot2, kontrolę fontów i audyt wydania (624 pliki, 0 ustaleń). ELF w produkcyjnym ISO ma 8 219 792 bajty, a plik ELF z symbolami ma 12 893 512 bajtów. Wyrównany zakres pamięci jądra wynosi 11 538 432 bajty od `0x100000`, z końcem `0xC01000` i limitem `0xF00000`.

Sygnał i odzyskiwanie przeszły sprawdzenia funkcjonalne. Łączna cisza spadła o 66,5%, a ciągłość pozostaje niezaliczona. Dalszą integrację Ring 3 warto uzależnić od powtórzenia tego samego scenariusza w QEMU uruchomionym bezpośrednio na fizycznym Linuksie, z jednym vCPU i tym samym obciążeniem pulpitu. Porównanie śladów krótkich okien dobrego wykonania oraz momentu dostarczenia IRQ powinno rozdzielić opóźnienia gotowego workera od przerw w wykonaniu gościa przed kolejną zmianą produkcyjnego timera lub buforów.
