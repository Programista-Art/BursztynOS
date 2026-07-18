/*
 * Mechanizm: Skok z Jądra (Ring 0) do Programu Użytkownika (Ring 3)
 * Opis: Ustawia segmenty procesora na wariant User Data (0x1B) 
 * i używa sztuczki z IRETQ do obniżenia uprawnień i wykonania skoku w User Code (0x23).
 */

.global przejdz_do_ring3

.section .text
.code64

/*
 * extern "C" void przejdz_do_ring3(uint64_t rip, uint64_t rsp);
 * RDI = Punkt wejścia (RIP z loadera)
 * RSI = Wirtualny szczyt stosu Ring 3 (RSP z loadera)
 */
przejdz_do_ring3:
    /* 1. Ładowanie rejestrów segmentowych danymi Użytkownika. 
       KRYTYCZNE: 0x18 to offset w GDT, ale w Ring 3 musimy dodać flagę RPL=3.
       0x18 | 3 = 0x1B. 
    */
    mov $0x1B, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* 2. Przygotowanie fałszywego stosu dla instrukcji IRETQ */
    
    /* PUSH 1: SS (Stack Segment) - Musi być 0x1B! */
    pushq $0x1B      
    
    /* PUSH 2: RSP (Wskaźnik stosu przekazany przez Loader w RSI) */
    pushq %rsi       
    
    /* PUSH 3: RFLAGS (Flagi procesora) - Włączamy przerwania (bit 9 = 0x200) */
    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax       
    
    /* PUSH 4: CS (Code Segment) - Znowu dodajemy RPL=3 (0x20 | 3 = 0x23!) */
    pushq $0x23      
    
    /* PUSH 5: RIP (Adres pierwszej instrukcji shell.cpp przekazany w RDI) */
    pushq %rdi       

    /* 3. Ostateczny skok! Procesor zdejmie te 5 wartości, obniży sobie 
       poziom uprawnień do Ring 3 i bezpiecznie wyląduje w powłoce. */
    iretq