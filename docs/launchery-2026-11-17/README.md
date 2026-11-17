# Audyt launcherów QEMU: runwindows.ps1 i rununix_virtio_latest.sh (2026-11-17)

Plik linuksowy nazywa się `rununix_virtio_latest.sh` (z podkreślnikami). Starszy `rununix.sh` nie był częścią zadania i nie został zmieniony.

## Znalezione problemy

### rununix_virtio_latest.sh

| Problem | Skutek |
|---|---|
| Brak `virtio-tablet-pci` (Makefile dodaje go jako ostatnie urządzenie, BDF 00:09.0) | Inny sprzęt niż `make runuefi`. Na hostach z bezwzględnym wskaźnikiem (VirtualBox) brak preferowanego tabletu. |
| Brak obejścia SDL2 + IBus | AltGr+litera ginie przy `-display sdl` z aktywnym IBus (zmierzone: `docs/notatnik-2026-11-17/sdl-ibus-probe.txt`). |
| Brak `-boot order=d` przy ISO (Makefile i Windows go mają) | W BIOS zainstalowany system na dysku może przejąć start zamiast ISO. |
| `file=$DISK` bez podwojenia przecinków | Ścieżka z przecinkiem psuła parsowanie `-drive` (sprawdzone: QEMU kończy się błędem). |
| Tryb 13 usuwał `wirtualny_dysk.img` bez potwierdzenia | Ryzyko utraty zainstalowanego systemu i danych. |
| Dysk będący dowiązaniem do urządzenia albo zerwanym dowiązaniem nie był odrzucany | `qemu-img create` mógł pisać przez dowiązanie. |
| `rng-random,filename=/dev/urandom` zamiast `rng-builtin` z Makefile | Niespójność konfiguracji, bez zmiany sprzętu gościa. |
| Brak trybu bez menu i dry-run, `qemu-img` wymagany zawsze | Brak walidacji bez uruchamiania. |
| Komentarze odwoływały się do `./rununix.sh` | Nieaktualna dokumentacja. |

### runwindows.ps1

| Problem | Skutek |
|---|---|
| `& $qemu ... 2>&1` przy `$ErrorActionPreference = "Stop"` | W Windows PowerShell 5.1 linia stderr QEMU staje się błędem kończącym skrypt. Dotyczyło trybów z logiem, sond `-display help`/`-device help` i `-Audit`. |
| `Tee-Object` | W 5.1 log zapisywany w UTF-16 po dekodowaniu wyjścia stroną kodową OEM. Polskie znaki z portu szeregowego były zniekształcone. |
| `Set-Location` bez `[Environment]::CurrentDirectory` | Katalog procesu QEMU i `GetFullPath` nie odpowiadały katalogowi skryptu. Względne `-Iso` było rozwiązywane względem katalogu procesu. |
| `file=$Disk` bez podwojenia przecinków | Jak wyżej. |
| Pełne ścieżki z polskimi znakami w argv QEMU | Ryzyko przy budowach QEMU czytających argv w stronie kodowej ANSI. |
| Brak trybu bez menu, kod wyjścia QEMU niewidoczny | `-PrintArgv` tylko dla trybu 2. |

## Zmiany

### rununix_virtio_latest.sh

* Profil VirtIO jak `QEMU_VIRTIO_COMMON_ARGS`: `rng-builtin`, `virtio-tablet-pci` na końcu, mysz względna zostaje jako fallback. Walidacja urządzeń obejmuje tablet.
* `-boot order=d,menu=on` przed `-cdrom` w trybach z ISO. Tryby „zainstalowany” bez zmian (`-boot order=c`, `bootindex=1`).
* Na Linuksie `IBUS_ADDRESS=unix:path=/nonexistent/...` trafia wyłącznie do środowiska procesu QEMU (`IBUS_ADDRESS=... "$QEMU" "$@"`), bez `export`. Wyłączenie: `QEMU_SDL_BEZ_IME=0`.
  * Dla SDL problem jest zmierzony.
  * Dla domyślnego GTK zachowanie z IBus nie zostało zmierzone (`docs/notatnik-2026-11-17/qemu-ui-ibus-probe.txt`), dlatego obejście działa dla każdego frontendu, tak jak w Makefile.
* `qemu_opt_path`: podwojone przecinki w `file=` (`-drive`, pflash).
* Bezpieczeństwo dysku:
  * odmowa, gdy dysk jest urządzeniem, katalogiem albo zerwanym dowiązaniem;
  * pusty dysk powstaje tylko, gdy plik nie istnieje;
  * tryb 13 wymaga wpisania `USUN`.
* `--mode N` (bez menu, kod wyjścia = kod QEMU), `--dry-run`/`--print-argv`, `--help`. `qemu-img` jest wymagany tylko przy tworzeniu dysku.
* Ostrzeżenie, gdy `system_operacyjny.bin` jest nowszy niż ISO. Brak ISO to błąd, bez cichego fallbacku.

### runwindows.ps1

* `Invoke-NativeCapture`: sondy QEMU i `qemu-img` z lokalnym `$ErrorActionPreference = "Continue"`. Uruchomienie QEMU także w `Continue`.
* Log COM1 przez `StreamWriter` UTF-8, `[Console]::OutputEncoding` na czas uruchomienia ustawione na UTF-8 i potem przywracane.
* `[Environment]::CurrentDirectory = $ScriptDir`. Względne `-Iso` liczone od katalogu wywołania.
* `ConvertTo-QemuPath`: pliki z katalogu launchera przekazywane względnie (`.\wirtualny_dysk.img`, `.\BursztynOS.iso`, `.\.qemu_firmware\...`). `ConvertTo-QemuOptionPath` podwaja przecinki.
* `-Mode N` (0–18, walidowane): bez menu, `exit` z kodem QEMU. `-PrintArgv -Mode N` raportuje dowolny tryb, a bez `-Mode` dalej tryb 2.
* `Prepare-Disk`: odmowa, gdy ścieżka dysku jest katalogiem. Tworzenie dalej przez `CreateNew` lub `qemu-img`, bez nadpisywania.
* Ostrzeżenie o nieaktualnym ISO. W nagłówku opis AltGr w Windows.
* Bez zmian: profile urządzeń (Windows zachowuje względną mysz jako kanoniczną, tablet tylko przez `-PointerDevice`), GTK jako domyślny frontend, UEFI przez scalony `-bios`, `-audiodev none`.

## Porównanie konfiguracji

| Element | Makefile `runuefi` | Linux launcher | Windows launcher |
|---|---|---|---|
| Maszyna / CPU / akceleracja | `pc,hpet=on` / `max` / `tcg,thread=single`, 1 vCPU, 2G | to samo | to samo |
| Frontend | SDL | GTK `zoom-to-fit=on` (macOS: Cocoa) | GTK `zoom-to-fit=on` (opcja SDL) |
| Audio | HDA + alsa | HDA + alsa (Linux) / coreaudio | HDA + none |
| Dysk / sieć / RNG / GPU | virtio-blk / virtio-net / rng-builtin / virtio-vga 1280×720 | to samo | to samo |
| Wejście | keyboard + mouse + tablet (ostatni) | to samo | keyboard + mouse (tablet opcjonalnie) |
| UEFI | `-bios OVMF.fd` lub pflash | to samo | scalony VARS+CODE przez `-bios` |
| ISO | `-boot order=d,menu=on -cdrom` | to samo | to samo |
| Legacy | AHCI + e1000 + std VGA, PS/2 | AHCI + e1000 + std VGA + xHCI USB kbd/mysz (bez zmian) | to samo co Linux |

Profil legacy launcherów ma USB HID obok PS/2 z `i8042` maszyny `pc`. QEMU kieruje zdarzenia do jednego aktywnego urządzenia klawiatury i jednego wskaźnika, więc zdarzenia nie są dublowane. Profil zachowano bez zmian.

## Walidacja

* `make -j1 test-unix-launcher` (nowy): **113 PASS**. Obejmuje:
  * `bash -n`, shebang, prawo wykonania;
  * dry-run 15 trybów;
  * tryb 2 identyczny z `make runuefi-argv` (opcje, wartości, kolejność, topologia PCI);
  * ścieżkę ze spacją, polskimi znakami i przecinkiem;
  * dowiązanie do urządzenia, zerwane dowiązanie, tryb 13 bez potwierdzenia, brak ISO, złe opcje;
  * IBus tylko w prefiksie procesu QEMU.
* `make -j1 test-launcher-parity`: 9 PASS. Na tym hoście nie ma `pwsh`, więc argv Windows pochodzi ze statycznej kopii w teście, a nie z wykonania skryptu.
* `make -j1 test-windows-launcher`: 14 testów OK (10 dotychczasowych zaktualizowanych do nowych fragmentów i 4 nowe).
* QEMU z `-machine none` (bez gościa): `file=` z podwojonym przecinkiem działa, bez podwojenia QEMU kończy się błędem, a `-cdrom` przyjmuje surową ścieżkę z przecinkiem.

## Test natywny (jeden, Linux)

`natywny-start-unix/`: `./rununix_virtio_latest.sh --mode 2` uruchomiony z innego katalogu (`/tmp`) z domyślnym GTK, przez `QEMU_EXTRA_ARGS` dodano `-snapshot` (brak zapisu na `wirtualny_dysk.img`) i QMP na `127.0.0.1`. Bez wysyłania klawiszy.

Wynik:
* pulpit po 46,7 s;
* QEMU widzi PS/2 Mouse, Virtio Mouse i Virtio Tablet, a gość rejestruje Virtio Keyboard, Mouse, Tablet i VirtIO-RNG;
* 0 błędów jądra;
* zamknięcie przez QMP `quit`, kod launchera 0;
* jeden przestój hosta 1,0 s.

Pierwsza próba kończyła się `UNIX socket path ... too long` z winy uprzęży testowej. QEMU nie wystartowało, a launcher poprawnie zwrócił kod 1.

## Niezweryfikowane

* Cały launcher Windows na Windows 11: składnia PowerShell (brak `pwsh` na hoście), wykonanie, GTK/SDL, AltGr z rzeczywistej klawiatury Windows, ścieżki Unicode i obsługa przez QEMU dla Windows.
* AltGr przy GTK z IBus na Linuksie: niezmierzone, obejście stosowane zapobiegawczo.
* Klawiatura w natywnym starcie launchera: celowo nie wysyłano klawiszy.
* macOS, BIOS i tryby legacy natywnie: sprawdzono tylko dry-run.
