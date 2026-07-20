# Narzędzia kompilacji
CXX = x86_64-linux-gnu-g++
AS = x86_64-linux-gnu-as
LD = x86_64-linux-gnu-ld
OBJCOPY = x86_64-linux-gnu-objcopy

# Flagi kompilatora C++ (Freestanding, brak standardowej biblioteki)
CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2

# Lista wszystkich skompilowanych obiektów jądra
OBJS = boot.o gdt.o tss.o apic.o idt.o przerwania.o klawiatura.o mysz.o zegar-rtc.o pmm.o vmm.o psf.o grafika.o syscall.o syscalls.o ring3.o loader.o kernel.o shell_blob.o

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
	echo '    set gfxpayload=1024x768x32,1024x768x24,800x600x32,auto' >> isodir/boot/grub/grub.cfg
# 	echo '    set gfxpayload=1024x768x32' >> isodir/boot/grub/grub.cfg
	echo '    multiboot2 /boot/system_operacyjny.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o BursztynOS.iso isodir --xorriso=xorriso

isodir/boot/grub/grub.cfg:
	mkdir -p isodir/boot/grub
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'insmod vbe' >> isodir/boot/grub/grub.cfg
	echo 'insmod vga' >> isodir/boot/grub/grub.cfg
	echo 'insmod video' >> isodir/boot/grub/grub.cfg
	echo 'set gfxmode=1024x768x32' >> isodir/boot/grub/grub.cfg
	echo 'set gfxpayload=1024x768x32,1024x768x24,800x600x32,auto' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "Bursztyn OS" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot2 /boot/system_operacyjny.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg	

# === URUCHOMIENIE W QEMU ===
run: iso
	qemu-system-x86_64 -cdrom BursztynOS.iso -m 2G -vga std -serial stdio
# 	qemu-system-x86_64 -cdrom BursztynOS.iso -m 2G -vga cirrus -serial stdio
# 	qemu-system-x86_64 -cdrom BursztynOS.iso -m 2G -vga none -serial stdio

# === CZYSZCZENIE PROJEKTU ===
clear:
	rm -f *.o *.bin *.elf *.iso
	rm -rf isodir