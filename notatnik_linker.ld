/*
 * Bursztyn OS - linker Notatnika (.bur)
 *
 * Ten skrypt jest czescia ABI pliku .bur i MUSI byc zgodny z NaglowekBur
 * zapisanym w notatnik.cpp.
 *
 * Uklad obrazu:
 *
 *   naglowek:
 *     file offset  0x0000
 *     VA           0x600000
 *     region       0x1000
 *
 *   TEXT:
 *     file offset  0x1000
 *     VA           0x601000
 *     region       0x8000 = 32 KiB
 *
 *   DATA + BSS:
 *     file offset  0x9000
 *     VA           0x609000
 *     memory size  0x20000 = 128 KiB
 *
 * Naglowek .bur w poprawionym notatnik.cpp zawiera dokladnie:
 *
 *   tekst_przesuniecie = 0x1000
 *   tekst_rozmiar      = 0x8000
 *   tekst_wirtualny    = 0x601000
 *
 *   dane_przesuniecie  = 0x9000
 *   dane_rozmiar       = 0x20000
 *   dane_wirtualny     = 0x609000
 *
 * Loader Bursztyn OS wymaga calego segmentu TEXT w pliku, dlatego TEXT
 * jest dopelniany zerami do pelnych 32 KiB.
 *
 * DATA nie jest dopelniane do 128 KiB w pliku. dane_rozmiar jest rozmiarem
 * segmentu w pamieci; loader kopiuje dostepna czesc DATA i zeruje reszte,
 * co realizuje BSS bez niepotrzebnego powiekszania .bur.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* =========================================================================
 * 1. STALE ABI .BUR
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
 * notatnik.elf jest tylko etapem posrednim przed objcopy -O binary,
 * ale prawa segmentow sa jawne:
 *
 *   header  R--
 *   text    R-X
 *   data    RW-
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
     * NAGLOWEK .BUR
     * ------------------------------------------------------------------ */

    .naglowek BUR_HEADER_VADDR : AT(BUR_HEADER_OFFSET)
    {
        __bur_header_start = .;

        KEEP(*(.naglowek))
        KEEP(*(.naglowek.*))

        __bur_header_payload_end = .;

        /*
         * Pierwsza strona pliku nalezy wylacznie do formatu .bur.
         * Dopelnienie zapewnia TEXT dokladnie od offsetu 0x1000.
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

        /*
         * Punkt wejscia jest trzymany na poczatku sekcji kodu, o ile
         * kompilator wystawi osobna sekcje .text._start.
         */
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
         * Koniec rzeczywiscie wykorzystanego TEXT zapisujemy PRZED
         * dopelnieniem, aby ASSERT wykryl przekroczenie 32 KiB.
         */
        __bur_text_used_end = .;

        /*
         * Naglowek .bur deklaruje tekst_rozmiar == 0x8000.
         * Loader wymaga, aby caly ten region fizycznie istnial w pliku.
         */
        . = BUR_TEXT_SIZE;

        __bur_text_end = .;
    } :bur_text = 0x00

    /* ---------------------------------------------------------------------
     * DATA - bajty rzeczywiscie obecne w notatnik.bin
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
         * Dla -fno-pie GOT zwykle jest pusty. Jezeli toolchain utworzy
         * statyczne wpisy GOT, sa czescia zapisywalnego DATA.
         */
        *(.got)
        *(.got.*)

        . = ALIGN(16);

        __bur_data_file_end = .;
    } :bur_data

    /* ---------------------------------------------------------------------
     * BSS / COMMON - pamiec, ale bez bajtow w .bur
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
     * KONSTRUKTORY / DESTRUKTORY C++ - JESZCZE NIEOBSŁUGIWANE
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
     * TLS - JESZCZE NIEOBSŁUGIWANY W RING 3
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
     * DYNAMIC LINKING - NIEDOZWOLONY W .bur
     * ------------------------------------------------------------------ */

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
    }

    /* ---------------------------------------------------------------------
     * METADANE NIEPOTRZEBNE W RUNTIME
     * ------------------------------------------------------------------ */

    /DISCARD/ :
    {
        /*
         * Ring 3 Bursztyna nie ma obecnie exception runtime/unwindera.
         * notatnik.cpp jest kompilowany z -fno-exceptions.
         */
        *(.eh_frame)
        *(.eh_frame.*)
        *(.eh_frame_hdr)
        *(.gcc_except_table)
        *(.gcc_except_table.*)

        *(.comment)

        *(.note)
        *(.note.*)

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
     * ========================================================================= */

    ASSERT(
        DEFINED(_start),
        "BURSZTYN NOTATNIK: brak symbolu _start"
    )

    ASSERT(
        DEFINED(naglowek),
        "BURSZTYN NOTATNIK: brak symbolu naglowek"
    )

    ASSERT(
        (__bur_header_payload_end - __bur_header_start) == 60,
        "BURSZTYN NOTATNIK: NaglowekBur musi miec dokladnie 60 bajtow"
    )

    ASSERT(
        SIZEOF(.naglowek) == BUR_HEADER_SIZE,
        "BURSZTYN NOTATNIK: region naglowka musi miec dokladnie 0x1000 bajtow"
    )

    ASSERT(
        ADDR(.naglowek) == BUR_HEADER_VADDR,
        "BURSZTYN NOTATNIK: nieprawidlowy VA naglowka"
    )

    ASSERT(
        LOADADDR(.naglowek) == BUR_HEADER_OFFSET,
        "BURSZTYN NOTATNIK: nieprawidlowy offset pliku naglowka"
    )

    ASSERT(
        naglowek == BUR_HEADER_VADDR,
        "BURSZTYN NOTATNIK: symbol naglowek musi zaczynac obraz .bur"
    )

    ASSERT(
        ADDR(.text) == BUR_TEXT_VADDR,
        "BURSZTYN NOTATNIK: nieprawidlowy VA TEXT"
    )

    ASSERT(
        LOADADDR(.text) == BUR_TEXT_OFFSET,
        "BURSZTYN NOTATNIK: nieprawidlowy offset pliku TEXT"
    )

    ASSERT(
        __bur_text_used_end <= BUR_TEXT_END,
        "BURSZTYN NOTATNIK: kod + rodata przekraczaja 32 KiB segmentu TEXT"
    )

    ASSERT(
        SIZEOF(.text) == BUR_TEXT_SIZE,
        "BURSZTYN NOTATNIK: TEXT musi miec dokladnie 0x8000 bajtow"
    )

    ASSERT(
        _start >= BUR_TEXT_VADDR &&
        _start < BUR_TEXT_END,
        "BURSZTYN NOTATNIK: _start musi znajdowac sie w TEXT"
    )

    ASSERT(
        ADDR(.data) == BUR_DATA_VADDR,
        "BURSZTYN NOTATNIK: nieprawidlowy VA DATA"
    )

    ASSERT(
        LOADADDR(.data) == BUR_DATA_OFFSET,
        "BURSZTYN NOTATNIK: nieprawidlowy offset pliku DATA"
    )

    ASSERT(
        __bur_data_file_end <= BUR_DATA_END,
        "BURSZTYN NOTATNIK: zainicjalizowane DATA przekracza 32 KiB"
    )

    ASSERT(
        __bur_data_memory_end <= BUR_DATA_END,
        "BURSZTYN NOTATNIK: DATA + BSS przekraczaja 32 KiB"
    )

    ASSERT(
        __bur_ctor_start == __bur_ctor_end,
        "BURSZTYN NOTATNIK: wykryto konstruktory/destruktory C++, ale runtime .bur ich nie uruchamia"
    )

    ASSERT(
        __bur_tls_start == __bur_tls_end,
        "BURSZTYN NOTATNIK: wykryto TLS, ale runtime Ring 3 go jeszcze nie obsluguje"
    )

    ASSERT(
        __bur_dynamic_start == __bur_dynamic_end,
        "BURSZTYN NOTATNIK: wykryto dynamic linking/PLT - .bur musi byc statyczny"
    )

    ASSERT(
        BUR_HEADER_OFFSET + BUR_HEADER_SIZE == BUR_TEXT_OFFSET,
        "BURSZTYN NOTATNIK: HEADER i TEXT nie sa sasiednie w pliku"
    )

    ASSERT(
        BUR_HEADER_VADDR + BUR_HEADER_SIZE == BUR_TEXT_VADDR,
        "BURSZTYN NOTATNIK: HEADER i TEXT nie sa sasiednie w VA"
    )

    ASSERT(
        BUR_TEXT_OFFSET + BUR_TEXT_SIZE == BUR_DATA_OFFSET,
        "BURSZTYN NOTATNIK: TEXT i DATA nie sa sasiednie w pliku"
    )

    ASSERT(
        BUR_TEXT_VADDR + BUR_TEXT_SIZE == BUR_DATA_VADDR,
        "BURSZTYN NOTATNIK: TEXT i DATA nie sa sasiednie w VA"
    )
}

/* =========================================================================
 * 5. SYMBOLE POMOCNICZE
 * ========================================================================= */

/*
 * Te symbole pozwola docelowo generowac NaglowekBur z linkera zamiast
 * duplikowac stale recznie w notatnik.cpp.
 */
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
