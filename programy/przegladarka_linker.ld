ENTRY(_start)
SECTIONS
{
. = 0x600000;
.naglowek : { *(.naglowek) }

. = 0x601000;
.text : { 
    *(.text*) 
    *(.rodata*)
    *(.eh_frame*)
}
ASSERT(SIZEOF(.text) <= 0xF000, "Segment kodu przegladarki przekracza 60 KiB")

. = 0x610000; 
.data : { *(.data*) }
.bss : { 
    *(.bss*) 
    *(COMMON) 
}
ASSERT((ADDR(.bss) + SIZEOF(.bss) - 0x610000) <= 0x210000,
       "Segment danych przegladarki przekracza 2112 KiB")


}
