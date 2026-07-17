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
    /* Instrukcja SYSCALL zmieniła CPL na 0 (Tryb Jądra) oraz wymieniła CS i SS z MSR STAR.
       Dodatkowo zapisała adres powrotny w RCX oraz RFLAGS w R11. Odtąd, RSP nadal wskazuje 
       na stos Użytkownika, co jest niebezpieczne.
    */
    
    /* 1. Odmrażamy przygotowaną przestrzeń Jądra dla stosu poprzez operację na rejestrach w tle */
    swapgs                                       /* Zamienia aktualny rejestr GS z ukrytym wskaźnikiem KernelGSBase */
    
    /* Zapiszemy stary wskaźnik użytkownika w bezpiecznym miejscu w pamięci (.data) i załadujemy rdzenny wskaźnik jądra. 
       Unikamy zapisu w %gs:-8, co mogłoby wywołać błąd stronicowania (Page Fault)!
    */
    movq %rsp, bws_user_rsp_schowek(%rip)        /* Skryj User RSP w bezpiecznej zmiennej (adresowanie RIP-relative) */
    movq %gs:0, %rsp                             /* Załaduj prawdziwy, bezpieczny stos Jądra zdefiniowany wcześniej */
    pushq bws_user_rsp_schowek(%rip)             /* Popchnij zachowany wskaźnik User RSP na nowo utworzony stos Ring 0 */

    /* 2. Zabezpiecz rejestry zepsute przez SYSCALL (RCX, R11) */
    pushq %rcx                                   /* Adres powrotu do procedury wewnątrz procesu Ring 3 */
    pushq %r11                                   /* Zamrożone RFLAGS aplikacji w Ring 3 */

    /* 3. TŁUMACZENIE KONWENCJI BWS (Bursztyn OS ABI) 
       Standard wejścia od programisty: 
       - Numer Wywołania (Kod BWS)           => w R8
       - 1. Argument                         => w R9
       - 2. Argument                         => w R10
       - 3. Argument                         => w R12
       - 4. Argument                         => w R13
       
       Standard System V AMD64 ABI, jakiego oczekuje funkcja kompilatora C++:
       - 1. Param (Numer BWS)                => w RDI
       - 2. Param (Arg 1)                    => w RSI
       - 3. Param (Arg 2)                    => w RDX
       - 4. Param (Arg 3)                    => w RCX
       - 5. Param (Arg 4)                    => w R8
    */
    movq %r8,  %rdi                              /* Przesuń Numer Funkcji (z R8 na RDI dla parametru 1) */
    movq %r9,  %rsi                              /* Przesuń Argument 1 (z R9 na RSI dla parametru 2) */
    movq %r10, %rdx                              /* Przesuń Argument 2 (z R10 na RDX dla parametru 3) */
    movq %r12, %rcx                              /* Przesuń Argument 3 (z R12 na RCX dla parametru 4) */
    movq %r13, %r8                               /* Przesuń Argument 4 (z R13 na użyty przed chwilą R8 jako argument 5) */

    /* Utrwalenie środowiska (Zachowaj użyte rejestry, przed wywołaniem obcego kodu w C++) */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* KRYTYCZNE: Zgodnie z wytycznymi ABI, należy wyzerować flagę kierunku przed skokiem do C++! */
    cld

    /* 4. Rozwiń skrzydła, wywołując docelowy parser systemu */
    .extern obsluga_wywolan_systemowych
    call obsluga_wywolan_systemowych
    
    /* Po udanej operacji C++, wynik leży zmagazynowany w rejestrze RAX i zostanie tam do samego IRET/SYSRET, by Ring 3 odczytał z niego status */

    /* 5. Czas ewakuacji i przebudowy stanu, by opuścić strefę Jądra */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx

    /* Odtwórz rejestry zmielone na potrzeby SYSCALL, aby umożliwić procesorowi wejście wg tych adresów */
    popq %r11                                    /* Przywróć RFLAGS z powrotem do R11, by CPU mógł je załadować w Ring 3 */
    popq %rcx                                    /* Przywróć docelowy wektor wyjścia procesu do RCX, by SYSRET znalazł drogę w locie */

    /* Zdejmujemy ukryty RSP Użytkownika i uciekamy ze spadochronem (Cofnięcie zmiany stosu) */
    popq %rsp
    swapgs                                       /* Przywróć GSBase Użytkownika, chroniąc na powrót stos i offset KernelGS */

    /* 6. Komenda skokowa wyjścia bez zapowiedzi do Ring 3 */
    sysretq