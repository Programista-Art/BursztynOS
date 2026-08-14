# ==========================================
# Makefile dla Bursztyn OS (64-bit)
# ==========================================

# Narzędzia kompilacji
CC = x86_64-linux-gnu-gcc
CXX = x86_64-linux-gnu-g++
AS = x86_64-linux-gnu-as
LD = x86_64-linux-gnu-ld
OBJCOPY = x86_64-linux-gnu-objcopy

# Wspólne flagi dla C i C++
COMMON_FLAGS = -ffreestanding -O2 -Wall -Wextra -mcmodel=large -mno-red-zone \
               -mno-mmx -mno-sse -mno-sse2 -fno-stack-protector -fcf-protection=none \
               -DMBEDTLS_USER_CONFIG_FILE=\"bursztyn_mbedtls_config.h\" \
               -Ibiblioteki/mbedtls/include -Ibiblioteki/mbedtls

# Rozdzielenie flag na C (mbedTLS) i C++ (Jądro)
CFLAGS = $(COMMON_FLAGS)
CXXFLAGS = $(COMMON_FLAGS) -fno-exceptions -fno-rtti

# KRYTYCZNA POPRAWKA: Wycofanie -Os na rzecz stabilnego -O2. 
# Zabezpiecza to przed samowolnym generowaniem ukrytych skoków przez GCC.
RING3_FLAGS = -ffreestanding -O2 -Wall -Wextra -mcmodel=large -mno-red-zone \
              -mno-mmx -mno-sse -mno-sse2 -fno-stack-protector -fcf-protection=none \
              -fno-exceptions -fno-rtti -fno-pie

# --- Budowanie silnika krypto (mbedTLS) ---
MBEDTLS_OBJS = $(patsubst %.c, %.o, $(wildcard biblioteki/mbedtls/library/*.c))

# Lista wszystkich skompilowanych obiektów jądra
OBJS = boot.o gdt.o tss.o apic.o idt.o przerwania.o e1000.o siec.o hda.o klawiatura.o \
       przegladarka_blob.o mysz.o zegar-rtc.o pmm.o vmm.o psf.o grafika.o syscall.o \
       syscalls.o pci.o ahci.o ring3.o notatnik_blob.o kalkulator_blob.o loader.o \
       kernel.o shell_blob.o menedzer_okien_blob.o uefi_gop.o bursztyn_gui.o dzwiek_blob.o \
       mbedtls_port.o tls.o $(MBEDTLS_OBJS)

# Główny cel domyślny
all: system_operacyjny.bin

# Reguły kompilacji dla plików C++ (.cpp do .o)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Reguły kompilacji dla plików C (.c do .o) - dla mbedTLS
biblioteki/mbedtls/library/%.o: biblioteki/mbedtls/library/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Reguły kompilacji dla plików Asemblera (.S do .o)
%.o: %.S
	$(AS) -c $< -o $@

# Kompilacja portu łączącego Jądro z mbedTLS
mbedtls_port.o: biblioteki/mbedtls/mbedtls_port.cpp
	$(CXX) $(CXXFLAGS) -c biblioteki/mbedtls/mbedtls_port.cpp -o mbedtls_port.o

# === NOWY STEROWNIK DŹWIĘKU INTEL HDA ===
hda.o: sterowniki/dzwiek/hda.cpp
	$(CXX) $(CXXFLAGS) -fno-pie -c sterowniki/dzwiek/hda.cpp -o hda.o    

# === STEROWNIK UEFI GOP ===
uefi_gop.o: sterowniki/grafika/uefi_gop.cpp
	$(CXX) $(CXXFLAGS) -c sterowniki/grafika/uefi_gop.cpp -o uefi_gop.o    

# BIBLIOTEKA GUI (RING 3) - Używa RING3_FLAGS (-Os)
bursztyn_gui.o: bursztyn_gui.cpp
	$(CXX) $(RING3_FLAGS) -c bursztyn_gui.cpp -o bursztyn_gui.o

# === BUDOWANIE POWŁOKI BURSZTYNA (RING 3) ===
shell_tmp.o: shell.cpp
	$(CXX) $(RING3_FLAGS) -c shell.cpp -o shell_tmp.o

shell_blob.o: shell_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie shell_tmp.o bursztyn_gui.o -o shell.elf
	$(OBJCOPY) -O binary shell.elf shell.bin
	$(LD) -r -b binary shell.bin -o shell_blob.o

# Budowanie Przeglądarki Hussar (Ring 3)
przegladarka_tmp.o: programy/przegladarka.cpp
	$(CXX) $(RING3_FLAGS) -c programy/przegladarka.cpp -o przegladarka_tmp.o

przegladarka_blob.o: przegladarka_tmp.o bursztyn_gui.o
	$(LD) -T programy/przegladarka_linker.ld -nostdlib -no-pie przegladarka_tmp.o bursztyn_gui.o -o przegladarka.elf
	$(OBJCOPY) -O binary przegladarka.elf przegladarka.bin
	$(LD) -r -b binary przegladarka.bin -o przegladarka_blob.o

# BUDOWANIE NOTATNIKA (RING 3)
notatnik_tmp.o: notatnik.cpp
	$(CXX) $(RING3_FLAGS) -c notatnik.cpp -o notatnik_tmp.o

notatnik_blob.o: notatnik_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie notatnik_tmp.o bursztyn_gui.o -o notatnik.elf
	$(OBJCOPY) -O binary notatnik.elf notatnik.bin
	$(LD) -r -b binary notatnik.bin -o notatnik_blob.o

# === BUDOWANIE KALKULATORA (RING 3) ===
kalkulator_tmp.o: kalkulator.cpp
	$(CXX) $(RING3_FLAGS) -c kalkulator.cpp -o kalkulator_tmp.o

kalkulator_blob.o: kalkulator_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie kalkulator_tmp.o bursztyn_gui.o -o kalkulator.elf
	$(OBJCOPY) -O binary kalkulator.elf kalkulator.bin
	$(LD) -r -b binary kalkulator.bin -o kalkulator_blob.o

# BUDOWANIE MENEDZERA OKIEN (RING 3)
menedzer_okien_tmp.o: menedzer_okien.cpp
	$(CXX) $(RING3_FLAGS) -c menedzer_okien.cpp -o menedzer_okien_tmp.o

menedzer_okien_blob.o: menedzer_okien_tmp.o bursztyn_gui.o
	$(LD) -T notatnik_linker.ld -nostdlib -no-pie menedzer_okien_tmp.o bursztyn_gui.o -o menedzer_okien.elf
	$(OBJCOPY) -O binary menedzer_okien.elf menedzer_okien.bin
	$(LD) -r -b binary menedzer_okien.bin -o menedzer_okien_blob.o

# === DODANIE PLIKU WAV BEZPOŚREDNIO DO JĄDRA ===
dzwiek.wav:
	@echo "UWAGA: Brak pliku dzwiek.wav! Generuje pusty plik (odegra sie dzwiek zastepczy)."
	touch dzwiek.wav

dzwiek_blob.o: dzwiek.wav
	$(LD) -r -b binary dzwiek.wav -o dzwiek_blob.o

# KONSOLIDACJA JĄDRA 
system_operacyjny.bin: $(OBJS) linker.ld
	$(CXX) -T linker.ld -nostdlib -no-pie -z noexecstack -o system_operacyjny.bin $(OBJS) -lgcc

# === BUDOWANIE OBRAZU ISO I KONFIGURACJA GRUB-A ===
iso: system_operacyjny.bin
	mkdir -p isodir/boot/grub
	cp system_operacyjny.bin isodir/boot/
	echo 'set timeout=0' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "Bursztyn OS" {' >> isodir/boot/grub/grub.cfg
	echo '    insmod all_video' >> isodir/boot/grub/grub.cfg
	echo '    set gfxmode=1024x768x32,auto' >> isodir/boot/grub/grub.cfg
	echo '    set gfxpayload=keep' >> isodir/boot/grub/grub.cfg
	echo '    multiboot2 /boot/system_operacyjny.bin' >> isodir/boot/grub/grub.cfg
	echo '    boot' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o BursztynOS.iso isodir --xorriso=xorriso

# === URUCHOMIENIE W QEMU (LINUX - ALSA + INTEL HDA) ===
run: iso
	if [ ! -f wirtualny_dysk.img ]; then qemu-img create -f raw wirtualny_dysk.img 40M; fi
	if [ -f tapeta.bmp ]; then dd if=tapeta.bmp of=wirtualny_dysk.img bs=512 seek=10 conv=notrunc; fi
	
	qemu-system-x86_64 \
		-accel tcg,thread=single \
		-cdrom BursztynOS.iso \
		-drive id=disk,file=wirtualny_dysk.img,format=raw,if=none \
		-device ahci,id=ahci \
		-device ide-hd,drive=disk,bus=ahci.0 \
		-netdev user,id=n1 \
		-device e1000,netdev=n1 \
		-audiodev alsa,id=snd \
		-device intel-hda \
		-device hda-output,audiodev=snd \
		-m 2G \
		-vga std \
		-serial stdio

# === CZYSZCZENIE PROJEKTU ===
clear:
	rm -f *.o *.bin *.elf *.iso
	rm -f biblioteki/mbedtls/library/*.o
	rm -rf isodir
cdysk:
	rm -rf wirtualny_dysk.img

rm:
	rm -f *.o *.bin *.elf *.iso
	rm -f biblioteki/mbedtls/library/*.o
	rm -rf wirtualny_dysk.img
	rm -rf isodir
