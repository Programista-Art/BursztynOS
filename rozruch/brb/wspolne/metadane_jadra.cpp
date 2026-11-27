/*
 * BRB - wykrywanie metadanych natywnego wejscia jadra (Faza R1B). Opis: metadane_jadra.h.
 */
#include "rozruch/brb/wspolne/metadane_jadra.h"

#include "jadro/arch/metadane_jadra.h"
#include "rozruch/brb/wspolne/arytmetyka.h"

namespace brb {

namespace {

constexpr uint64_t KONIEC_DOLNEJ_POLOWY = 0x0000800000000000ull;
constexpr uint64_t POCZATEK_GORNEJ_POLOWY = 0xFFFF800000000000ull;

bool kanoniczny(uint64_t va) { return va < KONIEC_DOLNEJ_POLOWY || va >= POCZATEK_GORNEJ_POLOWY; }

}  // namespace

BladMetadanych znajdz_metadane_wejscia(const uint8_t* dane, uint64_t rozmiar, const PlanLadowania& plan,
                                       MetadaneWejscia* wynik) {
    if (!dane || !wynik || plan.liczba_segmentow == 0 || plan.liczba_segmentow > ELF_MAKS_SEGMENTOW) {
        return BladMetadanych::BrakDanych;
    }
    *wynik = {};
    const uint64_t okno = rozmiar < MetadaneJadra::OKNO_WYSZUKIWANIA ? rozmiar : MetadaneJadra::OKNO_WYSZUKIWANIA;
    uint64_t trafienie = 0;
    uint32_t trafien = 0;
    for (uint64_t off = 0; off + 8 <= okno; off += 8) {
        if (le64(dane + off) != MetadaneJadra::SYGNATURA) continue;
        ++trafien;
        trafienie = off;
    }
    if (trafien == 0) return BladMetadanych::NieZnaleziono;
    if (trafien > 1) return BladMetadanych::Niejednoznaczne;
    if (!zakres_w_buforze(trafienie, MetadaneJadra::ROZMIAR, rozmiar) || trafienie + MetadaneJadra::ROZMIAR > okno) {
        return BladMetadanych::Obciete;
    }
    // Musi lezec w calosci w danych z pliku segmentu tylko do odczytu (bez W i X).
    int32_t seg = -1;
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        const SegmentPlanu& s = plan.segmenty[i];
        if (trafienie >= s.przesuniecie_pliku && trafienie - s.przesuniecie_pliku <= s.bajty_z_pliku &&
            MetadaneJadra::ROZMIAR <= s.bajty_z_pliku - (trafienie - s.przesuniecie_pliku)) {
            seg = static_cast<int32_t>(i);
            break;
        }
    }
    if (seg < 0) return BladMetadanych::PozaSegmentem;
    const SegmentPlanu& sm = plan.segmenty[seg];
    if ((sm.prawa & (ELF_PF_W | ELF_PF_X)) != 0 || (sm.prawa & ELF_PF_R) == 0) return BladMetadanych::PozaSegmentem;

    const uint8_t* m = dane + trafienie;
    if (le64(m + 56) != MetadaneJadra::SYGNATURA_KONCOWA) return BladMetadanych::ZlaSygnaturaKoncowa;
    if (le16(m + 8) != MetadaneJadra::WERSJA) return BladMetadanych::NieobslugiwanaWersja;
    if (le16(m + 10) != MetadaneJadra::ROZMIAR) return BladMetadanych::ZlyRozmiar;
    const uint32_t kontrakt = le32(m + 12);
    if (kontrakt != MetadaneJadra::KONTRAKT_WEJSCIA_R1B) return BladMetadanych::NieobslugiwanyKontrakt;
    if (le32(m + 44) != 0 || le64(m + 48) != 0) return BladMetadanych::PolaZarezerwowane;
    const uint32_t cechy = le32(m + 40);
    if ((cechy & ~MetadaneJadra::MASKA_CECH) != 0) return BladMetadanych::NieznaneCechy;
    const uint64_t wyrownanie = le64(m + 32);
    if (!potega_dwojki(wyrownanie) || wyrownanie < MetadaneJadra::WYROWNANIE_PODKLADU) return BladMetadanych::ZleWyrownanie;
    const uint64_t baza = le64(m + 16);
    uint64_t najnizszy = ~0ull;
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        if (plan.segmenty[i].adres_wirtualny < najnizszy) najnizszy = plan.segmenty[i].adres_wirtualny;
    }
    if (baza != najnizszy) return BladMetadanych::BazaNiezgodna;
    const uint64_t wejscie = le64(m + 24);
    if (!kanoniczny(wejscie)) return BladMetadanych::WejscieNiekanoniczne;
    if (wejscie == plan.wejscie_wirtualne) return BladMetadanych::WejscieRowneElf;
    int32_t sw = -1;
    for (uint32_t i = 0; i < plan.liczba_segmentow; ++i) {
        const SegmentPlanu& s = plan.segmenty[i];
        if (wejscie >= s.adres_wirtualny && wejscie - s.adres_wirtualny < s.rozmiar_w_pamieci) {
            sw = static_cast<int32_t>(i);
            break;
        }
    }
    if (sw < 0) return BladMetadanych::WejsciePozaSegmentem;
    const SegmentPlanu& se = plan.segmenty[sw];
    if ((se.prawa & ELF_PF_X) == 0) return BladMetadanych::WejscieNiewykonywalne;
    if (wejscie - se.adres_wirtualny >= se.bajty_z_pliku) return BladMetadanych::WejsciePozaPlikiem;

    wynik->przesuniecie_pliku = trafienie;
    wynik->segment = static_cast<uint32_t>(seg);
    wynik->kontrakt_wejscia = kontrakt;
    wynik->baza_wirtualna = baza;
    wynik->wejscie64 = wejscie;
    wynik->wejscie64_offset = wejscie - baza;
    wynik->segment_wejscia = static_cast<uint32_t>(sw);
    wynik->wymagane_cechy = cechy;
    wynik->wyrownanie_podkladu = wyrownanie;
    return BladMetadanych::Brak;
}

const char* opis_bledu_metadanych(BladMetadanych blad) {
    switch (blad) {
    case BladMetadanych::Brak: return "OK";
    case BladMetadanych::BrakDanych: return "brak danych lub planu";
    case BladMetadanych::NieZnaleziono: return "brak metadanych wejscia w oknie 32 KiB";
    case BladMetadanych::Niejednoznaczne: return "wiecej niz jedna sygnatura metadanych";
    case BladMetadanych::PozaSegmentem: return "metadane poza danymi segmentu tylko do odczytu";
    case BladMetadanych::Obciete: return "metadane obciete";
    case BladMetadanych::ZlaSygnaturaKoncowa: return "zla sygnatura koncowa";
    case BladMetadanych::NieobslugiwanaWersja: return "nieobslugiwana wersja metadanych";
    case BladMetadanych::ZlyRozmiar: return "zly rozmiar metadanych";
    case BladMetadanych::NieobslugiwanyKontrakt: return "nieobslugiwany kontrakt wejscia";
    case BladMetadanych::PolaZarezerwowane: return "niezerowe pola zarezerwowane";
    case BladMetadanych::NieznaneCechy: return "nieznane wymagane cechy CPU";
    case BladMetadanych::ZleWyrownanie: return "zle wyrownanie podkladu";
    case BladMetadanych::BazaNiezgodna: return "baza wirtualna niezgodna z naglowkami programu";
    case BladMetadanych::WejscieNiekanoniczne: return "niekanoniczny adres wejscia";
    case BladMetadanych::WejsciePozaSegmentem: return "wejscie poza segmentami";
    case BladMetadanych::WejscieNiewykonywalne: return "wejscie w segmencie niewykonywalnym";
    case BladMetadanych::WejsciePozaPlikiem: return "wejscie poza danymi z pliku (BSS)";
    case BladMetadanych::WejscieRowneElf: return "wejscie natywne rowne e_entry";
    }
    return "nieznany blad metadanych";
}

}  // namespace brb
