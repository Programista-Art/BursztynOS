#!/usr/bin/env bash
set -euo pipefail

# Reproducible musl bootstrap for the BursztynOS POSIX gateway.
# The source is not vendored: it is downloaded into the ignored build tree,
# verified, patched in a disposable copy, and installed into a sysroot.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${MUSL_VERSION:-1.2.5}"
ARCHIVE_SHA256="${MUSL_SHA256:-a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4}"
BUILD_ROOT="${MUSL_BUILD_ROOT:-$ROOT/.musl-build}"
ARCHIVE="$BUILD_ROOT/musl-$VERSION.tar.gz"
SOURCE="$BUILD_ROOT/musl-$VERSION"
PATCHED="$BUILD_ROOT/musl-$VERSION-bursztyn"
SYSROOT="${MUSL_SYSROOT:-$BUILD_ROOT/sysroot}"
URL="${MUSL_URL:-https://musl.libc.org/releases/musl-$VERSION.tar.gz}"

mkdir -p "$BUILD_ROOT"

if [[ "${FORCE_MUSL_BUILD:-0}" != "1" &&
      -f "$SYSROOT/usr/share/bursztyn-musl.json" &&
      -f "$SYSROOT/usr/lib/libc.a" &&
      -f "$SYSROOT/usr/include/bursztyn/syscall.h" ]]; then
    printf '[MUSL] sysroot already ready: %s\n' "$SYSROOT"
    exit 0
fi

if [[ ! -f "$ARCHIVE" ]]; then
    curl --fail --location --retry 3 --output "$ARCHIVE" "$URL"
fi

printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum --check --strict

rm -rf "$SOURCE" "$PATCHED"
mkdir -p "$SOURCE" "$PATCHED"
tar --extract --gzip --file "$ARCHIVE" --strip-components=1 --directory "$SOURCE"
cp -a "$SOURCE"/. "$PATCHED"/

# musl's Linux x86_64 port emits SYSCALL in C wrappers and in the small
# assembly helpers used by thread teardown/clone. BursztynOS deliberately
# exposes the same register ABI through DPL3 INT 0x80, so change only the
# instruction encoding; syscall numbers and errno values remain Linux ABI
# compatible.
python3 - "$PATCHED" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
header = root / "arch" / "x86_64" / "syscall_arch.h"
text = header.read_text(encoding="utf-8")
old = '"syscall"'
new = '"int $0x80"'
if text.count(old) != 7:
    raise SystemExit(
        f"unexpected syscall wrapper count in {header}: {text.count(old)}"
    )
header.write_text(text.replace(old, new), encoding="utf-8")

asm_dir = root / "src" / "thread" / "x86_64"
for path in asm_dir.glob("*.s"):
    text = path.read_text(encoding="utf-8")
    patched = text.replace("\tsyscall", "\tint $0x80")
    if patched != text:
        path.write_text(patched, encoding="utf-8")
PY

rm -rf "$SYSROOT"
mkdir -p "$SYSROOT"
pushd "$PATCHED" >/dev/null
CC="${CC:-x86_64-linux-gnu-gcc}" \
AR="${AR:-x86_64-linux-gnu-ar}" \
RANLIB="${RANLIB:-x86_64-linux-gnu-ranlib}" \
./configure \
    --target=x86_64-linux-musl \
    --prefix=/usr \
    --disable-shared
make -j"${JOBS:-$(nproc)}"
make DESTDIR="$SYSROOT" install
popd >/dev/null

mkdir -p "$SYSROOT/usr/include/bursztyn"
cp "$ROOT/narzedzia/sdk/bursztyn_syscall.h" \
   "$SYSROOT/usr/include/bursztyn/syscall.h"

mkdir -p "$SYSROOT/usr/share"
cat > "$SYSROOT/usr/share/bursztyn-musl.json" <<EOF
{
  "musl_version": "$VERSION",
  "source_sha256": "$ARCHIVE_SHA256",
  "syscall_entry": "int 0x80",
  "target": "x86_64-bursztynos",
  "shared_libraries": false
}
EOF

printf '[MUSL] sysroot ready: %s\n' "$SYSROOT"
