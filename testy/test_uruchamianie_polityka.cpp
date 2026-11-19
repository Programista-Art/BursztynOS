// Host test of the BWS 10 launch policy (jadro/bws/uruchamianie_polityka.h).
#include <stdio.h>
#include "../jadro/bws/uruchamianie_polityka.h"
#include "../jadro/gui/desktop_entries.h"
#include "../jadro/gui/app_identity.h"
static int fails=0,checks=0;
#define CHECK(c) do{++checks;if(!(c)){++fails;printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);}}while(0)
using namespace uruchamianie;
int main(){
    const uint64_t explorer_rights=PRAWO_GUI|PRAWO_PLIKI_CZYTAJ|PRAWO_PLIKI_ZAPISZ|PRAWO_URUCHOM_PROGRAM;
    // Explorer (level 4, uruchom_program) may delegate; table values never come from disk.
    CHECK(rodzic_moze_delegowac(PZB_UZYTKOWNIK,explorer_rights));
    CHECK(rodzic_moze_delegowac(PZB_USLUGI,PRAWO_URUCHOM_PROGRAM));
    CHECK(!rodzic_moze_delegowac(PZB_UZYTKOWNIK,PRAWO_GUI|PRAWO_PLIKI_CZYTAJ)); // no launch right
    CHECK(!rodzic_moze_delegowac(5,explorer_rights));                           // sandbox
    // Every desktop launcher with an elevated manifest level is in the table; others are not.
    int elevated=0;
    for(const auto& l:DesktopEntries::Launchers){
        const auto* a=znajdz_aplikacje_systemowa(l.path);
        // PRAWO_ZARZADZANIE_PROCESAMI (uprzywilejowane) wolno nadac wylacznie Menedzerowi zadan.
        const uint64_t dozwolone=PRAWO_SYSTEM_CONFIG|(AppIdentity::equal(AppIdentity::TaskManager,l.path)?PRAWO_ZARZADZANIE_PROCESAMI:0);
        if(a){++elevated;CHECK(a->poziom<PZB_UZYTKOWNIK);CHECK(!(a->prawa&PRAWA_UPRZYWILEJOWANE&~dozwolone));}
    }
    CHECK(elevated==5);   // Ustawienia, Diagnostyka, Test dzwieku, Aktualizacje, Menedzer zadan
    for(const auto& a:kAplikacjeSystemowe)
        CHECK(!(a.prawa&PRAWO_ZARZADZANIE_PROCESAMI)||AppIdentity::equal(AppIdentity::TaskManager,a.sciezka));
    CHECK(znajdz_aplikacje_systemowa("/programy/ustawienia.cebula/ustawienia.bur")!=nullptr);
    CHECK(znajdz_aplikacje_systemowa("/programy/kalkulator.cebula/kalkulator.bur")==nullptr);
    // Hostile lookalikes are NOT recognised (exact match only).
    CHECK(!znajdz_aplikacje_systemowa("/programy/ustawienia.cebula/ustawienia.bur/"));
    CHECK(!znajdz_aplikacje_systemowa("/programy/ustawienia.cebula/ustawienia.bu"));
    CHECK(!znajdz_aplikacje_systemowa("/uzytkownicy/x/programy/ustawienia.cebula/ustawienia.bur"));
    CHECK(!znajdz_aplikacje_systemowa("/programy/../programy/ustawienia.cebula/ustawienia.bur"));
    CHECK(!znajdz_aplikacje_systemowa("/Programy/ustawienia.cebula/ustawienia.bur"));
    CHECK(!znajdz_aplikacje_systemowa(""));
    CHECK(!znajdz_aplikacje_systemowa(nullptr));
    // No table entry grants driver/debug rights.
    for(const auto& a:kAplikacjeSystemowe){CHECK(!(a.prawa&(PRAWO_STEROWNIK|PRAWO_DEBUG)));}
    // Loader error classification.
    CHECK(klasyfikuj_blad_loadera("file-not-found")==BLAD_BRAK_PLIKU);
    CHECK(klasyfikuj_blad_loadera("signature-mismatch")==BLAD_INTEGRALNOSC);
    CHECK(klasyfikuj_blad_loadera("signature-file-missing")==BLAD_INTEGRALNOSC);
    CHECK(klasyfikuj_blad_loadera("invalid-magic")==BLAD_FORMAT);
    CHECK(klasyfikuj_blad_loadera("text-not-page-aligned")==BLAD_FORMAT);
    CHECK(klasyfikuj_blad_loadera("bur-v2-security-requirements")==BLAD_FORMAT);
    CHECK(klasyfikuj_blad_loadera("already-running")==BLAD_JUZ_DZIALA);
    CHECK(klasyfikuj_blad_loadera("no-process-slot")==BLAD_ZASOBY);
    CHECK(klasyfikuj_blad_loadera("invalid-or-overlong-app-path")==BLAD_ARGUMENT);
    CHECK(klasyfikuj_blad_loadera("mapuj_stos_uzytkownika-failed")==BLAD_TWORZENIE_PROCESU);
    CHECK(klasyfikuj_blad_loadera(nullptr)==BLAD_NIEZNANY);
    for(unsigned k=0;k<=BLAD_MAX+2;++k){const char* m=komunikat_pl(static_cast<uint8_t>(k));CHECK(m&&m[0]);}
    printf("uruchamianie_polityka: %d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
