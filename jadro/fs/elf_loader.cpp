#include "elf_loader.h"

/*
 * Bursztyn OS - loader ELF64 (walidacja i opis).
 *
 * Plik jest przenosny: nie zawiera w sobie mapowania, alokacji ani zaleznosci
 * od jadra. Dzieki temu cala logika bezpieczenstwa (ktora decyduje, czy plik
 * jest poprawny) jest testowana na hoscie bez QEMU i bez ryzyka paniki.
 *
 * Mapowanie do przestrzeni procesu robi loader.cpp w miejscu, ktore ma juz
 * PML4, alokacje ramek i W^X. Ten modul mowi WYLACZNIE "co i jak zmapowac".
 */

namespace {

inline uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t le64(const uint8_t* p) {
    return static_cast<uint64_t>(le32(p)) |
           (static_cast<uint64_t>(le32(p + 4)) << 32);
}

/* Gorna granica dolnej polowy kanonicznej: Ring 3 musi sie w niej zmiescic. */
constexpr uint64_t USER_LIMIT = 0x0000800000000000ULL;
constexpr uint64_t USER_MIN = 0x0000000000010000ULL;  /* 64 KiB: nie 0, nie 4 KiB */

/* Maksymalny rozmiar obrazu, ktory przyjmujemy (32 MiB). */
constexpr uint64_t ELF_MAKS_ROZMIAR = 32ULL * 1024ULL * 1024ULL;

bool zakres_w_pliku(uint64_t offset, uint64_t dlugosc, uint64_t rozmiar) {
    if (offset > rozmiar) return false;
    return dlugosc <= rozmiar - offset;
}

}  // namespace

const char* elf_status_opis(ElfStatus status) {
    switch (status) {
        case ELF_OK: return "OK";
        case ELF_ZA_DUZY: return "obraz wiekszy niz limit";
        case ELF_ZLA_MAGIA: return "zla magia (nie ELF)";
        case ELF_ZLA_KLASA: return "zla klasa (nie ELF64)";
        case ELF_ZLY_ENDIAN: return "zly endianness";
        case ELF_ZLA_WERSJA: return "zla wersja ELF";
        case ELF_ZLY_TYP: return "zly e_type (dozwolone EXEC/DYN)";
        case ELF_ZLA_ARCHITEKTURA: return "zla architektura (wymagane x86-64)";
        case ELF_ZLY_NAGLOWEK_PROGRAMU: return "zly naglowek tablicy programow";
        case ELF_ZLY_SEGMENT: return "zly segment PT_LOAD";
        case ELF_SEGMENTY_NACHODZA: return "segmenty nachodza na siebie";
        case ELF_WEJSCIE_POZA_KODEM: return "punkt wejscia poza segmentem RX";
        case ELF_WX_NARUSZONE: return "segment jednoczesnie PF_X i PF_W (W^X)";
        case ELF_LINKOWANIE_DYNAMICZNE: return "wymagane linkowanie dynamiczne (PT_INTERP/PT_DYNAMIC)";
        case ELF_ZLY_ROZMIAR: return "niespojne rozmiary";
        case ELF_ZLA_TABLICA_SEKCJI: return "zla tablica sekcji";
        case ELF_RELOKACJE_NIEOBSLUGIWANE: return "nieobslugiwany typ relokacji ELF";
        case ELF_RELOKACJA_POZA_OBRAZEM: return "relokacja poza zapisywalnym obrazem";
        case ELF_RELOKACJE_ZA_DUZO: return "za duzo relokacji ELF";
        case ELF_BIAS_NIEPOPRAWNY: return "niepoprawny load bias ELF";
        default: return "nieznany status";
    }
}

ElfStatus elf_waliduj(const uint8_t* obraz, size_t rozmiar) {
    if (!obraz) return ELF_ZLA_MAGIA;
    if (rozmiar < sizeof(Elf64_Ehdr)) return ELF_ZLY_ROZMIAR;
    if (static_cast<uint64_t>(rozmiar) > ELF_MAKS_ROZMIAR) return ELF_ZA_DUZY;

    /* --- e_ident ------------------------------------------------------- */
    for (unsigned i = 0; i < 4; ++i) {
        if (obraz[i] != ELF_MAGIA[i]) return ELF_ZLA_MAGIA;
    }
    if (obraz[4] != ELFCLASS64) return ELF_ZLA_KLASA;
    if (obraz[5] != ELFDATA2LSB) return ELF_ZLY_ENDIAN;
    if (obraz[6] != EV_CURRENT) return ELF_ZLA_WERSJA;

    const uint16_t e_type = le16(obraz + 16);
    const uint16_t e_machine = le16(obraz + 18);
    if (e_type != ET_EXEC && e_type != ET_DYN) return ELF_ZLY_TYP;
    if (e_machine != EM_X86_64) return ELF_ZLA_ARCHITEKTURA;

    const uint64_t e_entry = le64(obraz + 24);
    const bool jest_dyn = e_type == ET_DYN;
    const uint64_t e_phoff = le64(obraz + 32);
    const uint16_t e_phentsize = le16(obraz + 54);
    const uint16_t e_phnum = le16(obraz + 56);

    /* --- tablica programow -------------------------------------------- */
    if (e_phnum == 0 || e_phentsize != sizeof(Elf64_Phdr)) return ELF_ZLY_NAGLOWEK_PROGRAMU;
    if (!zakres_w_pliku(e_phoff,
                        static_cast<uint64_t>(e_phnum) * sizeof(Elf64_Phdr),
                        rozmiar))
        return ELF_ZLY_NAGLOWEK_PROGRAMU;

    uint32_t liczba_ladujacych = 0;
    bool ma_wejscie = false;
    uint64_t poprzedni_koniec = 0;
    bool ma_poprzedni = false;

    struct Zakres { uint64_t start, koniec; };
    Zakres zakresy[ELF_MAKS_SEGMENTOW] = {};
    uint32_t liczba_zakresow = 0;

    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t* ph = obraz + e_phoff + static_cast<uint64_t>(i) * sizeof(Elf64_Phdr);
        const uint32_t p_type = le32(ph + 0);
        const uint32_t p_flags = le32(ph + 4);
        const uint64_t p_offset = le64(ph + 8);
        const uint64_t p_vaddr = le64(ph + 16);
        const uint64_t p_filesz = le64(ph + 32);
        const uint64_t p_memsz = le64(ph + 40);

        if (p_type == PT_INTERP || p_type == PT_DYNAMIC)
            return ELF_LINKOWANIE_DYNAMICZNE;

        if (p_type == PT_GNU_STACK) {
            /* Obecnosc PT_GNU_STACK jest OK; wlasciwosc NX wynika z flag. */
            continue;
        }
        if (p_type != PT_LOAD) continue;

        if (liczba_ladujacych >= ELF_MAKS_SEGMENTOW) return ELF_ZLY_SEGMENT;
        ++liczba_ladujacych;

        if (p_memsz < p_filesz) return ELF_ZLY_ROZMIAR;
        if (p_memsz == 0) continue;   /* pusty segment jest legalny, pomijamy */
        if (!zakres_w_pliku(p_offset, p_filesz, rozmiar)) return ELF_ZLY_SEGMENT;

        /*
         * ET_EXEC podaje juz docelowe VA, natomiast ET_DYN podaje RVA
         * wzgledem load bias. Bias zostanie dobrany przez loader tak, aby
         * nawet RVA 0 trafilo ponad strone zerowa i poza obszary kernela.
         */
        if (!jest_dyn && p_vaddr < USER_MIN) return ELF_ZLY_SEGMENT;
        if (p_vaddr >= USER_LIMIT) return ELF_ZLY_SEGMENT;
        if (p_memsz > USER_LIMIT - p_vaddr) return ELF_ZLY_SEGMENT;

        /* W^X: segment nie moze byc jednoczesnie wykonywalny i zapisywalny. */
        if ((p_flags & PF_X) && (p_flags & PF_W)) return ELF_WX_NARUSZONE;

        const uint64_t koniec = p_vaddr + p_memsz;
        if (p_vaddr <= e_entry && e_entry < koniec && (p_flags & PF_X)) ma_wejscie = true;

        /*
         * Sprawdzanie nakladania: tablica programow nie musi byc posortowana.
         * Porownujemy z kazdym wczesniejszym zakresem - przy limicie 8
         * segmentow koszt jest pomijalny, a gwarancja pelna.
         */
        for (uint32_t k = 0; k < liczba_zakresow; ++k) {
            if (p_vaddr < zakresy[k].koniec && zakresy[k].start < koniec)
                return ELF_SEGMENTY_NACHODZA;
        }
        zakresy[liczba_zakresow++] = {p_vaddr, koniec};

        if (ma_poprzedni && p_vaddr < poprzedni_koniec) {
            /* Kolejnosc nie ma znaczenia dla poprawnosci, ale zostawiamy
             * zmienne, zeby kompilator nie ostrzegal o nieuzyciu. */
            (void)poprzedni_koniec;
        }
        poprzedni_koniec = koniec;
        ma_poprzedni = true;
    }

    if (liczba_ladujacych == 0) return ELF_ZLY_SEGMENT;
    if (!ma_wejscie) return ELF_WEJSCIE_POZA_KODEM;
    return ELF_OK;
}

ElfStatus elf_opisz(const uint8_t* obraz, size_t rozmiar, ElfObraz* wynik) {
    if (!wynik) return ELF_ZLY_ROZMIAR;
    *wynik = {};
    const ElfStatus status = elf_waliduj(obraz, rozmiar);
    if (status != ELF_OK) return status;

    const uint64_t e_entry = le64(obraz + 24);
    const uint64_t e_phoff = le64(obraz + 32);
    const uint16_t e_phentsize = le16(obraz + 54);
    const uint16_t e_phnum = le16(obraz + 56);

    wynik->wejscie = e_entry;
    wynik->phent = e_phentsize;
    wynik->phnum = e_phnum;
    wynik->phdr = 0;
    wynik->dolny_adres = UINT64_MAX;
    wynik->gorny_adres = 0;
    wynik->wymaga_nx_stosu = true;   /* brak PT_GNU_STACK => domyslnie NX */

    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t* ph = obraz + e_phoff + static_cast<uint64_t>(i) * sizeof(Elf64_Phdr);
        const uint32_t p_type = le32(ph + 0);
        const uint32_t p_flags = le32(ph + 4);
        const uint64_t p_offset = le64(ph + 8);
        const uint64_t p_vaddr = le64(ph + 16);
        const uint64_t p_filesz = le64(ph + 32);
        const uint64_t p_memsz = le64(ph + 40);

        if (p_type == PT_PHDR) {
            /*
             * PT_PHDR jest wiarygodny tylko wtedy, gdy opisuje te sama
             * tablice, ktora zwalidowalismy na poczatku.
             */
            const uint64_t rozmiar_tablicy =
                static_cast<uint64_t>(e_phnum) * e_phentsize;
            if (p_offset == e_phoff &&
                p_filesz >= rozmiar_tablicy &&
                p_memsz >= rozmiar_tablicy) {
                wynik->phdr = le64(ph + 16);
            }
            continue;
        }
        if (p_type == PT_GNU_STACK) {
            /* PF_X na stosie to jawne zyczenie uzytkownika, ktore odrzucamy. */
            wynik->wymaga_nx_stosu = (p_flags & PF_X) == 0;
            continue;
        }
        if (p_type != PT_LOAD || p_memsz == 0) continue;

        ElfSegment& seg = wynik->segmenty[wynik->liczba_segmentow++];
        seg.wirtualny = p_vaddr;
        seg.rozmiar_pliku = p_filesz;
        seg.rozmiar_pamieci = p_memsz;
        seg.przesuniecie = p_offset;
        seg.flagi = p_flags;
        seg.wykonywalny = (p_flags & PF_X) ? 1U : 0U;
        seg.zapisywalny = (p_flags & PF_W) ? 1U : 0U;
        seg.zarezerwowane = 0;
        wynik->suma_flag |= p_flags;

        if (p_vaddr < wynik->dolny_adres) wynik->dolny_adres = p_vaddr;
        const uint64_t koniec = p_vaddr + p_memsz;
        if (koniec > wynik->gorny_adres) wynik->gorny_adres = koniec;

        /*
         * Typowe ET_EXEC nie maja PT_PHDR, ale tablica programow lezy
         * wtedy w pierwszym PT_LOAD. Zapisujemy jej VA dla AT_PHDR tylko
         * gdy caly naglowek jest rzeczywiscie obecny w ladowanej czesci.
         */
        const uint64_t rozmiar_tablicy =
            static_cast<uint64_t>(e_phnum) * e_phentsize;
        if (wynik->phdr == 0 &&
            e_phoff >= p_offset &&
            e_phoff - p_offset <= p_filesz &&
            rozmiar_tablicy <= p_filesz - (e_phoff - p_offset) &&
            e_phoff - p_offset <= UINT64_MAX - p_vaddr) {
            wynik->phdr = p_vaddr + (e_phoff - p_offset);
        }
    }

    if (wynik->phdr != 0) {
        const uint64_t rozmiar_tablicy =
            static_cast<uint64_t>(wynik->phnum) * wynik->phent;
        bool phdr_zmapowany = false;
        for (uint32_t i = 0; i < wynik->liczba_segmentow; ++i) {
            const ElfSegment& segment = wynik->segmenty[i];
            if (wynik->phdr < segment.wirtualny ||
                wynik->phdr - segment.wirtualny > segment.rozmiar_pamieci ||
                rozmiar_tablicy >
                    segment.rozmiar_pamieci -
                    (wynik->phdr - segment.wirtualny)) {
                continue;
            }
            phdr_zmapowany = true;
            break;
        }
        if (!phdr_zmapowany) wynik->phdr = 0;
    }

    if (wynik->dolny_adres == UINT64_MAX) wynik->dolny_adres = 0;
    return ELF_OK;
}

namespace {

bool relokacja_w_zapisywalnym_segmentach(
    const uint8_t* obraz,
    uint64_t e_phoff,
    uint16_t e_phnum,
    uint64_t offset
) {
    if (offset > UINT64_MAX - sizeof(uint64_t)) return false;
    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t* ph =
            obraz + e_phoff + static_cast<uint64_t>(i) * sizeof(Elf64_Phdr);
        if (le32(ph + 0) != PT_LOAD) continue;
        const uint32_t flags = le32(ph + 4);
        const uint64_t vaddr = le64(ph + 16);
        const uint64_t memsz = le64(ph + 40);
        if ((flags & PF_W) == 0 || memsz < sizeof(uint64_t) ||
            offset < vaddr || offset - vaddr > memsz - sizeof(uint64_t)) {
            continue;
        }
        return true;
    }
    return false;
}

}  // namespace

ElfStatus elf_zbierz_relokacje(
    const uint8_t* obraz,
    size_t rozmiar,
    ElfRelokacja* wynik,
    uint32_t pojemnosc,
    uint32_t* liczba
) {
    if (!liczba) return ELF_ZLY_ROZMIAR;
    *liczba = 0;

    const ElfStatus validacja = elf_waliduj(obraz, rozmiar);
    if (validacja != ELF_OK) return validacja;
    if (le16(obraz + 16) != ET_DYN) return ELF_ZLY_TYP;

    const uint64_t e_shoff = le64(obraz + 40);
    const uint16_t e_shentsize = le16(obraz + 58);
    const uint16_t e_shnum = le16(obraz + 60);
    if (e_shnum == 0) return ELF_OK;
    if (e_shentsize != sizeof(Elf64_Shdr)) return ELF_ZLA_TABLICA_SEKCJI;
    if (!zakres_w_pliku(
            e_shoff,
            static_cast<uint64_t>(e_shnum) * sizeof(Elf64_Shdr),
            rozmiar)) {
        return ELF_ZLA_TABLICA_SEKCJI;
    }

    const uint64_t e_phoff = le64(obraz + 32);
    const uint16_t e_phnum = le16(obraz + 56);
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const uint8_t* sh =
            obraz + e_shoff + static_cast<uint64_t>(i) * sizeof(Elf64_Shdr);
        const uint32_t section_type = le32(sh + 4);
        if (section_type == SHT_REL || section_type == SHT_RELR) {
            return ELF_RELOKACJE_NIEOBSLUGIWANE;
        }
        if (section_type != SHT_RELA) continue;

        const uint64_t sh_offset = le64(sh + 24);
        const uint64_t sh_size = le64(sh + 32);
        const uint64_t sh_entsize = le64(sh + 56);
        if (sh_entsize != sizeof(Elf64_Rela) ||
            (sh_size % sizeof(Elf64_Rela)) != 0 ||
            !zakres_w_pliku(sh_offset, sh_size, rozmiar)) {
            return ELF_ZLA_TABLICA_SEKCJI;
        }

        const uint64_t ile = sh_size / sizeof(Elf64_Rela);
        if (ile > ELF_MAKS_RELOKACJI - *liczba ||
            ile > pojemnosc - *liczba ||
            (ile != 0 && !wynik)) {
            return ELF_RELOKACJE_ZA_DUZO;
        }

        for (uint64_t j = 0; j < ile; ++j) {
            const uint8_t* rela =
                obraz + sh_offset + j * sizeof(Elf64_Rela);
            const uint64_t r_offset = le64(rela + 0);
            const uint64_t r_info = le64(rela + 8);
            const uint32_t typ = static_cast<uint32_t>(r_info & 0xFFFFFFFFULL);
            const uint32_t symbol = static_cast<uint32_t>(r_info >> 32);
            if (typ != R_X86_64_RELATIVE || symbol != 0) {
                return ELF_RELOKACJE_NIEOBSLUGIWANE;
            }
            if (!relokacja_w_zapisywalnym_segmentach(
                    obraz, e_phoff, e_phnum, r_offset)) {
                return ELF_RELOKACJA_POZA_OBRAZEM;
            }
            wynik[*liczba] = {
                r_offset,
                static_cast<int64_t>(le64(rela + 16)),
                typ,
                symbol
            };
            ++*liczba;
        }
    }
    return ELF_OK;
}

ElfStatus elf_przesun_obraz(ElfObraz* obraz, uint64_t bias) {
    if (!obraz || (bias & (0x1000ULL - 1ULL)) != 0) {
        return ELF_BIAS_NIEPOPRAWNY;
    }

    if (obraz->wejscie > UINT64_MAX - bias ||
        obraz->phdr > UINT64_MAX - bias ||
        obraz->dolny_adres > UINT64_MAX - bias ||
        obraz->gorny_adres > UINT64_MAX - bias) {
        return ELF_BIAS_NIEPOPRAWNY;
    }
    for (uint32_t i = 0; i < obraz->liczba_segmentow; ++i) {
        if (obraz->segmenty[i].wirtualny > UINT64_MAX - bias) {
            return ELF_BIAS_NIEPOPRAWNY;
        }
    }

    obraz->wejscie += bias;
    if (obraz->phdr != 0) obraz->phdr += bias;
    obraz->dolny_adres += bias;
    obraz->gorny_adres += bias;
    for (uint32_t i = 0; i < obraz->liczba_segmentow; ++i) {
        obraz->segmenty[i].wirtualny += bias;
    }
    return ELF_OK;
}

bool elf_oblicz_relative(
    const ElfRelokacja& relokacja,
    uint64_t bias,
    uint64_t* wartosc
) {
    if (!wartosc ||
        relokacja.typ != R_X86_64_RELATIVE ||
        relokacja.symbol != 0) {
        return false;
    }

    if (relokacja.addend >= 0) {
        const uint64_t addend = static_cast<uint64_t>(relokacja.addend);
        if (bias > UINT64_MAX - addend) return false;
        *wartosc = bias + addend;
        return true;
    }

    /*
     * Nie liczymy -INT64_MIN bezposrednio: ta wartosc nie miesci sie
     * w int64_t i sanitizery slusznie zglaszaja takie signed overflow.
     */
    const uint64_t magnituda =
        static_cast<uint64_t>(-(relokacja.addend + 1)) + 1ULL;
    if (bias < magnituda) return false;
    *wartosc = bias - magnituda;
    return true;
}

uint16_t elf_typ(const uint8_t* obraz, size_t rozmiar) {
    if (elf_waliduj(obraz, rozmiar) != ELF_OK) return 0;
    return le16(obraz + 16);
}

bool elf_wejscie_w_kodzie(const ElfObraz& obraz) {
    for (uint32_t i = 0; i < obraz.liczba_segmentow; ++i) {
        const ElfSegment& s = obraz.segmenty[i];
        if (!s.wykonywalny) continue;
        if (obraz.wejscie >= s.wirtualny &&
            obraz.wejscie < s.wirtualny + s.rozmiar_pamieci)
            return true;
    }
    return false;
}

bool elf_narusza_wx(const ElfObraz& obraz) {
    for (uint32_t i = 0; i < obraz.liczba_segmentow; ++i) {
        if (obraz.segmenty[i].wykonywalny && obraz.segmenty[i].zapisywalny)
            return true;
    }
    return false;
}
