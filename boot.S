/* Mechanizm: Nagłówek Multiboot2, Identity Paging oraz Long Mode (64-bit)
   Opis: Bootloader ładuje nas w 32-bitowym trybie chronionym (Protected Mode).
   Naszym zadaniem jest poprawne wyczyszczenie środowiska, konfiguracja
   podstawowych, 4-poziomowych tablic stron mapujących pierwsze gigabajty 1:1,
   przełączenie procesora w tryb Long Mode i skok do funkcji kernel_main() w C++.
*/

/* Zmienne pomocnicze dla nagłówka Multiboot2 */
.set MULTIBOOT2_HEADER_MAGIC, 0xE85250D6
.set GRUB_MULTIBOOT_ARCHITECTURE_I386, 0 /* Pomimo kodu x86_64 GRUB używa I386 na wczesnym etapie */
.set MULTIBOOT_HEADER_TAG_END, 0

/* Flagi tagów informacyjnych Multiboot2 - prosimy o Memory Map (tag 6) */
.set MULTIBOOT_TAG_TYPE_MEMORY_MAP, 6

.section .multiboot
.align 8
/* Początek nagłówka Multiboot2 */
header_start:
    .long MULTIBOOT2_HEADER_MAGIC                                /* Sygnatura (Magiczna liczba) Multiboot2 */
    .long GRUB_MULTIBOOT_ARCHITECTURE_I386                       /* Architektura 32-bit (x86) przekazywana przez GRUB */
    .long header_end - header_start                              /* Obliczenie całkowitego rozmiaru nagłówka */
    /* Checksum (Suma kontrolna musi równać się 0 po dodaniu tych trzech wartości w u2) */
    .long -(MULTIBOOT2_HEADER_MAGIC + GRUB_MULTIBOOT_ARCHITECTURE_I386 + (header_end - header_start))

    /* Znacznik z prośbą o przekazanie tagów informacyjnych, tutaj Memory Map */
    .align 8
    .short 1                                                     /* Typ 1: Information request */
    .short 0                                                     /* Flagi */
    .long 12                                                     /* Rozmiar taga */
    .long MULTIBOOT_TAG_TYPE_MEMORY_MAP

    /* Wymagany przez specyfikację znacznik oznaczający koniec tagów Multiboot */
    .align 8
    .short MULTIBOOT_HEADER_TAG_END                              /* Numer typu dla taga zakończenia (0) */
    .short 0                                                     /* Flagi taga (brak flag) */
    .long 8                                                      /* Rozmiar taga (8 bajtów) */
header_end:
/* Koniec nagłówka Multiboot2 */

.section .bss
.align 4096
/* Pre-alokacja początkowej przestrzeni dla 4-poziomowych tablic stron (Paging) */
.global pml4_table /* Udostępnienie zmiennej, wymagane później w apic.cpp (LAPIC) */
.global pd_table
pml4_table:
    .skip 4096                                                   /* Tablica poziomu 4 (PML4) -> 4 KB przestrzeni */
pdp_table:
    .skip 4096                                                   /* Tablica poziomu 3 (PDP) -> 4 KB przestrzeni */
pd_table:
    .skip 4096                                                   /* Tablica poziomu 2 (PD) mapująca 2MB strony -> 4 KB przestrzeni */

/* Stos systemowy */
.align 16
.global stack_top                                                /* Udostępnienie wskaźnika szczytu stosu dla kodu w C++ (syscalls.cpp) */
stack_bottom:
    .skip 16384                                                  /* Zarezerwuj 16 KB (16384 bajty) stosu dla wczesnego jądra */
stack_top:

.section .rodata
.align 8
/* Tymczasowa Tablica Global Descriptor Table (GDT) wymagana w środowisku 64-bit do wejścia w Long Mode (Docelowa będzie w gdt.cpp) */
gdt64:
    .quad 0x0000000000000000                                     /* Wpis nr 0: Zero, tzw. Null Descriptor, wymagany przez procesor */
gdt64_code:
    /* Wpis nr 1: Kod jądra 64-bit Ring 0. Zawiera flagę wykonywalności i 64-bitowy bit długi (L) */
    .quad 0x00209A0000000000
gdt64_data:
    /* Wpis nr 2: Dane jądra 64-bit Ring 0. Zawiera flagę zapisu (RW) */
    .quad 0x0000920000000000
gdt64_pointer:
    .word gdt64_pointer - gdt64 - 1                              /* Limit: Obliczony rozmiar tablicy minus 1 */
    .quad gdt64                                                  /* Adres startowy tablicy GDT */

.section .text
.code32
.global _start
/* Punkt wejścia uruchamiany przez bootloader. Jesteśmy w trybie 32-bit Protected Mode */
_start:
    /* Zaczynamy od ustawienia przestrzeni stosu używanej do wykonywania push/pop */
    movl $stack_top, %esp                                        /* Ustaw adres szczytu stosu (stack_top) do rejestru ESP */

    /* Zapisz parametry Multiboot (EAX, EBX) do bezpiecznych rejestrów przed przejściem trybów (np EDI, ESI)
       ponieważ 32-bitowy stos będzie nadpisany/trudny w odzyskaniu przy pop%rax */
    movl %eax, %edi                                              /* Zapisz magic number do EDI */
    movl %ebx, %esi                                              /* Zapisz strukturę informacji sprzętowej do ESI */

    /* * KONFIGURACJA STRONICOWANIA IDENTITY PAGING
     * Ustawiamy pierwsze wpisy każdej tabeli tak, aby 1. wpis w PML4 wskazywał na PDP,
     * PDP wskazywało na PD, a wpis w PD mapował pierwszą 2MB stronę fizyczną (Virtual = Physical).
     */
    
    /* 1. Ustaw wpis w PML4 Table wskazujący na bazę PDP Table */
    movl $pdp_table, %eax                                        /* Załaduj bazowy adres tablicy PDP do rejestru EAX */
    orl $0b11, %eax                                              /* Skonfiguruj flagi wpisu do OR: Obecna (bit 0=1) i Zapisywalna (bit 1=1) */
    movl %eax, pml4_table                                        /* Zapisz spreparowany wskaźnik do pierwszego (indeks 0) wpisu PML4 */

    /* 2. Ustaw wpis w PDP Table wskazujący na bazę PD Table */
    movl $pd_table, %eax                                         /* Załaduj adres bazowy PD do rejestru EAX */
    orl $0b11, %eax                                              /* Zastosuj te same flagi: Obecna oraz Zapisywalna */
    movl %eax, pdp_table                                         /* Zapisz wynik pod pierwszy (indeks 0) wpis PDP */

    /* 3. Ustaw wpis w PD Table ładujący Huge Page (Stronę 2 Megabajtową) na 0x0 fizycznej pamięci */
    movl $0x0, %eax                                              /* Początkowy mapowany adres fizyczny to całkowite '0' (0x0) */
    orl $0b10000011, %eax                                        /* Flagi: Obecna (1), Zapisywalna (1), Strona 2MB PS bit (1 na 7 bicie) */
    movl %eax, pd_table                                          /* Skieruj zmapowaną stronę pod pierwszy (indeks 0) wpis PD */

    /* PRZYGOTOWANIE DO PRZEŁĄCZENIA LONG MODE (64-BIT) */

    /* Przypisz strukturę tablicy stron (PML4) do rejestru głównego stronicowania (CR3) */
    movl $pml4_table, %eax                                       /* Pobierz adres wiodącego węzła (PML4) z pamięci */
    movl %eax, %cr3                                              /* Zapisz go w sprzętowym rejestrze sterującym mapą pamięci (CR3) */

    /* Włącz Physical Address Extension (PAE) niezbędne do pracy Long Mode */
    movl %cr4, %eax                                              /* Odczytaj aktualny stan rejestru CR4 do EAX */
    orl $0b100000, %eax                                          /* Włącz (wymuś za pomocą OR) bit nr 5, co oznacza tryb PAE */
    movl %eax, %cr4                                              /* Zapisz zmieniony stan rejestru na powrót do procesora (CR4) */

    /* Ustaw flagę EFER.LME (Long Mode Enable) zezwalającą na instrukcje 64-bitowe */
    movl $0xC0000080, %ecx                                       /* Kod Extended Feature Enable Register do operacji odczytu w MSR */
    rdmsr                                                        /* Odczytaj jego zawartość sprzętową prosto do pary EAX:EDX */
    orl $0x100, %eax                                             /* Zmodyfikuj EAX dodając bit 8 stanowiący LME (Long Mode Enable) */
    wrmsr                                                        /* Zapisz ponownie flagi konfiguracyjne do urządzenia (MSR) */

    /* Ostateczne włączenie modułu zrzutów pamięci wirtualnej (Paging w rejestrze CR0) */
    movl %cr0, %eax                                              /* Pobierz stan procesora ze specjalnego rejestru sterowania CR0 */
    orl $0x80000000, %eax                                        /* Dodaj do jego wartości bit 31, Paging Enable (PE) */
    movl %eax, %cr0                                              /* Od tego momentu instrukcje traktują adresy liniowe jako wirtualne! */

    /* Ładowanie skompilowanej struktury GDT */
    lgdt gdt64_pointer                                           /* Załaduj wskaźnik na architekturę segmentacji 64-bit z dyrektywy wejścia */

    /* Far Jump. Skaczemy, aktualizując Selektor Kodu (CS) na 0x08 co wprowadza nas faktycznie z 32-bit w natywny tryb 64-bit */
    ljmp $0x08, $long_mode_start                                 /* CS = 0x08 to nasz gdt64_code zdefiniowany powyżej (Indeks 1 * 8 bitów) */

.code64
/* Odtąd operujemy pełnymi 64-bitowymi instrukcjami w oparciu o zestaw AMD64/Intel64 */
long_mode_start:
    /* Zaktualizuj rejestry dla strefy danych, na wartości dla 64-bit (0x10) */
    movw $0x10, %ax                                              /* Selektor gdt64_data to 0x10 (Indeks 2 * 8 bitów), wczytany do AX */
    movw %ax, %ds                                                /* Ustaw Data Segment (DS) na nową flagę */
    movw %ax, %es                                                /* Ustaw Extra Segment (ES) */
    movw %ax, %fs                                                /* Ustaw rejestr FS */
    movw %ax, %gs                                                /* Ustaw rejestr GS */
    movw %ax, %ss                                                /* Ustaw segment bazowy stosu (SS) */

    /* Odtwórz pełen zakres szczytowy w 64-bit wskaźniku stosu */
    movq $stack_top, %rsp                                        /* Ustaw wskaźnik RSP na szczyt nowo zdefiniowanego stosu */

    /* W tym momencie w RDI mamy rozszerzony z EDI magic number, a w RSI strukturę z ESI */
    /* Dokładnie tam, gdzie System V AMD64 ABI oczekuje pierwszego i drugiego argumentu funkcji! */

    /* Wykonaj ostateczne przekierowanie egzekucji na funkcję docelową zdefiniowaną w plikach C/C++ */
    .extern kernel_main                                          /* Deklaracja by linker rozwiązał adres na docelowej liście C++ */
    call kernel_main                                             /* Wywołaj funkcję wejściową kernel_main w kodzie jądra (C++) */

    /* W niespotykanym przypadku powrotu z kernel_main do strefy Assembly, usypiaj ciągle jednostkę w pętli C-Halt */
hlt_loop:
    cli                                                          /* Wyłączenie przerwań (Clear Interrupt Flag) zamyka okno dla zewnętrznych sygnałów */
    hlt                                                          /* Uśpij procesor do odwołania redukując użycie prądu (Halt) */
    jmp hlt_loop                                                 /* Pętla wraca odgórnie (Jump) upewniając się, że nie wyjdziemy za etykietę */