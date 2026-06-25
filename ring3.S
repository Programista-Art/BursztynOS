/*
 * Mechanizm: Ręczne przełączenie powrotu kontekstu do Ring 3
 * Opis: Używa mechaniki IRETQ do oszukania stosu tak, jakby procesor wracał
 * ze sprzętowego przerwania Ring 3, w którym nigdy de facto nie byliśmy! Wkłada
 * fałszywe flagi ochronne by uziemić kod w niskim pierścieniu uprawnień użytkownika.
 */

.global przejdz_do_ring3
.section .text
.code64

/*
 * void przejdz_do_ring3(void* adres_kodu, void* adres_stosu)
 * Zgodnie z ABI, adres docelowy w C++ ląduje w RDI, a wskazanie przydzielonego stosu procesu w RSI.
 */
przejdz_do_ring3:
    /* Przerwania na czas ładowania nowego wózka ratunkowego muszą zostać zablokowane by ochronić stack jądra */
    cli
    
    /* 
     * Aby wykonać IRETQ wymagane jest ułożenie stosu w rygorystyczny sposób (5 elementowy):
     * SS (Stack Segment) -> RSP (Wskaźnik stosu) -> RFLAGS -> CS (Code Segment) -> RIP (Instruction Pointer).
     */
    
    /* 1. SS (Stack Segment User Data) */
    /* Zgodnie z konwencją GDT Bursztyn OS z etapu 2 i logiką Fast Syscall, User Data to wpis 4 (0x20). 
       Zawsze dodajemy +3 (co robi OR z flagą RPL=3) by poinformować procesor, że segment ten ma przywileje Ring 3. */
    pushq $0x23                                  /* Zrzucamy 0x20 (User Data) + 3 (Ring) */

    /* 2. RSP (User Stack Pointer) */
    /* Przydzielony docelowemu programowi Ring 3 stos wciągnięto jako argument funkcji w RSI */
    pushq %rsi                                   /* Zrzucamy Wskaźnik */

    /* 3. RFLAGS */
    /* Należy włączyć tu przerwania z powrotem. Bit nr 9 to IF (Interrupt Enable Flag) 0x200 */
    pushf                                        /* Zrzuć obecne RFLAGS rdzennego wirtualizatora by zachować integralność architekturalną */
    popq %rax                                    /* Podejmij go z powrotem lecz przemieść na stół operacyjny (RAX) */
    orq $0x200, %rax                             /* Doklej flagę przerwań sprzętowych. Inaczej Ring 3 padłby pod powłoką zamrożenia układu i powiadomień. */
    pushq %rax                                   /* Skryj nowo zmodyfikowaną flagę sprzętową znów w ramce wyjściowej */

    /* 4. CS (Code Segment User Code) */
    /* Wedle Bursztyn OS (i naszego ustawienia GDT), User Code to wpis 3 (0x18). Dodajemy RPL=3. */
    pushq $0x1B                                  /* Zrzucamy 0x18 (User Code) + 3 (Ring) */

    /* 5. RIP (Instruction Pointer) */
    /* Przesłany w RDI jako pierwszy argument */
    pushq %rdi                                   /* Odkładamy jako ostanie zwieńczenie punkt docelowy powrotu procedury pod IRET! */

    /* Fałszywy wektor został w pełni i bezstratnie ułożony w obrębie 5 zmiennych sprzętowych... */

    /* Zmiana rejestrów wspierających dla przestrzeni użytkownika (Mimo braku pełnego wymogu w 64-bitach, lepiej chronić pule) */
    movw $0x23, %ax
    movw %ax, %ds                                /* Podpinamy nowy zestaw segmentu na pule Data Usera */
    movw %ax, %es                                /* Kopia dla ES */
    movw %ax, %fs                                /* Kopia dla rzutnika FS */
    movw %ax, %gs                                /* Kopia docelowego zagnieżdżenia percesowego GS */

    /* Procesor poprawnie usunie zdefiniowane wpisy na stosie przywracając oznaczony kontekst do pierścienia 3, i opuści kernel */
    iretq
