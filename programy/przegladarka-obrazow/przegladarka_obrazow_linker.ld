/*
 * Bursztyn OS - linker Przegladarki Obrazow (.bur)
 *
 * Skrypt jest częścią ABI pliku .bur i musi być zgodny z NaglowekBur
 * zapisanym w przegladarka-obrazow.cpp:
 *
 *   nagłówek:  offset 0x0000, obszar 0x1000,   VA 0x1000000
 *   TEXT:      offset 0x1000, rozmiar 0x30000, VA 0x1001000
 *   DATA/BSS:  offset 0x31000, rozmiar 0x20000, VA 0x1031000
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

BUR_HEADER_OFFSET = 0x0000;
BUR_HEADER_SIZE   = 0x1000;
BUR_HEADER_VADDR  = 0x1000000;

BUR_TEXT_OFFSET   = 0x1000;
BUR_TEXT_SIZE     = 0x30000;
BUR_TEXT_VADDR    = 0x1001000;

BUR_DATA_OFFSET   = 0x31000;
BUR_DATA_SIZE     = 0x20000;
BUR_DATA_VADDR    = 0x1031000;

BUR_TEXT_END      = BUR_TEXT_VADDR + BUR_TEXT_SIZE;
BUR_DATA_END      = BUR_DATA_VADDR + BUR_DATA_SIZE;

PHDRS
{
    bur_header PT_LOAD FLAGS(4);
    bur_text   PT_LOAD FLAGS(5);
    bur_data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    .naglowek BUR_HEADER_VADDR : AT(BUR_HEADER_OFFSET)
    {
        __bur_header_start = .;
        KEEP(*(.naglowek))
        KEEP(*(.naglowek.*))
        __bur_header_payload_end = .;
        . = BUR_HEADER_SIZE;
        __bur_header_end = .;
    } :bur_header = 0x00

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
        __bur_text_used_end = .;
        . = BUR_TEXT_SIZE;
        __bur_text_end = .;
    } :bur_text = 0x00

    .data BUR_DATA_VADDR : AT(BUR_DATA_OFFSET)
    {
        __bur_data_start = .;
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        *(.gnu.linkonce.d.*)
        *(.gnu.linkonce.s.*)
        *(.got)
        *(.got.*)
        . = ALIGN(16);
        __bur_data_file_end = .;
    } :bur_data

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

    .bur_konstruktory (NOLOAD) : {
        __bur_ctor_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
        __bur_ctor_end = .;
    } :bur_data

    .bur_destruktory (NOLOAD) : {
        __bur_dtor_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        __bur_dtor_end = .;
    } :bur_data

    /DISCARD/ :
    {
        *(.interp)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.hash)
        *(.gnu.hash)
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }

    ASSERT(
        ADDR(.naglowek) == BUR_HEADER_VADDR,
        "BURSZTYN KALKULATOR: nieprawidlowy VA naglowka"
    )

    ASSERT(
        LOADADDR(.naglowek) == BUR_HEADER_OFFSET,
        "BURSZTYN KALKULATOR: nieprawidlowy offset pliku naglowka"
    )

    ASSERT(
        ADDR(.text) == BUR_TEXT_VADDR,
        "BURSZTYN KALKULATOR: nieprawidlowy VA TEXT"
    )

    ASSERT(
        LOADADDR(.text) == BUR_TEXT_OFFSET,
        "BURSZTYN KALKULATOR: nieprawidlowy offset pliku TEXT"
    )

    ASSERT(
        __bur_text_used_end <= BUR_TEXT_END,
        "BURSZTYN KALKULATOR: kod + rodata przekraczaja segment TEXT"
    )

    ASSERT(
        SIZEOF(.text) == BUR_TEXT_SIZE,
        "BURSZTYN KALKULATOR: TEXT musi miec dokladnie BUR_TEXT_SIZE bajtow"
    )

    ASSERT(
        _start >= BUR_TEXT_VADDR &&
        _start < BUR_TEXT_END,
        "BURSZTYN KALKULATOR: _start musi znajdowac sie w TEXT"
    )

    ASSERT(
        ADDR(.data) == BUR_DATA_VADDR,
        "BURSZTYN KALKULATOR: nieprawidlowy VA DATA"
    )

    ASSERT(
        LOADADDR(.data) == BUR_DATA_OFFSET,
        "BURSZTYN KALKULATOR: nieprawidlowy offset pliku DATA"
    )

    ASSERT(
        BUR_TEXT_OFFSET + BUR_TEXT_SIZE == BUR_DATA_OFFSET,
        "BURSZTYN KALKULATOR: TEXT i DATA nie sa sasiednie w pliku"
    )

    ASSERT(
        BUR_TEXT_VADDR + BUR_TEXT_SIZE == BUR_DATA_VADDR,
        "BURSZTYN KALKULATOR: TEXT i DATA nie sa sasiednie w VA"
    )
}
