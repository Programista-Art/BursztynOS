/* BUR v1: file-backed TEXT/DATA, trailing zero-fill represented only in memory.
 * The linker owns the header so its offsets cannot drift from the sections.
 * 512 KiB is this small utility's build budget, not the BUR ABI maximum. */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
BUR_HEADER_VADDR = 0x1000000;
BUR_HEADER_SIZE = 0x1000;
BUR_TEXT_OFFSET = 0x1000;
BUR_TEXT_VADDR = 0x1001000;
BUR_TEXT_SIZE = SIZEOF(.text);
BUR_DATA_OFFSET = BUR_TEXT_OFFSET + BUR_TEXT_SIZE;
BUR_DATA_VADDR = BUR_TEXT_VADDR + BUR_TEXT_SIZE;
BUR_DATA_SIZE = ALIGN(__bur_data_memory_end - BUR_DATA_VADDR, 4096);
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
        QUAD(BUR_TEXT_OFFSET) QUAD(BUR_TEXT_SIZE) QUAD(BUR_TEXT_VADDR)
        QUAD(BUR_DATA_OFFSET) QUAD(BUR_DATA_SIZE) QUAD(BUR_DATA_VADDR)
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
    .data BUR_DATA_VADDR : AT(BUR_DATA_OFFSET) {
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
    /* Unsupported runtime mechanisms fail the link instead of disappearing. */
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
    ASSERT(DEFINED(_start), "HDA BUR: missing entry")
    ASSERT(SIZEOF(.bur_unsupported) == 0, "HDA BUR: unsupported runtime section")
    ASSERT(BUR_TEXT_SIZE > 0 && BUR_TEXT_SIZE <= 0x34000, "HDA BUR: TEXT budget")
    ASSERT(BUR_DATA_SIZE > 0 && BUR_DATA_SIZE <= 0x800000, "HDA BUR: DATA/BSS budget")
    ASSERT(BUR_DATA_OFFSET + SIZEOF(.data) <= 524288, "HDA BUR: file exceeds 512 KiB")
}
