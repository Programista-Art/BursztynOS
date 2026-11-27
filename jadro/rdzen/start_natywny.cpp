/*
 * Bursztyn OS - natywne wejscie 64-bit, czesc C++ (Faza R1B).
 *
 * wejscie_jadra64 (jadro/arch/wejscie64.S) ustawia wlasny stos, GDT i kanarek stosu, po czym wywoluje
 * wejscie_jadra64_c(magic, PA kontraktu, RSP wejscia, RFLAGS wejscia). Tutaj:
 *   1. zbieramy stan procesora (CR0/CR3/CR4, EFER, CS, CPUID) i walidujemy go czysta funkcja
 *      StanWejscia64::waliduj_stan (jadro/arch/stan_wejscia64.h),
 *   2. przyjmujemy kontrakt pamieci rozruchu (PrzyjmijKontraktNatywny w pmm.cpp: kopia, walidacja, CR3, pomiar
 *      podkladu przez tablice przejscia),
 *   3. zbiegamy sie ze wspolnym potokiem: kernel_main_natywny -> start_jadra (kernel.cpp).
 * Kazdy blad: deterministyczna sciezka terminalna (COM1 + cli/hlt), bez powrotu do programu rozruchowego, bez
 * uslug UEFI (po ExitBootServices nie istnieja).
 *
 * Wariant testowy (BURSZTYN_TEST_WEJSCIE64=1, NIGDY w produkcji): r1b_uprzaz_testowa - opis nizej.
 */

#include "jadro/arch/metadane_jadra.h"
#include "jadro/arch/stan_wejscia64.h"
#include "kontrakt_pamieci.h"
#include "pamiec.h"
#if defined(BURSZTYN_TEST_WEJSCIE64) && BURSZTYN_TEST_WEJSCIE64
#include "plan_przejscia.h"
#endif

#include <stdint.h>
#include <stddef.h>

extern "C" void SerialLog(const char* tekst);   // sterowniki/grafika/grafika.cpp: zapis bajtow do COM1 (0x3F8)
extern "C" [[noreturn]] void kernel_main_natywny(const KontraktPamieci::KontraktV1* kontrakt);

namespace {

void serial_wypisz(const char* t) { SerialLog(t); }

/* COM1 115200 8N1 - ta sama minimalna sekwencja co serial_inicjalizuj() w kernel.cpp (wylacznie porty we/wy). */
void serial_inicjalizuj() {
    const uint16_t com1 = 0x3F8;
    const uint8_t sekwencja[][2] = {{1, 0x00}, {3, 0x80}, {0, 0x03}, {1, 0x00}, {3, 0x03}, {2, 0xC7}, {4, 0x0B}};
    for (const auto& z : sekwencja) {
        asm volatile("outb %0, %1" : : "a"(z[1]), "Nd"(static_cast<uint16_t>(com1 + z[0])));
    }
}

void wypisz_hex(uint64_t v) {
    char b[19] = {'0', 'x'};
    const char* c = "0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) b[2 + i] = c[(v >> (60 - 4 * i)) & 0xFU];
    b[18] = 0;
    serial_wypisz(b);
}

[[noreturn]] void awaria(const char* etap, const char* opis, uint64_t kod, uint64_t adres) {
    serial_wypisz("\n[WEJSCIE64] KRYTYCZNY BLAD ");
    serial_wypisz(etap);
    serial_wypisz(": ");
    serial_wypisz(opis ? opis : "?");
    serial_wypisz(" kod=");
    wypisz_hex(kod);
    serial_wypisz(" adres=");
    wypisz_hex(adres);
    serial_wypisz("\n[WEJSCIE64] Start zatrzymany (bez powrotu do programu rozruchowego).\n");
    for (;;) asm volatile("cli; hlt" ::: "memory");
}

void cpuid(uint32_t lisc, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(lisc), "c"(0U));
}

StanWejscia64::Stan zbierz_stan(uint64_t magic, uint64_t kontrakt, uint64_t rsp, uint64_t rflags) {
    StanWejscia64::Stan s{};
    s.magic = magic;
    s.kontrakt = kontrakt;
    s.rsp_wejscia = rsp;
    s.rflags_wejscia = rflags;
    asm volatile("mov %%cr0, %0" : "=r"(s.cr0));
    asm volatile("mov %%cr3, %0" : "=r"(s.cr3));
    asm volatile("mov %%cr4, %0" : "=r"(s.cr4));
    uint32_t lo = 0, hi = 0;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080U));   // EFER istnieje w kazdym procesorze x86-64
    s.efer = (static_cast<uint64_t>(hi) << 32) | lo;
    uint16_t cs = 0;
    asm volatile("mov %%cs, %0" : "=r"(cs));
    s.cs = cs;
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    s.cpuid_1_edx = d;
    cpuid(0x80000000U, &a, &b, &c, &d);
    s.cpuid_max_rozszerzony = a;
    if (a >= 0x80000001U) { cpuid(0x80000001U, &a, &b, &c, &d); s.cpuid_80000001_edx = d; }
    if (s.cpuid_max_rozszerzony >= 0x80000008U) { cpuid(0x80000008U, &a, &b, &c, &d); s.cpuid_80000008_eax = a; }
    return s;
}

} // namespace

extern "C" [[noreturn]] void wejscie_jadra64_c(uint64_t magic, uint64_t kontrakt, uint64_t rsp_wejscia,
                                               uint64_t rflags_wejscia) {
    serial_inicjalizuj();
    const StanWejscia64::Stan stan = zbierz_stan(magic, kontrakt, rsp_wejscia, rflags_wejscia);
    const StanWejscia64::Blad b = StanWejscia64::waliduj_stan(stan, MetadaneJadra::MAGIC_WEJSCIA_NATYWNEGO);
    if (b != StanWejscia64::Blad::OK) {
        awaria("stanu procesora", StanWejscia64::opis(b), static_cast<uint64_t>(b), stan.cr3);
    }
    const KontraktPamieci::KontraktV1* k = PrzyjmijKontraktNatywny(kontrakt, stan.cr3);
    if (!k) {
        const StatusPamieciRozruchu st = PmmStatusRozruchu();
        awaria("kontraktu pamieci", st.opis, st.kod, st.adres);
    }
    serial_wypisz("[WEJSCIE64] stan CPU i kontrakt poprawne; baza fizyczna obrazu=");
    wypisz_hex(k->jadro_baza_fizyczna);
    serial_wypisz(" baza wirtualna=");
    wypisz_hex(k->jadro_baza_wirtualna);
    serial_wypisz("\n");
    kernel_main_natywny(k);
}

#if defined(BURSZTYN_TEST_WEJSCIE64) && BURSZTYN_TEST_WEJSCIE64
/* =====================================================================================================================
 * UPRZAZ TESTOWA R1B (wylacznie wariant testowy jadra; nie jest programem rozruchowym i nie trafia do produkcji).
 *
 * Wywolywana na samym poczatku kernel_main (po boot.S, pod blanketowa tozsamoscia 0..4 GiB, przed jakakolwiek
 * inicjalizacja). Robi to, co w R2 zrobi BRB, ale wylacznie w pamieci, ktorej wlasnosc jest jednoznaczna:
 *   - zrodlo: mapa MB2 programu rozruchowego; po przekazaniu sterowania pamiec typu 1 nalezy do systemu, z wyjatkiem
 *     zasobow rozruchu (obraz, MBI, moduly, framebuffer) - te sa omijane,
 *   - obraz z kolizja z pamiecia firmware (np. ACPI NVS) => uprzaz rezygnuje (sciezka MB2 zatrzyma start w PMM),
 *   - kopia obrazu pod inna baze fizyczna (niewyrownana do 2 MiB, aby wymusic strony 4 KiB), kopia MBI, stos przejscia,
 *     kontrakt i pula tablic w tym samym regionie typu 1; tablice z PlanPrzejscia (W^X, tozsamosc tylko zasobow),
 *   - przelaczenie CR3 i skok do wejscie_jadra64 (r1b_przejscie_testowe w wejscie64.S).
 * Kazdy blad przed przelaczeniem: powrot do zwyklej sciezki MB2.
 * ===================================================================================================================*/

extern "C" uint8_t __kernel_start[], __kernel_end[], __text_start[], __text_end[], __rodata_start[], __rodata_end[];
extern "C" uint8_t __data_start[], __data_end[], __boot_end[], __kernel_dynamic_end[];
extern "C" [[noreturn]] void r1b_przejscie_testowe(uint64_t cr3, uint64_t rsp, uint64_t magic, uint64_t kontrakt);

namespace {

constexpr uint64_t R1B_MIN_BAZA = 0x2000000ULL;      // powyzej okna VA obrazu i modulow niskiej pamieci
constexpr uint64_t R1B_PRZESUNIECIE = 0x5000ULL;     // baza niewyrownana do 2 MiB
constexpr uint32_t R1B_RAMKI_PULI = 64;
constexpr uint64_t R1B_STOS = 0x4000ULL;

void kopiuj_bajty(void* cel, const void* zrodlo, uint64_t n) {
    auto* d = static_cast<volatile uint8_t*>(cel);
    const auto* s = static_cast<const volatile uint8_t*>(zrodlo);
    for (uint64_t i = 0; i < n; ++i) d[i] = s[i];
}

void zeruj(void* cel, uint64_t n) {
    auto* d = static_cast<volatile uint8_t*>(cel);
    for (uint64_t i = 0; i < n; ++i) d[i] = 0;
}

bool koliduje(const KontraktPamieci::KontraktV1& k, uint64_t p, uint64_t n) {
    for (uint32_t i = 0; i < k.liczba_zakresow; ++i) {
        uint64_t pw = k.zakresy[i].poczatek & ~0xFFFULL, kw = 0;
        Adresy::wyrownaj_w_gore_do_strony(k.zakresy[i].poczatek + k.zakresy[i].dlugosc, &kw);
        if (Adresy::zakresy_nachodza(pw, kw - pw, p, n)) return true;
    }
    return Adresy::zakresy_nachodza(k.jadro_baza_wirtualna, k.jadro_rozmiar, p, n);   // cien okna VA
}

void log(const char* t) { serial_wypisz("[R1B-TEST] "); serial_wypisz(t); serial_wypisz("\n"); }

KontraktPamieci::KontraktV1 kontrakt_mb2;   // .bss obrazu oryginalnego (tylko przed przelaczeniem)

} // namespace

extern "C" void r1b_uprzaz_testowa(uint64_t magic, uint64_t mbi) {
    serial_inicjalizuj();
    log("uprzaz wejscia natywnego (wariant testowy) - start");
    if (magic != 0x36D76289ULL || mbi == 0) { log("brak MB2 - rezygnacja"); return; }
    const KontraktPamieci::SymboleObrazu sym{
        reinterpret_cast<uint64_t>(__kernel_start), reinterpret_cast<uint64_t>(__boot_end),
        reinterpret_cast<uint64_t>(__text_start), reinterpret_cast<uint64_t>(__text_end),
        reinterpret_cast<uint64_t>(__rodata_start), reinterpret_cast<uint64_t>(__rodata_end),
        reinterpret_cast<uint64_t>(__data_start), reinterpret_cast<uint64_t>(__kernel_dynamic_end),
        reinterpret_cast<uint64_t>(__kernel_end)};
    // Kontekst: boot.S, blanketowa tozsamosc 0..4 GiB, obraz zaladowany pod PA == VA (wymog MB2/boot.S).
    auto dostep = [](uint64_t pa, uint64_t n) -> const uint8_t* {
        return (pa && n && pa + n <= 0x100000000ULL && pa + n > pa) ? reinterpret_cast<const uint8_t*>(pa) : nullptr;
    };
    auto tozsamosc = [](uint64_t va, uint64_t* pa) { *pa = va; return true; };
    KontraktPamieci::KontraktV1& m = kontrakt_mb2;
    if (KontraktPamieci::zbuduj_z_multiboot2(mbi, sym, 0, dostep, tozsamosc, &m) != KontraktPamieci::Blad::OK) {
        log("adapter MB2 odrzucil MBI - rezygnacja");
        return;
    }
    const uint8_t* mapa = dostep(m.mapa_adres, m.mapa_rozmiar);
    if (!mapa ||
        KontraktPamieci::klasyfikuj_wobec_mapy_mb2(mapa, m.mapa_rozmiar, m.jadro_baza_fizyczna, m.jadro_rozmiar).blad !=
            KontraktPamieci::Blad::OK) {
        log("obraz MB2 koliduje z pamiecia nie-RAM (np. ACPI NVS) - rezygnacja, sciezka MB2 zatrzyma start");
        return;
    }
    const uint64_t rozmiar = m.jadro_rozmiar;
    uint64_t mbi_strony = 0;
    Adresy::wyrownaj_w_gore_do_strony(m.informacje_rozmiar, &mbi_strony);
    const uint64_t potrzeba = rozmiar + R1B_RAMKI_PULI * 0x1000ULL + R1B_STOS + 0x1000ULL + mbi_strony;
    // Wybor regionu typu 1 (pierwsze dopasowanie, kroki 2 MiB, ograniczona liczba prob).
    uint32_t rozmiar_wpisu = 0;
    kopiuj_bajty(&rozmiar_wpisu, mapa + 8, 4);
    const uint64_t wpisy = (m.mapa_rozmiar - 16) / rozmiar_wpisu;
    uint64_t baza = 0;
    for (uint64_t i = 0; i < wpisy && !baza; ++i) {
        uint64_t b = 0, dl = 0;
        uint32_t typ = 0;
        kopiuj_bajty(&b, mapa + 16 + i * rozmiar_wpisu, 8);
        kopiuj_bajty(&dl, mapa + 16 + i * rozmiar_wpisu + 8, 8);
        kopiuj_bajty(&typ, mapa + 16 + i * rozmiar_wpisu + 16, 4);
        if (typ != 1 || dl == 0 || b > UINT64_MAX - dl) continue;
        const uint64_t koniec = b + dl < 0x100000000ULL ? b + dl : 0x100000000ULL;
        uint64_t kand = b > R1B_MIN_BAZA ? b : R1B_MIN_BAZA;
        kand = ((kand + 0x1FFFFFULL) & ~0x1FFFFFULL) + R1B_PRZESUNIECIE;
        for (int proba = 0; proba < 512 && kand + potrzeba <= koniec; ++proba, kand += 0x200000ULL) {
            if (!koliduje(m, kand, potrzeba)) { baza = kand; break; }
        }
    }
    if (!baza) { log("brak wolnego regionu typu 1 - rezygnacja"); return; }
    const uint64_t pula = baza + rozmiar;
    const uint64_t stos = pula + R1B_RAMKI_PULI * 0x1000ULL;
    const uint64_t kontrakt_pa = stos + R1B_STOS;
    const uint64_t mbi_kopia = kontrakt_pa + 0x1000ULL;

    // 1. Kopia obrazu: dane z pliku z biezacego obrazu (.text/.rodata niezmienione, .data przed inicjalizacja jadra),
    //    BSS i luz za nim wyzerowane (kontrakt: BSS zerowy).
    const uint64_t z_pliku = reinterpret_cast<uint64_t>(__data_end) - sym.kernel_start;
    kopiuj_bajty(reinterpret_cast<void*>(baza), __kernel_start, z_pliku);
    zeruj(reinterpret_cast<void*>(baza + z_pliku), rozmiar - z_pliku);
    // 2. Kopia MBI (oryginal lezy w cieniu okna VA obrazu: GRUB BIOS kladzie go pod 0x100080).
    kopiuj_bajty(reinterpret_cast<void*>(mbi_kopia), reinterpret_cast<const void*>(mbi), m.informacje_rozmiar);

    // 3. Kontrakt natywny.
    KontraktPamieci::KontraktV1 k{};
    k.sygnatura = KontraktPamieci::SYGNATURA;
    k.wersja = KontraktPamieci::WERSJA;
    k.rozmiar = sizeof(KontraktPamieci::KontraktV1);
    k.zrodlo = KontraktPamieci::ZRODLO_NATYWNE_64;
    k.flagi = KontraktPamieci::FLAGA_TOZSAMOSC_ZASOBOW;
    k.model_podkladu = KontraktPamieci::PODKLAD_CIAGLY;
    k.jadro_baza_wirtualna = m.jadro_baza_wirtualna;
    k.jadro_baza_fizyczna = baza;
    k.jadro_rozmiar = rozmiar;
    k.cr3_rozruchu = pula;   // pierwsza ramka puli = PML4 (PlanPrzejscia przydziela ja jako pierwsza)
    k.mapa_adres = mbi_kopia + (m.mapa_adres - mbi);
    k.mapa_rozmiar = m.mapa_rozmiar;
    k.mapa_format = m.mapa_format;
    k.mapa_rozmiar_wpisu = m.mapa_rozmiar_wpisu;
    k.informacje_adres = mbi_kopia;
    k.informacje_rozmiar = m.informacje_rozmiar;
    k.liczba_segmentow = m.liczba_segmentow;
    for (uint32_t i = 0; i < m.liczba_segmentow; ++i) k.segmenty[i] = m.segmenty[i];
    auto dodaj = [&](uint64_t p, uint64_t n, uint32_t r, uint32_t z, uint32_t f) {
        k.zakresy[k.liczba_zakresow++] = {p, n, r, z, f, 0};
    };
    dodaj(baza, rozmiar, KontraktPamieci::RODZAJ_OBRAZ_JADRA, KontraktPamieci::ZYCIE_TRWALE, 0);
    dodaj(mbi_kopia, m.informacje_rozmiar, KontraktPamieci::RODZAJ_INFORMACJE_ROZRUCHU, KontraktPamieci::ZYCIE_TRWALE,
          KontraktPamieci::ZAKRES_DOSTEP_TOZSAMOSCIOWY);
    dodaj(pula, R1B_RAMKI_PULI * 0x1000ULL, KontraktPamieci::RODZAJ_TABLICE_STRON_ROZRUCHU,
          KontraktPamieci::ZYCIE_DO_PRZELACZENIA_CR3, 0);
    dodaj(stos, R1B_STOS + 0x1000ULL, KontraktPamieci::RODZAJ_ODZYSKIWALNY_ROZRUCHU,
          KontraktPamieci::ZYCIE_DO_PRZELACZENIA_CR3, 0);
    for (uint32_t i = 0; i < m.liczba_zakresow; ++i) {
        const KontraktPamieci::Zakres& z = m.zakresy[i];
        if (z.rodzaj == KontraktPamieci::RODZAJ_MODUL || z.rodzaj == KontraktPamieci::RODZAJ_FRAMEBUFFER) {
            if (k.liczba_zakresow >= KontraktPamieci::MAKS_ZAKRESOW) { log("za duzo zakresow - rezygnacja"); return; }
            k.zakresy[k.liczba_zakresow++] = z;
        }
    }
    const KontraktPamieci::OczekiwanyObraz ocz = KontraktPamieci::oczekiwany_z_symboli(sym);
    ObrazJadra::Uklad uklad{};
    const KontraktPamieci::Blad bk = KontraktPamieci::waliduj(&k, sizeof(k), &ocz, &uklad);
    if (bk != KontraktPamieci::Blad::OK) { log("kontrakt natywny odrzucony przez walidator - rezygnacja"); log(KontraktPamieci::opis_bledu(bk)); return; }
    kopiuj_bajty(reinterpret_cast<void*>(kontrakt_pa), &k, sizeof(k));

    // 4. CPU: NX i szerokosc fizyczna z CPUID; EFER.NXE przed tablicami z bitem 63.
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000U), "c"(0U));
    const uint32_t maks = a;
    bool nx = false;
    unsigned bity = 36;
    if (maks >= 0x80000001U) { asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001U), "c"(0U)); nx = (d >> 20) & 1U; }
    if (maks >= 0x80000008U) { asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000008U), "c"(0U)); bity = a & 0xFFU; }

    // 5. Tablice przejscia: okno -> kopia; tozsamosc: MBI (R), moduly (R), stos (RW), kontrakt (R); pula (R).
    PlanPrzejscia::ZakresTozsamosci t[KontraktPamieci::MAKS_ZAKRESOW + 4]{};
    uint32_t nt = 0;
    t[nt++] = {mbi_kopia, m.informacje_rozmiar, PlanPrzejscia::KLASA_TYLKO_ODCZYT, 0};
    t[nt++] = {stos, R1B_STOS, PlanPrzejscia::KLASA_STOS, 0};
    t[nt++] = {kontrakt_pa, sizeof(k), PlanPrzejscia::KLASA_TYLKO_ODCZYT, 0};
    for (uint32_t i = 0; i < m.liczba_zakresow; ++i) {
        if (m.zakresy[i].rodzaj == KontraktPamieci::RODZAJ_MODUL) {
            t[nt++] = {m.zakresy[i].poczatek, m.zakresy[i].dlugosc, PlanPrzejscia::KLASA_TYLKO_ODCZYT, 0};
        }
    }
    const PlanPrzejscia::Parametry par{&uklad, t, nt, nx, bity, pula, reinterpret_cast<uint64_t*>(pula), R1B_RAMKI_PULI};
    PlanPrzejscia::Wynik w{};
    const PlanPrzejscia::Blad bp = PlanPrzejscia::zbuduj(par, &w);
    if (bp != PlanPrzejscia::Blad::OK || w.cr3 != pula) { log("plan tablic przejscia odrzucony - rezygnacja"); log(PlanPrzejscia::opis(bp)); return; }

    serial_wypisz("[R1B-TEST] przeniesienie: PA obrazu ");
    wypisz_hex(sym.kernel_start);
    serial_wypisz(" -> ");
    wypisz_hex(baza);
    serial_wypisz(", VA bez zmian; ramek tablic=");
    wypisz_hex(w.uzyte_ramki);
    serial_wypisz(nx ? ", NX\n" : ", bez NX\n");
    if (nx) {
        uint32_t lo = 0, hi = 0;
        asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080U));
        lo |= 1U << 11;
        asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0xC0000080U));
    }
    // 6. Przelaczenie CR3 i skok do natywnego wejscia (bez powrotu).
    r1b_przejscie_testowe(pula, stos + R1B_STOS, MetadaneJadra::MAGIC_WEJSCIA_NATYWNEGO, kontrakt_pa);
}
#endif
