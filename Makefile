# Narzędzia kompilacji
CXX = x86_64-linux-gnu-g++
AS = x86_64-linux-gnu-as
LD = x86_64-linux-gnu-ld
OBJCOPY = x86_64-linux-gnu-objcopy

# Flagi kompilatora C++ (Freestanding, brak standardowej biblioteki)
CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2

# Lista wszystkich skompilowanych obiektów
OBJS = boot.o gdt.o tss.o apic.o idt.o przerwania.o klawiatura.o mysz.o zegar-rtc.o pmm.o vmm.o psf.o grafika.o syscall.o syscalls.o pci.o ahci.o ring3.o loader.o kernel.o shell_blob.o

# Główny cel domyślny
all: system_operacyjny.bin

# Reguły kompilacji dla plików C++ (.cpp do .o)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Reguły kompilacji dla plików Asemblera (.S do .o)
%.o: %.S
	$(AS) -c $< -o $@

# === BUDOWANIE POWŁOKI BURSZTYNA (RING 3) ===
shell_tmp.o: shell.cpp
	$(CXX) $(CXXFLAGS) -fno-pie -c shell.cpp -o shell_tmp.o

shell_blob.o: shell_tmp.o
	$(LD) -T shell_linker.ld -nostdlib -no-pie shell_tmp.o -o shell.elf
	$(OBJCOPY) -O binary shell.elf shell.bin
	$(LD) -r -b binary shell.bin -o shell_blob.o

# === KONSOLIDACJA JĄDRA ===
system_operacyjny.bin: $(OBJS)
	$(CXX) -T linker.ld -nostdlib -no-pie -z noexecstack -o system_operacyjny.bin $(OBJS) -lgcc

# === BUDOWANIE OBRAZU ISO I KONFIGURACJA GRUB-A ===
iso: system_operacyjny.bin
	rm -rf isodir
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
# UWAGA: Aby AHCI moglo cos odczytac, przypinamy prawdziwy, pusty wirtualny dysk SATA
run: iso
# Tworzymy surowy plik dysku (raw) jeśli nie istnieje
	if [ ! -f wirtualny_dysk.img ]; then qemu-img create -f raw wirtualny_dysk.img 10M; fi
	# Jeśli masz plik tapeta.bmp w folderze, wgraj go na dysk zaczynając od Sektora 10 (LBA 10)
	if [ -f tapeta.bmp ]; then dd if=tapeta.bmp of=wirtualny_dysk.img bs=512 seek=10 conv=notrunc; fi
	qemu-system-x86_64 -cdrom BursztynOS.iso -drive id=disk,file=wirtualny_dysk.img,format=raw,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 2G -serial stdio
# 	qemu-img create -f qcow2 wirtualny_dysk.qcow2 10M || true
# 	qemu-system-x86_64 -cdrom BursztynOS.iso -drive id=disk,file=wirtualny_dysk.qcow2,if=none -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 -m 2G -serial stdio

# === CZYSZCZENIE PROJEKTU ===
clear:
	rm -f *.o *.bin *.elf *.iso
	rm -rf isodir

cdysk:
	rm wirtualny_dysk.img
