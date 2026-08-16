#!/usr/bin/env bash
#
# Bursztyn OS - napraw_mbedtls.sh
#
# Bezpieczna instalacja/naprawa mbedTLS 2.28.x dla kernela bare-metal.
#
# Najwazniejsze cechy:
#   - najpierw pobieranie i walidacja, dopiero potem podmiana,
#   - rollback przy bledzie instalacji,
#   - opcjonalna kontrola SHA-256,
#   - brak deterministycznego rand()/LCG jako zrodla entropii TLS,
#   - memory_buffer_alloc zamiast systemowego malloc/free,
#   - TLS 1.2 + ECDHE + AES-GCM,
#   - brak filesystem/network/time libc.
#
# Domyslnie:
#   mbedTLS 2.28.8
#
# Uzycie:
#   ./napraw_mbedtls.sh
#   ./napraw_mbedtls.sh --archive /sciezka/mbedtls.zip
#   ./napraw_mbedtls.sh --sha256 <64-znakowy-hash>
#   ./napraw_mbedtls.sh --keep-backup
#
# Kernel MUSI docelowo dostarczyc:
#
#   extern "C" bool bursztyn_krypto_wypelnij_losowe(
#       uint8_t* bufor,
#       size_t dlugosc
#   );
#
# Bez tego hooka mbedTLS failuje bezpiecznie podczas seedowania CTR-DRBG.
#

set -Eeuo pipefail
IFS=$'\n\t'

SCRIPT_DIR="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1
    pwd -P
)"

PROJECT_ROOT="${PROJECT_ROOT:-$SCRIPT_DIR}"
MBEDTLS_VERSION="${MBEDTLS_VERSION:-2.28.8}"
MBEDTLS_ROOT="${MBEDTLS_ROOT:-$PROJECT_ROOT/biblioteki/mbedtls}"
MBEDTLS_URL="${MBEDTLS_URL:-https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v${MBEDTLS_VERSION}.zip}"
MBEDTLS_ARCHIVE="${MBEDTLS_ARCHIVE:-}"
MBEDTLS_SHA256="${MBEDTLS_SHA256:-}"
KEEP_BACKUP=0

log()  { printf '%s\n' "$*"; }
warn() { printf 'UWAGA: %s\n' "$*" >&2; }
die()  { printf 'BLAD: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Bursztyn OS - napraw_mbedtls.sh

Opcje:
  --version WERSJA      Domyslnie: 2.28.8
  --archive PLIK        Lokalny ZIP zamiast pobierania
  --sha256 HASH         Oczekiwany SHA-256 archiwum
  --keep-backup         Zachowaj kopie poprzedniej instalacji
  -h, --help            Pomoc

Zmienne:
  PROJECT_ROOT
  MBEDTLS_ROOT
  MBEDTLS_VERSION
  MBEDTLS_URL
  MBEDTLS_ARCHIVE
  MBEDTLS_SHA256
EOF
}

while (($# > 0)); do
    case "$1" in
        --version)
            (($# >= 2)) || die "Brak argumentu dla --version."
            MBEDTLS_VERSION="$2"
            MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v${MBEDTLS_VERSION}.zip"
            shift 2
            ;;
        --archive)
            (($# >= 2)) || die "Brak argumentu dla --archive."
            MBEDTLS_ARCHIVE="$2"
            shift 2
            ;;
        --sha256)
            (($# >= 2)) || die "Brak argumentu dla --sha256."
            MBEDTLS_SHA256="$2"
            shift 2
            ;;
        --keep-backup)
            KEEP_BACKUP=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Nieznana opcja: $1"
            ;;
    esac
done

[[ "$MBEDTLS_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ||
    die "Nieprawidlowy MBEDTLS_VERSION: $MBEDTLS_VERSION"

if [[ -n "$MBEDTLS_SHA256" ]] &&
   [[ ! "$MBEDTLS_SHA256" =~ ^[0-9A-Fa-f]{64}$ ]]; then
    die "MBEDTLS_SHA256 musi miec 64 znaki hex."
fi

need_cmd() {
    command -v "$1" >/dev/null 2>&1 ||
        die "Brak narzedzia: $1"
}

need_cmd mktemp
need_cmd unzip
need_cmd sha256sum
need_cmd awk
need_cmd find
need_cmd cp
need_cmd mv
need_cmd rm

if [[ -z "$MBEDTLS_ARCHIVE" ]]; then
    if command -v curl >/dev/null 2>&1; then
        DOWNLOADER="curl"
    elif command -v wget >/dev/null 2>&1; then
        DOWNLOADER="wget"
    else
        die "Brak curl/wget. Uzyj --archive albo zainstaluj downloader."
    fi
else
    DOWNLOADER="local"
fi

mkdir -p "$PROJECT_ROOT"

TMP_DIR="$(mktemp -d "$PROJECT_ROOT/.mbedtls-repair.XXXXXX")"
ARCHIVE_FILE="$TMP_DIR/mbedtls.zip"
EXTRACT_DIR="$TMP_DIR/extract"
STAGE_DIR="$TMP_DIR/stage"
BACKUP_DIR="$TMP_DIR/backup"

mkdir -p "$EXTRACT_DIR" "$STAGE_DIR" "$BACKUP_DIR"

INSTALL_IN_PROGRESS=0
INSTALL_FINISHED=0

ITEMS=(
    include
    library
    bursztyn_mbedtls_config.h
    mbedtls_port.cpp
    .bursztyn-mbedtls-version
    .bursztyn-mbedtls-source.sha256
)

rollback_install() {
    local item

    ((INSTALL_IN_PROGRESS != 0)) || return 0

    warn "Przywracam poprzednia instalacje mbedTLS..."

    for item in "${ITEMS[@]}"; do
        rm -rf -- "$MBEDTLS_ROOT/$item"

        if [[ -e "$BACKUP_DIR/$item" ||
              -L "$BACKUP_DIR/$item" ]]; then
            mv -- "$BACKUP_DIR/$item" "$MBEDTLS_ROOT/$item"
        fi
    done

    INSTALL_IN_PROGRESS=0
}

cleanup() {
    local status=$?

    if ((status != 0)); then
        rollback_install || true
    fi

    if ((INSTALL_FINISHED == 0 || KEEP_BACKUP == 0)); then
        rm -rf -- "$TMP_DIR"
    else
        log "Backup pozostawiono w:"
        log "  $BACKUP_DIR"
    fi

    trap - EXIT INT TERM HUP
    exit "$status"
}

trap cleanup EXIT INT TERM HUP

log "=== Narzedzie naprawcze mbedTLS dla Bursztyn OS ==="
log ""

# ---------------------------------------------------------------------------
# 1. Pobieranie
# ---------------------------------------------------------------------------

log "[1/8] Pobieranie/przygotowanie mbedTLS ${MBEDTLS_VERSION}..."

if [[ -n "$MBEDTLS_ARCHIVE" ]]; then
    [[ -f "$MBEDTLS_ARCHIVE" ]] ||
        die "Nie znaleziono archiwum: $MBEDTLS_ARCHIVE"

    cp -- "$MBEDTLS_ARCHIVE" "$ARCHIVE_FILE"
elif [[ "$DOWNLOADER" == "curl" ]]; then
    curl \
        --fail \
        --location \
        --proto '=https' \
        --tlsv1.2 \
        --retry 3 \
        --retry-delay 1 \
        --connect-timeout 20 \
        --output "$ARCHIVE_FILE" \
        "$MBEDTLS_URL"
else
    wget \
        --https-only \
        --secure-protocol=TLSv1_2 \
        --tries=3 \
        --timeout=20 \
        --output-document="$ARCHIVE_FILE" \
        "$MBEDTLS_URL"
fi

[[ -s "$ARCHIVE_FILE" ]] ||
    die "Archiwum jest puste."

# ---------------------------------------------------------------------------
# 2. SHA-256
# ---------------------------------------------------------------------------

log "[2/8] SHA-256..."

ACTUAL_SHA256="$(sha256sum "$ARCHIVE_FILE" | awk '{print $1}')"

[[ "$ACTUAL_SHA256" =~ ^[0-9a-f]{64}$ ]] ||
    die "Nie udalo sie obliczyc SHA-256."

if [[ -n "$MBEDTLS_SHA256" ]]; then
    EXPECTED_SHA256="$(printf '%s' "$MBEDTLS_SHA256" | tr 'A-F' 'a-f')"

    [[ "$ACTUAL_SHA256" == "$EXPECTED_SHA256" ]] ||
        die "Nieprawidlowy SHA-256. Otrzymano: $ACTUAL_SHA256"

    log "SHA-256: OK"
else
    warn "Nie podano przypietego SHA-256. Wykonuje walidacje strukturalna archiwum."
    log "SHA-256 pobranego archiwum: $ACTUAL_SHA256"
fi

# ---------------------------------------------------------------------------
# 3. Rozpakowanie i walidacja
# ---------------------------------------------------------------------------

log "[3/8] Rozpakowywanie i walidacja..."

unzip -q "$ARCHIVE_FILE" -d "$EXTRACT_DIR"

SOURCE_DIR="$(
    find "$EXTRACT_DIR" \
        -mindepth 1 \
        -maxdepth 1 \
        -type d \
        -name 'mbedtls-*' \
        -print \
        -quit
)"

[[ -n "$SOURCE_DIR" ]] ||
    die "Brak katalogu mbedtls-* w archiwum."

required_files=(
    include/mbedtls/config.h
    include/mbedtls/ssl.h
    include/mbedtls/entropy.h
    include/mbedtls/ctr_drbg.h
    include/mbedtls/x509_crt.h
    library/ssl_tls.c
    library/entropy.c
    library/ctr_drbg.c
    library/x509_crt.c
    library/platform.c
    library/memory_buffer_alloc.c
)

for rel in "${required_files[@]}"; do
    [[ -f "$SOURCE_DIR/$rel" ]] ||
        die "Brak wymaganego pliku: $rel"
done

cp -a -- "$SOURCE_DIR/include" "$STAGE_DIR/include"
cp -a -- "$SOURCE_DIR/library" "$STAGE_DIR/library"

# Zrodla zewnetrzne maja byc czyste.
find "$STAGE_DIR/library" -type f -name '*.o' -delete

# ---------------------------------------------------------------------------
# 4. Konfiguracja
# ---------------------------------------------------------------------------

log "[4/8] Generowanie bursztyn_mbedtls_config.h..."

cat > "$STAGE_DIR/bursztyn_mbedtls_config.h" <<'EOF'
/*
 * Bursztyn OS - user config dla mbedTLS 2.28.x
 *
 * Kompilacja:
 *   -DMBEDTLS_USER_CONFIG_FILE="bursztyn_mbedtls_config.h"
 *
 * Profil:
 *   bare-metal, TLS 1.2 client, ECDHE, AES-GCM, X.509, CTR-DRBG.
 */

#pragma once

#include <stddef.h>

/* Platforma */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS

#define MBEDTLS_PLATFORM_PRINTF_ALT
#define MBEDTLS_PLATFORM_SNPRINTF_ALT
#define MBEDTLS_PLATFORM_VSNPRINTF_ALT
#define MBEDTLS_PLATFORM_EXIT_ALT

#define MBEDTLS_MEMORY_BUFFER_ALLOC_C

/* Brak systemowych uslug libc/OS */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_ENTROPY_NV_SEED

/*
 * Dopoki Bursztyn OS nie dostarczy mbedTLS poprawnego czasu UTC,
 * weryfikacja dat notBefore/notAfter certyfikatu nie jest mozliwa.
 */

/* Brak PSA w obecnym porcie */
#undef MBEDTLS_PSA_CRYPTO_C
#undef MBEDTLS_USE_PSA_CRYPTO
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_PSA_ITS_FILE_C

/* Brak opcjonalnych instrukcji CPU */
#undef MBEDTLS_AESNI_C
#undef MBEDTLS_PADLOCK_C

/* Entropia / DRBG */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* Tylko TLS 1.2 */
#undef MBEDTLS_SSL_PROTO_SSL3
#undef MBEDTLS_SSL_PROTO_TLS1
#undef MBEDTLS_SSL_PROTO_TLS1_1

#define MBEDTLS_SSL_PROTO_TLS1_2



/* Forward secrecy ECDHE */
#undef MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECJPAKE_ENABLED

#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* Szyfrowanie */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_GCM

#undef MBEDTLS_CIPHER_MODE_CBC
#undef MBEDTLS_SSL_CBC_RECORD_SPLITTING
#undef MBEDTLS_ARC4_C
#undef MBEDTLS_DES_C

/* Hash / PK */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C

#undef MBEDTLS_MD2_C
#undef MBEDTLS_MD4_C
#undef MBEDTLS_MD5_C
#undef MBEDTLS_RIPEMD160_C

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* X.509 */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* ECDH / ECDSA */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C

#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED

/* TLS */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_MAX_CONTENT_LEN 16384

/* RSA do 4096 bitow */
#define MBEDTLS_MPI_MAX_SIZE 512

#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_THREADING_C
#undef MBEDTLS_THREADING_PTHREAD
EOF

# ---------------------------------------------------------------------------
# 5. Port integracyjny
# ---------------------------------------------------------------------------

log "[5/8] Generowanie mbedtls_port.cpp..."

cat > "$STAGE_DIR/mbedtls_port.cpp" <<'EOF'
/*
 * Bursztyn OS - port mbedTLS 2.28.x
 *
 * Nie ma tutaj rand(), LCG ani stalego seeda.
 * Brak kryptograficznego zrodla losowosci powoduje fail-closed.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#include "mbedtls/entropy.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/platform.h"

namespace {

constexpr size_t MBEDTLS_HEAP_SIZE =
    256U * 1024U;

alignas(64)
unsigned char mbedtls_heap[
    MBEDTLS_HEAP_SIZE
];

bool mbedtls_port_gotowy =
    false;

}

/*
 * Docelowy hook kernela.
 *
 * Implementacja MUSI byc CSPRNG/bezpiecznym zrodlem entropii.
 * Nie uzywaj stalego seeda, LCG, samego RDTSC ani samego RTC.
 */
extern "C" bool bursztyn_krypto_wypelnij_losowe(
    uint8_t* bufor,
    size_t dlugosc
) __attribute__((weak));

extern "C" int mbedtls_hardware_poll(
    void* data,
    unsigned char* output,
    size_t len,
    size_t* olen
) {
    (void)data;

    if (!olen) {
        return
            MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    *olen =
        0;

    if (len == 0) {
        return 0;
    }

    if (!output ||
        !bursztyn_krypto_wypelnij_losowe) {

        return
            MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    if (!bursztyn_krypto_wypelnij_losowe(
            output,
            len)) {

        return
            MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    *olen =
        len;

    return 0;
}

/* -------------------------------------------------------------------------
 * Minimalne funkcje libc wymagane przez kod mbedTLS
 * ------------------------------------------------------------------------- */

extern "C" void* memset(
    void* dest,
    int value,
    size_t n
) {
    if (!dest) {
        return dest;
    }

    unsigned char* p =
        static_cast<unsigned char*>(
            dest
        );

    const unsigned char v =
        static_cast<unsigned char>(
            value
        );

    while (n-- != 0) {
        *p++ =
            v;
    }

    return dest;
}

extern "C" void* memcpy(
    void* dest,
    const void* src,
    size_t n
) {
    if (!dest ||
        !src) {

        return dest;
    }

    unsigned char* d =
        static_cast<unsigned char*>(
            dest
        );

    const unsigned char* s =
        static_cast<const unsigned char*>(
            src
        );

    while (n-- != 0) {
        *d++ =
            *s++;
    }

    return dest;
}

extern "C" void* memmove(
    void* dest,
    const void* src,
    size_t n
) {
    if (!dest ||
        !src ||
        dest == src ||
        n == 0) {

        return dest;
    }

    unsigned char* d =
        static_cast<unsigned char*>(
            dest
        );

    const unsigned char* s =
        static_cast<const unsigned char*>(
            src
        );

    if (d < s) {
        while (n-- != 0) {
            *d++ =
                *s++;
        }
    } else {
        d +=
            n;

        s +=
            n;

        while (n-- != 0) {
            *--d =
                *--s;
        }
    }

    return dest;
}

extern "C" int memcmp(
    const void* lhs,
    const void* rhs,
    size_t n
) {
    if (n == 0) {
        return 0;
    }

    if (!lhs) {
        return rhs ? -1 : 0;
    }

    if (!rhs) {
        return 1;
    }

    const unsigned char* a =
        static_cast<const unsigned char*>(
            lhs
        );

    const unsigned char* b =
        static_cast<const unsigned char*>(
            rhs
        );

    for (size_t i = 0;
         i < n;
         ++i) {

        if (a[i] !=
            b[i]) {

            return
                a[i] <
                        b[i]
                    ? -1
                    : 1;
        }
    }

    return 0;
}

extern "C" size_t strlen(
    const char* s
) {
    if (!s) {
        return 0;
    }

    size_t n =
        0;

    while (s[n] != '\0') {
        ++n;
    }

    return n;
}

extern "C" int strcmp(
    const char* lhs,
    const char* rhs
) {
    if (!lhs) {
        return rhs ? -1 : 0;
    }

    if (!rhs) {
        return 1;
    }

    while (*lhs != '\0' &&
           *lhs == *rhs) {

        ++lhs;
        ++rhs;
    }

    const unsigned char a =
        static_cast<unsigned char>(
            *lhs
        );

    const unsigned char b =
        static_cast<unsigned char>(
            *rhs
        );

    return
        a == b
            ? 0
            : (a < b ? -1 : 1);
}

extern "C" int strncmp(
    const char* lhs,
    const char* rhs,
    size_t n
) {
    if (n == 0) {
        return 0;
    }

    if (!lhs) {
        return rhs ? -1 : 0;
    }

    if (!rhs) {
        return 1;
    }

    for (size_t i = 0;
         i < n;
         ++i) {

        const unsigned char a =
            static_cast<unsigned char>(
                lhs[i]
            );

        const unsigned char b =
            static_cast<unsigned char>(
                rhs[i]
            );

        if (a != b) {
            return
                a < b
                    ? -1
                    : 1;
        }

        if (a == 0) {
            return 0;
        }
    }

    return 0;
}

extern "C" char* strncpy(
    char* dest,
    const char* src,
    size_t n
) {
    if (!dest) {
        return dest;
    }

    size_t i =
        0;

    if (src) {
        while (i < n &&
               src[i] != '\0') {

            dest[i] =
                src[i];

            ++i;
        }
    }

    while (i < n) {
        dest[i++] =
            '\0';
    }

    return dest;
}

extern "C" char* strchr(
    const char* s,
    int c
) {
    if (!s) {
        return nullptr;
    }

    const char needle =
        static_cast<char>(
            c
        );

    for (;;) {
        if (*s ==
            needle) {

            return
                const_cast<char*>(
                    s
                );
        }

        if (*s ==
            '\0') {

            return nullptr;
        }

        ++s;
    }
}

extern "C" char* strstr(
    const char* haystack,
    const char* needle
) {
    if (!haystack ||
        !needle) {

        return nullptr;
    }

    if (*needle ==
        '\0') {

        return
            const_cast<char*>(
                haystack
            );
    }

    for (const char* h =
             haystack;
         *h != '\0';
         ++h) {

        const char* a =
            h;

        const char* b =
            needle;

        while (*a != '\0' &&
               *b != '\0' &&
               *a == *b) {

            ++a;
            ++b;
        }

        if (*b ==
            '\0') {

            return
                const_cast<char*>(
                    h
                );
        }
    }

    return nullptr;
}

extern "C" void* __memcpy_chk(
    void* dest,
    const void* src,
    size_t len,
    size_t destlen
) {
    if (len >
        destlen) {

        return nullptr;
    }

    return
        memcpy(
            dest,
            src,
            len
        );
}

extern "C" void* __memset_chk(
    void* dest,
    int value,
    size_t len,
    size_t destlen
) {
    if (len >
        destlen) {

        return nullptr;
    }

    return
        memset(
            dest,
            value,
            len
        );
}

/* -------------------------------------------------------------------------
 * Platform printf/snprintf.
 *
 * To jest tylko diagnostyka. Funkcja zachowuje NUL i nie interpretuje
 * format stringa, wiec nie jest powierzchnia format-string dla kernela.
 * ------------------------------------------------------------------------- */

static int bursztyn_vsnprintf(
    char* out,
    size_t out_size,
    const char* format,
    va_list args
) {
    (void)args;

    if (!format) {
        if (out &&
            out_size > 0) {

            out[0] =
                '\0';
        }

        return -1;
    }

    size_t len =
        0;

    while (format[len] != '\0') {
        ++len;
    }

    if (out &&
        out_size > 0) {

        size_t copy =
            len;

        if (copy >=
            out_size) {

            copy =
                out_size -
                1U;
        }

        for (size_t i = 0;
             i < copy;
             ++i) {

            out[i] =
                format[i];
        }

        out[copy] =
            '\0';
    }

    if (len >
        0x7FFFFFFFU) {

        return -1;
    }

    return
        static_cast<int>(
            len
        );
}

static int bursztyn_snprintf(
    char* out,
    size_t out_size,
    const char* format,
    ...
) {
    va_list args;

    va_start(
        args,
        format
    );

    const int ret =
        bursztyn_vsnprintf(
            out,
            out_size,
            format,
            args
        );

    va_end(
        args
    );

    return ret;
}

static int bursztyn_printf(
    const char* format,
    ...
) {
    (void)format;
    return 0;
}

[[noreturn]]
static void bursztyn_exit(
    int status
) {
    (void)status;

    asm volatile(
        "cli"
        :
        :
        : "memory"
    );

    for (;;) {
        asm volatile(
            "hlt"
            :
            :
            : "memory"
        );
    }
}

/* -------------------------------------------------------------------------
 * Inicjalizacja
 * ------------------------------------------------------------------------- */

extern "C" void inicjalizuj_mbedtls() {
    if (__atomic_load_n(
            &mbedtls_port_gotowy,
            __ATOMIC_ACQUIRE)) {

        return;
    }

    mbedtls_memory_buffer_alloc_init(
        mbedtls_heap,
        sizeof(
            mbedtls_heap
        )
    );

    (void)mbedtls_platform_set_printf(
        bursztyn_printf
    );

    (void)mbedtls_platform_set_snprintf(
        bursztyn_snprintf
    );

    (void)mbedtls_platform_set_vsnprintf(
        bursztyn_vsnprintf
    );

    (void)mbedtls_platform_set_exit(
        bursztyn_exit
    );

    __atomic_store_n(
        &mbedtls_port_gotowy,
        true,
        __ATOMIC_RELEASE
    );
}
EOF

# ---------------------------------------------------------------------------
# 6. Manifest i lokalny test kompilacji portu
# ---------------------------------------------------------------------------

printf '%s\n' "$MBEDTLS_VERSION" \
    > "$STAGE_DIR/.bursztyn-mbedtls-version"

printf '%s\n' "$ACTUAL_SHA256" \
    > "$STAGE_DIR/.bursztyn-mbedtls-source.sha256"

log "[6/8] Walidacja wygenerowanych plikow..."

[[ -s "$STAGE_DIR/bursztyn_mbedtls_config.h" ]] ||
    die "Nie wygenerowano konfiguracji."

[[ -s "$STAGE_DIR/mbedtls_port.cpp" ]] ||
    die "Nie wygenerowano portu."

if command -v x86_64-linux-gnu-g++ >/dev/null 2>&1; then
    x86_64-linux-gnu-g++ \
        -std=gnu++17 \
        -O2 \
        -ffreestanding \
        -fno-asynchronous-unwind-tables \
        -fno-unwind-tables \
        -mcmodel=large \
        -mno-red-zone \
        -mno-mmx \
        -mno-sse \
        -mno-sse2 \
        -fno-stack-protector \
        -fcf-protection=none \
        -fno-pic \
        -fno-pie \
        -fno-exceptions \
        -fno-rtti \
        -Wall \
        -Wextra \
        -Wpedantic \
        -Werror \
        -DMBEDTLS_USER_CONFIG_FILE=\"bursztyn_mbedtls_config.h\" \
        -I"$STAGE_DIR/include" \
        -I"$STAGE_DIR" \
        -c "$STAGE_DIR/mbedtls_port.cpp" \
        -o "$TMP_DIR/mbedtls_port.o"

    log "mbedtls_port.cpp: kompilacja OK"
else
    warn "Brak x86_64-linux-gnu-g++; pomijam lokalny test kompilacji."
fi

# ---------------------------------------------------------------------------
# 7. Transakcyjna podmiana
# ---------------------------------------------------------------------------

log "[7/8] Instalowanie..."

mkdir -p "$MBEDTLS_ROOT"

INSTALL_IN_PROGRESS=1

for item in "${ITEMS[@]}"; do
    if [[ -e "$MBEDTLS_ROOT/$item" ||
          -L "$MBEDTLS_ROOT/$item" ]]; then

        mv -- "$MBEDTLS_ROOT/$item" "$BACKUP_DIR/$item"
    fi
done

for item in "${ITEMS[@]}"; do
    [[ -e "$STAGE_DIR/$item" ||
       -L "$STAGE_DIR/$item" ]] ||
        die "Brak staged elementu: $item"

    mv -- "$STAGE_DIR/$item" "$MBEDTLS_ROOT/$item"
done

INSTALL_IN_PROGRESS=0
INSTALL_FINISHED=1

# ---------------------------------------------------------------------------
# 8. Koniec
# ---------------------------------------------------------------------------

log "[8/8] Gotowe."

if ((KEEP_BACKUP == 0)); then
    rm -rf -- "$BACKUP_DIR"
fi

log ""
log "==================================================="
log "mbedTLS ${MBEDTLS_VERSION} przygotowany dla Bursztyn OS."
log ""
log "Katalog:"
log "  $MBEDTLS_ROOT"
log ""
log "SHA-256 zrodla:"
log "  $ACTUAL_SHA256"
log ""
log "WAZNE:"
log "  HTTPS wymaga bezpiecznej implementacji:"
log "  bursztyn_krypto_wypelnij_losowe(uint8_t*, size_t)"
log ""
log "Po zmianie biblioteki/configu:"
log "  make clean"
log "  make run"
log ""
log "Usuwanie biblioteki/mbedtls/library/*.o przez make clean jest poprawne."
log "To tylko artefakty kompilacji, nie zrodla mbedTLS."
log "==================================================="
