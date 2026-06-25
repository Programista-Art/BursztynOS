/*
 * Mechanizm: Skrypt Linkera dla Architektury x86_64 z symbolami dla PMM
 * Opis: Ustawia odpowiednie sekcje oprogramowania z naciskiem na poprawne
 * wyrównanie nagłówka Multiboot2, co jest kluczowe, aby GRUB wykrył obraz jądra.
 * Dodatkowo eksportuje symbole __kernel_start oraz __kernel_end dla PMM.
 */

/* Punkt startowy (entry) dla bootloadera to etykieta _start zdefiniowana w boot.S */
ENTRY(_start)

SECTIONS
{
    /* Zgodnie z tradycją i wymogami, wyższy kod jądra załadowany będzie od fizycznego adresu 1 MB */
    . = 1M;
    
    /* Symbol oznaczający fizyczny początek jądra w pamięci RAM */
    __kernel_start = .;

    /* Sekcja startowa bootloadera. GRUB poszukuje sygnatury w początkowych kilobajtach, dlatego MUSI być pierwsza */
    .boot : ALIGN(4K)
    {
        /* Instrukcja KEEP chroni sekcję .multiboot przed usunięciem (tzw. "garbage collection") przez linker */
        KEEP(*(.multiboot))
    }

    /* Główna sekcja z kodem wykonywalnym jądra */
    .text : ALIGN(4K)
    {
        *(.text)
    }

    /* Sekcja zmiennych oraz stałych, które posiadają zadeklarowaną zawartość (tylko do odczytu) */
    .rodata : ALIGN(4K)
    {
        *(.rodata)
    }

    /* Sekcja zmiennych oraz struktur zainicjowanych danymi modyfikowalnymi (do odczytu i zapisu) */
    .data : ALIGN(4K)
    {
        *(.data)
    }

    /* Sekcja bss zadeklarowana na niezainicjowane zmienne (wypełniana zerami przy starcie) i przestrzeń na stos */
    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    /* Symbol oznaczający fizyczny koniec jądra w pamięci RAM (Po wyrównaniu do rozmiaru strony 4K) */
    . = ALIGN(4K);
    __kernel_end = .;
}
