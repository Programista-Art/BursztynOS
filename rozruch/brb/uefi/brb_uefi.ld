/*
 * BRB - skrypt linkera obrazu PE32+ aplikacji UEFI (GNU ld 2.42, emulacja i386pep, wejscie: obiekty ELF64).
 * Wlasny, minimalny skrypt (nie kopia domyslnego skryptu ld). Wynik:
 *
 *   .text   kod                      CODE | EXECUTE | READ
 *   .rdata  stale + .data.rel.ro     INITIALIZED_DATA | READ   (wskazniki relokowane przez firmware PRZED
 *                                                              ewentualnym ustawieniem ochrony stron)
 *   .data   dane zapisywalne         INITIALIZED_DATA | READ | WRITE
 *   .bss    dane zerowane            UNINITIALIZED_DATA | READ | WRITE (pomijana, gdy pusta)
 *   .reloc  relokacje bazowe         tworzone przez ld (--enable-reloc-section), DISCARDABLE
 *
 * Brak sekcji jednoczesnie zapisywalnych i wykonywalnych. Sekcje bez znaczenia w czasie wykonania
 * (komentarze kompilatora, notatki ELF, unwind, import/eksport) sa odrzucane - BRB nie ma importow.
 * Wyrownanie sekcji (__section_alignment__) i pliku podaje Makefile: 0x1000 / 0x200.
 */
SECTIONS
{
    . = SIZEOF_HEADERS;
    . = ALIGN(__section_alignment__);

    .text __image_base__ + __section_alignment__ :
    {
        *(.text)
        *(.text.*)
    }

    .rdata BLOCK(__section_alignment__) :
    {
        *(.rodata)
        *(.rodata.*)
        *(.data.rel.ro)
        *(.data.rel.ro.*)
    }

    .data BLOCK(__section_alignment__) :
    {
        *(.data)
        *(.data.*)
    }

    .bss BLOCK(__section_alignment__) :
    {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    .reloc BLOCK(__section_alignment__) :
    {
        *(.reloc)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.idata*)
        *(.edata)
        *(.pdata)
        *(.xdata)
        *(.debug*)
        *(.gnu.*)
        *(.init_array*)
        *(.fini_array*)
        *(.ctors*)
        *(.dtors*)
    }
}
