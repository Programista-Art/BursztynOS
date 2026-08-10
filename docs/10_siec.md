# 10. Sieć w Bursztyn OS 🌐

Niniejszy rozdział dokumentuje ewolucję systemu Bursztyn OS od odizolowanego Jądra do w pełni responsywnego, dwukierunkowego systemu sieciowego opartego na natywnym stosie TCP/IP, działającego w doskonałej harmonii ze spolonizowanym, graficznym interfejsem użytkownika (GUI).
I. Komunikacja Sieciowa ICMP i Powłoka po Polsku 🇵🇱

Pierwszym etapem otwarcia systemu na świat było zbudowanie fundamentów adresacji, nawiązanie podstawowego dialogu z siecią lokalną oraz przygotowanie interfejsu dla polskiego użytkownika.

## 🏆 Zrealizowane funkcje:

1. Pełnoprawny Protokół ARP i Tabela Cache (ARP Cache)
Zastąpiono dotychczasowe wysyłanie pakietów "w ciemno" (Broadcast) inteligentnym mechanizmem rozwiązywania adresów fizycznych:

* Jądro potrafi aktywnie wysyłać zapytania ARP ("Kto ma ten adres IP?").

* Zbudowano w pamięci RAM dynamiczną tablicę podręczną, która w tle uczy się adresów MAC urządzeń w sieci i buforuje je, eliminując obciążające powtarzanie zapytań przy kolejnych operacjach.

## 2. Działający obustronnie PING / PONG (ICMP)
Rozwiązano problem blokowania włókien i zsynchronizowano czas oczekiwania na odpowiedź pakietu z karty sieciowej Intel PRO/1000 (E1000):

* Po wydaniu polecenia ping z poziomu Powłoki, Jądro w bezpieczny sposób wstrzymuje na ułamek sekundy przetwarzanie i aktywnie nasłuchuje portów (polling).

* W efekcie Bursztyn OS z sukcesem odbiera pakiety ICMP Echo Reply od zewnętrznych routerów, potwierdzając pełną sprawność Warstwy 3 (IPv4).

## 3. Powłoka Bursztynowa (GUI) – Całkowita Polonizacja (UTF-8)
Wykorzystując autorski silnik renderowania Menedżera Okien oraz nową, proporcjonalną czcionkę 16x16 pikseli, przeprowadzono pełną lokalizację interfejsu Powłoki (działającej jako niezależny proces w Ring 3):

* Nowa nazwa i zachęta: Terminal zyskał oficjalną nazwę Powłoka Bursztynowa, a znak zachęty zmieniono na intuicyjne powłoka>.

* Polskie ogonki: Wszystkie logi, opisy poleceń w menu pomoc, instrukcje obsługi oraz komunikaty o błędach posługują się teraz poprawną polszczyzną, natywnie renderując znaki kodowania UTF-8 (ą, ć, ę, ł, ń, ó, ś, ź, ż).

## II. Odkrywanie Internetu - UDP, DHCP i DNS 🌍📡

Drugi etap to gigantyczny skok naprzód w rozwoju stosu sieciowego. Wyjście poza proste pakiety ICMP i warstwę trzecią (IP) pozwoliło systemowi wkroczyć z impetem w świat portów (Warstwa 4 - UDP) oraz zaawansowanych protokołów aplikacyjnych (Warstwa 7). Bursztyn OS zyskał pełną niezależność sieciową.

## 🏆 Zrealizowane funkcje:

1. Protokół UDP i Automatyczna Konfiguracja (Klient DHCP)
System nie wymaga już wpisywania adresów IP na sztywno w kodzie Jądra:

* Wdrożono lekki i szybki protokół UDP (User Datagram Protocol), który operuje na numerach portów (źródłowych i docelowych).

* Napisano od podstaw zintegrowanego klienta DHCP. Przy starcie, Bursztyn OS posiada adres 0.0.0.0 i wysyła w sieć zapytanie UDP Broadcast (Discover) na porcie 67.

* System poprawnie przechodzi przez proces negocjacji DORA, odbierając Ofertę (Offer) od routera, prosząc o jej przydział (Request) i ostatecznie zapisując swój nowy, w pełni zautomatyzowany adres IP (np. 10.0.2.15), a także adres Bramy Domyślnej (Routera).

## 2. Klient DNS (Domain Name System)
Terminal przestał operować wyłącznie na adresach numerycznych.

* Wdrożono nowe Wywołanie Systemowe (BWS-012), które pozwala aplikacjom w Ring 3 prosić Jądro o rozwiązanie nazwy domenowej.

* Po wpisaniu np. ping google.com, system w locie tłumaczy ten tekst na format binarny DNS, pakuje go w protokół UDP i wysyła na port 53 do publicznego serwera.

* Parser sieciowy odbiera odpowiedź, dekoduje wskaźniki kompresji DNS i wyciąga prawdziwy adres IP, zwracając go z powrotem do Powłoki.

## 3. Inteligentne Trasowanie (Routing)

* System potrafi matematycznie ocenić (przy pomocy maski podsieci), czy docelowy adres IP znajduje się w tej samej sieci lokalnej, czy leży w zewnętrznym Internecie.

* Jeśli adres jest zewnętrzny, Jądro pyta protokołem ARP o adres fizyczny Bramy Domyślnej (Routera) i to do niej adresuje ramkę Ethernetową z danymi.

## III. Protokół TCP i Klient HTTP 🏆

Ostatni krok to implementacja niezawodnej komunikacji strumieniowej oraz pobierania plików z serwerów WWW.

## 🏆 Zrealizowane funkcje:

## 1. Maszyna Stanów TCP (Transmission Control Protocol)
Zbudowano w pełni funkcjonalny mechanizm nawiązywania i kontroli połączeń:

* Implementacja procedury Three-way handshake (SYN, SYN-ACK, ACK). System potrafi "uścisnąć dłoń" z serwerem, aby uzgodnić parametry okna transmisji.

* Obsługa numerów sekwencyjnych (SEQ) i potwierdzeń (ACK), co gwarantuje spójność strumienia danych.

* Generowanie "Pseudo-nagłówka" i zaawansowanych sum kontrolnych TCP.

* Eleganckie rozłączanie sesji po zakończeniu transmisji za pomocą flagi FIN.

## 2. Klient HTTP wbudowany w Jądro
Na fundamentach TCP zaimplementowano parser Warstwy Aplikacji:

* Funkcja systemowa formuje poprawne, tekstowe żądanie sieciowe (np. GET [ścieżka] HTTP/1.0\r\nHost: [domena]\r\n\r\n).

* Parser potrafi samodzielnie odciąć nagłówki odpowiedzi serwera WWW, pozostawiając w buforze wyłącznie czyste dane pobieranego pliku.

## 3. Brama Wywołań Systemowych (BWS-013) - Zapis na dysk AHCI
Połączono potęgę stosu sieciowego (Intel E1000) ze sterownikiem dysku twardego (AHCI):

* Wywołanie BWS-013 alokuje duży bufor w bezpiecznej przestrzeni Ring 0 na czas pobierania, weryfikuje uprawnienia PZB (PRAWO_SIEC + PRAWO_PLIKI_ZAPISZ), a po udanym transferze automatycznie tworzy na dysku plik BSP i utrwala w nim pobrane z sieci dane.

## 4. Graficzny Menedżer Pobierania (Powłoka Bursztynowa)
Wpisanie komendy pobierz w oknie graficznego Terminala uruchamia pełną reakcję łańcuchową:

* Zapytanie DNS (UDP) tłumaczy słowo na adres IP.

* Jądro (BWS-013) zestawia sesję TCP i protokołem HTTP pobiera plik do pamięci.

* Plik jest fizycznie zapisywany na dysku AHCI, gotowy do odczytania komendą czytaj.

Przykład użycia: powłoka> pobierz example.com / /test.html