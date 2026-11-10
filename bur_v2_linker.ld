OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
  . = 0x1000;
  .text : { *(.text .text.*) . = ALIGN(4096); }
  .rodata : { *(.rodata .rodata.*) . = ALIGN(4096); }
  .ssp_guard : { KEEP(*(.data.ssp_guard)) . = ALIGN(4096); }
  .data : {
    *(.data .data.*) *(.got .got.*) *(.igot .igot.*)
    . = ALIGN(4096);
  }
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) . = ALIGN(4096); }
  .rela.dyn : { *(.rela.*) }
  .dynamic : { *(.dynamic) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .hash : { *(.hash) *(.gnu.hash) }
  .unsupported : { *(.init_array* .fini_array* .ctors* .dtors* .tdata* .tbss*) }
  ASSERT(SIZEOF(.unsupported) == 0, "BUR v2: constructors/TLS require unsupported runtime")
  ASSERT(SIZEOF(.ssp_guard) == 4096, "BUR v2: missing canary page")
  /DISCARD/ : { *(.naglowek .naglowek.*) *(.comment .note* .eh_frame* .gcc_except_table*) }
}
