# Testy regresji: kursory, Snake 2.0, Przeglądarka Obrazów 2.0, pasek zadań

Środowisko natywne: QEMU/TCG, `narzedzia/guest_session.py` (+ `guest_ctl.py`), produkcyjny `BursztynOS.iso`
(UEFI 1280×720; mysz USB i tablet VirtIO), zrzuty w `.bsf-test/v3-sess/` (poza Git). Rozszerzenia
narzędzia w tej pracy: `peek` (odczyt pamięci gościa – tylko diagnostyka), `ctrlwheel`, `rclick` dla tabletu.

## Wyniki natywne

| Scenariusz | Wynik |
|---|---|
| Nowa strzałka (`ikonki/kursor.png`) na pulpicie, w oknach, w pasku; czubek = hotspot | OK (zrzuty `v3-cur-zoom.png`, kursor w każdym zrzucie) |
| Kursor rozciągania (zaktualizowany PNG) – wcześniejsza wersja zasobu | OK; nowa wersja: brak osobnego zrzutu krawędzi (nieweryfikowane natywnie) |
| Snake: brak grubej ramki, cienka obwódka, motyw, menu Gra/Ustawienia/Pomoc, Wynik/Rekord | OK (`v3-snake.png`) |
| Snake: ustawienie „Szybko”, zamknięcie, ponowne uruchomienie → „Szybko 150 ms” (trwałość) | OK po poprawkach (manifest `pliki_*`, katalog ustawień); przed poprawką status „Nie zapisano rekordu” (PZB DENY) |
| Snake: domyślna prędkość 220 ms | status „Normalnie 220 ms”; **pomiar czasu kroku w QEMU nie wykonany** (brak licznika w gościu); dowód: test hosta zegarem makiety |
| Snake: rekord > 0 po restarcie | **nieweryfikowane natywnie** (logika i zapis: host; zapis pliku: natywnie sprawdzony na ustawieniach) |
| Przeglądarka: galeria `/dane/zdjecia` (6 plików z modułu ISO), „Zdjęcie N z 6”, kolejność naturalna | OK (log `[GALERIA] zdjecia zaimportowane: 6`) |
| JPEG (szary) 1920×1410, BTP1 1280×720: otwarcie i nawigacja, zawijanie 6→1 | OK |
| PNG 3508×2480, PNG 3659×3000, JPEG 4724×3543, JPEG 8000×8000 | odrzucone komunikatem „Obraz zbyt duży” (limit jądra ≈4 Mpx), nawigacja dalej działa |
| BMP i BTP2 natywnie | **nieweryfikowane natywnie** (test hosta: BMP, BTP1 RAW/RLE, BTP2) |
| Zoom: +/-, 100%, slider, etykieta, Dopasuj; przesuwanie myszą; Ctrl + kółko (mysz USB) | OK (150%, 50% itd.); Ctrl+kółko na tablecie VirtIO: brak zdarzeń kółka (nieweryfikowane) |
| Pasek zadań nad oknem Snake wsuniętym pod dolną krawędź | OK (`v4-tb2.png`) |
| Kliknięcie w pasek pod oknem Snake aktywuje przycisk paska (nie Snake) | OK |
| „Nad paskiem” (prawy przycisk na przycisku okna): okno nad paskiem; cofnięcie – pasek znów nad oknem | OK (`v5-above.png`, `v5-revoked2.png`, flaga w pamięci jądra) |
| UEFI 1920×1080, BIOS dla tej wersji; resize/maksymalizacja nowych okien | **nieweryfikowane natywnie w tej pracy** (poprzednia praca: BIOS 1920×1080, UEFI 1280×720) |
| Tablet VirtIO + mysz USB | tablet: wszystkie kroki powyżej oprócz kółka; mysz USB: Ctrl+kółko |
| Przywrócenie strzałki po resize/zamknięciu/minimalizacji | nie testowane osobno (mechanizm stanu per proces bez zmian) |

## Testy hosta (ASan/UBSan tam, gdzie wspierane)

Nowe/zmienione: `test-resize-cursor-host` (1523 sprawdzeń), `test-snake-host` (~298 tys.),
`test-przegladarka-obrazow-host` (300), `test-taskbar-stacking-host` (20), `test-wm-click-routing`
(rozszerzony o pasek/„nad paskiem”), `test-gpu-quiesce-host` (naprawiony przestarzały asercja).
Pełna lista uruchomionej regresji i wynik: patrz raport końcowy.

`test-gpu-quiesce-host`: awaria kompilacji istniała (test odwoływał się do pola `Sekwencja::komunikat`
usuniętego przy przeprojektowaniu animacji powitalnej). To nieaktualny test, nie regresja produktu;
asercję zastąpiono równoważną: po 3,2 s splash jest aktywny w fazie `Pasek` (`Stan::Widoczny`).

## Pomiary wydajności

* Rozmiar jądra: `__kernel_end` = `0xAF8000` (10,97 MiB); limit 15 MiB (`0xF00000`) – zapas ≈4,03 MiB.
  (poprzednio `0xAEA000`; wzrost o 56 KiB m.in. dzięki nowej strzałce, importowi galerii, rozszerzeniu kompozytora i bloby aplikacji).
* Dekodowanie obrazów (host, `-O1`): PNG 3508×2480 0,15 s; JPEG szary 1920×1410 0,05 s; BTP 1280×720 <0,01 s.
  Pod TCG w QEMU zdjęcie było już widoczne na zrzucie wykonanym 7–9 s po kliknięciu „Następne”
  (dokładnego czasu nie mierzono); okno przeglądarki jest w tym czasie zajęte, pulpit pozostaje responsywny.
* Zoom/przesuwanie: renderer liczy wyłącznie widoczne piksele; pod TCG wynik był widoczny na zrzucie
  wykonanym 2–4 s po akcji (bez dokładnych pomiarów), na sprzęcie nie mierzono. Dekodowanie nie jest powtarzane.
* Opóźnienia kursora, przeciąganie okien, klatka kompozytora, pasek zadań: **bez nowych pomiarów**.
  Zmiana kompozytora dodaje jedną kompozycję regionu paska (część wspólna z dirty-rectem) tylko,
  gdy pasek jest zarejestrowany.

## Aktualizacja (kolejna iteracja)

Zmienione wyniki natywne: PNG 3508×2480, PNG 3659×3000, JPEG 4724×3543 otwierają się w pełnej rozdzielczości, JPEG
8000×8000 jako podgląd 1/4 (poprzedni wiersz „odrzucone” jest nieaktualny); Ctrl+kółko na tablecie VirtIO działa;
rekord Snake > 0 zweryfikowany; Ctrl+Alt+B odzyskuje pasek; BIOS 1920×1080 uruchomiony. Pełne zestawienie:
`image-viewer-large-images.md`, `gallery-packaging-security.md`, `desktop-stabilization-followup.md`.
