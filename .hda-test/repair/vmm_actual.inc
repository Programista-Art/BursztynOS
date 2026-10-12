static bool adres_kanoniczny(
    uint64_t adres
) {
    const uint64_t gorne =
        adres >> 48;

    const bool bit47 =
        ((adres >> 47) & 1ULL) != 0;

    if (!bit47) {
        return gorne == 0;
    }

    return gorne == 0xFFFFULL;
}
static bool adres_user_kanoniczny(
    uint64_t adres
) {
    return
        adres_kanoniczny(adres) &&
        ((adres >> 47) & 1ULL) == 0;
}
extern "C" bool VmmMapujNowaStroneProcesu(void* va, void* pa, bool zapis) {
    BlokadaVMM blokada;
    const uint64_t adres = reinterpret_cast<uint64_t>(va);
    if (!vmm_zainicjalizowany || !pa || adres < RING3_BASE ||
        !adres_user_kanoniczny(adres) || (adres & 4095U) ||
        (reinterpret_cast<uint64_t>(pa) & 4095U)) return false;
    uint64_t* pml4 = pml4_z_cr3();
    if (!pml4) return false;
    const auto obecne = sprawdz_mapowanie(pml4, adres);
    if (obecne.obecna && obecne.user) return false;
    uint32_t flagi = VMM_FLAGA_OBECNA | VMM_FLAGA_USER;
    if (zapis) flagi |= VMM_FLAGA_ZAPIS;
    return mapuj_strone_wewnetrznie(pml4, adres,
        reinterpret_cast<uint64_t>(pa), flagi);
}