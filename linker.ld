/*
 * Mechanizm: Skrypt Linkera dla Architektury x86_64 z symbolami dla PMM
 * Opis: Ustawia odpowiednie sekcje oprogramowania z naciskiem na poprawne
 * wyrównanie nagłówka Multiboot2, co jest kluczowe, aby GRUB wykrył obraz jądra.
 * Dodatkowo eksportuje symbole __kernel_start oraz __kernel_end dla PMM
 * oraz zapewnia wsparcie dla globalnych konstruktorów C++.
 */

ENTRY(_start)

SECTIONS
{
    /* Zgodnie z tradycją i wymogami, wyższy kod jądra załadowany będzie od fizycznego adresu 1 MB */
    . = 1M;
    __kernel_start = .;

    /* Sekcja startowa bootloadera. GRUB poszukuje sygnatury w początkowych kilobajtach, dlatego MUSI być pierwsza */
    .boot : ALIGN(4K)
    {
        KEEP(*(.multiboot))
    }

    /* Główna sekcja z kodem wykonywalnym jądra. Maska .* łapie zoptymalizowane podsekcje C++ */
    .text : ALIGN(4K)
    {
        *(.text .text.*)
    }

    /* Sekcja zmiennych oraz stałych, które posiadają zadeklarowaną zawartość (tylko do odczytu) */
    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    /* Sekcja zmiennych oraz struktur zainicjowanych danymi modyfikowalnymi (do odczytu i zapisu) */
    .data : ALIGN(4K)
    {
        /* Zbieranie globalnych konstruktorów C++ dla środowiska obiektowego */
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE(__init_array_end = .);

        *(.data .data.*)
    }

    /* Sekcja bss zadeklarowana na niezainicjowane zmienne (wypełniana zerami przy starcie) i przestrzeń na stos */
    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /* Symbol oznaczający fizyczny koniec jądra w pamięci RAM (Po wyrównaniu do rozmiaru strony 4K) */
    . = ALIGN(4K);
    __kernel_end = .;
}