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
#   - VirtIO-INPUT    -> klawiatura + mysz
#   - MSI/MSI-X obslugiwane przez Bursztyn OS
#   - Intel HDA pozostaje aktualnym backendem audio
#
# Wymagane pliki w katalogu launchera:
#   BursztynOS.iso
#   wirtualny_dysk.img        (tworzony automatycznie, domyslnie 4 GiB)
#
# Opcjonalnie:
#   .bursztyn_ci_test.iso
#
# Skrypt NIE kompiluje systemu.
# Uruchamia istniejace ISO lub zainstalowany system z wirtualny_dysk.img.

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
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

ISO_NORMAL="${BURSZTYN_ISO:-$SCRIPT_DIR/BursztynOS.iso}"
ISO_CI_TEST="$SCRIPT_DIR/.bursztyn_ci_test.iso"
DISK="$SCRIPT_DIR/wirtualny_dysk.img"
DISK_SIZE="${DISK_SIZE:-4G}"

LOG_DIR="$SCRIPT_DIR/logi_qemu"
OVMF_VARS_RUNTIME="$SCRIPT_DIR/.bursztyn_ovmf_vars_unix.fd"

# Ustawienia opcjonalne:
#
#   AUDIO_BACKEND=alsa|pipewire|pa|sdl|coreaudio|off
#   QEMU_DISPLAY_ARGS="-display gtk,zoom-to-fit=off"
#   QEMU_EXTRA_ARGS="-d guest_errors"
#
# Produkcyjny profil SDL + tablet, zgodny z Makefile.
AUDIO_BACKEND="${AUDIO_BACKEND:-$DEFAULT_AUDIO}"
QEMU_DISPLAY_ARGS="${QEMU_DISPLAY_ARGS:--display sdl}"
QEMU_EXTRA_ARGS="${QEMU_EXTRA_ARGS:-}"

# Firmware mozna nadpisac recznie:
#
# Linux:
#   OVMF_MONO=/usr/share/ovmf/OVMF.fd ./rununix.sh
#
# lub:
#   OVMF_CODE=/.../OVMF_CODE.fd \
#   OVMF_VARS=/.../OVMF_VARS.fd \
#   ./rununix.sh
#
# macOS/Homebrew:
# detekcja jest automatyczna, ale zmienne powyzej tez maja pierwszenstwo.
OVMF_MONO="${OVMF_MONO:-}"
OVMF_CODE="${OVMF_CODE:-}"
OVMF_VARS="${OVMF_VARS:-}"

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

qemu_has_device() {
    local device_name="$1"
    "$QEMU" -device help 2>/dev/null | grep -Eq \
        "(^|[[:space:]\"'])${device_name}([[:space:]\"',]|$)"
}

qemu_device_supports_property() {
    local device_name="$1"
    local property_name="$2"

    "$QEMU" -device "${device_name},help" 2>&1 | \
        grep -Eq "(^|[[:space:]])${property_name}([=<[:space:]]|$)"
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

prepare_disk() {
    if [[ ! -f "$DISK" ]]; then
        echo
        echo "Tworzenie pustego dysku wirtualnego: $DISK_SIZE..."
        "$QEMU_IMG" create -f raw "$DISK" "$DISK_SIZE" || {
            show_error "Nie udalo sie utworzyc dysku wirtualnego."
            exit 1
        }
    fi
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
    if [[ "$AUDIO_BACKEND" == "off" || "$AUDIO_BACKEND" == "none" ]]; then
        echo "[QEMU] Audio: wylaczone."
        return 0
    fi

    QEMU_ARGS+=(
        -audiodev "${AUDIO_BACKEND},id=snd"
        -device intel-hda
        -device hda-output,audiodev=snd
    )
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
    local gpu_dev="virtio-vga,disable-legacy=on,xres=1280,yres=720"
    local kbd_dev="virtio-keyboard-pci"
    local mouse_dev="virtio-tablet-pci"

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
        -drive "id=disk,file=$DISK,format=raw,if=none"
        -device "$blk_dev"

        -netdev user,id=n1
        -device "$net_dev"

        -object rng-random,id=rng0,filename=/dev/urandom
        -device "$rng_dev"

        -vga none
        -device "$gpu_dev"

        -device "$kbd_dev"
        -device "$mouse_dev"
    )

    append_optional_cli_args

    if [[ -n "$iso" ]]; then
        QEMU_ARGS+=( -cdrom "$iso" )
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
        -drive "id=disk,file=$DISK,format=raw,if=none"
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
        QEMU_ARGS+=( -cdrom "$iso" )
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
        rm -f "$OVMF_VARS_RUNTIME"
        cp "$OVMF_VARS" "$OVMF_VARS_RUNTIME"

        UEFI_ARGS=(
            -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
            -drive "if=pflash,format=raw,file=$OVMF_VARS_RUNTIME"
        )
        return 0
    fi

    if [[ -n "$OVMF_CODE" && -f "$OVMF_CODE" ]]; then
        warn "Znaleziono CODE UEFI bez VARS. NVRAM firmware nie bedzie trwale zapisywany."
        UEFI_ARGS=(
            -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
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

    for dev in \
        virtio-blk-pci \
        virtio-net-pci \
        virtio-rng-pci \
        virtio-vga \
        virtio-keyboard-pci \
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

print_qemu_result() {
    local code="$1"
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

    echo
    echo "============================================================"
    echo "Uruchamianie: $mode_name"
    echo "Host:         $HOST_NAME / $HOST_ARCH"
    echo "Firmware:     $firmware"
    echo "Profil:       $profile"
    echo "ISO:          $(basename "$iso")"
    echo "Dysk:         $(basename "$DISK")"
    echo "RAM:          2 GiB"
    echo "CPU:          TCG / 1 vCPU"

    if [[ "$profile" == "virtio" ]]; then
        echo "Storage:      VirtIO-BLK"
        echo "Network:      VirtIO-NET"
        echo "RNG:          VirtIO-RNG"
        echo "GPU:          VirtIO-GPU 2D / virtio-vga"
        echo "Input:        VirtIO keyboard + mouse"
    else
        echo "Storage:      AHCI"
        echo "Network:      E1000"
        echo "GPU:          std VGA / Bochs"
        echo "Input:        xHCI + USB HID"
    fi

    echo "Audio:        $AUDIO_BACKEND"
    echo "============================================================"
    echo

    local code=0

    if [[ "$save_log" == "true" ]]; then
        mkdir -p "$LOG_DIR"

        local stamp safe_name log_file
        stamp="$(date '+%Y-%m-%d_%H-%M-%S')"
        safe_name="$(printf '%s' "$mode_name" | tr -c 'A-Za-z0-9_-' '_')"
        log_file="$LOG_DIR/${stamp}_${safe_name}.log"

        echo "Log COM1:"
        echo "$log_file"
        echo

        "$QEMU" "${final_args[@]}" 2>&1 | tee "$log_file"
        code=${PIPESTATUS[0]}
    else
        "$QEMU" "${final_args[@]}"
        code=$?
    fi

    print_qemu_result "$code"
}

run_installed() {
    local profile="${1:-virtio}"

    # Nie tworzymy pustego dysku dla trybu "zainstalowany".
    require_file "$DISK" "zainstalowanego wirtualny_dysk.img"

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

    echo "============================================================"
    echo

    local code=0
    "$QEMU" "${final_args[@]}"
    code=$?
    print_qemu_result "$code"
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

require_command "$QEMU"
require_command "$QEMU_IMG"

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
    echo "  0  - Wyjscie"
    echo

    read -r -p "Wybierz tryb [0-18]: " choice

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
            if [[ -f "$DISK" ]]; then
                rm -f "$DISK"
                echo
                echo "Usunieto: $DISK"
            else
                echo
                echo "wirtualny_dysk.img juz nie istnieje."
            fi
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
        0)
            exit 0
            ;;
        *)
            show_error "Nieprawidlowy wybor."
            ;;
    esac
done
