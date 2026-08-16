/*
 * Bursztyn OS - linker Powłoki Bursztynowej (shell.bur)
 *
 * Skrypt jest częścią ABI pliku .bur i musi być zgodny z NaglowekBur
 * zapisanym w shell.cpp:
 *
 *   nagłówek:  offset 0x0000, obszar 0x1000
 *   TEXT:      offset 0x1000, rozmiar 0x8000,  VA 0x601000
 *   DATA/BSS:  offset 0x9000, rozmiar 0x20000, VA 0x609000
 *
 * Loader Bursztyn OS zeruje cały segment danych przed kopiowaniem jego
 * zainicjalizowanej części, dlatego .bss jest NOLOAD i nie zwiększa .bur.
 *
 * Zmiana tych wartości wymaga jednoczesnej zmiany NaglowekBur w shell.cpp.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* =========================================================================
 * 1. STAŁE ABI
 * ========================================================================= */

BUR_HEADER_OFFSET = 0x0000;
BUR_HEADER_SIZE   = 0x1000;
BUR_HEADER_VADDR  = 0x600000;

BUR_TEXT_OFFSET   = 0x1000;
BUR_TEXT_SIZE     = 0x8000;
BUR_TEXT_VADDR    = 0x601000;

BUR_DATA_OFFSET   = 0x9000;
BUR_DATA_SIZE     = 0x20000;
BUR_DATA_VADDR    = 0x609000;

BUR_TEXT_END      = BUR_TEXT_VADDR + BUR_TEXT_SIZE;
BUR_DATA_END      = BUR_DATA_VADDR + BUR_DATA_SIZE;

/* =========================================================================
 * 2. PROGRAM HEADERS ELF
 * ========================================================================= */

/*
 * shell.elf jest tylko etapem pośrednim przed objcopy -O binary, ale warto
 * zachować poprawne prawa segmentów również w ELF:
 *
 *   nagłówek  R--
 *   tekst     R-X
 *   dane      RW-
 */
PHDRS
{
    bur_header PT_LOAD FLAGS(4);
    bur_text   PT_LOAD FLAGS(5);
    bur_data   PT_LOAD FLAGS(6);
}

/* =========================================================================
 * 3. SEKCJE
 * ========================================================================= */

SECTIONS
{
    /* ---------------------------------------------------------------------
     * NAGŁÓWEK .BUR
     * ------------------------------------------------------------------ */

    .naglowek BUR_HEADER_VADDR : AT(BUR_HEADER_OFFSET)
    {
        __bur_header_start = .;

        KEEP(*(.naglowek))
        KEEP(*(.naglowek.*))

        __bur_header_payload_end = .;

        /*
         * Dopełnienie do 4096 B gwarantuje, że objcopy umieści TEXT
         * dokładnie pod offsetem 0x1000.
         */
        . = BUR_HEADER_SIZE;

        __bur_header_end = .;
    } :bur_header = 0x00

    /* ---------------------------------------------------------------------
     * TEXT + RODATA
     * ------------------------------------------------------------------ */

    .text BUR_TEXT_VADDR : AT(BUR_TEXT_OFFSET)
    {
        __bur_text_start = .;

        KEEP(*(.text._start))
        KEEP(*(.text._start.*))

        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)

        . = ALIGN(16);

        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        *(.gnu.linkonce.r.*)

        . = ALIGN(16);

        /*
         * Koniec rzeczywiście używanego kodu/rodata zapisujemy PRZED
         * dopełnieniem, aby ASSERT mógł wykryć przekroczenie 32 KiB.
         */
        __bur_text_used_end = .;

        /*
         * Loader wymaga pełnych tekst_rozmiar == 0x8000 bajtów w pliku.
         */
        . = BUR_TEXT_SIZE;

        __bur_text_end = .;
    } :bur_text = 0x00

    /* ---------------------------------------------------------------------
     * DATA - część obecna fizycznie w shell.bin
     * ------------------------------------------------------------------ */

    .data BUR_DATA_VADDR : AT(BUR_DATA_OFFSET)
    {
        __bur_data_start = .;

        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.d.*)
        *(.gnu.linkonce.s.*)

        /*
         * Dla obecnego nie-PIE Ring 3 GOT zwykle jest pusty, ale jeżeli
         * toolchain utworzy statyczne wpisy GOT, należą do writable DATA.
         */
        *(.got)
        *(.got.*)

        . = ALIGN(16);

        __bur_data_file_end = .;
    } :bur_data

    /* ---------------------------------------------------------------------
     * BSS / COMMON - tylko pamięć, bez bajtów w pliku
     * ------------------------------------------------------------------ */

    .bss (NOLOAD) :
    {
        . = ALIGN(16);

        __bur_bss_start = .;

        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(.gnu.linkonce.b.*)
        *(.gnu.linkonce.sb.*)
        *(COMMON)

        . = ALIGN(16);

        __bur_bss_end = .;
        __bur_data_memory_end = .;
    } :bur_data

    /* ---------------------------------------------------------------------
     * KONSTRUKTORY C++ - JESZCZE NIEOBSŁUGIWANE
     * ------------------------------------------------------------------ */

    .bur_konstruktory (NOLOAD) :
    {
        __bur_ctor_start = .;

        KEEP(*(.preinit_array))
        KEEP(*(.preinit_array.*))
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        KEEP(*(.fini_array))
        KEEP(*(.fini_array.*))
        KEEP(*(.ctors))
        KEEP(*(.ctors.*))
        KEEP(*(.dtors))
        KEEP(*(.dtors.*))

        __bur_ctor_end = .;
    }

    /* ---------------------------------------------------------------------
     * TLS - JESZCZE NIEOBSŁUGIWANY
     * ------------------------------------------------------------------ */

    .bur_tls (NOLOAD) :
    {
        __bur_tls_start = .;

        *(.tdata)
        *(.tdata.*)
        *(.tbss)
        *(.tbss.*)

        __bur_tls_end = .;
    }

    /* ---------------------------------------------------------------------
     * DYNAMIC LINKING / PLT - NIEDOZWOLONE W .bur
     * ------------------------------------------------------------------ */

    .bur_dynamiczne (NOLOAD) :
    {
        __bur_dynamic_start = .;

        *(.dynamic)
        *(.dynsym)
        *(.dynstr)
        *(.hash)
        *(.gnu.hash)
        *(.plt)
        *(.plt.*)
        *(.interp)

        __bur_dynamic_end = .;
    }

    /* ---------------------------------------------------------------------
     * METADANE NIEPOTRZEBNE RUNTIME
     * ------------------------------------------------------------------ */

    /DISCARD/ :
    {
        /*
         * Bursztyn Ring 3 nie posiada obecnie wyjątków C++ ani unwindera.
         */
        *(.eh_frame)
        *(.eh_frame.*)
        *(.eh_frame_hdr)
        *(.gcc_except_table)
        *(.gcc_except_table.*)

        *(.comment)

        /*
         * Usuwamy notatki ELF; .note.GNU-stack nie należy do obrazu .bur.
         */
        *(.note)
        *(.note.*)

        /*
         * Sekcje debug/toolchain.
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

    /* =========================================================================
     * 4. ASSERTY ABI
     *
     * GNU ld wymaga tutaj ASSERT(...) bez końcowego średnika.
     * ========================================================================= */

    ASSERT(
        DEFINED(_start),
        "BURSZTYN BUR: brak symbolu _start"
    )

    ASSERT(
        DEFINED(naglowek),
        "BURSZTYN BUR: brak symbolu naglowek"
    )

    ASSERT(
        __bur_header_payload_end <= BUR_HEADER_VADDR + BUR_HEADER_SIZE,
        "BURSZTYN BUR: NaglowekBur przekroczyl 4096 bajtow"
    )

    ASSERT(
        SIZEOF(.naglowek) == BUR_HEADER_SIZE,
        "BURSZTYN BUR: obszar naglowka musi miec dokladnie 0x1000 bajtow"
    )

    ASSERT(
        ADDR(.naglowek) == BUR_HEADER_VADDR,
        "BURSZTYN BUR: nieprawidlowy VA naglowka"
    )

    ASSERT(
        LOADADDR(.naglowek) == BUR_HEADER_OFFSET,
        "BURSZTYN BUR: nieprawidlowy offset pliku naglowka"
    )

    ASSERT(
        naglowek >= BUR_HEADER_VADDR &&
        naglowek < BUR_HEADER_VADDR + BUR_HEADER_SIZE,
        "BURSZTYN BUR: symbol naglowek musi lezec w pierwszych 4096 bajtach"
    )

    ASSERT(
        ADDR(.text) == BUR_TEXT_VADDR,
        "BURSZTYN BUR: nieprawidlowy VA TEXT"
    )

    ASSERT(
        LOADADDR(.text) == BUR_TEXT_OFFSET,
        "BURSZTYN BUR: nieprawidlowy offset pliku TEXT"
    )

    ASSERT(
        __bur_text_used_end <= BUR_TEXT_END,
        "BURSZTYN BUR: shell przekroczyl 32 KiB segmentu TEXT"
    )

    ASSERT(
        SIZEOF(.text) == BUR_TEXT_SIZE,
        "BURSZTYN BUR: TEXT musi miec dokladnie 0x8000 bajtow po dopelnieniu"
    )

    ASSERT(
        _start >= BUR_TEXT_VADDR &&
        _start < BUR_TEXT_END,
        "BURSZTYN BUR: _start musi znajdowac sie w TEXT"
    )

    ASSERT(
        ADDR(.data) == BUR_DATA_VADDR,
        "BURSZTYN BUR: nieprawidlowy VA DATA"
    )

    ASSERT(
        LOADADDR(.data) == BUR_DATA_OFFSET,
        "BURSZTYN BUR: nieprawidlowy offset pliku DATA"
    )

    ASSERT(
        __bur_data_file_end <= BUR_DATA_END,
        "BURSZTYN BUR: zainicjalizowane DATA przekroczylo 128 KiB"
    )

    ASSERT(
        __bur_data_memory_end <= BUR_DATA_END,
        "BURSZTYN BUR: DATA+BSS przekroczyly 128 KiB"
    )

    ASSERT(
        __bur_ctor_start == __bur_ctor_end,
        "BURSZTYN BUR: wykryto konstruktory/destruktory C++, ale runtime .bur ich jeszcze nie uruchamia"
    )

    ASSERT(
        __bur_tls_start == __bur_tls_end,
        "BURSZTYN BUR: wykryto TLS (.tdata/.tbss), ale runtime Ring 3 go jeszcze nie obsluguje"
    )

    ASSERT(
        __bur_dynamic_start == __bur_dynamic_end,
        "BURSZTYN BUR: wykryto dynamic linking/PLT - .bur musi byc statyczny"
    )

    ASSERT(
        BUR_TEXT_OFFSET + BUR_TEXT_SIZE == BUR_DATA_OFFSET,
        "BURSZTYN BUR: TEXT i DATA nie sa sasiednimi regionami pliku"
    )

    ASSERT(
        BUR_TEXT_VADDR + BUR_TEXT_SIZE == BUR_DATA_VADDR,
        "BURSZTYN BUR: TEXT i DATA nie sa sasiednimi regionami VA"
    )
}

/* =========================================================================
 * 5. SYMBOLE POMOCNICZE
 * ========================================================================= */

/*
 * Umożliwią później wygenerowanie NaglowekBur ze symboli linkera zamiast
 * ręcznego duplikowania stałych w shell.cpp.
 */
PROVIDE(__bur_text_file_offset = BUR_TEXT_OFFSET);
PROVIDE(__bur_text_segment_size = BUR_TEXT_SIZE);
PROVIDE(__bur_text_virtual = BUR_TEXT_VADDR);

PROVIDE(__bur_data_file_offset = BUR_DATA_OFFSET);
PROVIDE(__bur_data_segment_size = BUR_DATA_SIZE);
PROVIDE(__bur_data_virtual = BUR_DATA_VADDR);
