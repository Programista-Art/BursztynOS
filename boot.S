/*
 * Bursztyn OS - boot.S
 *
 * Wejscie Multiboot2 i bezpieczne przejscie z 32-bit protected mode
 * do x86_64 Long Mode.
 *
 * GRUB przekazuje:
 *   EAX = 0x36D76289
 *   EBX = fizyczny adres struktury informacji Multiboot2
 *
 * Bootstrap tworzy identity mapping 0..4 GiB przy uzyciu stron 2 MiB.
 * Docelowy VMM pozniej zastapi te tablice wlasnymi mapowaniami.
 */

/* =========================================================================
 * 1. MULTIBOOT2 HEADER
 * ========================================================================= */

.set MULTIBOOT2_HEADER_MAGIC,            0xE85250D6
.set MULTIBOOT2_ARCH_I386,               0
.set MULTIBOOT2_HEADER_TAG_END,          0
.set MULTIBOOT2_HEADER_TAG_INFO_REQUEST, 1
.set MULTIBOOT2_HEADER_TAG_FRAMEBUFFER,  5

.set MULTIBOOT2_INFO_TAG_MEMORY_MAP,      6
.set MULTIBOOT2_INFO_TAG_FRAMEBUFFER,     8

.set MULTIBOOT2_BOOTLOADER_MAGIC,         0x36D76289

.section .multiboot, "a"
.balign 8

multiboot_header_start:
    .long MULTIBOOT2_HEADER_MAGIC
    .long MULTIBOOT2_ARCH_I386
    .long multiboot_header_end - multiboot_header_start
    .long -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCH_I386 + (multiboot_header_end - multiboot_header_start))

    /*
     * Kernel wymaga mapy pamieci i informacji o framebufferze.
     */
    .balign 8
    .short MULTIBOOT2_HEADER_TAG_INFO_REQUEST
    .short 0
    .long 16
    .long MULTIBOOT2_INFO_TAG_MEMORY_MAP
    .long MULTIBOOT2_INFO_TAG_FRAMEBUFFER

    /*
     * Preferowany tryb framebuffer.
     */
    .balign 8
    .short MULTIBOOT2_HEADER_TAG_FRAMEBUFFER
    .short 0
    .long 20
    .long 1280
    .long 720
    .long 32

    /*
     * Koniec naglowka.
     */
    .balign 8
    .short MULTIBOOT2_HEADER_TAG_END
    .short 0
    .long 8

multiboot_header_end:

/* =========================================================================
 * 2. BOOTSTRAP PAGE TABLES + STACK
 * ========================================================================= */

.section .bss, "aw", @nobits

/*
 * PML4[0] -> PDP
 *
 * PDP[0] -> pd_table    : 0..1 GiB
 * PDP[1] -> pd_table_1  : 1..2 GiB
 * PDP[2] -> pd_table_2  : 2..3 GiB
 * PDP[3] -> pd_table_3  : 3..4 GiB
 *
 * Kazdy PD ma 512 wpisow po 2 MiB.
 */
.balign 4096

.global pml4_table
pml4_table:
    .skip 4096

.global pdp_table
pdp_table:
    .skip 4096

.global pd_table
pd_table:
    .skip 4096

pd_table_1:
    .skip 4096

pd_table_2:
    .skip 4096

pd_table_3:
    .skip 4096

/*
 * Stos startowy kernela.
 * Symbol stack_top jest uzywany pozniej przez TSS/scheduler/syscalls.
 */
.balign 16

stack_bottom:
    .skip 65536

.global stack_top
stack_top:

/*
 * Kod bledu wczesnego bootu, przydatny w debuggerze:
 *
 * 0 = brak bledu
 * 1 = zly Multiboot2 magic
 * 2 = zly wskaznik/alignment Multiboot2 info
 * 3 = brak CPUID
 * 4 = brak MSR
 * 5 = brak PAE
 * 6 = brak PSE
 * 7 = brak extended CPUID
 * 8 = brak Long Mode
 */
.balign 4

.global boot_error_code
boot_error_code:
    .long 0

/* =========================================================================
 * 3. TYMCZASOWA GDT
 * ========================================================================= */

.section .rodata, "a"
.balign 8

gdt64:
    /* 0x00 - null */
    .quad 0x0000000000000000

    /* 0x08 - Ring 0, 64-bit code, L=1, D/B=0 */
gdt64_code:
    .quad 0x00209A0000000000

    /* 0x10 - Ring 0, read/write data */
gdt64_data:
    .quad 0x0000920000000000

gdt64_end:

/*
 * LGDT wykonywane jest jeszcze w 32-bit mode, wiec pseudo-deskryptor
 * ma 6 bajtow: limit16 + base32.
 */
gdt64_pointer32:
    .word gdt64_end - gdt64 - 1
    .long gdt64

/* =========================================================================
 * 4. STALE CPU / PAGING
 * ========================================================================= */

.set PAGE_PRESENT,  0x001
.set PAGE_WRITE,    0x002
.set PAGE_HUGE_2M,  0x080

.set CR0_WP,        0x00010000
.set CR0_PG,        0x80000000

.set CR4_PSE,       0x00000010
.set CR4_PAE,       0x00000020

.set EFER_MSR,      0xC0000080
.set EFER_LME,      0x00000100

.set CPUID_FEAT_EDX_PSE, 0x00000008
.set CPUID_FEAT_EDX_MSR, 0x00000020
.set CPUID_FEAT_EDX_PAE, 0x00000040
.set CPUID_EXT_EDX_LM,   0x20000000

.set BOOT_PAGE_TABLE_DWORDS, ((6 * 4096) / 4)
.set BOOT_PDE_COUNT,          (4 * 512)

/* =========================================================================
 * 5. ENTRY 32-BIT
 * ========================================================================= */

.section .text, "ax"
.code32

.global _start
.type _start, @function

_start:
    /*
     * Nie polegamy na stanie IF/DF pozostawionym przez bootloader.
     */
    cli
    cld

    /*
     * Zachowaj argumenty Multiboot2:
     *   EBP = magic
     *   ESI = info pointer
     *
     * EDI bedzie uzywany przez REP STOSL.
     */
    movl %eax, %ebp
    movl %ebx, %esi

    /*
     * CALL do lokalnych helperow wymaga juz poprawnego stosu.
     */
    movl $stack_top, %esp
    andl $-16, %esp

    /*
     * Wczesna walidacja.
     */
    cmpl $MULTIBOOT2_BOOTLOADER_MAGIC, %ebp
    jne boot_fail_bad_magic

    testl %esi, %esi
    jz boot_fail_bad_info

    /*
     * Multiboot2 info structure jest wyrownana do 8 bajtow.
     */
    testl $7, %esi
    jnz boot_fail_bad_info

    /* ---------------------------------------------------------------------
     * Sprawdz wymagane mozliwosci CPU
     * ------------------------------------------------------------------ */

    call boot_cpu_has_cpuid
    testl %eax, %eax
    jz boot_fail_no_cpuid

    movl $1, %eax
    cpuid

    testl $CPUID_FEAT_EDX_MSR, %edx
    jz boot_fail_no_msr

    testl $CPUID_FEAT_EDX_PAE, %edx
    jz boot_fail_no_pae

    testl $CPUID_FEAT_EDX_PSE, %edx
    jz boot_fail_no_pse

    movl $0x80000000, %eax
    cpuid

    cmpl $0x80000001, %eax
    jb boot_fail_no_extended_cpuid

    movl $0x80000001, %eax
    cpuid

    testl $CPUID_EXT_EDX_LM, %edx
    jz boot_fail_no_long_mode

    /* ---------------------------------------------------------------------
     * Wyzeruj WSZYSTKIE bootstrap page tables
     * ------------------------------------------------------------------ */

    xorl %eax, %eax
    movl $pml4_table, %edi
    movl $BOOT_PAGE_TABLE_DWORDS, %ecx
    rep stosl

    /* ---------------------------------------------------------------------
     * PML4[0] -> PDP
     * ------------------------------------------------------------------ */

    movl $pdp_table, %eax
    orl $(PAGE_PRESENT | PAGE_WRITE), %eax

    movl %eax, pml4_table
    movl $0,   pml4_table + 4

    /* ---------------------------------------------------------------------
     * PDP[0..3] -> cztery PD
     * ------------------------------------------------------------------ */

    movl $pd_table, %eax
    orl $(PAGE_PRESENT | PAGE_WRITE), %eax
    movl %eax, pdp_table + 0
    movl $0,   pdp_table + 4

    movl $pd_table_1, %eax
    orl $(PAGE_PRESENT | PAGE_WRITE), %eax
    movl %eax, pdp_table + 8
    movl $0,   pdp_table + 12

    movl $pd_table_2, %eax
    orl $(PAGE_PRESENT | PAGE_WRITE), %eax
    movl %eax, pdp_table + 16
    movl $0,   pdp_table + 20

    movl $pd_table_3, %eax
    orl $(PAGE_PRESENT | PAGE_WRITE), %eax
    movl %eax, pdp_table + 24
    movl $0,   pdp_table + 28

    /* ---------------------------------------------------------------------
     * 2048 * 2 MiB = 4 GiB identity mapping
     * ------------------------------------------------------------------ */

    movl $pd_table, %edi
    xorl %ebx, %ebx
    movl $BOOT_PDE_COUNT, %ecx

boot_map_2m_loop:
    movl %ebx, %eax
    orl $(PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE_2M), %eax

    movl %eax, 0(%edi)
    movl $0,   4(%edi)

    addl $0x00200000, %ebx
    addl $8, %edi

    decl %ecx
    jnz boot_map_2m_loop

    /*
     * Wszystkie zapisy page tables maja byc widoczne przed aktywacja.
     */
    mfence

    /* ---------------------------------------------------------------------
     * Zaladuj tymczasowa GDT
     * ------------------------------------------------------------------ */

    lgdt gdt64_pointer32

    /* ---------------------------------------------------------------------
     * CR4: PSE + PAE
     *
     * PAE wlaczamy przed zaladowaniem CR3 jako PML4.
     * ------------------------------------------------------------------ */

    movl %cr4, %eax
    orl $(CR4_PSE | CR4_PAE), %eax
    movl %eax, %cr4

    /* ---------------------------------------------------------------------
     * CR3 -> PML4
     * ------------------------------------------------------------------ */

    movl $pml4_table, %eax
    movl %eax, %cr3

    /* ---------------------------------------------------------------------
     * EFER.LME
     * ------------------------------------------------------------------ */

    movl $EFER_MSR, %ecx
    rdmsr
    orl $EFER_LME, %eax
    wrmsr

    /* ---------------------------------------------------------------------
     * CR0.WP + CR0.PG
     *
     * CR0.PE zostalo juz ustawione przez GRUB.
     * ------------------------------------------------------------------ */

    movl %cr0, %eax
    orl $(CR0_WP | CR0_PG), %eax
    movl %eax, %cr0

    /*
     * Far jump laduje 64-bitowy CS i faktycznie wchodzi w 64-bit mode.
     */
    ljmp $0x08, $long_mode_start

.size _start, . - _start

/* =========================================================================
 * 6. DETEKCJA CPUID
 * ========================================================================= */

.type boot_cpu_has_cpuid, @function

boot_cpu_has_cpuid:
    /*
     * CPUID jest dostepne, jezeli mozemy zmienic EFLAGS.ID (bit 21).
     * Oryginalne EFLAGS przywracamy przed RET.
     */
    pushfl
    popl %eax

    movl %eax, %ecx
    xorl $0x00200000, %eax

    pushl %eax
    popfl

    pushfl
    popl %eax

    xorl %ecx, %eax
    andl $0x00200000, %eax

    /*
     * Zachowaj wynik PRZED przywroceniem EFLAGS.
     * POPFL zmienia m.in. ZF, wiec SETNE nie moze korzystac bezposrednio
     * z flag pozostawionych przez AND.
     */
    movl %eax, %edx

    pushl %ecx
    popfl

    testl %edx, %edx
    setne %al
    movzbl %al, %eax
    ret

.size boot_cpu_has_cpuid, . - boot_cpu_has_cpuid

/* =========================================================================
 * 7. WCZESNE BLEDY
 * ========================================================================= */

boot_fail_bad_magic:
    movl $1, boot_error_code
    jmp boot_halt

boot_fail_bad_info:
    movl $2, boot_error_code
    jmp boot_halt

boot_fail_no_cpuid:
    movl $3, boot_error_code
    jmp boot_halt

boot_fail_no_msr:
    movl $4, boot_error_code
    jmp boot_halt

boot_fail_no_pae:
    movl $5, boot_error_code
    jmp boot_halt

boot_fail_no_pse:
    movl $6, boot_error_code
    jmp boot_halt

boot_fail_no_extended_cpuid:
    movl $7, boot_error_code
    jmp boot_halt

boot_fail_no_long_mode:
    movl $8, boot_error_code
    jmp boot_halt

boot_halt:
    cli

1:
    hlt
    jmp 1b

/* =========================================================================
 * 8. ENTRY 64-BIT
 * ========================================================================= */

.code64

.type long_mode_start, @function

long_mode_start:
    /*
     * Tymczasowy segment danych Ring 0.
     */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /*
     * FS/GS selektory zerujemy. Ich bazy beda ustawiane pozniej przez
     * mechanizm per-CPU / SWAPGS.
     */
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs

    cld

    /*
     * SysV AMD64:
     * przed CALL RSP jest 16-byte aligned.
     * Po zapisaniu return address kernel_main ma RSP mod 16 == 8.
     */
    movq $stack_top, %rsp
    andq $-16, %rsp

    /*
     * Jawne zero-extension argumentow z trybu 32-bit:
     *
     * RDI = Multiboot2 magic
     * RSI = Multiboot2 info physical pointer
     */
    movl %ebp, %edi
    movl %esi, %esi

    /*
     * Czytelny terminator najwczesniejszego stack trace.
     */
    xorl %ebp, %ebp

    .extern kernel_main
    call kernel_main

    /*
     * kernel_main nie powinien wrocic.
     */
long_mode_halt:
    cli

2:
    hlt
    jmp 2b

.size long_mode_start, . - long_mode_start

/* =========================================================================
 * 9. NON-EXECUTABLE HOST STACK NOTE
 * ========================================================================= */

.section .note.GNU-stack, "", @progbits
