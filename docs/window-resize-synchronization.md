# Synchronizacja zmiany rozmiaru okna (belka vs powierzchnia klienta)

## Defekt
Podczas przeciągania prawej krawędzi okna Ustawień belka tytułu i ramka rosły do nowej szerokości, a treść
klienta zostawała w starej szerokości; w poszerzonym pasie było widać tapetę lub okna pod spodem. Po puszczeniu
przycisku treść „doganiała”. Zrzut: `docs/zrzuty-resize-2026-11-15/przed_naprawa_srodek_przeciagania.png`
(Diagnostyka widoczna przez okno Ustawień po prawej stronie starego klienta).

## Przyczyna (z dowodu, nie z założenia)
Nie leży w aplikacji Ustawień. `BursztynUI::Okno::zastosuj_oczekujaca_geometrie()` (`bursztyn_ui/okno.h`) w jednej
klatce: wywołuje BWS 61, przelicza układ (`przelicz_uklad`) i rysuje całe okno — układ i kontrolki dostają
poprawne wymiary. Winne jest **jądro**: `zmien_geometrie_warstwy()` → `utworz_warstwe()`
(`jadro/gui/skladacz_obrazu.cpp`) natychmiast publikuje nowy bufor w nowym rozmiarze, wyzerowany
(alfa 0) z ostatnią treścią skopiowaną w lewy górny róg, a kompozytor od razu rysuje belkę/ramkę wg nowej
geometrii warstwy. Aplikacja Ring 3 publikuje pełną klatkę dopiero później (w QEMU/TCG to setki ms – sekundy),
więc w międzyczasie powiększony pas jest **przezroczysty**. Dotyczy każdego okna, które zmienia rozmiar
(wszystkie aplikacje korzystają z tej samej ścieżki jądra).
Dowód natywny: zrzuty w trakcie przeciągania przed/po poprawce (UEFI 1280×720, tablet VirtIO).

## Poprawka (centralna, jądro) — stan po weryfikacji końcowej
1. `jadro/gui/rozszerzenie_powierzchni.h` (`kopiuj_i_rozszerz`, użyte w `utworz_warstwe`): nowy bufor = stara treść +
   **kryjące** rozszerzenie w kolorze tła okna z aktywnego motywu (`motyw->oknotlo`). Wersja z replikacją krawędzi
   została **odrzucona**: (a) błędnie zakładała alfę 0xFF — piksele aplikacji mają górny bajt 0 (`0` = przezroczysty,
   każda inna wartość = kryjąca; `0x01000000` = czerń), więc w praktyce zawsze wybierała ciemny kolor zapasowy i dawała
   ciemny pas w jasnym motywie (test hosta z alfą 0xFF tego nie wykrył — poprawiony na semantykę aplikacji),
   (b) replikacja krawędzi rozciągała suwaki i paski stanu w poziome pasy.
2. `bws_gui_odswiez_pulpit` (BWS 19, `gui_odswiez_pulpit()` w Eksploratorze `draw_full(true)`) zerował powierzchnię
   aplikacji przed pełnym odrysowaniem; przez czas rysowania (230–400 ms w TCG) kompozytor pokazywał przezroczyste,
   w połowie namalowane okno (cała treść Eksploratora „znikała” podczas przeciągania — zrzut przed poprawką w
   historii: `docs/zrzuty-resize-2026-11-15/`). Teraz po zmianie rozmiaru (`skladacz_obrazu_tresc_zachowana_po_zmianie_rozmiaru`:
   treść zachowana i nowa klatka jeszcze nieopublikowana) powierzchnia nie jest zerowana; zwykłe pełne odrysowania
   poza zmianą rozmiaru zachowują się jak dawniej.
Brak czekania na aplikację i blokad kompozytora; indeksy `size_t`; własność pamięci bez zmian.

## Weryfikacja natywna (końcowa)
QEMU/TCG UEFI 1280×720, produkcyjne ISO + build diagnostyczny `BURSZTYN_DIAG_RESIZE_LATENCY=1`; każda aplikacja w
osobnym, świeżym gościu: poszerzanie prawej krawędzią (zrzuty w trakcie), narożnik SE (powiększanie i odwrócenie bez
puszczenia), dolna krawędź; wszystkie 9 aplikacji (Notatnik, Eksplorator, Husarz, Powłoka, Snake, Przeglądarka Obrazów,
Diagnostyka, Kalkulator, Ustawienia): brak tapety/okien pod spodem w oknie, belka i klient spójne, brak pasów
z replikowanych kontrolek, zaokrąglone rogi bez zmian, strzałka wraca po puszczeniu przycisku. Jasny motyw
(Ustawienia): jasny, jednolity pas, bez ciemnych pasów. Mysz USB (Notatnik): to samo; tablet VirtIO: pozostałe.
Zrzuty: `docs/zrzuty-resize-2026-11-15/`.
Artefakt pozostały: w chwilowo rozszerzonym obszarze widać płaskie tło motywu (nie kolor tła aplikacji, np.
czarny Terminal/biała strona Husarza) do czasu nowej klatki; stary układ (suwaki itp.) pozostaje w starych miejscach.

## Opóźnienie (build `BURSZTYN_DIAG_RESIZE_LATENCY=1`, TCG, 6 zmian rozmiaru na aplikację)
`app_redraw_us` = od zastosowania nowej geometrii w jądrze (BWS 61) do publikacji klatki przez aplikację;
`publish→presented` = kompozytor (zebranie + prezentacja). Mediana / maks:
| Aplikacja | redraw aplikacji | kompozytor |
|---|---|---|
| Kalkulator | 56 / 80 ms | 14 / 26 ms |
| Powłoka | 94 / 151 ms | 16 / 23 ms |
| Snake | 105 / 193 ms | 18 / 37 ms |
| Notatnik | 119 / 151 ms | 25 / 49 ms |
| Husarz | 181 / 201 ms | 29 / 53 ms |
| Diagnostyka | 197 / 308 ms | 29 / 39 ms |
| Eksplorator | 261 / 404 ms | 42 / 184 ms |
| Ustawienia | 356 / 675 ms | 41 / 77 ms |
| Przeglądarka Obrazów (obraz 3508×2480) | 806 / 1187 ms | 32 / 63 ms |
Kompozytor: zebranie ~25–35 ms, prezentacja ~1–2 ms; **opóźnienie pochodzi z odrysowania w Ring 3** (programowe
renderowanie całego klienta, tekst BSF, skalowanie obrazu, w TCG ~25× wolniej niż na sprzęcie), nie z kompozytora.
Nie zmieniano kompozytora ani BursztynUI. Możliwa dalsza optymalizacja (nie zrobiona, wymaga osobnej decyzji):
odrysowanie tylko odsłoniętej części zamiast całego klienta przy zmianie rozmiaru. Pamięć tymczasowa: jeden dodatkowy
bufor warstwy na czas wymiany (stary zwalniany po odpublikowaniu), bez zmian.

## Znane problemy
* Jednorazowo (1 z ~15 uruchomień) w jednym z przebiegów test-owych jądro zatrzymało się przy starcie w
  `tapety_obsluz_obraz` (#PF Ring 0, CR2 0x120236000, BWS 69, proces Tapety) — przed jakąkolwiek zmianą rozmiaru; log:
  `docs/zrzuty-resize-2026-11-15/crash-tapety-boot-serial.txt`. Niepowtarzalne w kolejnych uruchomieniach; nie
  zbadane (ścieżka obrazów Tapet, nie zmiana rozmiaru okien). Wymaga osobnego śledztwa.
* `make test-startup-visibility` kończy się FAIL 58 (`utworz_warstwe(... sw+1 ...) < 0`): test z przeniesienia repo
  zakłada odrzucanie warstwy szerszej od ekranu, a zatwierdzony kod pozwala na 2× ekranu (zmiana z etapu zmiany
  rozmiaru). Przestarzała asercja, niezwiązana z tą poprawką; nie zmieniana.
* Nie przetestowano: UEFI 1920×1080, BIOS, jasnego motywu dla wszystkich aplikacji, ruchu okna częściowo poza
  ekranem po poprawce, minimalizacji/maksymalizacji po poprawce (nie zmieniano tych ścieżek).
* Dolne strefy okien sięgających poza y≈676 są niedostępne (pasek zadań nad oknami; zamierzone).

## Kursory zmiany rozmiaru
`DOCELOWA_SZEROKOSC` 19→24: ↔ **24×11**, ↕ **11×24**, ukośne **25×25** (cel ≈ 26; proporcje źródła 128×57
zachowane). Strzałka zwykła 16×18 i `ikonki/kursor.png` bez zmian; hitboxy bez zmian (5 px).

## Testy
`make test-rozszerzenie-powierzchni-host` (ASan/UBSan, 1006 sprawdzeń: powiększenie/zmniejszenie, jasny/ciemny motyw, rogi, czerń 0x01000000, przezroczysty
szybka seria zmian, niepoprawne argumenty), `make test-resize-cursor-host` (rozmiary, hotspot),
`test-window-geometry-host`, `test-taskbar-stacking-host`, `test-wm-click-routing`, `test-cursor-host`,
`test-pointer-graphics-host`, `test-mouse-pipeline-host`, `test-virtio-input-events`, `test-launch-policy-host` — rc=0.
Jądro: ostatni segment kończy się na 0xB16370 (~11,1 MiB; limit 15 MiB).

## Przegląd bezpieczeństwa powierzchni po zmianie rozmiaru (2026-11-15, etap stabilizacji)
* Własność i czas życia: nowy bufor warstwy jest w pełni zapisany (`kopiuj_i_rozszerz`: treść + kryjące rozszerzenie) przed
  `aktywna=true`; stary bufor zwalniany po odpublikowaniu (`zwolnij_lub_odrocz`); błąd alokacji zostawia starą warstwę.
  Rozmiary sprawdzane przez `wymiary_mieszcza_sie_na_ekranie` (do 2× ekranu na osi, bez przepełnień).
* Częściowo namalowana treść: kompozytor czyta **cień** ostatnio opublikowanej klatki (`cien_warstwy_*`, grafika.cpp)
  do `gui_odswiez()` właściciela. **Znaleziona luka:** BWS 19 (`gui_odswiez_pulpit`) zerował żywy bufor bez cienia i bez
  oznaczenia transakcji, więc każde pełne odrysowanie przez tę ścieżkę (Eksplorator `draw_full(true)`: zmiana rozmiaru,
  motyw, DPI) pokazywało przezroczysty, w połowie namalowany bufor. Poprawka: dla warstw z_order≠0 BWS 19 tworzy cień i
  oznacza dirty całej warstwy (transakcja kończona `gui_odswiez`), a po zmianie rozmiaru nie zeruje bufora.
  Test hosta (`test_boot_graphics.cpp`, 94 sprawdzenia): bufor zerowany, ale `bws_gui_cien_warstwy` zwraca ostatnią klatkę;
  zły rozmiar ⇒ brak cienia; po `gui_odswiez` koniec transakcji; brak wycieków (ASan).
* Cień ma zawór bezpieczeństwa 1 s i jest zwalniany przy usunięciu warstwy; jeśli rozmiar cienia ≠ rozmiar warstwy
  (zmiana rozmiaru między klatkami), kompozytor czyta na żywo bufor z kompletną treścią (stara + rozszerzenie), nigdy
  niezgodny bufor. Ring 3 nie mapuje cudzych powierzchni — bez zmian.
* Nie znaleziono wyścigu ani nieprawidłowego czasu życia wymagającego zmian w kompozytorze; nie przeprojektowywano go.

## `test-startup-visibility` (naprawiony)
Test zakładał dwie nieaktualne rzeczy: (1) odrzucenie warstwy szerszej od ekranu (`sw+1`) oraz (2) ukrywanie okna po
zmianie rozmiaru do nowej klatki. Zatwierdzona polityka (`wymiary_mieszcza_sie_na_ekranie`, doc `window-manager-resizing.md`):
do 2× ekranu na osi, współrzędne ujemne i poza ekranem dozwolone, zmiana rozmiaru zachowuje ostatnią klatkę. Test sprawdza
teraz: zero/ujemne/`2·sw+1`/`2·sh+1`/`INT_MAX`/nadmierną alokację ⇒ odrzucenie bez naruszenia zatwierdzonej powierzchni;
`sw+1`, `2·sw×2·sh`, ujemne i bardzo ujemne współrzędne, okno częściowo poza ekranem ⇒ dozwolone (kompozytor bez zapisu
poza scenę, ASan); po zmianie rozmiaru okno widoczne, stara treść zachowana, powiększony pas = kolor tła motywu (nie tapeta).
Osobno: `test_boot_graphics` nie linkował się (brak stubu `virtio_gpu_get_driver`, stary błąd) — dodano stub zwracający
`nullptr` (test nie ma sterownika VirtIO). Wynik: 11 610 + 94 + 20 sprawdzeń PASS.
