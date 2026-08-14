.set MULTIBOOT2_HEADER_MAGIC, 0xE85250D6
.set GRUB_MULTIBOOT_ARCHITECTURE_I386, 0
.set MULTIBOOT_HEADER_TAG_END, 0

.section .multiboot, "a"
.align 8
header_start:
    .long MULTIBOOT2_HEADER_MAGIC
    .long GRUB_MULTIBOOT_ARCHITECTURE_I386
    .long header_end - header_start
    .long -(MULTIBOOT2_HEADER_MAGIC + GRUB_MULTIBOOT_ARCHITECTURE_I386 + (header_end - header_start))

    /* Znacznik: Żądanie Framebuffera */
    .align 8
    .short 5
    .short 0
    .long 20
    .long 1280
    .long 720
    .long 32

    /* Znacznik końcowy */
    .align 8
    .short MULTIBOOT_HEADER_TAG_END
    .short 0
    .long 8
header_end:

.section .bss
.align 4096
.global pml4_table
.global pdp_table
.global pd_table
pml4_table:
    .skip 4096
pdp_table:
    .skip 4096
pd_table:
    .skip 4096

.align 16
.global stack_top
stack_bottom:
    .skip 65536
stack_top:

.section .rodata
.align 8
gdt64:
    .quad 0x0000000000000000
gdt64_code:
    .quad 0x00209A0000000000
gdt64_data:
    .quad 0x0000920000000000
gdt64_pointer:
    .word gdt64_pointer - gdt64 - 1
    .quad gdt64

.section .text
.code32
.global _start
_start:
    movl $stack_top, %esp
    
    /* BEZPIECZEŃSTWO: Zabezpieczamy argumenty Multiboot2 w %edi i %esi */
    movl %eax, %edi
    movl %ebx, %esi

    /* PML4[0] -> pdp_table */
    movl $pdp_table, %eax
    orl $0b11, %eax
    movl %eax, pml4_table

    /* PDP[0] -> pd_table */
    movl $pd_table, %eax
    orl $0b11, %eax
    movl %eax, pdp_table

    /* Wypełnij 512 wpisów po 2MB = 1 GB identity mapping */
    /* ZMIANA KRYTYCZNA: Używamy %edx zamiast %edi, by nie zepsuć magic number! */
    movl $pd_table, %edx         
    movl $0, %ecx                /* Licznik wpisów */
    movl $0, %ebx                /* Adres fizyczny */

map_loop:
    movl %ebx, %eax              /* Skopiuj adres fizyczny do %eax */
    orl $0x00000083, %eax        /* Zmieszaj adres z flagami Present+Writable+Huge (0x83) */
    movl %eax, (%edx)            /* Zapisz z powrotem do tablicy stron! */
    movl $0, 4(%edx)             /* Górne 32 bity = 0 (adresy < 4GB) */
    addl $0x200000, %ebx         /* Następne 2MB */
    addl $8, %edx                /* Następny wpis (8 bajtów) */
    incl %ecx
    cmpl $512, %ecx
    jl map_loop

    /* Załaduj CR3 */
    movl $pml4_table, %eax
    movl %eax, %cr3

    /* Włącz PAE */
    movl %cr4, %eax
    orl $0b100000, %eax
    movl %eax, %cr4

    /* Włącz Long Mode (LME) */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    /* Włącz Paging */
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    /* Skok do 64-bit */
    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    movq $stack_top, %rsp

    /* Parametry z %rdi i %rsi zostaną automatycznie przekazane! */
    .extern kernel_main
    call kernel_main

hlt_loop:
    cli
    hlt
    jmp hlt_loop
    