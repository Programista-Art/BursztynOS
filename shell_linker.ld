ENTRY(_start)

SECTIONS
{
    /* Przestrzeń adresowa użytkownika zaczyna się od 4MB */
    . = 0x400000;
    
    /* 1. Nagłówek Bursztyn OS (Zabezpieczony dyrektywą KEEP) */
    .naglowek : {
        KEEP(*(.naglowek))
        . = ALIGN(4096);
    }

    /* 2. Kod programu */
    .text : {
        KEEP(*(.text*))
        . = ALIGN(4096);
    }

    /* 3. Zmienne globalne i stringi */
    .data : {
        KEEP(*(.rodata*))
        KEEP(*(.data*))
        KEEP(*(.bss*))
        KEEP(*(COMMON))
        . = ALIGN(4096);
    }
}