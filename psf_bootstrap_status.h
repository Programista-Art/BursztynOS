#pragma once
#include "block_device.h"

// In-memory, bootstrap-only status. Neither BSP2 structures nor syscall ABI.
enum class PsfBladStartu {
    Ok, NieGotowy, ZlaSciezka, ZlyKorzen, BrakRodzica, NieKatalog,
    Odczyt, JuzIstnieje, BrakWezlow, BrakBlokow, LimitKatalogu, Zapis, Flush
};
enum class PsfEtapStartu { NieProbowano, Ok, Blad };
struct PsfStanStartu {
    PsfBladStartu blad=PsfBladStartu::Ok;
    PsfEtapStartu lookup{},tworzenie{},alokacjaWezla{},wpis{},alokacjaBloku{},zapis{},flush{};
    bool gotowy=false,trwaly=false,istnial=false;
    BlockDeviceType backend=BlockDeviceType::Ramdisk;
    BlockError bladBlokowy=BlockError::Ok;
    uint64_t korzen=0,wezel=0,rodzic=0,wpisyRodzica=0;
    uint64_t wolneWezly=0,wolneBloki=0,blokZapisu=0;
    char sciezkaFizyczna[160]{}; // Slot-resolved path, filled only in opt-in diagnostics.
};
inline const char* psf_blad_startu_nazwa(PsfBladStartu b) {
    switch(b) {
    case PsfBladStartu::Ok:return "PSF_OK";
    case PsfBladStartu::NieGotowy:return "PSF_NIEZAMONTOWANY";
    case PsfBladStartu::ZlaSciezka:return "PSF_ZLA_SCIEZKA";
    case PsfBladStartu::ZlyKorzen:return "PSF_USZKODZONY_KORZEN";
    case PsfBladStartu::BrakRodzica:return "PSF_BRAK_RODZICA";
    case PsfBladStartu::NieKatalog:return "PSF_NIE_KATALOG";
    case PsfBladStartu::Odczyt:return "PSF_ODCZYT_NIEUDANY";
    case PsfBladStartu::JuzIstnieje:return "PSF_JUZ_ISTNIEJE";
    case PsfBladStartu::BrakWezlow:return "PSF_BRAK_WEZLOW";
    case PsfBladStartu::BrakBlokow:return "PSF_BRAK_BLOKOW";
    case PsfBladStartu::LimitKatalogu:return "PSF_LIMIT_KATALOGU";
    case PsfBladStartu::Zapis:return "PSF_ZAPIS_NIEUDANY";
    case PsfBladStartu::Flush:return "PSF_FLUSH_NIEUDANY";
    }
    return "PSF_NIEZNANY_BLAD";
}
inline const char* psf_etap_startu_nazwa(PsfEtapStartu e) {
    return e==PsfEtapStartu::Ok?"OK":e==PsfEtapStartu::Blad?"FAIL":"not-attempted";
}
inline const char* psf_backend_startu_nazwa(BlockDeviceType t) {
    switch(t) {
    case BlockDeviceType::AHCI:return "AHCI";
    case BlockDeviceType::VirtIO:return "VirtIO-BLK";
    case BlockDeviceType::NVMe:return "NVMe";
    case BlockDeviceType::Ramdisk:return "RAM";
    default:return "BlockDevice-unknown";
    }
}
