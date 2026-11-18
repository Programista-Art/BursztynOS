# Notatnik: polskie znaki w natywnym oknie QEMU, fokus po menu, okna pomocy (2026-11-17)

## Przyczyny braku polskich znaków

Problem miał dwie niezależne przyczyny. Obie potwierdzono dowodami.

### 1. Host: SDL2 + IBus gubił klawisz litery przy AltGr (główna przyczyna)

`make runuefi` uruchamia QEMU z `-display sdl`. Na hoście (Linux w VirtualBox, Cinnamon) działa IBus (`XMODIFIERS=@im=ibus`). SDL 2.30 przekazuje klawisze tekstowe do IBus. Dla AltGr+litera IBus zatwierdza gotowy znak („ą”) jako `SDL_TEXTINPUT`, a SDL **nie wysyła** `SDL_KEYDOWN` litery. QEMU przekazuje gościowi wyłącznie `KEYDOWN`/`KEYUP`, więc Bursztyn OS dostawał samo naciśnięcie i zwolnienie AltGr, bez litery. Zwykłe litery przechodziły.

Dowód: `sdl-ibus-probe.txt` (`narzedzia/sdl_ime_probe.py`, okno SDL2 bez QEMU, klawisze XTest):

| Klawisze | IBus aktywny | IBus odłączony |
|---|---|---|
| `n`, `o`, `t`, `x`, Enter | KEYDOWN litery | KEYDOWN litery |
| AltGr+a / s / z | **tylko Right Alt** (TEXT=ą/ś/ż) | Right Alt + A/S/Z |
| Shift+AltGr+l | **Left Shift, Right Alt** (TEXT=Ł) | Left Shift, Right Alt, L |
| LCtrl+AltGr+e | Left Ctrl, Right Alt, E | Left Ctrl, Right Alt, E |

To problem warstwy SDL/IBus hosta. Nie dotyczy X11/XTest ani sterownika Bursztyn OS, więc sterownika nie zmieniano.

Poprawka w `Makefile`: `export IBUS_ADDRESS := unix:path=/nonexistent/bursztyn-qemu-bez-ibus`, kiedy `QEMU_SDL_BEZ_IME=1` (domyślnie). Działa tylko w procesach uruchamianych przez `make` i odcina QEMU (SDL) od IBus. Pulpit hosta nadal używa IBus. Wyłączenie: `make QEMU_SDL_BEZ_IME=0 runuefi`.

QEMU uruchomione ręcznie poza `make` z `-display sdl` przy aktywnym IBus nadal będzie gubić AltGr+litera. Rozwiązania w takim przypadku:
* `IBUS_ADDRESS=unix:path=/nonexistent qemu-system-x86_64 ...`;
* `-display gtk`.

### 2. BursztynUI: fokus klawiatury zostawał w pasku menu albo ginął

Ustalono w natywnej sesji 1 (`natywne-1-przed-poprawka/`, prawdziwe klawisze X11 → SDL → QEMU) i odtworzono w teście hosta:

* **Fokus w pasku menu.** Po kliknięciu menu (np. Pomoc) i wybraniu pozycji fokus zostawał w `MenuGlowne`. Wpisywane litery sterowały menu, a edytor pozostawał pusty (zrzut `06-polskie-znaki.png`).
* **Fokus ginął po przełączeniu okna.** `BWS_ZDARZENIE_BLUR` zerował fokus kontrolki, a `FOCUS` go nie przywracał. Po zamknięciu okna pomocy albo powrocie z innego okna klawiatura nie trafiała do edytora aż do kliknięcia.

Poprawka centralna w `bursztyn_ui/okno.h`, `bursztyn_ui/kontrolka.h` i `bursztyn_ui/menu_glowne.h`:
* wejście na pasek menu zapamiętuje poprzednią kontrolkę;
* po zamknięciu menu (wykonane polecenie, Esc, klik obok, zamknięty dialog otwarty z menu) fokus wraca do tej kontrolki;
* Esc albo ponowny lewy Alt kończy tryb menu;
* tekst i klawisze, które nie sterują menu, wpisane po lewym Alcie, trafiają do edytora;
* `FOCUS` przywraca kontrolkę sprzed `BLUR`, wyłącznie gdy nadal jest w drzewie okna (porównanie adresów bez dereferencji).

## Pozostałe zmiany

* **Pasek narzędzi usunięty.** Edytor zaczyna się bezpośrednio pod paskiem menu (odstęp panelu 2 px). Pasek stanu został.
* **Plik:** Nowy, Otwórz…, Zapisz, Zapisz jako…, Zakończ. Usunięto etykietę „Alt+F4”, bo ten skrót nie był zaimplementowany.
* **Pomoc:** Sterowanie i O Notatniku jako natywne okna podrzędne (BWS 101/102).
  * Nowe rodzaje `RODZAJ_O_NOTATNIKU = 7` i `RODZAJ_STEROWANIE_NOTATNIK = 8` (`jadro/bws/okna_podrzedne.h`, `bursztyn_ui/okno_podrzedne.h`). Numery 1–6 Snake'a i Przeglądarki obrazów są bez zmian.
  * Notatnik dostał prawo `okno_podrzedne` w manifeście (`jadro/rdzen/kernel.cpp`). Proces okna informacyjnego nadal dostaje tylko `PRAWO_GUI` (bez plików, sieci i uruchamiania programów).
  * Teksty pomocy opisują wyłącznie zaimplementowane funkcje.

## Wyniki natywne

Konfiguracja: `natywne-4/`, jedna instancja QEMU. Argumenty jak w `make runuefi` (UEFI, `pc`, virtio-keyboard i PS/2, virtio-tablet, SDL, 1280×720). Dysk `wirtualny_dysk.img` był podłączony z `snapshot=on`, więc nie został zmieniony. QEMU działało z `IBUS_ADDRESS` jak w Makefile. Klawiatura szła przez XTest → serwer X → okno SDL. Przed każdym klawiszem sprawdzano blokadę ekranu i faktyczny fokus wejścia X11. Przestoje hosta: 0. Błędy jądra i aplikacji w `serial.log`: 0.

| Sprawdzenie | Wynik | Dowód |
|---|---|---|
| Brak paska Nowy/Otwórz/Zapisz, edytor tuż pod menu | **PASS** | `01`, `02` |
| Zwykłe pisanie, 9 małych i 9 wielkich liter przez AltGr i Shift+AltGr | **PASS** | `02` |
| Wariant LCtrl+AltGr (9 liter) | **PASS** | `02` |
| Fokus po otwarciu i zamknięciu (Esc) menu Plik, Edycja, Pomoc | **PASS** | `03-*`, `04` („123ż”) |
| Sterowanie i O Notatniku jako okna BWS 101 (rodzaje 8 i 7), zamknięcie Esc | **PASS** | `05`, `06`, log `[OKNA-PODRZEDNE]` |
| Fokus wraca do edytora po zamknięciu okna pomocy, bez klikania | **PASS** | `07` |
| Menu kontekstowe (prawy przycisk): dostępność pozycji, Esc | **PASS** | `08` |
| Ctrl+S → Zapisz jako z nazwą UTF-8 „xtest-żołw.txt”, tytuł bez `*` | **PASS** | `11`, `12` |
| Ctrl+N, potem Ctrl+O tego pliku | **PASS** | `13`–`15` |
| Ctrl+Shift+S → „kopia-ściezki.txt” | **PASS** | `16` |
| Treść po ponownym otwarciu | **PASS (pośrednio)** | patrz niżej |

Treść po ponownym otwarciu:
* Obszar tekstu po ponownym otwarciu jest identyczny ze stanem przed zapisem. Średnia różnica pikseli wynosi 0,06, czyli tyle co kursor. Liczba linii też się zgadza (6).
* Notatnik zgłasza „Zapisano” wyłącznie po odczycie kontrolnym bajt w bajt.
* Bajtów pliku nie odczytano niezależnie po stronie hosta, bo zmiany na dysku `snapshot=on` są odrzucane po zamknięciu QEMU.

### Niezweryfikowane natywnie

* USB-klawiatura w tej konfiguracji: `make runuefi` jej nie ma. Kodu USB nie zmieniano, `test-usb-keyboard-repeat-host` przechodzi.
* Ręczne pisanie fizyczną klawiaturą użytkownika: zamiast niego XTest, który idzie tą samą drogą X11 → SDL → QEMU.
* Zamknięcie Notatnika z otwartym oknem pomocy. Logikę rejestru pokrywa `test-okna-podrzedne-host`, a jądra nie zmieniano.
* BIOS, inne rozdzielczości, sprzęt fizyczny.

### Sesje nieważne

* `natywne-2-ekran-zablokowany/`, `natywne-3-ekran-zablokowany/`: w trakcie tych sesji ekran hosta był zablokowany (cinnamon-screensaver). Klawisze XTest trafiły do ekranu blokady, a nie do QEMU, i spowodowały kilka nieudanych prób odblokowania. Wyników nie wolno uznawać.
* Skrypty (`narzedzia/notatnik_xtest_qemu.py`, `narzedzia/sdl_ime_probe.py`) od tej pory odmawiają pracy przy zablokowanym ekranie lub fokusie X11 poza oknem docelowym.

## Testy hosta (make -j1, po kolei, ASan/UBSan)

| Test | Wynik |
|---|---|
| `test-notatnik-polski-host` | 2691 PASS |
| `test-okna-podrzedne-host` | 209 270 PASS |
| `test-notepad-unicode-host` | 60 662 PASS |
| `test-notepad-bidi-host` | 6100 PASS |
| `test-bursztynui-bidi-host` | 803 PASS |
| `test-gui-text-input-host` | 7293 PASS |
| `test-kontrolki4` | 1379 PASS, 0 FAIL |
| `test-bursztyn-ui-polecenia` | 23/23 |
| `test-snake-host` | 298 672 PASS |
| `test-przegladarka-obrazow-host` | 402 PASS |
| `test-explorer-host` | 15 706 PASS |
| `test-wm-click-routing` | 258 PASS |
| `test-husarz-runtime-host` | 2390 PASS |
| `test-file-popup-host` | 308 494 PASS |
| `test-desktop-files-host` | 86 403 PASS |
| `test-input-coexistence-host` | PASS |
| `test-usb-keyboard-repeat-host` | PASS |

Test `test-notatnik-polski-host` odtwarza dokładny scenariusz natywny: klik w Pomoc, Strzałka w dół + Enter, BLUR/FOCUS okna pomocy, pisanie z AltGr. Bez poprawki `okno.h` test pada (sprawdzone mutacją).

Jądro kończy się na `0xB56000` (ok. 11,3 MiB, limit 15 MiB).
