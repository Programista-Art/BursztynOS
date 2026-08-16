/*
 * Bursztyn OS - glowny skrypt linkera jadra x86_64
 *
 * Ten plik jest scisle powiazany z boot.S.
 *
 * Uklad obrazu:
 *
 *   0x00100000  .boot
 *               naglowek Multiboot2
 *
 *   page-align  .text
 *               wykonywalny kod jadra
 *
 *   page-align  .rodata
 *               stale dane tylko do odczytu
 *
 *   page-align  .data
 *               dane zapisywalne oraz tablice C++ init/fini
 *
 *   page-align  .bss
 *               dane zerowane, bootstrap page tables i stos z boot.S
 *
 * Bootstrap boot.S tworzy identity mapping:
 *
 *   0x00000000 .. 0xFFFFFFFF
 *
 * czyli pelne pierwsze 4 GiB przestrzeni fizycznej/wirtualnej przy pomocy
 * 2048 stron huge-page po 2 MiB.
 *
 * Do chwili przejecia kontroli przez docelowy VMM:
 *
 *   - caly obraz jadra,
 *   - bootstrap PML4/PDP/PD,
 *   - stos startowy,
 *   - tymczasowa GDT,
 *   - informacje Multiboot2 uzywane bezposrednio
 *
 * musza byc dostepne w tym identity mapping.
 *
 * UWAGA:
 * boot.S mapuje tymczasowo takze strone NULL. Docelowy VMM usuwa to
 * mapowanie i ustanawia ostrzejsze prawa pamieci.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* =========================================================================
 * 1. STALE BOOTSTRAP ABI
 * ========================================================================= */

KERNEL_VMA_START = 0x00100000;

/*
 * boot.S mapuje adresy:
 *
 *   0x0000000000000000 .. 0x00000000FFFFFFFF
 *
 * Gorna granica jest zapisana jako adres pierwszego bajtu POZA mappingiem.
 */
BOOT_IDENTITY_LIMIT = 0x0000000100000000;

BOOT_PAGE_SIZE_4K = 0x1000;
BOOT_HUGE_PAGE_2M = 0x200000;

/*
 * Typowy GNU ld z tym ukladem umieszcza pierwszy PT_LOAD pod file offset
 * 0x1000. Multiboot2 wymaga calego naglowka w pierwszych 32768 bajtach.
 *
 * 0x1000 + 0x7000 == 0x8000.
 */
MULTIBOOT_FILE_WINDOW = 0x8000;
MULTIBOOT_BOOT_MAX_SIZE = 0x7000;

/* =========================================================================
 * 2. PROGRAM HEADERS ELF
 * ========================================================================= */

/*
 * Jawne prawa segmentow:
 *
 *   boot   R--   sam naglowek Multiboot2, bez kodu
 *   text   R-X   kod
 *   rodata R--   stale
 *   data   RW-   dane i BSS
 *
 * W^X:
 * Nie tworzymy zadnego segmentu jednoczesnie zapisywalnego i wykonywalnego.
 */
PHDRS
{
    boot   PT_LOAD FLAGS(4); /* R-- */
    text   PT_LOAD FLAGS(5); /* R-X */
    rodata PT_LOAD FLAGS(4); /* R-- */
    data   PT_LOAD FLAGS(6); /* RW- */
}

/* =========================================================================
 * 3. SEKCJE
 * ========================================================================= */

SECTIONS
{
    /*
     * GRUB laduje jadro od 1 MiB.
     *
     * W bootstrapie VMA == LMA, poniewaz boot.S uruchamia sie jeszcze
     * z identity mappingiem.
     */
    . = KERNEL_VMA_START;

    __kernel_start = .;
    __kernel_phys_start = .;

    /* =====================================================================
     * MULTIBOOT2 HEADER
     * ================================================================== */

    .boot ALIGN(BOOT_PAGE_SIZE_4K) :
    {
        __boot_start = .;
        __multiboot_start = .;

        /*
         * KEEP zabezpiecza naglowek przed --gc-sections.
         *
         * W .boot nie powinien znalezc sie kod wykonywalny.
         */
        KEEP(*(.multiboot))
        KEEP(*(.multiboot.*))

        __multiboot_end = .;
        __boot_end = .;
    } :boot

    /* =====================================================================
     * KOD WYKONYWALNY
     * ================================================================== */

    .text ALIGN(BOOT_PAGE_SIZE_4K) :
    {
        __text_start = .;

        /*
         * _start z boot.S powinien naturalnie trafic bardzo blisko poczatku
         * .text, ale nie opieramy ABI na kolejnosci plikow obiektowych.
         */
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)

        /*
         * Freestandingowy toolchain/biblioteki moga utworzyc .init/.fini.
         */
        *(.init)
        *(.fini)

        . = ALIGN(16);

        __text_end = .;
    } :text

    /* =====================================================================
     * DANE TYLKO DO ODCZYTU
     * ================================================================== */

    .rodata ALIGN(BOOT_PAGE_SIZE_4K) :
    {
        __rodata_start = .;

        *(.rodata)
        *(.rodata.*)

        *(.srodata)
        *(.srodata.*)

        *(.gnu.linkonce.r.*)

        . = ALIGN(16);

        __rodata_end = .;
    } :rodata

    /* =====================================================================
     * DANE ZAPISYWALNE
     * ================================================================== */

    .data ALIGN(BOOT_PAGE_SIZE_4K) :
    {
        __data_start = .;

        /*
         * C++ PREINIT ARRAY
         */
        . = ALIGN(16);

        __preinit_array_start = .;

        KEEP(*(.preinit_array))
        KEEP(*(.preinit_array.*))

        __preinit_array_end = .;

        /*
         * C++ INIT ARRAY / stare .ctors
         *
         * Linker jedynie zachowuje te wpisy.
         * Jezeli zakres nie jest pusty, kod startowy kernela MUSI wykonac
         * konstruktory przed uzyciem obiektow wymagajacych inicjalizacji.
         */
        . = ALIGN(16);

        __init_array_start = .;

        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))

        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))

        __init_array_end = .;

        /*
         * C++ FINI ARRAY / stare .dtors.
         *
         * Kernel zwykle nie wykonuje destruktorow przy normalnym shutdown,
         * ale zakres pozostaje jawny i nie jest gubiony.
         */
        . = ALIGN(16);

        __fini_array_start = .;

        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))

        KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.dtors))

        __fini_array_end = .;

        /*
         * Standardowe dane RW.
         */
        . = ALIGN(16);

        *(.data)
        *(.data.*)

        *(.sdata)
        *(.sdata.*)

        /*
         * Bez dynamicznego RELRO te sekcje nadal wymagaja miejsca RW.
         */
        *(.data.rel.ro)
        *(.data.rel.ro.*)

        *(.gnu.linkonce.d.*)
        *(.gnu.linkonce.s.*)

        /*
         * Przy -fno-pie/-no-pie GOT powinien byc niewielki albo pusty,
         * ale statyczne odwolania wygenerowane przez toolchain sa dozwolone.
         */
        *(.got)
        *(.got.*)
        *(.got.plt)

        . = ALIGN(16);

        __data_end = .;
    } :data

    /* =====================================================================
     * BSS
     * ================================================================== */

    .bss ALIGN(BOOT_PAGE_SIZE_4K) (NOLOAD) :
    {
        __bss_start = .;

        *(.bss)
        *(.bss.*)

        *(.sbss)
        *(.sbss.*)

        *(.gnu.linkonce.b.*)
        *(.gnu.linkonce.sb.*)

        /*
         * COMMON dla zgodnosci ze starszymi obiektami.
         */
        *(COMMON)

        . = ALIGN(16);

        __bss_end = .;
    } :data

    /* =====================================================================
     * TLS - JESZCZE NIE JEST CZESCIA ABI KERNELA
     * ================================================================== */

    /*
     * Nie wyrzucamy TLS po cichu.
     *
     * Jezeli kompilator zacznie emitowac .tdata/.tbss, linker ma przerwac
     * build zamiast stworzyc kernel, ktory pozniej odczyta niezainicjalizowany
     * TLS.
     */
    .kernel_tls_guard ALIGN(16) (NOLOAD) :
    {
        __kernel_tls_start = .;

        *(.tdata)
        *(.tdata.*)

        *(.tbss)
        *(.tbss.*)

        __kernel_tls_end = .;
    } :data

    /* =====================================================================
     * DYNAMIC LINKING - ZABRONIONY
     * ================================================================== */

    /*
     * Bursztyn kernel jest statycznym freestanding ELF bez dynamic loadera.
     * Te sekcje sa zbierane wyłącznie po to, aby ASSERT mogl wykryc blad.
     */
    .kernel_dynamic_guard ALIGN(16) (NOLOAD) :
    {
        __kernel_dynamic_start = .;

        *(.interp)
        *(.dynamic)

        *(.dynsym)
        *(.dynstr)

        *(.hash)
        *(.gnu.hash)

        *(.plt)
        *(.plt.*)
        *(.iplt)

        *(.rela.dyn)
        *(.rela.plt)
        *(.rela.iplt)

        *(.rel.dyn)
        *(.rel.plt)
        *(.rel.iplt)

        __kernel_dynamic_end = .;
    } :data

    /* =====================================================================
     * KONIEC OBRAZU JADRA
     * ================================================================== */

    . = ALIGN(BOOT_PAGE_SIZE_4K);

    __kernel_end = .;
    __kernel_phys_end = .;

    __kernel_size =
        __kernel_end -
        __kernel_start;

    /*
     * Przydatne dla diagnostyki bootstrapowego identity mappingu.
     */
    __boot_identity_limit =
        BOOT_IDENTITY_LIMIT;

    __boot_identity_size =
        BOOT_IDENTITY_LIMIT;

    /* =====================================================================
     * SEKCJE NIEPOTRZEBNE W FREESTANDING KERNELU
     * ================================================================== */

    /DISCARD/ :
    {
        /*
         * Metadane kompilatora.
         */
        *(.comment)

        /*
         * GNU-stack note jest informacja dla hostowego linkera.
         * Makefile dodatkowo uzywa -z noexecstack.
         */
        *(.note.GNU-stack)

        /*
         * Inne notatki ELF nie sa potrzebne w runtime kernela.
         */
        *(.note)
        *(.note.*)

        /*
         * Kernel jest budowany z -fno-exceptions i nie posiada unwindera.
         */
        *(.eh_frame)
        *(.eh_frame.*)
        *(.eh_frame_hdr)

        *(.gcc_except_table)
        *(.gcc_except_table.*)

        /*
         * Informacje debug nie powinny trafic do finalnego runtime image.
         * Symbole ELF moga nadal pozostac w pliku kernel.elf zależnie od
         * flags linkera; te sekcje nie sa ladowane przez kernel.
         */
        *(.debug)
        *(.debug_*)
        *(.line)
        *(.stab)
        *(.stabstr)
        *(.gnu_debuglink)
        *(.gnu_debugdata)

        *(.gnu.lto_*)
    }

    /* =====================================================================
     * 4. KONTROLE LINK-TIME
     * ================================================================== */

    ASSERT(
        DEFINED(_start),
        "Bursztyn OS: brak symbolu _start"
    )

    ASSERT(
        SIZEOF(.boot) > 0,
        "Bursztyn OS: brak naglowka Multiboot2"
    )

    ASSERT(
        __kernel_start == KERNEL_VMA_START,
        "Bursztyn OS: jadro musi zaczynac sie pod 1 MiB"
    )

    ASSERT(
        __multiboot_start == KERNEL_VMA_START,
        "Bursztyn OS: naglowek Multiboot2 musi byc pierwsza sekcja obrazu"
    )

    /*
     * Przy tym PHDRS GNU ld umieszcza pierwszy PT_LOAD pod offsetem 0x1000.
     * Rozmiar <= 0x7000 gwarantuje, ze .boot konczy sie najpozniej na
     * file offset 0x8000, czyli w wymaganych pierwszych 32 KiB.
     *
     * Finalny build powinien nadal byc sprawdzany grub-file lub readelf,
     * bo file offset jest cecha finalnego ELF, nie samego VMA.
     */
    ASSERT(
        SIZEOF(.boot) <= MULTIBOOT_BOOT_MAX_SIZE,
        "Bursztyn OS: .boot przekracza bezpieczne okno pierwszych 32 KiB ELF"
    )

    ASSERT(
        (_start >= __text_start) &&
        (_start < __text_end),
        "Bursztyn OS: _start musi znajdowac sie w wykonywalnym .text"
    )

    /*
     * boot.S korzysta z 32-bitowych adresow podczas fazy pre-long-mode.
     * Caly obraz kernela musi wiec miescic sie w bootstrapowym mappingu
     * 0..4 GiB.
     */
    ASSERT(
        __kernel_end <= BOOT_IDENTITY_LIMIT,
        "Bursztyn OS: obraz jadra przekracza bootstrap identity mapping 4 GiB z boot.S"
    )

    ASSERT(
        _start < BOOT_IDENTITY_LIMIT,
        "Bursztyn OS: _start znajduje sie poza bootstrap identity mapping 4 GiB"
    )

    /*
     * Wymagania struktur stron z boot.S.
     */
    ASSERT(
        DEFINED(pml4_table),
        "Bursztyn OS: boot.S nie eksportuje pml4_table"
    )

    ASSERT(
        DEFINED(pdp_table),
        "Bursztyn OS: boot.S nie eksportuje pdp_table"
    )

    ASSERT(
        DEFINED(pd_table),
        "Bursztyn OS: boot.S nie eksportuje pd_table"
    )

    ASSERT(
        (pml4_table & (BOOT_PAGE_SIZE_4K - 1)) == 0,
        "Bursztyn OS: pml4_table musi byc wyrownane do 4 KiB"
    )

    ASSERT(
        (pdp_table & (BOOT_PAGE_SIZE_4K - 1)) == 0,
        "Bursztyn OS: pdp_table musi byc wyrownane do 4 KiB"
    )

    ASSERT(
        (pd_table & (BOOT_PAGE_SIZE_4K - 1)) == 0,
        "Bursztyn OS: pd_table musi byc wyrownane do 4 KiB"
    )

    ASSERT(
        pml4_table < BOOT_IDENTITY_LIMIT,
        "Bursztyn OS: pml4_table poza bootstrap identity mapping 4 GiB"
    )

    ASSERT(
        pdp_table < BOOT_IDENTITY_LIMIT,
        "Bursztyn OS: pdp_table poza bootstrap identity mapping 4 GiB"
    )

    ASSERT(
        pd_table < BOOT_IDENTITY_LIMIT,
        "Bursztyn OS: pd_table poza bootstrap identity mapping 4 GiB"
    )

    ASSERT(
        DEFINED(stack_top),
        "Bursztyn OS: boot.S nie eksportuje stack_top"
    )

    ASSERT(
        (stack_top & 0xF) == 0,
        "Bursztyn OS: stack_top musi byc wyrownany do 16 bajtow"
    )

    ASSERT(
        stack_top <= BOOT_IDENTITY_LIMIT,
        "Bursztyn OS: stos startowy wychodzi poza bootstrap identity mapping 4 GiB"
    )

    /*
     * Obecny kernel nie ma runtime TLS.
     */
    ASSERT(
        SIZEOF(.kernel_tls_guard) == 0,
        "Bursztyn OS: wykryto TLS (.tdata/.tbss), ale kernel nie inicjalizuje jeszcze TLS"
    )

    /*
     * Dynamiczny ELF nie moze byc uruchomiony bez dynamic loadera.
     */
    ASSERT(
        SIZEOF(.kernel_dynamic_guard) == 0,
        "Bursztyn OS: wykryto dynamic linking/PLT; kernel musi pozostac statyczny"
    )

    /*
     * Nie dopuszczamy pustych podstawowych segmentow.
     */
    ASSERT(
        SIZEOF(.text) > 0,
        "Bursztyn OS: pusty segment .text"
    )

    ASSERT(
        SIZEOF(.rodata) >= 0,
        "Bursztyn OS: nieprawidlowy segment .rodata"
    )

    ASSERT(
        __kernel_phys_start == __kernel_start,
        "Bursztyn OS: bootstrap wymaga VMA == LMA"
    )

    ASSERT(
        __kernel_phys_end == __kernel_end,
        "Bursztyn OS: bootstrap wymaga VMA == LMA do konca obrazu"
    )
}
