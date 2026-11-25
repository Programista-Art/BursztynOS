# Dźwięk w launcherach QEMU (`runwindows.ps1`, `rununix_virtio_latest.sh`)

Gość Bursztyn OS ma **zawsze** to samo urządzenie: Intel HDA
(`-device intel-hda -device hda-output,audiodev=snd`, PCI 00:02.0, przed urządzeniami VirtIO).
Launcher wybiera wyłącznie **wyjście dźwięku hosta** – jeden `-audiodev <backend>,id=snd`. Topologia PCI,
dyski, sieć, ekran i wejście nie zależą od wyboru dźwięku.

## Tryby

| Tryb | Windows (`-AudioMode`) | Linux (`--audio`) | Efekt |
|---|---|---|---|
| DirectSound | `dsound` – **domyślny, zalecany w Windows 11** | – (niedostępny) | słyszalne na głośnikach Windows |
| PipeWire | – | `pipewire` | słyszalne |
| PulseAudio | – | `pa` (także `pulseaudio`; działa z pipewire-pulse) | słyszalne |
| ALSA | – | `alsa` – **domyślny** (jak `Makefile`/`make runuefi`) | słyszalne |
| SDL | `sdl` | `sdl` | słyszalne; SDL sam wybiera sterownik (`SDL_AUDIODRIVER`) |
| Automatycznie | – | `auto` | PipeWire → PulseAudio → ALSA, pierwszy działający |
| Nagrywanie dźwięku do WAV | `wav` | `wav` | zapis wyjścia HDA gościa do pliku, **bez** odtwarzania |
| Brak dźwięku | `none` | `none` (także `off`) | nic nie trafia do hosta; HDA nadal jest w gościu |

Interaktywnie: w menu głównym opcja **A – Wyjście dźwięku** (obie platformy) pokazuje dostępność każdego trybu.

### Przykłady
Windows 11 (PowerShell):
```powershell
.\runwindows.ps1                                   # menu; dźwięk: DirectSound
.\runwindows.ps1 -Mode 2                           # UEFI + VirtIO, DirectSound
.\runwindows.ps1 -Mode 2 -AudioMode sdl            # gdy DirectSound nie działa
.\runwindows.ps1 -Mode 2 -AudioMode wav            # nagranie do logi_qemu\nagrania_audio\bursztyn-hda-DATA.wav
.\runwindows.ps1 -Mode 2 -AudioMode wav -AudioWavPath "nagrania\próba 1.wav"
.\runwindows.ps1 -Mode 2 -AudioMode none
.\runwindows.ps1 -PrintArgv -Mode 2 -AudioMode sdl # tylko raport argv (pola audio_mode, audiodev)
```
Linux:
```bash
./rununix_virtio_latest.sh                         # menu; dźwięk: ALSA (A = zmiana)
./rununix_virtio_latest.sh --mode 2 --audio auto
./rununix_virtio_latest.sh --mode 2 --audio pipewire
./rununix_virtio_latest.sh --mode 2 --audio wav --audio-wav "nagrania/próba 1.wav"
./rununix_virtio_latest.sh --dry-run --mode 2 --audio pa   # argv + podsumowanie dźwięku na stderr
./rununix_virtio_latest.sh --audio-diag            # diagnostyka dźwięku hosta (tylko odczyt)
AUDIO_BACKEND=pipewire ./rununix_virtio_latest.sh --mode 2  # dotychczasowa zmienna nadal działa
```
Kody wyjścia (Linux i Windows z `-Mode`): kod QEMU; `2` – zła opcja; `3` (Linux) / `1` (Windows) – błąd
konfiguracji dźwięku, QEMU **nie** zostało uruchomione.

## Kontroler dźwięku gościa (HDA albo AC97)

Opcje powyżej wybierają **wyjście hosta**. Niezależnie od nich gość ma jeden kontroler dźwięku w slocie
PCI 00:02.0: domyślnie zawsze **Intel HDA** (wszystkie profile produkcyjne, bez zmian). **Intel ICH AC97**
(`-device AC97,audiodev=snd`, sterownik `docs/ac97.md`) jest wyłącznie jawnym wyborem do rozwoju i testów:

| | Windows | Linux/macOS | Makefile |
|---|---|---|---|
| Intel HDA (domyślnie) | `-GuestAudio hda` | `--guest-audio hda` | `QEMU_GUEST_AUDIO=hda` |
| Intel ICH AC97 (deweloperski) | `-GuestAudio ac97` | `--guest-audio ac97` lub `GUEST_AUDIO=ac97` | `QEMU_GUEST_AUDIO=ac97` |

AC97 zastępuje HDA (nigdy oba naraz). Jeżeli ta instalacja QEMU nie ma urządzenia `AC97`, launcher kończy
się błędem bez uruchamiania maszyny (bez cichego powrotu do HDA). AC97 w VirtualBox: kontroler audio
„ICH AC97” w ustawieniach maszyny – konfiguracja VirtualBox nie jest zmieniana przez repozytorium.

## Wykrywanie backendów
* Lista pochodzi z **wykrytego** pliku QEMU: `qemu-system-x86_64 -audiodev help` (Windows: ścieżka z `Find-Qemu`
  / `-QemuPath`, nie zakładane `C:\Program Files\qemu`). Parser bierze linie zawierające samą nazwę (`[a-z0-9_]`),
  pomija nagłówki i `\r` – bez numerów linii.
* Obecność backendu w QEMU **nie dowodzi**, że działa serwer dźwięku. Linux sprawdza dodatkowo (tylko odczyt,
  timeout 3 s):
  * PipeWire – gniazdo `$XDG_RUNTIME_DIR/pipewire-0` i odpowiedź `pw-cli info 0`,
  * PulseAudio – gniazdo `$XDG_RUNTIME_DIR/pulse/native` (albo `PULSE_SERVER`) i `pactl info`; domyślne wyjście
    `auto_null` = **brak urządzenia wyjściowego**,
  * ALSA – karta w `/proc/asound/cards` (albo wtyczka działającego serwera),
  * SDL – którykolwiek z powyższych.
* Brak listy (stara wersja QEMU, błąd uruchomienia): jawny wybór jest użyty z ostrzeżeniem „niesprawdzony”,
  `auto` kończy się błędem.
* **Jawny wybór nigdy nie jest po cichu zamieniany** na inny backend ani na `none`. Niedostępny backend: komunikat
  z przyczyną i powrót do menu (interaktywnie) albo kod 3/1 (bez menu). `--dry-run` tylko ostrzega.
* Windows: DirectSound nie ma serwera do sprawdzenia; launcher ostrzega, gdy Windows nie zgłasza żadnego
  urządzenia dźwiękowego (`Win32_SoundDevice`).

## Nagrywanie WAV
* Natywny backend QEMU `wav`: `-audiodev wav,id=snd,path=<plik>` – **jeden** argument; przecinek w ścieżce jest
  podwajany (składnia opcji QEMU), spacje i polskie znaki przechodzą bez cytowania powłoki (tablica argumentów).
* Domyślnie `logi_qemu/nagrania_audio/bursztyn-hda-RRRR-MM-DD_GG-MM-SS.wav` (katalog ignorowany przez git);
  nazwy są unikalne (`-2`, `-3`, …). Ścieżka względna – względem katalogu wywołania; katalog – nazwa domyślna w nim.
* Istniejący plik: nadpisanie tylko z `--audio-overwrite` / `-AudioOverwrite` albo po potwierdzeniu w menu;
  bez tego odmowa. Dowiązania są odrzucane. Katalog jest tworzony dopiero przy prawdziwym uruchomieniu.
* Przed startem launcher wypisuje „Ścieżka nagrania”, po zakończeniu – lokalizację i rozmiar pliku.
* To nagranie **wyjścia HDA gościa** (wszystko, co gra Bursztyn OS), nie biblioteka muzyki `/dane/muzyka`.
* Windows: ścieżka w katalogu launchera jest przekazywana względnie (`.\…`); ścieżka spoza niego ze znakami spoza
  ASCII dostaje ostrzeżenie (QEMU w Windows może jej nie otworzyć).

## Błędy dźwięku
Przed startem launcher wypisuje: wyjście dźwięku i `-audiodev`, wersję QEMU, urządzenie Intel HDA, stan hosta,
ścieżkę nagrania. Wyjście QEMU nie jest ukrywane (Linux: stderr kopiowany do pliku tymczasowego przez `tee`;
Windows: stdout+stderr przez potok). Po zakończeniu komunikaty **w formacie QEMU** (prefiks `audio:`, `alsa:`,
`pulseaudio:`, `pipewire:`, `sdl:`, `dsound:`, `wav:`, `-audiodev`) są klasyfikowane – linie portu szeregowego
gościa nie:

| Rodzaj | Przykład komunikatu QEMU |
|---|---|
| backend nieobsługiwany | `-audiodev foo,id=snd: Parameter 'driver' does not accept value` |
| serwer dźwięku niedostępny | `pulseaudio: pa_context_connect() failed`, `Connection refused` |
| brak urządzenia wyjściowego | `alsa: Could not initialize DAC`, `No such device`, `DSERR_NODRIVER` |
| błąd zapisu WAV | `wav: Failed to open wave file` |
| inicjalizacja nieudana | `audio: Could not init 'sdl' audio driver`, `SDL_OpenAudio failed` |
| inicjalizacja DirectSoundCapture (Windows) | `dsound: Could not initialize DirectSoundCapture` |
| inny błąd QEMU | kod ≠ 0 bez komunikatów o dźwięku – „przyczyna niezwiązana z wyborem dźwięku” |

**DirectSoundCapture** (2026-11-23, potwierdzone natywnie na Windows 11): `-AudioMode dsound` kończy się błędem
inicjalizacji DirectSoundCapture. Launcher klasyfikuje ten komunikat jako błąd inicjalizacji dźwięku hosta (przy kodzie
QEMU ≠ 0 – błąd, przy 0 – ostrzeżenie), nie jako błąd niezwiązany z dźwiękiem, i wskazuje działające wyjścia:
`-AudioMode sdl` (odsłuch – potwierdzony na Windows 11) i `-AudioMode wav` (nagranie – potwierdzone). To osobny
problem backendu hosta; sterownik HDA Bursztyn OS ani Grajek nie są pod niego zmieniane.

Pozostałe ostrzeżenia o **nagrywaniu** (capture, np. brak mikrofonu) nie są błędem odtwarzania. Brak
komunikatów nie jest raportowany jako „dźwięk działa” – tylko „brak komunikatów o błędach (to nie potwierdza
słyszalnego odtwarzania)”.

## Tor dźwięku i bufory
Grajek → BWS 110 → mikser PCM jądra (48 000 Hz, stereo, PCM16) → DMA Intel HDA → emulowane `hda-output` →
silnik audio QEMU (`-audiodev`, domyślnie `out.frequency=44100`, więc QEMU przelicza 48 kHz → 44,1 kHz; nagrania
WAV z testów natywnych mają 44,1 kHz) → backend (DirectSound / PipeWire / PulseAudio / ALSA / SDL) → serwer
dźwięku hosta (często ponowne przeliczenie do częstotliwości urządzenia).
* Launchery **nie ustawiają** `out.frequency`, `timer-period`, `out.buffer-length` ani opcji backendów. Możliwe
  dostrojenie (np. `out.frequency=48000`, aby uniknąć jednego przeliczenia) wymaga pomiaru natywnego na danej
  wersji QEMU i nie zostało wprowadzone.
* Przerwy w dźwięku gościa (pominięte okresy HDA przy intensywnym GUI pod TCG) to osobny temat – zob.
  `docs/grajek.md`; zmiana backendu hosta ich nie usuwa.

## Rozwiązywanie problemów
**Brak dźwięku**
1. Sprawdź podsumowanie przed startem: wyjście dźwięku, stan hosta.
2. Windows: spróbuj `-AudioMode sdl`; sprawdź domyślne urządzenie odtwarzania w Ustawieniach dźwięku Windows.
3. Linux: `./rununix_virtio_latest.sh --audio-diag` – który serwer odpowiada, domyślne wyjście (`auto_null` = brak),
   karty ALSA; potem `--audio auto` albo jawny backend z listy „dostępny”.
4. Sprawdź, czy gość w ogóle wysyła dźwięk: `--audio wav` / `-AudioMode wav` i odsłuchaj plik. Pusty/cichy plik
   = problem po stronie gościa, nie hosta.
5. W gościu: Grajek / Test dźwięku, głośność w Bursztynowym Zasobniku.

**Trzaski i przerwy**
1. Nagraj `wav` – jeśli przerwy są w pliku, powstają w gościu/emulacji (np. pominięte okresy HDA), a nie
   w backendzie hosta.
2. Jeśli plik WAV jest czysty, a odsłuch trzeszczy – problem w backendzie/serwerze hosta: porównaj PipeWire,
   PulseAudio, ALSA, SDL (Windows: DirectSound vs SDL).
3. Obciążenie hosta (TCG, inne maszyny wirtualne) zwiększa ryzyko przerw; uruchamiaj jedną instancję QEMU.

## Linux Mint w VirtualBox: krótkie dźwięki przy bezczynności
Zgłoszenie: krótkie, dziwne dźwięki, gdy Linux jest bezczynny. **Przyczyna nie jest zdiagnozowana.** Procedura
(bez zmian w ustawieniach VirtualBox i dźwięku Linuksa, bez restartów usług):
1. **QEMU całkowicie zatrzymane** (`pgrep -a qemu-system` – pusto). Słuchaj kilka minut. Dźwięki występują →
   źródło jest poza Bursztyn OS i QEMU (VirtualBox: emulowana karta HDA/AC97 i jej sterownik hosta Windows,
   PipeWire w Mint, zarządzanie energią karty `snd_hda_intel power_save`, powiadomienia pulpitu). **Zmiana
   launchera Bursztyn OS nie może tego usunąć.**
2. W tym stanie: `./rununix_virtio_latest.sh --audio-diag` (tylko odczyt) – sekcja „aktywne strumienie
   (sink-inputs)” pokazuje, czy coś odtwarza dźwięk w chwili szumu; `systemd-detect-virt` = `oracle` potwierdza
   VirtualBox.
3. **QEMU z `--audio none`**: dźwięki takie jak w kroku 1 → nadal niezwiązane z QEMU.
4. **QEMU z `--audio wav`** (gość działa, host nic nie odtwarza): dźwięki → nie pochodzą z wyjścia QEMU.
   Odsłuch pliku WAV pokazuje, co faktycznie wysyłał gość.
5. **QEMU z `--audio pipewire`, potem `pa`, `alsa`, `sdl`**: dźwięki tylko z jednym backendem → problem tego toru
   hosta; dźwięki ze wszystkimi, ale nie przy `none`/`wav` → wyjście dźwięku gościa (Bursztyn OS) albo wspólny
   serwer hosta; porównaj z nagraniem WAV z tej samej sesji.
6. Zapisz dla każdego kroku: czas, tryb, czy dźwięk wystąpił, wynik `--audio-diag`.
Ustawień VirtualBox (kontroler audio, sterownik hosta) i PipeWire nie należy zmieniać bez osobnej decyzji.

## Testy
`make -j1 test-launcher-audio` (`testy/test_launcher_audio.py`): Linux wykonywany na kopii skryptu z atrapą QEMU,
`pactl`, `pw-cli`, gniazdami Unix i plikiem kart ALSA (wykrywanie, `auto`, jawne backendy, niedostępne backendy,
WAV ze spacjami/UTF-8/przecinkiem, brak nadpisywania, dowiązania, klasyfikacja błędów, linia gościa ≠ błąd,
brak QEMU, menu A, diagnostyka, wszystkie profile uruchomieniowe, jedna para `-audiodev`/HDA, topologia PCI);
Windows – statycznie, a przy dostępnym `pwsh` także parser składni i `-PrintArgv` dla `dsound`/`sdl`/`none`.
Ponadto `test-unix-launcher`, `test-windows-launcher`, `test-launcher-parity`.
