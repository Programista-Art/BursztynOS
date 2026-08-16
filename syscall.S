/*
 * Bursztyn OS - brama Bursztynowych Wywolan Systemowych (BWS)
 *
 * Wejscie:
 *   instrukcja SYSCALL wykonana z Ring 3.
 *
 * ABI Ring 3 -> BWS:
 *
 *   R8  = numer funkcji
 *   R9  = argument 1
 *   R10 = argument 2
 *   R12 = argument 3
 *   R13 = argument 4
 *
 * Sprzetowe zachowanie SYSCALL:
 *
 *   RCX = RIP, pod ktory nalezy wrocic
 *   R11 = RFLAGS uzytkownika
 *   CS/SS zostaja zmienione wedlug IA32_STAR
 *   RSP NIE jest automatycznie zmieniany
 *
 * ABI C++ obsluga_wywolan_systemowych():
 *
 *   RDI = numer funkcji
 *   RSI = argument 1
 *   RDX = argument 2
 *   RCX = argument 3
 *   R8  = argument 4
 *   RAX = wynik
 *
 * Najwazniejsze zasady:
 *
 *  1. Nigdy nie zapisujemy RSP Ring 3 do globalnej zmiennej.
 *     RSP aplikacji jest przechowywany na prywatnym stosie Ring 0.
 *
 *  2. SWAPGS wykonywany jest natychmiast po wejsciu, a LFENCE blokuje
 *     spekulacyjne uzycie starej bazy GS przed odczytem stosu jadra.
 *
 *  3. Dopiero po przejsciu na poprawny stos Ring 0 i zapisaniu calego
 *     kontekstu mozemy wlaczyc maskowalne IRQ.
 *
 *  4. Timer moze przerwac syscall, ale obecny dispatcher IDT nie zmienia
 *     procesu podczas zwyklego wykonywania Ring 0. Wyjatek stanowi proces
 *     oznaczony jako PROCES_PUSTY podczas zakonczania - wtedy syscall juz
 *     nie wraca.
 *
 *  5. Przed SYSRETQ ponownie blokujemy IRQ, sprawdzamy docelowy RIP/RSP,
 *     ustawiamy bezpieczne RFLAGS użytkownika i dopiero wtedy wykonujemy
 *     SWAPGS.
 *
 *  6. Stary RAX nie jest zachowywany - zgodnie z ABI bws_wywolaj() RAX
 *     jest rejestrem wyniku. Pozostale GPR, poza sprzetowo niszczonymi
 *     RCX/R11, sa zachowywane.
 *
 * Wymagania od syscalls.cpp / schedulera:
 *
 *   IA32_GS_BASE po SWAPGS w Ring 0 wskazuje na:
 *
 *       uint64_t bezpieczny_stos_jadra;
 *
 *   czyli:
 *
 *       %gs:0 = aktualny szczyt prywatnego stosu Ring 0.
 *
 *   Scheduler aktualizuje bezpieczny_stos_jadra przy zmianie procesu.
 */

.section .text
.code64

/* =========================================================================
 * STALE
 * ========================================================================= */

/*
 * Minimalny poprawny adres Ring 3.
 * Strona NULL i pierwsza strona sa niedostepne dla aplikacji.
 */
.set BWS_MIN_USER_ADDR, 0x1000

/*
 * Bezpieczne RFLAGS przy powrocie do bws_wywolaj():
 *
 *   bit 1 = zawsze ustawiony
 *   IF    = 1
 *
 * Celowo zerujemy m.in.:
 *   TF, DF, IOPL, NT, RF, AC, VM.
 *
 * Wrapper C++ deklaruje clobber "cc", wiec nie obiecuje aplikacji
 * zachowania flag arytmetycznych przez syscall.
 */
.set BWS_RFLAGS_USER, 0x202

/* =========================================================================
 * IMPORT C++
 * ========================================================================= */

.extern obsluga_wywolan_systemowych

/* =========================================================================
 * WEJSCIE LSTAR
 * ========================================================================= */

.global brama_wywolan_systemowych
.type brama_wywolan_systemowych, @function
.p2align 4

brama_wywolan_systemowych:
    /*
     * W tym miejscu:
     *
     *   RSP = stos Ring 3
     *   RCX = user RIP po instrukcji SYSCALL
     *   R11 = user RFLAGS
     *
     * IA32_FMASK w syscalls.cpp kasuje IF/DF/TF/NT/AC dla Ring 0.
     *
     * RAX jest rejestrem wyniku BWS, wiec jego poprzednia wartosc nie musi
     * zostac zachowana. Uzywamy go jako chwilowego schowka user RSP.
     */
    movq %rsp, %rax

    /*
     * Przelacz:
     *
     *   GS_BASE        -> kernelowy GS
     *   KERNEL_GS_BASE -> user GS
     */
    swapgs

    /*
     * Bariera dla klasy problemow speculative SWAPGS.
     * Po LFENCE odczyt %gs:0 nie powinien zostac wykonany spekulacyjnie
     * z baza GS sprzed SWAPGS.
     */
    lfence

    /*
     * %gs:0 zawiera aktualny szczyt stosu Ring 0 ustawiony przez scheduler.
     */
    movq %gs:0, %rsp

    /*
     * Stos Ring 0 musi byc niezerowy i 16-bajtowo wyrownany.
     *
     * Nie wykonujemy jeszcze PUSH, wiec przy bledzie mozemy bezpiecznie
     * zatrzymac CPU bez dalszego niszczenia pamieci.
     */
    testq %rsp, %rsp
    jz .Lbws_fatal_kernel_stack

    testq $0x0F, %rsp
    jnz .Lbws_fatal_kernel_stack

    /* ---------------------------------------------------------------------
     * ZAPIS KONTEKSTU
     * ------------------------------------------------------------------ */

    /*
     * Kolejnosc na stosie od najstarszego elementu:
     *
     *   user_rsp
     *   user_rip (RCX)
     *   user_rflags (R11)
     *   RDI
     *   RSI
     *   RDX
     *   R8
     *   R9
     *   R10
     *   RBX
     *   RBP
     *   R12
     *   R13
     *   R14
     *   R15
     *
     * To 15 qword = 120 bajtow.
     */

    pushq %rax                    /* RSP Ring 3 */
    pushq %rcx                    /* RIP Ring 3 */
    pushq %r11                    /* RFLAGS Ring 3 */

    pushq %rdi
    pushq %rsi
    pushq %rdx

    pushq %r8
    pushq %r9
    pushq %r10

    pushq %rbx
    pushq %rbp

    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /*
     * Szczyt kernel stack byl wyrownany do 16.
     *
     * 15 PUSH x 8 B = 120 B, czyli RSP % 16 == 8.
     * System V AMD64 wymaga RSP % 16 == 0 bezposrednio PRZED CALL.
     */
    subq $8, %rsp

    /* ---------------------------------------------------------------------
     * TRANSLACJA ABI BWS -> SYSTEM V AMD64
     * ------------------------------------------------------------------ */

    /*
     * Uzywamy zapisanych jeszcze w rejestrach wartosci argumentow.
     */
    movq %r8,  %rdi               /* nr_funkcji */
    movq %r9,  %rsi               /* arg1 */
    movq %r10, %rdx               /* arg2 */
    movq %r12, %rcx               /* arg3 */
    movq %r13, %r8                /* arg4 */

    /*
     * ABI C/C++ wymaga DF=0 niezaleznie od tego, co robil kod user.
     * IA32_FMASK rowniez kasuje DF, ale CLD stanowi lokalna gwarancje.
     */
    cld

    /*
     * Od tej chwili:
     *  - jestesmy na prywatnym stosie Ring 0,
     *  - user RSP/RIP sa zapisane,
     *  - wszystkie wymagane GPR sa zapisane,
     *  - GS wskazuje dane kernela.
     *
     * Możemy wiec obslugiwac IRQ podczas dluzszych syscalli (AHCI, siec,
     * GUI). IDT nie wykona zwyklego context switcha, dopoki przerwany kod
     * ma CS Ring 0 i proces nie zostal oznaczony jako zakonczony.
     */
    sti

    call obsluga_wywolan_systemowych

    /*
     * RAX = wynik BWS.
     *
     * Przed odtwarzaniem ramy blokujemy maskowalne IRQ. User RFLAGS zostana
     * ustawione jawnie na BWS_RFLAGS_USER przy SYSRETQ.
     */
    cli

    /* Usun 8-bajtowy padding ABI. */
    addq $8, %rsp

    /* ---------------------------------------------------------------------
     * ODTWORZENIE GPR
     * ------------------------------------------------------------------ */

    popq %r15
    popq %r14
    popq %r13
    popq %r12

    popq %rbp
    popq %rbx

    popq %r10
    popq %r9
    popq %r8

    popq %rdx
    popq %rsi
    popq %rdi

    /*
     * Na stosie zostaly:
     *
     *   [RSP + 0x00] zapisane user RFLAGS
     *   [RSP + 0x08] user RIP
     *   [RSP + 0x10] user RSP
     *
     * Oryginalne RFLAGS nie sa odtwarzane, wiec pierwszy qword pomijamy.
     */
    addq $8, %rsp

    /*
     * RCX jest sprzetowym celem SYSRETQ.
     * Wrapper Ring 3 deklaruje RCX jako clobbered.
     */
    popq %rcx

    /* ---------------------------------------------------------------------
     * WALIDACJA USER RIP
     * ------------------------------------------------------------------ */

    cmpq $BWS_MIN_USER_ADDR, %rcx
    jb .Lbws_fatal_user_return

    /*
     * Bursztyn OS umieszcza Ring 3 w dolnej polowie kanonicznej:
     *
     *   0x0000000000001000 .. 0x00007FFFFFFFFFFF
     *
     * Po SHR 47 wynik musi byc rowny 0.
     *
     * Jest to rowniez ochrona przed historycznie niebezpiecznym SYSRET
     * z niekanonicznym RCX, ktory na czesci CPU generuje #GP juz w Ring 0.
     */
    movq %rcx, %r11
    shrq $47, %r11
    jne .Lbws_fatal_user_return

    /* ---------------------------------------------------------------------
     * WALIDACJA USER RSP
     * ------------------------------------------------------------------ */

    /*
     * Ostatni qword na kernel stack to RSP aplikacji.
     * R11 jest rejestrem clobbered przez ABI SYSCALL, wiec moze sluzyc
     * jako scratch.
     */
    movq (%rsp), %r11

    cmpq $BWS_MIN_USER_ADDR, %r11
    jb .Lbws_fatal_user_return

    /*
     * RSP Ring 3 rowniez musi znajdowac sie w dolnej kanonicznej polowie.
     * Wartosci oryginalnej nie tracimy - nadal lezy na kernel stack.
     */
    shrq $47, %r11
    jne .Lbws_fatal_user_return

    /*
     * Nie wymuszamy tu RSP % 16 == 8. SYSCALL znajduje sie wewnatrz
     * zwyklej funkcji bws_wywolaj(), wiec chwilowe wyrownanie stosu zalezy
     * od kodu wygenerowanego przez kompilator. Musimy odtworzyc dokladnie
     * ten sam RSP, z ktorym wykonano SYSCALL.
     */
    movq (%rsp), %r11
    movq %r11, %rsp

    /*
     * Po zmianie RSP na user stack nie wykonujemy zadnych PUSH/POP ani
     * odczytow spod RSP. Pozostaly tylko instrukcje rejestrowe/MSR.
     */

    /*
     * Ustal bezpieczne flagi użytkownika.
     *
     * R11 jest drugim specjalnym rejestrem SYSRETQ i zgodnie z ABI
     * bws_wywolaj() jest clobbered.
     */
    movq $BWS_RFLAGS_USER, %r11

    /*
     * Ostatnia zmiana GS przed Ring 3.
     *
     * Po SWAPGS:
     *   GS_BASE        = user GS (obecnie 0)
     *   KERNEL_GS_BASE = kernelowy GS z &bezpieczny_stos_jadra
     *
     * Nie wykonujemy juz dostepu przez GS.
     */
    swapgs

    /*
     * SYSRETQ:
     *   RIP    <- RCX
     *   RFLAGS <- R11
     *   CS/SS  <- selektory wynikajace z IA32_STAR
     *
     * RSP pozostaje ustawiony na zapisany stos aplikacji.
     */
    sysretq

/* =========================================================================
 * FAIL-CLOSED
 * ========================================================================= */

.Lbws_fatal_kernel_stack:
    /*
     * GS jest juz kernelowy, ale RSP jest zerowy/niepoprawnie wyrownany.
     * Nie probujemy wywolywac C++ ani loggera.
     *
     * UD2 daje jednoznaczny punkt zatrzymania w emulatorze/debuggerze.
     */
    cli
    ud2

.Lbws_fatal_kernel_stack_hlt:
    hlt
    jmp .Lbws_fatal_kernel_stack_hlt

.Lbws_fatal_user_return:
    /*
     * Ten blad oznacza uszkodzona rame powrotu procesu albo naruszenie
     * integralnosci kernela. Nadal znajdujemy sie na poprawnym kernel stack
     * i z kernelowym GS.
     *
     * Nie wykonujemy SYSRETQ z podejrzanym RCX/RSP.
     */
    cli
    ud2

.Lbws_fatal_user_return_hlt:
    hlt
    jmp .Lbws_fatal_user_return_hlt

.size brama_wywolan_systemowych, .-brama_wywolan_systemowych

/*
 * Plik nie wymaga wykonywalnego stosu.
 */
.section .note.GNU-stack,"",@progbits
