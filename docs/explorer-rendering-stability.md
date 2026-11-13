# Eksplorator: migotanie przy przewijaniu i wyszukiwaniu

Gałąź `feature/bsf-2-2-font-formats`. Zmiany niezatwierdzone.

## Objaw

Podczas szybkiego przewijania (kółko, pasek) i pisania w polu „Szukaj” zawartość
Eksploratora chwilowo znika i pojawia się ponownie.

## Przyczyna (zmierzona)

Aplikacje rysują do bufora **warstwy** wieloma osobnymi syscallami (Eksplorator: ~400 syscalli
`gui_rysuj_prostokat` / `gui_blit_alpha` na klatkę). Kompozytor składa warstwę w
każdej chwili, gdy cokolwiek innego zażąda złożenia (ruch kursora, inne okno,
zegar). Jądro nie rozróżniało „klatki ukończonej” od „klatki w trakcie rysowania”:
`gui_odswiez()` oznaczał tylko region do ponownego złożenia, ale *odczyt* bufora przez
kompozytor mógł nastąpić między syscallami — widać wtedy tło wiersza już wyczyszczone,
a tekst jeszcze nienarysowany.

Pomiar (liczba złożeń warstwy, której właściciel jest w trakcie rysowania; metryka
`torn_compose` w `[GUI PERF]`, build z `BURSZTYNDIAGGUIRUNTIME=1`; Eksplorator,
katalog `/programy`, 20 s: porcje kółka myszy na przemian z ruchem wskaźnika, QEMU/TCG):

| wariant | złożenia „rozdartej” klatki | złożenia z cienia | odświeżenia `gui_odswiez` |
|---|---|---|---|
| przed (cień wyłączony, `BURSZTYN_DIAG_NO_SHADOW=1`) | **39** | 0 | 251 |
| po (cień, zawór 1 s) | **0** | 39 | 171 |
| po (cień, zawór 250 ms — wersja pośrednia) | 7 | 19 | 196 |

Ostatni wiersz pokazał, że zawór 250 ms był za krótki dla klatek Eksploratora pod
TCG (setki syscalli po ≥ 0,25 ms) — zawór ustawiono na 1 s.

Aplikacje `BursztynUI` (`Okno::rysuj`) rysują każdy brudny region do bufora w Ring 3 i
przekazują go jednym `gui_blit` (jedno złożenie = jedna spójna zmiana), więc nie
były dotknięte; problem dotyczył aplikacji z rysowaniem bezpośrednim (Eksplorator,
Terminal, Snake, Przeglądarka Obrazów).

## Poprawka: cień warstwy (`sterowniki/grafika/grafika.cpp`, `jadro/gui/skladacz_obrazu.cpp`)

* Przy **pierwszym zapisie do warstwy po publikacji** jądro kopiuje opublikowaną treść do
  cienia (raz na klatkę; bufor z dedykowanego allocatora grafiki).
* Kompozytor (`zloz_snapshot_region`) czyta cień dla warstw w trakcie rysowania — użytkownik
  widzi zawsze ostatnią **ukończoną** klatkę.
* `gui_odswiez()` kończy transakcję (cień nieaktywny, kompozytor czyta bufor na żywo).
* Zawór bezpieczeństwa: cień starszy niż 1 s jest ignorowany, więc aplikacja, która
  nie odświeża, nigdy nie zostanie zamrożona.
* Pomija warstwę pulpitu (`z_order == 0`); cień zwalniany przy końcu procesu
  (`bws_gui_usun_stan_procesu`); zmiana rozmiaru okna unieważnia cień (kontrola rozmiaru).
* Brak pamięci na cień → zachowanie sprzed poprawki (nie ma nowych błędów krytycznych).
* Nie wprowadzono wspólnej pamięci między procesami: cień jest pamięcią jądra.

Gdyby okno miało być odświeżane bez `gui_odswiez()` (inna ścieżka publikacji), zawór 1 s
zadziała jako zabezpieczenie; takich ścieżek w produkcyjnych aplikacjach nie znaleziono.

## Zmiana rozmiaru bez znikania okna

`zmien_geometrie_warstwy()` zachowuje teraz ostatnią opublikowaną treść i widoczność
(poprzednio nowa warstwa była zerowana i niewidoczna do pierwszej klatki), więc
rozciąganie okna nie powoduje znikania.

## Wyszukiwanie i przewijanie — stan i granice

* Eksplorator rysuje listę wirtualnie (`draw_list_only`, tylko widoczne wiersze,
  `TabelaWirtualna`) — tej architektury nie zmieniano.
* Nie zmieniono mechaniki wyszukiwania (filtrowanie modelu na każdy znak). Metryka
  `torn_compose` = 0 obejmuje scenariusz kółka+ruchu; **pisanie w polu Szukaj nie zostało
  osobno zmierzone natywnie** (korzysta z tego samego mechanizmu rysowania i cienia).
* Nie dodano debounce ani anulowania wyszukiwania.
* Nie wykonano testów 1000 / kilka tysięcy plików (tylko katalogi do 19 wpisów) ani
  testów obrazkowych porównujących zrzuty.

## Testy

* Natywnie: skrypty `narzedzia/guest_session.py` + pomiar `[GUI PERF]` (tabela wyżej).
* Host: `make test-explorer-host` (po poprawce atrapy `gui_blit`, patrz niżej),
  `test-compositor-cursor`, `test-gui-runtime-diag`.

## Stan testu hosta `test-explorer-host`

`make test-explorer-host` **pada już na HEAD `2c30b5d`** (sprawdzone w odłączonym worktree) i pada
nadal — asercja `opaque` (wszystkie zapisane piksele mają alfa 255). Zlokalizowano
piksele (np. (92..95, 91..96), strzałka „wstecz” paska narzędzi) o wartości `0x00FFBF00`, czyli
kolor akcentu **bez alfa**; w produkcji kompozytor traktuje piksel jako przezroczysty tylko dla
dokładnie `0x00000000`, więc taki piksel jest widoczny. Nie rozstrzygnięto, czy źródłem jest
atrapa motywu w `testy/bursztyn_ui_host_fixture.h`, czy stała w kodzie ikon. To rozjazd
oczekiwania testu z realnym modelem przezroczystości, nie zmiana z tej sesji;
**nie został naprawiony** (nie osłabiałem asercji). Inne testy Eksploratora (`test-desktop-files-host`, `test-file-popup-host`,
`test-shell-host`) przechodzą.
