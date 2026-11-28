#include "ladowanie_natywne.h"

#include "mapa_uefi.h"
#include "../wspolne/arytmetyka.h"
#include "../wspolne/pamiec_podstawowa.h"
#include "../../../jadro/mm/kontrakt_pamieci.h"
#include "../../../jadro/mm/plan_przejscia.h"

namespace brb {

namespace {

constexpr uint64_t STRONA = 0x1000;

struct KontekstZwalniania {
    EFI_BOOT_SERVICES* bs;
    LogSzeregowy* log;
};

bool zwolnij_strony(void* ctx, uint64_t adres, uint64_t strony) {
    auto* k = static_cast<KontekstZwalniania*>(ctx);
    const EFI_STATUS s = k->bs->FreePages(adres, UINTN(strony));
    if (EFI_ERROR(s)) {
        k->log->tekst("[BRB] R2 sprzatanie: FreePages ");
        k->log->hex(adres);
        k->log->tekst(" strony=");
        k->log->dziesietnie(strony);
        k->log->tekst(" nieudane status=");
        k->log->hex(s);
        k->log->linia(" - zasob porzucony, bez ponownego zwalniania");
        return false;
    }
    return true;
}

void log_zakres(LogSzeregowy& log, uint64_t a, uint64_t strony) {
    log.hex(a);
    log.znak('-');
    log.hex(a + strony * STRONA);
    log.tekst(" strony=");
    log.dziesietnie(strony);
}

struct KontekstNvs {
    LogSzeregowy* log;
    uint32_t zakresy;
    uint64_t bajty;
    const RejestrZasobow* rejestr;
    bool kolizja;
};

void wpis_nvs(void* ctx, const WpisMapy& e) {
    auto* k = static_cast<KontekstNvs*>(ctx);
    if (e.klasa != KlasaPamieci::AcpiNvs) return;
    ++k->zakresy;
    k->bajty += e.koniec - e.poczatek;
    k->log->tekst("[BRB] R2 mapa: ACPI NVS ");
    log_zakres(*k->log, e.poczatek, e.strony);
    k->log->znak('\n');
    if (k->rejestr) {
        for (uint32_t i = 0; i < k->rejestr->liczba; ++i) {
            const WpisZasobu& w = k->rejestr->wpisy[i];
            if (w.wlasny && !w.chroniony && w.adres < e.koniec && e.poczatek < w.adres + w.strony * STRONA) k->kolizja = true;
        }
    }
}

struct KontekstDowodow {
    LogSzeregowy* log;
    uint32_t licznik;
};

void wpis_dowodu(void* ctx, const WpisMapy& e) {
    auto* k = static_cast<KontekstDowodow*>(ctx);
    if (++k->licznik > 8) return;
    k->log->tekst("[BRB] R2 mapa:   typ=");
    k->log->dziesietnie(e.typ);
    k->log->znak(' ');
    k->log->tekst(nazwa_klasy(e.klasa));
    k->log->znak(' ');
    log_zakres(*k->log, e.poczatek, e.strony);
    k->log->znak('\n');
}

// Migawka: zakresy ACPI NVS (dowod) i - gdy rejestr podany - deskryptory pokrywajace wlasne zasoby oraz kontrola,
// ze zaden wlasny zasob nie lezy na NVS. Bufor migawki zwalniany zawsze.
WynikMapy migawka(EFI_BOOT_SERVICES* bs, const RejestrZasobow* rejestr, const char* etykieta, LogSzeregowy& log,
                  RaportR2* r, bool* kolizja_nvs) {
    MigawkaMapy m;
    const WynikMapy w = pobierz_migawke_mapy(bs, &m);
    if (w.blad != BladPobraniaMapy::Brak) {
        log.tekst("[BRB] R2 mapa: blad pobrania - ");
        log.linia(opis_bledu_pobrania_mapy(w.blad));
        return w;
    }
    log.tekst("[BRB] R2 mapa (");
    log.tekst(etykieta);
    log.tekst(", migawka - nie ostateczna mapa ExitBootServices) wpisy=");
    log.dziesietnie(m.widok.liczba);
    log.tekst(" strony_acpi_nvs=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::AcpiNvs)]);
    log.znak('\n');
    KontekstNvs kn{&log, 0, 0, rejestr, false};
    (void)wpisy_w_zakresie(m.widok, 0, ~0ull, wpis_nvs, &kn);
    r->zakresy_nvs = kn.zakresy;
    r->bajty_nvs = kn.bajty;
    if (kolizja_nvs) *kolizja_nvs = kn.kolizja;
    if (rejestr) {
        for (uint32_t i = 0; i < rejestr->liczba; ++i) {
            const WpisZasobu& z = rejestr->wpisy[i];
            if (!z.wlasny || z.chroniony) continue;
            log.tekst("[BRB] R2 mapa: zasob ");
            log.tekst(nazwa_zasobu(z.rodzaj));
            log.znak(' ');
            log_zakres(log, z.adres, z.strony);
            log.linia(" pokrywajace deskryptory:");
            KontekstDowodow kd{&log, 0};
            (void)wpisy_w_zakresie(m.widok, z.adres, z.adres + z.strony * STRONA, wpis_dowodu, &kd);
        }
    }
    const EFI_STATUS sz = zwolnij_migawke_mapy(bs, &m);
    if (EFI_ERROR(sz)) {
        ++r->bledy_sprzatania;
        log.tekst("[BRB] R2 sprzatanie: FreePages bufora mapy nieudane status=");
        log.hex(sz);
        log.znak('\n');
    }
    return w;
}

// Przydzial < 4 GiB przez firmware + rejestracja. Przydzial niezarejestrowany (kolizja) jest natychmiast zwalniany.
EFI_STATUS przydziel(EFI_BOOT_SERVICES* bs, RejestrZasobow* rej, EFI_MEMORY_TYPE typ, UINTN strony, RodzajZasobu rodzaj,
                     uint64_t* adres, LogSzeregowy& log, bool* kolizja) {
    *kolizja = false;
    EFI_PHYSICAL_ADDRESS a = UMIESZCZENIE_LIMIT_TYMCZASOWY - 1;
    const EFI_STATUS s = bs->AllocatePages(AllocateMaxAddress, typ, strony, &a);
    if (EFI_ERROR(s)) return s;
    int32_t k = -1;
    const BladRejestru br = rejestr_dodaj(rej, a, strony, rodzaj, false, &k);
    if (br != BladRejestru::Brak) {
        *kolizja = true;
        log.tekst("[BRB] R2: przydzial ");
        log.tekst(nazwa_zasobu(rodzaj));
        log.tekst(" koliduje z zywym zasobem albo rejestr pelny - natychmiastowe zwolnienie\n");
        const EFI_STATUS sz = bs->FreePages(a, strony);
        if (EFI_ERROR(sz)) log.linia("[BRB] R2: zwolnienie kolidujacego przydzialu nieudane");
        return EFI_ABORTED;
    }
    *adres = a;
    log.tekst("[BRB] R2: przydzielono ");
    log.tekst(nazwa_zasobu(rodzaj));
    log.znak(' ');
    log_zakres(log, a, strony);
    log.tekst(typ == EfiLoaderCode ? " EfiLoaderCode" : " EfiLoaderData");
    log.linia(" AllocateMaxAddress<4GiB");
    return EFI_SUCCESS;
}

uint32_t prawa_elf_na_obraz(uint32_t prawa) {
    uint32_t u = 0;
    if (prawa & ELF_PF_R) u |= ObrazJadra::ODCZYT;
    if (prawa & ELF_PF_W) u |= ObrazJadra::ZAPIS;
    if (prawa & ELF_PF_X) u |= ObrazJadra::WYKONANIE;
    return u;
}

EFI_STATUS koniec(RaportR2* r, LogSzeregowy& log, BladR2 b, EFI_STATUS s) {
    r->blad = b;
    r->status = s;
    if (b != BladR2::Brak) {
        log.tekst("[BRB] blad fazy R2: BRB-E6");
        log.znak(char('0' + (uint32_t(b) / 10) % 10));
        log.znak(char('0' + uint32_t(b) % 10));
        log.znak(' ');
        log.tekst(opis_bledu_r2(b));
        if (r->blad_umiejscowienia != BladUmiejscowienia::Brak) { log.tekst(" ("); log.tekst(opis_bledu_umiejscowienia(r->blad_umiejscowienia)); log.znak(')'); }
        if (r->blad_ladowania != BladLadowania::Brak) { log.tekst(" ("); log.tekst(opis_bledu_ladowania(r->blad_ladowania)); log.znak(')'); }
        if (r->blad_tablic != BladTablic::Brak) {
            log.tekst(" (");
            log.tekst(opis_bledu_tablic(r->blad_tablic));
            log.tekst(" adres=");
            log.hex(r->tablice.adres_bledu);
            log.znak(')');
        }
        if (r->blad_planu) { log.tekst(" (plan: "); log.tekst(PlanPrzejscia::opis(PlanPrzejscia::Blad(r->blad_planu))); log.znak(')'); }
        log.tekst(" status=");
        log.hex(s);
        log.znak('\n');
    }
    return s;
}

}  // namespace

CechyProcesora odczytaj_cechy_procesora() {
    CechyProcesora c{36, false, false, false};
    uint32_t a = 0, b = 0, cx = 0, d = 0;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(cx), "=d"(d) : "a"(0x80000000u), "c"(0u));
    const uint32_t maks = a;
    if (maks >= 0x80000001u) {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(cx), "=d"(d) : "a"(0x80000001u), "c"(0u));
        c.nx = (d >> 20) & 1u;
        c.long_mode = (d >> 29) & 1u;
    }
    if (maks >= 0x80000008u) {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(cx), "=d"(d) : "a"(0x80000008u), "c"(0u));
        if (a & 0xFFu) c.bity_fizyczne = a & 0xFFu;
    }
#ifndef BRB_TEST_HOSTA
    uint64_t cr4 = 0;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));   // wylacznie odczyt (BRB dziala w CPL 0 pod UEFI)
    c.la57_aktywne = (cr4 >> 12) & 1u;
#endif
    return c;
}

const char* opis_bledu_r2(BladR2 b) {
    switch (b) {
        case BladR2::Brak: return "brak bledu";
        case BladR2::BrakFunkcji: return "brak uslug firmware albo danych";
        case BladR2::Procesor: return "procesor nie spelnia kontraktu natywnego (NX, Long Mode, LA57, MAXPHYADDR)";
        case BladR2::Wymagania: return "plan albo metadane niezgodne z modelem ciaglego podkladu";
        case BladR2::MapaPamieci: return "mapa pamieci niedostepna";
        case BladR2::BrakPamieciCiaglej: return "firmware nie przydzielilo ciaglego podkladu ponizej 4 GiB";
        case BladR2::Umiejscowienie: return "przydzial firmware nie spelnia wymagan umieszczenia";
        case BladR2::Rejestr: return "przydzial koliduje z zywym zasobem BRB";
        case BladR2::Przesuniecie: return "przesuniecie planu na baze fizyczna odrzucone";
        case BladR2::Ladowanie: return "ladowanie segmentow odrzucone przez kontrole";
        case BladR2::Weryfikacja: return "weryfikacja zaladowanego obrazu nieudana";
        case BladR2::PrzydzialZasobu: return "przydzial zasobu przejscia nieudany";
        case BladR2::UkladObrazu: return "uklad obrazu dla tablic przejscia odrzucony";
        case BladR2::PlanTablic: return "plan tablic przejscia odrzucony";
        case BladR2::WeryfikacjaTablic: return "niezalezna weryfikacja tablic przejscia nieudana";
        case BladR2::NakladanieNaNvs: return "wlasny przydzial nachodzi na ACPI NVS";
        case BladR2::SprzatanieNieudane: return "zwolnienie zasobow nieudane";
    }
    return "nieznany blad fazy R2";
}

EFI_STATUS faza_r2_zaladuj_natywnie(EFI_BOOT_SERVICES* bs, const EFI_LOADED_IMAGE_PROTOCOL* obraz, const BuforJadra& bufor,
                                    const PlanLadowania& plan, const MetadaneWejscia& meta, const CechyProcesora& cpu,
                                    const DostepFizyczny& pamiec, LogSzeregowy& log, RaportR2* r) {
    if (!r) return EFI_INVALID_PARAMETER;
    brb_memset(r, 0, sizeof(*r));
    r->ladowanie.segment_bledu = -1;
    rejestr_inicjalizuj(&r->rejestr);
    if (!bs || !bs->AllocatePages || !bs->FreePages || !bs->GetMemoryMap || !obraz || bufor.adres == 0 || !pamiec.wskaznik)
        return koniec(r, log, BladR2::BrakFunkcji, EFI_UNSUPPORTED);
    log.linia("[BRB] etap R2: elastyczne umieszczenie fizyczne jadra, ladowanie i tablice przejscia (bez uruchamiania jadra)");

    // ---- CPU (odczyt; BRB niczego nie przelacza) ----
    log.tekst("[BRB] R2 cpu: MAXPHYADDR=");
    log.dziesietnie(cpu.bity_fizyczne);
    log.tekst(cpu.nx ? " NX=tak" : " NX=nie");
    log.tekst(cpu.long_mode ? " LM=tak" : " LM=nie");
    log.linia(cpu.la57_aktywne ? " LA57=aktywne" : " LA57=nieaktywne");
    if (!cpu.nx || !cpu.long_mode || cpu.la57_aktywne || cpu.bity_fizyczne < 36 || cpu.bity_fizyczne > 52)
        return koniec(r, log, BladR2::Procesor, EFI_UNSUPPORTED);

    // ---- Wymagania obrazu (metadane + plan Fazy 3) ----
    r->blad_umiejscowienia = wymagania_obrazu(plan, meta, &r->wymagania);
    if (r->blad_umiejscowienia != BladUmiejscowienia::Brak) return koniec(r, log, BladR2::Wymagania, EFI_LOAD_ERROR);
    const WymaganiaObrazu& wym = r->wymagania;
    r->etap = EtapR2::WymaganiaGotowe;
    log.tekst("[BRB] R2: baza_wirtualna=");
    log.hex(wym.baza_wirtualna);
    log.tekst(" rozmiar=");
    log.dziesietnie(wym.rozmiar);
    log.tekst(" B wyrownanie=");
    log.hex(wym.wyrownanie);
    log.tekst(" strony_przydzialu=");
    log.dziesietnie(wym.strony_przydzialu);
    log.tekst(" wejscie64=");
    log.hex(meta.wejscie64);
    log.linia(" (p_paddr ELF nie jest adresem docelowym)");

    // ---- Rejestr: zakresy chronione (obraz BRB, bufor zrodla) ----
    {
        const uint64_t o0 = reinterpret_cast<uintptr_t>(obraz->ImageBase) & ~(STRONA - 1);
        uint64_t o1 = 0;
        if (!wyrownaj_w_gore(reinterpret_cast<uintptr_t>(obraz->ImageBase) + obraz->ImageSize, STRONA, &o1) ||
            rejestr_dodaj(&r->rejestr, o0, (o1 - o0) / STRONA, RodzajZasobu::ObrazBrb, true, nullptr) != BladRejestru::Brak ||
            rejestr_dodaj(&r->rejestr, bufor.adres, bufor.strony, RodzajZasobu::BuforZrodla, true, nullptr) != BladRejestru::Brak)
            return koniec(r, log, BladR2::Rejestr, EFI_LOAD_ERROR);
    }

    // ---- Migawka przed przydzialami (dowody NVS) ----
    {
        const WynikMapy wm = migawka(bs, nullptr, "przed przydzialem", log, r, nullptr);
        if (wm.blad != BladPobraniaMapy::Brak) return koniec(r, log, BladR2::MapaPamieci, wm.status);
    }

    KontekstZwalniania kz{bs, &log};
    BladR2 blad = BladR2::Brak;
    EFI_STATUS status = EFI_SUCCESS;
    auto porazka = [&](BladR2 b, EFI_STATUS s) { blad = b; status = s; };

#if defined(BRB_TEST_R2) && BRB_TEST_R2 == 4
    // TYLKO wariant testowy: prawowity przydzial stron ponizej 4 GiB ze wzorcem straznika, zanim BRB poprosi o
    // podklad. Firmware musi umiescic jadro gdzie indziej; wzorzec jest sprawdzany po zakonczeniu R2.
    uint64_t straznik = 0;
    {
        bool kol = false;
        if (przydziel(bs, &r->rejestr, EfiLoaderData, UINTN(wym.strony_przydzialu), RodzajZasobu::TestowyStraznik, &straznik,
                      log, &kol) == EFI_SUCCESS) {
            uint8_t* p = pamiec.wskaznik(pamiec.kontekst, straznik, wym.strony_przydzialu * STRONA);
            if (p) for (uint64_t i = 0; i < wym.strony_przydzialu * STRONA; ++i) p[i] = uint8_t(0xA5 ^ (i * 31));
            for (uint32_t i = 0; i < r->rejestr.liczba; ++i)
                if (r->rejestr.wpisy[i].rodzaj == RodzajZasobu::TestowyStraznik) r->rejestr.wpisy[i].chroniony = true;
            log.linia("[BRB] R2 TEST: straznik zajety przed przydzialem podkladu");
        }
    }
#endif

    // ---- Przydzial ciaglego podkladu (AllocateMaxAddress, z dopelnieniem wyrownania) ----
    uint64_t przydzial = 0;
    {
        UINTN strony = UINTN(wym.strony_przydzialu);
#if defined(BRB_TEST_R2) && BRB_TEST_R2 == 1
        strony = UINTN(wym.strony_przydzialu) << 20;   // TYLKO test: zadanie przekraczajace pamiec -> odmowa firmware
#endif
        bool kol = false;
        EFI_STATUS s = EFI_SUCCESS;
#if defined(BRB_TEST_R2) && BRB_TEST_R2 == 2
        (void)strony;
        s = EFI_OUT_OF_RESOURCES;   // TYLKO test: wstrzyknieta odmowa przydzialu (bez wywolania firmware)
#else
        s = przydziel(bs, &r->rejestr, EfiLoaderData, strony, RodzajZasobu::PodkladJadra, &przydzial, log, &kol);
#endif
        if (EFI_ERROR(s)) {
            r->status = s;
            log.tekst("[BRB] R2: AllocateMaxAddress podkladu odrzucony status=");
            log.hex(s);
            log.znak('\n');
            porazka(kol ? BladR2::Rejestr : BladR2::BrakPamieciCiaglej, kol ? EFI_LOAD_ERROR : s);
        }
    }
    if (blad == BladR2::Brak) {
        r->blad_umiejscowienia = wybierz_baze(wym, przydzial, wym.strony_przydzialu, UMIESZCZENIE_LIMIT_TYMCZASOWY,
                                              cpu.bity_fizyczne, &r->umiejscowienie);
        if (r->blad_umiejscowienia != BladUmiejscowienia::Brak) porazka(BladR2::Umiejscowienie, EFI_LOAD_ERROR);
    }
    PlanLadowania plan_p;
    brb_memset(&plan_p, 0, sizeof(plan_p));
    uint64_t baza = r->umiejscowienie.baza;
    if (blad == BladR2::Brak) {
        r->etap = EtapR2::PodkladPrzydzielony;
        log.tekst("[BRB] R2: baza fizyczna obrazu=");
        log.hex(baza);
        log.tekst(" (VA ");
        log.hex(wym.baza_wirtualna);
        log.tekst(") dopelnienie_przed=");
        log.dziesietnie(r->umiejscowienie.dopelnienie_przed);
        log.tekst(" dopelnienie_po=");
        log.dziesietnie(r->umiejscowienie.dopelnienie_po);
        log.linia(" B (posiadane, niezapisywane)");
        r->blad_ladowania = przesun_plan(plan, baza, &plan_p);
        if (r->blad_ladowania != BladLadowania::Brak) porazka(BladR2::Przesuniecie, EFI_LOAD_ERROR);
    }
    if (blad == BladR2::Brak) {
        for (uint32_t i = 0; i < plan_p.liczba_segmentow; ++i) {
            const SegmentPlanu& s = plan_p.segmenty[i];
            log.tekst("[BRB] R2: seg");
            log.dziesietnie(i);
            log.tekst(" wirt=");
            log.hex(s.adres_wirtualny);
            log.tekst(" p_paddr=");
            log.hex(plan.segmenty[i].adres_fizyczny);
            log.tekst(" offset=");
            log.hex(s.adres_wirtualny - wym.baza_wirtualna);
            log.tekst(" cel_fiz=");
            log.hex(s.adres_fizyczny);
            log.tekst(" pamiec=");
            log.dziesietnie(s.rozmiar_w_pamieci);
            log.znak('\n');
        }
        // Kopiowanie: jedyny zakres celu = obraz w posiadanym przydziale (dopelnienie wyrownania nie jest zapisywane).
        ZakresyPrzydzialu posiadane;
        brb_memset(&posiadane, 0, sizeof(posiadane));
        posiadane.liczba = 1;
        posiadane.zakresy[0] = ZakresStron{baza, wym.strony};
        posiadane.strony_lacznie = wym.strony;
        if (!rejestr_posiada(r->rejestr, baza, wym.rozmiar, RodzajZasobu::PodkladJadra)) {
            porazka(BladR2::Rejestr, EFI_LOAD_ERROR);
        } else {
            const ZrodloJadra zrodlo{bufor.dane(), bufor.rozmiar, bufor.adres, uint64_t(bufor.strony) * STRONA};
            r->blad_ladowania = zaladuj_segmenty(plan_p, POLITYKA_JADRA_BURSZTYN, zrodlo, posiadane, pamiec, &r->ladowanie, &baza);
            if (r->blad_ladowania != BladLadowania::Brak) {
                porazka(BladR2::Ladowanie, EFI_LOAD_ERROR);
            } else {
                r->etap = EtapR2::SegmentySkopiowane;
#if defined(BRB_TEST_R2) && BRB_TEST_R2 == 5
                // TYLKO test: zmiana jednego bajtu .text w POSIADANYM obrazie - weryfikacja musi to wykryc.
                if (uint8_t* p = pamiec.wskaznik(pamiec.kontekst, plan_p.wejscie_fizyczne, 1)) *p ^= 0xFF;
#endif
                r->blad_ladowania = weryfikuj_zaladowany_obraz(plan_p, zrodlo, posiadane, pamiec, &r->ladowanie);
                for (uint32_t i = 0; i < r->ladowanie.liczba; ++i) {
                    const RaportSegmentu& s = r->ladowanie.segmenty[i];
                    log.tekst("[BRB] R2: weryfikacja seg");
                    log.dziesietnie(i);
                    log.tekst(" fiz=");
                    log.hex(s.adres);
                    log.tekst(" z_pliku=");
                    log.dziesietnie(s.z_pliku);
                    log.tekst(" bss=");
                    log.dziesietnie(s.bss);
                    log.tekst(s.dane_ok ? " dane=OK" : " dane=BLAD");
                    log.linia(s.bss_ok ? " bss=OK" : " bss=BLAD");
                }
                if (r->blad_ladowania != BladLadowania::Brak) {
                    log.tekst("[BRB] R2: weryfikacja nieudana adres=");
                    log.hex(r->ladowanie.adres_bledu);
                    log.znak('\n');
                    porazka(BladR2::Weryfikacja, EFI_ABORTED);
                } else {
                    r->etap = EtapR2::ObrazZweryfikowany;
                    log.tekst("[BRB] R2: obraz zweryfikowany bajt po bajcie (dane=plik, BSS=0, luki i wypelnienie=0 B: ");
                    log.dziesietnie(r->ladowanie.bajty_wypelnienia);
                    log.tekst(") z_pliku=");
                    log.dziesietnie(r->ladowanie.bajty_skopiowane);
                    log.tekst(" bss=");
                    log.dziesietnie(r->ladowanie.bajty_bss);
                    log.znak('\n');
                }
            }
        }
    }

    // ---- Zasoby przejscia (kazdy przez AllocatePages, w rejestrze) ----
    if (blad == BladR2::Brak) {
        struct { UINTN strony; EFI_MEMORY_TYPE typ; RodzajZasobu rodzaj; uint64_t* adres; } z[4] = {
            {R2_RAMKI_PULI, EfiLoaderData, RodzajZasobu::TabliceStron, &r->pula_pa},
            {R2_STRONY_STOSU, EfiLoaderData, RodzajZasobu::StosPrzejscia, &r->stos_pa},
            {R2_STRONY_KODU, EfiLoaderCode, RodzajZasobu::KodPrzejscia, &r->kod_pa},
            {R2_STRONY_INFORMACJI, EfiLoaderData, RodzajZasobu::InformacjeRozruchu, &r->informacje_pa},
        };
        for (auto& x : z) {
            bool kol = false;
            EFI_STATUS s = EFI_SUCCESS;
#if defined(BRB_TEST_R2) && BRB_TEST_R2 == 3
            if (x.rodzaj == RodzajZasobu::TabliceStron) s = EFI_OUT_OF_RESOURCES;   // TYLKO test: odmowa puli tablic
            else
#endif
            s = przydziel(bs, &r->rejestr, x.typ, x.strony, x.rodzaj, x.adres, log, &kol);
            if (EFI_ERROR(s)) {
                log.tekst("[BRB] R2: przydzial ");
                log.tekst(nazwa_zasobu(x.rodzaj));
                log.tekst(" nieudany status=");
                log.hex(s);
                log.znak('\n');
                porazka(kol ? BladR2::Rejestr : BladR2::PrzydzialZasobu, kol ? EFI_LOAD_ERROR : s);
                break;
            }
            uint8_t* p = pamiec.wskaznik(pamiec.kontekst, *x.adres, uint64_t(x.strony) * STRONA);
            if (!p) { porazka(BladR2::PrzydzialZasobu, EFI_LOAD_ERROR); break; }
            brb_memset(p, 0, uint64_t(x.strony) * STRONA);
        }
        if (blad == BladR2::Brak) r->etap = EtapR2::ZasobyPrzejscia;
    }

    // ---- Tablice przejscia (PlanPrzejscia z R1B) i niezalezna weryfikacja ----
    if (blad == BladR2::Brak) {
        ObrazJadra::Segment seg[ObrazJadra::MAKS_SEGMENTOW];
        brb_memset(seg, 0, sizeof(seg));
        const uint32_t n = plan_p.liczba_segmentow;
        bool ok = n > 0 && n <= ObrazJadra::MAKS_SEGMENTOW;
        for (uint32_t i = 0; ok && i < n; ++i) {
            const SegmentPlanu& s = plan_p.segmenty[i];
            const uint64_t poczatek = s.adres_wirtualny & ~(STRONA - 1);
            const uint64_t nastepny = i + 1 < n ? (plan_p.segmenty[i + 1].adres_wirtualny & ~(STRONA - 1))
                                                : wym.baza_wirtualna + wym.rozmiar;
            if (nastepny <= poczatek) { ok = false; break; }
            seg[i] = ObrazJadra::Segment{poczatek, nastepny - poczatek, (s.adres_wirtualny - poczatek) + s.rozmiar_w_pamieci,
                                         prawa_elf_na_obraz(s.prawa), 0};
        }
        ObrazJadra::Uklad uklad{};
        if (!ok || ObrazJadra::zbuduj_uklad(wym.baza_wirtualna, wym.rozmiar, baza, seg, n, &uklad) != ObrazJadra::BladUkladu::OK) {
            porazka(BladR2::UkladObrazu, EFI_LOAD_ERROR);
        } else {
            const PlanPrzejscia::ZakresTozsamosci t[3] = {
                {r->kod_pa, R2_STRONY_KODU * STRONA, PlanPrzejscia::KLASA_KOD_PRZEJSCIA, 0},
                {r->stos_pa, R2_STRONY_STOSU * STRONA, PlanPrzejscia::KLASA_STOS, 0},
                {r->informacje_pa, R2_STRONY_INFORMACJI * STRONA, PlanPrzejscia::KLASA_TYLKO_ODCZYT, 0},
            };
            uint8_t* pula = pamiec.wskaznik(pamiec.kontekst, r->pula_pa, R2_RAMKI_PULI * STRONA);
            const PlanPrzejscia::Parametry par{&uklad, t, 3, cpu.nx, cpu.bity_fizyczne, r->pula_pa,
                                               reinterpret_cast<uint64_t*>(pula), uint32_t(R2_RAMKI_PULI)};
            PlanPrzejscia::Wynik w{};
            const PlanPrzejscia::Blad bp = pula ? PlanPrzejscia::zbuduj(par, &w) : PlanPrzejscia::Blad::BRAK_DANYCH;
            if (bp != PlanPrzejscia::Blad::OK) {
                r->blad_planu = uint32_t(bp);
                porazka(BladR2::PlanTablic, EFI_LOAD_ERROR);
            } else {
                r->etap = EtapR2::TabliceZbudowane;
                r->cr3 = w.cr3;
                r->ramki_tablic_uzyte = w.uzyte_ramki;
                log.tekst("[BRB] R2: tablice przejscia zbudowane CR3=");
                log.hex(w.cr3);
                log.tekst(" ramki=");
                log.dziesietnie(w.uzyte_ramki);
                log.tekst("/");
                log.dziesietnie(R2_RAMKI_PULI);
                log.linia(" (nieaktywne - CR3 nie jest ladowany w R2)");
                const ZasobTozsamosciowy zasoby[3] = {
                    {r->kod_pa, R2_STRONY_KODU * STRONA, ZASOB_KOD_PRZEJSCIA},
                    {r->stos_pa, R2_STRONY_STOSU * STRONA, ZASOB_STOS},
                    {r->informacje_pa, R2_STRONY_INFORMACJI * STRONA, ZASOB_TYLKO_ODCZYT},
                };
                const OczekiwaniaTablic o{&plan_p, wym.baza_wirtualna, baza, wym.rozmiar, meta.wejscie64, zasoby, 3,
                                          r->pula_pa, R2_RAMKI_PULI, w.cr3, r->informacje_pa,
                                          sizeof(KontraktPamieci::KontraktV1), cpu.bity_fizyczne, cpu.nx};
                const DostepTablic dt{[](void* k, uint64_t pa) -> const uint64_t* {
                    auto* p = static_cast<const DostepFizyczny*>(k);
                    return reinterpret_cast<const uint64_t*>(p->wskaznik(p->kontekst, pa, STRONA));
                }, const_cast<DostepFizyczny*>(&pamiec)};
                r->blad_tablic = weryfikuj_tablice_przejscia(o, dt, &r->tablice);
                if (r->blad_tablic != BladTablic::Brak) {
                    porazka(BladR2::WeryfikacjaTablic, EFI_ABORTED);
                } else {
                    r->etap = EtapR2::TabliceZweryfikowane;
                    log.tekst("[BRB] R2: tablice zweryfikowane niezaleznie: liscie=");
                    log.dziesietnie(r->tablice.liscie);
                    log.tekst(" okno=");
                    log.dziesietnie(r->tablice.strony_okna);
                    log.tekst(" zasoby=");
                    log.dziesietnie(r->tablice.strony_zasobow);
                    log.tekst(" pula=");
                    log.dziesietnie(r->tablice.strony_puli);
                    log.tekst(" tablice=");
                    log.dziesietnie(r->tablice.tablice);
                    log.tekst(" wejscie64 ");
                    log.hex(meta.wejscie64);
                    log.tekst(" -> ");
                    log.hex(r->tablice.wejscie_pa);
                    log.linia(" W+X=0 U=0 aliasy=0 (uprawnienia zamierzone, nieaktywne sprzetowo)");
                }
            }
        }
    }

    // ---- Migawka po przydzialach: dowody i kontrola NVS ----
    if (blad == BladR2::Brak) {
        bool kolizja = false;
        const WynikMapy wm = migawka(bs, &r->rejestr, "po przydzialach", log, r, &kolizja);
        if (wm.blad != BladPobraniaMapy::Brak) porazka(BladR2::MapaPamieci, wm.status);
        else if (kolizja) porazka(BladR2::NakladanieNaNvs, EFI_ABORTED);
        else log.linia("[BRB] R2: zaden wlasny przydzial nie nachodzi na ACPI NVS");
    }

    // ---- Zwolnienie wszystkiego (R2 nie przekazuje sterowania) ----
    const uint32_t bledy = rejestr_zwolnij_wszystko(&r->rejestr, zwolnij_strony, &kz);
    r->bledy_sprzatania += bledy;
    r->wlasne_po_sprzataniu = rejestr_wlasne(r->rejestr);
#if defined(BRB_TEST_R2) && BRB_TEST_R2 == 4
    if (straznik) {
        const uint8_t* p = pamiec.wskaznik(pamiec.kontekst, straznik, wym.strony_przydzialu * STRONA);
        bool nienaruszony = p != nullptr;
        for (uint64_t i = 0; p && i < wym.strony_przydzialu * STRONA; ++i) if (p[i] != uint8_t(0xA5 ^ (i * 31))) { nienaruszony = false; break; }
        log.linia(nienaruszony ? "[BRB] R2 TEST: straznik NIENARUSZONY" : "[BRB] R2 TEST: straznik ZMIENIONY");
        if (!nienaruszony && blad == BladR2::Brak) porazka(BladR2::Weryfikacja, EFI_ABORTED);
        for (uint32_t i = 0; i < r->rejestr.liczba; ++i) {
            WpisZasobu& w = r->rejestr.wpisy[i];
            if (w.rodzaj == RodzajZasobu::TestowyStraznik && w.wlasny) {
                w.wlasny = false;
                if (!zwolnij_strony(&kz, w.adres, w.strony)) ++r->bledy_sprzatania;
            }
        }
    }
#endif
    log.tekst("[BRB] R2: zasoby zwolnione=");
    log.dziesietnie(r->rejestr.zwolnione);
    log.tekst(" wlasne_po=");
    log.dziesietnie(r->wlasne_po_sprzataniu);
    log.tekst(" bledy_sprzatania=");
    log.dziesietnie(r->bledy_sprzatania);
    log.znak('\n');
    if (blad == BladR2::Brak && (r->bledy_sprzatania != 0 || r->wlasne_po_sprzataniu != 0))
        porazka(BladR2::SprzatanieNieudane, EFI_DEVICE_ERROR);
    if (blad == BladR2::Brak) r->etap = EtapR2::ZasobyZwolnione;
    return koniec(r, log, blad, status);
}

}  // namespace brb
