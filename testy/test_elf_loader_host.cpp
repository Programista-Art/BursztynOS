/*
 * Host gate loadera ELF64 (punkt 2.14 planu).
 *
 * Test jest pozytywny i negatywny: buduje poprawne obrazy ELF w pamieci
 * (nie z plikow, zeby nie zalezec od toolchaina) i sprawdza, ze loader
 *
 *   - przyjmuje poprawny obraz ET_EXEC/ET_DYN z segmentami RX i RW+NX,
 *   - odrzuca kazda pojedyncza mutacje, ktora lamie ktorys warunek,
 *   - nigdy nie mapuje segmentu jednoczesnie PF_X i PF_W (W^X),
 *   - poprawnie wylicza BSS (p_memsz > p_filesz) i granice obrazu,
 *   - odrzuca PT_INTERP/PT_DYNAMIC (linkowanie dynamiczne nie jest gotowe).
 *
 * Kazdy negatywny przypadek ma byc odrzucony PRZED jakimkolwiek mapowaniem,
 * dlatego test sprawdza tylko funkcje czyste - zadnego VMM w tym tescie nie ma.
 */
#include "jadro/fs/elf_loader.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool warunek, const char* opis) {
    ++g_checks;
    if (!warunek) {
        std::printf("FAIL: %s\n", opis);
        ++g_failures;
    }
}

void check_status(ElfStatus rzeczywisty, ElfStatus oczekiwany, const char* opis) {
    ++g_checks;
    if (rzeczywisty != oczekiwany) {
        std::printf("FAIL: %s (oczekiwano %d '%s', otrzymano %d '%s')\n",
                    opis, int(oczekiwany), elf_status_opis(oczekiwany),
                    int(rzeczywisty), elf_status_opis(rzeczywisty));
        ++g_failures;
    }
}

/* =========================================================================
 * BUDOWANIE OBRAZOW W PAMIECI
 * ========================================================================= */

struct Budowniczy {
    std::vector<uint8_t> dane;

    explicit Budowniczy(size_t rozmiar = 0x2000) : dane(rozmiar, 0) {
        std::memset(dane.data(), 0, dane.size());
    }

    uint8_t* baza() { return dane.data(); }
    const uint8_t* baza() const { return dane.data(); }
    size_t rozmiar() const { return dane.size(); }

    void put16(size_t off, uint16_t v) {
        dane[off] = uint8_t(v & 0xFF);
        dane[off + 1] = uint8_t((v >> 8) & 0xFF);
    }
    void put32(size_t off, uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) dane[off + i] = uint8_t((v >> (8 * i)) & 0xFF);
    }
    void put64(size_t off, uint64_t v) {
        for (unsigned i = 0; i < 8; ++i) dane[off + i] = uint8_t((v >> (8 * i)) & 0xFF);
    }

    /* Naglowek: poprawny ELF64 LE x86-64. */
    void naglowek(uint16_t typ, uint64_t wejscie, uint64_t phoff, uint16_t phnum) {
        dane[0] = 0x7F; dane[1] = 'E'; dane[2] = 'L'; dane[3] = 'F';
        dane[4] = ELFCLASS64;
        dane[5] = ELFDATA2LSB;
        dane[6] = EV_CURRENT;
        dane[7] = 0;  /* System V ABI */
        put16(16, typ);
        put16(18, EM_X86_64);
        put32(20, 1);
        put64(24, wejscie);
        put64(32, phoff);
        put64(40, 0);      /* e_shoff */
        put32(48, 0);      /* e_flags */
        put16(52, 64);     /* e_ehsize */
        put16(54, 56);     /* e_phentsize */
        put16(56, phnum);
        put16(58, 64);     /* e_shentsize */
        put16(60, 0);      /* e_shnum */
        put16(62, 0);      /* e_shstrndx */
    }

    void phdr(size_t off, uint32_t typ, uint32_t flagi, uint64_t plik_off,
              uint64_t vaddr, uint64_t filesz, uint64_t memsz) {
        put32(off + 0, typ);
        put32(off + 4, flagi);
        put64(off + 8, plik_off);
        put64(off + 16, vaddr);
        put64(off + 24, vaddr);   /* p_paddr = vaddr (bez znaczenia) */
        put64(off + 32, filesz);
        put64(off + 40, memsz);
        put64(off + 48, 0x1000);
    }
};

/* Poprawny obraz: dwa segmenty (RX .text + RW .data z BSS). */
Budowniczy obraz_poprawny(uint16_t typ = ET_EXEC) {
    Budowniczy b(0x2000);
    b.naglowek(typ, 0x400000 + 0x80, 64, 2);
    b.phdr(64, PT_LOAD, PF_R | PF_X, 0x1000, 0x400000, 0x200, 0x200);
    b.phdr(64 + 56, PT_LOAD, PF_R | PF_W, 0x1200, 0x401000, 0x100, 0x400);
    return b;
}

/* =========================================================================
 * TESTY POZYTYWNE
 * ========================================================================= */

void test_pozytywne() {
    const Budowniczy b = obraz_poprawny();
    check_status(elf_waliduj(b.baza(), b.rozmiar()), ELF_OK, "poprawny ELF64 EXEC");

    ElfObraz obraz{};
    check_status(elf_opisz(b.baza(), b.rozmiar(), &obraz), ELF_OK, "opis poprawnego obrazu");
    check(obraz.wejscie == 0x400080, "e_entry");
    check(obraz.liczba_segmentow == 2, "liczba segmentow");
    check(obraz.dolny_adres == 0x400000, "dolny adres");
    check(obraz.gorny_adres == 0x401400, "gorny adres (z BSS)");
    check(obraz.segmenty[0].wykonywalny && !obraz.segmenty[0].zapisywalny,
          "segment 0 RX");
    check(!obraz.segmenty[1].wykonywalny && obraz.segmenty[1].zapisywalny,
          "segment 1 RW");
    check(obraz.segmenty[1].rozmiar_pamieci == 0x400, "BSS: p_memsz");
    check(obraz.segmenty[1].rozmiar_pliku == 0x100, "BSS: p_filesz");
    check(elf_wejscie_w_kodzie(obraz), "wejscie w segmencie RX");
    check(!elf_narusza_wx(obraz), "brak naruszenia W^X");

    /* ET_DYN (PIE) jest dozwolony. */
    const Budowniczy dyn = obraz_poprawny(ET_DYN);
    check_status(elf_waliduj(dyn.baza(), dyn.rozmiar()), ELF_OK, "poprawny ELF64 DYN");

    ElfObraz obraz_dyn{};
    check_status(elf_opisz(dyn.baza(), dyn.rozmiar(), &obraz_dyn), ELF_OK, "opis DYN");

    /* PT_GNU_STACK bez PF_X => NX stos. */
    Budowniczy stack = obraz_poprawny();
    stack.naglowek(ET_EXEC, 0x400080, 64, 3);
    stack.phdr(64 + 112, PT_GNU_STACK, PF_R | PF_W, 0, 0, 0, 0);
    check_status(elf_waliduj(stack.baza(), stack.rozmiar()), ELF_OK, "PT_GNU_STACK RW");
    ElfObraz obraz_stack{};
    check_status(elf_opisz(stack.baza(), stack.rozmiar(), &obraz_stack), ELF_OK, "opis z GNU_STACK");
    check(obraz_stack.wymaga_nx_stosu, "GNU_STACK RW => wymaga NX stosu");

    /* Segment z zerowym p_memsz jest legalny i pomijany. */
    Budowniczy pusty = obraz_poprawny();
    pusty.naglowek(ET_EXEC, 0x400080, 64, 3);
    pusty.phdr(64 + 112, PT_LOAD, PF_R, 0x1000, 0x500000, 0, 0);
    check_status(elf_waliduj(pusty.baza(), pusty.rozmiar()), ELF_OK, "segment o memsz=0");
    ElfObraz obraz_pusty{};
    check_status(elf_opisz(pusty.baza(), pusty.rozmiar(), &obraz_pusty), ELF_OK, "opis z pustym");
    check(obraz_pusty.liczba_segmentow == 2, "pusty segment pominiety");
}

/* =========================================================================
 * TESTY NEGATYWNE (kazda mutacja osobno)
 * ========================================================================= */

void test_negatywne() {
    /* 1. Za maly bufor. */
    check_status(elf_waliduj(nullptr, 0), ELF_ZLA_MAGIA, "nullptr");
    const Budowniczy b = obraz_poprawny();
    check_status(elf_waliduj(b.baza(), 32), ELF_ZLY_ROZMIAR, "bufor mniejszy od naglowka");

    /* 2. Zla magia / klasa / endianness / wersja. */
    {
        Budowniczy m = obraz_poprawny();
        m.dane[1] = 'X';
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLA_MAGIA, "zla magia");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.dane[4] = 1;  /* ELFCLASS32 */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLA_KLASA, "klasa 32-bit");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.dane[5] = 2;  /* big endian */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_ENDIAN, "big endian");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.dane[6] = 0;
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLA_WERSJA, "zla wersja");
    }

    /* 3. Zly e_type i architektura. */
    {
        Budowniczy m = obraz_poprawny();
        m.put16(16, 1);  /* ET_REL */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_TYP, "e_type ET_REL");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.put16(18, 3);  /* EM_386 */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLA_ARCHITEKTURA, "EM_386");
    }

    /* 4. Zla tablica programow. */
    {
        Budowniczy m = obraz_poprawny();
        m.put16(54, 32);  /* e_phentsize */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_NAGLOWEK_PROGRAMU,
                     "zly e_phentsize");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.put64(32, 0x100000);  /* e_phoff poza plikiem */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_NAGLOWEK_PROGRAMU,
                     "e_phoff poza plikiem");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.put16(56, 0);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_NAGLOWEK_PROGRAMU,
                     "e_phnum == 0");
    }

    /* 5. Segmenty: W^X, adresy, rozmiary, nakladanie. */
    {
        Budowniczy m = obraz_poprawny();
        m.phdr(64, PT_LOAD, PF_R | PF_X | PF_W, 0x1000, 0x400000, 0x200, 0x200);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_WX_NARUSZONE,
                     "segment PF_X|PF_W");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.phdr(64, PT_LOAD, PF_R | PF_X, 0x1000, 0x0, 0x200, 0x200);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_SEGMENT,
                     "segment w stronie 0");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.phdr(64, PT_LOAD, PF_R | PF_X, 0x1000, 0x0000800000000000ULL, 0x200, 0x200);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_SEGMENT,
                     "segment w gornej polowie kanonicznej");
    }
    {
        Budowniczy m = obraz_poprawny();
        /* p_memsz < p_filesz */
        m.phdr(64, PT_LOAD, PF_R | PF_X, 0x1000, 0x400000, 0x400, 0x100);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_ROZMIAR,
                     "p_memsz < p_filesz");
    }
    {
        Budowniczy m = obraz_poprawny();
        /* p_offset + p_filesz poza plikiem */
        m.phdr(64, PT_LOAD, PF_R | PF_X, 0x1F00, 0x400000, 0x400, 0x400);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_SEGMENT,
                     "segment poza plikiem");
    }
    {
        Budowniczy m = obraz_poprawny();
        /* Nachodzace segmenty: drugi zaczyna sie w pierwszym. */
        m.phdr(64 + 56, PT_LOAD, PF_R | PF_W, 0x1200, 0x400100, 0x100, 0x400);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_SEGMENTY_NACHODZA,
                     "segmenty nachodza");
    }
    {
        Budowniczy m = obraz_poprawny();
        /* Nakladanie wykrywane takze przy nieposortowanej tablicy:
         * segment 0 obejmuje 0x400100..0x400500, segment 1 0x400000..0x400200,
         * wiec nachodza na siebie w 0x400100..0x400200. */
        m.phdr(64, PT_LOAD, PF_R | PF_W, 0x1000, 0x400100, 0x100, 0x400);
        m.phdr(64 + 56, PT_LOAD, PF_R | PF_X, 0x1200, 0x400000, 0x200, 0x200);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_SEGMENTY_NACHODZA,
                     "nachodzenie przy nieposortowanej tablicy");
    }

    /* 6. Punkt wejscia poza segmentem wykonywalnym. */
    {
        Budowniczy m = obraz_poprawny();
        m.put64(24, 0x401000);  /* w segmencie RW, nie RX */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_WEJSCIE_POZA_KODEM,
                     "wejscie w segmencie RW");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.put64(24, 0x900000);  /* poza obrazem */
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_WEJSCIE_POZA_KODEM,
                     "wejscie poza obrazem");
    }

    /* 7. Linkowanie dynamiczne. */
    {
        Budowniczy m = obraz_poprawny();
        m.naglowek(ET_EXEC, 0x400080, 64, 3);
        m.phdr(64 + 112, PT_INTERP, PF_R, 0x1000, 0x400000, 0x20, 0x20);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_LINKOWANIE_DYNAMICZNE,
                     "PT_INTERP");
    }
    {
        Budowniczy m = obraz_poprawny();
        m.naglowek(ET_EXEC, 0x400080, 64, 3);
        m.phdr(64 + 112, PT_DYNAMIC, PF_R | PF_W, 0x1000, 0x400000, 0x20, 0x20);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_LINKOWANIE_DYNAMICZNE,
                     "PT_DYNAMIC");
    }

    /* 8. Brak jakiegokolwiek PT_LOAD. */
    {
        Budowniczy m = obraz_poprawny();
        m.put32(64, 0x60000000U);  /* nieznany typ */
        m.put32(64 + 56, 0x60000000U);
        check_status(elf_waliduj(m.baza(), m.rozmiar()), ELF_ZLY_SEGMENT,
                     "brak PT_LOAD");
    }
}

/* =========================================================================
 * TESTY ODPORNOSCI: kazdy bajt naglowka i tablicy programow
 * ========================================================================= */

void test_mutacje() {
    const Budowniczy b = obraz_poprawny();

    /*
     * Test wlasnosci: dla KAZDEJ mutacji pojedynczego bajtu w naglowku
     * i tablicy programow sprawdzamy, ze jezeli loader przyjmuje obraz,
     * to wszystkie niezmienniki bezpieczenstwa nadal zachodza.
     *
     * To jest mocniejsze niz "mutacja musi byc odrzucona": wiele mutacji
     * jest semantycznie nieistotnych (padding, p_align, flaga PF_R), a
     * wymaganie odrzucenia kazdej z nich testowaloby nie loader, a jego
     * brak tolerancji. Wlasnosci, ktore MUSZA zachodzic po kazdym przyjeciu:
     *
     *   - zaden segment nie jest jednoczesnie PF_X i PF_W,
     *   - kazdy segment ma p_memsz >= p_filesz,
     *   - kazdy segment ma p_offset + p_filesz w granicach pliku,
     *   - segmenty nie nachodza na siebie,
     *   - punkt wejscia lezy w segmencie wykonywalnym,
     *   - wszystkie adresy leza w dolnej polowie kanonicznej i poza strona 0.
     */
    int przyjete = 0;
    int odrzucone = 0;
    const size_t koniec = 64 + 2 * sizeof(Elf64_Phdr);

    for (size_t off = 0; off < koniec; ++off) {
        Budowniczy m = b;
        m.dane[off] ^= 0xFF;
        const ElfStatus s = elf_waliduj(m.baza(), m.rozmiar());
        ++g_checks;
        if (s != ELF_OK) {
            ++odrzucone;
            continue;
        }
        ++przyjete;

        ElfObraz obraz{};
        const ElfStatus opis_status = elf_opisz(m.baza(), m.rozmiar(), &obraz);
        ++g_checks;
        if (opis_status != ELF_OK) {
            std::printf("FAIL: mutacja bajtu %zu przyjeta, ale opis zawiodl\n", off);
            ++g_failures;
            continue;
        }

        ++g_checks;
        if (elf_narusza_wx(obraz)) {
            std::printf("FAIL: mutacja bajtu %zu lamie W^X\n", off);
            ++g_failures;
        }
        ++g_checks;
        if (!elf_wejscie_w_kodzie(obraz)) {
            std::printf("FAIL: mutacja bajtu %zu daje wejscie poza kodem\n", off);
            ++g_failures;
        }
        for (uint32_t k = 0; k < obraz.liczba_segmentow; ++k) {
            const ElfSegment& seg = obraz.segmenty[k];
            ++g_checks;
            if (seg.rozmiar_pamieci < seg.rozmiar_pliku) {
                std::printf("FAIL: mutacja bajtu %zu lamie memsz >= filesz\n", off);
                ++g_failures;
            }
            ++g_checks;
            if (seg.przesuniecie > m.rozmiar() ||
                seg.rozmiar_pliku > m.rozmiar() - seg.przesuniecie) {
                std::printf("FAIL: mutacja bajtu %zu lamie granice pliku\n", off);
                ++g_failures;
            }
            ++g_checks;
            if (seg.wirtualny < 0x10000ULL ||
                seg.wirtualny + seg.rozmiar_pamieci > 0x0000800000000000ULL) {
                std::printf("FAIL: mutacja bajtu %zu lamie granice uzytkownika\n", off);
                ++g_failures;
            }
        }
        for (uint32_t x = 0; x < obraz.liczba_segmentow; ++x) {
            for (uint32_t y = x + 1; y < obraz.liczba_segmentow; ++y) {
                const ElfSegment& a = obraz.segmenty[x];
                const ElfSegment& c = obraz.segmenty[y];
                ++g_checks;
                if (a.wirtualny < c.wirtualny + c.rozmiar_pamieci &&
                    c.wirtualny < a.wirtualny + a.rozmiar_pamieci) {
                    std::printf("FAIL: mutacja bajtu %zu daje nachodzace segmenty\n", off);
                    ++g_failures;
                }
            }
        }
    }

    check(przyjete + odrzucone == static_cast<int>(koniec),
          "kazda mutacja daje wynik");
    check(odrzucone > 50, "wiekszosc mutacji naglowka jest odrzucana");
    check(przyjete > 0, "niektore mutacje sa semantycznie nieistotne (tolerancja)");

    /* Obciecie obrazu na kazdej dlugosci nie moze wyjsc poza bufor (ASan). */
    const Budowniczy b2 = obraz_poprawny();
    int obciec_ok = 0;
    for (size_t n = 0; n < b2.rozmiar(); n += 7) {
        if (elf_waliduj(b2.baza(), n) != ELF_OK) ++obciec_ok;
    }
    check(obciec_ok > 100, "obciete obrazy sa odrzucane");
}

/* =========================================================================
 * OPISY STATUSOW
 * ========================================================================= */

void test_opisy() {
    const ElfStatus wszystkie[] = {
        ELF_OK, ELF_ZA_DUZY, ELF_ZLA_MAGIA, ELF_ZLA_KLASA, ELF_ZLY_ENDIAN,
        ELF_ZLA_WERSJA, ELF_ZLY_TYP, ELF_ZLA_ARCHITEKTURA,
        ELF_ZLY_NAGLOWEK_PROGRAMU, ELF_ZLY_SEGMENT, ELF_SEGMENTY_NACHODZA,
        ELF_WEJSCIE_POZA_KODEM, ELF_WX_NARUSZONE, ELF_LINKOWANIE_DYNAMICZNE,
        ELF_ZLY_ROZMIAR,
    };
    for (ElfStatus s : wszystkie) {
        check(elf_status_opis(s) != nullptr && elf_status_opis(s)[0] != '\0',
              "kazdy status ma opis");
    }
    check_status(elf_waliduj(nullptr, 0), ELF_ZLA_MAGIA, "nullptr -> konkretny status");
}

}  // namespace

int main() {
    test_pozytywne();
    test_negatywne();
    test_mutacje();
    test_opisy();

    if (g_failures != 0) {
        std::printf("[ELF-LOADER-HOST] FAIL %d/%d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("[ELF-LOADER-HOST] PASS %d asercji\n", g_checks);
    return 0;
}
