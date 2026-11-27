#include "plik_jadra.h"

#include "../wspolne/arytmetyka.h"
#include "../wspolne/elf64.h"
#include "../wspolne/pamiec_podstawowa.h"

namespace brb {

namespace {

constexpr EFI_GUID GUID_SFS = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
constexpr EFI_GUID GUID_INFO_PLIKU = EFI_FILE_INFO_ID;

WynikPliku wynik(EFI_STATUS s, BladPliku b, UINT32 odczyty = 0) { return WynikPliku{s, b, odczyty}; }

// Protokol pliku (korzen woluminu albo otwarty plik) musi miec rewizje >= 1 i wszystkie uzywane funkcje.
bool protokol_pliku_poprawny(const EFI_FILE_PROTOCOL* p) {
    return p && p->Revision >= EFI_FILE_PROTOCOL_REVISION && p->Open && p->Close && p->Read && p->GetInfo;
}

// Zamyka uchwyt na kazdej sciezce. Close (13.5.3) zawsze zwraca EFI_SUCCESS wg specyfikacji; inny status
// jest logowany jako anomalia firmware, ale nie zmienia wyniku.
void zamknij(EFI_FILE_PROTOCOL*& p, LogSzeregowy& log) {
    if (!p) return;
    const EFI_STATUS s = p->Close(p);
    if (s != EFI_SUCCESS) {
        log.tekst("[BRB] plik: Close zwrocil status=");
        log.hex(s);
        log.znak('\n');
    }
    p = nullptr;
}

struct InfoPliku {
    UINT64 rozmiar;
    UINT64 atrybuty;
};

WynikPliku czytaj_info(EFI_FILE_PROTOCOL* plik, InfoPliku* info) {
    alignas(8) UINT8 bufor[MAKS_ROZMIAR_INFO_PLIKU];
    UINTN rozmiar = sizeof(bufor);
    const EFI_STATUS s = plik->GetInfo(plik, &GUID_INFO_PLIKU, &rozmiar, bufor);
    if (s == EFI_BUFFER_TOO_SMALL) return wynik(EFI_LOAD_ERROR, BladPliku::Metadane);
    if (EFI_ERROR(s)) return wynik(s, BladPliku::Metadane);
    // Rozmiar zwrocony przez firmware jest niezaufany: musi obejmowac stala czesc i co najmniej NUL nazwy.
    if (rozmiar < EFI_FILE_INFO_STALA_CZESC + sizeof(CHAR16) || rozmiar > sizeof(bufor))
        return wynik(EFI_LOAD_ERROR, BladPliku::NiespojneMetadane);
    UINT64 pole_size, file_size, atrybuty;
    brb_memcpy(&pole_size, bufor + offsetof(EFI_FILE_INFO, Size), 8);
    brb_memcpy(&file_size, bufor + offsetof(EFI_FILE_INFO, FileSize), 8);
    brb_memcpy(&atrybuty, bufor + offsetof(EFI_FILE_INFO, Attribute), 8);
    if (pole_size < EFI_FILE_INFO_STALA_CZESC + sizeof(CHAR16) || pole_size > rozmiar)
        return wynik(EFI_LOAD_ERROR, BladPliku::NiespojneMetadane);
    bool jest_nul = false;
    for (UINT64 o = EFI_FILE_INFO_STALA_CZESC; o + 1 < pole_size; o += 2) {
        if (bufor[o] == 0 && bufor[o + 1] == 0) { jest_nul = true; break; }
    }
    if (!jest_nul) return wynik(EFI_LOAD_ERROR, BladPliku::NiespojneMetadane);
    info->rozmiar = file_size;
    info->atrybuty = atrybuty;
    return wynik(EFI_SUCCESS, BladPliku::Brak);
}

}  // namespace

const char* opis_bledu_pliku(BladPliku b) {
    switch (b) {
        case BladPliku::Brak: return "brak bledu";
        case BladPliku::BrakSystemuPlikow: return "wolumin BRB nie udostepnia systemu plikow";
        case BladPliku::ZlyProtokol: return "niepoprawny protokol systemu plikow firmware";
        case BladPliku::OtwarcieWoluminu: return "nie mozna otworzyc woluminu";
        case BladPliku::BrakPliku: return "brak pliku jadra";
        case BladPliku::OtwarciePliku: return "nie mozna otworzyc pliku jadra";
        case BladPliku::Metadane: return "nie mozna odczytac metadanych pliku";
        case BladPliku::NiespojneMetadane: return "niespojne metadane pliku";
        case BladPliku::Katalog: return "sciezka jadra wskazuje katalog";
        case BladPliku::PustyPlik: return "plik jadra jest pusty";
        case BladPliku::ZaDuzyPlik: return "plik jadra przekracza limit 64 MiB";
        case BladPliku::Alokacja: return "brak pamieci na bufor pliku";
        case BladPliku::Odczyt: return "blad odczytu pliku";
        case BladPliku::PrzedwczesnyKoniec: return "nieoczekiwany koniec pliku";
        case BladPliku::ZaDuzoOdczytow: return "firmware zwraca zbyt male porcje danych";
        case BladPliku::NiespojnyOdczyt: return "firmware zglosil odczyt wiekszy niz bufor";
        case BladPliku::PlikDluzszyNizZgloszono: return "plik dluzszy niz rozmiar w metadanych";
    }
    return "nieznany blad pliku";
}

void zwolnij_bufor_jadra(EFI_BOOT_SERVICES* bs, BuforJadra* b) {
    if (!b || b->adres == 0) return;
    bs->FreePages(b->adres, b->strony);
    b->adres = 0;
    b->strony = 0;
    b->rozmiar = 0;
}

WynikPliku wczytaj_plik_jadra(EFI_BOOT_SERVICES* bs, EFI_HANDLE urzadzenie, const CHAR16* sciezka, UINT64 maks,
                              BuforJadra* wy, LogSzeregowy& log) {
    if (!bs || !wy || !sciezka || !bs->AllocatePages || !bs->FreePages)
        return wynik(EFI_INVALID_PARAMETER, BladPliku::ZlyProtokol);
    *wy = BuforJadra{0, 0, 0};
    if (!urzadzenie) return wynik(EFI_UNSUPPORTED, BladPliku::BrakSystemuPlikow);

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* sfs = nullptr;
    EFI_STATUS s = bs->HandleProtocol(urzadzenie, &GUID_SFS, reinterpret_cast<VOID**>(&sfs));
    if (EFI_ERROR(s) || !sfs) return wynik(EFI_ERROR(s) ? s : EFI_UNSUPPORTED, BladPliku::BrakSystemuPlikow);
    if (sfs->Revision < EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION || !sfs->OpenVolume)
        return wynik(EFI_UNSUPPORTED, BladPliku::ZlyProtokol);

    EFI_FILE_PROTOCOL* korzen = nullptr;
    s = sfs->OpenVolume(sfs, &korzen);
    if (EFI_ERROR(s) || !korzen) return wynik(EFI_ERROR(s) ? s : EFI_DEVICE_ERROR, BladPliku::OtwarcieWoluminu);
    // Protokol, ktory nie przeszedl walidacji, nie jest wywolywany wcale (takze Close): jego wskazniki funkcji
    // sa niezaufane - wyciek uchwytu firmware jest bezpieczniejszy niz skok pod niezweryfikowany adres.
    if (!protokol_pliku_poprawny(korzen)) return wynik(EFI_UNSUPPORTED, BladPliku::ZlyProtokol);
    log.linia("[BRB] etap 2: wolumin rozruchowy otwarty (urzadzenie obrazu BRB, tylko odczyt)");

    EFI_FILE_PROTOCOL* plik = nullptr;
    s = korzen->Open(korzen, &plik, sciezka, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !plik) {
        zamknij(korzen, log);
        if (s == EFI_NOT_FOUND) return wynik(s, BladPliku::BrakPliku);
        return wynik(EFI_ERROR(s) ? s : EFI_DEVICE_ERROR, BladPliku::OtwarciePliku);
    }
    if (!protokol_pliku_poprawny(plik)) {
        zamknij(korzen, log);
        return wynik(EFI_UNSUPPORTED, BladPliku::ZlyProtokol);
    }

    // Wszystkie dalsze sciezki przechodza przez "koniec": zamkniecie plik/korzen i ewentualne zwolnienie stron.
    WynikPliku w = wynik(EFI_SUCCESS, BladPliku::Brak);
    InfoPliku info{};
    UINTN strony = 0;
    EFI_PHYSICAL_ADDRESS adres = 0;
    UINT64 odczytane = 0;
    UINT32 odczyty = 0;
    do {
        w = czytaj_info(plik, &info);
        if (w.blad != BladPliku::Brak) break;
        if (info.atrybuty & EFI_FILE_DIRECTORY) { w = wynik(EFI_LOAD_ERROR, BladPliku::Katalog); break; }
        if (info.rozmiar == 0) { w = wynik(EFI_LOAD_ERROR, BladPliku::PustyPlik); break; }
        if (info.rozmiar > maks) { w = wynik(EFI_BAD_BUFFER_SIZE, BladPliku::ZaDuzyPlik); break; }
        log.tekst("[BRB] plik: ");
        log.tekst(SCIEZKA_JADRA_ASCII);
        log.tekst(" rozmiar=");
        log.dziesietnie(info.rozmiar);
        log.linia(" B (metadane poprawne)");

        UINT64 zaokraglony;
        if (!wyrownaj_w_gore(info.rozmiar, ELF_STRONA, &zaokraglony)) { w = wynik(EFI_BAD_BUFFER_SIZE, BladPliku::ZaDuzyPlik); break; }
        strony = UINTN(zaokraglony / ELF_STRONA);
        s = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, strony, &adres);
        if (EFI_ERROR(s) || adres == 0) {
            adres = 0;
            w = wynik(EFI_ERROR(s) ? s : EFI_OUT_OF_RESOURCES, BladPliku::Alokacja);
            break;
        }
        UINT8* cel = reinterpret_cast<UINT8*>(static_cast<uintptr_t>(adres));
        // Limit iteracji: pelne porcje + margines na porcje skrocone przez firmware (np. granice klastrow).
        const UINT64 maks_odczytow = info.rozmiar / 4096 + 64;
        while (odczytane < info.rozmiar) {
            if (odczyty >= maks_odczytow) { w = wynik(EFI_LOAD_ERROR, BladPliku::ZaDuzoOdczytow); break; }
            const UINT64 pozostalo = info.rozmiar - odczytane;
            const UINTN porcja = pozostalo < ROZMIAR_KAWALKA_ODCZYTU ? UINTN(pozostalo) : ROZMIAR_KAWALKA_ODCZYTU;
            UINTN n = porcja;
            ++odczyty;
            s = plik->Read(plik, &n, cel + odczytane);
            if (EFI_ERROR(s)) { w = wynik(s, BladPliku::Odczyt); break; }
            if (n > porcja) { w = wynik(EFI_LOAD_ERROR, BladPliku::NiespojnyOdczyt); break; }
            if (n == 0) { w = wynik(EFI_LOAD_ERROR, BladPliku::PrzedwczesnyKoniec); break; }
            odczytane += n;
        }
        if (w.blad != BladPliku::Brak) break;
        // Sonda konca pliku: kolejny Read musi zwrocic 0 B (plik nie jest dluzszy niz zglosily metadane).
        UINT8 sonda = 0;
        UINTN n = 1;
        ++odczyty;
        s = plik->Read(plik, &n, &sonda);
        if (EFI_ERROR(s)) { w = wynik(s, BladPliku::Odczyt); break; }
        if (n != 0) { w = wynik(EFI_LOAD_ERROR, BladPliku::PlikDluzszyNizZgloszono); break; }
    } while (false);

    zamknij(plik, log);
    zamknij(korzen, log);
    w.odczyty = odczyty;
    if (w.blad != BladPliku::Brak) {
        if (adres) bs->FreePages(adres, strony);
        return w;
    }
    *wy = BuforJadra{adres, strony, info.rozmiar};
    return w;
}

}  // namespace brb
