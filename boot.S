/*
 * Mechanizm: Nagłówek Multiboot2, Identity Paging oraz Long Mode (64-bit)
 * Opis: Ustawienie tablic stron, aktywacja Long Mode i skok do C++.
 */

.set MULTIBOOT2_HEADER_MAGIC, 0xE85250D6
.set GRUB_MULTIBOOT_ARCHITECTURE_I386, 0 
.set MULTIBOOT_HEADER_TAG_END, 0
.set MULTIBOOT_TAG_TYPE_MEMORY_MAP, 6

/* Flaga "a" (allocatable) jest WYMAGANA przez nowsze wersje GNU Binutils. */
.section .multiboot, "a"
.align 8
header_start:
    .long MULTIBOOT2_HEADER_MAGIC
    .long GRUB_MULTIBOOT_ARCHITECTURE_I386
    .long header_end - header_start
    /* Suma kontrolna nagłówka */
    .long -(MULTIBOOT2_HEADER_MAGIC + GRUB_MULTIBOOT_ARCHITECTURE_I386 + (header_end - header_start))

    /* Znacznik nr 1: Żądanie Framebuffera (Typ 5) */
    /* KRYTYCZNA ZMIANA: Zamiast 0,0,32 podajemy konkretne wymiary 1024x768x32, 
       które są w 100% obsługiwane przez QEMU (-vga std). 
       GRUB ma problem z "0" (dowolne) i może zignorować żądanie graficzne! */
    .align 8
    .short 5
    .short 0
    .long 20
    .long 1024   /* Szerokość na sztywno */
    .long 768    /* Wysokość na sztywno */
    .long 32     /* Głębia kolorów (32 bity) */

    /* Znacznik nr 2: Prośba o mapę pamięci (Dla PMM) */
    .align 8
    .short 1
    .short 0
    .long 12
    .long MULTIBOOT_TAG_TYPE_MEMORY_MAP

    /* Znacznik końcowy Multiboot2 */
    .align 8
    .short MULTIBOOT_HEADER_TAG_END
    .short 0
    .long 8
header_end:

.section .bss
.align 4096
/* Tablice stronicowania dla początkowego Identity Paging */
.global pml4_table
.global pd_table
pml4_table:
    .skip 4096
pdp_table:
    .skip 4096
pd_table:
    .skip 4096

/* Stos systemowy wyrównany do 16 bajtów (Wymóg System V ABI) */
.align 16
.global stack_top
stack_bottom:
    .skip 16384
stack_top:

.section .rodata
.align 8
/* Tymczasowe GDT potrzebne do wejścia w Long Mode */
gdt64:
    .quad 0x0000000000000000       /* Null Descriptor */
gdt64_code:
    .quad 0x00209A0000000000       /* Kod Jądra 64-bit */
gdt64_data:
    .quad 0x0000920000000000       /* Dane Jądra 64-bit */
gdt64_pointer:
    .word gdt64_pointer - gdt64 - 1
    .quad gdt64

.section .text
.code32
.global _start
_start:
    /* Ustawienie wskaźnika stosu */
    movl $stack_top, %esp

    /* Zapisanie parametrów od GRUB-a, by przetrwały przełączenie trybów */
    movl %eax, %edi
    movl %ebx, %esi

    /* Konfiguracja Identity Paging dla 2MB (Tymczasowe mapowanie 1:1) */
    movl $pdp_table, %eax
    orl $0b11, %eax
    movl %eax, pml4_table

    movl $pd_table, %eax
    orl $0b11, %eax
    movl %eax, pdp_table

    movl $0x0, %eax
    orl $0b10000011, %eax
    movl %eax, pd_table

    /* Przypisanie bazy stron do rejestru CR3 */
    movl $pml4_table, %eax
    movl %eax, %cr3

    /* Włączenie trybu PAE w rejestrze CR4 */
    movl %cr4, %eax
    orl $0b100000, %eax
    movl %eax, %cr4

    /* Aktywacja Long Mode (LME) w rejestrach specjalnych MSR */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    /* Ostateczne włączenie Pagingu (Rejestr CR0) */
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    /* Załadowanie GDT i długi skok (Far Jump) do trybu 64-bit */
    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    /* Aktualizacja rejestrów segmentowych dla strefy 64-bit (Indeks danych) */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* Odświeżenie wskaźnika stosu dla szerszego rejestru RSP */
    movq $stack_top, %rsp

    /* Skok do funkcji jądra napisanej w C++ (RDI i RSI niosą dane od GRUB-a) */
    .extern kernel_main
    call kernel_main

hlt_loop:
    cli
    hlt
    jmp hlt_loop
    