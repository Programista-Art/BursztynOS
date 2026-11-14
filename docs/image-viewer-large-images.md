# Przeglądarka Obrazów — duże obrazy (dwupoziomowy model)

## Problem

Przeglądarka otwierała tylko 2 z 6 zdjęć w `/dane/zdjecia`: pojedyncza alokacja ponad 16 MiB była w jądrze
zarezerwowana dla programu Tapety, a wspólny dekoder miał limit 8 Mpx / 16 MiB. Trzeba rozdzielić trzy sprawy:

| Sprawa | Gdzie jest ograniczana |
|---|---|
| rozmiar zakodowanego pliku | `OBRAZ_MAX_PLIK` (16 MiB, domyślnie) / `OBRAZ_TWARDY_MAX_PLIK` (32 MiB, tylko jawnie) |
| wymiary i pamięć dekodowania | `OBRAZ_MAX_BOK` 8192, `OBRAZ_MAX_PIKSELE` 8 Mpx (domyślnie) / `OBRAZ_TWARDY_MAX_PIKSELE` 17 Mpx (tylko jawnie); szacunek szczytu pamięci w aplikacji |
| pamięć procesu (tymczasowa + trwała) | sterta procesu 256 MiB (jądro) i `BUDZET_SZCZYTU` = 200 MiB (aplikacja) |

Nie ma „nieograniczonej” alokacji ani globalnego wyłączenia limitów.

## Poziom 1 — pełna rozdzielczość

Warunki (sprawdzane PRZED wczytaniem pliku, z nagłówka): bok ≤ 8192, piksele ≤ 17 Mpx, plik ≤ 32 MiB oraz
szacunek szczytu pamięci `plik + k·px` ≤ 200 MiB (`k` = 7 dla PNG: bufor surowy + RGBA; `k` = 9 dla JPEG:
współczynniki + płaszczyzny + RGBA). Dekodowanie używa `dekoduj_obraz_z_alfa_limity()` — limity podnoszone
tylko na czas jednego wywołania i zawsze przycinane do twardych sufitów.
Niepowodzenie alokacji w trakcie (dekoder zgłasza je jako „uszkodzony”, aplikacja rozpoznaje je po liczniku
odmów alokatora) kończy się powrotem do poziomu 2 dla JPEG albo komunikatem „Brak pamięci na obraz”.

### Zaufana alokacja w jądrze (`bws_alokuj_sterte`, BWS 35)

Pojedynczy blok do **72 MiB** (powierzchnia BGRA 17 Mpx = 64,9 MiB) dostaje wyłącznie proces, który spełnia
wszystkie warunki jednocześnie:

1. loader zweryfikował **SHA-256 bajtów, które mapuje** (tablica wbudowana w jądro, `skroty_systemowe.generated.h`,
   wygenerowana z `przegladarka_obrazow.bin` przy budowaniu; albo podpis release dla aktualizacji), a nie
   samą nazwę pliku, etykietę PZB czy manifest — wynik zapisuje `g_przegladarka_obrazow_zweryfikowana[pid]`
   (czyszczone przy rezerwacji slotu, więc PID nie dziedziczy zaufania);
2. ścieżka procesu to `/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur`;
3. proces ma `PRAWO_GUI | PRAWO_PLIKI_CZYTAJ`.

Inne aplikacje: limit bez zmian (40 MiB tylko Tapety, 16 MiB reszta). Obowiązuje nadal limit sterty 256 MiB,
kontrola przepełnień i rollback mapowania.

## Poziom 2 — podgląd JPEG (`obrazy/podglad_jpeg.*`)

Dla JPEG, który nie mieści się w poziomie 1 (np. 8000×8000 = 244 MiB BGRA). Własny, wznawialny dekoder:
nie tworzy pełnego obrazu, czyta plik **strumieniowo** (bufor 32 KiB, bez wczytywania całości — plik 30,9 MB
nie zajmuje pamięci), zapisuje tylko zredukowane płaszczyzny składowych:

* baseline/extended (SOF0/1): skala 1/2 albo 1/4 (zredukowane IDCT 4×4 / 2×2) albo 1/8 (tylko DC) — wybierana tak,
  by podgląd ≤ 4,2 Mpx i ≤ 48 MiB (8000×8000 → 1/4, 2000×2000, 15,2 MiB);
* progresywny (SOF2): tylko skany DC → zawsze 1/8;
* restart interval, 4:4:4/4:2:0/4:2:2, skala szarości, Adobe RGB; odrzuca arytmetyczne, 12-bit, CMYK;
* praca w porcjach ~12 ms co 16 ms (timer aplikacji) — interfejs pozostaje żywy, wskaźnik postępu w widoku;
* nawigacja w trakcie dekodowania zamyka dekoder i zwalnia pamięć.

Nie jest to widok 100%: etykieta „[podgląd 1/N]” w pasku stanu, procent względem PLIKU (`~5%`), przycisk 100%
nieaktywny, skrót `1`/`0` i menu pokazują wyjaśnienie. Wymiary w pasku stanu to wymiary pliku.
Brak podglądu dla PNG (ponad 17 Mpx → komunikat „podgląd tylko dla JPEG”).

## Poprawka walidatora JPEG

Plik 4724×3543 (libjpeg, progresywny) był odrzucany jako „uszkodzony”, bo `sprawdz_jpeg` zabraniał każdej
ponownej definicji tablicy Huffmana. libjpeg zapisuje kolejne DHT między skanami progresywnymi. Redefinicja
jest teraz dozwolona **tylko między skanami JPEG progresywnego**; duplikaty w baseline i przed pierwszym skanem
nadal są odrzucane (istniejące asercje `test_dekoder_obrazu.cpp` bez zmian).

## Wyniki natywne (QEMU/TCG, UEFI 1280×720, tablet VirtIO; „Pomoc → Informacje o obrazie”)

| Plik | Wymiary | Tryb | Powierzchnia | Szczyt pamięci obrazu | Czas wczytania |
|---|---|---|---|---|---|
| `19_06_2019.png` | 3508×2480 | pełna rozdzielczość | 33,1 MiB | 61,4 MiB | 1,4 s |
| `2019. ołówkiem.jpg` | 1920×1410 | pełna rozdzielczość | — | — | (niemierzony) |
| `2019r-digital-painting-…jpg` | 8000×8000 (30,9 MB) | **podgląd 1/4** (2000×2000) | 15,2 MiB | 26,7 MiB | 90 s (TCG; host: 3,5 s) |
| `jablka2w.png` | 3659×3000 | pełna rozdzielczość | 41,8 MiB | 84,3 MiB | 3,6 s |
| `obraz-generowany.btp` | 1280×720 | pełna rozdzielczość | — | — | (niemierzony) |
| `Warszawa-2019.jpg` | 4724×3543 | pełna rozdzielczość | 63,8 MiB | 143,2 MiB | 6,2 s |

Pamięć po przejściu do kolejnego pliku wraca do rozmiaru bieżącej powierzchni („Aktualnie zajęte” = powierzchnia).
Ostatnie rysowanie (pierwszy render Dopasuj, TCG): 0,31–0,42 s. Pomiary są z QEMU bez KVM (wolne ok. 25×).
Szczyt dla JPEG 4724×3543 (143 MiB) mieści się w budżecie 200 MiB i w stercie 256 MiB.

## Testy

* `make test-przegladarka-obrazow-host` (ASan/UBSan, 371 sprawdzeń): poziom 1 (PNG/JPEG do 17 Mpx), odrzucenia
  z nagłówka (bok > 8192, PNG > 17 Mpx; ≤ 3 odczyty), poziom 2 (JPEG 420/444+restart/progresywny/szary/48 Mpx:
  skala, kolory ćwiartek, brak widoku 100%, powiększenie podglądu), nawigacja w trakcie podglądu, brak pamięci
  (powrót do podglądu; błąd bez wycieków), pamięć po zamknięciu = baza, JPEG progresywny z redefinicją DHT.
* `make test-image` (w tym nienaruszone asercje duplikatów tablic).
* Natywnie: wszystkie 6 plików (tabela wyżej), zoom przyciski/suwak/Ctrl+kółko (tablet i mysz USB), Dopasuj,
  100% (tam, gdzie możliwe), nawigacja, błąd dekodowania nie blokuje nawigacji (host).

## Ograniczenia

* Podgląd 8000×8000 w QEMU/TCG trwa ~1,5 min (UI pozostaje responsywny); na sprzęcie ~sekundy.
* Brak widoku 100% dla podglądu i brak podglądu dla PNG powyżej budżetu.
* Przy zmianie rozmiaru okna obraz pozostaje (render z powierzchni); nie testowano natywnie wszystkich kombinacji.
* Test braku pamięci tylko na hoście (symulowany limit alokatora), nie w gościu.
