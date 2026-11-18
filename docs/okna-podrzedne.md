# Okna podrzędne BursztynUI (modeless, natywne)

## Przyczyna wad dawnych okien informacyjnych
Przeglądarka Obrazów i Snake pokazywały Pomoc przez `Komunikat::informacja` (modalny popup rysowany WEWNĄTRZ okna rodzica,
`zarzadca_popupow`). Komponent ma sztywne wymiary i prosty układ tekstu: długi, wielowierszowy tekst nie był zawijany do
szerokości, wysokość nie zależała od liczby wierszy (przyciski i separator nachodziły na treść), a wszystko było obcinane
do powierzchni rodzica i zależne od jego rozmiaru. To wada komponentu (zły model: dialog 1-2 zdań), nie samych aplikacji.

## Architektura
System ma **jedną warstwę okna na proces** (`tablica_warstw[pid]`), więc „okno podrzędne” nie mogło być drugą warstwą tego
samego procesu bez przebudowy kompozytora. Zamiast emulacji (prostokąt w buforze rodzica) okno podrzędne to prawdziwy, osobny
proces Ring 3 z własnym oknem, własnością i fokusem — wbudowany program `okno-informacyjne` (BursztynUI).

| Element | Plik | Rola |
|---|---|---|
| API aplikacji | `bursztyn_ui/okno_podrzedne.h` (`OknoPodrzedne::otworz`) | wywołuje BWS 101 z rodzajem, tytułem i treścią |
| Okno (klient) | `bursztyn_ui/okno_podrzedne.h` (`OknoInformacyjne`) + `programy/okno_informacyjne/` | układ BSF, przewijanie, pasek akcji OK/Anuluj, klawisze |
| Układ treści | `bursztyn_ui/tresc_uklad.h` | zawijanie po słowach/znakach UTF-8, 2 kolumny, nagłówki; czysta logika, test hosta |
| Jądro: wlasność | `jadro/bws/okna_podrzedne.h`, `bws_nowoczesne.cpp` | rejestr (dziecko → właściciel), limity, duplikaty, treść tylko dla dziecka (BWS 102) |
| Prawo | `PRAWO_OKNO_PODRZEDNE` (`pzb.h`, manifest `okno_podrzedne`) | tylko Przeglądarka Obrazów i Snake; nie daje ogólnego uruchamiania programów |
| Stos/min/przywracanie | `skladacz_obrazu.cpp` | dzieci zostają NAD właścicielem; minimalizacja/przywrócenie razem z właścicielem |
| Sprzątanie | `scheduler.cpp` → `okna_podrzedne_przed_sprzataniem` | zakończony właściciel: dzieci osierocone + prośba o zamknięcie; zakończone dziecko: fokus wraca do właściciela |
| Loader | `loader.cpp` | okno informacyjne jako jedyny program wielokrotnych instancji (≤ 6); PID ostatniego uruchomienia |

BWS 101 `(rodzaj, treść*, długość, tytuł*)`: 1 = utworzono, 2 = istniejące (ten sam właściciel i rodzaj) aktywowane z fokusem i
odświeżoną treścią (zdarzenie OTWORZ_PLIK → ponowny BWS 102), 0 = odmowa. Walidacja: prawo, rodzaj 1..6, treść 1..4096 B bez NUL
i znaków sterujących (poza `\n`, `\t`), tytuł < 64 B, ≤ 4 okna na właściciela, ≤ 8 w systemie, dziecko nie otwiera okien,
własność nie dziedziczona po ponownym użyciu PID (okna osierocone mają właściciela −1; `ustaw_pid` odrzuca duplikat PID).
Proces dziecka dostaje wyłącznie PRAWO_GUI (bez plików, sieci, uruchamiania).

## Format treści
`# Sekcja` (nagłówek, akcent motywu), `Pole⇥Wartość` (dwie kolumny; kolumna kluczy ≤ 40% szerokości, oba pola zawijane),
pusta linia (odstęp), inne wiersze = akapity zawijane. Tekst mierzony aktywną czcionką BSF i skalą UI (`CzcionkaUI`).
Rozmiar okna: 520 px logicznych (≤ 90% obszaru roboczego), wysokość z układu (≤ 80% obszaru), minimum 360×240; dłuższa treść
jest przewijana (pasek, kółko, ↑ ↓ PgUp PgDn Home End), nigdy skracana ani pomniejszana. Dolny pasek akcji (OK, Anuluj) jest
osobnym wierszem pod separatorem. Enter = OK (chyba że fokus na Anuluj), Esc / X / Anuluj = Anuluj; wyniki `Wynik::OK` /
`Wynik::Anuluj` są rozróżnialne dla przyszłych okien edycyjnych. Zdarzenia znakowe będące echem klawiatury fizycznej
(sekwencje VT strzałek zaczynają się od ESC) są ignorowane — inaczej strzałki zamykałyby okno (błąd znaleziony natywnie).

## Zawartość okien
Przeglądarka Obrazów: O programie (nazwa, wersja 0.1, formaty, galeria `/dane/zdjecia`, limity 17 Mpx / 200 MiB / podgląd JPEG),
Informacje o obrazie (wszystkie dotychczasowe pola + format; dwie kolumny), Sterowanie (nawigacja, powiększanie, przesuwanie).
Snake: Sterowanie, Zasady gry (zgodne z `snake_logika.h`: wynik = długość − 3, ściana/ciało kończą grę, pełna plansza = wygrana,
rekord), oraz O grze (ten sam wadliwy komponent, więc również przeniesiony). Pozostał modalny komunikat „Widok 100% niedostępny”.

## Testy
Host (ASan/UBSan): `test-tresc-uklad-host` (117), `test-okna-podrzedne-host` (209 253: rejestr, limity, walidacja, duplikaty, osierocanie,
ponowne użycie PID, 20 000 losowych operacji), `test-snake-host`, `test-przegladarka-obrazow-host` (okna zlecane z właściwym rodzajem/tytułem/treścią,
brak modalnego popupu, wszystkie pola informacji, treść mieści się w układzie w wąskim oknie), `test-hda-diag` (numeracja BWS 101/102).
Natywnie (QEMU/TCG): UEFI 1280×720 tablet — wszystkie okna (ciemny i jasny motyw), fokus, przesuwanie, stos nad rodzicem, duplikat (aktywacja z fokusem),
OK / Anuluj / X / Enter / Esc, strzałki i WASD w oknie podrzędnym nie sterują wężem, minimalizacja i przywrócenie rodzica z dzieckiem, zamknięcie rodzica
zamyka dziecko, zmiana rozmiaru okna podrzędnego (pasek przewijania + kółko), 31 cykli otwarcie/zamknięcie bez wzrostu zajętości ramek PMM (459760–459763), mysz USB (relatywna),
BIOS 1920×1080. Zrzuty: `docs/zrzuty-okna-podrzedne-2026-11-15/`.

## Znalezione przy okazji
* Limit 96 plików w planie instalacji/kontroli (`kernel.cpp`) był wyczerpany — nowy program nie instalował się (podniesiono do 112) oraz brakowało katalogu `/programy/okno-informacyjne.cebula` w liście katalogów startowych.
* `BWS 97` (`proces_zabij`) oznacza proces jako KONCZACY, ale nie rejestruje jego zasobów do zwolnienia (`zasoby_do_zwolnienia` ustawia tylko samozakończenie): zabity proces nie jest sprzątany. Dlatego okna podrzędne zamykane są prośbą (ZAMKNIJ), nie zabiciem. Nie naprawiano (poza zakresem).
* `testy/test_dzwieku_packaging.py` i `test_diagnostyka_packaging.py` oczekują `BWS_PIERWSZY_WOLNY = 98` (nieaktualne od wcześniejszych etapów).

## Ograniczenia
Wymiary 1024×768 nie były testowane (UEFI uruchamia 1280×720, BIOS 1920×1080). Okno podrzędne nie jest ukryte na pasku zadań (jest osobnym wpisem).
Brak wymuszonego zabicia zawieszonego okna informacyjnego (patrz BWS 97). Tekst z innego okna nie jest odświeżany, jeśli proces dziecka nie obsługuje OTWORZ_PLIK.
