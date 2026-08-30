ENTRY(_start)

SECTIONS
{
    /* Nagłówek BUR musi być na samym początku pliku! */
    . = 0x0;
    .naglowek : {
        KEEP(*(.naglowek))
    }

    /* Sekcja kodu aplikacji */
    . = 0x1001000;
    .text : ALIGN(4K) {
        *(.text)
    }

    /* Sekcja danych aplikacji */
    . = 0x1009000;
    .data : ALIGN(4K) {
        *(.data)
        *(.rodata*)
    }

    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }
}