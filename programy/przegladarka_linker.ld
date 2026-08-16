/*
 * Bursztyn OS - Hussar / przegladarka_linker.ld
 *
 * Skrypt linkera aplikacji Ring 3 `przegladarka.cpp`.
 * Jest to czesc ABI formatu .bur i MUSI pozostac zgodny z NaglowekBur
 * zapisanym w przegladarka.cpp.
 *
 * Uklad surowego pliku .bur po `objcopy -O binary`:
 *
 *   plik 0x00000 .. 0x00FFF   HEADER  -> VA 0x600000
 *   plik 0x01000 .. 0x0FFFF   TEXT    -> VA 0x601000, 0xF000 bajtow
 *   plik 0x10000 ..           DATA    -> VA 0x610000
 *
 * Loader rezerwuje dla DATA+BSS lacznie 0x80000 bajtow:
 *
 *   VA 0x610000 .. 0x68FFFF
 *
 * BSS jest NOLOAD: nie powieksza pliku .bur. Loader kopiuje dostepna czesc
 * DATA z pliku i zeruje pozostala pamiec segmentu do rozmiaru 0x80000.
 *
 * Dlaczego DATA jest tak duze:
 * obecne ABI BWS29/BWS30 przenosi adres bufora HTTP tylko w 32 bitach.
 * Hussar trzyma dlatego niski 256 KiB bounce-buffer w .bss pod 0x610000+.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* =========================================================================
 * 1. ABI .bur HUSSARA
 * ========================================================================= */

BUR_HEADER_OFFSET = 0x00000;
BUR_HEADER_SIZE   = 0x01000;
BUR_HEADER_VADDR  = 0x600000;

BUR_TEXT_OFFSET   = 0x01000;
BUR_TEXT_SIZE     = 0x0F000;
BUR_TEXT_VADDR    = 0x601000;

BUR_DATA_OFFSET   = 0x10000;
BUR_DATA_SIZE     = 0x80000;
BUR_DATA_VADDR    = 0x610000;

BUR_TEXT_END      = BUR_TEXT_VADDR + BUR_TEXT_SIZE;
BUR_DATA_END      = BUR_DATA_VADDR + BUR_DATA_SIZE;

/* =========================================================================
 * 2. PROGRAM HEADERS ELF
 * ========================================================================= */

/*
 * Intermediate ELF ma prawa zgodne z przeznaczeniem danych:
 *
 *   header R--
 *   text   R-X
 *   data   RW-
 *
 * Finalnym artefaktem ladowanym przez Bursztyn jest surowy .bur, ale
 * poprawne prawa PT_LOAD ulatwiaja diagnostyke readelf/objdump i unikaja RWX.
 */
PHDRS
{
    header PT_LOAD FLAGS(4); /* R-- */
    text   PT_LOAD FLAGS(5); /* R-X */
    data   PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
    /* =====================================================================
     * 3. NAGLOWEK .bur
     * ================================================================== */

    .naglowek BUR_HEADER_VADDR : AT(BUR_HEADER_OFFSET)
    {
        __bur_header_start = .;
        __bur_header_payload_start = .;

        KEEP(*(.naglowek))
        KEEP(*(.naglowek.*))

        __bur_header_payload_end = .;

        /*
         * Loader oczekuje TEXT fizycznie od file offset 0x1000.
         * Wewnatrz output section kropka jest offsetem wzgledem poczatku
         * sekcji, dlatego ustawiamy `. = BUR_HEADER_SIZE`, nie VMA+SIZE.
         */
        . = BUR_HEADER_SIZE;

        __bur_header_end = .;
    } :header = 0

    /* =====================================================================
     * 4. TEXT + RODATA
     * ================================================================== */

    .text BUR_TEXT_VADDR : AT(BUR_TEXT_OFFSET)
    {
        __bur_text_start = .;

        /* Punkt wejscia powinien pozostac nawet przy --gc-sections. */
        KEEP(*(.text._start))

        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)

        /* Stale sa czescia segmentu TEXT/R-X w obecnym ABI .bur. */
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        *(.lrodata)
        *(.lrodata.*)
        *(.gnu.linkonce.r.*)

        *(.init)
        *(.fini)

        . = ALIGN(16);
        __bur_text_used_end = .;

        /*
         * Naglowek deklaruje tekst_rozmiar = 0xF000. Loader kopiuje caly
         * zadeklarowany TEXT, wiec region musi fizycznie istniec w raw .bur.
         */
        . = BUR_TEXT_SIZE;

        __bur_text_end = .;
    } :text = 0

    /* =====================================================================
     * 5. DATA - tylko dane fizycznie obecne w pliku
     * ================================================================== */

    .data BUR_DATA_VADDR : AT(BUR_DATA_OFFSET)
    {
        __bur_data_start = .;

        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        *(.ldata)
        *(.ldata.*)

        *(.data.rel.ro)
        *(.data.rel.ro.*)

        *(.gnu.linkonce.d.*)
        *(.gnu.linkonce.s.*)

        /*
         * Przy -fno-pie/-no-pie GOT zwykle jest pusty lub maly. Jest to
         * statyczna tabela adresow, nie oznacza dynamicznego loadera.
         */
        *(.got)
        *(.got.*)
        *(.got.plt)

        . = ALIGN(16);
        __bur_data_file_end = .;
    } :data

    /* =====================================================================
     * 6. BSS - pamiec zerowana, bez powiekszania .bur
     * ================================================================== */

    .bss (NOLOAD) :
    {
        __bur_bss_start = .;

        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(.lbss)
        *(.lbss.*)

        *(.gnu.linkonce.b.*)
        *(.gnu.linkonce.sb.*)
        *(.gnu.linkonce.lb.*)

        *(COMMON)

        . = ALIGN(16);
        __bur_bss_end = .;
        __bur_data_memory_end = .;
    } :data

    /* =====================================================================
     * 7. KONSTRUKTORY C++ - NIEDOZWOLONE W OBECNYM RUNTIME APLIKACJI
     * ================================================================== */

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
    } :data

    /* =====================================================================
     * 8. TLS - NIEDOZWOLONY DOPÓKI LOADER RING 3 GO NIE INICJALIZUJE
     * ================================================================== */

    .bur_tls (NOLOAD) :
    {
        __bur_tls_start = .;

        *(.tdata)
        *(.tdata.*)
        *(.tbss)
        *(.tbss.*)

        __bur_tls_end = .;
    } :data

    /* =====================================================================
     * 9. DYNAMICZNY ELF - NIEDOZWOLONY
     * ================================================================== */

    .bur_dynamiczne (NOLOAD) :
    {
        __bur_dynamic_start = .;

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

        __bur_dynamic_end = .;
    } :data

    /* =====================================================================
     * 10. SEKCJE NIEPOTRZEBNE W APLIKACJI FREESTANDING
     * ================================================================== */

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)

        /* Hussar jest budowany z -fno-exceptions i bez unwindera. */
        *(.eh_frame)
        *(.eh_frame.*)
        *(.eh_frame_hdr)
        *(.gcc_except_table)
        *(.gcc_except_table.*)

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
     * 11. SYMBOLE ABI DLA NARZEDZI / PRZYSZLEGO GENEROWANIA NAGLOWKA
     * ================================================================== */

    PROVIDE(__bur_header_file_offset = BUR_HEADER_OFFSET);
    PROVIDE(__bur_header_region_size = BUR_HEADER_SIZE);
    PROVIDE(__bur_header_virtual     = BUR_HEADER_VADDR);

    PROVIDE(__bur_text_file_offset   = BUR_TEXT_OFFSET);
    PROVIDE(__bur_text_segment_size  = BUR_TEXT_SIZE);
    PROVIDE(__bur_text_virtual       = BUR_TEXT_VADDR);

    PROVIDE(__bur_data_file_offset   = BUR_DATA_OFFSET);
    PROVIDE(__bur_data_segment_size  = BUR_DATA_SIZE);
    PROVIDE(__bur_data_virtual       = BUR_DATA_VADDR);

    PROVIDE(__bur_entry              = _start);

    /* =====================================================================
     * 12. KONTROLE LINK-TIME
     * ================================================================== */

    ASSERT(
        DEFINED(_start),
        "Hussar: brak symbolu _start"
    )

    ASSERT(
        DEFINED(naglowek),
        "Hussar: brak symbolu naglowek .bur"
    )

    ASSERT(
        (__bur_header_payload_end - __bur_header_payload_start) == 60,
        "Hussar: NaglowekBur musi miec dokladnie 60 bajtow"
    )

    ASSERT(
        ADDR(.naglowek) == BUR_HEADER_VADDR,
        "Hussar: nieprawidlowy VMA naglowka .bur"
    )

    ASSERT(
        LOADADDR(.naglowek) == BUR_HEADER_OFFSET,
        "Hussar: nieprawidlowy file/LMA offset naglowka .bur"
    )

    ASSERT(
        SIZEOF(.naglowek) == BUR_HEADER_SIZE,
        "Hussar: region naglowka musi miec 0x1000 bajtow"
    )

    ASSERT(
        naglowek == BUR_HEADER_VADDR,
        "Hussar: symbol naglowek musi zaczynac sie pod 0x600000"
    )

    ASSERT(
        ADDR(.text) == BUR_TEXT_VADDR,
        "Hussar: TEXT musi zaczynac sie pod 0x601000"
    )

    ASSERT(
        LOADADDR(.text) == BUR_TEXT_OFFSET,
        "Hussar: TEXT musi zaczynac sie w pliku pod 0x1000"
    )

    ASSERT(
        __bur_text_used_end <= BUR_TEXT_END,
        "Hussar: kod/rodata przekracza region TEXT 0xF000"
    )

    ASSERT(
        SIZEOF(.text) == BUR_TEXT_SIZE,
        "Hussar: TEXT musi byc fizycznie wypelniony do 0xF000 bajtow"
    )

    ASSERT(
        (_start >= BUR_TEXT_VADDR) && (_start < BUR_TEXT_END),
        "Hussar: _start znajduje sie poza segmentem TEXT"
    )

    ASSERT(
        ADDR(.data) == BUR_DATA_VADDR,
        "Hussar: DATA musi zaczynac sie pod 0x610000"
    )

    ASSERT(
        LOADADDR(.data) == BUR_DATA_OFFSET,
        "Hussar: DATA musi zaczynac sie w pliku pod 0x10000"
    )

    ASSERT(
        (__bur_data_file_end - BUR_DATA_VADDR) <= BUR_DATA_SIZE,
        "Hussar: inicjalizowane DATA przekracza 0x80000 bajtow"
    )

    ASSERT(
        (__bur_data_memory_end - BUR_DATA_VADDR) <= BUR_DATA_SIZE,
        "Hussar: DATA+BSS przekracza 0x80000 bajtow"
    )

    /*
     * Caly DATA/BSS konczy sie pod 0x690000, a wiec daleko ponizej 4 GiB.
     * To zachowuje wymaganie obecnego 32-bitowego ABI bufora BWS29/BWS30.
     */
    ASSERT(
        __bur_data_memory_end <= BUR_DATA_END,
        "Hussar: niski DATA/BSS wychodzi poza VA 0x610000..0x68FFFF"
    )

    ASSERT(
        SIZEOF(.bur_konstruktory) == 0,
        "Hussar: wykryto konstruktory/destruktory C++, ale runtime .bur ich nie uruchamia"
    )

    ASSERT(
        SIZEOF(.bur_tls) == 0,
        "Hussar: wykryto TLS (.tdata/.tbss), ale loader .bur nie inicjalizuje TLS"
    )

    ASSERT(
        SIZEOF(.bur_dynamiczne) == 0,
        "Hussar: wykryto dynamiczny ELF/PLT; aplikacja .bur musi byc statyczna"
    )

    /* Surowy plik ma layout bez dziur miedzy zadeklarowanymi regionami. */
    ASSERT(
        BUR_HEADER_OFFSET + BUR_HEADER_SIZE == BUR_TEXT_OFFSET,
        "Hussar: HEADER i TEXT nie sa ciagle w pliku .bur"
    )

    ASSERT(
        BUR_TEXT_OFFSET + BUR_TEXT_SIZE == BUR_DATA_OFFSET,
        "Hussar: TEXT i DATA nie sa ciagle w pliku .bur"
    )

    ASSERT(
        BUR_HEADER_VADDR + BUR_HEADER_SIZE == BUR_TEXT_VADDR,
        "Hussar: HEADER i TEXT nie sa ciagle w VA"
    )

    ASSERT(
        BUR_TEXT_VADDR + BUR_TEXT_SIZE == BUR_DATA_VADDR,
        "Hussar: TEXT i DATA nie sa ciagle w VA"
    )
}
