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
* osobno: **w tym utworze** (zerowany przy wczytaniu utworu) i **od uruchomienia** Grajka,
* statystyki są czytane co ~50 pompowań **oraz tuż przed każdym `PCM_CLOSE`/`PCM_FINISH`** (wcześniej końcówka
  zamykanego strumienia ginęła); zamknięty strumień nie jest liczony podwójnie (delta od ostatniego odczytu uchwytu),
* **przerwa producenta**: najdłuższy odstęp między kolejnymi uzupełnieniami kolejki w stanie Odtwarzanie (także do
  chwili zamknięcia strumienia) i liczba odstępów > 85,3 ms (każdy taki odstęp przy pełnej kolejce = niedobór),
* komunikat: „Przerwy w dźwięku – brak danych Grajka: X ms w tym utworze, Y ms od uruchomienia.”

### Diagnostyka (opcja „Diagnostyka”, klucz `diagnostyka=1`)
Pomiar czasu sekcji licznikiem TSC (bez wywołań systemowych w gorącej ścieżce i bez logów na port szeregowy):
dekodowanie, konwersja, korektor, analiza, BWS 110, widmo, rysowanie drzewa BursztynUI, publikacja (BWS 57),
tabele, wizualizacja, okładka. Co ~1 s drugi wiersz paska stanu pokazuje ms/s każdej sekcji, maksymalny czas
rysowania w cyklu, liczbę odrysowań, maksymalną przerwę producenta i niedobór utworu. Przy zakończeniu Grajka
podsumowanie trafia do ustawień aplikacji (`/uzytkownicy/<u>/Ustawienia/grajek/diagnostyka`). Pomiar nie widzi
czasu, w którym proces nie był wykonywany (np. kompozycja w PID 0) – to pokazuje dopiero przerwa producenta.

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
Brak okładki → domyślna grafika Grajka (generowana, bez plików).

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
Proporcjonalny: biblioteka ≈ 31 % szerokości (300–480 DIP), karty Kolejka/Korektor ≈ 42 % reszty (330–420 DIP),
okładka zmniejsza się w wąskim oknie, kolumny tabel dzielą szerokość tabeli (nagłówki nigdy węższe niż ich tekst,
nadmiar – przewijanie poziome), przyciski wypełniają komórki siatki sterowania. Opis korektora jest dokowany
przed siatką suwaków (wcześniej nachodził na suwaki i etykiety Hz). Sprawdzone testem hosta przy 980×580,
1280×680, 1920×1040 i skali 150 %. Pełny tytuł obciętego tekstu jest w podpowiedzi (bufor podpowiedzi BursztynUI:
63 B).

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

## Muzyka w ISO (moduł `muzyka.pak`)
`narzedzia/package_muzyka.py` pakuje pliki z `dane/muzyka/` (uzupełnione przez użytkownika; repozytorium nie zawiera
muzyki) do modułu Multiboot2 w kontenerze BZP3 (jak galeria). Jądro przy rozruchu **żywego** systemu kopiuje je do
`/dane/muzyka/` (bez nadpisywania, od najmniejszych, CRC przed zapisem, wpis pomijany w całości, gdy na RAM‑dysku
zostałoby < 24 MiB – `psf_wolne_bajty()`). Instalator muzyki nie kopiuje. Wyłączenie: `make MUZYKA_W_ISO=0`.
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
* `make -j1 test-muzyka-pak-host` – moduł muzyki (rozszerzenia, katalog, rezerwa, brak nadpisywania).
* `make -j1 test-grajek` – wszystkie powyższe.

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
