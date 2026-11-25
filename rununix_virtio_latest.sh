#!/usr/bin/env bash
# Bursztyn OS - QEMU launcher dla Linux + macOS
# BIOS / UEFI + VirtIO Full + tryb zgodnosci Legacy
#
# Domyslny profil "VirtIO Full" odpowiada najnowszej architekturze Bursztyn OS:
#   - VirtIO 1.x Modern PCI
#   - VirtIO-BLK      -> BlockDevice
#   - VirtIO-NET      -> NetworkDevice
#   - VirtIO-RNG
#   - VirtIO-GPU 2D   -> Graphics HAL
#   - VirtIO-INPUT    -> klawiatura + mysz wzgledna + tablet bezwzgledny
#                        (jak QEMU_VIRTIO_COMMON_ARGS w Makefile; tablet ostatni, BDF 00:09.0)
#   - MSI/MSI-X obslugiwane przez Bursztyn OS
#   - Intel HDA pozostaje aktualnym backendem audio
#
# Wymagane pliki w katalogu launchera:
#   BursztynOS.iso            (wynik `make -j1 iso`; brak = blad, bez cichego fallbacku)
#   wirtualny_dysk.img        (TRWALY dysk uzytkownika; tworzony pusty tylko, gdy nie istnieje,
#                              nigdy nie jest nadpisywany ani formatowany przez launcher)
#
# Opcjonalnie:
#   .bursztyn_ci_test.iso
#
# Skrypt NIE kompiluje systemu.
# Uruchamia istniejace ISO lub zainstalowany system z wirtualny_dysk.img.
#
# Uzycie:
#   ./rununix_virtio_latest.sh                 menu interaktywne (jak dotad)
#   ./rununix_virtio_latest.sh --mode 2        bez menu: tryb 2 (UEFI + VirtIO Full); kod wyjscia = kod QEMU
#   ./rununix_virtio_latest.sh --dry-run --mode 2
#                                              tylko wypisuje polecenie QEMU (nie tworzy dysku, nie uruchamia)
#   ./rununix_virtio_latest.sh --help
#   ./rununix_virtio_latest.sh --mode 2 --audio pipewire      wyjscie dzwieku PipeWire
#   ./rununix_virtio_latest.sh --mode 2 --audio auto          wybor automatyczny (PipeWire > PulseAudio > ALSA)
#   ./rununix_virtio_latest.sh --mode 2 --audio wav --audio-wav "nagrania/próba 1.wav"
#   ./rununix_virtio_latest.sh --audio-diag                  diagnostyka dzwieku hosta (tylko odczyt)
#
# Dzwiek: gosc zawsze ma Intel HDA (-device intel-hda -device hda-output,audiodev=snd, PCI 00:02.0).
# Zmienia sie tylko backend hosta (-audiodev ...,id=snd). Domyslnie ALSA (jak Makefile / make runuefi),
# chyba ze podano --audio albo AUDIO_BACKEND. Launcher nie instaluje, nie restartuje i nie konfiguruje
# uslug dzwieku hosta. Opis: docs/qemu-dzwiek-launchery.md.
#
# Klawiatura (AltGr / polskie znaki): przy -display sdl SDL2 oddaje klawisze tekstowe IBus; dla
# AltGr+litera IBus zatwierdza gotowy znak i SDL nie wysyla klawisza litery, wiec QEMU dostaje sam
# AltGr (zmierzone: docs/notatnik-2026-11-17/sdl-ibus-probe.txt). Na Linuksie launcher ustawia
# IBUS_ADDRESS na nieistniejacy adres WYLACZNIE w srodowisku procesu QEMU - jak Makefile, dla kazdego
# frontendu (dla GTK zachowanie z IBus nie zostalo zmierzone; obejscie jest tam nieszkodliwe, bo
# okno goscia QEMU nie korzysta z IME). Pulpit hosta nadal uzywa IBus. Wylaczenie: QEMU_SDL_BEZ_IME=0.
# Diagnoza: narzedzia/sdl_ime_probe.py, narzedzia/qemu_ui_ime_probe.py.

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Wzgledna sciezka --audio-wav odnosi sie do katalogu, z ktorego uruchomiono skrypt.
INVOCATION_DIR="$PWD"
cd "$SCRIPT_DIR"

OS_NAME="$(uname -s)"
HOST_ARCH="$(uname -m)"

case "$OS_NAME" in
    Linux)
        HOST_NAME="Linux"
        DEFAULT_AUDIO="alsa"
        ;;
    Darwin)
        HOST_NAME="macOS"
        DEFAULT_AUDIO="coreaudio"
        ;;
    *)
        echo "BLAD: Nieobslugiwany host: $OS_NAME" >&2
        echo "Ten launcher obsluguje Linux i macOS." >&2
        exit 1
        ;;
esac

QEMU="${QEMU:-qemu-system-x86_64}"
QEMU_IMG="${QEMU_IMG:-qemu-img}"

ISO_NORMAL="$SCRIPT_DIR/BursztynOS.iso"
ISO_CI_TEST="$SCRIPT_DIR/.bursztyn_ci_test.iso"
DISK="$SCRIPT_DIR/wirtualny_dysk.img"
DISK_SIZE="${DISK_SIZE:-4G}"

LOG_DIR="$SCRIPT_DIR/logi_qemu"
OVMF_VARS_RUNTIME="$SCRIPT_DIR/.bursztyn_ovmf_vars_unix.fd"

# Ustawienia opcjonalne:
#
#   AUDIO_BACKEND=auto|alsa|pipewire|pa|sdl|wav|none|off|coreaudio   (--audio ma pierwszenstwo)
#   AUDIO_WAV_PATH=plik.wav                                       (dla trybu wav; --audio-wav ma pierwszenstwo)
#   QEMU_DISPLAY_ARGS="-display gtk,zoom-to-fit=off"
#   QEMU_EXTRA_ARGS="-d guest_errors"
#
# Linux defaults to GTK with viewport scaling, matching Windows.
# macOS retains Cocoa; QEMU_DISPLAY_ARGS can select another frontend.
AUDIO_BACKEND="${AUDIO_BACKEND:-$DEFAULT_AUDIO}"
# Match Windows: GTK fits the guest image to the host window, without choosing
# guest physical resolution. macOS retains its native Cocoa frontend.
if [[ "$HOST_NAME" == "Linux" ]]; then
    QEMU_DISPLAY_ARGS="${QEMU_DISPLAY_ARGS:--display gtk,zoom-to-fit=on}"
else
    QEMU_DISPLAY_ARGS="${QEMU_DISPLAY_ARGS:-}"
fi
GUEST_WIDTH="${GUEST_WIDTH:-1280}"
GUEST_HEIGHT="${GUEST_HEIGHT:-720}"
if [[ ! "$GUEST_WIDTH" =~ ^[0-9]{3,4}$ || ! "$GUEST_HEIGHT" =~ ^[0-9]{3,4}$ ]] ||
   ((10#$GUEST_WIDTH < 640 || 10#$GUEST_WIDTH > 4096 || 10#$GUEST_HEIGHT < 480 || 10#$GUEST_HEIGHT > 4096)); then
    echo "Invalid GPU hint: use width 640..4096 and height 480..4096" >&2
    exit 1
fi
QEMU_EXTRA_ARGS="${QEMU_EXTRA_ARGS:-}"

# Firmware mozna nadpisac recznie:
#
# Linux:
#   OVMF_MONO=/usr/share/ovmf/OVMF.fd ./rununix_virtio_latest.sh
#
# lub:
#   OVMF_CODE=/.../OVMF_CODE.fd \
#   OVMF_VARS=/.../OVMF_VARS.fd \
#   ./rununix_virtio_latest.sh
#
# macOS/Homebrew:
# detekcja jest automatyczna, ale zmienne powyzej tez maja pierwszenstwo.
OVMF_MONO="${OVMF_MONO:-}"
OVMF_CODE="${OVMF_CODE:-}"
OVMF_VARS="${OVMF_VARS:-}"

# SDL2 + IBus (AltGr): patrz naglowek. 1 = QEMU bez IBus (domyslnie, tylko proces QEMU), 0 = bez zmian.
QEMU_SDL_BEZ_IME="${QEMU_SDL_BEZ_IME:-1}"
IBUS_ADDRESS_BEZ_IME="unix:path=/nonexistent/bursztyn-qemu-bez-ibus"

DRY_RUN=false
MODE=""
AUDIO_CLI=""
AUDIO_WAV_CLI=""
AUDIO_OVERWRITE=false
AUDIO_DIAG=false
AUDIO_WAV_PATH="${AUDIO_WAV_PATH:-}"
# Kod wyjscia dla bledow konfiguracji dzwieku (backend nieobslugiwany/niedostepny, zly plik WAV).
AUDIO_EXIT_CODE=3

usage() {
    cat <<'EOF'
Bursztyn OS - launcher QEMU (Linux/macOS)

  --mode N, -m N      uruchom tryb N z menu (0-18) bez menu interaktywnego
  --dry-run, -n       wypisz polecenie QEMU bez uruchamiania (bez tworzenia dysku i NVRAM)
  --print-argv        to samo co --dry-run
  --audio TRYB        wyjscie dzwieku hosta (gosc zawsze ma Intel HDA):
                        auto      - automatycznie: PipeWire > PulseAudio > ALSA (dzialajacy serwer)
                        pipewire  - PipeWire (slyszalne)
                        pa        - PulseAudio, takze pipewire-pulse (slyszalne)
                        alsa      - ALSA (slyszalne; domyslnie, jak Makefile)
                        sdl       - SDL (slyszalne; SDL sam wybiera sterownik)
                        wav       - nagrywanie dzwieku goscia do pliku WAV (bez odtwarzania)
                        none      - brak dzwieku (HDA zostaje, nic nie trafia do hosta)
  --audio-wav PLIK    plik nagrania dla --audio wav (domyslnie logi_qemu/nagrania_audio/...)
  --audio-overwrite   pozwol nadpisac istniejacy plik nagrania
  --audio-diag        diagnostyka dzwieku hosta (tylko odczyt: QEMU, PipeWire/PulseAudio, ALSA, VM)
  --guest-audio KONTROLER
                      kontroler dzwieku GOSCIA (nie mylic z --audio, ktore wybiera wyjscie hosta):
                        hda   - Intel HDA (domyslnie, wszystkie profile produkcyjne)
                        ac97  - Intel ICH AC97 (TYLKO do rozwoju/testow sterownika AC97; zamiast HDA)
  --help, -h          ta pomoc

Kody wyjscia: kod QEMU; 2 = zla opcja; 3 = blad konfiguracji dzwieku (QEMU nie uruchomiono).

Zmienne: QEMU, QEMU_IMG, QEMU_DISPLAY_ARGS, QEMU_EXTRA_ARGS, AUDIO_BACKEND, AUDIO_WAV_PATH, GUEST_AUDIO, GUEST_WIDTH,
GUEST_HEIGHT, DISK_SIZE, OVMF_MONO, OVMF_CODE, OVMF_VARS, QEMU_SDL_BEZ_IME (1/0).
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n|--dry-run|--print-argv) DRY_RUN=true ;;
        -m|--mode)
            if [[ $# -lt 2 ]]; then
                echo "BLAD: --mode wymaga numeru trybu." >&2
                exit 2
            fi
            MODE="$2"
            shift
            ;;
        --mode=*) MODE="${1#--mode=}" ;;
        --audio)
            if [[ $# -lt 2 ]]; then
                echo "BLAD: --audio wymaga nazwy trybu (auto, pipewire, pa, alsa, sdl, wav, none)." >&2
                exit 2
            fi
            AUDIO_CLI="$2"
            shift
            ;;
        --audio=*) AUDIO_CLI="${1#--audio=}" ;;
        --audio-wav)
            if [[ $# -lt 2 ]]; then
                echo "BLAD: --audio-wav wymaga sciezki pliku." >&2
                exit 2
            fi
            AUDIO_WAV_CLI="$2"
            shift
            ;;
        --audio-wav=*) AUDIO_WAV_CLI="${1#--audio-wav=}" ;;
        --audio-overwrite) AUDIO_OVERWRITE=true ;;
        --audio-diag) AUDIO_DIAG=true ;;
        --guest-audio)
            if [[ $# -lt 2 ]]; then
                echo "BLAD: --guest-audio wymaga nazwy kontrolera (hda, ac97)." >&2
                exit 2
            fi
            GUEST_AUDIO_CLI="$2"
            shift
            ;;
        --guest-audio=*) GUEST_AUDIO_CLI="${1#--guest-audio=}" ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "BLAD: nieznana opcja: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ -n "$MODE" && ! "$MODE" =~ ^([0-9]|1[0-8])$ ]]; then
    echo "BLAD: nieprawidlowy tryb: $MODE (dozwolone 0-18)." >&2
    exit 2
fi
if [[ "$DRY_RUN" == "true" && -z "$MODE" ]]; then
    MODE="2"
fi

# Zadany tryb dzwieku: --audio > AUDIO_BACKEND > domyslny hosta. Nazwy zastepcze: off -> none,
# pulseaudio -> pa. Sciezka WAV: --audio-wav > AUDIO_WAV_PATH; podanie --audio-wav bez --audio = wav.
normalize_audio_mode() {
    case "$1" in
        off|none|brak) printf 'none' ;;
        pulseaudio|pulse|pa) printf 'pa' ;;
        pipewire|pw) printf 'pipewire' ;;
        *) printf '%s' "$1" ;;
    esac
}
if [[ -n "$AUDIO_WAV_CLI" ]]; then
    AUDIO_WAV_PATH="$AUDIO_WAV_CLI"
    [[ -z "$AUDIO_CLI" ]] && AUDIO_CLI="wav"
fi
AUDIO_REQUEST="$(normalize_audio_mode "${AUDIO_CLI:-$AUDIO_BACKEND}")"
case "$HOST_NAME:$AUDIO_REQUEST" in
    *:auto|*:pipewire|*:pa|*:alsa|*:sdl|*:wav|*:none|macOS:coreaudio) ;;
    *:dsound|*:directsound)
        echo "BLAD: DirectSound jest dostepny tylko w launcherze Windows (runwindows.ps1)." >&2
        exit 2
        ;;
    *)
        echo "BLAD: nieznany tryb dzwieku: $AUDIO_REQUEST (auto, pipewire, pa, alsa, sdl, wav, none)." >&2
        exit 2
        ;;
esac
# Kontroler dzwieku goscia: --guest-audio > GUEST_AUDIO > hda. AC97 jest wylacznie jawnym wyborem deweloperskim.
GUEST_AUDIO="${GUEST_AUDIO_CLI:-${GUEST_AUDIO:-hda}}"
case "$GUEST_AUDIO" in
    hda|ac97) ;;
    *)
        echo "BLAD: nieznany kontroler dzwieku goscia: $GUEST_AUDIO (hda, ac97)." >&2
        exit 2
        ;;
esac
if [[ -n "$AUDIO_CLI" && "$AUDIO_REQUEST" != "wav" && -n "$AUDIO_WAV_CLI" ]]; then
    echo "BLAD: --audio-wav dziala tylko z --audio wav." >&2
    exit 2
fi

show_error() {
    echo
    echo "BLAD: $1" >&2
}

warn() {
    echo "UWAGA: $1" >&2
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        show_error "Brak programu: $1"
        exit 1
    fi
}

require_file() {
    local path="$1"
    local desc="$2"

    if [[ ! -f "$path" ]]; then
        show_error "Nie znaleziono: $desc"
        echo "$path" >&2
        exit 1
    fi
}

first_existing() {
    local p
    for p in "$@"; do
        if [[ -n "$p" && -f "$p" ]]; then
            printf '%s\n' "$p"
            return 0
        fi
    done
    return 1
}

brew_prefix_qemu() {
    if command -v brew >/dev/null 2>&1; then
        brew --prefix qemu 2>/dev/null || true
    fi
}

# Wyjscie QEMU jest najpierw zapisywane w zmiennej: przy `set -o pipefail` potok "qemu | grep -q" bywal falszywie
# nieudany (grep -q konczy po pierwszym trafieniu, piszacy QEMU dostaje SIGPIPE -> "BRAK QEMU device").
qemu_has_device() {
    local device_name="$1" lista
    lista="$("$QEMU" -device help 2>/dev/null)" || return 1
    grep -Eq "(^|[[:space:]\"'])${device_name}([[:space:]\"',]|$)" <<< "$lista"
}

qemu_device_supports_property() {
    local device_name="$1"
    local property_name="$2"

    local wlasciwosci
    wlasciwosci="$("$QEMU" -device "${device_name},help" 2>&1)" || true
    grep -Eq "(^|[[:space:]])${property_name}([=<[:space:]]|$)" <<< "$wlasciwosci"
}

append_disable_legacy_if_supported() {
    local device_name="$1"
    local base_spec="$2"

    if qemu_device_supports_property "$device_name" "disable-legacy"; then
        printf '%s\n' "${base_spec},disable-legacy=on"
    else
        # Nie kazdy typ VirtIO (np. virtio-vga w niektorych wydaniach QEMU)
        # eksponuje ta wlasciwosc. Sam brak property nie oznacza braku
        # nowoczesnych PCI capabilities.
        printf '%s\n' "$base_spec"
    fi
}

detect_ovmf_linux_pair() {
    local pair code vars

    for pair in \
        "/usr/share/OVMF/OVMF_CODE.fd|/usr/share/OVMF/OVMF_VARS.fd" \
        "/usr/share/OVMF/OVMF_CODE_4M.fd|/usr/share/OVMF/OVMF_VARS_4M.fd" \
        "/usr/share/edk2/x64/OVMF_CODE.fd|/usr/share/edk2/x64/OVMF_VARS.fd" \
        "/usr/share/edk2/ovmf/OVMF_CODE.fd|/usr/share/edk2/ovmf/OVMF_VARS.fd"
    do
        code="${pair%%|*}"
        vars="${pair#*|}"

        if [[ -f "$code" && -f "$vars" ]]; then
            OVMF_CODE="$code"
            OVMF_VARS="$vars"
            return 0
        fi
    done

    return 1
}

detect_ovmf() {
    # Jawne ustawienia uzytkownika maja pierwszenstwo.
    if [[ -n "$OVMF_MONO" && -f "$OVMF_MONO" ]]; then
        return 0
    fi

    if [[ -n "$OVMF_CODE" && -f "$OVMF_CODE" ]]; then
        return 0
    fi

    if [[ "$HOST_NAME" == "Linux" ]]; then
        OVMF_MONO="$(first_existing \
            /usr/share/ovmf/OVMF.fd \
            /usr/share/OVMF/OVMF.fd \
            /usr/share/qemu/OVMF.fd \
            2>/dev/null || true)"

        if [[ -n "$OVMF_MONO" ]]; then
            return 0
        fi

        detect_ovmf_linux_pair || true
    else
        local bp=""
        bp="$(brew_prefix_qemu)"

        OVMF_CODE="$(first_existing \
            "${bp:+$bp/share/qemu/edk2-x86_64-code.fd}" \
            /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
            /usr/local/share/qemu/edk2-x86_64-code.fd \
            2>/dev/null || true)"

        OVMF_VARS="$(first_existing \
            "${bp:+$bp/share/qemu/edk2-i386-vars.fd}" \
            "${bp:+$bp/share/qemu/edk2-x86_64-vars.fd}" \
            /opt/homebrew/share/qemu/edk2-i386-vars.fd \
            /opt/homebrew/share/qemu/edk2-x86_64-vars.fd \
            /usr/local/share/qemu/edk2-i386-vars.fd \
            /usr/local/share/qemu/edk2-x86_64-vars.fd \
            2>/dev/null || true)"
    fi
}

# Opcje QEMU (file=...) dziela sie na przecinkach: przecinek w sciezce musi byc podwojony.
qemu_opt_path() {
    printf '%s' "${1//,/,,}"
}

# Dysk musi byc zwyklym plikiem (lub dowiazaniem do zwyklego pliku). Urzadzenie blokowe, katalog
# albo zerwane dowiazanie oznaczaja blad konfiguracji: launcher nic wtedy nie tworzy ani nie zapisuje.
assert_disk_path_safe() {
    if [[ -L "$DISK" && ! -e "$DISK" ]]; then
        show_error "wirtualny_dysk.img jest zerwanym dowiazaniem - odmowa (nic nie utworzono):"
        echo "$DISK" >&2
        exit 1
    fi
    if [[ -e "$DISK" && ! -f "$DISK" ]]; then
        show_error "wirtualny_dysk.img nie jest zwyklym plikiem (urzadzenie/katalog) - odmowa:"
        echo "$DISK" >&2
        exit 1
    fi
}

prepare_disk() {
    assert_disk_path_safe
    if [[ ! -e "$DISK" ]]; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "(dry-run) brak $DISK - zwykly start utworzylby pusty dysk $DISK_SIZE" >&2
            return 0
        fi
        require_command "$QEMU_IMG"
        echo
        echo "Tworzenie pustego dysku wirtualnego: $DISK_SIZE..."
        "$QEMU_IMG" create -f raw "$DISK" "$DISK_SIZE" || {
            show_error "Nie udalo sie utworzyc dysku wirtualnego."
            exit 1
        }
    fi
}

# Ostrzezenie (bez zmiany zachowania), gdy w drzewie budowania jadro jest nowsze niz ISO.
check_iso_fresh() {
    local iso="$1"
    local kernel="$SCRIPT_DIR/system_operacyjny.bin"
    if [[ -f "$kernel" && "$kernel" -nt "$iso" ]]; then
        warn "$(basename "$iso") jest starszy niz system_operacyjny.bin - obraz moze byc nieaktualny (make -j1 iso)."
    fi
}

sdl_ime_fix_active() {
    [[ "$HOST_NAME" == "Linux" && "$QEMU_SDL_BEZ_IME" == "1" ]]
}

# Uruchamia QEMU; obejscie IBus trafia tylko do srodowiska tego procesu potomnego.
exec_qemu() {
    if sdl_ime_fix_active; then
        IBUS_ADDRESS="$IBUS_ADDRESS_BEZ_IME" "$QEMU" "$@"
    else
        "$QEMU" "$@"
    fi
}

print_dry_run() {
    echo "# Bursztyn OS: polecenie QEMU (dry-run, nic nie uruchomiono)"
    if sdl_ime_fix_active; then
        printf 'IBUS_ADDRESS=%q ' "$IBUS_ADDRESS_BEZ_IME"
    fi
    local arg out
    out=""
    for arg in "$QEMU" "$@"; do
        if [[ "$arg" =~ ^[A-Za-z0-9_./,=:@+-]+$ ]]; then
            out+="$arg "
        else
            out+="$(printf '%q' "$arg") "
        fi
    done
    printf '%s\n' "${out% }"
}

# ============================================================ DZWIEK HOSTA (backend -audiodev)
# Gosc zawsze ma ten sam Intel HDA (intel-hda + hda-output,audiodev=snd, PCI 00:02.0, przed VirtIO).
# Wybierany jest tylko backend hosta. Zasady:
#  - lista backendow pochodzi z zainstalowanego QEMU (`-audiodev help`), nie z zalozen o dystrybucji,
#  - obecnosc backendu w QEMU NIE dowodzi, ze dziala serwer dzwieku - PipeWire/PulseAudio sa sprawdzane
#    osobno (gniazdo w XDG_RUNTIME_DIR i, jesli jest klient, odpowiedz serwera); ALSA - karta w /proc/asound,
#  - jawny wybor nigdy nie jest po cichu zamieniany na inny backend ani na "none",
#  - launcher niczego nie instaluje ani nie restartuje (PipeWire/PulseAudio/ALSA tylko odczyt, z timeoutem).
AUDIO_EFFECTIVE=""        # nazwa backendu QEMU po rozstrzygnieciu (np. pipewire)
AUDIO_ARG=""              # pelna wartosc -audiodev (jeden argument)
AUDIO_HOST_INFO=""        # opis wyjscia hosta do podsumowania
AUDIO_WAV_FINAL=""        # bezwzgledna sciezka nagrania (tryb wav)
AUDIO_STATUS_REASON=""    # ostatni wynik audio_backend_status
QEMU_AUDIO_BACKENDS=""    # lista z `-audiodev help` (po jednym w linii) albo pusta
QEMU_AUDIO_PROBED=false

audio_label() {
    case "$1" in
        auto) printf 'Automatycznie' ;;
        pipewire) printf 'PipeWire' ;;
        pa) printf 'PulseAudio' ;;
        alsa) printf 'ALSA' ;;
        sdl) printf 'SDL' ;;
        wav) printf 'Nagrywanie dźwięku do WAV' ;;
        none) printf 'Brak dźwięku' ;;
        coreaudio) printf 'CoreAudio' ;;
        *) printf '%s' "$1" ;;
    esac
}

qemu_runnable() {
    command -v "$QEMU" >/dev/null 2>&1
}

# Parsuje `-audiodev help`: kazda linia z sama nazwa [a-z0-9_]; naglowki (z ':') i puste linie pomijane.
parse_audiodev_help() {
    local line
    while IFS= read -r line; do
        line="${line//$'\r'/}"
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        [[ "$line" =~ ^[a-z0-9_]+$ ]] && printf '%s\n' "$line"
    done
}

probe_qemu_audio_backends() {
    [[ "$QEMU_AUDIO_PROBED" == "true" ]] && return 0
    QEMU_AUDIO_PROBED=true
    QEMU_AUDIO_BACKENDS=""
    qemu_runnable || return 1
    local out
    out="$(timeout 15 "$QEMU" -audiodev help 2>/dev/null)" || return 1
    QEMU_AUDIO_BACKENDS="$(printf '%s\n' "$out" | parse_audiodev_help)"
    [[ -n "$QEMU_AUDIO_BACKENDS" ]]
}

qemu_has_audio_backend() {
    grep -qx -- "$1" <<< "$QEMU_AUDIO_BACKENDS"
}

qemu_version_line() {
    qemu_runnable || { printf '(nie znaleziono %s)' "$QEMU"; return; }
    local v
    v="$(timeout 15 "$QEMU" --version 2>/dev/null | head -n 1)"
    printf '%s' "${v:-(nieznana - QEMU nie podal wersji)}"
}

runtime_dir() {
    printf '%s' "${XDG_RUNTIME_DIR:-}"
}

# Status serwera PipeWire: 0 = dziala, 1 = brak. Opis w AUDIO_STATUS_REASON.
pipewire_server_status() {
    local dir sock
    dir="${PIPEWIRE_RUNTIME_DIR:-$(runtime_dir)}"
    if [[ -z "$dir" ]]; then
        AUDIO_STATUS_REASON="brak XDG_RUNTIME_DIR - brak sesji użytkownika z serwerem dźwięku"
        return 1
    fi
    sock="$dir/${PIPEWIRE_REMOTE:-pipewire-0}"
    if [[ ! -S "$sock" ]]; then
        AUDIO_STATUS_REASON="serwer PipeWire nie działa (brak gniazda $sock)"
        return 1
    fi
    if command -v pw-cli >/dev/null 2>&1; then
        if ! timeout 3 pw-cli info 0 >/dev/null 2>&1; then
            AUDIO_STATUS_REASON="gniazdo $sock istnieje, ale serwer PipeWire nie odpowiada"
            return 1
        fi
        AUDIO_STATUS_REASON="serwer PipeWire odpowiada"
    else
        AUDIO_STATUS_REASON="gniazdo PipeWire istnieje (bez pw-cli - odpowiedź serwera niesprawdzona)"
    fi
    return 0
}

# Status serwera PulseAudio (takze pipewire-pulse). 0 = dziala, 1 = brak, 2 = brak urzadzenia wyjsciowego.
pulse_server_status() {
    local dir sock info sink
    dir="$(runtime_dir)"
    sock="$dir/pulse/native"
    if [[ -z "${PULSE_SERVER:-}" ]]; then
        if [[ -z "$dir" ]]; then
            AUDIO_STATUS_REASON="brak XDG_RUNTIME_DIR - brak sesji użytkownika z serwerem dźwięku"
            return 1
        fi
        if [[ ! -S "$sock" ]]; then
            AUDIO_STATUS_REASON="serwer PulseAudio nie działa (brak gniazda $sock)"
            return 1
        fi
    fi
    if command -v pactl >/dev/null 2>&1; then
        if ! info="$(timeout 3 pactl info 2>/dev/null)"; then
            AUDIO_STATUS_REASON="serwer PulseAudio nie odpowiada (pactl info)"
            return 1
        fi
        sink="$(printf '%s\n' "$info" | sed -n 's/^Default Sink: //p; s/^Domyślny odpływ: //p' | head -n 1)"
        if [[ -z "$sink" || "$sink" == "auto_null" || "$sink" == *".monitor" ]]; then
            AUDIO_STATUS_REASON="serwer odpowiada, ale brak urządzenia wyjściowego (domyślne wyjście: ${sink:-brak})"
            return 2
        fi
        local name
        name="$(printf '%s\n' "$info" | sed -n 's/^Server Name: //p; s/^Nazwa serwera: //p' | head -n 1)"
        AUDIO_STATUS_REASON="${name:-serwer PulseAudio} odpowiada, wyjście: $sink"
    else
        AUDIO_STATUS_REASON="gniazdo PulseAudio istnieje (bez pactl - odpowiedź serwera niesprawdzona)"
    fi
    return 0
}

alsa_cards_status() {
    local cards="${BURSZTYN_ASOUND_CARDS:-/proc/asound/cards}"
    if [[ -r "$cards" ]] && grep -Eq '^[[:space:]]*[0-9]+[[:space:]]+\[' "$cards"; then
        AUDIO_STATUS_REASON="karta ALSA: $(grep -Em1 '^[[:space:]]*[0-9]+[[:space:]]+\[' "$cards" | sed 's/^[[:space:]]*//; s/[[:space:]]\{2,\}/ /g')"
        return 0
    fi
    if pipewire_server_status || pulse_server_status; then
        AUDIO_STATUS_REASON="brak karty w $cards; ALSA tylko przez wtyczkę serwera dźwięku (pipewire/pulse)"
        return 0
    fi
    AUDIO_STATUS_REASON="brak karty dźwiękowej ALSA ($cards) i brak serwera dźwięku"
    return 1
}

# Dostepnosc backendu: 0 = mozna uzyc, 1 = niedostepny (powod w AUDIO_STATUS_REASON).
audio_backend_status() {
    local b="$1"
    if [[ -n "$QEMU_AUDIO_BACKENDS" ]] && ! qemu_has_audio_backend "$b"; then
        AUDIO_STATUS_REASON="ta instalacja QEMU nie ma backendu '$b' (-audiodev help)"
        return 1
    fi
    case "$b" in
        pipewire) pipewire_server_status ;;
        pa) pulse_server_status; [[ $? -eq 0 ]] ;;
        alsa) alsa_cards_status ;;
        sdl)
            if pipewire_server_status || pulse_server_status || alsa_cards_status; then
                AUDIO_STATUS_REASON="SDL wybierze własny sterownik (SDL_AUDIODRIVER); serwer/karta dostępne"
                return 0
            fi
            AUDIO_STATUS_REASON="brak serwera dźwięku i karty ALSA - SDL nie ma dokąd odtwarzać"
            return 1
            ;;
        wav) AUDIO_STATUS_REASON="zapis do pliku, bez odtwarzania" ;;
        none) AUDIO_STATUS_REASON="dźwięk wyłączony" ;;
        coreaudio) AUDIO_STATUS_REASON="CoreAudio (macOS)" ;;
        *) AUDIO_STATUS_REASON="nieznany backend"; return 1 ;;
    esac
}

# Sciezka nagrania WAV: domyslnie logi_qemu/nagrania_audio/bursztyn-hda-DATA.wav (unikalna); sciezka
# wzgledna wzgledem katalogu wywolania; katalog = nazwa domyslna w nim. Istniejacy plik: tylko za
# --audio-overwrite albo po potwierdzeniu w trybie interaktywnym. Dowiazania sa odrzucane.
resolve_wav_path() {
    local p="$AUDIO_WAV_PATH" stamp base candidate n
    stamp="$(date '+%Y-%m-%d_%H-%M-%S')"
    if [[ -z "$p" ]]; then
        p="$LOG_DIR/nagrania_audio"
    elif [[ "$p" != /* ]]; then
        p="$INVOCATION_DIR/$p"
    fi
    if [[ -z "$AUDIO_WAV_PATH" || -d "$p" || "$p" == */ ]]; then
        base="${p%/}/bursztyn-hda-$stamp"
        candidate="$base.wav"
        n=2
        while [[ -e "$candidate" || -L "$candidate" ]]; do
            candidate="$base-$n.wav"
            n=$((n + 1))
        done
        p="$candidate"
    fi
    if [[ -L "$p" ]]; then
        show_error "Ścieżka nagrania jest dowiązaniem - odmowa: $p"
        return 1
    fi
    if [[ -e "$p" && ! -f "$p" ]]; then
        show_error "Ścieżka nagrania istnieje i nie jest zwykłym plikiem: $p"
        return 1
    fi
    if [[ -f "$p" && "$AUDIO_OVERWRITE" != "true" ]]; then
        if [[ -z "$MODE" && -t 0 && "$DRY_RUN" != "true" ]]; then
            local answer=""
            read -r -p "Plik nagrania istnieje: $p - nadpisać? [t/N]: " answer || answer=""
            if [[ "$answer" != "t" && "$answer" != "T" ]]; then
                echo "Anulowano - istniejące nagranie nie zostało nadpisane."
                return 1
            fi
        else
            show_error "Plik nagrania już istnieje (nie nadpisuję bez --audio-overwrite): $p"
            return 1
        fi
    fi
    local dir="${p%/*}"
    if [[ "$DRY_RUN" != "true" && ! -d "$dir" ]]; then
        if ! mkdir -p -- "$dir"; then
            show_error "Nie można utworzyć katalogu nagrań: $dir"
            return 1
        fi
    fi
    AUDIO_WAV_FINAL="$p"
}

# Rozstrzyga AUDIO_REQUEST -> AUDIO_EFFECTIVE / AUDIO_ARG. Zwraca 1 przy bledzie konfiguracji (bez QEMU).
resolve_audio() {
    local req="$AUDIO_REQUEST" b
    AUDIO_EFFECTIVE=""; AUDIO_ARG=""; AUDIO_HOST_INFO=""; AUDIO_WAV_FINAL=""
    local probed=true
    probe_qemu_audio_backends || probed=false
    if [[ "$req" == "auto" ]]; then
        if [[ "$probed" != "true" ]]; then
            show_error "Tryb automatyczny wymaga listy backendów QEMU ($QEMU -audiodev help), a nie udało się jej odczytać."
            echo "Wybierz backend jawnie: --audio pipewire|pa|alsa|sdl|wav|none" >&2
            return 1
        fi
        local candidates="pipewire pa alsa"
        [[ "$HOST_NAME" == "macOS" ]] && candidates="coreaudio"
        local reasons=""
        for b in $candidates; do
            if audio_backend_status "$b"; then
                AUDIO_EFFECTIVE="$b"
                AUDIO_HOST_INFO="automatycznie: $(audio_label "$b") - $AUDIO_STATUS_REASON"
                break
            fi
            reasons+="  - $(audio_label "$b"): $AUDIO_STATUS_REASON"$'\n'
        done
        if [[ -z "$AUDIO_EFFECTIVE" ]]; then
            show_error "Błąd uruchomienia dźwięku: nie znaleziono działającego wyjścia dźwięku hosta."
            printf '%s' "$reasons" >&2
            echo "Nie przełączam po cichu na 'none'. Wybierz jawnie: --audio wav (nagrywanie) albo --audio none." >&2
            return 1
        fi
    else
        AUDIO_EFFECTIVE="$req"
        if [[ "$probed" != "true" ]]; then
            warn "Nie udało się odczytać listy backendów QEMU (-audiodev help) - nie można sprawdzić '$req'."
            AUDIO_HOST_INFO="$(audio_label "$req") (niesprawdzony - brak listy backendów QEMU)"
        elif audio_backend_status "$req"; then
            AUDIO_HOST_INFO="$(audio_label "$req") - $AUDIO_STATUS_REASON"
        else
            if [[ "$DRY_RUN" == "true" ]]; then
                warn "$(audio_label "$req"): $AUDIO_STATUS_REASON (dry-run - tylko ostrzeżenie)"
                AUDIO_HOST_INFO="$(audio_label "$req") - NIEDOSTĘPNY: $AUDIO_STATUS_REASON"
            else
                show_error "Błąd uruchomienia dźwięku: $(audio_label "$req") jest niedostępny - $AUDIO_STATUS_REASON."
                echo "QEMU nie zostało uruchomione. Wybierz inne wyjście dźwięku (menu 'A' albo --audio ...)." >&2
                return 1
            fi
        fi
    fi
    if [[ "$AUDIO_EFFECTIVE" == "wav" ]]; then
        resolve_wav_path || return 1
        AUDIO_ARG="wav,id=snd,path=$(qemu_opt_path "$AUDIO_WAV_FINAL")"
        AUDIO_HOST_INFO="nagrywanie do pliku WAV (bez odtwarzania na głośnikach hosta)"
    else
        AUDIO_ARG="${AUDIO_EFFECTIVE},id=snd"
    fi
    return 0
}

print_audio_summary() {
    echo "Wyjście dźwięku: $(audio_label "$AUDIO_REQUEST")$([[ "$AUDIO_REQUEST" == "auto" ]] && printf ' -> %s' "$(audio_label "$AUDIO_EFFECTIVE")") (-audiodev $AUDIO_ARG)"
    echo "Wersja QEMU:     $(qemu_version_line)"
    if [[ "$GUEST_AUDIO" == "ac97" ]]; then
        echo "Kontroler dźwięku gościa: Intel ICH AC97 (AC97,audiodev=snd, PCI 00:02.0) - tryb deweloperski, bez Intel HDA"
    else
        echo "Wykryte urządzenie Intel HDA: intel-hda + hda-output,audiodev=snd (PCI 00:02.0, przed VirtIO)"
    fi
    echo "Dźwięk hosta:    $AUDIO_HOST_INFO"
    if [[ -n "$AUDIO_WAV_FINAL" ]]; then
        echo "Ścieżka nagrania: $AUDIO_WAV_FINAL"
    fi
}

AUDIO_QEMU_LINE_RE='^(qemu[^ :]*: )?(audio|dsound|sdl|wav|alsa|pulseaudio|pa|pipewire|oss|jack|coreaudio|spice|dbus)[^:]{0,24}:|-audiodev|sdl_openaudio'
# Klasyfikacja komunikatow QEMU o dzwieku (stderr albo log). Wynik: "rodzaj<TAB>linie".
# Rodzaje: unsupported, server, device, file, init, none.
classify_audio_output() {
    local file="$1" lines kind="none"
    [[ -r "$file" ]] || { printf 'none\t'; return; }
    # Tylko komunikaty w formacie QEMU (prefiks backendu albo -audiodev), nie linie portu szeregowego goscia.
    lines="$(grep -aiE "$AUDIO_QEMU_LINE_RE" "$file" | head -n 8)"
    if [[ -z "$lines" ]]; then
        printf 'none\t'
        return
    fi
    if grep -aqiE "unknown audio driver|audiodev.*(invalid|unknown|not supported)|parameter 'driver'|invalid parameter value.*driver" <<<"$lines"; then
        kind="unsupported"
    elif grep -aqiE "pa_context_connect|connection refused|failed to connect|pw_context_connect|host is down|no such file or directory.*(pulse|pipewire)" <<<"$lines"; then
        kind="server"
    elif grep -aqiE "no (available )?audio device|no such device|could not open.*device|could not initialize dac|snd_pcm_open|auto_null|device or resource busy" <<<"$lines"; then
        kind="device"
    elif grep -aqiE "wav: |wave file" <<<"$lines"; then
        kind="file"
    elif grep -aqiE "could not init|failed to (create|initiali[sz]e)|sdl_openaudio|init.*fail" <<<"$lines"; then
        kind="init"
    fi
    printf '%s\t%s' "$kind" "$lines"
}

# Raport po zakonczeniu QEMU: nie twierdzi, ze dzwiek gral, tylko dlatego, ze proces wystartowal.
report_audio_outcome() {
    local code="$1" file="$2" result kind lines
    result="$(classify_audio_output "$file")"
    kind="${result%%$'\t'*}"
    lines="${result#*$'\t'}"
    echo
    case "$kind" in
        unsupported) echo "Błąd uruchomienia dźwięku: backend '$AUDIO_EFFECTIVE' nie jest obsługiwany przez to QEMU." ;;
        server) echo "Błąd uruchomienia dźwięku: serwer dźwięku ($AUDIO_EFFECTIVE) jest niedostępny." ;;
        device) echo "Błąd uruchomienia dźwięku: brak urządzenia wyjściowego hosta ($AUDIO_EFFECTIVE)." ;;
        file) echo "Błąd nagrywania: QEMU nie mógł zapisać pliku WAV." ;;
        init) echo "Błąd uruchomienia dźwięku: inicjalizacja backendu '$AUDIO_EFFECTIVE' nie powiodła się." ;;
    esac
    if [[ "$kind" != "none" ]]; then
        echo "Komunikaty QEMU:"
        printf '%s\n' "$lines" | sed 's/^/  /'
        echo "Wybierz inne wyjście dźwięku (menu 'A' albo --audio ...) - launcher nie przełącza go samodzielnie."
    elif [[ "$code" -ne 0 ]]; then
        echo "QEMU zakończył się kodem $code bez komunikatów o dźwięku - przyczyna niezwiązana z wyborem dźwięku (zob. wyjście powyżej)."
    elif [[ "$AUDIO_EFFECTIVE" != "none" && "$AUDIO_EFFECTIVE" != "wav" ]]; then
        echo "Dźwięk: brak komunikatów o błędach backendu $(audio_label "$AUDIO_EFFECTIVE") (to nie potwierdza słyszalnego odtwarzania)."
    fi
    if [[ -n "$AUDIO_WAV_FINAL" ]]; then
        if [[ -f "$AUDIO_WAV_FINAL" ]]; then
            echo "Nagranie WAV: $AUDIO_WAV_FINAL ($(wc -c < "$AUDIO_WAV_FINAL") B; dźwięk wyjścia HDA gościa, nie biblioteka muzyki Bursztyn OS)"
        else
            echo "Nagranie WAV nie powstało: $AUDIO_WAV_FINAL"
        fi
    fi
    AUDIO_LAST_KIND="$kind"
}
AUDIO_LAST_KIND="none"

# Uruchamia QEMU z kopia stderr do pliku tymczasowego (stderr nadal widoczny w terminalu); serial (stdout) bez
# zmian. Z logiem: stdout+stderr przez tee do pliku logu (jak dotad). Ustawia kod w zmiennej QEMU_RUN_CODE.
run_qemu_captured() {
    local save_log="$1" mode_name="$2"
    shift 2
    local code=0 capture=""
    if [[ "$save_log" == "true" ]]; then
        mkdir -p "$LOG_DIR"
        local stamp safe_name log_file
        stamp="$(date '+%Y-%m-%d_%H-%M-%S')"
        safe_name="$(printf '%s' "$mode_name" | tr -c 'A-Za-z0-9_-' '_')"
        log_file="$LOG_DIR/${stamp}_${safe_name}.log"
        echo "Log COM1:"
        echo "$log_file"
        echo
        exec_qemu "$@" 2>&1 | tee "$log_file"
        code=${PIPESTATUS[0]}
        capture="$log_file"
        report_audio_outcome "$code" "$capture"
    else
        capture="$(mktemp "${TMPDIR:-/tmp}/bursztyn-qemu-stderr.XXXXXX")"
        exec_qemu "$@" 2> >(tee -a "$capture" >&2)
        code=$?
        wait "$!" 2>/dev/null || true
        report_audio_outcome "$code" "$capture"
        rm -f -- "$capture"
    fi
    QEMU_RUN_CODE="$code"
}
QEMU_RUN_CODE=0

# Interaktywny wybor wyjscia dzwieku (menu glowne: A). Niedostepne opcje sa pokazane z powodem.
audio_menu() {
    probe_qemu_audio_backends || warn "Nie udało się odczytać listy backendów QEMU - dostępność niesprawdzona."
    local opts=(auto pipewire pa alsa sdl wav none)
    [[ "$HOST_NAME" == "macOS" ]] && opts=(auto coreaudio sdl wav none)
    local desc
    echo
    echo "================ Wyjście dźwięku (gość: Intel HDA) ================"
    local i=1 b
    for b in "${opts[@]}"; do
        case "$b" in
            auto) desc="wybierz automatycznie działający serwer (PipeWire > PulseAudio > ALSA)" ;;
            pipewire) desc="odtwarzanie przez PipeWire" ;;
            pa) desc="odtwarzanie przez PulseAudio (także pipewire-pulse)" ;;
            alsa) desc="odtwarzanie przez ALSA (domyślne, jak Makefile)" ;;
            sdl) desc="odtwarzanie przez SDL (alternatywa)" ;;
            coreaudio) desc="odtwarzanie przez CoreAudio" ;;
            wav) desc="zapis dźwięku gościa do pliku WAV, bez odtwarzania" ;;
            none) desc="bez dźwięku (diagnostyka); HDA zostaje" ;;
        esac
        local st="dostępny"
        if [[ "$b" != "auto" ]]; then
            if audio_backend_status "$b"; then st="dostępny"; else st="NIEDOSTĘPNY: $AUDIO_STATUS_REASON"; fi
        fi
        printf '  %d - %-26s %s\n' "$i" "$(audio_label "$b")" "$desc"
        printf '      %s\n' "$st"
        i=$((i + 1))
    done
    echo "  0 - powrót (bez zmian; obecnie: $(audio_label "$AUDIO_REQUEST"))"
    local pick=""
    read -r -p "Wybierz wyjście dźwięku: " pick || return 0
    [[ "$pick" == "0" || -z "$pick" ]] && return 0
    if [[ ! "$pick" =~ ^[0-9]+$ ]] || (( pick < 1 || pick > ${#opts[@]} )); then
        show_error "Nieprawidłowy wybór."
        return 1
    fi
    b="${opts[$((pick - 1))]}"
    if [[ "$b" != "auto" ]] && ! audio_backend_status "$b"; then
        show_error "$(audio_label "$b") jest niedostępny: $AUDIO_STATUS_REASON"
        return 1
    fi
    if [[ "$b" == "wav" ]]; then
        local path=""
        read -r -p "Ścieżka nagrania (Enter = logi_qemu/nagrania_audio/...): " path || path=""
        AUDIO_WAV_PATH="$path"
    fi
    AUDIO_REQUEST="$b"
    echo "Wyjście dźwięku: $(audio_label "$b")"
}

# Diagnostyka dzwieku hosta - wylacznie odczyt (bez restartow uslug i zmian konfiguracji).
audio_diag() {
    echo "=== Diagnostyka dźwięku hosta (tylko odczyt) ==="
    echo "Host:            $HOST_NAME / $HOST_ARCH"
    if command -v systemd-detect-virt >/dev/null 2>&1; then
        echo "Wirtualizacja:   $(systemd-detect-virt 2>/dev/null || echo brak) (oracle = VirtualBox)"
    fi
    echo "QEMU:            $(qemu_version_line)"
    if probe_qemu_audio_backends; then
        echo "Backendy QEMU:   $(printf '%s ' $QEMU_AUDIO_BACKENDS)"
    else
        echo "Backendy QEMU:   (nie udało się odczytać -audiodev help)"
    fi
    echo "XDG_RUNTIME_DIR: ${XDG_RUNTIME_DIR:-(brak)}"
    local b
    for b in pipewire pa alsa sdl; do
        if audio_backend_status "$b"; then
            printf '  %-11s dostępny - %s\n' "$(audio_label "$b")" "$AUDIO_STATUS_REASON"
        else
            printf '  %-11s NIEDOSTĘPNY - %s\n' "$(audio_label "$b")" "$AUDIO_STATUS_REASON"
        fi
    done
    if command -v pactl >/dev/null 2>&1; then
        echo "--- pactl info (serwer, format próbek) ---"
        timeout 3 pactl info 2>/dev/null | grep -E '^(Server Name|Server Version|Default Sample Specification|Default Sink|Nazwa serwera|Domyślny)' || echo "(brak odpowiedzi)"
        echo "--- wyjścia (sinks) ---"
        timeout 3 pactl list short sinks 2>/dev/null || echo "(brak odpowiedzi)"
        echo "--- aktywne strumienie odtwarzania (sink-inputs) - kto teraz gra ---"
        timeout 3 pactl list short sink-inputs 2>/dev/null || echo "(brak odpowiedzi)"
    fi
    echo "--- /proc/asound/cards ---"
    cat "${BURSZTYN_ASOUND_CARDS:-/proc/asound/cards}" 2>/dev/null || echo "(brak)"
    echo "--- uruchomione procesy QEMU ---"
    pgrep -a qemu-system 2>/dev/null || echo "(żaden - QEMU nie działa)"
    echo
    echo "Szumy/trzaski przy bezczynnym Linuksie: procedura w docs/qemu-dzwiek-launchery.md (sekcja VirtualBox)."
    echo "Jeśli dźwięki występują, gdy QEMU nie działa, zmiana launchera Bursztyn OS nie może ich usunąć."
}

append_common_machine_args() {
    QEMU_ARGS+=(
        -machine pc,hpet=on
        -cpu max
        -accel tcg,thread=single
        -smp 1
        -m 2G
        -serial stdio
    )
}

append_audio_args() {
    # Jeden -audiodev (id=snd) i jeden kontroler goscia; backend z resolve_audio. Wyciszenie zachowuje kontroler
    # i jego slot PCI. AC97 (--guest-audio ac97) zajmuje ten sam slot zamiast HDA - nigdy oba naraz.
    if [[ "$GUEST_AUDIO" == "ac97" ]]; then
        QEMU_ARGS+=(
            -audiodev "$AUDIO_ARG"
            -device AC97,audiodev=snd
        )
        return
    fi
    QEMU_ARGS+=(
        -audiodev "$AUDIO_ARG"
        -device intel-hda
        -device hda-output,audiodev=snd
    )
}

# AC97 tylko na jawne zadanie i tylko, gdy ta instalacja QEMU je ma (bez cichego powrotu do HDA).
check_guest_audio_device() {
    [[ "$GUEST_AUDIO" == "ac97" ]] || return 0
    if [[ "$DRY_RUN" == "true" ]]; then
        return 0
    fi
    if ! qemu_has_device AC97; then
        show_error "Ta instalacja QEMU nie ma urzadzenia AC97 (--guest-audio ac97)."
        echo "QEMU nie zostalo uruchomione. Uzyj domyslnego --guest-audio hda albo zainstaluj QEMU z AC97." >&2
        return 1
    fi
    return 0
}

append_optional_cli_args() {
    # Celowo rozbijamy kontrolowane zmienne srodowiskowe na argumenty.
    # To sa opcje launchera podawane przez wlasciciela projektu.
    if [[ -n "$QEMU_DISPLAY_ARGS" ]]; then
        # shellcheck disable=SC2206
        local display_parts=( $QEMU_DISPLAY_ARGS )
        QEMU_ARGS+=( "${display_parts[@]}" )
    fi

    if [[ -n "$QEMU_EXTRA_ARGS" ]]; then
        # shellcheck disable=SC2206
        local extra_parts=( $QEMU_EXTRA_ARGS )
        QEMU_ARGS+=( "${extra_parts[@]}" )
    fi
}

build_virtio_args() {
    local iso="${1:-}"
    local installed_boot="${2:-false}"
    local blk_dev="virtio-blk-pci,drive=disk,disable-legacy=on"
    local net_dev="virtio-net-pci,netdev=n1,disable-legacy=on"
    local rng_dev="virtio-rng-pci,rng=rng0,disable-legacy=on"
    local gpu_dev="virtio-vga,disable-legacy=on,xres=$GUEST_WIDTH,yres=$GUEST_HEIGHT"
    local kbd_dev="virtio-keyboard-pci"
    local mouse_dev="virtio-mouse-pci"
    local tablet_dev="virtio-tablet-pci"

    QEMU_ARGS=()
    append_common_machine_args

    # Dokladna kolejnosc jak w Makefile/QEMU_BASE_ARGS:
    # Intel HDA musi zostac dodany PRZED urzadzeniami VirtIO.
    # Kanoniczny uklad PCI na obecnym profilu:
    #   00:02 HDA, 00:03 BLK, 00:04 NET, 00:05 RNG,
    #   00:06 GPU, 00:07 keyboard, 00:08 mouse.
    # Poprzednia wersja launchera dodawala HDA dopiero po VirtIO i przez to
    # przesuwala wszystkie BDF o jeden slot (BLK=00:02, NET=00:03, RNG=00:04).
    append_audio_args

    # UEFI/OVMF: dysk zainstalowanego systemu musi miec jawny priorytet.
    if [[ "$installed_boot" == "true" ]]; then
        blk_dev+=",bootindex=1"
    fi

    # Ten blok celowo odpowiada QEMU_VIRTIO_INSTALLED_ARGS z Makefile,
    # bez dynamicznego zmieniania transportu INPUT zależnie od wersji QEMU.
    QEMU_ARGS+=(
        -drive "id=disk,file=$(qemu_opt_path "$DISK"),format=raw,if=none"
        -device "$blk_dev"

        -netdev user,id=n1
        -device "$net_dev"

        -object rng-builtin,id=rng0
        -device "$rng_dev"

        -vga none
        -device "$gpu_dev"

        -device "$kbd_dev"
        -device "$mouse_dev"
        # Jak Makefile (docs/virtio-gpu-mouse-reliability-2026-11-11.md): bezwzgledny tablet jako
        # OSTATNIE urzadzenie; mysz wzgledna zostaje jako fallback.
        -device "$tablet_dev"
    )

    append_optional_cli_args

    if [[ -n "$iso" ]]; then
        # Jak Makefile/Windows: ISO ma pierwszenstwo przed zainstalowanym systemem na dysku.
        QEMU_ARGS+=( -boot order=d,menu=on -cdrom "$iso" )
    fi
}

build_legacy_args() {
    local iso="${1:-}"
    local installed_boot="${2:-false}"
    local ahci_disk="ide-hd,drive=disk,bus=ahci.0,unit=0"

    QEMU_ARGS=()
    append_common_machine_args

    # Jak w Makefile: HDA jest czescia bazowego profilu i pojawia sie przed
    # kontrolerem dysku / karta sieciowa. Zachowuje to identyczna topologie PCI.
    append_audio_args

    # Jak wyzej: dla zainstalowanego systemu ustawiamy jawny priorytet UEFI.
    if [[ "$installed_boot" == "true" ]]; then
        ahci_disk+=",bootindex=1"
    fi

    QEMU_ARGS+=(
        -drive "id=disk,file=$(qemu_opt_path "$DISK"),format=raw,if=none"
        -device ich9-ahci,id=ahci
        -device "$ahci_disk"

        -netdev user,id=n1
        -device e1000,netdev=n1

        -vga std
        -global VGA.vgamem_mb=64

        -device qemu-xhci,id=xhci
        -device usb-kbd,id=usbkbd,bus=xhci.0
        -device usb-mouse,id=usbmouse,bus=xhci.0
    )

    append_optional_cli_args

    if [[ -n "$iso" ]]; then
        QEMU_ARGS+=( -boot order=d,menu=on -cdrom "$iso" )
    fi
}

uefi_args() {
    detect_ovmf
    UEFI_ARGS=()

    if [[ -n "$OVMF_MONO" && -f "$OVMF_MONO" ]]; then
        UEFI_ARGS=( -bios "$OVMF_MONO" )
        return 0
    fi

    if [[ -n "$OVMF_CODE" && -f "$OVMF_CODE" && -n "$OVMF_VARS" && -f "$OVMF_VARS" ]]; then
        # Kopia robocza NVRAM launchera (wlasny plik .bursztyn_ovmf_vars_unix.fd); oryginal VARS
        # z pakietu systemowego nie jest modyfikowany.
        if [[ "$DRY_RUN" != "true" ]]; then
            rm -f "$OVMF_VARS_RUNTIME"
            cp "$OVMF_VARS" "$OVMF_VARS_RUNTIME"
        fi

        UEFI_ARGS=(
            -drive "if=pflash,format=raw,readonly=on,file=$(qemu_opt_path "$OVMF_CODE")"
            -drive "if=pflash,format=raw,file=$(qemu_opt_path "$OVMF_VARS_RUNTIME")"
        )
        return 0
    fi

    if [[ -n "$OVMF_CODE" && -f "$OVMF_CODE" ]]; then
        warn "Znaleziono CODE UEFI bez VARS. NVRAM firmware nie bedzie trwale zapisywany."
        UEFI_ARGS=(
            -drive "if=pflash,format=raw,readonly=on,file=$(qemu_opt_path "$OVMF_CODE")"
        )
        return 0
    fi

    show_error "Nie znaleziono firmware OVMF/EDK2 dla UEFI."
    echo "Linux: zainstaluj pakiet ovmf/edk2-ovmf albo ustaw OVMF_MONO / OVMF_CODE / OVMF_VARS."
    echo "macOS: zainstaluj QEMU przez Homebrew albo ustaw OVMF_CODE / OVMF_VARS."
    exit 1
}

validate_virtio_environment() {
    local missing=0
    local dev

    if [[ "$DRY_RUN" == "true" ]]; then
        return 0
    fi

    for dev in \
        virtio-blk-pci \
        virtio-net-pci \
        virtio-rng-pci \
        virtio-vga \
        virtio-keyboard-pci \
        virtio-mouse-pci \
        virtio-tablet-pci
    do
        if ! qemu_has_device "$dev"; then
            echo "BRAK QEMU device: $dev" >&2
            missing=1
        fi
    done

    if [[ "$missing" -ne 0 ]]; then
        show_error "Ta instalacja QEMU nie posiada wszystkich wymaganych urzadzen VirtIO."
        echo "Zaktualizuj QEMU lub uzyj trybu LEGACY."
        exit 1
    fi
}

LAST_QEMU_CODE=0

print_qemu_result() {
    local code="$1"
    LAST_QEMU_CODE="$code"
    echo

    if [[ "$code" -eq 0 ]]; then
        echo "QEMU zakonczone poprawnie."
    else
        echo "QEMU zakonczone z kodem: $code"
    fi
}

run_qemu() {
    local firmware="$1"
    local iso="$2"
    local mode_name="$3"
    local save_log="$4"
    local profile="${5:-virtio}"

    require_file "$iso" "obrazu ISO"
    check_iso_fresh "$iso"
    # Dzwiek przed dyskiem: blad konfiguracji dzwieku nie tworzy dysku i nie uruchamia QEMU.
    resolve_audio || return "$AUDIO_EXIT_CODE"
    check_guest_audio_device || return "$AUDIO_EXIT_CODE"
    prepare_disk

    if [[ "$profile" == "virtio" ]]; then
        validate_virtio_environment
        build_virtio_args "$iso"
    else
        build_legacy_args "$iso"
    fi

    local final_args=()

    if [[ "$firmware" == "UEFI" ]]; then
        uefi_args
        final_args=( "${UEFI_ARGS[@]}" "${QEMU_ARGS[@]}" )
    else
        final_args=( "${QEMU_ARGS[@]}" )
    fi

    final_args+=( -name "Bursztyn OS - $mode_name" )

    if [[ "$DRY_RUN" == "true" ]]; then
        print_audio_summary >&2
        print_dry_run "${final_args[@]}"
        return 0
    fi

    echo
    echo "============================================================"
    echo "Uruchamianie: $mode_name"
    echo "Host:         $HOST_NAME / $HOST_ARCH"
    echo "Firmware:     $firmware"
    echo "Profil:       $profile"
    echo "ISO:          $iso"
    echo "Dysk:         $DISK (trwaly, zapis wlaczony)"
    echo "RAM:          2 GiB"
    echo "CPU:          TCG / 1 vCPU"

    if [[ "$profile" == "virtio" ]]; then
        echo "Storage:      VirtIO-BLK"
        echo "Network:      VirtIO-NET"
        echo "RNG:          VirtIO-RNG"
        echo "GPU:          VirtIO-GPU 2D / virtio-vga"
        echo "Input:        VirtIO keyboard + mouse + tablet"
    else
        echo "Storage:      AHCI"
        echo "Network:      E1000"
        echo "GPU:          std VGA / Bochs"
        echo "Input:        xHCI + USB HID"
    fi

    print_audio_summary
    echo "Display:      ${QEMU_DISPLAY_ARGS:-(domyslny QEMU)}"
    if sdl_ime_fix_active; then
        echo "Klawiatura:   QEMU bez IBus (tylko ten proces; AltGr/polskie znaki); QEMU_SDL_BEZ_IME=0 wylacza"
    fi
    echo "============================================================"
    echo

    run_qemu_captured "$save_log" "$mode_name" "${final_args[@]}"
    print_qemu_result "$QEMU_RUN_CODE"
}

run_installed() {
    local profile="${1:-virtio}"

    # Nie tworzymy pustego dysku dla trybu "zainstalowany".
    assert_disk_path_safe
    require_file "$DISK" "zainstalowanego wirtualny_dysk.img"
    resolve_audio || return "$AUDIO_EXIT_CODE"
    check_guest_audio_device || return "$AUDIO_EXIT_CODE"

    if [[ "$profile" == "virtio" ]]; then
        validate_virtio_environment
        build_virtio_args "" "true"
    else
        build_legacy_args "" "true"
    fi

    uefi_args

    local final_args=(
        "${UEFI_ARGS[@]}"
        "${QEMU_ARGS[@]}"
        -boot order=c
        -name "Bursztyn OS - Zainstalowany UEFI"
    )

    if [[ "$DRY_RUN" == "true" ]]; then
        print_audio_summary >&2
        print_dry_run "${final_args[@]}"
        return 0
    fi

    echo
    echo "============================================================"
    echo "Uruchamianie zainstalowanego Bursztyn OS"
    echo "Host:         $HOST_NAME / $HOST_ARCH"
    echo "Firmware:     UEFI"
    echo "Profil:       $profile"
    echo "ISO:          BRAK - start tylko z dysku"
    echo "Dysk:         $(basename "$DISK")"
    echo "Boot:         UEFI / EFI/BOOT/BOOTX64.EFI"
    echo "Boot priority: bootindex=1"

    if [[ "$profile" == "virtio" ]]; then
        echo "Storage:      VirtIO-BLK"
        echo "Network:      VirtIO-NET"
        echo "GPU:          VirtIO-GPU"
        echo "Input:        VirtIO-INPUT"
    else
        echo "Storage:      AHCI"
        echo "Network:      E1000"
        echo "GPU:          std VGA"
        echo "Input:        USB HID"
    fi

    print_audio_summary
    echo "============================================================"
    echo

    run_qemu_captured "false" "Zainstalowany UEFI" "${final_args[@]}"
    print_qemu_result "$QEMU_RUN_CODE"
}

info_tests_canonical() {
    echo
    echo "Po pojawieniu sie GRUB wybierz:"
    echo "Bursztyn OS - testy automatyczne"
}

info_tests_interactive() {
    echo
    echo "Po starcie pulpitu uruchom aplikacje:"
    echo "Testy systemu"
    echo
    echo "Nastepnie kliknij [Uruchom test]."
}

info_display() {
    echo
    echo "RECZNY TEST USTAWIEN EKRANU / VIRTIO-GPU"
    echo
    echo "Po starcie uruchom: Ustawienia ekranu"
    echo
    echo "Sprawdz m.in.:"
    echo "  - 1280x720"
    echo "  - 1920x1080"
    echo "  - 2560x1440"
    echo "  - 3440x1440"
    echo "  - 3840x2160"
    echo "  - rollback po 15 s"
    echo "  - kursor, taskbar, popupy i compositor"
}

info_geom_trace() {
    echo
    echo "UWAGA: GEOM-TRACE jest flaga KOMPILACJI."
    echo "ISO musi byc zbudowane z:"
    echo "BURSZTYN_DIAG_GEOM_TRACE=1"
}

info_installer() {
    echo
    echo "INSTALATOR BURSZTYN OS"
    echo
    echo "Po pojawieniu sie GRUB wybierz wpis instalatora."
    echo "Dysk testowy ma domyslnie $DISK_SIZE."
    echo
    echo "UWAGA: instalator moze usunac zawartosc wirtualnego dysku."
}

# Usuwanie dysku tylko po jawnym potwierdzeniu (dysk jest TRWALY: zainstalowany system, pliki uzytkownika).
delete_disk() {
    if [[ ! -e "$DISK" && ! -L "$DISK" ]]; then
        echo
        echo "wirtualny_dysk.img juz nie istnieje."
        return 0
    fi
    if [[ -e "$DISK" && ! -f "$DISK" ]]; then
        show_error "wirtualny_dysk.img nie jest zwyklym plikiem - odmowa usuniecia:"
        echo "$DISK" >&2
        return 1
    fi
    if [[ "$DRY_RUN" == "true" ]]; then
        echo "(dry-run) usunieto by: $DISK"
        return 0
    fi
    echo
    echo "UWAGA: zostanie trwale usuniety dysk (zainstalowany system i dane):"
    echo "  $DISK"
    local answer=""
    read -r -p "Wpisz USUN, aby potwierdzic: " answer || answer=""
    if [[ "$answer" != "USUN" ]]; then
        echo "Anulowano - dysk nie zostal usuniety."
        return 1
    fi
    rm -f -- "$DISK"
    echo "Usunieto: $DISK"
}

run_choice() {
    local choice="$1"
    case "$choice" in
        1)
            run_qemu "BIOS" "$ISO_NORMAL" "Normalny BIOS VirtIO Full" "false" "virtio"
            ;;
        2)
            run_qemu "UEFI" "$ISO_NORMAL" "Normalny UEFI VirtIO Full" "false" "virtio"
            ;;
        3)
            info_tests_canonical
            run_qemu "BIOS" "$ISO_NORMAL" "Testy GRUB BIOS VirtIO" "true" "virtio"
            ;;
        4)
            info_tests_canonical
            run_qemu "UEFI" "$ISO_NORMAL" "Testy GRUB UEFI VirtIO" "true" "virtio"
            ;;
        5)
            require_file "$ISO_CI_TEST" ".bursztyn_ci_test.iso"
            run_qemu "BIOS" "$ISO_CI_TEST" "CI BIOS VirtIO" "true" "virtio"
            ;;
        6)
            require_file "$ISO_CI_TEST" ".bursztyn_ci_test.iso"
            run_qemu "UEFI" "$ISO_CI_TEST" "CI UEFI VirtIO" "true" "virtio"
            ;;
        7)
            info_tests_interactive
            run_qemu "BIOS" "$ISO_NORMAL" "Interaktywne BIOS VirtIO" "true" "virtio"
            ;;
        8)
            info_tests_interactive
            run_qemu "UEFI" "$ISO_NORMAL" "Interaktywne UEFI VirtIO" "true" "virtio"
            ;;
        9)
            info_display
            run_qemu "BIOS" "$ISO_NORMAL" "Ekran BIOS VirtIO-GPU" "true" "virtio"
            ;;
        10)
            info_display
            run_qemu "UEFI" "$ISO_NORMAL" "Ekran UEFI VirtIO-GPU" "true" "virtio"
            ;;
        11)
            info_geom_trace
            run_qemu "BIOS" "$ISO_NORMAL" "GEOM-TRACE BIOS VirtIO" "true" "virtio"
            ;;
        12)
            info_geom_trace
            run_qemu "UEFI" "$ISO_NORMAL" "GEOM-TRACE UEFI VirtIO" "true" "virtio"
            ;;
        13)
            delete_disk
            ;;
        14)
            run_installed "virtio"
            ;;
        15)
            info_installer
            run_qemu "UEFI" "$ISO_NORMAL" "Instalator UEFI VirtIO Full" "true" "virtio"
            ;;
        16)
            run_qemu "BIOS" "$ISO_NORMAL" "BIOS Legacy" "false" "legacy"
            ;;
        17)
            run_qemu "UEFI" "$ISO_NORMAL" "UEFI Legacy" "false" "legacy"
            ;;
        18)
            run_installed "legacy"
            ;;
        a|A)
            audio_menu
            ;;
        0)
            exit 0
            ;;
        *)
            show_error "Nieprawidlowy wybor."
            return 1
            ;;
    esac
}

if [[ "$AUDIO_DIAG" == "true" ]]; then
    audio_diag
    exit 0
fi

if [[ "$DRY_RUN" != "true" ]]; then
    require_command "$QEMU"
fi

if [[ -n "$MODE" ]]; then
    LAST_QEMU_CODE=0
    run_choice "$MODE"
    rc=$?
    if [[ "$rc" -ne 0 ]]; then
        exit "$rc"
    fi
    exit "$LAST_QEMU_CODE"
fi

while true; do
    echo
    echo "================================================================"
    echo "                  Bursztyn OS - QEMU"
    echo "                    Linux / macOS"
    echo "================================================================"
    echo
    echo " VIRTIO FULL - NAJNOWSZE STEROWNIKI"
    echo "  1  - BIOS + VirtIO Full"
    echo "  2  - UEFI + VirtIO Full"
    echo
    echo " TESTY AUTOMATYCZNE - kanoniczne BursztynOS.iso / VirtIO"
    echo "  3  - BIOS  (wybierz wpis testow w GRUB)"
    echo "  4  - UEFI  (wybierz wpis testow w GRUB)"
    echo
    echo " TESTY CI - .bursztyn_ci_test.iso / VirtIO"
    echo "  5  - BIOS"
    echo "  6  - UEFI"
    echo
    echo " TESTY INTERAKTYWNE / VirtIO"
    echo "  7  - BIOS"
    echo "  8  - UEFI"
    echo
    echo " USTAWIENIA EKRANU / VIRTIO-GPU"
    echo "  9  - BIOS"
    echo " 10  - UEFI"
    echo
    echo " GEOM-TRACE / VirtIO"
    echo " 11  - BIOS"
    echo " 12  - UEFI"
    echo
    echo " NARZEDZIA"
    echo " 13  - Usun wirtualny_dysk.img"
    echo
    echo " ZAINSTALOWANY SYSTEM"
    echo " 14  - UEFI + VirtIO Full, bez ISO"
    echo
    echo " INSTALATOR"
    echo " 15  - UEFI + VirtIO Full + BursztynOS.iso"
    echo
    echo " TRYB ZGODNOSCI / LEGACY"
    echo " 16  - BIOS: AHCI + E1000 + std VGA + USB HID"
    echo " 17  - UEFI: AHCI + E1000 + std VGA + USB HID"
    echo " 18  - Zainstalowany UEFI Legacy, bez ISO"
    echo
    echo " WYJŚCIE DŹWIĘKU (gość: Intel HDA)"
    echo "  A  - zmień (obecnie: $(audio_label "$AUDIO_REQUEST"))"
    echo
    echo "  0  - Wyjscie"
    echo

    read -r -p "Wybierz tryb [0-18, A]: " choice || exit 0

    run_choice "$choice" || true
done
