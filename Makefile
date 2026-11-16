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
#   make test / test-smoke / test-security / test-regression / test-nightly
#                       - lokalna weryfikacja (bez GitHuba; docs/12_lokalna_weryfikacja.md)
#   make test-local-suite MODE=smoke|security|regression|nightly
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
	-fstack-protector-strong \
	-mstack-protector-guard=global \
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

# Nagłówki sterowników po przenosinach z korzenia (sterowniki/pci/pci.h, virtio_*.h, …).
STEROWNIKI_INCLUDES := \
	-Isterowniki/pci \
	-Isterowniki/virtio \
	-Isterowniki/magazyn \
	-Isterowniki/siec \
	-Isterowniki/wejscie \
	-Isterowniki/czas \
	-Isterowniki/platforma \
	-Isterowniki/grafika \
	-Isterowniki

# Nagłówki jądra po przenosinach do jadro/ (wariant B).
JADRO_INCLUDES := \
	-Ijadro/arch \
	-Ijadro/mm \
	-Ijadro/fs \
	-Ijadro/bws \
	-Ijadro/gui \
	-Ijadro/siec \
	-Ijadro/aktualizacja \
	-Ijadro/diag \
	-Ijadro/rdzen

# Wspólne -I dla harnessów host (g++ lokalny).
HOST_INCLUDES := -I. $(STEROWNIKI_INCLUDES) $(JADRO_INCLUDES)

MBEDTLS_CONFIG := \
	-DMBEDTLS_USER_CONFIG_FILE=\"bursztyn_mbedtls_config.h\"

KERNEL_CXXFLAGS := \
	-mgeneral-regs-only \
	-std=gnu++17 \
	-O2 -g \
	-I. \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_CXX) \
	-fno-exceptions \
	-fno-rtti \
	-fstack-usage \
	$(MBEDTLS_CONFIG) \
	$(MBEDTLS_INCLUDES) \
	$(STEROWNIKI_INCLUDES) \
	$(JADRO_INCLUDES) \
	-DBURSZTYN_DEFAULT_DISPLAY_720P=1

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
	-I. \
	$(FREESTANDING_FLAGS) \
	$(CPU_FLAGS) \
	$(HARDEN_FLAGS) \
	$(WARN_CXX) \
	-ffunction-sections \
	-fdata-sections \
	-fno-exceptions \
	-fno-rtti \
	-fno-threadsafe-statics -DBSF_NATIVE=1 -fcheck-new \
	$(STEROWNIKI_INCLUDES) \
	$(JADRO_INCLUDES)

# Explicit rollout switch: v2 (relocatable PIE + Ring 3 ASLR) is the release default; BUR_FORMAT=1 remains supported.
BUR_FORMAT ?= 2
ifeq ($(filter $(BUR_FORMAT),1 2),)
$(error BUR_FORMAT must be 1 or 2)
endif
ifeq ($(BUR_FORMAT),2)
RING3_FLAGS := $(filter-out -mcmodel=large -fno-pic -fno-pie,$(RING3_FLAGS)) -mcmodel=small -fPIE -fvisibility=hidden
BUR_CONVERT = $(PYTHON) narzedzia/elftoburv2.py
bur_linker = bur_v2_linker.ld
BUR_ORPHANS =
else
BUR_CONVERT = $(OBJCOPY) -O binary
bur_linker = $(1)
BUR_ORPHANS = --orphan-handling=error
endif

BURSZTYN_DIAG_FORCE_TAPETY_PIE_BASE ?=
ifneq ($(strip $(BURSZTYN_DIAG_FORCE_TAPETY_PIE_BASE)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_FORCE_TAPETY_PIE_BASE=$(BURSZTYN_DIAG_FORCE_TAPETY_PIE_BASE)ULL
endif
BURSZTYN_DIAG_LATE_KMAP ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_LATE_KMAP)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_LATE_KMAP=1
endif
BURSZTYN_DIAG_VMM_SELFTEST ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_VMM_SELFTEST)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_VMM_SELFTEST=1
endif
BURSZTYN_DIAG_VMM_NO_SYNC ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_VMM_NO_SYNC)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_VMM_NO_SYNC=1
endif
BURSZTYN_DIAG_RESIZE_LATENCY ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_RESIZE_LATENCY)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_RESIZE_LATENCY=1
endif
# Log cyklu zycia procesow (wolne ramki PMM przy zwolnieniu slotu) - pomiar wyciekow w testach natywnych. Domyslnie wylaczone.
BURSZTYN_DIAG_PROC_LIFECYCLE ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_PROC_LIFECYCLE)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_PROC_LIFECYCLE=1
endif
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

# Metryki kosztu skladania/prezentacji sceny. Wylaczone w obrazie produkcyjnym.
BURSZTYN_DEBUG_PERF ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DEBUG_PERF)),)
	KERNEL_CXXFLAGS += -DBURSZTYN_DEBUG_PERF=1
endif

# Metryki ruchu kursora, drag i coalescingu dirty. Wylaczone domyslnie.
BURSZTYN_DEBUG_GUI_PERF ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DEBUG_GUI_PERF)),)
	KERNEL_CXXFLAGS += -DBURSZTYN_DEBUG_GUI_PERF=1
endif

# Pomiar etapow BWS10/BUR bez zmiany semantyki loadera.
BURSZTYN_DEBUG_LOADER_PERF ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DEBUG_LOADER_PERF)),)
	KERNEL_CXXFLAGS += -DBURSZTYN_DEBUG_LOADER_PERF=1
endif

# DIAG: N pustych plikow w /uzytkownicy/Pulpit/duzy (test duzych katalogow Eksploratora).
BURSZTYN_DIAG_FIXTURE_FILES ?= 0
ifneq ($(BURSZTYN_DIAG_FIXTURE_FILES),0)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_FIXTURE_FILES=$(BURSZTYN_DIAG_FIXTURE_FILES)
endif

# A/B: stare aktywne oczekiwanie (pause) na zakonczenie komendy VirtIO-GPU.
BURSZTYN_GPU_POLL_WAIT ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_GPU_POLL_WAIT)),)
KERNEL_CXXFLAGS += -DBURSZTYN_GPU_POLL_WAIT=1
endif
virtio_gpu.o kernel.o: .build-config/gui-perf

# Lokalny serwer HTTPS z testowym root CA (tylko diagnostyka; patrz tls.cpp):
#   make iso BURSZTYN_DIAG_TEST_CA=1  (generuje jadro/siec/tls_test_ca.generated.h z .tls-fixture/ca.pem)
BURSZTYN_DIAG_TEST_CA ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_TEST_CA)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_TEST_CA=1
tls.o: jadro/siec/tls_test_ca.generated.h
jadro/siec/tls_test_ca.generated.h:
	testy/husarz_load/make_tls_fixture.sh
	$(PYTHON) narzedzia/generuj_test_ca.py .tls-fixture/ca.pem $@
endif

# Pomiar kosztu kryptografii handshake'u w gosciu ([TLS COST] w logu szeregowym).
BURSZTYN_DIAG_TLS_COST ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_TLS_COST)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_TLS_COST=1
endif

# A/B pomiarow: wylacza kooperacyjne oddawanie CPU w obliczeniach TLS.
BURSZTYN_DIAG_NO_YIELD ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_NO_YIELD)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_NO_YIELD=1
endif

# A/B pomiarow: wylacza cien warstwy (tear-free prezentacje).
BURSZTYN_DIAG_NO_SHADOW ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_NO_SHADOW)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_NO_SHADOW=1
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
BURSZTYN_DIAG_RESIZE_ROUTING ?= 0
ifneq ($(filter 1 yes true,$(BURSZTYN_DIAG_RESIZE_ROUTING)),)
KERNEL_CXXFLAGS += -DBURSZTYN_DIAG_RESIZE_ROUTING=1
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
	-T jadro/arch/linker.ld \
	-nostdlib \
	-no-pie \
	-static \
	-Wl,-z,noexecstack \
	-Wl,-z,max-page-size=0x1000 \
	-Wl,--build-id=none

RING3_LDFLAGS := \
	-nostdlib \
	-no-pie \
	--gc-sections \
	--undefined=__stack_chk_guard \
	--undefined=__stack_chk_fail

ifeq ($(BUR_FORMAT),2)
RING3_LDFLAGS := $(filter-out -no-pie,$(RING3_LDFLAGS)) -shared -Bsymbolic --no-undefined -z text
endif

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
RING3_STD_LINKER := programy/shell/shell_linker.ld

NOTATNIK_LINKER := programy/notatnik/notatnik_linker.ld

KALKULATOR_LINKER := programy/kalkulator/kalkulator_linker.ld

EKSPLORATOR_LINKER := programy/eksplorator/eksplorator_linker.ld

PRZEGLADARKA_LINKER := programy/przegladarka/przegladarka_linker.ld

AKTUALIZACJE_LINKER := programy/aktualizacje/aktualizacje_linker.ld

USTAWIENIA_LINKER := programy/ustawienia/ustawienia_linker.ld

MENEDZER_OKIEN_LINKER := programy/menedzer_okien/menedzer_okien_linker.ld

# ============================================================================
# 6. MBEDTLS
# ============================================================================

MBEDTLS_SRCS := $(wildcard biblioteki/mbedtls/library/*.c)
MBEDTLS_OBJS := $(MBEDTLS_SRCS:.c=.o)

# ============================================================================
# 7. OBIEKTY JADRA
# ============================================================================

OBJS := entropia.o instalator.o instalator_blob.o storage_format.o blokowy.o ekran_blob.o ekran.o ekran_format.o bufory_grafiki.o bochs_vbe.o tapety_blob.o tapety.o btp.o \
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
	fpu_probe_blob.o \
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
	stack_protector.o \
	notatnik_blob.o \
	eksplorator_blob.o \
	kalkulator_blob.o \
	loader.o \
	podpis_paczki.o \
	czcionka_systemowa.o \
	kernel.o \
	shell_blob.o \
	menedzer_okien_blob.o \
	uefi_gop.o \
	dzwiek_blob.o \
	cytaty_blob.o \
	sponsorzy_blob.o \
	wsparcie_mecenasi_blob.o \
	wsparcie_sponsorzy_blob.o \
	wsparcie_wkladtworczy_blob.o \
	ikona_zasilania_blob.o \
	ikona_uruchomponownie_blob.o \
	ikona_zamknij_blob.o \
	ikona_uspij_blob.o \
	ikona_zablokuj_blob.o \
	ikona_nav_wstecz_blob.o \
	ikona_nav_dalej_blob.o \
	ikona_nav_wgore_blob.o \
	ikona_folder_blob.o \
	ikona_plik_blob.o \
	ikona_app_notatnik_blob.o \
	ikona_app_kalkulator_blob.o \
	ikona_app_husarz_blob.o \
	ikona_app_testy_blob.o \
	ikona_app_snake_blob.o \
	ikona_app_eksplorator_blob.o \
	ikona_app_obrazy_blob.o \
	ikona_app_demo_ui_blob.o \
	ikona_app_kontrolki2_blob.o \
	ikona_app_kontrolki3_blob.o \
	ikona_app_kontrolki4_blob.o \
	ikona_app_aktualizacje_blob.o \
	ikona_app_ustawienia_blob.o \
	ikona_app_test_dzwieku_blob.o \
	ikona_app_diagnostyka_blob.o \
	ikona_app_shell_blob.o \
	ikona_app_tapety_blob.o \
	heap.o \
	scheduler.o \
	fpu.o \
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
	okno_informacyjne_blob.o \
	przegladarka_obrazow_blob.o \
	aktualizacje_blob.o \
	motyw.o \
	test_dzwieku_blob.o \
	diagnostyka_systemu.o diagnostyka_systemu_bws.o diagnostyka_systemu_boot.o diagnostyka_blob.o \
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
	prepare-disk clean clear distclean cdysk rm help \
	test test-smoke test-security test-security-host test-regression test-nightly \
	test-local-suite

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


# --- jadro/* explicit object rules (wariant B) ---
backend.o: jadro/gui/backend.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
bezpieczenstwo.o: jadro/bws/bezpieczenstwo.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
boot.o: jadro/arch/boot.S
	$(CC) $(ASM_FLAGS) -c $< -o $@
bufory_grafiki.o: jadro/gui/bufory_grafiki.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
czcionka_systemowa.o: jadro/gui/czcionka_systemowa.cpp bsf/unicode_layout_data.inc bsf/unicode_layout_index.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
diagnostyka_systemu.o: jadro/diag/diagnostyka_systemu.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
diagnostyka_systemu_boot.o: jadro/diag/diagnostyka_systemu_boot.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
diagnostyka_systemu_bws.o: jadro/diag/diagnostyka_systemu_bws.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
ekran.o: jadro/gui/ekran.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
ekran_format.o: jadro/gui/ekran_format.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
ekran_powitalny.o: jadro/gui/ekran_powitalny.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
fpu.o: jadro/arch/fpu.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
frontend.o: jadro/gui/frontend.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
gdt.o: jadro/arch/gdt.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
gui_image.o: jadro/gui/gui_image.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
heap.o: jadro/mm/heap.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
idt.o: jadro/arch/idt.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
instalator.o: jadro/gui/instalator.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
kernel.o: jadro/rdzen/kernel.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
loader.o: jadro/fs/loader.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
podpis_paczki.o: jadro/fs/podpis_paczki.cpp jadro/fs/podpis_paczki.h jadro/aktualizacja/aktualizacja_klucz.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
missing_backend.o: jadro/gui/missing_backend.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
motyw.o: jadro/gui/motyw.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
pmm.o: jadro/mm/pmm.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
przerwania.o: jadro/arch/przerwania.S
	$(CC) $(ASM_FLAGS) -c $< -o $@
psf.o: jadro/fs/psf.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
ring3.o: jadro/arch/ring3.S
	$(CC) $(ASM_FLAGS) -c $< -o $@
scheduler.o: jadro/bws/scheduler.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
siec.o: jadro/siec/siec.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
skladacz_obrazu.o: jadro/gui/skladacz_obrazu.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
storage_format.o: jadro/fs/storage_format.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
stos_jadra.o: jadro/mm/stos_jadra.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
syscall.o: jadro/arch/syscall.S
	$(CC) $(ASM_FLAGS) -c $< -o $@
tapety.o: jadro/gui/tapety.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
test.o: jadro/rdzen/test.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
tls.o: jadro/siec/tls.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
tss.o: jadro/arch/tss.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@
vmm.o: jadro/mm/vmm.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

# ============================================================================
# KONFIGURACJA KLUCZA AKTUALIZACJI
# ============================================================================
# Domyslnie kompilujemy z kluczem wydania (release).
# Opcje: release, test, dev (z BURSZTYN_AKT_KLUCZ_PUBLICZNY=<plik.pem>)
BURSZTYN_AKT_TRUST_ROOT ?= release

.PHONY: .aktualizacja_klucz_check
.aktualizacja_klucz_check:
	@$(PYTHON) narzedzia/aktualizacje/generuj_naglowek_klucza.py --tryb $(BURSZTYN_AKT_TRUST_ROOT) $(if $(BURSZTYN_AKT_KLUCZ_PUBLICZNY),--publiczny $(BURSZTYN_AKT_KLUCZ_PUBLICZNY),) --wyjscie jadro/aktualizacja/aktualizacja_klucz.h

jadro/aktualizacja/aktualizacja_klucz.h: .aktualizacja_klucz_check

aktualizacja.o: jadro/aktualizacja/aktualizacja.cpp jadro/aktualizacja/aktualizacja.h jadro/aktualizacja/aktualizacja_format.h jadro/aktualizacja/aktualizacja_klucz.h .aktualizacja_klucz_check
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

mbedtls_port.o: biblioteki/mbedtls/mbedtls_port.cpp biblioteki/mbedtls/bursztyn_mbedtls_config.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

# Bounded resource tables in the modern BWS layer are intentionally dynamic;
# optimize its dispatcher for size so the fixed pre-Ring3 kernel VA contract
# (kernel <= 14 MiB with >= 2 MiB guard below RING3_BASE at 16 MiB) remains intact.
bws_nowoczesne.o: jadro/bws/bws_nowoczesne.cpp jadro/bws/bws_nowoczesne.h jadro/bws/bws_api.h jadro/bws/bws_hda_diag.h sterowniki/dzwiek/hda.h sterowniki/dzwiek/hda_diag.h sterowniki/dzwiek/hda_diag_format.h
	$(CXX) $(KERNEL_CXXFLAGS) -Os -c $< -o $@

syscalls.o: jadro/bws/syscalls.cpp jadro/bws/bws_api.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

hda.o: sterowniki/dzwiek/hda.cpp sterowniki/dzwiek/hda_transport.h sterowniki/dzwiek/hda.h sterowniki/dzwiek/hda_wewnetrzny.h sterowniki/dzwiek/hda_diag_format.h jadro/mm/pamiec.h sterowniki/pci/pci.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

hda_transport.o: sterowniki/dzwiek/hda_transport.cpp sterowniki/dzwiek/hda_transport.h sterowniki/dzwiek/hda_diag_format.h jadro/mm/pamiec.h sterowniki/pci/pci.h sterowniki/dma.h sterowniki/czas/hpet.h
	$(CXX) $(KERNEL_CXXFLAGS) -c sterowniki/dzwiek/hda_transport.cpp -o $@

hda_diag.o: sterowniki/dzwiek/hda_diag.cpp sterowniki/dzwiek/hda_transport.h sterowniki/dzwiek/hda_diag.h sterowniki/dzwiek/hda_diag_format.h sterowniki/dzwiek/hda_wewnetrzny.h sterowniki/dzwiek/hda.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

uefi_gop.o: sterowniki/grafika/uefi_gop.cpp sterowniki/grafika/uefi_gop.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

hpet.o: sterowniki/czas/hpet.cpp sterowniki/czas/hpet.h sterowniki/platforma/acpi.h jadro/mm/pamiec.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

dma.o: sterowniki/dma.cpp sterowniki/dma.h jadro/mm/pamiec.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

usb.o: sterowniki/usb/usb.cpp sterowniki/usb/usb.h sterowniki/usb/xhci.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

xhci.o: sterowniki/usb/xhci.cpp sterowniki/usb/xhci.h sterowniki/usb/xhci_ring.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

xhci_ring.o: sterowniki/usb/xhci_ring.cpp sterowniki/usb/xhci_ring.h sterowniki/usb/xhci_trb.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

usb_hid.o: sterowniki/usb/usb_hid.cpp sterowniki/usb/usb_hid.h sterowniki/wejscie/input_device.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

pci.o: sterowniki/pci/pci.cpp sterowniki/pci/pci.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

virtqueue.o: sterowniki/virtio/virtqueue.cpp sterowniki/virtio/virtqueue.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

virtio_pci.o: sterowniki/virtio/virtio_pci.cpp sterowniki/virtio/virtio_pci.h sterowniki/virtio/virtqueue.h sterowniki/pci/pci.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

virtio_gpu.o: sterowniki/virtio/virtio_gpu.cpp sterowniki/virtio/virtio_gpu.h sterowniki/virtio/virtio_pci.h sterowniki/virtio/virtqueue.h sterowniki/pci/pci.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

virtio_rng.o: sterowniki/virtio/virtio_rng.cpp sterowniki/virtio/virtio_rng.h sterowniki/virtio/virtio_pci.h sterowniki/virtio/virtqueue.h sterowniki/pci/pci.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

virtio_blk.o: sterowniki/virtio/virtio_blk.cpp sterowniki/virtio/virtio_blk.h sterowniki/virtio/virtio_pci.h sterowniki/virtio/virtqueue.h sterowniki/pci/pci.h sterowniki/magazyn/blokowy.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

virtio_net.o: sterowniki/virtio/virtio_net.cpp sterowniki/virtio/virtio_net.h sterowniki/virtio/virtio_pci.h sterowniki/virtio/virtqueue.h sterowniki/pci/pci.h sterowniki/siec/network_device.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

virtio_input.o: sterowniki/virtio/virtio_input.cpp sterowniki/virtio/virtio_input.h sterowniki/virtio/virtio_pci.h sterowniki/virtio/virtqueue.h sterowniki/pci/pci.h sterowniki/wejscie/input_device.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

ahci.o: sterowniki/magazyn/ahci.cpp sterowniki/magazyn/ahci.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

blokowy.o: sterowniki/magazyn/blokowy.cpp sterowniki/magazyn/blokowy.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

e1000.o: sterowniki/siec/e1000.cpp sterowniki/siec/e1000.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

network_device.o: sterowniki/siec/network_device.cpp sterowniki/siec/network_device.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

klawiatura.o: sterowniki/wejscie/klawiatura.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

mysz.o: sterowniki/wejscie/mysz.cpp
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

input_device.o: sterowniki/wejscie/input_device.cpp sterowniki/wejscie/input_device.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

acpi.o: sterowniki/platforma/acpi.cpp sterowniki/platforma/acpi.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

apic.o: sterowniki/platforma/apic.cpp sterowniki/platforma/apic.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

grafika.o: sterowniki/grafika/grafika.cpp sterowniki/grafika/grafika.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

zegar-rtc.o: sterowniki/czas/zegar-rtc.cpp sterowniki/czas/zegar-rtc.h
	$(CXX) $(KERNEL_CXXFLAGS) -I. -c $< -o $@

# ============================================================================
# 11. BIBLIOTEKA GUI RING 3
# ============================================================================

stack_protector.o: jadro/mm/stack_protector.cpp jadro/mm/stack_protector.h jadro/mm/stos_jadra.h jadro/bws/scheduler.h
	$(CXX) $(KERNEL_CXXFLAGS) -fno-stack-protector -c $< -o $@

ring3_ssp.o: jadro/mm/ring3_ssp.cpp jadro/mm/stack_protector.h jadro/bws/bws_api.h
	$(CXX) $(RING3_FLAGS) -fno-stack-protector -Os -ffunction-sections -fdata-sections -c $< -o $@

# BSF 2.0 text layout (UAX #9, OpenType GSUB/GPOS, shared layout). Ring 3
# only; no libc mem* symbols exist there, so loop->memcpy idioms are disabled.
# Host builds link the static Unicode blob; Ring 3 fetches it via BWS 51/2.
BSF2_CORE := bsf/unicode_props.cpp bsf/bidi.cpp bsf/otl.cpp bsf/shape.cpp bsf/layout.cpp
BSF2_SOURCES := $(BSF2_CORE) bsf/unicode_layout_static.cpp
BSF2_HEADERS := bsf/text.h bsf/otl.h bsf/shape.h bsf/layout.h bsf/bsf.h bsf/unicode_layout.h bsf/unicode_layout_index.h
BSF2_OBJECTS := bsf2_props.o bsf2_bidi.o bsf2_otl.o bsf2_shape.o bsf2_layout.o bsf2_unicode_bws.o
BSF2_FLAGS = $(RING3_FLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-tree-loop-distribute-patterns -Os -fstack-usage
bsf2_unicode_bws.o: bsf/unicode_layout_bws.cpp $(BSF2_HEADERS) jadro/gui/bursztyn_gui.h
	$(CXX) $(BSF2_FLAGS) -c $< -o $@
bsf2_props.o: bsf/unicode_props.cpp $(BSF2_HEADERS)
	$(CXX) $(BSF2_FLAGS) -c $< -o $@
bsf2_bidi.o: bsf/bidi.cpp $(BSF2_HEADERS)
	$(CXX) $(BSF2_FLAGS) -c $< -o $@
# BSF 2.2: GPOS VariationIndex deltas use the float ItemVariationStore code
# (same SSE policy as bsf_core.o).
bsf2_otl.o: bsf/otl.cpp $(BSF2_HEADERS)
	$(CXX) $(BSF2_FLAGS) -msse2 -mfpmath=sse -mno-avx -c $< -o $@
bsf2_shape.o: bsf/shape.cpp $(BSF2_HEADERS)
	$(CXX) $(BSF2_FLAGS) -c $< -o $@
bsf2_layout.o: bsf/layout.cpp $(BSF2_HEADERS)
	$(CXX) $(BSF2_FLAGS) -c $< -o $@
-include bsf2_props.d bsf2_bidi.d bsf2_otl.d bsf2_shape.d bsf2_layout.d

bursztyn_gui.o: bursztyn_gui_base.o bsf_core.o bsf_runtime.o bsf_bitmap.o $(BSF2_OBJECTS) ring3_ssp.o
	$(LD) -r bursztyn_gui_base.o bsf_core.o bsf_runtime.o bsf_bitmap.o $(BSF2_OBJECTS) ring3_ssp.o -o $@

# Legacy LTR-only GUI library (BSF_COMPLEX_TEXT=0), kept as an explicit
# per-app opt-out. Every app is embedded in the kernel image, which must end
# below 15 MiB (RING3_BASE - 1 MiB guard). Since the desktop sample BMPs moved
# to Multiboot2 modules (2026-11-11) there is room for the BSF 2.0 engine in
# every GUI app, so the list is empty: to opt an app out, add its *_tmp.o
# here AND link it with $(BSF_APP_GUI) instead of $(BSF_APP_GUI).
BSF_LTR_APPS_TMP :=
BSF_APP_GUI := bursztyn_gui.o
$(BSF_LTR_APPS_TMP): RING3_FLAGS += -DBSF_COMPLEX_TEXT=0
bursztyn_gui_base_ltr.o: jadro/gui/bursztyn_gui.cpp jadro/gui/bursztyn_gui.h bsf/runtime.h
	$(CXX) $(RING3_FLAGS) -DBSF_COMPLEX_TEXT=0 -Os -ffunction-sections -fdata-sections -c $< -o $@
bsf_runtime_ltr.o: bsf/runtime.cpp bsf/runtime.h bsf/config.h $(BSF2_HEADERS)
	$(CXX) $(RING3_FLAGS) -DBSF_COMPLEX_TEXT=0 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Os -fstack-usage -c $< -o $@
bursztyn_gui_ltr.o: bursztyn_gui_base_ltr.o bsf_core.o bsf_runtime_ltr.o bsf_bitmap.o ring3_ssp.o
	$(LD) -r bursztyn_gui_base_ltr.o bsf_core.o bsf_runtime_ltr.o bsf_bitmap.o ring3_ssp.o -o $@

bursztyn_gui_base.o: jadro/gui/bursztyn_gui.cpp jadro/gui/bursztyn_gui.h bsf/runtime.h
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@

# Background resource workers/FPU probes have no font renderer.
bursztyn_gui_minimal_base.o: jadro/gui/bursztyn_gui.cpp jadro/gui/bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -UBSF_NATIVE -Os -ffunction-sections -fdata-sections -c $< -o $@

bursztyn_gui_minimal.o: bursztyn_gui_minimal_base.o ring3_ssp.o
	$(LD) -r bursztyn_gui_minimal_base.o ring3_ssp.o -o $@

BSF_CORE_INCLUDES := bsf/cff.inc bsf/variations.inc bsf/inflate.inc bsf/png.inc bsf/color.inc
bsf_core.o: bsf/bsf.cpp bsf/bsf.h bsf/vendor/stb_truetype.h $(BSF_CORE_INCLUDES)
	$(CXX) $(RING3_FLAGS) -msse2 -mfpmath=sse -mno-avx -Os -fstack-usage -c $< -o $@

bsf_runtime.o: bsf/runtime.cpp bsf/runtime.h bsf/config.h $(BSF2_HEADERS)
	$(CXX) $(RING3_FLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Os -fstack-usage -c $< -o $@

bsf_bitmap.o: bsf/bitmap.cpp bsf/runtime.h
	$(CXX) $(RING3_FLAGS) -Os -c $< -o $@

-include bursztyn_gui_base.d bsf_core.d bsf_runtime.d bsf_bitmap.d bursztyn_gui_minimal.d

# ============================================================================
# 12. SHELL .bur
# ============================================================================

shell_tmp.o: programy/shell/shell.cpp programy/shell/shell_terminal.h programy/shell/shell_context.h programy/shell/shell_sponsors.h jadro/rdzen/wersja_systemu.h jadro/gui/bursztyn_gui.h bursztyn_ui/czcionka.h bursztyn_ui/rysowanie.h bursztyn_ui/pasek_przewijania.h bursztyn_ui/menu_kontekstowe.h
	$(CXX) $(RING3_FLAGS) -I. -Os -fstack-usage -c $< -o $@

.PHONY: shellblob.o
shellblob.o: shell_blob.o

shell_blob.o: shell_tmp.o bsf_terminal.o bursztyn_gui.o programy/shell/shell_bsf_linker.ld
	$(LD) -T $(call bur_linker,programy/shell/shell_bsf_linker.ld) $(RING3_LDFLAGS) \
		shell_tmp.o bsf_terminal.o bursztyn_gui.o -o shell.elf
	$(BUR_CONVERT) shell.elf shell.bin
	$(PYTHON) narzedzia/kompresuj_blob.py shell.bin shell.bin.bz
	$(LD) -r -b binary shell.bin.bz -o shell_blob.o

# ============================================================================
# 13. PRZEGLADARKA HUSSAR .bur
# ============================================================================

HUSARZ_DEBUG ?= 0
HUSARZ_RESOURCE_DEBUG ?= $(HUSARZ_DEBUG)
HUSARZ_PERF ?= 0
BSF_PROFILE ?= 0
.PHONY: bsf-profile-config-force
.bsf-test/profile-build-config: bsf-profile-config-force
	@mkdir -p .bsf-test
	@$(PYTHON) -c "from pathlib import Path; p=Path('$@'); v='profile=$(BSF_PROFILE)'; p.write_text(v) if not p.exists() or p.read_text()!=v else None"
menedzer_okien_tmp.o: .bsf-test/profile-build-config
.PHONY: husarz-build-config-force
.husarz-test/build-config: husarz-build-config-force
	@mkdir -p .husarz-test
	@$(PYTHON) -c "from pathlib import Path; p=Path('$@'); v='debug=$(HUSARZ_RESOURCE_DEBUG) perf=$(HUSARZ_PERF)'; p.write_text(v) if not p.exists() or p.read_text()!=v else None"
przegladarka_tmp.o husarz_worker_tmp.o: .husarz-test/build-config jadro/siec/http_target.h
tls.o: jadro/siec/http_target.h
przegladarka_tmp.o: programy/przegladarka/przegladarka.cpp Makefile programy/http_kody.h jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h) $(wildcard programy/husarz/*.h) $(wildcard programy/husarz/*.inc)
	$(CXX) $(RING3_FLAGS) -msse2 -mfpmath=sse -mno-avx -fcoroutines -fcheck-new -DHUSARZ_DEBUG_RESOURCES=$(HUSARZ_RESOURCE_DEBUG) -DHUSARZ_PERF=$(HUSARZ_PERF) -I. -Os -fstack-usage -ffunction-sections -fdata-sections -c $< -o $@

przegladarka_blob.o: przegladarka_tmp.o bursztyn_gui.o image_ring3.o btp_ring3.o $(PRZEGLADARKA_LINKER)
	$(LD) --gc-sections -T $(call bur_linker,$(PRZEGLADARKA_LINKER)) $(RING3_LDFLAGS) \
		przegladarka_tmp.o bursztyn_gui.o image_ring3.o btp_ring3.o -o przegladarka.elf
	$(BUR_CONVERT) przegladarka.elf przegladarka.bin
	$(PYTHON) narzedzia/kompresuj_blob.py przegladarka.bin przegladarka.bin.bz
	$(LD) -r -b binary przegladarka.bin.bz -o przegladarka_blob.o

husarz_worker_tmp.o: programy/przegladarka/przegladarka.cpp Makefile $(wildcard programy/husarz/*.h) $(wildcard programy/husarz/*.inc)
	$(CXX) $(RING3_FLAGS) -msse2 -mfpmath=sse -mno-avx -fcoroutines -fcheck-new -DHUSARZ_DEBUG_RESOURCES=$(HUSARZ_RESOURCE_DEBUG) -DHUSARZ_PERF=$(HUSARZ_PERF) -DHUSARZ_WORKER_ONLY=1 -Wno-unused-function -I. -Os -fstack-usage -ffunction-sections -fdata-sections -c $< -o $@

# BSF 2.2: the resource worker decodes WOFF/WOFF2 (web fonts are parsed in
# this separate low-privilege process; Brotli is linked nowhere else).
# Vendored Brotli decoder (bsf/vendor/brotli, v1.1.0, MIT) — WOFF2 only.
BROTLI_DEC_SOURCES := $(wildcard bsf/vendor/brotli/dec/*.c) $(addprefix bsf/vendor/brotli/common/,constants.c context.c dictionary.c platform.c shared_dictionary.c transform.c)
RING3_CFLAGS = $(filter-out -std=gnu++17 -fno-rtti -fno-threadsafe-statics -fcheck-new $(WARN_CXX),$(RING3_FLAGS)) -std=gnu11 -w
BROTLI_RING3_OBJECTS := $(patsubst bsf/vendor/brotli/%.c,.ring3-brotli/%.o,$(BROTLI_DEC_SOURCES))
.ring3-brotli/%.o: bsf/vendor/brotli/%.c
	@mkdir -p $(dir $@)
	$(CC) $(RING3_CFLAGS) -Ibsf/vendor/brotli/include -Os -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -c $< -o $@
webfont_ring3.o: bsf/webfont.cpp bsf/webfont.h bsf/bsf.h
	$(CXX) $(RING3_FLAGS) -Ibsf/vendor/brotli/include -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Os -c $< -o $@
HUSARZ_WORKER_FONT_OBJECTS := bsf_core.o webfont_ring3.o $(BROTLI_RING3_OBJECTS)
husarz_worker_blob.o: husarz_worker_tmp.o bursztyn_gui_minimal.o image_ring3.o btp_ring3.o $(HUSARZ_WORKER_FONT_OBJECTS) $(PRZEGLADARKA_LINKER)
	$(LD) --defsym=HUSARZ_WORKER_BUILD=1 --gc-sections -T $(call bur_linker,$(PRZEGLADARKA_LINKER)) $(RING3_LDFLAGS) husarz_worker_tmp.o bursztyn_gui_minimal.o image_ring3.o btp_ring3.o $(HUSARZ_WORKER_FONT_OBJECTS) -o husarz_worker.elf
	$(BUR_CONVERT) husarz_worker.elf husarz_worker.bin
	$(PYTHON) narzedzia/kompresuj_blob.py husarz_worker.bin husarz_worker.bin.bz
	$(LD) -r -b binary husarz_worker.bin.bz -o husarz_worker_blob.o

# ============================================================================
# 14. NOTATNIK .bur
# ============================================================================

notatnik_tmp.o: programy/notatnik/notatnik.cpp jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

notatnik_blob.o: notatnik_tmp.o bursztyn_gui.o $(NOTATNIK_LINKER)
	$(LD) --gc-sections -T $(call bur_linker,$(NOTATNIK_LINKER)) $(RING3_LDFLAGS) \
		notatnik_tmp.o bursztyn_gui.o -o notatnik.elf
	$(BUR_CONVERT) notatnik.elf notatnik.bin
	$(PYTHON) narzedzia/kompresuj_blob.py notatnik.bin notatnik.bin.bz
	$(LD) -r -b binary notatnik.bin.bz -o notatnik_blob.o

# ============================================================================
# 15. EKSPLORATOR PLIKOW .bur
# ============================================================================

eksplorator_tmp.o: programy/eksplorator/eksplorator-plikow.cpp jadro/gui/desktop_entries.h jadro/gui/bursztyn_gui.h jadro/gui/plik_popup_ui.h jadro/gui/plik_operacje_ui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -fstack-usage -c $< -o $@

eksplorator_blob.o: eksplorator_tmp.o bursztyn_gui.o $(EKSPLORATOR_LINKER)
	$(LD) -T $(call bur_linker,$(EKSPLORATOR_LINKER)) $(RING3_LDFLAGS) \
		eksplorator_tmp.o bursztyn_gui.o -o eksplorator.elf
	$(BUR_CONVERT) eksplorator.elf eksplorator.bin
	$(PYTHON) narzedzia/kompresuj_blob.py eksplorator.bin eksplorator.bin.bz
	$(LD) -r -b binary eksplorator.bin.bz -o eksplorator_blob.o

# ============================================================================
# 16. KALKULATOR .bur
# ============================================================================

kalkulator_tmp.o: programy/kalkulator/kalkulator.cpp jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

kalkulator_blob.o: kalkulator_tmp.o $(BSF_APP_GUI) $(KALKULATOR_LINKER)
	$(LD) --gc-sections -T $(call bur_linker,$(KALKULATOR_LINKER)) $(RING3_LDFLAGS) \
		kalkulator_tmp.o $(BSF_APP_GUI) -o kalkulator.elf
	$(BUR_CONVERT) kalkulator.elf kalkulator.bin
	$(PYTHON) narzedzia/kompresuj_blob.py kalkulator.bin kalkulator.bin.bz
	$(LD) -r -b binary kalkulator.bin.bz -o kalkulator_blob.o

# Test
test_tmp.o: programy/test/test.cpp jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

test_blob.o: test_tmp.o ekran_format_ring3.o btp_ring3.o $(BSF_APP_GUI) programy/test/test_linker.ld
	$(LD) --gc-sections -T $(call bur_linker,programy/test/test_linker.ld) $(RING3_LDFLAGS) \
		test_tmp.o ekran_format_ring3.o btp_ring3.o $(BSF_APP_GUI) -o test.elf
	$(BUR_CONVERT) test.elf test.bin
	$(PYTHON) narzedzia/kompresuj_blob.py test.bin test.bin.bz
	$(LD) -r -b binary test.bin.bz -o test_blob.o

# BursztynUI Demo
demo_ui_tmp.o: programy/demo_ui/demo_ui.cpp jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@

demo_ui_blob.o: demo_ui_tmp.o $(BSF_APP_GUI) programy/demo_ui/demo_ui_linker.ld
	$(LD) --gc-sections -T $(call bur_linker,programy/demo_ui/demo_ui_linker.ld) $(RING3_LDFLAGS) \
		demo_ui_tmp.o $(BSF_APP_GUI) -o demo_ui.elf
	$(BUR_CONVERT) demo_ui.elf demo_ui.bin
	$(PYTHON) narzedzia/kompresuj_blob.py demo_ui.bin demo_ui.bin.bz
	$(LD) -r -b binary demo_ui.bin.bz -o demo_ui_blob.o

# BursztynUI Kontrolki 2
kontrolki2_tmp.o: programy/kontrolki2/kontrolki2.cpp jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@

kontrolki2_blob.o: kontrolki2_tmp.o $(BSF_APP_GUI) programy/demo_ui/demo_ui_linker.ld
	$(LD) --gc-sections -T $(call bur_linker,programy/demo_ui/demo_ui_linker.ld) $(RING3_LDFLAGS) \
		kontrolki2_tmp.o $(BSF_APP_GUI) -o kontrolki2.elf
	$(BUR_CONVERT) kontrolki2.elf kontrolki2.bin
	$(PYTHON) narzedzia/kompresuj_blob.py kontrolki2.bin kontrolki2.bin.bz
	$(LD) -r -b binary kontrolki2.bin.bz -o kontrolki2_blob.o

# BursztynUI Kontrolki 3. Zaleznosc wildcard wymusza przebudowe aplikacji
# po zmianie dowolnej publicznej kontrolki; analogiczna zaleznosc test_tmp.o
# znajduje sie powyzej.
kontrolki3tmp.o: programy/kontrolki3/kontrolki3.cpp jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

kontrolki3blob.o: kontrolki3tmp.o $(BSF_APP_GUI) programy/kontrolki3/kontrolki3_linker.ld
	$(LD) --gc-sections -T $(call bur_linker,programy/kontrolki3/kontrolki3_linker.ld) $(RING3_LDFLAGS) \
		kontrolki3tmp.o $(BSF_APP_GUI) -o kontrolki3.elf
	$(BUR_CONVERT) kontrolki3.elf kontrolki3.bin
	$(PYTHON) narzedzia/kompresuj_blob.py kontrolki3.bin kontrolki3.bin.bz
	$(LD) -r -b binary kontrolki3.bin.bz -o kontrolki3blob.o

# BursztynUI Kontrolki 4 - Showroom nowych komponentow retained-mode.
kontrolki4tmp.o: programy/kontrolki4/kontrolki4.cpp jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

kontrolki4blob.o: kontrolki4tmp.o $(BSF_APP_GUI) programy/kontrolki4/kontrolki4_linker.ld
	$(LD) --gc-sections -T $(call bur_linker,programy/kontrolki4/kontrolki4_linker.ld) $(RING3_LDFLAGS) \
		kontrolki4tmp.o $(BSF_APP_GUI) -o kontrolki4.elf
	$(BUR_CONVERT) kontrolki4.elf kontrolki4.bin
	$(PYTHON) narzedzia/kompresuj_blob.py kontrolki4.bin kontrolki4.bin.bz
	$(LD) -r -b binary kontrolki4.bin.bz -o kontrolki4blob.o

# Obrazy testowe pulpitu (test.bmp, test1.bmp) nie sa linkowane do jadra:
# narzedzia/package_desktop_images.py dodaje je jako moduly Multiboot2
# (etykiety pulpit/test.bmp, pulpit/test1.bmp) do kazdego wpisu GRUB.
DESKTOP_IMAGES := obrazy/test.bmp obrazy/test1.bmp
kernel.o: jadro/rdzen/obrazy_startowe.h

# Logo systemu logo.png
logo_png_blob.o: obrazy/logo.png
	$(LD) -r -b binary obrazy/logo.png -o logo_png_blob.o

# Maska pokrycia logotypu dla ekranu powitalnego. Jadro nie dekoduje PNG na
# sciezce startu: naglowek jest generowany z obrazy/logo.png i wersjonowany.
LOGO_MASKA_H := jadro/gui/logo_maska_dane.h
$(LOGO_MASKA_H): obrazy/logo.png narzedzia/generuj_maske_logo.py
	$(PYTHON) narzedzia/generuj_maske_logo.py --zrodlo obrazy/logo.png --wyjscie $@

# Maski napisow ekranu powitalnego. Jadro nie parsuje TTF/OTF w Ring 0, a ekran
# startuje przed systemem plikow: kontury Noto Sans sa rasteryzowane podczas
# budowania rasteryzatorem BSF (stb_truetype); naglowek jest wersjonowany.
NAPISY_POWITANIA_H := jadro/gui/napisy_powitania_dane.h
NAPISY_POWITANIA_CZCIONKA := czcionki/otf/NotoSans-Regular.otf
$(NAPISY_POWITANIA_H): $(NAPISY_POWITANIA_CZCIONKA) narzedzia/generuj_napisy_powitania.cpp bsf/vendor/stb_truetype.h
	@mkdir -p .wm-test
	g++ -std=c++17 -O2 -I. -Wno-unused-function narzedzia/generuj_napisy_powitania.cpp -o .wm-test/generuj-napisy-powitania
	.wm-test/generuj-napisy-powitania --czcionka $(NAPISY_POWITANIA_CZCIONKA) --wyjscie $@




# Okno informacyjne (okna podrzedne BursztynUI; uruchamiane wylacznie przez BWS 101)
OKNO_INFORMACYJNE_LINKER := programy/okno_informacyjne/okno_informacyjne_linker.ld
okno_informacyjne_tmp.o: programy/okno_informacyjne/okno_informacyjne.cpp bursztyn_ui/okno_podrzedne.h bursztyn_ui/tresc_uklad.h jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

okno_informacyjne_blob.o: okno_informacyjne_tmp.o $(BSF_APP_GUI) $(OKNO_INFORMACYJNE_LINKER)
	$(LD) --gc-sections -T $(call bur_linker,$(OKNO_INFORMACYJNE_LINKER)) $(RING3_LDFLAGS) \
		okno_informacyjne_tmp.o $(BSF_APP_GUI) -o okno_informacyjne.elf
	$(BUR_CONVERT) okno_informacyjne.elf okno_informacyjne.bin
	$(PYTHON) narzedzia/kompresuj_blob.py okno_informacyjne.bin okno_informacyjne.bin.bz
	$(LD) -r -b binary okno_informacyjne.bin.bz -o okno_informacyjne_blob.o

# Snake (BursztynUI)
SNAKE_LINKER := gry/snake/snake_linker.ld
snake_tmp.o: gry/snake/snake.cpp gry/snake/snake_logika.h jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@

snake_blob.o: snake_tmp.o $(BSF_APP_GUI) $(SNAKE_LINKER)
	$(LD) --gc-sections -T $(call bur_linker,$(SNAKE_LINKER)) $(RING3_LDFLAGS) \
		snake_tmp.o $(BSF_APP_GUI) -o snake.elf
	$(BUR_CONVERT) snake.elf snake.bin
	$(PYTHON) narzedzia/kompresuj_blob.py snake.bin snake.bin.bz
	$(LD) -r -b binary snake.bin.bz -o snake_blob.o

# Przegladarka Obrazow (BursztynUI + silnik BMP w silnik_obrazu.h)
PRZEGLADARKA_OBRAZOW_LINKER := programy/przegladarka-obrazow/przegladarka_obrazow_linker.ld
podglad_jpeg_ring3.o: obrazy/podglad_jpeg.cpp obrazy/podglad_jpeg.h obrazy/dekoder_obrazu.h
	$(CXX) $(RING3_FLAGS) -O2 -ffunction-sections -fdata-sections -Wno-unused-function -Wno-unused-parameter -c $< -o $@

przegladarka_obrazow_tmp.o: programy/przegladarka-obrazow/przegladarka-obrazow.cpp programy/przegladarka-obrazow/silnik_obrazu.h programy/przegladarka-obrazow/galeria.h programy/przegladarka-obrazow/powiekszenie.h obrazy/dekoder_obrazu.h obrazy/podglad_jpeg.h jadro/gui/bursztyn_gui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Iprogramy/przegladarka-obrazow -Os -ffunction-sections -fdata-sections -c $< -o $@

przegladarka_obrazow_blob.o: przegladarka_obrazow_tmp.o btp_ring3.o image_ring3.o podglad_jpeg_ring3.o $(BSF_APP_GUI) $(PRZEGLADARKA_OBRAZOW_LINKER)
	$(LD) --gc-sections -T $(call bur_linker,$(PRZEGLADARKA_OBRAZOW_LINKER)) $(RING3_LDFLAGS) \
		przegladarka_obrazow_tmp.o btp_ring3.o image_ring3.o podglad_jpeg_ring3.o $(BSF_APP_GUI) -o przegladarka_obrazow.elf
	$(BUR_CONVERT) przegladarka_obrazow.elf przegladarka_obrazow.bin
	$(PYTHON) narzedzia/kompresuj_blob.py przegladarka_obrazow.bin przegladarka_obrazow.bin.bz
	$(LD) -r -b binary przegladarka_obrazow.bin.bz -o przegladarka_obrazow_blob.o

# Aktualizacje Systemu
aktualizacje_tmp.o: programy/aktualizacje/aktualizacje.cpp jadro/gui/bursztyn_gui.h jadro/bws/bws_ota.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -c $< -o $@

aktualizacje_blob.o: aktualizacje_tmp.o bursztyn_gui.o $(AKTUALIZACJE_LINKER)
	$(LD) -T $(call bur_linker,$(AKTUALIZACJE_LINKER)) $(RING3_LDFLAGS) \
		aktualizacje_tmp.o bursztyn_gui.o -o aktualizacje.elf
	$(BUR_CONVERT) aktualizacje.elf aktualizacje.bin
	$(PYTHON) narzedzia/kompresuj_blob.py aktualizacje.bin aktualizacje.bin.bz
	$(LD) -r -b binary aktualizacje.bin.bz -o aktualizacje_blob.o


# Test dźwięku — diagnostyka HDA
BURSZTYNDIAGTESTDZWIEKU ?= 0
ifeq ($(BURSZTYNDIAGTESTDZWIEKU),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGTESTDZWIEKU=1
RING3_FLAGS += -DBURSZTYNDIAGTESTDZWIEKU=1
endif

test_dzwieku_tmp.o: programy/test_dzwieku/test_dzwieku.cpp jadro/gui/bursztyn_gui.h jadro/bws/bws_hda_diag.h jadro/bws/bws_czcionka.h jadro/gui/test_dzwieku_trace.h \
		sterowniki/dzwiek/hda_diag_format.h jadro/rdzen/wersja_systemu.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

test_dzwieku_blob.o: test_dzwieku_tmp.o $(BSF_APP_GUI) programy/test_dzwieku/test_dzwieku_linker.ld testy/test_dzwieku_size.py
	$(LD) $(BUR_ORPHANS) -T $(call bur_linker,programy/test_dzwieku/test_dzwieku_linker.ld) $(RING3_LDFLAGS) \
		test_dzwieku_tmp.o $(BSF_APP_GUI) -o test_dzwieku.elf
	$(BUR_CONVERT) test_dzwieku.elf test_dzwieku.bin
	$(PYTHON) testy/test_dzwieku_size.py test_dzwieku.bin
	$(PYTHON) narzedzia/kompresuj_blob.py test_dzwieku.bin test_dzwieku.bin.bz
	$(LD) -r -b binary test_dzwieku.bin.bz -o test_dzwieku_blob.o

ustawienia_tmp.o: programy/ustawienia/ustawienia.cpp $(wildcard programy/ustawienia/*.h) $(wildcard bursztyn_ui/*.h) bsf/runtime.h
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

ustawienia_blob.o: ustawienia_tmp.o bursztyn_gui.o $(USTAWIENIA_LINKER)
	$(LD) --gc-sections -T $(call bur_linker,$(USTAWIENIA_LINKER)) $(RING3_LDFLAGS) ustawienia_tmp.o bursztyn_gui.o -o ustawienia.elf
	$(BUR_CONVERT) ustawienia.elf ustawienia.bin
	$(LD) -r -b binary ustawienia.bin -o $@

# REPRO (tylko diagnostyka BWS2/BWS46)
repro_tmp.o: programy/repro/repro.cpp jadro/gui/bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@

repro_blob.o: repro_tmp.o $(BSF_APP_GUI) programy/repro/repro_linker.ld
	$(LD) -T $(call bur_linker,programy/repro/repro_linker.ld) $(RING3_LDFLAGS) \
		repro_tmp.o $(BSF_APP_GUI) -o repro.elf
	$(BUR_CONVERT) repro.elf repro.bin
	$(PYTHON) narzedzia/kompresuj_blob.py repro.bin repro.bin.bz
	$(LD) -r -b binary repro.bin.bz -o repro_blob.o

# ============================================================================
# 16. MENEDZER OKIEN .bur
# ============================================================================

menedzer_okien_tmp.o: programy/menedzer_okien/menedzer_okien.cpp jadro/gui/desktop_entries.h jadro/gui/desktop_layout_settings.h jadro/gui/desktop_layout.h jadro/gui/bursztyn_gui.h jadro/gui/ui_skala.h jadro/gui/pulpit_geometria.h obrazy/png_dekoder.h jadro/gui/plik_popup_ui.h jadro/gui/plik_operacje_ui.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -DBSF_PROFILE=$(BSF_PROFILE) -Os -fstack-usage -c $< -o $@

menedzer_okien_blob.o: menedzer_okien_tmp.o bursztyn_gui.o ikona_png_ring3.o image_png_ring3.o btp_ring3.o $(MENEDZER_OKIEN_LINKER)
	$(LD) -T $(call bur_linker,$(MENEDZER_OKIEN_LINKER)) $(RING3_LDFLAGS) \
		menedzer_okien_tmp.o bursztyn_gui.o ikona_png_ring3.o image_png_ring3.o btp_ring3.o -o menedzer_okien.elf
	$(BUR_CONVERT) menedzer_okien.elf menedzer_okien.bin
	$(PYTHON) narzedzia/kompresuj_blob.py menedzer_okien.bin menedzer_okien.bin.bz
	$(LD) -r -b binary menedzer_okien.bin.bz -o menedzer_okien_blob.o

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

kernel.o aktualizacja.o: jadro/rdzen/wersja_systemu.h

# ============================================================================
# 18. LINKOWANIE JADRA
# ============================================================================

$(KERNEL): $(OBJS) jadro/arch/linker.ld
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
# Zdjecia galerii (dane/zdjecia, nazwy moga zawierac spacje): odcisk listy plikow zamiast wildcard.
GALERIA_STAMP := .galeria.stamp
.PHONY: FORCE
$(GALERIA_STAMP): FORCE narzedzia/package_galeria.py
	@$(PYTHON) narzedzia/package_galeria.py --stamp $@

$(GRUB_CFG): narzedzia/package_release_kernel.py narzedzia/package_release_notices.py $(KERNEL) ustawienia_blob.o $(TAPETY_PAK) .installer-build/BOOTX64.EFI Makefile narzedzia/package_bsf.py narzedzia/package_support.py narzedzia/package_power_icons.py narzedzia/package_desktop_images.py narzedzia/package_galeria.py $(GALERIA_STAMP) $(DESKTOP_IMAGES) $(wildcard ikonki/przycisk*.png) $(wildcard dane/sponsorze/*.txt) czcionki/ttf czcionki/otf $(shell $(PYTHON) -c 'from pathlib import Path; from narzedzia.bsf_release_assets import assets; print(" ".join(str(p) for p in assets(Path("."))))')
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	$(PYTHON) narzedzia/package_release_kernel.py $(KERNEL) $(ISO_DIR)/boot/$(KERNEL) $(OBJCOPY)
	$(PYTHON) narzedzia/package_release_notices.py $(ISO_DIR)
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
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		'' \
		'menuentry "Bursztyn OS - testy automatyczne" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		'menuentry "Bursztyn OS - testy automatyczne (niszczacy test dysku)" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated bursztyn.test.diskdestructive=1 bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		'menuentry "Bursztyn OS Instalator" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    echo "Ladowanie jadra..."' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.install=1 bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    echo "Ladowanie zasobow instalatora..."' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    module2 /boot/installer/BOOTX64.EFI installer.efi' \
		'    module2 /boot/system_operacyjny.bin installer.kernel' \
		'    echo "Uruchamianie instalatora..."' \
		'    boot' \
		'}' \
		> $(GRUB_CFG)
	$(PYTHON) narzedzia/package_bsf.py $(ISO_DIR)
	$(PYTHON) narzedzia/package_support.py $(ISO_DIR)
	$(PYTHON) narzedzia/package_power_icons.py $(ISO_DIR)
	$(PYTHON) narzedzia/package_desktop_images.py $(ISO_DIR)
	$(PYTHON) narzedzia/package_galeria.py $(ISO_DIR)

.installer-build/BOOTX64.EFI: narzedzia/instalator/grub-embedded.cfg Makefile
	@mkdir -p .installer-build
	grub-mkstandalone -O x86_64-efi --locales="" --fonts="" --modules="part_gpt fat loadenv test normal multiboot2 configfile search search_fs_file all_video gfxterm" -o $@ "boot/grub/grub.cfg=$<"

.PHONY: test-network-device test-e1000-qemu test-virtio-pci test-virtio-qemu test-virtio-blk-qemu test-virtio-net-qemu test-virtio-gpu-qemu test-virtio-input-qemu test-normal-boot test-diff-boot installer-unit-test installer-test run-installer test-aktualizacje test-rozruch test-aktualizacje-gui test-bur-loader test-aktualizacja-qemu test-rozruch-qemu test-ota audit-stos-jadra test-stos-jadra test-wspolbieznosc test-guard-page test-16k-bws84 test-motywy test-motywy-unit test-motywy-gui test-motywy-qemu test-notatnik-gui test-kalkulator-gui test-przegladarka-gui test-bursztyn-ui-polecenia test-kontrolki4 test-kontrolki4-qemu

test-network-device:
	@mkdir -p .nettest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_NET_HOST_TEST -fsanitize=address,undefined sterowniki/siec/network_device.cpp testy/test_network_device.cpp -o .nettest/test-network-device
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .nettest/test-network-device

test-e1000-qemu: iso
	$(PYTHON) testy/test_e1000_qemu.py

.PHONY: test-virtio-input-events
test-virtio-input-events:
	@mkdir -p .virtiotest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_PMM_HOST_TEST \
		-fsanitize=address,undefined -pthread \
		sterowniki/wejscie/input_device.cpp testy/test_virtio_input_events.cpp -o .virtiotest/test-virtio-input-events
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/test-virtio-input-events

test-virtio-pci:
	$(PYTHON) testy/test_input_boot_order.py
	@mkdir -p .virtiotest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_PMM_HOST_TEST $(if $(filter 1 yes true,$(BURSZTYNDIAGVIRTIOINPUTINIT)),-DBURSZTYNDIAGVIRTIOINPUTINIT=1) $(if $(filter 1,$(BURSZTYNDIAGVIRTIOINPUTQEMU)),-DBURSZTYNDIAGVIRTIOINPUTQEMU=1) -fsanitize=address,undefined -pthread sterowniki/pci/pci.cpp jadro/mm/pmm.cpp sterowniki/virtio/virtio_pci.cpp sterowniki/virtio/virtqueue.cpp sterowniki/virtio/virtio_rng.cpp sterowniki/virtio/virtio_blk.cpp sterowniki/siec/network_device.cpp sterowniki/virtio/virtio_net.cpp sterowniki/virtio/virtio_gpu.cpp sterowniki/wejscie/input_device.cpp sterowniki/virtio/virtio_input.cpp sterowniki/usb/usb_hid.cpp sterowniki/wejscie/klawiatura.cpp testy/test_virtio_pci.cpp -o .virtiotest/test-virtio-pci
	$(PYTHON) narzedzia/skan_instrukcji_uprzywilejowanych.py .virtiotest/test-virtio-pci
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/test-virtio-pci

.PHONY: test-input-init-locks
test-input-init-locks:
	@mkdir -p .virtiotest
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_LOCK_HOST_TEST -DBURSZTYN_PMM_HOST_TEST -DBURSZTYNDIAGVIRTIOINPUTINIT=1 -ffunction-sections -fdata-sections -fsanitize=address,undefined -pthread jadro/mm/heap.cpp jadro/mm/pmm.cpp sterowniki/pci/pci.cpp testy/test_input_init_locks.cpp -Wl,--gc-sections -o .virtiotest/test-input-init-locks
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/test-input-init-locks

test-virtio-qemu: $(CI_TEST_ISO)
	$(PYTHON) testy/test_virtio_qemu.py

test-virtio-blk-qemu: $(CI_TEST_ISO)
	$(PYTHON) testy/test_virtio_blk_qemu.py

test-virtio-net-qemu: $(CI_TEST_ISO)
	$(PYTHON) testy/test_virtio_net_qemu.py

test-virtio-gpu-qemu: $(CI_TEST_ISO)
	$(PYTHON) testy/test_virtio_gpu_qemu.py

test-virtio-input-qemu: $(CI_TEST_ISO)
	$(PYTHON) testy/test_virtio_input_qemu.py

test-normal-boot: iso
	$(PYTHON) testy/test_normal_boot_qemu.py

test-diff-boot: iso
	$(PYTHON) testy/test_diff_boot.py

test-kontrolki4:
	@mkdir -p .kontrolki4-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined testy/test_kontrolki4.cpp -o .kontrolki4-test/test-k4
	ASAN_OPTIONS=detect_leaks=0 .kontrolki4-test/test-k4

test-kontrolki4-qemu: iso
	$(PYTHON) testy/test_kontrolki4_qemu.py

test-bursztyn-ui-polecenia:
	@mkdir -p .bursztyn-ui-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined testy/test_bursztyn_ui_polecenia.cpp -o .bursztyn-ui-test/test-polecenia
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-polecenia

test-motywy-unit:
	@mkdir -p .motywytest
	g++ -std=c++17 -O2 -Wall -Wextra $(HOST_INCLUDES) testy/test_motyw_host.cpp -o .motywytest/test_motyw_host
	.motywytest/test_motyw_host

test-motywy-gui:
	$(PYTHON) testy/test_motywy_gui.py

test-notatnik-gui: notatnik_blob.o
	$(PYTHON) testy/test_notatnik_gui.py

test-kalkulator-gui: kalkulator_blob.o
	$(PYTHON) testy/test_kalkulator_gui.py

test-przegladarka-gui: przegladarka_blob.o husarz_worker_blob.o
	$(PYTHON) testy/test_przegladarka_gui.py

test-motywy: test-motywy-unit test-motywy-gui test-notatnik-gui test-kalkulator-gui

test-motywy-qemu: $(CI_TEST_ISO) prepare-disk
	$(PYTHON) testy/test_motywy_qemu.py

test-16k-bws84:
	$(PYTHON) narzedzia/aktualizacje/test_16k_bws84_irq.py
test-bur-loader:
	$(PYTHON) testy/test_bur_loader.py
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
	$(PYTHON) testy/test_guard_page.py
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
	$(PYTHON) testy/test_wspolbieznosc.py
installer-unit-test:
	@mkdir -p .installertest
	g++ -std=c++17 -O1 -g -Wall -Wextra $(HOST_INCLUDES) -DBURSZTYN_PSF_HOST_TEST -fsanitize=address,undefined narzedzia/instalator/test_storage.cpp jadro/fs/storage_format.cpp jadro/fs/psf.cpp -o .installertest/test-storage
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-storage
installer-test: iso installer-unit-test
	$(PYTHON) narzedzia/instalator/qemu_install.py test
run-installer: iso
	$(PYTHON) narzedzia/instalator/qemu_install.py run

instalator_tmp.o: programy/instalator/instalator.cpp jadro/gui/instalator_format.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -c $< -o $@
instalator_blob.o: instalator_tmp.o bursztyn_gui.o programy/instalator/instalator_linker.ld
	$(LD) --gc-sections -T $(call bur_linker,programy/instalator/instalator_linker.ld) $(RING3_LDFLAGS) instalator_tmp.o bursztyn_gui.o -o instalator.elf
	$(BUR_CONVERT) instalator.elf instalator.bin
	$(PYTHON) narzedzia/kompresuj_blob.py instalator.bin instalator.bin.bz
	$(LD) -r -b binary instalator.bin.bz -o $@

$(ISO): check-kernel $(KERNEL) $(TAPETY_PAK) $(GRUB_CFG) narzedzia/verifyreleasemetadata.py
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR) --xorriso=$(XORRISO)
	$(PYTHON) narzedzia/verify_bsf_iso.py $@
	$(PYTHON) narzedzia/verifyreleasemetadata.py $@ --output .release-private/release-inventory.json
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
$(CI_TEST_ISO): $(KERNEL) check-kernel $(GRUB_CFG) $(TAPETY_PAK) $(DESKTOP_IMAGES)
	@mkdir -p $(CI_TEST_DIR)/boot/grub
	@cp $(KERNEL) $(CI_TEST_DIR)/boot/
	@cp $(TAPETY_PAK) $(CI_TEST_DIR)/boot/tapety.pak
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS - testy automatyczne" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		> $(CI_TEST_DIR)/boot/grub/grub.cfg
	@$(PYTHON) narzedzia/package_desktop_images.py $(CI_TEST_DIR)
	@$(GRUB_MKRESCUE) -o $@ $(CI_TEST_DIR) --xorriso=$(XORRISO) >/dev/null 2>&1
	@rm -rf $(CI_TEST_DIR)

$(CI_BLK_DESTRUCTIVE_ISO): $(KERNEL) check-kernel $(GRUB_CFG) $(TAPETY_PAK) $(DESKTOP_IMAGES)
	@mkdir -p $(CI_BLK_DIR)/boot/grub
	@cp $(KERNEL) $(CI_BLK_DIR)/boot/
	@cp $(TAPETY_PAK) $(CI_BLK_DIR)/boot/tapety.pak
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS - testy automatyczne (niszczacy test dysku)" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test=automated bursztyn.test.diskdestructive=1 bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		> $(CI_BLK_DIR)/boot/grub/grub.cfg
	@$(PYTHON) narzedzia/package_desktop_images.py $(CI_BLK_DIR)
	@$(GRUB_MKRESCUE) -o $@ $(CI_BLK_DIR) --xorriso=$(XORRISO) >/dev/null 2>&1
	@rm -rf $(CI_BLK_DIR)

$(CI_DESTRUCTIVE_ONLY_ISO): $(KERNEL) check-kernel $(GRUB_CFG) $(TAPETY_PAK) $(DESKTOP_IMAGES)
	@mkdir -p $(CI_DEST_ONLY_DIR)/boot/grub
	@cp $(KERNEL) $(CI_DEST_ONLY_DIR)/boot/
	@cp $(TAPETY_PAK) $(CI_DEST_ONLY_DIR)/boot/tapety.pak
	@printf '%s\n' \
		'set timeout=0' \
		'set default=0' \
		'' \
		'menuentry "Bursztyn OS - flaga niszczaca bez trybu automatycznego" {' \
		'    insmod all_video' \
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.test.diskdestructive=1 bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    boot' \
		'}' \
		> $(CI_DEST_ONLY_DIR)/boot/grub/grub.cfg
	@$(PYTHON) narzedzia/package_desktop_images.py $(CI_DEST_ONLY_DIR)
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

$(GRUB8K_CFG): $(KERNEL) check-test8k $(TAPETY_PAK) $(DESKTOP_IMAGES)
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
		'    set gfxmode=1280x720x32' \
		'    set gfxpayload=keep' \
		'    multiboot2 /boot/system_operacyjny.bin bursztyn.refresh=$(BURSZTYN_REFRESH_HZ) bursztyn.video=$(BURSZTYN_VIDEO_MODE)' \
		'    module2 /boot/tapety.pak tapety.pak' \
		'    module2 /boot/test8k.bmp test8k.bmp' \
		'    boot' \
		'}' \
		> $(GRUB8K_CFG)
	@$(PYTHON) narzedzia/package_desktop_images.py $(ISO8K_DIR)

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

BURSZTYN_VIDEO_MODE ?= 1280x720
ifneq ($(filter 800x600 1024x768 1280x720 1280x800 1366x768 1600x900 1920x1080 2560x1440 3440x1440 3840x2160,$(BURSZTYN_VIDEO_MODE)),$(BURSZTYN_VIDEO_MODE))
$(error Unsupported BURSZTYN_VIDEO_MODE)
endif
BURSZTYN_REFRESH_HZ ?= 60
ifneq ($(filter 60 120 144 165 240,$(BURSZTYN_REFRESH_HZ)),$(BURSZTYN_REFRESH_HZ))
$(error BURSZTYN_REFRESH_HZ musi byc jedna z wartosci: 60, 120, 144, 165, 240)
endif

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
	-device virtio-mouse-pci \
	-device virtio-tablet-pci

# Wskaznik (docs/virtio-gpu-mouse-reliability-2026-11-11.md): podstawowym
# urzadzeniem jest bezwzgledny virtio-tablet-pci (dodany NA KONCU, BDF
# pozostalych 7 urzadzen bez zmian). Linuksowy frontend SDL 2.30 w trybie
# wzglednym przelicza ruch z bezwzglednego wskaznika hosta (VirtualBox/VMware,
# tablety, ekrany dotykowe) jako prev-cur w jednostkach urzadzenia, co daje
# odwrocony i przeskalowany ruch kursora. Z tabletem QEMU nie wchodzi w tryb
# wzgledny. Fallback: virtio-mouse-pci (wzgledna), potem PS/2 z i8042.

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
	rm -rf $(ISO_DIR) $(ISO8K_DIR) $(CI_TEST_DIR) $(CI_BLK_DIR) $(CI_DEST_ONLY_DIR) .build-config isodir_test

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
	@echo ""
	@echo "Lokalna weryfikacja (bez GitHuba; szczegoly: docs/12_lokalna_weryfikacja.md):"
	@echo "  make test / test-smoke - szybki gate przed commitem"
	@echo "  make test-security     - SSP, fuzz smoke, QEMU abuse, guard-page"
	@echo "  make test-security STACK_AUDIT=1 - j.w. + audit-stos-jadra"
	@echo "  make test-regression   - VirtIO/OTA/instalator/Husarz/HDA/input"
	@echo "  make test-nightly      - security + regression + fuzz-extended"
	@echo "  make test-local-suite MODE=smoke|security|regression|nightly"
	@echo "                         - orkiestrator JSON (.local-ci/)"

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
jadro/gui/tapety_format.h: jadro/gui/tapety_nazwy.h
tapety.o: jadro/gui/tapety.cpp jadro/gui/tapety.h jadro/gui/tapety_format.h jadro/gui/tapety_nazwy.h jadro/mm/pamiec.h jadro/gui/bufory_grafiki.h obrazy/skalowanie.h narzedzia/tapety-btp/tapetybtp.h
kernel.o bws_nowoczesne.o: jadro/gui/tapety.h jadro/gui/tapety_format.h jadro/gui/tapety_nazwy.h obrazy/skalowanie.h
syscalls.o: jadro/gui/tapety_format.h jadro/gui/tapety_nazwy.h
instalator.o ekran.o: jadro/gui/tapety.h jadro/gui/tapety_format.h jadro/gui/tapety_nazwy.h

kernel.o: sterowniki/virtio/virtio_pci.h sterowniki/virtio/virtio_blk.h sterowniki/virtio/virtio_net.h sterowniki/virtio/virtio_gpu.h sterowniki/virtio/virtio_rng.h sterowniki/virtio/virtio_input.h

btp_ring3.o: narzedzia/tapety-btp/tapetybtp.cpp narzedzia/tapety-btp/tapetybtp.h
	$(CXX) $(RING3_FLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@
image_ring3.o: obrazy/dekoder_obrazu.cpp obrazy/dekoder_obrazu.h obrazy/vendor/stb_image.h narzedzia/tapety-btp/tapetybtp.h
	$(CXX) $(RING3_FLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Os -ffunction-sections -fdata-sections -Wno-unused-function -Wno-unused-parameter -c $< -o $@

.PHONY: test-image
test-image:
	$(PYTHON) obrazy/testy/test_jpeg.py
	$(PYTHON) obrazy/testy/test_png.py
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -I. -Ijadro/gui obrazy/test_tapety_jpeg.cpp -o /tmp/bursztyn-test-tapety-jpeg
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/bursztyn-test-tapety-jpeg
	$(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -I. -Ijadro/gui obrazy/test_tapety_manifest.cpp -o /tmp/bursztyn-test-tapety-manifest
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/bursztyn-test-tapety-manifest
tapety_tmp.o: programy/tapety/tapety.cpp programy/tapety/klient.h programy/tapety/miniatury.h jadro/gui/tapety_format.h obrazy/skalowanie.h obrazy/dekoder_obrazu.h narzedzia/tapety-btp/tapetybtp.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c $< -o $@
	@test -s $@ || { rm -f $@; echo "[BLAD] tapety_tmp.o jest pusty"; exit 1; }
tapety_blob.o: tapety_tmp.o btp_ring3.o image_ring3.o $(BSF_APP_GUI) programy/tapety/tapety_linker.ld testy/test_tapety_size.py
	@if [ ! -s tapety_tmp.o ]; then \
		echo "[TAPETY BUILD] tapety_tmp.o jest pusty lub uszkodzony - ponowna kompilacja..."; \
		$(CXX) $(RING3_FLAGS) -I. -Os -ffunction-sections -fdata-sections -c programy/tapety/tapety.cpp -o tapety_tmp.o; \
	fi
	$(LD) --gc-sections -T $(call bur_linker,programy/tapety/tapety_linker.ld) $(RING3_LDFLAGS) tapety_tmp.o btp_ring3.o image_ring3.o $(BSF_APP_GUI) -o tapety.elf
	$(BUR_CONVERT) tapety.elf tapety.bin
	$(PYTHON) testy/test_tapety_size.py tapety.bin
	$(PYTHON) narzedzia/kompresuj_blob.py tapety.bin tapety.bin.bz
	$(LD) -r -b binary tapety.bin.bz -o $@

$(filter %tmp.o %_tmp.o,$(wildcard *.o)): obrazy/skalowanie.h

test_tmp.o: programy/test/tapety_testy.h programy/tapety/klient.h programy/tapety/miniatury.h jadro/gui/tapety_format.h narzedzia/tapety-btp/tapetybtp.h

test_tmp.o: programy/test/kontrolki3_stabilizacja.h

bochs_vbe.o: sterowniki/grafika/bochs_vbe.cpp sterowniki/grafika/bochs_vbe.h jadro/gui/ekran_format.h
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

ekran_tmp.o: programy/ekran/ekran.cpp programy/ekran/klient.h programy/ekran/ekran_okno.h jadro/gui/ekran_format.h jadro/gui/ui_skala.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -c $< -o $@
ekran_blob.o: ekran_tmp.o $(BSF_APP_GUI) programy/ekran/ekran_linker.ld
	$(LD) --gc-sections -T $(call bur_linker,programy/ekran/ekran_linker.ld) $(RING3_LDFLAGS) ekran_tmp.o $(BSF_APP_GUI) -o ekran.elf
	$(BUR_CONVERT) ekran.elf ekran.bin
	$(PYTHON) narzedzia/kompresuj_blob.py ekran.bin ekran.bin.bz
	$(LD) -r -b binary ekran.bin.bz -o $@
$(filter %_tmp.o,$(OBJS)) bursztyn_gui.o: jadro/gui/ui_skala.h

ekran_format_ring3.o: jadro/gui/ekran_format.cpp jadro/gui/ekran_format.h
	$(CXX) $(RING3_FLAGS) -c $< -o $@
bws_nowoczesne.o ekran.o grafika.o kernel.o: jadro/gui/ekran.h jadro/gui/ekran_format.h jadro/gui/bufory_grafiki.h
test_tmp.o: programy/test/ekran_testy.h programy/ekran/klient.h programy/ekran/ekran_okno.h jadro/gui/ekran_format.h jadro/gui/pulpit_geometria.h

# Central WM chrome and shared popup layout dependencies.
skladacz_obrazu.o: jadro/gui/wm_dekoracje.h jadro/gui/motyw.h bursztyn_ui/czcionka.h jadro/gui/ui_skala.h
motyw.o: jadro/gui/skladacz_obrazu.h
shell_tmp.o: bursztyn_ui/widok_menu_popup.h
grafika.o: jadro/gui/wm_input.h
bursztyn_gui.o: jadro/gui/wm_belka_hit.h

.PHONY: test-wm-click-routing test-wm-dekoracje test-shell-host
test-wm-click-routing:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_wm_click_routing.cpp bursztyn_ui/ikona_png.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp -Wl,--gc-sections -o .wm-test/click-routing
	ASAN_OPTIONS=detect_leaks=0 .wm-test/click-routing

test-wm-dekoracje:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -fsanitize=address,undefined testy/test_wm_dekoracje.cpp -o .wm-test/dekoracje
	ASAN_OPTIONS=detect_leaks=0 .wm-test/dekoracje

test-shell-host:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_shell_terminal.cpp bsf/bsf.cpp bsf/terminal.cpp -Wl,--gc-sections -o .wm-test/shell
	ASAN_OPTIONS=detect_leaks=0 .wm-test/shell

BURSZTYNDIAGCURSORPERF ?= 0
ifeq ($(BURSZTYNDIAGCURSORPERF),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGCURSORPERF=1
endif

.PHONY: test-cursor-host
test-cursor-host:
	mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -Wall -Wextra -fsanitize=address,undefined testy/test_cursor_host.cpp -o .wm-test/cursor
	ASAN_OPTIONS=detect_leaks=0 .wm-test/cursor

grafika.o skladacz_obrazu.o: jadro/gui/kursor_prezentacja.h jadro/gui/kursor_diag.h
virtio_input.o: jadro/gui/kursor_diag.h

.PHONY: test-cursor-hover
test-cursor-hover:
	mkdir -p .wm-test
	g++ $(HOST_INCLUDES) -std=c++17 -O0 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_cursor_hover.cpp -Wl,--gc-sections -o .wm-test/cursor-hover
	ASAN_OPTIONS=detect_leaks=0 .wm-test/cursor-hover

BURSZTYNDIAGGUIRUNTIME ?= 0
ifeq ($(BURSZTYNDIAGGUIRUNTIME),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGGUIRUNTIME=1
RING3_FLAGS += -DBURSZTYNDIAGGUIRUNTIME=1
endif

GUI_PERF_BUILD_CONFIG := .build-config/gui-perf
.PHONY: gui-perf-build-config-force
gui-perf-build-config-force:
$(GUI_PERF_BUILD_CONFIG): gui-perf-build-config-force
	@mkdir -p .build-config
	@$(PYTHON) -c "from pathlib import Path; p=Path('$@'); v='runtime=$(BURSZTYNDIAGGUIRUNTIME) perf=$(BURSZTYN_DEBUG_PERF) gui_perf=$(BURSZTYN_DEBUG_GUI_PERF) loader=$(BURSZTYN_DEBUG_LOADER_PERF) tlscost=$(BURSZTYN_DIAG_TLS_COST) noyield=$(BURSZTYN_DIAG_NO_YIELD) noshadow=$(BURSZTYN_DIAG_NO_SHADOW) testca=$(BURSZTYN_DIAG_TEST_CA) gpupoll=$(BURSZTYN_GPU_POLL_WAIT) fixture=$(BURSZTYN_DIAG_FIXTURE_FILES)'; p.write_text(v) if not p.exists() or p.read_text()!=v else None"

scheduler.o syscalls.o grafika.o skladacz_obrazu.o loader.o kernel.o virtio_input.o idt.o tls.o siec.o: jadro/gui/gui_runtime_diag.h $(GUI_PERF_BUILD_CONFIG)
shell_tmp.o: programy/shell/shell_runtime_diag.h

virtio_input.o: jadro/gui/gui_runtime_diag.h

.PHONY: test-gui-runtime-diag
test-gui-runtime-diag:
	mkdir -p .wm-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -I. -Ijadro/gui -Ijadro/bws -Isterowniki/czas -fsanitize=address,undefined testy/test_gui_runtime_diag.cpp -o .wm-test/gui-runtime-diag
	ASAN_OPTIONS=detect_leaks=0 .wm-test/gui-runtime-diag

.PHONY: test-compositor-cursor
test-compositor-cursor:
	mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_compositor_cursor.cpp -Wl,--gc-sections -o .wm-test/compositor-cursor
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/compositor-cursor

BURSZTYNDIAGABSMAP ?= 0
ifeq ($(BURSZTYNDIAGABSMAP),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGABSMAP=1
RING3_FLAGS += -DBURSZTYNDIAGABSMAP=1
endif
input_device.o: jadro/gui/abs_map_diag.h

.PHONY: test-abs-map-diag
test-abs-map-diag:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -Wall -Wextra -ffunction-sections -fdata-sections -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYNDIAGABSMAP=1 -fsanitize=address,undefined sterowniki/wejscie/input_device.cpp testy/test_abs_map_diag.cpp -Wl,--gc-sections -o .wm-test/abs-map-diag
	ASAN_OPTIONS=detect_leaks=1 .wm-test/abs-map-diag

.PHONY: test-explorer-host
test-explorer-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined testy/test_explorer_host.cpp -o .bursztyn-ui-test/test-explorer
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-explorer

.PHONY: test-tapety-bur
test-tapety-bur: tapety_blob.o
	$(PYTHON) testy/test_tapety_regression.py

.PHONY: test-hda-physical test-hda-tone test-hda-regression-qemu test-hda-diag test-dzwieku-packaging test-dzwieku-repair
test-hda-physical:
	$(PYTHON) testy/test_hda_physical.py
test-hda-tone:
	$(PYTHON) testy/test_hda_tone.py
test-hda-regression-qemu: all
	$(PYTHON) testy/test_hda_regression_qemu.py
test-dzwieku-repair: test_dzwieku_blob.o
	$(PYTHON) testy/test_dzwieku_repair.py

test-dzwieku-packaging: test_dzwieku_blob.o
	$(PYTHON) testy/test_dzwieku_packaging.py

test-hda-diag:
	@mkdir -p .hda-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined testy/test_hda_diag.cpp -o .hda-test/test-hda-diag
	ASAN_OPTIONS=detect_leaks=0 .hda-test/test-hda-diag
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined testy/test_hda_bws_isolation.cpp -o .hda-test/test-hda-bws
	ASAN_OPTIONS=detect_leaks=0 .hda-test/test-hda-bws

.PHONY: test-desktop-files-host
test-desktop-files-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined testy/test_desktop_files_host.cpp bursztyn_ui/ikona_png.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp -o .bursztyn-ui-test/test-desktop-files
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-desktop-files

.PHONY: test-usluga-ui-protokol-host
test-usluga-ui-protokol-host:
	@mkdir -p .bursztyn-ui-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -I. -fsanitize=address,undefined testy/test_usluga_ui_protokol_host.cpp -o .bursztyn-ui-test/test-usluga-ui-protokol
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-usluga-ui-protokol

.PHONY: test-desktop-capacity-host
test-desktop-capacity-host:
	@mkdir -p .bursztyn-ui-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined testy/test_desktop_capacity_host.cpp -o .bursztyn-ui-test/test-desktop-capacity
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-desktop-capacity

# Opt-in aggregate move diagnostics; no normal-build logging or GPU behavior changes.
BURSZTYNDIAGMOVE ?= 0
ifeq ($(BURSZTYNDIAGMOVE),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGMOVE=1
endif
syscalls.o grafika.o skladacz_obrazu.o: jadro/gui/move_trace.h

.PHONY: test-filesystem-move-host
test-filesystem-move-host: installer-unit-test
	@mkdir -p .installertest
	g++ $(HOST_INCLUDES) -std=c++17 -O0 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -DBURSZTYN_PSF_HOST_TEST -fsanitize=address,undefined testy/test_move_authorization.cpp jadro/fs/storage_format.cpp jadro/fs/psf.cpp -o .installertest/test-move-authorization
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-move-authorization

bursztyn_gui.o syscalls.o: jadro/bws/bws_pliki.h jadro/gui/motyw.h

.PHONY: test-desktop-selection-theme
test-desktop-selection-theme:
	@mkdir -p .motywytest
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined testy/test_desktop_selection_theme.cpp -o .motywytest/test-desktop-selection
	ASAN_OPTIONS=detect_leaks=0 .motywytest/test-desktop-selection

.PHONY: test-file-popup-host
test-file-popup-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined testy/test_file_popup_host.cpp -o .bursztyn-ui-test/test-file-popup
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-file-popup

.PHONY: test-desktop-settings-host
test-desktop-settings-host:
	@mkdir -p .installertest
	g++ $(HOST_INCLUDES) -std=c++17 -O0 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -DBURSZTYN_PSF_HOST_TEST -fsanitize=address,undefined testy/test_desktop_settings_authorization.cpp jadro/fs/storage_format.cpp jadro/fs/psf.cpp -o .installertest/test-desktop-settings
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-desktop-settings

.PHONY: test-startup-visibility
grafika.o skladacz_obrazu.o kernel.o: jadro/gui/skladacz_obrazu.h
# Host-only: production compositor lifecycle + kernel graphics + boot policy.
test-startup-visibility:
	@mkdir -p .wm-test
	g++ -std=c++17 -O0 -g0 $(HOST_INCLUDES) -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_startup_visibility.cpp -Wl,--gc-sections -o .wm-test/startup-visibility
	ASAN_OPTIONS=detect_leaks=0 .wm-test/startup-visibility
	g++ -std=c++17 -O0 -g0 $(HOST_INCLUDES) -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_boot_graphics.cpp -Wl,--gc-sections -o .wm-test/boot-graphics
	ASAN_OPTIONS=detect_leaks=0 .wm-test/boot-graphics
	$(PYTHON) testy/test_startup_policy.py

# Welcome layer dependencies and deterministic host regression suite.
ekran_powitalny.o: jadro/gui/ekran_powitalny.h jadro/gui/ekran_powitalny_api.h jadro/gui/ui_skala.h $(LOGO_MASKA_H) $(NAPISY_POWITANIA_H)
grafika.o skladacz_obrazu.o kernel.o: jadro/gui/ekran_powitalny_api.h
kernel.o: jadro/rdzen/opcje_startowe.h

.PHONY: test-logo-maska
test-logo-maska:
	$(PYTHON) testy/test_logo_maska.py

.PHONY: test-powitanie
test-powitanie:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -fsanitize=address,undefined testy/test_powitanie.cpp -o .wm-test/powitanie
	ASAN_OPTIONS=detect_leaks=0 .wm-test/powitanie
	$(PYTHON) testy/test_logo_maska.py
	$(PYTHON) testy/test_napisy_powitania.py
	$(PYTHON) testy/test_powitanie_policy.py
	$(MAKE) --no-print-directory test-splash-sekwencja

# Sekwencjonowanie etapow zalezne od prezentacji (przestoje, odrzucone klatki,
# zawiniecie zegara). SPLASH_KLATKI=katalog zapisuje klatki PPM scenariusza z przestojem.
.PHONY: test-splash-sekwencja
test-splash-sekwencja:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -fsanitize=address,undefined testy/test_splash_sekwencja.cpp -o .wm-test/splash-sekwencja
	ASAN_OPTIONS=detect_leaks=0 .wm-test/splash-sekwencja $(SPLASH_KLATKI)

BURSZTYNDIAGSTARTUPREBOOT ?= 0
ifeq ($(BURSZTYNDIAGSTARTUPREBOOT),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSTARTUPREBOOT=1
endif
grafika.o skladacz_obrazu.o syscalls.o ekran_powitalny.o: jadro/diag/diagnostyka_startu.h
syscalls.o: jadro/gui/ekran_powitalny_api.h jadro/gui/skladacz_obrazu.h

syscalls.o: jadro/gui/cykl_wylaczania_grafiki.h

.PHONY: test-powitanie-reboot
test-powitanie-reboot:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -pthread -fsanitize=address,undefined testy/test_powitanie_reboot.cpp -o .wm-test/powitanie-reboot
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/powitanie-reboot

.PHONY: test-gpu-quiesce-host
test-gpu-quiesce-host:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_gpu_quiesce_host.cpp -Wl,--gc-sections -o .wm-test/gpu-quiesce
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/gpu-quiesce

# Opt-in sparse splash timing diagnostics; production default has no frame logs.
BURSZTYNDIAGSPLASHTIMING ?= 0
ifeq ($(BURSZTYNDIAGSPLASHTIMING),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSPLASHTIMING=1
endif

.PHONY: test-splash-timing test-windows-launcher
test-splash-timing:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -DBURSZTYNDIAGSPLASHTIMING=1 -fsanitize=address,undefined testy/test_splash_timing.cpp -o .wm-test/splash-timing
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-timing

test-windows-launcher:
	$(PYTHON) testy/test_windows_launcher_config.py

BURSZTYNDIAGSPLASHFIRSTFRAME ?= 0
ifeq ($(BURSZTYNDIAGSPLASHFIRSTFRAME),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSPLASHFIRSTFRAME=1
endif
virtio_gpu.o: jadro/diag/diagnostyka_startu.h
grafika.o: sterowniki/virtio/virtio_gpu.h
.PHONY: test-splash-first-frame
test-splash-first-frame:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -DBURSZTYNDIAGSPLASHFIRSTFRAME=1 -fsanitize=address,undefined testy/test_splash_first_frame.cpp -o .wm-test/splash-first-frame
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-first-frame

BURSZTYNDIAGSPLASHRUNTIMEFRAMES ?= 0
ifeq ($(BURSZTYNDIAGSPLASHRUNTIMEFRAMES),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGSPLASHRUNTIMEFRAMES=1
endif
grafika.o ekran_powitalny.o skladacz_obrazu.o virtio_gpu.o virtio_pci.o uefi_gop.o kernel.o: jadro/gui/prezentacja_startu.h jadro/diag/diagnostyka_klatek_startu.h

.PHONY: test-splash-snapshots
test-splash-snapshots:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -fsanitize=address,undefined testy/test_splash_snapshots.cpp -o .wm-test/splash-snapshots
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-snapshots

# Restricted ptrace environments may override only leak checking; ASan/UBSan
# remain compiled into all of these startup verification binaries.
STARTUP_ASAN_OPTIONS ?= detect_leaks=1

.PHONY: test-splash-production-path
STARTUP_HOST_FLAGS := -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_GRAPHICS_HOST_TEST -DBURSZTYN_COMPOSITOR_HOST_TEST -DBURSZTYNDIAGSPLASHRUNTIMEFRAMES=1 -fsanitize=address,undefined -pthread
STARTUP_HOST_SOURCES := testy/test_splash_production_path.cpp sterowniki/grafika/grafika.cpp jadro/gui/ekran_powitalny.cpp jadro/gui/skladacz_obrazu.cpp sterowniki/grafika/uefi_gop.cpp sterowniki/pci/pci.cpp sterowniki/virtio/virtio_pci.cpp sterowniki/virtio/virtqueue.cpp sterowniki/virtio/virtio_rng.cpp sterowniki/virtio/virtio_blk.cpp sterowniki/siec/network_device.cpp sterowniki/virtio/virtio_net.cpp sterowniki/virtio/virtio_gpu.cpp sterowniki/wejscie/input_device.cpp sterowniki/virtio/virtio_input.cpp sterowniki/usb/usb_hid.cpp sterowniki/wejscie/klawiatura.cpp
.wm-test/runtime-host/testy/test_splash_production_path.o .wm-test/runtime-host/pmm.o: STARTUP_HOST_FLAGS += -DBURSZTYN_PMM_HOST_TEST
STARTUP_HOST_OBJECTS := $(addprefix .wm-test/runtime-host/,$(STARTUP_HOST_SOURCES:.cpp=.o))
.wm-test/runtime-host/%.o: %.cpp
	@mkdir -p $(@D)
	g++ $(HOST_INCLUDES) $(STARTUP_HOST_FLAGS) -MMD -MP -c $< -o $@
-include $(STARTUP_HOST_OBJECTS:.o=.d)
test-splash-production-path: $(STARTUP_HOST_OBJECTS) .wm-test/runtime-host/pmm.o
	@mkdir -p .wm-test
	g++ $(HOST_INCLUDES) $(STARTUP_HOST_FLAGS) -Wl,--gc-sections $(STARTUP_HOST_OBJECTS) .wm-test/runtime-host/pmm.o -o .wm-test/splash-production-path
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/splash-production-path

# Bootstrap status is shared with kernel early startup; explicit first-build
# edges complement generated dependencies when upgrading an existing worktree.
psf.o kernel.o: jadro/fs/psf.h jadro/fs/psf_bootstrap_status.h
psf.o: jadro/fs/psf_usuwanie.inc jadro/fs/plik_status.h
bursztyn_gui.o syscalls.o: jadro/fs/plik_status.h
kernel.o: jadro/fs/psf_bootstrap.h
-include $(wildcard *.d jadro/*/*.d biblioteki/mbedtls/library/*.d)

PSF_BOOTSTRAP_HOST_FLAGS := -std=c++17 -O1 -g -Wall -Wextra $(HOST_INCLUDES) -Ibiblioteki/mbedtls/include -DBURSZTYN_PSF_HOST_TEST -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined
.PHONY: test-gui-text-input-host test-recursive-delete-host test-file-watch-host
test-file-watch-host:
	@mkdir -p .installertest
	g++ $(HOST_INCLUDES) $(PSF_BOOTSTRAP_HOST_FLAGS) testy/test_file_watch_host.cpp -o .installertest/test-file-watch
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-file-watch
test-gui-text-input-host:
	@mkdir -p .bursztyn-ui-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined testy/test_gui_text_input.cpp sterowniki/wejscie/klawiatura.cpp -o .bursztyn-ui-test/test-gui-text-input
	ASAN_OPTIONS=detect_leaks=0 .bursztyn-ui-test/test-gui-text-input
test-recursive-delete-host:
	@mkdir -p .installertest
	g++ $(HOST_INCLUDES) $(PSF_BOOTSTRAP_HOST_FLAGS) testy/test_recursive_delete_host.cpp jadro/fs/storage_format.cpp -o .installertest/test-recursive-delete
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-recursive-delete
.PHONY: test-psf-bootstrap-host
test-psf-bootstrap-host:
	@mkdir -p .installertest
	g++ $(HOST_INCLUDES) $(PSF_BOOTSTRAP_HOST_FLAGS) -DBURSZTYNDIAGPSFBOOTSTRAP=0 testy/test_psf_bootstrap_host.cpp jadro/aktualizacja/aktualizacja.cpp jadro/fs/storage_format.cpp -o .installertest/test-psf-bootstrap-normal
	ASAN_OPTIONS=detect_leaks=0 .installertest/test-psf-bootstrap-normal
	g++ $(HOST_INCLUDES) $(PSF_BOOTSTRAP_HOST_FLAGS) -DBURSZTYNDIAGPSFBOOTSTRAP=1 testy/test_psf_bootstrap_host.cpp jadro/aktualizacja/aktualizacja.cpp jadro/fs/storage_format.cpp -o .installertest/test-psf-bootstrap
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

POINTING_HOST_FLAGS := $(if $(filter 1,$(BURSZTYNDIAGXHCIPOINTING)),-DBURSZTYNDIAGXHCIPOINTING=1) $(if $(filter 1,$(BURSZTYNDIAGPS2)),-DBURSZTYNDIAGPS2=1) $(if $(filter 1,$(BURSZTYNDIAGUSBHID)),-DBURSZTYNDIAGUSBHID=1) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_VIRTIO_HOST_TEST -fsanitize=address,undefined -fno-omit-frame-pointer
.PHONY: test-pointing-host
test-pointing-host:
	@mkdir -p .pointingtest
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) testy/test_pointing.cpp sterowniki/usb/hid_pointer.cpp sterowniki/wejscie/input_device.cpp -o .pointingtest/pointing
	.pointingtest/pointing
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -pthread -DBURSZTYN_PS2_HOST_TEST -DREAL_KEYBOARD=1 testy/test_ps2_controller.cpp sterowniki/wejscie/input_device.cpp sterowniki/wejscie/klawiatura.cpp sterowniki/usb/usb_hid.cpp -o .pointingtest/ps2-controller
	.pointingtest/ps2-controller
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) testy/test_ohci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/wejscie/input_device.cpp -o .pointingtest/ohci
	.pointingtest/ohci
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -ffunction-sections -fdata-sections -DBURSZTYN_IDT_HOST_TEST testy/test_xhci_pointer.cpp jadro/arch/idt.cpp sterowniki/pci/pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp sterowniki/wejscie/input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-pointer
	.pointingtest/xhci-pointer

mysz.o: jadro/gui/ps2_mouse_parser.h jadro/gui/pointing_diag.h
hid_pointer.o: jadro/gui/pointing_diag.h sterowniki/wejscie/input_device.h
xhci.o usb.o: sterowniki/usb/ohci.h sterowniki/usb/hid_pointer.h

# One bounded shared diagnostic stream; production default is silent.
BURSZTYNDIAGXHCIPOINTING ?= 0
ifeq ($(BURSZTYNDIAGXHCIPOINTING),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGXHCIPOINTING=1
endif
input_device.o mysz.o grafika.o hid_pointer.o xhci.o ohci.o idt.o: jadro/gui/xhci_pointing_diag.h

.PHONY: test-irq-allocation-host
test-irq-allocation-host:
	@mkdir -p .pointingtest
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -DBURSZTYN_IDT_HOST_TEST -ffunction-sections -fdata-sections jadro/arch/idt.cpp testy/test_irq_allocation.cpp -Wl,--gc-sections -o .pointingtest/irq-allocation
	.pointingtest/irq-allocation

.PHONY: test-pointer-regression-host
test-pointer-regression-host:
	@mkdir -p .pointingtest
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) testy/test_pointer_regression.cpp sterowniki/usb/hid_pointer.cpp sterowniki/wejscie/input_device.cpp -o .pointingtest/pointer-regression
	.pointingtest/pointer-regression
	$(PYTHON) testy/test_pointer_boot_order.py
input_device.o grafika.o hid_pointer.o: jadro/gui/pointer_coordinates.h

.PHONY: test-pointer-graphics-host
test-pointer-graphics-host: $(filter-out .wm-test/runtime-host/testy/test_splash_production_path.o,$(STARTUP_HOST_OBJECTS))
	g++ $(HOST_INCLUDES) $(STARTUP_HOST_FLAGS) -DBURSZTYN_PMM_HOST_TEST -Wl,--gc-sections testy/test_pointer_graphics.cpp sterowniki/usb/hid_pointer.cpp $^ -o .wm-test/pointer-graphics
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) .wm-test/pointer-graphics

# Host frontend model -> QEMU event -> production InputDevice -> production
# cursor/hit-test, at every supported resolution (docs/virtio-gpu-mouse-reliability-2026-11-11.md).
.PHONY: test-mouse-pipeline-host
test-mouse-pipeline-host: $(filter-out .wm-test/runtime-host/testy/test_splash_production_path.o,$(STARTUP_HOST_OBJECTS))
	g++ $(HOST_INCLUDES) $(STARTUP_HOST_FLAGS) -DBURSZTYN_PMM_HOST_TEST -Wl,--gc-sections testy/test_mouse_pipeline_host.cpp sterowniki/usb/hid_pointer.cpp $^ -o .wm-test/mouse-pipeline
	@for r in 800x600 1024x768 1280x720 1920x1080 2560x1440; do \
		ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .wm-test/mouse-pipeline $${r%x*} $${r#*x} || exit 1; \
	done

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
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -DBURSZTYNDIAGXHCIPHYSICALMOUSE=0 -ffunction-sections -fdata-sections testy/test_xhci_physical_mouse.cpp jadro/arch/idt.cpp sterowniki/pci/pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp sterowniki/wejscie/input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-physical-mouse-production
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 .pointingtest/xhci-physical-mouse-production
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -DBURSZTYNDIAGXHCIPHYSICALMOUSE=1 -ffunction-sections -fdata-sections testy/test_xhci_physical_mouse.cpp jadro/arch/idt.cpp sterowniki/pci/pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp sterowniki/wejscie/input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-physical-mouse
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 .pointingtest/xhci-physical-mouse

BURSZTYNDIAGINPUTCOEXISTENCE ?= 0
ifeq ($(BURSZTYNDIAGINPUTCOEXISTENCE),1)
KERNEL_CXXFLAGS += -DBURSZTYNDIAGINPUTCOEXISTENCE=1
endif

.PHONY: test-input-coexistence-host
test-input-coexistence-host:
	python3 testy/test_input_diagnostic_parity.py

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
	$(PYTHON) testy/test_launcher_parity.py

.PHONY: test-virtio-input-qemu-parity-host
test-virtio-input-qemu-parity-host:
	$(PYTHON) testy/test_virtio_input_qemu_parity.py

.PHONY: test-virtio-input-forensic-host
test-virtio-input-forensic-host:
	@mkdir -p .virtiotest/forensic-validation
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_VIRTIO_HOST_TEST -DBURSZTYN_PMM_HOST_TEST -DBURSZTYNDIAGVIRTIOINPUTFORENSIC=$(BURSZTYNDIAGVIRTIOINPUTFORENSIC) -fsanitize=address,undefined -pthread sterowniki/pci/pci.cpp jadro/mm/pmm.cpp sterowniki/virtio/virtio_pci.cpp sterowniki/virtio/virtqueue.cpp sterowniki/virtio/virtio_rng.cpp sterowniki/virtio/virtio_blk.cpp sterowniki/siec/network_device.cpp sterowniki/virtio/virtio_net.cpp sterowniki/virtio/virtio_gpu.cpp sterowniki/wejscie/input_device.cpp sterowniki/virtio/virtio_input.cpp sterowniki/usb/usb_hid.cpp sterowniki/wejscie/klawiatura.cpp testy/test_virtio_input_forensic.cpp -o .virtiotest/forensic-validation/queue-$(BURSZTYNDIAGVIRTIOINPUTFORENSIC)
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .virtiotest/forensic-validation/queue-$(BURSZTYNDIAGVIRTIOINPUTFORENSIC)

.PHONY: test-usb-composite-host
test-usb-composite-host:
	@mkdir -p .pointingtest
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -no-pie testy/test_usb_composite.cpp sterowniki/usb/hid_pointer.cpp sterowniki/wejscie/input_device.cpp -o .pointingtest/usb-composite
	.pointingtest/usb-composite

.PHONY: test-xhci-multi-host
test-xhci-multi-host:
	@mkdir -p .pointingtest
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -no-pie -DBURSZTYN_BOOT_TIMING_HOST_TEST -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -ffunction-sections -fdata-sections testy/test_xhci_multi.cpp jadro/arch/idt.cpp sterowniki/pci/pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp sterowniki/wejscie/input_device.cpp -Wl,--gc-sections -o .pointingtest/xhci-multi
	.pointingtest/xhci-multi

.PHONY: test-xhci-multi-qemu
test-xhci-multi-qemu: all
	$(PYTHON) testy/test_xhci_multi_qemu.py
	$(PYTHON) testy/test_xhci_multi_qemu.py --virtio-input

# Native read-only system diagnostics utility (.cebula / BUR v1).
diagnostyka_tmp.o: programy/diagnostyka/diagnostyka.cpp programy/diagnostyka/model.h jadro/diag/diagnostyka_systemu_abi.h jadro/diag/diagnostyka_systemu_format.h $(wildcard bursztyn_ui/*.h)
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

diagnostyka_blob.o: diagnostyka_tmp.o bursztyn_gui.o programy/diagnostyka/diagnostyka_linker.ld
	$(LD) $(BUR_ORPHANS) -T $(call bur_linker,programy/diagnostyka/diagnostyka_linker.ld) $(RING3_LDFLAGS) diagnostyka_tmp.o bursztyn_gui.o -o diagnostyka.elf
	$(BUR_CONVERT) diagnostyka.elf diagnostyka.bin
	$(PYTHON) narzedzia/kompresuj_blob.py diagnostyka.bin diagnostyka.bin.bz
	$(LD) -r -b binary diagnostyka.bin.bz -o diagnostyka_blob.o

.PHONY: test-diagnostyka-systemu-host
test-diagnostyka-systemu-host:
	@mkdir -p .input-test/diagnostyka
	g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread $(HOST_INCLUDES) -DDIAG_HOST_TEST -DBURSZTYN_VIRTIO_HOST_TEST -fsanitize=address,undefined jadro/diag/diagnostyka_systemu.cpp jadro/diag/diagnostyka_systemu_bws.cpp sterowniki/wejscie/input_device.cpp testy/test_diagnostyka_systemu.cpp -o .input-test/diagnostyka/host
	ASAN_OPTIONS=detect_leaks=1 .input-test/diagnostyka/host

.PHONY: test-diagnostyka-qemu
test-diagnostyka-qemu: all
	$(PYTHON) testy/test_diagnostyka_qemu.py

.PHONY: test-diagnostyka-xhci-host
test-diagnostyka-xhci-host:
	@mkdir -p .input-test/diagnostyka
	g++ $(HOST_INCLUDES) $(POINTING_HOST_FLAGS) -no-pie -DDIAG_HOST_TEST -DBURSZTYN_BOOT_TIMING_HOST_TEST -DBURSZTYN_XHCI_HOST_TEST -DBURSZTYN_IDT_HOST_TEST -ffunction-sections -fdata-sections testy/test_diagnostyka_xhci.cpp jadro/diag/diagnostyka_systemu.cpp jadro/arch/idt.cpp sterowniki/pci/pci.cpp sterowniki/usb/hid_pointer.cpp sterowniki/usb/xhci_ring.cpp sterowniki/wejscie/input_device.cpp -Wl,--gc-sections -o .input-test/diagnostyka/xhci
	.input-test/diagnostyka/xhci

.PHONY: test-diagnostyka-packaging
test-diagnostyka-packaging: diagnostyka_blob.o
	$(PYTHON) testy/test_diagnostyka_packaging.py

.PHONY: test-husarz-engine-host test-husarz-runtime-host

test-husarz-engine-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(HOST_INCLUDES) testy/test_husarz_engine.cpp -o .husarz-test/engine
	ASAN_OPTIONS=detect_leaks=1 .husarz-test/engine

test-husarz-runtime-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -fno-inline -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) testy/test_husarz_runtime.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp -Wl,--gc-sections -o .husarz-test/runtime
	ASAN_OPTIONS=detect_leaks=1 .husarz-test/runtime

.PHONY: test-husarz-qemu
test-husarz-qemu: iso
	$(PYTHON) testy/test_husarz_qemu.py

.PHONY: test-user-fault-policy test-user-fault-qemu-build

test-user-fault-policy:
	@mkdir -p .husarz-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined testy/test_user_fault_policy.cpp -o .husarz-test/user-fault-policy
	.husarz-test/user-fault-policy

# Substitute only the browser payload in a separate linked test kernel. The
# production kernel, browser BUR and ordinary ISO are never overwritten.
test-user-fault-qemu-build: all
	@mkdir -p .husarz-test/user-fault
	$(CC) $(ASM_FLAGS) -c narzedzia/fixtures/husarz/user_fault.S -o .husarz-test/user-fault/probe.o
	$(LD) --gc-sections -T $(call bur_linker,$(PRZEGLADARKA_LINKER)) $(RING3_LDFLAGS) .husarz-test/user-fault/probe.o ring3_ssp.o -o .husarz-test/user-fault/probe.elf
	$(BUR_CONVERT) .husarz-test/user-fault/probe.elf .husarz-test/user-fault/przegladarka.bin
	cd .husarz-test/user-fault && $(LD) -r -b binary przegladarka.bin -o probe-blob.o
	$(CXX) $(KERNEL_LDFLAGS) -o .husarz-test/user-fault/kernel.bin $(filter-out przegladarka_blob.o,$(OBJS)) .husarz-test/user-fault/probe-blob.o -lgcc

.PHONY: test-user-page-fault-qemu-build
test-user-page-fault-qemu-build: all
	@mkdir -p .husarz-test/user-page-fault
	$(CC) $(ASM_FLAGS) -DHUSARZ_FAULT_PAGE=1 -c narzedzia/fixtures/husarz/user_fault.S -o .husarz-test/user-page-fault/probe.o
	$(LD) --gc-sections -T $(call bur_linker,$(PRZEGLADARKA_LINKER)) $(RING3_LDFLAGS) .husarz-test/user-page-fault/probe.o ring3_ssp.o -o .husarz-test/user-page-fault/probe.elf
	$(BUR_CONVERT) .husarz-test/user-page-fault/probe.elf .husarz-test/user-page-fault/przegladarka.bin
	cd .husarz-test/user-page-fault && $(LD) -r -b binary przegladarka.bin -o probe-blob.o
	$(CXX) $(KERNEL_LDFLAGS) -o .husarz-test/user-page-fault/kernel.bin $(filter-out przegladarka_blob.o,$(OBJS)) .husarz-test/user-page-fault/probe-blob.o -lgcc

.PHONY: test-husarz-stack
test-husarz-stack: przegladarka_tmp.o
	$(PYTHON) testy/test_husarz_stack.py

.PHONY: test-husarz-fidelity-host
test-husarz-fidelity-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(HOST_INCLUDES) testy/test_husarz_fidelity.cpp -o .husarz-test/fidelity
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/fidelity

.PHONY: test-husarz-layout-host
test-husarz-layout-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(HOST_INCLUDES) testy/test_husarz_layout.cpp -o .husarz-test/layout
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/layout

.PHONY: test-husarz-performance-host
test-husarz-performance-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(HOST_INCLUDES) testy/test_husarz_performance.cpp -o .husarz-test/performance
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/performance

.PHONY: test-husarz-selection-host
test-husarz-selection-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(HOST_INCLUDES) testy/test_husarz_selection.cpp -o .husarz-test/selection
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/selection

.PHONY: test-husarz-gif-host
test-husarz-gif-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(HOST_INCLUDES) testy/test_husarz_gif.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp -o .husarz-test/gif
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/gif

.PHONY: test-husarz-javascript-host
test-husarz-javascript-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O1 -g -Wall -Wextra -Wno-misleading-indentation -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(HOST_INCLUDES) testy/test_husarz_javascript.cpp -o .husarz-test/javascript
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/javascript

.PHONY: test-fpu-host
test-fpu-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -O2 -mno-sse -mno-sse2 -mno-mmx $(HOST_INCLUDES) testy/test_fpu_host.cpp -o .husarz-test/fpu
	.husarz-test/fpu

fpu_probe_tmp.o: programy/fpu_probe.cpp jadro/arch/fpu.h jadro/gui/bursztyn_gui.h
	$(CXX) $(RING3_FLAGS) -mgeneral-regs-only -I. -Os -c $< -o $@
fpu_probe_blob.o: fpu_probe_tmp.o bursztyn_gui_minimal.o $(PRZEGLADARKA_LINKER)
	$(LD) --defsym=HUSARZ_WORKER_BUILD=1 --gc-sections -T $(call bur_linker,$(PRZEGLADARKA_LINKER)) $(RING3_LDFLAGS) fpu_probe_tmp.o bursztyn_gui_minimal.o -o fpu_probe.elf
	$(BUR_CONVERT) fpu_probe.elf fpu_probe.bin
	$(PYTHON) narzedzia/kompresuj_blob.py fpu_probe.bin fpu_probe.bin.bz
	$(LD) -r -b binary fpu_probe.bin.bz -o $@

.PHONY: test-kernel-simd
test-kernel-simd: $(KERNEL)
	$(PYTHON) testy/test_kernel_simd.py $(KERNEL)

.PHONY: test-network-lease-host
test-network-lease-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie $(HOST_INCLUDES) testy/test_network_lease.cpp -o .husarz-test/network-lease
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/network-lease

.PHONY: test-husarz-css-host
test-husarz-css-host:
	@mkdir -p .husarz-test
	g++ -std=c++17 -fcoroutines -O0 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie $(HOST_INCLUDES) testy/test_husarz_css_compat.cpp -o .husarz-test/css-compat
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .husarz-test/css-compat

.PHONY: test-husarz-bidi-host
test-husarz-bidi-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -fcoroutines -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) testy/test_husarz_bidi.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/husarz-bidi
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/husarz-bidi

.PHONY: bench-husarz-bidi
bench-husarz-bidi:
	@mkdir -p .bsf-test
	g++ -std=c++17 -fcoroutines -O2 -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) testy/bench_husarz_bidi.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/bench-husarz-bidi
	.bsf-test/bench-husarz-bidi

.PHONY: test-bsf-host
test-bsf-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections testy/test_bsf.cpp bsf/bsf.cpp -Wl,--gc-sections -o .bsf-test/bsf-host
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/bsf-host

.PHONY: test-bsf-ui-host
test-bsf-ui-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -fcoroutines -O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) testy/test_bsf_ui.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/ui-host
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/ui-host

.PHONY: profile-bsf-host
profile-bsf-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O2 -ffunction-sections -fdata-sections narzedzia/profile_bsf.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/profile
	.bsf-test/profile

.PHONY: test-desktop-ttf-host
test-desktop-ttf-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -fcoroutines -O0 -g0 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) testy/test_desktop_ttf.cpp bursztyn_ui/ikona_png.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/desktop-ttf-host
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/desktop-ttf-host

.PHONY: test-slider-pixels-host
test-slider-pixels-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-pie -no-pie -ffunction-sections -fdata-sections testy/test_slider_pixels.cpp -Wl,--gc-sections -o .bsf-test/slider-pixels
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/slider-pixels

.PHONY: test-desktop-rubber-host
test-desktop-rubber-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -fcoroutines -O0 -g0 -DBURSZTYN_RUBBER_ONLY -fsanitize=address,undefined -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) testy/test_desktop_ttf.cpp bursztyn_ui/ikona_png.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/rubber-interleave-final
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/rubber-interleave-final

wsparcie_mecenasi_blob.o: dane/sponsorze/mecenasi.txt
	$(LD) -r -b binary $< -o $@

wsparcie_sponsorzy_blob.o: dane/sponsorze/sponsorzy.txt
	$(LD) -r -b binary $< -o $@

wsparcie_wkladtworczy_blob.o: dane/sponsorze/wkladtworczy.txt
	$(LD) -r -b binary $< -o $@

.PHONY: test-display-geometry-host test-display-snapshot-host
test-display-geometry-host: $(filter-out .wm-test/runtime-host/testy/test_splash_production_path.o,$(STARTUP_HOST_OBJECTS)) .wm-test/runtime-host/pmm.o
	g++ $(HOST_INCLUDES) $(STARTUP_HOST_FLAGS) -DBURSZTYN_PMM_HOST_TEST -Wl,--gc-sections testy/test_display_geometry_host.cpp jadro/gui/ekran_format.cpp $^ -o .wm-test/display-geometry
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .wm-test/display-geometry

test-display-snapshot-host:
	@mkdir -p .wm-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -fsanitize=address,undefined testy/test_display_snapshot.cpp -Wl,--gc-sections -o .wm-test/display-snapshot
	ASAN_OPTIONS=$(STARTUP_ASAN_OPTIONS) UBSAN_OPTIONS=halt_on_error=1 .wm-test/display-snapshot

# Keep new retained-paint dependencies explicit for checkouts carrying older .d files.
grafika.o syscalls.o bursztyn_gui_base.o bursztyn_gui_minimal.o menedzer_okien_tmp.o: jadro/bws/bws_gui_batch.h
skladacz_obrazu.o: jadro/gui/desktop_modeset.h jadro/gui/display_geometry.h
menedzer_okien_tmp.o: jadro/gui/desktop_selection_paint.h

.PHONY: test-settings-themes-power-host
test-settings-themes-power-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O0 -g -fsanitize=address,undefined -fno-pie -no-pie -ffunction-sections -fdata-sections testy/test_settings_themes_power.cpp bursztyn_ui/ikona_png.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp -Wl,--gc-sections -o .bsf-test/settings-themes-power
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/settings-themes-power

menedzer_okien_tmp.o: jadro/gui/start_menu_ui.h obrazy/dekoder_obrazu.h

ikona_zasilania_blob.o: ikonki/przyciskzasilania.png
	$(LD) -r -b binary $< -o $@

ikona_uruchomponownie_blob.o: ikonki/przyciskuruchomponownie.png
	$(LD) -r -b binary $< -o $@

ikona_zamknij_blob.o: ikonki/przyciskzamknij.png
	$(LD) -r -b binary $< -o $@

ikona_uspij_blob.o: ikonki/przyciskuspij.png
	$(LD) -r -b binary $< -o $@

ikona_zablokuj_blob.o: ikonki/przyciskzablokuj.png
	$(LD) -r -b binary $< -o $@

ikona_nav_wstecz_blob.o: ikonki/nav_wstecz.png
	$(LD) -r -b binary $< -o $@

ikona_nav_dalej_blob.o: ikonki/nav_dalej.png
	$(LD) -r -b binary $< -o $@

ikona_nav_wgore_blob.o: ikonki/nav_wgore.png
	$(LD) -r -b binary $< -o $@

ikona_folder_blob.o: ikonki/folder.png
	$(LD) -r -b binary $< -o $@

ikona_plik_blob.o: ikonki/plik.png
	$(LD) -r -b binary $< -o $@

ikona_app_notatnik_blob.o: ikonki/app_notatnik.png
	$(LD) -r -b binary $< -o $@

ikona_app_kalkulator_blob.o: ikonki/app_kalkulator.png
	$(LD) -r -b binary $< -o $@

ikona_app_husarz_blob.o: ikonki/app_husarz.png
	$(LD) -r -b binary $< -o $@

ikona_app_testy_blob.o: ikonki/app_testy.png
	$(LD) -r -b binary $< -o $@

ikona_app_snake_blob.o: ikonki/app_snake.png
	$(LD) -r -b binary $< -o $@

ikona_app_eksplorator_blob.o: ikonki/app_eksplorator.png
	$(LD) -r -b binary $< -o $@

ikona_app_obrazy_blob.o: ikonki/app_obrazy.png
	$(LD) -r -b binary $< -o $@

ikona_app_demo_ui_blob.o: ikonki/app_demo_ui.png
	$(LD) -r -b binary $< -o $@

ikona_app_kontrolki2_blob.o: ikonki/app_kontrolki2.png
	$(LD) -r -b binary $< -o $@

ikona_app_kontrolki3_blob.o: ikonki/app_kontrolki3.png
	$(LD) -r -b binary $< -o $@

ikona_app_kontrolki4_blob.o: ikonki/app_kontrolki4.png
	$(LD) -r -b binary $< -o $@

ikona_app_aktualizacje_blob.o: ikonki/app_aktualizacje.png
	$(LD) -r -b binary $< -o $@

ikona_app_ustawienia_blob.o: ikonki/app_ustawienia.png
	$(LD) -r -b binary $< -o $@

ikona_app_test_dzwieku_blob.o: ikonki/app_test_dzwieku.png
	$(LD) -r -b binary $< -o $@

ikona_app_diagnostyka_blob.o: ikonki/app_diagnostyka.png
	$(LD) -r -b binary $< -o $@

ikona_app_shell_blob.o: ikonki/app_shell.png
	$(LD) -r -b binary $< -o $@

ikona_app_tapety_blob.o: ikonki/app_tapety.png
	$(LD) -r -b binary $< -o $@

ikona_png_ring3.o: bursztyn_ui/ikona_png.cpp bursztyn_ui/ikona_png.h
	$(CXX) $(RING3_FLAGS) -Os -fstack-usage -c $< -o $@

.PHONY: test-power-png-host
test-power-png-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-pie -no-pie -fno-omit-frame-pointer obrazy/test_ikona_png.cpp obrazy/dekoder_obrazu.cpp narzedzia/tapety-btp/tapetybtp.cpp -o .bsf-test/power-png-decoder
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/power-png-decoder

image_png_ring3.o: obrazy/dekoder_obrazu.cpp obrazy/dekoder_obrazu.h obrazy/vendor/stb_image.h
	$(CXX) $(RING3_FLAGS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Os -DBURSZTYN_PNG_ONLY=1 -fstack-usage -c $< -o $@

.PHONY: test-stack-protector-host test-stack-protector-qemu test-stack-protector test-bws-abuse-qemu test-fuzz-smoke fuzz-extended test-bur-v2-aslr-host test-security-hardening
test-stack-protector-host:
	@mkdir -p .ssp-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -fsanitize=address,undefined testy/test_stack_protector_host.cpp -o .ssp-test/test_ssp_asan
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .ssp-test/test_ssp_asan
	g++ -std=c++17 -O2 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -fstack-protector-strong -DBURSZTYN_SSP_DEATH_TEST=1 testy/test_stack_protector_host.cpp -o .ssp-test/test_ssp_death
	.ssp-test/test_ssp_death

test-stack-protector-qemu: iso
	$(PYTHON) testy/test_stack_protector_qemu.py

test-stack-protector: test-stack-protector-host test-stack-protector-qemu

test-bws-abuse-qemu: iso
	$(PYTHON) testy/test_bws_abuse_qemu.py

test-fuzz-smoke:
	$(PYTHON) narzedzia/run_fuzz_suite.py --mode=smoke

fuzz-extended:
	$(PYTHON) narzedzia/run_fuzz_suite.py --mode=extended

test-bur-v2-aslr-host:
	@mkdir -p .aslr-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic $(HOST_INCLUDES) -DBURSZTYN_LOADER_HOST_TEST -fsanitize=address,undefined jadro/fs/loader.cpp testy/test_bur_v2_aslr_host.cpp -o .aslr-test/test_bur_v2_aslr_host
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .aslr-test/test_bur_v2_aslr_host

# Host-only security slice (szybki fragment gate'u security).
test-security-host: test-stack-protector-host test-bur-v2-aslr-host test-fuzz-smoke

# Pelny gate bezpieczenstwa: host + QEMU SSP/abuse + guard-page.
# Opcjonalnie: make test-security STACK_AUDIT=1
test-security: test-security-host test-stack-protector-qemu test-bws-abuse-qemu test-guard-page
	@if [ "$(STACK_AUDIT)" = "1" ]; then $(MAKE) audit-stos-jadra; fi

# Alias wstecznie kompatybilny — teraz obejmuje tez QEMU abuse i guard-page.
test-security-hardening: test-security

# Szybki lokalny gate przed commitem (bez fuzz-extended).
test-smoke: test-stack-protector-host test-bur-v2-aslr-host test-fuzz-smoke \
	test-virtio-pci test-motywy-unit test-wm-click-routing test-normal-boot \
	test-startup-visibility test-compositor-cursor test-powitanie

test: test-smoke

# Regresja produktowa (dluzsza matryca QEMU/host).
test-regression: test-virtio-qemu test-virtio-blk-qemu test-virtio-net-qemu \
	test-virtio-gpu-qemu test-virtio-input-qemu \
	test-ota installer-test \
	test-husarz-engine-host test-input-coexistence-host \
	test-hda-regression-qemu

# Nocny przebieg: security + regression + pelny fuzz.
test-nightly: test-security test-regression fuzz-extended

# Orkiestrator z timeoutami i raportem JSON w .local-ci/
MODE ?= smoke
test-local-suite:
	$(PYTHON) narzedzia/run_local_suite.py --mode=$(MODE)


.PHONY: bur-format-force bur-v1 bur-v2 test-bur-v2-qemu test-bur-v2-converter
.bur-format-config: bur-format-force
	@printf '%s\n' '$(BUR_FORMAT)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
aktualizacje_tmp.o bsf_bitmap.o bsf_core.o bsf_runtime.o bsf_terminal.o btp_ring3.o bursztyn_gui_base.o bursztyn_gui_minimal_base.o demo_ui_tmp.o diagnostyka_tmp.o ekran_format_ring3.o ekran_tmp.o eksplorator_tmp.o fpu_probe_tmp.o husarz_worker_tmp.o ikona_png_ring3.o image_png_ring3.o image_ring3.o instalator_tmp.o kalkulator_tmp.o kontrolki2_tmp.o kontrolki3tmp.o kontrolki4tmp.o menedzer_okien_tmp.o notatnik_tmp.o przegladarka_obrazow_tmp.o przegladarka_tmp.o repro_tmp.o ring3_ssp.o shell_tmp.o snake_tmp.o okno_informacyjne_tmp.o tapety_tmp.o test_dzwieku_tmp.o test_tmp.o ustawienia_tmp.o: .bur-format-config
bur-v1:
	$(MAKE) BUR_FORMAT=1 all ustawienia_blob.o
bur-v2:
	$(MAKE) BUR_FORMAT=2 all ustawienia_blob.o

test-bur-v2-qemu: all
	$(PYTHON) narzedzia/test_bur_v2_qemu.py

.PHONY: test-bsf-graphemes-host
test-bsf-graphemes-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-pie -no-pie narzedzia/test_bsf_graphemes.cpp bsf/bsf.cpp -o .bsf-test/graphemes
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/graphemes

$(GRUB_CFG): czcionki/release-fonts.txt narzedzia/bsf_release_assets.py
bsf_core.o: bsf/unicode.inc bsf/unicode_data.inc

.PHONY: test-notepad-unicode-host
test-notepad-unicode-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -fcoroutines -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections -I. narzedzia/test_notepad_unicode.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/notepad-unicode
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/notepad-unicode

.PHONY: test-notepad-bidi-host
test-notepad-bidi-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -fcoroutines -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections -I. testy/test_notepad_bidi.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/notepad-bidi
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/notepad-bidi

.PHONY: test-bursztynui-bidi-host
test-bursztynui-bidi-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -fcoroutines -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections -I. testy/test_bursztynui_bidi.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/bursztynui-bidi
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/bursztynui-bidi

.PHONY: bench-notepad-bidi
bench-notepad-bidi:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -fcoroutines -O2 -fno-pie -no-pie -ffunction-sections -fdata-sections -I. testy/bench_notepad_bidi.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/bench-notepad-bidi
	.bsf-test/bench-notepad-bidi

.PHONY: fuzz-bsf-coverage-host
fuzz-bsf-coverage-host:
	@mkdir -p .bsf-test/fuzz-corpus
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fsanitize-coverage=trace-pc -fno-omit-frame-pointer -fno-pie -ffunction-sections -fdata-sections -c bsf/bsf.cpp -o .bsf-test/bsf-coverage.o
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections narzedzia/fuzz_bsf_coverage.cpp .bsf-test/bsf-coverage.o -Wl,--gc-sections -o .bsf-test/fuzz-bsf-coverage
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/fuzz-bsf-coverage czcionki/ttf/DejaVuSans.ttf .bsf-test/fuzz-corpus 10000

.PHONY: bsf-editor-observer
bsf-editor-observer: .bsf-test/editor-observer.json
.bsf-test/editor-observer.json: narzedzia/bsf_editor_observer.cpp programy/notatnik/notatnik.cpp $(wildcard bursztyn_ui/*.h) bsf/edit.h
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -fcoroutines -O0 -ffunction-sections -fdata-sections -Wno-invalid-offsetof $< -Wl,--gc-sections -o .bsf-test/editor-observer
	.bsf-test/editor-observer > $@

.PHONY: test-usb-keyboard-repeat-host
test-usb-keyboard-repeat-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-pie -no-pie -I. narzedzia/test_usb_keyboard_repeat.cpp -o .bsf-test/usb-keyboard-repeat
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/usb-keyboard-repeat

.PHONY: test-bsf-resources-host
test-bsf-resources-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fcheck-new -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections -I. narzedzia/test_bsf_resources.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/resources
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/resources

bsf_terminal.o: bsf/terminal.cpp bsf/terminal.h bsf/terminal_width.inc
	$(CXX) $(RING3_FLAGS) -Os -c $< -o $@

.PHONY: test-shell-ttf-host
test-shell-ttf-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -DBSF_NATIVE=1 -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-pie -no-pie testy/test_shell_terminal.cpp bsf/bsf.cpp bsf/terminal.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/shell-ttf
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/shell-ttf

.PHONY: bsf-display-layout
bsf-display-layout:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -I. -ffunction-sections -fdata-sections narzedzia/bsf_display_layout.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .bsf-test/display-layout-generator
	.bsf-test/display-layout-generator

.PHONY: bsf-installer-observer
bsf-installer-observer:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -fcoroutines -O0 -ffunction-sections -fdata-sections -I. narzedzia/bsf_installer_observer.cpp -Wl,--gc-sections -o .bsf-test/installer-observer
	.bsf-test/installer-observer > .bsf-test/installer-observer.json

.PHONY: test-package-signature-host
test-package-signature-host: test_dzwieku_blob.o
	$(PYTHON) testy/test_podpis_paczki.py

# Bounded graphics telemetry is shared by production and host fixtures.
grafika.o skladacz_obrazu.o virtio_gpu.o ekran.o: jadro/gui/graphics_perf.h

grafika.o skladacz_obrazu.o ekran.o kernel.o: jadro/gui/display_mode_policy.h

grafika.o skladacz_obrazu.o: jadro/gui/graphics_copy.h

bufory_grafiki.o: jadro/gui/graphics_perf.h
.PHONY: test-display-mode-policy
test-display-mode-policy:
	@mkdir -p .wm-test
	g++ -std=c++17 -O2 -I. -fsanitize=address,undefined testy/test_display_mode_policy.cpp -o .wm-test/display-mode-policy
	.wm-test/display-mode-policy

# The source moved to jadro/mm; retain the shared host fixture object path.
.wm-test/runtime-host/pmm.o: jadro/mm/pmm.cpp
	@mkdir -p $(@D)
	g++ $(HOST_INCLUDES) $(STARTUP_HOST_FLAGS) -MMD -MP -c $< -o $@

virtio_gpu.o: jadro/gui/display_mode_policy.h

# ---------------------------------------------------------------- BSF 2.0
.PHONY: test-bsf2-kernel-abi-host test-bsf-bidi-host test-bsf-shaping-host test-bsf-layout-host test-bsf2-host bsf-shaping-oracle
test-bsf-bidi-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie testy/test_bsf_bidi.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .bsf-test/bidi
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/bidi
test-bsf-shaping-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie testy/test_bsf_shaping.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .bsf-test/shaping
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/shaping
test-bsf-layout-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie testy/test_bsf_layout.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .bsf-test/layout
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/layout
# Regenerate expectations with the host HarfBuzz (development oracle only).
bsf-shaping-oracle:
	$(PYTHON) narzedzia/generate_bsf_shaping_oracle.py
test-bsf2-host: test-bsf-bidi-host test-bsf-shaping-host test-bsf-layout-host test-bsf2-kernel-abi-host
.PHONY: test-bsf21-layout-host
test-bsf21-layout-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie testy/test_bsf21_layout.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .bsf-test/bsf21-layout
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/bsf21-layout

# ---- BSF 2.2 font formats (host; FreeType/HarfBuzz are test-only oracles) --
BSF22_SAN := -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie
BSF22_ENV := ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
FREETYPE_CFLAGS = $(shell pkg-config --cflags freetype2)
FREETYPE_LIBS = $(shell pkg-config --libs freetype2)
.PHONY: test-bsf22-cff-host test-bsf22-shaping-host test-bsf22-host test-bsf22-collections-host
test-bsf22-cff-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) $(BSF22_SAN) $(FREETYPE_CFLAGS) testy/test_bsf22_cff.cpp bsf/bsf.cpp $(FREETYPE_LIBS) -o .bsf-test/bsf22-cff
	$(BSF22_ENV) .bsf-test/bsf22-cff
# Same production shaper as test-bsf-shaping-host, BSF 2.2 format corpora.
test-bsf22-shaping-host: test-bsf-shaping-host
	$(BSF22_ENV) .bsf-test/shaping testy/fixtures/bsf22/shaping_cff.tsv --subset
	$(BSF22_ENV) .bsf-test/shaping testy/fixtures/bsf22/shaping_variations.tsv --subset
	$(BSF22_ENV) .bsf-test/shaping testy/fixtures/bsf22/shaping_emoji.tsv --subset
test-bsf22-collections-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) $(BSF22_SAN) -fcheck-new $(FREETYPE_CFLAGS) testy/test_bsf22_collections.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp $(FREETYPE_LIBS) -o .bsf-test/bsf22-collections
	$(BSF22_ENV) .bsf-test/bsf22-collections
.PHONY: test-bsf22-font-catalog-host
test-bsf22-font-catalog-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) $(BSF22_SAN) -fcheck-new $(FREETYPE_CFLAGS) testy/test_bsf22_font_catalog.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp bsf/terminal.cpp $(FREETYPE_LIBS) -o .bsf-test/bsf22-font-catalog
	$(BSF22_ENV) .bsf-test/bsf22-font-catalog
# Vendored Brotli decoder (bsf/vendor/brotli, v1.1.0, MIT) — WOFF2 only.
BROTLI_INCLUDES := -Ibsf/vendor/brotli/include
BROTLI_HOST_OBJECTS := $(patsubst bsf/vendor/brotli/%.c,.bsf-test/brotli-host/%.o,$(BROTLI_DEC_SOURCES))
.bsf-test/brotli-host/%.o: bsf/vendor/brotli/%.c
	@mkdir -p $(dir $@)
	gcc -std=c99 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(BROTLI_INCLUDES) -c $< -o $@
.PHONY: test-bsf22-woff-host
test-bsf22-woff-host: $(BROTLI_HOST_OBJECTS)
	g++ $(HOST_INCLUDES) $(BSF22_SAN) $(BROTLI_INCLUDES) $(FREETYPE_CFLAGS) testy/test_bsf22_woff.cpp bsf/webfont.cpp bsf/bsf.cpp $(BROTLI_HOST_OBJECTS) $(FREETYPE_LIBS) -o .bsf-test/bsf22-woff
	$(BSF22_ENV) .bsf-test/bsf22-woff
test-bsf22-host: test-bsf22-cff-host test-bsf22-shaping-host test-bsf22-collections-host test-bsf22-font-catalog-host test-bsf22-woff-host test-bsf22-husarz-webfonts-host test-bsf22-variations-host test-bsf22-color-host test-bsf22-emoji-host

# BWS 51 (incl. command 2) boundary/negative tests on the production handler
# source with a simulated Ring 3 address space, Ring 3 blob validation and the
# desktop BMP Multiboot2 module validator.
test-bsf2-kernel-abi-host: testy/test_bsf2_kernel_abi.cpp jadro/gui/czcionka_systemowa.cpp jadro/rdzen/obrazy_startowe.h bsf/unicode_layout.h bsf/unicode_layout_index.h bsf/unicode_layout_data.inc
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -I. -Ijadro/bws -Ijadro/gui -fsanitize=address,undefined -fno-omit-frame-pointer testy/test_bsf2_kernel_abi.cpp -o .bsf-test/kernel-abi
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/kernel-abi

# BSF 2.0 coverage-guided fuzzing (ASan + UBSan + trace-pc) on testy/fuzz/fuzz_engine.h.
# Iterations: BSF2_FUZZ_ITERS (default per target below). Corpora: .fuzz-corpus/bsf2.
BSF2_FUZZ_FLAGS := -std=c++17 -O1 -g -I. -fsanitize=address,undefined -fsanitize-coverage=trace-pc -fno-omit-frame-pointer
BSF2_FUZZ_ENV := ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
.PHONY: fuzz-bsf2-smoke
fuzz-bsf2-smoke:
	@mkdir -p .fuzz-build/bsf2 .fuzz-corpus/bsf2
	g++ $(BSF2_FUZZ_FLAGS) testy/fuzz/fuzz_bsf_bidi.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .fuzz-build/bsf2/fuzz_bsf_bidi
	g++ $(BSF2_FUZZ_FLAGS) testy/fuzz/fuzz_bsf_layout.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .fuzz-build/bsf2/fuzz_bsf_layout
	g++ $(BSF2_FUZZ_FLAGS) testy/fuzz/fuzz_bsf_otl.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .fuzz-build/bsf2/fuzz_bsf_otl
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf2/fuzz_bsf_bidi --iterations=$(or $(BSF2_FUZZ_ITERS),20000) --corpus-out=.fuzz-corpus/bsf2/bidi --metrics-out=.fuzz-build/bsf2/bidi.json
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf2/fuzz_bsf_layout --iterations=$(or $(BSF2_FUZZ_ITERS),5000) --corpus-out=.fuzz-corpus/bsf2/layout --metrics-out=.fuzz-build/bsf2/layout.json
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf2/fuzz_bsf_otl --iterations=$(or $(BSF2_FUZZ_ITERS),3000) --corpus-out=.fuzz-corpus/bsf2/otl --metrics-out=.fuzz-build/bsf2/otl.json

# BSF 2.1 editing and Husarz bidi fuzzers (ASan + UBSan + trace-pc).
.PHONY: fuzz-bsf21-smoke
fuzz-bsf21-smoke:
	@mkdir -p .fuzz-build/bsf21 .fuzz-corpus/bsf21
	g++ $(BSF2_FUZZ_FLAGS) testy/fuzz/fuzz_bsf21_editing.cpp $(BSF2_SOURCES) bsf/bsf.cpp -o .fuzz-build/bsf21/fuzz_bsf21_editing
	g++ $(BSF2_FUZZ_FLAGS) -fcoroutines -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) -Itesty testy/fuzz/fuzz_husarz_bidi.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp -Wl,--gc-sections -o .fuzz-build/bsf21/fuzz_husarz_bidi
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf21/fuzz_bsf21_editing --iterations=$(or $(BSF21_FUZZ_ITERS),4000) --corpus-out=.fuzz-corpus/bsf21/editing --metrics-out=.fuzz-build/bsf21/editing.json
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf21/fuzz_husarz_bidi --iterations=$(or $(BSF21_HUSARZ_FUZZ_ITERS),1500) --corpus-out=.fuzz-corpus/bsf21/husarz --metrics-out=.fuzz-build/bsf21/husarz.json

.PHONY: test-bsf22-husarz-webfonts-host
test-bsf22-husarz-webfonts-host: $(BROTLI_HOST_OBJECTS)
	@mkdir -p .bsf-test
	g++ -std=c++17 -fcoroutines -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie -ffunction-sections -fdata-sections $(HOST_INCLUDES) $(BROTLI_INCLUDES) testy/test_bsf22_husarz_webfonts.cpp bsf/webfont.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp $(BROTLI_HOST_OBJECTS) -Wl,--gc-sections -o .bsf-test/bsf22-husarz-webfonts
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/bsf22-husarz-webfonts

.PHONY: test-bsf22-variations-host
test-bsf22-variations-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) $(BSF22_SAN) $(FREETYPE_CFLAGS) testy/test_bsf22_variations.cpp bsf/bsf.cpp $(FREETYPE_LIBS) -o .bsf-test/bsf22-variations
	$(BSF22_ENV) .bsf-test/bsf22-variations

.PHONY: test-bsf22-color-host
test-bsf22-color-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) $(BSF22_SAN) $(FREETYPE_CFLAGS) testy/test_bsf22_color.cpp bsf/bsf.cpp $(FREETYPE_LIBS) -o .bsf-test/bsf22-color
	$(BSF22_ENV) .bsf-test/bsf22-color

.PHONY: test-bsf22-emoji-host
test-bsf22-emoji-host:
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) $(BSF22_SAN) -fcheck-new $(FREETYPE_CFLAGS) testy/test_bsf22_emoji.cpp bsf/bsf.cpp bsf/runtime.cpp $(BSF2_SOURCES) bsf/bitmap.cpp $(FREETYPE_LIBS) -o .bsf-test/bsf22-emoji
	$(BSF22_ENV) .bsf-test/bsf22-emoji

# BSF 2.2 coverage-guided fuzzing: fonts (sfnt/collections/CFF/CFF2/gvar/
# colour), WOFF/WOFF2 and PNG strikes. Iterations: BSF22_FUZZ_ITERS.
.PHONY: fuzz-bsf22
fuzz-bsf22: $(BROTLI_HOST_OBJECTS)
	@mkdir -p .fuzz-build/bsf22 .fuzz-corpus/bsf22
	g++ $(BSF2_FUZZ_FLAGS) -DBSF22_TARGET=1 testy/fuzz/fuzz_bsf22.cpp bsf/bsf.cpp -o .fuzz-build/bsf22/fuzz_bsf22_font
	g++ $(BSF2_FUZZ_FLAGS) $(BROTLI_INCLUDES) -DBSF22_TARGET=2 testy/fuzz/fuzz_bsf22.cpp bsf/webfont.cpp bsf/bsf.cpp $(BROTLI_HOST_OBJECTS) -o .fuzz-build/bsf22/fuzz_bsf22_webfont
	g++ $(BSF2_FUZZ_FLAGS) -DBSF22_TARGET=3 testy/fuzz/fuzz_bsf22.cpp bsf/bsf.cpp -o .fuzz-build/bsf22/fuzz_bsf22_png
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf22/fuzz_bsf22_font --iterations=$(or $(BSF22_FUZZ_ITERS),20000) --corpus-out=.fuzz-corpus/bsf22/font --metrics-out=.fuzz-build/bsf22/font.json
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf22/fuzz_bsf22_webfont --iterations=$(or $(BSF22_FUZZ_ITERS),20000) --corpus-out=.fuzz-corpus/bsf22/webfont --metrics-out=.fuzz-build/bsf22/webfont.json
	$(BSF2_FUZZ_ENV) .fuzz-build/bsf22/fuzz_bsf22_png --iterations=$(or $(BSF22_FUZZ_ITERS),50000) --corpus-out=.fuzz-corpus/bsf22/png --metrics-out=.fuzz-build/bsf22/png.json

.PHONY: bench-bsf22
BROTLI_BENCH_OBJECTS := $(patsubst bsf/vendor/brotli/%.c,.bsf-test/brotli-o2/%.o,$(BROTLI_DEC_SOURCES))
.bsf-test/brotli-o2/%.o: bsf/vendor/brotli/%.c
	@mkdir -p $(dir $@)
	gcc -std=c99 -O2 $(BROTLI_INCLUDES) -c $< -o $@
bench-bsf22: $(BROTLI_BENCH_OBJECTS)
	@mkdir -p .bsf-test
	g++ $(HOST_INCLUDES) $(BROTLI_INCLUDES) -std=c++17 -O2 testy/bench_bsf22.cpp bsf/webfont.cpp bsf/bsf.cpp $(BSF2_SOURCES) $(BROTLI_BENCH_OBJECTS) -o .bsf-test/bench-bsf22
	.bsf-test/bench-bsf22

# Kernel compressed application blobs (jadro/rdzen/blob_skompresowany.h):
# decoder vs zlib on every built application binary + corruption tests.
.PHONY: test-window-geometry-host
test-window-geometry-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer testy/test_okno_geometria.cpp -o .bsf-test/oknogeo
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/oknogeo

# Tozsamosc aplikacji systemowych: SHA-256 plikow .bur zbudowanych razem z jadrem.
SKROTY_SYSTEMOWE_H := jadro/fs/skroty_systemowe.generated.h
SKROTY_SYSTEMOWE_APPS := ustawienia_blob.o diagnostyka_blob.o test_dzwieku_blob.o aktualizacje_blob.o ekran_blob.o przegladarka_obrazow_blob.o okno_informacyjne_blob.o
$(SKROTY_SYSTEMOWE_H): $(SKROTY_SYSTEMOWE_APPS) narzedzia/generuj_skroty_systemowe.py
	$(PYTHON) narzedzia/generuj_skroty_systemowe.py $@ \
		/programy/ustawienia.cebula/ustawienia.bur=ustawienia.bin \
		/programy/diagnostyka.cebula/diagnostyka.bur=diagnostyka.bin \
		/programy/test_dzwieku.cebula/test_dzwieku.bur=test_dzwieku.bin \
		/programy/aktualizacje.cebula/aktualizacje.bur=aktualizacje.bin \
		/programy/ekran.cebula/ekran.bur=ekran.bin \
		/programy/przegladarka-obrazow.cebula/przegladarka-obrazow.bur=przegladarka_obrazow.bin \
		/programy/okno-informacyjne.cebula/okno-informacyjne.bur=okno_informacyjne.bin
# Kursor zmiany rozmiaru: opcjonalne ikonki/kursor_rozciagania.png (brak pliku nie psuje budowania).
KURSOR_ROZCIAGANIA_H := jadro/gui/kursor_rozciagania.generated.h
KURSOR_ROZCIAGANIA_PNG := ikonki/kursor.png ikonki/kursor_rozciagania.png ikonki/kursorrozciagania.png
.PHONY: FORCE
FORCE:
$(KURSOR_ROZCIAGANIA_H): narzedzia/generuj_kursor_rozciagania.py FORCE
	$(PYTHON) narzedzia/generuj_kursor_rozciagania.py $@ $(KURSOR_ROZCIAGANIA_PNG)
grafika.o: $(KURSOR_ROZCIAGANIA_H) jadro/gui/kursor_rozciagania.h

loader.o: $(SKROTY_SYSTEMOWE_H) jadro/fs/tozsamosc_systemowa.h jadro/fs/tozsamosc_typy.h

.PHONY: test-system-binary-protection-host
test-system-binary-protection-host:
	@mkdir -p .installertest
	g++ $(HOST_INCLUDES) -std=c++17 -O0 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -DBURSZTYN_PSF_HOST_TEST -fsanitize=address,undefined testy/test_system_binary_protection.cpp jadro/fs/storage_format.cpp jadro/fs/psf.cpp -o .installertest/test-system-binary-protection
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .installertest/test-system-binary-protection

.PHONY: test-launch-policy-host
test-launch-policy-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Ijadro/bws testy/test_uruchamianie_polityka.cpp -o .bsf-test/launchpolicy
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/launchpolicy

.PHONY: test-kernel-blobz-host
test-kernel-blobz-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer testy/test_blob_skompresowany.cpp -lz -o .bsf-test/blobz
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/blobz

# Centralny kursor zmiany rozmiaru: generator (Python) + wybor/zasob (C++, ASan/UBSan).
.PHONY: test-okna-podrzedne-host
test-okna-podrzedne-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_okna_podrzedne_host.cpp -o .bsf-test/okna_podrzedne_host
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/okna_podrzedne_host

.PHONY: test-zakonczenie-procesu-host
test-zakonczenie-procesu-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_zakonczenie_procesu_host.cpp -o .bsf-test/zakonczenie_procesu_host
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/zakonczenie_procesu_host

.PHONY: test-punkt-zwrotu-syscalla-host
test-punkt-zwrotu-syscalla-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_punkt_zwrotu_syscalla_host.cpp -o .bsf-test/punkt_zwrotu_syscalla_host
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/punkt_zwrotu_syscalla_host

.PHONY: test-zakonczenie-harness-host
test-zakonczenie-harness-host:
	$(PYTHON) testy/test_zakonczenie_klasyfikacja.py

.PHONY: test-tresc-uklad-host
test-tresc-uklad-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_tresc_uklad_host.cpp -o .bsf-test/tresc_uklad_host
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/tresc_uklad_host

.PHONY: test-vmm-synchronizacja-host
test-vmm-synchronizacja-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_vmm_synchronizacja_host.cpp -o .bsf-test/vmm_synchronizacja_host
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/vmm_synchronizacja_host

.PHONY: test-vmm-zakres-host
test-vmm-zakres-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_vmm_zakres_host.cpp -o .bsf-test/vmm_zakres_host
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/vmm_zakres_host

.PHONY: test-rozszerzenie-powierzchni-host
test-rozszerzenie-powierzchni-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_rozszerzenie_powierzchni_host.cpp -o .bsf-test/rozszerzenie_powierzchni_host
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/rozszerzenie_powierzchni_host

.PHONY: test-ikony-aplikacji
test-ikony-aplikacji:
	$(PYTHON) testy/test_ikony_aplikacji.py

.PHONY: test-resize-cursor-host
test-resize-cursor-host: $(KURSOR_ROZCIAGANIA_H)
	$(PYTHON) testy/test_kursor_rozciagania_generator.py
	mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -I. -Ijadro/gui testy/test_kursor_rozciagania_host.cpp -o .bsf-test/kursor_rozciagania_host
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/kursor_rozciagania_host

.PHONY: test-snake-host
test-snake-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_snake_host.cpp -o .bursztyn-ui-test/test-snake
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bursztyn-ui-test/test-snake

.PHONY: test-przegladarka-obrazow-host
test-przegladarka-obrazow-host:
	@mkdir -p .bursztyn-ui-test
	$(PYTHON) testy/generuj_obrazy_testowe.py .bsf-test/img-fixtures
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -Iprogramy/przegladarka-obrazow -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_przegladarka_obrazow_host.cpp obrazy/dekoder_obrazu.cpp obrazy/podglad_jpeg.cpp narzedzia/tapety-btp/tapetybtp.cpp -o .bursztyn-ui-test/test-przegladarka-obrazow
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bursztyn-ui-test/test-przegladarka-obrazow .bsf-test/img-fixtures

.PHONY: test-taskbar-stacking-host
test-taskbar-stacking-host:
	@mkdir -p .bursztyn-ui-test
	g++ -std=c++17 -O1 -g $(HOST_INCLUDES) -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_taskbar_stacking_host.cpp -o .bursztyn-ui-test/test-taskbar-stacking
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bursztyn-ui-test/test-taskbar-stacking

.PHONY: test-ustawienia-aplikacji-host
test-ustawienia-aplikacji-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_ustawienia_aplikacji_host.cpp -o .bsf-test/ustawienia-aplikacji
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/ustawienia-aplikacji

.PHONY: test-galeria-pak-host
test-galeria-pak-host:
	@mkdir -p .bsf-test
	$(PYTHON) -c "import sys; sys.path.insert(0,'narzedzia'); import package_galeria as g, pathlib; pathlib.Path('.bsf-test/zdjecia-test.pak').write_bytes(g.zbuduj(g.zbierz()))"
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_galeria_pak_host.cpp -o .bsf-test/galeria-pak
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/galeria-pak .bsf-test/zdjecia-test.pak
	$(PYTHON) narzedzia/test_release_metadata.py

.PHONY: test-galeria-kopiowanie-host
test-galeria-kopiowanie-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_galeria_kopiowanie_host.cpp -o .bsf-test/galeria-kopiowanie
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .bsf-test/galeria-kopiowanie

.PHONY: test-polityka-alokacji-host
test-polityka-alokacji-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_polityka_alokacji_host.cpp -o .bsf-test/polityka-alokacji
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/polityka-alokacji

.PHONY: test-ochrona-sciezek-host
test-ochrona-sciezek-host:
	@mkdir -p .bsf-test
	g++ -std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined testy/test_ochrona_sciezek_host.cpp -o .bsf-test/ochrona-sciezek
	UBSAN_OPTIONS=halt_on_error=1 .bsf-test/ochrona-sciezek
