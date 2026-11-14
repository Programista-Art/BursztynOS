# Testy natywne: Snake, Przeglądarka Obrazów, systemowy kursor zmiany rozmiaru

Środowisko: QEMU/TCG, `narzedzia/guest_session.py` (sterowanie myszą/klawiaturą przez QMP,
zrzuty `screendump`, odczyt stanu okien z pamięci gościa). Produkcyjny obraz `BursztynOS.iso`
(oraz `BursztynOS_8k.iso` z obrazem testowym 1600×900 jako moduł Multiboot2).
Wszystkie zrzuty są w `.bsf-test/` (katalog roboczy, ignorowany przez Git).

## Macierz wyników

| Scenariusz | Wynik | Dowód |
|---|---|---|
| UEFI 1280×720, mysz USB — start Snake | OK | `snake-sess/snake-1.png` |
| UEFI 1280×720, mysz USB — Snake: pauza, koniec gry, nakładki | OK | `snake-pause.png` |
| UEFI 1280×720, mysz USB — Snake: zmiana rozmiaru (róg, lewa krawędź, minimum, ruch poza ekran) | OK (560×560 → 680×600 → 740×600 → min → x=940) | `resize-snake.png` |
| UEFI 1280×720, **VirtIO tablet** — Snake: ta sama sekwencja | OK | `snake-tab/resize-snake.png` |
| VirtIO tablet — Snake: zmiana rozmiaru w pauzie (stan gry nietknięty: ta sama komórka jabłka, wynik 0, dł. 3) | OK | `tab-A.png`, `tab-C.png` |
| VirtIO tablet — Snake: maksymalizacja (plansza 37 px/komórkę, wyśrodkowana) i przywrócenie do 560×560 | OK | `tab-max2.png` |
| Zmiana motywu (Ustawienia → Motywy → „Bursztyn Jasny”, podgląd) przy działającym Snake | OK, interfejs i plansza zmieniają kolory bez restartu gry | `theme-jasny.png` |
| BIOS (VGA std) **1920×1080**, mysz USB — Snake: zmiana rozmiaru | OK (minimum 440×380) | `bios-sess/resize-snake.png` |
| Przeglądarka Obrazów (8k ISO): menu, pasek narzędzi, status | OK | `img-sess/img-2.png` |
| Przeglądarka: natywny dialog „Otwórz obraz” (filtr BMP), wybór `test8k.bmp` (1600×900, 24 bit) | OK | `img-4.png`, `img-7.png`, `img-8.png` |
| Przeglądarka: tryb Dopasuj — kolory narożników (czerwony/zielony/niebieski/żółty) w poprawnych miejscach | OK | `img-8.png` |
| Przeglądarka: tryb 1:1 + przeciąganie myszą (pozycja (200,100) zgodna z przesunięciem) | OK | `img-9.png`, `img-10.png` |
| Przeglądarka: zmiana rozmiaru (róg, lewa krawędź, minimum 360×260, ruch poza ekran) przy zachowaniu stanu 1:1 (200,100) | OK | `resize-przegladarka_obrazow.png` |
| Kursor: strefa lewa/prawa (poziomy), górna (pionowy), róg (ukośny) — UEFI/płaszczyzna sprzętowa | OK, hotspot na krawędzi | `cur-ew-zoom.png`, `cur-ns-zoom.png`, `cur-nwse-zoom.png` |
| Kursor: ścieżka programowego overlayu (BIOS) | OK | `bios-sess/bios-cur-zoom.png` |
| Kursor: róg przy tablecie VirtIO | OK | `snake-tab/tab-C.png` (kursor ukośny w prawym dolnym rogu) |

## Nie wykonano / ograniczenia testów

* UEFI 1920×1080 nie był uruchamiany osobno (rozdzielczość 1920×1080 pokrywa przebieg BIOS).
  Inne rozdzielczości i skalowanie DPI nie były testowane natywnie.
* Zmiana motywu przetestowana natywnie tylko na Snake (jasny); Przeglądarka Obrazów — wyłącznie test
  hosta (dwa motywy + czarny). Pozostałe motywy systemowe: tylko testy hosta (kolory z `motyw_kolor`).
* Nakładanie okien i wiele aplikacji: obserwowane na zrzutach (Diagnostyka + Snake + Ustawienia),
  bez osobnego scenariusza automatycznego.
* Kursor przy utracie fokusu / zamknięciu okna / minimalizacji: opiera się na istniejącym
  mechanizmie jądra (stan per proces); osobnego testu natywnego nie wykonano.
* **Biały kursor bez obrysu jest prawie niewidoczny na jasnym tle** (potwierdzone zrzutem
  `snake-tab/cur-light-zoom.png`). Nie zmieniano koloru — wymagałoby to innego PNG lub zgody na obrys.
* Pomiar responsywności pulpitu przy działających obu aplikacjach nie był wykonany osobno
  (Snake ma timer tylko w stanie „Gra”; Przeglądarka tylko w trakcie dekodowania 1:1).

## Testy hosta (wszystkie ASan/UBSan tam, gdzie wspierane)

`make test-resize-cursor-host`, `test-snake-host` (298 420 sprawdzeń), `test-przegladarka-obrazow-host`
(112), oraz regresja: window-geometry, display-geometry, explorer, cursor, pointer-graphics,
mouse-pipeline, pointing, desktop-files, file-popup, notepad-unicode, notepad-bidi, bursztynui-bidi,
gui-text-input, settings-themes-power, system-binary-protection, launch-policy, bsf-ui, kernel-blobz,
package-signature, shell, stack-protector, bur-v2-aslr, input-coexistence, desktop-settings,
desktop-capacity, filesystem-move, desktop-ttf, slider-pixels, power-png — **32/33 PASS**.
`test-gpu-quiesce-host` nie kompiluje się (brak pola `komunikat` w `Powitanie::Sekwencja`) — kod
powitania nie był zmieniany w tej pracy.
