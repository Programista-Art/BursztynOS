/*
 * Mechanizm: Zewnętrzna Brama Asemblerowa Wywołań (BWS - Bursztynowe Wywołania Systemowe)
 * Opis: Punkt wejścia dla instrukcji sprzętowej SYSCALL wykonywanej z Ring 3.
 * Zrzuca rejestry ułamkowe i implementuje tzw. Standard BWS (translacje rejestrów).
 */

.global brama_wywolan_systemowych

/* Bezpieczny, globalny schowek na wskaźnik stosu aplikacji (Ring 3) */
.section .data
.align 8
bws_user_rsp_schowek:
    .quad 0

.section .text
.code64

brama_wywolan_systemowych:
    swapgs
    movq %rsp, bws_user_rsp_schowek(%rip)
    movq %gs:0, %rsp

    /* 1. Zabezpieczenie rejestrów niszczonych przez SYSCALL */
    pushq %rcx
    pushq %r11

    /* 2. KRYTYCZNE: Zabezpieczenie rejestrów przed optymalizatorem GCC!
       Funkcje C++ niszczą rejestry RDI, RSI, RDX, R8, R9, R10.
       Musimy je zachować, aby programy Ring 3 nie traciły swoich zmiennych! */
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r8
    pushq %r9
    pushq %r10

    /* 3. TŁUMACZENIE KONWENCJI BWS (Bursztyn OS ABI) */
    movq %r8,  %rdi                              /* Numer Funkcji */
    movq %r9,  %rsi                              /* Argument 1 */
    movq %r10, %rdx                              /* Argument 2 */
    movq %r12, %rcx                              /* Argument 3 */
    movq %r13, %r8                               /* Argument 4 */

    /* 4. Zabezpieczenie reszty środowiska przed skokiem do C++ */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    cld
    .extern obsluga_wywolan_systemowych
    call obsluga_wywolan_systemowych
    
    /* RAX posiada teraz wynik operacji dla Ring 3 */

    /* 5. Odtworzenie stanu bezpiecznego procesu użytkownika */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    /* Odtwarzamy rejestry wyczyszczone z argumentów */
    popq %r10
    popq %r9
    popq %r8
    popq %rdx
    popq %rsi
    popq %rdi

    /* Odtwarzamy rejestry z flagami dla SYSRET */
    popq %r11
    popq %rcx

    /* 6. Powrót na stos użytkownika */
    movq bws_user_rsp_schowek(%rip), %rsp
    swapgs

    sysretq