#!/bin/sh
# BRB Faza 4A-R - odizolowany fixture diagnostyczny GRUB (NIE jest obrazem produkcyjnym).
#
# Buduje male ISO narzedziem produkcyjnym (grub-mkrescue + xorriso, ten sam GRUB 2.12 co cel `make iso`)
# zawierajace WYLACZNIE dokladne jadro wydania (boot/system_operacyjny.bin) i grub.cfg odwzorowujacy wpis
# "Bursztyn OS" z Makefile (all_video, gfxmode 1280x720x32, gfxpayload=keep, multiboot2 + parametry).
# Roznice wzgledem produkcji (jawne): tapety.pak tylko gdy podany (generowany do .brb-build), dodatkowe polecenia
# diagnostyczne GRUB (echo, lsefimmap, lsmmap) wypisywane na COM1. Zadnych zmian w jadrze.
#
# Uzycie: zbuduj_fixture_grub.sh <jadro> <katalog_wyjsciowy_w_.brb-build> [tapety.pak]
# Z trzecim argumentem fixture laduje takze module2 /boot/tapety.pak (jak produkcyjny wpis "Bursztyn OS").
set -eu
JADRO=$1
WY=$2
TAPETY=${3:-}
case "$WY" in
    *.brb-build*) ;;
    *) echo "[FIXTURE] BLAD: katalog wyjsciowy musi lezec w .brb-build/"; exit 2 ;;
esac
SRC="$WY/iso-src"
mkdir -p "$SRC/boot/grub"
cp "$JADRO" "$SRC/boot/system_operacyjny.bin"
MODUL=""
if [ -n "$TAPETY" ]; then cp "$TAPETY" "$SRC/boot/tapety.pak"; MODUL="module2 /boot/tapety.pak tapety.pak"; fi
cat > "$SRC/boot/grub/grub.cfg" <<EOF
serial --unit=0 --speed=115200
terminal_output --append serial
set pager=0
echo "GRUB-DIAG: start platforma=\${grub_platform} cpu=\${grub_cpu}"
echo "GRUB-DIAG: lsefimmap-przed"
lsefimmap
echo "GRUB-DIAG: lsmmap-przed"
lsmmap
insmod all_video
set gfxmode=1280x720x32
set gfxpayload=keep
echo "GRUB-DIAG: PRZED-MULTIBOOT"
sleep 4
multiboot2 /boot/system_operacyjny.bin bursztyn.refresh=60 bursztyn.video=1280x720
$MODUL
echo "GRUB-DIAG: PO-MULTIBOOT"
lsefimmap
echo "GRUB-DIAG: BOOT"
boot
EOF
grub-mkrescue -o "$WY/fixture-grub.iso" "$SRC" >"$WY/grub-mkrescue.log" 2>&1
sha256sum "$WY/fixture-grub.iso" "$SRC/boot/system_operacyjny.bin" "$SRC/boot/grub/grub.cfg"
ls -la "$WY/fixture-grub.iso"
