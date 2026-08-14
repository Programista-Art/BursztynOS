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

    pushq $0x1B      
    pushq %rsi       
    
    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax       
    
    pushq $0x23      
    pushq %rdi       

    /* TESTUJ CZY WYMAGANY JEST SWAPGS PRZED SKOKIEM */
    test %rdx, %rdx
    jz brak_swapgs
    swapgs
brak_swapgs:
    iretq