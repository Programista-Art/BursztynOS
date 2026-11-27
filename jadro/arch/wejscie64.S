/*
 * Bursztyn OS - natywne wejscie 64-bit jadra (Faza R1B).
 *
 * Wejscie Multiboot2 (_start w boot.S, e_entry, 32-bit PM) pozostaje bez zmian. To jest OSOBNA sciezka dla
 * przyszlego BRB (R2), wchodzaca juz w trybie IA-32e. Kontrakt stanu: jadro/arch/stan_wejscia64.h,
 * docs/brb/r1b-wejscie-64bit.md.
 *
 *   RDI = MAGIC_WEJSCIA_NATYWNEGO ("BRB1JRAK"), RSI = PA kontraktu pamieci rozruchu (KontraktV1),
 *   RSP = stos przejscia (≡ 0 mod 16 w chwili `jmp`), IF = 0, CPL 0, stronicowanie 4-poziomowe,
 *   okno VA obrazu zmapowane na podklad, zasoby z kontraktu dostepne tozsamosciowo.
 *
 * Kolejnosc (zadna instrukcja uprzywilejowana zalezna od opcjonalnej cechy przed walidacja):
 *   1. zapis RFLAGS, RSP, RDI, RSI w rejestrach zachowywanych (r12..r15), cli, cld,
 *   2. wlasny stos jadra (stack_top w .bss obrazu - VA okna) i wlasna GDT (deskryptory z ustawionym bitem
 *      accessed, bo .rodata jest w tablicach przejscia tylko do odczytu), przeladowanie CS przez lretq,
 *   3. stack_protector_early_init (jak w boot.S, przed pierwsza ramka C++),
 *   4. wejscie_jadra64_c(magic, kontrakt, rsp_wejscia, rflags_wejscia) - walidacja stanu i kontraktu,
 *      zbieznosc ze wspolnym potokiem start_jadra(). Funkcja nigdy nie wraca; awaryjnie: cli; hlt.
 */

#include "metadane_jadra_asm.h"

/* =========================================================================
 * 1. METADANE WEJSCIA (segment .boot, za naglowkiem Multiboot2)
 * ========================================================================= */

.section .multiboot.metadane, "a"
.balign 8
.global metadane_jadra
metadane_jadra:
    .quad METADANE_SYGNATURA
    .short METADANE_WERSJA
    .short METADANE_ROZMIAR
    .long METADANE_KONTRAKT_R1B
    .quad __kernel_start
    .quad wejscie_jadra64
    .quad METADANE_WYROWNANIE
    .long METADANE_CECHY
    .long 0
    .quad 0
    .quad METADANE_SYGNATURA_KONCOWA
metadane_jadra_koniec:
.if (metadane_jadra_koniec - metadane_jadra) != METADANE_ROZMIAR
.error "metadane_jadra: zly rozmiar"
.endif

/* =========================================================================
 * 2. GDT WEJSCIA NATYWNEGO (tylko do odczytu)
 * ========================================================================= */

.section .rodata, "a"
.balign 16
gdt_wejscia64:
    .quad 0x0000000000000000      /* 0x00 null */
    .quad 0x00209B0000000000      /* 0x08 kod 64-bit, DPL 0, accessed */
    .quad 0x0000930000000000      /* 0x10 dane, DPL 0, accessed */
gdt_wejscia64_koniec:

.balign 8
gdt_wejscia64_wskaznik:
    .word gdt_wejscia64_koniec - gdt_wejscia64 - 1
    .quad gdt_wejscia64

/* =========================================================================
 * 3. WEJSCIE
 * ========================================================================= */

.section .text, "ax"
.code64

.global wejscie_jadra64
.type wejscie_jadra64, @function
wejscie_jadra64:
    pushfq                          /* 8 B na stosie przejscia (kontrakt: stos zapisywalny, >= 4 KiB) */
    popq %r15                       /* RFLAGS z chwili wejscia */
    cli
    cld
    movq %rsp, %r14                 /* RSP z chwili skoku (po pushf/pop - ta sama wartosc) */
    movq %rdi, %r12                 /* magic */
    movq %rsi, %r13                 /* PA kontraktu */

    movabsq $stack_top, %rsp
    andq $-16, %rsp
    xorl %ebp, %ebp

    movabsq $gdt_wejscia64_wskaznik, %rax
    lgdt (%rax)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs
    movabsq $1f, %rax
    pushq $0x08
    pushq %rax
    lretq
1:
    .extern stack_protector_early_init
    call stack_protector_early_init

    movq %r12, %rdi
    movq %r13, %rsi
    movq %r14, %rdx
    movq %r15, %rcx
    .extern wejscie_jadra64_c
    call wejscie_jadra64_c

wejscie_jadra64_stop:               /* wejscie_jadra64_c nigdy nie wraca */
    cli
2:
    hlt
    jmp 2b
.size wejscie_jadra64, . - wejscie_jadra64

#if defined(BURSZTYN_TEST_WEJSCIE64) && BURSZTYN_TEST_WEJSCIE64
/* =========================================================================
 * 4. TYLKO WARIANT TESTOWY: przejscie uprzezy R1B (jadro/rdzen/start_natywny.cpp)
 *
 * r1b_przejscie_testowe(cr3, rsp, magic, kontrakt):
 *   RDI = CR3 tablic przejscia, RSI = szczyt stosu przejscia (tozsamosc, RW+NX), RDX = magic, RCX = kontrakt.
 * Ten kod lezy w oknie VA obrazu: po `mov cr3` te same VA wskazuja KOPIE obrazu o identycznej zawartosci .text,
 * wiec kolejna instrukcja pochodzi juz z kopii. Po przelaczeniu nie uzywa pamieci poza rejestrami.
 * ========================================================================= */
.global r1b_przejscie_testowe
.type r1b_przejscie_testowe, @function
r1b_przejscie_testowe:
    cli
    movq %rsi, %rsp
    movq %rdx, %r8
    movq %rcx, %r9
    movq %rdi, %cr3
    movq %r8, %rdi
    movq %r9, %rsi
    movabsq $wejscie_jadra64, %rax
    jmp *%rax
.size r1b_przejscie_testowe, . - r1b_przejscie_testowe
#endif

.section .note.GNU-stack, "", @progbits
