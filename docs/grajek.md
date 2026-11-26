# Grajek 1.0 – natywny odtwarzacz muzyki Bursztyn OS

Grajek to aplikacja Ring 3 (PZB 4) napisana w BursztynUI. Odtwarza pliki **WAV** i **MP3** z katalogu
`/dane/muzyka` przez systemowy interfejs PCM **BWS 110** (wspólny mikser jądra → Intel HDA). Wszystkie napisy są po
polsku. Program: `/programy/grajek.cebula/grajek.bur`, manifest `opis.aplikacji`, wpis „Grajek” w menu Start (akcja
24), ikona `/ikonki/app_grajek.png`.

## Architektura

| Moduł (`programy/grajek/`) | Rola |
|---|---|
| `grajek.cpp` | punkt wejścia `_start`, powiązanie z API Ring 3 (PSF, BWS 110, ustawienia, HPET), adaptacyjny odstęp zadania okresowego |
| `okno.h`, `widoki.h` | okno BursztynUI: biblioteka, bieżący utwór, kolejka, korektor, sterowanie, podpowiedzi |
| `kontroler.h` | logika aplikacji bez GUI: przejścia kolejki, powtarzanie/losowo, odświeżanie, zapis ustawień, tryb w tle |
| `silnik.h` | maszyna stanów odtwarzania i cykl życia strumienia BWS 110 |
| `dekoder.h` | interfejs dekoderów (rozszerzalny o FLAC/OGG), dekoder WAV, rozpoznawanie formatu po zawartości |
| `dekoder_mp3.cpp` | dekoder MP3 (dr_mp3), jedyna jednostka kompilowana z SSE2 |
| `wav.h`, `id3.h`, `mp3_info.h`, `metadane.h` | parsery RIFF/WAVE, ID3v1/v2.2–2.4, nagłówki MPEG + Xing/VBRI |
| `przetwarzanie.h` | konwersja do 48 kHz stereo S16 (Hermite, całkowita), mono → stereo |
| `korektor.h`, `tabele_dsp.h` | 10‑pasmowy korektor stałoprzecinkowy (tabele generowane: `narzedzia/generuj_tabele_grajka.py`) |
| `analiza.h` | wskaźniki poziomu L/P i widmo (FFT 512, Q15) z rzeczywistych próbek |
| `biblioteka.h` | przyrostowy skan `/dane/muzyka`, walidacja, sortowanie (po polsku, naturalne), filtr |
| `kolejka.h` | kolejka, losowo (permutacja), powtarzanie, listy M3U8 |
| `konfiguracja.h`, `okladka.h`, `zrodlo.h`, `pamiec.h`, `tekst.h` | ustawienia, okładki, czytnik plików, alokator z limitem, UTF‑8 |
| `pomiar.h` | opcjonalny pomiar czasu sekcji (TSC, bez wywołań systemowych i logów; domyślnie wyłączony) |

Potok dźwięku: plik → dekoder (WAV/MP3, natywna częstotliwość) → konwersja 48 kHz stereo S16 → korektor →
analizator (kopia do wizualizacji) → `PCM_WRITE` (BWS 110) → mikser jądra → HDA. Głośność i wyciszenie to
`PCM_GAIN` strumienia (0–100 %, jądro mnoży próbki przy miksowaniu).

### Model wykonania
Ring 3 nie ma wątków użytkownika, więc Grajek używa kooperacyjnego zadania okresowego `AplikacjaGUI` (1 tick ≈ 4 ms
podczas odtwarzania i skanu, 25 ticków w bezczynności; bez aktywnego czekania – proces śpi w
`gui_czekaj_na_zdarzenie_z_timeoutem`). Każdy tick: (1) okładka nowego utworu – dekodowana **przed** otwarciem
strumienia, (2) produkcja PCM (uzupełnianie kolejki 4096 ramek = 85 ms, gdy wolne ≥ 512), (3) zdarzenia silnika,
(4) jeden krok skanu biblioteki, (5) odłożony zapis ustawień (≤ co 2 s). Interfejs odświeża postęp 1×/s, a
wizualizację 4×/s i tylko zmienione pola.

### Odrysowania a ciągłość dźwięku
Rysowanie BursztynUI jest synchroniczne w tym samym wątku co produkcja PCM, a kolejka jądra (4096 ramek) to 85,3 ms.
Każdy przestój producenta dłuższy niż zawartość kolejki kończy się niedoborem. Dlatego:
* stan ustalony unieważnia tylko małe obszary: etykieta czasu i suwak (1 Hz), zmienione pola wizualizacji (4 Hz, gdy
  wartości się zmieniły; pola danych mają ograniczony rozmiar niezależnie od wielkości okna), wiersz wskaźnika w
  tabelach przy zmianie stanu; pomiar hosta: 10 s odtwarzania z wizualizacją = 20 odrysowań po średnio 30 tys. px
  (3,5 % okna 1280×680), bez pełnego odrysowania i bez przeliczania układu,
* pasek stanu ma stałą wysokość – komunikat (np. o niedoborze, najwyżej co 5 s) unieważnia tylko pasek,
* długie teksty (tytuł, wykonawca, format, komunikat) są dopasowywane z wielokropkiem tylko przy zmianie tekstu lub
  szerokości, nie przy każdym rysowaniu,
* `Okno::hak_rysowania` (BursztynUI, domyślnie pusty) daje punkt kontrolny tuż przed każdą publikacją regionu
  (BWS 57): Grajek wywołuje `Silnik::dopompuj()` – kolejka jest pełna (≥ 4096 − 128 ramek) w chwili, gdy proces
  oddaje sterowanie do BWS 57 i kompozycji. `dopompuj()` nie ładuje plików ani nie otwiera strumienia,
* po dograniu kolejki w pauzie strumień jest zamykany; wznowienie otwiera nowy strumień z prerollem jądra.

### Liczniki ciągłości (`StatystykiCiaglosci`, `silnik.h`)
* **niedobór** = licznik jądra `starved` strumienia Grajka: ramki 48 kHz okresów HDA zatwierdzonych do DMA, w których
  strumień był oczekiwany (po prerollu, przed `PCM_FINISH`/`PCM_CLOSE`), a jego kolejka była pusta. To cisza na
  wyjściu spowodowana brakiem danych Grajka. **Nie obejmuje**: pauzy i dogrywania po FINISH, ładowania i zmiany
  utworu, prerollu nowego strumienia (przewijanie), okresów pominiętych przez transport HDA (utrata własności DMA,
  `bezpieczna_cisza` w statystykach HDA) ani ciszy nagranej w pliku,
* osobno: **w tym utworze** i **od uruchomienia** Grajka. Licznik utworu jest monotoniczny w obrębie utworu i
  zerowany **wyłącznie** na granicy utworu (`Silnik::wczytaj`: wybór, następny/poprzedni, powtórzenie utworu,
  przejście automatyczne); przewijanie, pauza/wznowienie i ponowne otwarcie strumienia go nie zerują. Każda granica
  zwiększa `Silnik::pokolenie_utworu()`,
* statystyki są czytane co ~50 pompowań **oraz tuż przed każdym `PCM_CLOSE`/`PCM_FINISH`** (wcześniej końcówka
  zamykanego strumienia ginęła); zamknięty strumień nie jest liczony podwójnie (delta od ostatniego odczytu uchwytu),
* **przerwa producenta**: najdłuższy odstęp między kolejnymi uzupełnieniami kolejki w stanie Odtwarzanie (także do
  chwili zamknięcia strumienia) i liczba odstępów > 85,3 ms (każdy taki odstęp przy pełnej kolejce = niedobór),
* komunikat: „Przerwy w dźwięku – brak danych Grajka: X ms w tym utworze, Y ms od uruchomienia.” – migawka
  liczników w chwili złożenia, najwyżej co 5 s (każda zmiana tekstu to odrysowanie). Niedobór zgłoszony w tym oknie
  nie ginie: komunikat jest odświeżany po upływie okna. Na granicy utworu aktywny komunikat jest składany od nowa
  natychmiast („0 ms w tym utworze, Y ms od uruchomienia”).
* **Defekt naprawiony 2026-11-23** (natywny QEMU 2026-11-22): komunikat „192 ms w tym utworze, 234 ms od
  uruchomienia” był widoczny już przed startem MP3 (zrzut `n12`) – 192 ms powstało przy przywróceniu okna, gdy grał
  jeszcze WAV z fazy C (przerwy 85 + 106 ms przy 299,7 s nagrania). Komunikat nie był odświeżany po zmianie utworu,
  więc przy MP3 wyglądał na wartość MP3, a kolejny (21 ms, już dla MP3) – na spadek licznika. Sam licznik nie malał.

### Diagnostyka (opcja „Diagnostyka”, klucz `diagnostyka=1`, karta „Diagnostyka”)
Pomiar czasu sekcji licznikiem TSC (bez wywołań systemowych w gorącej ścieżce i bez logów na port szeregowy):
dekodowanie, konwersja, korektor, analiza, BWS 110, widmo, rysowanie drzewa BursztynUI, publikacja (BWS 57),
tabele, wizualizacja, okładka.
* Pasek stanu ma zawsze **jeden wiersz** (stała wysokość): przy włączonej diagnostyce po prawej jest krótkie
  podsumowanie „Niedobór X ms · przerwa Y ms · GUI Z ms/s” (kliknięcie otwiera kartę „Diagnostyka”). Włączenie
  diagnostyki nie przesuwa sterowania i nie przelicza układu (wcześniej drugi wiersz podnosił przyciski o ~27 px).
* Karta **Diagnostyka** pokazuje wszystkie liczniki w tabeli „Pomiar | Wartość” bez obcinania: niedobór utworu i
  sesji, epizody, maks. przerwa producenta, przerwy > 85 ms, min. i bieżąca zajętość kolejki PCM, ms/s każdej sekcji,
  maks. rysowanie i publikacja, odrysowania/s. Liczniki ciągłości działają zawsze; czasy sekcji – przy włączonej
  opcji („—” bez pomiaru). Karta jest odświeżana raz na ~1 s i tylko gdy jest widoczna; unieważniane są wyłącznie
  komórki zmienionych wartości.
* **Czasy sekcji to czas zegarowy**, nie wyłączny czas CPU: obejmują wywłaszczenie procesu przez PID 0 i oczekiwanie
  wewnątrz wywołań systemowych (np. oddanie CPU między pasami BWS 57). Nie widzą czasu, w którym proces nie był
  wykonywany – to pokazuje przerwa producenta. Okresy pominięte przez transport HDA nie są tu liczone (statystyki
  HDA jądra: `bezpieczna_cisza`, `utracone_okresy`).
* **Maks. czekanie na CPU** i **Maks. uśpienie procesu** (2026-11-23): liczniki planisty jądra z `PCM_QUERY_STATS`
  (BwsPcmStats V2, od uruchomienia): najdłuższy czas, gdy Grajek był gotowy, a CPU miał inny wątek (PID 0 –
  kompozytor/HDA, inne aplikacje), oraz najdłuższe dobrowolne uśpienie. Interpretacja: przerwa producenta ≈ czekanie
  na CPU → opóźniał inny wątek; przerwa producenta ≫ obu → Grajek sam był zajęty (praca Ring 3 albo długie wywołanie
  systemowe, np. BWS 57 – czas w Ring 0 liczy się jako czas procesu).
* Przy zakończeniu Grajka wszystkie wiersze trafiają do ustawień aplikacji (`…/Ustawienia/grajek/diagnostyka`).

## Przerwy przy zmaksymalizowanym oknie i wizualizacji (analiza 2026-11-23)
Zgłoszenie (Windows 11, QEMU SDL/WAV): wyraźnie więcej przerw przy zmaksymalizowanym oknie, jeszcze więcej z
wizualizacją, WAV płynniej niż MP3. Zrzuty (różne chwile/sesje – nie łączone w jeden pomiar): niedobór utworu 640 ms,
sesji 693 ms, 3 epizody, maks. przerwa producenta 333 ms, 26 przerw > 85 ms, maks. rysowanie 48,2 ms, maks.
publikacja BWS 57 98,3 ms, dekodowanie ≈ 286 ms/s (czas zegarowy).

**Potwierdzone w kodzie (przyczyna zależna od rozmiaru okna):** cień warstwy (ochrona przed rozdartą klatką,
`sterowniki/grafika/grafika.cpp`) kopiował przy **pierwszym zapisie każdej klatki całą warstwę okna** (szer × wys
pikseli) – w nieprzerywalnym BWS 57, pod blokadą ekranu, bez punktów kontrolnych HDA – także gdy Grajek zmieniał
tylko etykietę czasu albo słupki widma. Dla okna 1920×1040 to 2 mln pikseli (8 MiB) na każdą klatkę
(wizualizacja 4 Hz + postęp 1 Hz + diagnostyka 1 Hz). Drugi koszt: wszystkie zmiany klatki trafiały do kompozytora
jako **jeden prostokąt obejmujący**. Model hosta (rzeczywiste regiony publikowane przez Grajka, 10 s z wizualizacją;
nie pomiar natywny): kopia cienia 1920×1040 ≈ 6,0 mln px/s przed poprawką, ≈ 0,17 mln px/s po; 1180×660 – 2,3 mln
→ 0,07 mln px/s. Praca samego Grajka (rysowanie Ring 3) była już ograniczona i niezależna od rozmiaru okna.

**Poprawka (jądro, wąska):** cień jest utrzymywany przyrostowo – po publikacji (BWS 17) kopiowane są tylko
opublikowane prostokąty, więc na początku następnej klatki cień już zawiera ostatnią klatkę. Pełna kopia (jak
dotąd) tylko przy niepewnej zgodności: nowy bufor cienia, zmiana bufora/rozmiaru warstwy, zmiana **epoki treści**
(`skladacz_obrazu_epoka_tresci()`: tworzenie, zmiana rozmiaru, czyszczenie, usunięcie warstwy, zmiana trybu
ekranu), porzucone oczekujące obszary (aktywacja warstwy, zmiana trybu), zbiorcza wymiana regionów (BWS 57/4).
Pełna kopia ma punkty kontrolne HDA co ~64 tys. px. Oczekujące obszary procesu: do 8 prostokątów (łączenie przy
stracie ≤ 1/8, przepełnienie = prostokąt obejmujący), we współrzędnych warstwy; kompozytor dostaje każdy osobno.
Przy okazji naprawiony prostokąt pełnej klatki BWS 19 (był podawany jako (0,0,szer,wys) **ekranu**). Synchronizacja
(blokada ekranu, zawór 1 s, zwalnianie cienia z procesem) bez zmian. Testy: `test_boot_graphics` (190 sprawdzeń, w
tym sonda „zapis bez śledzenia” dowodząca braku pełnej kopii, okno przesunięte między rysowaniem a publikacją,
zmiana epoki, aktywacja, BWS 19, punkty HDA).

**Hipotezy (bez dowodu natywnego):** (1) okresy HDA Skip z pomiaru 2026-11-22 (praca odroczona HDA spóźniona
> ~37 ms) mogły powstawać właśnie w tej nieprzerywalnej kopii – nowa wersja ją usuwa w stanie ustalonym; (2) czas
dekodowania 286 ms/s obejmuje wywłaszczenie (TSC zegarowy) – nie jest wyłącznym czasem CPU. Potwierdzenie wymaga
natywnego nagrania WAV i liczników (poniżej).

**WAV kontra MP3:** oba pliki testowe mają 48 kHz (brak resamplingu). Pomiar hosta (`dr_mp3` 0.7.4, ta sama
konfiguracja): MP3 ≈ 1,1–1,25 ms CPU na 1 s audio, WAV ≈ 0,15 ms – ok. 8×; dekodowanie porcjami ≤ 2048 ramek
(maks. ≈ 0,5 ms na hoście). minimp3 liczy w float/SSE, które pod emulacją TCG jest wielokrotnie droższe niż
arytmetyka całkowita – większy udział CPU dekodera zmniejsza zapas na rysowanie i kompozycję. Odczyt pliku:
okno 64 KiB (przy 64 kbit/s ≈ co 8 s), metadane i tablica punktów przewijania poza odtwarzaniem – nie wpływają na
ciągłość. Pomiar natywny WAV vs MP3 w tych samych warunkach nie został wykonany.

### BWS 110 – cykl życia strumienia
* `PCM_OPEN` (48000 Hz, 2 kanały, S16) przy starcie odtwarzania; `PCM_BUSY`/`PCM_LIMIT` → ponowienie w kolejnym ticku
  (≤ 250 prób), `PCM_DEVICE` → błąd „Brak urządzenia dźwięku (Intel HDA)”.
* `PCM_WRITE` ≤ 512 ramek; zaakceptowany prefiks przesuwa bufor pośredni, reszta czeka (brak duplikatów i pominięć).
  Po zapisie częściowym silnik czeka na kolejny tick (kolejka pełna). `PCM_FULL`/`PCM_BUSY` – ponowienie.
* `PCM_QUERY` – zajętość; pozycja odtwarzania = zaakceptowane − w kolejce (dokładność do zawartości kolejki).
* `PCM_QUERY_STATS` (co ~200 ms oraz przed każdym CLOSE/FINISH) – licznik `starved` raportowany jako **niedobór**
  (osobno od pauzy i ciszy; patrz „Liczniki ciągłości”).
* `PCM_FINISH` – pauza (kolejka ≤ 85 ms dogrywa się, mikser nie liczy braku danych jako niedoboru; po dograniu
  strumień jest zamykany) i koniec utworu.
* `PCM_CLOSE` – stop, przewijanie, zmiana utworu, zamknięcie. API nie ma operacji „flush”: zamknięcie i ponowne
  otwarcie jest jedynym sposobem porzucenia kolejki, więc stare próbki nigdy nie trafiają do nowego utworu.

### Stany
`Zatrzymany`, `Ładowanie`, `Odtwarzanie`, `Pauza`, `Przewijanie`, `Błąd`. Niedozwolone przejścia (np. pauza bez
odtwarzania, wznowienie podczas odtwarzania, przewijanie bez utworu) są odrzucane.

## Formaty
* **WAV**: RIFF/WAVE, PCM 8/16/24/32 bit, IEEE float 32 bit (konwersja bez FPU), WAVE_FORMAT_EXTENSIBLE; mono i
  stereo; 8000–192000 Hz. Walidacja fragmentów, długości, `block_align`, przycięcie `data` do pliku, nieznane
  fragmenty pomijane, limit liczby fragmentów. Kompresja (ADPCM, A‑law…) – czytelny komunikat.
* **MP3**: MPEG‑1/2/2.5 Layer III, CBR i VBR (Xing/Info/VBRI, opóźnienie/dopełnienie LAME), mono/stereo,
  8–48 kHz. Strumieniowo przez okno 64 KiB (`czytaj_fragment_pliku`) – pliku nie wczytuje się w całości.
* Rozszerzenie (`.wav`/`.mp3`, dowolna wielkość liter) wybiera kandydatów, ale o przyjęciu decyduje zawartość
  (RIFF/WAVE albo dwie spójne ramki Layer III).
* Wyjście zawsze 48 kHz stereo S16 (jedyny format BWS 110). 48 kHz przechodzi bit w bit; inne częstotliwości –
  interpolacja Hermite'a.

### Dekoder MP3 i licencja
`programy/grajek/zewnetrzne/dr_mp3.h` – dr_mp3 v0.7.4 (David Reid), kopia niezmodyfikowana; licencja do wyboru:
Public Domain (Unlicense) albo MIT No Attribution; bazuje na minimp3 (CC0). Tekst licencji:
`programy/grajek/zewnetrzne/LICENSE`, trafia też do `licenses/dr_mp3.txt` w ISO (`package_release_notices.py`).
Brak kodu GPL. Konfiguracja Ring 3 (bez stdio/libc, własny alokator, bez asercji) jest w `dekoder_mp3.cpp`.
Znany błąd dr_mp3 0.7.4 (punkt szukania w oknie 3 początkowych ramek przy gęstej tablicy) jest omijany przez odstęp
punktów ≥ 8 ramek; krótkie pliki są przewijane dokładnie „od początku”.

## Metadane i okładki
ID3v2.2/2.3/2.4 (UTF‑8, UTF‑16 z BOM, UTF‑16BE, Latin‑1; unsynchronisation; rozmiary nie‑syncsafe iTunes),
ID3v1/v1.1 jako uzupełnienie, RIFF LIST/INFO (INAM, IART, IPRD, ICRD, ITRK). Pola: tytuł, wykonawca (lub wykonawca
albumu), album, rok, numer. Brak tytułu → nazwa pliku. Limity: okno tagu 256 KiB, tekst ramki 1 KiB, pola 96 B,
512 ramek. Okładka (APIC/PIC, JPEG/PNG, preferowana „front cover”): ≤ 2 MiB danych, ≤ 3000×3000 i ≤ 4 Mpx
sprawdzane z nagłówka **przed** dekodowaniem (wspólny dekoder `obrazy/dekoder_obrazu`), skalowana raz do 220 px.
Brak okładki w tagu → **płyta winylowa użytkownika** (`ikonki/plyta_winylowa.jpg`, patrz „Grafiki”); okładka z tagu
ma pierwszeństwo. Kontrolka okładki skaluje obraz **raz na rozmiar** (średnia obszarowa przy zmniejszaniu, dwuliniowo
przy powiększaniu, proporcje zachowane) w `rozmiesc()` i rysuje go 1:1 – wcześniej najbliższy sąsiad przy każdym
rysowaniu. Wygenerowana grafika płyty (`grafika_domyslna`) została usunięta.

## Grafiki (ikony przycisków i płyta)
Źródła użytkownika (repozytorium, tylko czytane – nigdy nie zmieniane ani przerysowywane):
* `ikonki/plyta_winylowa.jpg` – progresywny JPEG 492×492, 146 235 B; osadzony w obrazie systemu **bez zmian** jako
  `/ikonki/grajek_plyta_winylowa.jpg` (live i instalacja – te same strumienie bajtów co pozostałe ikony systemu;
  ścieżki hosta nie są dostępne w gościu). Grajek czyta go i dekoduje **raz** przy starcie (`GrafikaPlyty`,
  `okladka.h`: limit 256 KiB, wymiary z nagłówka przed dekodowaniem, wspólny dekoder stb_image), skaluje raz do
  352 px (zapas na 200 % skali okładki 176 DIP) i zwalnia oryginał.
* `ikonki/grajek/*.png` – 14 ikon RGBA (284–404 px). Krok budowania `narzedzia/przygotuj_zasoby_grajka.py` (Pillow,
  zależność budowania jak generator tapet) tworzy `grajek_zasoby/grajek_<akcja>.png` 128×128 RGBA: proporcje i
  przezroczystość zachowane (Lanczos na premnożonej alfie), kolory bez zmian. 128×128 to limit dekodera ikon Ring 3.

| Plik użytkownika | Akcja | Przycisk |
|---|---|---|
| `previovs.png` | poprzedni utwór | transport |
| `play.png` / `pause.png` | Odtwórz / Pauza | jeden przycisk główny (ikona wg stanu) |
| `stop.png` | Stop | transport |
| `next.png` | następny utwór | transport |
| `mute.png` | wycisz / przywróć dźwięk | przełącznik (stan zaznaczony) przy suwaku głośności |
| `refresh.png` | odśwież bibliotekę | biblioteka |
| `add to.png` | dodaj do kolejki | biblioteka |
| `move up.png` / `move down.png` | przesuń w kolejce | kolejka |
| `delete.png` | usuń z kolejki | kolejka |
| `clear.png` | wyczyść kolejkę | kolejka |
| `save list.png` / `load list.png` | zapisz / wczytaj listę M3U8 | kolejka |

Bez pasującej grafiki (zostają tekstowe): **Zakończ** (brak ikony zamknięcia), Powtarzanie, Losowo, W tle,
Wizualizacja, Diagnostyka, karty i nagłówki. Brak pliku lub plik uszkodzony/zbyt duży → przycisk tekstowy z tą samą
akcją i podpowiedzią (nic nie jest zastępowane inną grafiką ani emoji).

Komponent **`BursztynUI::PrzyciskIkony`** (`bursztyn_ui/przycisk_ikony.h`, wielokrotnego użytku): `ObrazIkony`
dekoduje PNG raz (także porażka jest zapamiętana) i skaluje raz na bok (średnia obszarowa, premnożona alfa) w
`zmierz()`/układzie – nigdy przy rysowaniu. Stany: normalny, najechany (jaśniejszy kafel + obwódka akcentu),
wciśnięty, wyłączony (przygaszony kafel, ikona z alfą 40 %), fokus klawiatury (pierścień akcentu), zaznaczony
(odcień akcentu + pierścień), rodzaj Główny (większy, okrągły, stały pierścień). Klawiatura: Spacja/Enter;
podpowiedzi po polsku (`ustaw_podpowiedz`). Ikona leży na **jasnym kaflu motywu**: korpus większości grafik ma kolor
#25252A, praktycznie taki jak przycisk ciemnego motywu (#262B31) – bez kafla „Stop”, strzałki i „Odśwież” byłyby
niewidoczne. Kafel nie zmienia samej grafiki.

## Biblioteka, kolejka, listy
* Skan przyrostowy przy starcie i po „Odśwież bibliotekę”; podkatalogi opcjonalnie (≤ 3 poziomy, ≤ 32 katalogi);
  ≤ 512 utworów (dynamicznie przydzielona kolekcja); nazwy UTF‑8 ze spacjami i polskimi znakami.
* Sortowanie: tytuł (naturalnie, po polsku: a < ą < b), wykonawca, ścieżka. Szukanie bez wielkości liter i bez
  polskich znaków (np. „zolc” znajduje „Żółć”).
* Dwuklik/Enter w bibliotece: kolejka = bieżący widok, start od wybranego. Kolejka: dodaj, usuń, w górę/w dół,
  wyczyść, losowo (każdy utwór raz na obieg), powtarzanie: wył./utwór/wszystkie, koniec listy.
* „Poprzedni” po > 3 s wraca na początek utworu.
* Listy M3U8 zapisywane w ustawieniach aplikacji (`/uzytkownicy/<u>/Ustawienia/grajek/kolejka.m3u8`, ≤ 4000 B,
  ok. 30–60 pozycji – przy przepełnieniu komunikat „zapisano N z M”). Przy wczytaniu akceptowane są tylko
  bezwzględne ścieżki pod `/dane/muzyka/` bez `.`/`..`, `\`, znaków sterujących i niepoprawnego UTF‑8.

## Korektor i wizualizacja
10 pasm (31 Hz – 16 kHz), ±12 dB, filtry szczytowe RBJ (Q 1,41) w Q28 z 64‑bitowym akumulatorem i sprzężeniem
zwrotnym błędu kwantyzacji (usuwa „martwą strefę” niskich pasm). Zapas: tłumienie wstępne o największe dodatnie
wzmocnienie, nasycenie do S16. Wyłączony korektor lub wszystkie pasma 0 dB = dokładne przejście bit w bit.
Presety: Płaski, Basy, Rock, Pop, Wokal, Własne ustawienia. Wizualizacja: wskaźniki L/P (szczyt, skala −60..0 dB)
i widmo 24 pasm (odświeżanie 4 Hz); liczona tylko gdy okno jest widoczne i opcja włączona; opóźnienie względem
dźwięku ≤ 85 ms. **Domyślnie wyłączona** (pomiar w QEMU: odrysowania zwiększają przerwy dźwięku), włączana jednym
kliknięciem i zapamiętywana.

## Układ okna
Proporcjonalny, a wysokości i minima pochodzą z **metryk bieżącej czcionki** (natywnie TTF przez BSF – inne niż
czcionka bitmapowa hosta, na której wcześniej sprawdzano układ):
* nagłówek „Grajek” (skala 2) ma wysokość z czcionki (wcześniej stałe 40 px obcinały dolny łuk „j”),
* biblioteka ≈ 31 % szerokości, karty ≈ 42 % reszty; **minimum panelu liczone z zawartości** (kolumny tabel z
  pomiaru nagłówków i typowych wartości, siatki przycisków, 10 kolumn korektora) – przy 1080 px nagłówki
  biblioteki („Format”) mieszczą się bez przewijania,
* kolejka: jeden wiersz 6 ikon (w górę, w dół, usuń, wyczyść, zapisz, wczytaj) w równych odstępach; biblioteka:
  ikony „Odśwież” i „Dodaj do kolejki” wyrównane do lewej (pełne opisy w podpowiedziach),
* korektor: wiersz [Włączony][preset], etykiety dB i Hz w kontrolce `EtykietyPasm` wyśrodkowane nad/pod
  suwakami, a gdy kolumna jest węższa niż tekst – naprzemiennie w dwóch rzędach (wcześniej „125”, „250”, „500”
  stykały się). `SkalaUI` nie schodzi poniżej 100 %, więc zmniejszanie czcionki nie jest możliwe. Opis korektora ma
  wysokość z zawijania tekstu (wcześniej stałe 52 px ucinały trzeci wiersz w TTF),
* sterowanie: wiersz transportu [stop][poprzedni][**Odtwórz/Pauza**][następny] – grupa symetryczna, przycisk
  główny dokładnie na środku okna, większy i okrągły; po prawej wyciszenie tuż przy suwaku głośności i wartość %.
  Drugi wiersz (Losowo, Powtarzanie, W tle, Wizualizacja, Diagnostyka, Zakończ) w kolumnach Auto – bez nachodzenia
  przy minimalnym oknie; teksty zmienne („Powtarzanie: wszystkie”, „wycisz.”) mają minimum najszerszego wariantu.
  Cel kliknięcia ikony ≥ 34 DIP,
* nagłówki kart z wysokością z czcionki (`Karty::ustaw_zawijanie_naglowkow`) – przy 150 % nie są przycięte,
* podpowiedzi BursztynUI (`podpowiedz.h`) zawijane i mierzone rzeczywistą czcionką (wcześniej stałe 8 px na bajt i
  16 px na wiersz – ramka węższa i niższa niż tekst TTF z polskimi znakami). Bufor podpowiedzi kontrolki: 63 B.
Sprawdzane testami hosta z czcionką bitmapową i z TTF przy 980×580, 1080×640, 1280×680, 1280×720, 1920×1040,
1920×1080 i 1470×870 (150 %).

## Odtwarzanie w tle
Opcja „Odtwarzanie w tle”: zamknięcie okna (X lub zdarzenie zamknięcia) minimalizuje je, a muzyka gra dalej;
okno przywraca się z paska zadań albo z menu Start (pojedyncza instancja – aktywacja istniejącego okna). Przycisk
„Zakończ” zawsze zatrzymuje muzykę i kończy proces (także Menedżer zadań). Bez tej opcji minimalizacja wstrzymuje
odtwarzanie, a zamknięcie zamyka strumień PCM i kończy proces. Żadnej usługi systemowej ani podwyższonych praw.

## Bezpieczeństwo i limity
* Prawa: `okna`, `pliki_czytaj`, `ustawienia_aplikacji` (bez `pliki_zapisz` – muzyka jest tylko czytana).
* Wszystkie wejścia (WAV, MP3, ID3, RIFF, obrazy, M3U8, ustawienia) traktowane jako niezaufane: arytmetyka
  64‑bitowa granic, limity rozmiarów i liczby elementów, brak rekurencji bez limitu.
* Dekoder MP3: limit pojedynczej alokacji 8 MiB, budżet 1 MiB odczytu na wywołanie bez wyprodukowania próbek
  (uszkodzony plik kończy się błędem zamiast pętli).
* Całe dekodowanie i DSP w Ring 3; jądro dostaje tylko PCM przez BWS 110 (kopiowane i sprawdzane przez jądro).

## Muzyka na dysku zainstalowanego systemu
Zainstalowany Bursztyn OS montuje system plików BSP2 z partycji dysku, więc `/dane/muzyka` jest **trwałe**: Grajek
czyta WAV i MP3 bezpośrednio z dysku (`czytaj_fragment_pliku`, okno 64 KiB, przesunięcia 64‑bitowe – cały plik nigdy
nie trafia do RAM), także pliki większe niż dawny 128 MiB RAM‑dysk (limit formatu ok. 1,0 GiB na plik). Muzykę
dodaje się bez przebudowy ISO:
* z komputera hosta – **Bursztynowy Importer Plików** (`docs/bursztynowy-importer.md`), przy wyłączonej maszynie,
* w Bursztyn OS – Eksploratorem (kopiowanie, przenoszenie, foldery; pasek stanu pokazuje wolne miejsce).

Po skanie komunikat biblioteki mówi, gdzie leżą pliki (BWS 111, `jadro/bws/bws_magazyn.h`):
„Pliki są na dysku trwałym.” albo – w sesji Live / awaryjnym RAM FS – „Sesja Live: system plików działa w RAM –
pliki znikną po restarcie.” (komunikat ostrzegawczy). Grajek nie opisuje RAM‑dysku jako pamięci trwałej i nie ma
żadnego specjalnego uprawnienia do plików (manifest: `okna`, `pliki_czytaj`).

## Muzyka demonstracyjna w ISO (moduł `muzyka.pak`, tylko na żądanie)
**Domyślne ISO nie zawiera muzyki i nie czyta katalogu `dane/muzyka/`** (`MUZYKA_W_ISO ?= 0`). Moduł służy wyłącznie
do testów i demonstracji sesji Live: `make MUZYKA_W_ISO=1 iso`. Osoba budująca ISO odpowiada za prawa do
umieszczonych nagrań.
`narzedzia/package_muzyka.py` pakuje wtedy pliki z `dane/muzyka/` (uzupełnione przez użytkownika; repozytorium nie zawiera
muzyki) do modułu Multiboot2 w kontenerze BZP3 (jak galeria). Jądro przy rozruchu **żywego** systemu kopiuje je do
`/dane/muzyka/` (bez nadpisywania, od najmniejszych, CRC przed zapisem, wpis pomijany w całości, gdy na RAM‑dysku
zostałoby < 24 MiB – `psf_wolne_bajty()`). Instalator muzyki nie kopiuje.
Nazwy plików ≤ 55 bajtów (limit PSF). RAM‑dysk żywego systemu ma 128 MiB wspólne z systemem – duże pliki WAV mogą
się nie zmieścić: każdy pominięty plik jest wypisywany z nazwą, rozmiarem i wolnym miejscem
(`[MUZYKA] pominieto (brak miejsca na RAM-dysku): <nazwa> (N MiB, wolne M MiB, rezerwa 24 MiB)`), a paker przy
budowie ostrzega, gdy muzyka w module przekracza orientacyjnie 64 MiB. Limity pakera: 64 MiB na plik, 160 MiB na
moduł, 256 plików. Prywatne nagrania w `dane/muzyka/` są ignorowane przez git (zostaje tylko `README.md`).

## Testy
* `make -j1 test-grajek-host` – logika (ASan/UBSan): tekst, WAV (9 formatów + fuzz), ID3 (fuzz), MP3 z ramek
  syntetyzowanych bit po bicie (rzeczywisty ton 1 kHz w L i 2 kHz w P, kolejność kanałów, przewijanie co do próbki,
  VBR/Xing, MPEG‑2, uszkodzenia, budżet pętli), konwersja częstotliwości (porcje = całość), korektor (odpowiedź,
  stabilność, zgodność z double ≤ 1 LSB), analiza, silnik na prawdziwym mikserze jądra (BUSY, zapisy częściowe,
  niedobory, pauza, przewijanie, zmiana utworu, sprzątanie), biblioteka, kolejka/M3U8, konfiguracja, okładki.
* `make -j1 test-grajek-host` obejmuje też `test_ciaglosc`: niedobór utworu/sesji co do ramki po symulowanym
  przestoju GUI, odczyt przed CLOSE (przestój tuż przed przewinięciem), brak niedoboru przy pauzie, przewijaniu,
  wznowieniu i końcu utworu, punkt kontrolny `dopompuj()` (przestój 120 ms podzielony punktem kontrolnym – bez
  niedoboru; bez punktu – niedobór), przerwy producenta.
* `make -j1 test-grajek-ui-host` – kontroler i okno BursztynUI; `test_odrysowania_i_uklad`: liczba i obszar
  odrysowań w stanie ustalonym, brak pełnych odrysowań i przeliczeń układu przy komunikatach, kolejka pełna po
  cyklu rysowania, zero odrysowań w tle, diagnostyka, układ przy 3 rozdzielczościach i skali 150 % (rozłączność
  kontrolek, etykiety Hz w kolumnach, opis pod suwakami, nagłówki kolumn, długi tytuł), 10 działających suwaków.
* `test_komunikat_niedoboru` (w `test-grajek-ui-host`): regresja natywnego defektu licznika – niedobór w utworze A,
  drugi niedobór w oknie 5 s (odświeżenie po oknie), przewijanie/pauza/wznowienie bez zerowania, zmiana utworu →
  natychmiast „0 ms w tym utworze” przy zachowanej sesji, wyświetlana wartość nigdy większa niż licznik bieżącego
  utworu, licznik monotoniczny w obrębie utworu.
* `make -j1 test-grajek-ui-ttf-host` – układ z **prawdziwym TTF** (BSF + DejaVu, `-DBSF_NATIVE=1`): 5 rozmiarów ×
  2 stany diagnostyki × 3 karty (nagłówek, kolumny bez przewijania, przyciski, etykiety pasm rozłączne i nad
  suwakami, opis w całości, wiersz sterowania, pasek stanu), stabilność położenia przy przełączaniu diagnostyki,
  odrysowania częściowe, tło, podpowiedzi BursztynUI. Wspólne sprawdzenia: `testy/grajek_uklad_sprawdzenia.h`.
* `make -j1 test-muzyka-pak-host` – moduł muzyki (rozszerzenia, katalog, rezerwa, brak nadpisywania).
* `test_ikony_i_grafiki` (w `test-grajek-ui-host`): 14 ikon zdekodowanych raz i przypisanych do akcji, płyta =
  oryginalny plik bajt w bajt, dekodowana raz, okładka skalowana raz na rozmiar (zero skalowań w 2 s odtwarzania),
  okładka z tagu ma pierwszeństwo, 150 % (ikony w nowym boku bez ponownego dekodowania), stany (najechanie,
  wyłączenie, wciśnięcie i zwolnienie poza przyciskiem, zaznaczenie), Spacja/Enter, fokus, podpowiedź w obszarze
  roboczym przy kursorze, brak grafik (przyciski tekstowe działają), uszkodzony PNG, za duży PNG, obcięty JPEG.
* `test_grafika_plyty`, `test_opoznienia_wav_mp3` (w `test-grajek-host`): JPEG użytkownika (progresywny) – jeden
  odczyt i dekodowanie, zachowany średni kolor; MP3 i WAV przy przestojach 40/120/333/200 ms – niedobór dokładnie
  = przestój − kolejka, wyjście bez przerw = dekodowanie referencyjne (bez duplikatów i pominięć).
* `make -j1 test-zasoby-grajka` – narzędzie konwersji (mapa = pliki użytkownika, 128×128 RGBA, przezroczystość i
  proporcje, determinizm, oryginały niezmienione) i spójność listy akcji w narzędziu, Makefile, jądrze i Grajku.
* `make -j1 test-grajek` – wszystkie powyższe (zależą od `grajek_zasoby/`, tworzonego automatycznie).

## Znane ograniczenia
* Brak wątków Ring 3: produkcja PCM współdzieli pętlę z GUI. Długie operacje (pierwsze przewinięcie MP3 buduje
  tablicę punktów – pełny skan nagłówków pliku; dekodowanie dużej okładki) wykonują się synchronicznie – przy
  przewijaniu i ładowaniu strumień jest zamknięty, więc nie powodują niedoboru, ale chwilowo wstrzymują interfejs.
* Kolejka BWS 110 to tylko 85 ms, a Ring 3 nie ma wątków: każdy przestój procesu Grajka dłuższy niż kolejka (pełne
  odrysowanie okna pod emulacją, długa kompozycja/prezentacja w PID 0, opóźnienie planisty) kończy się niedoborem –
  jest raportowany, nie maskowany. Grajek może jedynie skracać i dzielić własną pracę (punkty kontrolne). Najmniejsza
  bezpieczna poprawa po stronie systemu: większa pojemność kolejki strumienia (`Pcm::CAPACITY_FRAMES`, np. 8192 =
  171 ms, +16 KiB BSS na strumień) – zmienia opóźnienie pauzy/FINISH i wymaga osobnej weryfikacji natywnej; nie
  została wprowadzona.
* Zmniejszanie częstotliwości (> 48 kHz) bez filtru antyaliasingowego.
* Pozycja odtwarzania jest dokładna do zawartości kolejki jądra; wizualizacja wyprzedza dźwięk o ≤ 85 ms.
* Brak FLAC/OGG (interfejs `Dekoder` przygotowany), brak gapless.

## Wyniki natywne (QEMU TCG, 2026-11-21, ISO produkcyjne, 2 przebiegi)
* Import `muzyka.pak`: 2 z 3 plików (`Bursztyn OS 2.mp3`, `Electric Pulse.wav`); `2Heartbeat Loud.wav` (49 MB)
  pominięty przez rezerwę 24 MiB RAM‑dysku – zgodnie z projektem.
* Uruchomienie z menu Start, skan, metadane, okładka JPEG z APIC, odtwarzanie MP3 i WAV, pauza/wznowienie,
  przewijanie w pauzie, odtwarzanie w tle (zamknięcie = minimalizacja, dźwięk gra dalej), brak `[USER FAULT]`.
* Analiza nagrania wyjścia HDA (`--audio-wav`), przebieg 2 (po ograniczeniu odrysowań): WAV z wizualizacją 17,2 %
  ciszy / 5,1 przerwy ≥ 5 ms na s; WAV bez wizualizacji 3,7 % / 1,2 na s; MP3 z wizualizacją 11,4 % / 3,7 na s.
  Odtwarzanie w tle (bez rysowania, przebieg 1): 0–6 % ciszy. Na sprzęcie fizycznym nie testowano.
* Korekta (2026-11-22): wcześniejsze zdanie „licznik `starved` = 533 ms, reszta przerw powstaje poza kolejką Grajka”
  nie ma pokrycia w zapisanych pomiarach (brak surowego logu tej liczby, nie wiadomo, którego przebiegu dotyczyła),
  a zrzut z komunikatem „łącznie 51658 ms” pokazuje, że licznik Grajka potrafi być duży. Licznik był sumą od
  uruchomienia procesu (wszystkie utwory i przebiegi), więc nie da się go porównać z procentem ciszy jednego
  nagrania. Podział przerw na „Grajek” i „transport HDA” pozostaje hipotezą do weryfikacji nowymi licznikami
  (niedobór utworu + przerwa producenta) i statystykami HDA (`bezpieczna_cisza`, `utracone_okresy`).
* Po zmianach z 2026-11-22 (odrysowania częściowe, punkty kontrolne PCM, liczniki) **nie wykonano** testów natywnych.

## Wyniki natywne (QEMU TCG, 2026-11-22, jedna sesja, ISO `3c335885…`)
Fazy (po ~82 s analizy każda, bez ładowania/pauzy/końca utworu), cisza = próbki zerowe L+R w nagraniu HDA:
| Faza | Cisza | Przerwy ≥ 5 ms | Okresy pominięte przez HDA | Niedobór Grajka |
|---|---|---|---|---|
| WAV, wizualizacja wył. | 1,2 % (994 ms) | 45 | 50 (≈ 1066 ms) | 42 ms |
| WAV, wizualizacja wł. | 3,3 % (2721 ms) | 125 | 156 w szerszym oknie | 0 ms przyrostu |
| WAV w tle (zminimalizowane) | 0,0 % | 0 | 8 (≈ restart utworu) | 0 ms |
| MP3, wizualizacja wł. | 2,9 % (2350 ms) | 109 | 118 (≈ 2517 ms) | 0 ms (192 ms z komunikatu dotyczyło WAV – zob. wyżej) |
* Mediana przerwy 21 ms, maksimum 42 ms = 1–2 okresy HDA (1024 ramki). Cisza w nagraniu ≈ liczba pominiętych
  okresów × 21,3 ms; niedobór Grajka jest o rząd wielkości mniejszy. **Pozostałe przerwy to okresy pominięte przez
  ochronę własności DMA (`Action::Skip`), nie brak danych Grajka.**
* Maksymalizacja/przeciąganie okna podczas odtwarzania: 7–8 % ciszy, przerwy do 277 ms, ~300 ms niedoboru Grajka,
  2 odzyskania DMA przez watchdog (bez błędów DMA).

### Związek GUI → przerwy HDA (analiza kodu, 2026-11-23)
* Przerwanie zegara HDA tylko aktualizuje watchdog (`hda_pcm_watchdog_irq`), nie miksuje. Okresy wypełnia praca
  odroczona (`hda_obsluz_okresowo`) wyłącznie w punktach kontrolnych: wejście/wyjście każdego wywołania systemowego,
  między pasami BWS 57 (`scheduler_oddaj_cpu_w_syscallu`), w pętli PID 0, między regionami prezentacji kompozytora
  (`skladacz_obrazu.cpp`), w oczekiwaniu VirtIO-GPU.
* Okres można zapisać tylko, gdy jest ≥ `GUARD_BYTES` (2,25 okresu = 48 ms) przed pozycją DMA; wyprzedzenie to 4
  okresy (85 ms; piąty tylko przy ≥ 1024 ramkach w kolejkach). Praca odroczona spóźniona o więcej niż ok. 37 ms
  (59 ms z piątym okresem) zastaje okres zbyt blisko sprzętu → pominięcie (cisza 21 ms) – zgodnie z projektem.
  Natywnie `maks_czekanie_ns` pracy = 57–58 ms, `maks_spoznienie_ns` do 175 ms.
* Potwierdzone: przerwy są skorelowane z odrysowaniami okna Grajka (tło bez odrysowań: 0 przerw; ~2 odrysowania/s –
  postęp i wiersz diagnostyki: 0,55/s; + wizualizacja 4 Hz: 1,3–1,5/s; karta/maksymalizacja/przywrócenie: 2,4/s) i mają długość okresów HDA.
* **Niepotwierdzone (hipotezy do natywnego śledzenia)**: który odcinek bez punktu kontrolnego przekracza ~37 ms –
  kompozycja PID 0 (składanie warstw przed pętlą prezentacji), prezentacja VirtIO-GPU, niedzielony blit BWS 57
  (≤ 128 tys. px, np. 57 tys. px wizualizacji, wykonywany atomowo), czy odcinki z IF=0. Rysowanie w Ring 3 jest
  wywłaszczalne (tick 4 ms), więc bezpośrednio nie blokuje pracy HDA.
* Propozycja (niewdrożona): opcjonalny, wyłączony domyślnie ślad pominięć HDA (dla każdego `Skip`: czas od żądania,
  ostatni punkt kontrolny, PID, numer ostatniego BWS, czy trwała kompozycja), a po wskazaniu odcinka – wąska zmiana
  (np. punkt kontrolny HDA w pętli składania kompozytora albo dzielenie blitów > 64 tys. px) z deterministycznym
  testem modelu hosta. Bez zmian w buforach PCM i ochronie DMA.

## Otwieranie pliku z systemu (dwuklik MP3/WAV)

* Skojarzenie: `jadro/gui/skojarzenia_plikow.h` (`.mp3`, `.wav` → `/programy/grajek.cebula/grajek.bur`, bez
  rozróżniania wielkości liter) – to samo miejsce co `.txt` (Notatnik) i obrazy (Przeglądarka Obrazów);
  `otworz_plik_skojarzony` używa go w Eksploratorze i na pulpicie.
* Przekazanie ścieżki: ogólny BWS 10 (program + argument startowy, do 511 B, bez obcinania). Grajek nie działa →
  nowy proces, argument przez BWS 45 przy starcie (`grajek.cpp`). Grajek działa → jądro podmienia argument
  istniejącego procesu i wysyła `BWS_ZDARZENIE_OTWORZ_PLIK` (`OknoGrajka::otworz_argument_startowy`).
* `Odtwarzacz::otworz_plik`: ścieżka bezwzględna bez `.`/`..`/pustych składników i znaków sterujących, < 192 B;
  utwór z biblioteki albo analiza tego jednego pliku (`Biblioteka::analizuj_plik` – ta sama droga co skan, o formacie
  decyduje zawartość). Pliki spoza `/dane/muzyka` (jeśli PZB pozwala je czytać) są dopisywane jako „zewnętrzne”
  (`dodaj_zewnetrzny`, maks. 32 na sesję): mają zwykłe indeksy, nie pojawiają się w widoku biblioteki, przetrwają
  ponowny skan, nic nie jest kopiowane. Kolejka: utwór już w kolejce → przejście do jego pozycji, inaczej dopisanie
  na koniec; start od razu (BWS 110, strumieniowo). Ten sam utwór już grający – gra dalej; w pauzie – wznowienie.
  Błąd → polski komunikat, brak automatycznego przejścia na inny utwór.
* Grajek otwarty z Eksploratora dziedziczy prawa `rodzic & manifest`; Eksplorator ma więc `ustawienia_aplikacji`
  (zapis wyłącznie w katalogu ustawień danej aplikacji), żeby Grajek mógł zapisać konfigurację i kolejkę.
* Ograniczenia: zminimalizowane okno Grajka nie jest przywracane (brak API samoaktywacji okna); kolejka M3U8 po
  ponownym starcie odtwarza tylko utwory z biblioteki.
