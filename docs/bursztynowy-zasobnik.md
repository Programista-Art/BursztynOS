# Bursztynowy Zasobnik

Obszar stanu systemu na pasku zadań, tuż przed zegarem. Gałąź `feature/bursztynowy-zasobnik` (od `97bffab`, zatwierdzony Kalendarz).

Zasobnik jest częścią zaufanego Menedżera Okien (WM). Nie ma osobnego procesu, kompozytora ani frameworka GUI: rysuje się jak zegar paska (własny bufor obszaru, publikacja tym samym kanałem), a panele to `PowierzchniaPopup` obsługiwane przez `ZarzadcaPopupow` i `PlikiUI::ObslugaPopup` – ten sam mechanizm co menu Start, menu przepełnienia i kalendarz paska.

## Architektura

| Warstwa | Plik | Rola |
|---|---|---|
| ABI | `jadro/bws/zasobnik_api.h` | BWS 107–109, struktury V1, wyniki `BWS_ZAS_*` |
| Jądro – logika | `jadro/bws/zasobnik_jadro.h` | autoryzacja, budowa odpowiedzi (wspólne z testami hosta) |
| Jądro – powiadomienia | `jadro/bws/powiadomienia.h`, `powiadomienia_jadra.h` | ograniczona kolejka, limity, czyszczenie tekstu; API `powiadomienie_systemowe()` |
| Jądro – obsługa | `jadro/bws/bws_nowoczesne.cpp` | `zasobnik_stan`, `zasobnik_polecenie`, `powiadomienia_bws` |
| Dźwięk | `sterowniki/dzwiek/hda_glosnosc.h`, `hda.cpp` | głośność systemowa na wzmacniaczu kodeka |
| Sieć | `jadro/siec/siec.cpp` | `siec_pobierz_konfiguracje_ipv4()` (tylko odczyt) |
| WM – rdzeń | `jadro/gui/zasobnik.h` | rejestr, układ, glify, powierzchnia, model stanu, wspólne położenie paneli |
| WM – panele | `jadro/gui/zasobnik_panele.h` | panele i `Zasobnik::Kontroler` |
| WM – integracja | `programy/menedzer_okien/menedzer_okien.cpp` | konfiguracja przy zegarze, routing zdarzeń, dymki, odświeżanie |

### Rejestr i cykl życia elementów

- Element ma stałą tożsamość `Zasobnik::Id`, ikonę, priorytet (kto zostaje przy braku miejsca), kolejność (od lewej), widoczność, dostępność, bity stanu wizualnego i dymek (≤ 96 B, cięty na granicy znaku UTF-8).
- API: `zarejestruj`, `aktualizuj` (zwraca, czy zmienił się wygląd – sam dymek nie wymaga przerysowania), `ustaw_widocznosc`, `usun`. Duplikat tożsamości i pełny rejestr (8) są odrzucane.
- Panele, dymki i kotwice odwołują się do elementu **po Id**, nigdy wskaźnikiem. Zniknięcie urządzenia (np. nośnika) usuwa element; otwarty panel tego elementu zostaje zamknięty.
- Rejestrowane zawsze: Szybkie ustawienia, Powiadomienia, Dźwięk, Sieć. Nośniki wymienne – tylko gdy jądro je raportuje. Bateria i wskaźnik CPU/RAM nie są rejestrowane (patrz niżej).
- Rejestr jest wewnętrzny dla WM. Aplikacje Ring 3 nie mają żadnego API do ikon zasobnika.

### Układ i przepełnienie

- Sloty 32 × (pasek − 8) DIP, odstęp 2 DIP, margines 4 DIP; prawa krawędź obszaru = lewa krawędź zegara.
- Przyciski okien kończą się przed zasobnikiem (`metryki_paska().koniec_x`). Zasobnik dostaje szerokość paska po zarezerwowaniu przycisku Start, jednego przycisku okna i przycisku przepełnienia.
- Przy braku miejsca zostają elementy o najwyższym priorytecie (Szybkie ustawienia, potem Powiadomienia, Dźwięk, Sieć). Zegar nigdy nie jest zasłaniany.
- Ukryte moduły są dostępne z panelu Szybkie ustawienia (przyciski Sieć, Powiadomienia). Panel ukrytego elementu kotwiczy się przy prawej krawędzi zasobnika.

### Rysowanie i wydajność

- Ikony są rysowane wektorowo w liczbach całkowitych (Ring 3 bez SSE) z pokryciem 3 × 3, w kolorach motywu. Nie ma plików PNG, a ikony są ostre w każdej skali.
- Stany: przekreślenie (brak backendu/urządzenia), fale głośnika 1–3 według poziomu, „×” akcentu przy wyciszeniu, akcentowa kropka (nieprzeczytane / sieć bez łącza lub IPv4), podświetlenie najechania, tło i pasek akcentu aktywnego panelu.
- `Powierzchnia` przerysowuje obszar zasobnika tylko przy zmianie sygnatury (generacja rejestru, najechanie, aktywny panel, skala, kolory motywu, układ). Bufor rośnie tylko przy większym obszarze.
  - Brak alokacji w stanie ustalonym i przy ruchu myszy (test hosta).
  - Pasek nie jest przerysowywany przy zmianach audio/sieci; wyjątek: zmiana szerokości zasobnika (np. pojawił się nośnik).
- Odczyt stanu jądra (BWS 107, ~0,6 KiB) najwyżej co 2 s, co 1 s przy otwartym panelu. Wywoływany z pętli zdarzeń – bez osobnej pętli odpytywania, bez aktywnego czekania.
- Lista powiadomień (BWS 109) jest pobierana tylko przy zmianie generacji.
- Etykiety mają stały bufor i nie unieważniają się przy tym samym tekście, więc odświeżanie co sekundę nie mierzy tekstu ani nie alokuje.

### Zdarzenia, fokus i zegar

| Zdarzenie | Zachowanie |
|---|---|
| Klik ikony | Otwiera jej panel. Zamyka przy tym menu Start, kalendarz paska i menu przepełnienia (hak `przed_otwarciem`). |
| Klik tej samej ikony przy otwartym panelu | Pochłonięty; panel zamknięty. |
| Klik poza panelem | Panel zamknięty, klik przechodzi dalej: inna ikona otwiera swój panel, zegar – kalendarz, Start – menu, przycisk okna – okno. Jednym kliknięciem. |
| Escape | Zamyka aktywny panel. |
| BLUR, zmiana ekranu | Zamykają panel. |
| Zmiana motywu | Odświeża panele i powierzchnię. |
| Fokus | Fokus klawiatury jest w panelu (suwak głośności, przełącznik). Po zamknięciu wraca zwykły routing pulpitu. Sam pasek nie ma nawigacji klawiaturą (tak było już wcześniej). |
| TIMER | Nigdy nie kończy iteracji pętli WM (`popup_konczy_obsluge`), więc zegar paska aktualizuje się przy otwartym panelu. To ta sama reguła co przy kalendarzu i menu przepełnienia. |
| Dymki | Ten sam automat `PasekZadan::Dymek` (opóźnienie 600 ms) i ten sam dymek systemowy BWS 56 co przyciski okien. Klucz `0x5A53…|Id` jest spoza zakresu `window_id`. Przy otwartym popupie WM dymków nie ma. |

Wspólne położenie paneli: `Zasobnik::polozenie_panelu()` – nad paskiem, prawa krawędź przy prawej krawędzi kotwicy, w granicach ekranu; na niskim ekranie panel jest krótszy. Kalendarz paska używa tej samej funkcji (`KalendarzPaska::polozenie`). Wysokość panelu zależy od widocznych wierszy (np. liczby powiadomień), a zmiana wysokości przesuwa panel.

## Moduły

| Moduł | Stan | Źródło danych / operacja |
|---|---|---|
| Szybkie ustawienia | działa | głośność i wyciszenie (wspólne z panelem Głośność), motyw jasny/ciemny (istniejące `BWS_MOTYW_POLECENIE`/`MOTYW_CMD_USTAW` Ustawień), skróty Sieć/Powiadomienia, Ekran i Ustawienia (`uruchom_lub_aktywuj`, jedna instancja). Jasność ekranu: „niedostępna” (system nie ma sterownika podświetlenia; nie ma symulacji nakładką). Brak przełączników sieci (system nie ma mechanizmów konfiguracji). |
| Głośność | działa (QEMU: zmierzone) | wzmacniacz wyjściowy kodeka HDA na aktywnej trasie |
| Sieć | działa | rejestr `NetworkDevice` (te same liczniki co BWS 105) + konfiguracja IPv4 z DHCP |
| Powiadomienia | działa | kolejka jądra, prawdziwe zdarzenia sterowników |
| Nośniki wymienne | tylko informacja; w praktyce nieaktywny | rejestr blokowy (`removable`). Dziś żaden sterownik nie raportuje nośnika wymiennego (brak USB Mass Storage). Bezpieczne usuwanie: „niedostępne”, bez klikalnej akcji. |
| Bateria | wyłączony | ACPI w jądrze nie ma interpretera AML (`_BST`/`_BIF`/`_PSR`), więc nie da się wiarygodnie odczytać baterii. Element nie jest rejestrowany, flaga `BWS_ZAS_BATERIA_OBSLUGA` = 0. |
| CPU/RAM | wyłączony | zbiorcze dane są dostępne tylko przez BWS 103/105 z prawem Menedżera zadań (`PRAWO_ZARZADZANIE_PROCESAMI`). WM go nie ma i nie dostał – PZB bez zmian. |

### Dźwięk – rzeczywisty backend

Sterownik HDA nie miał miksera. Odtwarzanie to generator tonu jądra (BWS 27/90), a trasa ustawiała stały, konserwatywny gain. Teraz:

- **Kontrolka głośności:** pierwszy regulowany wzmacniacz wyjściowy aktywnej trasy, licząc od konwertera (Amp Caps NumSteps > 0); dla potwierdzonego kodeka QEMU – DAC NID 2.
- **100 %** = gain, który sterownik i tak zapisywał (−18 dB z `hda_konserwatywne_wzmocnienie` albo 0x4A dla QEMU). Domyślna głośność się nie zmienia, a zapas bezpieczeństwa głośników zostaje.
- **0–100 %** mapuje się liniowo na kroki 0..gain_ref (kroki HDA są w dB).
- **Wyciszenie** = bit Mute (tylko z „Mute Capable”); odciszenie przywraca poziom. Poziom 0 z „Mute Capable” też ustawia Mute.
- **Ponowna konfiguracja trasy** (np. przed tonem) zapisuje bieżący poziom użytkownika.
- **Brak sprzętu:** `SET_AMP_GAIN_MUTE` przez transport; zajęty transport = `BWS_ZAS_ZAJETE` i stan bez zmian. Nie ma „sukcesu” bez zapisu do sprzętu.
- **Trasa bez regulowanego wzmacniacza:** programowe wzmocnienie w jedynym miejscu tworzenia próbek PCM (`probka_trojkatna`), z nasyceniem do int16. Wyciszenie zatrzymuje bieżący strumień, bo bufor jest już wygenerowany. Przy wzmacniaczu skalowanie programowe jest tożsamością (bez podwójnego tłumienia).
- **Synchronizacja:** stan jest w jądrze (`generacja`). WM czyta go co 1–2 s, a po każdym poleceniu natychmiast, więc suwaki, ikona i dymek pokazują wartość z jądra.

### Sieć – źródło danych

- **Interfejs, sterownik, łącze, MTU, liczniki:**
  - `UrzadzeniaMigawka::siec()`, czyli ta sama funkcja i te same kanoniczne liczniki `NetDeviceStats` co BWS 105;
  - RX liczony raz w `net_receive_frame`, TX po zakończeniu deskryptora – bez podwójnego liczenia ramek;
  - interfejs główny stosu, a bez niego pierwszy zarejestrowany.
- **Łącze:** z `get_link_state()`. `Unknown` = „Nieznany” (nie „połączono”).
- **IPv4, maska, brama, DNS:** tylko po DHCP ACK (`konfiguracja_ipv4_gotowa`). Domyślne 255.255.255.0 i 8.8.8.8 sprzed DHCP nie są raportowane jako konfiguracja. Brak wartości = „Niedostępne”.
- **Tempo** liczy WM z różnicy dwóch migawek i `czas_ns` (HPET), tylko dla tego samego interfejsu.
- **Bez wskaźnika „Internet”:** system nie wykonuje wiarygodnego testu łączności.

### Powiadomienia – model bezpieczeństwa

- **Kolejka:** jedna, w jądrze, 16 wpisów, stałe bufory (tytuł 64 B, treść 160 B, nadawca 32 B), bez alokacji. Operacje w krótkiej sekcji bez przerwań.
- **Źródło SYSTEM** może ustawić wyłącznie kod jądra: `powiadomienie_systemowe()`.
  - Haki prawdziwych zdarzeń:
    - gotowość kontrolera HDA;
    - DHCP: sukces, DHCPNAK, brak DHCPACK;
    - podłączenie klawiatury/urządzenia wskazującego USB oraz urządzenia USB bez sterownika;
    - trwała awaria VirtIO-GPU.
  - Brak zdarzeń symulowanych.
- **Aplikacje: BWS 109 ZGLOS** (`PRAWO_GUI`):
  - źródło zawsze APLIKACJA, PID i nadawca z `proces_t.sciezka_pliku` (loader), nigdy z tekstu aplikacji;
  - poziomy INFO/SUKCES/OSTRZEŻENIE (BŁĄD tylko dla systemu);
  - limit 3 zgłoszenia na 30 s na proces i najwyżej 4 wpisy procesu w kolejce;
  - zgłoszenie aplikacji nigdy nie wypiera nieprzeczytanego powiadomienia systemowego (wtedy `BWS_ZAS_LIMIT`);
  - limit procesu jest zerowany przy jego zakończeniu (`powiadomienia_posprzataj_proces`).
- **Tekst:** tylko poprawny UTF-8; znaki sterujące → spacja; niepoprawna sekwencja kończy tekst; cięcie na granicy znaku; zawsze NUL.
- **Wyświetlanie:** WM pokazuje nadawcę według pola źródła: „System · …” albo „Aplikacja: nazwa · …”. Aplikacja nazwana „system” wyświetla się jako „Aplikacja: system”.
- **Obsługa w panelu:**
  - nieprzeczytane mają kolor akcentu (błąd i ostrzeżenie – kolor poziomu), a ikona dzwonka ma kropkę;
  - zamknięcie panelu oznacza obejrzane wpisy jako przeczytane;
  - „×” usuwa wpis;
  - „Oznacz jako przeczytane” i „Wyczyść” działają na całej liście;
  - „Brak powiadomień”, gdy kolejka jest pusta.
- **Brak** wiadomości z sieci, kont i synchronizacji.

## Nowe wywołania BWS

Wszystkie:
- sprawdzają znaną wersję (`BWS_ZASOBNIK_V1`) i rozmiar bufora ≥ struktury;
- przed jedną kopią sprawdzają zakres i mapowanie **całego** bufora (`czy_bezpieczny_zakres_uzytkownika_do_zapisu` + `bws_vmm_sprawdz_zakres_uzytkownika`), bez częściowego zapisu na granicy stron;
- budują strukturę wyzerowaną w jądrze;
- przy błędzie nie zmieniają bufora;
- nie zwracają wskaźników jądra, adresów fizycznych ani surowych struktur sterowników.

| BWS | Argumenty | Kto | Wynik |
|---|---|---|---|
| 107 `BWS_ZASOBNIK_STAN` | bufor, rozmiar, wersja | tylko WM: `PRAWO_SYSTEM_CONFIG` + PZB ≤ ZAUFANE **i** ścieżka programu WM (jak BWS 56 bez wejścia) | `BwsZasobnikStanV1`: dźwięk, sieć, nośniki, liczniki powiadomień |
| 108 `BWS_ZASOBNIK_POLECENIE` | polecenie, wartość | tylko WM | głośność 0..100, wyciszenie 0/1, przeczytane/usuń (id), wyczyść, przeczytane wszystkie |
| 109 `BWS_POWIADOMIENIA` | POBIERZ: bufor, rozmiar | tylko WM | `BwsPowiadomieniaListaV1` (od najnowszego) |
| 109 `BWS_POWIADOMIENIA` | ZGLOS: bufor, rozmiar | `PRAWO_GUI` | `BwsPowiadomienieZgloszenieV1` (wersja, rozmiar, poziom, tytuł, treść); limity jak wyżej |

`BWS_PIERWSZY_WOLNY` = 110 (testy ABI zaktualizowane). Ustawienia, Ustawienia ekranu i Aktualizacje mają te same prawa systemowe co WM, ale nie jego ścieżkę, więc BWS 107/108 ich odrzucają (test hosta).

## Testy

- **`make -j1 test-zasobnik-host`** (nowy, ASan/UBSan, 1468 sprawdzeń):
  - rejestr i stała tożsamość;
  - układ, odstępy, trafienia, przepełnienie według priorytetu (zegar nigdy zasłonięty), rozdzielczości 640–3840 i skale 100/200 %;
  - wspólne położenie paneli (też kalendarza);
  - glify i stany, powierzchnia bez alokacji w stanie ustalonym;
  - model stanu: dźwięk, sieć, powiadomienia, nośniki, brak odpowiedzi;
  - kolejka powiadomień: granice, limity, wypieranie, zaufane źródło, czyszczenie UTF-8;
  - mapowanie głośności HDA;
  - autoryzacja i budowa odpowiedzi BWS 107–109;
  - kontroler z atrapą jądra:
    - synchronizacja suwaka, wyciszenie i przywrócenie;
    - zajęty sprzęt, brak backendu;
    - zmiana z innego komponentu;
    - otwieranie, przełączanie, klik poza, Escape, TIMER;
    - sieć z tempem i „Niedostępne”;
    - powiadomienia: usuwanie, czyszczenie, odczyt przy zamknięciu, dynamiczna wysokość;
    - szybkie ustawienia, nośnik pojawia się i znika;
    - 200 cykli otwórz/zamknij bez wzrostu alokacji.
- **`make -j1 test-wm-click-routing`** (rozszerzony):
  - zasobnik przy zegarze;
  - klik ikony, zegar przy otwartym panelu (TIMER), kalendarz ↔ panel ↔ menu Start ↔ menu przepełnienia jednym kliknięciem;
  - ponowny klik, Escape;
  - dymek ikony;
  - rozdzielczości i skale;
  - przepełnienie przycisków okien przed zasobnikiem.
- **`make -j1 test-hda-tone`** (rozszerzony; prawdziwy `hda.cpp`, modelowane MMIO i transport):
  - verby `SET_AMP_GAIN_MUTE` dla 40 %, 0 %, wyciszenia i odciszenia;
  - zajęty transport = stan bez zmian;
  - ponowna konfiguracja trasy zachowuje poziom;
  - backend programowy: połowa amplitudy, cisza i zatrzymanie strumienia.
  - Skrypt dostał brakujące ścieżki `-I` (wcześniej nie kompilował się bez nich).
