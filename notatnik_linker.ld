ENTRY(_start)

SECTIONS
{
    /* 1. Ustawienie wirtualnego adresu bazowego na 0x600000 */
    . = 0x600000;

    /* Nagłówek - ląduje na samym początku pliku (offset 0) */
    .naglowek : {
        KEEP(*(.naglowek))
    }

    /* 2. Przesunięcie w pamięci do 0x601000. 
       W pliku fizycznym .bin objcopy wygeneruje offset 4096 (0x1000) */
    . = 0x601000;
    .text : {
        *(.text)
        *(.rodata*)
    }

    /* 3. Przesunięcie do 0x605000 (w pliku .bin offset wyniesie 20480) */
    . = 0x605000;
    .data : {
        *(.data)
    }
    
    .bss : {
        *(.bss)
        *(COMMON)
    }
}