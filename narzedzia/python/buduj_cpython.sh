#!/usr/bin/env bash
set -euo pipefail

# Cross-build CPython against the already-built BursztynOS/musl sysroot.
# This script is intentionally never called by a test target.  It downloads
# and builds only when the explicit `make cpython-build` target is requested.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${CPYTHON_VERSION:-3.12.8}"
ARCHIVE_SHA256="${CPYTHON_SHA256:-5978435c479a376648cb02854df3b892ace9ed7d32b1fead652712bee9d03a45}"
URL="${CPYTHON_URL:-https://www.python.org/ftp/python/$VERSION/Python-$VERSION.tgz}"
BUILD_ROOT="${CPYTHON_BUILD_ROOT:-$ROOT/.cpython-build}"
MUSL_SYSROOT="${MUSL_SYSROOT:-$ROOT/.musl-build/sysroot}"
HOST_TRIPLE="${CPYTHON_HOST:-x86_64-linux-musl}"
CC="${CC:-x86_64-linux-gnu-gcc}"
AR="${AR:-x86_64-linux-gnu-ar}"
RANLIB="${RANLIB:-x86_64-linux-gnu-ranlib}"
BUILD_TRIPLE="${BUILD_TRIPLE:-}"
PYTHON_FOR_BUILD="${PYTHON_FOR_BUILD:-python3}"
JOBS="${JOBS:-$(nproc)}"

ARCHIVE="$BUILD_ROOT/Python-$VERSION.tgz"
SOURCE="$BUILD_ROOT/Python-$VERSION"
BUILD="$BUILD_ROOT/build"
RUNTIME_ROOT="${CPYTHON_RUNTIME_ROOT:-$BUILD_ROOT/runtime}"
CONFIG_SITE="$BUILD_ROOT/config.site"

if [[ ! -f "$MUSL_SYSROOT/usr/lib/libc.a" ||
      ! -f "$MUSL_SYSROOT/usr/lib/crt1.o" ]]; then
    printf '[CPYTHON] missing musl sysroot: %s\n' "$MUSL_SYSROOT" >&2
    printf '[CPYTHON] build it explicitly first with: make musl-sysroot\n' >&2
    exit 1
fi

if ! command -v "$CC" >/dev/null 2>&1 ||
   ! command -v "$AR" >/dev/null 2>&1 ||
   ! command -v "$RANLIB" >/dev/null 2>&1; then
    printf '[CPYTHON] x86_64 cross toolchain is required (CC=%s)\n' "$CC" >&2
    exit 1
fi
if [[ -z "$BUILD_TRIPLE" ]]; then
    BUILD_TRIPLE="$("$CC" -dumpmachine)"
fi

if ! "$PYTHON_FOR_BUILD" -c \
    'import sys; raise SystemExit(0 if sys.version_info[:2] == (3, 12) else 1)' \
    >/dev/null 2>&1; then
    printf '[CPYTHON] PYTHON_FOR_BUILD must be CPython 3.12: %s\n' \
        "$PYTHON_FOR_BUILD" >&2
    exit 1
fi

mkdir -p "$BUILD_ROOT"
if [[ ! -f "$ARCHIVE" ]]; then
    curl --fail --location --retry 3 --output "$ARCHIVE" "$URL"
fi
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum --check --strict

rm -rf "$SOURCE" "$BUILD" "$RUNTIME_ROOT"
mkdir -p "$SOURCE" "$BUILD" "$RUNTIME_ROOT"
tar --extract --gzip --file "$ARCHIVE" --strip-components=1 --directory "$SOURCE"

# CPython's configure probes must not select facilities that require the
# unfinished process/thread, dynamic-loader, event, or *at() syscall set.
cat > "$CONFIG_SITE" <<'EOF'
ac_cv_func_accept4=no
ac_cv_func_copy_file_range=no
ac_cv_func_dup3=no
ac_cv_func_epoll_create1=no
ac_cv_func_eventfd=no
ac_cv_func_fork=no
ac_cv_func_fstatat=no
ac_cv_func_futimens=no
ac_cv_func_getentropy=no
ac_cv_func_getrandom=no
ac_cv_func_inotify_init1=no
ac_cv_func_kqueue=no
ac_cv_func_memfd_create=no
ac_cv_func_openat=no
ac_cv_func_pipe2=no
ac_cv_func_posix_spawn=no
ac_cv_func_pthread_atfork=no
ac_cv_func_pthread_create=no
ac_cv_func_sched_getaffinity=no
ac_cv_func_sched_setaffinity=no
ac_cv_func_sem_open=no
ac_cv_func_sendfile=no
ac_cv_func_signalfd=no
ac_cv_func_timerfd_create=no
ac_cv_func_utimensat=no
ac_cv_func_vfork=no
ac_cv_func_dlopen=no
ac_cv_file__dev_ptc=no
ac_cv_file__dev_ptmx=no
EOF

pushd "$BUILD" >/dev/null
CONFIG_SITE="$CONFIG_SITE" \
CC="$CC" AR="$AR" RANLIB="$RANLIB" \
CPPFLAGS="--sysroot=$MUSL_SYSROOT" \
CFLAGS="--sysroot=$MUSL_SYSROOT -O2 -fno-pie -ffile-prefix-map=$SOURCE=/usr/src/Python-$VERSION" \
LDFLAGS="--sysroot=$MUSL_SYSROOT -static -no-pie -Wl,--build-id=none" \
PKG_CONFIG=/bin/false \
"$SOURCE/configure" \
    --build="$BUILD_TRIPLE" \
    --host="$HOST_TRIPLE" \
    --with-build-python="$PYTHON_FOR_BUILD" \
    --prefix=/usr \
    --exec-prefix=/usr \
    --with-platlibdir=lib \
    --disable-shared \
    --with-static-libpython \
    --without-ensurepip \
    --without-lto \
    --disable-ipv6 \
    --disable-test-modules

make -j"$JOBS"
make DESTDIR="$RUNTIME_ROOT" install
popd >/dev/null

PYTHON_LIBDIR="$RUNTIME_ROOT/usr/lib/python${VERSION%.*}"
PYTHON_BINDIR="$RUNTIME_ROOT/usr/bin"
mkdir -p "$PYTHON_BINDIR" "$PYTHON_LIBDIR/site-packages"

# A static ET_EXEC cannot load extension modules.  Keep the pure-Python
# library and the statically linked interpreter, but never package .so files.
find "$RUNTIME_ROOT/usr" \( -type f -o -type l \) \
    \( -name '*.so' -o -name '*.a' \) -delete
find "$PYTHON_LIBDIR" -type d -name 'lib-dynload' -prune -exec rm -rf {} +

if [[ ! -e "$PYTHON_BINDIR/python3" ]]; then
    if [[ -e "$PYTHON_BINDIR/python${VERSION%.*}" ]]; then
        ln -s "python${VERSION%.*}" "$PYTHON_BINDIR/python3"
    else
        printf '[CPYTHON] install did not produce /usr/bin/python3*; inspect %s\n' \
            "$PYTHON_BINDIR" >&2
        exit 1
    fi
fi

for required in \
    "$PYTHON_BINDIR/python3" \
    "$PYTHON_LIBDIR/encodings" \
    "$PYTHON_LIBDIR/sysconfig.py" \
    "$PYTHON_LIBDIR/site-packages"; do
    [[ -e "$required" ]] || {
        printf '[CPYTHON] runtime layout is incomplete: %s\n' "$required" >&2
        exit 1
    }
done

cp "$ROOT/narzedzia/python/cpython-runtime.manifest.json" \
   "$RUNTIME_ROOT/usr/share/cpython-runtime.manifest.json"
printf '[CPYTHON] runtime staged at: %s\n' "$RUNTIME_ROOT"
