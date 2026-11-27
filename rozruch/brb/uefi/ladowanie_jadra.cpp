#include "ladowanie_jadra.h"

#include "../wspolne/arytmetyka.h"
#include "../wspolne/pamiec_podstawowa.h"

namespace brb {

namespace {

bool nakladaja_sie(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) { return a0 < b1 && b0 < a1; }

void log_zakres(LogSzeregowy& log, uint64_t poczatek, uint64_t strony) {
    log.hex(poczatek);
    log.znak('-');
    log.hex(poczatek + strony * ELF_STRONA);
    log.tekst(" strony=");
    log.dziesietnie(strony);
}

struct KontekstDowodow {
    LogSzeregowy* log;
    uint32_t licznik;
};

void wypisz_wpis(void* ctx, const WpisMapy& e) {
    auto* k = static_cast<KontekstDowodow*>(ctx);
    if (++k->licznik > 32) return;   // ograniczenie logu
    k->log->tekst("[BRB] mapa:   typ=");
    k->log->dziesietnie(e.typ);
    k->log->znak(' ');
    k->log->tekst(nazwa_klasy(e.klasa));
    k->log->znak(' ');
    log_zakres(*k->log, e.poczatek, e.strony);
    k->log->tekst(" atrybuty=");
    k->log->hex(e.atrybuty);
    k->log->znak('\n');
}

// Migawka mapy + dowody dla wskazanych zakresow (wszystkich albo jednego). Bufor migawki zawsze zwalniany.
WynikMapy migawka_z_dowodami(EFI_BOOT_SERVICES* bs, const ZakresyPrzydzialu& z, int32_t tylko, LogSzeregowy& log,
                             RaportFazy4A* r) {
    MigawkaMapy m;
    const WynikMapy w = pobierz_migawke_mapy(bs, &m);
    if (w.blad != BladPobraniaMapy::Brak) {
        log.tekst("[BRB] mapa: blad pobrania - ");
        log.tekst(opis_bledu_pobrania_mapy(w.blad));
        if (w.blad == BladPobraniaMapy::Walidacja) {
            log.tekst(": ");
            log.tekst(opis_bledu_mapy(w.blad_walidacji));
        }
        log.znak('\n');
        return w;
    }
    r->wpisy_mapy = m.widok.liczba;
    log.tekst("[BRB] mapa: migawka (nie ostateczna - Faza 4C pobierze mape tuz przed ExitBootServices) wpisy=");
    log.dziesietnie(m.widok.liczba);
    log.tekst(" deskryptor=");
    log.dziesietnie(m.rozmiar_deskryptora);
    log.tekst(" B wersja=");
    log.dziesietnie(m.wersja);
    log.tekst(" proby=");
    log.dziesietnie(m.proby);
    log.tekst(" klucz=");
    log.hex(m.klucz);
    log.znak('\n');
    log.tekst("[BRB] mapa: strony wolna=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::Wolna)]);
    log.tekst(" uslugi-rozruchowe=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::UslugiRozruchowe)]);
    log.tekst(" ladowarka=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::Ladowarka)]);
    log.tekst(" runtime=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::UslugiCzasuWykonania)]);
    log.tekst(" acpi=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::AcpiOdzyskiwalna)] +
                    m.statystyki.strony[uint8_t(KlasaPamieci::AcpiNvs)]);
    log.tekst(" zarezerwowana=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::Zarezerwowana)]);
    log.tekst(" mmio=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::Mmio)]);
    log.tekst(" nieznana=");
    log.dziesietnie(m.statystyki.strony[uint8_t(KlasaPamieci::Nieznana)]);
    log.tekst(" najwyzszy_adres=");
    log.hex(m.statystyki.najwyzszy_adres);
    log.tekst(" wpisy>4GiB=");
    log.dziesietnie(m.statystyki.liczba_powyzej_4gib);
    log.znak('\n');
    for (uint32_t i = 0; i < z.liczba; ++i) {
        if (tylko >= 0 && int32_t(i) != tylko) continue;
        log.tekst("[BRB] mapa: zakres ");
        log.dziesietnie(i);
        log.znak(' ');
        log_zakres(log, z.zakresy[i].poczatek, z.zakresy[i].strony);
        log.linia(" pokrywajace deskryptory:");
        KontekstDowodow k{&log, 0};
        const uint64_t luki = wpisy_w_zakresie(m.widok, z.zakresy[i].poczatek,
                                               z.zakresy[i].poczatek + z.zakresy[i].strony * ELF_STRONA, wypisz_wpis, &k);
        if (luki) {
            log.tekst("[BRB] mapa:   bajty nieopisane w mapie=");
            log.dziesietnie(luki);
            log.znak('\n');
        }
    }
    const EFI_STATUS sz = zwolnij_migawke_mapy(bs, &m);
    if (EFI_ERROR(sz)) {
        ++r->bledy_sprzatania;
        log.tekst("[BRB] sprzatanie: FreePages bufora mapy nieudane status=");
        log.hex(sz);
        log.znak('\n');
    }
    return w;
}

// Zwolnienie posiadanych zakresow w odwrotnej kolejnosci. Kazdy zakres zwalniany najwyzej raz; blad FreePages
// jest raportowany, a zakres porzucany (ponowne zwolnienie mogloby trafic w pamiec przydzielona komus innemu).
uint32_t zwolnij_przydzial(EFI_BOOT_SERVICES* bs, PrzydzialJadra* p, LogSzeregowy& log, RaportFazy4A* r) {
    uint32_t zwolnione = 0;
    for (uint32_t n = p->liczba; n > 0; --n) {
        auto& z = p->zakresy[n - 1];
        if (!z.wlasny) continue;
        const EFI_STATUS s = bs->FreePages(z.adres, z.strony);
        z.wlasny = false;
        if (EFI_ERROR(s)) {
            ++r->bledy_sprzatania;
            log.tekst("[BRB] sprzatanie: FreePages zakresu ");
            log.dziesietnie(n - 1);
            log.tekst(" nieudane status=");
            log.hex(s);
            log.linia(" - zakres porzucony, bez ponownego zwalniania");
            continue;
        }
        ++zwolnione;
    }
    return zwolnione;
}

EFI_STATUS koniec(RaportFazy4A* r, LogSzeregowy& log, BladPrzydzialu b, EFI_STATUS s) {
    r->blad = b;
    r->status = s;
    if (b != BladPrzydzialu::Brak) {
        log.tekst("[BRB] blad fazy 4A: BRB-E5");
        log.znak(char('0' + (uint32_t(b) / 10) % 10));
        log.znak(char('0' + uint32_t(b) % 10));
        log.znak(' ');
        log.tekst(opis_bledu_przydzialu(b));
        if (r->blad_ladowania != BladLadowania::Brak) {
            log.tekst(" (BRB-E3");
            log.znak(char('0' + (uint32_t(r->blad_ladowania) / 10) % 10));
            log.znak(char('0' + uint32_t(r->blad_ladowania) % 10));
            log.znak(' ');
            log.tekst(opis_bledu_ladowania(r->blad_ladowania));
            log.znak(')');
        }
        if (r->zakres_bledu >= 0) {
            log.tekst(" zakres=");
            log.dziesietnie(uint64_t(r->zakres_bledu));
        }
        log.tekst(" status=");
        log.hex(s);
        log.znak('\n');
    }
    return s;
}

}  // namespace

const char* opis_bledu_przydzialu(BladPrzydzialu b) {
    switch (b) {
        case BladPrzydzialu::Brak: return "brak bledu";
        case BladPrzydzialu::BrakFunkcji: return "brak uslug przydzialu stron firmware";
        case BladPrzydzialu::PlanPrzydzialu: return "plan przydzialu stron odrzucony";
        case BladPrzydzialu::KonfliktZObrazemBrb: return "pamiec jadra nachodzi na obraz BRB";
        case BladPrzydzialu::KonfliktZBuforemZrodla: return "pamiec jadra nachodzi na bufor pliku jadra";
        case BladPrzydzialu::MapaPamieci: return "mapa pamieci niedostepna";
        case BladPrzydzialu::ZakresZajety: return "pamiec wymagana przez jadro jest zajeta";
        case BladPrzydzialu::ZlyAdresZwrocony: return "firmware przydzielilo inny adres niz zadany";
        case BladPrzydzialu::Ladowanie: return "ladowanie segmentow odrzucone przez kontrole";
        case BladPrzydzialu::Weryfikacja: return "weryfikacja zaladowanego obrazu nieudana";
        case BladPrzydzialu::SprzatanieNieudane: return "zwolnienie pamieci nieudane";
    }
    return "nieznany blad fazy 4A";
}

EFI_STATUS faza4a_zaladuj_i_zweryfikuj(EFI_BOOT_SERVICES* bs, const EFI_LOADED_IMAGE_PROTOCOL* obraz,
                                       const BuforJadra& bufor, const PlanLadowania& plan, const DostepFizyczny& pamiec,
                                       LogSzeregowy& log, RaportFazy4A* r) {
    if (!r) return EFI_INVALID_PARAMETER;
    brb_memset(r, 0, sizeof(*r));
    r->zakres_bledu = -1;
    r->ladowanie.segment_bledu = -1;
    r->etap = EtapFazy4A::WejscieZweryfikowane;
    if (!bs || !bs->AllocatePages || !bs->FreePages || !obraz || bufor.adres == 0 || !pamiec.wskaznik)
        return koniec(r, log, BladPrzydzialu::BrakFunkcji, EFI_UNSUPPORTED);
    log.linia("[BRB] etap 4A: przydzial pamieci fizycznej i ladowanie segmentow (bez uruchamiania jadra)");

    // ---- Plan przydzialu (z planu Fazy 3) ----
    r->blad_ladowania = zbuduj_zakresy_przydzialu(plan, &r->zakresy);
    if (r->blad_ladowania != BladLadowania::Brak) return koniec(r, log, BladPrzydzialu::PlanPrzydzialu, EFI_LOAD_ERROR);
    r->etap = EtapFazy4A::PlanPrzydzialuGotowy;
    const ZakresyPrzydzialu& zk = r->zakresy;
    for (uint32_t i = 0; i < zk.liczba; ++i) {
        log.tekst("[BRB] 4A: zakres ");
        log.dziesietnie(i);
        log.tekst(" zadany fiz=");
        log_zakres(log, zk.zakresy[i].poczatek, zk.zakresy[i].strony);
        log.linia(" typ=EfiLoaderData AllocateAddress");
    }

    // ---- Konflikty z zywymi zasobami BRB (przed jakakolwiek alokacja) ----
    const uint64_t obraz0 = reinterpret_cast<uintptr_t>(obraz->ImageBase);
    uint64_t obraz1 = 0, bufor1 = 0;
    const bool obraz_ok = dodaj_u64(obraz0, obraz->ImageSize, &obraz1);
    if (!dodaj_u64(bufor.adres, uint64_t(bufor.strony) * ELF_STRONA, &bufor1) || !obraz_ok)
        return koniec(r, log, BladPrzydzialu::PlanPrzydzialu, EFI_LOAD_ERROR);
    for (uint32_t i = 0; i < zk.liczba; ++i) {
        const uint64_t a = zk.zakresy[i].poczatek, b = a + zk.zakresy[i].strony * ELF_STRONA;
        if (nakladaja_sie(a, b, obraz0, obraz1)) {
            r->zakres_bledu = int32_t(i);
            return koniec(r, log, BladPrzydzialu::KonfliktZObrazemBrb, EFI_LOAD_ERROR);
        }
        if (nakladaja_sie(a, b, bufor.adres, bufor1)) {
            r->zakres_bledu = int32_t(i);
            return koniec(r, log, BladPrzydzialu::KonfliktZBuforemZrodla, EFI_LOAD_ERROR);
        }
    }

    // ---- Migawka mapy (dowody; bufor zwalniany przed przydzialem, aby nie zajmowal celow) ----
    const WynikMapy wm = migawka_z_dowodami(bs, zk, -1, log, r);
    if (wm.blad != BladPobraniaMapy::Brak) {
        r->blad_mapy = wm.blad;
        r->blad_walidacji_mapy = wm.blad_walidacji;
        return koniec(r, log, BladPrzydzialu::MapaPamieci, wm.status);
    }

    // ---- Transakcyjny przydzial: wszystkie zakresy albo zaden ----
    PrzydzialJadra p{};
    for (uint32_t i = 0; i < zk.liczba; ++i) {
        EFI_PHYSICAL_ADDRESS adres = zk.zakresy[i].poczatek;
        const UINTN strony = UINTN(zk.zakresy[i].strony);
        const EFI_STATUS s = bs->AllocatePages(AllocateAddress, EfiLoaderData, strony, &adres);
        if (s == EFI_SUCCESS) {
            // Od tej chwili BRB jest wlascicielem tego, co firmware zwrocilo (nawet przy zlym adresie).
            p.zakresy[p.liczba].adres = adres;
            p.zakresy[p.liczba].strony = strony;
            p.zakresy[p.liczba].wlasny = true;
            ++p.liczba;
            if (p.liczba > r->przydzielone_max) r->przydzielone_max = p.liczba;
        }
        if (s != EFI_SUCCESS || adres != zk.zakresy[i].poczatek) {
            r->zakres_bledu = int32_t(i);
            const BladPrzydzialu b = s != EFI_SUCCESS ? BladPrzydzialu::ZakresZajety : BladPrzydzialu::ZlyAdresZwrocony;
            log.tekst("[BRB] 4A: AllocateAddress zakresu ");
            log.dziesietnie(i);
            log.tekst(s != EFI_SUCCESS ? " odrzucony status=" : " zwrocil inny adres=");
            log.hex(s != EFI_SUCCESS ? s : adres);
            log.linia(" - wycofanie przydzialow");
            r->wycofane = zwolnij_przydzial(bs, &p, log, r);
            log.tekst("[BRB] 4A: wycofano zakresow=");
            log.dziesietnie(r->wycofane);
            log.tekst(" bledy_sprzatania=");
            log.dziesietnie(r->bledy_sprzatania);
            log.znak('\n');
            // Dowody: stan mapy PO wycofaniu, dla zakresu, ktory nie zostal przydzielony.
            (void)migawka_z_dowodami(bs, zk, int32_t(i), log, r);
            return koniec(r, log, b, s != EFI_SUCCESS ? s : EFI_LOAD_ERROR);
        }
        log.tekst("[BRB] 4A: zakres ");
        log.dziesietnie(i);
        log.tekst(" przydzielony fiz=");
        log_zakres(log, adres, strony);
        log.znak('\n');
    }
    r->etap = EtapFazy4A::ZakresyPrzydzielone;

    // ---- Kopiowanie i weryfikacja (wylacznie w posiadanych zakresach) ----
    const ZrodloJadra zrodlo{bufor.dane(), bufor.rozmiar, bufor.adres, uint64_t(bufor.strony) * ELF_STRONA};
    BladPrzydzialu blad = BladPrzydzialu::Brak;
    EFI_STATUS status = EFI_SUCCESS;
    r->blad_ladowania = zaladuj_segmenty(plan, POLITYKA_JADRA_BURSZTYN, zrodlo, zk, pamiec, &r->ladowanie);
    if (r->blad_ladowania != BladLadowania::Brak) {
        blad = BladPrzydzialu::Ladowanie;
        status = EFI_LOAD_ERROR;
    } else {
        r->etap = EtapFazy4A::SegmentySkopiowane;
        log.tekst("[BRB] 4A: segmenty skopiowane: bajty_z_pliku=");
        log.dziesietnie(r->ladowanie.bajty_skopiowane);
        log.tekst(" bss_wyzerowane=");
        log.dziesietnie(r->ladowanie.bajty_bss);
        log.znak('\n');
        r->blad_ladowania = weryfikuj_zaladowany_obraz(plan, zrodlo, zk, pamiec, &r->ladowanie);
        for (uint32_t i = 0; i < r->ladowanie.liczba; ++i) {
            const RaportSegmentu& s = r->ladowanie.segmenty[i];
            log.tekst("[BRB] 4A: weryfikacja seg");
            log.dziesietnie(i);
            log.tekst(" fiz=");
            log.hex(s.adres);
            log.tekst(" z_pliku=");
            log.dziesietnie(s.z_pliku);
            log.tekst(" pamiec=");
            log.dziesietnie(s.w_pamieci);
            log.tekst(" bss=");
            log.dziesietnie(s.bss);
            log.tekst(s.dane_ok ? " dane=OK" : " dane=BLAD");
            log.linia(s.bss_ok ? " bss=OK" : " bss=BLAD");
        }
        if (r->blad_ladowania != BladLadowania::Brak) {
            blad = BladPrzydzialu::Weryfikacja;
            status = EFI_ABORTED;
            log.tekst("[BRB] 4A: weryfikacja nieudana adres=");
            log.hex(r->ladowanie.adres_bledu);
            log.znak('\n');
        } else {
            r->etap = EtapFazy4A::ObrazZweryfikowany;
            log.tekst("[BRB] 4A: obraz zweryfikowany bajt po bajcie (dane=plik, BSS=0, wypelnienie=0 B: ");
            log.dziesietnie(r->ladowanie.bajty_wypelnienia);
            log.linia("); prawa ELF tylko zadeklarowane - tablice stron firmware bez zmian");
        }
    }

    // ---- Zwolnienie stron jadra (zawsze; Faza 4A nie przekazuje sterowania) ----
    const uint32_t zwolnione = zwolnij_przydzial(bs, &p, log, r);
    log.tekst("[BRB] 4A: strony jadra zwolnione zakresy=");
    log.dziesietnie(zwolnione);
    log.tekst("/");
    log.dziesietnie(p.liczba);
    log.tekst(" bledy_sprzatania=");
    log.dziesietnie(r->bledy_sprzatania);
    log.znak('\n');
    if (blad == BladPrzydzialu::Brak && r->bledy_sprzatania != 0) {
        blad = BladPrzydzialu::SprzatanieNieudane;
        status = EFI_DEVICE_ERROR;
    }
    if (blad == BladPrzydzialu::Brak) r->etap = EtapFazy4A::ZasobyZwolnione;
    return koniec(r, log, blad, status);
}

}  // namespace brb
