#!/usr/bin/env bash
# Bursztyn OS - QEMU BIOS / UEFI + USB HID + aktualne testy
# Linux + macOS
#
# Zgodne z aktualnym Makefile / bieżącą architekturą:
#   BursztynOS.iso
#       - kanoniczne ISO
#       - normalny start / testy / instalator wybierane z GRUB
#
#   .bursztyn_ci_test.iso
#       - tymczasowy artefakt CI używany przez "make run-test"
#
#   wirtualny_dysk.img
#       - domyślnie 4 GiB
#       - wystarczający dla bieżącego instalatora:
#         ESP 512 MiB + BSP2 2 GiB + GPT/alignment
#       - opcja 14 uruchamia zainstalowany Bursztyn OS WYŁĄCZNIE z dysku,
#         bez obrazu ISO (odpowiednik "make run-installed")
#
# Test obrazu 8K nie jest obsługiwany przez ten launcher.
#
# GEOM-TRACE jest flagą KOMPILACJI:
#   BURSZTYN_DIAG_GEOM_TRACE=1
#
# Skrypt NIE kompiluje Bursztyn OS.
# Uruchamia istniejące obrazy ISO lub już zainstalowany wirtualny_dysk.img.

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

OS_NAME="$(uname -s)"
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
        echo "BLAD: Nieobslugiwany system hosta: $OS_NAME" >&2
        echo "Ten launcher jest przeznaczony dla Linux i macOS." >&2
        exit 1
        ;;
esac

QEMU="${QEMU:-qemu-system-x86_64}"
QEMU_IMG="${QEMU_IMG:-qemu-img}"
AUDIO_BACKEND="${AUDIO_BACKEND:-$DEFAULT_AUDIO}"

ISO_NORMAL="$SCRIPT_DIR/BursztynOS.iso"
ISO_CI_TEST="$SCRIPT_DIR/.bursztyn_ci_test.iso"
DISK="$SCRIPT_DIR/wirtualny_dysk.img"
DISK_SIZE="${DISK_SIZE:-4G}"
LOG_DIR="$SCRIPT_DIR/logi_qemu"
OVMF_VARS_RUNTIME="$SCRIPT_DIR/.bursztyn_ovmf_vars_unix.fd"

# Można nadpisać ręcznie:
#   OVMF_MONO=/sciezka/OVMF.fd ./rununix.sh
# albo:
#   OVMF_CODE=/sciezka/OVMF_CODE.fd \
#   OVMF_VARS=/sciezka/OVMF_VARS.fd \
#   ./rununix.sh
OVMF_MONO="${OVMF_MONO:-}"
OVMF_CODE="${OVMF_CODE:-}"
OVMF_VARS="${OVMF_VARS:-}"

show_error() {
    echo
    echo "BLAD: $1" >&2
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

detect_ovmf_linux_pair() {
    local pair code vars

    # Nie mieszamy CODE i VARS z różnych wariantów firmware.
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
    # Ustawienia użytkownika mają pierwszeństwo.
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

        # Homebrew QEMU zwykle używa x86_64 CODE + wspólnego x86 VARS.
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

common_args() {
    # Parametr może być pusty. Pusty oznacza start BEZ ISO.
    local iso="${1:-}"

    QEMU_ARGS=(
        -machine pc,hpet=on
        -cpu max
        -accel tcg,thread=single
        -smp 1

        -drive "id=disk,file=$DISK,format=raw,if=none"
        -device ich9-ahci,id=ahci
        -device ide-hd,drive=disk,bus=ahci.0,unit=0

        -netdev user,id=n1
        -device e1000,netdev=n1

        -audiodev "$AUDIO_BACKEND,id=snd"
        -device intel-hda
        -device hda-output,audiodev=snd

        -device qemu-xhci,id=xhci
        -device usb-kbd,id=usbkbd,bus=xhci.0
        -device usb-mouse,id=usbmouse,bus=xhci.0

        -m 2G
        -vga std
        -global VGA.vgamem_mb=64
        -serial stdio
    )

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
        # Każdy start dostaje świeżą kopię VARS, więc testy są deterministyczne.
        rm -f "$OVMF_VARS_RUNTIME"
        cp "$OVMF_VARS" "$OVMF_VARS_RUNTIME"

        UEFI_ARGS=(
            -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
            -drive "if=pflash,format=raw,file=$OVMF_VARS_RUNTIME"
        )
        return 0
    fi

    if [[ -n "$OVMF_CODE" && -f "$OVMF_CODE" ]]; then
        echo "UWAGA: znaleziono tylko kod UEFI bez pliku VARS."
        echo "Uruchamiam pojedynczy readonly pflash; NVRAM firmware nie bedzie trwale zapisywany."

        UEFI_ARGS=(
            -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
        )
        return 0
    fi

    show_error "Nie znaleziono firmware OVMF/EDK2 dla UEFI."
    echo "Linux: zainstaluj pakiet ovmf/edk2-ovmf albo ustaw OVMF_MONO / OVMF_CODE / OVMF_VARS."
    echo "macOS: zainstaluj QEMU przez Homebrew albo ustaw OVMF_CODE/OVMF_VARS."
    exit 1
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

    require_file "$iso" "obrazu ISO"
    prepare_disk
    common_args "$iso"

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
    echo "Host:         $HOST_NAME"
    echo "Firmware:     $firmware"
    echo "ISO:          $(basename "$iso")"
    echo "Dysk:         $(basename "$DISK")"
    echo "RAM:          2 GiB"
    echo "CPU:          TCG / 1 vCPU"
    echo "VGA:          std / 64 MiB VRAM"
    echo "Audio:        $AUDIO_BACKEND"
    echo "USB:          xHCI + keyboard + mouse"
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
    # Odpowiednik "make run-installed".
    #
    # Celowo NIE wywołujemy prepare_disk(). Jeśli obraz nie istnieje,
    # nie tworzymy pustego dysku i nie udajemy, że system jest zainstalowany.
    require_file "$DISK" "zainstalowanego wirtualny_dysk.img"

    common_args ""
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
    echo "Host:         $HOST_NAME"
    echo "Firmware:     UEFI"
    echo "ISO:          BRAK - start tylko z dysku"
    echo "Dysk:         $(basename "$DISK")"
    echo "Boot:         UEFI / EFI/BOOT/BOOTX64.EFI"
    echo "RAM:          2 GiB"
    echo "CPU:          TCG / 1 vCPU"
    echo "VGA:          std / 64 MiB VRAM"
    echo "Audio:        $AUDIO_BACKEND"
    echo "USB:          xHCI + keyboard + mouse"
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
    echo
    echo "Aktualny pakiet automatyczny obejmuje:"
    echo "  BWS 61..83"
    echo "  BursztynUI"
    echo "  Kontrolki 2"
    echo "  Kontrolki 3 + stabilizacja"
    echo "  Tapety BTP"
    echo "  Ustawienia ekranu / modeset / rollback / pamiec"
}

info_tests_interactive() {
    echo
    echo "Po starcie pulpitu uruchom:"
    echo "Testy systemu"
    echo
    echo "Nastepnie kliknij [Uruchom test]."
}

info_display_bios() {
    echo
    echo "RECZNY TEST USTAWIEN EKRANU / BOCHS VBE"
    echo
    echo "Po starcie uruchom z menu Start: Ustawienia ekranu"
    echo
    echo "Sprawdz:"
    echo "  1. 1920x1080 (Full HD)"
    echo "  2. 2560x1440 (QHD)"
    echo "  3. 3440x1440 (UWQHD)"
    echo "  4. 3840x2160 (4K UHD)"
    echo "  5. skale 100%, 125%, 150%, 175%, 200%"
    echo "  6. Zachowaj ustawienia"
    echo "  7. rollback po 15 s bez potwierdzenia"
    echo "  8. pasek zadan, Start, mysz, maksymalizacje i Tapety"
    echo
    echo "Launcher wystawia QEMU std VGA z 64 MiB VRAM."
}

info_display_uefi() {
    echo
    echo "RECZNY TEST USTAWIEN EKRANU / UEFI"
    echo
    echo "Po starcie uruchom z menu Start: Ustawienia ekranu"
    echo
    echo "Sprawdz biezaca rozdzielczosc, aktywny backend oraz skale UI."
}

info_geom_trace() {
    echo
    echo "UWAGA: GEOM-TRACE jest flaga kompilacji, nie opcja QEMU."
    echo "BursztynOS.iso musi byc zbudowane z:"
    echo "BURSZTYN_DIAG_GEOM_TRACE=1"
}

info_installer() {
    echo
    echo "INSTALATOR BURSZTYN OS"
    echo
    echo "Po pojawieniu sie GRUB wybierz wpis instalatora Bursztyn OS."
    echo "Dysk testowy ma domyslnie $DISK_SIZE."
    echo
    echo "UWAGA:"
    echo "Instalator w trybie 'Uzyj calego dysku' usuwa zawartosc wybranego dysku."
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
    echo " NORMALNY START - kanoniczne BursztynOS.iso"
    echo "  1  - BIOS"
    echo "  2  - UEFI"
    echo
    echo " AKTUALNE TESTY AUTOMATYCZNE - kanoniczne BursztynOS.iso"
    echo "  3  - BIOS  (wybierz wpis testow w GRUB)"
    echo "  4  - UEFI  (wybierz wpis testow w GRUB)"
    echo
    echo " TESTY AUTOMATYCZNE CI - odpowiednik make run-test"
    echo "  5  - BIOS  (.bursztyn_ci_test.iso)"
    echo "  6  - UEFI  (.bursztyn_ci_test.iso)"
    echo
    echo " TESTY INTERAKTYWNE - aplikacja Testy systemu"
    echo "  7  - BIOS"
    echo "  8  - UEFI"
    echo
    echo " USTAWIENIA EKRANU - TEST RECZNY"
    echo "  9  - BIOS / Bochs VBE (FHD, QHD, UWQHD, 4K, skala, rollback)"
    echo " 10  - UEFI (backend + skala)"
    echo
    echo " GEOM-TRACE"
    echo " 11  - BIOS"
    echo " 12  - UEFI"
    echo
    echo " NARZEDZIA"
    echo " 13  - Usun wirtualny_dysk.img (kolejny start utworzy pusty 4 GiB)"
    echo
    echo " ZAINSTALOWANY SYSTEM - BEZ ISO"
    echo " 14  - UEFI z wirtualny_dysk.img (odpowiednik make run-installed)"
    echo "       Nie podlacza BursztynOS.iso."
    echo
    echo " INSTALATOR"
    echo " 15  - UEFI z ISO (wybierz Instalator Bursztyn OS w GRUB)"
    echo
    echo "  0  - Wyjscie"
    echo

    read -r -p "Wybierz tryb [0-15]: " choice

    case "$choice" in
        1)
            run_qemu "BIOS" "$ISO_NORMAL" "Normalny BIOS" "false"
            ;;
        2)
            run_qemu "UEFI" "$ISO_NORMAL" "Normalny UEFI" "false"
            ;;
        3)
            info_tests_canonical
            run_qemu "BIOS" "$ISO_NORMAL" "Aktualne testy automatyczne - GRUB BIOS" "true"
            ;;
        4)
            info_tests_canonical
            run_qemu "UEFI" "$ISO_NORMAL" "Aktualne testy automatyczne - GRUB UEFI" "true"
            ;;
        5)
            require_file "$ISO_CI_TEST" ".bursztyn_ci_test.iso"
            run_qemu "BIOS" "$ISO_CI_TEST" "Automatyczne CI BIOS" "true"
            ;;
        6)
            require_file "$ISO_CI_TEST" ".bursztyn_ci_test.iso"
            run_qemu "UEFI" "$ISO_CI_TEST" "Automatyczne CI UEFI" "true"
            ;;
        7)
            info_tests_interactive
            run_qemu "BIOS" "$ISO_NORMAL" "Interaktywne testy BIOS" "true"
            ;;
        8)
            info_tests_interactive
            run_qemu "UEFI" "$ISO_NORMAL" "Interaktywne testy UEFI" "true"
            ;;
        9)
            info_display_bios
            run_qemu "BIOS" "$ISO_NORMAL" "Ustawienia ekranu BIOS Bochs VBE" "true"
            ;;
        10)
            info_display_uefi
            run_qemu "UEFI" "$ISO_NORMAL" "Ustawienia ekranu UEFI" "true"
            ;;
        11)
            info_geom_trace
            run_qemu "BIOS" "$ISO_NORMAL" "GEOM-TRACE BIOS" "true"
            ;;
        12)
            info_geom_trace
            run_qemu "UEFI" "$ISO_NORMAL" "GEOM-TRACE UEFI" "true"
            ;;
        13)
            if [[ -f "$DISK" ]]; then
                rm -f "$DISK"
                echo
                echo "Usunieto: $DISK"
                echo "Przy kolejnym starcie zostanie utworzony pusty dysk $DISK_SIZE."
            else
                echo
                echo "wirtualny_dysk.img juz nie istnieje."
            fi
            ;;
        14)
            run_installed
            ;;
        15)
            info_installer
            run_qemu "UEFI" "$ISO_NORMAL" "Instalator Bursztyn OS UEFI" "true"
            ;;
        0)
            exit 0
            ;;
        *)
            show_error "Nieprawidlowy wybor."
            ;;
    esac
done
