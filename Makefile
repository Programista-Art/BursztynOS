# Narzędzia kompilacji
CXX = x86_64-linux-gnu-g++
AS = x86_64-linux-gnu-as
LD = x86_64-linux-gnu-ld
OBJCOPY = x86_64-linux-gnu-objcopy

# Flagi kompilatora C++ (Freestanding, brak standardowej biblioteki)
CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -fno-stack-protector -fcf-protection=none

# Lista wszystkich skompilowanych obiektów jądra
OBJS = boot.o gdt.o tss.o apic.o idt.o przerwania.o e1000.o siec.o klawiatura.o mysz.o zegar-rtc.o pmm.o vmm.o psf.o grafika.o syscall.o syscalls.o pci.o ahci.o ring3.o notatnik_blob.o kalkulator_blob.o loader.o kernel.o shell_blob.o menedzer_okien_blob.o uefi_gop.o

# Główny cel domyślny
all: system_operacyjny.bin

# Reguły kompilacji dla plików C++ (.cpp do .o)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Reguły kompilacji dla plików Asemblera (.S do .o)
%.o: %.S
	$(AS) -c $< -o $@

# === STEROWNIK UEFI GOP ===
uefi_gop.o: sterowniki/grafika/uefi_gop.cpp
	$(CXX) $(CXXFLAGS) -c sterowniki/grafika/uefi_gop.cpp -o uefi_gop.o	


# BIBLIOTEKA GUI (RING 3)
bursztyn_gui.o: bursztyn_gui.cpp
	$(CXX) $(CXXFLAGS) -fno-pie -c bursztyn_gui.cpp -o bursztyn_gui.o


# === BUDOWANIE POWŁOKI BURSZTYNA (RING 3) ===
shell_tmp.o: shell.cpp
	$(CXX) $(CXXFLAGS) -fno-pie -c shell.cpp -o shell_tmp.o

shell_blob.o: shell_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie shell_tmp.o bursztyn_gui.o -o shell.elf
	$(OBJCOPY) -O binary shell.elf shell.bin
	$(LD) -r -b binary shell.bin -o shell_blob.o


# BUDOWANIE NOTATNIKA (RING 3)
notatnik_tmp.o: notatnik.cpp
	$(CXX) $(CXXFLAGS) -fno-pie -c notatnik.cpp -o notatnik_tmp.o

notatnik_blob.o: notatnik_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie notatnik_tmp.o bursztyn_gui.o -o notatnik.elf
	$(OBJCOPY) -O binary notatnik.elf notatnik.bin
	$(LD) -r -b binary notatnik.bin -o notatnik_blob.o

# Kalkulator
# === BUDOWANIE KALKULATORA (RING 3) ===
kalkulator_tmp.o: kalkulator.cpp
	$(CXX) $(CXXFLAGS) -fno-pie -c kalkulator.cpp -o kalkulator_tmp.o

kalkulator_blob.o: kalkulator_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie kalkulator_tmp.o bursztyn_gui.o -o kalkulator.elf
	$(OBJCOPY) -O binary kalkulator.elf kalkulator.bin
	$(LD) -r -b binary kalkulator.bin -o kalkulator_blob.o

# BUDOWANIE MENEDZERA OKIEN (RING 3)
menedzer_okien_tmp.o: menedzer_okien.cpp
	$(CXX) $(CXXFLAGS) -fno-pie -c menedzer_okien.cpp -o menedzer_okien_tmp.o

menedzer_okien_blob.o: menedzer_okien_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie menedzer_okien_tmp.o bursztyn_gui.o -o menedzer_okien.elf
	$(OBJCOPY) -O binary menedzer_okien.elf menedzer_okien.bin
	$(LD) -r -b binary menedzer_okien.bin -o menedzer_okien_blob.o

#  KONSOLIDACJA JĄDRA 
system_operacyjny.bin: $(OBJS)
	$(CXX) -T linker.ld -nostdlib -no-pie -z noexecstack -o system_operacyjny.bin $(OBJS) -lgcc


# === BUDOWANIE OBRAZU ISO I KONFIGURACJA GRUB-A ===
iso: system_operacyjny.bin
	mkdir -p isodir/boot/grub
	cp system_operacyjny.bin isodir/boot/
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "Bursztyn OS" {' >> isodir/boot/grub/grub.cfg
	echo '    insmod all_video' >> isodir/boot/grub/grub.cfg
	echo '    set gfxpayload=keep' >> isodir/boot/grub/grub.cfg
	echo '    multiboot2 /boot/system_operacyjny.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o BursztynOS.iso isodir --xorriso=xorriso



# === URUCHOMIENIE W QEMU ===

 runb: iso
	if [ ! -f wirtualny_dysk.img ]; then qemu-img create -f raw wirtualny_dysk.img 40M; fi
	if [ -f tapeta.bmp ]; then dd if=tapeta.bmp of=wirtualny_dysk.img bs=512 seek=10 conv=notrunc; fi
	qemu-system-x86_64 -cdrom BursztynOS.iso -drive id=disk,file=wirtualny_dysk.img,format=raw,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -netdev user,id=n1 -device e1000,netdev=n1 -m 2G -vga std -serial stdio



# === URUCHOMIENIE W QEMU ===
run: iso
	if [ ! -f wirtualny_dysk.img ]; then qemu-img create -f raw wirtualny_dysk.img 40M; fi
	if [ -f tapeta.bmp ]; then dd if=tapeta.bmp of=wirtualny_dysk.img bs=512 seek=10 conv=notrunc; fi
	
	# TEST 1: Tryb UEFI (Sterownik UEFI GOP). Odkomentuj to, aby sprawdzić grafikę pod EFI:
	#qemu-system-x86_64 -bios /usr/share/qemu/OVMF.fd -accel tcg, -drive id=cdrom0,file=BursztynOS.iso,format=raw,media=cdrom,if=none -device ahci,id=ahci -device ide-cd,drive=cdrom0,bus=ahci.1 -drive id=disk,file=wirtualny_dysk.img,format=raw,if=none -device ide-hd,drive=disk,bus=ahci.0 -netdev user,id=n1 -device e1000,netdev=n1 -m 2G -vga std -serial stdio
	#qemu-system-x86_64 -bios /usr/share/qemu/OVMF.fd -drive id=cdrom0,file=BursztynOS.iso,format=raw,media=cdrom,if=none -device ahci,id=ahci -device ide-cd,drive=cdrom0,bus=ahci.1 -drive id=disk,file=wirtualny_dysk.img,format=raw,if=none -device ide-hd,drive=disk,bus=ahci.0 -netdev user,id=n1 -device e1000,netdev=n1 -m 2G -vga qxl -serial stdio
	# TEST 2: Tryb BIOS (Sterownik VESA VBE). Zakomentuj linię wyżej i odkomentuj poniższą, aby sprawdzić Legacy BIOS:
	#qemu-system-x86_64 -cdrom BursztynOS.iso -drive id=disk,file=wirtualny_dysk.img,format=raw,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -netdev user,id=n1 -device e1000,netdev=n1 -m 2G -vga std -serial stdio
	# TEST 1: Tryb UEFI (Wymaga zainstalowanego pakietu ovmf). Uzywamy karty qxl.
	qemu-system-x86_64 -bios /usr/share/qemu/OVMF.fd -accel tcg,thread=single -drive id=cdrom0,file=BursztynOS.iso,format=raw,media=cdrom,if=none -device ahci,id=ahci -device ide-cd,drive=cdrom0,bus=ahci.1 -drive id=disk,file=wirtualny_dysk.img,format=raw,if=none -device ide-hd,drive=disk,bus=ahci.0 -netdev user,id=n1 -device e1000,netdev=n1 -m 2G -vga qxl -serial stdio

	# TEST 2: Tryb BIOS. Zakomentuj linię wyżej (TEST 1) i odkomentuj poniższą, aby sprawdzić Legacy BIOS z karta qxl:
	# qemu-system-x86_64 -accel tcg,thread=single -cdrom BursztynOS.iso -drive id=disk,file=wirtualny_dysk.img,format=raw,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -netdev user,id=n1 -device e1000,netdev=n1 -m 2G -vga qxl -serial stdio


# === CZYSZCZENIE PROJEKTU ===
clear:
	rm -f *.o *.bin *.elf *.iso
	rm -rf isodir
cdysk:
	rm -rf wirtualny_dysk.img

rm:
	rm -f *.o *.bin *.elf *.iso
	#rm -rf isodir
	rm -rf wirtualny_dysk.img