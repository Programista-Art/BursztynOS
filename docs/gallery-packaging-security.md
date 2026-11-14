# Pakiet galerii: bezpieczeństwo i skanowanie wydania

## Co było źle

Poprzedni format „BZP2” kodował zdjęcia wariantem base64 z alfabetem dobranym tak, by skaner metadanych wydania
(`narzedzia/verifyreleasemetadata.py`) nigdy nie znalazł w nich wzorców (`/`, `:`, `.`, `-`, `_`, `@`, `A` zostały
usunięte z alfabetu). To było obejście skanera, a nie jego poprawne użycie: dane binarne były ukryte przed
wzorcami, a przy okazji ISO rosło o 33% (modul 72 MB zamiast 55 MB).

## Nowy kontener BZP3 (`jadro/fs/galeria_pak.h`)

Binarny, bez kodowania:

```
"BZP3" | u32 wersja=1 | u32 liczba (1..256) | u32 rozmiar tabeli | u8[32] SHA-256 tabeli
tabela: { u8 dl_nazwy (1..63) | nazwa UTF-8 | u32 rozmiar | u32 przesuniecie | u32 CRC-32 danych } × liczba
dane:   kolejno, bez luk, do końca kontenera
```

Parser (jądro, host, skaner) odrzuca: złą sygnaturę/wersję/liczbę, tabelę poza kontenerem lub nie zużytą w całości,
niezgodną SHA-256 tabeli, nazwy ze znakami sterującymi, `/`, `\`, `.`, `..`, nazwy dłuższe niż 63 B, rozszerzenia
spoza `png/jpg/jpeg/btp/bmp`, duplikaty (także różniące się wielkością liter ASCII), rozmiar 0 lub > 64 MiB,
przesunięcia niezgodne z ciągłym układem (przepełnienia sprawdzane w 64 bitach), wpisy wychodzące poza kontener
oraz „ogon” po ostatnim wpisie. Kontener ≤ 200 MiB, wpisów ≤ 256.

Import (jądro, `importuj_galerie_z_modulu`):
* ścieżka docelowa to zawsze `/dane/zdjecia/<nazwa>` (nazwa zwalidowana, brak ucieczki z katalogu);
* istniejące pliki użytkownika **nigdy** nie są nadpisywane;
* CRC-32 wpisu jest sprawdzane przed utworzeniem pliku; niepełny plik jest usuwany;
* zdjęcia **nie** trafiają do obrazu jądra (moduł Multiboot2 `/boot/zdjecia.pak`).

## Skaner wydania rozróżnia rodzaje danych (`narzedzia/galeria_skaner.py`)

| Rodzaj | Traktowanie |
|---|---|
| nagłówek, tabela, nazwy plików w kontenerze | skanowane jako tekst wszystkimi wzorcami (sekrety, ścieżki, nazwy źródeł) |
| metadane zdjęć (JPEG APPn/COM, PNG tEXt/iTXt/zTXt/eXIf/iCCP…, nagłówki BMP/BTP) | skanowane jako tekst wszystkimi wzorcami |
| dane pikseli (entropia JPEG, IDAT PNG, dane BMP/BTP) | **walidowane wg rzeczywistego formatu** (JPEG: markery do EOI; PNG: chunki z CRC, IHDR/IEND; BMP: nagłówki i rozmiar; BTP1: dokładny rozmiar RAW/RLE; BTP2: pełne dekodowanie `btpcodec`), nie skanowane jako tekst |
| niepoprawny kontener lub wpis | finding (`gallery-container-invalid` / `gallery-entry-invalid`) **oraz** skan surowych bajtów — nic nie jest pomijane po cichu |
| każdy inny plik | bez zmian: pełny skan surowych bajtów |

Skaner dla prawdziwego pakietu: 6 wpisów, 18 regionów, 43 546 B tekstowych z 55 MB, 0 findings. Pozostałe
kontrole wydania (symbole, debug, ścieżki developerskie, diagnostyka, EFI) nietknięte. Testy:
`narzedzia/test_release_metadata.py` (16 testów, w tym: piksele nie są skanowane, metadane i nazwy są,
manipulacja kontenerem/CRC/SHA/nazwami/duplikatami/ogonem jest zgłaszana i skanowana surowo),
`make test-galeria-pak-host` (60 sprawdzeń + fuzz 20 000 mutacji pod ASan/UBSan, zgodność z prawdziwym pakietem).

Ograniczenie: ukrycie tekstu w samych pikselach (np. w IDAT) nie jest wykrywalne tekstowo — to właściwość
skompresowanych danych; format jest walidowany strukturalnie, a metadane (jedyne miejsce na tekst) skanowane.

## Rozmiar ISO

| | rozmiar ISO | moduł zdjecia.pak |
|---|---|---|
| BZP2 (base64 wariant) | 118 908 928 B | ~73,9 MB |
| BZP3 (surowe) | 100 423 680 B | 55 460 562 B |

(−18,5 MB, −15,6%). Kernel: `__kernel_end` = 0xB00000 (11,0 MiB), zapas do limitu 15 MiB: 4,0 MiB.

## Live ISO i system zainstalowany

* **Live** (RAM-dysk PSF 128 MiB wspólny z systemem): import od najmniejszych zdjęć; wpis, który się nie mieści,
  jest usuwany w całości i zgłaszany w logu (`[GALERIA] wpis nie miesci sie w RAM-dysku`). UEFI 1280×720: 6/6;
  BIOS 1920×1080 (przed zmianą kolejności): 5/6, jeden wpis nie zmieścił się (dowód w logu `BLEDNE wpisy: 1`).
* **Instalator**: wpis rozruchowy instalatora dostaje `module2 /boot/zdjecia.pak`; po tapetach nowy etap
  `galeria_instaluj_*` (porcje 64 KiB, flush po pliku) kopiuje zdjęcia na dysk docelowy: nie nadpisuje, usuwa
  niepełny plik, a **każdy błąd (np. brak miejsca) kończy tylko etap galerii** — instalacja systemu, weryfikacja
  i znacznik rozruchu przebiegają dalej. Zainstalowany system czyta zdjęcia z dysku (bez modułu).
* **Status testów**: kontener i parser — host (ASan/UBSan, fuzz); import live — natywnie (UEFI i BIOS);
  etap instalatora — kompiluje się i jest wpięty w maszynę stanów, ale **nie był uruchomiony w gościu**
  (harness instalatora jest w tym środowisku niewiarygodny: timeouty VIRTIO-BLK/GOP). Wymaga ręcznego testu:
  instalacja z ISO → pierwszy start → `/dane/zdjecia` ma 6 plików; instalacja na małym dysku → instalacja kończy się
  sukcesem bez zdjęć (w logu `[INSTALL] galeria: blad zapisu`).
