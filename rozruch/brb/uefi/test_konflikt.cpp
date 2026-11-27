/*
 * BRB - WYLACZNIE wariant testowy Fazy 4A (cel make brb-uefi-test-konflikt, -DBRB_TEST_KONFLIKT_STRONA=...).
 * Nie jest kompilowany do obrazu brb-x64.efi.
 *
 * Symuluje cudzy, prawowity przydzial strony z zakresu jadra: przed Faza 4A strona jest przydzielana przez
 * AllocatePages(AllocateAddress) i wypelniana wzorcem straznika. Dla modulu ladowania jest to pamiec obca
 * (nie ma jej w tabeli wlasnosci). Po Fazie 4A sprawdzane jest, czy wzorzec jest nienaruszony
 * (BRB nie nadpisal zajetej strony), po czym strona jest zwalniana przez kod testowy, ktory ja przydzielil.
 */
#ifdef BRB_TEST_KONFLIKT_STRONA

#include "program.h"

namespace brb {

namespace {
uint8_t bajt_straznika(uint64_t i) { return uint8_t(0xA7 ^ (i * 31u) ^ (i >> 8)); }
}

void zajmij_strone_testowa(EFI_BOOT_SERVICES* bs, UINT64 adres, const DostepFizyczny& pamiec, BlokadaTestowa* b,
                           LogSzeregowy& log) {
    b->adres = adres;
    b->zajeta = false;
    EFI_PHYSICAL_ADDRESS a = adres;
    const EFI_STATUS s = bs->AllocatePages(AllocateAddress, EfiLoaderData, 1, &a);
    if (EFI_ERROR(s) || a != adres) {
        log.tekst("[BRB-TEST] nie mozna zajac strony ");
        log.hex(adres);
        log.tekst(" status=");
        log.hex(s);
        log.linia(" - scenariusz konfliktu NIEMIARODAJNY");
        if (!EFI_ERROR(s)) bs->FreePages(a, 1);
        return;
    }
    uint8_t* p = pamiec.wskaznik(pamiec.kontekst, adres, 4096);
    if (!p) {
        bs->FreePages(a, 1);
        log.linia("[BRB-TEST] brak dostepu do strony testowej");
        return;
    }
    for (uint64_t i = 0; i < 4096; ++i) p[i] = bajt_straznika(i);
    b->zajeta = true;
    log.tekst("[BRB-TEST] strona ");
    log.hex(adres);
    log.linia(" zajeta przez prawowity przydzial testowy, straznik zapisany (4096 B)");
}

void zwolnij_strone_testowa(EFI_BOOT_SERVICES* bs, const DostepFizyczny& pamiec, BlokadaTestowa* b, LogSzeregowy& log) {
    if (!b->zajeta) return;
    const uint8_t* p = pamiec.wskaznik(pamiec.kontekst, b->adres, 4096);
    uint64_t zle = 0;
    for (uint64_t i = 0; p && i < 4096; ++i) zle += p[i] != bajt_straznika(i);
    log.tekst("[BRB-TEST] straznik strony ");
    log.hex(b->adres);
    log.linia(zle == 0 && p ? " NIENARUSZONY" : " USZKODZONY");
    const EFI_STATUS s = bs->FreePages(b->adres, 1);
    log.tekst("[BRB-TEST] strona testowa zwolniona status=");
    log.hex(s);
    log.znak('\n');
    b->zajeta = false;
}

}  // namespace brb

#endif
