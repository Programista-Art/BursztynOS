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
#   make run            - QEMU BIOS + VirtIO Full (domyslnie)
#   make runuefi        - QEMU UEFI/OVMF + VirtIO Full (domyslnie)
#   make run-legacy     - QEMU BIOS bez VirtIO (AHCI/E1000/STD VGA/PS2)
#   make runuefi-legacy - QEMU UEFI bez VirtIO (AHCI/E1000/GOP-STD VGA/PS2)
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
AS      := $(CROSS)as
PYTHON  ?= python3

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
ISO8K  := BursztynOS_8k.iso
TAPETY_PAK := tapety.pak
TAPETY_DIR ?= obrazy/tapety
BURSZTYN_TAPETA_DOMYSLNA ?=
TAPETY_SOURCES_MANIFEST := .tapety-sources.manifest
TAPETY_ISO_CHECK := .tapety-iso-check.pak
GRUB_ISO_CHECK := .grub-iso-check.cfg

ISO_DIR     := isodir
ISO8K_DIR   := isodir_8k
GRUB_CFG    := $(ISO_DIR)/boot/grub/grub.cfg
GRUB8K_CFG  := $(ISO8K_DIR)/boot/grub/grub.cfg
VIRTUAL_DISK ?= $(if $(VIRTUALDISK),$(if $(wildcard $(VIRTUALDISK)),$(VIRTUALDISK),$(if $(wildcard $(subst wirtualnydysk,wirtualny_dysk,$(VIRTUALDISK))),$(subst wirtualnydysk,wirtualny_dysk,$(VIRTUALDISK)),$(VIRTUALDISK))),wirtualny_dysk.img)

CI_TEST_DIR := .isodir_ci_test
CI_TEST_ISO := .bursztyn_ci_test.iso
CI_BLK_DIR  := .isodir_ci_blk_destructive
CI_BLK_DESTRUCTIVE_ISO := .bursztyn_ci_blk_destructive.iso
CI_DEST_ONLY_DIR := .isodir_ci_dest_only
CI_DESTRUCTIVE_ONLY_ISO := .bursztyn_ci_destructive_only.iso

LARGE_TEST_BMP ?= $(firstword $(wildcard obrazy/test8k.bmp) $(wildcard obrazy/2test.bmp) obrazy/test8k.bmp)

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
	-O2 -g \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_CXX) \
	-fno-exceptions \
	-fno-rtti \
	-fstack-usage \
	$(MBEDTLS_CONFIG) \
	$(MBEDTLS_INCLUDES)

MBEDTLS_CFLAGS := \
	-std=gnu11 \
	-O2 \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_C) \
	-fstack-usage \
	$(MBEDTLS_CONFIG) \
	$(MBEDTLS_INCLUDES)

RING3_FLAGS := \
	-std=gnu++17 \
	-O2 \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_CXX) \
	-ffunction-sections \
	-fdata-sections \
	-fno-exceptions \
	-fno-rtti \
	-fno-threadsafe-statics

BURSZTYN_DIAG_GEOM_TRACE ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_GEOM_TRACE) $(GEOM_TRACE)),)
RING3_FLAGS += -DBURSZTYN_DIAG_GEOM_TRACE=1
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_GEOM_TRACE=1
endif

# Telemetria startu / WM-BOOT / BOOT-TELEMETRY. Domyslnie wylaczona.
BURSZTYN_DIAG_BOOT ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_BOOT)),)
RING3_FLAGS += -DBURSZTYN_DIAG_BOOT=1
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_BOOT=1
endif

# Szczegolowe logi warstw kompozytora BWS. Domyslnie wylaczone.
BURSZTYN_DEBUG_GUI_LAYERS ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DEBUG_GUI_LAYERS)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DEBUG_GUI_LAYERS=1
endif

# Diagnostyczny 16 KiB stos jadra (reprodukcja historycznego overflow).
# Wylacznie do kontrolowanych testow: make iso BURSZTYN_DIAG_STOS16K=1
BURSZTYN_DIAG_STOS16K ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_STOS16K)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_STOS16K=1
endif

# Opcjonalne verbose logi OTA trace i stack metrics (domyslnie wylaczone w wydaniu)
BURSZTYN_OTA_TRACE ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_OTA_TRACE)),)
KERNEL_CXXFLAGS += -DBURSZTYN_OTA_TRACE=1
RING3_FLAGS += -DBURSZTYN_OTA_TRACE=1
endif

BURSZTYN_STACK_TRACE ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_STACK_TRACE)),)
KERNEL_CXXFLAGS += -DBURSZTYN_STACK_TRACE=1
endif

# Bounded early PCI/VirtIO probe tracing. Disabled in production builds.
BURSZTYN_DIAG_PCI_TRACE ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_PCI_TRACE)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_PCI_TRACE=1
endif

# Dokladne znaczniki diagnostyczne wczesnego bootu VirtIO/PCI.  Tryb jest
# wylaczony w obrazie produkcyjnym i sluzy wylacznie do odtworzenia zawieszen.
BURSZTYN_DIAG_VIRTIO_BOOT ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_VIRTIO_BOOT)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_VIRTIO_BOOT=1
endif

BURSZTYN_DIAG_TAPETY ?= 0
BURSZTYNDIAGTAPETY ?= $(BURSZTYN_DIAG_TAPETY)
ifneq ($(filter 1 yes true,$(BURSZTYNDIAGTAPETY) $(BURSZTYN_DIAG_TAPETY)),)
RING3_FLAGS += -DBURSZTYNDIAGTAPETY=1 -DBURSZTYN_DIAG_TAPETY=1
KERNEL_CXXFLAGS += -DBURSZTYNDIAGTAPETY=1 -DBURSZTYN_DIAG_TAPETY=1
endif
BURSZTYNDIAGTAPETYPERF ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYNDIAGTAPETYPERF)),)
RING3_FLAGS += -DBURSZTYNDIAGTAPETYPERF=1
KERNEL_CXXFLAGS += -DBURSZTYNDIAGTAPETYPERF=1
endif

BURSZTYNDIAGREBOOT ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYNDIAGREBOOT) $(BURSZTYN_DIAG_REBOOT)),)
RING3_FLAGS += -DBURSZTYNDIAGREBOOT=1
KERNEL_CXXFLAGS += -DBURSZTYNDIAGREBOOT=1
endif

# Szczegolowe logi kazdego zdarzenia wskaznika. Domyslnie wylaczone;
# jednorazowe logi rejestracji urzadzenia i zmiany wlasciciela pozostaja aktywne.
BURSZTYNDIAGINPUTPOINTER ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYNDIAGINPUTPOINTER)),)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGINPUTPOINTER=1
endif

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
	-no-pie \
	--gc-sections

# ============================================================================
# 5. LINKERY APLIKACJI .bur
# ============================================================================

#
# Shell, Kalkulator i Menedzer Okien maja obecnie identyczny kontrakt BUR:
#
#   TEXT off 0x1000, size 0x8000, VA 0x1001000
#   DATA off 0x9000, size 0x20000, VA 0x1009000
#
# Dlatego korzystaja ze wspolnego, zweryfikowanego shell_linker.ld.
#
RING3_STD_LINKER := shell_linker.ld

NOTATNIK_LINKER := notatnik_linker.ld

KALKULATOR_LINKER := kalkulator_linker.ld

EKSPLORATOR_LINKER := programy/eksplorator/eksplorator_linker.ld

PRZEGLADARKA_LINKER := programy/przegladarka_linker.ld

AKTUALIZACJE_LINKER := programy/aktualizacje/aktualizacje_linker.ld

MOTYWY_LINKER := programy/motywy/motywy_linker.ld

MENEDZER_OKIEN_LINKER := menedzer_okien_linker.ld

# ============================================================================
# 6. MBEDTLS
# ============================================================================

MBEDTLS_SRCS := $(wildcard biblioteki/mbedtls/library/*.c)
MBEDTLS_OBJS := $(MBEDTLS_SRCS:.c=.o)

# ============================================================================
# 7. OBIEKTY JADRA
# ============================================================================

OBJS := instalator.o instalator_blob.o storage_format.o blokowy.o ekran_blob.o ekran.o ekran_format.o bufory_grafiki.o bochs_vbe.o tapety_blob.o tapety.o btp.o \
	boot.o \
	gdt.o \
	tss.o \
	acpi.o \
	hpet.o \
	dma.o \
	usb.o \
	xhci.o \
	xhci_ring.o \
	usb_hid.o \
	hid_pointer.o \
	ohci.o \
	apic.o \
	idt.o \
	przerwania.o \
	network_device.o \
	e1000.o \
	siec.o \
	hda.o \
	hda_diag.o \
	hda_transport.o \
	klawiatura.o \
	przegladarka_blob.o \
	husarz_worker_blob.o \
	mysz.o \
	zegar-rtc.o \
	pmm.o \
	vmm.o \
	psf.o \
	grafika.o \
	ekran_powitalny.o \
	syscall.o \
	syscalls.o \
	bws_nowoczesne.o \
	pci.o \
	virtio_pci.o \
	virtqueue.o \
	virtio_rng.o \
	virtio_blk.o \
	virtio_net.o \
	virtio_gpu.o \
	virtio_input.o \
	input_device.o \
	ahci.o \
	ring3.o \
	stos_jadra.o \
	notatnik_blob.o \
	eksplorator_blob.o \
	kalkulator_blob.o \
	loader.o \
	czcionka_systemowa.o \
	kernel.o \
	shell_blob.o \
	menedzer_okien_blob.o \
	uefi_gop.o \
	dzwiek_blob.o \
	cytaty_blob.o \
	sponsorzy_blob.o \
	heap.o \
	scheduler.o \
	skladacz_obrazu.o \
 mbedtls_port.o \
 tls.o \
 bezpieczenstwo.o \
 aktualizacja.o \
	test_blob.o \
	demo_ui_blob.o \
	kontrolki2_blob.o \
	kontrolki3blob.o \
	kontrolki4blob.o \
	repro_blob.o \
	snake_blob.o \
	przegladarka_obrazow_blob.o \
	aktualizacje_blob.o \
	motyw.o \
	motywy_blob.o \
	test_dzwieku_blob.o \
	diagnostyka_systemu.o diagnostyka_systemu_bws.o diagnostyka_systemu_boot.o diagnostyka_blob.o \
	test_bmp_blob.o \
	test1_bmp_blob.o \
	logo_png_blob.o \
	$(MBEDTLS_OBJS)

# ============================================================================
# 8. CELE GLOWNE
# ============================================================================

.PHONY: \
	all kernel iso iso8k bios uefi \
	run runuefi run-legacy runuefi-legacy run-installed run-installed-legacy \
	run8k run-test run-geom-trace runusb runusbkbd runusbmouse runusbhid \
	check-kernel check-test8k check-tools check-ovmf \
	prepare-disk clean clear distclean cdysk rm help

all: $(KERNEL) $(TAPETY_PAK)

kernel: $(KERNEL)

#
# `bios` i `uefi` buduja to samo hybrydowe ISO GRUB.
# Roznica jest w firmware QEMU podczas uruchamiania.
#
bios: iso

uefi: iso

# Compiler-generated header dependencies cover PSF, kernel and embedded Ring3.
BURSZTYNDIAGVIRTIOINPUTINIT ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYNDIAGVIRTIOINPUTINIT)),)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGVIRTIOINPUTINIT=1
endif

KERNEL_CXXFLAGS += -MMD -MP
RING3_FLAGS += -MMD -MP
MBEDTLS_CFLAGS += -MMD -MP

# Targeted base-tree diagnostics only; no normal filesystem-operation logging.
BURSZTYNDIAGPSFBOOTSTRAP ?= 0
ifeq ($(BURSZTYNDIAGPSFBOOTSTRAP),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGPSFBOOTSTRAP=1
endif

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

# ============================================================================
# KONFIGURACJA KLUCZA AKTUALIZACJI
# ============================================================================
# Domyslnie kompilujemy z kluczem wydania (release).
# Opcje: release, test, dev (z BURSZTYN_AKT_KLUCZ_PUBLICZNY=<plik.pem>)
BURSZTYN_AKT_TRUST_ROOT ?= release

.PHONY: .aktualizacja_klucz_check
.aktualizacja_klucz_check:
	@$(PYTHON) narzedzia/aktualizacje/generuj_naglowek_klucza.py --tryb $(BURSZTYN_AKT_TRUST_ROOT) $(if $(BURSZTYN_AKT_KLUCZ_PUBLICZNY),--publiczny $(BURSZTYN_AKT_KLUCZ_PUBLICZNY),) --wyjscie aktualizacja_klucz.h

aktualizacja_klucz.h: .aktualizacja_klucz_check

aktualizacja.o: aktualizacja.cpp aktualizacja.h aktualizacja_format.h aktualizacja_klucz.h .aktualizacja_klucz_check
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

mbedtls_port.o: biblioteki/mbedtls/mbedtls_port.cpp biblioteki/mbedtls/bursztyn_mbedtls_config.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

# Bounded resource tables in the modern BWS layer are intentionally dynamic;
# optimize its dispatcher for size so the fixed pre-Ring3 kernel VA contract
# (kernel <= 14 MiB with >= 2 MiB guard below RING3_BASE at 16 MiB) remains intact.
bws_nowoczesne.o: bws_nowoczesne.cpp bws_nowoczesne.h bws_api.h bws_hda_diag.h sterowniki/dzwiek/hda.h sterowniki/dzwiek/hda_diag.h sterowniki/dzwiek/hda_diag_format.h
	$(CXX) $(KERNEL_CXXFLAGS) -Os -c $< -o $@

syscalls.o: syscalls.cpp bws_api.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

hda.o: sterowniki/dzwiek/hda.cpp sterowniki/dzwiek/hda_transport.h sterowniki/dzwiek/hda.h sterowniki/dzwiek/hda_wewnetrzny.h sterowniki/dzwiek/hda_diag_format.h pamiec.h pci.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

hda_transport.o: sterowniki/dzwiek/hda_transport.cpp sterowniki/dzwiek/hda_transport.h sterowniki/dzwiek/hda_diag_format.h pamiec.h pci.h sterowniki/dma.h sterowniki/czas/hpet.h
	$(CXX) $(KERNEL_CXXFLAGS) -c sterowniki/dzwiek/hda_transport.cpp -o $@

hda_diag.o: sterowniki/dzwiek/hda_diag.cpp sterowniki/dzwiek/hda_transport.h sterowniki/dzwiek/hda_diag.h sterowniki/dzwiek/hda_diag_format.h sterowniki/dzwiek/hda_wewnetrzny.h sterowniki/dzwiek/hda.h
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

usb_hid.o: sterowniki/usb/usb_hid.cpp sterowniki/usb/usb_hid.h input_device.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

# ============================================================================
# 11. BIBLIOTEKA GUI RING 3
# ============================================================================

bursztyn_gui.o: bursztyn_gui.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@

# ============================================================================
# 12. SHELL .bur
# ============================================================================

shell_tmp.o: shell.cpp shell_terminal.h shell_context.h shell_sponsors.h wersja_systemu.h bursztyn_gui.h bursztyn_ui/czcionka.h bursztyn_ui/rysowanie.h bursztyn_ui/pasek_przewijania.h bursztyn_ui/menu_kontekstowe.h
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

.PHONY: shellblob.o
shellblob.o: shell_blob.o

shell_blob.o: shell_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		shell_tmp.o bursztyn_gui.o -o shell.elf
	$(OBJCOPY) -O binary shell.elf shell.bin
	$(LD) -r -b binary shell.bin -o shell_blob.o

# ============================================================================
# 13. PRZEGLADARKA HUSSAR .bur
# ============================================================================

HUSARZ_DEBUG ?= 0
HUSARZ_PERF ?= 0
.PHONY: husarz-build-config-force
.husarz-test/build-config: husarz-build-config-force
	@mkdir -p .husarz-test
	@$(PYTHON) -c "from pathlib import Path; p=Path('$@'); v='debug=$(HUSARZ_DEBUG) perf=$(HUSARZ_PERF)'; p.write_text(v) if not p.exists() or p.read_text()!=v else None"
przegladarka_tmp.o husarz_worker_tmp.o: .husarz-test/build-config
przegladarka_tmp.o: programy/przegladarka.cpp Makefile programy/http_kody.h bursztyn_gui.h $(wildcard bursztyn_ui/*.h) $(wildcard programy/husarz/*.h) $(wildcard programy/husarz/*.inc)
	$(CXX) $(RING3_FLAGS) -fcoroutines -fcheck-new -DHUSARZ_DEBUG_RESOURCES=$(HUSARZ_DEBUG) -DHUSARZ_PERF=$(HUSARZ_PERF) -I. -Os -fstack-usage -ffunction-sections -fdata-sections -c $< -o $@

przegladarka_blob.o: przegladarka_tmp.o bursztyn_gui.o image_ring3.o btp_ring3.o $(PRZEGLADARKA_LINKER)
	$(LD) --gc-sections -T $(PRZEGLADARKA_LINKER) $(RING3_LDFLAGS) \
		przegladarka_tmp.o bursztyn_gui.o image_ring3.o btp_ring3.o -o przegladarka.elf
	$(OBJCOPY) -O binary przegladarka.elf przegladarka.bin
	$(LD) -r -b binary przegladarka.bin -o przegladarka_blob.o

husarz_worker_tmp.o: programy/przegladarka.cpp Makefile $(wildcard programy/husarz/*.h) $(wildcard programy/husarz/*.inc)
	$(CXX) $(RING3_FLAGS) -fcoroutines -fcheck-new -DHUSARZ_DEBUG_RESOURCES=$(HUSARZ_DEBUG) -DHUSARZ_PERF=$(HUSARZ_PERF) -DHUSARZ_WORKER_ONLY=1 -Wno-unused-function -I. -Os -fstack-usage -ffunction-sections -fdata-sections -c $< -o $@

husarz_worker_blob.o: husarz_worker_tmp.o bursztyn_gui.o image_ring3.o btp_ring3.o $(PRZEGLADARKA_LINKER)
	$(LD) --gc-sections -T $(PRZEGLADARKA_LINKER) $(RING3_LDFLAGS) husarz_worker_tmp.o bursztyn_gui.o image_ring3.o btp_ring3.o -o husarz_worker.elf
	$(OBJCOPY) -O binary husarz_worker.elf husarz_worker.bin
	$(LD) -r -b binary husarz_worker.bin -o husarz_worker_blob.o

# ============================================================================
# 14. NOTATNIK .bur
# ============================================================================

notatnik_tmp.o: notatnik.cpp bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

notatnik_blob.o: notatnik_tmp.o bursztyn_gui.o $(NOTATNIK_LINKER)
	$(LD) --gc-sections -T $(NOTATNIK_LINKER) $(RING3_LDFLAGS) \
		notatnik_tmp.o bursztyn_gui.o -o notatnik.elf
	$(OBJCOPY) -O binary notatnik.elf notatnik.bin
	$(LD) -r -b binary notatnik.bin -o notatnik_blob.o

# ============================================================================
# 15. EKSPLORATOR PLIKOW .bur
# ============================================================================

eksplorator_tmp.o: programy/eksplorator/eksplorator-plikow.cpp desktop_entries.h bursztyn_gui.h plik_popup_ui.h plik_operacje_ui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

eksplorator_blob.o: eksplorator_tmp.o bursztyn_gui.o $(EKSPLORATOR_LINKER)
	$(LD) -T $(EKSPLORATOR_LINKER) $(RING3_LDFLAGS) \
		eksplorator_tmp.o bursztyn_gui.o -o eksplorator.elf
	$(OBJCOPY) -O binary eksplorator.elf eksplorator.bin
	$(LD) -r -b binary eksplorator.bin -o eksplorator_blob.o

# ============================================================================
# 16. KALKULATOR .bur
# ============================================================================

kalkulator_tmp.o: kalkulator.cpp bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

kalkulator_blob.o: kalkulator_tmp.o bursztyn_gui.o $(KALKULATOR_LINKER)
	$(LD) --gc-sections -T $(KALKULATOR_LINKER) $(RING3_LDFLAGS) \
		kalkulator_tmp.o bursztyn_gui.o -o kalkulator.elf
	$(OBJCOPY) -O binary kalkulator.elf kalkulator.bin
	$(LD) -r -b binary kalkulator.bin -o kalkulator_blob.o

# Test
test_tmp.o: programy/test/test.cpp bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

test_blob.o: test_tmp.o ekran_format_ring3.o btp_ring3.o bursztyn_gui.o programy/test/test_linker.ld
	$(LD) --gc-sections -T programy/test/test_linker.ld $(RING3_LDFLAGS) \
		test_tmp.o ekran_format_ring3.o btp_ring3.o bursztyn_gui.o -o test.elf
	$(OBJCOPY) -O binary test.elf test.bin
	$(LD) -r -b binary test.bin -o test_blob.o

# BursztynUI Demo
demo_ui_tmp.o: programy/demo_ui/demo_ui.cpp bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@

demo_ui_blob.o: demo_ui_tmp.o bursztyn_gui.o programy/demo_ui/demo_ui_linker.ld
	$(LD) --gc-sections -T programy/demo_ui/demo_ui_linker.ld $(RING3_LDFLAGS) \
		demo_ui_tmp.o bursztyn_gui.o -o demo_ui.elf
	$(OBJCOPY) -O binary demo_ui.elf demo_ui.bin
	$(LD) -r -b binary demo_ui.bin -o demo_ui_blob.o

# BursztynUI Kontrolki 2
kontrolki2_tmp.o: programy/kontrolki2/kontrolki2.cpp bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@

kontrolki2_blob.o: kontrolki2_tmp.o bursztyn_gui.o programy/demo_ui/demo_ui_linker.ld
	$(LD) --gc-sections -T programy/demo_ui/demo_ui_linker.ld $(RING3_LDFLAGS) \
		kontrolki2_tmp.o bursztyn_gui.o -o kontrolki2.elf
	$(OBJCOPY) -O binary kontrolki2.elf kontrolki2.bin
	$(LD) -r -b binary kontrolki2.bin -o kontrolki2_blob.o

# BursztynUI Kontrolki 3. Zaleznosc wildcard wymusza przebudowe aplikacji
# po zmianie dowolnej publicznej kontrolki; analogiczna zaleznosc test_tmp.o
# znajduje sie powyzej.
kontrolki3tmp.o: programy/kontrolki3/kontrolki3.cpp bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

kontrolki3blob.o: kontrolki3tmp.o bursztyn_gui.o programy/kontrolki3/kontrolki3_linker.ld
	$(LD) --gc-sections -T programy/kontrolki3/kontrolki3_linker.ld $(RING3_LDFLAGS) \
		kontrolki3tmp.o bursztyn_gui.o -o kontrolki3.elf
	$(OBJCOPY) -O binary kontrolki3.elf kontrolki3.bin
	$(LD) -r -b binary kontrolki3.bin -o kontrolki3blob.o

# BursztynUI Kontrolki 4 - Showroom nowych komponentow retained-mode.
kontrolki4tmp.o: programy/kontrolki4/kontrolki4.cpp bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

kontrolki4blob.o: kontrolki4tmp.o bursztyn_gui.o programy/kontrolki4/kontrolki4_linker.ld
	$(LD) --gc-sections -T programy/kontrolki4/kontrolki4_linker.ld $(RING3_LDFLAGS) \
		kontrolki4tmp.o bursztyn_gui.o -o kontrolki4.elf
	$(OBJCOPY) -O binary kontrolki4.elf kontrolki4.bin
	$(LD) -r -b binary kontrolki4.bin -o kontrolki4blob.o

# Obraz testowy test.bmp	
test_bmp_blob.o: obrazy/test.bmp
	$(LD) -r -b binary obrazy/test.bmp -o test_bmp_blob.o

# Drugi obraz testowy
test1_bmp_blob.o: obrazy/test1.bmp
	$(LD) -r -b binary obrazy/test1.bmp -o test1_bmp_blob.o

# Logo systemu logo.png
logo_png_blob.o: obrazy/logo.png
	$(LD) -r -b binary obrazy/logo.png -o logo_png_blob.o




# Snake
snake_tmp.o: gry/snake/snake.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

snake_blob.o: snake_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		snake_tmp.o bursztyn_gui.o -o snake.elf
	$(OBJCOPY) -O binary snake.elf snake.bin
	$(LD) -r -b binary snake.bin -o snake_blob.o

# Przegladarka Obrazow
przegladarka_obrazow_tmp.o: programy/przegladarka-obrazow/przegladarka-obrazow.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -Os -c $< -o $@

przegladarka_obrazow_blob.o: przegladarka_obrazow_tmp.o bursztyn_gui.o $(RING3_STD_LINKER)
	$(LD) -T $(RING3_STD_LINKER) $(RING3_LDFLAGS) \
		przegladarka_obrazow_tmp.o bursztyn_gui.o -o przegladarka_obrazow.elf
	$(OBJCOPY) -O binary przegladarka_obrazow.elf przegladarka_obrazow.bin
	$(LD) -r -b binary przegladarka_obrazow.bin -o przegladarka_obrazow_blob.o

# Aktualizacje Systemu
aktualizacje_tmp.o: programy/aktualizacje/aktualizacje.cpp bursztyn_gui.h bws_ota.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -c $< -o $@

aktualizacje_blob.o: aktualizacje_tmp.o bursztyn_gui.o $(AKTUALIZACJE_LINKER)
	$(LD) -T $(AKTUALIZACJE_LINKER) $(RING3_LDFLAGS) \
		aktualizacje_tmp.o bursztyn_gui.o -o aktualizacje.elf
	$(OBJCOPY) -O binary aktualizacje.elf aktualizacje.bin
	$(LD) -r -b binary aktualizacje.bin -o aktualizacje_blob.o

# Motywy Systemowe
motywy_tmp.o: programy/motywy/motywy.cpp bursztyn_gui.h motyw.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -c $< -o $@

motywy_blob.o: motywy_tmp.o bursztyn_gui.o $(MOTYWY_LINKER)
	$(LD) -T $(MOTYWY_LINKER) $(RING3_LDFLAGS) \
		motywy_tmp.o bursztyn_gui.o -o motywy.elf
	$(OBJCOPY) -O binary motywy.elf motywy.bin
	$(LD) -r -b binary motywy.bin -o motywy_blob.o

# Test dźwięku — diagnostyka HDA
BURSZTYNDIAGTESTDZWIEKU ?= 0
ifeq ($(BURSZTYNDIAGTESTDZWIEKU),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGTESTDZWIEKU=1
RING3_FLAGS += -DBURSZTYNDIAGTESTDZWIEKU=1
endif

test_dzwieku_tmp.o: programy/test_dzwieku/test_dzwieku.cpp bursztyn_gui.h bws_hda_diag.h bws_czcionka.h test_dzwieku_trace.h \
		sterowniki/dzwiek/hda_diag_format.h wersja_systemu.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

test_dzwieku_blob.o: test_dzwieku_tmp.o bursztyn_gui.o programy/test_dzwieku/test_dzwieku_linker.ld narzedzia/test_dzwieku_size.py
	$(LD) --orphan-handling=error -T programy/test_dzwieku/test_dzwieku_linker.ld $(RING3_LDFLAGS) \
		test_dzwieku_tmp.o bursztyn_gui.o -o test_dzwieku.elf
	$(OBJCOPY) -O binary test_dzwieku.elf test_dzwieku.bin
	$(PYTHON) narzedzia/test_dzwieku_size.py test_dzwieku.bin
	$(LD) -r -b binary test_dzwieku.bin -o test_dzwieku_blob.o

# REPRO (tylko diagnostyka BWS2/BWS46)
repro_tmp.o: programy/repro/repro.cpp bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

repro_blob.o: repro_tmp.o bursztyn_gui.o $(EKSPLORATOR_LINKER)
	$(LD) -T $(EKSPLORATOR_LINKER) $(RING3_LDFLAGS) \
		repro_tmp.o bursztyn_gui.o -o repro.elf
	$(OBJCOPY) -O binary repro.elf repro.bin
	$(LD) -r -b binary repro.bin -o repro_blob.o

# ============================================================================
# 16. MENEDZER OKIEN .bur
# ============================================================================

menedzer_okien_tmp.o: menedzer_okien.cpp desktop_entries.h desktop_layout_settings.h desktop_layout.h bursztyn_gui.h ui_skala.h pulpit_geometria.h obrazy/png_dekoder.h plik_popup_ui.h plik_operacje_ui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

menedzer_okien_blob.o: menedzer_okien_tmp.o bursztyn_gui.o $(MENEDZER_OKIEN_LINKER)
	$(LD) -T $(MENEDZER_OKIEN_LINKER) $(RING3_LDFLAGS) \
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

sponsorzy_blob.o: dane/sponsorze/sponsorzy
	$(LD) -r -b binary $< -o $@

kernel.o aktualizacja.o: wersja_systemu.h

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
# 20. ISO GRUB - STANDARD (BEZ MODULU 8K) + DEVELOPERSKIE ISO8K
# ============================================================================

#
# Standardowe i jedyne kanoniczne ISO (BursztynOS.iso)
# Zawiera dwa wpisy GRUB:
# 1. Bursztyn OS (domyslny, czysty start bez autostartu testow)
# 2. Bursztyn OS - testy automatyczne (multiboot2 bursztyn.test=automated)
#
$(GRUB_CFG): $(KERNEL) $(TAPETY_PAK) .installer-build/BOOTX64.EFI Makefile
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL) $(ISO_DIR)/boot/
	@echo "[ISO] Kopiuje $(TAPETY_PAK) jako /boot/tapety.pak (modul Multiboot2)."
	@cp $(TAPETY_PAK) $(ISO_DIR)/boot/tapety.pak
	@mkdir -p $(ISO_DIR)/boot/installer
	@cp .installer-build/BOOTX64.EFI $(ISO_DIR)/boot/installer/BOOTX64.EFI
	@printf '%s\n' \
		'set timeout=3' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		'' \
		'menuentry "Bursztyn OS - testy automatyczne" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		'menuentry "Bursztyn OS - testy automatyczne (niszczacy test dysku)" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated bursztyn.test.diskdestructive=1' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		'menuentry "Bursztyn OS Instalator" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    echo "Ladowanie jadra..."' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.install=1' \
		'    echo "Ladowanie zasobow instalatora..."' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    module2 /boot/installer/BOOTX64.EFI installer.efi' \
		'    module2 /boot/system_operacyjny.bin installer.kernel' \
		'    echo "Uruchamianie instalatora..."' \
		'    boot' \
		'}' \
		> $(GRUB_CFG)

.installer-build/BOOTX64.EFI: narzedzia/instalator/grub-embedded.cfg Makefile
	@mkdir -p .installer-build
	grub-mkstandalone -O x86_64-efi --locales="" --fonts="" --modules="part_gpt fat loadenv test normal multiboot2 configfile search search_fs_file all_video gfxterm" -o $@ "boot/grub/grub.cfg=$<"

.PHONY: test-network-device test-e1000-qemu test-virtio-pci test-virtio-qemu test-virtio-blk-qemu test-virtio-net-qemu test-virtio-gpu-qemu test-virtio-input-qemu test-normal-boot test-diff-boot installer-unit-test installer-test run-installer test-aktualizacje test-rozruch test-aktualizacje-gui test-bur-loader test-aktualizacja-qemu test-rozruch-qemu test-ota audit-stos-jadra test-stos-jadra test-wspolbieznosc test-guard-page test-16k-bws84 test-motywy test-motywy-unit test-motywy-gui test-motywy-qemu test-notatnik-gui test-kalkulator-gui test-przegladarka-gui test-bursztyn-ui-polecenia test-kontrolki4 test-kontrolki4-qemu

test-network-device:
	@mkdir -p .nettest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -DBURSZTYN_NET_HOST_TEST -fsanitize=address,undefined network_device.cpp narzedzia/test_network_device.cpp -o .nettest/test-network-device
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .nettest/test-network-device

test-e1000-qemu: iso
	$(PYTHON) narzedzia/test_e1000_qemu.py

.PHONY: test-virtio-input-events
test-virtio-input-events:
	@mkdir -p .virtiotest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_PMM_HOST_TEST \
		-fsanitize=address,undefined -pthread \
		input_device.cpp narzedzia/test_virtio_input_events.cpp -o .virtiotest/test-virtio-input-events
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/test-virtio-input-events

test-virtio-pci:
	$(PYTHON) narzedzia/test_input_boot_order.py
	@mkdir -p .virtiotest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_PMM_HOST_TEST $(if $(filter 1 yes true,$(BURSZTYNDIAGVIRTIOINPUTINIT)),-DBURSZTYNDIAGVIRTIOINPUTINIT=1) $(if $(filter 1,$(BURSZTYNDIAGVIRTIOINPUTQEMU)),-DBURSZTYNDIAGVIRTIOINPUTQEMU=1) -fsanitize=address,undefined -pthread pci.cpp pmm.cpp virtio_pci.cpp virtqueue.cpp virtio_rng.cpp virtio_blk.cpp network_device.cpp virtio_net.cpp virtio_gpu.cpp input_device.cpp virtio_input.cpp sterowniki/usb/usb_hid.cpp klawiatura.cpp narzedzia/test_virtio_pci.cpp -o .virtiotest/test-virtio-pci
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/test-virtio-pci

.PHONY: test-input-init-locks
test-input-init-locks:
	@mkdir -p .virtiotest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -DBURSZTYN_LOCK_HOST_TEST -DBURSZTYN_PMM_HOST_TEST -DBURSZTYNDIAGVIRTIOINPUTINIT=1 -ffunction-sections -fdata-sections -fsanitize=address,undefined -pthread heap.cpp pmm.cpp pci.cpp narzedzia/test_input_init_locks.cpp -Wl,--gc-sections -o .virtiotest/test-input-init-locks
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/test-input-init-locks

test-virtio-qemu: $(CI_TEST_ISO)
	$(PYTHON) narzedzia/test_virtio_qemu.py

test-virtio-blk-qemu: $(CI_TEST_ISO)
	$(PYTHON) narzedzia/test_virtio_blk_qemu.py

test-virtio-net-qemu: $(CI_TEST_ISO)
	$(PYTHON) narzedzia/test_virtio_net_qemu.py

test-virtio-gpu-qemu: $(CI_TEST_ISO)
	$(PYTHON) narzedzia/test_virtio_gpu_qemu.py

test-virtio-input-qemu: $(CI_TEST_ISO)
	$(PYTHON) narzedzia/test_virtio_input_qemu.py

test-normal-boot: iso
	$(PYTHON) narzedzia/test_normal_boot_qemu.py

test-diff-boot: iso
	$(PYTHON) narzedzia/test_diff_boot.py

test-kontrolki4:
	@mkdir -p .kontrolki4-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined narzedzia/test_kontrolki4.cpp -o .kontrolki4-test/test-k4
	ASAN_OPTIONS=detect_leaks=0 .kontrolki4-test/test-k4

test-kontrolki4-qemu: iso
	$(PYTHON) narzedzia/test_kontrolki4_qemu.py

test-bursztyn-ui-polecenia:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined narzedzia/test_bursztyn_ui_polecenia.cpp -o .bursztyn-ui-test/test-polecenia
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-polecenia

test-motywy-unit:
	@mkdir -p .motywytest
	g++ -std=c++17 -O2 -Wall -Wextra narzedzia/test_motyw_host.cpp -o .motywytest/test_motyw_host
	.motywytest/test_motyw_host

test-motywy-gui:
	$(PYTHON) narzedzia/test_motywy_gui.py

test-notatnik-gui: notatnik_blob.o
	$(PYTHON) narzedzia/test_notatnik_gui.py

test-kalkulator-gui: kalkulator_blob.o
	$(PYTHON) narzedzia/test_kalkulator_gui.py

test-przegladarka-gui: przegladarka_blob.o husarz_worker_blob.o
	$(PYTHON) narzedzia/test_przegladarka_gui.py

test-motywy: test-motywy-unit test-motywy-gui test-notatnik-gui test-kalkulator-gui

test-motywy-qemu: $(CI_TEST_ISO) prepare-disk
	$(PYTHON) narzedzia/test_motywy_qemu.py

test-16k-bws84:
	$(PYTHON) narzedzia/aktualizacje/test_16k_bws84_irq.py
test-bur-loader:
	$(PYTHON) narzedzia/test_bur_loader.py
test-aktualizacje:
	$(PYTHON) narzedzia/aktualizacje/test_aktualizacje.py
test-rozruch:
	$(PYTHON) narzedzia/aktualizacje/test_rozruch.py
test-aktualizacje-gui:
	$(PYTHON) narzedzia/aktualizacje/test_gui_aktualizacje.py
test-aktualizacja-qemu: iso
	$(PYTHON) narzedzia/aktualizacje/qemu_aktualizacja_test.py $(QEMU_TEST_ARGS)
test-rozruch-qemu: test-aktualizacja-qemu
test-ota: test-bur-loader test-aktualizacje test-rozruch test-aktualizacje-gui test-aktualizacja-qemu
test-guard-page: iso
	$(PYTHON) narzedzia/test_guard_page.py
# Audyt ramek stosu jadra: celowo NIE jest czescia normalnego `make`, poniewaz
# wymaga czystej diagnostycznej przebudowy wszystkich obiektow jadra.
# Sprawdza pelne pokrycie .su (w tym biblioteki/mbedtls), najwieksze ramki
# oraz funkcje o dynamicznym/nieznanym zuzyciu (nigdy liczone jako zero).
audit-stos-jadra:
	rm -f *.su
	rm -f biblioteki/mbedtls/library/*.su
	rm -f *.o
	rm -f biblioteki/mbedtls/library/*.o
	$(MAKE) $(KERNEL)
	$(PYTHON) narzedzia/analiza_stosu.py --dir . --makefile Makefile --fail-on-missing --max-frame 6144 --warn-frame 3072
test-stos-jadra: audit-stos-jadra
test-wspolbieznosc:
	$(PYTHON) narzedzia/test_wspolbieznosc.py
installer-unit-test:
	@mkdir -p .installertest
	g++ -std=c++17 -O1 -g -Wall -Wextra -DBURSZTYN_PSF_HOST_TEST -fsanitize=address,undefined narzedzia/instalator/test_storage.cpp storage_format.cpp psf.cpp -o .installertest/test-storage
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-storage
installer-test: iso installer-unit-test
	$(PYTHON) narzedzia/instalator/qemu_install.py test
run-installer: iso
	$(PYTHON) narzedzia/instalator/qemu_install.py run

instalator_tmp.o: programy/instalator/instalator.cpp instalator_format.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -c $< -o $@
instalator_blob.o: instalator_tmp.o bursztyn_gui.o programy/kontrolki3/kontrolki3_linker.ld
	$(LD) --gc-sections -T programy/kontrolki3/kontrolki3_linker.ld $(RING3_LDFLAGS) instalator_tmp.o bursztyn_gui.o -o instalator.elf
	$(OBJCOPY) -O binary instalator.elf instalator.bin
	$(LD) -r -b binary instalator.bin -o $@

$(ISO): check-kernel $(KERNEL) $(TAPETY_PAK) $(GRUB_CFG)
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR) --xorriso=$(XORRISO)
	@cmp -s $(TAPETY_PAK) $(ISO_DIR)/boot/tapety.pak || { echo "[TAPETY BUILD] BLAD: staging ISO nie jest identyczny z $(TAPETY_PAK)"; exit 1; }
	@rm -f $(TAPETY_ISO_CHECK)
	@$(XORRISO) -osirrox on -indev $@ -extract /boot/tapety.pak $(TAPETY_ISO_CHECK) >/dev/null 2>&1
	@cmp -s $(TAPETY_PAK) $(TAPETY_ISO_CHECK) || { echo "[TAPETY BUILD] BLAD: pakiet w ISO nie jest identyczny z $(TAPETY_PAK)"; exit 1; }
	@rm -f $(GRUB_ISO_CHECK)
	@$(XORRISO) -osirrox on -indev $@ -extract /boot/grub/grub.cfg $(GRUB_ISO_CHECK) >/dev/null 2>&1
	@cmp -s $(GRUB_CFG) $(GRUB_ISO_CHECK) || { echo "[TAPETY BUILD] BLAD: grub.cfg w ISO nie jest identyczny ze stagingiem"; exit 1; }
	@echo "[TAPETY BUILD] SHA256 host/staged/ISO:"
	@sha256sum $(TAPETY_PAK) $(ISO_DIR)/boot/tapety.pak $(TAPETY_ISO_CHECK)
	@echo "[TAPETY BUILD] Wpisy GRUB Multiboot2:"
	@grep -E 'multiboot2|module2.*tapety\.pak' $(GRUB_ISO_CHECK)
	@rm -f $(TAPETY_ISO_CHECK) $(GRUB_ISO_CHECK)

iso: $(ISO)

# Tymczasowy artefakt developerski/CI sluzacy wylacznie do bezobslugowego
# uruchomienia drugiego wpisu (bursztyn.test=automated) w QEMU bez oczekiwania na menu.
# Wykorzystuje dokladnie ten sam plik jadra $(KERNEL) co kanoniczny $(ISO).
$(CI_TEST_ISO): $(KERNEL) check-kernel $(GRUB_CFG) $(TAPETY_PAK)
	@mkdir -p $(CI_TEST_DIR)/boot/grub
	@cp $(KERNEL) $(CI_TEST_DIR)/boot/
	@cp $(TAPETY_PAK) $(CI_TEST_DIR)/boot/tapety.pak
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS - testy automatyczne" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		> $(CI_TEST_DIR)/boot/grub/grub.cfg
	@$(GRUB_MKRESCUE) -o $@ $(CI_TEST_DIR) --xorriso=$(XORRISO) >/dev/null 2>&1
	@rm -rf $(CI_TEST_DIR)

$(CI_BLK_DESTRUCTIVE_ISO): $(KERNEL) check-kernel $(GRUB_CFG) $(TAPETY_PAK)
	@mkdir -p $(CI_BLK_DIR)/boot/grub
	@cp $(KERNEL) $(CI_BLK_DIR)/boot/
	@cp $(TAPETY_PAK) $(CI_BLK_DIR)/boot/tapety.pak
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS - testy automatyczne (niszczacy test dysku)" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated bursztyn.test.diskdestructive=1' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		> $(CI_BLK_DIR)/boot/grub/grub.cfg
	@$(GRUB_MKRESCUE) -o $@ $(CI_BLK_DIR) --xorriso=$(XORRISO) >/dev/null 2>&1
	@rm -rf $(CI_BLK_DIR)

$(CI_DESTRUCTIVE_ONLY_ISO): $(KERNEL) check-kernel $(GRUB_CFG) $(TAPETY_PAK)
	@mkdir -p $(CI_DEST_ONLY_DIR)/boot/grub
	@cp $(KERNEL) $(CI_DEST_ONLY_DIR)/boot/
	@cp $(TAPETY_PAK) $(CI_DEST_ONLY_DIR)/boot/tapety.pak
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS - flaga niszczaca bez trybu automatycznego" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test.diskdestructive=1' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		> $(CI_DEST_ONLY_DIR)/boot/grub/grub.cfg
	@$(GRUB_MKRESCUE) -o $@ $(CI_DEST_ONLY_DIR) --xorriso=$(XORRISO) >/dev/null 2>&1
	@rm -rf $(CI_DEST_ONLY_DIR)

#
# Developerskie ISO 8K (BursztynOS_8k.iso) - dolacza modul Multiboot2 test8k.bmp oraz tapety.pak
#
check-test8k:
	@if [ ! -f "$(LARGE_TEST_BMP)" ]; then \
		echo "[BLAD] Brak pliku duzego obrazu testowego: $(LARGE_TEST_BMP)"; \
		echo "Umiesc obraz BMP pod sciezka obrazy/test8k.bmp (lub obrazy/2test.bmp) albo podaj LARGE_TEST_BMP=..."; \
		exit 1; \
	fi

$(GRUB8K_CFG): $(KERNEL) check-test8k $(TAPETY_PAK)
	@mkdir -p $(ISO8K_DIR)/boot/grub
	@cp $(KERNEL) $(ISO8K_DIR)/boot/
	@echo "[ISO-8K] Kopiuje $(LARGE_TEST_BMP) jako /boot/test8k.bmp (modul Multiboot2)."
	@cp "$(LARGE_TEST_BMP)" $(ISO8K_DIR)/boot/test8k.bmp
	@echo "[ISO-8K] Kopiuje $(TAPETY_PAK) jako /boot/tapety.pak (modul Multiboot2)."
	@cp $(TAPETY_PAK) $(ISO8K_DIR)/boot/tapety.pak
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS (8K Test)" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32,1024x768x32,auto' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    module2 /boot/test8k.bmp test8k.bmp' \
		'    boot' \
		'}' \
		> $(GRUB8K_CFG)

$(ISO8K): check-kernel check-test8k $(GRUB8K_CFG)
	$(GRUB_MKRESCUE) -o $@ $(ISO8K_DIR) --xorriso=$(XORRISO)

iso8k: $(ISO8K)

# ============================================================================
# 21. DYSK TESTOWY
# ============================================================================

$(VIRTUAL_DISK):
	$(QEMU_IMG) create -f raw $@ 4G

prepare-disk: $(VIRTUAL_DISK)
	@echo "[QEMU] Dysk przygotowany; bez zapisu zasobow pod surowe LBA."

# ============================================================================
# 22. QEMU - PROFILE URUCHOMIENIOWE
# ============================================================================
#
# Domyslne cele `make run` i `make runuefi` uruchamiaja pelny, aktualny stos
# VirtIO Bursztyn OS:
#   - VirtIO-BLK
#   - VirtIO-NET
#   - VirtIO-RNG
#   - VirtIO-GPU 2D / virtio-vga
#   - VirtIO-INPUT (klawiatura + mysz)
#
# Cele *-legacy uruchamiaja system bez VirtIO:
#   - SATA AHCI
#   - Intel E1000
#   - STD VGA / Bochs VBE (oraz GOP podczas startu UEFI)
#   - domyslne PS/2 QEMU
#
# QEMU_DISPLAY_ARGS pozostaje puste domyslnie, aby Makefile nie wymagal GTK.
# Przydatny wariant developerski:
#   make runuefi QEMU_DISPLAY_ARGS="-display gtk,zoom-to-fit=off"
#

QEMUACCEL ?= tcg,thread=single
QEMU_ACCEL ?= $(QEMUACCEL)
QEMU_RAM ?= 2G
QEMU_SMP ?= 1
QEMUDISPLAYARGS ?= $(if $(strip $(QEMU_DISPLAY_ARGS)),$(QEMU_DISPLAY_ARGS),-display sdl)
QEMU_VIRTIO_XRES ?= 1280
QEMU_VIRTIO_YRES ?= 720

QEMUBASEARGS ?= \
	-machine pc,hpet=on \
	-cpu max \
	-accel $(QEMUACCEL) \
	-smp $(QEMU_SMP) \
	-m $(QEMU_RAM) \
	-audiodev alsa,id=snd \
	-device intel-hda \
	-device hda-output,audiodev=snd \
	-serial stdio \
	$(QEMUDISPLAYARGS)

QEMU_BASE_ARGS := $(QEMUBASEARGS)

# Podsystem pamieci masowej QEMU
QEMU_DISK_DRIVE := -drive id=disk,file=$(VIRTUAL_DISK),format=raw,if=none
QEMU_VIRTIO_BLK := -device virtio-blk-pci,drive=disk,disable-legacy=on
QEMU_VIRTIO_BLK_BOOT := -device virtio-blk-pci,drive=disk,disable-legacy=on,bootindex=1

QEMU_AHCI_DISK := -device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0,unit=0
QEMU_AHCI_DISK_BOOT := -device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0,unit=0,bootindex=1

# Wspolne urzadzenia VirtIO (poza dyskiem)
QEMU_VIRTIO_COMMON_ARGS := \
	-netdev user,id=n1 \
	-device virtio-net-pci,netdev=n1,disable-legacy=on \
	-object rng-builtin,id=rng0 \
	-device virtio-rng-pci,rng=rng0,disable-legacy=on \
	-vga none \
	-device virtio-vga,disable-legacy=on,xres=$(QEMU_VIRTIO_XRES),yres=$(QEMU_VIRTIO_YRES) \
	-device virtio-keyboard-pci \
	-device virtio-mouse-pci

# Produkcyjny profil VirtIO 1.x Modern PCI (Live / CD / instalator).
# xres/yres sa jedynie poczatkowa wskazowka scanoutu QEMU; sterownik
# VirtIO-GPU Bursztyna zachowuje kontrole nad runtime modesettingiem.
QEMU_VIRTIO_ARGS := \
	$(QEMU_DISK_DRIVE) \
	$(QEMU_VIRTIO_BLK) \
	$(QEMU_VIRTIO_COMMON_ARGS)

# Profil VirtIO dla zainstalowanego systemu (rozruch z dysku z bootindex=1 dla firmware UEFI).
QEMU_VIRTIO_INSTALLED_ARGS := \
	$(QEMU_DISK_DRIVE) \
	$(QEMU_VIRTIO_BLK_BOOT) \
	$(QEMU_VIRTIO_COMMON_ARGS)

# Wspolne urzadzenia legacy (poza dyskiem)
QEMU_LEGACY_COMMON_ARGS := \
	-netdev user,id=n1 \
	-device e1000,netdev=n1 \
	-vga std \
	-global VGA.vgamem_mb=64

# Profil bez VirtIO (Live / CD). Celowo nie dodajemy xHCI/USB HID do zwyklego fallbacku,
# aby nie dublowac wejscia z domyslnym PS/2 QEMU. Osobne cele runusb* ponizej
# sluza do testowania sterownika xHCI/USB HID.
QEMU_LEGACY_ARGS := \
	$(QEMU_DISK_DRIVE) \
	$(QEMU_AHCI_DISK) \
	$(QEMU_LEGACY_COMMON_ARGS)

# Profil bez VirtIO dla zainstalowanego systemu (rozruch z dysku z bootindex=1 dla firmware UEFI).
QEMU_LEGACY_INSTALLED_ARGS := \
	$(QEMU_DISK_DRIVE) \
	$(QEMU_AHCI_DISK_BOOT) \
	$(QEMU_LEGACY_COMMON_ARGS)

QEMU_USB_HID_ARGS := \
	-device qemu-xhci,id=xhci \
	-device usb-kbd,id=usbkbd,bus=xhci.0 \
	-device usb-mouse,id=usbmouse,bus=xhci.0

# ============================================================================
# 23. RUN - BIOS
# ============================================================================

# Domyslny start BIOS: pelny aktualny profil VirtIO.
run: iso prepare-disk
	@echo "[QEMU] Start BIOS / VirtIO Full: GPU+BLK+NET+RNG+INPUT."
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_VIRTIO_ARGS) \
		-boot order=d,menu=on \
		-cdrom $(ISO)

# Start BIOS bez VirtIO: AHCI + E1000 + STD VGA/Bochs + PS/2.
run-legacy: iso prepare-disk
	@echo "[QEMU] Start BIOS bez VirtIO: AHCI + E1000 + STD VGA + PS/2."
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_LEGACY_ARGS) \
		-boot order=d,menu=on \
		-cdrom $(ISO)

run8k: iso8k prepare-disk
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_VIRTIO_ARGS) \
		-boot order=d,menu=on \
		-cdrom $(ISO8K)

run-test: $(CI_TEST_ISO) prepare-disk
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_VIRTIO_ARGS) \
		-boot order=d,menu=on \
		-cdrom $(CI_TEST_ISO)

run-geom-trace: prepare-disk
	@echo "[QEMU] UWAGA: zmiana flag kompilacji wymaga przebudowy obiektow."
	$(MAKE) clean
	$(MAKE) BURSZTYN_DIAG_GEOM_TRACE=1 run

# Testy fallbacku USB korzystaja z profilu bez VirtIO-INPUT.
runusb: iso prepare-disk
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_LEGACY_ARGS) \
		-device qemu-xhci,id=xhci \
		-boot order=d,menu=on \
		-cdrom $(ISO)

runusbkbd: iso prepare-disk
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_LEGACY_ARGS) \
		-device qemu-xhci,id=xhci \
		-device usb-kbd,id=usbkbd,bus=xhci.0 \
		-boot order=d,menu=on \
		-cdrom $(ISO)

runusbmouse: iso prepare-disk
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_LEGACY_ARGS) \
		-device qemu-xhci,id=xhci \
		-device usb-mouse,id=usbmouse,bus=xhci.0 \
		-boot order=d,menu=on \
		-cdrom $(ISO)

runusbhid: iso prepare-disk
	$(QEMU) \
		$(QEMU_BASE_ARGS) \
		$(QEMU_LEGACY_ARGS) \
		$(QEMU_USB_HID_ARGS) \
		-boot order=d,menu=on \
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

# Domyslny start UEFI: pelny aktualny profil VirtIO.
QEMU_UEFI_FIRMWARE_ARGS = $(if $(strip $(OVMF_MONO)),-bios "$(OVMF_MONO)",-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" -drive if=pflash,format=raw,file="$(OVMF_VARS_RUNTIME)")
RUNUEFI_ISO ?= $(ISO)
QEMU_RUNUEFI_ARGS = $(QEMU_UEFI_FIRMWARE_ARGS) $(QEMU_BASE_ARGS) $(QEMU_VIRTIO_ARGS) -boot order=d,menu=on -cdrom $(RUNUEFI_ISO)
runuefi: uefi prepare-disk check-ovmf
	@if [ -z "$(OVMF_MONO)" ]; then cp "$(OVMF_VARS)" "$(OVMF_VARS_RUNTIME)"; fi
	@$(PYTHON) narzedzia/qemu_launcher_audit.py -- $(QEMU) $(QEMU_RUNUEFI_ARGS)
	$(QEMU) $(QEMU_RUNUEFI_ARGS)

# These targets have no build/disk/firmware-copy prerequisites and never boot a VM.
.PHONY: runuefi-argv runuefi-audit
runuefi-argv:
	@$(PYTHON) narzedzia/qemu_launcher_audit.py -- $(QEMU) $(QEMU_RUNUEFI_ARGS)
runuefi-audit:
	@$(PYTHON) narzedzia/qemu_launcher_audit.py --probe -- $(QEMU) $(QEMU_RUNUEFI_ARGS)

# UEFI bez VirtIO: AHCI + E1000 + GOP/STD VGA + PS/2.
runuefi-legacy: uefi prepare-disk check-ovmf
	@if [ -n "$(OVMF_MONO)" ]; then \
		echo "[QEMU] Start UEFI/OVMF bez VirtIO (-bios)."; \
		exec $(QEMU) \
			-bios "$(OVMF_MONO)" \
			$(QEMU_BASE_ARGS) \
			$(QEMU_LEGACY_ARGS) \
			-boot order=d \
			-cdrom $(ISO); \
	else \
		echo "[QEMU] Start UEFI/OVMF bez VirtIO (pflash CODE+VARS)."; \
		cp "$(OVMF_VARS)" "$(OVMF_VARS_RUNTIME)"; \
		exec $(QEMU) \
			-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
			-drive if=pflash,format=raw,file="$(OVMF_VARS_RUNTIME)" \
			$(QEMU_BASE_ARGS) \
			$(QEMU_LEGACY_ARGS) \
			-boot order=d \
			-cdrom $(ISO); \
	fi

# Zainstalowany system z dysku, domyslnie przez VirtIO Full.
run-installed: prepare-disk check-ovmf
	@if [ -n "$(OVMF_MONO)" ]; then \
		echo "[QEMU] Start zainstalowanego Bursztyn OS / UEFI / VirtIO Full."; \
		exec $(QEMU) \
			-bios "$(OVMF_MONO)" \
			$(QEMU_BASE_ARGS) \
			$(QEMU_VIRTIO_INSTALLED_ARGS) \
			-boot order=c; \
	else \
		echo "[QEMU] Start zainstalowanego Bursztyn OS / UEFI / VirtIO Full (pflash)."; \
		cp "$(OVMF_VARS)" "$(OVMF_VARS_RUNTIME)"; \
		exec $(QEMU) \
			-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
			-drive if=pflash,format=raw,file="$(OVMF_VARS_RUNTIME)" \
			$(QEMU_BASE_ARGS) \
			$(QEMU_VIRTIO_INSTALLED_ARGS) \
			-boot order=c; \
	fi

# Zainstalowany system z dysku przez stary profil AHCI/E1000/STD VGA.
run-installed-legacy: prepare-disk check-ovmf
	@if [ -n "$(OVMF_MONO)" ]; then \
		echo "[QEMU] Start zainstalowanego Bursztyn OS / UEFI bez VirtIO."; \
		exec $(QEMU) \
			-bios "$(OVMF_MONO)" \
			$(QEMU_BASE_ARGS) \
			$(QEMU_LEGACY_INSTALLED_ARGS) \
			-boot order=c; \
	else \
		echo "[QEMU] Start zainstalowanego Bursztyn OS / UEFI bez VirtIO (pflash)."; \
		cp "$(OVMF_VARS)" "$(OVMF_VARS_RUNTIME)"; \
		exec $(QEMU) \
			-drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
			-drive if=pflash,format=raw,file="$(OVMF_VARS_RUNTIME)" \
			$(QEMU_BASE_ARGS) \
			$(QEMU_LEGACY_INSTALLED_ARGS) \
			-boot order=c; \
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
	rm -f *.o *.bin *.elf *.iso .*.iso *.su
	rm -f tapety.pak tapety.pak.d $(TAPETY_SOURCES_MANIFEST) $(TAPETY_ISO_CHECK) $(GRUB_ISO_CHECK) tapety_manifest.h tapetyzasoby.S tapetyzasoby.d tapety_zasoby.rsp tapety_zasoby.d tapety_zasoby.S tapety_zasoby.o
	rm -f biblioteki/mbedtls/library/*.o biblioteki/mbedtls/library/*.su
	rm -f $(OVMF_VARS_RUNTIME)
	rm -rf $(ISO_DIR) $(ISO8K_DIR) $(CI_TEST_DIR) $(CI_BLK_DIR) $(CI_DEST_ONLY_DIR) isodir_test

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
	@echo "  make iso       - zbuduj kanoniczne hybrydowe ISO GRUB (BursztynOS.iso)"
	@echo "  make iso8k     - zbuduj developerskie ISO z modulem 8K BMP (~97 MiB)"
	@echo "  make bios      - zbuduj ISO do startu BIOS"
	@echo "  make uefi      - zbuduj ISO do startu UEFI"
	@echo "  make run       - BIOS + VirtIO Full (GPU/BLK/NET/RNG/INPUT)"
	@echo "  make runuefi   - UEFI/OVMF + VirtIO Full (GPU/BLK/NET/RNG/INPUT)"
	@echo "  make run-legacy - BIOS bez VirtIO (AHCI/E1000/STD VGA/PS2)"
	@echo "  make runuefi-legacy - UEFI bez VirtIO (AHCI/E1000/GOP-STD VGA/PS2)"
	@echo "  make run-installed - zainstalowany system UEFI z dysku przez VirtIO Full"
	@echo "  make run-installed-legacy - zainstalowany system UEFI z dysku bez VirtIO"
	@echo "  make run8k     - BIOS + VirtIO Full z ISO zawierajacym modul 8K"
	@echo "  make run-test  - BIOS + VirtIO Full z automatyczna regresja (bursztyn.test=automated)"
	@echo "  make run-geom-trace - czysta przebudowa + BIOS VirtIO z BURSZTYN_DIAG_GEOM_TRACE=1"
	@echo "  make iso BURSZTYN_DIAG_BOOT=1 - build z telemetria BOOT/WM"
	@echo "  make iso BURSZTYN_DEBUG_GUI_LAYERS=1 - build z diagnostyka warstw BWS"
	@echo "  make check-tools - sprawdz wymagane narzedzia"
	@echo "  make clean     - usun artefakty kompilacji"
	@echo "  make cdysk     - usun wirtualny dysk"
	@echo "  make distclean - clean + cdysk"

# Manifest zmienia timestamp tylko wtedy, gdy zmieni sie posortowany zestaw nazw.
# Zaleznosc od katalogu wykrywa dodanie, usuniecie i zmiane nazwy pliku.
$(TAPETY_SOURCES_MANIFEST): $(TAPETY_DIR) narzedzia/tapety-btp/generujpakiet.py
	$(PYTHON) narzedzia/tapety-btp/generujpakiet.py --katalog $(TAPETY_DIR) $(if $(BURSZTYN_TAPETA_DOMYSLNA),--domyslna "$(BURSZTYN_TAPETA_DOMYSLNA)") --manifest-only $@

# Automatyczne pakowanie tapet BTP w zewnetrznym module Multiboot2 (tapety.pak).
$(TAPETY_PAK): $(TAPETY_SOURCES_MANIFEST) narzedzia/tapety-btp/generujpakiet.py narzedzia/tapety-btp/btpcodec.py
	$(PYTHON) narzedzia/tapety-btp/generujpakiet.py --katalog $(TAPETY_DIR) $(if $(BURSZTYN_TAPETA_DOMYSLNA),--domyslna "$(BURSZTYN_TAPETA_DOMYSLNA)") --wyjscie $(TAPETY_PAK) --dep tapety.pak.d

-include tapety.pak.d

btp.o: narzedzia/tapety-btp/tapetybtp.cpp narzedzia/tapety-btp/tapetybtp.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
tapety_format.h: tapety_nazwy.h
tapety.o: tapety.cpp tapety.h tapety_format.h tapety_nazwy.h pamiec.h bufory_grafiki.h obrazy/skalowanie.h narzedzia/tapety-btp/tapetybtp.h
kernel.o bws_nowoczesne.o: tapety.h tapety_format.h tapety_nazwy.h obrazy/skalowanie.h
syscalls.o: tapety_format.h tapety_nazwy.h
instalator.o ekran.o: tapety.h tapety_format.h tapety_nazwy.h

pci.o: pci.cpp pci.h
virtio_pci.o: virtio_pci.cpp virtio_pci.h virtqueue.h pci.h
virtio_gpu.o: virtio_gpu.cpp virtio_gpu.h virtio_pci.h virtqueue.h pci.h idt.h apic.h grafika.h ekran.h ekran_format.h
virtio_rng.o: virtio_rng.cpp virtio_rng.h virtio_pci.h virtqueue.h pci.h
virtio_blk.o: virtio_blk.cpp virtio_blk.h virtio_pci.h virtqueue.h pci.h blokowy.h
virtio_net.o: virtio_net.cpp virtio_net.h virtio_pci.h virtqueue.h pci.h network_device.h
virtio_input.o: virtio_input.cpp virtio_input.h virtio_pci.h virtqueue.h pci.h input_device.h
kernel.o: virtio_pci.h virtio_blk.h virtio_net.h virtio_gpu.h virtio_rng.h virtio_input.h


btp_ring3.o: narzedzia/tapety-btp/tapetybtp.cpp narzedzia/tapety-btp/tapetybtp.h
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@
image_ring3.o: obrazy/dekoder_obrazu.cpp obrazy/dekoder_obrazu.h obrazy/vendor/stb_image.h narzedzia/tapety-btp/tapetybtp.h
	$(CXX) $(RING3_FLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Os -ffunction-sections -fdata-sections -Wno-unused-function -Wno-unused-parameter -c $< -o $@

.PHONY: test-image
test-image:
	$(PYTHON) obrazy/testy/test_jpeg.py
	$(PYTHON) obrazy/testy/test_png.py
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined obrazy/test_tapety_jpeg.cpp -o /tmp/bursztyn-test-tapety-jpeg
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/bursztyn-test-tapety-jpeg
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined obrazy/test_tapety_manifest.cpp -o /tmp/bursztyn-test-tapety-manifest
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/bursztyn-test-tapety-manifest
tapety_tmp.o: programy/tapety/tapety.cpp programy/tapety/klient.h programy/tapety/miniatury.h tapety_format.h obrazy/skalowanie.h obrazy/dekoder_obrazu.h narzedzia/tapety-btp/tapetybtp.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@
	@test -s $@ || { rm -f $@; echo "[BLAD] tapety_tmp.o jest pusty"; exit 1; }
tapety_blob.o: tapety_tmp.o btp_ring3.o image_ring3.o bursztyn_gui.o programy/tapety/tapety_linker.ld narzedzia/test_tapety_size.py
	@if [ ! -s tapety_tmp.o ]; then \
		echo "[TAPETY BUILD] tapety_tmp.o jest pusty lub uszkodzony - ponowna kompilacja..."; \
		$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c programy/tapety/tapety.cpp -o tapety_tmp.o; \
	fi
	$(LD) --gc-sections -T programy/tapety/tapety_linker.ld $(RING3_LDFLAGS) tapety_tmp.o btp_ring3.o image_ring3.o bursztyn_gui.o -o tapety.elf
	$(OBJCOPY) -O binary tapety.elf tapety.bin
	$(PYTHON) narzedzia/test_tapety_size.py tapety.bin
	$(LD) -r -b binary tapety.bin -o $@

$(filter %tmp.o %_tmp.o,$(wildcard *.o)): obrazy/skalowanie.h

test_tmp.o: programy/test/tapety_testy.h programy/tapety/klient.h programy/tapety/miniatury.h tapety_format.h narzedzia/tapety-btp/tapetybtp.h

test_tmp.o: programy/test/kontrolki3_stabilizacja.h

bochs_vbe.o: sterowniki/grafika/bochs_vbe.cpp sterowniki/grafika/bochs_vbe.h ekran_format.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

ekran_tmp.o: programy/ekran/ekran.cpp programy/ekran/klient.h programy/ekran/ekran_okno.h ekran_format.h ui_skala.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -c $< -o $@
ekran_blob.o: ekran_tmp.o bursztyn_gui.o programy/kontrolki3/kontrolki3_linker.ld
	$(LD) --gc-sections -T programy/kontrolki3/kontrolki3_linker.ld $(RING3_LDFLAGS) ekran_tmp.o bursztyn_gui.o -o ekran.elf
	$(OBJCOPY) -O binary ekran.elf ekran.bin
	$(LD) -r -b binary ekran.bin -o $@
$(filter %_tmp.o,$(OBJS)) bursztyn_gui.o: ui_skala.h

ekran_format_ring3.o: ekran_format.cpp ekran_format.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@
bws_nowoczesne.o ekran.o grafika.o kernel.o: ekran.h ekran_format.h bufory_grafiki.h
test_tmp.o: programy/test/ekran_testy.h programy/ekran/klient.h programy/ekran/ekran_okno.h ekran_format.h pulpit_geometria.h

# Central WM chrome and shared popup layout dependencies.
skladacz_obrazu.o: wm_dekoracje.h motyw.h bursztyn_ui/czcionka.h ui_skala.h
motyw.o: skladacz_obrazu.h
shell_tmp.o: bursztyn_ui/widok_menu_popup.h
grafika.o: wm_input.h
bursztyn_gui.o: wm_belka_hit.h

.PHONY: test-wm-click-routing test-wm-dekoracje test-shell-host
test-wm-click-routing:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_wm_click_routing.cpp -Wl,--gc-sections -o .wm-test/click-routing
	ASAN_OPTIONS=detect_leaks=0 .wm-test/click-routing

test-wm-dekoracje:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined narzedzia/test_wm_dekoracje.cpp -o .wm-test/dekoracje
	ASAN_OPTIONS=detect_leaks=0 .wm-test/dekoracje

test-shell-host:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_shell_terminal.cpp -Wl,--gc-sections -o .wm-test/shell
	ASAN_OPTIONS=detect_leaks=0 .wm-test/shell

BURSZTYNDIAGCURSORPERF ?= 0
ifeq ($(BURSZTYNDIAGCURSORPERF),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGCURSORPERF=1
endif

.PHONY: test-cursor-host
test-cursor-host:
	mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined narzedzia/test_cursor_host.cpp -o .wm-test/cursor
	ASAN_OPTIONS=detect_leaks=0 .wm-test/cursor

grafika.o skladacz_obrazu.o: kursor_prezentacja.h kursor_diag.h
virtio_input.o: kursor_diag.h

.PHONY: test-cursor-hover
test-cursor-hover:
	mkdir -p .wm-test
	g++ -std=c++17 -O0 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_cursor_hover.cpp -Wl,--gc-sections -o .wm-test/cursor-hover
	ASAN_OPTIONS=detect_leaks=0 .wm-test/cursor-hover

BURSZTYNDIAGGUIRUNTIME ?= 0
ifeq ($(BURSZTYNDIAGGUIRUNTIME),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGGUIRUNTIME=1
RING3_FLAGS += -DBURSZTYNDIAGGUIRUNTIME=1
endif
scheduler.o syscalls.o grafika.o skladacz_obrazu.o: gui_runtime_diag.h
shell_tmp.o: shell_runtime_diag.h

virtio_input.o: gui_runtime_diag.h

.PHONY: test-gui-runtime-diag
test-gui-runtime-diag:
	mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined narzedzia/test_gui_runtime_diag.cpp -o .wm-test/gui-runtime-diag
	ASAN_OPTIONS=detect_leaks=0 .wm-test/gui-runtime-diag

.PHONY: test-compositor-cursor
test-compositor-cursor:
	mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_compositor_cursor.cpp -Wl,--gc-sections -o .wm-test/compositor-cursor
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/compositor-cursor

BURSZTYNDIAGABSMAP ?= 0
ifeq ($(BURSZTYNDIAGABSMAP),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGABSMAP=1
RING3_FLAGS += -DBURSZTYNDIAGABSMAP=1
endif
input_device.o: abs_map_diag.h

.PHONY: test-abs-map-diag
test-abs-map-diag:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -ffunction-sections -fdata-sections -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYNDIAGABSMAP=1 -fsanitize=address,undefined input_device.cpp narzedzia/test_abs_map_diag.cpp -Wl,--gc-sections -o .wm-test/abs-map-diag
	ASAN_OPTIONS=detect_leaks=1 .wm-test/abs-map-diag

.PHONY: test-explorer-host
test-explorer-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined narzedzia/test_explorer_host.cpp -o .bursztyn-ui-test/test-explorer
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-explorer

.PHONY: test-tapety-bur
test-tapety-bur: tapety_blob.o
	$(PYTHON) narzedzia/test_tapety_regression.py

.PHONY: test-hda-physical test-hda-tone test-hda-regression-qemu test-hda-diag test-dzwieku-packaging test-dzwieku-repair
test-hda-physical:
	$(PYTHON) narzedzia/test_hda_physical.py
test-hda-tone:
	$(PYTHON) narzedzia/test_hda_tone.py
test-hda-regression-qemu: all
	$(PYTHON) narzedzia/test_hda_regression_qemu.py
test-dzwieku-repair: test_dzwieku_blob.o
	$(PYTHON) narzedzia/test_dzwieku_repair.py

test-dzwieku-packaging: test_dzwieku_blob.o
	$(PYTHON) narzedzia/test_dzwieku_packaging.py

test-hda-diag:
	@mkdir -p .hda-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined narzedzia/test_hda_diag.cpp -o .hda-test/test-hda-diag
	ASAN_OPTIONS=detect_leaks=0 .hda-test/test-hda-diag
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined narzedzia/test_hda_bws_isolation.cpp -o .hda-test/test-hda-bws
	ASAN_OPTIONS=detect_leaks=0 .hda-test/test-hda-bws

.PHONY: test-desktop-files-host
test-desktop-files-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined narzedzia/test_desktop_files_host.cpp -o .bursztyn-ui-test/test-desktop-files
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-desktop-files

.PHONY: test-desktop-capacity-host
test-desktop-capacity-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined narzedzia/test_desktop_capacity_host.cpp -o .bursztyn-ui-test/test-desktop-capacity
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-desktop-capacity

# Opt-in aggregate move diagnostics; no normal-build logging or GPU behavior changes.
BURSZTYNDIAGMOVE ?= 0
ifeq ($(BURSZTYNDIAGMOVE),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGMOVE=1
endif
syscalls.o grafika.o skladacz_obrazu.o: move_trace.h

.PHONY: test-filesystem-move-host
test-filesystem-move-host: installer-unit-test
	@mkdir -p .installertest
	g++ -std=c++17 -O0 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -DBURSZTYN_PSF_HOST_TEST -fsanitize=address,undefined narzedzia/test_move_authorization.cpp storage_format.cpp psf.cpp -o .installertest/test-move-authorization
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-move-authorization

bursztyn_gui.o syscalls.o: bws_pliki.h motyw.h

.PHONY: test-desktop-selection-theme
test-desktop-selection-theme:
	@mkdir -p .motywytest
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined narzedzia/test_desktop_selection_theme.cpp -o .motywytest/test-desktop-selection
	ASAN_OPTIONS=detect_leaks=0 .motywytest/test-desktop-selection

.PHONY: test-file-popup-host
test-file-popup-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined narzedzia/test_file_popup_host.cpp -o .bursztyn-ui-test/test-file-popup
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-file-popup

.PHONY: test-desktop-settings-host
test-desktop-settings-host:
	@mkdir -p .installertest
	g++ -std=c++17 -O0 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -DBURSZTYN_PSF_HOST_TEST -fsanitize=address,undefined narzedzia/test_desktop_settings_authorization.cpp storage_format.cpp psf.cpp -o .installertest/test-desktop-settings
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-desktop-settings

.PHONY: test-startup-visibility
grafika.o skladacz_obrazu.o kernel.o: skladacz_obrazu.h
# Host-only: production compositor lifecycle + kernel graphics + boot policy.
test-startup-visibility:
	@mkdir -p .wm-test
	g++ -std=c++17 -O0 -g0 -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_startup_visibility.cpp -Wl,--gc-sections -o .wm-test/startup-visibility
	ASAN_OPTIONS=detect_leaks=0 .wm-test/startup-visibility
	g++ -std=c++17 -O0 -g0 -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_boot_graphics.cpp -Wl,--gc-sections -o .wm-test/boot-graphics
	ASAN_OPTIONS=detect_leaks=0 .wm-test/boot-graphics
	$(PYTHON) narzedzia/test_startup_policy.py

# Welcome layer dependencies and deterministic host regression suite.
ekran_powitalny.o: ekran_powitalny.h ekran_powitalny_api.h bursztyn_ui/czcionka.h ui_skala.h
grafika.o skladacz_obrazu.o kernel.o: ekran_powitalny_api.h
kernel.o: opcje_startowe.h

.PHONY: test-powitanie
test-powitanie:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined narzedzia/test_powitanie.cpp -o .wm-test/powitanie
	ASAN_OPTIONS=detect_leaks=0 .wm-test/powitanie
	$(PYTHON) narzedzia/test_powitanie_policy.py

BURSZTYNDIAGSTARTUPREBOOT ?= 0
ifeq ($(BURSZTYNDIAGSTARTUPREBOOT),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSTARTUPREBOOT=1
endif
grafika.o skladacz_obrazu.o syscalls.o ekran_powitalny.o: diagnostyka_startu.h
syscalls.o: ekran_powitalny_api.h skladacz_obrazu.h

syscalls.o: cykl_wylaczania_grafiki.h

.PHONY: test-powitanie-reboot
test-powitanie-reboot:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -pthread -fsanitize=address,undefined narzedzia/test_powitanie_reboot.cpp -o .wm-test/powitanie-reboot
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/powitanie-reboot

.PHONY: test-gpu-quiesce-host
test-gpu-quiesce-host:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined narzedzia/test_gpu_quiesce_host.cpp -Wl,--gc-sections -o .wm-test/gpu-quiesce
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/gpu-quiesce

# Opt-in sparse splash timing diagnostics; production default has no frame logs.
BURSZTYNDIAGSPLASHTIMING ?= 0
ifeq ($(BURSZTYNDIAGSPLASHTIMING),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSPLASHTIMING=1
endif

.PHONY: test-splash-timing test-windows-launcher
test-splash-timing:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -DBURSZTYNDIAGSPLASHTIMING=1 -fsanitize=address,undefined narzedzia/test_splash_timing.cpp -o .wm-test/splash-timing
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-timing

test-windows-launcher:
	$(PYTHON) narzedzia/test_windows_launcher_config.py

BURSZTYNDIAGSPLASHFIRSTFRAME ?= 0
ifeq ($(BURSZTYNDIAGSPLASHFIRSTFRAME),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSPLASHFIRSTFRAME=1
endif
virtio_gpu.o: diagnostyka_startu.h
grafika.o: virtio_gpu.h
.PHONY: test-splash-first-frame
test-splash-first-frame:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -DBURSZTYNDIAGSPLASHFIRSTFRAME=1 -fsanitize=address,undefined narzedzia/test_splash_first_frame.cpp -o .wm-test/splash-first-frame
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-first-frame

BURSZTYNDIAGSPLASHRUNTIMEFRAMES ?= 0
ifeq ($(BURSZTYNDIAGSPLASHRUNTIMEFRAMES),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSPLASHRUNTIMEFRAMES=1
endif
grafika.o ekran_powitalny.o skladacz_obrazu.o virtio_gpu.o virtio_pci.o uefi_gop.o kernel.o: prezentacja_startu.h diagnostyka_klatek_startu.h

.PHONY: test-splash-snapshots
test-splash-snapshots:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined narzedzia/test_splash_snapshots.cpp -o .wm-test/splash-snapshots
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-snapshots

# Restricted ptrace environments may override only leak checking; ASan/UBSan
# remain compiled into all of these startup verification binaries.
STARTUP_ASAN_OPTIONS ?= detect_leaks=1

.PHONY: test-splash-production-path
STARTUP_HOST_FLAGS := -std=c++17 -O1 -g -I. -ffunction-sections -fdata-sections -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_GRAPHICS_HOST_TEST -DBURSZTYN_COMPOSITOR_HOST_TEST -DBURSZTYNDIAGSPLASHRUNTIMEFRAMES=1 -fsanitize=address,undefined -pthread
STARTUP_HOST_SOURCES := narzedzia/test_splash_production_path.cpp grafika.cpp ekran_powitalny.cpp skladacz_obrazu.cpp sterowniki/grafika/uefi_gop.cpp pci.cpp virtio_pci.cpp virtqueue.cpp virtio_rng.cpp virtio_blk.cpp network_device.cpp virtio_net.cpp virtio_gpu.cpp input_device.cpp virtio_input.cpp sterowniki/usb/usb_hid.cpp klawiatura.cpp
.wm-test/runtime-host/narzedzia/test_splash_production_path.o .wm-test/runtime-host/pmm.o: STARTUP_HOST_FLAGS += -DBURSZTYN_PMM_HOST_TEST
STARTUP_HOST_OBJECTS := $(addprefix .wm-test/runtime-host/,$(STARTUP_HOST_SOURCES:.cpp=.o))
.wm-test/runtime-host/%.o: %.cpp
	@mkdir -p $(@D)
	g++ $(STARTUP_HOST_FLAGS) -MMD -MP -c $< -o $@
-include $(STARTUP_HOST_OBJECTS:.o=.d)
test-splash-production-path: $(STARTUP_HOST_OBJECTS) .wm-test/runtime-host/pmm.o
	@mkdir -p .wm-test
	g++ $(STARTUP_HOST_FLAGS) -Wl,--gc-sections $(STARTUP_HOST_OBJECTS) .wm-test/runtime-host/pmm.o -o .wm-test/splash-production-path
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-production-path

# Bootstrap status is shared with kernel early startup; explicit first-build
# edges complement generated dependencies when upgrading an existing worktree.
psf.o kernel.o: psf.h psf_bootstrap_status.h
psf.o: psf_usuwanie.inc plik_status.h
bursztyn_gui.o syscalls.o: plik_status.h
kernel.o: psf_bootstrap.h
-include $(wildcard *.d biblioteki/mbedtls/library/*.d)

PSF_BOOTSTRAP_HOST_FLAGS := -std=c++17 -O1 -g -Wall -Wextra -I. -Ibiblioteki/mbedtls/include -DBURSZTYN_PSF_HOST_TEST -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined
.PHONY: test-gui-text-input-host test-recursive-delete-host test-file-watch-host
test-file-watch-host:
	@mkdir -p .installertest
	g++ $(PSF_BOOTSTRAP_HOST_FLAGS) narzedzia/test_file_watch_host.cpp -o .installertest/test-file-watch
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-file-watch
test-gui-text-input-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined narzedzia/test_gui_text_input.cpp klawiatura.cpp -o .bursztyn-ui-test/test-gui-text-input
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-gui-text-input
test-recursive-delete-host:
	@mkdir -p .installertest
	g++ $(PSF_BOOTSTRAP_HOST_FLAGS) narzedzia/test_recursive_delete_host.cpp storage_format.cpp -o .installertest/test-recursive-delete
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-recursive-delete
.PHONY: test-psf-bootstrap-host
test-psf-bootstrap-host:
	@mkdir -p .installertest
	g++ $(PSF_BOOTSTRAP_HOST_FLAGS) -DBURSZTYNDIAGPSFBOOTSTRAP=0 narzedzia/test_psf_bootstrap_host.cpp aktualizacja.cpp storage_format.cpp -o .installertest/test-psf-bootstrap-normal
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-psf-bootstrap-normal
	g++ $(PSF_BOOTSTRAP_HOST_FLAGS) -DBURSZTYNDIAGPSFBOOTSTRAP=1 narzedzia/test_psf_bootstrap_host.cpp aktualizacja.cpp storage_format.cpp -o .installertest/test-psf-bootstrap
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-psf-bootstrap


hid_pointer.o: sterowniki/usb/hid_pointer.cpp sterowniki/usb/hid_pointer.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

ohci.o: sterowniki/usb/ohci.cpp sterowniki/usb/ohci.h sterowniki/usb/hid_pointer.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

BURSZTYNDIAGPS2 ?= 0
BURSZTYNDIAGUSBHID ?= 0
ifeq ($(BURSZTYNDIAGPS2),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGPS2=1
endif
ifeq ($(BURSZTYNDIAGUSBHID),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGUSBHID=1
endif

POINTING_HOST_FLAGS := $(if $(filter 1,$(BURSZTYNDIAGXHCIPOINTING)),-DBURSZTYNDIAGXHCIPOINTING=1) $(if $(filter 1,$(BURSZTYNDIAGPS2)),-DBURSZTYNDIAGPS2=1) $(if $(filter 1,$(BURSZTYNDIAGUSBHID)),-DBURSZTYNDIAGUSBHID=1) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -DBURSZTYN_VIRTIO_HOST_TEST -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: test-pointing-host
test-pointing-host:
	@mkdir -p .pointingtest
	g++ $(POINTING_HOST_FLAGS) narzedzia/test_pointing.cpp sterowniki/usb/hid_pointer.cpp input_device.cpp -o .pointingtest/pointing
	.pointingtest/pointing
	g++ $(POINTING_HOST_FLAGS) -pthread -DBURSZTYN_PS2_HOST_TEST -DREAL_KEYBOARD=1 narzedzia/test_ps2_controller.cpp input_device.cpp klawiatura.cpp sterowniki/usb/usb_hid.cpp -o .pointingtest/ps2-controller
	.pointingtest/ps2-controller
	g++ $(POINTING_HOST_FLAGS) narzedzia/test_ohci.cpp sterowniki/usb/hid_pointer.cpp input_device.cpp -o .pointingtest/ohci
	.pointingtest/ohci
	g++ $(POINTING_HOST_FLAGS) -ffunction-sections -fdata-sections -DBURSZTYN_IDT_HOST_TEST narzedzia/test_xhci_pointer.cpp idt.cpp pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-pointer
	.pointingtest/xhci-pointer

mysz.o: ps2_mouse_parser.h pointing_diag.h
hid_pointer.o: pointing_diag.h input_device.h
xhci.o usb.o: sterowniki/usb/ohci.h sterowniki/usb/hid_pointer.h

# One bounded shared diagnostic stream; production default is silent.
BURSZTYNDIAGXHCIPOINTING ?= 0
ifeq ($(BURSZTYNDIAGXHCIPOINTING),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGXHCIPOINTING=1
endif
input_device.o mysz.o grafika.o hid_pointer.o xhci.o ohci.o idt.o: xhci_pointing_diag.h

.PHONY: test-irq-allocation-host
test-irq-allocation-host:
	@mkdir -p .pointingtest
	g++ $(POINTING_HOST_FLAGS) -DBURSZTYN_IDT_HOST_TEST -ffunction-sections -fdata-sections idt.cpp narzedzia/test_irq_allocation.cpp -Wl,--gc-sections -o .pointingtest/irq-allocation
	.pointingtest/irq-allocation

.PHONY: test-pointer-regression-host
test-pointer-regression-host:
	@mkdir -p .pointingtest
	g++ $(POINTING_HOST_FLAGS) narzedzia/test_pointer_regression.cpp sterowniki/usb/hid_pointer.cpp input_device.cpp -o .pointingtest/pointer-regression
	.pointingtest/pointer-regression
	$(PYTHON) narzedzia/test_pointer_boot_order.py
input_device.o grafika.o hid_pointer.o: pointer_coordinates.h

.PHONY: test-pointer-graphics-host
test-pointer-graphics-host: $(filter-out .wm-test/runtime-host/narzedzia/test_splash_production_path.o,$(STARTUP_HOST_OBJECTS))
	g++ $(STARTUP_HOST_FLAGS) -DBURSZTYN_PMM_HOST_TEST -Wl,--gc-sections narzedzia/test_pointer_graphics.cpp sterowniki/usb/hid_pointer.cpp $^ -o .wm-test/pointer-graphics
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/pointer-graphics

# Physical hardware diagnostics: bounded, deferred, no polling or timing changes.
BURSZTYNDIAGXHCIPHYSICALMOUSE ?= 0
ifeq ($(BURSZTYNDIAGXHCIPHYSICALMOUSE),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGXHCIPHYSICALMOUSE=1
endif
xhci.o: sterowniki/usb/xhci_hw.h
xhci.o hid_pointer.o input_device.o: sterowniki/usb/xhci_physical_diag.h

.PHONY: test-xhci-physical-mouse-host
test-xhci-physical-mouse-host:
	@mkdir -p .pointingtest
	g++ $(POINTING_HOST_FLAGS) -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -DBURSZTYNDIAGXHCIPHYSICALMOUSE=0 -ffunction-sections -fdata-sections narzedzia/test_xhci_physical_mouse.cpp idt.cpp pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-physical-mouse-production
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 .pointingtest/xhci-physical-mouse-production
	g++ $(POINTING_HOST_FLAGS) -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -DBURSZTYNDIAGXHCIPHYSICALMOUSE=1 -ffunction-sections -fdata-sections narzedzia/test_xhci_physical_mouse.cpp idt.cpp pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-physical-mouse
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 .pointingtest/xhci-physical-mouse

BURSZTYNDIAGINPUTCOEXISTENCE ?= 0
ifeq ($(BURSZTYNDIAGINPUTCOEXISTENCE),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGINPUTCOEXISTENCE=1
endif

.PHONY: test-input-coexistence-host
test-input-coexistence-host:
	python3 narzedzia/test_input_diagnostic_parity.py

BURSZTYNDIAGBOOTXHCITIMING ?= 0
BURSZTYNDIAGVIRTIOINPUTQEMU ?= 0
BURSZTYNDIAGVIRTIOINPUTFORENSIC ?= 0
ifeq ($(BURSZTYNDIAGVIRTIOINPUTFORENSIC),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGVIRTIOINPUTFORENSIC=1
endif
ifeq ($(BURSZTYNDIAGBOOTXHCITIMING),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGBOOTXHCITIMING=1
endif
ifeq ($(BURSZTYNDIAGVIRTIOINPUTQEMU),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGVIRTIOINPUTQEMU=1
endif

.PHONY: test-launcher-parity
test-launcher-parity:
	$(PYTHON) narzedzia/test_launcher_parity.py

.PHONY: test-virtio-input-qemu-parity-host
test-virtio-input-qemu-parity-host:
	$(PYTHON) narzedzia/test_virtio_input_qemu_parity.py

.PHONY: test-virtio-input-forensic-host
test-virtio-input-forensic-host:
	@mkdir -p .virtiotest/forensic-validation
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -I. -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_PMM_HOST_TEST -DBURSZTYNDIAGVIRTIOINPUTFORENSIC=$(BURSZTYNDIAGVIRTIOINPUTFORENSIC) -fsanitize=address,undefined -pthread pci.cpp pmm.cpp virtio_pci.cpp virtqueue.cpp virtio_rng.cpp virtio_blk.cpp network_device.cpp virtio_net.cpp virtio_gpu.cpp input_device.cpp virtio_input.cpp sterowniki/usb/usb_hid.cpp klawiatura.cpp narzedzia/test_virtio_input_forensic.cpp -o .virtiotest/forensic-validation/queue-$(BURSZTYNDIAGVIRTIOINPUTFORENSIC)
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/forensic-validation/queue-$(BURSZTYNDIAGVIRTIOINPUTFORENSIC)

.PHONY: test-usb-composite-host
test-usb-composite-host:
	@mkdir -p .pointingtest
	g++ $(POINTING_HOST_FLAGS) -no-pie narzedzia/test_usb_composite.cpp sterowniki/usb/hid_pointer.cpp input_device.cpp -o .pointingtest/usb-composite
	.pointingtest/usb-composite

.PHONY: test-xhci-multi-host
test-xhci-multi-host:
	@mkdir -p .pointingtest
	g++ $(POINTING_HOST_FLAGS) -no-pie -DBURSZTYN_BOOT_TIMING_HOST_TEST -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -ffunction-sections -fdata-sections narzedzia/test_xhci_multi.cpp idt.cpp pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-multi
	.pointingtest/xhci-multi

.PHONY: test-xhci-multi-qemu
test-xhci-multi-qemu: all
	$(PYTHON) narzedzia/test_xhci_multi_qemu.py
	$(PYTHON) narzedzia/test_xhci_multi_qemu.py --virtio-input

# Native read-only system diagnostics utility (.cebula / BUR v1).
diagnostyka_tmp.o: programy/diagnostyka/diagnostyka.cpp programy/diagnostyka/model.h diagnostyka_systemu_abi.h diagnostyka_systemu_format.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

diagnostyka_blob.o: diagnostyka_tmp.o bursztyn_gui.o programy/diagnostyka/diagnostyka_linker.ld
	$(LD) --orphan-handling=error -T programy/diagnostyka/diagnostyka_linker.ld $(RING3_LDFLAGS) diagnostyka_tmp.o bursztyn_gui.o -o diagnostyka.elf
	$(OBJCOPY) -O binary diagnostyka.elf diagnostyka.bin
	$(LD) -r -b binary diagnostyka.bin -o diagnostyka_blob.o

.PHONY: test-diagnostyka-systemu-host
test-diagnostyka-systemu-host:
	@mkdir -p .input-test/diagnostyka
	g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -I. -DDIAG_HOST_TEST -DBURSZTYN_VIRTIO_HOST_TEST -fsanitize=address,undefined diagnostyka_systemu.cpp diagnostyka_systemu_bws.cpp input_device.cpp narzedzia/test_diagnostyka_systemu.cpp -o .input-test/diagnostyka/host
	ASAN_OPTIONS=detect_leaks=1 .input-test/diagnostyka/host

.PHONY: test-diagnostyka-qemu
test-diagnostyka-qemu: all
	$(PYTHON) narzedzia/test_diagnostyka_qemu.py

.PHONY: test-diagnostyka-xhci-host
test-diagnostyka-xhci-host:
	@mkdir -p .input-test/diagnostyka
	g++ $(POINTING_HOST_FLAGS) -no-pie -DDIAG_HOST_TEST -DBURSZTYN_BOOT_TIMING_HOST_TEST -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -ffunction-sections -fdata-sections narzedzia/test_diagnostyka_xhci.cpp diagnostyka_systemu.cpp idt.cpp pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp input_device.cpp -Wl,--gc-sections -o .input-test/diagnostyka/xhci
	.input-test/diagnostyka/xhci

.PHONY: test-diagnostyka-packaging
test-diagnostyka-packaging: diagnostyka_blob.o
	$(PYTHON) narzedzia/test_diagnostyka_packaging.py

.PHONY: test-husarz-engine-host test-husarz-runtime-host

test-husarz-engine-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. narzedzia/test_husarz_engine.cpp -o .husarz-test/engine
	ASAN_OPTIONS=detect_leaks=1 .husarz-test/engine

test-husarz-runtime-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections -I. narzedzia/test_husarz_runtime.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp -Wl,--gc-sections -o .husarz-test/runtime
	ASAN_OPTIONS=detect_leaks=1 .husarz-test/runtime

.PHONY: test-husarz-qemu
test-husarz-qemu: iso
	$(PYTHON) narzedzia/test_husarz_qemu.py

.PHONY: test-user-fault-policy test-user-fault-qemu-build

test-user-fault-policy:
	@mkdir -p .husarz-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined narzedzia/test_user_fault_policy.cpp -o .husarz-test/user-fault-policy
	.husarz-test/user-fault-policy

# Substitute only the browser payload in a separate linked test kernel. The
# production kernel, browser BUR and ordinary ISO are never overwritten.
test-user-fault-qemu-build: all
	@mkdir -p .husarz-test/user-fault
	$(CC) $(ASM_FLAGS) -c narzedzia/fixtures/husarz/user_fault.S -o .husarz-test/user-fault/probe.o
	$(LD) --gc-sections -T $(PRZEGLADARKA_LINKER) $(RING3_LDFLAGS) .husarz-test/user-fault/probe.o -o .husarz-test/user-fault/probe.elf
	$(OBJCOPY) -O binary .husarz-test/user-fault/probe.elf .husarz-test/user-fault/przegladarka.bin
	cd .husarz-test/user-fault && $(LD) -r -b binary przegladarka.bin -o probe-blob.o
	$(CXX) $(KERNEL_LDFLAGS) -o .husarz-test/user-fault/kernel.bin $(filter-out przegladarka_blob.o,$(OBJS)) .husarz-test/user-fault/probe-blob.o -lgcc

.PHONY: test-user-page-fault-qemu-build
test-user-page-fault-qemu-build: all
	@mkdir -p .husarz-test/user-page-fault
	$(CC) $(ASM_FLAGS) -DHUSARZ_FAULT_PAGE=1 -c narzedzia/fixtures/husarz/user_fault.S -o .husarz-test/user-page-fault/probe.o
	$(LD) --gc-sections -T $(PRZEGLADARKA_LINKER) $(RING3_LDFLAGS) .husarz-test/user-page-fault/probe.o -o .husarz-test/user-page-fault/probe.elf
	$(OBJCOPY) -O binary .husarz-test/user-page-fault/probe.elf .husarz-test/user-page-fault/przegladarka.bin
	cd .husarz-test/user-page-fault && $(LD) -r -b binary przegladarka.bin -o probe-blob.o
	$(CXX) $(KERNEL_LDFLAGS) -o .husarz-test/user-page-fault/kernel.bin $(filter-out przegladarka_blob.o,$(OBJS)) .husarz-test/user-page-fault/probe-blob.o -lgcc

.PHONY: test-husarz-stack
test-husarz-stack: przegladarka_tmp.o
	$(PYTHON) narzedzia/test_husarz_stack.py

.PHONY: test-husarz-fidelity-host
test-husarz-fidelity-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -I. narzedzia/test_husarz_fidelity.cpp -o .husarz-test/fidelity
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/fidelity

.PHONY: test-husarz-layout-host
test-husarz-layout-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -I. narzedzia/test_husarz_layout.cpp -o .husarz-test/layout
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/layout

.PHONY: test-husarz-performance-host
test-husarz-performance-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -I. narzedzia/test_husarz_performance.cpp -o .husarz-test/performance
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/performance
