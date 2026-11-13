# Zmiana rozmiaru okien i ruch poza ekran

Gałąź `feature/bsf-2-2-font-formats`. Zmiany niezatwierdzone.

## Stan przed zmianą (audyt)

* Chrome okna (belka, ramka 1 px) rysuje kompozytor (`wm_rysuj_dekoracje`), ale
  **logikę ruchu okna wykonuje aplikacja**: `BursztynUI::Okno` (`bursztyn_ui/okno.h`) oraz
  własne pętle Eksploratora, Terminala (`programy/shell`), Przeglądarki Obrazów i Snake’a.
* Ruch: pozycja okna była przycinana do obszaru roboczego (`dopasuj_do_obszaru_roboczego(false)`,
  `AktowkaWindow::clamp()`), a rozmiar — do rozmiaru obszaru roboczego. Zmiany rozmiaru
  myszą nie było (tylko maksymalizacja).
* Jądro: pozycja warstwy może być ujemna i wykraczać poza ekran (kompozytor liczy w
  `int64_t` i przycina do framebuffera), natomiast `wymiary_mieszcza_sie_na_ekranie()` zabraniało
  warstwy większej niż ekran, a zmiana rozmiaru tworzyła warstwę od nowa i **chowała
  okno** do pierwszej opublikowanej klatki.

## Zmiany

### Logika współdzielona: `jadro/gui/okno_geometria.h`
Czysta, testowalna na hoście (bez BWS), liczy w `int64_t` z jawnym przycinaniem do `int32`:

* `krawedz_w_punkcie()` — strefa chwytania: pas 5 px (skalowany) wzdłuż krawędzi wewnątrz okna,
  rogi: ramiona 14 px wzdłuż pasów. Wnętrze klienta nigdy nie jest strefą chwytania.
  Dla bardzo małych okien rogi nie zajmują całej powierzchni.
* `zmien_rozmiar()` — przeciwległe krawędzie zakotwiczone (przy osiągnięciu minimum okno nie
  „ucieka” za kursorem), rozmiar w `[min,max]`, odporność na skrajne `dx/dy`.
* `limity_ruchu()/przytnij_ruch()` — swobodny ruch poza ekran z zachowaniem
  odzyskiwalności: ≥ 96 px (skalowanych) paska tytułu widoczne w poziomie, górna krawędź
  co najwyżej pół paska nad obszarem roboczym, pasek nie niżej niż dolna krawędź obszaru.
* `kursor_dla_krawedzi()` — kursory zmiany rozmiaru (`GUI_CURSOR_RESIZE_*`).

### BursztynUI (`bursztyn_ui/okno.h`) — wszystkie aplikacje na `Okno`
Notatnik, Husarz, Ustawienia, Diagnostyka, Kalkulator, Aktualizacje, Test dźwięku,
Kontrolki 2/3/4, Demo UI, Tapety, Ekran. Instalator ma zmianę rozmiaru **wyłączoną**
(kreator o stałym układzie, zachowanie modalne).

* Mysz-w-dół w strefie krawędzi/rogu → przechwycenie wskaźnika, zapamiętanie geometrii
  startowej; przyciski belki nie są uruchamiane (zmiana rozmiaru jest sprawdzana pierwsza,
  strefa to tylko pas wewnętrzny, więc przyciski w 28-pikselowej belce poniżej 5 px działają).
* Ruch: geometria jest tylko zapamiętywana, a **stosowana raz na klatkę** (na początku
  `rysuj()`), więc wiele zdarzeń ruchu daje jedną realokację warstwy.
* Mysz-w-górę / zminimalizowanie / maksymalizacja / zmiana ekranu anulują zmianę rozmiaru.
* Minimalny rozmiar: 320×200 (nie więcej niż rozmiar początkowy). Kalkulator: nie można go
  zmniejszyć poniżej rozmiaru początkowego (stałe proporcje przycisków), można powiększać.
* Maksymalny rozmiar: 2× obszar roboczy na osi (limit kompozytora).
* Hover: kursor zmiany rozmiaru w strefie chwytania (poza popupami i przechwyceniem).

### Eksplorator (własna pętla)
`AktowkaWindow::clamp()` dopuszcza okno do 2× ekranu i ruch poza ekran (ta sama reguła
odzyskiwalności), a pętla ma zmianę rozmiaru krawędziami/rogami z ograniczeniem do jednej
realokacji na ~33 ms i zastosowaniem oczekującej geometrii także po upływie czasu.
Zmaksymalizowane okno pozostaje w obszarze roboczym.

### Terminal (`programy/shell`) i Przeglądarka Obrazów
Zmiana rozmiaru krawędziami/rogami w obu aplikacjach (własne pętle): Terminal min 320×200,
max 2× obszar roboczy; Przeglądarka Obrazów min 360×260 (stałe `WIN_MIN_*`), po zmianie
unieważnia cache rozmiaru i przerysowuje. Przeglądarka Obrazów dostała też swobodny ruch poza
ekran z regułą odzyskiwalności (wcześniej przycinanie do ekranu); Terminal nie przycinał ruchu.
**Obie zmiany przeszły tylko kompilację i testy hosta (`test-shell-host`, `test-shell-ttf-host`);
natywnie nie były uruchamiane w tej sesji.**

### Jądro (`jadro/gui/skladacz_obrazu.cpp`)
* `wymiary_mieszcza_sie_na_ekranie()` — do **2×** ekranu na osi (59 MiB dla 2560×1440 mieści się
  w 64-MiB slocie `bufory_grafiki`; łączną pamięć warstw nadal ogranicza `MAKS_PAMIEC_WARSTW`).
* `zmien_geometrie_warstwy()` — zachowuje ostatnio opublikowaną treść (lewy górny róg) i
  widoczność przy zmianie rozmiaru, więc okno nie znika na czas przerysowania.

## Odzyskiwanie okna

* ≥ 96 px paska tytułu zawsze widoczne w poziomie, górna krawędź ≥ −pół paska.
* Zmiana rozdzielczości / skali → `dopasuj_do_obszaru_roboczego(false)` ponownie stosuje regułę
  odzyskiwalności (automatyczne przywrócenie uchwytu).
* Maksymalizacja/Przywróć, Alt+Tab, taskbar — bez zmian.
* **Nie zaimplementowano** przesuwania okna klawiaturą ani Alt+przeciągnięcie.

## Testy

* `make test-window-geometry-host` (ASan/UBSan): 771 385 sprawdzeń — strefy chwytania dla
  wszystkich 8 stref i dla okien 1…24 px, zakotwiczenie krawędzi, minimum/maksimum, skrajne
  `INT32_MIN/MAX`, granice ruchu dla 800×600, 1024×768, 1280×720, 1920×1080, 2560×1440,
  okno większe niż pulpit, idempotencja, odzyskanie po spadku rozdzielczości, 200 000
  losowych przypadków (niezmienniki).
* Natywnie (QEMU/TCG, UEFI 1280x720): Notatnik — rozciąganie prawego dolnego rogu i lewej
  krawędzi, przesunięcie okna poza prawą krawędź ekranu; Eksplorator — rozciąganie
  dolnego prawego rogu i lewej krawędzi (okno wychodzi poza ekran, układ kolumn dostosowuje
  się). Brak błędów jądra/procesu w logu szeregowym.
  Zrzuty: `.bsf-test/sess-final/notepad-resized.png`, `notepad-offright.png`, `expl-resized.png`.

## Ograniczenia

* Snake nie ma zmiany rozmiaru krawędziami (stały układ planszy).
* Terminal i Przeglądarka Obrazów: zmiana rozmiaru niezweryfikowana natywnie (patrz wyżej).
* Zmiana rozmiaru okien przy rozdzielczościach 800×600, 1024×768, 1920×1080 i 2560×1440
  zweryfikowana tylko logiką hosta; natywnie sprawdzono 1280×720.
* Test z tabletem VirtIO (pointer absolutny) — niewykonany w tej sesji (harness używa myszy USB).
* Okno wychodzące poza dolną krawędź obszaru roboczego może przykrywać pasek zadań (pasek
  jest częścią warstwy pulpitu); belka tytułu nigdy nie schodzi poniżej dolnej krawędzi
  obszaru roboczego.
