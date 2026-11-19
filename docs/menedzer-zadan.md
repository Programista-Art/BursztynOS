# Menedżer zadań — architektura, BWS 103/104 i model pomiarów

Natywna aplikacja Ring 3 (`/programy/menedzer_zadan.cebula/menedzer_zadan.bur`, BursztynUI),
która pokazuje procesy z rzeczywistej tablicy schedulera, użycie procesora i pamięci oraz
pozwala zakończyć zwykłą aplikację przez jądro.

## Uprawnienia

* Nowe prawo `PRAWO_ZARZADZANIE_PROCESAMI` (bit 10, `jadro/bws/pzb.h`) jest w
  `PRAWA_ZNANE` i `PRAWA_UPRZYWILEJOWANE` (zdejmowane dziecku o PZB ≥ UŻYTKOWNIK).
* Nadawane **wyłącznie** z tabeli aplikacji systemowych jądra
  (`jadro/bws/uruchamianie_polityka.h`): Menedżer zadań dostaje PZB 3 i prawa
  `GUI | PLIKI_CZYTAJ | ZARZADZANIE_PROCESAMI` — bez `SYSTEM_CONFIG`, zapisu plików, sieci
  i uruchamiania programów. Wpis w tabeli wymusza weryfikację wbudowanej sumy SHA-256
  (`jadro/fs/skroty_systemowe.generated.h`), więc podmieniony plik nie dostaje prawa.
* Manifest nie zna nazwy tego prawa — zwykły `.cebula` nie może go zażądać.
* BWS 103/104 wymagają prawa **i** PZB ≤ ZAUFANE. Wyłączony przycisk w GUI nie jest
  zabezpieczeniem — decyzję zawsze podejmuje jądro.

## BWS 103 — migawka procesów

`bws_wywolaj(BWS_PROCESY_MIGAWKA, bufor, rozmiar_bajtów, BWS_PROCESY_V2_ABI, 0)`

* Bufor: `BwsProcesyNaglowek` (96 B), za nim `BwsProcesInfoV2[]` (168 B każdy),
  `jadro/bws/procesy_migawka.h`. Pola mają stałe typy i rozmiary (static_assert).
* Wynik: `BWS_MIGAWKA_OK` albo `BRAK_UPRAWNIEN`, `ZLY_BUFOR`, `ZLA_WERSJA`. Przy błędzie
  nic nie jest zapisywane.
* Za mały bufor: wpisy są obcinane (rosnący PID), `BWS_MIGAWKA_OBCIETA` jest ustawiona,
  `liczba_procesow` podaje rzeczywistą liczbę. Bufor mniejszy niż nagłówek to błąd.
  Liczba wpisów jest ograniczona do 16, więc wyliczenie rozmiaru nie może się przepełnić.
* Spójność: scheduler kopiuje wszystkie 16 slotów w jednej krótkiej sekcji bez przerwań
  (jeden procesor), bez alokacji i bez kopiowania do Ring 3. Pamięć jest liczona potem,
  osobno dla każdego procesu, po ponownym sprawdzeniu stanu, generacji i przestrzeni
  adresowej. Kopia do Ring 3 następuje na końcu, poza sekcjami krytycznymi.
* Dane: PID, stan schedulera, **generacja**, PZB, maska praw, skumulowany czas CPU, chwila
  startu, pamięć (patrz niżej) i ścieżka obrazu programu (`proces_t.sciezka_pliku`
  ustawiana przez loader). Żadnych wskaźników jądra ani pamięci innych procesów.
* `ochrona` i flaga `BWS_PROC_MOZNA_ZAKONCZYC` wynikają z tej samej funkcji, której używa
  BWS 104 (`ProcesyMigawka::ochrona`).

## BWS 104 — zakończenie procesu o danej tożsamości

`bws_wywolaj(BWS_PROCES_ZAKONCZ, pid, generacja, 0, 0)`

* Sprawdzenie generacji, polityki ochrony i przekazanie żądania do
  `scheduler_zazadaj_zakonczenia()` odbywa się w jednej sekcji bez przerwań, więc slot nie
  może w tym czasie zmienić właściciela. Inna generacja oznacza `BWS_ZAKONCZ_BRAK_PROCESU`:
  ponownie użyty PID nigdy nie zostanie zakończony zamiast wybranego procesu.
* Wyniki: `ZAAKCEPTOWANO` (proces przeszedł w KOŃCZĄCY), `JUZ_KONCZONY`, `BRAK_PROCESU`,
  `BRAK_UPRAWNIEN`, `CHRONIONY`, `WLASNY`, `URUCHAMIANY`, `ZLE_ARGUMENTY`.
* **Przyjęcie ≠ zakończenie.** Zasoby (PML4 i strony, stos jądra, warstwa okna, zasoby BWS)
  zwalnia później `posprzataj_zakonczone_procesy()` w kontekście timera — ten sam
  mechanizm co BWS 97 i samozakończenie. Menedżer zadań nie zwalnia niczego sam.
  Aplikacja raportuje zakończenie dopiero, gdy proces o tej tożsamości zniknie z migawki.
* Każde wywołanie uprawnione jest logowane na porcie szeregowym: `[MENEDZER-ZADAN] BWS104 ...`. Odmowy
  `BRAK_UPRAWNIEN` są logowane najwyżej 4 razy na proces (PID i generacja), potem jedna linia o wyciszeniu —
  zwykła aplikacja nie może zalewać logu.

### Procesy chronione

| Powód | Reguła |
|---|---|
| Jądro | PID 0 |
| Własny proces | wywołujący — aplikacja proponuje zwykłe zamknięcie okna |
| Menedżer okien | obie ścieżki WM (te same co dla BWS 40/56) |
| Usługa systemowa | PZB < ZAUFANE (jądro, sterowniki, usługi, np. Aktualizacje) |
| Wyższe zaufanie | PZB celu niższe niż PZB wywołującego |
| Uruchamiany | slot w budowie przez loader |

Procesy chronione pozostają widoczne na liście; przycisk pokazuje powód, a jądro i tak odmawia.

## Pomiar procesora

* Każdy odcinek czasu od włączenia rozliczania należy do **dokładnie jednego** kubełka:
  czas CPU procesu albo bezczynność (`jadro/bws/rozliczanie_cpu.h`).
* Odcinek kończy się przy każdym rzeczywistym przełączeniu kontekstu (tick timera lub
  programowe oddanie CPU) oraz na granicach HLT pętli PID 0. Źródło czasu: HPET
  (`czas_monotoniczny_ns`).
* Czas jest przypisywany procesowi bieżącemu — razem z jego syscallami i z przerwaniami,
  które nadeszły w tym czasie. Jądro nie ma osobnego kubełka IRQ, więc czasu przerwań nie
  da się wydzielić. To udokumentowane ograniczenie.
* PID 0 wykonuje prawdziwą pracę (USB, wejście, kompozytor) i jest pokazywany jako
  „Jądro systemu”. Bezczynność to wyłącznie czas w HLT. Przerwanie budzące HLT jest liczone
  do bezczynności do końca obsługi (rząd mikrosekund).
* Aplikacja liczy użycie w **przedziale** między dwiema migawkami (ok. 1 s):
  `Δczas_cpu / Δczas_rozliczony`. Procesy dopasowuje para (PID, generacja). Liczniki nie
  są zerowane przy próbkowaniu. Spadek licznika, brak upływu czasu, pierwsza próbka i błąd
  odczytu dają „Niedostępne” zamiast zgadywanej wartości.
* Procesor systemu: `1 − Δbezczynność / Δczas_rozliczony`, czyli maksymalnie 100% na
  jednym rdzeniu.
* Bez HPET: każdy tick timera (250 Hz, 4 ms) przypisuje swój okres kubełkowi bieżącemu.
  To próbkowanie statystyczne, a aplikacja pokazuje źródło pomiaru. Bez HPET i bez
  skalibrowanego timera pomiar jest niedostępny.

## Pomiar pamięci

* **RAM systemu.** „Zarządzana” to ramki PMM (MEMORY_AVAILABLE w pierwszych 4 GiB, bez
  obrazu jądra), „W użyciu” to zarządzana minus wolna. Pokazywana jest też wartość RAM
  zgłoszona przez firmware.
* **Pamięć procesu** to dokładnie to, co jądro zwalnia przy jego zakończeniu (przejście po
  prywatnej połowie tablic stron według reguły `zniszcz_hierarchie_procesu`,
  `jadro/mm/pamiec_procesu.h`):
  * prywatne ramki USER (obraz, stos, sterta),
  * ramki tablic stron (PML4, PDP, PD, PT),
  * stos jądra procesu,
  * bufor warstwy okna w kompozytorze.
* **Pamięć współdzielona (SHM, `PTE_SHARED`)** jest pokazywana osobno i **nie** jest
  wliczana do procesu — ta sama ramka może być zmapowana w kilku procesach (bez
  podwójnego liczenia). Kopia „cienia” warstwy okna po stronie BWS nie jest wliczana.
* „Sterta zarezerwowana” to zakres wirtualny, a nie RAM.
* PID 0 nie ma przypisanej pamięci. Widok Wydajność pokazuje resztę („Jądro, bufory i
  pamięć współdzielona”) jako różnicę między pamięcią w użyciu a sumą procesów.
* Przejście tablic stron odbywa się pod blokadą VMM, tylko dla przestrzeni z rejestru
  żywych klonów.

## Integracja

* Skrót pulpitu (`DesktopEntries`, id `menedzer-zadan`) jest dopisany **na końcu** listy,
  więc zapisane pozycje pozostałych skrótów się nie zmieniają, a nowy skrót trafia do wolnej
  komórki siatki.
* Wpis w menu Start ma akcję 22. Menedżer okien uruchamia
  `AppIdentity::TaskManager` przez `uruchom_lub_aktywuj` (pojedyncza instancja — ponowne
  uruchomienie aktywuje okno).
* Ta sama ikona `/ikonki/app_menedzer_zadan.png` jest używana na pulpicie, w menu Start i
  na pasku zadań (rejestr ikon po ścieżce programu). Generator ikony:
  `narzedzia/generuj_ikone_menedzera_zadan.py`.
* Instalacja przez `instaluj_wbudowane_programy()` działa tak samo w sesji Live i w systemie
  zainstalowanym.

## Sprzątanie procesu (jedna ścieżka dla BWS 104, BWS 97, okien podrzędnych i zwykłego wyjścia)

1. `scheduler_zazadaj_zakonczenia(pid)` — stan KOŃCZĄCY, zasoby zapisane w `zasoby_do_zwolnienia[pid]`
   (`oczekuje = true`). Zwykłe wyjście (BWS 32, `bws_zakoncz_biezacy_proces`) najpierw samo usuwa warstwę i
   powiadamia WM pod blokadą ekranu, potem trafia do tej samej kolejki.
2. `posprzataj_zakonczone_procesy()` w przełączeniu kontekstu, nigdy dla procesu bieżącego (jego stos jądra jest
   w użyciu). Timer wywłaszcza tylko Ring 3, bezczynny PID 0 przy wolnej blokadzie ekranu albo jawne punkty
   oczekiwania, więc blokada ekranu i sterty nie są wtedy trzymane. Kolejność:
   `okna_podrzedne_przed_sprzataniem` → `usun_warstwe(pid)` (bufor warstwy, metadane, popup/overlay/pasek) →
   `BWS_ZDARZENIE_OKNO_ZAMKNIETE` do WM tylko, jeśli warstwa jeszcze istniała (brak duplikatu po BWS 32) →
   `bws_gui_usun_stan_procesu` (fokus) → `bws_gui_zwolnij_mysz_procesu` → `bws_nowoczesne_posprzataj_proces`
   (zasoby BWS) → `ZniszczPrzestrzenAdresowaProcesu` (PML4 i strony, nie bieżące CR3) → `stos_jadra_zwolnij` →
   slot PUSTY. `oczekuje = false` przed oddaniem slotu, więc żadna ścieżka nie sprząta dwa razy.
3. Pasek zadań WM po każdym zdarzeniu buduje się od nowa z migawki okien jądra (`gui_pobierz_okna`); ikony są
   przypisane po `window_id = generacja_okna << 32 | pid`, więc wpis nie przechodzi na nowy proces o tym samym PID.
4. Słabe hooki schedulera muszą mieć linkowanie zgodne z definicją — `make test-slabe-hooki-jadra` sprawdza, że
   każda słaba referencja w obiektach jądra trafia w definicję w obrazie (regresja okien-duchów).

## Koszt rysowania karty Wydajność (zacinanie kursora, 2026-11-19)

* Przyczyna: `Etykieta::linie()` (BursztynUI) przy zawijaniu mierzyła cały prefiks po każdym znaku — O(n²) pomiarów
  BSF na każde rysowanie. Dwa opisy pod wykresami (~200 B) zmieniają się co sekundę: host -O2 z prawdziwym BSF
  ~2,2 ms na etykietę, cała karta 3,3–4,6 ms vs 1,2–2,0 ms na karcie Procesy; wykresy tylko ~0,17 ms. Pod TCG ta
  seria pracy w Ring 3 opóźniała obiegi PID 0 (round-robin), który rysuje programowy kursor.
* Poprawka: wyszukiwanie binarne najdłuższego mieszczącego się prefiksu (identyczny wynik — test różnicowy z prawdziwym
  BSF), brak pomiaru całego tekstu przy zawijaniu, `Wykres::ustaw_punkty` bez unieważnienia przy tych samych danych.
  Etykieta 0,5–0,7 ms, karta Wydajność ≈ karta Procesy.
* Natywnie po poprawce: CPU Menedżera 44–60 ms/s na Wydajności i 53–58 ms/s na Procesach; opóźnienie kursora takie
  samo na obu kartach. Pomiary: `docs/menedzer-zadan-2026-11-19-wydajnosc/`.
* Opisy pod wykresami rozciągają się na wiersz siatki — wcześniej miały wysokość pustego tekstu do zmiany rozmiaru okna.

## Testy

* `make -j1 test-menedzer-zadan-host` (ASan/UBSan) — rozliczanie CPU, pamięć z tablic stron,
  polityka i ABI BWS 103/104, model aplikacji, rejestracja i prawdziwe okno BursztynUI z
  symulowanym jądrem. Testy na kontrolowanych licznikach nie dowodzą dokładności pomiaru na
  sprzęcie.
* `test.bur` (tryb automatyczny) sprawdza w prawdziwym jądrze, że aplikacja bez prawa dostaje
  odmowę BWS 103/104.

## Monitorowanie dysków, sieci, I/O procesów i grafiki (2026-11-19)

BWS 105 (telemetria urządzeń), BWS 106 (liczniki I/O procesów), kolumny „Dysk” i „Sieć” oraz nowa karta
„Wydajność” są opisane w `docs/menedzer-zadan-monitorowanie.md` – razem z dokładną semantyką każdego licznika
i listą metryk niedostępnych (zajętość dysku, prędkość łącza, fizyczne I/O procesu, obciążenie GPU).
