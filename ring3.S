/*
 * Mechanizm: Skok z Jądra (Ring 0) do Programu Użytkownika (Ring 3)
 */

.global przejdz_do_ring3

.section .text
.code64

/*
 * extern "C" void przejdz_do_ring3(uint64_t rip, uint64_t rsp, bool z_syscalla);
 * RDI = Punkt wejścia (RIP z loadera)
 * RSI = Wirtualny szczyt stosu Ring 3 (RSP z loadera)
 * RDX = flaga z_syscalla (1 = wykonaj swapgs, 0 = nie wykonuj)
 */
przejdz_do_ring3:
    /* Ustawiamy segmenty danych dla Ring 3. 
       UWAGA: W 64-bit NIE RUSZAMY FS i GS! GS jest zarządzany przez SWAPGS! */
    mov $0x1B, %ax
    mov %ax, %ds
    mov %ax, %es

    pushq $0x1B      /* SS (Segment Stosu Ring 3) */
    pushq %rsi       /* RSP (Szczyt stosu Ring 3) */
    
    pushfq
    popq %rax
    orq $0x200, %rax /* Włącz flagę przerwań (IF) */
    pushq %rax       /* RFLAGS */
    
    pushq $0x23      /* CS (Segment Kodu Ring 3) */
    pushq %rdi       /* RIP (Punkt wejścia) */

    /* Jeśli z_syscalla == true, wykonaj SWAPGS aby przywrócić GS użytkownika */
    test %rdx, %rdx
    jz brak_swapgs
    swapgs
brak_swapgs:
    iretq