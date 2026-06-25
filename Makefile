# Definicja narzędzi używanych do kompilacji i łączenia
# Wymagane jest środowisko kompilatora skrośnego (Cross-Compiler) zbudowanego dla x86_64-elf
CC = x86_64-linux-gnu-g++
AS = x86_64-linux-gnu-as
LD = x86_64-linux-gnu-ld

# Restrykcyjne flagi kompilacji dla jądra (C++)
CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2

# Flagi asmeblera
ASFLAGS = 

# Flagi linkera: używamy własnego skryptu linker.ld i nie podpinamy standardowych bibliotek
LDFLAGS = -T linker.ld -nostdlib -no-pie

# ZAKTUALIZOWANA Lista plików obiektowych - Dodałem tu psf.o, tss.o, klawiaturę, mysz, loader i syscalle!
OBJS = boot.o gdt.o tss.o apic.o idt.o przerwania.o klawiatura.o mysz.o pmm.o vmm.o psf.o syscall.o syscalls.o ring3.o loader.o kernel.o

# Domyślny cel, generujący binarkę systemu 
all: system_operacyjny.bin

# Reguła łącząca pliki w ostateczny obraz
system_operacyjny.bin: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

# === REGUŁY KOMPILACJI POSZCZEGÓLNYCH PLIKÓW ===

boot.o: boot.S
	$(AS) $(ASFLAGS) -c $< -o $@

gdt.o: gdt.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

tss.o: tss.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

apic.o: apic.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

idt.o: idt.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

przerwania.o: przerwania.S
	$(AS) $(ASFLAGS) -c $< -o $@

klawiatura.o: klawiatura.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

mysz.o: mysz.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

pmm.o: pmm.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

vmm.o: vmm.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

psf.o: psf.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

syscall.o: syscall.S
	$(AS) $(ASFLAGS) -c $< -o $@

syscalls.o: syscalls.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

ring3.o: ring3.S
	$(AS) $(ASFLAGS) -c $< -o $@

loader.o: loader.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

kernel.o: kernel.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

# Reguła czyszcząca artefakty
clear:
	rm -f $(OBJS) system_operacyjny.bin

# Reguła automatyzująca budowę obrazu i uruchomienie w QEMU
run: system_operacyjny.bin
	mkdir -p isodir/boot/grub
	cp system_operacyjny.bin isodir/boot/
	grub-mkrescue -o BursztynOS.iso isodir --xorriso=xorriso
	qemu-system-x86_64 -cdrom BursztynOS.iso -m 2G -serial stdio