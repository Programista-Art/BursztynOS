#!/bin/bash
export DISPLAY=:0
cp wirtualny_dysk.img disp.img
qemu-system-x86_64 \
    -bios "/usr/share/ovmf/OVMF.fd" \
    -machine pc,hpet=on -cpu max -accel tcg,thread=multi -smp 1 -m 2G \
    -audiodev alsa,id=snd -device intel-hda -device hda-output,audiodev=snd \
    -serial file:serial_xdotool.log \
    -drive id=disk,file=disp.img,format=raw,if=none -device virtio-blk-pci,drive=disk,disable-legacy=on \
    -netdev user,id=n1 -device virtio-net-pci,netdev=n1,disable-legacy=on \
    -object rng-random,id=rng0,filename=/dev/urandom -device virtio-rng-pci,rng=rng0,disable-legacy=on \
    -vga none -device virtio-vga,disable-legacy=on,xres=1280,yres=720 \
    -device virtio-keyboard-pci -device virtio-mouse-pci \
    -boot order=d \
    -cdrom BursztynOS.iso > /dev/null 2>&1 &
QEMU_PID=$!
echo "QEMU started with PID $QEMU_PID"
sleep 20
WID=$(xdotool search --name "QEMU" | head -1)
if [ -z "$WID" ]; then
    echo "No QEMU window found"
    kill $QEMU_PID
    exit 1
fi
xdotool windowactivate $WID
sleep 1
# Menu Start
xdotool mousemove --window $WID 50 700 click 1
sleep 2
# Tapety
xdotool mousemove --window $WID 140 507 click 1
sleep 15
# Wallpaper
xdotool mousemove --window $WID 303 418 click 1
sleep 2
# Zastosuj
xdotool mousemove --window $WID 380 570 click 1
sleep 5
# Menu Start
xdotool mousemove --window $WID 50 700 click 1
sleep 2
# Uruchom ponownie
xdotool mousemove --window $WID 140 632 click 1
echo "Reboot clicked, waiting 20s to see if it reboots..."
sleep 20
if kill -0 $QEMU_PID 2>/dev/null; then
    echo "QEMU still running, capturing GDB!"
    gdb -ex 'set pagination off' -ex 'thread apply all bt' -ex 'quit' -p $QEMU_PID > gdb_xdotool.log 2>&1
    kill -9 $QEMU_PID
else
    echo "QEMU exited cleanly or rebooted"
fi
