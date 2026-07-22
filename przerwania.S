/*
 * Mechanizm: Zewnętrzne interfejsy i sterowanie wyjątkami Asembler x86_64
 * Opis: Pełni rolę nośną, tworzy skrypty ładujące tablice do rejestrów w procesorze
 * oraz implementuje szkielet makr dla obsługi wejść z poszczególnych wektorów wyjątków.
 */

.global zaladuj_zaktualizowane_gdt
.global zaladuj_zaktualizowane_idt
.global tablica_isr

.section .text
.code64

/*
 * Ładowanie wskaźnika tablicy Deskryptorów globalnych (GDT).
 * System V AMD64 ABI przekazuje pierwszy argument (czyli `adres_gdtr`) z funkcji w C++ do RDI.
 */
zaladuj_zaktualizowane_gdt:
    lgdt (%rdi)                                /* Załaduj specjalny wektor z wykorzystaniem zarezerwowanego adresu RDI */

    /* Wymuś wymianę w segmencie Danych Jądra poprzez przesunięcie 0x10 */
    mov $0x10, %ax
    mov %ax, %ds                               /* Rejestr Data Segment (DS) */
    mov %ax, %es                               /* Rejestr Extra Segment (ES) */
    mov %ax, %fs                               /* Rejestr przestrzeni specjalnej FS */
    mov %ax, %gs                               /* Rejestr przestrzeni specjalnej GS */
    mov %ax, %ss                               /* Rejestr wskaźnika docelowego Stack Segment (SS) */

    /* * Aby zaktualizować Segment Kodu Jądra CS używamy "Far Jump" / Return na 64 bity (iretq albo retfq).
     * Odłóż wymagany Selektor Kodu (0x08) i spreparowany w C++ adres na stos 
     * zanim wymusisz zsynchronizowanie rejestru skokiem LRETQ.
     */
    pushq $0x08                                /* Pchamy selektor Kodu Jądra */
    lea _przeladuj_cs(%rip), %rax              /* Generujemy przesunięcie etykiety z ominięciem RIP pod bezpieczny wskaźnik do RAX */
    pushq %rax                                 /* Pchamy wygenerowany adres skoku powrotnego jako wskaźnik docelowy powrotu */
    lretq                                      /* Daleki powrót procesora konsumujący selektor oraz RIP nadpisując starą tablicę */

_przeladuj_cs:
    ret                                        /* System poprawnie przebrnął i ładuje powrót z powrotem na główną ścieżkę jądra C++ */

/*
 * Ładowanie wskaźnika tablicy Deskryptorów z Przerwaniami IDT.
 */
zaladuj_zaktualizowane_idt:
    lidt (%rdi)                                /* Wykorzystaj wbudowaną instrukcję w rdzeń procesora w celu załadowania zlokalizowanego pakietu */
    ret

/*
 * -------------------------------------------------------------
 * MACRA BUDUJĄCE Procedury Obsługi Błędów (ISR Stubs)
 * Generują unikalne wektory zrzucające odpowiedni numer w stosie (i ewentualnie 0 dla błędu "BezKoduBledu").
 * Zawsze odwołują się do głównego wektora analizy `wspolne_isr_wejscie`.
 * -------------------------------------------------------------
 */
.macro ISR_BEZKODUBLEDU nr
.global isr\nr
isr\nr:
    pushq $0                                   /* Odłóż symboliczne 0 jako zapchajdziurę by struktura w C++ pokrywała się wymiarami dla wyjątków bez kodu błędu */
    pushq $\nr                                 /* Odłóż informację o numerze wywołanego wektora by C++ wiedział z jakim wyjątkiem ma do czynienia */
    jmp wspolne_isr_wejscie                    /* Zawsze przekieruj bieg akcji z zachowaniem stosu do wspólnej instrukcji przygotowawczej Push-All */
.endm

.macro ISR_Z_KODEMBLEDU nr
.global isr\nr
isr\nr:
    /* Procesor intelowsko już sam odłożył prawdziwy 8 Bajtowy Kod Błędu na stosie */
    pushq $\nr                                 /* My dorzucamy wyłącznie unikalny dla tego wyjątku numer identyfikacyjny wektora */
    jmp wspolne_isr_wejscie                    /* Powrót w ramiona Push-All */
.endm

/* Zadeklarowanie poszczególnych systemowych procedur procesora (0-31 to zarezerwowane błędy, 32-255 to przerwania hardware/software, np LAPIC Timer) */
ISR_BEZKODUBLEDU 0  /* Błąd operacji dzielenia przez zero (Działający test zadania w C++) */
ISR_BEZKODUBLEDU 1  /* Wyjątek operacji debugowania i Trace */
ISR_BEZKODUBLEDU 2  /* Systemowy niemaskowalny NMI (Non Maskable Interrupt) */
ISR_BEZKODUBLEDU 3  /* Operacja pułapki czasowej (Breakpoint INT 3) */
ISR_BEZKODUBLEDU 4  /* Ostrzeżenie na temat przepełnienia stosu na szynie Overflow (INTO) */
ISR_BEZKODUBLEDU 5  /* Złamanie granicy zasięgu alokowanej puli operacji tablicowych */
ISR_BEZKODUBLEDU 6  /* Invalid Opcode - Najbardziej morderczy przypadek podania błędnego polecenia kompilatorowi! */
ISR_BEZKODUBLEDU 7  /* Wymóg emulacji jednostki zmiennoprzecinkowej z koprocesora FPU */
ISR_Z_KODEMBLEDU 8  /* Przerażający Double Fault (Błąd podczas próby obsługi błędu...) Cudem uratowany przez wektor IST */
ISR_BEZKODUBLEDU 9  /* Koprocesor złamał zasady chronione przy generowaniu segmentów (Przestarzałe zjawisko) */
ISR_Z_KODEMBLEDU 10 /* TSS, tablica wyboru procesów pożarła przydział (Bad TSS) */
ISR_Z_KODEMBLEDU 11 /* Pamięć Segmentacji nieobecna po załadowaniu na flagę */
ISR_Z_KODEMBLEDU 12 /* Błąd przydziału stosu lokalnego chronionego Stack-Segment Fault */
ISR_Z_KODEMBLEDU 13 /* General Protection Fault - Błąd segmentacji i uprawnień we wczesnych środowiskach bez VMM! */
ISR_Z_KODEMBLEDU 14 /* Słynny Błąd Stronicowania (Page Fault), po przydziale i odczytaniu nieistniejących lub pustych wskaźników */
ISR_BEZKODUBLEDU 15 /* Wolny odział awaryjny procesora (Zastrzeżony przez Intel/AMD) */
ISR_BEZKODUBLEDU 16 /* Exception związany z matematycznym działaniem Koprocesora x87 FPU (Kropka Zmiennoprzecinkowa) */
ISR_Z_KODEMBLEDU 17 /* Wektor wyrównania (Alignment Check) uległ kompresji bez zapowiedzi w 4 poziomie bazy (Rzadkie) */
ISR_BEZKODUBLEDU 18 /* Zastrzeżony mechanizm wyjątku testów sprawdzających platformy sprzętowej (Machine Check, zazwyczaj sprzęt pada) */
ISR_BEZKODUBLEDU 19 /* Przerwano działanie specjalnej puli rzutowania SIMD w środowiskach Streaming extensions x86_64 */
ISR_BEZKODUBLEDU 20 /* Awaria / Ostrzeżenie na mechanizmie procesowym zagnieżdżania trybów sprzętowych Virtualization VMM (Tryb VMX) */

/* Odcinki puste, zarezerwowane oficjalnie by zajmować przestrzeń (od 21 do 31) */
ISR_BEZKODUBLEDU 21
ISR_BEZKODUBLEDU 22
ISR_BEZKODUBLEDU 23
ISR_BEZKODUBLEDU 24
ISR_BEZKODUBLEDU 25
ISR_BEZKODUBLEDU 26
ISR_BEZKODUBLEDU 27
ISR_BEZKODUBLEDU 28
ISR_BEZKODUBLEDU 29
ISR_Z_KODEMBLEDU 30 /* Zabezpieczony wyjątek związany z incydentami Security Extensions. Kody zawsze zwracają wektor błędu. */
ISR_BEZKODUBLEDU 31 /* Koniec chronionej bramy. */

/* LAPIC Timer (zaprogramowany przez nas w apic.cpp aby wywołać wektor np. 32) */
ISR_BEZKODUBLEDU 32

/* Inne wolne dla sprzętu np APIC (od 33 do 47 zarezerwowane na przyszłe testy) */
ISR_BEZKODUBLEDU 33
ISR_BEZKODUBLEDU 34
ISR_BEZKODUBLEDU 35
ISR_BEZKODUBLEDU 36
ISR_BEZKODUBLEDU 37
ISR_BEZKODUBLEDU 38
ISR_BEZKODUBLEDU 39
ISR_BEZKODUBLEDU 40
ISR_BEZKODUBLEDU 41
ISR_BEZKODUBLEDU 42
ISR_BEZKODUBLEDU 43
ISR_BEZKODUBLEDU 44
ISR_BEZKODUBLEDU 45
ISR_BEZKODUBLEDU 46
ISR_BEZKODUBLEDU 47

/*
 * PUSH-ALL (Zrzucenie pełnego środowiska do stosu by ochronić stan przed utratą z powodu obsługi wyjątku w C++)
 */
wspolne_isr_wejscie:
    /* Każdy makro wiersz powyżej dorzucił kod + i numer błędu do stosu (Zgodnie z ułożeniem struct RejestryStanowe). Pchamy resztę w tył. */
    pushq %rax                                 /* Przestrzeń głównego roboczego EAX wyrzutni systemu */
    pushq %rbx                                 /* Akcelerator pamięci podręcznej rejestru danych systemu */
    pushq %rcx                                 /* Wskaźnik pętlowy iteratorów RCX */
    pushq %rdx                                 /* Wskźanik bazowy połówki danych wejścia I/O RDX */
    pushq %rdi                                 /* Pamięciowy offset danych odbiorczych */
    pushq %rsi                                 /* Pamięciowy offset danych nadawczych na źródle strumieni RDI RSI */
    pushq %rbp                                 /* Głębinowy pod-wskaźnik ramki w odzyskiwaniu Base Pointer stosu */
    
    /* Głębsze wejścia procesora z nowości systemu x86_64, generalne rejestry z R prefixem */
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Oczekujemy na zwrot wskaźnika w obróbce System V AMD64 do RDI. To nasz "struct RejestryStanowe* r" na wlocie C++! */
    movq %rsp, %rdi                            

    /* KRYTYCZNE: Zgodnie z wytycznymi ABI, należy wyzerować flagę kierunku (Direction Flag) przed skokiem do C++! */
    cld

    /* Rozpoczynamy procedurę C++ z nowym, wyjętym środowiskiem w C++ bez obaw o stan starego kodu */
    .extern WspolnaObslugaPrzerwan
    call WspolnaObslugaPrzerwan

    /* Po udanym zakończeniu operacji zdejmij wszystko używając procedur od tyłu (POP-ALL) */
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

    /* Wyzeruj z pamięci wygenerowany nr wyjątku i ewentualny nadpisywany kod błędu (łącznie 16 bajtów) bez destrukcji */
    addq $16, %rsp

    /* Opuść wektor procedury przywracając ostateczny RIP i powracając z dalekiej zaświatówki przez specyficzny Return po Interrupt Gate */
    iretq

/* * Zadeklarowanie tabeli macierzowej zbierającej wskaźniki na poszczególne pakiety 
 * dla skryptu C++ który poprosi o jej załadowanie do GDT pod numerami (Indeksy w tabeli wskaźników 0-47)
 */
.section .data
.align 8
tablica_isr:
    .quad isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9
    .quad isr10, isr11, isr12, isr13, isr14, isr15, isr16, isr17, isr18, isr19
    .quad isr20, isr21, isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29
    .quad isr30, isr31, isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    .quad isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
    