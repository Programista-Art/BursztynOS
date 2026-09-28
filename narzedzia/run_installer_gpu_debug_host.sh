#!/usr/bin/env bash
# ==============================================================================
# narzedzia/run_installer_gpu_debug_host.sh
#
# Transparent host-side diagnostic launcher for BursztynOS under QEMU + GTK.
# Replicates production 'make runuefi' topology and device ordering exactly,
# while using a disposable disk by default and allowing interactive GDB / GTK runs.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    cat << 'EOF'
Usage: narzedzia/run_installer_gpu_debug_host.sh <GPU_VARIANT> <ACCELERATOR> [OPTIONS]

Arguments:
  GPU_VARIANT:
      modern-only   VirtIO-GPU modern-only
                    (-device virtio-vga,disable-legacy=on,xres=1280,yres=720)
      transitional  VirtIO-GPU transitional (legacy compatibility enabled)
                    (-device virtio-vga,xres=1280,yres=720)

  ACCELERATOR:
      single        TCG single-threaded (-accel tcg,thread=single)
      multi         TCG multi-threaded  (-accel tcg,thread=multi)

Execution Modes:
  --gdb             Launch QEMU under GDB from the start (gdb --args qemu...)
  --normal          Launch QEMU directly in interactive GTK mode (default)

Additional Options:
  --disk <path>     Path to raw disk image (default: /tmp/bursztyn-gpu-debug.img)
  --iso <path>      Path to ISO file (default: BursztynOS.iso in repo root)
  --dry-run         Print QEMU argv and exit without launching
  -h, --help        Display this help message

Test Matrix Runs (select 'Bursztyn OS Instalator' in GRUB):
  A: ./narzedzia/run_installer_gpu_debug_host.sh modern-only single --gdb
  B: ./narzedzia/run_installer_gpu_debug_host.sh modern-only multi --gdb
  C: ./narzedzia/run_installer_gpu_debug_host.sh transitional single --gdb
  D: ./narzedzia/run_installer_gpu_debug_host.sh transitional multi --gdb
EOF
}

GPU_VARIANT=""
ACCEL_MODE=""
EXEC_MODE="normal"
DISK_IMG="/tmp/bursztyn-gpu-debug.img"
ISO_PATH="${REPO_ROOT}/BursztynOS.iso"
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        modern-only|modern)
            GPU_VARIANT="modern-only"
            shift
            ;;
        transitional|legacy)
            GPU_VARIANT="transitional"
            shift
            ;;
        single)
            ACCEL_MODE="single"
            shift
            ;;
        multi)
            ACCEL_MODE="multi"
            shift
            ;;
        --gdb)
            EXEC_MODE="gdb"
            shift
            ;;
        --normal)
            EXEC_MODE="normal"
            shift
            ;;
        --disk)
            if [[ $# -lt 2 ]]; then
                echo "Error: --disk requires an image path argument" >&2
                exit 1
            fi
            DISK_IMG="$2"
            shift 2
            ;;
        --disk=*)
            DISK_IMG="${1#*=}"
            shift
            ;;
        --iso)
            if [[ $# -lt 2 ]]; then
                echo "Error: --iso requires an ISO path argument" >&2
                exit 1
            fi
            ISO_PATH="$2"
            shift 2
            ;;
        --iso=*)
            ISO_PATH="${1#*=}"
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --gpu)
            if [[ $# -lt 2 ]]; then
                echo "Error: --gpu requires modern-only or transitional" >&2
                exit 1
            fi
            GPU_VARIANT="$2"
            shift 2
            ;;
        --gpu=*)
            GPU_VARIANT="${1#*=}"
            shift
            ;;
        --accel)
            if [[ $# -lt 2 ]]; then
                echo "Error: --accel requires single or multi" >&2
                exit 1
            fi
            ACCEL_MODE="$2"
            shift 2
            ;;
        --accel=*)
            ACCEL_MODE="${1#*=}"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Error: Unknown argument '$1'" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ -z "$GPU_VARIANT" ]]; then
    echo "Error: GPU variant not specified. Choose 'modern-only' or 'transitional'." >&2
    usage
    exit 1
fi

if [[ "$GPU_VARIANT" != "modern-only" && "$GPU_VARIANT" != "transitional" ]]; then
    echo "Error: Invalid GPU variant '$GPU_VARIANT'. Choose 'modern-only' or 'transitional'." >&2
    exit 1
fi

if [[ -z "$ACCEL_MODE" ]]; then
    echo "Error: Accelerator mode not specified. Choose 'single' or 'multi'." >&2
    usage
    exit 1
fi

if [[ "$ACCEL_MODE" != "single" && "$ACCEL_MODE" != "multi" ]]; then
    echo "Error: Invalid accelerator mode '$ACCEL_MODE'. Choose 'single' or 'multi'." >&2
    exit 1
fi

# ------------------------------------------------------------------------------
# Disk verification & isolation
# ------------------------------------------------------------------------------
PROD_DISK_CANONICAL="${REPO_ROOT}/wirtualny_dysk.img"
REAL_PROD_DISK=""
if [[ -f "$PROD_DISK_CANONICAL" ]]; then
    REAL_PROD_DISK="$(realpath "$PROD_DISK_CANONICAL" 2>/dev/null || readlink -f "$PROD_DISK_CANONICAL" 2>/dev/null || echo "$PROD_DISK_CANONICAL")"
else
    REAL_PROD_DISK="$(realpath -m "$PROD_DISK_CANONICAL" 2>/dev/null || readlink -f "$PROD_DISK_CANONICAL" 2>/dev/null || echo "$PROD_DISK_CANONICAL")"
fi

GIVEN_DISK_REAL=""
if [[ -f "$DISK_IMG" ]]; then
    GIVEN_DISK_REAL="$(realpath "$DISK_IMG" 2>/dev/null || readlink -f "$DISK_IMG" 2>/dev/null || echo "$DISK_IMG")"
else
    GIVEN_DISK_REAL="$(realpath -m "$DISK_IMG" 2>/dev/null || readlink -f "$DISK_IMG" 2>/dev/null || echo "$DISK_IMG")"
fi

IS_PRODUCTION_DISK=false
if [[ -n "$REAL_PROD_DISK" && "$GIVEN_DISK_REAL" == "$REAL_PROD_DISK" ]]; then
    IS_PRODUCTION_DISK=true
elif [[ "$DISK_IMG" == "wirtualny_dysk.img" || "$DISK_IMG" == "./wirtualny_dysk.img" || "$DISK_IMG" == "$PROD_DISK_CANONICAL" ]]; then
    IS_PRODUCTION_DISK=true
fi

if [[ "$IS_PRODUCTION_DISK" == "true" ]]; then
    echo "[BLAD] Ten skrypt diagnostyczny nie moze uzywac wirtualny_dysk.img."
    echo "[BLAD] Uzyj dysku tymczasowego w /tmp."
    exit 1
fi

echo "[SECURITY] Using disposable disk: $DISK_IMG (production disk wirtualny_dysk.img protected)"
DISPOSABLE_TARGET_BYTES=4294967296  # 4 GiB (4294967296 bytes)
if [[ ! -f "$DISK_IMG" ]]; then
    echo "[DISK] Disposable disk does not exist. Creating 4G sparse raw image at: $DISK_IMG"
    qemu-img create -f raw "$DISK_IMG" 4G
else
    CURRENT_VSIZE=""
    if command -v qemu-img >/dev/null 2>&1; then
        CURRENT_VSIZE="$(qemu-img info --output=json "$DISK_IMG" 2>/dev/null | grep '"virtual-size":' | head -n1 | tr -dc '0-9')"
    fi
    if [[ -z "$CURRENT_VSIZE" ]]; then
        CURRENT_VSIZE="$(stat -c %s "$DISK_IMG" 2>/dev/null || echo 0)"
    fi

    if [[ "$CURRENT_VSIZE" -ne "$DISPOSABLE_TARGET_BYTES" ]]; then
        echo "[DISK] Disposable disk virtual size ($CURRENT_VSIZE bytes) != 4 GiB ($DISPOSABLE_TARGET_BYTES bytes). Recreating: $DISK_IMG"
        rm -f "$DISK_IMG"
        qemu-img create -f raw "$DISK_IMG" 4G
    else
        echo "[DISK] Existing disposable disk verified (virtual size: 4 GiB): $DISK_IMG"
    fi
fi

# ------------------------------------------------------------------------------
# ISO verification
# ------------------------------------------------------------------------------
if [[ ! -f "$ISO_PATH" ]]; then
    echo "Error: ISO not found at '$ISO_PATH'." >&2
    echo "Build the ISO with diagnostics first: make iso BURSZTYN_DIAG_VIRTIO_BOOT=1" >&2
    exit 1
fi

# ------------------------------------------------------------------------------
# OVMF Firmware discovery (matching Makefile check-ovmf logic)
# ------------------------------------------------------------------------------
OVMF_MONO=""
for f in "${OVMF_MONO:-}" /usr/share/ovmf/OVMF.fd /usr/share/OVMF/OVMF.fd /usr/share/qemu/OVMF.fd; do
    if [[ -n "$f" && -f "$f" ]]; then
        OVMF_MONO="$f"
        break
    fi
done

OVMF_CODE=""
OVMF_VARS=""
if [[ -z "$OVMF_MONO" ]]; then
    for f in "${OVMF_CODE:-}" /usr/share/OVMF/OVMF_CODE.fd /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/edk2/x64/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd; do
        if [[ -n "$f" && -f "$f" ]]; then
            OVMF_CODE="$f"
            break
        fi
    done
    for f in "${OVMF_VARS:-}" /usr/share/OVMF/OVMF_VARS.fd /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/edk2/x64/OVMF_VARS.fd /usr/share/edk2/ovmf/OVMF_VARS.fd; do
        if [[ -n "$f" && -f "$f" ]]; then
            OVMF_VARS="$f"
            break
        fi
    done
fi

if [[ -z "$OVMF_MONO" && ( -z "$OVMF_CODE" || -z "$OVMF_VARS" ) ]]; then
    echo "Error: No complete OVMF firmware found on host (/usr/share/ovmf or /usr/share/OVMF)." >&2
    exit 1
fi

# ------------------------------------------------------------------------------
# Exact Production Device Order Construction
# ------------------------------------------------------------------------------
QEMU_ARGS=()

# 1. Firmware
VARS_RUNTIME=""
if [[ -n "$OVMF_MONO" ]]; then
    QEMU_ARGS+=(-bios "$OVMF_MONO")
else
    VARS_RUNTIME="/tmp/bursztyn-ovmf-vars-$$.fd"
    cp "$OVMF_VARS" "$VARS_RUNTIME"
    QEMU_ARGS+=(
        -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
        -drive "if=pflash,format=raw,file=$VARS_RUNTIME"
    )
fi

# 2. QEMU_BASE_ARGS
QEMU_ARGS+=(
    -machine pc,hpet=on
    -cpu max
)

if [[ "$ACCEL_MODE" == "single" ]]; then
    QEMU_ARGS+=(-accel tcg,thread=single)
else
    QEMU_ARGS+=(-accel tcg,thread=multi)
fi

QEMU_ARGS+=(
    -smp 1
    -m 2G
    -audiodev alsa,id=snd
    -device intel-hda
    -device hda-output,audiodev=snd
    -serial stdio
    -display gtk,gl=off
)

# 3. Storage Subsystem (QEMU_DISK_DRIVE + QEMU_VIRTIO_BLK)
QEMU_ARGS+=(
    -drive "id=disk,file=$DISK_IMG,format=raw,if=none"
    -device virtio-blk-pci,drive=disk,disable-legacy=on
)

# 4. Networking (QEMU_VIRTIO_COMMON_ARGS: virtio-net)
QEMU_ARGS+=(
    -netdev user,id=n1
    -device virtio-net-pci,netdev=n1,disable-legacy=on
)

# 5. RNG (QEMU_VIRTIO_COMMON_ARGS: virtio-rng)
QEMU_ARGS+=(
    -object rng-random,id=rng0,filename=/dev/urandom
    -device virtio-rng-pci,rng=rng0,disable-legacy=on
)

# 6. Graphics / GPU (QEMU_VIRTIO_COMMON_ARGS: -vga none + virtio-vga)
QEMU_ARGS+=(-vga none)
if [[ "$GPU_VARIANT" == "modern-only" ]]; then
    QEMU_ARGS+=(-device virtio-vga,disable-legacy=on,xres=1280,yres=720)
else
    QEMU_ARGS+=(-device virtio-vga,xres=1280,yres=720)
fi

# 7. Input (QEMU_VIRTIO_COMMON_ARGS: virtio-keyboard, virtio-mouse)
QEMU_ARGS+=(
    -device virtio-keyboard-pci
    -device virtio-mouse-pci
)

# 8. Media & Boot Order (runuefi)
QEMU_ARGS+=(
    -boot order=d
    -cdrom "$ISO_PATH"
)

# ------------------------------------------------------------------------------
# Diagnostics log directory setup
# ------------------------------------------------------------------------------
LOG_DIR="/tmp/bursztyn-gpu-debug"
mkdir -p "$LOG_DIR"
SERIAL_LOG="${LOG_DIR}/serial.log"
STDERR_LOG="${LOG_DIR}/qemu.stderr"
PID_FILE="${LOG_DIR}/qemu.pid"
POST_FREEZE_GDB="${LOG_DIR}/post_freeze.gdb"

cat << 'EOF' > "$POST_FREEZE_GDB"
set pagination off
info threads
thread apply all bt
EOF

START_TIME="$(date -u +"%Y-%m-%d %H:%M:%S UTC")"

# ------------------------------------------------------------------------------
# Print Full QEMU Argv
# ------------------------------------------------------------------------------
echo "================================================================================"
echo "BursztynOS GPU Diagnostic Launcher (Host-Side)"
echo "================================================================================"
echo "Timestamp:       $START_TIME"
echo "Execution Mode:  $EXEC_MODE"
echo "GPU Variant:     $GPU_VARIANT"
echo "Accelerator:     $ACCEL_MODE"
echo "Disk Image:      $DISK_IMG"
echo "ISO Image:       $ISO_PATH"
echo "Display:         GTK (gl=off)"
echo "Serial Log:      $SERIAL_LOG"
echo "QEMU Stderr:     $STDERR_LOG"
echo "--------------------------------------------------------------------------------"
echo "FULL QEMU ARGV (identical device order to production make runuefi):"
for arg in "qemu-system-x86_64" "${QEMU_ARGS[@]}"; do
    printf '    %s\n' "$arg"
done
echo "--------------------------------------------------------------------------------"
echo "Exact command line:"
printf '%q ' "qemu-system-x86_64" "${QEMU_ARGS[@]}"
echo -e "\n================================================================================"

# ------------------------------------------------------------------------------
# Launch Execution
# ------------------------------------------------------------------------------
if [[ "$DRY_RUN" == "true" ]]; then
    echo "[DRY-RUN] Script executed with --dry-run. Exiting without launching QEMU."
    exit 0
fi

if [[ "$EXEC_MODE" == "gdb" ]]; then
    echo ""
    echo "================================================================================"
    echo "GDB INTERACTIVE MODE"
    echo "QEMU is launched under GDB from the start (avoiding ptrace attach issues)."
    echo ""
    echo "STEPS:"
    echo "  1. At the (gdb) prompt, start QEMU by typing:"
    echo "         run"
    echo "  2. When the GTK window opens, select from the GRUB boot menu:"
    echo "         Bursztyn OS Instalator"
    echo "  3. When the machine freezes during boot / VirtIO reset:"
    echo "     Return to this terminal and press: Ctrl-C"
    echo "  4. Execute these GDB commands:"
    echo "         set pagination off"
    echo "         info threads"
    echo "         thread apply all bt"
    echo "     (or: source $POST_FREEZE_GDB)"
    echo "  5. Copy the thread backtraces for analysis."
    echo "================================================================================"
    echo ""
    exec gdb --args qemu-system-x86_64 "${QEMU_ARGS[@]}"
else
    # Non-GDB Normal interactive GTK run
    : > "$SERIAL_LOG"
    : > "$STDERR_LOG"

    {
        echo "=== BursztynOS Host Run Log ==="
        echo "Start: $START_TIME"
        echo "GPU:   $GPU_VARIANT"
        echo "Accel: $ACCEL_MODE"
        echo "Disk:  $DISK_IMG"
        echo "ISO:   $ISO_PATH"
        echo "Command line:"
        printf '%q ' "qemu-system-x86_64" "${QEMU_ARGS[@]}"
        echo -e "\n==============================="
    } > "$STDERR_LOG"

    QEMU_PID=""

    cleanup() {
        local exit_code=$?
        trap - INT TERM EXIT
        if [[ -n "$QEMU_PID" ]] && kill -0 "$QEMU_PID" 2>/dev/null; then
            echo -e "\n[HOST DEBUG] Stopping QEMU process (PID $QEMU_PID)..." >&2
            kill -TERM "$QEMU_PID" 2>/dev/null || true
            for _ in {1..15}; do
                if ! kill -0 "$QEMU_PID" 2>/dev/null; then
                    break
                fi
                sleep 0.2
            done
            if kill -0 "$QEMU_PID" 2>/dev/null; then
                echo "[HOST DEBUG] Force-killing QEMU process (PID $QEMU_PID)..." >&2
                kill -KILL "$QEMU_PID" 2>/dev/null || true
            fi
            wait "$QEMU_PID" 2>/dev/null || true
        fi
        rm -f "$PID_FILE" "${VARS_RUNTIME:-}" 2>/dev/null || true
        exit "$exit_code"
    }

    trap cleanup INT TERM EXIT

    echo "[HOST DEBUG] Launching QEMU..."
    qemu-system-x86_64 "${QEMU_ARGS[@]}" \
        > >(tee -a "$SERIAL_LOG") \
        2> >(tee -a "$STDERR_LOG" >&2) &
    QEMU_PID=$!
    echo "$QEMU_PID" > "$PID_FILE"

    echo "[HOST DEBUG] QEMU PID: $QEMU_PID"
    echo "[HOST DEBUG] Waiting for QEMU session... (Close window or Ctrl-C to finish)"

    wait "$QEMU_PID" 2>/dev/null || true
    EXIT_STATUS=$?

    echo ""
    echo "[HOST DEBUG] QEMU PID $QEMU_PID exited with code $EXIT_STATUS."
    echo "[HOST DEBUG] Serial output captured at: $SERIAL_LOG"
    echo "[HOST DEBUG] QEMU stderr captured at:  $STDERR_LOG"
    exit "$EXIT_STATUS"
fi
