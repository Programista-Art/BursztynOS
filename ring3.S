/*
 * Bursztyn OS - przejscie Ring 0 -> Ring 3
 *
 * ABI:
 *
 *   extern "C" [[noreturn]]
 *   void przejdz_do_ring3(
 *       uint64_t rip,
 *       uint64_t rsp,
 *       bool wykonaj_swapgs
 *   );
 *
 * System V AMD64:
 *
 *   RDI = docelowy RIP Ring 3
 *   RSI = docelowy RSP Ring 3
 *   RDX = 0       -> nie wykonuj SWAPGS
 *         != 0    -> wykonaj SWAPGS bezposrednio przed IRETQ
 *
 * GDT Bursztyn OS:
 *
 *   0x08 = Kernel Code
 *   0x10 = Kernel Data
 *   0x1B = User Data | RPL 3
 *   0x23 = User Code | RPL 3
 *
 * Funkcja nie wraca.
 *
 * Bezpieczenstwo:
 *  - maskowalne IRQ sa blokowane podczas budowania ramy IRETQ,
 *  - RIP i RSP musza nalezec do dolnej kanonicznej polowy adresow,
 *  - strona NULL nie jest akceptowana,
 *  - RSP musi odpowiadac ABI startu programu: 16n + 8,
 *  - RFLAGS Ring 3 sa tworzone od zera zamiast kopiowania flag jadra,
 *  - wszystkie ogolne rejestry sa zerowane przed wejsciem do aplikacji,
 *    aby nie ujawniac wskaznikow/danych jadra,
 *  - FS i GS nie sa nadpisywane zwyklym MOV,
 *  - SWAPGS, jesli wymagane, znajduje sie bezposrednio przed IRETQ.
 *
 * UWAGA:
 * Poprawne uzycie SWAPGS wymaga spojnej polityki IA32_GS_BASE /
 * IA32_KERNEL_GS_BASE w kodzie SYSCALL i przerwan. Trzeci argument zostaje
 * zachowany dla zgodnosci z dotychczasowym ABI Bursztyna.
 */

.section .text
.code64

/* =========================================================================
 * STALE
 * ========================================================================= */

.set SELEKTOR_DANYCH_USER, 0x1B
.set SELEKTOR_KODU_USER,   0x23

/*
 * RFLAGS startowe procesu:
 *
 *   bit 1 = zawsze 1
 *   IF    = 1
 *
 * Celowo NIE przenosimy z Ring 0:
 *   CF/PF/AF/ZF/SF/TF/DF/OF/IOPL/NT/RF/AC/VIF/VIP/ID.
 */
.set RFLAGS_USER_START, 0x202

/*
 * Dolna kanoniczna polowa x86_64 konczy sie przed 0x0000800000000000.
 * Testujemy to przez przesuniecie o 47 bitow: dla poprawnego adresu
 * uzytkownika wynik musi byc rowny 0.
 */
.set MIN_ADRES_USER, 0x1000

/* =========================================================================
 * PUBLICZNE WEJSCIE
 * ========================================================================= */

.global przejdz_do_ring3
.type przejdz_do_ring3, @function
.p2align 4

przejdz_do_ring3:
    /*
     * Od tej chwili nie pozwalamy timerowi/sprzetowi wejsc pomiedzy
     * przygotowanie stosu a IRETQ. IRETQ przywroci IF=1 z ramy Ring 3.
     */
    cli

    /* ---------------------------------------------------------------------
     * 1. WALIDACJA RIP
     * ------------------------------------------------------------------ */

    /*
     * RIP nie moze wskazywac w strone NULL ani nisko zarezerwowany obszar.
     */
    cmpq $MIN_ADRES_USER, %rdi
    jb .Lring3_fatal

    /*
     * Wymagamy dolnego, kanonicznego adresu Ring 3:
     *
     *   0x0000000000000000 .. 0x00007FFFFFFFFFFF
     */
    movq %rdi, %rax
    shrq $47, %rax
    jne .Lring3_fatal

    /* ---------------------------------------------------------------------
     * 2. WALIDACJA RSP
     * ------------------------------------------------------------------ */

    cmpq $MIN_ADRES_USER, %rsi
    jb .Lring3_fatal

    movq %rsi, %rax
    shrq $47, %rax
    jne .Lring3_fatal

    /*
     * Programy .bur zaczynaja w _start skompilowanym jako zwykla funkcja
     * C/C++. GCC dla System V AMD64 oczekuje na wejsciu:
     *
     *     RSP mod 16 == 8
     *
     * Loader Bursztyna przygotowuje stos wlasnie w ten sposob.
     */
    movq %rsi, %rax
    andq $0x0F, %rax
    cmpq $8, %rax
    jne .Lring3_fatal

    /* ---------------------------------------------------------------------
     * 3. NORMALIZACJA DECYZJI SWAPGS
     * ------------------------------------------------------------------ */

    /*
     * Nie ufamy konkretnej wartosci bool z kodu wywolujacego.
     * Kazde RDX != 0 traktujemy jako true.
     *
     * R11 zostanie wyzerowane przed IRETQ.
     */
    xorq %r11, %r11
    testq %rdx, %rdx
    setne %r11b

    /* ---------------------------------------------------------------------
     * 4. SEGMENTY DANYCH USER
     * ------------------------------------------------------------------ */

    /*
     * W long mode DS/ES nie uczestnicza w zwyklej adresacji liniowej,
     * ale poprawne selektory DPL3 utrzymuja spojny stan procesu.
     *
     * Nie zapisujemy FS ani GS. Ich bazy sa zarzadzane niezaleznie,
     * a GS moze byc czescia mechanizmu SWAPGS/per-CPU.
     */
    movw $SELEKTOR_DANYCH_USER, %ax
    movw %ax, %ds
    movw %ax, %es

    /* ---------------------------------------------------------------------
     * 5. BUDOWA SPRZETOWEJ RAMY IRETQ
     * ------------------------------------------------------------------ */

    /*
     * IRETQ przy zmianie CPL z 0 do 3 oczekuje na stosie:
     *
     *   [RSP + 0x00] RIP
     *   [RSP + 0x08] CS
     *   [RSP + 0x10] RFLAGS
     *   [RSP + 0x18] USER_RSP
     *   [RSP + 0x20] USER_SS
     *
     * PUSH wykonujemy w odwrotnej kolejnosci.
     */

    pushq $SELEKTOR_DANYCH_USER
    pushq %rsi
    pushq $RFLAGS_USER_START
    pushq $SELEKTOR_KODU_USER
    pushq %rdi

    /* ---------------------------------------------------------------------
     * 6. USUNIECIE STANU JADRA Z GPR
     * ------------------------------------------------------------------ */

    /*
     * Po IRETQ aplikacja moze odczytac wszystkie GPR.
     * Nie pozostawiamy w nich:
     *  - adresu wejscia,
     *  - stosu,
     *  - argumentow wywolujacego,
     *  - wskaznikow lub tymczasowych danych kernela.
     *
     * RSP musi oczywiscie pozostac wskaznikiem ramy IRETQ.
     * R11 tymczasowo przechowuje tylko decyzje o SWAPGS.
     */
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rdi, %rdi
    xorq %rsi, %rsi
    xorq %rbp, %rbp

    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10

    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    /* ---------------------------------------------------------------------
     * 7. GS I OSTATECZNY POWROT DO RING 3
     * ------------------------------------------------------------------ */

    testq %r11, %r11
    jz .Lring3_bez_swapgs

    /*
     * Zerujemy ostatni GPR PRZED SWAPGS.
     *
     * SWAPGS jest celowo ostatnia instrukcja przed IRETQ. Minimalizuje to
     * okno, w ktorym CPU nadal wykonuje Ring 0, ale GS wskazuje juz baze
     * przeznaczona dla Ring 3.
     *
     * NMI/MCE w tym pojedynczym oknie wymagaja docelowo odpornej polityki
     * SWAPGS/IST w przerwania.S. Tego problemu nie da sie rozwiazac tylko
     * w tym pliku.
     */
    xorq %r11, %r11
    swapgs
    iretq

.Lring3_bez_swapgs:
    xorq %r11, %r11
    iretq

/* =========================================================================
 * FAIL-CLOSED
 * ========================================================================= */

.Lring3_fatal:
    /*
     * Nie wracamy do wywolujacego, bo funkcja przejscia do Ring 3 powinna
     * byc uzywana jako [[noreturn]]. Bledny RIP/RSP oznacza uszkodzony
     * kontekst procesu lub blad kernela.
     *
     * UD2 daje jednoznaczny #UD w debuggerze. Jesli obsluga wyjatku nie
     * zatrzyma CPU, petla HLT jest ostatecznym zabezpieczeniem.
     */
    cli
    ud2

.Lring3_fatal_hlt:
    hlt
    jmp .Lring3_fatal_hlt

.size przejdz_do_ring3, .-przejdz_do_ring3

/*
 * Plik nie wymaga wykonywalnego stosu.
 */
.section .note.GNU-stack,"",@progbits
