# ============================================================================
# Bursztyn OS - Makefile
# x86_64, BIOS/GRUB Multiboot2 + UEFI/OVMF/GRUB Multiboot2
#
# Najwazniejsze cele:
#
#   make / make all     - buduje kernel
#   make iso            - buduje hybrydowe ISO GRUB
#   make bios           - alias do budowania ISO dla uruchomienia BIOS
#   make uefi           - alias do budowania ISO dla uruchomienia UEFI
#   make run            - QEMU w trybie BIOS
#   make runuefi        - QEMU w trybie UEFI/OVMF
#
# Ten sam kernel Multiboot2 jest ladowany przez GRUB zarowno po starcie
# legacy BIOS, jak i po starcie GRUB-a z firmware UEFI.
# ============================================================================

.DELETE_ON_ERROR:

# ============================================================================
# 1. NARZEDZIA
# ============================================================================

CROSS ?= x86_64-linux-gnu-

CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
LD      := $(CROSS)ld
OBJCOPY := $(CROSS)objcopy
NM      := $(CROSS)nm
READELF := $(CROSS)readelf

QEMU     ?= qemu-system-x86_64
QEMU_IMG ?= qemu-img

GRUB_MKRESCUE ?= grub-mkrescue
GRUB_FILE     ?= grub-file
XORRISO       ?= xorriso

# ============================================================================
# 2. PLIKI / OBRAZY
# ============================================================================

KERNEL := system_operacyjny.bin
ISO    := BursztynOS.iso

ISO_DIR     := isodir
GRUB_CFG    := $(ISO_DIR)/boot/grub/grub.cfg
VIRTUAL_DISK := wirtualny_dysk.img

# ============================================================================
# 3. OVMF / UEFI
# ============================================================================
#
# Obslugujemy dwa popularne uklady pakietow:
#
#   1. monolityczne OVMF.fd -> QEMU -bios
#   2. OVMF_CODE.fd + OVMF_VARS.fd -> dwa urzadzenia pflash
#
# Sciezki mozna zawsze nadpisac:
#
#   make runuefi OVMF_MONO=/moja/sciezka/OVMF.fd
#
# albo:
#
#   make runuefi OVMF_CODE=/.../OVMF_CODE.fd OVMF_VARS=/.../OVMF_VARS.fd
#

OVMF_MONO ?= $(firstword \
	$(wildcard /usr/share/ovmf/OVMF.fd) \
	$(wildcard /usr/share/OVMF/OVMF.fd) \
	$(wildcard /usr/share/qemu/OVMF.fd))

OVMF_CODE ?= $(firstword \
	$(wildcard /usr/share/OVMF/OVMF_CODE.fd) \
	$(wildcard /usr/share/OVMF/OVMF_CODE_4M.fd) \
	$(wildcard /usr/share/edk2/x64/OVMF_CODE.fd) \
	$(wildcard /usr/share/edk2/ovmf/OVMF_CODE.fd))

OVMF_VARS ?= $(firstword \
	$(wildcard /usr/share/OVMF/OVMF_VARS.fd) \
	$(wildcard /usr/share/OVMF/OVMF_VARS_4M.fd) \
	$(wildcard /usr/share/edk2/x64/OVMF_VARS.fd) \
	$(wildcard /usr/share/edk2/ovmf/OVMF_VARS.fd))

OVMF_VARS_RUNTIME := .bursztyn_ovmf_vars.fd

# ============================================================================
# 4. FLAGI KOMPILACJI
# ============================================================================

CPU_FLAGS := \
	-mcmodel=large \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2

HARDEN_FLAGS := \
	-fno-stack-protector \
	-fcf-protection=none \
	-fno-pic \
	-fno-pie

FREESTANDING_FLAGS := \
	-ffreestanding \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables

WARN_CXX := \
	-Wall \
	-Wextra \
	-Wpedantic

WARN_C := \
	-Wall \
	-Wextra

MBEDTLS_INCLUDES := \
	-Ibiblioteki/mbedtls/include \
	-Ibiblioteki/mbedtls

MBEDTLS_CONFIG := \
	-DMBEDTLS_USER_CONFIG_FILE=\"bursztyn_mbedtls_config.h\"

KERNEL_CXXFLAGS := \
	-std=gnu++17 \
	-O2 \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_CXX) \
	-fno-exceptions \
	-fno-rtti \
	$(MBEDTLS_CONFIG) \
	$(MBEDTLS_INCLUDES)

MBEDTLS_CFLAGS := \
	-std=gnu11 \
	-O2 \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_C) \
	$(MBEDTLS_CONFIG) \
	$(MBEDTLS_INCLUDES)

RING3_FLAGS := \
	-std=gnu++17 \
	-O2 \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_CXX) \
	-fno-exceptions \
	-fno-rtti

#
# Pliki .S kompilujemy przez GCC, nie bezposrednio przez `as`.
# Dzieki temu zachowujemy prawidlowa obsluge assembler-with-cpp dla .S.
#
ASM_FLAGS := \
	-ffreestanding \
	-mno-red-zone \
	-fno-pic \
	-fno-pie

KERNEL_LDFLAGS := \
	-T linker.ld \
	-nostdlib \
	-no-pie \
	-static \
	-Wl,-z,noexecstack \
	-Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none

RING3_LDFLAGS := \
	-nostdlib \
	-no-pie

# ============================================================================
# 5. LINKERY APLIKACJI .bur
# ============================================================================

#
# Shell, Kalkulator i Menedzer Okien maja obecnie identyczny kontrakt BUR:
#
#   TEXT off 0x1000, size 0x8000, VA 0x601000
#   DATA off 0x9000, size 0x20000, VA 0x609000
#
# Dlatego korzystaja ze wspolnego, zweryfikowanego shell_linker.ld.
#
RING3_STD_LINKER := shell_linker.ld

NOTATNIK_LINKER := notatnik_linker.ld

PRZEGLADARKA_LINKER := programy/przegladarka_linker.ld

# ============================================================================
# 6. MBEDTLS
# ============================================================================

MBEDTLS_SRCS := $(wildcard biblioteki/mbedtls/library/*.c)
MBEDTLS_OBJS := $(MBEDTLS_SRCS:.c=.o)

# ============================================================================
# 7. OBIEKTY JADRA
# ============================================================================

OBJS := \
	boot.o \
	gdt.o \
	tss.o \
	acpi.o \
	hpet.o \
	dma.o \
	usb.o \
	xhci.o \
	xhci_ring.o \
	apic.o \
	idt.o \
	przerwania.o \
	e1000.o \
	siec.o \
	hda.o \
	klawiatura.o \
	przegladarka_blob.o \
	mysz.o \
	zegar-rtc.o \
	pmm.o \
	vmm.o \
	psf.o \
	grafika.o \
	syscall.o \
	syscalls.o \
	pci.o \
	ahci.o \
	ring3.o \
	notatnik_blob.o \
	eksplorator_blob.o \
	kalkulator_blob.o \
	loader.o \
	kernel.o \
	shell_blob.o \
	menedzer_okien_blob.o \
	uefi_gop.o \
	dzwiek_blob.o \
	cytaty_blob.o \
	heap.o \
	scheduler.o \
	skladacz_obrazu.o \
	mbedtls_port.o \
	tls.o \
	bezpieczenstwo.o \
	test_blob.o \
	$(MBEDTLS_OBJS)

# ============================================================================
# 8. CELE GLOWNE
# ============================================================================

.PHONY: \
	all kernel iso bios uefi run runusb runusbkbd runusbmouse runusbhid runuefi \
	check-kernel check-tools check-ovmf \
	prepare-disk clean clear distclean cdysk rm help

all: $(KERNEL)

kernel: $(KERNEL)

#
# `bios` i `uefi` buduja to samo hybrydowe ISO GRUB.
# Roznica jest w firmware QEMU podczas uruchamiania.
#
bios: iso

uefi: iso

# ============================================================================
# 9. REGULY OGOLNE
# ============================================================================

%.o: %.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(ASM_FLAGS) -c $< -o $@

biblioteki/mbedtls/library/%.o: biblioteki/mbedtls/library/%.c
	$(CC) $(MBEDTLS_CFLAGS) -c $< -o $@

# ============================================================================
# 10. OBIEKTY ZE SPECJALNYMI SCIEZKAMI
# ============================================================================

mbedtls_port.o: biblioteki/mbedtls/mbedtls_port.cpp biblioteki/mbedtls/bursztyn_mbedtls_config.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

hda.o: sterowniki/dzwiek/hda.cpp sterowniki/dzwiek/hda.h pamiec.h pci.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

uefi_gop.o: sterowniki/grafika/uefi_gop.cpp sterowniki/grafika/uefi_gop.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

hpet.o: sterowniki/czas/hpet.cpp sterowniki/czas/hpet.h acpi.h pamiec.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

dma.o: sterowniki/dma.cpp sterowniki/dma.h pamiec.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

usb.o: sterowniki/usb/usb.cpp sterowniki/usb/usb.h sterowniki/usb/xhci.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

xhci.o: sterowniki/usb/xhci.cpp sterowniki/usb/xhci.h sterowniki/usb/xhci_ring.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

xhci_ring.o: sterowniki/usb/xhci_ring.cpp sterowniki/usb/xhci_ring.h sterowniki/usb/xhci_trb.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

# ============================================================================
# 11. BIBLIOTEKA GUI RING 3
# ============================================================================

bursztyn_gui.o: bursztyn_gui.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

# ============================================================================
# 12. SHELL .bur
# ============================================================================

shell_tmp.o: shell.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

shell_blob.o: shell_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		shell_tmp.o bursztyn_gui.o -o shell.elf
	$(OBJCOPY) -O binary shell.elf shell.bin
	$(LD) -r -b binary shell.bin -o shell_blob.o

# ============================================================================
# 13. PRZEGLADARKA HUSSAR .bur
# ============================================================================

przegladarka_tmp.o: programy/przegladarka.cpp programy/http_kody.h bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

przegladarka_blob.o: przegladarka_tmp.o bursztyn_gui.o $(PRZEGLADARKA_LINKER)
	$(LD) -T $(PRZEGLADARKA_LINKER) $(RING3_LDFLAGS) \
		przegladarka_tmp.o bursztyn_gui.o -o przegladarka.elf
	$(OBJCOPY) -O binary przegladarka.elf przegladarka.bin
	$(LD) -r -b binary przegladarka.bin -o przegladarka_blob.o

# ============================================================================
# 14. NOTATNIK .bur
# ============================================================================

notatnik_tmp.o: notatnik.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

notatnik_blob.o: notatnik_tmp.o bursztyn_gui.o $(NOTATNIK_LINKER)
	$(LD) -T $(NOTATNIK_LINKER) $(RING3_LDFLAGS) \
		notatnik_tmp.o bursztyn_gui.o -o notatnik.elf
	$(OBJCOPY) -O binary notatnik.elf notatnik.bin
	$(LD) -r -b binary notatnik.bin -o notatnik_blob.o

# ============================================================================
# 15. EKSPLORATOR PLIKOW .bur
# ============================================================================

eksplorator_tmp.o: programy/eksplorator/eksplorator-plikow.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

eksplorator_blob.o: eksplorator_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		eksplorator_tmp.o bursztyn_gui.o -o eksplorator.elf
	$(OBJCOPY) -O binary eksplorator.elf eksplorator.bin
	$(LD) -r -b binary eksplorator.bin -o eksplorator_blob.o

# ============================================================================
# 16. KALKULATOR .bur
# ============================================================================

kalkulator_tmp.o: kalkulator.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

kalkulator_blob.o: kalkulator_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		kalkulator_tmp.o bursztyn_gui.o -o kalkulator.elf
	$(OBJCOPY) -O binary kalkulator.elf kalkulator.bin
	$(LD) -r -b binary kalkulator.bin -o kalkulator_blob.o

# Test
test_tmp.o: test.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

test_blob.o: test_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		test_tmp.o bursztyn_gui.o -o test.elf
	$(OBJCOPY) -O binary test.elf test.bin
	$(LD) -r -b binary test.bin -o test_blob.o

# ============================================================================
# 16. MENEDZER OKIEN .bur
# ============================================================================

menedzer_okien_tmp.o: menedzer_okien.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

menedzer_okien_blob.o: menedzer_okien_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		menedzer_okien_tmp.o bursztyn_gui.o -o menedzer_okien.elf
	$(OBJCOPY) -O binary menedzer_okien.elf menedzer_okien.bin
	$(LD) -r -b binary menedzer_okien.bin -o menedzer_okien_blob.o

# ============================================================================
# 17. DZWIEK WAV OSADZONY W JADRZE
# ============================================================================

dzwiek.wav:
	@echo "UWAGA: brak dzwiek.wav - tworze pusty plik zastepczy."
	@touch $@

dzwiek_blob.o: dzwiek.wav
	$(LD) -r -b binary $< -o $@

cytaty_blob.o: cytaty.txt
	$(LD) -r -b binary $< -o $@

# ============================================================================
# 18. LINKOWANIE JADRA
# ============================================================================

$(KERNEL): $(OBJS) linker.ld
	$(CXX) $(KERNEL_LDFLAGS) -o $@ $(OBJS) -lgcc

# ============================================================================
# 19. KONTROLA MULTIBOOT2
# ============================================================================

check-kernel: $(KERNEL)
	@if command -v $(GRUB_FILE) >/dev/null 2>&1; then \
		if $(GRUB_FILE) --is-x86-multiboot2 $(KERNEL); then \
			echo "[OK] $(KERNEL) jest poprawnym obrazem Multiboot2."; \
		else \
			echo "[BLAD] GRUB nie rozpoznaje $(KERNEL) jako Multiboot2."; \
			exit 1; \
		fi; \
	else \
		echo "[UWAGA] Brak grub-file - pomijam dodatkowa walidacje Multiboot2."; \
	fi

# ============================================================================
# 20. ISO GRUB - BIOS + UEFI
# ============================================================================

$(GRUB_CFG): $(KERNEL)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL) $(ISO_DIR)/boot/
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin' \
		'    boot' \
		'}' \
		> $(GRUB_CFG)

$(ISO): check-kernel $(GRUB_CFG)
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR) --xorriso=$(XORRISO)

iso: $(ISO)

# ============================================================================
# 21. DYSK TESTOWY
# ============================================================================

$(VIRTUAL_DISK):
	$(QEMU_IMG) create -f raw $@ 40M

prepare-disk: $(VIRTUAL_DISK)
	@if [ -f tapeta.bmp ]; then \
		echo "[QEMU] Kopiuje tapeta.bmp do sektora 10 dysku testowego."; \
		dd if=tapeta.bmp of=$(VIRTUAL_DISK) bs=512 seek=10 conv=notrunc status=none; \
	fi

# ============================================================================
# 22. QEMU - WSPOLNE OPCJE
# ============================================================================

QEMU_COMMON_ARGS := \
	-machine pc,hpet=on \
	-cpu max \
	-accel tcg,thread=single \
	-drive id=disk,file=$(VIRTUAL_DISK),format=raw,if=none \
	-device ich9-ahci,id=ahci \
	-device ide-hd,drive=disk,bus=ahci.0,unit=0 \
	-netdev user,id=n1 \
	-device e1000,netdev=n1 \
	-audiodev alsa,id=snd \
	-device intel-hda \
	-device hda-output,audiodev=snd \
	-m 2G \
	-vga std \
	-serial stdio

# ============================================================================
# 23. RUN - LEGACY BIOS
# ============================================================================

run: iso prepare-disk
	$(QEMU) \
		$(QEMU_COMMON_ARGS) \
		-cdrom $(ISO)

runusb: iso prepare-disk
	$(QEMU) \
		$(QEMU_COMMON_ARGS) \
		-device qemu-xhci,id=xhci \
		-cdrom $(ISO)

runusbkbd: iso prepare-disk
	$(QEMU) \
		$(QEMU_COMMON_ARGS) \
		-device qemu-xhci,id=xhci \
		-device usb-kbd,id=usbkbd,bus=xhci.0 \
		-cdrom $(ISO)

runusbmouse: iso prepare-disk
	$(QEMU) \
		$(QEMU_COMMON_ARGS) \
		-device qemu-xhci,id=xhci \
		-device usb-mouse,id=usbmouse,bus=xhci.0 \
		-cdrom $(ISO)

runusbhid: iso prepare-disk
	$(QEMU) \
		$(QEMU_COMMON_ARGS) \
		-device qemu-xhci,id=xhci \
		-device usb-kbd,id=usbkbd,bus=xhci.0 \
		-device usb-mouse,id=usbmouse,bus=xhci.0 \
		-cdrom $(ISO)

# ============================================================================
# 24. RUNUEFI - UEFI / OVMF
# ============================================================================

check-ovmf:
	@if [ -n "$(OVMF_MONO)" ]; then \
		echo "[OK] OVMF monolityczne: $(OVMF_MONO)"; \
	elif [ -n "$(OVMF_CODE)" ] && [ -n "$(OVMF_VARS)" ]; then \
		echo "[OK] OVMF CODE: $(OVMF_CODE)"; \
		echo "[OK] OVMF VARS: $(OVMF_VARS)"; \
	else \
		echo "[BLAD] Nie znaleziono kompletnego firmware OVMF."; \
		echo "Zainstaluj pakiet ovmf albo podaj OVMF_MONO=..."; \
		echo "ewentualnie OVMF_CODE=... OVMF_VARS=..."; \
		exit 1; \
	fi

runuefi: uefi prepare-disk check-ovmf
	@if [ -n "$(OVMF_MONO)" ]; then \
		echo "[QEMU] Start Bursztyn OS przez UEFI/OVMF (-bios)."; \
		exec $(QEMU) \
			-bios "$(OVMF_MONO)" \
			$(QEMU_COMMON_ARGS) \
			-cdrom $(ISO); \
	else \
		echo "[QEMU] Start Bursztyn OS przez UEFI/OVMF (pflash CODE+VARS)."; \
		cp "$(OVMF_VARS)" "$(OVMF_VARS_RUNTIME)"; \
		exec $(QEMU) \
			-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
			-drive if=pflash,format=raw,file="$(OVMF_VARS_RUNTIME)" \
			$(QEMU_COMMON_ARGS) \
			-cdrom $(ISO); \
	fi

# ============================================================================
# 25. KONTROLA NARZEDZI
# ============================================================================

check-tools:
	@set -e; \
	for narzedzie in \
		$(CC) $(CXX) $(LD) $(OBJCOPY) \
		$(QEMU) $(QEMU_IMG) $(GRUB_MKRESCUE) $(XORRISO); \
	do \
		if ! command -v $$narzedzie >/dev/null 2>&1; then \
			echo "[BRAK] $$narzedzie"; \
			exit 1; \
		fi; \
	done; \
	echo "[OK] Podstawowe narzedzia sa dostepne."

# ============================================================================
# 26. CZYSZCZENIE
# ============================================================================

clean:
	rm -f *.o *.bin *.elf *.iso
	rm -f biblioteki/mbedtls/library/*.o
	rm -f $(OVMF_VARS_RUNTIME)
	rm -rf $(ISO_DIR)

#
# Zachowanie starej nazwy `clear`.
#
clear: clean

cdysk:
	rm -f $(VIRTUAL_DISK)

distclean: clean cdysk

#
# Zachowanie starego `make rm`, ale teraz jako alias pelnego czyszczenia.
#
rm: distclean

# ============================================================================
# 27. POMOC
# ============================================================================

help:
	@echo "Bursztyn OS - cele Makefile:"
	@echo "  make all       - zbuduj kernel"
	@echo "  make iso       - zbuduj hybrydowe ISO GRUB"
	@echo "  make bios      - zbuduj ISO do startu BIOS"
	@echo "  make uefi      - zbuduj ISO do startu UEFI"
	@echo "  make run       - uruchom QEMU w trybie BIOS"
	@echo "  make runuefi   - uruchom QEMU z OVMF/UEFI"
	@echo "  make check-tools - sprawdz wymagane narzedzia"
	@echo "  make clean     - usun artefakty kompilacji"
	@echo "  make cdysk     - usun wirtualny dysk"
	@echo "  make distclean - clean + cdysk"
