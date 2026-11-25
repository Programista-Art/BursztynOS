# Instalator: weryfikacja plików systemowych i diagnostyka błędów

## Przebieg

Maszyna stanów `jadro/gui/instalator.cpp`: GPT → ESP → BSP2FORMAT → SYSTEMFILES → WALLPAPERS → GALLERY →
VERIFY → znacznik ukończenia (`BSPINST1`, stan 2). Znacznik jest zapisywany **wyłącznie** po pełnej weryfikacji;
każdy błąd zatrzymuje dalsze zapisy (`INST_BLAD`).

* **SYSTEMFILES** (`kernel_instaluj_system_step`, `jadro/rdzen/kernel.cpp`): plan z `instaluj_wbudowane_programy`
  (≤ 128 plików), katalogi `KATALOGI_SYSTEMOWE`, każdy plik porcjami 4 KiB + FLUSH. Dla zapisanych bajtów liczone
  jest CRC-32 całego pliku i każdego segmentu 64 KiB (`InstalacjaPlikow::Rejestr`).
* **VERIFY** (`kernel_weryfikuj_system_step`): FLUSH, odczyt kontrolny metadanych BSP2, potem każdy plik systemowy
  czytany z PSF porcjami 4 KiB (`InstalacjaPlikow::weryfikuj_krok`, `jadro/gui/instalacja_plikow.h`).

Zapis blokowy na dysk docelowy jest dozwolony tylko w trakcie kroku instalatora
(`blokowy_zezwol_instalatorowi`); zapis innego procesu w tym czasie jest odrzucany przez warstwę blokową, PSF
wyłącza wtedy zapis trwały, a instalacja kończy się błędem przed znacznikiem.

## Kody błędów (`jadro/gui/instalator_format.h`)

| Kod | Stała | Typowy etap |
|---|---|---|
| 7 | `INST_IO` | GPT/ESP, VERIFY (metadane, pliki systemowe, tapety), znacznik |
| 8 | `INST_FORMAT` | BSP2FORMAT |
| 9 | `INST_PLIKI` | SYSTEMFILES, WALLPAPERS |

## Diagnostyka (od 2026-11-24)

Błąd zapisu lub weryfikacji **każdego** pliku systemowego (wcześniej logowane były tylko Ekran i Tapety) daje:

* komunikat w oknie instalatora, np.
  `Weryfikacja plików systemowych: /programy/grajek.cebula/grajek.bur – niezgodna zawartość (CRC-32), bloki 16–31.`
* jedną linię w logu szeregowym (ograniczona długość, bez zawartości plików):

```
[INSTALL DIAG] phase=VERIFY cause=crc-mismatch object=/programy/grajek.cebula/grajek.bur file=115/120
  expected=0x9dd62f8e actual=0x0f2078d0 offset=65536 length=65536 blocks=16-31 lba=1097824 persistent=1
```

| Pole | Znaczenie |
|---|---|
| `phase` | `PLAN`, `MKDIR`, `WRITE` (SYSTEMFILES) albo `VERIFY` |
| `cause` | `missing`, `size-mismatch` (expected/actual w bajtach), `read-error` (błąd odczytu nośnika, np. timeout VIRTIO-BLK), `crc-mismatch` (expected/actual CRC-32 segmentu albo pliku), `create`, `write`, `flush`, `unpack`, `mkdir`, `limit`, `transient-source` (źródło wpisu planu na stosie), `source-changed` (zapisane bajty ≠ migawka źródła z planu) |
| `file` | numer pliku w kolejności planu / liczba plików |
| `offset`, `length`, `blocks` | zakres w pliku (porcja 4 KiB albo segment 64 KiB) i numery bloków BSP2 pliku |
| `lba` | bezwzględny LBA dysku pierwszego bloku zakresu (`psf_lba_bloku_pliku`) |
| `reread` | tylko `crc-mismatch`: CRC drugiego odczytu tego samego segmentu wprost z nośnika (po porzuceniu czystych bloków cache): `matches-expected:unstable-read` = niestabilny odczyt, `same-as-first:medium-content` = na dysku naprawdę są inne bajty, `read-error` = drugi odczyt się nie udał |
| `read_us`, `max_read_us` | czas odczytu 4 KiB przed błędem i najdłuższy odczyt w tej weryfikacji (µs, zegar HPET) |
| `flush` | wynik ostatniego FLUSH PSF (`ok`/`failed`) |
| `persistent` | `1` = PSF nadal zapisuje trwale; `0` = PSF wyłączył zapis trwały po błędzie I/O |

Interpretacja: `read-error` z `persistent=0` wskazuje na nośnik/sterownik (szukaj w logu `[VIRTIO-BLK] ERROR`);
`crc-mismatch` przy poprawnym odczycie oznacza, że bajty na nośniku różnią się od zapisanych (utracony albo
przekłamany zapis, albo zmiana pliku po zapisie); `missing`/`size-mismatch` – plik usunięty lub zmieniony po zapisie.

Sterownik VirtIO-BLK loguje `[VIRTIO-BLK] TIMEOUT op=read lba=… sectors=… elapsed_ms=… limit_ms=5000
reason=deadline|spin-guard|no-clock`. Krótki odczyt (`used.len` < dane + status przy statusie OK) jest od
2026-11-25 **błędem I/O** (cały bufor wyzerowany, licznik udanych odczytów bez zmian, pierwsze 16 logowane jako
`[VIRTIO-BLK] ERROR short read … bytes=… expected=…`): sterownik negocjuje wyłącznie `VIRTIO_F_VERSION_1`, a
VIRTIO 1.x zabrania zakładania czegokolwiek o danych poza pierwszymi `len` bajtami. Wcześniej brakująca część
była zerowana i zwracano sukces. QEMU zgłasza pełną długość, więc nie dotyczyło to błędu 108/120.

Komunikat GUI ma trzy wiersze (etap z numerem pliku, ścieżka, przyczyna z blokami) i jest zawijany w oknie
instalatora; pełny rekord pozostaje w logu szeregowym.
Po udanej weryfikacji: `[INSTALL] VERIFY files OK: files=… bytes=… segments=… max_read_us=…`.

## Czy weryfikacja czyta dysk, czy pamięć podręczną?

Dysk. Faza metadanych (`psf_weryfikuj_dysk_step`) porównuje każdy blok metadanych z nośnikiem bajt po bajcie i
kończy się `wyczysc_cache_blokow()`, więc faza plików systemowych zaczyna z pustą pamięcią podręczną danych PSF
(256 bloków = 1 MiB) i każdy blok czyta z nośnika (test hosta, scenariusz 12: plan mieszczący się w cache, sektor
uszkodzony na nośniku po zapisie – wykryty). Zapisy instalatora idą przez `Storage::write_verify` (zapis + odczyt
kontrolny sektora). Jedynie diagnostyczny drugi odczyt segmentu musi ominąć cache – służy do tego
`psf_porzuc_czyste_bloki_cache()`, które porzuca wyłącznie czyste bloki i nic nie robi, gdy cokolwiek jest brudne.

## Potwierdzony błąd natywny (2026-11-25): źródło planu na stosie

Windows 11, QEMU TCG, VirtIO-BLK, jednorazowy `instalacja-test.img`:
`[INSTALL DIAG] phase=VERIFY cause=crc-mismatch object=/programy/pcm440.cebula/opis.aplikacji file=108/120
expected=0x559d4d22 actual=0x163a1d8b offset=0 length=130 blocks=0-0 lba=1096008 reread=0x163a1d8b(same-as-first…)`.

Zamierzona treść pliku (manifest PCM 440 Hz z `instaluj_wbudowane_programy`) ma 130 B i CRC-32 **0xb1ce25d5** –
różną od obu wartości z logu. Przyczyna: `manifest440`/`manifest660` były **lokalnymi** tablicami
(`const char x[] = "…"`, kompilator kopiuje literał na stos: `rep movsq` → `lea 0xb32(%rsp)`), a plan krokowy
(`PlanowanyPlik`) zapamiętuje tylko wskaźnik. Plik 108 jest zapisywany w późniejszym `perform()`, gdy ramka już
nie istnieje: na dysk trafiły bajty, które akurat były w tym miejscu stosu (A), a CRC rejestru – liczone po
zapisie i FLUSH z tej samej, znów nadpisanej pamięci (B). Nośnik był stabilny (`same-as-first`), VirtIO-BLK i
PSF działały poprawnie. Test hosta (scenariusz 14a) odtwarza tę sygnaturę co do pola, łącznie z `lba=1096008`.

Poprawka i zabezpieczenia:

* `static const char manifest440[]/manifest660[]` (jak wszystkie pozostałe manifesty).
* `planuj_plik` odrzuca wpis, którego źródło leży między bieżącym RSP a ramką `kernel_instaluj_system_start`
  (`cause=transient-source`, etap PLAN – instalacja nie zaczyna kopiowania).
* Migawka CRC źródła w chwili planowania (pliki surowe ≤ 64 KiB; skompresowane bloby mają własne CRC w nagłówku
  BZB1 sprawdzane przy rozpakowaniu): po zapisie pliku CRC zapisanych bajtów musi się z nią zgadzać, inaczej
  `phase=WRITE cause=source-changed` (expected = CRC z planu) i brak znacznika.
* `narzedzia/instalator/manifest_instalacji.py` (i `testy/test_instalator_zrodla_planu.py`) odrzuca nie-static
  tablice przekazywane do funkcji planu.

## Natywna diagnostyka w Windows 11 (QEMU)

Launcher `runwindows.ps1` domyślnie używa trwałego `wirtualny_dysk.img`. Do testów służy **`-TestDisk`**
(jednorazowy plik `.img`, tworzony tylko gdy nie istnieje – `CreateNew`, nigdy `qemu-img create`; odrzucane są
nazwy `wirtualny_dysk.img`/`wirtualnydysk.img`, ścieżki UNC i urządzeń; wymaga wolnego miejsca rozmiar + 1 GiB),
**`-UseExistingTestDisk`** (jawna zgoda na istniejący dysk testowy, np. start zainstalowanego systemu),
**`-IsoSha256`** (niezgodna suma ISO przerywa przed QEMU) i **`-SerialLog`** (pełny log COM1 także w trybie 14).

```powershell
.\runwindows.ps1 -PrintArgv -Mode 15 -TestDisk instalacja-test.img -AudioMode sdl     # tylko JSON, nic nie tworzy
.\runwindows.ps1 -Mode 15 -TestDisk instalacja-test.img -IsoSha256 <sha256> -AudioMode sdl
.\runwindows.ps1 -Mode 14 -TestDisk instalacja-test.img -UseExistingTestDisk -SerialLog -AudioMode sdl
```

Log COM1: `logi_qemu\<data>_Instalator_UEFI_VirtIO_Full.log` (pełny, zapis linia po linii).

## Testy hosta

`make -j1 test-instalacja-host` (po `make -j1 iso`): `narzedzia/instalator/manifest_instalacji.py` odtwarza
rzeczywisty manifest instalacji z `kernel.cpp` i modułów wpisu instalatora w `isodir/boot/grub/grub.cfg`
(te same ścieżki i bajty), a `testy/test_instalacja_host.cpp` instaluje go na jednorazowych obrazach w
`.installertest/` produkcyjnym kodem PSF/GPT i modułem `instalacja_plikow.h`: czysty dysk 4 GiB (BSP2 2 GiB) z
niezależnym ponownym montażem i porównaniem bajt po bajcie, przekłamany odczyt, utracony zapis, błąd odczytu,
błąd zapisu i FLUSH, plik usunięty/zmieniony po zapisie, zapis spoza okna instalatora, pełny dysk, pełna tablica
węzłów, nazwy 55/56 B, uszkodzone metadane. Krok SYSTEMFILES jest w teście odwzorowany (kernel.cpp nie buduje się
na hoście). Nigdy nie używa `wirtualny_dysk.img`.
