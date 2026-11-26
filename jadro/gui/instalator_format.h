#pragma once
#include <stdint.h>
constexpr uint64_t INSTALATOR_IPC=0x425350494E535431ULL;
constexpr const char* INSTALATOR_PROGRAM="/programy/instalator.cebula/instalator.bur";
enum { INST_STAN, INST_DYSK, INST_PODGLAD, INST_POTWIERDZ, INST_KROK };
enum { INST_OK, INST_UPRAWNIENIA, INST_DYSK_BLAD, INST_ZRODLO, INST_ZA_MALY,
       INST_BRAK_PLIKOW, INST_POTWIERDZENIE, INST_IO, INST_FORMAT, INST_PLIKI, INST_ZAJETY };
enum { INST_BACKEND_NIEZNANY, INST_BACKEND_AHCI, INST_BACKEND_VIRTIO_BLK };
enum { INST_GOTOWY, INST_GPT, INST_ESP, INST_BSP, INST_SYSTEM, INST_TAPETY,
       INST_WERYFIKACJA, INST_ZNACZNIK, INST_KONIEC, INST_BLAD };
enum EtapPostepuInstalacji : uint32_t {
    POSTEP_PRZYGOTOWANIE,
    POSTEP_GPT,
    POSTEP_ESP,
    POSTEP_BSP2,
    POSTEP_SYSTEM,
    POSTEP_TAPETY,
    POSTEP_WERYFIKACJA,
    POSTEP_COMPLETE
};
inline uint32_t instalator_postep_nowy(uint32_t obecny, uint32_t zadany, bool complete) {
    if (obecny > 100) obecny = 100;
    if (zadany > 100) zadany = 100;
    if (!complete && zadany == 100) zadany = 99;
    return zadany < obecny ? obecny : zadany;
}
inline bool instalator_etap_postepu_poprawny(uint32_t obecny, uint32_t nastepny) {
    return obecny <= POSTEP_COMPLETE && nastepny <= POSTEP_COMPLETE && nastepny >= obecny;
}
inline uint32_t instalator_podpostep(uint32_t poczatek, uint32_t koniec,
                                    uint32_t wykonane, uint32_t razem) {
    if (koniec < poczatek) return poczatek;
    if (!razem || !wykonane) return poczatek;
    if (wykonane >= razem) return koniec;
    const uint32_t zakres = koniec - poczatek;
    return poczatek + static_cast<uint32_t>((static_cast<uint64_t>(zakres) * wykonane) / razem);
}
struct InstalatorZadanie {
    uint32_t wersja,operacja;
    uint64_t odpowiedz,token;
    uint32_t dysk,zgoda_zrodlo;
    char potwierdzenie[16];
};
struct InstalatorOdpowiedz {
    uint32_t wersja,status,etap,liczba_dyskow;
    uint32_t dysk,sektor,odczyt,zapis;
    uint32_t nosnik_instalacyjny,bursztyn,uefi,gop;
    uint32_t xhci,backend;
    uint32_t procent,postep_etap,wykonane,razem;
    uint32_t pliki_wykonane,pliki_razem,tapety_wykonane,tapety_razem;
    uint64_t sektory,esp_mib,bsp_mib,nieprzydzielone_mib,token,zapisy;
    char model[41],serial[21],blad[256];   // blad: do 3 wierszy (\n), zob. InstalacjaPlikow::opisz
};
static_assert(sizeof(InstalatorOdpowiedz)<=512,"IPC response limit");

inline bool instalator_potwierdzenie_poprawne(const char* tekst, uint32_t pojemnosc) {
    static constexpr char wymagane[] = "BURSZTYN";
    if (!tekst || pojemnosc < sizeof(wymagane)) return false;
    for (uint32_t i = 0; i < sizeof(wymagane); ++i) {
        if (tekst[i] != wymagane[i]) return false;
    }
    return true;
}

inline bool instalator_mozna_zainstalowac(bool dysk_poprawny,
                                          bool instalator_uprawniony,
                                          bool tekst_poprawny,
                                          bool potwierdzenie_zrodla_wymagane,
                                          bool potwierdzenie_zrodla_zaznaczone) {
    return dysk_poprawny && instalator_uprawniony && tekst_poprawny &&
           (!potwierdzenie_zrodla_wymagane || potwierdzenie_zrodla_zaznaczone);
}
