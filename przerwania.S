/*
 * Bursztyn OS - obsluga GDT/IDT oraz stuby przerwan x86_64
 *
 * Plik odpowiada za:
 *  - przeladowanie GDTR i segmentow jadra,
 *  - zaladowanie IDTR,
 *  - utworzenie stubow ISR 0..47,
 *  - ujednolicenie ramy wyjatkow z kodem i bez kodu bledu,
 *  - zachowanie wszystkich ogolnych rejestrow procesora,
 *  - bezpieczne wejscie do WspolnaObslugaPrzerwan(),
 *  - powrot przez iretq, rowniez do Ring 3,
 *  - wspolprace z schedulerem zwracajacym nowy wskaznik ramy w RAX.
 *
 * Uklad ramy po zapisaniu GPR musi odpowiadac strukturze RejestryStanowe
 * uzywanej przez pzb.h / idt.cpp / loader.cpp:
 *
 *   +0x00  r15
 *   +0x08  r14
 *   +0x10  r13
 *   +0x18  r12
 *   +0x20  r11
 *   +0x28  r10
 *   +0x30  r9
 *   +0x38  r8
 *   +0x40  rbp
 *   +0x48  rsi
 *   +0x50  rdi
 *   +0x58  rdx
 *   +0x60  rcx
 *   +0x68  rbx
 *   +0x70  rax
 *   +0x78  wektor_przerwania
 *   +0x80  kod_bledu
 *   +0x88  RIP
 *   +0x90  CS
 *   +0x98  RFLAGS
 *   +0xA0  stary RSP   (tylko przy zmianie CPL/IST)
 *   +0xA8  stary SS    (tylko przy zmianie CPL/IST)
 *
 * Przy wyjatkach bez sprzetowego kodu bledu stub dodaje sztuczne zero.
 * Dzieki temu pola wektor/kod/RIP/CS/RFLAGS maja zawsze te same offsety.
 */

.section .text
.code64

/* =========================================================================
 * STALE
 * ========================================================================= */

.set SELEKTOR_KODU_JADRA,  0x08
.set SELEKTOR_DANYCH_JADRA, 0x10

/*
 * Offset pola CS wzgledem poczatku RejestryStanowe po PUSH_GPRS.
 */
.set RAMA_OFFSET_CS, 0x90

/*
 * Liczba bajtow zajmowana przez:
 *   wektor + kod bledu
 */
.set RAMA_ROZMIAR_NORMALIZACJI, 16

/* =========================================================================
 * IMPORT DYSPozytora C++
 * ========================================================================= */

.extern WspolnaObslugaPrzerwan

/* =========================================================================
 * PRZELADOWANIE GDT
 * ========================================================================= */

.global zaladuj_zaktualizowane_gdt
.type zaladuj_zaktualizowane_gdt, @function

zaladuj_zaktualizowane_gdt:
    /*
     * Pierwszy argument System V AMD64 ABI:
     *   RDI = adres 10-bajtowego GDTR.
     *
     * Na czas wymiany GDT blokujemy maskowalne IRQ. Oryginalny stan IF
     * przywracamy po przeladowaniu CS.
     */
    pushfq
    cli

    lgdt (%rdi)

    /*
     * Segmenty danych sa plaskie. W long mode ich base/limit nie steruja
     * zwykla adresacja, ale poprawne selektory nadal sa wymagane m.in.
     * przez mechanizmy ochrony i przejscia poziomow uprzywilejowania.
     */
    movw $SELEKTOR_DANYCH_JADRA, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /*
     * Celowo NIE przeladowujemy FS ani GS.
     *
     * W x86_64 ich bazy moga sluzyc do TLS/per-CPU i byc ustawiane przez
     * MSR/FSGSBASE. Resetowanie selektora podczas pozniejszego lgdt mogloby
     * zniszczyc ten stan.
     */

    /*
     * CS nie da sie zapisac zwyklym MOV. Far return laduje nowy selektor
     * kodu i wraca do lokalnej etykiety.
     *
     * Stos przed lretq:
     *   [RSP + 0]  nowy RIP
     *   [RSP + 8]  nowy CS
     *   [RSP +16]  zapisane RFLAGS
     */
    pushq $SELEKTOR_KODU_JADRA
    leaq .Lgdt_cs_przeladowany(%rip), %rax
    pushq %rax
    lretq

.Lgdt_cs_przeladowany:
    /*
     * Przywrocenie flag przywraca rowniez poprzedni stan IF.
     */
    popfq
    ret

.size zaladuj_zaktualizowane_gdt, .-zaladuj_zaktualizowane_gdt

/* =========================================================================
 * ZALADOWANIE IDT
 * ========================================================================= */

.global zaladuj_zaktualizowane_idt
.type zaladuj_zaktualizowane_idt, @function

zaladuj_zaktualizowane_idt:
    /*
     * RDI = adres 10-bajtowego IDTR.
     *
     * Samo LIDT jest atomowe z punktu widzenia widocznosci nowej bazy
     * deskryptorow. InicjalizujIDT() dodatkowo kontroluje stan IF.
     */
    lidt (%rdi)
    ret

.size zaladuj_zaktualizowane_idt, .-zaladuj_zaktualizowane_idt

/* =========================================================================
 * MAKRA ISR
 * ========================================================================= */

/*
 * Wyjatek/przerwanie BEZ kodu bledu wypychanego przez CPU.
 *
 * Po wejsciu CPU pozostawil:
 *
 *   RIP
 *   CS
 *   RFLAGS
 *   [RSP, SS gdy nastapila zmiana CPL]
 *
 * Dokladamy:
 *
 *   0
 *   wektor
 *
 * W efekcie na szczycie stosu:
 *
 *   wektor
 *   kod_bledu=0
 *   RIP
 *   CS
 *   RFLAGS
 */
.macro ISR_BEZ_KODU nr
    .global isr\nr
    .type isr\nr, @function

isr\nr:
    pushq $0
    pushq $\nr
    jmp wspolne_isr_wejscie

    .size isr\nr, .-isr\nr
.endm

/*
 * Wyjatek Z kodem bledu wypychanego przez CPU.
 *
 * CPU pozostawil:
 *
 *   kod_bledu
 *   RIP
 *   CS
 *   RFLAGS
 *   [RSP, SS]
 *
 * Dokladamy tylko numer wektora.
 */
.macro ISR_Z_KODEM nr
    .global isr\nr
    .type isr\nr, @function

isr\nr:
    pushq $\nr
    jmp wspolne_isr_wejscie

    .size isr\nr, .-isr\nr
.endm

/* =========================================================================
 * WYJATKI CPU 0..31
 * ========================================================================= */

/*
 * Lista wektorow z kodem bledu jest zgodna z x86_64:
 *
 *   8   #DF  Double Fault
 *   10  #TS  Invalid TSS
 *   11  #NP  Segment Not Present
 *   12  #SS  Stack-Segment Fault
 *   13  #GP  General Protection
 *   14  #PF  Page Fault
 *   17  #AC  Alignment Check
 *   21  #CP  Control Protection
 *   29  #VC  VMM Communication (AMD)
 *   30  #SX  Security Exception
 *
 * #CP i #VC byly w starej wersji blednie traktowane jako wyjatki bez kodu,
 * co przesuwalo cala rame i prowadzilo do blednego RIP/CS przy obsludze.
 */

ISR_BEZ_KODU 0      /* #DE Divide Error */
ISR_BEZ_KODU 1      /* #DB Debug */
ISR_BEZ_KODU 2      /* NMI */
ISR_BEZ_KODU 3      /* #BP Breakpoint */
ISR_BEZ_KODU 4      /* #OF Overflow */
ISR_BEZ_KODU 5      /* #BR BOUND Range Exceeded */
ISR_BEZ_KODU 6      /* #UD Invalid Opcode */
ISR_BEZ_KODU 7      /* #NM Device Not Available */
ISR_Z_KODEM   8     /* #DF Double Fault */
ISR_BEZ_KODU 9      /* legacy Coprocessor Segment Overrun */
ISR_Z_KODEM   10    /* #TS Invalid TSS */
ISR_Z_KODEM   11    /* #NP Segment Not Present */
ISR_Z_KODEM   12    /* #SS Stack-Segment Fault */
ISR_Z_KODEM   13    /* #GP General Protection */
ISR_Z_KODEM   14    /* #PF Page Fault */
ISR_BEZ_KODU 15     /* Reserved */
ISR_BEZ_KODU 16     /* #MF x87 Floating-Point */
ISR_Z_KODEM   17    /* #AC Alignment Check */
ISR_BEZ_KODU 18     /* #MC Machine Check */
ISR_BEZ_KODU 19     /* #XM SIMD Floating-Point */
ISR_BEZ_KODU 20     /* #VE Virtualization */
ISR_Z_KODEM   21    /* #CP Control Protection */
ISR_BEZ_KODU 22     /* Reserved */
ISR_BEZ_KODU 23     /* Reserved */
ISR_BEZ_KODU 24     /* Reserved */
ISR_BEZ_KODU 25     /* Reserved */
ISR_BEZ_KODU 26     /* Reserved */
ISR_BEZ_KODU 27     /* Reserved */
ISR_BEZ_KODU 28     /* #HV Hypervisor Injection */
ISR_Z_KODEM   29    /* #VC VMM Communication */
ISR_Z_KODEM   30    /* #SX Security Exception */
ISR_BEZ_KODU 31     /* Reserved */

/* =========================================================================
 * WEKTORY APIC / IRQ 32..47
 * ========================================================================= */

ISR_BEZ_KODU 32     /* Local APIC timer */
ISR_BEZ_KODU 33     /* PS/2 keyboard / IRQ1 */

ISR_BEZ_KODU 34
ISR_BEZ_KODU 35
ISR_BEZ_KODU 36
ISR_BEZ_KODU 37
ISR_BEZ_KODU 38
ISR_BEZ_KODU 39
ISR_BEZ_KODU 40
ISR_BEZ_KODU 41
ISR_BEZ_KODU 42
ISR_BEZ_KODU 43

ISR_BEZ_KODU 44     /* PS/2 mouse / IRQ12 */

ISR_BEZ_KODU 45
ISR_BEZ_KODU 46
ISR_BEZ_KODU 47     /* Local APIC spurious vector */

/* =========================================================================
 * ZAPIS / ODTWORZENIE GPR
 * ========================================================================= */

.macro PUSH_GPRS
    /*
     * Kolejnosc jest celowa: po ostatnim PUSH RSP wskazuje na R15,
     * czyli pierwszy element RejestryStanowe.
     */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro POP_GPRS
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rsi
    popq %rdi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
.endm

/* =========================================================================
 * WSPOLNE WEJSCIE ISR
 * ========================================================================= */

.type wspolne_isr_wejscie, @function
.p2align 4

wspolne_isr_wejscie:
    /*
     * Interrupt gate wyczyscil IF, ale DF jest czescia RFLAGS procesu.
     * Zachowujemy oryginalne RFLAGS na ramie sprzetowej, a przed C++
     * zerujemy DF zgodnie z System V AMD64 ABI.
     */
    PUSH_GPRS

    /*
     * Jezeli przerwanie przyszlo z Ring 3, procesor zmienil juz CS na
     * kernelowy, lecz baza GS moze nadal nalezec do procesu.
     *
     * SWAPGS przelacza na kernelowy GS. Na wyjsciu decyzja jest wykonywana
     * ponownie na podstawie CS RAMY WYBRANEJ przez scheduler, wiec poprawnie
     * obsluguje takze:
     *
     *   Ring3 -> Ring3
     *   Ring3 -> Ring0
     *   Ring0 -> Ring3 (pierwszy start procesu z PID 0)
     */
    testq $3, RAMA_OFFSET_CS(%rsp)
    jz .Lbez_swapgs_wejscie
    swapgs

.Lbez_swapgs_wejscie:
    cld

    /*
     * RDI = RejestryStanowe*.
     */
    movq %rsp, %rdi

    /*
     * Wymog ABI:
     * przed CALL RSP musi byc wyrownany do 16 bajtow.
     *
     * Przerwanie moze wystapic w dowolnej instrukcji kodu Ring 0, dlatego
     * nie wolno zakladac konkretnego ulozenia RSP. Zaokraglamy stos w dol.
     * Maksymalnie zuzywamy dodatkowe 15 bajtow, nie naruszajac zapisanej
     * ramy, bo CALL zapisuje adres powrotu ponizej niej.
     */
    andq $-16, %rsp

    call WspolnaObslugaPrzerwan

    /*
     * Dispatcher zwraca w RAX wskaznik ramy, z ktorej mamy wykonac iretq.
     * Moze to byc rama tego samego procesu albo rama wybrana przez scheduler.
     *
     * Null oznacza krytyczne uszkodzenie schedulera/dispatchera. Nie wolno
     * wykonac MOV RSP,0 ani kontynuowac do przypadkowej pamieci.
     */
    testq %rax, %rax
    jz .Lfatalny_rsp

    movq %rax, %rsp

    /*
     * Jezeli WYBRANA rama wraca do Ring 3, przed iretq przywracamy user GS.
     *
     * Dziala to rowniez przy pierwszym przejsciu PID0 Ring0 -> Ring3:
     * na wejsciu nie bylo SWAPGS, tutaj zostanie wykonany jeden SWAPGS.
     */
    testq $3, RAMA_OFFSET_CS(%rsp)
    jz .Lbez_swapgs_wyjscie
    swapgs

.Lbez_swapgs_wyjscie:
    POP_GPRS

    /*
     * Usuwamy zawsze dwa ujednolicone pola:
     *   wektor_przerwania
     *   kod_bledu
     *
     * Dla wyjatku z kodem pierwsze pochodzi od stuba, drugie od CPU.
     * Dla pozostalych oba zostaly utworzone przez stub.
     */
    addq $RAMA_ROZMIAR_NORMALIZACJI, %rsp

    /*
     * IRETQ sam rozpoznaje, czy wraca do Ring 0 czy Ring 3.
     * Przy powrocie do Ring 3 pobierze dodatkowo zapisane RSP i SS.
     */
    iretq

.Lfatalny_rsp:
    /*
     * Nie probujemy wywolywac loggera: jego stos lub przestrzen adresowa
     * mogly byc przyczyna bledu. UD2 jest pomocne w emulatorze/GDB, a HLT
     * gwarantuje zatrzymanie nawet gdy debugger kontynuuje wykonanie.
     */
    cli
    ud2

.Lfatalny_rsp_hlt:
    hlt
    jmp .Lfatalny_rsp_hlt

.size wspolne_isr_wejscie, .-wspolne_isr_wejscie

/* =========================================================================
 * TABLICA ADRESOW ISR 0..47
 * ========================================================================= */

.section .rodata
.align 8

.global tablica_isr
.type tablica_isr, @object

tablica_isr:
    .quad isr0
    .quad isr1
    .quad isr2
    .quad isr3
    .quad isr4
    .quad isr5
    .quad isr6
    .quad isr7
    .quad isr8
    .quad isr9
    .quad isr10
    .quad isr11
    .quad isr12
    .quad isr13
    .quad isr14
    .quad isr15
    .quad isr16
    .quad isr17
    .quad isr18
    .quad isr19
    .quad isr20
    .quad isr21
    .quad isr22
    .quad isr23
    .quad isr24
    .quad isr25
    .quad isr26
    .quad isr27
    .quad isr28
    .quad isr29
    .quad isr30
    .quad isr31
    .quad isr32
    .quad isr33
    .quad isr34
    .quad isr35
    .quad isr36
    .quad isr37
    .quad isr38
    .quad isr39
    .quad isr40
    .quad isr41
    .quad isr42
    .quad isr43
    .quad isr44
    .quad isr45
    .quad isr46
    .quad isr47

.size tablica_isr, .-tablica_isr

/*
 * Informacja dla linkera ELF: ten plik nie wymaga wykonywalnego stosu.
 */
.section .note.GNU-stack,"",@progbits
