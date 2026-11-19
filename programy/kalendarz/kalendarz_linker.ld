/* BUR v1: file-backed TEXT/DATA. DATA LMA/VMA wyliczane PO .text
 * (LOADADDR+SIZEOF), bo wczesne SIZEOF(.text)==0 dawalo overlap LMA. */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
BUR_HEADER_VADDR = 0x1000000;
BUR_HEADER_SIZE = 0x1000;
BUR_TEXT_OFFSET = 0x1000;
BUR_TEXT_VADDR = 0x1001000;
PHDRS {
    bur_header PT_LOAD FLAGS(4);
    bur_text PT_LOAD FLAGS(5);
    bur_data PT_LOAD FLAGS(6);
}
SECTIONS {
    .naglowek BUR_HEADER_VADDR : AT(0) {
        naglowek = .;
        LONG(0x00525542)
        QUAD(_start)
        QUAD(BUR_TEXT_OFFSET)
        QUAD(SIZEOF(.text))
        QUAD(BUR_TEXT_VADDR)
        QUAD(LOADADDR(.text) + SIZEOF(.text))
        QUAD(ALIGN(__bur_data_memory_end - (ADDR(.text) + SIZEOF(.text)), 4096))
        QUAD(ADDR(.text) + SIZEOF(.text))
        . = BUR_HEADER_SIZE;
    } :bur_header = 0
    .text BUR_TEXT_VADDR : AT(BUR_TEXT_OFFSET) {
        KEEP(*(.text._start)) KEEP(*(.text._start.*))
        *(.text) *(.text.*) *(.gnu.linkonce.t.*)
        . = ALIGN(16);
        *(.rodata) *(.rodata.*) *(.srodata) *(.srodata.*) *(.gnu.linkonce.r.*)
        __bur_text_used_end = .;
        . = ALIGN(4096);
    } :bur_text = 0
    .data (ADDR(.text) + SIZEOF(.text)) : AT(LOADADDR(.text) + SIZEOF(.text)) {
        *(.data) *(.data.*) *(.sdata) *(.sdata.*)
        *(.gnu.linkonce.d.*) *(.gnu.linkonce.s.*) *(.got) *(.got.*)
        . = ALIGN(16);
        __bur_data_file_end = .;
    } :bur_data
    .bss (NOLOAD) : {
        . = ALIGN(16);
        *(.bss) *(.bss.*) *(.sbss) *(.sbss.*)
        *(.gnu.linkonce.b.*) *(.gnu.linkonce.sb.*) *(COMMON)
        . = ALIGN(16);
        __bur_data_memory_end = .;
    } :bur_data
    .bur_unsupported (NOLOAD) : {
        KEEP(*(.preinit_array*)) KEEP(*(.init_array*)) KEEP(*(.fini_array*))
        KEEP(*(.ctors*)) KEEP(*(.dtors*))
        *(.tdata*) *(.tbss*) *(.dynamic) *(.dynsym) *(.dynstr)
        *(.hash) *(.gnu.hash) *(.plt*) *(.iplt*) *(.igot*) *(.interp) *(.rel*)
    }
    /DISCARD/ : {
        *(.eh_frame*) *(.gcc_except_table*) *(.comment) *(.note*)
        *(.debug*) *(.line) *(.stab*) *(.gnu_debuglink) *(.gnu_debugdata)
        *(.gnu.lto_*)
    }
    ASSERT(DEFINED(_start), "KALENDARZ BUR: missing entry")
    ASSERT(SIZEOF(.bur_unsupported) == 0, "KALENDARZ BUR: unsupported runtime section")
    ASSERT(SIZEOF(.text) > 0 && SIZEOF(.text) <= 0x36000, "KALENDARZ BUR: TEXT budget")
    ASSERT((__bur_data_memory_end - (ADDR(.text) + SIZEOF(.text))) > 0 &&
           (__bur_data_memory_end - (ADDR(.text) + SIZEOF(.text))) <= 0x800000,
           "KALENDARZ BUR: DATA/BSS budget")
    ASSERT((LOADADDR(.text) + SIZEOF(.text) + SIZEOF(.data)) <= 524288,
           "KALENDARZ BUR: file exceeds 512 KiB")
}
