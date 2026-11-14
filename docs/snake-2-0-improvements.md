# Snake 2.0

Uzupełnia `docs/snake-bursztynui-migration.md`.

## Wygląd

* **Gruba brązowo-złota ramka zniknęła.** Poprzednio rysowano jako ramkę logiczne ściany siatki
  20×15 (cały obwód = 1 komórka, ok. 26 px). Teraz widoczne jest tylko pole gry 18×13 (`WIDOCZNE_X/Y`),
  a ściany logiczne pozostają w logice (siatka 20×15, reguły kolizji bez zmian). Pole otacza
  **cienka obwódka ≈1 px** (skalowana DPI, `ui_skaluj(1, …)`) w kolorze `MOTYW_KOLOR_RAMKA`;
  oddzielenie od okna to subtelna różnica tła: okno `OKNOTLO`, pole gry `POLETEKSTOWE`.
  Żaden kolor ramki nie jest zakodowany na stałe. Komórki są kwadratowe, plansza wyśrodkowana
  (`uklad_planszy`: komórka = `min(w/18, h/13)`).
* Interfejs: menu **Gra / Ustawienia / Pomoc**, wiersz **Wynik / Rekord** z przyciskami „Nowa gra”
  i „Pauza/Wznów”, plansza, pasek stanu (stan | prędkość | skróty). Bez zbędnego drugiego tytułu
  „Snake” w aplikacji. Nakładka stanu (start/pauza/koniec/wygrana) jest kompaktowa – wymiary wynikają z
  zmierzonego tekstu i nie zasłania planszy; kolory z motywu.
* Przy uderzeniu w ścianę głowa zostaje na ostatnim polu (nie wchodzi w niewidoczną ścianę).

## Prędkość

| Ustawienie | Odstęp kroku |
|---|---|
| Wolno | 300 ms |
| **Normalnie (domyślnie)** | **220 ms** |
| Szybko | 150 ms |

Zegar monotoniczny (`gui_czas_monotoniczny_ns`) + zadanie okresowe co ~32 ms tylko w stanie „Gra”;
liczba klatek nie wpływa na tempo; maks. 3 kroki na wywołanie (brak „doganiania” po przerwie);
w pauzie/końcu/gotowości proces czeka na zdarzenia (bez busy-wait). Zmiana prędkości w trakcie gry
nie restartuje jej (ustawia tylko nowy odstęp). Testowane zegarem makiety: 299 ms → brak kroku,
301 ms → dokładnie jeden krok (Wolno), 151 ms (Szybko), 220 ms (Normalnie).

## Menu i skróty

* **Gra:** Nowa gra (start od razu), Pauza/Wznów, Rozpocznij od nowa (stan „Gotowy”), Zakończ.
* **Ustawienia:** Prędkość gry (przyciski opcji), Widoczność siatki (pole wyboru).
  Dźwięk pominięto (brak natywnego API do ponownego użycia bez dużej pracy pobocznej).
* **Pomoc:** Sterowanie, Zasady gry, O grze (natywne okna informacyjne).
* Klawisze: strzałki/WASD, Spacja (pauza/wznowienie), `R` (od nowa), `P`, `Esc` zamyka menu.
  Gdy menu lub okno dialogowe jest otwarte, klawisze należą do niego – strzałki nie zmieniają kierunku
  węża; przyciski nie przejmują fokusu klawiatury; najwyżej jeden skręt na krok.

## Rekord i ustawienia (trwałe)

* Plik tekstowy `snake1 / rekord=N / predkosc=K / siatka=B` (≤128 B), zapis przez
  `ustawienia_zapisz("snake","stan",…)` → `/uzytkownicy/domyslny/Ustawienia/snake/stan` – zwykłe
  uprawnienia PZB aplikacji, bez podnoszenia przywilejów. Walidacja ścisła (`snake_ustawienia.h`):
  zły nagłówek, nie-cyfry, rekord > 231, prędkość > 2, nieznany klucz, plik > 128 B ⇒ wartości domyślne.
* Zapis tylko przy zmianie i w ważnych momentach (koniec gry, zmiana ustawienia, nowa gra, zamknięcie),
  nigdy co klatkę; po zapisie następuje odczyt kontrolny. Gdy system plików odmówi, status pokazuje
  „Nie zapisano rekordu”, a komunikat nowego rekordu dopisuje „(nie zapisano)” – bez udawania sukcesu.
  Atomowa podmiana pliku nie jest dostępna w BWS; mały plik jest zapisywany jednym wywołaniem.
* Komunikat „Nowy rekord: N” na ekranie końca gry/wygranej.
* **Znaleziona i naprawiona usterka:** `ustawienia_zapisz` tworzył tylko ostatni poziom katalogu, a
  `/uzytkownicy/domyslny/Ustawienia` nie istnieje w świeżym systemie – żadna aplikacja nie mogła
  zapisać ustawień. `zapewnij_katalog_aplikacji` tworzy teraz kolejne poziomy ścieżki.

## Testy

`make test-snake-host` (ASan/UBSan, ~298 tys. sprawdzeń): logika (w tym zwycięstwo, kolizje, pauza,
restart, domyślne 220 ms i presety), parser ustawień (dane uszkodzone/skrajne/zbyt duże), rekord
(zapis tylko na końcu gry, wczytanie po „ponownym otwarciu”, uszkodzony plik, odmowa zapisu),
prędkości (granice czasu, zmiana bez restartu, pauza), menu i fokus, motyw (obwódka 1 px z motywu,
brak grubej ramki), układ, zmiana rozmiaru bez zmiany stanu gry.

## Aktualizacja: uprawnienia plikowe

Snake nie ma już ogólnego `pliki_zapisz`; ma nowe prawo `ustawienia_aplikacji` (zapis wyłącznie w
`/uzytkownicy/<u>/Ustawienia/snake/`, identyfikator wyprowadza jądro ze ścieżki procesu). Rekord > 0 zweryfikowany
natywnie (rekord 1 przetrwał zamknięcie i ponowne uruchomienie). Szczegóły: `desktop-stabilization-followup.md`.
