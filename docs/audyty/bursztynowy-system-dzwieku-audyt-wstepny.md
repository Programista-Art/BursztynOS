# Wstępny audyt odtwarzania audio

Stan źródeł: `5ec2065` na `feature/bursztynowy-zasobnik`. To dokument rozpoznawczy, nie opis zaimplementowanego systemu PCM. Commit Zasobnika jest obecny na tej gałęzi, lecz nie jest przodkiem lokalnego `main` ani `test`. Nie sprawdzano zdalnego stanu przez sieć. Skrót BSD występuje w dokumentacji jako nazwa rodziny systemów; w przeszukanych źródłach projektu nie znaleziono wewnętrznego identyfikatora tej nazwy. Ze względu na możliwą niejednoznaczność nie wprowadzono go do kodu.

## Istniejąca ścieżka

* `sterowniki/dzwiek/hda_transport.cpp` obsługuje osobne pierścienie DMA CORB/RIRB, ich inicjalizację i transakcje z terminami; status RIRB jest potwierdzany przez zapis W1C. `INTCTL` pozostaje wyłączony: status RIRB nie oznacza dostarczania IRQ.
* Diagnostyka odkrywa topologię kodeków i trasy; `hda.cpp` wybiera jedną trasę wyjściową, preferując m.in. głośnik Intel. Osobny fallback konfiguracji dotyczy potwierdzonego kontrolera QEMU 8086:2668. Trasy i istniejącej inicjalizacji nie należy zastępować założeniami o jednym kodeku.
* Wyjście ma stały format 48 kHz, stereo, PCM16. PMM przydziela stronę BDL i 48 osobnych stron DMA po 4096 bajtów. Generator jądra wypełnia próbki trójkątne, buduje od 2 do 48 wpisów BDL i odtwarza w pętli jeden całkowity okres sygnału. Wpisy mają IOC=0. To nie jest kolejka niezależnych okresów PCM ani transport ciągłych danych aplikacji.
* `hda_test_ton` zatrzymuje istniejący stream, resetuje i programuje deskryptor i uruchamia DMA. Jest asynchroniczny, ale kolejny ton zastępuje poprzedni. HPET określa termin zakończenia. `hda_obsluz_okresowo` sprawdza również błędy FIFO/deskryptora; try-lock sterownika nie czeka na właściciela.
* Przerwanie timera tylko ustawia flagę pracy odroczonej. Obsługa następuje poza IRQ w PID0 lub na granicy syscalla. W przeanalizowanym kodzie nie ma gwarancji maksymalnego opóźnienia takiej obsługi ani mechanizmu odzyskiwania przegapionych okresów DMA; nie można na tej podstawie zadeklarować niezawodnej ciągłej kolejki ani opóźnienia audio.
* Test dźwięku używa BWS 89/90; starsza ścieżka syscalla także wywołuje generator tonu. Obie należy skoordynować z przyszłym jedynym właścicielem urządzenia.

## Uprawnienia, procesy i głośność

`jadro/bws/bws_api.h` rezerwuje 107–109 dla Zasobnika i wskazuje 110 jako pierwszy wolny numer w tym stanie źródeł. Operacje stanu i sterowania Zasobnika są ograniczone do autoryzowanego Menedżera Okien. Sterownik preferuje wzmacniacz kodeka z istniejącym zachowawczym punktem odniesienia; w razie jego braku generator stosuje programowe skalowanie. Zmiana master mute w tym trybie zatrzymuje już wygenerowany ton. Nowy ciągły PCM musi korzystać z tego samego stanu i stosować fallback dokładnie raz.

Scheduler udostępnia `scheduler_generacja_procesu(pid)`. Normalne zakończenie i odroczone sprzątanie schedulera wywołują `bws_nowoczesne_posprzataj_proces`; jest to kandydat na punkt sprzątania strumieni, po zweryfikowaniu kolejności blokad i wszystkich ścieżek błędów. Istnieje kopiowanie Ring 3 przez `skopiuj_z_przestrzeni_uzytkownika`, z kontrolą zakresu i stron; częściowa kopia do bufora jądra przy błędzie nie może zostać opublikowana jako przyjęte próbki. `kmalloc`/`kfree` obsługują istniejące zasoby IPC; strony DMA pochodzą z PMM, nie z pamięci aplikacji.

IPC i SHM istnieją (kolejka endpointu 16 wiadomości, globalny limit SHM 8 MiB), ale nie znaleziono gotowego interfejsu terminowego dostarczania okresów do HDA przez zaufaną usługę Ring 3. Samo istnienie IPC nie dowodzi bezpieczeństwa ani terminowości takiej usługi. Ograniczony mikser jądra jest uzasadnionym kierunkiem pośrednim, pod warunkiem oddzielenia go od ABI i sterownika oraz rozwiązania terminowej obsługi DMA.

## Nierozstrzygnięte przed wdrożeniem

Potrzebne są projekt i testy własności okresów DMA przy zawinięciu LPIB, wykrywaniu opóźnień większych niż jeden obrót oraz bezpiecznym wypełnianiu ciszą; sama pozycja modulo nie wystarcza do policzenia utraconych obrotów. Należy ustalić model synchronizacji z zakończeniem procesu, kolejność blokad, ograniczenia buforów i semantykę częściowego zapisu oraz skoordynować testy tonu z jedynym silnikiem. Nie ma jeszcze nowego ABI, miksera, aplikacji PCM, pomiarów ani walidacji QEMU lub sprzętu fizycznego.
