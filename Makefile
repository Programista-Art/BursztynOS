# Definicja narzędzi używanych do kompilacji i łączenia (Cross-Compiler x86_64-elf)
CC = x86_64-linux-gnu-g++
AS = x86_64-linux-gnu-as
LD = x86_64-linux-gnu-ld

# Restrykcyjne flagi kompilacji dla jądra (C++)
CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
ASFLAGS = 
LDFLAGS = -T linker.ld -nostdlib -no-pie -z noexecstack

# Lista plików obiektowych (shell_blob.o na samym końcu)
OBJS = boot.o gdt.o tss.o apic.o idt.o przerwania.o klawiatura.o mysz.o pmm.o vmm.o psf.o grafika.o syscall.o syscalls.o ring3.o loader.o kernel.o shell_blob.o

# Domyślny cel kompilacji
all: system_operacyjny.bin

# Reguła łącząca pliki w ostateczny obraz
system_operacyjny.bin: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

# Reguły kompilacji dla C++ i Asemblera
%.o: %.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

# --- KOMPILACJA PROGRAMU UŻYTKOWNIKA (Terminal Ring 3) ---
shell_blob.o: shell.cpp shell_linker.ld
	$(CC) $(CXXFLAGS) -fno-pie -c shell.cpp -o shell_tmp.o
	$(LD) -T shell_linker.ld -nostdlib -no-pie shell_tmp.o -o shell.elf
	x86_64-linux-gnu-objcopy -O binary shell.elf shell.bin
	$(LD) -r -b binary shell.bin -o shell_blob.o

# === BUDOWA OBRAZU I URUCHAMIANIE ===
# Reguła generująca pełny obraz ISO z własnym menu GRUB
iso: system_operacyjny.bin
	mkdir -p isodir/boot/grub
	cp system_operacyjny.bin isodir/boot/
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "Bursztyn OS" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot2 /boot/system_operacyjny.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o BursztynOS.iso isodir --xorriso=xorriso

# Uruchomienie w maszynie wirtualnej
run: iso
	qemu-system-x86_64 -cdrom BursztynOS.iso -m 2G

# Reguła czyszcząca artefakty
clear:
	rm -f $(OBJS) system_operacyjny.bin BursztynOS.iso shell_tmp.o shell.elf shell.bin
	rm -rf isodir