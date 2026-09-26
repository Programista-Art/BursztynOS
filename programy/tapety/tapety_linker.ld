/* Kontrolki 3: 144 KiB TEXT (zmierzone uzycie 0x20e80), 128 KiB DATA. */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

BUR_HEADER_OFFSET = 0x0000;
BUR_HEADER_SIZE   = 0x1000;
BUR_HEADER_VADDR  = 0x1000000;
BUR_TEXT_OFFSET   = 0x1000;
BUR_TEXT_SIZE     = 0x40000;
BUR_TEXT_VADDR    = 0x1001000;
BUR_DATA_OFFSET   = 0x41000;
BUR_DATA_SIZE     = 0x20000;
BUR_DATA_VADDR    = 0x1041000;
BUR_TEXT_END      = BUR_TEXT_VADDR + BUR_TEXT_SIZE;
BUR_DATA_END      = BUR_DATA_VADDR + BUR_DATA_SIZE;

PHDRS {
    bur_header PT_LOAD FLAGS(4);
    bur_text PT_LOAD FLAGS(5);
    bur_data PT_LOAD FLAGS(6);
}

SECTIONS {
    .naglowek BUR_HEADER_VADDR : AT(BUR_HEADER_OFFSET) {
        __bur_header_start = .;
        KEEP(*(.naglowek)) KEEP(*(.naglowek.*))
        __bur_header_payload_end = .;
        . = BUR_HEADER_SIZE;
        __bur_header_end = .;
    } :bur_header = 0x00

    .text BUR_TEXT_VADDR : AT(BUR_TEXT_OFFSET) {
        __bur_text_start = .;
        KEEP(*(.text._start)) KEEP(*(.text._start.*))
        *(.text) *(.text.*) *(.gnu.linkonce.t.*)
        . = ALIGN(16);
        *(.rodata) *(.rodata.*) *(.srodata) *(.srodata.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(16);
        __bur_text_used_end = .;
        . = BUR_TEXT_SIZE;
        __bur_text_end = .;
    } :bur_text = 0x00

    .data BUR_DATA_VADDR : AT(BUR_DATA_OFFSET) {
        __bur_data_start = .;
        *(.data) *(.data.*) *(.sdata) *(.sdata.*)
        *(.gnu.linkonce.d.*) *(.gnu.linkonce.s.*) *(.got) *(.got.*)
        . = ALIGN(16);
        __bur_data_file_end = .;
    } :bur_data

    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bur_bss_start = .;
        *(.bss) *(.bss.*) *(.sbss) *(.sbss.*)
        *(.gnu.linkonce.b.*) *(.gnu.linkonce.sb.*) *(COMMON)
        . = ALIGN(16);
        __bur_bss_end = .;
        __bur_data_memory_end = .;
    } :bur_data

    .bur_konstruktory (NOLOAD) : {
        __bur_ctor_start = .;
        KEEP(*(.preinit_array)) KEEP(*(.preinit_array.*))
        KEEP(*(.init_array)) KEEP(*(.init_array.*))
        KEEP(*(.fini_array)) KEEP(*(.fini_array.*))
        KEEP(*(.ctors)) KEEP(*(.ctors.*)) KEEP(*(.dtors)) KEEP(*(.dtors.*))
        __bur_ctor_end = .;
    }
    .bur_tls (NOLOAD) : {
        __bur_tls_start = .;
        *(.tdata) *(.tdata.*) *(.tbss) *(.tbss.*)
        __bur_tls_end = .;
    }
    .bur_dynamiczne (NOLOAD) : {
        __bur_dynamic_start = .;
        *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu.hash)
        *(.plt) *(.plt.*) *(.interp)
        __bur_dynamic_end = .;
    }
    /DISCARD/ : {
        *(.eh_frame) *(.eh_frame.*) *(.eh_frame_hdr)
        *(.gcc_except_table) *(.gcc_except_table.*)
        *(.comment) *(.note) *(.note.*) *(.debug) *(.debug_*)
        *(.line) *(.stab) *(.stabstr) *(.gnu_debuglink) *(.gnu_debugdata)
        *(.gnu.lto_*)
    }

    ASSERT(DEFINED(_start), "KONTROLKI3 BUR: brak _start")
    ASSERT(DEFINED(naglowek), "KONTROLKI3 BUR: brak naglowka")
    ASSERT(__bur_header_payload_end <= BUR_HEADER_VADDR + BUR_HEADER_SIZE,
           "KONTROLKI3 BUR: naglowek przekroczyl 4 KiB")
    ASSERT(SIZEOF(.naglowek) == BUR_HEADER_SIZE,
           "KONTROLKI3 BUR: nieprawidlowy obszar naglowka")
    ASSERT(__bur_text_used_end <= BUR_TEXT_END,
           "KONTROLKI3 BUR: TEXT przekroczyl 192 KiB")
    ASSERT(SIZEOF(.text) == BUR_TEXT_SIZE,
           "KONTROLKI3 BUR: nieprawidlowy TEXT")
    ASSERT(__bur_data_file_end <= BUR_DATA_END,
           "KONTROLKI3 BUR: DATA przekroczylo 128 KiB")
    ASSERT(__bur_data_memory_end <= BUR_DATA_END,
           "KONTROLKI3 BUR: DATA+BSS przekroczyly 128 KiB")
    ASSERT(__bur_ctor_start == __bur_ctor_end,
           "KONTROLKI3 BUR: konstruktory nie sa obslugiwane")
    ASSERT(__bur_tls_start == __bur_tls_end,
           "KONTROLKI3 BUR: TLS nie jest obslugiwany")
    ASSERT(__bur_dynamic_start == __bur_dynamic_end,
           "KONTROLKI3 BUR: dynamic linking jest zabroniony")
    ASSERT(BUR_TEXT_OFFSET + BUR_TEXT_SIZE == BUR_DATA_OFFSET,
           "KONTROLKI3 BUR: niespojne offsety")
    ASSERT(BUR_TEXT_VADDR + BUR_TEXT_SIZE == BUR_DATA_VADDR,
           "KONTROLKI3 BUR: niespojne VA")
}
