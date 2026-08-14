ENTRY(_start)

SECTIONS
{
    . = 0x600000;
    
    .naglowek : { 
        KEEP(*(.naglowek)) 
    }
    
    . = 0x601000;
    .text : { 
        *(.text*) 
        *(.rodata*)
        *(.rodata.*)
        *(.eh_frame*)
    }

    /* Naglowek BUR rezerwuje 32 KiB na kod. Dane zaczynaja sie
       w pliku od 0x9000 i w pamieci pod 0x609000. */
    ASSERT(SIZEOF(.text) <= 0x8000, "Segment kodu aplikacji przekracza 32 KiB")
    . = 0x609000;
    .data : { 
        *(.data*) 
        *(.data.*)
        *(.data.rel.ro*)
    }
    
    . = ALIGN(16);
    .bss : { 
        *(.bss*) 
        *(COMMON) 
    }
    ASSERT((ADDR(.bss) + SIZEOF(.bss) - 0x609000) <= 0x20000,
           "Segment danych aplikacji przekracza 128 KiB")
}
