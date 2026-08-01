# 10. Sieć w Bursztyn OS 🌐

Niniejszy rozdział dokumentuje ewolucję systemu Bursztyn OS od odizolowanego jądra do w pełni responsywnego, dwukierunkowego systemu sieciowego opartego na autorskim stosie TCP/IP, działającego w harmonii ze spolonizowanym, graficznym interfejsem użytkownika.

# I. Komunikacja Sieciowa ICMP i Powłoka po Polsku 🇵🇱

Pierwszym etapem otwarcia systemu na świat było zbudowanie fundamentów adresacji i nawiązanie podstawowego dialogu z siecią lokalną, a także przygotowanie interfejsu dla polskiego użytkownika.

# 🏆 Zrealizowane funkcje:

1. Pełnoprawny Protokół ARP i Tabela Cache (ARP Cache)
Zastąpiliśmy dotychczasowe wysyłanie pakietów "w ciemno" (Broadcast) inteligentnym mechanizmem rozwiązywania adresów fizycznych:

1. Jądro potrafi teraz aktywnie wysyłać zapytania ARP ("Kto ma ten adres IP?").

Zbudowałęm w pamięci RAM dynamiczną tablicę podręczną, która uczy się adresów MAC urządzeń w sieci w tle i zapisuje je, eliminując powtarzanie zapytań przy kolejnych operacjach.

# 2. Działający obustronnie PING / PONG (ICMP)
Rozwiązaliśmy problem blokowania wątków i zsynchronizowaliśmy czas oczekiwania na odpowiedź pakietu z karty sieciowej Intel E1000:

1. Po wydaniu polecenia ping z poziomu systemu, Jądro w bezpieczny sposób wstrzymuje na ułamek sekundy przetwarzanie i aktywnie nasłuchuje portów (polling).

1. W efekcie Bursztyn OS z sukcesem otrzymuje pakiety ICMP Echo Reply od zewnętrznego routera, potwierdzając pełną sprawność warstwy 3 (IPv4).

# 3. Powłoka Bursztynowa – Całkowita Polonizacja (UTF-8)
Wykorzystując zaawansowany dekoder znaków diakrytycznych oraz smukłą czcionkę 8x16, przeprowadziliśmy pełną lokalizację interfejsu powłoki tekstowej w Ring 3:

1. Nowa nazwa i zachęta: Terminal zyskał oficjalną nazwę Powłoka Bursztynowa, a znak zachęty zmieniono na intuicyjne powłoka> .

1. Polskie ogonki: Wszystkie komunikaty powitania, opisy poleceń w menu pomoc, instrukcje obsługi oraz komunikaty o błędach posługują się teraz poprawną polszczyzną (używając znaków typu ą, ć, ę, ł, ń, ó, ś, ź, ż).

# II. Odkrywanie Internetu - UDP, DHCP i DNS 🌍📡

Drugi etap to gigantyczny skok naprzód w rozwoju stosu sieciowego. Wyszliśmy poza proste pakiety ICMP i warstwę trzecią (IP), wkraczając z impetem w świat portów (Warstwa 4 - UDP) oraz zaawansowanych protokołów aplikacyjnych (Warstwa 7). Nasz system zyskał pełną niezależność sieciową!

## 🏆 Zrealizowane funkcje:

1. Protokół UDP i Automatyczna Konfiguracja (Klient DHCP)
Koniec z wpisywaniem adresów IP na sztywno w kodzie jądra!

Wdrożyłem lekki i szybki protokół UDP (User Datagram Protocol), który pozwala na komunikację z użyciem numerów portów (źródłowych i docelowych).

Napisaliśmy od podstaw klienta DHCP. Przy starcie, Bursztyn OS ma adres 0.0.0.0 i wysyła w sieć głośne zapytanie UDP Broadcast (Discover) na porcie 67.

System poprawnie przechodzi przez proces DORA, odbierając Ofertę (Offer) od routera, prosząc o jej przydział (Request) i ostatecznie zapisując swój nowy, w pełni zautomatyzowany adres IP (np. 10.0.2.15), a także adres Bramy Domyślnej (Routera).

## 2. Klient DNS (Domain Name System)
Nasz terminal przestał operować wyłącznie na "suchych" liczbach. Nauczyliśmy Bursztyn OS mówić ludzkim językiem!

1. Nowe Wywołanie Systemowe (BWS nr 12), które pozwala aplikacjom w Ring 3 prosić Jądro o rozwiązanie nazwy domenowej.

1. Po wpisaniu np. ping google.com, system w locie tłumaczy ten tekst na specjalny format binarny DNS, pakuje go w protokół UDP i wysyła na port 53 do publicznego serwera Google (8.8.8.8).

1. Parser sieciowy potrafi odebrać odpowiedź, zdekodować wskaźniki kompresji DNS i wyciągnąć prawdziwy adres IP, przekazując go z powrotem do Powłoki Bursztynowej.

## 3. Inteligentne Trasowanie (Routing)

1. System potrafi teraz matematycznie ocenić (przy pomocy maski podsieci), czy docelowy adres IP znajduje się w tej samej sieci, czy też leży gdzieś daleko w Internecie.

1. Jeśli adres jest zewnętrzny, Jądro nie pyta bez sensu całej sieci o jego MAC. Zamiast tego pyta protokołem ARP o adres fizyczny Bramy Domyślnej i to do niej pakuje "kopertę" Ethernetową z danymi do wysłania w świat!

## III. Protokół TCP i Klient HTTP 🏆

Dodałem pobieranie plików z serwerów WWW.

🏆 Zrealizowane funkcje:

1. Maszyna Stanów TCP (Transmission Control Protocol)
To serce niezawodnej komunikacji w Internecie.

1. Zbudowałęm w pełni funkcjonalny mechanizm Three-way handshake (SYN, SYN-ACK, ACK). System potrafi "uścisnąć dłoń" z serwerem, aby uzgodnić parametry transmisji.

1. Zaimplementowałęm obsługę numerów sekwencyjnych (SEQ) i potwierdzeń (ACK), co pozwala upewnić się, że żadne dane nie zginęły po drodze.

1. Stworzyłem tzw. "Pseudo-nagłówek" i mechanizm generowania skomplikowanych sum kontrolnych specyficznych dla TCP.

1. Dodałęm eleganckie rozłączanie sesji za pomocą flagi FIN.

## 2. Klient HTTP wbudowany w Jądro
Na fundamentach TCP zbudowałem parser Warstwy Aplikacji.

1. Funkcja systemowa formuje poprawne, tekstowe żądanie sieciowe: GET [ścieżka] HTTP/1.0\r\nHost: [domena].

1. System jest na tyle inteligentny, że przy odbiorze potrafi samodzielnie odciąć niewidoczne nagłówki serwera (szukając podwójnego znaku nowej linii \r\n\r\n), zostawiając nam samo "mięsko" pobieranego pliku!

## 3. Brama Wywołań Systemowych (BWS 13) - Menedżer Pobierania
Połączyliśmy potęgę naszej karty sieciowej (Intel E1000) ze sterownikiem dysku twardego (AHCI).

Zdefiniowaliśmy nowy wektor w Jądrze, który alokuje duży bufor przestrzeni Ring 0 na czas pobierania, a po udanym transferze automatycznie tworzy na dysku plik w formacie BSP64 i napełnia go pobranymi danymi.

4. Komenda pobierz w Powłoce Bursztynowej
Wpisanie komendy pobierz [domena] [sciezka] [zapisz_jako] uruchamia całą reakcję łańcuchową:

1. Zapytanie DNS (UDP) tłumaczy słowo na adres IP.

1. Jądro (BWS 13) zestawia sesję TCP i protokołem HTTP ciągnie plik do pamięci.

1. Plik jest fizycznie zapisywany na dysku, gotowy do odczytania komendą czytaj.

Przykład użycia: pobierz example.com / /test.html
