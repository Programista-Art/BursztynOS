# BSF 2.2 — raport końcowy (stan na 2026-11-12)

> **Status:** milestone'y 1–7 są zaimplementowane i przechodzą testy hostowe
> z implementacjami referencyjnymi. Weryfikacja natywna przeszła w całości:
> UEFI 1280×720, UEFI 1920×1080 i BIOS, po 35/35 sprawdzeń. Nie wszystkie
> wymagania pełnego BSF 2.2 są spełnione; ograniczenia wymienia sekcja 5,
> otwarte punkty sekcja 6.

## 1. Gałąź, baza, stan repozytorium

| | |
|---|---|
| Gałąź | `feature/bsf-2-2-font-formats` |
| Baza | `58131c0` (zweryfikowane BSF 2.1, `feature/bsf-2-1-text-integration`). BSF 2.1 **nie** jest w `test` (`test` = `544a51e`), więc zgodnie z instrukcją bazą jest commit BSF 2.1. |
| Commity | **brak** (nic nie zacommitowano, nie wypchnięto, nie scalono) |
| `main`, `test`, `UNITRONIX` | nietknięte |
| Drzewo robocze | ok. 80 zmienionych/nowych ścieżek (kod, testy, fixtury, dokumentacja) |

## 2. Co zostało wdrożone

### Milestone 1 — OpenType CFF (`bsf/cff.inc`)
- Rozpoznawanie formatu obrysów z danych (glyf/loca → TrueType, CFF2, CFF, same bitmapy kolorowe), a nie z rozszerzenia pliku.
- Bezpieczny parser CFF:
  - header, INDEX (offsety monotoniczne, offSize 1–4), Name/Top/String/GSubr/CharStrings, Private DICT, lokalne subrs;
  - charset, FontMatrix, czcionki CID-keyed (FDArray/FDSelect 0/3/4).
- Interpreter Type 2:
  - wszystkie operatory rysujące, flex, hintmask/cntrmask, subrs z biasem, `seac`, operatory arytmetyczne;
  - limity: stos 48/513, głębokość 10, 200 000 operacji, 384 stemy, 1 024 kontury.
- Krzywe sześcienne są rasteryzowane przez wspólny rasteryzator z adaptacyjnym podziałem. Ścieżka TrueType daje bit w bit ten sam wynik co w 2.1.

### Milestone 2 — kolekcje TTC/OTC
- Walidacja nagłówka `ttcf` i katalogu każdej twarzy (współdzielone tabele, zakazane nakładanie się, duplikaty tagów).
- Każda twarz jest osobną pozycją katalogu ze stabilnym id `ścieżka#indeks`.
- Leniwe ładowanie per twarz dla dużych plików:
  - przy wykrywaniu czytane są tylko tabele metadanych;
  - przy renderowaniu czytane są tylko tabele, których BSF używa, przez odczyty zakresowe BWS 59;
  - bufory tabel są liczone referencjami i współdzielone między twarzami tej samej kolekcji.

### Milestone 3 — katalog, Ustawienia, osobna czcionka Terminala
- Katalogi `/czcionki/{ttf,otf,ttc,otc}`; każdy przyjmuje tylko swoje rozszerzenie.
- Rozszerzone `FontInfo`: format, obrysy, twarz kolekcji, osie, kolor, status, rozmiar, nazwa pełna i PostScript.
- Ustawienia → Czcionki:
  - linia szczegółów czcionki;
  - podgląd pokazuje „brak glifów” zamiast cichego zastępstwa inną czcionką;
  - próbki CJK;
  - osobna lista „Czcionka terminala”;
  - instalacja `.otf/.ttc/.otc`.
- `Config` v2 z polem `terminal_family`; kernel migruje stary plik v1.
- Terminal przyjmuje wyłącznie czcionki o stałej szerokości (sprawdzane z rzeczywistych advance'ów), więc siatka komórek się nie zmienia.

### Milestone 4 — WOFF/WOFF2 (`bsf/webfont.cpp`, `bsf/inflate.inc`)
- WOFF 1.0: własny ograniczony inflater zlib, walidacja katalogu, limity rozmiaru i współczynnika kompresji sprawdzane **przed** alokacją.
- WOFF2:
  - Brotli v1.1.0 (MIT, przypięty hashem, tylko dekoder, wyłącznie w workerze Husarza);
  - pełna rekonstrukcja `glyf`/`loca` (w tym overlap bitmap) i transformacji `hmtx`.
- Wynik zawsze przechodzi przez ten sam walidator `Face`.

### Milestone 5 — Husarz `@font-face`
- Parser CSS zapisuje deskryptory `src`, `font-weight`, `font-style`, `font-stretch`, `unicode-range`, `font-display`.
- Źródła są próbowane po kolei:
  - `local()` dopasowuje zainstalowaną rodzinę;
  - nieobsługiwane `format()` jest pomijane bez żądania sieciowego;
  - nieudane źródło przechodzi do następnego.
- Asynchroniczne pobieranie w istniejącej kolejce zasobów (`RequestKind::Font`).
- **Dekodowanie odbywa się w osobnym procesie workera**; przeglądarka waliduje wynik ponownie.
- CORS po stronie odpowiedzi: `Access-Control-Allow-Origin: *` albo origin dokumentu.
- Fonty o zasięgu karty: niewidoczne w katalogu systemowym, z nazwami, których CSS strony nie może podrobić, zwalniane przy nawigacji i zamknięciu karty.
- Po załadowaniu czcionki style są przeliczane, a dokument przelicza układ i odrysowuje się.

### Milestone 6 — fonty zmienne (`bsf/variations.inc`)
- Ogólny model osi `fvar`/`avar` i nazwane instancje.
- Interpolacja `gvar`: krotki, regiony pośrednie, IUP, kompozyty, punkty fantomowe.
- CFF2 `blend`, HVAR (advance'y), MVAR (metryki linii), delty VariationIndex w GPOS.
- Instancja jest częścią id twarzy, więc cache nie mieszają instancji.

### Milestone 7 — fonty kolorowe i emoji (`bsf/color.inc`, `bsf/png.inc`)
- COLRv0/CPAL.
- COLRv1: wszystkie formaty paint 1–32, warianty Var*, 28 trybów kompozycji, ClipList.
- CBDT/CBLC (PNG 17/18/19) i sbix (`png `, `dupe`).
- Własny ograniczony dekoder PNG.
- Limity grafu: głębokość 32, 8 192 węzłów, wykrywanie cykli.
- Emoji:
  - preferowanie kolorowej czcionki przy VS16 i sekwencjach;
  - ligatury ZWJ dopasowywane jak w HarfBuzz;
  - rysowanie w kolorze w BursztynUI, Notatniku i Husarzu.
- W ISO dołączona **Noto Color Emoji COLRv1** (OFL, 4,8 MB).

### Inne zmiany w kodzie wspólnym (zachowanie BSF 2.1 zachowane)
- Limit szerokości glifu podniesiony z 8 do 32 em. Ligatura basmala w Noto Naskh ma 12,2 em i była wcześniej niesłusznie odrzucana.
- PairPos format 1: offsety device liczone od PairSet, zgodnie ze specyfikacją. Wcześniej błąd był nieszkodliwy, bo tablice device nie były w ogóle stosowane.
- W produkcyjnym ISO dodano Noto Sans OTF (Regular, Bold, CFF) w `/czcionki/otf`.

### Kernel (Ring 0): bez parsowania fontów
- Walidacja ścieżek czterech katalogów czcionek.
- Granica odczytu BWS 59: 64 MiB zamiast 16 MiB.
- Import modułów bootowania według rozszerzenia.
- Tworzenie nowych katalogów przy starcie.
- Transport `Config` v2 z migracją v1.

## 3. Wyniki testów (fakty zmierzone)

### Hostowe (ASan + UBSan, `make test-bsf22-host`)
| Cel | Wynik |
|---|---|
| `test-bsf22-cff-host` | 65 702 sprawdzeń; **16 395/16 395** obrysów identycznych z FreeType; średnia różnica rastra CFF 1,8–4,0 % (dla porównania TrueType DejaVu 4,4 %); 3 000 mutacji |
| `test-bsf22-shaping-host` | HarfBuzz: 120/120 (BSF 2.0), **44/44** CFF, **51/51** instancje zmienne, **22/22** emoji |
| `test-bsf22-collections-host` | 19 122 sprawdzeń; prawdziwa kolekcja Noto CJK 19,5 MB: wykrywanie czyta 5,8 MB, pierwsza twarz to 16,2 MB w pamięci, druga dokłada 0,45 MB; 2 704 obrysy CJK = FreeType |
| `test-bsf22-font-catalog-host` | 307 sprawdzeń (migracja v1→v2, Terminal, refresh, znikające wybory) |
| `test-bsf22-woff-host` | 62 085 sprawdzeń; 10 fontów zdekodowanych zgodnie ze źródłem; bomba Brotli (32 MiB w 103 B) odrzucona; 7 500 mutacji |
| `test-bsf22-husarz-webfonts-host` | 185 sprawdzeń (CSS, kolejność `src`, CORS, izolacja zasięgów, szerokości przed i po załadowaniu fontu) |
| `test-bsf22-variations-host` | 43 107 sprawdzeń; 2 718 + 5 192 + 6 357 obrysów = FreeType we wszystkich testowanych instancjach (TrueType `gvar` i CFF2) |
| `test-bsf22-color-host` | 1 093 sprawdzeń; CBDT 53/53 identyczne z FreeType; COLRv1 vs Skia: średnio 0,38 %, najgorzej 0,72 %; 6 000 mutacji |
| `test-bsf22-emoji-host` | 192 sprawdzeń (ZWJ, odcienie skóry, flagi, keycapy, VS15/VS16, linia bazowa) |

### Fuzzing sterowany pokryciem (`make fuzz-bsf22`)
| Cel | Wykonania | Krawędzie | Korpus | Znaleziska |
|---|---|---|---|---|
| font (sfnt/TTC/CFF/CFF2/gvar/kolor) | 300 016 | 3 447 | 954 | 0 |
| woff/woff2 | 300 013 | 2 737 | 293 | 0 |
| png | 300 012 | 373 | 115 | 0 |

Krótka kampania; nie dowodzi braku błędów w dekoderach.

### Regresja BSF 2.0/2.1
Te zestawy przechodzą:
- bidi (BidiTest 770 241, BidiCharacterTest 91 707);
- layout (409 086 sprawdzeń), layout 2.1;
- UI, BursztynUI, Notatnik (bidi i Unicode), Husarz (8 zestawów), shell, PSF;
- ABI kernela, autoryzacja ustawień, motywy.

Wyjątki:
- **`test-desktop-ttf-host` nie przechodzi**, ale identycznie nie przechodzi na czystej bazie BSF 2.1, więc to błąd sprzed tej pracy.
- Zmieniłem 4 asercje, które sprawdzały stary kontrakt BSF 2.1. Każda sprawdza teraz nowe zachowanie; żadna nie została usunięta:
  - mocki zgłaszają nowe katalogi czcionek jako nieistniejące;
  - plik większy niż 16 MiB jest akceptowany;
  - granica odczytu BWS 59 w teście autoryzacji to `MaxFontFile`;
  - porównanie WOFF2 pomija bity `head` i tabelę `DSIG`, które zmienia sam enkoder.
- `test-bsf-ui-host` wykonuje 9 069 sprawdzeń zamiast 9 084. Sprawdziłem przyczynę: próbka flagi idzie teraz przez układ kształtowany, ma inną szerokość, więc pętla po pikselach wykonuje mniej iteracji. Żadna asercja nie została usunięta.

### Natywnie (QEMU, produkcyjne ISO plus fixtury kolekcji)
- **UEFI 1280×720, workload `bsf22`: 35/35 sprawdzeń OK**, zero błędów stron i zero timeoutów GPU. Potwierdzone:
  - katalog w procesie Ustawień: OTF CFF, 3 twarze TTC, 4 twarze CJK OTC, czcionka emoji, DejaVu, wszystkie ze statusem OK;
  - w Husarzu systemowy CFF i CJK, kolorowe emoji (zrzut ekranu);
  - web fonty WOFF2, WOFF (arabski), OTF (hebrajski), font zmienny, dwie wagi;
  - kolejność `src` z pominięciem nieobsługiwanego formatu, fallback przy złych danych i przy pliku za dużym;
  - CORS: 1 żądanie odrzucone, 1 dozwolone;
  - zwalnianie fontów przy nawigacji;
  - Notatnik, Kalkulator, Snake, Kontrolki 4 i shell działają.
- **UEFI 1920×1080: 35/35**, **BIOS: 35/35**, w obu zero błędów i zero timeoutów GPU.
- Sprzęt fizyczny, QEMU pod Windows i system zainstalowany na dysku: **nietestowane**.

## 4. Wydajność (`make bench-bsf22`, host -O2)
- Rasteryzacja przy chybieniu cache, na glif:
  - TrueType 5,1 µs (16 px) i 7,6 µs (48 px), bez zmian względem 2.1;
  - CFF 39,6 µs i 71,7 µs;
  - CFF2 46–50 µs i 83–87 µs;
  - `gvar` dokłada ok. 28%.
  CFF jest 8–10× wolniejszy, ale koszt dotyczy tylko chybienia cache glifów. Zwykły tekst w DejaVu idzie dokładnie ścieżką z 2.1.
- Dekodowanie WOFF/WOFF2: 0,2–5,4 ms na font (w workerze).
- Glif kolorowy przy chybieniu cache: COLRv1 0,19 ms (20 px) i 1,4 ms (64 px), CBDT 0,26–0,40 ms.
- Nie zmierzono A/B układu stron w Husarzu ani opóźnienia pisania w Notatniku względem bazy 2.1. Narzut ok. 10–15% opisany przy BSF 2.1 nie został zmierzony ponownie.

Szczegóły: `docs/bsf2-2-performance.md`.

## 4a. Rozmiar jądra i pamięć
- `__kernel_end`: baza **0xD39000** (zbudowana w worktree) → **0xE9F000**. To nadal poniżej limitu 15 MiB, ale zapas spadł z 1 820 KiB do **388 KiB**.
- Przyczyna: `bsf_core.o` urósł z 24,8 do 81,1 KB, a kopia trafia do ok. 20 wbudowanych aplikacji. Worker Husarza urósł o ok. 190 KB (Brotli i dekoder WOFF).
- Limit 15 MiB nie został zmieniony.
- Pamięć na proces: do 40 MiB danych czcionek w pamięci, cache kolorowych glifów 1 MiB (alokowany dopiero przy pierwszym kolorowym glifie), do 24 MiB fontów webowych w procesie przeglądarki.

## 5. Znane ograniczenia (stan rzeczywisty)
- Kolekcji **większych niż 16 MiB nie da się zainstalować natywnie**: brak zapisu porcjami w Ring 3, a import modułów bootowania ma limit 16 MiB. Loader obsługuje pliki do 64 MiB i jest to zweryfikowane na hoście.
- Fonty webowe muszą mieścić się w 240 KiB (limit zasobu Husarza i klienta HTTP).
- CORS jest sprawdzany tylko po stronie odpowiedzi, bo klient HTTP w kernelu nie wysyła nagłówka `Origin`.
- `font-display` działa jak `swap` (`optional` nie podmienia fontu po pierwszym układzie).
- Fonty zmienne: brak FeatureVariations i `avar` 2.0. Ustawienia i Husarz renderują instancję domyślną (API `set_variations` istnieje), brak `font-variation-settings`.
- Kolor: brak OpenType-SVG, brak formatów monochromatycznych CBDT, sbix bez JPG/TIFF/PDF.
- Prezentacja emoji jest przybliżeniem właściwości `Emoji_Presentation`.
- Terminal nie rysuje emoji w kolorze.
- CFF bez hintingu; FontMatrix z FDArray nie jest składany z macierzą główną.
- Brak shapingu skryptów indyjskich i z Azji Południowo-Wschodniej (bez zmian względem 2.1).

## 6. Co zostało do zrobienia
1. ISO zostało przebudowane z poprawką Ustawień (próbki zawijane po słowach). Wynik ostatniego natywnego testu smoke: `.bsf-test/native22-final.log` (był w toku w chwili zapisu).
2. Pomiar A/B wydajności Husarza i Notatnika względem bazy 2.1 (`bench-husarz-bidi`, `bench-notepad-bidi`).
3. Ręczna weryfikacja trwałości czcionki systemowej i terminala po restarcie, odświeżania katalogu natywnie oraz zachowania przy małej ilości pamięci (nie są zautomatyzowane).
4. Rekomendacje na kolejny milestone:
   - zmniejszyć duplikację BSF w każdej aplikacji (zapas jądra 388 KiB);
   - przyspieszyć CFF (cache Private DICT i subrs dla każdego FD);
   - dodać do kernela zapis plików porcjami i nagłówek `Origin` w kliencie HTTP;
   - zaimplementować FeatureVariations, `font-variation-settings` i suwaki osi;
   - dodać shaping skryptów indyjskich.

## 7. Jak uruchomić weryfikację
```
make test-bsf22-host          # wszystkie testy hostowe BSF 2.2
make fuzz-bsf22               # fuzzing (BSF22_FUZZ_ITERS=300000 dla dłuższej kampanii)
make bench-bsf22              # mikrobenchmarki
make iso && python3 narzedzia/bsf2_native_qemu.py --workload bsf22 [--video 1920x1080 | --bios]
```

Szczegóły techniczne: `docs/bsf2-2-testreport.md`, `bsf2-2-performance.md`, `bsf2-2-architecture.md`, `bsf2-2-cff.md`,
`bsf2-2-collections.md`, `bsf2-2-woff-woff2.md`, `bsf2-2-husarz-webfonts.md`,
`bsf2-2-variable-fonts.md`, `bsf2-2-color-fonts.md`,
`bsf2-2-font-management.md`, `bsf2-2-security.md`, `bsf2supportmatrix.md`.
