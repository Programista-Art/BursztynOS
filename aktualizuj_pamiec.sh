#!/bin/bash

set -e

echo "=== Aktualizacja układu pamięci aplikacji Ring 3 ==="

PLIKI=(
    shell.cpp
    menedzer_okien.cpp
    programy/*.cpp
)

# TEXT:
# plik: 0x1000
# rozmiar: 0xF000 = 61440
# VA: 0x601000
sed -i -E \
's/[0-9]+,[[:space:]]*[0-9]+,[[:space:]]*0x601000/4096, 61440, 0x601000/g' \
"${PLIKI[@]}"

# DATA:
# po 0x1000 + 0xF000 = 0x10000 = 65536
# VA: 0x601000 + 0xF000 = 0x610000
sed -i -E \
's/[0-9]+,[[:space:]]*[0-9]+,[[:space:]]*0x609000/65536, 131072, 0x610000/g' \
"${PLIKI[@]}"

echo "=== Nowy układ ==="
echo "TEXT: offset=4096, size=61440, VA=0x601000"
echo "DATA: offset=65536, size=131072, VA=0x610000"

echo "Gotowe."