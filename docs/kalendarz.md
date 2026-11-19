# Kalendarz Bursztyn OS

Gałąź `feature/kalendarz` (od `0357cdd`, po scaleniu prac nad Menedżerem zadań). Dwie części:

1. **Kalendarz paska zadań:** kliknięcie zegara otwiera kompaktowy panel nad paskiem.
2. **Aplikacja „Kalendarz”:** natywny program Ring 3 w menu Start.

Obie korzystają z tego samego komponentu `BursztynUI::Kalendarz` (`bursztyn_ui/kalendarz.h`). Nie powstała druga implementacja kalendarza.

## Źródło daty i czasu

| Element | Stan |
|---|---|
| Wywołanie | BWS 9 (`BWS_CZAS_RTC`), nowy tryb addytywny: `arg2 = BWS_CZAS_LOKALNY_V1` → struktura `BwsCzasLokalny` (24 B: rok, miesiąc, dzień, godzina, minuta, sekunda, dzień tygodnia, flagi) – `jadro/bws/czas_lokalny.h` |
| Zgodność | `arg2 = 0` (każde dotychczasowe wywołanie) = jak dotąd tekst `HH:MM:SS`. Bez nowego numeru BWS. |
| Prawa | żadne – jak tekstowy BWS 9. Zwracane są tylko liczby, bez pamięci jądra. |
| Źródło | `pobierz_czas_rtc_bezpiecznie()` sterownika RTC. Sterownik sam przelicza CMOS (traktowany jako UTC) na czas lokalny CET/CEST (`sterowniki/czas/zegar-rtc.h`). Flaga `BWS_CZAS_STREFA_CET` to raportuje. |
| Brak próbki RTC | wynik 0 – aplikacje pokazują „Data systemowa niedostępna” i wyłączają „Dzisiaj”. Zastępcza data 2000-01-01 sterownika nie jest pokazywana. |
| Pomocnik Ring 3 | `bursztyn_ui/data_systemowa.h`: `pobierz_date_systemowa()` waliduje wersję, rozmiar, zakres i spójność dnia tygodnia |

**Ograniczenia:**
- Strefa czasowa i czas letni są regułą sterownika RTC ustaloną przy kompilacji (CET/CEST). System nie ma konfiguracji strefy, a widżety niczego nie przeliczają.
- Rok CMOS bez rejestru stulecia jest interpretowany jako 2000–2099.
- Komponent obsługuje lata 1900–2100.

## Komponent `BursztynUI::Kalendarz` – rozszerzenia (zgodne wstecz)

Istniało:
- model gregoriański (lata przestępne z regułą stuleci, dzień tygodnia metodą Sakamoto);
- siatka 6 × 7 z poniedziałkiem w pierwszej kolumnie;
- nawigacja `<< < > >>`;
- klawiatura: strzałki, Page Up/Down;
- przerysowanie tylko zmienionych komórek przy najechaniu.

Dodano:
- **Dzień dzisiejszy:** `ustaw_dzisiaj()` wypełnia komórkę akcentem; gdy dzisiaj jest też wybrane, dochodzi ramka akcentu. Wyróżnienie jest niezależne od wyboru, a przy zmianie dnia przerysowują się tylko dwie komórki.
- **Akcja „Dzisiaj”:** `pokaz_dzisiaj()` (także klawisz Home, gdy znana jest data systemowa).
- **Widok:** `widok_rok()`, `widok_miesiac()`, `ustaw_widok()` oraz wywołanie zwrotne `przy_zmianie_widoku()` przy nawigacji.
- **`DataKalendarza`:** polskie nazwy miesięcy (mianownik i dopełniacz) i dni tygodnia, `formatuj()` („czwartek, 19 listopada 2026”), `dzien_roku()`, `tydzien_iso()` (ISO 8601).
- **Akcesory geometrii siatki:** `komorka_dla_dnia()`, `pierwszy_dzien_tygodnia()`, `dni_widocznego_miesiaca()`.
- **Poprawki wyglądu (po sesji natywnej):**
  - przyciski `<< < > >>` mają szerokość tekstu w aktywnej czcionce + odstęp i przerwę 3 DIP (wcześniej stałe 20 i 18 DIP zlewały się w „<<<”), mierzoną raz na skalę i rozmiar czcionki;
  - tekst wybranego dnia ma kolor `motyw_kolor_tekstu_zaznaczenia()` (kontrast w motywie ciemnym).

## Kalendarz paska zadań (Menedżer Okien)

- **Kliknięcie zegara:** `TaskbarClock::Surface::contains()` → `przelacz_kalendarz_paska()` w `programy/menedzer_okien/menedzer_okien.cpp`.
- **Panel:** `KalendarzPaska::Surface` (`jadro/gui/kalendarz_paska.h`) – `PowierzchniaPopup` zaufanego WM, obsługiwana tym samym `ZarzadcaPopupow` i `PlikiUI::ObslugaPopup` co menu Start i menu przepełnienia paska.
  - To nie jest dymek z flagą `BWS_POPUP_BEZ_WEJSCIA` (BWS 56) ani nowy proces. Zwykłe aplikacje nie dostają żadnej nowej możliwości.
- **Zawartość:** nagłówek z wybraną datą po polsku, komponent `Kalendarz`, przycisk „Dzisiaj”.
- **Położenie** (`KalendarzPaska::polozenie`):
  - nad paskiem zadań, prawa krawędź przy prawej krawędzi zegara;
  - w granicach ekranu, nie zasłania zegara;
  - na niskim ekranie panel jest krótszy.
- **Otwieranie:** każde otwarcie pokazuje miesiąc z datą systemową. Nawigacja trwa, dopóki panel jest otwarty.
- **Zamykanie:**
  - ponowne kliknięcie zegara (zdarzenie pochłonięte, panel się nie otwiera ponownie);
  - Escape;
  - kliknięcie poza panelem – zamyka i przekazuje kliknięcie dalej, więc na przykład przycisk Start otwiera menu od razu;
  - utrata fokusu (BLUR), zmiana ekranu, otwarcie menu Start lub menu przepełnienia.
- **Fokus:** fokus popupu jest na siatce (strzałki, Home).
- **Dymki:** dymki paska są wstrzymane, dopóki panel jest otwarty (`inny_popup_wm_otwarty`).
- **Zegar przy otwartym panelu:** zdarzenia TIMER nie są pochłaniane przez panel, więc zegar dalej się aktualizuje (usterka wykryta natywnie i poprawiona).
- **Koszt:**
  - zegar zmienia tło (stan aktywny) tylko przy wejściu lub wyjściu kursora i otwarciu lub zamknięciu panelu – przerysowuje się sam zegar, nie pasek;
  - datę WM czyta raz na sekundę i tylko przy otwartym panelu (przesunięcie „dzisiaj” o północy).

## Aplikacja „Kalendarz”

| Element | Wartość |
|---|---|
| Program | `/programy/kalendarz.cebula/kalendarz.bur` (`AppIdentity::Calendar`), źródła `programy/kalendarz/` |
| Manifest | PZB 4 (aplikacja użytkownika), uprawnienia `["okna"]` – bez SYSTEM_CONFIG, plików, sieci i zarządzania procesami |
| Instalacja | `instaluj_paczke()` w `jadro/rdzen/kernel.cpp` – ta sama ścieżka dla Live ISO i instalowanego systemu; katalog w `psf_bootstrap.h` |
| Menu Start | „Kalendarz” (alias „data dzień miesiąc rok”), akcja 23 → `uruchom_lub_aktywuj()` – druga próba aktywuje istniejące okno |
| Ikona | `ikonki/app_kalendarz.png` (128 × 128, oryginalna, generator `narzedzia/generuj_ikone_kalendarza.py`); menu Start i pasek zadań przez `AppIcons::Dodatkowe` |
| Skrót na pulpicie | brak (zgodnie z wymaganiem) |

Nazwa pliku ikony: w repozytorium nie było ani `ikonki/appkalendarz.png`, ani `app_kalendarz.png`. Wszystkie ikony aplikacji mają konwencję `app_<nazwa>.png`, więc ikona to `app_kalendarz.png`. Generator nie nadpisuje istniejącego pliku bez `--nadpisz`.

**Okno** (`programy/kalendarz/okno.h`):
- 720 × 560, minimum 580 × 420 (najdłuższy tytuł „Październik 2026” w podwójnej czcionce mieści się obok przycisków).
- **Na górze:** miesiąc i rok (podwójna czcionka), `<`, „Dzisiaj”, `>`.
- **Pośrodku:** komponent `Kalendarz` z fokusem klawiatury.
- **Na dole:**
  - wybrana data po polsku;
  - dzień roku, tydzień ISO i odległość od dziś („jutro”, „za 35 dni”, „3 dni temu”).
- **Rozszerzalność:** `KalendarzApp::OpisDnia` (`model.h`) to jedyne źródło treści panelu dnia; tam dojdą przyszłe wydarzenia bez wymiany komponentu siatki.
- **Data:** sprawdzana co ~30 s zadaniem okresowym pętli (bez aktywnego czekania). Bez zmiany daty nie ma przerysowań ani alokacji.

## Testy

- **`make -j1 test-kalendarz-host`** (nowy, ASan/UBSan):
  - wszystkie dni lat 1900–2100 porównane z niezależną referencją (dni od 1900-01-01): długości miesięcy, lata przestępne, dzień tygodnia, układ kolumn, dzień roku, tydzień ISO;
  - formatowanie po polsku;
  - ABI BWS 9;
  - komponent: mysz, klawiatura, „Dzisiaj”, granice zakresu, rysowanie dnia dzisiejszego, brak alokacji przy ruchu myszy;
  - aplikacja: przyciski, przełom roku, zmiana daty, brak daty systemowej, rozmiary 440×420–1920×1080 przy skalach 100–200%, zmiana motywu;
  - położenie panelu na 800×600–1920×1080 przy skalach 100–200%.
- **`make -j1 test-wm-click-routing`** (rozszerzony):
  - klik zegara, Escape, klik poza panelem (zamknięcie i przekazanie kliknięcia), ponowny klik zegara;
  - menu Start i menu przepełnienia, dymki;
  - zmiana daty o północy, brak daty;
  - rozdzielczości i skale;
  - wpis w menu Start z ikoną, pojedyncza instancja, brak skrótu na pulpicie.

## Stabilizacja (2026-11-19, przegląd końcowy)

- **Routing zdarzenia TIMER w WM:**
  - `PlikiUI::ObslugaPopup::zdarzenie()` zwraca „obsłużone” dla każdego zdarzenia przy otwartym popupie, także dla TIMER.
  - Pętla WM robiła wtedy `continue` i pomijała aktualizację zegara. Dotyczyło to menu przepełnienia, kalendarza paska, menu kontekstowego i dialogów pulpitu; menu Start miało własny wyjątek.
  - Teraz jeden wspólny warunek `popup_konczy_obsluge()` w pętli: popupy dostają TIMER, ale TIMER nigdy nie kończy iteracji.
  - `ObslugaPopup` celowo nie jest zmieniany: wywołujący (także Eksplorator) odświeżają ekran w gałęzi „obsłużone”, np. przy miganiu kursora w dialogach.
- **Jedna nawigacja w aplikacji:**
  - `Kalendarz::ustaw_pasek_nawigacji(false)` – opcja domyślnie włączona; panel paska zachowuje pasek komponentu;
  - aplikacja ma w nagłówku `<< < Dzisiaj > >>`;
  - minimalna szerokość okna to 640 px.
- **Wybrana data poza widocznym miesiącem:**
  - wybór jest zachowywany przy nawigacji (zamierzone);
  - szara komórka sąsiedniego miesiąca z wybraną datą ma ramkę;
  - aplikacja dopisuje „· poza widocznym miesiącem”;
  - w panelu paska nagłówek zawsze pokazuje wybraną datę, a tytuł siatki – widoczny miesiąc.
- **BWS 9:**
  - nieznane wartości `arg2` zwracają 0 bez zapisu (wcześniej wpadłyby w tryb tekstowy i zapisały 32 bajty);
  - przed kopiowaniem sprawdzany jest zakres i mapowanie całego bufora, więc struktura na granicy stron nie zostanie zapisana częściowo;
  - struktura ma 24 B bez dopełnienia i jest zerowana w jądrze;
  - nadal bez żadnego prawa.
