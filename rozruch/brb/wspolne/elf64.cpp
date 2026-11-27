#include "elf64.h"

#include "arytmetyka.h"
#include "pamiec_podstawowa.h"

namespace brb {

namespace {

// gABI: przesuniecia pol naglowka ELF64 i naglowka programu.
constexpr uint64_t EH_TYPE = 16, EH_MACHINE = 18, EH_VERSION = 20, EH_ENTRY = 24, EH_PHOFF = 32,
                   EH_EHSIZE = 52, EH_PHENTSIZE = 54, EH_PHNUM = 56, EH_ROZMIAR = 64;
constexpr uint64_t PH_TYPE = 0, PH_FLAGS = 4, PH_OFFSET = 8, PH_VADDR = 16, PH_PADDR = 24, PH_FILESZ = 32,
                   PH_MEMSZ = 40, PH_ALIGN = 48, PH_ROZMIAR = 56;
constexpr uint32_t PT_LOAD = 1, PT_DYNAMIC = 2, PT_INTERP = 3, PT_SHLIB = 5, PT_TLS = 7;
constexpr uint16_t ET_EXEC = 2, EM_X86_64 = 62, PN_XNUM = 0xFFFF;
constexpr uint8_t ELFCLASS64 = 2, ELFDATA2LSB = 1, EV_CURRENT = 1, OSABI_SYSV = 0, OSABI_GNU = 3;
constexpr uint32_t MB2_MAGIC = 0xE85250D6u, MB2_ARCH_I386 = 0;

WynikElf blad(BladElf b, int32_t ph = -1) { return WynikElf{b, ph}; }

struct Zakres { uint64_t poczatek, koniec; };

bool nakladaja_sie(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a0 < b1 && b0 < a1; }

// Suma (bez powtorzen) zakresow stron: sortowanie przez wstawianie (n <= 16) i scalanie.
bool suma_zakresow(const PlanLadowania& p, uint64_t* wynik) {
    Zakres z[ELF_MAKS_SEGMENTOW];
    const uint32_t n = p.liczba_segmentow;
    for (uint32_t i = 0; i < n; ++i) {
        Zakres nowy{p.segmenty[i].strona_poczatek, p.segmenty[i].strona_koniec};
        uint32_t j = i;
        while (j > 0 && z[j - 1].poczatek > nowy.poczatek) { z[j] = z[j - 1]; --j; }
        z[j] = nowy;
    }
    uint64_t suma = 0;
    uint64_t pocz = z[0].poczatek, kon = z[0].koniec;
    for (uint32_t i = 1; i <= n; ++i) {
        if (i < n && z[i].poczatek <= kon) {
            if (z[i].koniec > kon) kon = z[i].koniec;
            continue;
        }
        if (!dodaj_u64(suma, kon - pocz, &suma)) return false;
        if (i < n) { pocz = z[i].poczatek; kon = z[i].koniec; }
    }
    *wynik = suma;
    return true;
}

BladElf sprawdz_mb2(const uint8_t* d, uint64_t rozmiar, PlanLadowania* plan) {
    const uint64_t okno = rozmiar < MB2_OKNO_PLIKU ? rozmiar : MB2_OKNO_PLIKU;
    bool byl_magic = false;
    for (uint64_t off = 0; off + 16 <= okno; off += 8) {
        if (le32(d + off) != MB2_MAGIC) continue;
        byl_magic = true;
        const uint32_t arch = le32(d + off + 4), dlugosc = le32(d + off + 8), suma = le32(d + off + 12);
        if (uint32_t(MB2_MAGIC + arch + dlugosc + suma) != 0) continue;   // MB2 3.1.1: suma 32-bitowa = 0
        // Dalsze pola sa wiazace: niepoprawny naglowek z poprawna suma jest bledem, nie "brakiem".
        if (arch != MB2_ARCH_I386) return BladElf::ZlyNaglowekMb2;
        if (dlugosc < 24 || dlugosc % 8 != 0 || !zakres_w_buforze(off, dlugosc, okno)) return BladElf::ZlyNaglowekMb2;
        uint64_t t = off + 16;
        const uint64_t koniec = off + dlugosc;
        uint32_t tagi = 0;
        bool koniec_tagow = false;
        while (t < koniec && tagi < MB2_MAKS_TAGOW) {
            if (koniec - t < 8) return BladElf::ZlyNaglowekMb2;
            const uint16_t typ = le16(d + t);
            const uint32_t r = le32(d + t + 4);
            if (r < 8 || r > koniec - t) return BladElf::ZlyNaglowekMb2;
            ++tagi;
            if (typ == 0) {
                if (r != 8 || t + 8 != koniec) return BladElf::ZlyNaglowekMb2;  // tag konca ostatni
                koniec_tagow = true;
                break;
            }
            uint64_t krok;
            if (!wyrownaj_w_gore(r, 8, &krok) || krok > koniec - t) return BladElf::ZlyNaglowekMb2;
            t += krok;
        }
        if (!koniec_tagow) return BladElf::ZlyNaglowekMb2;
        plan->mb2 = NaglowekMb2{true, off, dlugosc, arch, tagi, -1};
        for (uint32_t i = 0; i < plan->liczba_segmentow; ++i) {
            const SegmentPlanu& s = plan->segmenty[i];
            if (off >= s.przesuniecie_pliku && off - s.przesuniecie_pliku < s.bajty_z_pliku &&
                dlugosc <= s.bajty_z_pliku - (off - s.przesuniecie_pliku))
                plan->mb2.segment = int32_t(i);
        }
        return BladElf::Brak;
    }
    return byl_magic ? BladElf::ZlyNaglowekMb2 : BladElf::BrakNaglowkaMb2;
}

}  // namespace

WynikElf zbuduj_plan_ladowania(const uint8_t* d, uint64_t rozmiar, const PolitykaElf& pol, PlanLadowania* plan) {
    if (!plan) return blad(BladElf::ZaKrotki);
    brb_memset(plan, 0, sizeof(*plan));
    plan->mb2.segment = -1;
    if (!d || rozmiar < EH_ROZMIAR) return blad(BladElf::ZaKrotki);
    if (rozmiar > ELF_MAKS_ROZMIAR_PLIKU) return blad(BladElf::ZaDuzyPlik);
    plan->rozmiar_pliku = rozmiar;

    // ---- e_ident i naglowek ----
    if (d[0] != 0x7F || d[1] != 'E' || d[2] != 'L' || d[3] != 'F') return blad(BladElf::ZlaSygnatura);
    if (d[4] != ELFCLASS64) return blad(BladElf::NieKlasa64);
    if (d[5] != ELFDATA2LSB) return blad(BladElf::NieLittleEndian);
    if (d[6] != EV_CURRENT) return blad(BladElf::ZlaWersja);
    if ((d[7] != OSABI_SYSV && d[7] != OSABI_GNU) || d[8] != 0) return blad(BladElf::NieobslugiwaneAbi);
    if (le16(d + EH_TYPE) != ET_EXEC) return blad(BladElf::NieobslugiwanyTyp);
    if (le16(d + EH_MACHINE) != EM_X86_64) return blad(BladElf::ZlaArchitektura);
    if (le32(d + EH_VERSION) != EV_CURRENT) return blad(BladElf::ZlaWersja);
    if (le16(d + EH_EHSIZE) != EH_ROZMIAR) return blad(BladElf::ZlyRozmiarNaglowka);
    if (le16(d + EH_PHENTSIZE) != PH_ROZMIAR) return blad(BladElf::ZlyRozmiarWpisuPh);
    const uint16_t phnum = le16(d + EH_PHNUM);
    if (phnum == 0 || phnum == PN_XNUM || phnum > ELF_MAKS_NAGLOWKOW_PROGRAMU) return blad(BladElf::ZlaLiczbaPh);
    const uint64_t phoff = le64(d + EH_PHOFF);
    uint64_t rozmiar_tablicy;
    if (!mnoz_u64(phnum, PH_ROZMIAR, &rozmiar_tablicy) || phoff < EH_ROZMIAR || phoff % 8 != 0 ||
        !zakres_w_buforze(phoff, rozmiar_tablicy, rozmiar))
        return blad(BladElf::TablicaPhPozaPlikiem);
    const uint64_t wejscie = le64(d + EH_ENTRY);

    // ---- naglowki programu ----
    uint64_t poprzedni_vaddr = 0;
    for (uint32_t i = 0; i < phnum; ++i) {
        const uint8_t* ph = d + phoff + uint64_t(i) * PH_ROZMIAR;   // w granicach: sprawdzono tablice
        const int32_t idx = int32_t(i);
        const uint32_t typ = le32(ph + PH_TYPE);
        if (typ == PT_DYNAMIC || typ == PT_INTERP || typ == PT_SHLIB || typ == PT_TLS)
            return blad(BladElf::NieobslugiwanySegment, idx);
        if (typ != PT_LOAD) continue;   // PT_NULL, PT_NOTE, PT_PHDR, PT_GNU_STACK i pokrewne: bez wplywu
        if (plan->liczba_segmentow >= ELF_MAKS_SEGMENTOW) return blad(BladElf::ZaDuzoSegmentow, idx);
        const uint32_t flagi = le32(ph + PH_FLAGS);
        const uint64_t off = le64(ph + PH_OFFSET), vaddr = le64(ph + PH_VADDR), paddr = le64(ph + PH_PADDR);
        const uint64_t filesz = le64(ph + PH_FILESZ), memsz = le64(ph + PH_MEMSZ), align = le64(ph + PH_ALIGN);
        if (memsz == 0) return blad(BladElf::ZerowySegment, idx);
        if (filesz > memsz) return blad(BladElf::PlikWiekszyNizPamiec, idx);
        if (!zakres_w_buforze(off, filesz, rozmiar)) return blad(BladElf::SegmentPozaPlikiem, idx);
        if (align > 1) {
            if (!potega_dwojki(align)) return blad(BladElf::ZleWyrownanie, idx);
            if ((vaddr & (align - 1)) != (off & (align - 1))) return blad(BladElf::ZlaKongruencja, idx);
        }
        uint64_t vkoniec, pkoniec, pstrona_koniec, vstrona_koniec;
        if (!dodaj_u64(vaddr, memsz, &vkoniec) || !dodaj_u64(paddr, memsz, &pkoniec) ||
            !wyrownaj_w_gore(pkoniec, ELF_STRONA, &pstrona_koniec) ||
            !wyrownaj_w_gore(vkoniec, ELF_STRONA, &vstrona_koniec))
            return blad(BladElf::PrzepelnienieAdresu, idx);
        if (pol.wymagaj_tozsamosci && paddr != vaddr) return blad(BladElf::AdresNietozsamosciowy, idx);
        if (paddr < pol.min_adres_fizyczny || pstrona_koniec > pol.maks_koniec_fizyczny)
            return blad(BladElf::AdresPozaZakresem, idx);
        if (!(flagi & ELF_PF_R)) return blad(BladElf::BrakPrawaOdczytu, idx);
        if ((flagi & ELF_PF_W) && (flagi & ELF_PF_X)) return blad(BladElf::ZapisIWykonanie, idx);
        if (plan->liczba_segmentow > 0 && vaddr < poprzedni_vaddr) return blad(BladElf::Nieposortowane, idx);
        poprzedni_vaddr = vaddr;

        SegmentPlanu& s = plan->segmenty[plan->liczba_segmentow];
        s.indeks_ph = i;
        s.prawa = flagi & (ELF_PF_R | ELF_PF_W | ELF_PF_X);
        s.przesuniecie_pliku = off;
        s.adres_fizyczny = paddr;
        s.adres_wirtualny = vaddr;
        s.bajty_z_pliku = filesz;
        s.rozmiar_w_pamieci = memsz;
        s.bajty_zerowane = memsz - filesz;
        s.wyrownanie = align;
        s.strona_poczatek = wyrownaj_w_dol(paddr, ELF_STRONA);
        s.strona_koniec = pstrona_koniec;
        s.liczba_stron = (pstrona_koniec - s.strona_poczatek) / ELF_STRONA;

        // Kolizje z wczesniejszymi segmentami: wspolne bajty (fizycznie lub wirtualnie) zawsze bledem;
        // wspolna strona dopuszczalna tylko przy identycznych prawach (jedna strona = jedne prawa).
        const uint64_t vstrona_poczatek = wyrownaj_w_dol(vaddr, ELF_STRONA);
        for (uint32_t j = 0; j < plan->liczba_segmentow; ++j) {
            const SegmentPlanu& o = plan->segmenty[j];
            if (nakladaja_sie(paddr, pkoniec, o.adres_fizyczny, o.adres_fizyczny + o.rozmiar_w_pamieci) ||
                nakladaja_sie(vaddr, vkoniec, o.adres_wirtualny, o.adres_wirtualny + o.rozmiar_w_pamieci))
                return blad(BladElf::Nakladanie, idx);
            const uint64_t ov0 = wyrownaj_w_dol(o.adres_wirtualny, ELF_STRONA);
            const uint64_t ov1 = (o.adres_wirtualny + o.rozmiar_w_pamieci + ELF_STRONA - 1) & ~(ELF_STRONA - 1);
            const bool wspolna = nakladaja_sie(s.strona_poczatek, s.strona_koniec, o.strona_poczatek, o.strona_koniec) ||
                                 nakladaja_sie(vstrona_poczatek, vstrona_koniec, ov0, ov1);
            if (wspolna && o.prawa != s.prawa) return blad(BladElf::KonfliktStrony, idx);
        }
        if (!dodaj_u64(plan->bajty_z_pliku, filesz, &plan->bajty_z_pliku) ||
            !dodaj_u64(plan->bajty_zerowane, s.bajty_zerowane, &plan->bajty_zerowane))
            return blad(BladElf::PrzepelnienieAdresu, idx);
        ++plan->liczba_segmentow;
    }
    if (plan->liczba_segmentow == 0) return blad(BladElf::BrakSegmentow);

    // ---- zakres i pamiec ----
    plan->zakres_poczatek = plan->segmenty[0].strona_poczatek;
    plan->zakres_koniec = plan->segmenty[0].strona_koniec;
    for (uint32_t i = 1; i < plan->liczba_segmentow; ++i) {
        if (plan->segmenty[i].strona_poczatek < plan->zakres_poczatek) plan->zakres_poczatek = plan->segmenty[i].strona_poczatek;
        if (plan->segmenty[i].strona_koniec > plan->zakres_koniec) plan->zakres_koniec = plan->segmenty[i].strona_koniec;
    }
    if (!suma_zakresow(*plan, &plan->pamiec_stron) || plan->pamiec_stron > pol.maks_pamiec)
        return blad(BladElf::PrzekroczonyLimitPamieci);

    // ---- wejscie ----
    bool znalezione = false;
    for (uint32_t i = 0; i < plan->liczba_segmentow; ++i) {
        const SegmentPlanu& s = plan->segmenty[i];
        if (wejscie < s.adres_wirtualny || wejscie - s.adres_wirtualny >= s.rozmiar_w_pamieci) continue;
        znalezione = true;
        if (!(s.prawa & ELF_PF_X)) return blad(BladElf::WejscieNiewykonywalne, int32_t(s.indeks_ph));
        if (wejscie - s.adres_wirtualny >= s.bajty_z_pliku) return blad(BladElf::WejscieWObszarzeZerowanym, int32_t(s.indeks_ph));
        plan->wejscie_wirtualne = wejscie;
        plan->wejscie_fizyczne = s.adres_fizyczny + (wejscie - s.adres_wirtualny);  // < koniec segmentu: bez przepelnienia
        plan->segment_wejscia = i;
        break;
    }
    if (!znalezione) return blad(BladElf::WejsciePozaSegmentem);
    if (plan->wejscie_fizyczne >= pol.maks_wejscie) return blad(BladElf::WejsciePozaZakresem);

    // ---- Multiboot2 (kontrakt wejscia _start w trybie zgodnosci) ----
    if (pol.wymagaj_mb2) {
        const BladElf b = sprawdz_mb2(d, rozmiar, plan);
        if (b != BladElf::Brak) return blad(b);
    }
    return blad(BladElf::Brak);
}

const char* opis_bledu_elf(BladElf b) {
    switch (b) {
        case BladElf::Brak: return "brak bledu";
        case BladElf::ZaKrotki: return "plik krotszy niz naglowek ELF64";
        case BladElf::ZaDuzyPlik: return "plik wiekszy niz limit 64 MiB";
        case BladElf::ZlaSygnatura: return "brak sygnatury ELF";
        case BladElf::NieKlasa64: return "to nie jest ELFCLASS64";
        case BladElf::NieLittleEndian: return "kolejnosc bajtow inna niz little-endian";
        case BladElf::ZlaWersja: return "nieobslugiwana wersja ELF";
        case BladElf::NieobslugiwaneAbi: return "nieobslugiwane EI_OSABI/EI_ABIVERSION";
        case BladElf::ZlaArchitektura: return "architektura inna niz x86-64 (EM_X86_64)";
        case BladElf::NieobslugiwanyTyp: return "typ obiektu inny niz ET_EXEC";
        case BladElf::ZlyRozmiarNaglowka: return "e_ehsize != 64";
        case BladElf::ZlyRozmiarWpisuPh: return "e_phentsize != 56";
        case BladElf::ZlaLiczbaPh: return "niepoprawna liczba naglowkow programu";
        case BladElf::TablicaPhPozaPlikiem: return "tablica naglowkow programu poza plikiem";
        case BladElf::NieobslugiwanySegment: return "segment dynamiczny/INTERP/TLS (nieobslugiwany)";
        case BladElf::ZaDuzoSegmentow: return "za duzo segmentow PT_LOAD";
        case BladElf::ZerowySegment: return "segment PT_LOAD o zerowym rozmiarze";
        case BladElf::PlikWiekszyNizPamiec: return "p_filesz wieksze niz p_memsz";
        case BladElf::SegmentPozaPlikiem: return "dane segmentu poza plikiem";
        case BladElf::ZleWyrownanie: return "p_align nie jest potega dwojki";
        case BladElf::ZlaKongruencja: return "p_vaddr i p_offset niezgodne modulo p_align";
        case BladElf::PrzepelnienieAdresu: return "przepelnienie arytmetyki adresow";
        case BladElf::AdresPozaZakresem: return "segment poza dozwolonym zakresem fizycznym";
        case BladElf::AdresNietozsamosciowy: return "p_paddr != p_vaddr (jadro wymaga tozsamosci)";
        case BladElf::Nieposortowane: return "segmenty PT_LOAD nieposortowane wg p_vaddr";
        case BladElf::Nakladanie: return "segmenty nakladaja sie";
        case BladElf::KonfliktStrony: return "segmenty o roznych prawach na wspolnej stronie";
        case BladElf::ZapisIWykonanie: return "segment jednoczesnie zapisywalny i wykonywalny";
        case BladElf::BrakPrawaOdczytu: return "segment bez prawa odczytu";
        case BladElf::BrakSegmentow: return "brak segmentow PT_LOAD";
        case BladElf::WejsciePozaSegmentem: return "punkt wejscia poza segmentami";
        case BladElf::WejscieNiewykonywalne: return "punkt wejscia w segmencie niewykonywalnym";
        case BladElf::WejscieWObszarzeZerowanym: return "punkt wejscia w obszarze zerowanym (BSS)";
        case BladElf::WejsciePozaZakresem: return "punkt wejscia poza zakresem 32-bitowym";
        case BladElf::PrzekroczonyLimitPamieci: return "przekroczony limit pamieci jadra";
        case BladElf::BrakNaglowkaMb2: return "brak naglowka Multiboot2";
        case BladElf::ZlyNaglowekMb2: return "niepoprawny naglowek Multiboot2";
    }
    return "nieznany blad ELF";
}

}  // namespace brb
