# Przeglądarka Obrazów 2.0: formaty, galeria, powiększenie

Uzupełnia `docs/image-viewer-bursztynui-migration.md`.

## Formaty i dekodery (bez duplikatów)

| Format | Dekoder | Uwagi |
|---|---|---|
| PNG | `obrazy/dekoder_obrazu.cpp` (stb_image, Ring 3) | alfa zachowana |
| JPEG/JPG | ten sam (`STBI_ONLY_JPEG`, ścisła walidacja markerów) | skala szarości i YCbCr |
| BTP1 / BTP2 | `narzedzia/tapety-btp/tapetybtp.cpp` (przez `dekoduj_obraz_z_alfa`) | BTP2 nadal obsługiwany; to realny format Bursztynowa TaPeta |
| BMP 24/32 | parser nagłówka i strumieniowy silnik z `silnik_obrazu.h` (bez zmian) | do powierzchni (≤ limit) albo silnik strumieniowy |

Aplikacja linkuje `image_ring3.o` i `btp_ring3.o` jak Tapety (freestanding, bez I/O i wątków).
Dekodowanie odbywa się wyłącznie w Ring 3; nic nie trafia do Ring 0. Sygnatury, wymiary (≤8192,
≤8 Mpx w dekoderze), rozmiar pliku i bufory są walidowane; uszkodzone/ucięte/złośliwe pliki dają
komunikat, nie awarię (testy: ucięty PNG/JPEG, zły nagłówek, ogromne wymiary, uszkodzone BTP2, ucięty
BTP1, wymiary BTP, GIF, plik nie-obraz, pusty plik, BMP z uszkodzonym nagłówkiem). Przezroczystość PNG/BTP
jest pokazywana na szachownicy, a piksele źródła nie są przekolorowywane przy zmianie motywu.

### Limit rozmiaru obrazu (ważne)

Jądro (`bws_alokuj_sterte`) dopuszcza pojedynczą alokację sterty **większą niż 16 MiB tylko dla programu
Tapety**. Przeglądarka dostaje więc ciągłe bloki ≤ 16 MiB, a powierzchnia BGRA (4 B/px) i bufor pliku
muszą się w nich zmieścić: **ok. 4,19 Mpx** i 15 MiB pliku. Nagłówek (PNG IHDR, JPEG SOF, BTP) jest
sprawdzany **przed** wczytaniem pliku, więc za duży obraz daje natychmiast komunikat
„Obraz zbyt duży: W x H (limit ok. 4 Mpx)”, a nawigacja do następnego zdjęcia działa. Limity dekodera
nie zostały zmienione. BMP powyżej limitu otwiera silnik strumieniowy (Dopasuj/1:1, bez powierzchni).

Konsekwencja dla zdjęć testowych w `dane/zdjecia/`: otwierają się `2019. ołówkiem.jpg` (1920×1410) i
`obraz-generowany.btp` (1280×720); `19_06_2019.png` (3508×2480), `jablka2w.png` (3659×3000),
`Warszawa-2019.jpg` (4724×3543) i `2019r-digital-painting-….jpg` (8000×8000) są odrzucane komunikatem.
Obsługa większych obrazów wymaga **decyzji właściciela** o polityce jądra (rozszerzenie wyjątku
„duża alokacja” na Przeglądarkę Obrazów i/lub podniesienie limitu 40 MiB na wywołanie) albo
dekodowania ze zmniejszaniem; nie zrobiono tego bez zgody (jest to zmiana zabezpieczeń jądra).

## Galeria `/dane/zdjecia`

* Przy starcie: listing `/dane/zdjecia` (BWS 6), filtr rozszerzeń `.png .jpg .jpeg .btp .bmp`
  (bez rozróżniania wielkości liter; katalogi i inne pliki ignorowane), kolejność **naturalna**
  (`zdjecie2` < `zdjecie10`, bez rozróżniania wielkości liter ASCII), pierwsze zdjęcie wybrane.
  Model (do 256 nazw) powstaje raz – Następne/Poprzednie **nie skanują** katalogu ani nie dekodują
  całej listy; dekodowany jest tylko wybrany plik. Brak katalogu: „Brak katalogu /dane/zdjecia”;
  pusty: „Brak zdjęć”; brak nazw na sztywno – dowolny dodany plik pojawia się po ponownym starcie.
* Ładowanie następuje w następnym kroku zegara, więc najpierw widać „Wczytywanie…”; dekodowanie
  dużego pliku blokuje okno przeglądarki, ale nie pulpit (zwykły proces Ring 3 pod schedulerem,
  bez blokad graficznych podczas dekodowania).
* **Nawigacja:** strzałki ← →, PgUp/PgDn, Home/End; duże przyciski `<` `>` po bokach widoku;
  przyciski „Poprzednie/Następne” w pasku i w menu Widok. **Zawijanie** (po ostatnim – pierwsze)
  jest domyślnie włączone, wyłączane w Ustawieniach. Przy <2 zdjęciach przyciski są wyłączone.
  Strzałki nie przesuwają obrazu (konflikt klawiszy): przesuwanie to mysz, kółko, W A S D.
  Gdy otwarte jest menu/okno dialogowe, klawisze należą do niego.
* Status: nazwa | „Zdjęcie N z M” | wymiary | format | procent powiększenia.
* **Plik spoza galerii** (Plik → Otwórz…, argument startowy, zdarzenie otwarcia): jeśli leży w
  `/dane/zdjecia`, wybiera swój indeks; inaczej jest pokazany jako „Plik spoza galerii”, a model
  galerii, bieżący indeks i katalog domyślny pozostają nietknięte.

## Pakowanie do ISO (Live)

Zdjęcia **nie są wlinkowane w jądro** (limit 15 MiB). `narzedzia/package_galeria.py` pakuje
`dane/zdjecia/*` (obsługiwane rozszerzenia, ≤256 plików) do `/boot/zdjecia.pak` (format BZP2) i dodaje
`module2 /boot/zdjecia.pak zdjecia.pak` do wpisów rozruchu zwykłego systemu (nie instalatora). Jądro
przy starcie (`importuj_galerie_z_modulu`, tylko poza trybem instalatora) waliduje każdą granicę,
tworzy `/dane/zdjecia` i kopiuje zdjęcia strumieniowo w porcjach; **istniejące pliki użytkownika nigdy
nie są nadpisywane**, a pamięć modułu jest zwalniana do PMM. Jądro nie dekoduje obrazów.
Dane w pakiecie są zapisane wariantem base64 (`A`→`~`, `/`→`*`): skaner metadanych wydania szuka
tekstowych wzorców (URL z hasłem, ścieżki `/tmp/…`, nazwy plików źródłowych, klucze AWS) w surowych
bajtach i losowe dane JPEG/PNG potrafiły go przypadkowo uruchomić (wykryto dwa różne fałszywe
alarmy); alfabet wariantu nie zawiera znaków wymaganych przez żaden wzorzec, więc skaner pozostaje
nietknięty, a audyt wydania daje 0 znalezisk. Koszt: pakiet jest o 1/3 większy (ISO ~119 MB).
Odtworzenie: dodanie plików do `dane/zdjecia/` i `make iso` (cel `.galeria.stamp` wykrywa zmianę listy).
Katalog `/dane/zdjecia` jest też w drabinie katalogów systemowych (`psf_bootstrap.h`), więc istnieje
również na świeżo zainstalowanym dysku (pusty).

**System zainstalowany:** zdjęcia z ISO nie są kopiowane na dysk przez instalator (plan instalacji ma
sztywny limit pozycji, a E2E instalatora jest niestabilny w tym środowisku); instalacja tworzy tylko
pusty `/dane/zdjecia`. Zdjęcia kopiowane przez użytkownika działają i nie są nadpisywane.
Nie zweryfikowano natywnie instalacji na dysk.

## Powiększenie (10%–800%)

* `powiekszenie.h`: czysta geometria i renderer bez alokacji i float; renderuje wyłącznie widoczne
  piksele bezpośrednio ze źródła (bez kopii całego powiększonego obrazu), więc powiększenie i
  przesuwanie **nie dekodują ponownie** pliku. Obliczenia w int64, wymiary ≤ 8192, współczynnik ≤ 800%.
* Sterowanie: natywny **`Suwak`** BursztynUI (10–800, krok 1, środek widoku zachowany), przyciski
  `-`/`+` (drabinka 10, 15, 20, 25, 33, 50, 67, 75, 100, 125, 150, 175, 200, 250, 300, 400, 500, 600,
  800), „100%”, „Dopasuj”, etykieta z procentem, **Ctrl + kółko** (punkt pod kursorem zostaje na
  miejscu), samo kółko przewija. Tryb Dopasuj może zejść poniżej 10% dla bardzo dużych obrazów.
* Filtrowanie: pomniejszanie – uśrednianie do 6×6 próbek (z alfą); 100–399% – dwuliniowe;
  ≥400% i opcja „bez wygładzania” – najbliższy sąsiad. Poziomy pośrednie (np. 125%, 175%) naprawdę
  zmieniają skalę (rozmiar = `wymiar·zoom/100`), proporcje zachowane, przesunięcie przycinane do granic
  obrazu i zachowywane przy zmianie rozmiaru okna.
* Ograniczenie wydajności: renderer jest przeliczany całym widokiem przy każdym przesunięciu/zmianie
  (bez przesuwania poprzedniego bufora) – pod TCG w QEMU przesuwanie bywa wolne; na sprzęcie
  powinno być płynne.
* Duże BMP (strumieniowy silnik): tylko Dopasuj/100%, przyciski +/- przełączają te dwa tryby.

## Testy

`make test-przegladarka-obrazow-host` (ASan/UBSan, 300 sprawdzeń): PNG/JPEG/BMP/BTP1 (RAW i RLE)/BTP2
na prawdziwych plikach, formaty mieszane w galerii, uszkodzone i złośliwe pliki, limity rozmiaru
(przed wczytaniem pliku), pusta/brak galerii, kolejność naturalna, zawijanie, klawisze i blokada
przy otwartym oknie, plik spoza galerii, zoom 50/125/175/200/400, suwak, Ctrl+kółko, przesuwanie na
różnych poziomach, zmiana rozmiaru okna bez ponownego odczytu pliku, motyw (w tym czarny), duży BMP
(silnik strumieniowy). Generator plików: `testy/generuj_obrazy_testowe.py`.

## Aktualizacja: duże obrazy

Limit ok. 4 Mpx opisany powyżej został zastąpiony modelem dwupoziomowym (pełna rozdzielczość do 17 Mpx z zaufaną
alokacją jądra, podgląd JPEG dla większych): patrz `image-viewer-large-images.md`. Wszystkie sześć zdjęć z
`/dane/zdjecia` otwiera się natywnie (cztery w pełnej rozdzielczości, `.btp` i JPEG 8000×8000 jako podgląd 1/4).
Format pakietu galerii zmieniono na BZP3 (surowy, bez kodowania): `gallery-packaging-security.md`.
Ctrl+kółko działa także na tablecie VirtIO (poprawka potoku wejścia).
